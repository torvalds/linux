// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define MXIC_NOR_OP_RD_CR2	0x71		/* Read configuration register 2 opcode */
#define MXIC_NOR_OP_WR_CR2	0x72		/* Write configuration register 2 opcode */
#define MXIC_NOR_ADDR_CR2_MODE	0x00000000	/* CR2 address for setting spi/sopi/dopi mode */
#define MXIC_NOR_ADDR_CR2_DC	0x00000300	/* CR2 address for setting dummy cycles */
#define MXIC_NOR_REG_DOPI_EN	0x2		/* Enable Octal DTR */
#define MXIC_NOR_REG_SPI_EN	0x0		/* Enable SPI */

/* Convert dummy cycles to bit pattern */
#define MXIC_NOR_REG_DC(p) \
	((20 - (p)) >> 1)

#define MXIC_NOR_WR_CR2(addr, ndata, buf)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(MXIC_NOR_OP_WR_CR2, 0),	\
		   SPI_MEM_OP_ADDR(4, addr, 0),			\
		   SPI_MEM_OP_NO_DUMMY,				\
		   SPI_MEM_OP_DATA_OUT(ndata, buf, 0))

static int
mx25l25635_post_bfpt_fixups(struct spi_nor *nor,
			    const struct sfdp_parameter_header *bfpt_header,
			    const struct sfdp_bfpt *bfpt)
{
	/*
	 * MX25L25635F supports 4B opcodes but MX25L25635E does not.
	 * Unfortunately, Macronix has re-used the same JEDEC ID for both
	 * variants which prevents us from defining a new entry in the parts
	 * table.
	 * We need a way to differentiate MX25L25635E and MX25L25635F, and it
	 * seems that the F version advertises support for Fast Read 4-4-4 in
	 * its BFPT table.
	 */
	if (bfpt->dwords[SFDP_DWORD(5)] & BFPT_DWORD5_FAST_READ_4_4_4)
		nor->params->flags |= SNOR_F_4B_OPCODES;

	return 0;
}

static int
macronix_qpp4b_post_sfdp_fixups(struct spi_nor *nor)
{
	/* PP_1_1_4_4B is supported but missing in 4BAIT. */
	struct spi_nor_flash_parameter *params = nor->params;

	params->hwcaps.mask |= SNOR_HWCAPS_PP_1_1_4;
	spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_1_1_4],
				SPINOR_OP_PP_1_1_4_4B, SNOR_PROTO_1_1_4);

	return 0;
}

static int
mx25l3255e_late_init_fixups(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	/*
	 * SFDP of MX25L3255E is JESD216, which does not include the Quad
	 * Enable bit Requirement in BFPT. As a result, during BFPT parsing
	 * the quad_enable mask is reset. Therefore, it is necessary to
	 * correct this setting by late_init.
	 */
	params->qe_mask[0] = BIT(6);
	params->qe_mask[1] = 0;

	/*
	 * In addition, MX25L3255E also supports 1-4-4 page program in 3-byte
	 * address mode. However, since the 3-byte address 1-4-4 page program
	 * is not defined in SFDP, it needs to be configured in late_init.
	 */
	params->hwcaps.mask |= SNOR_HWCAPS_PP_1_4_4;
	spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_1_4_4],
				SPINOR_OP_PP_1_4_4, SNOR_PROTO_1_4_4);

	return 0;
}

static int
mx25l12805d_4pp3b_post_sfdp_fixups(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	/*
	 * JEDEC ID 0xc22018 is shared by MX25L12805D (no SFDP, no 4PP) and
	 * MX25L12833F/MX25L12845G (support SFDP and 4PP in 3-byte mode).
	 * The legacy 05D lacks SFDP and will not execute this hook. For
	 * the newer flashes, 3-byte 1-4-4 PP is not defined in SFDP, so
	 * we safely enable it here.
	 */
	params->hwcaps.mask |= SNOR_HWCAPS_PP_1_4_4;
	spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_1_4_4],
				SPINOR_OP_PP_1_4_4, SNOR_PROTO_1_4_4);

	return 0;
}

static const struct spi_nor_fixups mx25l25635_fixups = {
	.post_bfpt = mx25l25635_post_bfpt_fixups,
	.post_sfdp = macronix_qpp4b_post_sfdp_fixups,
};

static const struct spi_nor_fixups macronix_qpp4b_fixups = {
	.post_sfdp = macronix_qpp4b_post_sfdp_fixups,
};

static const struct spi_nor_fixups mx25l3255e_fixups = {
	.late_init = mx25l3255e_late_init_fixups,
};

static const struct spi_nor_fixups mx25l12805d_4pp3b_fixups = {
	.post_sfdp = mx25l12805d_4pp3b_post_sfdp_fixups,
};

static const struct flash_info macronix_nor_parts[] = {
	{
		.id = SNOR_ID(0xc2, 0x20, 0x10),
		.name = "mx25l512e",
		.size = SZ_64K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x12),
		.name = "mx25l2005a",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x13),
		.name = "mx25l4005a",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x14),
		.name = "mx25l8005",
		.size = SZ_1M,
	}, {
		/* MX25L1606E */
		.id = SNOR_ID(0xc2, 0x20, 0x15),
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x16),
		.name = "mx25l3205d",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x17),
		.name = "mx25l6405d",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K,
	}, {
		/* MX25L12805D, MX25L12833F, MX25L12845G */
		.id = SNOR_ID(0xc2, 0x20, 0x18),
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_4BIT_BP,
		.no_sfdp_flags = SECT_4K,
	}, {
		/* MX25L25635E, MX25L25645G */
		.id = SNOR_ID(0xc2, 0x20, 0x19),
	}, {
		/* MX25L51245G, MX25L51273G, MX66L51235F */
		.id = SNOR_ID(0xc2, 0x20, 0x1a),
	}, {
		/* MX66L1G45G */
		.id = SNOR_ID(0xc2, 0x20, 0x1b),
	}, {
		/* MX66L2G45G */
		.id = SNOR_ID(0xc2, 0x20, 0x1c),
	}, {
		.id = SNOR_ID(0xc2, 0x23, 0x14),
		.name = "mx25v8035f",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x32),
		.name = "mx25u2033e",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x33),
		.name = "mx25u4035",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x34),
		.name = "mx25u8035",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x36),
		.name = "mx25u3235f",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x37),
		.name = "mx25u6435f",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x38),
		.name = "mx25u12835f",
		.size = SZ_16M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* MX25U51245G, MX25U51293G */
		.id = SNOR_ID(0xc2, 0x25, 0x3a),
	}, {
		/* MX66U1G45G, MX66U1G93G */
		.id = SNOR_ID(0xc2, 0x25, 0x3b),
	}, {
		/* MX66U2G45G */
		.id = SNOR_ID(0xc2, 0x25, 0x3c),
	}, {
		.id = SNOR_ID(0xc2, 0x26, 0x18),
		.name = "mx25l12855e",
		.size = SZ_16M,
	}, {
		.id = SNOR_ID(0xc2, 0x26, 0x19),
		.name = "mx25l25655e",
		.size = SZ_32M,
	}, {
		.id = SNOR_ID(0xc2, 0x26, 0x1b),
		.name = "mx66l1g55g",
		.size = SZ_128M,
		.no_sfdp_flags = SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x28, 0x15),
		.name = "mx25r1635f",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x28, 0x16),
		.name = "mx25r3235f",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* MX25UW51245G */
		.id = SNOR_ID(0xc2, 0x81, 0x3a),
		.n_banks = 4,
		.flags = SPI_NOR_RWW,
	}, {
		/* MX25L3255E */
		.id = SNOR_ID(0xc2, 0x9e, 0x16),
	},
	/*
	 * This spares us of adding new flash entries for flashes that can be
	 * initialized solely based on the SFDP data, but still need the
	 * manufacturer hooks to set parameters that can't be discovered at SFDP
	 * parsing time.
	 */
	{ .id = SNOR_ID(0xc2) }
};

static int macronix_nor_octal_dtr_en(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf, i;
	int ret;

	/* Use dummy cycles which is parse by SFDP and convert to bit pattern. */
	buf[0] = MXIC_NOR_REG_DC(nor->params->reads[SNOR_CMD_READ_8_8_8_DTR].num_wait_states);
	op = (struct spi_mem_op)MXIC_NOR_WR_CR2(MXIC_NOR_ADDR_CR2_DC, 1, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	/* Set the octal and DTR enable bits. */
	buf[0] = MXIC_NOR_REG_DOPI_EN;
	op = (struct spi_mem_op)MXIC_NOR_WR_CR2(MXIC_NOR_ADDR_CR2_MODE, 1, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_nor_read_id(nor, nor->addr_nbytes, 4, buf,
			      SNOR_PROTO_8_8_8_DTR);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID after enabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	/* Macronix SPI-NOR flash 8D-8D-8D read ID would get 6 bytes data A-A-B-B-C-C */
	for (i = 0; i < nor->info->id->len; i++)
		if (buf[i * 2] != buf[(i * 2) + 1] || buf[i * 2] != nor->info->id->bytes[i])
			return -EINVAL;

	return 0;
}

static int macronix_nor_octal_dtr_dis(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	/*
	 * The register is 1-byte wide, but 1-byte transactions are not
	 * allowed in 8D-8D-8D mode. Since there is no register at the
	 * next location, just initialize the value to 0 and let the
	 * transaction go on.
	 */
	buf[0] = MXIC_NOR_REG_SPI_EN;
	buf[1] = 0x0;
	op = (struct spi_mem_op)MXIC_NOR_WR_CR2(MXIC_NOR_ADDR_CR2_MODE, 2, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, SNOR_PROTO_8_8_8_DTR);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_nor_read_id(nor, 0, 0, buf, SNOR_PROTO_1_1_1);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID after disabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	if (memcmp(buf, nor->info->id->bytes, nor->info->id->len))
		return -EINVAL;

	return 0;
}

static int macronix_nor_set_octal_dtr(struct spi_nor *nor, bool enable)
{
	return enable ? macronix_nor_octal_dtr_en(nor) : macronix_nor_octal_dtr_dis(nor);
}

static void macronix_nor_default_init(struct spi_nor *nor)
{
	nor->params->qe_mask[0] = BIT(6);
	nor->params->qe_mask[1] = 0;
}

static int macronix_nor_late_init(struct spi_nor *nor)
{
	if (!nor->params->set_4byte_addr_mode)
		nor->params->set_4byte_addr_mode = spi_nor_set_4byte_addr_mode_en4b_ex4b;
	nor->params->set_octal_dtr = macronix_nor_set_octal_dtr;

	return 0;
}

static const struct spi_nor_fixups macronix_nor_fixups = {
	.default_init = macronix_nor_default_init,
	.late_init = macronix_nor_late_init,
};

static const struct spi_nor_fixup macronix_fixups[] = {
	{ .fixups = &macronix_nor_fixups },
	{ .id = SNOR_ID(0xc2, 0x20, 0x18), .fixups = &mx25l12805d_4pp3b_fixups },
	{ .id = SNOR_ID(0xc2, 0x20, 0x19), .fixups = &mx25l25635_fixups },
	{ .id = SNOR_ID(0xc2, 0x20, 0x1a), .fixups = &macronix_qpp4b_fixups,
	  .fixup_flags = SPI_NOR_4B_OPCODES },
	{ .id = SNOR_ID(0xc2, 0x20, 0x1b), .fixups = &macronix_qpp4b_fixups },
	{ .id = SNOR_ID(0xc2, 0x20, 0x1c), .fixups = &macronix_qpp4b_fixups },
	{ .id = SNOR_ID(0xc2, 0x25, 0x3a), .fixups = &macronix_qpp4b_fixups },
	{ .id = SNOR_ID(0xc2, 0x25, 0x3b), .fixups = &macronix_qpp4b_fixups },
	{ .id = SNOR_ID(0xc2, 0x25, 0x3c), .fixups = &macronix_qpp4b_fixups },
	{ .id = SNOR_ID(0xc2, 0x9e, 0x16), .fixups = &mx25l3255e_fixups },
};

const struct spi_nor_manufacturer spi_nor_macronix = {
	.name = "macronix",
	.parts = macronix_nor_parts,
	.nparts = ARRAY_SIZE(macronix_nor_parts),
	.fixups = macronix_fixups,
	.nfixups = ARRAY_SIZE(macronix_fixups),
};
