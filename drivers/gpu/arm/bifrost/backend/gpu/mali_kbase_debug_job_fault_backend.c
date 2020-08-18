/*
 *
 * (C) COPYRIGHT 2012-2015,2018-2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include "mali_kbase_debug_job_fault.h"

#ifdef CONFIG_DEBUG_FS

/*GPU_CONTROL_REG(r)*/
static int gpu_control_reg_snapshot[] = {
	GPU_ID,
	SHADER_READY_LO,
	SHADER_READY_HI,
	TILER_READY_LO,
	TILER_READY_HI,
	L2_READY_LO,
	L2_READY_HI
};

/* JOB_CONTROL_REG(r) */
static int job_control_reg_snapshot[] = {
	JOB_IRQ_MASK,
	JOB_IRQ_STATUS
};

/* JOB_SLOT_REG(n,r) */
static int job_slot_reg_snapshot[] = {
	JS_HEAD_LO,
	JS_HEAD_HI,
	JS_TAIL_LO,
	JS_TAIL_HI,
	JS_AFFINITY_LO,
	JS_AFFINITY_HI,
	JS_CONFIG,
	JS_STATUS,
	JS_HEAD_NEXT_LO,
	JS_HEAD_NEXT_HI,
	JS_AFFINITY_NEXT_LO,
	JS_AFFINITY_NEXT_HI,
	JS_CONFIG_NEXT
};

/*MMU_REG(r)*/
static int mmu_reg_snapshot[] = {
	MMU_IRQ_MASK,
	MMU_IRQ_STATUS
};

/* MMU_AS_REG(n,r) */
static int as_reg_snapshot[] = {
	AS_TRANSTAB_LO,
	AS_TRANSTAB_HI,
	AS_TRANSCFG_LO,
	AS_TRANSCFG_HI,
	AS_MEMATTR_LO,
	AS_MEMATTR_HI,
	AS_FAULTSTATUS,
	AS_FAULTADDRESS_LO,
	AS_FAULTADDRESS_HI,
	AS_STATUS
};

bool kbase_debug_job_fault_reg_snapshot_init(struct kbase_context *kctx,
		int reg_range)
{
	int i, j;
	int offset = 0;
	int slot_number;
	int as_number;

	if (kctx->reg_dump == NULL)
		return false;

	slot_number = kctx->kbdev->gpu_props.num_job_slots;
	as_number = kctx->kbdev->gpu_props.num_address_spaces;

	/* get the GPU control registers*/
	for (i = 0; i < sizeof(gpu_control_reg_snapshot)/4; i++) {
		kctx->reg_dump[offset] =
				GPU_CONTROL_REG(gpu_control_reg_snapshot[i]);
		offset += 2;
	}

	/* get the Job control registers*/
	for (i = 0; i < sizeof(job_control_reg_snapshot)/4; i++) {
		kctx->reg_dump[offset] =
				JOB_CONTROL_REG(job_control_reg_snapshot[i]);
		offset += 2;
	}

	/* get the Job Slot registers*/
	for (j = 0; j < slot_number; j++)	{
		for (i = 0; i < sizeof(job_slot_reg_snapshot)/4; i++) {
			kctx->reg_dump[offset] =
			JOB_SLOT_REG(j, job_slot_reg_snapshot[i]);
			offset += 2;
		}
	}

	/* get the MMU registers*/
	for (i = 0; i < sizeof(mmu_reg_snapshot)/4; i++) {
		kctx->reg_dump[offset] = MMU_REG(mmu_reg_snapshot[i]);
		offset += 2;
	}

	/* get the Address space registers*/
	for (j = 0; j < as_number; j++) {
		for (i = 0; i < sizeof(as_reg_snapshot)/4; i++) {
			kctx->reg_dump[offset] =
					MMU_AS_REG(j, as_reg_snapshot[i]);
			offset += 2;
		}
	}

	WARN_ON(offset >= (reg_range*2/4));

	/* set the termination flag*/
	kctx->reg_dump[offset] = REGISTER_DUMP_TERMINATION_FLAG;
	kctx->reg_dump[offset + 1] = REGISTER_DUMP_TERMINATION_FLAG;

	dev_dbg(kctx->kbdev->dev, "kbase_job_fault_reg_snapshot_init:%d\n",
			offset);

	return true;
}

bool kbase_job_fault_get_reg_snapshot(struct kbase_context *kctx)
{
	int offset = 0;

	if (kctx->reg_dump == NULL)
		return false;

	while (kctx->reg_dump[offset] != REGISTER_DUMP_TERMINATION_FLAG) {
		kctx->reg_dump[offset+1] =
				kbase_reg_read(kctx->kbdev,
						kctx->reg_dump[offset]);
		offset += 2;
	}
	return true;
}


#endif
