// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info xilinx_parts[] = {
	/* Xilinx S3AN Internal Flash */
	{ "3S50AN", S3AN_INFO(0x1f2200, 64, 264) },
	{ "3S200AN", S3AN_INFO(0x1f2400, 256, 264) },
	{ "3S400AN", S3AN_INFO(0x1f2400, 256, 264) },
	{ "3S700AN", S3AN_INFO(0x1f2500, 512, 264) },
	{ "3S1400AN", S3AN_INFO(0x1f2600, 512, 528) },
};

/*
 * This code converts an address to the Default Address Mode, that has non
 * power of two page sizes. We must support this mode because it is the default
 * mode supported by Xilinx tools, it can access the whole flash area and
 * changing over to the Power-of-two mode is irreversible and corrupts the
 * original data.
 * Addr can safely be unsigned int, the biggest S3AN device is smaller than
 * 4 MiB.
 */
static u32 s3an_convert_addr(struct spi_nor *nor, u32 addr)
{
	u32 offset, page;

	offset = addr % nor->page_size;
	page = addr / nor->page_size;
	page <<= (nor->page_size > 512) ? 10 : 9;

	return page | offset;
}

static int xilinx_nor_setup(struct spi_nor *nor,
			    const struct spi_nor_hwcaps *hwcaps)
{
	int ret;

	ret = spi_nor_xread_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	nor->erase_opcode = SPINOR_OP_XSE;
	nor->program_opcode = SPINOR_OP_XPP;
	nor->read_opcode = SPINOR_OP_READ;
	nor->flags |= SNOR_F_NO_OP_CHIP_ERASE;

	/*
	 * This flashes have a page size of 264 or 528 bytes (known as
	 * Default addressing mode). It can be changed to a more standard
	 * Power of two mode where the page size is 256/512. This comes
	 * with a price: there is 3% less of space, the data is corrupted
	 * and the page size cannot be changed back to default addressing
	 * mode.
	 *
	 * The current addressing mode can be read from the XRDSR register
	 * and should not be changed, because is a destructive operation.
	 */
	if (nor->bouncebuf[0] & XSR_PAGESIZE) {
		/* Flash in Power of 2 mode */
		nor->page_size = (nor->page_size == 264) ? 256 : 512;
		nor->mtd.writebufsize = nor->page_size;
		nor->mtd.size = 8 * nor->page_size * nor->info->n_sectors;
		nor->mtd.erasesize = 8 * nor->page_size;
	} else {
		/* Flash in Default addressing mode */
		nor->params->convert_addr = s3an_convert_addr;
		nor->mtd.erasesize = nor->info->sector_size;
	}

	return 0;
}

static void xilinx_post_sfdp_fixups(struct spi_nor *nor)
{
	nor->params->setup = xilinx_nor_setup;
}

static const struct spi_nor_fixups xilinx_fixups = {
	.post_sfdp = xilinx_post_sfdp_fixups,
};

const struct spi_nor_manufacturer spi_nor_xilinx = {
	.name = "xilinx",
	.parts = xilinx_parts,
	.nparts = ARRAY_SIZE(xilinx_parts),
	.fixups = &xilinx_fixups,
};
