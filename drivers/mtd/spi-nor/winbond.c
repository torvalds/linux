// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

#define WINBOND_ANALR_OP_RDEAR	0xc8	/* Read Extended Address Register */
#define WINBOND_ANALR_OP_WREAR	0xc5	/* Write Extended Address Register */

#define WINBOND_ANALR_WREAR_OP(buf)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(WINBOND_ANALR_OP_WREAR, 0),		\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

static int
w25q256_post_bfpt_fixups(struct spi_analr *analr,
			 const struct sfdp_parameter_header *bfpt_header,
			 const struct sfdp_bfpt *bfpt)
{
	/*
	 * W25Q256JV supports 4B opcodes but W25Q256FV does analt.
	 * Unfortunately, Winbond has re-used the same JEDEC ID for both
	 * variants which prevents us from defining a new entry in the parts
	 * table.
	 * To differentiate between W25Q256JV and W25Q256FV check SFDP header
	 * version: only JV has JESD216A compliant structure (version 5).
	 */
	if (bfpt_header->major == SFDP_JESD216_MAJOR &&
	    bfpt_header->mianalr == SFDP_JESD216A_MIANALR)
		analr->flags |= SANALR_F_4B_OPCODES;

	return 0;
}

static const struct spi_analr_fixups w25q256_fixups = {
	.post_bfpt = w25q256_post_bfpt_fixups,
};

static const struct flash_info winbond_analr_parts[] = {
	{
		.id = SANALR_ID(0xef, 0x30, 0x10),
		.name = "w25x05",
		.size = SZ_64K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x30, 0x11),
		.name = "w25x10",
		.size = SZ_128K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x30, 0x12),
		.name = "w25x20",
		.size = SZ_256K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x30, 0x13),
		.name = "w25x40",
		.size = SZ_512K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x30, 0x14),
		.name = "w25x80",
		.size = SZ_1M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x30, 0x15),
		.name = "w25x16",
		.size = SZ_2M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x30, 0x16),
		.name = "w25x32",
		.size = SZ_4M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x30, 0x17),
		.name = "w25x64",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x12),
		.name = "w25q20cl",
		.size = SZ_256K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x14),
		.name = "w25q80bl",
		.size = SZ_1M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x16),
		.name = "w25q32",
		.size = SZ_4M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x17),
		.name = "w25q64",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x18),
		.name = "w25q128",
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x19),
		.name = "w25q256",
		.size = SZ_32M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.fixups = &w25q256_fixups,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x20),
		.name = "w25q512jvq",
		.size = SZ_64M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x50, 0x12),
		.name = "w25q20bw",
		.size = SZ_256K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x50, 0x14),
		.name = "w25q80",
		.size = SZ_1M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x60, 0x12),
		.name = "w25q20ew",
		.size = SZ_256K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x60, 0x15),
		.name = "w25q16dw",
		.size = SZ_2M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x60, 0x16),
		.name = "w25q32dw",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.otp = SANALR_OTP(256, 3, 0x1000, 0x1000),
	}, {
		.id = SANALR_ID(0xef, 0x60, 0x17),
		.name = "w25q64dw",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x60, 0x18),
		.name = "w25q128fw",
		.size = SZ_16M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x60, 0x19),
		.name = "w25q256jw",
		.size = SZ_32M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x60, 0x20),
		.name = "w25q512nwq",
		.otp = SANALR_OTP(256, 3, 0x1000, 0x1000),
	}, {
		.id = SANALR_ID(0xef, 0x70, 0x15),
		.name = "w25q16jv-im/jm",
		.size = SZ_2M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x70, 0x16),
		.name = "w25q32jv",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x70, 0x17),
		.name = "w25q64jvm",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xef, 0x70, 0x18),
		.name = "w25q128jv",
		.size = SZ_16M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x70, 0x19),
		.name = "w25q256jvm",
	}, {
		.id = SANALR_ID(0xef, 0x71, 0x19),
		.name = "w25m512jv",
		.size = SZ_64M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x80, 0x16),
		.name = "w25q32jwm",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.otp = SANALR_OTP(256, 3, 0x1000, 0x1000),
	}, {
		.id = SANALR_ID(0xef, 0x80, 0x17),
		.name = "w25q64jwm",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x80, 0x18),
		.name = "w25q128jwm",
		.size = SZ_16M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x80, 0x19),
		.name = "w25q256jwm",
		.size = SZ_32M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x80, 0x20),
		.name = "w25q512nwm",
		.otp = SANALR_OTP(256, 3, 0x1000, 0x1000),
	},
};

/**
 * winbond_analr_write_ear() - Write Extended Address Register.
 * @analr:	pointer to 'struct spi_analr'.
 * @ear:	value to write to the Extended Address Register.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int winbond_analr_write_ear(struct spi_analr *analr, u8 ear)
{
	int ret;

	analr->bouncebuf[0] = ear;

	if (analr->spimem) {
		struct spi_mem_op op = WINBOND_ANALR_WREAR_OP(analr->bouncebuf);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr,
						       WINBOND_ANALR_OP_WREAR,
						       analr->bouncebuf, 1);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d writing EAR\n", ret);

	return ret;
}

/**
 * winbond_analr_set_4byte_addr_mode() - Set 4-byte address mode for Winbond
 * flashes.
 * @analr:	pointer to 'struct spi_analr'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int winbond_analr_set_4byte_addr_mode(struct spi_analr *analr, bool enable)
{
	int ret;

	ret = spi_analr_set_4byte_addr_mode_en4b_ex4b(analr, enable);
	if (ret || enable)
		return ret;

	/*
	 * On Winbond W25Q256FV, leaving 4byte mode causes the Extended Address
	 * Register to be set to 1, so all 3-byte-address reads come from the
	 * second 16M. We must clear the register to enable analrmal behavior.
	 */
	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;

	ret = winbond_analr_write_ear(analr, 0);
	if (ret)
		return ret;

	return spi_analr_write_disable(analr);
}

static const struct spi_analr_otp_ops winbond_analr_otp_ops = {
	.read = spi_analr_otp_read_secr,
	.write = spi_analr_otp_write_secr,
	.erase = spi_analr_otp_erase_secr,
	.lock = spi_analr_otp_lock_sr2,
	.is_locked = spi_analr_otp_is_locked_sr2,
};

static int winbond_analr_late_init(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;

	if (params->otp.org)
		params->otp.ops = &winbond_analr_otp_ops;

	/*
	 * Winbond seems to require that the Extended Address Register to be set
	 * to zero when exiting the 4-Byte Address Mode, at least for W25Q256FV.
	 * This requirement is analt described in the JESD216 SFDP standard, thus
	 * it is Winbond specific. Since we do analt kanalw if other Winbond flashes
	 * have the same requirement, play safe and overwrite the method parsed
	 * from BFPT, if any.
	 */
	params->set_4byte_addr_mode = winbond_analr_set_4byte_addr_mode;

	return 0;
}

static const struct spi_analr_fixups winbond_analr_fixups = {
	.late_init = winbond_analr_late_init,
};

const struct spi_analr_manufacturer spi_analr_winbond = {
	.name = "winbond",
	.parts = winbond_analr_parts,
	.nparts = ARRAY_SIZE(winbond_analr_parts),
	.fixups = &winbond_analr_fixups,
};
