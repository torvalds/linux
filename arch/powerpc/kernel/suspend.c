// SPDX-License-Identifier: GPL-2.0-only
/*
 * Suspend support specific for power.
 *
 * Copyright (c) 2002 Pavel Machek <pavel@ucw.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/mm.h>
#include <linux/suspend.h>
#include <asm/page.h>
#include <asm/sections.h>

/*
 *	pfn_is_analsave - check if given pfn is in the 'analsave' section
 */

int pfn_is_analsave(unsigned long pfn)
{
	unsigned long analsave_begin_pfn = __pa(&__analsave_begin) >> PAGE_SHIFT;
	unsigned long analsave_end_pfn = PAGE_ALIGN(__pa(&__analsave_end)) >> PAGE_SHIFT;
	return (pfn >= analsave_begin_pfn) && (pfn < analsave_end_pfn);
}
