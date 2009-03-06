/*
 * swsusp.c - SuperH hibernation support
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/fpu.h>

struct swsusp_arch_regs swsusp_arch_regs_cpu0;

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long end_pfn = PAGE_ALIGN(__pa(&__nosave_end)) >> PAGE_SHIFT;

	return (pfn >= begin_pfn) && (pfn < end_pfn);
}

void save_processor_state(void)
{
	init_fpu(current);
}

void restore_processor_state(void)
{
	local_flush_tlb_all();
}
