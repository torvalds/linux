// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * MTK NAND Flash controller driver.
 * Copyright (C) 2016 MediaTek Inc.
 * Authors:	Xiaolei Li		<xiaolei.li@mediatek.com>
 *		Jorge Ramirez-Ortiz	<jorge.ramirez-ortiz@linaro.org>
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/mtd.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/mtd/nand-ecc-mtk.h>

/* NAND controller register definition */
#define NFI_CNFG		(0x00)
#define		CNFG_AHB		BIT(0)
#define		CNFG_READ_EN		BIT(1)
#define		CNFG_DMA_BURST_EN	BIT(2)
#define		CNFG_BYTE_RW		BIT(6)
#define		CNFG_HW_ECC_EN		BIT(8)
#define		CNFG_AUTO_FMT_EN	BIT(9)
#define		CNFG_OP_CUST		(6 << 12)
#define NFI_PAGEFMT		(0x04)
#define		PAGEFMT_FDM_ECC_SHIFT	(12)
#define		PAGEFMT_FDM_SHIFT	(8)
#define		PAGEFMT_SEC_SEL_512	BIT(2)
#define		PAGEFMT_512_2K		(0)
#define		PAGEFMT_2K_4K		(1)
#define		PAGEFMT_4K_8K		(2)
#define		PAGEFMT_8K_16K		(3)
/* NFI control */
#define NFI_CON			(0x08)
#define		CON_FIFO_FLUSH		BIT(0)
#define		CON_NFI_RST		BIT(1)
#define		CON_BRD			BIT(8)  /* burst  read */
#define		CON_BWR			BIT(9)	/* burst  write */
#define		CON_SEC_SHIFT		(12)
/* Timming control register */
#define NFI_ACCCON		(0x0C)
#define NFI_INTR_EN		(0x10)
#define		INTR_AHB_DONE_EN	BIT(6)
#define NFI_INTR_STA		(0x14)
#define NFI_CMD			(0x20)
#define NFI_ADDRNOB		(0x30)
#define NFI_COLADDR		(0x34)
#define NFI_ROWADDR		(0x38)
#define NFI_STRDATA		(0x40)
#define		STAR_EN			(1)
#define		STAR_DE			(0)
#define NFI_CNRNB		(0x44)
#define NFI_DATAW		(0x50)
#define NFI_DATAR		(0x54)
#define NFI_PIO_DIRDY		(0x58)
#define		PIO_DI_RDY		(0x01)
#define NFI_STA			(0x60)
#define		STA_CMD			BIT(0)
#define		STA_ADDR		BIT(1)
#define		STA_BUSY		BIT(8)
#define		STA_EMP_PAGE		BIT(12)
#define		NFI_FSM_CUSTDATA	(0xe << 16)
#define		NFI_FSM_MASK		(0xf << 16)
#define NFI_ADDRCNTR		(0x70)
#define		CNTR_MASK		GENMASK(16, 12)
#define		ADDRCNTR_SEC_SHIFT	(12)
#define		ADDRCNTR_SEC(val) \
		(((val) & CNTR_MASK) >> ADDRCNTR_SEC_SHIFT)
#define NFI_STRADDR		(0x80)
#define NFI_BYTELEN		(0x84)
#define NFI_CSEL		(0x90)
#define NFI_FDML(x)		(0xA0 + (x) * sizeof(u32) * 2)
#define NFI_FDMM(x)		(0xA4 + (x) * sizeof(u32) * 2)
#define NFI_FDM_MAX_SIZE	(8)
#define NFI_FDM_MIN_SIZE	(1)
#define NFI_DEBUG_CON1		(0x220)
#define		STROBE_MASK		GENMASK(4, 3)
#define		STROBE_SHIFT		(3)
#define		MAX_STROBE_DLY		(3)
#define NFI_MASTER_STA		(0x224)
#define		MASTER_STA_MASK		(0x0FFF)
#define NFI_EMPTY_THRESH	(0x23C)

#define MTK_NAME		"mtk-nand"
#define KB(x)			((x) * 1024UL)
#define MB(x)			(KB(x) * 1024UL)

#define MTK_TIMEOUT		(500000)
#define MTK_RESET_TIMEOUT	(1000000)
#define MTK_NAND_MAX_NSELS	(2)
#define MTK_NFC_MIN_SPARE	(16)
#define ACCTIMING(tpoecs, tprecs, tc2r, tw2r, twh, twst, trlt) \
	((tpoecs) << 28 | (tprecs) << 22 | (tc2r) << 16 | \
	(tw2r) << 12 | (twh) << 8 | (twst) << 4 | (trlt))

struct mtk_nfc_caps {
	const u8 *spare_size;
	u8 num_spare_size;
	u8 pageformat_spare_shift;
	u8 nfi_clk_div;
	u8 max_sector;
	u32 max_sector_size;
};

struct mtk_nfc_bad_mark_ctl {
	void (*bm_swap)(struct mtd_info *, u8 *buf, int raw);
	u32 sec;
	u32 pos;
};

/*
 * FDM: region used to store free OOB data
 */
struct mtk_nfc_fdm {
	u32 reg_size;
	u32 ecc_size;
};

struct mtk_nfc_nand_chip {
	struct list_head node;
	struct nand_chip nand;

	struct mtk_nfc_bad_mark_ctl bad_mark;
	struct mtk_nfc_fdm fdm;
	u32 spare_per_sector;

	int nsels;
	u8 sels[] __counted_by(nsels);
	/* nothing after this field */
};

struct mtk_nfc_clk {
	struct clk *nfi_clk;
	struct clk *pad_clk;
};

struct mtk_nfc {
	struct nand_controller controller;
	struct mtk_ecc_config ecc_cfg;
	struct mtk_nfc_clk clk;
	struct mtk_ecc *ecc;

	struct device *dev;
	const struct mtk_nfc_caps *caps;
	void __iomem *regs;

	struct completion done;
	struct list_head chips;

	u8 *buffer;

	unsigned long assigned_cs;
};

/*
 * supported spare size of each IP.
 * order should be the same with the spare size bitfiled defination of
 * register NFI_PAGEFMT.
 */
static const u8 spare_size_mt2701[] = {
	16, 26, 27, 28, 32, 36, 40, 44,	48, 49, 50, 51, 52, 62, 63, 64
};

static const u8 spare_size_mt2712[] = {
	16, 26, 27, 28, 32, 36, 40, 44, 48, 49, 50, 51, 52, 62, 61, 63, 64, 67,
	74
};

static const u8 spare_size_mt7622[] = {
	16, 26, 27, 28
};

static inline struct mtk_nfc_nand_chip *to_mtk_nand(struct nand_chip *nand)
{
	return container_of(nand, struct mtk_nfc_nand_chip, nand);
}

static inline u8 *data_ptr(struct nand_chip *chip, const u8 *p, int i)
{
	return (u8 *)p + i * chip->ecc.size;
}

static inline u8 *oob_ptr(struct nand_chip *chip, int i)
{
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	u8 *poi;

	/* map the sector's FDM data to free oob:
	 * the beginning of the oob area stores the FDM data of bad mark sectors
	 */

	if (i < mtk_nand->bad_mark.sec)
		poi = chip->oob_poi + (i + 1) * mtk_nand->fdm.reg_size;
	else if (i == mtk_nand->bad_mark.sec)
		poi = chip->oob_poi;
	else
		poi = chip->oob_poi + i * mtk_nand->fdm.reg_size;

	return poi;
}

static inline int mtk_data_len(struct nand_chip *chip)
{
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);

	return chip->ecc.size + mtk_nand->spare_per_sector;
}

static inline u8 *mtk_data_ptr(struct nand_chip *chip,  int i)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);

	return nfc->buffer + i * mtk_data_len(chip);
}

static inline u8 *mtk_oob_ptr(struct nand_chip *chip, int i)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);

	return nfc->buffer + i * mtk_data_len(chip) + chip->ecc.size;
}

static inline void nfi_writel(struct mtk_nfc *nfc, u32 val, u32 reg)
{
	writel(val, nfc->regs + reg);
}

static inline void nfi_writew(struct mtk_nfc *nfc, u16 val, u32 reg)
{
	writew(val, nfc->regs + reg);
}

static inline void nfi_writeb(struct mtk_nfc *nfc, u8 val, u32 reg)
{
	writeb(val, nfc->regs + reg);
}

static inline u32 nfi_readl(struct mtk_nfc *nfc, u32 reg)
{
	return readl_relaxed(nfc->regs + reg);
}

static inline u16 nfi_readw(struct mtk_nfc *nfc, u32 reg)
{
	return readw_relaxed(nfc->regs + reg);
}

static inline u8 nfi_readb(struct mtk_nfc *nfc, u32 reg)
{
	return readb_relaxed(nfc->regs + reg);
}

static void mtk_nfc_hw_reset(struct mtk_nfc *nfc)
{
	struct device *dev = nfc->dev;
	u32 val;
	int ret;

	/* reset all registers and force the NFI master to terminate */
	nfi_writel(nfc, CON_FIFO_FLUSH | CON_NFI_RST, NFI_CON);

	/* wait for the master to finish the last transaction */
	ret = readl_poll_timeout(nfc->regs + NFI_MASTER_STA, val,
				 !(val & MASTER_STA_MASK), 50,
				 MTK_RESET_TIMEOUT);
	if (ret)
		dev_warn(dev, "master active in reset [0x%x] = 0x%x\n",
			 NFI_MASTER_STA, val);

	/* ensure any status register affected by the NFI master is reset */
	nfi_writel(nfc, CON_FIFO_FLUSH | CON_NFI_RST, NFI_CON);
	nfi_writew(nfc, STAR_DE, NFI_STRDATA);
}

static int mtk_nfc_send_command(struct mtk_nfc *nfc, u8 command)
{
	struct device *dev = nfc->dev;
	u32 val;
	int ret;

	nfi_writel(nfc, command, NFI_CMD);

	ret = readl_poll_timeout_atomic(nfc->regs + NFI_STA, val,
					!(val & STA_CMD), 10,  MTK_TIMEOUT);
	if (ret) {
		dev_warn(dev, "nfi core timed out entering command mode\n");
		return -EIO;
	}

	return 0;
}

static int mtk_nfc_send_address(struct mtk_nfc *nfc, int addr)
{
	struct device *dev = nfc->dev;
	u32 val;
	int ret;

	nfi_writel(nfc, addr, NFI_COLADDR);
	nfi_writel(nfc, 0, NFI_ROWADDR);
	nfi_writew(nfc, 1, NFI_ADDRNOB);

	ret = readl_poll_timeout_atomic(nfc->regs + NFI_STA, val,
					!(val & STA_ADDR), 10, MTK_TIMEOUT);
	if (ret) {
		dev_warn(dev, "nfi core timed out entering address mode\n");
		return -EIO;
	}

	return 0;
}

static int mtk_nfc_hw_runtime_config(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	u32 fmt, spare, i;

	if (!mtd->writesize)
		return 0;

	spare = mtk_nand->spare_per_sector;

	switch (mtd->writesize) {
	case 512:
		fmt = PAGEFMT_512_2K | PAGEFMT_SEC_SEL_512;
		break;
	case KB(2):
		if (chip->ecc.size == 512)
			fmt = PAGEFMT_2K_4K | PAGEFMT_SEC_SEL_512;
		else
			fmt = PAGEFMT_512_2K;
		break;
	case KB(4):
		if (chip->ecc.size == 512)
			fmt = PAGEFMT_4K_8K | PAGEFMT_SEC_SEL_512;
		else
			fmt = PAGEFMT_2K_4K;
		break;
	case KB(8):
		if (chip->ecc.size == 512)
			fmt = PAGEFMT_8K_16K | PAGEFMT_SEC_SEL_512;
		else
			fmt = PAGEFMT_4K_8K;
		break;
	case KB(16):
		fmt = PAGEFMT_8K_16K;
		break;
	default:
		dev_err(nfc->dev, "invalid page len: %d\n", mtd->writesize);
		return -EINVAL;
	}

	/*
	 * the hardware will double the value for this eccsize, so we need to
	 * halve it
	 */
	if (chip->ecc.size == 1024)
		spare >>= 1;

	for (i = 0; i < nfc->caps->num_spare_size; i++) {
		if (nfc->caps->spare_size[i] == spare)
			break;
	}

	if (i == nfc->caps->num_spare_size) {
		dev_err(nfc->dev, "invalid spare size %d\n", spare);
		return -EINVAL;
	}

	fmt |= i << nfc->caps->pageformat_spare_shift;

	fmt |= mtk_nand->fdm.reg_size << PAGEFMT_FDM_SHIFT;
	fmt |= mtk_nand->fdm.ecc_size << PAGEFMT_FDM_ECC_SHIFT;
	nfi_writel(nfc, fmt, NFI_PAGEFMT);

	nfc->ecc_cfg.strength = chip->ecc.strength;
	nfc->ecc_cfg.len = chip->ecc.size + mtk_nand->fdm.ecc_size;

	return 0;
}

static inline void mtk_nfc_wait_ioready(struct mtk_nfc *nfc)
{
	int rc;
	u8 val;

	rc = readb_poll_timeout_atomic(nfc->regs + NFI_PIO_DIRDY, val,
				       val & PIO_DI_RDY, 10, MTK_TIMEOUT);
	if (rc < 0)
		dev_err(nfc->dev, "data not ready\n");
}

static inline u8 mtk_nfc_read_byte(struct nand_chip *chip)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	u32 reg;

	/* after each byte read, the NFI_STA reg is reset by the hardware */
	reg = nfi_readl(nfc, NFI_STA) & NFI_FSM_MASK;
	if (reg != NFI_FSM_CUSTDATA) {
		reg = nfi_readw(nfc, NFI_CNFG);
		reg |= CNFG_BYTE_RW | CNFG_READ_EN;
		nfi_writew(nfc, reg, NFI_CNFG);

		/*
		 * set to max sector to allow the HW to continue reading over
		 * unaligned accesses
		 */
		reg = (nfc->caps->max_sector << CON_SEC_SHIFT) | CON_BRD;
		nfi_writel(nfc, reg, NFI_CON);

		/* trigger to fetch data */
		nfi_writew(nfc, STAR_EN, NFI_STRDATA);
	}

	mtk_nfc_wait_ioready(nfc);

	return nfi_readb(nfc, NFI_DATAR);
}

static void mtk_nfc_read_buf(struct nand_chip *chip, u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i] = mtk_nfc_read_byte(chip);
}

static void mtk_nfc_write_byte(struct nand_chip *chip, u8 byte)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	u32 reg;

	reg = nfi_readl(nfc, NFI_STA) & NFI_FSM_MASK;

	if (reg != NFI_FSM_CUSTDATA) {
		reg = nfi_readw(nfc, NFI_CNFG) | CNFG_BYTE_RW;
		nfi_writew(nfc, reg, NFI_CNFG);

		reg = nfc->caps->max_sector << CON_SEC_SHIFT | CON_BWR;
		nfi_writel(nfc, reg, NFI_CON);

		nfi_writew(nfc, STAR_EN, NFI_STRDATA);
	}

	mtk_nfc_wait_ioready(nfc);
	nfi_writeb(nfc, byte, NFI_DATAW);
}

static void mtk_nfc_write_buf(struct nand_chip *chip, const u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		mtk_nfc_write_byte(chip, buf[i]);
}

static int mtk_nfc_exec_instr(struct nand_chip *chip,
			      const struct nand_op_instr *instr)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	unsigned int i;
	u32 status;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		mtk_nfc_send_command(nfc, instr->ctx.cmd.opcode);
		return 0;
	case NAND_OP_ADDR_INSTR:
		for (i = 0; i < instr->ctx.addr.naddrs; i++)
			mtk_nfc_send_address(nfc, instr->ctx.addr.addrs[i]);
		return 0;
	case NAND_OP_DATA_IN_INSTR:
		mtk_nfc_read_buf(chip, instr->ctx.data.buf.in,
				 instr->ctx.data.len);
		return 0;
	case NAND_OP_DATA_OUT_INSTR:
		mtk_nfc_write_buf(chip, instr->ctx.data.buf.out,
				  instr->ctx.data.len);
		return 0;
	case NAND_OP_WAITRDY_INSTR:
		return readl_poll_timeout(nfc->regs + NFI_STA, status,
					  !(status & STA_BUSY), 20,
					  instr->ctx.waitrdy.timeout_ms * 1000);
	default:
		break;
	}

	return -EINVAL;
}

static void mtk_nfc_select_target(struct nand_chip *nand, unsigned int cs)
{
	struct mtk_nfc *nfc = nand_get_controller_data(nand);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(nand);

	mtk_nfc_hw_runtime_config(nand_to_mtd(nand));

	nfi_writel(nfc, mtk_nand->sels[cs], NFI_CSEL);
}

static int mtk_nfc_exec_op(struct nand_chip *chip,
			   const struct nand_operation *op,
			   bool check_only)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	unsigned int i;
	int ret = 0;

	if (check_only)
		return 0;

	mtk_nfc_hw_reset(nfc);
	nfi_writew(nfc, CNFG_OP_CUST, NFI_CNFG);
	mtk_nfc_select_target(chip, op->cs);

	for (i = 0; i < op->ninstrs; i++) {
		ret = mtk_nfc_exec_instr(chip, &op->instrs[i]);
		if (ret)
			break;
	}

	return ret;
}

static int mtk_nfc_setup_interface(struct nand_chip *chip, int csline,
				   const struct nand_interface_config *conf)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	const struct nand_sdr_timings *timings;
	u32 rate, tpoecs, tprecs, tc2r, tw2r, twh, twst = 0, trlt = 0;
	u32 temp, tsel = 0;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return -ENOTSUPP;

	if (csline == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	rate = clk_get_rate(nfc->clk.nfi_clk);
	/* There is a frequency divider in some IPs */
	rate /= nfc->caps->nfi_clk_div;

	/* turn clock rate into KHZ */
	rate /= 1000;

	tpoecs = max(timings->tALH_min, timings->tCLH_min) / 1000;
	tpoecs = DIV_ROUND_UP(tpoecs * rate, 1000000);
	tpoecs &= 0xf;

	tprecs = max(timings->tCLS_min, timings->tALS_min) / 1000;
	tprecs = DIV_ROUND_UP(tprecs * rate, 1000000);
	tprecs &= 0x3f;

	/* sdr interface has no tCR which means CE# low to RE# low */
	tc2r = 0;

	tw2r = timings->tWHR_min / 1000;
	tw2r = DIV_ROUND_UP(tw2r * rate, 1000000);
	tw2r = DIV_ROUND_UP(tw2r - 1, 2);
	tw2r &= 0xf;

	twh = max(timings->tREH_min, timings->tWH_min) / 1000;
	twh = DIV_ROUND_UP(twh * rate, 1000000) - 1;
	twh &= 0xf;

	/* Calculate real WE#/RE# hold time in nanosecond */
	temp = (twh + 1) * 1000000 / rate;
	/* nanosecond to picosecond */
	temp *= 1000;

	/*
	 * WE# low level time should be expaned to meet WE# pulse time
	 * and WE# cycle time at the same time.
	 */
	if (temp < timings->tWC_min)
		twst = timings->tWC_min - temp;
	twst = max(timings->tWP_min, twst) / 1000;
	twst = DIV_ROUND_UP(twst * rate, 1000000) - 1;
	twst &= 0xf;

	/*
	 * RE# low level time should be expaned to meet RE# pulse time
	 * and RE# cycle time at the same time.
	 */
	if (temp < timings->tRC_min)
		trlt = timings->tRC_min - temp;
	trlt = max(trlt, timings->tRP_min) / 1000;
	trlt = DIV_ROUND_UP(trlt * rate, 1000000) - 1;
	trlt &= 0xf;

	/* Calculate RE# pulse time in nanosecond. */
	temp = (trlt + 1) * 1000000 / rate;
	/* nanosecond to picosecond */
	temp *= 1000;
	/*
	 * If RE# access time is bigger than RE# pulse time,
	 * delay sampling data timing.
	 */
	if (temp < timings->tREA_max) {
		tsel = timings->tREA_max / 1000;
		tsel = DIV_ROUND_UP(tsel * rate, 1000000);
		tsel -= (trlt + 1);
		if (tsel > MAX_STROBE_DLY) {
			trlt += tsel - MAX_STROBE_DLY;
			tsel = MAX_STROBE_DLY;
		}
	}
	temp = nfi_readl(nfc, NFI_DEBUG_CON1);
	temp &= ~STROBE_MASK;
	temp |= tsel << STROBE_SHIFT;
	nfi_writel(nfc, temp, NFI_DEBUG_CON1);

	/*
	 * ACCON: access timing control register
	 * -------------------------------------
	 * 31:28: tpoecs, minimum required time for CS post pulling down after
	 *        accessing the device
	 * 27:22: tprecs, minimum required time for CS pre pulling down before
	 *        accessing the device
	 * 21:16: tc2r, minimum required time from NCEB low to NREB low
	 * 15:12: tw2r, minimum required time from NWEB high to NREB low.
	 * 11:08: twh, write enable hold time
	 * 07:04: twst, write wait states
	 * 03:00: trlt, read wait states
	 */
	trlt = ACCTIMING(tpoecs, tprecs, tc2r, tw2r, twh, twst, trlt);
	nfi_writel(nfc, trlt, NFI_ACCCON);

	return 0;
}

static int mtk_nfc_sector_encode(struct nand_chip *chip, u8 *data)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	int size = chip->ecc.size + mtk_nand->fdm.reg_size;

	nfc->ecc_cfg.mode = ECC_DMA_MODE;
	nfc->ecc_cfg.op = ECC_ENCODE;

	return mtk_ecc_encode(nfc->ecc, &nfc->ecc_cfg, data, size);
}

static void mtk_nfc_no_bad_mark_swap(struct mtd_info *a, u8 *b, int c)
{
	/* nop */
}

static void mtk_nfc_bad_mark_swap(struct mtd_info *mtd, u8 *buf, int raw)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mtk_nfc_nand_chip *nand = to_mtk_nand(chip);
	u32 bad_pos = nand->bad_mark.pos;

	if (raw)
		bad_pos += nand->bad_mark.sec * mtk_data_len(chip);
	else
		bad_pos += nand->bad_mark.sec * chip->ecc.size;

	swap(chip->oob_poi[0], buf[bad_pos]);
}

static int mtk_nfc_format_subpage(struct mtd_info *mtd, u32 offset,
				  u32 len, const u8 *buf)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_fdm *fdm = &mtk_nand->fdm;
	u32 start, end;
	int i, ret;

	start = offset / chip->ecc.size;
	end = DIV_ROUND_UP(offset + len, chip->ecc.size);

	memset(nfc->buffer, 0xff, mtd->writesize + mtd->oobsize);
	for (i = 0; i < chip->ecc.steps; i++) {
		memcpy(mtk_data_ptr(chip, i), data_ptr(chip, buf, i),
		       chip->ecc.size);

		if (start > i || i >= end)
			continue;

		if (i == mtk_nand->bad_mark.sec)
			mtk_nand->bad_mark.bm_swap(mtd, nfc->buffer, 1);

		memcpy(mtk_oob_ptr(chip, i), oob_ptr(chip, i), fdm->reg_size);

		/* program the CRC back to the OOB */
		ret = mtk_nfc_sector_encode(chip, mtk_data_ptr(chip, i));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void mtk_nfc_format_page(struct mtd_info *mtd, const u8 *buf)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_fdm *fdm = &mtk_nand->fdm;
	u32 i;

	memset(nfc->buffer, 0xff, mtd->writesize + mtd->oobsize);
	for (i = 0; i < chip->ecc.steps; i++) {
		if (buf)
			memcpy(mtk_data_ptr(chip, i), data_ptr(chip, buf, i),
			       chip->ecc.size);

		if (i == mtk_nand->bad_mark.sec)
			mtk_nand->bad_mark.bm_swap(mtd, nfc->buffer, 1);

		memcpy(mtk_oob_ptr(chip, i), oob_ptr(chip, i), fdm->reg_size);
	}
}

static inline void mtk_nfc_read_fdm(struct nand_chip *chip, u32 start,
				    u32 sectors)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_nfc_fdm *fdm = &mtk_nand->fdm;
	u32 vall, valm;
	u8 *oobptr;
	int i, j;

	for (i = 0; i < sectors; i++) {
		oobptr = oob_ptr(chip, start + i);
		vall = nfi_readl(nfc, NFI_FDML(i));
		valm = nfi_readl(nfc, NFI_FDMM(i));

		for (j = 0; j < fdm->reg_size; j++)
			oobptr[j] = (j >= 4 ? valm : vall) >> ((j % 4) * 8);
	}
}

static inline void mtk_nfc_write_fdm(struct nand_chip *chip)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_nfc_fdm *fdm = &mtk_nand->fdm;
	u32 vall, valm;
	u8 *oobptr;
	int i, j;

	for (i = 0; i < chip->ecc.steps; i++) {
		oobptr = oob_ptr(chip, i);
		vall = 0;
		valm = 0;
		for (j = 0; j < 8; j++) {
			if (j < 4)
				vall |= (j < fdm->reg_size ? oobptr[j] : 0xff)
						<< (j * 8);
			else
				valm |= (j < fdm->reg_size ? oobptr[j] : 0xff)
						<< ((j - 4) * 8);
		}
		nfi_writel(nfc, vall, NFI_FDML(i));
		nfi_writel(nfc, valm, NFI_FDMM(i));
	}
}

static int mtk_nfc_do_write_page(struct mtd_info *mtd, struct nand_chip *chip,
				 const u8 *buf, int page, int len)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct device *dev = nfc->dev;
	dma_addr_t addr;
	u32 reg;
	int ret;

	addr = dma_map_single(dev, (void *)buf, len, DMA_TO_DEVICE);
	ret = dma_mapping_error(nfc->dev, addr);
	if (ret) {
		dev_err(nfc->dev, "dma mapping error\n");
		return -EINVAL;
	}

	reg = nfi_readw(nfc, NFI_CNFG) | CNFG_AHB | CNFG_DMA_BURST_EN;
	nfi_writew(nfc, reg, NFI_CNFG);

	nfi_writel(nfc, chip->ecc.steps << CON_SEC_SHIFT, NFI_CON);
	nfi_writel(nfc, lower_32_bits(addr), NFI_STRADDR);
	nfi_writew(nfc, INTR_AHB_DONE_EN, NFI_INTR_EN);

	init_completion(&nfc->done);

	reg = nfi_readl(nfc, NFI_CON) | CON_BWR;
	nfi_writel(nfc, reg, NFI_CON);
	nfi_writew(nfc, STAR_EN, NFI_STRDATA);

	ret = wait_for_completion_timeout(&nfc->done, msecs_to_jiffies(500));
	if (!ret) {
		dev_err(dev, "program ahb done timeout\n");
		nfi_writew(nfc, 0, NFI_INTR_EN);
		ret = -ETIMEDOUT;
		goto timeout;
	}

	ret = readl_poll_timeout_atomic(nfc->regs + NFI_ADDRCNTR, reg,
					ADDRCNTR_SEC(reg) >= chip->ecc.steps,
					10, MTK_TIMEOUT);
	if (ret)
		dev_err(dev, "hwecc write timeout\n");

timeout:

	dma_unmap_single(nfc->dev, addr, len, DMA_TO_DEVICE);
	nfi_writel(nfc, 0, NFI_CON);

	return ret;
}

static int mtk_nfc_write_page(struct mtd_info *mtd, struct nand_chip *chip,
			      const u8 *buf, int page, int raw)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	size_t len;
	const u8 *bufpoi;
	u32 reg;
	int ret;

	mtk_nfc_select_target(chip, chip->cur_cs);
	nand_prog_page_begin_op(chip, page, 0, NULL, 0);

	if (!raw) {
		/* OOB => FDM: from register,  ECC: from HW */
		reg = nfi_readw(nfc, NFI_CNFG) | CNFG_AUTO_FMT_EN;
		nfi_writew(nfc, reg | CNFG_HW_ECC_EN, NFI_CNFG);

		nfc->ecc_cfg.op = ECC_ENCODE;
		nfc->ecc_cfg.mode = ECC_NFI_MODE;
		ret = mtk_ecc_enable(nfc->ecc, &nfc->ecc_cfg);
		if (ret) {
			/* clear NFI config */
			reg = nfi_readw(nfc, NFI_CNFG);
			reg &= ~(CNFG_AUTO_FMT_EN | CNFG_HW_ECC_EN);
			nfi_writew(nfc, reg, NFI_CNFG);

			return ret;
		}

		memcpy(nfc->buffer, buf, mtd->writesize);
		mtk_nand->bad_mark.bm_swap(mtd, nfc->buffer, raw);
		bufpoi = nfc->buffer;

		/* write OOB into the FDM registers (OOB area in MTK NAND) */
		mtk_nfc_write_fdm(chip);
	} else {
		bufpoi = buf;
	}

	len = mtd->writesize + (raw ? mtd->oobsize : 0);
	ret = mtk_nfc_do_write_page(mtd, chip, bufpoi, page, len);

	if (!raw)
		mtk_ecc_disable(nfc->ecc);

	if (ret)
		return ret;

	return nand_prog_page_end_op(chip);
}

static int mtk_nfc_write_page_hwecc(struct nand_chip *chip, const u8 *buf,
				    int oob_on, int page)
{
	return mtk_nfc_write_page(nand_to_mtd(chip), chip, buf, page, 0);
}

static int mtk_nfc_write_page_raw(struct nand_chip *chip, const u8 *buf,
				  int oob_on, int pg)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct mtk_nfc *nfc = nand_get_controller_data(chip);

	mtk_nfc_format_page(mtd, buf);
	return mtk_nfc_write_page(mtd, chip, nfc->buffer, pg, 1);
}

static int mtk_nfc_write_subpage_hwecc(struct nand_chip *chip, u32 offset,
				       u32 data_len, const u8 *buf,
				       int oob_on, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	int ret;

	ret = mtk_nfc_format_subpage(mtd, offset, data_len, buf);
	if (ret < 0)
		return ret;

	/* use the data in the private buffer (now with FDM and CRC) */
	return mtk_nfc_write_page(mtd, chip, nfc->buffer, page, 1);
}

static int mtk_nfc_write_oob_std(struct nand_chip *chip, int page)
{
	return mtk_nfc_write_page_raw(chip, NULL, 1, page);
}

static int mtk_nfc_update_ecc_stats(struct mtd_info *mtd, u8 *buf, u32 start,
				    u32 sectors)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_ecc_stats stats;
	u32 reg_size = mtk_nand->fdm.reg_size;
	int rc, i;

	rc = nfi_readl(nfc, NFI_STA) & STA_EMP_PAGE;
	if (rc) {
		memset(buf, 0xff, sectors * chip->ecc.size);
		for (i = 0; i < sectors; i++)
			memset(oob_ptr(chip, start + i), 0xff, reg_size);
		return 0;
	}

	mtk_ecc_get_stats(nfc->ecc, &stats, sectors);
	mtd->ecc_stats.corrected += stats.corrected;
	mtd->ecc_stats.failed += stats.failed;

	return stats.bitflips;
}

static int mtk_nfc_read_subpage(struct mtd_info *mtd, struct nand_chip *chip,
				u32 data_offs, u32 readlen,
				u8 *bufpoi, int page, int raw)
{
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	u32 spare = mtk_nand->spare_per_sector;
	u32 column, sectors, start, end, reg;
	dma_addr_t addr;
	int bitflips = 0;
	size_t len;
	u8 *buf;
	int rc;

	mtk_nfc_select_target(chip, chip->cur_cs);
	start = data_offs / chip->ecc.size;
	end = DIV_ROUND_UP(data_offs + readlen, chip->ecc.size);

	sectors = end - start;
	column = start * (chip->ecc.size + spare);

	len = sectors * chip->ecc.size + (raw ? sectors * spare : 0);
	buf = bufpoi + start * chip->ecc.size;

	nand_read_page_op(chip, page, column, NULL, 0);

	addr = dma_map_single(nfc->dev, buf, len, DMA_FROM_DEVICE);
	rc = dma_mapping_error(nfc->dev, addr);
	if (rc) {
		dev_err(nfc->dev, "dma mapping error\n");

		return -EINVAL;
	}

	reg = nfi_readw(nfc, NFI_CNFG);
	reg |= CNFG_READ_EN | CNFG_DMA_BURST_EN | CNFG_AHB;
	if (!raw) {
		reg |= CNFG_AUTO_FMT_EN | CNFG_HW_ECC_EN;
		nfi_writew(nfc, reg, NFI_CNFG);

		nfc->ecc_cfg.mode = ECC_NFI_MODE;
		nfc->ecc_cfg.sectors = sectors;
		nfc->ecc_cfg.op = ECC_DECODE;
		rc = mtk_ecc_enable(nfc->ecc, &nfc->ecc_cfg);
		if (rc) {
			dev_err(nfc->dev, "ecc enable\n");
			/* clear NFI_CNFG */
			reg &= ~(CNFG_DMA_BURST_EN | CNFG_AHB | CNFG_READ_EN |
				CNFG_AUTO_FMT_EN | CNFG_HW_ECC_EN);
			nfi_writew(nfc, reg, NFI_CNFG);
			dma_unmap_single(nfc->dev, addr, len, DMA_FROM_DEVICE);

			return rc;
		}
	} else {
		nfi_writew(nfc, reg, NFI_CNFG);
	}

	nfi_writel(nfc, sectors << CON_SEC_SHIFT, NFI_CON);
	nfi_writew(nfc, INTR_AHB_DONE_EN, NFI_INTR_EN);
	nfi_writel(nfc, lower_32_bits(addr), NFI_STRADDR);

	init_completion(&nfc->done);
	reg = nfi_readl(nfc, NFI_CON) | CON_BRD;
	nfi_writel(nfc, reg, NFI_CON);
	nfi_writew(nfc, STAR_EN, NFI_STRDATA);

	rc = wait_for_completion_timeout(&nfc->done, msecs_to_jiffies(500));
	if (!rc)
		dev_warn(nfc->dev, "read ahb/dma done timeout\n");

	rc = readl_poll_timeout_atomic(nfc->regs + NFI_BYTELEN, reg,
				       ADDRCNTR_SEC(reg) >= sectors, 10,
				       MTK_TIMEOUT);
	if (rc < 0) {
		dev_err(nfc->dev, "subpage done timeout\n");
		bitflips = -EIO;
	} else if (!raw) {
		rc = mtk_ecc_wait_done(nfc->ecc, ECC_DECODE);
		bitflips = rc < 0 ? -ETIMEDOUT :
			mtk_nfc_update_ecc_stats(mtd, buf, start, sectors);
		mtk_nfc_read_fdm(chip, start, sectors);
	}

	dma_unmap_single(nfc->dev, addr, len, DMA_FROM_DEVICE);

	if (raw)
		goto done;

	mtk_ecc_disable(nfc->ecc);

	if (clamp(mtk_nand->bad_mark.sec, start, end) == mtk_nand->bad_mark.sec)
		mtk_nand->bad_mark.bm_swap(mtd, bufpoi, raw);
done:
	nfi_writel(nfc, 0, NFI_CON);

	return bitflips;
}

static int mtk_nfc_read_subpage_hwecc(struct nand_chip *chip, u32 off,
				      u32 len, u8 *p, int pg)
{
	return mtk_nfc_read_subpage(nand_to_mtd(chip), chip, off, len, p, pg,
				    0);
}

static int mtk_nfc_read_page_hwecc(struct nand_chip *chip, u8 *p, int oob_on,
				   int pg)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	return mtk_nfc_read_subpage(mtd, chip, 0, mtd->writesize, p, pg, 0);
}

static int mtk_nfc_read_page_raw(struct nand_chip *chip, u8 *buf, int oob_on,
				 int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_fdm *fdm = &mtk_nand->fdm;
	int i, ret;

	memset(nfc->buffer, 0xff, mtd->writesize + mtd->oobsize);
	ret = mtk_nfc_read_subpage(mtd, chip, 0, mtd->writesize, nfc->buffer,
				   page, 1);
	if (ret < 0)
		return ret;

	for (i = 0; i < chip->ecc.steps; i++) {
		memcpy(oob_ptr(chip, i), mtk_oob_ptr(chip, i), fdm->reg_size);

		if (i == mtk_nand->bad_mark.sec)
			mtk_nand->bad_mark.bm_swap(mtd, nfc->buffer, 1);

		if (buf)
			memcpy(data_ptr(chip, buf, i), mtk_data_ptr(chip, i),
			       chip->ecc.size);
	}

	return ret;
}

static int mtk_nfc_read_oob_std(struct nand_chip *chip, int page)
{
	return mtk_nfc_read_page_raw(chip, NULL, 1, page);
}

static inline void mtk_nfc_hw_init(struct mtk_nfc *nfc)
{
	/*
	 * CNRNB: nand ready/busy register
	 * -------------------------------
	 * 7:4: timeout register for polling the NAND busy/ready signal
	 * 0  : poll the status of the busy/ready signal after [7:4]*16 cycles.
	 */
	nfi_writew(nfc, 0xf1, NFI_CNRNB);
	nfi_writel(nfc, PAGEFMT_8K_16K, NFI_PAGEFMT);

	mtk_nfc_hw_reset(nfc);

	nfi_readl(nfc, NFI_INTR_STA);
	nfi_writel(nfc, 0, NFI_INTR_EN);
}

static irqreturn_t mtk_nfc_irq(int irq, void *id)
{
	struct mtk_nfc *nfc = id;
	u16 sta, ien;

	sta = nfi_readw(nfc, NFI_INTR_STA);
	ien = nfi_readw(nfc, NFI_INTR_EN);

	if (!(sta & ien))
		return IRQ_NONE;

	nfi_writew(nfc, ~sta & ien, NFI_INTR_EN);
	complete(&nfc->done);

	return IRQ_HANDLED;
}

static int mtk_nfc_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	struct mtk_nfc_fdm *fdm = &mtk_nand->fdm;
	u32 eccsteps;

	eccsteps = mtd->writesize / chip->ecc.size;

	if (section >= eccsteps)
		return -ERANGE;

	oob_region->length = fdm->reg_size - fdm->ecc_size;
	oob_region->offset = section * fdm->reg_size + fdm->ecc_size;

	return 0;
}

static int mtk_nfc_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	u32 eccsteps;

	if (section)
		return -ERANGE;

	eccsteps = mtd->writesize / chip->ecc.size;
	oob_region->offset = mtk_nand->fdm.reg_size * eccsteps;
	oob_region->length = mtd->oobsize - oob_region->offset;

	return 0;
}

static const struct mtd_ooblayout_ops mtk_nfc_ooblayout_ops = {
	.free = mtk_nfc_ooblayout_free,
	.ecc = mtk_nfc_ooblayout_ecc,
};

static void mtk_nfc_set_fdm(struct mtk_nfc_fdm *fdm, struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct mtk_nfc_nand_chip *chip = to_mtk_nand(nand);
	struct mtk_nfc *nfc = nand_get_controller_data(nand);
	u32 ecc_bytes;

	ecc_bytes = DIV_ROUND_UP(nand->ecc.strength *
				 mtk_ecc_get_parity_bits(nfc->ecc), 8);

	fdm->reg_size = chip->spare_per_sector - ecc_bytes;
	if (fdm->reg_size > NFI_FDM_MAX_SIZE)
		fdm->reg_size = NFI_FDM_MAX_SIZE;

	/* bad block mark storage */
	fdm->ecc_size = 1;
}

static void mtk_nfc_set_bad_mark_ctl(struct mtk_nfc_bad_mark_ctl *bm_ctl,
				     struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd_to_nand(mtd);

	if (mtd->writesize == 512) {
		bm_ctl->bm_swap = mtk_nfc_no_bad_mark_swap;
	} else {
		bm_ctl->bm_swap = mtk_nfc_bad_mark_swap;
		bm_ctl->sec = mtd->writesize / mtk_data_len(nand);
		bm_ctl->pos = mtd->writesize % mtk_data_len(nand);
	}
}

static int mtk_nfc_set_spare_per_sector(u32 *sps, struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct mtk_nfc *nfc = nand_get_controller_data(nand);
	const u8 *spare = nfc->caps->spare_size;
	u32 eccsteps, i, closest_spare = 0;

	eccsteps = mtd->writesize / nand->ecc.size;
	*sps = mtd->oobsize / eccsteps;

	if (nand->ecc.size == 1024)
		*sps >>= 1;

	if (*sps < MTK_NFC_MIN_SPARE)
		return -EINVAL;

	for (i = 0; i < nfc->caps->num_spare_size; i++) {
		if (*sps >= spare[i] && spare[i] >= spare[closest_spare]) {
			closest_spare = i;
			if (*sps == spare[i])
				break;
		}
	}

	*sps = spare[closest_spare];

	if (nand->ecc.size == 1024)
		*sps <<= 1;

	return 0;
}

static int mtk_nfc_ecc_init(struct device *dev, struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	const struct nand_ecc_props *requirements =
		nanddev_get_ecc_requirements(&nand->base);
	struct mtk_nfc *nfc = nand_get_controller_data(nand);
	u32 spare;
	int free, ret;

	/* support only ecc hw mode */
	if (nand->ecc.engine_type != NAND_ECC_ENGINE_TYPE_ON_HOST) {
		dev_err(dev, "ecc.engine_type not supported\n");
		return -EINVAL;
	}

	/* if optional dt settings not present */
	if (!nand->ecc.size || !nand->ecc.strength) {
		/* use datasheet requirements */
		nand->ecc.strength = requirements->strength;
		nand->ecc.size = requirements->step_size;

		/*
		 * align eccstrength and eccsize
		 * this controller only supports 512 and 1024 sizes
		 */
		if (nand->ecc.size < 1024) {
			if (mtd->writesize > 512 &&
			    nfc->caps->max_sector_size > 512) {
				nand->ecc.size = 1024;
				nand->ecc.strength <<= 1;
			} else {
				nand->ecc.size = 512;
			}
		} else {
			nand->ecc.size = 1024;
		}

		ret = mtk_nfc_set_spare_per_sector(&spare, mtd);
		if (ret)
			return ret;

		/* calculate oob bytes except ecc parity data */
		free = (nand->ecc.strength * mtk_ecc_get_parity_bits(nfc->ecc)
			+ 7) >> 3;
		free = spare - free;

		/*
		 * enhance ecc strength if oob left is bigger than max FDM size
		 * or reduce ecc strength if oob size is not enough for ecc
		 * parity data.
		 */
		if (free > NFI_FDM_MAX_SIZE) {
			spare -= NFI_FDM_MAX_SIZE;
			nand->ecc.strength = (spare << 3) /
					     mtk_ecc_get_parity_bits(nfc->ecc);
		} else if (free < 0) {
			spare -= NFI_FDM_MIN_SIZE;
			nand->ecc.strength = (spare << 3) /
					     mtk_ecc_get_parity_bits(nfc->ecc);
		}
	}

	mtk_ecc_adjust_strength(nfc->ecc, &nand->ecc.strength);

	dev_info(dev, "eccsize %d eccstrength %d\n",
		 nand->ecc.size, nand->ecc.strength);

	return 0;
}

static int mtk_nfc_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct device *dev = mtd->dev.parent;
	struct mtk_nfc *nfc = nand_get_controller_data(chip);
	struct mtk_nfc_nand_chip *mtk_nand = to_mtk_nand(chip);
	int len;
	int ret;

	if (chip->options & NAND_BUSWIDTH_16) {
		dev_err(dev, "16bits buswidth not supported");
		return -EINVAL;
	}

	/* store bbt magic in page, cause OOB is not protected */
	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	ret = mtk_nfc_ecc_init(dev, mtd);
	if (ret)
		return ret;

	ret = mtk_nfc_set_spare_per_sector(&mtk_nand->spare_per_sector, mtd);
	if (ret)
		return ret;

	mtk_nfc_set_fdm(&mtk_nand->fdm, mtd);
	mtk_nfc_set_bad_mark_ctl(&mtk_nand->bad_mark, mtd);

	len = mtd->writesize + mtd->oobsize;
	nfc->buffer = devm_kzalloc(dev, len, GFP_KERNEL);
	if (!nfc->buffer)
		return  -ENOMEM;

	return 0;
}

static const struct nand_controller_ops mtk_nfc_controller_ops = {
	.attach_chip = mtk_nfc_attach_chip,
	.setup_interface = mtk_nfc_setup_interface,
	.exec_op = mtk_nfc_exec_op,
};

static int mtk_nfc_nand_chip_init(struct device *dev, struct mtk_nfc *nfc,
				  struct device_node *np)
{
	struct mtk_nfc_nand_chip *chip;
	struct nand_chip *nand;
	struct mtd_info *mtd;
	int nsels;
	u32 tmp;
	int ret;
	int i;

	if (!of_get_property(np, "reg", &nsels))
		return -ENODEV;

	nsels /= sizeof(u32);
	if (!nsels || nsels > MTK_NAND_MAX_NSELS) {
		dev_err(dev, "invalid reg property size %d\n", nsels);
		return -EINVAL;
	}

	chip = devm_kzalloc(dev, sizeof(*chip) + nsels * sizeof(u8),
			    GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->nsels = nsels;
	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &tmp);
		if (ret) {
			dev_err(dev, "reg property failure : %d\n", ret);
			return ret;
		}

		if (tmp >= MTK_NAND_MAX_NSELS) {
			dev_err(dev, "invalid CS: %u\n", tmp);
			return -EINVAL;
		}

		if (test_and_set_bit(tmp, &nfc->assigned_cs)) {
			dev_err(dev, "CS %u already assigned\n", tmp);
			return -EINVAL;
		}

		chip->sels[i] = tmp;
	}

	nand = &chip->nand;
	nand->controller = &nfc->controller;

	nand_set_flash_node(nand, np);
	nand_set_controller_data(nand, nfc);

	nand->options |= NAND_USES_DMA | NAND_SUBPAGE_READ;

	/* set default mode in case dt entry is missing */
	nand->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	nand->ecc.write_subpage = mtk_nfc_write_subpage_hwecc;
	nand->ecc.write_page_raw = mtk_nfc_write_page_raw;
	nand->ecc.write_page = mtk_nfc_write_page_hwecc;
	nand->ecc.write_oob_raw = mtk_nfc_write_oob_std;
	nand->ecc.write_oob = mtk_nfc_write_oob_std;

	nand->ecc.read_subpage = mtk_nfc_read_subpage_hwecc;
	nand->ecc.read_page_raw = mtk_nfc_read_page_raw;
	nand->ecc.read_page = mtk_nfc_read_page_hwecc;
	nand->ecc.read_oob_raw = mtk_nfc_read_oob_std;
	nand->ecc.read_oob = mtk_nfc_read_oob_std;

	mtd = nand_to_mtd(nand);
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;
	mtd->name = MTK_NAME;
	mtd_set_ooblayout(mtd, &mtk_nfc_ooblayout_ops);

	mtk_nfc_hw_init(nfc);

	ret = nand_scan(nand, nsels);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "mtd parse partition error\n");
		nand_cleanup(nand);
		return ret;
	}

	list_add_tail(&chip->node, &nfc->chips);

	return 0;
}

static int mtk_nfc_nand_chips_init(struct device *dev, struct mtk_nfc *nfc)
{
	struct device_node *np = dev->of_node;
	struct device_node *nand_np;
	int ret;

	for_each_child_of_node(np, nand_np) {
		ret = mtk_nfc_nand_chip_init(dev, nfc, nand_np);
		if (ret) {
			of_node_put(nand_np);
			return ret;
		}
	}

	return 0;
}

static const struct mtk_nfc_caps mtk_nfc_caps_mt2701 = {
	.spare_size = spare_size_mt2701,
	.num_spare_size = 16,
	.pageformat_spare_shift = 4,
	.nfi_clk_div = 1,
	.max_sector = 16,
	.max_sector_size = 1024,
};

static const struct mtk_nfc_caps mtk_nfc_caps_mt2712 = {
	.spare_size = spare_size_mt2712,
	.num_spare_size = 19,
	.pageformat_spare_shift = 16,
	.nfi_clk_div = 2,
	.max_sector = 16,
	.max_sector_size = 1024,
};

static const struct mtk_nfc_caps mtk_nfc_caps_mt7622 = {
	.spare_size = spare_size_mt7622,
	.num_spare_size = 4,
	.pageformat_spare_shift = 4,
	.nfi_clk_div = 1,
	.max_sector = 8,
	.max_sector_size = 512,
};

static const struct of_device_id mtk_nfc_id_table[] = {
	{
		.compatible = "mediatek,mt2701-nfc",
		.data = &mtk_nfc_caps_mt2701,
	}, {
		.compatible = "mediatek,mt2712-nfc",
		.data = &mtk_nfc_caps_mt2712,
	}, {
		.compatible = "mediatek,mt7622-nfc",
		.data = &mtk_nfc_caps_mt7622,
	},
	{}
};
MODULE_DEVICE_TABLE(of, mtk_nfc_id_table);

static int mtk_nfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mtk_nfc *nfc;
	int ret, irq;

	nfc = devm_kzalloc(dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nand_controller_init(&nfc->controller);
	INIT_LIST_HEAD(&nfc->chips);
	nfc->controller.ops = &mtk_nfc_controller_ops;

	/* probe defer if not ready */
	nfc->ecc = of_mtk_ecc_get(np);
	if (IS_ERR(nfc->ecc))
		return PTR_ERR(nfc->ecc);
	else if (!nfc->ecc)
		return -ENODEV;

	nfc->caps = of_device_get_match_data(dev);
	nfc->dev = dev;

	nfc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nfc->regs)) {
		ret = PTR_ERR(nfc->regs);
		goto release_ecc;
	}

	nfc->clk.nfi_clk = devm_clk_get_enabled(dev, "nfi_clk");
	if (IS_ERR(nfc->clk.nfi_clk)) {
		dev_err(dev, "no clk\n");
		ret = PTR_ERR(nfc->clk.nfi_clk);
		goto release_ecc;
	}

	nfc->clk.pad_clk = devm_clk_get_enabled(dev, "pad_clk");
	if (IS_ERR(nfc->clk.pad_clk)) {
		dev_err(dev, "no pad clk\n");
		ret = PTR_ERR(nfc->clk.pad_clk);
		goto release_ecc;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -EINVAL;
		goto release_ecc;
	}

	ret = devm_request_irq(dev, irq, mtk_nfc_irq, 0x0, "mtk-nand", nfc);
	if (ret) {
		dev_err(dev, "failed to request nfi irq\n");
		goto release_ecc;
	}

	ret = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "failed to set dma mask\n");
		goto release_ecc;
	}

	platform_set_drvdata(pdev, nfc);

	ret = mtk_nfc_nand_chips_init(dev, nfc);
	if (ret) {
		dev_err(dev, "failed to init nand chips\n");
		goto release_ecc;
	}

	return 0;

release_ecc:
	mtk_ecc_release(nfc->ecc);

	return ret;
}

static void mtk_nfc_remove(struct platform_device *pdev)
{
	struct mtk_nfc *nfc = platform_get_drvdata(pdev);
	struct mtk_nfc_nand_chip *mtk_chip;
	struct nand_chip *chip;
	int ret;

	while (!list_empty(&nfc->chips)) {
		mtk_chip = list_first_entry(&nfc->chips,
					    struct mtk_nfc_nand_chip, node);
		chip = &mtk_chip->nand;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&mtk_chip->node);
	}

	mtk_ecc_release(nfc->ecc);
}

#ifdef CONFIG_PM_SLEEP
static int mtk_nfc_suspend(struct device *dev)
{
	struct mtk_nfc *nfc = dev_get_drvdata(dev);

	clk_disable_unprepare(nfc->clk.nfi_clk);
	clk_disable_unprepare(nfc->clk.pad_clk);

	return 0;
}

static int mtk_nfc_resume(struct device *dev)
{
	struct mtk_nfc *nfc = dev_get_drvdata(dev);
	struct mtk_nfc_nand_chip *chip;
	struct nand_chip *nand;
	int ret;
	u32 i;

	udelay(200);

	ret = clk_prepare_enable(nfc->clk.nfi_clk);
	if (ret) {
		dev_err(dev, "failed to enable nfi clk\n");
		return ret;
	}

	ret = clk_prepare_enable(nfc->clk.pad_clk);
	if (ret) {
		dev_err(dev, "failed to enable pad clk\n");
		clk_disable_unprepare(nfc->clk.nfi_clk);
		return ret;
	}

	/* reset NAND chip if VCC was powered off */
	list_for_each_entry(chip, &nfc->chips, node) {
		nand = &chip->nand;
		for (i = 0; i < chip->nsels; i++)
			nand_reset(nand, i);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(mtk_nfc_pm_ops, mtk_nfc_suspend, mtk_nfc_resume);
#endif

static struct platform_driver mtk_nfc_driver = {
	.probe  = mtk_nfc_probe,
	.remove_new = mtk_nfc_remove,
	.driver = {
		.name  = MTK_NAME,
		.of_match_table = mtk_nfc_id_table,
#ifdef CONFIG_PM_SLEEP
		.pm = &mtk_nfc_pm_ops,
#endif
	},
};

module_platform_driver(mtk_nfc_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Xiaolei Li <xiaolei.li@mediatek.com>");
MODULE_DESCRIPTION("MTK Nand Flash Controller Driver");
