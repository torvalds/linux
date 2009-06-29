/*
 * mach-davinci/nand.h
 *
 * Copyright © 2006 Texas Instruments.
 *
 * Ported to 2.6.23 Copyright © 2008 by
 *   Sander Huijsen <Shuijsen@optelecom-nkf.com>
 *   Troy Kisky <troy.kisky@boundarydevices.com>
 *   Dirk Behme <Dirk.Behme@gmail.com>
 *
 * --------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ARCH_ARM_DAVINCI_NAND_H
#define __ARCH_ARM_DAVINCI_NAND_H

#include <linux/mtd/nand.h>

#define NRCSR_OFFSET		0x00
#define AWCCR_OFFSET		0x04
#define A1CR_OFFSET		0x10
#define NANDFCR_OFFSET		0x60
#define NANDFSR_OFFSET		0x64
#define NANDF1ECC_OFFSET	0x70

/* 4-bit ECC syndrome registers */
#define NAND_4BIT_ECC_LOAD_OFFSET	0xbc
#define NAND_4BIT_ECC1_OFFSET		0xc0
#define NAND_4BIT_ECC2_OFFSET		0xc4
#define NAND_4BIT_ECC3_OFFSET		0xc8
#define NAND_4BIT_ECC4_OFFSET		0xcc
#define NAND_ERR_ADD1_OFFSET		0xd0
#define NAND_ERR_ADD2_OFFSET		0xd4
#define NAND_ERR_ERRVAL1_OFFSET		0xd8
#define NAND_ERR_ERRVAL2_OFFSET		0xdc

/* NOTE:  boards don't need to use these address bits
 * for ALE/CLE unless they support booting from NAND.
 * They're used unless platform data overrides them.
 */
#define	MASK_ALE		0x08
#define	MASK_CLE		0x10

struct davinci_nand_pdata {		/* platform_data */
	uint32_t		mask_ale;
	uint32_t		mask_cle;

	/* for packages using two chipselects */
	uint32_t		mask_chipsel;

	/* board's default static partition info */
	struct mtd_partition	*parts;
	unsigned		nr_parts;

	/* none  == NAND_ECC_NONE (strongly *not* advised!!)
	 * soft  == NAND_ECC_SOFT
	 * else  == NAND_ECC_HW, according to ecc_bits
	 *
	 * All DaVinci-family chips support 1-bit hardware ECC.
	 * Newer ones also support 4-bit ECC, but are awkward
	 * using it with large page chips.
	 */
	nand_ecc_modes_t	ecc_mode;
	u8			ecc_bits;

	/* e.g. NAND_BUSWIDTH_16 or NAND_USE_FLASH_BBT */
	unsigned		options;
};

#endif	/* __ARCH_ARM_DAVINCI_NAND_H */
