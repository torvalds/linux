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

#include <linux/cpumask.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/export.h>

/*
 * Allow cropping out bits beyond the end of the array.
 * Move to "lib" directory if more clients want to use this routine.
 */
int bitmap_parselist_crop(const char *bp, unsigned long *maskp, int nmaskbits)
{
	unsigned a, b;

	bitmap_zero(maskp, nmaskbits);
	do {
		if (!isdigit(*bp))
			return -EINVAL;
		a = simple_strtoul(bp, (char **)&bp, 10);
		b = a;
		if (*bp == '-') {
			bp++;
			if (!isdigit(*bp))
				return -EINVAL;
			b = simple_strtoul(bp, (char **)&bp, 10);
		}
		if (!(a <= b))
			return -EINVAL;
		if (b >= nmaskbits)
			b = nmaskbits-1;
		while (a <= b) {
			set_bit(a, maskp);
			a++;
		}
		if (*bp == ',')
			bp++;
	} while (*bp != '\0' && *bp != '\n');
	return 0;
}
EXPORT_SYMBOL(bitmap_parselist_crop);
