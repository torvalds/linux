// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "gc/gc_12_0_0_sh_mask.h"
#include "soc24_enum.h"

static int update_qpd_v12(struct device_queue_manager *dqm,
			 struct qcm_process_device *qpd);
static void init_sdma_vm_v12(struct device_queue_manager *dqm, struct queue *q,
			    struct qcm_process_device *qpd);
static bool set_cache_memory_policy_v12(struct device_queue_manager *dqm,
				   struct qcm_process_device *qpd,
				   enum cache_policy default_policy,
				   enum cache_policy alternate_policy,
				   void __user *alternate_aperture_base,
				   uint64_t alternate_aperture_size);

void device_queue_manager_init_v12(
	struct device_queue_manager_asic_ops *asic_ops)
{
	asic_ops->set_cache_memory_policy = set_cache_memory_policy_v12;
	asic_ops->update_qpd = update_qpd_v12;
	asic_ops->init_sdma_vm = init_sdma_vm_v12;
	asic_ops->mqd_manager_init = mqd_manager_init_v12;
}

static uint32_t compute_sh_mem_bases_64bit(struct kfd_process_device *pdd)
{
	uint32_t shared_base = pdd->lds_base >> 48;
	uint32_t private_base = pdd->scratch_base >> 48;

	return (shared_base << SH_MEM_BASES__SHARED_BASE__SHIFT) |
		private_base;
}

static bool set_cache_memory_policy_v12(struct device_queue_manager *dqm,
				   struct qcm_process_device *qpd,
				   enum cache_policy default_policy,
				   enum cache_policy alternate_policy,
				   void __user *alternate_aperture_base,
				   uint64_t alternate_aperture_size)
{
	qpd->sh_mem_config = (SH_MEM_ALIGNMENT_MODE_UNALIGNED <<
			      SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT) |
			      (3 << SH_MEM_CONFIG__INITIAL_INST_PREFETCH__SHIFT);

	qpd->sh_mem_ape1_limit = 0;
	qpd->sh_mem_ape1_base = 0;
	qpd->sh_mem_bases = compute_sh_mem_bases_64bit(qpd_to_pdd(qpd));

	pr_debug("sh_mem_bases 0x%X\n", qpd->sh_mem_bases);
	return true;
}

static int update_qpd_v12(struct device_queue_manager *dqm,
			 struct qcm_process_device *qpd)
{
	return 0;
}

static void init_sdma_vm_v12(struct device_queue_manager *dqm, struct queue *q,
			    struct qcm_process_device *qpd)
{
	/* Not needed on SDMAv4 onwards any more */
	q->properties.sdma_vm_addr = 0;
}
