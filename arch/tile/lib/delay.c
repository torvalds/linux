/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/thread_info.h>
#include <asm/fixmap.h>
#include <hv/hypervisor.h>

void __udelay(unsigned long usecs)
{
	hv_nanosleep(usecs * 1000);
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	hv_nanosleep(nsecs);
}
EXPORT_SYMBOL(__ndelay);

/* FIXME: should be declared in a header somewhere. */
EXPORT_SYMBOL(__delay);
