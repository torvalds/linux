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
#include <asm/uaccess.h>

void copy_page(void *to, void *from)
{
	memcpy(to, from, PAGE_SIZE);
}

void clear_page(void *to)
{
	memset(to, 0, PAGE_SIZE);
}

__kernel_size_t __copy_user(void *to, const void *from, __kernel_size_t n)
{
	memcpy(to, from, n);
	return 0;
}

__kernel_size_t __clear_user(void *to, __kernel_size_t n)
{
	memset(to, 0, n);
	return 0;
}
