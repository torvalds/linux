/*
 * arch/xtensa/kernel/xtensa_ksyms.c
 *
 * Export Xtensa-specific functions for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005  Tensilica Inc.
 *
 * Joe Taylor <joe@tensilica.com>
 */

#include <linux/module.h>
#include <asm/pgtable.h>

EXPORT_SYMBOL(empty_zero_page);

unsigned int __sync_fetch_and_and_4(volatile void *p, unsigned int v)
{
	BUG();
}
EXPORT_SYMBOL(__sync_fetch_and_and_4);

unsigned int __sync_fetch_and_or_4(volatile void *p, unsigned int v)
{
	BUG();
}
EXPORT_SYMBOL(__sync_fetch_and_or_4);
