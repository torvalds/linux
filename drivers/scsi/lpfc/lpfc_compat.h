/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2005 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

/*
 * This file provides macros to aid compilation in the Linux 2.4 kernel
 * over various platform architectures.
 */

/*******************************************************************
Note: HBA's SLI memory contains little-endian LW.
Thus to access it from a little-endian host,
memcpy_toio() and memcpy_fromio() can be used.
However on a big-endian host, copy 4 bytes at a time,
using writel() and readl().
 *******************************************************************/
#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN

static inline void
lpfc_memcpy_to_slim(void __iomem *dest, void *src, unsigned int bytes)
{
	uint32_t __iomem *dest32;
	uint32_t *src32;
	unsigned int four_bytes;


	dest32  = (uint32_t __iomem *) dest;
	src32  = (uint32_t *) src;

	/* write input bytes, 4 bytes at a time */
	for (four_bytes = bytes /4; four_bytes > 0; four_bytes--) {
		writel( *src32, dest32);
		readl(dest32); /* flush */
		dest32++;
		src32++;
	}

	return;
}

static inline void
lpfc_memcpy_from_slim( void *dest, void __iomem *src, unsigned int bytes)
{
	uint32_t *dest32;
	uint32_t __iomem *src32;
	unsigned int four_bytes;


	dest32  = (uint32_t *) dest;
	src32  = (uint32_t __iomem *) src;

	/* read input bytes, 4 bytes at a time */
	for (four_bytes = bytes /4; four_bytes > 0; four_bytes--) {
		*dest32 = readl( src32);
		dest32++;
		src32++;
	}

	return;
}

#else

static inline void
lpfc_memcpy_to_slim( void __iomem *dest, void *src, unsigned int bytes)
{
	__iowrite32_copy(dest, src, bytes);
}

static inline void
lpfc_memcpy_from_slim( void *dest, void __iomem *src, unsigned int bytes)
{
	/* actually returns 1 byte past dest */
	memcpy_fromio( dest, src, bytes);
}

#endif	/* __BIG_ENDIAN */
