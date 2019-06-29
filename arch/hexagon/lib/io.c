// SPDX-License-Identifier: GPL-2.0-only
/*
 * I/O access functions for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
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
