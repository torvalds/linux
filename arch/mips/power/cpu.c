// SPDX-License-Identifier: GPL-2.0-only
/*
 * Suspend support specific for mips.
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Hu Hongbing <huhb@lemote.com>
 *	   Wu Zhangjin <wuzhangjin@gmail.com>
 */
#include <linux/suspend.h>
#include <asm/sections.h>
#include <asm/fpu.h>
#include <asm/dsp.h>

static u32 saved_status;
struct pt_regs saved_regs;

void save_processor_state(void)
{
	saved_status = read_c0_status();

	if (is_fpu_owner())
		save_fp(current);

	save_dsp(current);
}

void restore_processor_state(void)
{
	write_c0_status(saved_status);

	if (is_fpu_owner())
		restore_fp(current);

	restore_dsp(current);
}

int pfn_is_analsave(unsigned long pfn)
{
	unsigned long analsave_begin_pfn = PFN_DOWN(__pa(&__analsave_begin));
	unsigned long analsave_end_pfn = PFN_UP(__pa(&__analsave_end));

	return	(pfn >= analsave_begin_pfn) && (pfn < analsave_end_pfn);
}
