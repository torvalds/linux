// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

/* flash_info mfr_flag. Used to read proprietary FSR register. */
#define USE_FSR		BIT(0)

#define SPINOR_OP_MT_DIE_ERASE	0xc4	/* Chip (die) erase opcode */
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

	if (memcmp(buf, nor->info->id->bytes, nor->info->id->len))
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

	if (memcmp(buf, nor->info->id->bytes, nor->info->id->len))
		return -EINVAL;

	return 0;
}

static int micron_st_nor_set_octal_dtr(struct spi_nor *nor, bool enable)
{
	return enable ? micron_st_nor_octal_dtr_en(nor) :
			micron_st_nor_octal_dtr_dis(nor);
}

static void mt35xu512aba_default_init(struct spi_nor *nor)
{
	nor->params->set_octal_dtr = micron_st_nor_set_octal_dtr;
}

static int mt35xu512aba_post_sfdp_fixup(struct spi_nor *nor)
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

	return 0;
}

static const struct spi_nor_fixups mt35xu512aba_fixups = {
	.default_init = mt35xu512aba_default_init,
	.post_sfdp = mt35xu512aba_post_sfdp_fixup,
};

static const struct flash_info micron_nor_parts[] = {
	{
		.id = SNOR_ID(0x2c, 0x5b, 0x1a),
		.name = "mt35xu512aba",
		.sector_size = SZ_128K,
		.size = SZ_64M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
				 SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.mfr_flags = USE_FSR,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &mt35xu512aba_fixups,
	}, {
		.id = SNOR_ID(0x2c, 0x5b, 0x1c),
		.name = "mt35xu02g",
		.sector_size = SZ_128K,
		.size = SZ_256M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ,
		.mfr_flags = USE_FSR,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	},
};

static int mt25qu512a_post_bfpt_fixup(struct spi_nor *nor,
				      const struct sfdp_parameter_header *bfpt_header,
				      const struct sfdp_bfpt *bfpt)
{
	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	return 0;
}

static struct spi_nor_fixups mt25qu512a_fixups = {
	.post_bfpt = mt25qu512a_post_bfpt_fixup,
};

static int st_nor_four_die_late_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	params->die_erase_opcode = SPINOR_OP_MT_DIE_ERASE;
	params->n_dice = 4;

	/*
	 * Unfortunately the die erase opcode does not have a 4-byte opcode
	 * correspondent for these flashes. The SFDP 4BAIT table fails to
	 * consider the die erase too. We're forced to enter in the 4 byte
	 * address mode in order to benefit of the die erase.
	 */
	return spi_nor_set_4byte_addr_mode(nor, true);
}

static int st_nor_two_die_late_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	params->die_erase_opcode = SPINOR_OP_MT_DIE_ERASE;
	params->n_dice = 2;

	/*
	 * Unfortunately the die erase opcode does not have a 4-byte opcode
	 * correspondent for these flashes. The SFDP 4BAIT table fails to
	 * consider the die erase too. We're forced to enter in the 4 byte
	 * address mode in order to benefit of the die erase.
	 */
	return spi_nor_set_4byte_addr_mode(nor, true);
}

static struct spi_nor_fixups n25q00_fixups = {
	.late_init = st_nor_four_die_late_init,
};

static struct spi_nor_fixups mt25q01_fixups = {
	.late_init = st_nor_two_die_late_init,
};

static struct spi_nor_fixups mt25q02_fixups = {
	.late_init = st_nor_four_die_late_init,
};

static const struct flash_info st_nor_parts[] = {
	{
		.name = "m25p05-nonjedec",
		.sector_size = SZ_32K,
		.size = SZ_64K,
	}, {
		.name = "m25p10-nonjedec",
		.sector_size = SZ_32K,
		.size = SZ_128K,
	}, {
		.name = "m25p20-nonjedec",
		.size = SZ_256K,
	}, {
		.name = "m25p40-nonjedec",
		.size = SZ_512K,
	}, {
		.name = "m25p80-nonjedec",
		.size = SZ_1M,
	}, {
		.name = "m25p16-nonjedec",
		.size = SZ_2M,
	}, {
		.name = "m25p32-nonjedec",
		.size = SZ_4M,
	}, {
		.name = "m25p64-nonjedec",
		.size = SZ_8M,
	}, {
		.name = "m25p128-nonjedec",
		.sector_size = SZ_256K,
		.size = SZ_16M,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x10),
		.name = "m25p05",
		.sector_size = SZ_32K,
		.size = SZ_64K,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x11),
		.name = "m25p10",
		.sector_size = SZ_32K,
		.size = SZ_128K,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x12),
		.name = "m25p20",
		.size = SZ_256K,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x13),
		.name = "m25p40",
		.size = SZ_512K,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x14),
		.name = "m25p80",
		.size = SZ_1M,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x15),
		.name = "m25p16",
		.size = SZ_2M,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x16),
		.name = "m25p32",
		.size = SZ_4M,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x17),
		.name = "m25p64",
		.size = SZ_8M,
	}, {
		.id = SNOR_ID(0x20, 0x20, 0x18),
		.name = "m25p128",
		.sector_size = SZ_256K,
		.size = SZ_16M,
	}, {
		.id = SNOR_ID(0x20, 0x40, 0x11),
		.name = "m45pe10",
		.size = SZ_128K,
	}, {
		.id = SNOR_ID(0x20, 0x40, 0x14),
		.name = "m45pe80",
		.size = SZ_1M,
	}, {
		.id = SNOR_ID(0x20, 0x40, 0x15),
		.name = "m45pe16",
		.size = SZ_2M,
	}, {
		.id = SNOR_ID(0x20, 0x63, 0x16),
		.name = "m25px32-s1",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x20, 0x71, 0x14),
		.name = "m25px80",
		.size = SZ_1M,
	}, {
		.id = SNOR_ID(0x20, 0x71, 0x15),
		.name = "m25px16",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x20, 0x71, 0x16),
		.name = "m25px32",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x20, 0x71, 0x17),
		.name = "m25px64",
		.size = SZ_8M,
	}, {
		.id = SNOR_ID(0x20, 0x73, 0x16),
		.name = "m25px32-s0",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x20, 0x80, 0x12),
		.name = "m25pe20",
		.size = SZ_256K,
	}, {
		.id = SNOR_ID(0x20, 0x80, 0x14),
		.name = "m25pe80",
		.size = SZ_1M,
	}, {
		.id = SNOR_ID(0x20, 0x80, 0x15),
		.name = "m25pe16",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x16),
		.name = "n25q032",
		.size = SZ_4M,
		.no_sfdp_flags = SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x17),
		.name = "n25q064",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x18),
		.name = "n25q128a13",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x19, 0x10, 0x44, 0x00),
		.name = "mt25ql256a",
		.size = SZ_32M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x19),
		.name = "n25q256a",
		.size = SZ_32M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x20, 0x10, 0x44, 0x00),
		.name = "mt25ql512a",
		.size = SZ_64M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x20),
		.name = "n25q512ax3",
		.size = SZ_64M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x21),
		.name = "n25q00",
		.size = SZ_128M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
		.fixups = &n25q00_fixups,
	}, {
		.id = SNOR_ID(0x20, 0xba, 0x22),
		.name = "mt25ql02g",
		.size = SZ_256M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
		.fixups = &mt25q02_fixups,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x15),
		.name = "n25q016a",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x16),
		.name = "n25q032a",
		.size = SZ_4M,
		.no_sfdp_flags = SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x17),
		.name = "n25q064a",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x18),
		.name = "n25q128a11",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x19, 0x10, 0x44, 0x00),
		.name = "mt25qu256a",
		.size = SZ_32M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x19),
		.name = "n25q256ax1",
		.size = SZ_32M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x20, 0x10, 0x44, 0x00),
		.name = "mt25qu512a",
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.mfr_flags = USE_FSR,
		.fixups = &mt25qu512a_fixups,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x20),
		.name = "n25q512a",
		.size = SZ_64M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			 SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x21, 0x10, 0x44, 0x00),
		.name = "mt25qu01g",
		.mfr_flags = USE_FSR,
		.fixups = &mt25q01_fixups,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x21),
		.name = "n25q00a",
		.size = SZ_128M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
		.fixups = &n25q00_fixups,
	}, {
		.id = SNOR_ID(0x20, 0xbb, 0x22),
		.name = "mt25qu02g",
		.size = SZ_256M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.mfr_flags = USE_FSR,
		.fixups = &mt25q02_fixups,
	}
};

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
}

static int micron_st_nor_late_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	if (nor->info->mfr_flags & USE_FSR)
		params->ready = micron_st_nor_ready;

	if (!params->set_4byte_addr_mode)
		params->set_4byte_addr_mode = spi_nor_set_4byte_addr_mode_wren_en4b_ex4b;

	return 0;
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
