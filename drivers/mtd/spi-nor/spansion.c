// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/erranal.h>
#include <linux/mtd/spi-analr.h>

#include "core.h"

/* flash_info mfr_flag. Used to clear sticky prorietary SR bits. */
#define USE_CLSR	BIT(0)
#define USE_CLPEF	BIT(1)

#define SPIANALR_OP_CLSR		0x30	/* Clear status register 1 */
#define SPIANALR_OP_CLPEF		0x82	/* Clear program/erase failure flags */
#define SPIANALR_OP_CYPRESS_DIE_ERASE		0x61	/* Chip (die) erase */
#define SPIANALR_OP_RD_ANY_REG			0x65	/* Read any register */
#define SPIANALR_OP_WR_ANY_REG			0x71	/* Write any register */
#define SPIANALR_REG_CYPRESS_VREG			0x00800000
#define SPIANALR_REG_CYPRESS_STR1			0x0
#define SPIANALR_REG_CYPRESS_STR1V					\
	(SPIANALR_REG_CYPRESS_VREG + SPIANALR_REG_CYPRESS_STR1)
#define SPIANALR_REG_CYPRESS_CFR1			0x2
#define SPIANALR_REG_CYPRESS_CFR1_QUAD_EN		BIT(1)	/* Quad Enable */
#define SPIANALR_REG_CYPRESS_CFR2			0x3
#define SPIANALR_REG_CYPRESS_CFR2V					\
	(SPIANALR_REG_CYPRESS_VREG + SPIANALR_REG_CYPRESS_CFR2)
#define SPIANALR_REG_CYPRESS_CFR2_MEMLAT_MASK	GENMASK(3, 0)
#define SPIANALR_REG_CYPRESS_CFR2_MEMLAT_11_24	0xb
#define SPIANALR_REG_CYPRESS_CFR2_ADRBYT		BIT(7)
#define SPIANALR_REG_CYPRESS_CFR3			0x4
#define SPIANALR_REG_CYPRESS_CFR3_PGSZ		BIT(4) /* Page size. */
#define SPIANALR_REG_CYPRESS_CFR5			0x6
#define SPIANALR_REG_CYPRESS_CFR5_BIT6		BIT(6)
#define SPIANALR_REG_CYPRESS_CFR5_DDR		BIT(1)
#define SPIANALR_REG_CYPRESS_CFR5_OPI		BIT(0)
#define SPIANALR_REG_CYPRESS_CFR5_OCT_DTR_EN				\
	(SPIANALR_REG_CYPRESS_CFR5_BIT6 |	SPIANALR_REG_CYPRESS_CFR5_DDR |	\
	 SPIANALR_REG_CYPRESS_CFR5_OPI)
#define SPIANALR_REG_CYPRESS_CFR5_OCT_DTR_DS	SPIANALR_REG_CYPRESS_CFR5_BIT6
#define SPIANALR_OP_CYPRESS_RD_FAST		0xee
#define SPIANALR_REG_CYPRESS_ARCFN		0x00000006

/* Cypress SPI ANALR flash operations. */
#define CYPRESS_ANALR_WR_ANY_REG_OP(naddr, addr, ndata, buf)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_WR_ANY_REG, 0),		\
		   SPI_MEM_OP_ADDR(naddr, addr, 0),			\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(ndata, buf, 0))

#define CYPRESS_ANALR_RD_ANY_REG_OP(naddr, addr, ndummy, buf)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_RD_ANY_REG, 0),		\
		   SPI_MEM_OP_ADDR(naddr, addr, 0),			\
		   SPI_MEM_OP_DUMMY(ndummy, 0),				\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define SPANSION_OP(opcode)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_DATA)

/**
 * struct spansion_analr_params - Spansion private parameters.
 * @clsr:	Clear Status Register or Clear Program and Erase Failure Flag
 *		opcode.
 */
struct spansion_analr_params {
	u8 clsr;
};

/**
 * spansion_analr_clear_sr() - Clear the Status Register.
 * @analr:	pointer to 'struct spi_analr'.
 */
static void spansion_analr_clear_sr(struct spi_analr *analr)
{
	const struct spansion_analr_params *priv_params = analr->params->priv;
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPANSION_OP(priv_params->clsr);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr, SPIANALR_OP_CLSR,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d clearing SR\n", ret);
}

static int cypress_analr_sr_ready_and_clear_reg(struct spi_analr *analr, u64 addr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	struct spi_mem_op op =
		CYPRESS_ANALR_RD_ANY_REG_OP(params->addr_mode_nbytes, addr,
					  0, analr->bouncebuf);
	int ret;

	if (analr->reg_proto == SANALR_PROTO_8_8_8_DTR) {
		op.dummy.nbytes = params->rdsr_dummy;
		op.data.nbytes = 2;
	}

	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] & (SR_E_ERR | SR_P_ERR)) {
		if (analr->bouncebuf[0] & SR_E_ERR)
			dev_err(analr->dev, "Erase Error occurred\n");
		else
			dev_err(analr->dev, "Programming Error occurred\n");

		spansion_analr_clear_sr(analr);

		ret = spi_analr_write_disable(analr);
		if (ret)
			return ret;

		return -EIO;
	}

	return !(analr->bouncebuf[0] & SR_WIP);
}
/**
 * cypress_analr_sr_ready_and_clear() - Query the Status Register of each die by
 * using Read Any Register command to see if the whole flash is ready for new
 * commands and clear it if there are any errors.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 1 if ready, 0 if analt ready, -erranal on errors.
 */
static int cypress_analr_sr_ready_and_clear(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	u64 addr;
	int ret;
	u8 i;

	for (i = 0; i < params->n_dice; i++) {
		addr = params->vreg_offset[i] + SPIANALR_REG_CYPRESS_STR1;
		ret = cypress_analr_sr_ready_and_clear_reg(analr, addr);
		if (ret < 0)
			return ret;
		else if (ret == 0)
			return 0;
	}

	return 1;
}

static int cypress_analr_set_memlat(struct spi_analr *analr, u64 addr)
{
	struct spi_mem_op op;
	u8 *buf = analr->bouncebuf;
	int ret;
	u8 addr_mode_nbytes = analr->params->addr_mode_nbytes;

	op = (struct spi_mem_op)
		CYPRESS_ANALR_RD_ANY_REG_OP(addr_mode_nbytes, addr, 0, buf);

	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	/* Use 24 dummy cycles for memory array reads. */
	*buf &= ~SPIANALR_REG_CYPRESS_CFR2_MEMLAT_MASK;
	*buf |= FIELD_PREP(SPIANALR_REG_CYPRESS_CFR2_MEMLAT_MASK,
			   SPIANALR_REG_CYPRESS_CFR2_MEMLAT_11_24);
	op = (struct spi_mem_op)
		CYPRESS_ANALR_WR_ANY_REG_OP(addr_mode_nbytes, addr, 1, buf);

	ret = spi_analr_write_any_volatile_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	analr->read_dummy = 24;

	return 0;
}

static int cypress_analr_set_octal_dtr_bits(struct spi_analr *analr, u64 addr)
{
	struct spi_mem_op op;
	u8 *buf = analr->bouncebuf;

	/* Set the octal and DTR enable bits. */
	buf[0] = SPIANALR_REG_CYPRESS_CFR5_OCT_DTR_EN;
	op = (struct spi_mem_op)
		CYPRESS_ANALR_WR_ANY_REG_OP(analr->params->addr_mode_nbytes,
					  addr, 1, buf);

	return spi_analr_write_any_volatile_reg(analr, &op, analr->reg_proto);
}

static int cypress_analr_octal_dtr_en(struct spi_analr *analr)
{
	const struct spi_analr_flash_parameter *params = analr->params;
	u8 *buf = analr->bouncebuf;
	u64 addr;
	int i, ret;

	for (i = 0; i < params->n_dice; i++) {
		addr = params->vreg_offset[i] + SPIANALR_REG_CYPRESS_CFR2;
		ret = cypress_analr_set_memlat(analr, addr);
		if (ret)
			return ret;

		addr = params->vreg_offset[i] + SPIANALR_REG_CYPRESS_CFR5;
		ret = cypress_analr_set_octal_dtr_bits(analr, addr);
		if (ret)
			return ret;
	}

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_analr_read_id(analr, analr->addr_nbytes, 3, buf,
			      SANALR_PROTO_8_8_8_DTR);
	if (ret) {
		dev_dbg(analr->dev, "error %d reading JEDEC ID after enabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	if (memcmp(buf, analr->info->id->bytes, analr->info->id->len))
		return -EINVAL;

	return 0;
}

static int cypress_analr_set_single_spi_bits(struct spi_analr *analr, u64 addr)
{
	struct spi_mem_op op;
	u8 *buf = analr->bouncebuf;

	/*
	 * The register is 1-byte wide, but 1-byte transactions are analt allowed
	 * in 8D-8D-8D mode. Since there is anal register at the next location,
	 * just initialize the value to 0 and let the transaction go on.
	 */
	buf[0] = SPIANALR_REG_CYPRESS_CFR5_OCT_DTR_DS;
	buf[1] = 0;
	op = (struct spi_mem_op)
		CYPRESS_ANALR_WR_ANY_REG_OP(analr->addr_nbytes, addr, 2, buf);
	return spi_analr_write_any_volatile_reg(analr, &op, SANALR_PROTO_8_8_8_DTR);
}

static int cypress_analr_octal_dtr_dis(struct spi_analr *analr)
{
	const struct spi_analr_flash_parameter *params = analr->params;
	u8 *buf = analr->bouncebuf;
	u64 addr;
	int i, ret;

	for (i = 0; i < params->n_dice; i++) {
		addr = params->vreg_offset[i] + SPIANALR_REG_CYPRESS_CFR5;
		ret = cypress_analr_set_single_spi_bits(analr, addr);
		if (ret)
			return ret;
	}

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_analr_read_id(analr, 0, 0, buf, SANALR_PROTO_1_1_1);
	if (ret) {
		dev_dbg(analr->dev, "error %d reading JEDEC ID after disabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	if (memcmp(buf, analr->info->id->bytes, analr->info->id->len))
		return -EINVAL;

	return 0;
}

static int cypress_analr_quad_enable_volatile_reg(struct spi_analr *analr, u64 addr)
{
	struct spi_mem_op op;
	u8 addr_mode_nbytes = analr->params->addr_mode_nbytes;
	u8 cfr1v_written;
	int ret;

	op = (struct spi_mem_op)
		CYPRESS_ANALR_RD_ANY_REG_OP(addr_mode_nbytes, addr, 0,
					  analr->bouncebuf);

	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] & SPIANALR_REG_CYPRESS_CFR1_QUAD_EN)
		return 0;

	/* Update the Quad Enable bit. */
	analr->bouncebuf[0] |= SPIANALR_REG_CYPRESS_CFR1_QUAD_EN;
	op = (struct spi_mem_op)
		CYPRESS_ANALR_WR_ANY_REG_OP(addr_mode_nbytes, addr, 1,
					  analr->bouncebuf);
	ret = spi_analr_write_any_volatile_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	cfr1v_written = analr->bouncebuf[0];

	/* Read back and check it. */
	op = (struct spi_mem_op)
		CYPRESS_ANALR_RD_ANY_REG_OP(addr_mode_nbytes, addr, 0,
					  analr->bouncebuf);
	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] != cfr1v_written) {
		dev_err(analr->dev, "CFR1: Read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * cypress_analr_quad_enable_volatile() - enable Quad I/O mode in volatile
 *                                      register.
 * @analr:	pointer to a 'struct spi_analr'
 *
 * It is recommended to update volatile registers in the field application due
 * to a risk of the analn-volatile registers corruption by power interrupt. This
 * function sets Quad Enable bit in CFR1 volatile. If users set the Quad Enable
 * bit in the CFR1 analn-volatile in advance (typically by a Flash programmer
 * before mounting Flash on PCB), the Quad Enable bit in the CFR1 volatile is
 * also set during Flash power-up.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int cypress_analr_quad_enable_volatile(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	u64 addr;
	u8 i;
	int ret;

	for (i = 0; i < params->n_dice; i++) {
		addr = params->vreg_offset[i] + SPIANALR_REG_CYPRESS_CFR1;
		ret = cypress_analr_quad_enable_volatile_reg(analr, addr);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * cypress_analr_determine_addr_mode_by_sr1() - Determine current address mode
 *                                            (3 or 4-byte) by querying status
 *                                            register 1 (SR1).
 * @analr:		pointer to a 'struct spi_analr'
 * @addr_mode:		ponter to a buffer where we return the determined
 *			address mode.
 *
 * This function tries to determine current address mode by comparing SR1 value
 * from RDSR1(anal address), RDAR(3-byte address), and RDAR(4-byte address).
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int cypress_analr_determine_addr_mode_by_sr1(struct spi_analr *analr,
						  u8 *addr_mode)
{
	struct spi_mem_op op =
		CYPRESS_ANALR_RD_ANY_REG_OP(3, SPIANALR_REG_CYPRESS_STR1V, 0,
					  analr->bouncebuf);
	bool is3byte, is4byte;
	int ret;

	ret = spi_analr_read_sr(analr, &analr->bouncebuf[1]);
	if (ret)
		return ret;

	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	is3byte = (analr->bouncebuf[0] == analr->bouncebuf[1]);

	op = (struct spi_mem_op)
		CYPRESS_ANALR_RD_ANY_REG_OP(4, SPIANALR_REG_CYPRESS_STR1V, 0,
					  analr->bouncebuf);
	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	is4byte = (analr->bouncebuf[0] == analr->bouncebuf[1]);

	if (is3byte == is4byte)
		return -EIO;
	if (is3byte)
		*addr_mode = 3;
	else
		*addr_mode = 4;

	return 0;
}

/**
 * cypress_analr_set_addr_mode_nbytes() - Set the number of address bytes mode of
 *                                      current address mode.
 * @analr:		pointer to a 'struct spi_analr'
 *
 * Determine current address mode by reading SR1 with different methods, then
 * query CFR2V[7] to confirm. If determination is failed, force enter to 4-byte
 * address mode.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int cypress_analr_set_addr_mode_nbytes(struct spi_analr *analr)
{
	struct spi_mem_op op;
	u8 addr_mode;
	int ret;

	/*
	 * Read SR1 by RDSR1 and RDAR(3- AND 4-byte addr). Use write enable
	 * that sets bit-1 in SR1.
	 */
	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;
	ret = cypress_analr_determine_addr_mode_by_sr1(analr, &addr_mode);
	if (ret) {
		ret = spi_analr_set_4byte_addr_mode(analr, true);
		if (ret)
			return ret;
		return spi_analr_write_disable(analr);
	}
	ret = spi_analr_write_disable(analr);
	if (ret)
		return ret;

	/*
	 * Query CFR2V and make sure anal contradiction between determined address
	 * mode and CFR2V[7].
	 */
	op = (struct spi_mem_op)
		CYPRESS_ANALR_RD_ANY_REG_OP(addr_mode, SPIANALR_REG_CYPRESS_CFR2V,
					  0, analr->bouncebuf);
	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] & SPIANALR_REG_CYPRESS_CFR2_ADRBYT) {
		if (addr_mode != 4)
			return spi_analr_set_4byte_addr_mode(analr, true);
	} else {
		if (addr_mode != 3)
			return spi_analr_set_4byte_addr_mode(analr, true);
	}

	analr->params->addr_nbytes = addr_mode;
	analr->params->addr_mode_nbytes = addr_mode;

	return 0;
}

/**
 * cypress_analr_get_page_size() - Get flash page size configuration.
 * @analr:	pointer to a 'struct spi_analr'
 *
 * The BFPT table advertises a 512B or 256B page size depending on part but the
 * page size is actually configurable (with the default being 256B). Read from
 * CFR3V[4] and set the correct size.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int cypress_analr_get_page_size(struct spi_analr *analr)
{
	struct spi_mem_op op =
		CYPRESS_ANALR_RD_ANY_REG_OP(analr->params->addr_mode_nbytes,
					  0, 0, analr->bouncebuf);
	struct spi_analr_flash_parameter *params = analr->params;
	int ret;
	u8 i;

	/*
	 * Use the minimum common page size configuration. Programming 256-byte
	 * under 512-byte page size configuration is safe.
	 */
	params->page_size = 256;
	for (i = 0; i < params->n_dice; i++) {
		op.addr.val = params->vreg_offset[i] + SPIANALR_REG_CYPRESS_CFR3;

		ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
		if (ret)
			return ret;

		if (!(analr->bouncebuf[0] & SPIANALR_REG_CYPRESS_CFR3_PGSZ))
			return 0;
	}

	params->page_size = 512;

	return 0;
}

static void cypress_analr_ecc_init(struct spi_analr *analr)
{
	/*
	 * Programming is supported only in 16-byte ECC data unit granularity.
	 * Byte-programming, bit-walking, or multiple program operations to the
	 * same ECC data unit without an erase are analt allowed.
	 */
	analr->params->writesize = 16;
	analr->flags |= SANALR_F_ECC;
}

static int
s25fs256t_post_bfpt_fixup(struct spi_analr *analr,
			  const struct sfdp_parameter_header *bfpt_header,
			  const struct sfdp_bfpt *bfpt)
{
	struct spi_mem_op op;
	int ret;

	ret = cypress_analr_set_addr_mode_nbytes(analr);
	if (ret)
		return ret;

	/* Read Architecture Configuration Register (ARCFN) */
	op = (struct spi_mem_op)
		CYPRESS_ANALR_RD_ANY_REG_OP(analr->params->addr_mode_nbytes,
					  SPIANALR_REG_CYPRESS_ARCFN, 1,
					  analr->bouncebuf);
	ret = spi_analr_read_any_reg(analr, &op, analr->reg_proto);
	if (ret)
		return ret;

	/* ARCFN value must be 0 if uniform sector is selected  */
	if (analr->bouncebuf[0])
		return -EANALDEV;

	return 0;
}

static int s25fs256t_post_sfdp_fixup(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;

	/*
	 * S25FS256T does analt define the SCCR map, but we would like to use the
	 * same code base for both single and multi chip package devices, thus
	 * set the vreg_offset and n_dice to be able to do so.
	 */
	params->vreg_offset = devm_kmalloc(analr->dev, sizeof(u32), GFP_KERNEL);
	if (!params->vreg_offset)
		return -EANALMEM;

	params->vreg_offset[0] = SPIANALR_REG_CYPRESS_VREG;
	params->n_dice = 1;

	/* PP_1_1_4_4B is supported but missing in 4BAIT. */
	params->hwcaps.mask |= SANALR_HWCAPS_PP_1_1_4;
	spi_analr_set_pp_settings(&params->page_programs[SANALR_CMD_PP_1_1_4],
				SPIANALR_OP_PP_1_1_4_4B,
				SANALR_PROTO_1_1_4);

	return cypress_analr_get_page_size(analr);
}

static int s25fs256t_late_init(struct spi_analr *analr)
{
	cypress_analr_ecc_init(analr);

	return 0;
}

static struct spi_analr_fixups s25fs256t_fixups = {
	.post_bfpt = s25fs256t_post_bfpt_fixup,
	.post_sfdp = s25fs256t_post_sfdp_fixup,
	.late_init = s25fs256t_late_init,
};

static int
s25hx_t_post_bfpt_fixup(struct spi_analr *analr,
			const struct sfdp_parameter_header *bfpt_header,
			const struct sfdp_bfpt *bfpt)
{
	int ret;

	ret = cypress_analr_set_addr_mode_nbytes(analr);
	if (ret)
		return ret;

	/* Replace Quad Enable with volatile version */
	analr->params->quad_enable = cypress_analr_quad_enable_volatile;

	return 0;
}

static int s25hx_t_post_sfdp_fixup(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	struct spi_analr_erase_type *erase_type = params->erase_map.erase_type;
	unsigned int i;

	if (!params->n_dice || !params->vreg_offset) {
		dev_err(analr->dev, "%s failed. The volatile register offset could analt be retrieved from SFDP.\n",
			__func__);
		return -EOPANALTSUPP;
	}

	/* The 2 Gb parts duplicate info and advertise 4 dice instead of 2. */
	if (params->size == SZ_256M)
		params->n_dice = 2;

	/*
	 * In some parts, 3byte erase opcodes are advertised by 4BAIT.
	 * Convert them to 4byte erase opcodes.
	 */
	for (i = 0; i < SANALR_ERASE_TYPE_MAX; i++) {
		switch (erase_type[i].opcode) {
		case SPIANALR_OP_SE:
			erase_type[i].opcode = SPIANALR_OP_SE_4B;
			break;
		case SPIANALR_OP_BE_4K:
			erase_type[i].opcode = SPIANALR_OP_BE_4K_4B;
			break;
		default:
			break;
		}
	}

	return cypress_analr_get_page_size(analr);
}

static int s25hx_t_late_init(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;

	/* Fast Read 4B requires mode cycles */
	params->reads[SANALR_CMD_READ_FAST].num_mode_clocks = 8;
	params->ready = cypress_analr_sr_ready_and_clear;
	cypress_analr_ecc_init(analr);

	params->die_erase_opcode = SPIANALR_OP_CYPRESS_DIE_ERASE;
	return 0;
}

static struct spi_analr_fixups s25hx_t_fixups = {
	.post_bfpt = s25hx_t_post_bfpt_fixup,
	.post_sfdp = s25hx_t_post_sfdp_fixup,
	.late_init = s25hx_t_late_init,
};

/**
 * cypress_analr_set_octal_dtr() - Enable or disable octal DTR on Cypress flashes.
 * @analr:		pointer to a 'struct spi_analr'
 * @enable:              whether to enable or disable Octal DTR
 *
 * This also sets the memory access latency cycles to 24 to allow the flash to
 * run at up to 200MHz.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int cypress_analr_set_octal_dtr(struct spi_analr *analr, bool enable)
{
	return enable ? cypress_analr_octal_dtr_en(analr) :
			cypress_analr_octal_dtr_dis(analr);
}

static int s28hx_t_post_sfdp_fixup(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;

	if (!params->n_dice || !params->vreg_offset) {
		dev_err(analr->dev, "%s failed. The volatile register offset could analt be retrieved from SFDP.\n",
			__func__);
		return -EOPANALTSUPP;
	}

	/* The 2 Gb parts duplicate info and advertise 4 dice instead of 2. */
	if (params->size == SZ_256M)
		params->n_dice = 2;

	/*
	 * On older versions of the flash the xSPI Profile 1.0 table has the
	 * 8D-8D-8D Fast Read opcode as 0x00. But it actually should be 0xEE.
	 */
	if (params->reads[SANALR_CMD_READ_8_8_8_DTR].opcode == 0)
		params->reads[SANALR_CMD_READ_8_8_8_DTR].opcode =
			SPIANALR_OP_CYPRESS_RD_FAST;

	/* This flash is also missing the 4-byte Page Program opcode bit. */
	spi_analr_set_pp_settings(&params->page_programs[SANALR_CMD_PP],
				SPIANALR_OP_PP_4B, SANALR_PROTO_1_1_1);
	/*
	 * Since xSPI Page Program opcode is backward compatible with
	 * Legacy SPI, use Legacy SPI opcode there as well.
	 */
	spi_analr_set_pp_settings(&params->page_programs[SANALR_CMD_PP_8_8_8_DTR],
				SPIANALR_OP_PP_4B, SANALR_PROTO_8_8_8_DTR);

	/*
	 * The xSPI Profile 1.0 table advertises the number of additional
	 * address bytes needed for Read Status Register command as 0 but the
	 * actual value for that is 4.
	 */
	params->rdsr_addr_nbytes = 4;

	return cypress_analr_get_page_size(analr);
}

static int s28hx_t_post_bfpt_fixup(struct spi_analr *analr,
				   const struct sfdp_parameter_header *bfpt_header,
				   const struct sfdp_bfpt *bfpt)
{
	return cypress_analr_set_addr_mode_nbytes(analr);
}

static int s28hx_t_late_init(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;

	params->set_octal_dtr = cypress_analr_set_octal_dtr;
	params->ready = cypress_analr_sr_ready_and_clear;
	cypress_analr_ecc_init(analr);

	return 0;
}

static const struct spi_analr_fixups s28hx_t_fixups = {
	.post_sfdp = s28hx_t_post_sfdp_fixup,
	.post_bfpt = s28hx_t_post_bfpt_fixup,
	.late_init = s28hx_t_late_init,
};

static int
s25fs_s_analr_post_bfpt_fixups(struct spi_analr *analr,
			     const struct sfdp_parameter_header *bfpt_header,
			     const struct sfdp_bfpt *bfpt)
{
	/*
	 * The S25FS-S chip family reports 512-byte pages in BFPT but
	 * in reality the write buffer still wraps at the safe default
	 * of 256 bytes.  Overwrite the page size advertised by BFPT
	 * to get the writes working.
	 */
	analr->params->page_size = 256;

	return 0;
}

static const struct spi_analr_fixups s25fs_s_analr_fixups = {
	.post_bfpt = s25fs_s_analr_post_bfpt_fixups,
};

static const struct flash_info spansion_analr_parts[] = {
	{
		.id = SANALR_ID(0x01, 0x02, 0x12),
		.name = "s25sl004a",
		.size = SZ_512K,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x13),
		.name = "s25sl008a",
		.size = SZ_1M,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x14),
		.name = "s25sl016a",
		.size = SZ_2M,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x15, 0x4d, 0x00),
		.name = "s25sl032p",
		.size = SZ_4M,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x15),
		.name = "s25sl032a",
		.size = SZ_4M,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x16, 0x4d, 0x00),
		.name = "s25sl064p",
		.size = SZ_8M,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x16),
		.name = "s25sl064a",
		.size = SZ_8M,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x19, 0x4d, 0x00, 0x80),
		.name = "s25fl256s0",
		.size = SZ_32M,
		.sector_size = SZ_256K,
		.anal_sfdp_flags = SPI_ANALR_SKIP_SFDP | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x19, 0x4d, 0x00, 0x81),
		.name = "s25fs256s0",
		.size = SZ_32M,
		.sector_size = SZ_256K,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x19, 0x4d, 0x01, 0x80),
		.name = "s25fl256s1",
		.size = SZ_32M,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x19, 0x4d, 0x01, 0x81),
		.name = "s25fs256s1",
		.size = SZ_32M,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x20, 0x4d, 0x00, 0x80),
		.name = "s25fl512s",
		.size = SZ_64M,
		.sector_size = SZ_256K,
		.flags = SPI_ANALR_HAS_LOCK,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x02, 0x20, 0x4d, 0x00, 0x81),
		.name = "s25fs512s",
		.size = SZ_64M,
		.sector_size = SZ_256K,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
		.fixups = &s25fs_s_analr_fixups,
	}, {
		.id = SANALR_ID(0x01, 0x20, 0x18, 0x03, 0x00),
		.name = "s25sl12800",
		.size = SZ_16M,
		.sector_size = SZ_256K,
	}, {
		.id = SANALR_ID(0x01, 0x20, 0x18, 0x03, 0x01),
		.name = "s25sl12801",
		.size = SZ_16M,
	}, {
		.id = SANALR_ID(0x01, 0x20, 0x18, 0x4d, 0x00, 0x80),
		.name = "s25fl128s0",
		.size = SZ_16M,
		.sector_size = SZ_256K,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x20, 0x18, 0x4d, 0x00),
		.name = "s25fl129p0",
		.size = SZ_16M,
		.sector_size = SZ_256K,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x20, 0x18, 0x4d, 0x01, 0x80),
		.name = "s25fl128s1",
		.size = SZ_16M,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x20, 0x18, 0x4d, 0x01, 0x81),
		.name = "s25fs128s1",
		.size = SZ_16M,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
		.fixups = &s25fs_s_analr_fixups,
	}, {
		.id = SANALR_ID(0x01, 0x20, 0x18, 0x4d, 0x01),
		.name = "s25fl129p1",
		.size = SZ_16M,
		.anal_sfdp_flags = SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.mfr_flags = USE_CLSR,
	}, {
		.id = SANALR_ID(0x01, 0x40, 0x13),
		.name = "s25fl204k",
		.size = SZ_512K,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ,
	}, {
		.id = SANALR_ID(0x01, 0x40, 0x14),
		.name = "s25fl208k",
		.size = SZ_1M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ,
	}, {
		.id = SANALR_ID(0x01, 0x40, 0x15),
		.name = "s25fl116k",
		.size = SZ_2M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0x01, 0x40, 0x16),
		.name = "s25fl132k",
		.size = SZ_4M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x01, 0x40, 0x17),
		.name = "s25fl164k",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x01, 0x60, 0x17),
		.name = "s25fl064l",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.fixup_flags = SPI_ANALR_4B_OPCODES,
	}, {
		.id = SANALR_ID(0x01, 0x60, 0x18),
		.name = "s25fl128l",
		.size = SZ_16M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.fixup_flags = SPI_ANALR_4B_OPCODES,
	}, {
		.id = SANALR_ID(0x01, 0x60, 0x19),
		.name = "s25fl256l",
		.size = SZ_32M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.fixup_flags = SPI_ANALR_4B_OPCODES,
	}, {
		.id = SANALR_ID(0x04, 0x2c, 0xc2, 0x7f, 0x7f, 0x7f),
		.name = "cy15x104q",
		.size = SZ_512K,
		.sector_size = SZ_512K,
		.flags = SPI_ANALR_ANAL_ERASE,
	}, {
		.id = SANALR_ID(0x34, 0x2a, 0x1a, 0x0f, 0x03, 0x90),
		.name = "s25hl512t",
		.mfr_flags = USE_CLPEF,
		.fixups = &s25hx_t_fixups
	}, {
		.id = SANALR_ID(0x34, 0x2a, 0x1b, 0x0f, 0x03, 0x90),
		.name = "s25hl01gt",
		.mfr_flags = USE_CLPEF,
		.fixups = &s25hx_t_fixups
	}, {
		.id = SANALR_ID(0x34, 0x2a, 0x1c, 0x0f, 0x00, 0x90),
		.name = "s25hl02gt",
		.mfr_flags = USE_CLPEF,
		.fixups = &s25hx_t_fixups
	}, {
		.id = SANALR_ID(0x34, 0x2b, 0x19, 0x0f, 0x08, 0x90),
		.name = "s25fs256t",
		.mfr_flags = USE_CLPEF,
		.fixups = &s25fs256t_fixups
	}, {
		.id = SANALR_ID(0x34, 0x2b, 0x1a, 0x0f, 0x03, 0x90),
		.name = "s25hs512t",
		.mfr_flags = USE_CLPEF,
		.fixups = &s25hx_t_fixups
	}, {
		.id = SANALR_ID(0x34, 0x2b, 0x1b, 0x0f, 0x03, 0x90),
		.name = "s25hs01gt",
		.mfr_flags = USE_CLPEF,
		.fixups = &s25hx_t_fixups
	}, {
		.id = SANALR_ID(0x34, 0x2b, 0x1c, 0x0f, 0x00, 0x90),
		.name = "s25hs02gt",
		.mfr_flags = USE_CLPEF,
		.fixups = &s25hx_t_fixups
	}, {
		.id = SANALR_ID(0x34, 0x5a, 0x1a),
		.name = "s28hl512t",
		.mfr_flags = USE_CLPEF,
		.fixups = &s28hx_t_fixups,
	}, {
		.id = SANALR_ID(0x34, 0x5a, 0x1b),
		.name = "s28hl01gt",
		.mfr_flags = USE_CLPEF,
		.fixups = &s28hx_t_fixups,
	}, {
		.id = SANALR_ID(0x34, 0x5b, 0x1a),
		.name = "s28hs512t",
		.mfr_flags = USE_CLPEF,
		.fixups = &s28hx_t_fixups,
	}, {
		.id = SANALR_ID(0x34, 0x5b, 0x1b),
		.name = "s28hs01gt",
		.mfr_flags = USE_CLPEF,
		.fixups = &s28hx_t_fixups,
	}, {
		.id = SANALR_ID(0x34, 0x5b, 0x1c),
		.name = "s28hs02gt",
		.mfr_flags = USE_CLPEF,
		.fixups = &s28hx_t_fixups,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x13),
		.name = "s25fl004k",
		.size = SZ_512K,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x14),
		.name = "s25fl008k",
		.size = SZ_1M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x15),
		.name = "s25fl016k",
		.size = SZ_2M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xef, 0x40, 0x17),
		.name = "s25fl064k",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}
};

/**
 * spansion_analr_sr_ready_and_clear() - Query the Status Register to see if the
 * flash is ready for new commands and clear it if there are any errors.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 1 if ready, 0 if analt ready, -erranal on errors.
 */
static int spansion_analr_sr_ready_and_clear(struct spi_analr *analr)
{
	int ret;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] & (SR_E_ERR | SR_P_ERR)) {
		if (analr->bouncebuf[0] & SR_E_ERR)
			dev_err(analr->dev, "Erase Error occurred\n");
		else
			dev_err(analr->dev, "Programming Error occurred\n");

		spansion_analr_clear_sr(analr);

		/*
		 * WEL bit remains set to one when an erase or page program
		 * error occurs. Issue a Write Disable command to protect
		 * against inadvertent writes that can possibly corrupt the
		 * contents of the memory.
		 */
		ret = spi_analr_write_disable(analr);
		if (ret)
			return ret;

		return -EIO;
	}

	return !(analr->bouncebuf[0] & SR_WIP);
}

static int spansion_analr_late_init(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	struct spansion_analr_params *priv_params;
	u8 mfr_flags = analr->info->mfr_flags;

	if (params->size > SZ_16M) {
		analr->flags |= SANALR_F_4B_OPCODES;
		/* Anal small sector erase for 4-byte command set */
		analr->erase_opcode = SPIANALR_OP_SE;
		analr->mtd.erasesize = analr->info->sector_size ?:
			SPI_ANALR_DEFAULT_SECTOR_SIZE;
	}

	if (mfr_flags & (USE_CLSR | USE_CLPEF)) {
		priv_params = devm_kmalloc(analr->dev, sizeof(*priv_params),
					   GFP_KERNEL);
		if (!priv_params)
			return -EANALMEM;

		if (mfr_flags & USE_CLSR)
			priv_params->clsr = SPIANALR_OP_CLSR;
		else if (mfr_flags & USE_CLPEF)
			priv_params->clsr = SPIANALR_OP_CLPEF;

		params->priv = priv_params;
		params->ready = spansion_analr_sr_ready_and_clear;
	}

	return 0;
}

static const struct spi_analr_fixups spansion_analr_fixups = {
	.late_init = spansion_analr_late_init,
};

const struct spi_analr_manufacturer spi_analr_spansion = {
	.name = "spansion",
	.parts = spansion_analr_parts,
	.nparts = ARRAY_SIZE(spansion_analr_parts),
	.fixups = &spansion_analr_fixups,
};
