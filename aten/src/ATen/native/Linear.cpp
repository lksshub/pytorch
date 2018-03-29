#include "ATen/ATen.h"
#include "ATen/NativeFunctions.h"


namespace at { namespace native {

Tensor bilinear(const Tensor& input1, const Tensor& input2, const Tensor& weight, const Tensor& bias) {
  AT_ASSERT(input1.dim() == input2.dim(), "bilinear(): input dimensions do not match: got %lld and %lld",
            (long long)input1.dim(), (long long)input2.dim());
  for (int64_t i = 0; i < input1.dim() - 1; i++) {
    AT_ASSERT(input1.size(i) == input2.size(i),
              "bilinear(): input batch dimensions do not match at dim %lld: got %lld and %lld",
              (long long)i, (long long)input1.size(i), (long long)input2.size(i));
  }
  AT_ASSERT(input1.size(input1.dim() - 1) == weight.size(1),
            "bilinear(): input1 size does not match weight size: got %lld but expected %lld",
            (long long)input1.size(input1.dim() - 1), (long long)weight.size(1));
  AT_ASSERT(input2.size(input2.dim() - 1) == weight.size(2),
            "bilinear(): input2 size does not match weight size: got %lld but expected %lld",
            (long long)input2.size(input2.dim() - 1), (long long)weight.size(2));
  AT_ASSERT(!bias.defined() || bias.size(0) == weight.size(0),
            "bilinear(): bias size does not match weight size: got %lld but expected %lld",
            (long long)bias.size(0), (long long)weight.size(0));

  std::vector<int64_t> output_size;
  auto size1 = input1.sizes();
  output_size.insert(output_size.end(), size1.begin(), size1.end() - 1);
  output_size.push_back(weight.size(0));

  auto output = input1.type().tensor(output_size);
  auto buf = input1.type().tensor(input2.sizes());

  size_t output_features = weight.size(0);
  auto input1_flattened = input1.view({-1, input1.size(-1)});
  auto buf_flattened = buf.view({-1, buf.size(-1)});
  for (size_t i = 0; i < output_features; i++) {
    at::mm_out(buf_flattened, input1_flattened, weight[i]);
    buf.mul_(input2);
    auto output_col = output.narrow(-1, i, 1);
    sum_out(output_col, buf, -1, true);
  }
  if (bias.defined()) {
    output = output + bias;
  }
  return output;
}

std::tuple<Tensor, Tensor, Tensor, Tensor> bilinear_backward(const Tensor& grad_out, const Tensor& input1, const Tensor& input2,
							     const Tensor& weight, const Tensor& bias, std::array<bool, 4> grad_mask) {
  Tensor grad_input1, grad_input2, grad_weight, grad_bias;

  size_t output_features = weight.size(0);
  auto input1_flattened = input1.view({-1, input1.size(-1)});
  auto input2_flattened = input2.view({-1, input2.size(-1)});
  auto grad_out_flattened = grad_out.view({-1, grad_out.size(-1)});

  if (grad_mask[0] || grad_mask[1]) {
    grad_input1 = at::mm(input2_flattened, weight[0].t());
    grad_input1.mul_(grad_out_flattened.narrow(1, 0, 1));
    grad_input2 = at::mm(input1_flattened, weight[0]);
    grad_input2.mul_(grad_out_flattened.narrow(1, 0, 1));
    for (size_t k = 1; k < output_features; k++) {
       auto buf = input2_flattened.mm(weight[k].t());
       buf.mul_(grad_out_flattened.narrow(1, k, 1));
       grad_input1 += buf;
       buf = input1_flattened.mm(weight[k]);
       buf.mul_(grad_out_flattened.narrow(1, k, 1));
       grad_input2.add_(buf);
    }
    grad_input1 = grad_input1.view_as(input1);
    grad_input2 = grad_input2.view_as(input2);
  }
  if (grad_mask[2]) {
    grad_weight = weight.type().tensor(weight.sizes());
    for (size_t k = 0; k < output_features; k++) {
      auto buf = input1_flattened.mul(grad_out_flattened.narrow(1, k, 1));
      auto weight_row = grad_weight[k];
      at::mm_out(weight_row, buf.t(), input2_flattened);
    }
  }
  if (grad_mask[3]) {
    grad_bias = grad_out_flattened.sum(0, false);
  }
  return std::tuple<Tensor, Tensor, Tensor, Tensor>(grad_input1, grad_input2, grad_weight, grad_bias);
}

}}  // namespace at::native
