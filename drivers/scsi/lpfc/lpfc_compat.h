/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_compat.h 1.32 2005/01/25 17:51:45EST sf_support Exp  $
 *
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

#if __BIG_ENDIAN

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
	/* actually returns 1 byte past dest */
	memcpy_toio( dest, src, bytes);
}

static inline void
lpfc_memcpy_from_slim( void *dest, void __iomem *src, unsigned int bytes)
{
	/* actually returns 1 byte past dest */
	memcpy_fromio( dest, src, bytes);
}

#endif	/* __BIG_ENDIAN */
