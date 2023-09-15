// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2009-2015 Freescale Semiconductor, Inc. and others
 *
 * Description: MPC5125, VF610, MCF54418 and Kinetis K70 Nand driver.
 * Jason ported to M54418TWR and MVFA5 (VF610).
 * Authors: Stefan Agner <stefan.agner@toradex.com>
 *          Bill Pringlemeir <bpringlemeir@nbsps.com>
 *          Shaohui Xie <b21989@freescale.com>
 *          Jason Jin <Jason.jin@freescale.com>
 *
 * Based on original driver mpc5121_nfc.c.
 *
 * Limitations:
 * - Untested on MPC5125 and M54418.
 * - DMA and pipelining not used.
 * - 2K pages or less.
 * - HW ECC: Only 2K page with 64+ OOB.
 * - HW ECC: Only 24 and 32-bit error correction implemented.
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/swab.h>

#define	DRV_NAME		"vf610_nfc"

/* Register Offsets */
#define NFC_FLASH_CMD1			0x3F00
#define NFC_FLASH_CMD2			0x3F04
#define NFC_COL_ADDR			0x3F08
#define NFC_ROW_ADDR			0x3F0c
#define NFC_ROW_ADDR_INC		0x3F14
#define NFC_FLASH_STATUS1		0x3F18
#define NFC_FLASH_STATUS2		0x3F1c
#define NFC_CACHE_SWAP			0x3F28
#define NFC_SECTOR_SIZE			0x3F2c
#define NFC_FLASH_CONFIG		0x3F30
#define NFC_IRQ_STATUS			0x3F38

/* Addresses for NFC MAIN RAM BUFFER areas */
#define NFC_MAIN_AREA(n)		((n) *  0x1000)

#define PAGE_2K				0x0800
#define OOB_64				0x0040
#define OOB_MAX				0x0100

/* NFC_CMD2[CODE] controller cycle bit masks */
#define COMMAND_CMD_BYTE1		BIT(14)
#define COMMAND_CAR_BYTE1		BIT(13)
#define COMMAND_CAR_BYTE2		BIT(12)
#define COMMAND_RAR_BYTE1		BIT(11)
#define COMMAND_RAR_BYTE2		BIT(10)
#define COMMAND_RAR_BYTE3		BIT(9)
#define COMMAND_NADDR_BYTES(x)		GENMASK(13, 13 - (x) + 1)
#define COMMAND_WRITE_DATA		BIT(8)
#define COMMAND_CMD_BYTE2		BIT(7)
#define COMMAND_RB_HANDSHAKE		BIT(6)
#define COMMAND_READ_DATA		BIT(5)
#define COMMAND_CMD_BYTE3		BIT(4)
#define COMMAND_READ_STATUS		BIT(3)
#define COMMAND_READ_ID			BIT(2)

/* NFC ECC mode define */
#define ECC_BYPASS			0
#define ECC_45_BYTE			6
#define ECC_60_BYTE			7

/*** Register Mask and bit definitions */

/* NFC_FLASH_CMD1 Field */
#define CMD_BYTE2_MASK				0xFF000000
#define CMD_BYTE2_SHIFT				24

/* NFC_FLASH_CM2 Field */
#define CMD_BYTE1_MASK				0xFF000000
#define CMD_BYTE1_SHIFT				24
#define CMD_CODE_MASK				0x00FFFF00
#define CMD_CODE_SHIFT				8
#define BUFNO_MASK				0x00000006
#define BUFNO_SHIFT				1
#define START_BIT				BIT(0)

/* NFC_COL_ADDR Field */
#define COL_ADDR_MASK				0x0000FFFF
#define COL_ADDR_SHIFT				0
#define COL_ADDR(pos, val)			(((val) & 0xFF) << (8 * (pos)))

/* NFC_ROW_ADDR Field */
#define ROW_ADDR_MASK				0x00FFFFFF
#define ROW_ADDR_SHIFT				0
#define ROW_ADDR(pos, val)			(((val) & 0xFF) << (8 * (pos)))

#define ROW_ADDR_CHIP_SEL_RB_MASK		0xF0000000
#define ROW_ADDR_CHIP_SEL_RB_SHIFT		28
#define ROW_ADDR_CHIP_SEL_MASK			0x0F000000
#define ROW_ADDR_CHIP_SEL_SHIFT			24

/* NFC_FLASH_STATUS2 Field */
#define STATUS_BYTE1_MASK			0x000000FF

/* NFC_FLASH_CONFIG Field */
#define CONFIG_ECC_SRAM_ADDR_MASK		0x7FC00000
#define CONFIG_ECC_SRAM_ADDR_SHIFT		22
#define CONFIG_ECC_SRAM_REQ_BIT			BIT(21)
#define CONFIG_DMA_REQ_BIT			BIT(20)
#define CONFIG_ECC_MODE_MASK			0x000E0000
#define CONFIG_ECC_MODE_SHIFT			17
#define CONFIG_FAST_FLASH_BIT			BIT(16)
#define CONFIG_16BIT				BIT(7)
#define CONFIG_BOOT_MODE_BIT			BIT(6)
#define CONFIG_ADDR_AUTO_INCR_BIT		BIT(5)
#define CONFIG_BUFNO_AUTO_INCR_BIT		BIT(4)
#define CONFIG_PAGE_CNT_MASK			0xF
#define CONFIG_PAGE_CNT_SHIFT			0

/* NFC_IRQ_STATUS Field */
#define IDLE_IRQ_BIT				BIT(29)
#define IDLE_EN_BIT				BIT(20)
#define CMD_DONE_CLEAR_BIT			BIT(18)
#define IDLE_CLEAR_BIT				BIT(17)

/*
 * ECC status - seems to consume 8 bytes (double word). The documented
 * status byte is located in the lowest byte of the second word (which is
 * the 4th or 7th byte depending on endianness).
 * Calculate an offset to store the ECC status at the end of the buffer.
 */
#define ECC_SRAM_ADDR		(PAGE_2K + OOB_MAX - 8)

#define ECC_STATUS		0x4
#define ECC_STATUS_MASK		0x80
#define ECC_STATUS_ERR_COUNT	0x3F

enum vf610_nfc_variant {
	NFC_VFC610 = 1,
};

struct vf610_nfc {
	struct nand_controller base;
	struct nand_chip chip;
	struct device *dev;
	void __iomem *regs;
	struct completion cmd_done;
	/* Status and ID are in alternate locations. */
	enum vf610_nfc_variant variant;
	struct clk *clk;
	/*
	 * Indicate that user data is accessed (full page/oob). This is
	 * useful to indicate the driver whether to swap byte endianness.
	 * See comments in vf610_nfc_rd_from_sram/vf610_nfc_wr_to_sram.
	 */
	bool data_access;
	u32 ecc_mode;
};

static inline struct vf610_nfc *chip_to_nfc(struct nand_chip *chip)
{
	return container_of(chip, struct vf610_nfc, chip);
}

static inline u32 vf610_nfc_read(struct vf610_nfc *nfc, uint reg)
{
	return readl(nfc->regs + reg);
}

static inline void vf610_nfc_write(struct vf610_nfc *nfc, uint reg, u32 val)
{
	writel(val, nfc->regs + reg);
}

static inline void vf610_nfc_set(struct vf610_nfc *nfc, uint reg, u32 bits)
{
	vf610_nfc_write(nfc, reg, vf610_nfc_read(nfc, reg) | bits);
}

static inline void vf610_nfc_clear(struct vf610_nfc *nfc, uint reg, u32 bits)
{
	vf610_nfc_write(nfc, reg, vf610_nfc_read(nfc, reg) & ~bits);
}

static inline void vf610_nfc_set_field(struct vf610_nfc *nfc, u32 reg,
				       u32 mask, u32 shift, u32 val)
{
	vf610_nfc_write(nfc, reg,
			(vf610_nfc_read(nfc, reg) & (~mask)) | val << shift);
}

static inline bool vf610_nfc_kernel_is_little_endian(void)
{
#ifdef __LITTLE_ENDIAN
	return true;
#else
	return false;
#endif
}

/*
 * Read accessor for internal SRAM buffer
 * @dst: destination address in regular memory
 * @src: source address in SRAM buffer
 * @len: bytes to copy
 * @fix_endian: Fix endianness if required
 *
 * Use this accessor for the internal SRAM buffers. On the ARM
 * Freescale Vybrid SoC it's known that the driver can treat
 * the SRAM buffer as if it's memory. Other platform might need
 * to treat the buffers differently.
 *
 * The controller stores bytes from the NAND chip internally in big
 * endianness. On little endian platforms such as Vybrid this leads
 * to reversed byte order.
 * For performance reason (and earlier probably due to unawareness)
 * the driver avoids correcting endianness where it has control over
 * write and read side (e.g. page wise data access).
 */
static inline void vf610_nfc_rd_from_sram(void *dst, const void __iomem *src,
					  size_t len, bool fix_endian)
{
	if (vf610_nfc_kernel_is_little_endian() && fix_endian) {
		unsigned int i;

		for (i = 0; i < len; i += 4) {
			u32 val = swab32(__raw_readl(src + i));

			memcpy(dst + i, &val, min(sizeof(val), len - i));
		}
	} else {
		memcpy_fromio(dst, src, len);
	}
}

/*
 * Write accessor for internal SRAM buffer
 * @dst: destination address in SRAM buffer
 * @src: source address in regular memory
 * @len: bytes to copy
 * @fix_endian: Fix endianness if required
 *
 * Use this accessor for the internal SRAM buffers. On the ARM
 * Freescale Vybrid SoC it's known that the driver can treat
 * the SRAM buffer as if it's memory. Other platform might need
 * to treat the buffers differently.
 *
 * The controller stores bytes from the NAND chip internally in big
 * endianness. On little endian platforms such as Vybrid this leads
 * to reversed byte order.
 * For performance reason (and earlier probably due to unawareness)
 * the driver avoids correcting endianness where it has control over
 * write and read side (e.g. page wise data access).
 */
static inline void vf610_nfc_wr_to_sram(void __iomem *dst, const void *src,
					size_t len, bool fix_endian)
{
	if (vf610_nfc_kernel_is_little_endian() && fix_endian) {
		unsigned int i;

		for (i = 0; i < len; i += 4) {
			u32 val;

			memcpy(&val, src + i, min(sizeof(val), len - i));
			__raw_writel(swab32(val), dst + i);
		}
	} else {
		memcpy_toio(dst, src, len);
	}
}

/* Clear flags for upcoming command */
static inline void vf610_nfc_clear_status(struct vf610_nfc *nfc)
{
	u32 tmp = vf610_nfc_read(nfc, NFC_IRQ_STATUS);

	tmp |= CMD_DONE_CLEAR_BIT | IDLE_CLEAR_BIT;
	vf610_nfc_write(nfc, NFC_IRQ_STATUS, tmp);
}

static void vf610_nfc_done(struct vf610_nfc *nfc)
{
	unsigned long timeout = msecs_to_jiffies(100);

	/*
	 * Barrier is needed after this write. This write need
	 * to be done before reading the next register the first
	 * time.
	 * vf610_nfc_set implicates such a barrier by using writel
	 * to write to the register.
	 */
	vf610_nfc_set(nfc, NFC_IRQ_STATUS, IDLE_EN_BIT);
	vf610_nfc_set(nfc, NFC_FLASH_CMD2, START_BIT);

	if (!wait_for_completion_timeout(&nfc->cmd_done, timeout))
		dev_warn(nfc->dev, "Timeout while waiting for BUSY.\n");

	vf610_nfc_clear_status(nfc);
}

static irqreturn_t vf610_nfc_irq(int irq, void *data)
{
	struct vf610_nfc *nfc = data;

	vf610_nfc_clear(nfc, NFC_IRQ_STATUS, IDLE_EN_BIT);
	complete(&nfc->cmd_done);

	return IRQ_HANDLED;
}

static inline void vf610_nfc_ecc_mode(struct vf610_nfc *nfc, int ecc_mode)
{
	vf610_nfc_set_field(nfc, NFC_FLASH_CONFIG,
			    CONFIG_ECC_MODE_MASK,
			    CONFIG_ECC_MODE_SHIFT, ecc_mode);
}

static inline void vf610_nfc_run(struct vf610_nfc *nfc, u32 col, u32 row,
				 u32 cmd1, u32 cmd2, u32 trfr_sz)
{
	vf610_nfc_set_field(nfc, NFC_COL_ADDR, COL_ADDR_MASK,
			    COL_ADDR_SHIFT, col);

	vf610_nfc_set_field(nfc, NFC_ROW_ADDR, ROW_ADDR_MASK,
			    ROW_ADDR_SHIFT, row);

	vf610_nfc_write(nfc, NFC_SECTOR_SIZE, trfr_sz);
	vf610_nfc_write(nfc, NFC_FLASH_CMD1, cmd1);
	vf610_nfc_write(nfc, NFC_FLASH_CMD2, cmd2);

	dev_dbg(nfc->dev,
		"col 0x%04x, row 0x%08x, cmd1 0x%08x, cmd2 0x%08x, len %d\n",
		col, row, cmd1, cmd2, trfr_sz);

	vf610_nfc_done(nfc);
}

static inline const struct nand_op_instr *
vf610_get_next_instr(const struct nand_subop *subop, int *op_id)
{
	if (*op_id + 1 >= subop->ninstrs)
		return NULL;

	(*op_id)++;

	return &subop->instrs[*op_id];
}

static int vf610_nfc_cmd(struct nand_chip *chip,
			 const struct nand_subop *subop)
{
	const struct nand_op_instr *instr;
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	int op_id = -1, trfr_sz = 0, offset = 0;
	u32 col = 0, row = 0, cmd1 = 0, cmd2 = 0, code = 0;
	bool force8bit = false;

	/*
	 * Some ops are optional, but the hardware requires the operations
	 * to be in this exact order.
	 * The op parser enforces the order and makes sure that there isn't
	 * a read and write element in a single operation.
	 */
	instr = vf610_get_next_instr(subop, &op_id);
	if (!instr)
		return -EINVAL;

	if (instr && instr->type == NAND_OP_CMD_INSTR) {
		cmd2 |= instr->ctx.cmd.opcode << CMD_BYTE1_SHIFT;
		code |= COMMAND_CMD_BYTE1;

		instr = vf610_get_next_instr(subop, &op_id);
	}

	if (instr && instr->type == NAND_OP_ADDR_INSTR) {
		int naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
		int i = nand_subop_get_addr_start_off(subop, op_id);

		for (; i < naddrs; i++) {
			u8 val = instr->ctx.addr.addrs[i];

			if (i < 2)
				col |= COL_ADDR(i, val);
			else
				row |= ROW_ADDR(i - 2, val);
		}
		code |= COMMAND_NADDR_BYTES(naddrs);

		instr = vf610_get_next_instr(subop, &op_id);
	}

	if (instr && instr->type == NAND_OP_DATA_OUT_INSTR) {
		trfr_sz = nand_subop_get_data_len(subop, op_id);
		offset = nand_subop_get_data_start_off(subop, op_id);
		force8bit = instr->ctx.data.force_8bit;

		/*
		 * Don't fix endianness on page access for historical reasons.
		 * See comment in vf610_nfc_wr_to_sram
		 */
		vf610_nfc_wr_to_sram(nfc->regs + NFC_MAIN_AREA(0) + offset,
				     instr->ctx.data.buf.out + offset,
				     trfr_sz, !nfc->data_access);
		code |= COMMAND_WRITE_DATA;

		instr = vf610_get_next_instr(subop, &op_id);
	}

	if (instr && instr->type == NAND_OP_CMD_INSTR) {
		cmd1 |= instr->ctx.cmd.opcode << CMD_BYTE2_SHIFT;
		code |= COMMAND_CMD_BYTE2;

		instr = vf610_get_next_instr(subop, &op_id);
	}

	if (instr && instr->type == NAND_OP_WAITRDY_INSTR) {
		code |= COMMAND_RB_HANDSHAKE;

		instr = vf610_get_next_instr(subop, &op_id);
	}

	if (instr && instr->type == NAND_OP_DATA_IN_INSTR) {
		trfr_sz = nand_subop_get_data_len(subop, op_id);
		offset = nand_subop_get_data_start_off(subop, op_id);
		force8bit = instr->ctx.data.force_8bit;

		code |= COMMAND_READ_DATA;
	}

	if (force8bit && (chip->options & NAND_BUSWIDTH_16))
		vf610_nfc_clear(nfc, NFC_FLASH_CONFIG, CONFIG_16BIT);

	cmd2 |= code << CMD_CODE_SHIFT;

	vf610_nfc_run(nfc, col, row, cmd1, cmd2, trfr_sz);

	if (instr && instr->type == NAND_OP_DATA_IN_INSTR) {
		/*
		 * Don't fix endianness on page access for historical reasons.
		 * See comment in vf610_nfc_rd_from_sram
		 */
		vf610_nfc_rd_from_sram(instr->ctx.data.buf.in + offset,
				       nfc->regs + NFC_MAIN_AREA(0) + offset,
				       trfr_sz, !nfc->data_access);
	}

	if (force8bit && (chip->options & NAND_BUSWIDTH_16))
		vf610_nfc_set(nfc, NFC_FLASH_CONFIG, CONFIG_16BIT);

	return 0;
}

static const struct nand_op_parser vf610_nfc_op_parser = NAND_OP_PARSER(
	NAND_OP_PARSER_PATTERN(vf610_nfc_cmd,
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_ADDR_ELEM(true, 5),
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(true, PAGE_2K + OOB_MAX),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)),
	NAND_OP_PARSER_PATTERN(vf610_nfc_cmd,
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_ADDR_ELEM(true, 5),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(true, PAGE_2K + OOB_MAX)),
	);

/*
 * This function supports Vybrid only (MPC5125 would have full RB and four CS)
 */
static void vf610_nfc_select_target(struct nand_chip *chip, unsigned int cs)
{
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	u32 tmp;

	/* Vybrid only (MPC5125 would have full RB and four CS) */
	if (nfc->variant != NFC_VFC610)
		return;

	tmp = vf610_nfc_read(nfc, NFC_ROW_ADDR);
	tmp &= ~(ROW_ADDR_CHIP_SEL_RB_MASK | ROW_ADDR_CHIP_SEL_MASK);
	tmp |= 1 << ROW_ADDR_CHIP_SEL_RB_SHIFT;
	tmp |= BIT(cs) << ROW_ADDR_CHIP_SEL_SHIFT;

	vf610_nfc_write(nfc, NFC_ROW_ADDR, tmp);
}

static int vf610_nfc_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op,
			     bool check_only)
{
	if (!check_only)
		vf610_nfc_select_target(chip, op->cs);

	return nand_op_parser_exec_op(chip, &vf610_nfc_op_parser, op,
				      check_only);
}

static inline int vf610_nfc_correct_data(struct nand_chip *chip, uint8_t *dat,
					 uint8_t *oob, int page)
{
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 ecc_status_off = NFC_MAIN_AREA(0) + ECC_SRAM_ADDR + ECC_STATUS;
	u8 ecc_status;
	u8 ecc_count;
	int flips_threshold = nfc->chip.ecc.strength / 2;

	ecc_status = vf610_nfc_read(nfc, ecc_status_off) & 0xff;
	ecc_count = ecc_status & ECC_STATUS_ERR_COUNT;

	if (!(ecc_status & ECC_STATUS_MASK))
		return ecc_count;

	nfc->data_access = true;
	nand_read_oob_op(&nfc->chip, page, 0, oob, mtd->oobsize);
	nfc->data_access = false;

	/*
	 * On an erased page, bit count (including OOB) should be zero or
	 * at least less then half of the ECC strength.
	 */
	return nand_check_erased_ecc_chunk(dat, nfc->chip.ecc.size, oob,
					   mtd->oobsize, NULL, 0,
					   flips_threshold);
}

static void vf610_nfc_fill_row(struct nand_chip *chip, int page, u32 *code,
			       u32 *row)
{
	*row = ROW_ADDR(0, page & 0xff) | ROW_ADDR(1, page >> 8);
	*code |= COMMAND_RAR_BYTE1 | COMMAND_RAR_BYTE2;

	if (chip->options & NAND_ROW_ADDR_3) {
		*row |= ROW_ADDR(2, page >> 16);
		*code |= COMMAND_RAR_BYTE3;
	}
}

static int vf610_nfc_read_page(struct nand_chip *chip, uint8_t *buf,
			       int oob_required, int page)
{
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int trfr_sz = mtd->writesize + mtd->oobsize;
	u32 row = 0, cmd1 = 0, cmd2 = 0, code = 0;
	int stat;

	vf610_nfc_select_target(chip, chip->cur_cs);

	cmd2 |= NAND_CMD_READ0 << CMD_BYTE1_SHIFT;
	code |= COMMAND_CMD_BYTE1 | COMMAND_CAR_BYTE1 | COMMAND_CAR_BYTE2;

	vf610_nfc_fill_row(chip, page, &code, &row);

	cmd1 |= NAND_CMD_READSTART << CMD_BYTE2_SHIFT;
	code |= COMMAND_CMD_BYTE2 | COMMAND_RB_HANDSHAKE | COMMAND_READ_DATA;

	cmd2 |= code << CMD_CODE_SHIFT;

	vf610_nfc_ecc_mode(nfc, nfc->ecc_mode);
	vf610_nfc_run(nfc, 0, row, cmd1, cmd2, trfr_sz);
	vf610_nfc_ecc_mode(nfc, ECC_BYPASS);

	/*
	 * Don't fix endianness on page access for historical reasons.
	 * See comment in vf610_nfc_rd_from_sram
	 */
	vf610_nfc_rd_from_sram(buf, nfc->regs + NFC_MAIN_AREA(0),
			       mtd->writesize, false);
	if (oob_required)
		vf610_nfc_rd_from_sram(chip->oob_poi,
				       nfc->regs + NFC_MAIN_AREA(0) +
						   mtd->writesize,
				       mtd->oobsize, false);

	stat = vf610_nfc_correct_data(chip, buf, chip->oob_poi, page);

	if (stat < 0) {
		mtd->ecc_stats.failed++;
		return 0;
	} else {
		mtd->ecc_stats.corrected += stat;
		return stat;
	}
}

static int vf610_nfc_write_page(struct nand_chip *chip, const uint8_t *buf,
				int oob_required, int page)
{
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int trfr_sz = mtd->writesize + mtd->oobsize;
	u32 row = 0, cmd1 = 0, cmd2 = 0, code = 0;
	u8 status;
	int ret;

	vf610_nfc_select_target(chip, chip->cur_cs);

	cmd2 |= NAND_CMD_SEQIN << CMD_BYTE1_SHIFT;
	code |= COMMAND_CMD_BYTE1 | COMMAND_CAR_BYTE1 | COMMAND_CAR_BYTE2;

	vf610_nfc_fill_row(chip, page, &code, &row);

	cmd1 |= NAND_CMD_PAGEPROG << CMD_BYTE2_SHIFT;
	code |= COMMAND_CMD_BYTE2 | COMMAND_WRITE_DATA;

	/*
	 * Don't fix endianness on page access for historical reasons.
	 * See comment in vf610_nfc_wr_to_sram
	 */
	vf610_nfc_wr_to_sram(nfc->regs + NFC_MAIN_AREA(0), buf,
			     mtd->writesize, false);

	code |= COMMAND_RB_HANDSHAKE;
	cmd2 |= code << CMD_CODE_SHIFT;

	vf610_nfc_ecc_mode(nfc, nfc->ecc_mode);
	vf610_nfc_run(nfc, 0, row, cmd1, cmd2, trfr_sz);
	vf610_nfc_ecc_mode(nfc, ECC_BYPASS);

	ret = nand_status_op(chip, &status);
	if (ret)
		return ret;

	if (status & NAND_STATUS_FAIL)
		return -EIO;

	return 0;
}

static int vf610_nfc_read_page_raw(struct nand_chip *chip, u8 *buf,
				   int oob_required, int page)
{
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	int ret;

	nfc->data_access = true;
	ret = nand_read_page_raw(chip, buf, oob_required, page);
	nfc->data_access = false;

	return ret;
}

static int vf610_nfc_write_page_raw(struct nand_chip *chip, const u8 *buf,
				    int oob_required, int page)
{
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	nfc->data_access = true;
	ret = nand_prog_page_begin_op(chip, page, 0, buf, mtd->writesize);
	if (!ret && oob_required)
		ret = nand_write_data_op(chip, chip->oob_poi, mtd->oobsize,
					 false);
	nfc->data_access = false;

	if (ret)
		return ret;

	return nand_prog_page_end_op(chip);
}

static int vf610_nfc_read_oob(struct nand_chip *chip, int page)
{
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	int ret;

	nfc->data_access = true;
	ret = nand_read_oob_std(chip, page);
	nfc->data_access = false;

	return ret;
}

static int vf610_nfc_write_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct vf610_nfc *nfc = chip_to_nfc(chip);
	int ret;

	nfc->data_access = true;
	ret = nand_prog_page_begin_op(chip, page, mtd->writesize,
				      chip->oob_poi, mtd->oobsize);
	nfc->data_access = false;

	if (ret)
		return ret;

	return nand_prog_page_end_op(chip);
}

static const struct of_device_id vf610_nfc_dt_ids[] = {
	{ .compatible = "fsl,vf610-nfc", .data = (void *)NFC_VFC610 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vf610_nfc_dt_ids);

static void vf610_nfc_preinit_controller(struct vf610_nfc *nfc)
{
	vf610_nfc_clear(nfc, NFC_FLASH_CONFIG, CONFIG_16BIT);
	vf610_nfc_clear(nfc, NFC_FLASH_CONFIG, CONFIG_ADDR_AUTO_INCR_BIT);
	vf610_nfc_clear(nfc, NFC_FLASH_CONFIG, CONFIG_BUFNO_AUTO_INCR_BIT);
	vf610_nfc_clear(nfc, NFC_FLASH_CONFIG, CONFIG_BOOT_MODE_BIT);
	vf610_nfc_clear(nfc, NFC_FLASH_CONFIG, CONFIG_DMA_REQ_BIT);
	vf610_nfc_set(nfc, NFC_FLASH_CONFIG, CONFIG_FAST_FLASH_BIT);
	vf610_nfc_ecc_mode(nfc, ECC_BYPASS);

	/* Disable virtual pages, only one elementary transfer unit */
	vf610_nfc_set_field(nfc, NFC_FLASH_CONFIG, CONFIG_PAGE_CNT_MASK,
			    CONFIG_PAGE_CNT_SHIFT, 1);
}

static void vf610_nfc_init_controller(struct vf610_nfc *nfc)
{
	if (nfc->chip.options & NAND_BUSWIDTH_16)
		vf610_nfc_set(nfc, NFC_FLASH_CONFIG, CONFIG_16BIT);
	else
		vf610_nfc_clear(nfc, NFC_FLASH_CONFIG, CONFIG_16BIT);

	if (nfc->chip.ecc.engine_type == NAND_ECC_ENGINE_TYPE_ON_HOST) {
		/* Set ECC status offset in SRAM */
		vf610_nfc_set_field(nfc, NFC_FLASH_CONFIG,
				    CONFIG_ECC_SRAM_ADDR_MASK,
				    CONFIG_ECC_SRAM_ADDR_SHIFT,
				    ECC_SRAM_ADDR >> 3);

		/* Enable ECC status in SRAM */
		vf610_nfc_set(nfc, NFC_FLASH_CONFIG, CONFIG_ECC_SRAM_REQ_BIT);
	}
}

static int vf610_nfc_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct vf610_nfc *nfc = chip_to_nfc(chip);

	vf610_nfc_init_controller(nfc);

	/* Bad block options. */
	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	/* Single buffer only, max 256 OOB minus ECC status */
	if (mtd->writesize + mtd->oobsize > PAGE_2K + OOB_MAX - 8) {
		dev_err(nfc->dev, "Unsupported flash page size\n");
		return -ENXIO;
	}

	if (chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_ON_HOST)
		return 0;

	if (mtd->writesize != PAGE_2K && mtd->oobsize < 64) {
		dev_err(nfc->dev, "Unsupported flash with hwecc\n");
		return -ENXIO;
	}

	if (chip->ecc.size != mtd->writesize) {
		dev_err(nfc->dev, "Step size needs to be page size\n");
		return -ENXIO;
	}

	/* Only 64 byte ECC layouts known */
	if (mtd->oobsize > 64)
		mtd->oobsize = 64;

	/* Use default large page ECC layout defined in NAND core */
	mtd_set_ooblayout(mtd, nand_get_large_page_ooblayout());
	if (chip->ecc.strength == 32) {
		nfc->ecc_mode = ECC_60_BYTE;
		chip->ecc.bytes = 60;
	} else if (chip->ecc.strength == 24) {
		nfc->ecc_mode = ECC_45_BYTE;
		chip->ecc.bytes = 45;
	} else {
		dev_err(nfc->dev, "Unsupported ECC strength\n");
		return -ENXIO;
	}

	chip->ecc.read_page = vf610_nfc_read_page;
	chip->ecc.write_page = vf610_nfc_write_page;
	chip->ecc.read_page_raw = vf610_nfc_read_page_raw;
	chip->ecc.write_page_raw = vf610_nfc_write_page_raw;
	chip->ecc.read_oob = vf610_nfc_read_oob;
	chip->ecc.write_oob = vf610_nfc_write_oob;

	chip->ecc.size = PAGE_2K;

	return 0;
}

static const struct nand_controller_ops vf610_nfc_controller_ops = {
	.attach_chip = vf610_nfc_attach_chip,
	.exec_op = vf610_nfc_exec_op,

};

static int vf610_nfc_probe(struct platform_device *pdev)
{
	struct vf610_nfc *nfc;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	struct device_node *child;
	const struct of_device_id *of_id;
	int err;
	int irq;

	nfc = devm_kzalloc(&pdev->dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->dev = &pdev->dev;
	chip = &nfc->chip;
	mtd = nand_to_mtd(chip);

	mtd->owner = THIS_MODULE;
	mtd->dev.parent = nfc->dev;
	mtd->name = DRV_NAME;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	nfc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nfc->regs))
		return PTR_ERR(nfc->regs);

	nfc->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(nfc->clk)) {
		dev_err(nfc->dev, "Unable to get and enable clock!\n");
		return PTR_ERR(nfc->clk);
	}

	of_id = of_match_device(vf610_nfc_dt_ids, &pdev->dev);
	if (!of_id)
		return -ENODEV;

	nfc->variant = (uintptr_t)of_id->data;

	for_each_available_child_of_node(nfc->dev->of_node, child) {
		if (of_device_is_compatible(child, "fsl,vf610-nfc-nandcs")) {

			if (nand_get_flash_node(chip)) {
				dev_err(nfc->dev,
					"Only one NAND chip supported!\n");
				of_node_put(child);
				return -EINVAL;
			}

			nand_set_flash_node(chip, child);
		}
	}

	if (!nand_get_flash_node(chip)) {
		dev_err(nfc->dev, "NAND chip sub-node missing!\n");
		return -ENODEV;
	}

	chip->options |= NAND_NO_SUBPAGE_WRITE;

	init_completion(&nfc->cmd_done);

	err = devm_request_irq(nfc->dev, irq, vf610_nfc_irq, 0, DRV_NAME, nfc);
	if (err) {
		dev_err(nfc->dev, "Error requesting IRQ!\n");
		return err;
	}

	vf610_nfc_preinit_controller(nfc);

	nand_controller_init(&nfc->base);
	nfc->base.ops = &vf610_nfc_controller_ops;
	chip->controller = &nfc->base;

	/* Scan the NAND chip */
	err = nand_scan(chip, 1);
	if (err)
		return err;

	platform_set_drvdata(pdev, nfc);

	/* Register device in MTD */
	err = mtd_device_register(mtd, NULL, 0);
	if (err)
		goto err_cleanup_nand;
	return 0;

err_cleanup_nand:
	nand_cleanup(chip);
	return err;
}

static void vf610_nfc_remove(struct platform_device *pdev)
{
	struct vf610_nfc *nfc = platform_get_drvdata(pdev);
	struct nand_chip *chip = &nfc->chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
}

#ifdef CONFIG_PM_SLEEP
static int vf610_nfc_suspend(struct device *dev)
{
	struct vf610_nfc *nfc = dev_get_drvdata(dev);

	clk_disable_unprepare(nfc->clk);
	return 0;
}

static int vf610_nfc_resume(struct device *dev)
{
	struct vf610_nfc *nfc = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(nfc->clk);
	if (err)
		return err;

	vf610_nfc_preinit_controller(nfc);
	vf610_nfc_init_controller(nfc);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(vf610_nfc_pm_ops, vf610_nfc_suspend, vf610_nfc_resume);

static struct platform_driver vf610_nfc_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = vf610_nfc_dt_ids,
		.pm	= &vf610_nfc_pm_ops,
	},
	.probe		= vf610_nfc_probe,
	.remove_new	= vf610_nfc_remove,
};

module_platform_driver(vf610_nfc_driver);

MODULE_AUTHOR("Stefan Agner <stefan.agner@toradex.com>");
MODULE_DESCRIPTION("Freescale VF610/MPC5125 NFC MTD NAND driver");
MODULE_LICENSE("GPL");
