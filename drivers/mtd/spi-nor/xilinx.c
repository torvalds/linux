// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define XILINX_OP_SE		0x50	/* Sector erase */
#define XILINX_OP_PP		0x82	/* Page program */
#define XILINX_OP_RDSR		0xd7	/* Read status register */

#define XSR_PAGESIZE		BIT(0)	/* Page size in Po2 or Linear */
#define XSR_RDY			BIT(7)	/* Ready */

#define XILINX_RDSR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(XILINX_OP_RDSR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define S3AN_FLASH(_id, _name, _n_sectors, _page_size)		\
	.id = _id,						\
	.name = _name,						\
	.size = 8 * (_page_size) * (_n_sectors),		\
	.sector_size = (8 * (_page_size)),			\
	.page_size = (_page_size),				\
	.flags = SPI_NOR_NO_FR

/* Xilinx S3AN share MFR with Atmel SPI NOR */
static const struct flash_info xilinx_nor_parts[] = {
	/* Xilinx S3AN Internal Flash */
	{ S3AN_FLASH(SNOR_ID(0x1f, 0x22, 0x00), "3S50AN", 64, 264) },
	{ S3AN_FLASH(SNOR_ID(0x1f, 0x24, 0x00), "3S200AN", 256, 264) },
	{ S3AN_FLASH(SNOR_ID(0x1f, 0x24, 0x00), "3S400AN", 256, 264) },
	{ S3AN_FLASH(SNOR_ID(0x1f, 0x25, 0x00), "3S700AN", 512, 264) },
	{ S3AN_FLASH(SNOR_ID(0x1f, 0x26, 0x00), "3S1400AN", 512, 528) },
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
static u32 s3an_nor_convert_addr(struct spi_nor *nor, u32 addr)
{
	u32 page_size = nor->params->page_size;
	u32 offset, page;

	offset = addr % page_size;
	page = addr / page_size;
	page <<= (page_size > 512) ? 10 : 9;

	return page | offset;
}

/**
 * xilinx_nor_read_sr() - Read the Status Register on S3AN flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @sr:		pointer to a DMA-able buffer where the value of the
 *              Status Register will be written.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int xilinx_nor_read_sr(struct spi_nor *nor, u8 *sr)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = XILINX_RDSR_OP(sr);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_read_reg(nor, XILINX_OP_RDSR, sr,
						      1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d reading SR\n", ret);

	return ret;
}

/**
 * xilinx_nor_sr_ready() - Query the Status Register of the S3AN flash to see
 * if the flash is ready for new commands.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 1 if ready, 0 if not ready, -errno on errors.
 */
static int xilinx_nor_sr_ready(struct spi_nor *nor)
{
	int ret;

	ret = xilinx_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	return !!(nor->bouncebuf[0] & XSR_RDY);
}

static int xilinx_nor_setup(struct spi_nor *nor,
			    const struct spi_nor_hwcaps *hwcaps)
{
	u32 page_size;
	int ret;

	ret = xilinx_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	nor->erase_opcode = XILINX_OP_SE;
	nor->program_opcode = XILINX_OP_PP;
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
		page_size = (nor->params->page_size == 264) ? 256 : 512;
		nor->params->page_size = page_size;
		nor->mtd.writebufsize = page_size;
		nor->params->size = nor->info->size;
		nor->mtd.erasesize = 8 * page_size;
	} else {
		/* Flash in Default addressing mode */
		nor->params->convert_addr = s3an_nor_convert_addr;
		nor->mtd.erasesize = nor->info->sector_size;
	}

	return 0;
}

static int xilinx_nor_late_init(struct spi_nor *nor)
{
	nor->params->setup = xilinx_nor_setup;
	nor->params->ready = xilinx_nor_sr_ready;

	return 0;
}

static const struct spi_nor_fixups xilinx_nor_fixups = {
	.late_init = xilinx_nor_late_init,
};

const struct spi_nor_manufacturer spi_nor_xilinx = {
	.name = "xilinx",
	.parts = xilinx_nor_parts,
	.nparts = ARRAY_SIZE(xilinx_nor_parts),
	.fixups = &xilinx_nor_fixups,
};
