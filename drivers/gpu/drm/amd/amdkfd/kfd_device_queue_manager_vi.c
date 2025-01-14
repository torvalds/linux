// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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
#include "gca/gfx_8_0_enum.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "oss/oss_3_0_sh_mask.h"

/*
 * Low bits must be 0000/FFFF as required by HW, high bits must be 0 to
 * stay in user mode.
 */
#define APE1_FIXED_BITS_MASK 0xFFFF80000000FFFFULL
/* APE1 limit is inclusive and 64K aligned. */
#define APE1_LIMIT_ALIGNMENT 0xFFFF

static bool set_cache_memory_policy_vi(struct device_queue_manager *dqm,
				       struct qcm_process_device *qpd,
				       enum cache_policy default_policy,
				       enum cache_policy alternate_policy,
				       void __user *alternate_aperture_base,
				       uint64_t alternate_aperture_size);
static int update_qpd_vi(struct device_queue_manager *dqm,
			 struct qcm_process_device *qpd);
static void init_sdma_vm(struct device_queue_manager *dqm,
			 struct queue *q,
			 struct qcm_process_device *qpd);

void device_queue_manager_init_vi(
	struct device_queue_manager_asic_ops *asic_ops)
{
	asic_ops->set_cache_memory_policy = set_cache_memory_policy_vi;
	asic_ops->update_qpd = update_qpd_vi;
	asic_ops->init_sdma_vm = init_sdma_vm;
	asic_ops->mqd_manager_init = mqd_manager_init_vi;
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

	WARN_ON((top_address_nybble & 1) || top_address_nybble > 0xE ||
		top_address_nybble == 0);

	return top_address_nybble << 12 |
			(top_address_nybble << 12) <<
			SH_MEM_BASES__SHARED_BASE__SHIFT;
}

static bool set_cache_memory_policy_vi(struct device_queue_manager *dqm,
		struct qcm_process_device *qpd,
		enum cache_policy default_policy,
		enum cache_policy alternate_policy,
		void __user *alternate_aperture_base,
		uint64_t alternate_aperture_size)
{
	uint32_t default_mtype;
	uint32_t ape1_mtype;
	unsigned int temp;
	bool retval = true;

	if (alternate_aperture_size == 0) {
		/* base > limit disables APE1 */
		qpd->sh_mem_ape1_base = 1;
		qpd->sh_mem_ape1_limit = 0;
	} else {
		/*
		 * In FSA64, APE1_Base[63:0] = { 16{SH_MEM_APE1_BASE[31]},
		 *			SH_MEM_APE1_BASE[31:0], 0x0000 }
		 * APE1_Limit[63:0] = { 16{SH_MEM_APE1_LIMIT[31]},
		 *			SH_MEM_APE1_LIMIT[31:0], 0xFFFF }
		 * Verify that the base and size parameters can be
		 * represented in this format and convert them.
		 * Additionally restrict APE1 to user-mode addresses.
		 */

		uint64_t base = (uintptr_t)alternate_aperture_base;
		uint64_t limit = base + alternate_aperture_size - 1;

		if (limit <= base || (base & APE1_FIXED_BITS_MASK) != 0 ||
		   (limit & APE1_FIXED_BITS_MASK) != APE1_LIMIT_ALIGNMENT) {
			retval = false;
			goto out;
		}

		qpd->sh_mem_ape1_base = base >> 16;
		qpd->sh_mem_ape1_limit = limit >> 16;
	}

	default_mtype = (default_policy == cache_policy_coherent) ?
			MTYPE_UC :
			MTYPE_NC;

	ape1_mtype = (alternate_policy == cache_policy_coherent) ?
			MTYPE_UC :
			MTYPE_NC;

	qpd->sh_mem_config =
			SH_MEM_ALIGNMENT_MODE_UNALIGNED <<
				   SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT |
			default_mtype << SH_MEM_CONFIG__DEFAULT_MTYPE__SHIFT |
			ape1_mtype << SH_MEM_CONFIG__APE1_MTYPE__SHIFT;

	/* On dGPU we're always in GPUVM64 addressing mode with 64-bit
	 * aperture addresses.
	 */
	temp = get_sh_mem_bases_nybble_64(qpd_to_pdd(qpd));
	qpd->sh_mem_bases = compute_sh_mem_bases_64bit(temp);

	pr_debug("sh_mem_bases nybble: 0x%X and register 0x%X\n",
		temp, qpd->sh_mem_bases);
out:
	return retval;
}

static int update_qpd_vi(struct device_queue_manager *dqm,
			 struct qcm_process_device *qpd)
{
	return 0;
}

static void init_sdma_vm(struct device_queue_manager *dqm,
			 struct queue *q,
			 struct qcm_process_device *qpd)
{
	/* On dGPU we're always in GPUVM64 addressing mode with 64-bit
	 * aperture addresses.
	 */
	q->properties.sdma_vm_addr =
		((get_sh_mem_bases_nybble_64(qpd_to_pdd(qpd))) <<
		 SDMA0_RLC0_VIRTUAL_ADDR__SHARED_BASE__SHIFT) &
		SDMA0_RLC0_VIRTUAL_ADDR__SHARED_BASE_MASK;
}
