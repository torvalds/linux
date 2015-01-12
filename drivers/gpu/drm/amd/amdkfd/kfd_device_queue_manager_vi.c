/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "kfd_device_queue_manager.h"

static bool set_cache_memory_policy_vi(struct device_queue_manager *dqm,
				   struct qcm_process_device *qpd,
				   enum cache_policy default_policy,
				   enum cache_policy alternate_policy,
				   void __user *alternate_aperture_base,
				   uint64_t alternate_aperture_size);
static int register_process_vi(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);
static int initialize_cpsch_vi(struct device_queue_manager *dqm);

void device_queue_manager_init_vi(struct device_queue_manager_ops *ops)
{
	pr_warn("amdkfd: VI DQM is not currently supported\n");

	ops->set_cache_memory_policy = set_cache_memory_policy_vi;
	ops->register_process = register_process_vi;
	ops->initialize = initialize_cpsch_vi;
}

static bool set_cache_memory_policy_vi(struct device_queue_manager *dqm,
				   struct qcm_process_device *qpd,
				   enum cache_policy default_policy,
				   enum cache_policy alternate_policy,
				   void __user *alternate_aperture_base,
				   uint64_t alternate_aperture_size)
{
	return false;
}

static int register_process_vi(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd)
{
	return -1;
}

static int initialize_cpsch_vi(struct device_queue_manager *dqm)
{
	return 0;
}
