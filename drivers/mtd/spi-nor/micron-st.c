// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

/* flash_info mfr_flag. Used to read proprietary FSR register. */
#define USE_FSR		BIT(0)

#define SPINOR_OP_RDFSR		0x70	/* Read flag status register */
#define SPINOR_OP_CLFSR		0x50	/* Clear flag status register */
#define SPINOR_OP_MT_DTR_RD	0xfd	/* Fast Read opcode in DTR mode */
#define SPINOR_OP_MT_RD_ANY_REG	0x85	/* Read volatile register */
#define SPINOR_OP_MT_WR_ANY_REG	0x81	/* Write volatile register */
#define SPINOR_REG_MT_CFR0V	0x00	/* For setting octal DTR mode */
#define SPINOR_REG_MT_CFR1V	0x01	/* For setting dummy cycles */
#define SPINOR_REG_MT_CFR1V_DEF	0x1f	/* Default dummy cycles */
#define SPINOR_MT_OCT_DTR	0xe7	/* Enable Octal DTR. */
#define SPINOR_MT_EXSPI		0xff	/* Enable Extended SPI (default) */

/* Flag Status Register bits */
#define FSR_READY		BIT(7)	/* Device status, 0 = Busy, 1 = Ready */
#define FSR_E_ERR		BIT(5)	/* Erase operation status */
#define FSR_P_ERR		BIT(4)	/* Program operation status */
#define FSR_PT_ERR		BIT(1)	/* Protection error bit */

/* Micron ST SPI NOR flash operations. */
#define MICRON_ST_NOR_WR_ANY_REG_OP(naddr, addr, ndata, buf)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MT_WR_ANY_REG, 0),		\
		   SPI_MEM_OP_ADDR(naddr, addr, 0),			\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(ndata, buf, 0))

#define MICRON_ST_RDFSR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDFSR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define MICRON_ST_CLFSR_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_CLFSR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

static int micron_st_nor_octal_dtr_en(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;
	u8 addr_mode_nbytes = nor->params->addr_mode_nbytes;

	/* Use 20 dummy cycles for memory array reads. */
	*buf = 20;
	op = (struct spi_mem_op)
		MICRON_ST_NOR_WR_ANY_REG_OP(addr_mode_nbytes,
					    SPINOR_REG_MT_CFR1V, 1, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	buf[0] = SPINOR_MT_OCT_DTR;
	op = (struct spi_mem_op)
		MICRON_ST_NOR_WR_ANY_REG_OP(addr_mode_nbytes,
					    SPINOR_REG_MT_CFR0V, 1, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_nor_read_id(nor, 0, 8, buf, SNOR_PROTO_8_8_8_DTR);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID after enabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	if (memcmp(buf, nor->info->id, nor->info->id_len))
		return -EINVAL;

	return 0;
}

static int micron_st_nor_octal_dtr_dis(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	/*
	 * The register is 1-byte wide, but 1-byte transactions are not allowed
	 * in 8D-8D-8D mode. The next register is the dummy cycle configuration
	 * register. Since the transaction needs to be at least 2 bytes wide,
	 * set the next register to its default value. This also makes sense
	 * because the value was changed when enabling 8D-8D-8D mode, it should
	 * be reset when disabling.
	 */
	buf[0] = SPINOR_MT_EXSPI;
	buf[1] = SPINOR_REG_MT_CFR1V_DEF;
	op = (struct spi_mem_op)
		MICRON_ST_NOR_WR_ANY_REG_OP(nor->addr_nbytes,
					    SPINOR_REG_MT_CFR0V, 2, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, SNOR_PROTO_8_8_8_DTR);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_nor_read_id(nor, 0, 0, buf, SNOR_PROTO_1_1_1);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID after disabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	if (memcmp(buf, nor->info->id, nor->info->id_len))
		return -EINVAL;

	return 0;
}

static int micron_st_nor_octal_dtr_enable(struct spi_nor *nor, bool enable)
{
	return enable ? micron_st_nor_octal_dtr_en(nor) :
			micron_st_nor_octal_dtr_dis(nor);
}

static void mt35xu512aba_default_init(struct spi_nor *nor)
{
	nor->params->octal_dtr_enable = micron_st_nor_octal_dtr_enable;
}

static void mt35xu512aba_post_sfdp_fixup(struct spi_nor *nor)
{
	/* Set the Fast Read settings. */
	nor->params->hwcaps.mask |= SNOR_HWCAPS_READ_8_8_8_DTR;
	spi_nor_set_read_settings(&nor->params->reads[SNOR_CMD_READ_8_8_8_DTR],
				  0, 20, SPINOR_OP_MT_DTR_RD,
				  SNOR_PROTO_8_8_8_DTR);

	nor->cmd_ext_type = SPI_NOR_EXT_REPEAT;
	nor->params->rdsr_dummy = 8;
	nor->params->rdsr_addr_nbytes = 0;

	/*
	 * The BFPT quad enable field is set to a reserved value so the quad
	 * enable function is ignored by spi_nor_parse_bfpt(). Make sure we
	 * disable it.
	 */
	nor->params->quad_enable = NULL;
}

static const struct spi_nor_fixups mt35xu512aba_fixups = {
	.default_init = mt35xu512aba_default_init,
	.post_sfdp = mt35xu512aba_post_sfdp_fixup,
};

static const struct flash_info micron_nor_parts[] = {
	{ "mt35xu512aba", INFO(0x2c5b1a, 0, 128 * 1024, 512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_OCTAL_READ |
			   SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE)
		MFR_FLAGS(USE_FSR)
		.fixups = &mt35xu512aba_fixups
	},
	{ "mt35xu02g", INFO(0x2c5b1c, 0, 128 * 1024, 2048)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_OCTAL_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		MFR_FLAGS(USE_FSR)
	},
};

static const struct flash_info st_nor_parts[] = {
	{ "n25q016a",	 INFO(0x20bb15, 0, 64 * 1024,   32)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q032",	 INFO(0x20ba16, 0, 64 * 1024,   64)
		NO_SFDP_FLAGS(SPI_NOR_QUAD_READ) },
	{ "n25q032a",	 INFO(0x20bb16, 0, 64 * 1024,   64)
		NO_SFDP_FLAGS(SPI_NOR_QUAD_READ) },
	{ "n25q064",     INFO(0x20ba17, 0, 64 * 1024,  128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q064a",    INFO(0x20bb17, 0, 64 * 1024,  128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q128a11",  INFO(0x20bb18, 0, 64 * 1024,  256)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
		      SPI_NOR_BP3_SR_BIT6)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "n25q128a13",  INFO(0x20ba18, 0, 64 * 1024,  256)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
		      SPI_NOR_BP3_SR_BIT6)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "mt25ql256a",  INFO6(0x20ba19, 0x104400, 64 * 1024,  512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		MFR_FLAGS(USE_FSR)
	},
	{ "n25q256a",    INFO(0x20ba19, 0, 64 * 1024,  512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "mt25qu256a",  INFO6(0x20bb19, 0x104400, 64 * 1024,  512)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
		      SPI_NOR_BP3_SR_BIT6)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		MFR_FLAGS(USE_FSR)
	},
	{ "n25q256ax1",  INFO(0x20bb19, 0, 64 * 1024,  512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "mt25ql512a",  INFO6(0x20ba20, 0x104400, 64 * 1024, 1024)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		MFR_FLAGS(USE_FSR)
	},
	{ "n25q512ax3",  INFO(0x20ba20, 0, 64 * 1024, 1024)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
		      SPI_NOR_BP3_SR_BIT6)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "mt25qu512a",  INFO6(0x20bb20, 0x104400, 64 * 1024, 1024)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		MFR_FLAGS(USE_FSR)
	},
	{ "n25q512a",    INFO(0x20bb20, 0, 64 * 1024, 1024)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
		      SPI_NOR_BP3_SR_BIT6)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "n25q00",      INFO(0x20ba21, 0, 64 * 1024, 2048)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
		      SPI_NOR_BP3_SR_BIT6 | NO_CHIP_ERASE)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "n25q00a",     INFO(0x20bb21, 0, 64 * 1024, 2048)
		FLAGS(NO_CHIP_ERASE)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "mt25ql02g",   INFO(0x20ba22, 0, 64 * 1024, 4096)
		FLAGS(NO_CHIP_ERASE)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},
	{ "mt25qu02g",   INFO(0x20bb22, 0, 64 * 1024, 4096)
		FLAGS(NO_CHIP_ERASE)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_FSR)
	},

	{ "m25p05",  INFO(0x202010,  0,  32 * 1024,   2) },
	{ "m25p10",  INFO(0x202011,  0,  32 * 1024,   4) },
	{ "m25p20",  INFO(0x202012,  0,  64 * 1024,   4) },
	{ "m25p40",  INFO(0x202013,  0,  64 * 1024,   8) },
	{ "m25p80",  INFO(0x202014,  0,  64 * 1024,  16) },
	{ "m25p16",  INFO(0x202015,  0,  64 * 1024,  32) },
	{ "m25p32",  INFO(0x202016,  0,  64 * 1024,  64) },
	{ "m25p64",  INFO(0x202017,  0,  64 * 1024, 128) },
	{ "m25p128", INFO(0x202018,  0, 256 * 1024,  64) },

	{ "m25p05-nonjedec",  INFO(0, 0,  32 * 1024,   2) },
	{ "m25p10-nonjedec",  INFO(0, 0,  32 * 1024,   4) },
	{ "m25p20-nonjedec",  INFO(0, 0,  64 * 1024,   4) },
	{ "m25p40-nonjedec",  INFO(0, 0,  64 * 1024,   8) },
	{ "m25p80-nonjedec",  INFO(0, 0,  64 * 1024,  16) },
	{ "m25p16-nonjedec",  INFO(0, 0,  64 * 1024,  32) },
	{ "m25p32-nonjedec",  INFO(0, 0,  64 * 1024,  64) },
	{ "m25p64-nonjedec",  INFO(0, 0,  64 * 1024, 128) },
	{ "m25p128-nonjedec", INFO(0, 0, 256 * 1024,  64) },

	{ "m45pe10", INFO(0x204011,  0, 64 * 1024,    2) },
	{ "m45pe80", INFO(0x204014,  0, 64 * 1024,   16) },
	{ "m45pe16", INFO(0x204015,  0, 64 * 1024,   32) },

	{ "m25pe20", INFO(0x208012,  0, 64 * 1024,  4) },
	{ "m25pe80", INFO(0x208014,  0, 64 * 1024, 16) },
	{ "m25pe16", INFO(0x208015,  0, 64 * 1024, 32)
		NO_SFDP_FLAGS(SECT_4K) },

	{ "m25px16",    INFO(0x207115,  0, 64 * 1024, 32)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "m25px32",    INFO(0x207116,  0, 64 * 1024, 64)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "m25px32-s0", INFO(0x207316,  0, 64 * 1024, 64)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "m25px32-s1", INFO(0x206316,  0, 64 * 1024, 64)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "m25px64",    INFO(0x207117,  0, 64 * 1024, 128) },
	{ "m25px80",    INFO(0x207114,  0, 64 * 1024, 16) },
};

/**
 * micron_st_nor_set_4byte_addr_mode() - Set 4-byte address mode for ST and
 * Micron flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int micron_st_nor_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = spi_nor_set_4byte_addr_mode(nor, enable);
	if (ret)
		return ret;

	return spi_nor_write_disable(nor);
}

/**
 * micron_st_nor_read_fsr() - Read the Flag Status Register.
 * @nor:	pointer to 'struct spi_nor'
 * @fsr:	pointer to a DMA-able buffer where the value of the
 *              Flag Status Register will be written. Should be at least 2
 *              bytes.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int micron_st_nor_read_fsr(struct spi_nor *nor, u8 *fsr)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = MICRON_ST_RDFSR_OP(fsr);

		if (nor->reg_proto == SNOR_PROTO_8_8_8_DTR) {
			op.addr.nbytes = nor->params->rdsr_addr_nbytes;
			op.dummy.nbytes = nor->params->rdsr_dummy;
			/*
			 * We don't want to read only one byte in DTR mode. So,
			 * read 2 and then discard the second byte.
			 */
			op.data.nbytes = 2;
		}

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_read_reg(nor, SPINOR_OP_RDFSR, fsr,
						      1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d reading FSR\n", ret);

	return ret;
}

/**
 * micron_st_nor_clear_fsr() - Clear the Flag Status Register.
 * @nor:	pointer to 'struct spi_nor'.
 */
static void micron_st_nor_clear_fsr(struct spi_nor *nor)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = MICRON_ST_CLFSR_OP;

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_CLFSR,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d clearing FSR\n", ret);
}

/**
 * micron_st_nor_ready() - Query the Status Register as well as the Flag Status
 * Register to see if the flash is ready for new commands. If there are any
 * errors in the FSR clear them.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 1 if ready, 0 if not ready, -errno on errors.
 */
static int micron_st_nor_ready(struct spi_nor *nor)
{
	int sr_ready, ret;

	sr_ready = spi_nor_sr_ready(nor);
	if (sr_ready < 0)
		return sr_ready;

	ret = micron_st_nor_read_fsr(nor, nor->bouncebuf);
	if (ret) {
		/*
		 * Some controllers, such as Intel SPI, do not support low
		 * level operations such as reading the flag status
		 * register. They only expose small amount of high level
		 * operations to the software. If this is the case we use
		 * only the status register value.
		 */
		return ret == -EOPNOTSUPP ? sr_ready : ret;
	}

	if (nor->bouncebuf[0] & (FSR_E_ERR | FSR_P_ERR)) {
		if (nor->bouncebuf[0] & FSR_E_ERR)
			dev_err(nor->dev, "Erase operation failed.\n");
		else
			dev_err(nor->dev, "Program operation failed.\n");

		if (nor->bouncebuf[0] & FSR_PT_ERR)
			dev_err(nor->dev,
				"Attempted to modify a protected sector.\n");

		micron_st_nor_clear_fsr(nor);

		/*
		 * WEL bit remains set to one when an erase or page program
		 * error occurs. Issue a Write Disable command to protect
		 * against inadvertent writes that can possibly corrupt the
		 * contents of the memory.
		 */
		ret = spi_nor_write_disable(nor);
		if (ret)
			return ret;

		return -EIO;
	}

	return sr_ready && !!(nor->bouncebuf[0] & FSR_READY);
}

static void micron_st_nor_default_init(struct spi_nor *nor)
{
	nor->flags |= SNOR_F_HAS_LOCK;
	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	nor->params->quad_enable = NULL;
	nor->params->set_4byte_addr_mode = micron_st_nor_set_4byte_addr_mode;
}

static void micron_st_nor_late_init(struct spi_nor *nor)
{
	if (nor->info->mfr_flags & USE_FSR)
		nor->params->ready = micron_st_nor_ready;
}

static const struct spi_nor_fixups micron_st_nor_fixups = {
	.default_init = micron_st_nor_default_init,
	.late_init = micron_st_nor_late_init,
};

const struct spi_nor_manufacturer spi_nor_micron = {
	.name = "micron",
	.parts = micron_nor_parts,
	.nparts = ARRAY_SIZE(micron_nor_parts),
	.fixups = &micron_st_nor_fixups,
};

const struct spi_nor_manufacturer spi_nor_st = {
	.name = "st",
	.parts = st_nor_parts,
	.nparts = ARRAY_SIZE(st_nor_parts),
	.fixups = &micron_st_nor_fixups,
};
