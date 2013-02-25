/*
 * I/O access functions for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <asm/io.h>

/*  These are all FIFO routines!  */

/*
 * __raw_readsw - read words a short at a time
 * @addr:  source address
 * @data:  data address
 * @len: number of shorts to read
 */
void __raw_readsw(const void __iomem *addr, void *data, int len)
{
	const volatile short int *src = (short int *) addr;
	short int *dst = (short int *) data;

	if ((u32)data & 0x1)
		panic("unaligned pointer to readsw");

	while (len-- > 0)
		*dst++ = *src;

}

/*
 * __raw_writesw - read words a short at a time
 * @addr:  source address
 * @data:  data address
 * @len: number of shorts to read
 */
void __raw_writesw(void __iomem *addr, const void *data, int len)
{
	const short int *src = (short int *)data;
	volatile short int *dst = (short int *)addr;

	if ((u32)data & 0x1)
		panic("unaligned pointer to writesw");

	while (len-- > 0)
		*dst = *src++;


}

/*  Pretty sure len is pre-adjusted for the length of the access already */
void __raw_readsl(const void __iomem *addr, void *data, int len)
{
	const volatile long *src = (long *) addr;
	long *dst = (long *) data;

	if ((u32)data & 0x3)
		panic("unaligned pointer to readsl");

	while (len-- > 0)
		*dst++ = *src;


}

void __raw_writesl(void __iomem *addr, const void *data, int len)
{
	const long *src = (long *)data;
	volatile long *dst = (long *)addr;

	if ((u32)data & 0x3)
		panic("unaligned pointer to writesl");

	while (len-- > 0)
		*dst = *src++;


}
