#include "Kernel.hpp"
#include "Context.hpp"

#include <iostream>
#include <cstdio>

namespace opencl {

void Kernel::init(Context *ctx, cl_kernel k, cl_program p,
                  size_t max_work_group_size) {
  if (initialized) cleanup();
  this->context = ctx;
  this->kernel_id = k;
  this->program_id = p;
  this->max_work_group_size = max_work_group_size;
  arg_stack_size = 0;
  initialized = true;
}

void Kernel::cleanup() {
  if (!initialized) return;
  initialized = false;

  if (kernel_id) clReleaseKernel(kernel_id);
  if (program_id) clReleaseProgram(program_id);
}

void Kernel::push_arg(size_t arg_size, const void *arg_value) {
  cl_int ciErr1 =
      clSetKernelArg(kernel_id, arg_stack_size, arg_size, arg_value);
  context->check_error(ciErr1, "Could not push kernel argument");
  ++arg_stack_size;
}

void Kernel::push_arg(const MemoryHandler *gpu_buf) {
  this->push_arg(sizeof(cl_mem), (void *)&gpu_buf->handle);
}

cl_event Kernel::execute(cl_uint work_dim,                //
                         const size_t *global_work_size,  //
                         const size_t *local_work_size,   //
                         cl_event *events_to_wait_for,
                         int events_to_wait_for_count) {
  context->check_error(context->was_initialized(),
                       "Context was not initialized");
  check_work_parameters(work_dim, global_work_size, local_work_size);

  // correct event parameters
  if (!events_to_wait_for) events_to_wait_for_count = 0;
  if (events_to_wait_for_count <= 0) events_to_wait_for = nullptr;

  arg_stack_size = 0;  // prepare for next invoke
  cl_command_queue *cmd_queue = context->command_queue();

  cl_event finish_token;
  cl_int ciErr1 = clEnqueueNDRangeKernel(
      *cmd_queue, kernel_id,              // what and where to execute
      work_dim, nullptr,                  // must be NULL
      global_work_size, local_work_size,  //
      events_to_wait_for_count, events_to_wait_for,  // sync events
      &finish_token);
  context->check_error(ciErr1, "Error in clEnqueueNDRangeKernel");
  return finish_token;
}

void Kernel::check_work_parameters(cl_uint work_dim,  //
                                   const size_t *global_work_size,
                                   const size_t *local_work_size) {
  // std::cout << std::endl
  // << "Work size: " << ((unsigned int)work_dim)
  // << "/" << (*global_work_size)
  // << "/" << (*local_work_size) << std::endl;

  char msg_buffer[192];
  if (work_dim < 1 || work_dim > 3) {
    snprintf(msg_buffer, sizeof(msg_buffer),
             "Work parameters: 1 <= (work_dim=%d) <= 3", work_dim);
    context->check_error(false, msg_buffer);
  }

  auto device = context->device();
  long long device_work_id_range = ((long long)1) << device.address_bits;
  long long real_global_work_size = 1,
            real_local_work_size = 1;  // # of work-items in work-group
  bool local_dims_lte_device_max = true,
       global_dims_divisible_by_local_dims = true;

  for (size_t i = 0; i < work_dim; i++) {
    real_global_work_size *= global_work_size[i];
    if (local_work_size) {
      real_local_work_size *= local_work_size[i];
      local_dims_lte_device_max &=
          local_work_size[i] <= device.work_items_for_dims[i];
      global_dims_divisible_by_local_dims &=
          global_work_size[i] % local_work_size[i] == 0;
    }
  }

  if (!local_dims_lte_device_max) {
    context->check_error(false,
                         "Work parameters: one of local dimensions are bigger "
                         "then device allows");
  }

  if (!global_dims_divisible_by_local_dims) {
    context->check_error(false,
                         "Work parameters: For each dimension "
                         "global_work_size[dim] should be multiply of "
                         "local_work_size[dim]");
  }

  if (real_global_work_size > device_work_id_range) {
    snprintf(msg_buffer, sizeof(msg_buffer),
             "Work parameters: global_work_size(%llu) is bigger then device "
             "address_bits(%d) can represent",
             real_global_work_size, device.address_bits);
    context->check_error(false, msg_buffer);
  }

  if (real_local_work_size > device.max_work_group_size ||
      real_local_work_size > this->max_work_group_size) {
    snprintf(msg_buffer, sizeof(msg_buffer),
             "Work parameters: local_work_size(%llu) is bigger then device(%d) "
             "or kernel(%d) allows",
             real_local_work_size, device.max_work_group_size,
             this->max_work_group_size);
    context->check_error(false, msg_buffer);
  }
}
}
