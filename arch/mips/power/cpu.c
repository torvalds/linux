// SPDX-License-Identifier: GPL-2.0-only
/*
 * Suspend support specific for mips.
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Hu Hongbing <huhb@lemote.com>
 *	   Wu Zhangjin <wuzhangjin@gmail.com>
 */
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

int pfn_is_yessave(unsigned long pfn)
{
	unsigned long yessave_begin_pfn = PFN_DOWN(__pa(&__yessave_begin));
	unsigned long yessave_end_pfn = PFN_UP(__pa(&__yessave_end));

	return	(pfn >= yessave_begin_pfn) && (pfn < yessave_end_pfn);
}
