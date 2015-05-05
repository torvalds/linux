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
#include "cik_regs.h"

static bool set_cache_memory_policy_cik(struct device_queue_manager *dqm,
				   struct qcm_process_device *qpd,
				   enum cache_policy default_policy,
				   enum cache_policy alternate_policy,
				   void __user *alternate_aperture_base,
				   uint64_t alternate_aperture_size);
static int register_process_cik(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);
static int initialize_cpsch_cik(struct device_queue_manager *dqm);
static void init_sdma_vm(struct device_queue_manager *dqm, struct queue *q,
				struct qcm_process_device *qpd);

void device_queue_manager_init_cik(struct device_queue_manager_asic_ops *ops)
{
	ops->set_cache_memory_policy = set_cache_memory_policy_cik;
	ops->register_process = register_process_cik;
	ops->initialize = initialize_cpsch_cik;
	ops->init_sdma_vm = init_sdma_vm;
}

static uint32_t compute_sh_mem_bases_64bit(unsigned int top_address_nybble)
{
	/* In 64-bit mode, we can only control the top 3 bits of the LDS,
	 * scratch and GPUVM apertures.
	 * The hardware fills in the remaining 59 bits according to the
	 * following pattern:
	 * LDS:		X0000000'00000000 - X0000001'00000000 (4GB)
	 * Scratch:	X0000001'00000000 - X0000002'00000000 (4GB)
	 * GPUVM:	Y0010000'00000000 - Y0020000'00000000 (1TB)
	 *
	 * (where X/Y is the configurable nybble with the low-bit 0)
	 *
	 * LDS and scratch will have the same top nybble programmed in the
	 * top 3 bits of SH_MEM_BASES.PRIVATE_BASE.
	 * GPUVM can have a different top nybble programmed in the
	 * top 3 bits of SH_MEM_BASES.SHARED_BASE.
	 * We don't bother to support different top nybbles
	 * for LDS/Scratch and GPUVM.
	 */

	BUG_ON((top_address_nybble & 1) || top_address_nybble > 0xE ||
		top_address_nybble == 0);

	return PRIVATE_BASE(top_address_nybble << 12) |
			SHARED_BASE(top_address_nybble << 12);
}

static bool set_cache_memory_policy_cik(struct device_queue_manager *dqm,
				   struct qcm_process_device *qpd,
				   enum cache_policy default_policy,
				   enum cache_policy alternate_policy,
				   void __user *alternate_aperture_base,
				   uint64_t alternate_aperture_size)
{
	uint32_t default_mtype;
	uint32_t ape1_mtype;

	default_mtype = (default_policy == cache_policy_coherent) ?
			MTYPE_NONCACHED :
			MTYPE_CACHED;

	ape1_mtype = (alternate_policy == cache_policy_coherent) ?
			MTYPE_NONCACHED :
			MTYPE_CACHED;

	qpd->sh_mem_config = (qpd->sh_mem_config & PTR32)
			| ALIGNMENT_MODE(SH_MEM_ALIGNMENT_MODE_UNALIGNED)
			| DEFAULT_MTYPE(default_mtype)
			| APE1_MTYPE(ape1_mtype);

	return true;
}

static int register_process_cik(struct device_queue_manager *dqm,
		struct qcm_process_device *qpd)
{
	struct kfd_process_device *pdd;
	unsigned int temp;

	BUG_ON(!dqm || !qpd);

	pdd = qpd_to_pdd(qpd);

	/* check if sh_mem_config register already configured */
	if (qpd->sh_mem_config == 0) {
		qpd->sh_mem_config =
			ALIGNMENT_MODE(SH_MEM_ALIGNMENT_MODE_UNALIGNED) |
			DEFAULT_MTYPE(MTYPE_NONCACHED) |
			APE1_MTYPE(MTYPE_NONCACHED);
		qpd->sh_mem_ape1_limit = 0;
		qpd->sh_mem_ape1_base = 0;
	}

	if (qpd->pqm->process->is_32bit_user_mode) {
		temp = get_sh_mem_bases_32(pdd);
		qpd->sh_mem_bases = SHARED_BASE(temp);
		qpd->sh_mem_config |= PTR32;
	} else {
		temp = get_sh_mem_bases_nybble_64(pdd);
		qpd->sh_mem_bases = compute_sh_mem_bases_64bit(temp);
	}

	pr_debug("kfd: is32bit process: %d sh_mem_bases nybble: 0x%X and register 0x%X\n",
		qpd->pqm->process->is_32bit_user_mode, temp, qpd->sh_mem_bases);

	return 0;
}

static void init_sdma_vm(struct device_queue_manager *dqm, struct queue *q,
				struct qcm_process_device *qpd)
{
	uint32_t value = SDMA_ATC;

	if (q->process->is_32bit_user_mode)
		value |= SDMA_VA_PTR32 | get_sh_mem_bases_32(qpd_to_pdd(qpd));
	else
		value |= SDMA_VA_SHARED_BASE(get_sh_mem_bases_nybble_64(
							qpd_to_pdd(qpd)));
	q->properties.sdma_vm_addr = value;
}

static int initialize_cpsch_cik(struct device_queue_manager *dqm)
{
	return init_pipelines(dqm, get_pipes_num(dqm), get_first_pipe(dqm));
}
