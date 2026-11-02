// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define WINBOND_NOR_OP_RDEAR	0xc8	/* Read Extended Address Register */
#define WINBOND_NOR_OP_WREAR	0xc5	/* Write Extended Address Register */
#define WINBOND_NOR_OP_SELDIE	0xc2	/* Select active die */

#define WINBOND_NOR_WREAR_OP(buf)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(WINBOND_NOR_OP_WREAR, 0),		\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

#define WINBOND_NOR_SELDIE_OP(buf)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(WINBOND_NOR_OP_SELDIE, 0),		\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

static bool is_w25qxxrv(const struct spi_nor *nor)
{
	struct sfdp_header *sfdp_h = spi_nor_sfdp_get_header(nor);

	/*
	 * W25QxxRV chips re-use the same ID as the W25QxxJV family.
	 *
	 * Chips are very similar, W25QxxRV brings mostly performance and power
	 * consumption improvements. The RV family does not require the multi
	 * die fixup.
	 *
	 * They can be distinguished based on their SFDP minor revision:
	 * W25QxxJV:        JESD216A, minor revision == 05h
	 * W25Q512/01/02JV: JESD216B, minor revision == 06h
	 * W25QxxRV:        JESD216F, minor revision >= 0Ah
	 */
	return sfdp_h->minor >= SFDP_JESD216F_MINOR;
}

static bool is_zd25q128c(const struct spi_nor *nor,
			 const struct sfdp_parameter_header *bfpt_header)
{
	/*
	 * Zetta ZD25Q128C is a clone of the Winbond device. But the encoded
	 * size is really wrong. It seems that they confused Mbit with MiB.
	 * Thus the flash is discovered as a 2MiB device.
	 */
	return bfpt_header->major == SFDP_JESD216_MAJOR &&
	       bfpt_header->minor == SFDP_JESD216_MINOR &&
	       nor->params->size == SZ_2M &&
	       nor->params->erase_map.regions[0].size == SZ_2M;
}

/*
 * Since SFDP is populated after ->default_init(), the match functions using
 * nor->sfdp as discriminant cannot be used for this specific early fixup.
 */
static bool winbond_jv_match(const struct spi_nor *nor)
{
	return !nor->sfdp || !is_w25qxxrv(nor);
}

static bool winbond_rv_match(const struct spi_nor *nor)
{
	return nor->sfdp && is_w25qxxrv(nor);
}

static bool is_w25qxxpw(const struct spi_nor *nor)
{
	struct sfdp_header *sfdp_h = spi_nor_sfdp_get_header(nor);

	/*
	 * W25QxxPW chips re-use the same ID as the W25QxxJW/NW family.
	 *
	 * Chips are very similar, W25QxxPW brings mostly performance and power
	 * consumption improvements. One key difference in behaviour is the
	 * automatic 16-byte based error correction.
	 *
	 * They can be distinguished based on their SFDP minor revision:
	 * W25QxxJW:       JESD216B, minor revision == 06h
	 * W25Q51/01/02NW: JESD216B, minor revision == 06h
	 * W25QxxPW:       JESD216F, minor revision >= 0Ah
	 */
	return sfdp_h->minor >= SFDP_JESD216F_MINOR;
}

static bool winbond_pw_match(const struct spi_nor *nor)
{
	return nor->sfdp && is_w25qxxpw(nor);
}

static bool winbond_pw_with_ecc_match(const struct spi_nor *nor)
{
	const struct spi_nor_id *id = nor->info->id;

	if (!winbond_pw_match(nor))
		return false;

	/* W25QxxPW densities <= 32 Mbit (id[2] <= 0x16) do not have built-in ECC support */
	return id->len == 3 && id->bytes[2] >= 0x17;
}

static int
w25q128_post_bfpt_fixups(struct spi_nor *nor,
			 const struct sfdp_parameter_header *bfpt_header,
			 const struct sfdp_bfpt *bfpt)
{
	if (is_zd25q128c(nor, bfpt_header) || winbond_rv_match(nor)) {
		nor->params->size = SZ_16M;
		nor->params->erase_map.regions[0].size = SZ_16M;
	}

	return 0;
}

static const struct spi_nor_fixups w25q128_fixups = {
	.post_bfpt = w25q128_post_bfpt_fixups,
};

static int
w25q256_post_bfpt_fixups(struct spi_nor *nor,
			 const struct sfdp_parameter_header *bfpt_header,
			 const struct sfdp_bfpt *bfpt)
{
	/*
	 * W25Q256JV supports 4B opcodes but W25Q256FV does not.
	 * Unfortunately, Winbond has re-used the same JEDEC ID for both
	 * variants which prevents us from defining a new entry in the parts
	 * table.
	 * To differentiate between W25Q256JV and W25Q256FV check SFDP header
	 * version: only JV has JESD216A compliant structure (version 5).
	 */
	if (bfpt_header->major == SFDP_JESD216_MAJOR &&
	    bfpt_header->minor == SFDP_JESD216A_MINOR)
		nor->params->flags |= SNOR_F_4B_OPCODES;

	return 0;
}

static const struct spi_nor_fixups w25q256_fixups = {
	.post_bfpt = w25q256_post_bfpt_fixups,
};

/**
 * winbond_nor_select_die() - Set active die.
 * @nor:	pointer to 'struct spi_nor'.
 * @die:	die to set active.
 *
 * Certain Winbond chips feature more than a single die. This is mostly hidden
 * to the user, except that some chips may experience time deviation when
 * modifying the status bits between dies, which in some corner cases may
 * produce problematic races. Being able to explicitly select a die to check its
 * state in this case may be useful.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int winbond_nor_select_die(struct spi_nor *nor, u8 die)
{
	int ret;

	nor->bouncebuf[0] = die;

	if (nor->spimem) {
		struct spi_mem_op op = WINBOND_NOR_SELDIE_OP(nor->bouncebuf);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor,
						       WINBOND_NOR_OP_SELDIE,
						       nor->bouncebuf, 1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d selecting die %d\n", ret, die);

	return ret;
}

static int winbond_nor_multi_die_ready(struct spi_nor *nor)
{
	int ret, i;

	for (i = 0; i < nor->params->n_dice; i++) {
		ret = winbond_nor_select_die(nor, i);
		if (ret)
			return ret;

		ret = spi_nor_sr_ready(nor);
		if (ret <= 0)
			return ret;
	}

	return 1;
}

static int
winbond_nor_multi_die_post_sfdp_fixups(struct spi_nor *nor)
{
	/*
	 * SFDP supports dice numbers, but this information is only available in
	 * optional additional tables which are not provided by these chips.
	 * Dice number has an impact though, because these devices need extra
	 * care when reading the busy bit.
	 */
	nor->params->n_dice = nor->params->size / SZ_64M;
	nor->params->ready = winbond_nor_multi_die_ready;

	return 0;
}

static const struct spi_nor_fixups winbond_nor_multi_die_fixups = {
	.post_sfdp = winbond_nor_multi_die_post_sfdp_fixups,
};

static int winbond_nor_partname_post_sfdp_fixups(struct spi_nor *nor)
{
	/*
	 * W25QxxRV parts re-use the JEDEC IDs of the JV family. Their name
	 * being a legacy field, it is kept for the already established JV parts
	 * but must not be exposed by the newer RV ones.
	 */
	nor->partname = NULL;

	return 0;
}

static const struct spi_nor_fixups winbond_nor_partname_fixups = {
	.post_sfdp = winbond_nor_partname_post_sfdp_fixups,
};

static int winbond_nor_ecc_configuration_post_sfdp_fixups(struct spi_nor *nor)
{
	/*
	 * PW chips feature automatic error correction. Non 16-byte aligned
	 * writes work, but disable error correction on the region until next erase.
	 */
	nor->params->writesize = 16;
	nor->params->flags |= SNOR_F_ECC;

	return 0;
}

static const struct spi_nor_fixups winbond_nor_ecc_configuration_fixups = {
	.post_sfdp = winbond_nor_ecc_configuration_post_sfdp_fixups,
};

static const struct flash_info winbond_nor_parts[] = {
	{
		.id = SNOR_ID(0xef, 0x30, 0x10),
		.name = "w25x05",
		.size = SZ_64K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x30, 0x11),
		.name = "w25x10",
		.size = SZ_128K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x30, 0x12),
		.name = "w25x20",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x30, 0x13),
		.name = "w25x40",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x30, 0x14),
		.name = "w25x80",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x30, 0x15),
		.name = "w25x16",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x30, 0x16),
		.name = "w25x32",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x30, 0x17),
		.name = "w25x64",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x40, 0x12),
		.name = "w25q20cl",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x40, 0x14),
		.name = "w25q80bl",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K,
	}, {
		/* W25Q32JV-Q/N, W25Q32RV-Q/N */
		.id = SNOR_ID(0xef, 0x40, 0x16),
		.name = "w25q32",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q64JV-Q/N, W25Q64RV-Q/N */
		.id = SNOR_ID(0xef, 0x40, 0x17),
		.name = "w25q64",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q128JV-Q/N, W25Q12RV-Q/N */
		.id = SNOR_ID(0xef, 0x40, 0x18),
		/* Flavors w/ and w/o SFDP. */
		.name = "w25q128",
		.size = SZ_16M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q256JV-Q/N, W25Q25RV-Q/N */
		.id = SNOR_ID(0xef, 0x40, 0x19),
		.name = "w25q256",
		.size = SZ_32M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			 SPI_NOR_TB_SR_BIT6 | SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q512JV-Q/N, W25Q51RV-Q/N */
		.id = SNOR_ID(0xef, 0x40, 0x20),
		.name = "w25q512jvq",
		.size = SZ_64M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q01JV-Q/N, W25Q01RV-Q/N */
		.id = SNOR_ID(0xef, 0x40, 0x21),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q02RV-Q/N */
		.id = SNOR_ID(0xef, 0x40, 0x22),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		.id = SNOR_ID(0xef, 0x50, 0x12),
		.name = "w25q20bw",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x50, 0x14),
		.name = "w25q80",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x60, 0x12),
		.name = "w25q20ew",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xef, 0x60, 0x15),
		.name = "w25q16dw",
		.size = SZ_2M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q32DW-P/G, W25Q32FW-G/Q, W25Q32JW-Q/N, W25Q33PW-Q/N */
		.id = SNOR_ID(0xef, 0x60, 0x16),
		.name = "w25q32dw",
		.size = SZ_4M,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.otp = SNOR_OTP(256, 3, 0x1000, 0x1000),
	}, {
		/* W25Q64DW-P/G, W25Q64FW-G/Q, W25Q64JW-Q/N, W25Q64PW-Q/N */
		.id = SNOR_ID(0xef, 0x60, 0x17),
		.name = "w25q64dw",
		.size = SZ_8M,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q128FW-G/Q, W25Q128JW-Q/N, W25Q12PW-Q/N */
		.id = SNOR_ID(0xef, 0x60, 0x18),
		.name = "w25q128fw",
		.size = SZ_16M,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q256JW-Q/N, W25Q25PW-Q/N */
		.id = SNOR_ID(0xef, 0x60, 0x19),
		.name = "w25q256jw",
		.size = SZ_32M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q512NW-Q/N */
		.id = SNOR_ID(0xef, 0x60, 0x20),
		.name = "w25q512nwq",
		.otp = SNOR_OTP(256, 3, 0x1000, 0x1000),
	}, {
		/* W25Q01NW-Q/N */
		.id = SNOR_ID(0xef, 0x60, 0x21),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q16JV-M */
		.id = SNOR_ID(0xef, 0x70, 0x15),
		.name = "w25q16jv-im/jm",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q32JV-M, W25Q32RV-M */
		.id = SNOR_ID(0xef, 0x70, 0x16),
		.name = "w25q32jv",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q64JV-M, W25Q64RV-M */
		.id = SNOR_ID(0xef, 0x70, 0x17),
		.name = "w25q64jvm",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q128JV-M, W25Q12RV-M */
		.id = SNOR_ID(0xef, 0x70, 0x18),
		.name = "w25q128jv",
		.size = SZ_16M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q256JV-M, W25Q25RV-M */
		.id = SNOR_ID(0xef, 0x70, 0x19),
		.name = "w25q256jvm",
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			 SPI_NOR_TB_SR_BIT6 | SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q512JV-M, W25Q51RV-M */
		.id = SNOR_ID(0xef, 0x70, 0x20),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q01JV-M, W25Q01RV-M */
		.id = SNOR_ID(0xef, 0x70, 0x21),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q02JV-M, W25Q02RV-M */
		.id = SNOR_ID(0xef, 0x70, 0x22),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		.id = SNOR_ID(0xef, 0x71, 0x19),
		.name = "w25m512jv",
		.size = SZ_64M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q32JW-M, W25Q33PW-M */
		.id = SNOR_ID(0xef, 0x80, 0x16),
		.name = "w25q32jwm",
		.size = SZ_4M,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.otp = SNOR_OTP(256, 3, 0x1000, 0x1000),
	}, {
		/* W25Q64JW-M, W25Q64PW-M */
		.id = SNOR_ID(0xef, 0x80, 0x17),
		.name = "w25q64jwm",
		.size = SZ_8M,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q128JW-M, W25Q12PW-M */
		.id = SNOR_ID(0xef, 0x80, 0x18),
		.name = "w25q128jwm",
		.size = SZ_16M,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q256JW-M, W25Q25PW-M */
		.id = SNOR_ID(0xef, 0x80, 0x19),
		.name = "w25q256jwm",
		.size = SZ_32M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* W25Q512NW-M */
		.id = SNOR_ID(0xef, 0x80, 0x20),
		.name = "w25q512nwm",
		.otp = SNOR_OTP(256, 3, 0x1000, 0x1000),
	}, {
		/* W25Q01NW-M */
		.id = SNOR_ID(0xef, 0x80, 0x21),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25Q02NW-M */
		.id = SNOR_ID(0xef, 0x80, 0x22),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25H512NW-M */
		.id = SNOR_ID(0xef, 0xa0, 0x20),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25H01NW-M */
		.id = SNOR_ID(0xef, 0xa0, 0x21),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/* W25H02NW-M */
		.id = SNOR_ID(0xef, 0xa0, 0x22),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			 SPI_NOR_4BIT_BP | SPI_NOR_HAS_CMP,
	}, {
		/*
		 * Catch all entry to make sure all chips solely relying
		 * on SFDP still go through the manufacturer hooks.
		 */
		.id = SNOR_ID(0xef),
	}
};

/**
 * winbond_nor_write_ear() - Write Extended Address Register.
 * @nor:	pointer to 'struct spi_nor'.
 * @ear:	value to write to the Extended Address Register.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int winbond_nor_write_ear(struct spi_nor *nor, u8 ear)
{
	int ret;

	nor->bouncebuf[0] = ear;

	if (nor->spimem) {
		struct spi_mem_op op = WINBOND_NOR_WREAR_OP(nor->bouncebuf);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor,
						       WINBOND_NOR_OP_WREAR,
						       nor->bouncebuf, 1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d writing EAR\n", ret);

	return ret;
}

/**
 * winbond_nor_set_4byte_addr_mode() - Set 4-byte address mode for Winbond
 * flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int winbond_nor_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
{
	int ret;

	ret = spi_nor_set_4byte_addr_mode_en4b_ex4b(nor, enable);
	if (ret || enable)
		return ret;

	/*
	 * On Winbond W25Q256FV, leaving 4byte mode causes the Extended Address
	 * Register to be set to 1, so all 3-byte-address reads come from the
	 * second 16M. We must clear the register to enable normal behavior.
	 */
	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = winbond_nor_write_ear(nor, 0);
	if (ret)
		return ret;

	return spi_nor_write_disable(nor);
}

static const struct spi_nor_otp_ops winbond_nor_otp_ops = {
	.read = spi_nor_otp_read_secr,
	.write = spi_nor_otp_write_secr,
	.erase = spi_nor_otp_erase_secr,
	.lock = spi_nor_otp_lock_sr2,
	.is_locked = spi_nor_otp_is_locked_sr2,
};

static int winbond_nor_late_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	if (params->otp.org)
		params->otp.ops = &winbond_nor_otp_ops;

	/*
	 * Winbond seems to require that the Extended Address Register to be set
	 * to zero when exiting the 4-Byte Address Mode, at least for W25Q256FV.
	 * This requirement is not described in the JESD216 SFDP standard, thus
	 * it is Winbond specific. Since we do not know if other Winbond flashes
	 * have the same requirement, play safe and overwrite the method parsed
	 * from BFPT, if any.
	 */
	params->set_4byte_addr_mode = winbond_nor_set_4byte_addr_mode;

	/*
	 * All W25Q/W25H chips do set the BFPT_DWORD15_QER_SR2_BIT1_NO_RD bit in
	 * their SFDP tables. The historical spi-nor assumption in this case has
	 * been to declare CR reads as unsupported, whereas the Jedec
	 * specification doesn't clearly state that. In practice, all these
	 * chips do support reading back the CR, which is needed for SWP support,
	 * so make sure that capability remains enabled.
	 * In practice, only exclude the old W25X family (JEDEC ID: EF 30 xx)
	 * which actually does not support this feature.
	 */
	if (nor->id[1] > 0x30)
		params->opcodes.read_sr2 = SPINOR_OP_RDCR;

	/*
	 * Winbond has reused many IDs, up to four times at this
	 * stage. In general, most of the chips with SFDP support are
	 * correctly described by the ID table, but the non-SFDP chips,
	 * however, are known to not feature as many capabilities. Make
	 * sure we filter out those capabilities to keep backward
	 * compatibility with these devices manufactured until ~2016.
	 */
	if (!nor->sfdp) {
		struct spi_nor_flash_parameter *p = nor->params;

		/* SPI_NOR_QUAD_PP was unsupported */
		p->hwcaps.mask &= ~SNOR_HWCAPS_PP_1_1_4;
		spi_nor_set_pp_settings(&p->page_programs[SNOR_CMD_PP_1_1_4], 0, 0);

		/* SPI_NOR_HAS_CMP was unsupported */
		nor->params->flags &= ~SNOR_F_HAS_SR2_CMP_BIT6;
	}

	return 0;
}

static const struct spi_nor_fixups winbond_nor_fixups = {
	.late_init = winbond_nor_late_init,
};

static const struct spi_nor_fixup winbond_fixups[] = {
	{ .fixups = &winbond_nor_fixups },
	{ .id = SNOR_ID(0xef, 0x40, 0x18), .fixups = &w25q128_fixups },
	{ .id = SNOR_ID(0xef, 0x40, 0x19), .fixups = &w25q256_fixups },
	{ .id = SNOR_ID(0xef, 0x40), .match = winbond_rv_match,
	  .fixups = &winbond_nor_partname_fixups },
	{ .id = SNOR_ID(0xef, 0x40, 0x21), .match = winbond_jv_match,
	  .fixups = &winbond_nor_multi_die_fixups },
	{ .id = SNOR_ID(0xef, 0x40, 0x22), .match = winbond_jv_match,
	  .fixups = &winbond_nor_multi_die_fixups },
	{ .id = SNOR_ID(0xef, 0x60), .match = winbond_pw_with_ecc_match,
	  .fixups = &winbond_nor_ecc_configuration_fixups },
	{ .id = SNOR_ID(0xef, 0x60), .match = winbond_pw_match,
	  .fixups = &winbond_nor_partname_fixups },
	{ .id = SNOR_ID(0xef, 0x70), .match = winbond_rv_match,
	  .fixups = &winbond_nor_partname_fixups },
	{ .id = SNOR_ID(0xef, 0x70, 0x18), .fixups = &w25q128_fixups },
	{ .id = SNOR_ID(0xef, 0x70, 0x21), .match = winbond_jv_match,
	  .fixups = &winbond_nor_multi_die_fixups },
	{ .id = SNOR_ID(0xef, 0x70, 0x22), .match = winbond_jv_match,
	  .fixups = &winbond_nor_multi_die_fixups },
	{ .id = SNOR_ID(0xef, 0x80), .match = winbond_pw_with_ecc_match,
	  .fixups = &winbond_nor_ecc_configuration_fixups },
	{ .id = SNOR_ID(0xef, 0x80), .match = winbond_pw_match,
	  .fixups = &winbond_nor_partname_fixups },
};

const struct spi_nor_manufacturer spi_nor_winbond = {
	.name = "winbond",
	.parts = winbond_nor_parts,
	.nparts = ARRAY_SIZE(winbond_nor_parts),
	.fixups = winbond_fixups,
	.nfixups = ARRAY_SIZE(winbond_fixups),
};
