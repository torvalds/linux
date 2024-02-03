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

void __load_user_fpu_regs(void)
{
	struct fpu *state = &current->thread.ufpu;

	fpu_lfpc_safe(&state->fpc);
	if (likely(cpu_has_vx()))
		load_vx_regs(state->vxrs);
	else
		load_fp_regs_vx(state->vxrs);
	clear_thread_flag(TIF_FPU);
}

void load_user_fpu_regs(void)
{
	raw_local_irq_disable();
	__load_user_fpu_regs();
	raw_local_irq_enable();
}
EXPORT_SYMBOL(load_user_fpu_regs);

void save_user_fpu_regs(void)
{
	unsigned long flags;
	struct fpu *state;

	local_irq_save(flags);

	if (test_thread_flag(TIF_FPU))
		goto out;

	state = &current->thread.ufpu;

	fpu_stfpc(&state->fpc);
	if (likely(cpu_has_vx()))
		save_vx_regs(state->vxrs);
	else
		save_fp_regs_vx(state->vxrs);
	set_thread_flag(TIF_FPU);
out:
	local_irq_restore(flags);
}
EXPORT_SYMBOL(save_user_fpu_regs);
