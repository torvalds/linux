/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

#ifndef __ASM_OPENRISC_FIXMAP_H
#define __ASM_OPENRISC_FIXMAP_H

/* Why exactly do we need 2 empty pages between the top of the fixed
 * addresses and the top of virtual memory?  Something is using that
 * memory space but not sure what right now... If you find it, leave
 * a comment here.
 */
#define FIXADDR_TOP	((unsigned long) (-2*PAGE_SIZE))

#include <linux/kernel.h>
#include <linux/bug.h>
#include <asm/page.h>

/*
 * On OpenRISC we use these special fixed_addresses for doing ioremap
 * early in the boot process before memory initialization is complete.
 * This is used, in particular, by the early serial console code.
 *
 * It's not really 'fixmap', per se, but fits loosely into the same
 * paradigm.
 */
enum fixed_addresses {
	/*
	 * FIX_IOREMAP entries are useful for mapping physical address
	 * space before ioremap() is useable, e.g. really early in boot
	 * before kmalloc() is working.
	 */
#define FIX_N_IOREMAPS  32
	FIX_IOREMAP_BEGIN,
	FIX_IOREMAP_END = FIX_IOREMAP_BEGIN + FIX_N_IOREMAPS - 1,
	__end_of_fixed_addresses
};

#define FIXADDR_SIZE		(__end_of_fixed_addresses << PAGE_SHIFT)
/* FIXADDR_BOTTOM might be a better name here... */
#define FIXADDR_START		(FIXADDR_TOP - FIXADDR_SIZE)

#include <asm-generic/fixmap.h>

#endif
