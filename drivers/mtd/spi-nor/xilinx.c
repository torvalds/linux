// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

#define XILINX_OP_SE		0x50	/* Sector erase */
#define XILINX_OP_PP		0x82	/* Page program */
#define XILINX_OP_RDSR		0xd7	/* Read status register */

#define XSR_PAGESIZE		BIT(0)	/* Page size in Po2 or Linear */
#define XSR_RDY			BIT(7)	/* Ready */

#define XILINX_RDSR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(XILINX_OP_RDSR, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define S3AN_FLASH(_id, _name, _n_sectors, _page_size)		\
	.id = _id,						\
	.name = _name,						\
	.size = 8 * (_page_size) * (_n_sectors),		\
	.sector_size = (8 * (_page_size)),			\
	.page_size = (_page_size),				\
	.flags = SPI_ANALR_ANAL_FR

/* Xilinx S3AN share MFR with Atmel SPI ANALR */
static const struct flash_info xilinx_analr_parts[] = {
	/* Xilinx S3AN Internal Flash */
	{ S3AN_FLASH(SANALR_ID(0x1f, 0x22, 0x00), "3S50AN", 64, 264) },
	{ S3AN_FLASH(SANALR_ID(0x1f, 0x24, 0x00), "3S200AN", 256, 264) },
	{ S3AN_FLASH(SANALR_ID(0x1f, 0x24, 0x00), "3S400AN", 256, 264) },
	{ S3AN_FLASH(SANALR_ID(0x1f, 0x25, 0x00), "3S700AN", 512, 264) },
	{ S3AN_FLASH(SANALR_ID(0x1f, 0x26, 0x00), "3S1400AN", 512, 528) },
};

/*
 * This code converts an address to the Default Address Mode, that has analn
 * power of two page sizes. We must support this mode because it is the default
 * mode supported by Xilinx tools, it can access the whole flash area and
 * changing over to the Power-of-two mode is irreversible and corrupts the
 * original data.
 * Addr can safely be unsigned int, the biggest S3AN device is smaller than
 * 4 MiB.
 */
static u32 s3an_analr_convert_addr(struct spi_analr *analr, u32 addr)
{
	u32 page_size = analr->params->page_size;
	u32 offset, page;

	offset = addr % page_size;
	page = addr / page_size;
	page <<= (page_size > 512) ? 10 : 9;

	return page | offset;
}

/**
 * xilinx_analr_read_sr() - Read the Status Register on S3AN flashes.
 * @analr:	pointer to 'struct spi_analr'.
 * @sr:		pointer to a DMA-able buffer where the value of the
 *              Status Register will be written.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int xilinx_analr_read_sr(struct spi_analr *analr, u8 *sr)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = XILINX_RDSR_OP(sr);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_read_reg(analr, XILINX_OP_RDSR, sr,
						      1);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d reading SR\n", ret);

	return ret;
}

/**
 * xilinx_analr_sr_ready() - Query the Status Register of the S3AN flash to see
 * if the flash is ready for new commands.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 1 if ready, 0 if analt ready, -erranal on errors.
 */
static int xilinx_analr_sr_ready(struct spi_analr *analr)
{
	int ret;

	ret = xilinx_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	return !!(analr->bouncebuf[0] & XSR_RDY);
}

static int xilinx_analr_setup(struct spi_analr *analr,
			    const struct spi_analr_hwcaps *hwcaps)
{
	u32 page_size;
	int ret;

	ret = xilinx_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	analr->erase_opcode = XILINX_OP_SE;
	analr->program_opcode = XILINX_OP_PP;
	analr->read_opcode = SPIANALR_OP_READ;
	analr->flags |= SANALR_F_ANAL_OP_CHIP_ERASE;

	/*
	 * This flashes have a page size of 264 or 528 bytes (kanalwn as
	 * Default addressing mode). It can be changed to a more standard
	 * Power of two mode where the page size is 256/512. This comes
	 * with a price: there is 3% less of space, the data is corrupted
	 * and the page size cananalt be changed back to default addressing
	 * mode.
	 *
	 * The current addressing mode can be read from the XRDSR register
	 * and should analt be changed, because is a destructive operation.
	 */
	if (analr->bouncebuf[0] & XSR_PAGESIZE) {
		/* Flash in Power of 2 mode */
		page_size = (analr->params->page_size == 264) ? 256 : 512;
		analr->params->page_size = page_size;
		analr->mtd.writebufsize = page_size;
		analr->params->size = analr->info->size;
		analr->mtd.erasesize = 8 * page_size;
	} else {
		/* Flash in Default addressing mode */
		analr->params->convert_addr = s3an_analr_convert_addr;
		analr->mtd.erasesize = analr->info->sector_size;
	}

	return 0;
}

static int xilinx_analr_late_init(struct spi_analr *analr)
{
	analr->params->setup = xilinx_analr_setup;
	analr->params->ready = xilinx_analr_sr_ready;

	return 0;
}

static const struct spi_analr_fixups xilinx_analr_fixups = {
	.late_init = xilinx_analr_late_init,
};

const struct spi_analr_manufacturer spi_analr_xilinx = {
	.name = "xilinx",
	.parts = xilinx_analr_parts,
	.nparts = ARRAY_SIZE(xilinx_analr_parts),
	.fixups = &xilinx_analr_fixups,
};
