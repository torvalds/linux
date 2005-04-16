/*
 * arch/sh/mm/pg-nommu.c
 *
 * clear_page()/copy_page() implementation for MMUless SH.
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/page.h>

static void copy_page_nommu(void *to, void *from)
{
	memcpy(to, from, PAGE_SIZE);
}

static void clear_page_nommu(void *to)
{
	memset(to, 0, PAGE_SIZE);
}

static int __init pg_nommu_init(void)
{
	copy_page = copy_page_nommu;
	clear_page = clear_page_nommu;

	return 0;
}

subsys_initcall(pg_nommu_init);

