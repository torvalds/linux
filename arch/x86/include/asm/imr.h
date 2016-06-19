/*
 * imr.h: Isolated Memory Region API
 *
 * Copyright(c) 2013 Intel Corporation.
 * Copyright(c) 2015 Bryan O'Donoghue <pure.logic@nexus-software.ie>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _IMR_H
#define _IMR_H

#include <linux/types.h>

/*
 * IMR agent access mask bits
 * See section 12.7.4.7 from quark-x1000-datasheet.pdf for register
 * definitions.
 */
#define IMR_ESRAM_FLUSH		BIT(31)
#define IMR_CPU_SNOOP		BIT(30)		/* Applicable only to write */
#define IMR_RMU			BIT(29)
#define IMR_VC1_SAI_ID3		BIT(15)
#define IMR_VC1_SAI_ID2		BIT(14)
#define IMR_VC1_SAI_ID1		BIT(13)
#define IMR_VC1_SAI_ID0		BIT(12)
#define IMR_VC0_SAI_ID3		BIT(11)
#define IMR_VC0_SAI_ID2		BIT(10)
#define IMR_VC0_SAI_ID1		BIT(9)
#define IMR_VC0_SAI_ID0		BIT(8)
#define IMR_CPU_0		BIT(1)		/* SMM mode */
#define IMR_CPU			BIT(0)		/* Non SMM mode */
#define IMR_ACCESS_NONE		0

/*
 * Read/Write access-all bits here include some reserved bits
 * These are the values firmware uses and are accepted by hardware.
 * The kernel defines read/write access-all in the same way as firmware
 * in order to have a consistent and crisp definition across firmware,
 * bootloader and kernel.
 */
#define IMR_READ_ACCESS_ALL	0xBFFFFFFF
#define IMR_WRITE_ACCESS_ALL	0xFFFFFFFF

/* Number of IMRs provided by Quark X1000 SoC */
#define QUARK_X1000_IMR_MAX	0x08
#define QUARK_X1000_IMR_REGBASE 0x40

/* IMR alignment bits - only bits 31:10 are checked for IMR validity */
#define IMR_ALIGN		0x400
#define IMR_MASK		(IMR_ALIGN - 1)

int imr_add_range(phys_addr_t base, size_t size,
		  unsigned int rmask, unsigned int wmask);

int imr_remove_range(phys_addr_t base, size_t size);

#endif /* _IMR_H */
