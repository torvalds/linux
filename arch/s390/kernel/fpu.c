// SPDX-License-Identifier: GPL-2.0
/*
 * In-kernel vector facility support functions
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <asm/fpu.h>

void __kernel_fpu_begin(struct kernel_fpu *state, int flags)
{
	__vector128 *vxrs = state->vxrs;
	int mask;

	/*
	 * Limit the save to the FPU/vector registers already
	 * in use by the previous context.
	 */
	flags &= state->hdr.mask;
	if (flags & KERNEL_FPC)
		fpu_stfpc(&state->hdr.fpc);
	if (!cpu_has_vx()) {
		if (flags & KERNEL_VXR_LOW)
			save_fp_regs_vx(vxrs);
		return;
	}
	mask = flags & KERNEL_VXR;
	if (mask == KERNEL_VXR) {
		vxrs += fpu_vstm(0, 15, vxrs);
		vxrs += fpu_vstm(16, 31, vxrs);
		return;
	}
	if (mask == KERNEL_VXR_MID) {
		vxrs += fpu_vstm(8, 23, vxrs);
		return;
	}
	mask = flags & KERNEL_VXR_LOW;
	if (mask) {
		if (mask == KERNEL_VXR_LOW)
			vxrs += fpu_vstm(0, 15, vxrs);
		else if (mask == KERNEL_VXR_V0V7)
			vxrs += fpu_vstm(0, 7, vxrs);
		else
			vxrs += fpu_vstm(8, 15, vxrs);
	}
	mask = flags & KERNEL_VXR_HIGH;
	if (mask) {
		if (mask == KERNEL_VXR_HIGH)
			vxrs += fpu_vstm(16, 31, vxrs);
		else if (mask == KERNEL_VXR_V16V23)
			vxrs += fpu_vstm(16, 23, vxrs);
		else
			vxrs += fpu_vstm(24, 31, vxrs);
	}
}
EXPORT_SYMBOL(__kernel_fpu_begin);

void __kernel_fpu_end(struct kernel_fpu *state, int flags)
{
	__vector128 *vxrs = state->vxrs;
	int mask;

	/*
	 * Limit the restore to the FPU/vector registers of the
	 * previous context that have been overwritten by the
	 * current context.
	 */
	flags &= state->hdr.mask;
	if (flags & KERNEL_FPC)
		fpu_lfpc(&state->hdr.fpc);
	if (!cpu_has_vx()) {
		if (flags & KERNEL_VXR_LOW)
			load_fp_regs_vx(vxrs);
		return;
	}
	mask = flags & KERNEL_VXR;
	if (mask == KERNEL_VXR) {
		vxrs += fpu_vlm(0, 15, vxrs);
		vxrs += fpu_vlm(16, 31, vxrs);
		return;
	}
	if (mask == KERNEL_VXR_MID) {
		vxrs += fpu_vlm(8, 23, vxrs);
		return;
	}
	mask = flags & KERNEL_VXR_LOW;
	if (mask) {
		if (mask == KERNEL_VXR_LOW)
			vxrs += fpu_vlm(0, 15, vxrs);
		else if (mask == KERNEL_VXR_V0V7)
			vxrs += fpu_vlm(0, 7, vxrs);
		else
			vxrs += fpu_vlm(8, 15, vxrs);
	}
	mask = flags & KERNEL_VXR_HIGH;
	if (mask) {
		if (mask == KERNEL_VXR_HIGH)
			vxrs += fpu_vlm(16, 31, vxrs);
		else if (mask == KERNEL_VXR_V16V23)
			vxrs += fpu_vlm(16, 23, vxrs);
		else
			vxrs += fpu_vlm(24, 31, vxrs);
	}
}
EXPORT_SYMBOL(__kernel_fpu_end);

void load_fpu_state(struct fpu *state, int flags)
{
	__vector128 *vxrs = &state->vxrs[0];
	int mask;

	if (flags & KERNEL_FPC)
		fpu_lfpc(&state->fpc);
	if (!cpu_has_vx()) {
		if (flags & KERNEL_VXR_V0V7)
			load_fp_regs_vx(state->vxrs);
		return;
	}
	mask = flags & KERNEL_VXR;
	if (mask == KERNEL_VXR) {
		fpu_vlm(0, 15, &vxrs[0]);
		fpu_vlm(16, 31, &vxrs[16]);
		return;
	}
	if (mask == KERNEL_VXR_MID) {
		fpu_vlm(8, 23, &vxrs[8]);
		return;
	}
	mask = flags & KERNEL_VXR_LOW;
	if (mask) {
		if (mask == KERNEL_VXR_LOW)
			fpu_vlm(0, 15, &vxrs[0]);
		else if (mask == KERNEL_VXR_V0V7)
			fpu_vlm(0, 7, &vxrs[0]);
		else
			fpu_vlm(8, 15, &vxrs[8]);
	}
	mask = flags & KERNEL_VXR_HIGH;
	if (mask) {
		if (mask == KERNEL_VXR_HIGH)
			fpu_vlm(16, 31, &vxrs[16]);
		else if (mask == KERNEL_VXR_V16V23)
			fpu_vlm(16, 23, &vxrs[16]);
		else
			fpu_vlm(24, 31, &vxrs[24]);
	}
}

void save_fpu_state(struct fpu *state, int flags)
{
	__vector128 *vxrs = &state->vxrs[0];
	int mask;

	if (flags & KERNEL_FPC)
		fpu_stfpc(&state->fpc);
	if (!cpu_has_vx()) {
		if (flags & KERNEL_VXR_LOW)
			save_fp_regs_vx(state->vxrs);
		return;
	}
	mask = flags & KERNEL_VXR;
	if (mask == KERNEL_VXR) {
		fpu_vstm(0, 15, &vxrs[0]);
		fpu_vstm(16, 31, &vxrs[16]);
		return;
	}
	if (mask == KERNEL_VXR_MID) {
		fpu_vstm(8, 23, &vxrs[8]);
		return;
	}
	mask = flags & KERNEL_VXR_LOW;
	if (mask) {
		if (mask == KERNEL_VXR_LOW)
			fpu_vstm(0, 15, &vxrs[0]);
		else if (mask == KERNEL_VXR_V0V7)
			fpu_vstm(0, 7, &vxrs[0]);
		else
			fpu_vstm(8, 15, &vxrs[8]);
	}
	mask = flags & KERNEL_VXR_HIGH;
	if (mask) {
		if (mask == KERNEL_VXR_HIGH)
			fpu_vstm(16, 31, &vxrs[16]);
		else if (mask == KERNEL_VXR_V16V23)
			fpu_vstm(16, 23, &vxrs[16]);
		else
			fpu_vstm(24, 31, &vxrs[24]);
	}
}
EXPORT_SYMBOL(save_fpu_state);
