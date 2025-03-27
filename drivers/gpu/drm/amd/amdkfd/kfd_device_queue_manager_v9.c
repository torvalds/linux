// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2016-2022 Advanced Micro Devices, Inc.
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
#include "vega10_enum.h"
#include "gc/gc_9_4_3_sh_mask.h"

static int update_qpd_v9(struct device_queue_manager *dqm,
			 struct qcm_process_device *qpd);
static void init_sdma_vm_v9(struct device_queue_manager *dqm, struct queue *q,
			    struct qcm_process_device *qpd);

void device_queue_manager_init_v9(
	struct device_queue_manager_asic_ops *asic_ops)
{
	asic_ops->update_qpd = update_qpd_v9;
	asic_ops->init_sdma_vm = init_sdma_vm_v9;
	asic_ops->mqd_manager_init = mqd_manager_init_v9;
}

static uint32_t compute_sh_mem_bases_64bit(struct kfd_process_device *pdd)
{
	uint32_t shared_base = pdd->lds_base >> 48;
	uint32_t private_base = pdd->scratch_base >> 48;

	return (shared_base << SH_MEM_BASES__SHARED_BASE__SHIFT) |
		private_base;
}

static int update_qpd_v9(struct device_queue_manager *dqm,
			 struct qcm_process_device *qpd)
{
	struct kfd_process_device *pdd;

	pdd = qpd_to_pdd(qpd);

	/* check if sh_mem_config register already configured */
	if (qpd->sh_mem_config == 0) {
		qpd->sh_mem_config = SH_MEM_ALIGNMENT_MODE_UNALIGNED <<
					SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT;

		if (dqm->dev->kfd->noretry)
			qpd->sh_mem_config |= 1 << SH_MEM_CONFIG__RETRY_DISABLE__SHIFT;

		if (KFD_GC_VERSION(dqm->dev->kfd) == IP_VERSION(9, 4, 3) ||
		    KFD_GC_VERSION(dqm->dev->kfd) == IP_VERSION(9, 4, 4) ||
		    KFD_GC_VERSION(dqm->dev->kfd) == IP_VERSION(9, 5, 0))
			qpd->sh_mem_config |=
				(1 << SH_MEM_CONFIG__F8_MODE__SHIFT);

		qpd->sh_mem_ape1_limit = 0;
		qpd->sh_mem_ape1_base = 0;
	}

	if (KFD_SUPPORT_XNACK_PER_PROCESS(dqm->dev)) {
		if (!pdd->process->xnack_enabled)
			qpd->sh_mem_config |= 1 << SH_MEM_CONFIG__RETRY_DISABLE__SHIFT;
		else
			qpd->sh_mem_config &= ~(1 << SH_MEM_CONFIG__RETRY_DISABLE__SHIFT);
	}

	qpd->sh_mem_bases = compute_sh_mem_bases_64bit(pdd);

	pr_debug("sh_mem_bases 0x%X sh_mem_config 0x%X\n", qpd->sh_mem_bases,
		 qpd->sh_mem_config);

	return 0;
}

static void init_sdma_vm_v9(struct device_queue_manager *dqm, struct queue *q,
			    struct qcm_process_device *qpd)
{
	/* Not needed on SDMAv4 any more */
	q->properties.sdma_vm_addr = 0;
}
