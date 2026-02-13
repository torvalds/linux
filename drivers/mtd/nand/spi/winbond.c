// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 exceet electronics GmbH
 *
 * Authors:
 *	Frieder Schrempf <frieder.schrempf@exceet.de>
 *	Boris Brezillon <boris.brezillon@bootlin.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>
#include <linux/units.h>
#include <linux/delay.h>

#define SPINAND_MFR_WINBOND		0xEF

#define WINBOND_CFG_BUF_READ		BIT(3)

#define W25N04KV_STATUS_ECC_5_8_BITFLIPS	(3 << 4)

#define W25N0XJW_SR4			0xD0
#define W25N0XJW_SR4_HS			BIT(2)

#define W35N01JW_VCR_IO_MODE_REG	0x00
#define W35N01JW_VCR_IO_MODE_SINGLE_SDR		0xFF
#define W35N01JW_VCR_IO_MODE_OCTAL_SDR		0xDF
#define W35N01JW_VCR_IO_MODE_OCTAL_DDR_DS	0xE7
#define W35N01JW_VCR_IO_MODE_OCTAL_DDR		0xC7
#define W35N01JW_VCR_DUMMY_CLOCK_REG	0x01

/*
 * "X2" in the core is equivalent to "dual output" in the datasheets,
 * "X4" in the core is equivalent to "quad output" in the datasheets.
 * Quad and octal capable chips feature an absolute maximum frequency of 166MHz.
 */

static SPINAND_OP_VARIANTS(read_cache_octal_variants,
		SPINAND_PAGE_READ_FROM_CACHE_8D_8D_8D_OP(0, 24, NULL, 0, 120 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_8D_8D_8D_OP(0, 16, NULL, 0, 86 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1D_8D_OP(0, 3, NULL, 0, 120 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1D_8D_OP(0, 2, NULL, 0, 105 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_8S_8S_OP(0, 20, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_8S_8S_OP(0, 16, NULL, 0, 162 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_8S_8S_OP(0, 12, NULL, 0, 124 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_8S_8S_OP(0, 8, NULL, 0, 86 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_8S_OP(0, 2, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_8S_OP(0, 1, NULL, 0, 133 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 0));

static SPINAND_OP_VARIANTS(write_cache_octal_variants,
		SPINAND_PROG_LOAD_8D_8D_8D_OP(true, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_8S_8S_OP(true, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_8S_OP(0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_octal_variants,
		SPINAND_PROG_LOAD_8D_8D_8D_OP(false, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_8S_8S_OP(false, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(false, 0, NULL, 0));

static SPINAND_OP_VARIANTS(read_cache_dual_quad_dtr_variants,
		SPINAND_PAGE_READ_FROM_CACHE_1S_4D_4D_OP(0, 8, NULL, 0, 80 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1D_4D_OP(0, 2, NULL, 0, 80 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_4S_4S_OP(0, 4, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_4S_4S_OP(0, 2, NULL, 0, 104 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2D_2D_OP(0, 4, NULL, 0, 80 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1D_2D_OP(0, 2, NULL, 0, 80 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(0, 2, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(0, 1, NULL, 0, 104 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1D_1D_OP(0, 2, NULL, 0, 80 * HZ_PER_MHZ),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 54 * HZ_PER_MHZ));

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_1S_4S_4S_OP(0, 2, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(true, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(false, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(false, 0, NULL, 0));

#define SPINAND_WINBOND_WRITE_VCR_1S_1S_1S(reg, buf)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x81, 1),				\
		   SPI_MEM_OP_ADDR(3, reg, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 1))

#define SPINAND_WINBOND_WRITE_VCR_8D_8D_8D(reg, buf)			\
	SPI_MEM_OP(SPI_MEM_DTR_OP_RPT_CMD(0x81, 8),			\
		   SPI_MEM_DTR_OP_ADDR(4, reg, 8),			\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_DTR_OP_DATA_OUT(2, buf, 8))

static SPINAND_OP_VARIANTS(winbond_w35_ops,
		SPINAND_WINBOND_WRITE_VCR_1S_1S_1S(0, NULL),
		SPINAND_WINBOND_WRITE_VCR_8D_8D_8D(0, NULL));

static struct spi_mem_op
spinand_fill_winbond_write_vcr_op(struct spinand_device *spinand, u8 reg, void *valptr)
{
	return (spinand->bus_iface == SSDR) ?
		(struct spi_mem_op)SPINAND_WINBOND_WRITE_VCR_1S_1S_1S(reg, valptr) :
		(struct spi_mem_op)SPINAND_WINBOND_WRITE_VCR_8D_8D_8D(reg, valptr);
}

#define SPINAND_WINBOND_SELECT_TARGET_1S_0_1S(buf)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xc2, 1),				\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 1))

static SPINAND_OP_VARIANTS(winbond_w25_ops,
		SPINAND_WINBOND_SELECT_TARGET_1S_0_1S(NULL));

static struct spi_mem_op
spinand_fill_winbond_select_target_op(struct spinand_device *spinand, void *valptr)
{
	WARN_ON_ONCE(spinand->bus_iface != SSDR);

	return (struct spi_mem_op)SPINAND_WINBOND_SELECT_TARGET_1S_0_1S(valptr);
}

static int w25m02gv_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int w25m02gv_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 2;
	region->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops w25m02gv_ooblayout = {
	.ecc = w25m02gv_ooblayout_ecc,
	.free = w25m02gv_ooblayout_free,
};

static int w25m02gv_select_target(struct spinand_device *spinand,
				  unsigned int target)
{
	struct spi_mem_op op = SPINAND_OP(spinand, winbond_select_target,
					  spinand->scratchbuf);

	*spinand->scratchbuf = target;
	return spi_mem_exec_op(spinand->spimem, &op);
}

static int w25n01kv_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = 64 + (8 * section);
	region->length = 7;

	return 0;
}

static int w25n02kv_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = 64 + (16 * section);
	region->length = 13;

	return 0;
}

static int w25n02kv_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 2;
	region->length = 14;

	return 0;
}

static const struct mtd_ooblayout_ops w25n01kv_ooblayout = {
	.ecc = w25n01kv_ooblayout_ecc,
	.free = w25n02kv_ooblayout_free,
};

static const struct mtd_ooblayout_ops w25n02kv_ooblayout = {
	.ecc = w25n02kv_ooblayout_ecc,
	.free = w25n02kv_ooblayout_free,
};

static int w25n01jw_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 12;
	region->length = 4;

	return 0;
}

static int w25n01jw_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section);
	region->length = 12;

	/* Extract BBM */
	if (!section) {
		region->offset += 2;
		region->length -= 2;
	}

	return 0;
}

static int w35n01jw_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 7)
		return -ERANGE;

	region->offset = (16 * section) + 12;
	region->length = 4;

	return 0;
}

static int w35n01jw_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 7)
		return -ERANGE;

	region->offset = 16 * section;
	region->length = 12;

	/* Extract BBM */
	if (!section) {
		region->offset += 2;
		region->length -= 2;
	}

	return 0;
}

static const struct mtd_ooblayout_ops w25n01jw_ooblayout = {
	.ecc = w25n01jw_ooblayout_ecc,
	.free = w25n01jw_ooblayout_free,
};

static const struct mtd_ooblayout_ops w35n01jw_ooblayout = {
	.ecc = w35n01jw_ooblayout_ecc,
	.free = w35n01jw_ooblayout_free,
};

static int w25n02kv_ecc_get_status(struct spinand_device *spinand,
				   u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);
	u8 mbf = 0;
	struct spi_mem_op op = SPINAND_OP(spinand, get_feature,
					  0x30, spinand->scratchbuf);

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case STATUS_ECC_HAS_BITFLIPS:
	case W25N04KV_STATUS_ECC_5_8_BITFLIPS:
		/*
		 * Let's try to retrieve the real maximum number of bitflips
		 * in order to avoid forcing the wear-leveling layer to move
		 * data around if it's not necessary.
		 */
		if (spi_mem_exec_op(spinand->spimem, &op))
			return nanddev_get_ecc_conf(nand)->strength;

		mbf = *(spinand->scratchbuf) >> 4;

		if (WARN_ON(mbf > nanddev_get_ecc_conf(nand)->strength || !mbf))
			return nanddev_get_ecc_conf(nand)->strength;

		return mbf;

	default:
		break;
	}

	return -EINVAL;
}

static int w25n0xjw_hs_cfg(struct spinand_device *spinand,
			   enum spinand_bus_interface iface)
{
	const struct spi_mem_op *op;
	bool hs;
	u8 sr4;
	int ret;

	if (iface != SSDR)
		return -EOPNOTSUPP;

	op = spinand->op_templates->read_cache;
	if (op->cmd.dtr || op->addr.dtr || op->dummy.dtr || op->data.dtr)
		hs = false;
	else if (op->cmd.buswidth == 1 && op->addr.buswidth == 1 &&
		 op->dummy.buswidth == 1 && op->data.buswidth == 1)
		hs = false;
	else if (!op->max_freq)
		hs = true;
	else
		hs = false;

	ret = spinand_read_reg_op(spinand, W25N0XJW_SR4, &sr4);
	if (ret)
		return ret;

	if (hs)
		sr4 |= W25N0XJW_SR4_HS;
	else
		sr4 &= ~W25N0XJW_SR4_HS;

	ret = spinand_write_reg_op(spinand, W25N0XJW_SR4, sr4);
	if (ret)
		return ret;

	return 0;
}

static int w35n0xjw_write_vcr(struct spinand_device *spinand, u8 reg, u8 val)
{
	struct spi_mem_op op = SPINAND_OP(spinand, winbond_write_vcr,
					  reg, spinand->scratchbuf);
	int ret;

	*spinand->scratchbuf = val;

	ret = spinand_write_enable_op(spinand);
	if (ret)
		return ret;

	ret = spi_mem_exec_op(spinand->spimem, &op);
	if (ret)
		return ret;

	/*
	 * Write VCR operation doesn't set the busy bit in SR, which means we
	 * cannot perform a status poll. Minimum time of 50ns is needed to
	 * complete the write.
	 */
	ndelay(50);

	return 0;
}

static int w35n0xjw_vcr_cfg(struct spinand_device *spinand,
			    enum spinand_bus_interface iface)
{
	const struct spi_mem_op *ref_op;
	unsigned int dummy_cycles;
	bool dtr, single;
	u8 io_mode;
	int ret;

	switch (iface) {
	case SSDR:
		ref_op = spinand->ssdr_op_templates.read_cache;
		break;
	case ODTR:
		ref_op = spinand->odtr_op_templates.read_cache;
		break;
	default:
		return -EOPNOTSUPP;
	}

	dummy_cycles = ((ref_op->dummy.nbytes * 8) / ref_op->dummy.buswidth) /
		(ref_op->dummy.dtr ? 2 : 1);
	switch (dummy_cycles) {
	case 8:
	case 12:
	case 16:
	case 20:
	case 24:
	case 28:
		break;
	default:
		return -EINVAL;
	}

	ret = w35n0xjw_write_vcr(spinand, W35N01JW_VCR_DUMMY_CLOCK_REG, dummy_cycles);
	if (ret)
		return ret;

	single = (ref_op->cmd.buswidth == 1 &&
		  ref_op->addr.buswidth == 1 &&
		  ref_op->data.buswidth == 1);
	dtr = (ref_op->cmd.dtr && ref_op->addr.dtr && ref_op->data.dtr);
	if (single && !dtr)
		io_mode = W35N01JW_VCR_IO_MODE_SINGLE_SDR;
	else if (!single && !dtr)
		io_mode = W35N01JW_VCR_IO_MODE_OCTAL_SDR;
	else if (!single && dtr)
		io_mode = W35N01JW_VCR_IO_MODE_OCTAL_DDR;
	else
		return -EINVAL;

	ret = w35n0xjw_write_vcr(spinand, W35N01JW_VCR_IO_MODE_REG, io_mode);
	if (ret)
		return ret;

	return 0;
}

static const struct spinand_info winbond_spinand_table[] = {
	/* 512M-bit densities */
	SPINAND_INFO("W25N512GW", /* 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xba, 0x20),
		     NAND_MEMORG(1, 2048, 64, 64, 512, 10, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25m02gv_ooblayout, NULL)),
	/* 1G-bit densities */
	SPINAND_INFO("W25N01GV", /* 3.3V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xaa, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25m02gv_ooblayout, NULL)),
	SPINAND_INFO("W25N01GW", /* 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xba, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25m02gv_ooblayout, NULL)),
	SPINAND_INFO("W25N01JW", /* high-speed 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xbc, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_dual_quad_dtr_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25n01jw_ooblayout, NULL),
		     SPINAND_CONFIGURE_CHIP(w25n0xjw_hs_cfg)),
	SPINAND_INFO("W25N01KV", /* 3.3V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xae, 0x21),
		     NAND_MEMORG(1, 2048, 96, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25n01kv_ooblayout, w25n02kv_ecc_get_status)),
	SPINAND_INFO("W35N01JW", /* 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xdc, 0x21),
		     NAND_MEMORG(1, 4096, 128, 64, 512, 10, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_octal_variants,
					      &write_cache_octal_variants,
					      &update_cache_octal_variants),
		     0,
		     SPINAND_INFO_VENDOR_OPS(&winbond_w35_ops),
		     SPINAND_ECCINFO(&w35n01jw_ooblayout, NULL),
		     SPINAND_CONFIGURE_CHIP(w35n0xjw_vcr_cfg)),
	SPINAND_INFO("W35N02JW", /* 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xdf, 0x22),
		     NAND_MEMORG(1, 4096, 128, 64, 512, 10, 1, 2, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_octal_variants,
					      &write_cache_octal_variants,
					      &update_cache_octal_variants),
		     0,
		     SPINAND_INFO_VENDOR_OPS(&winbond_w35_ops),
		     SPINAND_ECCINFO(&w35n01jw_ooblayout, NULL),
		     SPINAND_CONFIGURE_CHIP(w35n0xjw_vcr_cfg)),
	SPINAND_INFO("W35N04JW", /* 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xdf, 0x23),
		     NAND_MEMORG(1, 4096, 128, 64, 512, 10, 1, 4, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_octal_variants,
					      &write_cache_octal_variants,
					      &update_cache_octal_variants),
		     0,
		     SPINAND_INFO_VENDOR_OPS(&winbond_w35_ops),
		     SPINAND_ECCINFO(&w35n01jw_ooblayout, NULL),
		     SPINAND_CONFIGURE_CHIP(w35n0xjw_vcr_cfg)),
	/* 2G-bit densities */
	SPINAND_INFO("W25M02GV", /* 2x1G-bit 3.3V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xab, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 2),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_INFO_VENDOR_OPS(&winbond_w25_ops),
		     SPINAND_ECCINFO(&w25m02gv_ooblayout, NULL),
		     SPINAND_SELECT_TARGET(w25m02gv_select_target)),
	SPINAND_INFO("W25N02JW", /* high-speed 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xbf, 0x22),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 2, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_dual_quad_dtr_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25m02gv_ooblayout, NULL),
		     SPINAND_CONFIGURE_CHIP(w25n0xjw_hs_cfg)),
	SPINAND_INFO("W25N02KV", /* 3.3V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xaa, 0x22),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25n02kv_ooblayout, w25n02kv_ecc_get_status)),
	SPINAND_INFO("W25N02KW", /* 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xba, 0x22),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25n02kv_ooblayout, w25n02kv_ecc_get_status)),
	/* 4G-bit densities */
	SPINAND_INFO("W25N04KV", /* 3.3V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xaa, 0x23),
		     NAND_MEMORG(1, 2048, 128, 64, 4096, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25n02kv_ooblayout, w25n02kv_ecc_get_status)),
	SPINAND_INFO("W25N04KW", /* 1.8V */
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xba, 0x23),
		     NAND_MEMORG(1, 2048, 128, 64, 4096, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&w25n02kv_ooblayout, w25n02kv_ecc_get_status)),
};

static int winbond_spinand_init(struct spinand_device *spinand)
{
	struct nand_device *nand = spinand_to_nand(spinand);
	unsigned int i;

	/*
	 * Make sure all dies are in buffer read mode and not continuous read
	 * mode.
	 */
	for (i = 0; i < nand->memorg.ntargets; i++) {
		spinand_select_target(spinand, i);
		spinand_upd_cfg(spinand, WINBOND_CFG_BUF_READ,
				WINBOND_CFG_BUF_READ);
	}

	return 0;
}

static const struct spinand_manufacturer_ops winbond_spinand_manuf_ops = {
	.init = winbond_spinand_init,
};

const struct spinand_manufacturer winbond_spinand_manufacturer = {
	.id = SPINAND_MFR_WINBOND,
	.name = "Winbond",
	.chips = winbond_spinand_table,
	.nchips = ARRAY_SIZE(winbond_spinand_table),
	.ops = &winbond_spinand_manuf_ops,
};
