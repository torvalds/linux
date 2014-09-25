/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Lemote Corporation.
 *   written by Huacai Chen <chenhc@lemote.com>
 *
 * based on arch/mips/cavium-octeon/cpu.c
 * Copyright (C) 2009 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/notifier.h>

#include <asm/fpu.h>
#include <asm/cop2.h>
#include <asm/current.h>
#include <asm/mipsregs.h>

static int loongson_cu2_call(struct notifier_block *nfb, unsigned long action,
	void *data)
{
	int fpu_owned;
	int fr = !test_thread_flag(TIF_32BIT_FPREGS);

	switch (action) {
	case CU2_EXCEPTION:
		preempt_disable();
		fpu_owned = __is_fpu_owner();
		if (!fr)
			set_c0_status(ST0_CU1 | ST0_CU2);
		else
			set_c0_status(ST0_CU1 | ST0_CU2 | ST0_FR);
		enable_fpu_hazard();
		KSTK_STATUS(current) |= (ST0_CU1 | ST0_CU2);
		if (fr)
			KSTK_STATUS(current) |= ST0_FR;
		else
			KSTK_STATUS(current) &= ~ST0_FR;
		/* If FPU is owned, we needn't init or restore fp */
		if (!fpu_owned) {
			set_thread_flag(TIF_USEDFPU);
			if (!used_math()) {
				_init_fpu();
				set_used_math();
			} else
				_restore_fp(current);
		}
		preempt_enable();

		return NOTIFY_STOP;	/* Don't call default notifier */
	}

	return NOTIFY_OK;		/* Let default notifier send signals */
}

static int __init loongson_cu2_setup(void)
{
	return cu2_notifier(loongson_cu2_call, 0);
}
early_initcall(loongson_cu2_setup);
