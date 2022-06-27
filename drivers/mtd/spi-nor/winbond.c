// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define WINBOND_NOR_OP_RDEAR	0xc8	/* Read Extended Address Register */
#define WINBOND_NOR_OP_WREAR	0xc5	/* Write Extended Address Register */

#define WINBOND_NOR_WREAR_OP(buf)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(WINBOND_NOR_OP_WREAR, 0),		\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

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
		nor->flags |= SNOR_F_4B_OPCODES;

	return 0;
}

static const struct spi_nor_fixups w25q256_fixups = {
	.post_bfpt = w25q256_post_bfpt_fixups,
};

static const struct flash_info winbond_nor_parts[] = {
	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
	{ "w25x05", INFO(0xef3010, 0, 64 * 1024,  1)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25x10", INFO(0xef3011, 0, 64 * 1024,  2)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25x20", INFO(0xef3012, 0, 64 * 1024,  4)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25x40", INFO(0xef3013, 0, 64 * 1024,  8)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25x80", INFO(0xef3014, 0, 64 * 1024,  16)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25x16", INFO(0xef3015, 0, 64 * 1024,  32)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q16dw", INFO(0xef6015, 0, 64 * 1024,  32)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25x32", INFO(0xef3016, 0, 64 * 1024,  64)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q16jv-im/jm", INFO(0xef7015, 0, 64 * 1024,  32)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q20cl", INFO(0xef4012, 0, 64 * 1024,  4)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q20bw", INFO(0xef5012, 0, 64 * 1024,  4)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q20ew", INFO(0xef6012, 0, 64 * 1024,  4)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q32", INFO(0xef4016, 0, 64 * 1024,  64)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q32dw", INFO(0xef6016, 0, 64 * 1024,  64)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		OTP_INFO(256, 3, 0x1000, 0x1000) },
	{ "w25q32jv", INFO(0xef7016, 0, 64 * 1024,  64)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q32jwm", INFO(0xef8016, 0, 64 * 1024,  64)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		OTP_INFO(256, 3, 0x1000, 0x1000) },
	{ "w25q64jwm", INFO(0xef8017, 0, 64 * 1024, 128)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q128jwm", INFO(0xef8018, 0, 64 * 1024, 256)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q256jwm", INFO(0xef8019, 0, 64 * 1024, 512)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25x64", INFO(0xef3017, 0, 64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q64", INFO(0xef4017, 0, 64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q64dw", INFO(0xef6017, 0, 64 * 1024, 128)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q64jvm", INFO(0xef7017, 0, 64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q128fw", INFO(0xef6018, 0, 64 * 1024, 256)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q128jv", INFO(0xef7018, 0, 64 * 1024, 256)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25q80", INFO(0xef5014, 0, 64 * 1024,  16)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q80bl", INFO(0xef4014, 0, 64 * 1024,  16)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q128", INFO(0xef4018, 0, 64 * 1024, 256)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "w25q256", INFO(0xef4019, 0, 64 * 1024, 512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		.fixups = &w25q256_fixups },
	{ "w25q256jvm", INFO(0xef7019, 0, 64 * 1024, 512)
		PARSE_SFDP },
	{ "w25q256jw", INFO(0xef6019, 0, 64 * 1024, 512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "w25m512jv", INFO(0xef7119, 0, 64 * 1024, 1024)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ |
			      SPI_NOR_DUAL_READ) },
	{ "w25q512nwm", INFO(0xef8020, 0, 64 * 1024, 1024)
		PARSE_SFDP
		OTP_INFO(256, 3, 0x1000, 0x1000) },
	{ "w25q512jvq", INFO(0xef4020, 0, 64 * 1024, 1024)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
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

	ret = spi_nor_set_4byte_addr_mode(nor, enable);
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

static void winbond_nor_default_init(struct spi_nor *nor)
{
	nor->params->set_4byte_addr_mode = winbond_nor_set_4byte_addr_mode;
}

static void winbond_nor_late_init(struct spi_nor *nor)
{
	if (nor->params->otp.org->n_regions)
		nor->params->otp.ops = &winbond_nor_otp_ops;
}

static const struct spi_nor_fixups winbond_nor_fixups = {
	.default_init = winbond_nor_default_init,
	.late_init = winbond_nor_late_init,
};

const struct spi_nor_manufacturer spi_nor_winbond = {
	.name = "winbond",
	.parts = winbond_nor_parts,
	.nparts = ARRAY_SIZE(winbond_nor_parts),
	.fixups = &winbond_nor_fixups,
};
