// SPDX-License-Identifier: GPL-2.0
/*
 * ARM PL35X NAND flash controller driver
 *
 * Copyright (C) 2017 Xilinx, Inc
 * Author:
 *   Miquel Raynal <miquel.raynal@bootlin.com>
 * Original work (rewritten):
 *   Punnaiah Choudary Kalluri <punnaia@xilinx.com>
 *   Naga Sureshkumar Relli <nagasure@xilinx.com>
 */

#include <linux/amba/bus.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#define PL35X_NANDC_DRIVER_NAME "pl35x-nand-controller"

/* SMC controller status register (RO) */
#define PL35X_SMC_MEMC_STATUS 0x0
#define   PL35X_SMC_MEMC_STATUS_RAW_INT_STATUS1	BIT(6)
/* SMC clear config register (WO) */
#define PL35X_SMC_MEMC_CFG_CLR 0xC
#define   PL35X_SMC_MEMC_CFG_CLR_INT_DIS_1	BIT(1)
#define   PL35X_SMC_MEMC_CFG_CLR_INT_CLR_1	BIT(4)
#define   PL35X_SMC_MEMC_CFG_CLR_ECC_INT_DIS_1	BIT(6)
/* SMC direct command register (WO) */
#define PL35X_SMC_DIRECT_CMD 0x10
#define   PL35X_SMC_DIRECT_CMD_NAND_CS (0x4 << 23)
#define   PL35X_SMC_DIRECT_CMD_UPD_REGS (0x2 << 21)
/* SMC set cycles register (WO) */
#define PL35X_SMC_CYCLES 0x14
#define   PL35X_SMC_NAND_TRC_CYCLES(x) ((x) << 0)
#define   PL35X_SMC_NAND_TWC_CYCLES(x) ((x) << 4)
#define   PL35X_SMC_NAND_TREA_CYCLES(x) ((x) << 8)
#define   PL35X_SMC_NAND_TWP_CYCLES(x) ((x) << 11)
#define   PL35X_SMC_NAND_TCLR_CYCLES(x) ((x) << 14)
#define   PL35X_SMC_NAND_TAR_CYCLES(x) ((x) << 17)
#define   PL35X_SMC_NAND_TRR_CYCLES(x) ((x) << 20)
/* SMC set opmode register (WO) */
#define PL35X_SMC_OPMODE 0x18
#define   PL35X_SMC_OPMODE_BW_8 0
#define   PL35X_SMC_OPMODE_BW_16 1
/* SMC ECC status register (RO) */
#define PL35X_SMC_ECC_STATUS 0x400
#define   PL35X_SMC_ECC_STATUS_ECC_BUSY BIT(6)
/* SMC ECC configuration register */
#define PL35X_SMC_ECC_CFG 0x404
#define   PL35X_SMC_ECC_CFG_MODE_MASK 0xC
#define   PL35X_SMC_ECC_CFG_MODE_BYPASS 0
#define   PL35X_SMC_ECC_CFG_MODE_APB BIT(2)
#define   PL35X_SMC_ECC_CFG_MODE_MEM BIT(3)
#define   PL35X_SMC_ECC_CFG_PGSIZE_MASK	0x3
/* SMC ECC command 1 register */
#define PL35X_SMC_ECC_CMD1 0x408
#define   PL35X_SMC_ECC_CMD1_WRITE(x) ((x) << 0)
#define   PL35X_SMC_ECC_CMD1_READ(x) ((x) << 8)
#define   PL35X_SMC_ECC_CMD1_READ_END(x) ((x) << 16)
#define   PL35X_SMC_ECC_CMD1_READ_END_VALID(x) ((x) << 24)
/* SMC ECC command 2 register */
#define PL35X_SMC_ECC_CMD2 0x40C
#define   PL35X_SMC_ECC_CMD2_WRITE_COL_CHG(x) ((x) << 0)
#define   PL35X_SMC_ECC_CMD2_READ_COL_CHG(x) ((x) << 8)
#define   PL35X_SMC_ECC_CMD2_READ_COL_CHG_END(x) ((x) << 16)
#define   PL35X_SMC_ECC_CMD2_READ_COL_CHG_END_VALID(x) ((x) << 24)
/* SMC ECC value registers (RO) */
#define PL35X_SMC_ECC_VALUE(x) (0x418 + (4 * (x)))
#define   PL35X_SMC_ECC_VALUE_IS_CORRECTABLE(x) ((x) & BIT(27))
#define   PL35X_SMC_ECC_VALUE_HAS_FAILED(x) ((x) & BIT(28))
#define   PL35X_SMC_ECC_VALUE_IS_VALID(x) ((x) & BIT(30))

/* NAND AXI interface */
#define PL35X_SMC_CMD_PHASE 0
#define PL35X_SMC_CMD_PHASE_CMD0(x) ((x) << 3)
#define PL35X_SMC_CMD_PHASE_CMD1(x) ((x) << 11)
#define PL35X_SMC_CMD_PHASE_CMD1_VALID BIT(20)
#define PL35X_SMC_CMD_PHASE_ADDR(pos, x) ((x) << (8 * (pos)))
#define PL35X_SMC_CMD_PHASE_NADDRS(x) ((x) << 21)
#define PL35X_SMC_DATA_PHASE BIT(19)
#define PL35X_SMC_DATA_PHASE_ECC_LAST BIT(10)
#define PL35X_SMC_DATA_PHASE_CLEAR_CS BIT(21)

#define PL35X_NAND_MAX_CS 1
#define PL35X_NAND_LAST_XFER_SZ 4
#define TO_CYCLES(ps, period_ns) (DIV_ROUND_UP((ps) / 1000, period_ns))

#define PL35X_NAND_ECC_BITS_MASK 0xFFF
#define PL35X_NAND_ECC_BYTE_OFF_MASK 0x1FF
#define PL35X_NAND_ECC_BIT_OFF_MASK 0x7

struct pl35x_nand_timings {
	unsigned int t_rc:4;
	unsigned int t_wc:4;
	unsigned int t_rea:3;
	unsigned int t_wp:3;
	unsigned int t_clr:3;
	unsigned int t_ar:3;
	unsigned int t_rr:4;
	unsigned int rsvd:8;
};

struct pl35x_nand {
	struct list_head node;
	struct nand_chip chip;
	unsigned int cs;
	unsigned int addr_cycles;
	u32 ecc_cfg;
	u32 timings;
};

/**
 * struct pl35x_nandc - NAND flash controller driver structure
 * @dev: Kernel device
 * @conf_regs: SMC configuration registers for command phase
 * @io_regs: NAND data registers for data phase
 * @controller: Core NAND controller structure
 * @chips: List of connected NAND chips
 * @selected_chip: NAND chip currently selected by the controller
 * @assigned_cs: List of assigned CS
 * @ecc_buf: Temporary buffer to extract ECC bytes
 */
struct pl35x_nandc {
	struct device *dev;
	void __iomem *conf_regs;
	void __iomem *io_regs;
	struct nand_controller controller;
	struct list_head chips;
	struct nand_chip *selected_chip;
	unsigned long assigned_cs;
	u8 *ecc_buf;
};

static inline struct pl35x_nandc *to_pl35x_nandc(struct nand_controller *ctrl)
{
	return container_of(ctrl, struct pl35x_nandc, controller);
}

static inline struct pl35x_nand *to_pl35x_nand(struct nand_chip *chip)
{
	return container_of(chip, struct pl35x_nand, chip);
}

static int pl35x_ecc_ooblayout16_ecc(struct mtd_info *mtd, int section,
				     struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * chip->ecc.bytes);
	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int pl35x_ecc_ooblayout16_free(struct mtd_info *mtd, int section,
				      struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * chip->ecc.bytes) + 8;
	oobregion->length = 8;

	return 0;
}

static const struct mtd_ooblayout_ops pl35x_ecc_ooblayout16_ops = {
	.ecc = pl35x_ecc_ooblayout16_ecc,
	.free = pl35x_ecc_ooblayout16_free,
};

/* Generic flash bbt descriptors */
static u8 bbt_pattern[] = { 'B', 'b', 't', '0' };
static u8 mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 4,
	.len = 4,
	.veroffs = 20,
	.maxblocks = 4,
	.pattern = bbt_pattern
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 4,
	.len = 4,
	.veroffs = 20,
	.maxblocks = 4,
	.pattern = mirror_pattern
};

static void pl35x_smc_update_regs(struct pl35x_nandc *nfc)
{
	writel(PL35X_SMC_DIRECT_CMD_NAND_CS |
	       PL35X_SMC_DIRECT_CMD_UPD_REGS,
	       nfc->conf_regs + PL35X_SMC_DIRECT_CMD);
}

static int pl35x_smc_set_buswidth(struct pl35x_nandc *nfc, unsigned int bw)
{
	if (bw != PL35X_SMC_OPMODE_BW_8 && bw != PL35X_SMC_OPMODE_BW_16)
		return -EINVAL;

	writel(bw, nfc->conf_regs + PL35X_SMC_OPMODE);
	pl35x_smc_update_regs(nfc);

	return 0;
}

static void pl35x_smc_clear_irq(struct pl35x_nandc *nfc)
{
	writel(PL35X_SMC_MEMC_CFG_CLR_INT_CLR_1,
	       nfc->conf_regs + PL35X_SMC_MEMC_CFG_CLR);
}

static int pl35x_smc_wait_for_irq(struct pl35x_nandc *nfc)
{
	u32 reg;
	int ret;

	ret = readl_poll_timeout(nfc->conf_regs + PL35X_SMC_MEMC_STATUS, reg,
				 reg & PL35X_SMC_MEMC_STATUS_RAW_INT_STATUS1,
				 10, 1000000);
	if (ret)
		dev_err(nfc->dev,
			"Timeout polling on NAND controller interrupt (0x%x)\n",
			reg);

	pl35x_smc_clear_irq(nfc);

	return ret;
}

static int pl35x_smc_wait_for_ecc_done(struct pl35x_nandc *nfc)
{
	u32 reg;
	int ret;

	ret = readl_poll_timeout(nfc->conf_regs + PL35X_SMC_ECC_STATUS, reg,
				 !(reg & PL35X_SMC_ECC_STATUS_ECC_BUSY),
				 10, 1000000);
	if (ret)
		dev_err(nfc->dev,
			"Timeout polling on ECC controller interrupt\n");

	return ret;
}

static int pl35x_smc_set_ecc_mode(struct pl35x_nandc *nfc,
				  struct nand_chip *chip,
				  unsigned int mode)
{
	struct pl35x_nand *plnand;
	u32 ecc_cfg;

	ecc_cfg = readl(nfc->conf_regs + PL35X_SMC_ECC_CFG);
	ecc_cfg &= ~PL35X_SMC_ECC_CFG_MODE_MASK;
	ecc_cfg |= mode;
	writel(ecc_cfg, nfc->conf_regs + PL35X_SMC_ECC_CFG);

	if (chip) {
		plnand = to_pl35x_nand(chip);
		plnand->ecc_cfg = ecc_cfg;
	}

	if (mode != PL35X_SMC_ECC_CFG_MODE_BYPASS)
		return pl35x_smc_wait_for_ecc_done(nfc);

	return 0;
}

static void pl35x_smc_force_byte_access(struct nand_chip *chip,
					bool force_8bit)
{
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	int ret;

	if (!(chip->options & NAND_BUSWIDTH_16))
		return;

	if (force_8bit)
		ret = pl35x_smc_set_buswidth(nfc, PL35X_SMC_OPMODE_BW_8);
	else
		ret = pl35x_smc_set_buswidth(nfc, PL35X_SMC_OPMODE_BW_16);

	if (ret)
		dev_err(nfc->dev, "Error in Buswidth\n");
}

static void pl35x_nand_select_target(struct nand_chip *chip,
				     unsigned int die_nr)
{
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	struct pl35x_nand *plnand = to_pl35x_nand(chip);

	if (chip == nfc->selected_chip)
		return;

	/* Setup the timings */
	writel(plnand->timings, nfc->conf_regs + PL35X_SMC_CYCLES);
	pl35x_smc_update_regs(nfc);

	/* Configure the ECC engine */
	writel(plnand->ecc_cfg, nfc->conf_regs + PL35X_SMC_ECC_CFG);

	nfc->selected_chip = chip;
}

static void pl35x_nand_read_data_op(struct nand_chip *chip, u8 *in,
				    unsigned int len, bool force_8bit,
				    unsigned int flags, unsigned int last_flags)
{
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	unsigned int buf_end = len / 4;
	unsigned int in_start = round_down(len, 4);
	unsigned int data_phase_addr;
	u32 *buf32 = (u32 *)in;
	u8 *buf8 = (u8 *)in;
	int i;

	if (force_8bit)
		pl35x_smc_force_byte_access(chip, true);

	for (i = 0; i < buf_end; i++) {
		data_phase_addr = PL35X_SMC_DATA_PHASE + flags;
		if (i + 1 == buf_end)
			data_phase_addr = PL35X_SMC_DATA_PHASE + last_flags;

		buf32[i] = readl(nfc->io_regs + data_phase_addr);
	}

	/* No working extra flags on unaligned data accesses */
	for (i = in_start; i < len; i++)
		buf8[i] = readb(nfc->io_regs + PL35X_SMC_DATA_PHASE);

	if (force_8bit)
		pl35x_smc_force_byte_access(chip, false);
}

static void pl35x_nand_write_data_op(struct nand_chip *chip, const u8 *out,
				     int len, bool force_8bit,
				     unsigned int flags,
				     unsigned int last_flags)
{
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	unsigned int buf_end = len / 4;
	unsigned int in_start = round_down(len, 4);
	const u32 *buf32 = (const u32 *)out;
	const u8 *buf8 = (const u8 *)out;
	unsigned int data_phase_addr;
	int i;

	if (force_8bit)
		pl35x_smc_force_byte_access(chip, true);

	for (i = 0; i < buf_end; i++) {
		data_phase_addr = PL35X_SMC_DATA_PHASE + flags;
		if (i + 1 == buf_end)
			data_phase_addr = PL35X_SMC_DATA_PHASE + last_flags;

		writel(buf32[i], nfc->io_regs + data_phase_addr);
	}

	/* No working extra flags on unaligned data accesses */
	for (i = in_start; i < len; i++)
		writeb(buf8[i], nfc->io_regs + PL35X_SMC_DATA_PHASE);

	if (force_8bit)
		pl35x_smc_force_byte_access(chip, false);
}

static int pl35x_nand_correct_data(struct pl35x_nandc *nfc, unsigned char *buf,
				   unsigned char *read_ecc,
				   unsigned char *calc_ecc)
{
	unsigned short ecc_odd, ecc_even, read_ecc_lower, read_ecc_upper;
	unsigned short calc_ecc_lower, calc_ecc_upper;
	unsigned short byte_addr, bit_addr;

	read_ecc_lower = (read_ecc[0] | (read_ecc[1] << 8)) &
			 PL35X_NAND_ECC_BITS_MASK;
	read_ecc_upper = ((read_ecc[1] >> 4) | (read_ecc[2] << 4)) &
			 PL35X_NAND_ECC_BITS_MASK;

	calc_ecc_lower = (calc_ecc[0] | (calc_ecc[1] << 8)) &
			 PL35X_NAND_ECC_BITS_MASK;
	calc_ecc_upper = ((calc_ecc[1] >> 4) | (calc_ecc[2] << 4)) &
			 PL35X_NAND_ECC_BITS_MASK;

	ecc_odd = read_ecc_lower ^ calc_ecc_lower;
	ecc_even = read_ecc_upper ^ calc_ecc_upper;

	/* No error */
	if (likely(!ecc_odd && !ecc_even))
		return 0;

	/* One error in the main data; to be corrected */
	if (ecc_odd == (~ecc_even & PL35X_NAND_ECC_BITS_MASK)) {
		/* Bits [11:3] of error code give the byte offset */
		byte_addr = (ecc_odd >> 3) & PL35X_NAND_ECC_BYTE_OFF_MASK;
		/* Bits [2:0] of error code give the bit offset */
		bit_addr = ecc_odd & PL35X_NAND_ECC_BIT_OFF_MASK;
		/* Toggle the faulty bit */
		buf[byte_addr] ^= (BIT(bit_addr));

		return 1;
	}

	/* One error in the ECC data; no action needed */
	if (hweight32(ecc_odd | ecc_even) == 1)
		return 1;

	return -EBADMSG;
}

static void pl35x_nand_ecc_reg_to_array(struct nand_chip *chip, u32 ecc_reg,
					u8 *ecc_array)
{
	u32 ecc_value = ~ecc_reg;
	unsigned int ecc_byte;

	for (ecc_byte = 0; ecc_byte < chip->ecc.bytes; ecc_byte++)
		ecc_array[ecc_byte] = ecc_value >> (8 * ecc_byte);
}

static int pl35x_nand_read_eccbytes(struct pl35x_nandc *nfc,
				    struct nand_chip *chip, u8 *read_ecc)
{
	u32 ecc_value;
	int chunk;

	for (chunk = 0; chunk < chip->ecc.steps;
	     chunk++, read_ecc += chip->ecc.bytes) {
		ecc_value = readl(nfc->conf_regs + PL35X_SMC_ECC_VALUE(chunk));
		if (!PL35X_SMC_ECC_VALUE_IS_VALID(ecc_value))
			return -EINVAL;

		pl35x_nand_ecc_reg_to_array(chip, ecc_value, read_ecc);
	}

	return 0;
}

static int pl35x_nand_recover_data_hwecc(struct pl35x_nandc *nfc,
					 struct nand_chip *chip, u8 *data,
					 u8 *read_ecc)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int max_bitflips = 0, chunk;
	u8 calc_ecc[3];
	u32 ecc_value;
	int stats;

	for (chunk = 0; chunk < chip->ecc.steps;
	     chunk++, data += chip->ecc.size, read_ecc += chip->ecc.bytes) {
		/* Read ECC value for each chunk */
		ecc_value = readl(nfc->conf_regs + PL35X_SMC_ECC_VALUE(chunk));

		if (!PL35X_SMC_ECC_VALUE_IS_VALID(ecc_value))
			return -EINVAL;

		if (PL35X_SMC_ECC_VALUE_HAS_FAILED(ecc_value)) {
			mtd->ecc_stats.failed++;
			continue;
		}

		pl35x_nand_ecc_reg_to_array(chip, ecc_value, calc_ecc);
		stats = pl35x_nand_correct_data(nfc, data, read_ecc, calc_ecc);
		if (stats < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stats;
			max_bitflips = max_t(unsigned int, max_bitflips, stats);
		}
	}

	return max_bitflips;
}

static int pl35x_nand_write_page_hwecc(struct nand_chip *chip,
				       const u8 *buf, int oob_required,
				       int page)
{
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	struct pl35x_nand *plnand = to_pl35x_nand(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int first_row = (mtd->writesize <= 512) ? 1 : 2;
	unsigned int nrows = plnand->addr_cycles;
	u32 addr1 = 0, addr2 = 0, row;
	u32 cmd_addr;
	int i, ret;
	u8 status;

	ret = pl35x_smc_set_ecc_mode(nfc, chip, PL35X_SMC_ECC_CFG_MODE_APB);
	if (ret)
		return ret;

	cmd_addr = PL35X_SMC_CMD_PHASE |
		   PL35X_SMC_CMD_PHASE_NADDRS(plnand->addr_cycles) |
		   PL35X_SMC_CMD_PHASE_CMD0(NAND_CMD_SEQIN);

	for (i = 0, row = first_row; row < nrows; i++, row++) {
		u8 addr = page >> ((i * 8) & 0xFF);

		if (row < 4)
			addr1 |= PL35X_SMC_CMD_PHASE_ADDR(row, addr);
		else
			addr2 |= PL35X_SMC_CMD_PHASE_ADDR(row - 4, addr);
	}

	/* Send the command and address cycles */
	writel(addr1, nfc->io_regs + cmd_addr);
	if (plnand->addr_cycles > 4)
		writel(addr2, nfc->io_regs + cmd_addr);

	/* Write the data with the engine enabled */
	pl35x_nand_write_data_op(chip, buf, mtd->writesize, false,
				 0, PL35X_SMC_DATA_PHASE_ECC_LAST);
	ret = pl35x_smc_wait_for_ecc_done(nfc);
	if (ret)
		goto disable_ecc_engine;

	/* Copy the HW calculated ECC bytes in the OOB buffer */
	ret = pl35x_nand_read_eccbytes(nfc, chip, nfc->ecc_buf);
	if (ret)
		goto disable_ecc_engine;

	if (!oob_required)
		memset(chip->oob_poi, 0xFF, mtd->oobsize);

	ret = mtd_ooblayout_set_eccbytes(mtd, nfc->ecc_buf, chip->oob_poi,
					 0, chip->ecc.total);
	if (ret)
		goto disable_ecc_engine;

	/* Write the spare area with ECC bytes */
	pl35x_nand_write_data_op(chip, chip->oob_poi, mtd->oobsize, false, 0,
				 PL35X_SMC_CMD_PHASE_CMD1(NAND_CMD_PAGEPROG) |
				 PL35X_SMC_CMD_PHASE_CMD1_VALID |
				 PL35X_SMC_DATA_PHASE_CLEAR_CS);
	ret = pl35x_smc_wait_for_irq(nfc);
	if (ret)
		goto disable_ecc_engine;

	/* Check write status on the chip side */
	ret = nand_status_op(chip, &status);
	if (ret)
		goto disable_ecc_engine;

	if (status & NAND_STATUS_FAIL)
		ret = -EIO;

disable_ecc_engine:
	pl35x_smc_set_ecc_mode(nfc, chip, PL35X_SMC_ECC_CFG_MODE_BYPASS);

	return ret;
}

/*
 * This functions reads data and checks the data integrity by comparing hardware
 * generated ECC values and read ECC values from spare area.
 *
 * There is a limitation with SMC controller: ECC_LAST must be set on the
 * last data access to tell the ECC engine not to expect any further data.
 * In practice, this implies to shrink the last data transfert by eg. 4 bytes,
 * and doing a last 4-byte transfer with the additional bit set. The last block
 * should be aligned with the end of an ECC block. Because of this limitation,
 * it is not possible to use the core routines.
 */
static int pl35x_nand_read_page_hwecc(struct nand_chip *chip,
				      u8 *buf, int oob_required, int page)
{
	const struct nand_sdr_timings *sdr =
		nand_get_sdr_timings(nand_get_interface_config(chip));
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	struct pl35x_nand *plnand = to_pl35x_nand(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int first_row = (mtd->writesize <= 512) ? 1 : 2;
	unsigned int nrows = plnand->addr_cycles;
	unsigned int addr1 = 0, addr2 = 0, row;
	u32 cmd_addr;
	int i, ret;

	ret = pl35x_smc_set_ecc_mode(nfc, chip, PL35X_SMC_ECC_CFG_MODE_APB);
	if (ret)
		return ret;

	cmd_addr = PL35X_SMC_CMD_PHASE |
		   PL35X_SMC_CMD_PHASE_NADDRS(plnand->addr_cycles) |
		   PL35X_SMC_CMD_PHASE_CMD0(NAND_CMD_READ0) |
		   PL35X_SMC_CMD_PHASE_CMD1(NAND_CMD_READSTART) |
		   PL35X_SMC_CMD_PHASE_CMD1_VALID;

	for (i = 0, row = first_row; row < nrows; i++, row++) {
		u8 addr = page >> ((i * 8) & 0xFF);

		if (row < 4)
			addr1 |= PL35X_SMC_CMD_PHASE_ADDR(row, addr);
		else
			addr2 |= PL35X_SMC_CMD_PHASE_ADDR(row - 4, addr);
	}

	/* Send the command and address cycles */
	writel(addr1, nfc->io_regs + cmd_addr);
	if (plnand->addr_cycles > 4)
		writel(addr2, nfc->io_regs + cmd_addr);

	/* Wait the data to be available in the NAND cache */
	ndelay(PSEC_TO_NSEC(sdr->tRR_min));
	ret = pl35x_smc_wait_for_irq(nfc);
	if (ret)
		goto disable_ecc_engine;

	/* Retrieve the raw data with the engine enabled */
	pl35x_nand_read_data_op(chip, buf, mtd->writesize, false,
				0, PL35X_SMC_DATA_PHASE_ECC_LAST);
	ret = pl35x_smc_wait_for_ecc_done(nfc);
	if (ret)
		goto disable_ecc_engine;

	/* Retrieve the stored ECC bytes */
	pl35x_nand_read_data_op(chip, chip->oob_poi, mtd->oobsize, false,
				0, PL35X_SMC_DATA_PHASE_CLEAR_CS);
	ret = mtd_ooblayout_get_eccbytes(mtd, nfc->ecc_buf, chip->oob_poi, 0,
					 chip->ecc.total);
	if (ret)
		goto disable_ecc_engine;

	pl35x_smc_set_ecc_mode(nfc, chip, PL35X_SMC_ECC_CFG_MODE_BYPASS);

	/* Correct the data and report failures */
	return pl35x_nand_recover_data_hwecc(nfc, chip, buf, nfc->ecc_buf);

disable_ecc_engine:
	pl35x_smc_set_ecc_mode(nfc, chip, PL35X_SMC_ECC_CFG_MODE_BYPASS);

	return ret;
}

static int pl35x_nand_exec_op(struct nand_chip *chip,
			      const struct nand_subop *subop)
{
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	const struct nand_op_instr *instr, *data_instr = NULL;
	unsigned int rdy_tim_ms = 0, naddrs = 0, cmds = 0, last_flags = 0;
	u32 addr1 = 0, addr2 = 0, cmd0 = 0, cmd1 = 0, cmd_addr = 0;
	unsigned int op_id, len, offset, rdy_del_ns;
	int last_instr_type = -1;
	bool cmd1_valid = false;
	const u8 *addrs;
	int i, ret;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		instr = &subop->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (!cmds) {
				cmd0 = PL35X_SMC_CMD_PHASE_CMD0(instr->ctx.cmd.opcode);
			} else {
				cmd1 = PL35X_SMC_CMD_PHASE_CMD1(instr->ctx.cmd.opcode);
				if (last_instr_type != NAND_OP_DATA_OUT_INSTR)
					cmd1_valid = true;
			}
			cmds++;
			break;

		case NAND_OP_ADDR_INSTR:
			offset = nand_subop_get_addr_start_off(subop, op_id);
			naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
			addrs = &instr->ctx.addr.addrs[offset];
			cmd_addr |= PL35X_SMC_CMD_PHASE_NADDRS(naddrs);

			for (i = offset; i < naddrs; i++) {
				if (i < 4)
					addr1 |= PL35X_SMC_CMD_PHASE_ADDR(i, addrs[i]);
				else
					addr2 |= PL35X_SMC_CMD_PHASE_ADDR(i - 4, addrs[i]);
			}
			break;

		case NAND_OP_DATA_IN_INSTR:
		case NAND_OP_DATA_OUT_INSTR:
			data_instr = instr;
			len = nand_subop_get_data_len(subop, op_id);
			break;

		case NAND_OP_WAITRDY_INSTR:
			rdy_tim_ms = instr->ctx.waitrdy.timeout_ms;
			rdy_del_ns = instr->delay_ns;
			break;
		}

		last_instr_type = instr->type;
	}

	/* Command phase */
	cmd_addr |= PL35X_SMC_CMD_PHASE | cmd0 | cmd1 |
		    (cmd1_valid ? PL35X_SMC_CMD_PHASE_CMD1_VALID : 0);
	writel(addr1, nfc->io_regs + cmd_addr);
	if (naddrs > 4)
		writel(addr2, nfc->io_regs + cmd_addr);

	/* Data phase */
	if (data_instr && data_instr->type == NAND_OP_DATA_OUT_INSTR) {
		last_flags = PL35X_SMC_DATA_PHASE_CLEAR_CS;
		if (cmds == 2)
			last_flags |= cmd1 | PL35X_SMC_CMD_PHASE_CMD1_VALID;

		pl35x_nand_write_data_op(chip, data_instr->ctx.data.buf.out,
					 len, data_instr->ctx.data.force_8bit,
					 0, last_flags);
	}

	if (rdy_tim_ms) {
		ndelay(rdy_del_ns);
		ret = pl35x_smc_wait_for_irq(nfc);
		if (ret)
			return ret;
	}

	if (data_instr && data_instr->type == NAND_OP_DATA_IN_INSTR)
		pl35x_nand_read_data_op(chip, data_instr->ctx.data.buf.in,
					len, data_instr->ctx.data.force_8bit,
					0, PL35X_SMC_DATA_PHASE_CLEAR_CS);

	return 0;
}

static const struct nand_op_parser pl35x_nandc_op_parser = NAND_OP_PARSER(
	NAND_OP_PARSER_PATTERN(pl35x_nand_exec_op,
			       NAND_OP_PARSER_PAT_CMD_ELEM(true),
			       NAND_OP_PARSER_PAT_ADDR_ELEM(true, 7),
			       NAND_OP_PARSER_PAT_CMD_ELEM(true),
			       NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
			       NAND_OP_PARSER_PAT_DATA_IN_ELEM(true, 2112)),
	NAND_OP_PARSER_PATTERN(pl35x_nand_exec_op,
			       NAND_OP_PARSER_PAT_CMD_ELEM(false),
			       NAND_OP_PARSER_PAT_ADDR_ELEM(false, 7),
			       NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, 2112),
			       NAND_OP_PARSER_PAT_CMD_ELEM(false),
			       NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)),
	NAND_OP_PARSER_PATTERN(pl35x_nand_exec_op,
			       NAND_OP_PARSER_PAT_CMD_ELEM(false),
			       NAND_OP_PARSER_PAT_ADDR_ELEM(false, 7),
			       NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, 2112),
			       NAND_OP_PARSER_PAT_CMD_ELEM(true),
			       NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)),
	);

static int pl35x_nfc_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op,
			     bool check_only)
{
	if (!check_only)
		pl35x_nand_select_target(chip, op->cs);

	return nand_op_parser_exec_op(chip, &pl35x_nandc_op_parser,
				      op, check_only);
}

static int pl35x_nfc_setup_interface(struct nand_chip *chip, int cs,
				     const struct nand_interface_config *conf)
{
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	struct pl35x_nand *plnand = to_pl35x_nand(chip);
	struct pl35x_nand_timings tmgs = {};
	const struct nand_sdr_timings *sdr;
	unsigned int period_ns, val;
	struct clk *mclk;

	sdr = nand_get_sdr_timings(conf);
	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	mclk = of_clk_get_by_name(nfc->dev->parent->of_node, "memclk");
	if (IS_ERR(mclk)) {
		dev_err(nfc->dev, "Failed to retrieve SMC memclk\n");
		return PTR_ERR(mclk);
	}

	/*
	 * SDR timings are given in pico-seconds while NFC timings must be
	 * expressed in NAND controller clock cycles. We use the TO_CYCLE()
	 * macro to convert from one to the other.
	 */
	period_ns = NSEC_PER_SEC / clk_get_rate(mclk);

	/*
	 * PL35X SMC needs one extra read cycle in SDR Mode 5. This is not
	 * written anywhere in the datasheet but is an empirical observation.
	 */
	val = TO_CYCLES(sdr->tRC_min, period_ns);
	if (sdr->tRC_min <= 20000)
		val++;

	tmgs.t_rc = val;
	if (tmgs.t_rc != val || tmgs.t_rc < 2)
		return -EINVAL;

	val = TO_CYCLES(sdr->tWC_min, period_ns);
	tmgs.t_wc = val;
	if (tmgs.t_wc != val || tmgs.t_wc < 2)
		return -EINVAL;

	/*
	 * For all SDR modes, PL35X SMC needs tREA_max being 1,
	 * this is also an empirical result.
	 */
	tmgs.t_rea = 1;

	val = TO_CYCLES(sdr->tWP_min, period_ns);
	tmgs.t_wp = val;
	if (tmgs.t_wp != val || tmgs.t_wp < 1)
		return -EINVAL;

	val = TO_CYCLES(sdr->tCLR_min, period_ns);
	tmgs.t_clr = val;
	if (tmgs.t_clr != val)
		return -EINVAL;

	val = TO_CYCLES(sdr->tAR_min, period_ns);
	tmgs.t_ar = val;
	if (tmgs.t_ar != val)
		return -EINVAL;

	val = TO_CYCLES(sdr->tRR_min, period_ns);
	tmgs.t_rr = val;
	if (tmgs.t_rr != val)
		return -EINVAL;

	if (cs == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	plnand->timings = PL35X_SMC_NAND_TRC_CYCLES(tmgs.t_rc) |
			  PL35X_SMC_NAND_TWC_CYCLES(tmgs.t_wc) |
			  PL35X_SMC_NAND_TREA_CYCLES(tmgs.t_rea) |
			  PL35X_SMC_NAND_TWP_CYCLES(tmgs.t_wp) |
			  PL35X_SMC_NAND_TCLR_CYCLES(tmgs.t_clr) |
			  PL35X_SMC_NAND_TAR_CYCLES(tmgs.t_ar) |
			  PL35X_SMC_NAND_TRR_CYCLES(tmgs.t_rr);

	return 0;
}

static void pl35x_smc_set_ecc_pg_size(struct pl35x_nandc *nfc,
				      struct nand_chip *chip,
				      unsigned int pg_sz)
{
	struct pl35x_nand *plnand = to_pl35x_nand(chip);
	u32 sz;

	switch (pg_sz) {
	case SZ_512:
		sz = 1;
		break;
	case SZ_1K:
		sz = 2;
		break;
	case SZ_2K:
		sz = 3;
		break;
	default:
		sz = 0;
		break;
	}

	plnand->ecc_cfg = readl(nfc->conf_regs + PL35X_SMC_ECC_CFG);
	plnand->ecc_cfg &= ~PL35X_SMC_ECC_CFG_PGSIZE_MASK;
	plnand->ecc_cfg |= sz;
	writel(plnand->ecc_cfg, nfc->conf_regs + PL35X_SMC_ECC_CFG);
}

static int pl35x_nand_init_hw_ecc_controller(struct pl35x_nandc *nfc,
					     struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret = 0;

	if (mtd->writesize < SZ_512 || mtd->writesize > SZ_2K) {
		dev_err(nfc->dev,
			"The hardware ECC engine is limited to pages up to 2kiB\n");
		return -EOPNOTSUPP;
	}

	chip->ecc.strength = 1;
	chip->ecc.bytes = 3;
	chip->ecc.size = SZ_512;
	chip->ecc.steps = mtd->writesize / chip->ecc.size;
	chip->ecc.read_page = pl35x_nand_read_page_hwecc;
	chip->ecc.write_page = pl35x_nand_write_page_hwecc;
	chip->ecc.write_page_raw = nand_monolithic_write_page_raw;
	pl35x_smc_set_ecc_pg_size(nfc, chip, mtd->writesize);

	nfc->ecc_buf = devm_kmalloc(nfc->dev, chip->ecc.bytes * chip->ecc.steps,
				    GFP_KERNEL);
	if (!nfc->ecc_buf)
		return -ENOMEM;

	switch (mtd->oobsize) {
	case 16:
		/* Legacy Xilinx layout */
		mtd_set_ooblayout(mtd, &pl35x_ecc_ooblayout16_ops);
		chip->bbt_options |= NAND_BBT_NO_OOB_BBM;
		break;
	case 64:
		mtd_set_ooblayout(mtd, nand_get_large_page_ooblayout());
		break;
	default:
		dev_err(nfc->dev, "Unsupported OOB size\n");
		return -EOPNOTSUPP;
	}

	return ret;
}

static int pl35x_nand_attach_chip(struct nand_chip *chip)
{
	const struct nand_ecc_props *requirements =
		nanddev_get_ecc_requirements(&chip->base);
	struct pl35x_nandc *nfc = to_pl35x_nandc(chip->controller);
	struct pl35x_nand *plnand = to_pl35x_nand(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	if (chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_NONE &&
	    (!chip->ecc.size || !chip->ecc.strength)) {
		if (requirements->step_size && requirements->strength) {
			chip->ecc.size = requirements->step_size;
			chip->ecc.strength = requirements->strength;
		} else {
			dev_info(nfc->dev,
				 "No minimum ECC strength, using 1b/512B\n");
			chip->ecc.size = 512;
			chip->ecc.strength = 1;
		}
	}

	if (mtd->writesize <= SZ_512)
		plnand->addr_cycles = 1;
	else
		plnand->addr_cycles = 2;

	if (chip->options & NAND_ROW_ADDR_3)
		plnand->addr_cycles += 3;
	else
		plnand->addr_cycles += 2;

	switch (chip->ecc.engine_type) {
	case NAND_ECC_ENGINE_TYPE_ON_DIE:
		/* Keep these legacy BBT descriptors for ON_DIE situations */
		chip->bbt_td = &bbt_main_descr;
		chip->bbt_md = &bbt_mirror_descr;
		fallthrough;
	case NAND_ECC_ENGINE_TYPE_NONE:
	case NAND_ECC_ENGINE_TYPE_SOFT:
		break;
	case NAND_ECC_ENGINE_TYPE_ON_HOST:
		ret = pl35x_nand_init_hw_ecc_controller(nfc, chip);
		if (ret)
			return ret;
		break;
	default:
		dev_err(nfc->dev, "Unsupported ECC mode: %d\n",
			chip->ecc.engine_type);
		return -EINVAL;
	}

	return 0;
}

static const struct nand_controller_ops pl35x_nandc_ops = {
	.attach_chip = pl35x_nand_attach_chip,
	.exec_op = pl35x_nfc_exec_op,
	.setup_interface = pl35x_nfc_setup_interface,
};

static int pl35x_nand_reset_state(struct pl35x_nandc *nfc)
{
	int ret;

	/* Disable interrupts and clear their status */
	writel(PL35X_SMC_MEMC_CFG_CLR_INT_CLR_1 |
	       PL35X_SMC_MEMC_CFG_CLR_ECC_INT_DIS_1 |
	       PL35X_SMC_MEMC_CFG_CLR_INT_DIS_1,
	       nfc->conf_regs + PL35X_SMC_MEMC_CFG_CLR);

	/* Set default bus width to 8-bit */
	ret = pl35x_smc_set_buswidth(nfc, PL35X_SMC_OPMODE_BW_8);
	if (ret)
		return ret;

	/* Ensure the ECC controller is bypassed by default */
	ret = pl35x_smc_set_ecc_mode(nfc, NULL, PL35X_SMC_ECC_CFG_MODE_BYPASS);
	if (ret)
		return ret;

	/*
	 * Configure the commands that the ECC block uses to detect the
	 * operations it should start/end.
	 */
	writel(PL35X_SMC_ECC_CMD1_WRITE(NAND_CMD_SEQIN) |
	       PL35X_SMC_ECC_CMD1_READ(NAND_CMD_READ0) |
	       PL35X_SMC_ECC_CMD1_READ_END(NAND_CMD_READSTART) |
	       PL35X_SMC_ECC_CMD1_READ_END_VALID(NAND_CMD_READ1),
	       nfc->conf_regs + PL35X_SMC_ECC_CMD1);
	writel(PL35X_SMC_ECC_CMD2_WRITE_COL_CHG(NAND_CMD_RNDIN) |
	       PL35X_SMC_ECC_CMD2_READ_COL_CHG(NAND_CMD_RNDOUT) |
	       PL35X_SMC_ECC_CMD2_READ_COL_CHG_END(NAND_CMD_RNDOUTSTART) |
	       PL35X_SMC_ECC_CMD2_READ_COL_CHG_END_VALID(NAND_CMD_READ1),
	       nfc->conf_regs + PL35X_SMC_ECC_CMD2);

	return 0;
}

static int pl35x_nand_chip_init(struct pl35x_nandc *nfc,
				struct device_node *np)
{
	struct pl35x_nand *plnand;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	int cs, ret;

	plnand = devm_kzalloc(nfc->dev, sizeof(*plnand), GFP_KERNEL);
	if (!plnand)
		return -ENOMEM;

	ret = of_property_read_u32(np, "reg", &cs);
	if (ret)
		return ret;

	if (cs >= PL35X_NAND_MAX_CS) {
		dev_err(nfc->dev, "Wrong CS %d\n", cs);
		return -EINVAL;
	}

	if (test_and_set_bit(cs, &nfc->assigned_cs)) {
		dev_err(nfc->dev, "Already assigned CS %d\n", cs);
		return -EINVAL;
	}

	plnand->cs = cs;

	chip = &plnand->chip;
	chip->options = NAND_BUSWIDTH_AUTO | NAND_USES_DMA | NAND_NO_SUBPAGE_WRITE;
	chip->bbt_options = NAND_BBT_USE_FLASH;
	chip->controller = &nfc->controller;
	mtd = nand_to_mtd(chip);
	mtd->dev.parent = nfc->dev;
	nand_set_flash_node(chip, np);
	if (!mtd->name) {
		mtd->name = devm_kasprintf(nfc->dev, GFP_KERNEL,
					   "%s", PL35X_NANDC_DRIVER_NAME);
		if (!mtd->name) {
			dev_err(nfc->dev, "Failed to allocate mtd->name\n");
			return -ENOMEM;
		}
	}

	ret = nand_scan(chip, 1);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_cleanup(chip);
		return ret;
	}

	list_add_tail(&plnand->node, &nfc->chips);

	return ret;
}

static void pl35x_nand_chips_cleanup(struct pl35x_nandc *nfc)
{
	struct pl35x_nand *plnand, *tmp;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry_safe(plnand, tmp, &nfc->chips, node) {
		chip = &plnand->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&plnand->node);
	}
}

static int pl35x_nand_chips_init(struct pl35x_nandc *nfc)
{
	struct device_node *np = nfc->dev->of_node;
	int nchips = of_get_child_count(np);
	int ret;

	if (!nchips || nchips > PL35X_NAND_MAX_CS) {
		dev_err(nfc->dev, "Incorrect number of NAND chips (%d)\n",
			nchips);
		return -EINVAL;
	}

	for_each_child_of_node_scoped(np, nand_np) {
		ret = pl35x_nand_chip_init(nfc, nand_np);
		if (ret) {
			pl35x_nand_chips_cleanup(nfc);
			break;
		}
	}

	return ret;
}

static int pl35x_nand_probe(struct platform_device *pdev)
{
	struct device *smc_dev = pdev->dev.parent;
	struct amba_device *smc_amba = to_amba_device(smc_dev);
	struct pl35x_nandc *nfc;
	int ret;

	nfc = devm_kzalloc(&pdev->dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->dev = &pdev->dev;
	nand_controller_init(&nfc->controller);
	nfc->controller.ops = &pl35x_nandc_ops;
	INIT_LIST_HEAD(&nfc->chips);

	nfc->conf_regs = devm_ioremap_resource(&smc_amba->dev, &smc_amba->res);
	if (IS_ERR(nfc->conf_regs))
		return PTR_ERR(nfc->conf_regs);

	nfc->io_regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nfc->io_regs))
		return PTR_ERR(nfc->io_regs);

	ret = pl35x_nand_reset_state(nfc);
	if (ret)
		return ret;

	ret = pl35x_nand_chips_init(nfc);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, nfc);

	return 0;
}

static void pl35x_nand_remove(struct platform_device *pdev)
{
	struct pl35x_nandc *nfc = platform_get_drvdata(pdev);

	pl35x_nand_chips_cleanup(nfc);
}

static const struct of_device_id pl35x_nand_of_match[] = {
	{ .compatible = "arm,pl353-nand-r2p1" },
	{},
};
MODULE_DEVICE_TABLE(of, pl35x_nand_of_match);

static struct platform_driver pl35x_nandc_driver = {
	.probe = pl35x_nand_probe,
	.remove = pl35x_nand_remove,
	.driver = {
		.name = PL35X_NANDC_DRIVER_NAME,
		.of_match_table = pl35x_nand_of_match,
	},
};
module_platform_driver(pl35x_nandc_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("ARM PL35X NAND controller driver");
MODULE_LICENSE("GPL");
