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

void __kernel_fpu_begin(struct kernel_fpu *state, u32 flags)
{
	__vector128 *vxrs = state->vxrs;
	u32 mask;

	/*
	 * Limit the save to the FPU/vector registers already
	 * in use by the previous context.
	 */
	flags &= state->mask;
	if (flags & KERNEL_FPC)
		fpu_stfpc(&state->fpc);
	if (!cpu_has_vx()) {
		if (flags & KERNEL_VXR_LOW)
			save_fp_regs(state->fprs);
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
EXPORT_SYMBOL(__kernel_fpu_begin);

void __kernel_fpu_end(struct kernel_fpu *state, u32 flags)
{
	__vector128 *vxrs = state->vxrs;
	u32 mask;

	/*
	 * Limit the restore to the FPU/vector registers of the
	 * previous context that have been overwritten by the
	 * current context.
	 */
	flags &= state->mask;
	if (flags & KERNEL_FPC)
		fpu_lfpc(&state->fpc);
	if (!cpu_has_vx()) {
		if (flags & KERNEL_VXR_LOW)
			load_fp_regs(state->fprs);
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
EXPORT_SYMBOL(__kernel_fpu_end);

void __load_fpu_regs(void)
{
	struct fpu *state = &current->thread.fpu;
	void *regs = current->thread.fpu.regs;

	fpu_lfpc_safe(&state->fpc);
	if (likely(cpu_has_vx()))
		load_vx_regs(regs);
	else
		load_fp_regs(regs);
	clear_cpu_flag(CIF_FPU);
}

void load_fpu_regs(void)
{
	raw_local_irq_disable();
	__load_fpu_regs();
	raw_local_irq_enable();
}
EXPORT_SYMBOL(load_fpu_regs);

void save_fpu_regs(void)
{
	unsigned long flags;
	struct fpu *state;
	void *regs;

	local_irq_save(flags);

	if (test_cpu_flag(CIF_FPU))
		goto out;

	state = &current->thread.fpu;
	regs = current->thread.fpu.regs;

	fpu_stfpc(&state->fpc);
	if (likely(cpu_has_vx()))
		save_vx_regs(regs);
	else
		save_fp_regs(regs);
	set_cpu_flag(CIF_FPU);
out:
	local_irq_restore(flags);
}
EXPORT_SYMBOL(save_fpu_regs);
