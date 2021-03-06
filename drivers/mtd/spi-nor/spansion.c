// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define SPINOR_OP_RD_ANY_REG			0x65	/* Read any register */
#define SPINOR_OP_WR_ANY_REG			0x71	/* Write any register */
#define SPINOR_REG_CYPRESS_CFR2V		0x00800003
#define SPINOR_REG_CYPRESS_CFR2V_MEMLAT_11_24	0xb
#define SPINOR_REG_CYPRESS_CFR3V		0x00800004
#define SPINOR_REG_CYPRESS_CFR3V_PGSZ		BIT(4) /* Page size. */
#define SPINOR_REG_CYPRESS_CFR5V		0x00800006
#define SPINOR_REG_CYPRESS_CFR5V_OCT_DTR_EN	0x3
#define SPINOR_REG_CYPRESS_CFR5V_OCT_DTR_DS	0
#define SPINOR_OP_CYPRESS_RD_FAST		0xee

/**
 * spi_nor_cypress_octal_dtr_enable() - Enable octal DTR on Cypress flashes.
 * @nor:		pointer to a 'struct spi_nor'
 * @enable:              whether to enable or disable Octal DTR
 *
 * This also sets the memory access latency cycles to 24 to allow the flash to
 * run at up to 200MHz.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_cypress_octal_dtr_enable(struct spi_nor *nor, bool enable)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	if (enable) {
		/* Use 24 dummy cycles for memory array reads. */
		ret = spi_nor_write_enable(nor);
		if (ret)
			return ret;

		*buf = SPINOR_REG_CYPRESS_CFR2V_MEMLAT_11_24;
		op = (struct spi_mem_op)
			SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WR_ANY_REG, 1),
				   SPI_MEM_OP_ADDR(3, SPINOR_REG_CYPRESS_CFR2V,
						   1),
				   SPI_MEM_OP_NO_DUMMY,
				   SPI_MEM_OP_DATA_OUT(1, buf, 1));

		ret = spi_mem_exec_op(nor->spimem, &op);
		if (ret)
			return ret;

		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			return ret;

		nor->read_dummy = 24;
	}

	/* Set/unset the octal and DTR enable bits. */
	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	if (enable)
		*buf = SPINOR_REG_CYPRESS_CFR5V_OCT_DTR_EN;
	else
		*buf = SPINOR_REG_CYPRESS_CFR5V_OCT_DTR_DS;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WR_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(enable ? 3 : 4,
					   SPINOR_REG_CYPRESS_CFR5V,
					   1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_OUT(1, buf, 1));

	if (!enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDID, 1),
			   SPI_MEM_OP_ADDR(enable ? 4 : 0, 0, 1),
			   SPI_MEM_OP_DUMMY(enable ? 3 : 0, 1),
			   SPI_MEM_OP_DATA_IN(round_up(nor->info->id_len, 2),
					      buf, 1));

	if (enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	if (memcmp(buf, nor->info->id, nor->info->id_len))
		return -EINVAL;

	return 0;
}

static void s28hs512t_default_init(struct spi_nor *nor)
{
	nor->params->octal_dtr_enable = spi_nor_cypress_octal_dtr_enable;
	nor->params->writesize = 16;
}

static void s28hs512t_post_sfdp_fixup(struct spi_nor *nor)
{
	/*
	 * On older versions of the flash the xSPI Profile 1.0 table has the
	 * 8D-8D-8D Fast Read opcode as 0x00. But it actually should be 0xEE.
	 */
	if (nor->params->reads[SNOR_CMD_READ_8_8_8_DTR].opcode == 0)
		nor->params->reads[SNOR_CMD_READ_8_8_8_DTR].opcode =
			SPINOR_OP_CYPRESS_RD_FAST;

	/* This flash is also missing the 4-byte Page Program opcode bit. */
	spi_nor_set_pp_settings(&nor->params->page_programs[SNOR_CMD_PP],
				SPINOR_OP_PP_4B, SNOR_PROTO_1_1_1);
	/*
	 * Since xSPI Page Program opcode is backward compatible with
	 * Legacy SPI, use Legacy SPI opcode there as well.
	 */
	spi_nor_set_pp_settings(&nor->params->page_programs[SNOR_CMD_PP_8_8_8_DTR],
				SPINOR_OP_PP_4B, SNOR_PROTO_8_8_8_DTR);

	/*
	 * The xSPI Profile 1.0 table advertises the number of additional
	 * address bytes needed for Read Status Register command as 0 but the
	 * actual value for that is 4.
	 */
	nor->params->rdsr_addr_nbytes = 4;
}

static int s28hs512t_post_bfpt_fixup(struct spi_nor *nor,
				     const struct sfdp_parameter_header *bfpt_header,
				     const struct sfdp_bfpt *bfpt)
{
	/*
	 * The BFPT table advertises a 512B page size but the page size is
	 * actually configurable (with the default being 256B). Read from
	 * CFR3V[4] and set the correct size.
	 */
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RD_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(3, SPINOR_REG_CYPRESS_CFR3V, 1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_IN(1, nor->bouncebuf, 1));
	int ret;

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & SPINOR_REG_CYPRESS_CFR3V_PGSZ)
		nor->params->page_size = 512;
	else
		nor->params->page_size = 256;

	return 0;
}

static struct spi_nor_fixups s28hs512t_fixups = {
	.default_init = s28hs512t_default_init,
	.post_sfdp = s28hs512t_post_sfdp_fixup,
	.post_bfpt = s28hs512t_post_bfpt_fixup,
};

static int
s25fs_s_post_bfpt_fixups(struct spi_nor *nor,
			 const struct sfdp_parameter_header *bfpt_header,
			 const struct sfdp_bfpt *bfpt)
{
	/*
	 * The S25FS-S chip family reports 512-byte pages in BFPT but
	 * in reality the write buffer still wraps at the safe default
	 * of 256 bytes.  Overwrite the page size advertised by BFPT
	 * to get the writes working.
	 */
	nor->params->page_size = 256;

	return 0;
}

static struct spi_nor_fixups s25fs_s_fixups = {
	.post_bfpt = s25fs_s_post_bfpt_fixups,
};

static const struct flash_info spansion_parts[] = {
	/* Spansion/Cypress -- single (large) sector size only, at least
	 * for the chips listed here (without boot sectors).
	 */
	{ "s25sl032p",  INFO(0x010215, 0x4d00,  64 * 1024,  64,
			     SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25sl064p",  INFO(0x010216, 0x4d00,  64 * 1024, 128,
			     SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl128s0", INFO6(0x012018, 0x4d0080, 256 * 1024, 64,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      USE_CLSR) },
	{ "s25fl128s1", INFO6(0x012018, 0x4d0180, 64 * 1024, 256,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      USE_CLSR) },
	{ "s25fl256s0", INFO6(0x010219, 0x4d0080, 256 * 1024, 128,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      USE_CLSR) },
	{ "s25fl256s1", INFO6(0x010219, 0x4d0180, 64 * 1024, 512,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      USE_CLSR) },
	{ "s25fl512s",  INFO6(0x010220, 0x4d0080, 256 * 1024, 256,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      SPI_NOR_HAS_LOCK | USE_CLSR) },
	{ "s25fs128s1", INFO6(0x012018, 0x4d0181, 64 * 1024, 256,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | USE_CLSR)
	  .fixups = &s25fs_s_fixups, },
	{ "s25fs256s0", INFO6(0x010219, 0x4d0081, 256 * 1024, 128,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      USE_CLSR) },
	{ "s25fs256s1", INFO6(0x010219, 0x4d0181, 64 * 1024, 512,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      USE_CLSR) },
	{ "s25fs512s",  INFO6(0x010220, 0x4d0081, 256 * 1024, 256,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | USE_CLSR)
	  .fixups = &s25fs_s_fixups, },
	{ "s25sl12800", INFO(0x012018, 0x0300, 256 * 1024,  64, 0) },
	{ "s25sl12801", INFO(0x012018, 0x0301,  64 * 1024, 256, 0) },
	{ "s25fl129p0", INFO(0x012018, 0x4d00, 256 * 1024,  64,
			     SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     USE_CLSR) },
	{ "s25fl129p1", INFO(0x012018, 0x4d01,  64 * 1024, 256,
			     SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     USE_CLSR) },
	{ "s25sl004a",  INFO(0x010212,      0,  64 * 1024,   8, 0) },
	{ "s25sl008a",  INFO(0x010213,      0,  64 * 1024,  16, 0) },
	{ "s25sl016a",  INFO(0x010214,      0,  64 * 1024,  32, 0) },
	{ "s25sl032a",  INFO(0x010215,      0,  64 * 1024,  64, 0) },
	{ "s25sl064a",  INFO(0x010216,      0,  64 * 1024, 128, 0) },
	{ "s25fl004k",  INFO(0xef4013,      0,  64 * 1024,   8,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl008k",  INFO(0xef4014,      0,  64 * 1024,  16,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl016k",  INFO(0xef4015,      0,  64 * 1024,  32,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl064k",  INFO(0xef4017,      0,  64 * 1024, 128,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl116k",  INFO(0x014015,      0,  64 * 1024,  32,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl132k",  INFO(0x014016,      0,  64 * 1024,  64, SECT_4K) },
	{ "s25fl164k",  INFO(0x014017,      0,  64 * 1024, 128, SECT_4K) },
	{ "s25fl204k",  INFO(0x014013,      0,  64 * 1024,   8,
			     SECT_4K | SPI_NOR_DUAL_READ) },
	{ "s25fl208k",  INFO(0x014014,      0,  64 * 1024,  16,
			     SECT_4K | SPI_NOR_DUAL_READ) },
	{ "s25fl064l",  INFO(0x016017,      0,  64 * 1024, 128,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     SPI_NOR_4B_OPCODES) },
	{ "s25fl128l",  INFO(0x016018,      0,  64 * 1024, 256,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     SPI_NOR_4B_OPCODES) },
	{ "s25fl256l",  INFO(0x016019,      0,  64 * 1024, 512,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     SPI_NOR_4B_OPCODES) },
	{ "cy15x104q",  INFO6(0x042cc2, 0x7f7f7f, 512 * 1024, 1,
			      SPI_NOR_NO_ERASE) },
	{ "s28hs512t",   INFO(0x345b1a,      0, 256 * 1024, 256,
			     SECT_4K | SPI_NOR_OCTAL_DTR_READ |
			      SPI_NOR_OCTAL_DTR_PP)
	  .fixups = &s28hs512t_fixups,
	},
};

static void spansion_post_sfdp_fixups(struct spi_nor *nor)
{
	if (nor->params->size <= SZ_16M)
		return;

	nor->flags |= SNOR_F_4B_OPCODES;
	/* No small sector erase for 4-byte command set */
	nor->erase_opcode = SPINOR_OP_SE;
	nor->mtd.erasesize = nor->info->sector_size;
}

static const struct spi_nor_fixups spansion_fixups = {
	.post_sfdp = spansion_post_sfdp_fixups,
};

const struct spi_nor_manufacturer spi_nor_spansion = {
	.name = "spansion",
	.parts = spansion_parts,
	.nparts = ARRAY_SIZE(spansion_parts),
	.fixups = &spansion_fixups,
};
