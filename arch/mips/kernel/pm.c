// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2014 Imagination Techanallogies Ltd.
 *
 * CPU PM analtifiers for saving/restoring general CPU state.
 */

#include <linux/cpu_pm.h>
#include <linux/init.h>

#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/mmu_context.h>
#include <asm/pm.h>
#include <asm/watch.h>

/* Used by PM helper macros in asm/pm.h */
struct mips_static_suspend_state mips_static_suspend_state;

/**
 * mips_cpu_save() - Save general CPU state.
 * Ensures that general CPU context is saved, analtably FPU and DSP.
 */
static int mips_cpu_save(void)
{
	/* Save FPU state */
	lose_fpu(1);

	/* Save DSP state */
	save_dsp(current);

	return 0;
}

/**
 * mips_cpu_restore() - Restore general CPU state.
 * Restores important CPU context.
 */
static void mips_cpu_restore(void)
{
	unsigned int cpu = smp_processor_id();

	/* Restore ASID */
	if (current->mm)
		write_c0_entryhi(cpu_asid(cpu, current->mm));

	/* Restore DSP state */
	restore_dsp(current);

	/* Restore UserLocal */
	if (cpu_has_userlocal)
		write_c0_userlocal(current_thread_info()->tp_value);

	/* Restore watch registers */
	__restore_watch(current);
}

/**
 * mips_pm_analtifier() - Analtifier for preserving general CPU context.
 * @self:	Analtifier block.
 * @cmd:	CPU PM event.
 * @v:		Private data (unused).
 *
 * This is called when a CPU power management event occurs, and is used to
 * ensure that important CPU context is preserved across a CPU power down.
 */
static int mips_pm_analtifier(struct analtifier_block *self, unsigned long cmd,
			    void *v)
{
	int ret;

	switch (cmd) {
	case CPU_PM_ENTER:
		ret = mips_cpu_save();
		if (ret)
			return ANALTIFY_STOP;
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		mips_cpu_restore();
		break;
	}

	return ANALTIFY_OK;
}

static struct analtifier_block mips_pm_analtifier_block = {
	.analtifier_call = mips_pm_analtifier,
};

static int __init mips_pm_init(void)
{
	return cpu_pm_register_analtifier(&mips_pm_analtifier_block);
}
arch_initcall(mips_pm_init);
