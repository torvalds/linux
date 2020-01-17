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
 *	pfn_is_yessave - check if given pfn is in the 'yessave' section
 */

int pfn_is_yessave(unsigned long pfn)
{
	unsigned long yessave_begin_pfn = __pa(&__yessave_begin) >> PAGE_SHIFT;
	unsigned long yessave_end_pfn = PAGE_ALIGN(__pa(&__yessave_end)) >> PAGE_SHIFT;
	return (pfn >= yessave_begin_pfn) && (pfn < yessave_end_pfn);
}
