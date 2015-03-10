/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC NAND controller driver
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __ASM_MACH_JZ4740_JZ4740_NAND_H__
#define __ASM_MACH_JZ4740_JZ4740_NAND_H__

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#define JZ_NAND_NUM_BANKS 4

struct jz_nand_platform_data {
	int			num_partitions;
	struct mtd_partition	*partitions;

	struct nand_ecclayout	*ecc_layout;

	unsigned char banks[JZ_NAND_NUM_BANKS];

	void (*ident_callback)(struct platform_device *, struct nand_chip *,
				struct mtd_partition **, int *num_partitions);
};

#endif
