// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Amlogic Meson Nand Flash Controller Driver
 *
 * Copyright (c) 2018 Amlogic, inc.
 * Author: Liang Yang <liang.yang@amlogic.com>
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/mtd.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sched/task_stack.h>

#define NFC_REG_CMD		0x00
#define NFC_CMD_IDLE		(0xc << 14)
#define NFC_CMD_CLE		(0x5 << 14)
#define NFC_CMD_ALE		(0x6 << 14)
#define NFC_CMD_ADL		((0 << 16) | (3 << 20))
#define NFC_CMD_ADH		((1 << 16) | (3 << 20))
#define NFC_CMD_AIL		((2 << 16) | (3 << 20))
#define NFC_CMD_AIH		((3 << 16) | (3 << 20))
#define NFC_CMD_SEED		((8 << 16) | (3 << 20))
#define NFC_CMD_M2N		((0 << 17) | (2 << 20))
#define NFC_CMD_N2M		((1 << 17) | (2 << 20))
#define NFC_CMD_RB		BIT(20)
#define NFC_CMD_SCRAMBLER_ENABLE	BIT(19)
#define NFC_CMD_SCRAMBLER_DISABLE	0
#define NFC_CMD_SHORTMODE_DISABLE	0
#define NFC_CMD_RB_INT		BIT(14)

#define NFC_CMD_GET_SIZE(x)	(((x) >> 22) & GENMASK(4, 0))

#define NFC_REG_CFG		0x04
#define NFC_REG_DADR		0x08
#define NFC_REG_IADR		0x0c
#define NFC_REG_BUF		0x10
#define NFC_REG_INFO		0x14
#define NFC_REG_DC		0x18
#define NFC_REG_ADR		0x1c
#define NFC_REG_DL		0x20
#define NFC_REG_DH		0x24
#define NFC_REG_CADR		0x28
#define NFC_REG_SADR		0x2c
#define NFC_REG_PINS		0x30
#define NFC_REG_VER		0x38

#define NFC_RB_IRQ_EN		BIT(21)

#define CMDRWGEN(cmd_dir, ran, bch, short_mode, page_size, pages)	\
	(								\
		(cmd_dir)			|			\
		((ran) << 19)			|			\
		((bch) << 14)			|			\
		((short_mode) << 13)		|			\
		(((page_size) & 0x7f) << 6)	|			\
		((pages) & 0x3f)					\
	)

#define GENCMDDADDRL(adl, addr)		((adl) | ((addr) & 0xffff))
#define GENCMDDADDRH(adh, addr)		((adh) | (((addr) >> 16) & 0xffff))
#define GENCMDIADDRL(ail, addr)		((ail) | ((addr) & 0xffff))
#define GENCMDIADDRH(aih, addr)		((aih) | (((addr) >> 16) & 0xffff))

#define DMA_DIR(dir)		((dir) ? NFC_CMD_N2M : NFC_CMD_M2N)

#define ECC_CHECK_RETURN_FF	(-1)

#define NAND_CE0		(0xe << 10)
#define NAND_CE1		(0xd << 10)

#define DMA_BUSY_TIMEOUT	0x100000
#define CMD_FIFO_EMPTY_TIMEOUT	1000

#define MAX_CE_NUM		2

/* eMMC clock register, misc control */
#define CLK_SELECT_NAND		BIT(31)

#define NFC_CLK_CYCLE		6

/* nand flash controller delay 3 ns */
#define NFC_DEFAULT_DELAY	3000

#define ROW_ADDER(page, index)	(((page) >> (8 * (index))) & 0xff)
#define MAX_CYCLE_ADDRS		5
#define DIRREAD			1
#define DIRWRITE		0

#define ECC_PARITY_BCH8_512B	14
#define ECC_COMPLETE            BIT(31)
#define ECC_ERR_CNT(x)		(((x) >> 24) & GENMASK(5, 0))
#define ECC_ZERO_CNT(x)		(((x) >> 16) & GENMASK(5, 0))
#define ECC_UNCORRECTABLE	0x3f

#define PER_INFO_BYTE		8

struct meson_nfc_nand_chip {
	struct list_head node;
	struct nand_chip nand;
	unsigned long clk_rate;
	unsigned long level1_divider;
	u32 bus_timing;
	u32 twb;
	u32 tadl;
	u32 tbers_max;

	u32 bch_mode;
	u8 *data_buf;
	__le64 *info_buf;
	u32 nsels;
	u8 sels[];
};

struct meson_nand_ecc {
	u32 bch;
	u32 strength;
};

struct meson_nfc_data {
	const struct nand_ecc_caps *ecc_caps;
};

struct meson_nfc_param {
	u32 chip_select;
	u32 rb_select;
};

struct nand_rw_cmd {
	u32 cmd0;
	u32 addrs[MAX_CYCLE_ADDRS];
	u32 cmd1;
};

struct nand_timing {
	u32 twb;
	u32 tadl;
	u32 tbers_max;
};

struct meson_nfc {
	struct nand_controller controller;
	struct clk *core_clk;
	struct clk *device_clk;
	struct clk *phase_tx;
	struct clk *phase_rx;

	unsigned long clk_rate;
	u32 bus_timing;

	struct device *dev;
	void __iomem *reg_base;
	struct regmap *reg_clk;
	struct completion completion;
	struct list_head chips;
	const struct meson_nfc_data *data;
	struct meson_nfc_param param;
	struct nand_timing timing;
	union {
		int cmd[32];
		struct nand_rw_cmd rw;
	} cmdfifo;

	dma_addr_t daddr;
	dma_addr_t iaddr;

	unsigned long assigned_cs;
};

enum {
	NFC_ECC_BCH8_1K		= 2,
	NFC_ECC_BCH24_1K,
	NFC_ECC_BCH30_1K,
	NFC_ECC_BCH40_1K,
	NFC_ECC_BCH50_1K,
	NFC_ECC_BCH60_1K,
};

#define MESON_ECC_DATA(b, s)	{ .bch = (b),	.strength = (s)}

static struct meson_nand_ecc meson_ecc[] = {
	MESON_ECC_DATA(NFC_ECC_BCH8_1K, 8),
	MESON_ECC_DATA(NFC_ECC_BCH24_1K, 24),
	MESON_ECC_DATA(NFC_ECC_BCH30_1K, 30),
	MESON_ECC_DATA(NFC_ECC_BCH40_1K, 40),
	MESON_ECC_DATA(NFC_ECC_BCH50_1K, 50),
	MESON_ECC_DATA(NFC_ECC_BCH60_1K, 60),
};

static int meson_nand_calc_ecc_bytes(int step_size, int strength)
{
	int ecc_bytes;

	if (step_size == 512 && strength == 8)
		return ECC_PARITY_BCH8_512B;

	ecc_bytes = DIV_ROUND_UP(strength * fls(step_size * 8), 8);
	ecc_bytes = ALIGN(ecc_bytes, 2);

	return ecc_bytes;
}

NAND_ECC_CAPS_SINGLE(meson_gxl_ecc_caps,
		     meson_nand_calc_ecc_bytes, 1024, 8, 24, 30, 40, 50, 60);
NAND_ECC_CAPS_SINGLE(meson_axg_ecc_caps,
		     meson_nand_calc_ecc_bytes, 1024, 8);

static struct meson_nfc_nand_chip *to_meson_nand(struct nand_chip *nand)
{
	return container_of(nand, struct meson_nfc_nand_chip, nand);
}

static void meson_nfc_select_chip(struct nand_chip *nand, int chip)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	int ret, value;

	if (chip < 0 || WARN_ON_ONCE(chip >= meson_chip->nsels))
		return;

	nfc->param.chip_select = meson_chip->sels[chip] ? NAND_CE1 : NAND_CE0;
	nfc->param.rb_select = nfc->param.chip_select;
	nfc->timing.twb = meson_chip->twb;
	nfc->timing.tadl = meson_chip->tadl;
	nfc->timing.tbers_max = meson_chip->tbers_max;

	if (nfc->clk_rate != meson_chip->clk_rate) {
		ret = clk_set_rate(nfc->device_clk, meson_chip->clk_rate);
		if (ret) {
			dev_err(nfc->dev, "failed to set clock rate\n");
			return;
		}
		nfc->clk_rate = meson_chip->clk_rate;
	}
	if (nfc->bus_timing != meson_chip->bus_timing) {
		value = (NFC_CLK_CYCLE - 1) | (meson_chip->bus_timing << 5);
		writel(value, nfc->reg_base + NFC_REG_CFG);
		writel((1 << 31), nfc->reg_base + NFC_REG_CMD);
		nfc->bus_timing =  meson_chip->bus_timing;
	}
}

static void meson_nfc_cmd_idle(struct meson_nfc *nfc, u32 time)
{
	writel(nfc->param.chip_select | NFC_CMD_IDLE | (time & 0x3ff),
	       nfc->reg_base + NFC_REG_CMD);
}

static void meson_nfc_cmd_seed(struct meson_nfc *nfc, u32 seed)
{
	writel(NFC_CMD_SEED | (0xc2 + (seed & 0x7fff)),
	       nfc->reg_base + NFC_REG_CMD);
}

static void meson_nfc_cmd_access(struct nand_chip *nand, int raw, bool dir,
				 int scrambler)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct meson_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	u32 bch = meson_chip->bch_mode, cmd;
	int len = mtd->writesize, pagesize, pages;

	pagesize = nand->ecc.size;

	if (raw) {
		len = mtd->writesize + mtd->oobsize;
		cmd = (len & GENMASK(5, 0)) | scrambler | DMA_DIR(dir);
		writel(cmd, nfc->reg_base + NFC_REG_CMD);
		return;
	}

	pages = len / nand->ecc.size;

	cmd = CMDRWGEN(DMA_DIR(dir), scrambler, bch,
		       NFC_CMD_SHORTMODE_DISABLE, pagesize, pages);

	writel(cmd, nfc->reg_base + NFC_REG_CMD);
}

static void meson_nfc_drain_cmd(struct meson_nfc *nfc)
{
	/*
	 * Insert two commands to make sure all valid commands are finished.
	 *
	 * The Nand flash controller is designed as two stages pipleline -
	 *  a) fetch and b) excute.
	 * There might be cases when the driver see command queue is empty,
	 * but the Nand flash controller still has two commands buffered,
	 * one is fetched into NFC request queue (ready to run), and another
	 * is actively executing. So pushing 2 "IDLE" commands guarantees that
	 * the pipeline is emptied.
	 */
	meson_nfc_cmd_idle(nfc, 0);
	meson_nfc_cmd_idle(nfc, 0);
}

static int meson_nfc_wait_cmd_finish(struct meson_nfc *nfc,
				     unsigned int timeout_ms)
{
	u32 cmd_size = 0;
	int ret;

	/* wait cmd fifo is empty */
	ret = readl_relaxed_poll_timeout(nfc->reg_base + NFC_REG_CMD, cmd_size,
					 !NFC_CMD_GET_SIZE(cmd_size),
					 10, timeout_ms * 1000);
	if (ret)
		dev_err(nfc->dev, "wait for empty CMD FIFO time out\n");

	return ret;
}

static int meson_nfc_wait_dma_finish(struct meson_nfc *nfc)
{
	meson_nfc_drain_cmd(nfc);

	return meson_nfc_wait_cmd_finish(nfc, DMA_BUSY_TIMEOUT);
}

static u8 *meson_nfc_oob_ptr(struct nand_chip *nand, int i)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	int len;

	len = nand->ecc.size * (i + 1) + (nand->ecc.bytes + 2) * i;

	return meson_chip->data_buf + len;
}

static u8 *meson_nfc_data_ptr(struct nand_chip *nand, int i)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	int len, temp;

	temp = nand->ecc.size + nand->ecc.bytes;
	len = (temp + 2) * i;

	return meson_chip->data_buf + len;
}

static void meson_nfc_get_data_oob(struct nand_chip *nand,
				   u8 *buf, u8 *oobbuf)
{
	int i, oob_len = 0;
	u8 *dsrc, *osrc;

	oob_len = nand->ecc.bytes + 2;
	for (i = 0; i < nand->ecc.steps; i++) {
		if (buf) {
			dsrc = meson_nfc_data_ptr(nand, i);
			memcpy(buf, dsrc, nand->ecc.size);
			buf += nand->ecc.size;
		}
		osrc = meson_nfc_oob_ptr(nand, i);
		memcpy(oobbuf, osrc, oob_len);
		oobbuf += oob_len;
	}
}

static void meson_nfc_set_data_oob(struct nand_chip *nand,
				   const u8 *buf, u8 *oobbuf)
{
	int i, oob_len = 0;
	u8 *dsrc, *osrc;

	oob_len = nand->ecc.bytes + 2;
	for (i = 0; i < nand->ecc.steps; i++) {
		if (buf) {
			dsrc = meson_nfc_data_ptr(nand, i);
			memcpy(dsrc, buf, nand->ecc.size);
			buf += nand->ecc.size;
		}
		osrc = meson_nfc_oob_ptr(nand, i);
		memcpy(osrc, oobbuf, oob_len);
		oobbuf += oob_len;
	}
}

static int meson_nfc_queue_rb(struct meson_nfc *nfc, int timeout_ms)
{
	u32 cmd, cfg;
	int ret = 0;

	meson_nfc_cmd_idle(nfc, nfc->timing.twb);
	meson_nfc_drain_cmd(nfc);
	meson_nfc_wait_cmd_finish(nfc, CMD_FIFO_EMPTY_TIMEOUT);

	cfg = readl(nfc->reg_base + NFC_REG_CFG);
	cfg |= NFC_RB_IRQ_EN;
	writel(cfg, nfc->reg_base + NFC_REG_CFG);

	reinit_completion(&nfc->completion);

	/* use the max erase time as the maximum clock for waiting R/B */
	cmd = NFC_CMD_RB | NFC_CMD_RB_INT
		| nfc->param.chip_select | nfc->timing.tbers_max;
	writel(cmd, nfc->reg_base + NFC_REG_CMD);

	ret = wait_for_completion_timeout(&nfc->completion,
					  msecs_to_jiffies(timeout_ms));
	if (ret == 0)
		ret = -1;

	return ret;
}

static void meson_nfc_set_user_byte(struct nand_chip *nand, u8 *oob_buf)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	__le64 *info;
	int i, count;

	for (i = 0, count = 0; i < nand->ecc.steps; i++, count += 2) {
		info = &meson_chip->info_buf[i];
		*info |= oob_buf[count];
		*info |= oob_buf[count + 1] << 8;
	}
}

static void meson_nfc_get_user_byte(struct nand_chip *nand, u8 *oob_buf)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	__le64 *info;
	int i, count;

	for (i = 0, count = 0; i < nand->ecc.steps; i++, count += 2) {
		info = &meson_chip->info_buf[i];
		oob_buf[count] = *info;
		oob_buf[count + 1] = *info >> 8;
	}
}

static int meson_nfc_ecc_correct(struct nand_chip *nand, u32 *bitflips,
				 u64 *correct_bitmap)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	__le64 *info;
	int ret = 0, i;

	for (i = 0; i < nand->ecc.steps; i++) {
		info = &meson_chip->info_buf[i];
		if (ECC_ERR_CNT(*info) != ECC_UNCORRECTABLE) {
			mtd->ecc_stats.corrected += ECC_ERR_CNT(*info);
			*bitflips = max_t(u32, *bitflips, ECC_ERR_CNT(*info));
			*correct_bitmap |= 1 >> i;
			continue;
		}
		if ((nand->options & NAND_NEED_SCRAMBLING) &&
		    ECC_ZERO_CNT(*info) < nand->ecc.strength) {
			mtd->ecc_stats.corrected += ECC_ZERO_CNT(*info);
			*bitflips = max_t(u32, *bitflips,
					  ECC_ZERO_CNT(*info));
			ret = ECC_CHECK_RETURN_FF;
		} else {
			ret = -EBADMSG;
		}
	}
	return ret;
}

static int meson_nfc_dma_buffer_setup(struct nand_chip *nand, void *databuf,
				      int datalen, void *infobuf, int infolen,
				      enum dma_data_direction dir)
{
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	u32 cmd;
	int ret = 0;

	nfc->daddr = dma_map_single(nfc->dev, databuf, datalen, dir);
	ret = dma_mapping_error(nfc->dev, nfc->daddr);
	if (ret) {
		dev_err(nfc->dev, "DMA mapping error\n");
		return ret;
	}
	cmd = GENCMDDADDRL(NFC_CMD_ADL, nfc->daddr);
	writel(cmd, nfc->reg_base + NFC_REG_CMD);

	cmd = GENCMDDADDRH(NFC_CMD_ADH, nfc->daddr);
	writel(cmd, nfc->reg_base + NFC_REG_CMD);

	if (infobuf) {
		nfc->iaddr = dma_map_single(nfc->dev, infobuf, infolen, dir);
		ret = dma_mapping_error(nfc->dev, nfc->iaddr);
		if (ret) {
			dev_err(nfc->dev, "DMA mapping error\n");
			dma_unmap_single(nfc->dev,
					 nfc->daddr, datalen, dir);
			return ret;
		}
		cmd = GENCMDIADDRL(NFC_CMD_AIL, nfc->iaddr);
		writel(cmd, nfc->reg_base + NFC_REG_CMD);

		cmd = GENCMDIADDRH(NFC_CMD_AIH, nfc->iaddr);
		writel(cmd, nfc->reg_base + NFC_REG_CMD);
	}

	return ret;
}

static void meson_nfc_dma_buffer_release(struct nand_chip *nand,
					 int infolen, int datalen,
					 enum dma_data_direction dir)
{
	struct meson_nfc *nfc = nand_get_controller_data(nand);

	dma_unmap_single(nfc->dev, nfc->daddr, datalen, dir);
	if (infolen)
		dma_unmap_single(nfc->dev, nfc->iaddr, infolen, dir);
}

static int meson_nfc_read_buf(struct nand_chip *nand, u8 *buf, int len)
{
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	int ret = 0;
	u32 cmd;
	u8 *info;

	info = kzalloc(PER_INFO_BYTE, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = meson_nfc_dma_buffer_setup(nand, buf, len, info,
					 PER_INFO_BYTE, DMA_FROM_DEVICE);
	if (ret)
		goto out;

	cmd = NFC_CMD_N2M | (len & GENMASK(5, 0));
	writel(cmd, nfc->reg_base + NFC_REG_CMD);

	meson_nfc_drain_cmd(nfc);
	meson_nfc_wait_cmd_finish(nfc, 1000);
	meson_nfc_dma_buffer_release(nand, len, PER_INFO_BYTE, DMA_FROM_DEVICE);

out:
	kfree(info);

	return ret;
}

static int meson_nfc_write_buf(struct nand_chip *nand, u8 *buf, int len)
{
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	int ret = 0;
	u32 cmd;

	ret = meson_nfc_dma_buffer_setup(nand, buf, len, NULL,
					 0, DMA_TO_DEVICE);
	if (ret)
		return ret;

	cmd = NFC_CMD_M2N | (len & GENMASK(5, 0));
	writel(cmd, nfc->reg_base + NFC_REG_CMD);

	meson_nfc_drain_cmd(nfc);
	meson_nfc_wait_cmd_finish(nfc, 1000);
	meson_nfc_dma_buffer_release(nand, len, 0, DMA_TO_DEVICE);

	return ret;
}

static int meson_nfc_rw_cmd_prepare_and_execute(struct nand_chip *nand,
						int page, bool in)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	const struct nand_sdr_timings *sdr =
		nand_get_sdr_timings(&nand->data_interface);
	u32 *addrs = nfc->cmdfifo.rw.addrs;
	u32 cs = nfc->param.chip_select;
	u32 cmd0, cmd_num, row_start;
	int ret = 0, i;

	cmd_num = sizeof(struct nand_rw_cmd) / sizeof(int);

	cmd0 = in ? NAND_CMD_READ0 : NAND_CMD_SEQIN;
	nfc->cmdfifo.rw.cmd0 = cs | NFC_CMD_CLE | cmd0;

	addrs[0] = cs | NFC_CMD_ALE | 0;
	if (mtd->writesize <= 512) {
		cmd_num--;
		row_start = 1;
	} else {
		addrs[1] = cs | NFC_CMD_ALE | 0;
		row_start = 2;
	}

	addrs[row_start] = cs | NFC_CMD_ALE | ROW_ADDER(page, 0);
	addrs[row_start + 1] = cs | NFC_CMD_ALE | ROW_ADDER(page, 1);

	if (nand->options & NAND_ROW_ADDR_3)
		addrs[row_start + 2] =
			cs | NFC_CMD_ALE | ROW_ADDER(page, 2);
	else
		cmd_num--;

	/* subtract cmd1 */
	cmd_num--;

	for (i = 0; i < cmd_num; i++)
		writel_relaxed(nfc->cmdfifo.cmd[i],
			       nfc->reg_base + NFC_REG_CMD);

	if (in) {
		nfc->cmdfifo.rw.cmd1 = cs | NFC_CMD_CLE | NAND_CMD_READSTART;
		writel(nfc->cmdfifo.rw.cmd1, nfc->reg_base + NFC_REG_CMD);
		meson_nfc_queue_rb(nfc, PSEC_TO_MSEC(sdr->tR_max));
	} else {
		meson_nfc_cmd_idle(nfc, nfc->timing.tadl);
	}

	return ret;
}

static int meson_nfc_write_page_sub(struct nand_chip *nand,
				    int page, int raw)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	const struct nand_sdr_timings *sdr =
		nand_get_sdr_timings(&nand->data_interface);
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	int data_len, info_len;
	u32 cmd;
	int ret;

	meson_nfc_select_chip(nand, nand->cur_cs);

	data_len =  mtd->writesize + mtd->oobsize;
	info_len = nand->ecc.steps * PER_INFO_BYTE;

	ret = meson_nfc_rw_cmd_prepare_and_execute(nand, page, DIRWRITE);
	if (ret)
		return ret;

	ret = meson_nfc_dma_buffer_setup(nand, meson_chip->data_buf,
					 data_len, meson_chip->info_buf,
					 info_len, DMA_TO_DEVICE);
	if (ret)
		return ret;

	if (nand->options & NAND_NEED_SCRAMBLING) {
		meson_nfc_cmd_seed(nfc, page);
		meson_nfc_cmd_access(nand, raw, DIRWRITE,
				     NFC_CMD_SCRAMBLER_ENABLE);
	} else {
		meson_nfc_cmd_access(nand, raw, DIRWRITE,
				     NFC_CMD_SCRAMBLER_DISABLE);
	}

	cmd = nfc->param.chip_select | NFC_CMD_CLE | NAND_CMD_PAGEPROG;
	writel(cmd, nfc->reg_base + NFC_REG_CMD);
	meson_nfc_queue_rb(nfc, PSEC_TO_MSEC(sdr->tPROG_max));

	meson_nfc_dma_buffer_release(nand, data_len, info_len, DMA_TO_DEVICE);

	return ret;
}

static int meson_nfc_write_page_raw(struct nand_chip *nand, const u8 *buf,
				    int oob_required, int page)
{
	u8 *oob_buf = nand->oob_poi;

	meson_nfc_set_data_oob(nand, buf, oob_buf);

	return meson_nfc_write_page_sub(nand, page, 1);
}

static int meson_nfc_write_page_hwecc(struct nand_chip *nand,
				      const u8 *buf, int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	u8 *oob_buf = nand->oob_poi;

	memcpy(meson_chip->data_buf, buf, mtd->writesize);
	memset(meson_chip->info_buf, 0, nand->ecc.steps * PER_INFO_BYTE);
	meson_nfc_set_user_byte(nand, oob_buf);

	return meson_nfc_write_page_sub(nand, page, 0);
}

static void meson_nfc_check_ecc_pages_valid(struct meson_nfc *nfc,
					    struct nand_chip *nand, int raw)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	__le64 *info;
	u32 neccpages;
	int ret;

	neccpages = raw ? 1 : nand->ecc.steps;
	info = &meson_chip->info_buf[neccpages - 1];
	do {
		usleep_range(10, 15);
		/* info is updated by nfc dma engine*/
		smp_rmb();
		ret = *info & ECC_COMPLETE;
	} while (!ret);
}

static int meson_nfc_read_page_sub(struct nand_chip *nand,
				   int page, int raw)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	int data_len, info_len;
	int ret;

	meson_nfc_select_chip(nand, nand->cur_cs);

	data_len =  mtd->writesize + mtd->oobsize;
	info_len = nand->ecc.steps * PER_INFO_BYTE;

	ret = meson_nfc_rw_cmd_prepare_and_execute(nand, page, DIRREAD);
	if (ret)
		return ret;

	ret = meson_nfc_dma_buffer_setup(nand, meson_chip->data_buf,
					 data_len, meson_chip->info_buf,
					 info_len, DMA_FROM_DEVICE);
	if (ret)
		return ret;

	if (nand->options & NAND_NEED_SCRAMBLING) {
		meson_nfc_cmd_seed(nfc, page);
		meson_nfc_cmd_access(nand, raw, DIRREAD,
				     NFC_CMD_SCRAMBLER_ENABLE);
	} else {
		meson_nfc_cmd_access(nand, raw, DIRREAD,
				     NFC_CMD_SCRAMBLER_DISABLE);
	}

	ret = meson_nfc_wait_dma_finish(nfc);
	meson_nfc_check_ecc_pages_valid(nfc, nand, raw);

	meson_nfc_dma_buffer_release(nand, data_len, info_len, DMA_FROM_DEVICE);

	return ret;
}

static int meson_nfc_read_page_raw(struct nand_chip *nand, u8 *buf,
				   int oob_required, int page)
{
	u8 *oob_buf = nand->oob_poi;
	int ret;

	ret = meson_nfc_read_page_sub(nand, page, 1);
	if (ret)
		return ret;

	meson_nfc_get_data_oob(nand, buf, oob_buf);

	return 0;
}

static int meson_nfc_read_page_hwecc(struct nand_chip *nand, u8 *buf,
				     int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	u64 correct_bitmap = 0;
	u32 bitflips = 0;
	u8 *oob_buf = nand->oob_poi;
	int ret, i;

	ret = meson_nfc_read_page_sub(nand, page, 0);
	if (ret)
		return ret;

	meson_nfc_get_user_byte(nand, oob_buf);
	ret = meson_nfc_ecc_correct(nand, &bitflips, &correct_bitmap);
	if (ret == ECC_CHECK_RETURN_FF) {
		if (buf)
			memset(buf, 0xff, mtd->writesize);
		memset(oob_buf, 0xff, mtd->oobsize);
	} else if (ret < 0) {
		if ((nand->options & NAND_NEED_SCRAMBLING) || !buf) {
			mtd->ecc_stats.failed++;
			return bitflips;
		}
		ret  = meson_nfc_read_page_raw(nand, buf, 0, page);
		if (ret)
			return ret;

		for (i = 0; i < nand->ecc.steps ; i++) {
			u8 *data = buf + i * ecc->size;
			u8 *oob = nand->oob_poi + i * (ecc->bytes + 2);

			if (correct_bitmap & (1 << i))
				continue;
			ret = nand_check_erased_ecc_chunk(data,	ecc->size,
							  oob, ecc->bytes + 2,
							  NULL, 0,
							  ecc->strength);
			if (ret < 0) {
				mtd->ecc_stats.failed++;
			} else {
				mtd->ecc_stats.corrected += ret;
				bitflips =  max_t(u32, bitflips, ret);
			}
		}
	} else if (buf && buf != meson_chip->data_buf) {
		memcpy(buf, meson_chip->data_buf, mtd->writesize);
	}

	return bitflips;
}

static int meson_nfc_read_oob_raw(struct nand_chip *nand, int page)
{
	return meson_nfc_read_page_raw(nand, NULL, 1, page);
}

static int meson_nfc_read_oob(struct nand_chip *nand, int page)
{
	return meson_nfc_read_page_hwecc(nand, NULL, 1, page);
}

static bool meson_nfc_is_buffer_dma_safe(const void *buffer)
{
	if (virt_addr_valid(buffer) && (!object_is_on_stack(buffer)))
		return true;
	return false;
}

static void *
meson_nand_op_get_dma_safe_input_buf(const struct nand_op_instr *instr)
{
	if (WARN_ON(instr->type != NAND_OP_DATA_IN_INSTR))
		return NULL;

	if (meson_nfc_is_buffer_dma_safe(instr->ctx.data.buf.in))
		return instr->ctx.data.buf.in;

	return kzalloc(instr->ctx.data.len, GFP_KERNEL);
}

static void
meson_nand_op_put_dma_safe_input_buf(const struct nand_op_instr *instr,
				     void *buf)
{
	if (WARN_ON(instr->type != NAND_OP_DATA_IN_INSTR) ||
	    WARN_ON(!buf))
		return;

	if (buf == instr->ctx.data.buf.in)
		return;

	memcpy(instr->ctx.data.buf.in, buf, instr->ctx.data.len);
	kfree(buf);
}

static void *
meson_nand_op_get_dma_safe_output_buf(const struct nand_op_instr *instr)
{
	if (WARN_ON(instr->type != NAND_OP_DATA_OUT_INSTR))
		return NULL;

	if (meson_nfc_is_buffer_dma_safe(instr->ctx.data.buf.out))
		return (void *)instr->ctx.data.buf.out;

	return kmemdup(instr->ctx.data.buf.out,
		       instr->ctx.data.len, GFP_KERNEL);
}

static void
meson_nand_op_put_dma_safe_output_buf(const struct nand_op_instr *instr,
				      const void *buf)
{
	if (WARN_ON(instr->type != NAND_OP_DATA_OUT_INSTR) ||
	    WARN_ON(!buf))
		return;

	if (buf != instr->ctx.data.buf.out)
		kfree(buf);
}

static int meson_nfc_exec_op(struct nand_chip *nand,
			     const struct nand_operation *op, bool check_only)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	const struct nand_op_instr *instr = NULL;
	void *buf;
	u32 op_id, delay_idle, cmd;
	int i;

	meson_nfc_select_chip(nand, op->cs);
	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];
		delay_idle = DIV_ROUND_UP(PSEC_TO_NSEC(instr->delay_ns),
					  meson_chip->level1_divider *
					  NFC_CLK_CYCLE);
		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			cmd = nfc->param.chip_select | NFC_CMD_CLE;
			cmd |= instr->ctx.cmd.opcode & 0xff;
			writel(cmd, nfc->reg_base + NFC_REG_CMD);
			meson_nfc_cmd_idle(nfc, delay_idle);
			break;

		case NAND_OP_ADDR_INSTR:
			for (i = 0; i < instr->ctx.addr.naddrs; i++) {
				cmd = nfc->param.chip_select | NFC_CMD_ALE;
				cmd |= instr->ctx.addr.addrs[i] & 0xff;
				writel(cmd, nfc->reg_base + NFC_REG_CMD);
			}
			meson_nfc_cmd_idle(nfc, delay_idle);
			break;

		case NAND_OP_DATA_IN_INSTR:
			buf = meson_nand_op_get_dma_safe_input_buf(instr);
			if (!buf)
				return -ENOMEM;
			meson_nfc_read_buf(nand, buf, instr->ctx.data.len);
			meson_nand_op_put_dma_safe_input_buf(instr, buf);
			break;

		case NAND_OP_DATA_OUT_INSTR:
			buf = meson_nand_op_get_dma_safe_output_buf(instr);
			if (!buf)
				return -ENOMEM;
			meson_nfc_write_buf(nand, buf, instr->ctx.data.len);
			meson_nand_op_put_dma_safe_output_buf(instr, buf);
			break;

		case NAND_OP_WAITRDY_INSTR:
			meson_nfc_queue_rb(nfc, instr->ctx.waitrdy.timeout_ms);
			if (instr->delay_ns)
				meson_nfc_cmd_idle(nfc, delay_idle);
			break;
		}
	}
	meson_nfc_wait_cmd_finish(nfc, 1000);
	return 0;
}

static int meson_ooblayout_ecc(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand = mtd_to_nand(mtd);

	if (section >= nand->ecc.steps)
		return -ERANGE;

	oobregion->offset =  2 + (section * (2 + nand->ecc.bytes));
	oobregion->length = nand->ecc.bytes;

	return 0;
}

static int meson_ooblayout_free(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand = mtd_to_nand(mtd);

	if (section >= nand->ecc.steps)
		return -ERANGE;

	oobregion->offset = section * (2 + nand->ecc.bytes);
	oobregion->length = 2;

	return 0;
}

static const struct mtd_ooblayout_ops meson_ooblayout_ops = {
	.ecc = meson_ooblayout_ecc,
	.free = meson_ooblayout_free,
};

static int meson_nfc_clk_init(struct meson_nfc *nfc)
{
	int ret;

	/* request core clock */
	nfc->core_clk = devm_clk_get(nfc->dev, "core");
	if (IS_ERR(nfc->core_clk)) {
		dev_err(nfc->dev, "failed to get core clock\n");
		return PTR_ERR(nfc->core_clk);
	}

	nfc->device_clk = devm_clk_get(nfc->dev, "device");
	if (IS_ERR(nfc->device_clk)) {
		dev_err(nfc->dev, "failed to get device clock\n");
		return PTR_ERR(nfc->device_clk);
	}

	nfc->phase_tx = devm_clk_get(nfc->dev, "tx");
	if (IS_ERR(nfc->phase_tx)) {
		dev_err(nfc->dev, "failed to get TX clk\n");
		return PTR_ERR(nfc->phase_tx);
	}

	nfc->phase_rx = devm_clk_get(nfc->dev, "rx");
	if (IS_ERR(nfc->phase_rx)) {
		dev_err(nfc->dev, "failed to get RX clk\n");
		return PTR_ERR(nfc->phase_rx);
	}

	/* init SD_EMMC_CLOCK to sane defaults w/min clock rate */
	regmap_update_bits(nfc->reg_clk,
			   0, CLK_SELECT_NAND, CLK_SELECT_NAND);

	ret = clk_prepare_enable(nfc->core_clk);
	if (ret) {
		dev_err(nfc->dev, "failed to enable core clock\n");
		return ret;
	}

	ret = clk_prepare_enable(nfc->device_clk);
	if (ret) {
		dev_err(nfc->dev, "failed to enable device clock\n");
		goto err_device_clk;
	}

	ret = clk_prepare_enable(nfc->phase_tx);
	if (ret) {
		dev_err(nfc->dev, "failed to enable TX clock\n");
		goto err_phase_tx;
	}

	ret = clk_prepare_enable(nfc->phase_rx);
	if (ret) {
		dev_err(nfc->dev, "failed to enable RX clock\n");
		goto err_phase_rx;
	}

	ret = clk_set_rate(nfc->device_clk, 24000000);
	if (ret)
		goto err_phase_rx;

	return 0;
err_phase_rx:
	clk_disable_unprepare(nfc->phase_tx);
err_phase_tx:
	clk_disable_unprepare(nfc->device_clk);
err_device_clk:
	clk_disable_unprepare(nfc->core_clk);
	return ret;
}

static void meson_nfc_disable_clk(struct meson_nfc *nfc)
{
	clk_disable_unprepare(nfc->phase_rx);
	clk_disable_unprepare(nfc->phase_tx);
	clk_disable_unprepare(nfc->device_clk);
	clk_disable_unprepare(nfc->core_clk);
}

static void meson_nfc_free_buffer(struct nand_chip *nand)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);

	kfree(meson_chip->info_buf);
	kfree(meson_chip->data_buf);
}

static int meson_chip_buffer_init(struct nand_chip *nand)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	u32 page_bytes, info_bytes, nsectors;

	nsectors = mtd->writesize / nand->ecc.size;

	page_bytes =  mtd->writesize + mtd->oobsize;
	info_bytes = nsectors * PER_INFO_BYTE;

	meson_chip->data_buf = kmalloc(page_bytes, GFP_KERNEL);
	if (!meson_chip->data_buf)
		return -ENOMEM;

	meson_chip->info_buf = kmalloc(info_bytes, GFP_KERNEL);
	if (!meson_chip->info_buf) {
		kfree(meson_chip->data_buf);
		return -ENOMEM;
	}

	return 0;
}

static
int meson_nfc_setup_data_interface(struct nand_chip *nand, int csline,
				   const struct nand_data_interface *conf)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	const struct nand_sdr_timings *timings;
	u32 div, bt_min, bt_max, tbers_clocks;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return -ENOTSUPP;

	if (csline == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	div = DIV_ROUND_UP((timings->tRC_min / 1000), NFC_CLK_CYCLE);
	bt_min = (timings->tREA_max + NFC_DEFAULT_DELAY) / div;
	bt_max = (NFC_DEFAULT_DELAY + timings->tRHOH_min +
		  timings->tRC_min / 2) / div;

	meson_chip->twb = DIV_ROUND_UP(PSEC_TO_NSEC(timings->tWB_max),
				       div * NFC_CLK_CYCLE);
	meson_chip->tadl = DIV_ROUND_UP(PSEC_TO_NSEC(timings->tADL_min),
					div * NFC_CLK_CYCLE);
	tbers_clocks = DIV_ROUND_UP_ULL(PSEC_TO_NSEC(timings->tBERS_max),
					div * NFC_CLK_CYCLE);
	meson_chip->tbers_max = ilog2(tbers_clocks);
	if (!is_power_of_2(tbers_clocks))
		meson_chip->tbers_max++;

	bt_min = DIV_ROUND_UP(bt_min, 1000);
	bt_max = DIV_ROUND_UP(bt_max, 1000);

	if (bt_max < bt_min)
		return -EINVAL;

	meson_chip->level1_divider = div;
	meson_chip->clk_rate = 1000000000 / meson_chip->level1_divider;
	meson_chip->bus_timing = (bt_min + bt_max) / 2 + 1;

	return 0;
}

static int meson_nand_bch_mode(struct nand_chip *nand)
{
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	int i;

	if (nand->ecc.strength > 60 || nand->ecc.strength < 8)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(meson_ecc); i++) {
		if (meson_ecc[i].strength == nand->ecc.strength) {
			meson_chip->bch_mode = meson_ecc[i].bch;
			return 0;
		}
	}

	return -EINVAL;
}

static void meson_nand_detach_chip(struct nand_chip *nand)
{
	meson_nfc_free_buffer(nand);
}

static int meson_nand_attach_chip(struct nand_chip *nand)
{
	struct meson_nfc *nfc = nand_get_controller_data(nand);
	struct meson_nfc_nand_chip *meson_chip = to_meson_nand(nand);
	struct mtd_info *mtd = nand_to_mtd(nand);
	int nsectors = mtd->writesize / 1024;
	int ret;

	if (!mtd->name) {
		mtd->name = devm_kasprintf(nfc->dev, GFP_KERNEL,
					   "%s:nand%d",
					   dev_name(nfc->dev),
					   meson_chip->sels[0]);
		if (!mtd->name)
			return -ENOMEM;
	}

	if (nand->bbt_options & NAND_BBT_USE_FLASH)
		nand->bbt_options |= NAND_BBT_NO_OOB;

	nand->options |= NAND_NO_SUBPAGE_WRITE;

	ret = nand_ecc_choose_conf(nand, nfc->data->ecc_caps,
				   mtd->oobsize - 2 * nsectors);
	if (ret) {
		dev_err(nfc->dev, "failed to ECC init\n");
		return -EINVAL;
	}

	mtd_set_ooblayout(mtd, &meson_ooblayout_ops);

	ret = meson_nand_bch_mode(nand);
	if (ret)
		return -EINVAL;

	nand->ecc.mode = NAND_ECC_HW;
	nand->ecc.write_page_raw = meson_nfc_write_page_raw;
	nand->ecc.write_page = meson_nfc_write_page_hwecc;
	nand->ecc.write_oob_raw = nand_write_oob_std;
	nand->ecc.write_oob = nand_write_oob_std;

	nand->ecc.read_page_raw = meson_nfc_read_page_raw;
	nand->ecc.read_page = meson_nfc_read_page_hwecc;
	nand->ecc.read_oob_raw = meson_nfc_read_oob_raw;
	nand->ecc.read_oob = meson_nfc_read_oob;

	if (nand->options & NAND_BUSWIDTH_16) {
		dev_err(nfc->dev, "16bits bus width not supported");
		return -EINVAL;
	}
	ret = meson_chip_buffer_init(nand);
	if (ret)
		return -ENOMEM;

	return ret;
}

static const struct nand_controller_ops meson_nand_controller_ops = {
	.attach_chip = meson_nand_attach_chip,
	.detach_chip = meson_nand_detach_chip,
	.setup_data_interface = meson_nfc_setup_data_interface,
	.exec_op = meson_nfc_exec_op,
};

static int
meson_nfc_nand_chip_init(struct device *dev,
			 struct meson_nfc *nfc, struct device_node *np)
{
	struct meson_nfc_nand_chip *meson_chip;
	struct nand_chip *nand;
	struct mtd_info *mtd;
	int ret, i;
	u32 tmp, nsels;

	nsels = of_property_count_elems_of_size(np, "reg", sizeof(u32));
	if (!nsels || nsels > MAX_CE_NUM) {
		dev_err(dev, "invalid register property size\n");
		return -EINVAL;
	}

	meson_chip = devm_kzalloc(dev, struct_size(meson_chip, sels, nsels),
				  GFP_KERNEL);
	if (!meson_chip)
		return -ENOMEM;

	meson_chip->nsels = nsels;

	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &tmp);
		if (ret) {
			dev_err(dev, "could not retrieve register property: %d\n",
				ret);
			return ret;
		}

		if (test_and_set_bit(tmp, &nfc->assigned_cs)) {
			dev_err(dev, "CS %d already assigned\n", tmp);
			return -EINVAL;
		}
	}

	nand = &meson_chip->nand;
	nand->controller = &nfc->controller;
	nand->controller->ops = &meson_nand_controller_ops;
	nand_set_flash_node(nand, np);
	nand_set_controller_data(nand, nfc);

	nand->options |= NAND_USE_BOUNCE_BUFFER;
	mtd = nand_to_mtd(nand);
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;

	ret = nand_scan(nand, nsels);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "failed to register MTD device: %d\n", ret);
		nand_cleanup(nand);
		return ret;
	}

	list_add_tail(&meson_chip->node, &nfc->chips);

	return 0;
}

static int meson_nfc_nand_chip_cleanup(struct meson_nfc *nfc)
{
	struct meson_nfc_nand_chip *meson_chip;
	struct mtd_info *mtd;
	int ret;

	while (!list_empty(&nfc->chips)) {
		meson_chip = list_first_entry(&nfc->chips,
					      struct meson_nfc_nand_chip, node);
		mtd = nand_to_mtd(&meson_chip->nand);
		ret = mtd_device_unregister(mtd);
		if (ret)
			return ret;

		meson_nfc_free_buffer(&meson_chip->nand);
		nand_cleanup(&meson_chip->nand);
		list_del(&meson_chip->node);
	}

	return 0;
}

static int meson_nfc_nand_chips_init(struct device *dev,
				     struct meson_nfc *nfc)
{
	struct device_node *np = dev->of_node;
	struct device_node *nand_np;
	int ret;

	for_each_child_of_node(np, nand_np) {
		ret = meson_nfc_nand_chip_init(dev, nfc, nand_np);
		if (ret) {
			meson_nfc_nand_chip_cleanup(nfc);
			of_node_put(nand_np);
			return ret;
		}
	}

	return 0;
}

static irqreturn_t meson_nfc_irq(int irq, void *id)
{
	struct meson_nfc *nfc = id;
	u32 cfg;

	cfg = readl(nfc->reg_base + NFC_REG_CFG);
	if (!(cfg & NFC_RB_IRQ_EN))
		return IRQ_NONE;

	cfg &= ~(NFC_RB_IRQ_EN);
	writel(cfg, nfc->reg_base + NFC_REG_CFG);

	complete(&nfc->completion);
	return IRQ_HANDLED;
}

static const struct meson_nfc_data meson_gxl_data = {
	.ecc_caps = &meson_gxl_ecc_caps,
};

static const struct meson_nfc_data meson_axg_data = {
	.ecc_caps = &meson_axg_ecc_caps,
};

static const struct of_device_id meson_nfc_id_table[] = {
	{
		.compatible = "amlogic,meson-gxl-nfc",
		.data = &meson_gxl_data,
	}, {
		.compatible = "amlogic,meson-axg-nfc",
		.data = &meson_axg_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, meson_nfc_id_table);

static int meson_nfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_nfc *nfc;
	struct resource *res;
	int ret, irq;

	nfc = devm_kzalloc(dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->data = of_device_get_match_data(&pdev->dev);
	if (!nfc->data)
		return -ENODEV;

	nand_controller_init(&nfc->controller);
	INIT_LIST_HEAD(&nfc->chips);
	init_completion(&nfc->completion);

	nfc->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nfc->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(nfc->reg_base))
		return PTR_ERR(nfc->reg_base);

	nfc->reg_clk =
		syscon_regmap_lookup_by_phandle(dev->of_node,
						"amlogic,mmc-syscon");
	if (IS_ERR(nfc->reg_clk)) {
		dev_err(dev, "Failed to lookup clock base\n");
		return PTR_ERR(nfc->reg_clk);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	ret = meson_nfc_clk_init(nfc);
	if (ret) {
		dev_err(dev, "failed to initialize NAND clock\n");
		return ret;
	}

	writel(0, nfc->reg_base + NFC_REG_CFG);
	ret = devm_request_irq(dev, irq, meson_nfc_irq, 0, dev_name(dev), nfc);
	if (ret) {
		dev_err(dev, "failed to request NFC IRQ\n");
		ret = -EINVAL;
		goto err_clk;
	}

	ret = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "failed to set DMA mask\n");
		goto err_clk;
	}

	platform_set_drvdata(pdev, nfc);

	ret = meson_nfc_nand_chips_init(dev, nfc);
	if (ret) {
		dev_err(dev, "failed to init NAND chips\n");
		goto err_clk;
	}

	return 0;
err_clk:
	meson_nfc_disable_clk(nfc);
	return ret;
}

static int meson_nfc_remove(struct platform_device *pdev)
{
	struct meson_nfc *nfc = platform_get_drvdata(pdev);
	int ret;

	ret = meson_nfc_nand_chip_cleanup(nfc);
	if (ret)
		return ret;

	meson_nfc_disable_clk(nfc);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver meson_nfc_driver = {
	.probe  = meson_nfc_probe,
	.remove = meson_nfc_remove,
	.driver = {
		.name  = "meson-nand",
		.of_match_table = meson_nfc_id_table,
	},
};
module_platform_driver(meson_nfc_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Liang Yang <liang.yang@amlogic.com>");
MODULE_DESCRIPTION("Amlogic's Meson NAND Flash Controller driver");
