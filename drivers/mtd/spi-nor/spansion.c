// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

/* flash_info mfr_flag. Used to clear sticky prorietary SR bits. */
#define USE_CLSR	BIT(0)

#define SPINOR_OP_CLSR		0x30	/* Clear status register 1 */
#define SPINOR_OP_RD_ANY_REG			0x65	/* Read any register */
#define SPINOR_OP_WR_ANY_REG			0x71	/* Write any register */
#define SPINOR_REG_CYPRESS_VREG			0x00800000
#define SPINOR_REG_CYPRESS_STR1			0x0
#define SPINOR_REG_CYPRESS_STR1V					\
	(SPINOR_REG_CYPRESS_VREG + SPINOR_REG_CYPRESS_STR1)
#define SPINOR_REG_CYPRESS_CFR1			0x2
#define SPINOR_REG_CYPRESS_CFR1V					\
	(SPINOR_REG_CYPRESS_VREG + SPINOR_REG_CYPRESS_CFR1)
#define SPINOR_REG_CYPRESS_CFR1_QUAD_EN		BIT(1)	/* Quad Enable */
#define SPINOR_REG_CYPRESS_CFR2			0x3
#define SPINOR_REG_CYPRESS_CFR2V					\
	(SPINOR_REG_CYPRESS_VREG + SPINOR_REG_CYPRESS_CFR2)
#define SPINOR_REG_CYPRESS_CFR2_MEMLAT_11_24	0xb
#define SPINOR_REG_CYPRESS_CFR2_ADRBYT		BIT(7)
#define SPINOR_REG_CYPRESS_CFR3			0x4
#define SPINOR_REG_CYPRESS_CFR3V					\
	(SPINOR_REG_CYPRESS_VREG + SPINOR_REG_CYPRESS_CFR3)
#define SPINOR_REG_CYPRESS_CFR3_PGSZ		BIT(4) /* Page size. */
#define SPINOR_REG_CYPRESS_CFR5			0x6
#define SPINOR_REG_CYPRESS_CFR5V					\
	(SPINOR_REG_CYPRESS_VREG + SPINOR_REG_CYPRESS_CFR5)
#define SPINOR_REG_CYPRESS_CFR5_BIT6		BIT(6)
#define SPINOR_REG_CYPRESS_CFR5_DDR		BIT(1)
#define SPINOR_REG_CYPRESS_CFR5_OPI		BIT(0)
#define SPINOR_REG_CYPRESS_CFR5_OCT_DTR_EN				\
	(SPINOR_REG_CYPRESS_CFR5_BIT6 |	SPINOR_REG_CYPRESS_CFR5_DDR |	\
	 SPINOR_REG_CYPRESS_CFR5_OPI)
#define SPINOR_REG_CYPRESS_CFR5_OCT_DTR_DS	SPINOR_REG_CYPRESS_CFR5_BIT6
#define SPINOR_OP_CYPRESS_RD_FAST		0xee
#define SPINOR_REG_CYPRESS_ARCFN		0x00000006

/* Cypress SPI NOR flash operations. */
#define CYPRESS_NOR_WR_ANY_REG_OP(naddr, addr, ndata, buf)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WR_ANY_REG, 0),		\
		   SPI_MEM_OP_ADDR(naddr, addr, 0),			\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(ndata, buf, 0))

#define CYPRESS_NOR_RD_ANY_REG_OP(naddr, addr, ndummy, buf)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RD_ANY_REG, 0),		\
		   SPI_MEM_OP_ADDR(naddr, addr, 0),			\
		   SPI_MEM_OP_DUMMY(ndummy, 0),				\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define SPANSION_CLSR_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_CLSR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

/**
 * spansion_nor_clear_sr() - Clear the Status Register.
 * @nor:	pointer to 'struct spi_nor'.
 */
static void spansion_nor_clear_sr(struct spi_nor *nor)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPANSION_CLSR_OP;

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_CLSR,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d clearing SR\n", ret);
}

static int cypress_nor_sr_ready_and_clear_reg(struct spi_nor *nor, u64 addr)
{
	struct spi_mem_op op =
		CYPRESS_NOR_RD_ANY_REG_OP(nor->params->addr_mode_nbytes, addr,
					  0, nor->bouncebuf);
	int ret;

	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & (SR_E_ERR | SR_P_ERR)) {
		if (nor->bouncebuf[0] & SR_E_ERR)
			dev_err(nor->dev, "Erase Error occurred\n");
		else
			dev_err(nor->dev, "Programming Error occurred\n");

		spansion_nor_clear_sr(nor);

		ret = spi_nor_write_disable(nor);
		if (ret)
			return ret;

		return -EIO;
	}

	return !(nor->bouncebuf[0] & SR_WIP);
}
/**
 * cypress_nor_sr_ready_and_clear() - Query the Status Register of each die by
 * using Read Any Register command to see if the whole flash is ready for new
 * commands and clear it if there are any errors.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 1 if ready, 0 if not ready, -errno on errors.
 */
static int cypress_nor_sr_ready_and_clear(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;
	u64 addr;
	int ret;
	u8 i;

	for (i = 0; i < params->n_dice; i++) {
		addr = params->vreg_offset[i] + SPINOR_REG_CYPRESS_STR1;
		ret = cypress_nor_sr_ready_and_clear_reg(nor, addr);
		if (ret < 0)
			return ret;
		else if (ret == 0)
			return 0;
	}

	return 1;
}

static int cypress_nor_octal_dtr_en(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;
	u8 addr_mode_nbytes = nor->params->addr_mode_nbytes;

	/* Use 24 dummy cycles for memory array reads. */
	*buf = SPINOR_REG_CYPRESS_CFR2_MEMLAT_11_24;
	op = (struct spi_mem_op)
		CYPRESS_NOR_WR_ANY_REG_OP(addr_mode_nbytes,
					  SPINOR_REG_CYPRESS_CFR2V, 1, buf);

	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	nor->read_dummy = 24;

	/* Set the octal and DTR enable bits. */
	buf[0] = SPINOR_REG_CYPRESS_CFR5_OCT_DTR_EN;
	op = (struct spi_mem_op)
		CYPRESS_NOR_WR_ANY_REG_OP(addr_mode_nbytes,
					  SPINOR_REG_CYPRESS_CFR5V, 1, buf);

	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_nor_read_id(nor, nor->addr_nbytes, 3, buf,
			      SNOR_PROTO_8_8_8_DTR);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID after enabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	if (memcmp(buf, nor->info->id, nor->info->id_len))
		return -EINVAL;

	return 0;
}

static int cypress_nor_octal_dtr_dis(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	/*
	 * The register is 1-byte wide, but 1-byte transactions are not allowed
	 * in 8D-8D-8D mode. Since there is no register at the next location,
	 * just initialize the value to 0 and let the transaction go on.
	 */
	buf[0] = SPINOR_REG_CYPRESS_CFR5_OCT_DTR_DS;
	buf[1] = 0;
	op = (struct spi_mem_op)
		CYPRESS_NOR_WR_ANY_REG_OP(nor->addr_nbytes,
					  SPINOR_REG_CYPRESS_CFR5V, 2, buf);
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

static int cypress_nor_quad_enable_volatile_reg(struct spi_nor *nor, u64 addr)
{
	struct spi_mem_op op;
	u8 addr_mode_nbytes = nor->params->addr_mode_nbytes;
	u8 cfr1v_written;
	int ret;

	op = (struct spi_mem_op)
		CYPRESS_NOR_RD_ANY_REG_OP(addr_mode_nbytes, addr, 0,
					  nor->bouncebuf);

	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & SPINOR_REG_CYPRESS_CFR1_QUAD_EN)
		return 0;

	/* Update the Quad Enable bit. */
	nor->bouncebuf[0] |= SPINOR_REG_CYPRESS_CFR1_QUAD_EN;
	op = (struct spi_mem_op)
		CYPRESS_NOR_WR_ANY_REG_OP(addr_mode_nbytes, addr, 1,
					  nor->bouncebuf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	cfr1v_written = nor->bouncebuf[0];

	/* Read back and check it. */
	op = (struct spi_mem_op)
		CYPRESS_NOR_RD_ANY_REG_OP(addr_mode_nbytes, addr, 0,
					  nor->bouncebuf);
	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] != cfr1v_written) {
		dev_err(nor->dev, "CFR1: Read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * cypress_nor_quad_enable_volatile() - enable Quad I/O mode in volatile
 *                                      register.
 * @nor:	pointer to a 'struct spi_nor'
 *
 * It is recommended to update volatile registers in the field application due
 * to a risk of the non-volatile registers corruption by power interrupt. This
 * function sets Quad Enable bit in CFR1 volatile. If users set the Quad Enable
 * bit in the CFR1 non-volatile in advance (typically by a Flash programmer
 * before mounting Flash on PCB), the Quad Enable bit in the CFR1 volatile is
 * also set during Flash power-up.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int cypress_nor_quad_enable_volatile(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;
	u64 addr;
	u8 i;
	int ret;

	if (!params->n_dice)
		return cypress_nor_quad_enable_volatile_reg(nor,
						SPINOR_REG_CYPRESS_CFR1V);

	for (i = 0; i < params->n_dice; i++) {
		addr = params->vreg_offset[i] + SPINOR_REG_CYPRESS_CFR1;
		ret = cypress_nor_quad_enable_volatile_reg(nor, addr);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * cypress_nor_determine_addr_mode_by_sr1() - Determine current address mode
 *                                            (3 or 4-byte) by querying status
 *                                            register 1 (SR1).
 * @nor:		pointer to a 'struct spi_nor'
 * @addr_mode:		ponter to a buffer where we return the determined
 *			address mode.
 *
 * This function tries to determine current address mode by comparing SR1 value
 * from RDSR1(no address), RDAR(3-byte address), and RDAR(4-byte address).
 *
 * Return: 0 on success, -errno otherwise.
 */
static int cypress_nor_determine_addr_mode_by_sr1(struct spi_nor *nor,
						  u8 *addr_mode)
{
	struct spi_mem_op op =
		CYPRESS_NOR_RD_ANY_REG_OP(3, SPINOR_REG_CYPRESS_STR1V, 0,
					  nor->bouncebuf);
	bool is3byte, is4byte;
	int ret;

	ret = spi_nor_read_sr(nor, &nor->bouncebuf[1]);
	if (ret)
		return ret;

	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	is3byte = (nor->bouncebuf[0] == nor->bouncebuf[1]);

	op = (struct spi_mem_op)
		CYPRESS_NOR_RD_ANY_REG_OP(4, SPINOR_REG_CYPRESS_STR1V, 0,
					  nor->bouncebuf);
	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	is4byte = (nor->bouncebuf[0] == nor->bouncebuf[1]);

	if (is3byte == is4byte)
		return -EIO;
	if (is3byte)
		*addr_mode = 3;
	else
		*addr_mode = 4;

	return 0;
}

/**
 * cypress_nor_set_addr_mode_nbytes() - Set the number of address bytes mode of
 *                                      current address mode.
 * @nor:		pointer to a 'struct spi_nor'
 *
 * Determine current address mode by reading SR1 with different methods, then
 * query CFR2V[7] to confirm. If determination is failed, force enter to 4-byte
 * address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int cypress_nor_set_addr_mode_nbytes(struct spi_nor *nor)
{
	struct spi_mem_op op = {};
	u8 addr_mode;
	int ret;

	/*
	 * Read SR1 by RDSR1 and RDAR(3- AND 4-byte addr). Use write enable
	 * that sets bit-1 in SR1.
	 */
	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;
	ret = cypress_nor_determine_addr_mode_by_sr1(nor, &addr_mode);
	if (ret) {
		ret = spi_nor_set_4byte_addr_mode(nor, true);
		if (ret)
			return ret;
		return spi_nor_write_disable(nor);
	}
	ret = spi_nor_write_disable(nor);
	if (ret)
		return ret;

	/*
	 * Query CFR2V and make sure no contradiction between determined address
	 * mode and CFR2V[7].
	 */
	op = (struct spi_mem_op)
		CYPRESS_NOR_RD_ANY_REG_OP(addr_mode, SPINOR_REG_CYPRESS_CFR2V,
					  0, nor->bouncebuf);
	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & SPINOR_REG_CYPRESS_CFR2_ADRBYT) {
		if (addr_mode != 4)
			return spi_nor_set_4byte_addr_mode(nor, true);
	} else {
		if (addr_mode != 3)
			return spi_nor_set_4byte_addr_mode(nor, true);
	}

	nor->params->addr_nbytes = addr_mode;
	nor->params->addr_mode_nbytes = addr_mode;

	return 0;
}

static int cypress_nor_get_page_size_single_chip(struct spi_nor *nor)
{
	struct spi_mem_op op =
		CYPRESS_NOR_RD_ANY_REG_OP(nor->params->addr_mode_nbytes,
					  SPINOR_REG_CYPRESS_CFR3V, 0,
					  nor->bouncebuf);
	int ret;

	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & SPINOR_REG_CYPRESS_CFR3_PGSZ)
		nor->params->page_size = 512;
	else
		nor->params->page_size = 256;

	return 0;
}


static int cypress_nor_get_page_size_mcp(struct spi_nor *nor)
{
	struct spi_mem_op op =
		CYPRESS_NOR_RD_ANY_REG_OP(nor->params->addr_mode_nbytes,
					  0, 0, nor->bouncebuf);
	struct spi_nor_flash_parameter *params = nor->params;
	int ret;
	u8 i;

	/*
	 * Use the minimum common page size configuration. Programming 256-byte
	 * under 512-byte page size configuration is safe.
	 */
	params->page_size = 256;
	for (i = 0; i < params->n_dice; i++) {
		op.addr.val = params->vreg_offset[i] + SPINOR_REG_CYPRESS_CFR3;

		ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
		if (ret)
			return ret;

		if (!(nor->bouncebuf[0] & SPINOR_REG_CYPRESS_CFR3_PGSZ))
			return 0;
	}

	params->page_size = 512;

	return 0;
}

/**
 * cypress_nor_get_page_size() - Get flash page size configuration.
 * @nor:	pointer to a 'struct spi_nor'
 *
 * The BFPT table advertises a 512B or 256B page size depending on part but the
 * page size is actually configurable (with the default being 256B). Read from
 * CFR3V[4] and set the correct size.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int cypress_nor_get_page_size(struct spi_nor *nor)
{
	if (nor->params->n_dice)
		return cypress_nor_get_page_size_mcp(nor);
	return cypress_nor_get_page_size_single_chip(nor);
}

static void cypress_nor_ecc_init(struct spi_nor *nor)
{
	/*
	 * Programming is supported only in 16-byte ECC data unit granularity.
	 * Byte-programming, bit-walking, or multiple program operations to the
	 * same ECC data unit without an erase are not allowed.
	 */
	nor->params->writesize = 16;
	nor->flags |= SNOR_F_ECC;
}

static int
s25fs256t_post_bfpt_fixup(struct spi_nor *nor,
			  const struct sfdp_parameter_header *bfpt_header,
			  const struct sfdp_bfpt *bfpt)
{
	struct spi_mem_op op = {};
	int ret;

	ret = cypress_nor_set_addr_mode_nbytes(nor);
	if (ret)
		return ret;

	/* Read Architecture Configuration Register (ARCFN) */
	op = (struct spi_mem_op)
		CYPRESS_NOR_RD_ANY_REG_OP(nor->params->addr_mode_nbytes,
					  SPINOR_REG_CYPRESS_ARCFN, 1,
					  nor->bouncebuf);
	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	/* ARCFN value must be 0 if uniform sector is selected  */
	if (nor->bouncebuf[0])
		return -ENODEV;

	return cypress_nor_get_page_size(nor);
}

static int s25fs256t_post_sfdp_fixup(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	/* PP_1_1_4_4B is supported but missing in 4BAIT. */
	params->hwcaps.mask |= SNOR_HWCAPS_PP_1_1_4;
	spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_1_1_4],
				SPINOR_OP_PP_1_1_4_4B,
				SNOR_PROTO_1_1_4);

	return 0;
}

static void s25fs256t_late_init(struct spi_nor *nor)
{
	cypress_nor_ecc_init(nor);
}

static struct spi_nor_fixups s25fs256t_fixups = {
	.post_bfpt = s25fs256t_post_bfpt_fixup,
	.post_sfdp = s25fs256t_post_sfdp_fixup,
	.late_init = s25fs256t_late_init,
};

static int
s25hx_t_post_bfpt_fixup(struct spi_nor *nor,
			const struct sfdp_parameter_header *bfpt_header,
			const struct sfdp_bfpt *bfpt)
{
	int ret;

	ret = cypress_nor_set_addr_mode_nbytes(nor);
	if (ret)
		return ret;

	/* Replace Quad Enable with volatile version */
	nor->params->quad_enable = cypress_nor_quad_enable_volatile;

	return 0;
}

static int s25hx_t_post_sfdp_fixup(struct spi_nor *nor)
{
	struct spi_nor_erase_type *erase_type =
					nor->params->erase_map.erase_type;
	unsigned int i;

	/*
	 * In some parts, 3byte erase opcodes are advertised by 4BAIT.
	 * Convert them to 4byte erase opcodes.
	 */
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++) {
		switch (erase_type[i].opcode) {
		case SPINOR_OP_SE:
			erase_type[i].opcode = SPINOR_OP_SE_4B;
			break;
		case SPINOR_OP_BE_4K:
			erase_type[i].opcode = SPINOR_OP_BE_4K_4B;
			break;
		default:
			break;
		}
	}

	/* The 2 Gb parts duplicate info and advertise 4 dice instead of 2. */
	if (nor->params->size == SZ_256M)
		nor->params->n_dice = 2;

	return cypress_nor_get_page_size(nor);
}

static void s25hx_t_late_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	/* Fast Read 4B requires mode cycles */
	params->reads[SNOR_CMD_READ_FAST].num_mode_clocks = 8;

	cypress_nor_ecc_init(nor);

	/* Replace ready() with multi die version */
	if (params->n_dice)
		params->ready = cypress_nor_sr_ready_and_clear;
}

static struct spi_nor_fixups s25hx_t_fixups = {
	.post_bfpt = s25hx_t_post_bfpt_fixup,
	.post_sfdp = s25hx_t_post_sfdp_fixup,
	.late_init = s25hx_t_late_init,
};

/**
 * cypress_nor_octal_dtr_enable() - Enable octal DTR on Cypress flashes.
 * @nor:		pointer to a 'struct spi_nor'
 * @enable:              whether to enable or disable Octal DTR
 *
 * This also sets the memory access latency cycles to 24 to allow the flash to
 * run at up to 200MHz.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int cypress_nor_octal_dtr_enable(struct spi_nor *nor, bool enable)
{
	return enable ? cypress_nor_octal_dtr_en(nor) :
			cypress_nor_octal_dtr_dis(nor);
}

static int s28hx_t_post_sfdp_fixup(struct spi_nor *nor)
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

	return cypress_nor_get_page_size(nor);
}

static int s28hx_t_post_bfpt_fixup(struct spi_nor *nor,
				   const struct sfdp_parameter_header *bfpt_header,
				   const struct sfdp_bfpt *bfpt)
{
	int ret;

	ret = cypress_nor_set_addr_mode_nbytes(nor);
	if (ret)
		return ret;

	return 0;
}

static void s28hx_t_late_init(struct spi_nor *nor)
{
	nor->params->octal_dtr_enable = cypress_nor_octal_dtr_enable;
	cypress_nor_ecc_init(nor);
}

static const struct spi_nor_fixups s28hx_t_fixups = {
	.post_sfdp = s28hx_t_post_sfdp_fixup,
	.post_bfpt = s28hx_t_post_bfpt_fixup,
	.late_init = s28hx_t_late_init,
};

static int
s25fs_s_nor_post_bfpt_fixups(struct spi_nor *nor,
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

static const struct spi_nor_fixups s25fs_s_nor_fixups = {
	.post_bfpt = s25fs_s_nor_post_bfpt_fixups,
};

static const struct flash_info spansion_nor_parts[] = {
	/* Spansion/Cypress -- single (large) sector size only, at least
	 * for the chips listed here (without boot sectors).
	 */
	{ "s25sl032p",  INFO(0x010215, 0x4d00,  64 * 1024,  64)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25sl064p",  INFO(0x010216, 0x4d00,  64 * 1024, 128)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl128s0", INFO6(0x012018, 0x4d0080, 256 * 1024, 64)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fl128s1", INFO6(0x012018, 0x4d0180, 64 * 1024, 256)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fl256s0", INFO6(0x010219, 0x4d0080, 256 * 1024, 128)
		NO_SFDP_FLAGS(SPI_NOR_SKIP_SFDP | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fl256s1", INFO6(0x010219, 0x4d0180, 64 * 1024, 512)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fl512s",  INFO6(0x010220, 0x4d0080, 256 * 1024, 256)
		FLAGS(SPI_NOR_HAS_LOCK)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fs128s1", INFO6(0x012018, 0x4d0181, 64 * 1024, 256)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
		.fixups = &s25fs_s_nor_fixups, },
	{ "s25fs256s0", INFO6(0x010219, 0x4d0081, 256 * 1024, 128)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fs256s1", INFO6(0x010219, 0x4d0181, 64 * 1024, 512)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fs512s",  INFO6(0x010220, 0x4d0081, 256 * 1024, 256)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
		.fixups = &s25fs_s_nor_fixups, },
	{ "s25sl12800", INFO(0x012018, 0x0300, 256 * 1024,  64) },
	{ "s25sl12801", INFO(0x012018, 0x0301,  64 * 1024, 256) },
	{ "s25fl129p0", INFO(0x012018, 0x4d00, 256 * 1024,  64)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25fl129p1", INFO(0x012018, 0x4d01,  64 * 1024, 256)
		NO_SFDP_FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		MFR_FLAGS(USE_CLSR)
	},
	{ "s25sl004a",  INFO(0x010212,      0,  64 * 1024,   8) },
	{ "s25sl008a",  INFO(0x010213,      0,  64 * 1024,  16) },
	{ "s25sl016a",  INFO(0x010214,      0,  64 * 1024,  32) },
	{ "s25sl032a",  INFO(0x010215,      0,  64 * 1024,  64) },
	{ "s25sl064a",  INFO(0x010216,      0,  64 * 1024, 128) },
	{ "s25fl004k",  INFO(0xef4013,      0,  64 * 1024,   8)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "s25fl008k",  INFO(0xef4014,      0,  64 * 1024,  16)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "s25fl016k",  INFO(0xef4015,      0,  64 * 1024,  32)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "s25fl064k",  INFO(0xef4017,      0,  64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "s25fl116k",  INFO(0x014015,      0,  64 * 1024,  32)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "s25fl132k",  INFO(0x014016,      0,  64 * 1024,  64)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "s25fl164k",  INFO(0x014017,      0,  64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "s25fl204k",  INFO(0x014013,      0,  64 * 1024,   8)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "s25fl208k",  INFO(0x014014,      0,  64 * 1024,  16)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "s25fl064l",  INFO(0x016017,      0,  64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES) },
	{ "s25fl128l",  INFO(0x016018,      0,  64 * 1024, 256)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES) },
	{ "s25fl256l",  INFO(0x016019,      0,  64 * 1024, 512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES) },
	{ "s25fs256t",  INFO6(0x342b19, 0x0f0890, 0, 0)
		PARSE_SFDP
		.fixups = &s25fs256t_fixups },
	{ "s25hl512t",  INFO6(0x342a1a, 0x0f0390, 256 * 1024, 256)
		PARSE_SFDP
		MFR_FLAGS(USE_CLSR)
		.fixups = &s25hx_t_fixups },
	{ "s25hl01gt",  INFO6(0x342a1b, 0x0f0390, 256 * 1024, 512)
		PARSE_SFDP
		MFR_FLAGS(USE_CLSR)
		.fixups = &s25hx_t_fixups },
	{ "s25hl02gt",  INFO6(0x342a1c, 0x0f0090, 0, 0)
		PARSE_SFDP
		FLAGS(NO_CHIP_ERASE)
		.fixups = &s25hx_t_fixups },
	{ "s25hs512t",  INFO6(0x342b1a, 0x0f0390, 256 * 1024, 256)
		PARSE_SFDP
		MFR_FLAGS(USE_CLSR)
		.fixups = &s25hx_t_fixups },
	{ "s25hs01gt",  INFO6(0x342b1b, 0x0f0390, 256 * 1024, 512)
		PARSE_SFDP
		MFR_FLAGS(USE_CLSR)
		.fixups = &s25hx_t_fixups },
	{ "s25hs02gt",  INFO6(0x342b1c, 0x0f0090, 0, 0)
		PARSE_SFDP
		FLAGS(NO_CHIP_ERASE)
		.fixups = &s25hx_t_fixups },
	{ "cy15x104q",  INFO6(0x042cc2, 0x7f7f7f, 512 * 1024, 1)
		FLAGS(SPI_NOR_NO_ERASE) },
	{ "s28hl512t",   INFO(0x345a1a,      0, 256 * 1024, 256)
		PARSE_SFDP
		.fixups = &s28hx_t_fixups,
	},
	{ "s28hl01gt",   INFO(0x345a1b,      0, 256 * 1024, 512)
		PARSE_SFDP
		.fixups = &s28hx_t_fixups,
	},
	{ "s28hs512t",   INFO(0x345b1a,      0, 256 * 1024, 256)
		PARSE_SFDP
		.fixups = &s28hx_t_fixups,
	},
	{ "s28hs01gt",   INFO(0x345b1b,      0, 256 * 1024, 512)
		PARSE_SFDP
		.fixups = &s28hx_t_fixups,
	},
};

/**
 * spansion_nor_sr_ready_and_clear() - Query the Status Register to see if the
 * flash is ready for new commands and clear it if there are any errors.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 1 if ready, 0 if not ready, -errno on errors.
 */
static int spansion_nor_sr_ready_and_clear(struct spi_nor *nor)
{
	int ret;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & (SR_E_ERR | SR_P_ERR)) {
		if (nor->bouncebuf[0] & SR_E_ERR)
			dev_err(nor->dev, "Erase Error occurred\n");
		else
			dev_err(nor->dev, "Programming Error occurred\n");

		spansion_nor_clear_sr(nor);

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

	return !(nor->bouncebuf[0] & SR_WIP);
}

static void spansion_nor_late_init(struct spi_nor *nor)
{
	if (nor->params->size > SZ_16M) {
		nor->flags |= SNOR_F_4B_OPCODES;
		/* No small sector erase for 4-byte command set */
		nor->erase_opcode = SPINOR_OP_SE;
		nor->mtd.erasesize = nor->info->sector_size;
	}

	if (nor->info->mfr_flags & USE_CLSR)
		nor->params->ready = spansion_nor_sr_ready_and_clear;
}

static const struct spi_nor_fixups spansion_nor_fixups = {
	.late_init = spansion_nor_late_init,
};

const struct spi_nor_manufacturer spi_nor_spansion = {
	.name = "spansion",
	.parts = spansion_nor_parts,
	.nparts = ARRAY_SIZE(spansion_nor_parts),
	.fixups = &spansion_nor_fixups,
};
