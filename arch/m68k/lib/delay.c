/*
 *	arch/m68knommu/lib/delay.c
 *
 *	(C) Copyright 2004, Greg Ungerer <gerg@snapgear.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <asm/param.h>
#include <asm/delay.h>

EXPORT_SYMBOL(udelay);

void udelay(unsigned long usecs)
{
	_udelay(usecs);
}

