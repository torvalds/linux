// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018
 * Author: Christophe Kerello <christophe.kerello@st.com>
 */

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/rawnand.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* Bad block marker length */
#define FMC2_BBM_LEN			2

/* ECC step size */
#define FMC2_ECC_STEP_SIZE		512

/* BCHDSRx registers length */
#define FMC2_BCHDSRS_LEN		20

/* HECCR length */
#define FMC2_HECCR_LEN			4

/* Max requests done for a 8k nand page size */
#define FMC2_MAX_SG			16

/* Max chip enable */
#define FMC2_MAX_CE			2

/* Max ECC buffer length */
#define FMC2_MAX_ECC_BUF_LEN		(FMC2_BCHDSRS_LEN * FMC2_MAX_SG)

#define FMC2_TIMEOUT_MS			1000

/* Timings */
#define FMC2_THIZ			1
#define FMC2_TIO			8000
#define FMC2_TSYNC			3000
#define FMC2_PCR_TIMING_MASK		0xf
#define FMC2_PMEM_PATT_TIMING_MASK	0xff

/* FMC2 Controller Registers */
#define FMC2_BCR1			0x0
#define FMC2_PCR			0x80
#define FMC2_SR				0x84
#define FMC2_PMEM			0x88
#define FMC2_PATT			0x8c
#define FMC2_HECCR			0x94
#define FMC2_CSQCR			0x200
#define FMC2_CSQCFGR1			0x204
#define FMC2_CSQCFGR2			0x208
#define FMC2_CSQCFGR3			0x20c
#define FMC2_CSQAR1			0x210
#define FMC2_CSQAR2			0x214
#define FMC2_CSQIER			0x220
#define FMC2_CSQISR			0x224
#define FMC2_CSQICR			0x228
#define FMC2_CSQEMSR			0x230
#define FMC2_BCHIER			0x250
#define FMC2_BCHISR			0x254
#define FMC2_BCHICR			0x258
#define FMC2_BCHPBR1			0x260
#define FMC2_BCHPBR2			0x264
#define FMC2_BCHPBR3			0x268
#define FMC2_BCHPBR4			0x26c
#define FMC2_BCHDSR0			0x27c
#define FMC2_BCHDSR1			0x280
#define FMC2_BCHDSR2			0x284
#define FMC2_BCHDSR3			0x288
#define FMC2_BCHDSR4			0x28c

/* Register: FMC2_BCR1 */
#define FMC2_BCR1_FMC2EN		BIT(31)

/* Register: FMC2_PCR */
#define FMC2_PCR_PWAITEN		BIT(1)
#define FMC2_PCR_PBKEN			BIT(2)
#define FMC2_PCR_PWID_MASK		GENMASK(5, 4)
#define FMC2_PCR_PWID(x)		(((x) & 0x3) << 4)
#define FMC2_PCR_PWID_BUSWIDTH_8	0
#define FMC2_PCR_PWID_BUSWIDTH_16	1
#define FMC2_PCR_ECCEN			BIT(6)
#define FMC2_PCR_ECCALG			BIT(8)
#define FMC2_PCR_TCLR_MASK		GENMASK(12, 9)
#define FMC2_PCR_TCLR(x)		(((x) & 0xf) << 9)
#define FMC2_PCR_TCLR_DEFAULT		0xf
#define FMC2_PCR_TAR_MASK		GENMASK(16, 13)
#define FMC2_PCR_TAR(x)			(((x) & 0xf) << 13)
#define FMC2_PCR_TAR_DEFAULT		0xf
#define FMC2_PCR_ECCSS_MASK		GENMASK(19, 17)
#define FMC2_PCR_ECCSS(x)		(((x) & 0x7) << 17)
#define FMC2_PCR_ECCSS_512		1
#define FMC2_PCR_ECCSS_2048		3
#define FMC2_PCR_BCHECC			BIT(24)
#define FMC2_PCR_WEN			BIT(25)

/* Register: FMC2_SR */
#define FMC2_SR_NWRF			BIT(6)

/* Register: FMC2_PMEM */
#define FMC2_PMEM_MEMSET(x)		(((x) & 0xff) << 0)
#define FMC2_PMEM_MEMWAIT(x)		(((x) & 0xff) << 8)
#define FMC2_PMEM_MEMHOLD(x)		(((x) & 0xff) << 16)
#define FMC2_PMEM_MEMHIZ(x)		(((x) & 0xff) << 24)
#define FMC2_PMEM_DEFAULT		0x0a0a0a0a

/* Register: FMC2_PATT */
#define FMC2_PATT_ATTSET(x)		(((x) & 0xff) << 0)
#define FMC2_PATT_ATTWAIT(x)		(((x) & 0xff) << 8)
#define FMC2_PATT_ATTHOLD(x)		(((x) & 0xff) << 16)
#define FMC2_PATT_ATTHIZ(x)		(((x) & 0xff) << 24)
#define FMC2_PATT_DEFAULT		0x0a0a0a0a

/* Register: FMC2_CSQCR */
#define FMC2_CSQCR_CSQSTART		BIT(0)

/* Register: FMC2_CSQCFGR1 */
#define FMC2_CSQCFGR1_CMD2EN		BIT(1)
#define FMC2_CSQCFGR1_DMADEN		BIT(2)
#define FMC2_CSQCFGR1_ACYNBR(x)		(((x) & 0x7) << 4)
#define FMC2_CSQCFGR1_CMD1(x)		(((x) & 0xff) << 8)
#define FMC2_CSQCFGR1_CMD2(x)		(((x) & 0xff) << 16)
#define FMC2_CSQCFGR1_CMD1T		BIT(24)
#define FMC2_CSQCFGR1_CMD2T		BIT(25)

/* Register: FMC2_CSQCFGR2 */
#define FMC2_CSQCFGR2_SQSDTEN		BIT(0)
#define FMC2_CSQCFGR2_RCMD2EN		BIT(1)
#define FMC2_CSQCFGR2_DMASEN		BIT(2)
#define FMC2_CSQCFGR2_RCMD1(x)		(((x) & 0xff) << 8)
#define FMC2_CSQCFGR2_RCMD2(x)		(((x) & 0xff) << 16)
#define FMC2_CSQCFGR2_RCMD1T		BIT(24)
#define FMC2_CSQCFGR2_RCMD2T		BIT(25)

/* Register: FMC2_CSQCFGR3 */
#define FMC2_CSQCFGR3_SNBR(x)		(((x) & 0x1f) << 8)
#define FMC2_CSQCFGR3_AC1T		BIT(16)
#define FMC2_CSQCFGR3_AC2T		BIT(17)
#define FMC2_CSQCFGR3_AC3T		BIT(18)
#define FMC2_CSQCFGR3_AC4T		BIT(19)
#define FMC2_CSQCFGR3_AC5T		BIT(20)
#define FMC2_CSQCFGR3_SDT		BIT(21)
#define FMC2_CSQCFGR3_RAC1T		BIT(22)
#define FMC2_CSQCFGR3_RAC2T		BIT(23)

/* Register: FMC2_CSQCAR1 */
#define FMC2_CSQCAR1_ADDC1(x)		(((x) & 0xff) << 0)
#define FMC2_CSQCAR1_ADDC2(x)		(((x) & 0xff) << 8)
#define FMC2_CSQCAR1_ADDC3(x)		(((x) & 0xff) << 16)
#define FMC2_CSQCAR1_ADDC4(x)		(((x) & 0xff) << 24)

/* Register: FMC2_CSQCAR2 */
#define FMC2_CSQCAR2_ADDC5(x)		(((x) & 0xff) << 0)
#define FMC2_CSQCAR2_NANDCEN(x)		(((x) & 0x3) << 10)
#define FMC2_CSQCAR2_SAO(x)		(((x) & 0xffff) << 16)

/* Register: FMC2_CSQIER */
#define FMC2_CSQIER_TCIE		BIT(0)

/* Register: FMC2_CSQICR */
#define FMC2_CSQICR_CLEAR_IRQ		GENMASK(4, 0)

/* Register: FMC2_CSQEMSR */
#define FMC2_CSQEMSR_SEM		GENMASK(15, 0)

/* Register: FMC2_BCHIER */
#define FMC2_BCHIER_DERIE		BIT(1)
#define FMC2_BCHIER_EPBRIE		BIT(4)

/* Register: FMC2_BCHICR */
#define FMC2_BCHICR_CLEAR_IRQ		GENMASK(4, 0)

/* Register: FMC2_BCHDSR0 */
#define FMC2_BCHDSR0_DUE		BIT(0)
#define FMC2_BCHDSR0_DEF		BIT(1)
#define FMC2_BCHDSR0_DEN_MASK		GENMASK(7, 4)
#define FMC2_BCHDSR0_DEN_SHIFT		4

/* Register: FMC2_BCHDSR1 */
#define FMC2_BCHDSR1_EBP1_MASK		GENMASK(12, 0)
#define FMC2_BCHDSR1_EBP2_MASK		GENMASK(28, 16)
#define FMC2_BCHDSR1_EBP2_SHIFT		16

/* Register: FMC2_BCHDSR2 */
#define FMC2_BCHDSR2_EBP3_MASK		GENMASK(12, 0)
#define FMC2_BCHDSR2_EBP4_MASK		GENMASK(28, 16)
#define FMC2_BCHDSR2_EBP4_SHIFT		16

/* Register: FMC2_BCHDSR3 */
#define FMC2_BCHDSR3_EBP5_MASK		GENMASK(12, 0)
#define FMC2_BCHDSR3_EBP6_MASK		GENMASK(28, 16)
#define FMC2_BCHDSR3_EBP6_SHIFT		16

/* Register: FMC2_BCHDSR4 */
#define FMC2_BCHDSR4_EBP7_MASK		GENMASK(12, 0)
#define FMC2_BCHDSR4_EBP8_MASK		GENMASK(28, 16)
#define FMC2_BCHDSR4_EBP8_SHIFT		16

enum stm32_fmc2_ecc {
	FMC2_ECC_HAM = 1,
	FMC2_ECC_BCH4 = 4,
	FMC2_ECC_BCH8 = 8
};

enum stm32_fmc2_irq_state {
	FMC2_IRQ_UNKNOWN = 0,
	FMC2_IRQ_BCH,
	FMC2_IRQ_SEQ
};

struct stm32_fmc2_timings {
	u8 tclr;
	u8 tar;
	u8 thiz;
	u8 twait;
	u8 thold_mem;
	u8 tset_mem;
	u8 thold_att;
	u8 tset_att;
};

struct stm32_fmc2_nand {
	struct nand_chip chip;
	struct stm32_fmc2_timings timings;
	int ncs;
	int cs_used[FMC2_MAX_CE];
};

static inline struct stm32_fmc2_nand *to_fmc2_nand(struct nand_chip *chip)
{
	return container_of(chip, struct stm32_fmc2_nand, chip);
}

struct stm32_fmc2_nfc {
	struct nand_controller base;
	struct stm32_fmc2_nand nand;
	struct device *dev;
	void __iomem *io_base;
	void __iomem *data_base[FMC2_MAX_CE];
	void __iomem *cmd_base[FMC2_MAX_CE];
	void __iomem *addr_base[FMC2_MAX_CE];
	phys_addr_t io_phys_addr;
	phys_addr_t data_phys_addr[FMC2_MAX_CE];
	struct clk *clk;
	u8 irq_state;

	struct dma_chan *dma_tx_ch;
	struct dma_chan *dma_rx_ch;
	struct dma_chan *dma_ecc_ch;
	struct sg_table dma_data_sg;
	struct sg_table dma_ecc_sg;
	u8 *ecc_buf;
	int dma_ecc_len;

	struct completion complete;
	struct completion dma_data_complete;
	struct completion dma_ecc_complete;

	u8 cs_assigned;
	int cs_sel;
};

static inline struct stm32_fmc2_nfc *to_stm32_nfc(struct nand_controller *base)
{
	return container_of(base, struct stm32_fmc2_nfc, base);
}

/* Timings configuration */
static void stm32_fmc2_timings_init(struct nand_chip *chip)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	struct stm32_fmc2_nand *nand = to_fmc2_nand(chip);
	struct stm32_fmc2_timings *timings = &nand->timings;
	u32 pcr = readl_relaxed(fmc2->io_base + FMC2_PCR);
	u32 pmem, patt;

	/* Set tclr/tar timings */
	pcr &= ~FMC2_PCR_TCLR_MASK;
	pcr |= FMC2_PCR_TCLR(timings->tclr);
	pcr &= ~FMC2_PCR_TAR_MASK;
	pcr |= FMC2_PCR_TAR(timings->tar);

	/* Set tset/twait/thold/thiz timings in common bank */
	pmem = FMC2_PMEM_MEMSET(timings->tset_mem);
	pmem |= FMC2_PMEM_MEMWAIT(timings->twait);
	pmem |= FMC2_PMEM_MEMHOLD(timings->thold_mem);
	pmem |= FMC2_PMEM_MEMHIZ(timings->thiz);

	/* Set tset/twait/thold/thiz timings in attribut bank */
	patt = FMC2_PATT_ATTSET(timings->tset_att);
	patt |= FMC2_PATT_ATTWAIT(timings->twait);
	patt |= FMC2_PATT_ATTHOLD(timings->thold_att);
	patt |= FMC2_PATT_ATTHIZ(timings->thiz);

	writel_relaxed(pcr, fmc2->io_base + FMC2_PCR);
	writel_relaxed(pmem, fmc2->io_base + FMC2_PMEM);
	writel_relaxed(patt, fmc2->io_base + FMC2_PATT);
}

/* Controller configuration */
static void stm32_fmc2_setup(struct nand_chip *chip)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	u32 pcr = readl_relaxed(fmc2->io_base + FMC2_PCR);

	/* Configure ECC algorithm (default configuration is Hamming) */
	pcr &= ~FMC2_PCR_ECCALG;
	pcr &= ~FMC2_PCR_BCHECC;
	if (chip->ecc.strength == FMC2_ECC_BCH8) {
		pcr |= FMC2_PCR_ECCALG;
		pcr |= FMC2_PCR_BCHECC;
	} else if (chip->ecc.strength == FMC2_ECC_BCH4) {
		pcr |= FMC2_PCR_ECCALG;
	}

	/* Set buswidth */
	pcr &= ~FMC2_PCR_PWID_MASK;
	if (chip->options & NAND_BUSWIDTH_16)
		pcr |= FMC2_PCR_PWID(FMC2_PCR_PWID_BUSWIDTH_16);

	/* Set ECC sector size */
	pcr &= ~FMC2_PCR_ECCSS_MASK;
	pcr |= FMC2_PCR_ECCSS(FMC2_PCR_ECCSS_512);

	writel_relaxed(pcr, fmc2->io_base + FMC2_PCR);
}

/* Select target */
static int stm32_fmc2_select_chip(struct nand_chip *chip, int chipnr)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	struct stm32_fmc2_nand *nand = to_fmc2_nand(chip);
	struct dma_slave_config dma_cfg;
	int ret;

	if (nand->cs_used[chipnr] == fmc2->cs_sel)
		return 0;

	fmc2->cs_sel = nand->cs_used[chipnr];

	/* FMC2 setup routine */
	stm32_fmc2_setup(chip);

	/* Apply timings */
	stm32_fmc2_timings_init(chip);

	if (fmc2->dma_tx_ch && fmc2->dma_rx_ch) {
		memset(&dma_cfg, 0, sizeof(dma_cfg));
		dma_cfg.src_addr = fmc2->data_phys_addr[fmc2->cs_sel];
		dma_cfg.dst_addr = fmc2->data_phys_addr[fmc2->cs_sel];
		dma_cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_cfg.src_maxburst = 32;
		dma_cfg.dst_maxburst = 32;

		ret = dmaengine_slave_config(fmc2->dma_tx_ch, &dma_cfg);
		if (ret) {
			dev_err(fmc2->dev, "tx DMA engine slave config failed\n");
			return ret;
		}

		ret = dmaengine_slave_config(fmc2->dma_rx_ch, &dma_cfg);
		if (ret) {
			dev_err(fmc2->dev, "rx DMA engine slave config failed\n");
			return ret;
		}
	}

	if (fmc2->dma_ecc_ch) {
		/*
		 * Hamming: we read HECCR register
		 * BCH4/BCH8: we read BCHDSRSx registers
		 */
		memset(&dma_cfg, 0, sizeof(dma_cfg));
		dma_cfg.src_addr = fmc2->io_phys_addr;
		dma_cfg.src_addr += chip->ecc.strength == FMC2_ECC_HAM ?
				    FMC2_HECCR : FMC2_BCHDSR0;
		dma_cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

		ret = dmaengine_slave_config(fmc2->dma_ecc_ch, &dma_cfg);
		if (ret) {
			dev_err(fmc2->dev, "ECC DMA engine slave config failed\n");
			return ret;
		}

		/* Calculate ECC length needed for one sector */
		fmc2->dma_ecc_len = chip->ecc.strength == FMC2_ECC_HAM ?
				    FMC2_HECCR_LEN : FMC2_BCHDSRS_LEN;
	}

	return 0;
}

/* Set bus width to 16-bit or 8-bit */
static void stm32_fmc2_set_buswidth_16(struct stm32_fmc2_nfc *fmc2, bool set)
{
	u32 pcr = readl_relaxed(fmc2->io_base + FMC2_PCR);

	pcr &= ~FMC2_PCR_PWID_MASK;
	if (set)
		pcr |= FMC2_PCR_PWID(FMC2_PCR_PWID_BUSWIDTH_16);
	writel_relaxed(pcr, fmc2->io_base + FMC2_PCR);
}

/* Enable/disable ECC */
static void stm32_fmc2_set_ecc(struct stm32_fmc2_nfc *fmc2, bool enable)
{
	u32 pcr = readl(fmc2->io_base + FMC2_PCR);

	pcr &= ~FMC2_PCR_ECCEN;
	if (enable)
		pcr |= FMC2_PCR_ECCEN;
	writel(pcr, fmc2->io_base + FMC2_PCR);
}

/* Enable irq sources in case of the sequencer is used */
static inline void stm32_fmc2_enable_seq_irq(struct stm32_fmc2_nfc *fmc2)
{
	u32 csqier = readl_relaxed(fmc2->io_base + FMC2_CSQIER);

	csqier |= FMC2_CSQIER_TCIE;

	fmc2->irq_state = FMC2_IRQ_SEQ;

	writel_relaxed(csqier, fmc2->io_base + FMC2_CSQIER);
}

/* Disable irq sources in case of the sequencer is used */
static inline void stm32_fmc2_disable_seq_irq(struct stm32_fmc2_nfc *fmc2)
{
	u32 csqier = readl_relaxed(fmc2->io_base + FMC2_CSQIER);

	csqier &= ~FMC2_CSQIER_TCIE;

	writel_relaxed(csqier, fmc2->io_base + FMC2_CSQIER);

	fmc2->irq_state = FMC2_IRQ_UNKNOWN;
}

/* Clear irq sources in case of the sequencer is used */
static inline void stm32_fmc2_clear_seq_irq(struct stm32_fmc2_nfc *fmc2)
{
	writel_relaxed(FMC2_CSQICR_CLEAR_IRQ, fmc2->io_base + FMC2_CSQICR);
}

/* Enable irq sources in case of bch is used */
static inline void stm32_fmc2_enable_bch_irq(struct stm32_fmc2_nfc *fmc2,
					     int mode)
{
	u32 bchier = readl_relaxed(fmc2->io_base + FMC2_BCHIER);

	if (mode == NAND_ECC_WRITE)
		bchier |= FMC2_BCHIER_EPBRIE;
	else
		bchier |= FMC2_BCHIER_DERIE;

	fmc2->irq_state = FMC2_IRQ_BCH;

	writel_relaxed(bchier, fmc2->io_base + FMC2_BCHIER);
}

/* Disable irq sources in case of bch is used */
static inline void stm32_fmc2_disable_bch_irq(struct stm32_fmc2_nfc *fmc2)
{
	u32 bchier = readl_relaxed(fmc2->io_base + FMC2_BCHIER);

	bchier &= ~FMC2_BCHIER_DERIE;
	bchier &= ~FMC2_BCHIER_EPBRIE;

	writel_relaxed(bchier, fmc2->io_base + FMC2_BCHIER);

	fmc2->irq_state = FMC2_IRQ_UNKNOWN;
}

/* Clear irq sources in case of bch is used */
static inline void stm32_fmc2_clear_bch_irq(struct stm32_fmc2_nfc *fmc2)
{
	writel_relaxed(FMC2_BCHICR_CLEAR_IRQ, fmc2->io_base + FMC2_BCHICR);
}

/*
 * Enable ECC logic and reset syndrome/parity bits previously calculated
 * Syndrome/parity bits is cleared by setting the ECCEN bit to 0
 */
static void stm32_fmc2_hwctl(struct nand_chip *chip, int mode)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);

	stm32_fmc2_set_ecc(fmc2, false);

	if (chip->ecc.strength != FMC2_ECC_HAM) {
		u32 pcr = readl_relaxed(fmc2->io_base + FMC2_PCR);

		if (mode == NAND_ECC_WRITE)
			pcr |= FMC2_PCR_WEN;
		else
			pcr &= ~FMC2_PCR_WEN;
		writel_relaxed(pcr, fmc2->io_base + FMC2_PCR);

		reinit_completion(&fmc2->complete);
		stm32_fmc2_clear_bch_irq(fmc2);
		stm32_fmc2_enable_bch_irq(fmc2, mode);
	}

	stm32_fmc2_set_ecc(fmc2, true);
}

/*
 * ECC Hamming calculation
 * ECC is 3 bytes for 512 bytes of data (supports error correction up to
 * max of 1-bit)
 */
static inline void stm32_fmc2_ham_set_ecc(const u32 ecc_sta, u8 *ecc)
{
	ecc[0] = ecc_sta;
	ecc[1] = ecc_sta >> 8;
	ecc[2] = ecc_sta >> 16;
}

static int stm32_fmc2_ham_calculate(struct nand_chip *chip, const u8 *data,
				    u8 *ecc)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	u32 sr, heccr;
	int ret;

	ret = readl_relaxed_poll_timeout(fmc2->io_base + FMC2_SR,
					 sr, sr & FMC2_SR_NWRF, 10,
					 FMC2_TIMEOUT_MS);
	if (ret) {
		dev_err(fmc2->dev, "ham timeout\n");
		return ret;
	}

	heccr = readl_relaxed(fmc2->io_base + FMC2_HECCR);

	stm32_fmc2_ham_set_ecc(heccr, ecc);

	/* Disable ECC */
	stm32_fmc2_set_ecc(fmc2, false);

	return 0;
}

static int stm32_fmc2_ham_correct(struct nand_chip *chip, u8 *dat,
				  u8 *read_ecc, u8 *calc_ecc)
{
	u8 bit_position = 0, b0, b1, b2;
	u32 byte_addr = 0, b;
	u32 i, shifting = 1;

	/* Indicate which bit and byte is faulty (if any) */
	b0 = read_ecc[0] ^ calc_ecc[0];
	b1 = read_ecc[1] ^ calc_ecc[1];
	b2 = read_ecc[2] ^ calc_ecc[2];
	b = b0 | (b1 << 8) | (b2 << 16);

	/* No errors */
	if (likely(!b))
		return 0;

	/* Calculate bit position */
	for (i = 0; i < 3; i++) {
		switch (b % 4) {
		case 2:
			bit_position += shifting;
		case 1:
			break;
		default:
			return -EBADMSG;
		}
		shifting <<= 1;
		b >>= 2;
	}

	/* Calculate byte position */
	shifting = 1;
	for (i = 0; i < 9; i++) {
		switch (b % 4) {
		case 2:
			byte_addr += shifting;
		case 1:
			break;
		default:
			return -EBADMSG;
		}
		shifting <<= 1;
		b >>= 2;
	}

	/* Flip the bit */
	dat[byte_addr] ^= (1 << bit_position);

	return 1;
}

/*
 * ECC BCH calculation and correction
 * ECC is 7/13 bytes for 512 bytes of data (supports error correction up to
 * max of 4-bit/8-bit)
 */
static int stm32_fmc2_bch_calculate(struct nand_chip *chip, const u8 *data,
				    u8 *ecc)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	u32 bchpbr;

	/* Wait until the BCH code is ready */
	if (!wait_for_completion_timeout(&fmc2->complete,
					 msecs_to_jiffies(FMC2_TIMEOUT_MS))) {
		dev_err(fmc2->dev, "bch timeout\n");
		stm32_fmc2_disable_bch_irq(fmc2);
		return -ETIMEDOUT;
	}

	/* Read parity bits */
	bchpbr = readl_relaxed(fmc2->io_base + FMC2_BCHPBR1);
	ecc[0] = bchpbr;
	ecc[1] = bchpbr >> 8;
	ecc[2] = bchpbr >> 16;
	ecc[3] = bchpbr >> 24;

	bchpbr = readl_relaxed(fmc2->io_base + FMC2_BCHPBR2);
	ecc[4] = bchpbr;
	ecc[5] = bchpbr >> 8;
	ecc[6] = bchpbr >> 16;

	if (chip->ecc.strength == FMC2_ECC_BCH8) {
		ecc[7] = bchpbr >> 24;

		bchpbr = readl_relaxed(fmc2->io_base + FMC2_BCHPBR3);
		ecc[8] = bchpbr;
		ecc[9] = bchpbr >> 8;
		ecc[10] = bchpbr >> 16;
		ecc[11] = bchpbr >> 24;

		bchpbr = readl_relaxed(fmc2->io_base + FMC2_BCHPBR4);
		ecc[12] = bchpbr;
	}

	/* Disable ECC */
	stm32_fmc2_set_ecc(fmc2, false);

	return 0;
}

/* BCH algorithm correction */
static int stm32_fmc2_bch_decode(int eccsize, u8 *dat, u32 *ecc_sta)
{
	u32 bchdsr0 = ecc_sta[0];
	u32 bchdsr1 = ecc_sta[1];
	u32 bchdsr2 = ecc_sta[2];
	u32 bchdsr3 = ecc_sta[3];
	u32 bchdsr4 = ecc_sta[4];
	u16 pos[8];
	int i, den;
	unsigned int nb_errs = 0;

	/* No errors found */
	if (likely(!(bchdsr0 & FMC2_BCHDSR0_DEF)))
		return 0;

	/* Too many errors detected */
	if (unlikely(bchdsr0 & FMC2_BCHDSR0_DUE))
		return -EBADMSG;

	pos[0] = bchdsr1 & FMC2_BCHDSR1_EBP1_MASK;
	pos[1] = (bchdsr1 & FMC2_BCHDSR1_EBP2_MASK) >> FMC2_BCHDSR1_EBP2_SHIFT;
	pos[2] = bchdsr2 & FMC2_BCHDSR2_EBP3_MASK;
	pos[3] = (bchdsr2 & FMC2_BCHDSR2_EBP4_MASK) >> FMC2_BCHDSR2_EBP4_SHIFT;
	pos[4] = bchdsr3 & FMC2_BCHDSR3_EBP5_MASK;
	pos[5] = (bchdsr3 & FMC2_BCHDSR3_EBP6_MASK) >> FMC2_BCHDSR3_EBP6_SHIFT;
	pos[6] = bchdsr4 & FMC2_BCHDSR4_EBP7_MASK;
	pos[7] = (bchdsr4 & FMC2_BCHDSR4_EBP8_MASK) >> FMC2_BCHDSR4_EBP8_SHIFT;

	den = (bchdsr0 & FMC2_BCHDSR0_DEN_MASK) >> FMC2_BCHDSR0_DEN_SHIFT;
	for (i = 0; i < den; i++) {
		if (pos[i] < eccsize * 8) {
			change_bit(pos[i], (unsigned long *)dat);
			nb_errs++;
		}
	}

	return nb_errs;
}

static int stm32_fmc2_bch_correct(struct nand_chip *chip, u8 *dat,
				  u8 *read_ecc, u8 *calc_ecc)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	u32 ecc_sta[5];

	/* Wait until the decoding error is ready */
	if (!wait_for_completion_timeout(&fmc2->complete,
					 msecs_to_jiffies(FMC2_TIMEOUT_MS))) {
		dev_err(fmc2->dev, "bch timeout\n");
		stm32_fmc2_disable_bch_irq(fmc2);
		return -ETIMEDOUT;
	}

	ecc_sta[0] = readl_relaxed(fmc2->io_base + FMC2_BCHDSR0);
	ecc_sta[1] = readl_relaxed(fmc2->io_base + FMC2_BCHDSR1);
	ecc_sta[2] = readl_relaxed(fmc2->io_base + FMC2_BCHDSR2);
	ecc_sta[3] = readl_relaxed(fmc2->io_base + FMC2_BCHDSR3);
	ecc_sta[4] = readl_relaxed(fmc2->io_base + FMC2_BCHDSR4);

	/* Disable ECC */
	stm32_fmc2_set_ecc(fmc2, false);

	return stm32_fmc2_bch_decode(chip->ecc.size, dat, ecc_sta);
}

static int stm32_fmc2_read_page(struct nand_chip *chip, u8 *buf,
				int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret, i, s, stat, eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	int eccstrength = chip->ecc.strength;
	u8 *p = buf;
	u8 *ecc_calc = chip->ecc.calc_buf;
	u8 *ecc_code = chip->ecc.code_buf;
	unsigned int max_bitflips = 0;

	ret = nand_read_page_op(chip, page, 0, NULL, 0);
	if (ret)
		return ret;

	for (i = mtd->writesize + FMC2_BBM_LEN, s = 0; s < eccsteps;
	     s++, i += eccbytes, p += eccsize) {
		chip->ecc.hwctl(chip, NAND_ECC_READ);

		/* Read the nand page sector (512 bytes) */
		ret = nand_change_read_column_op(chip, s * eccsize, p,
						 eccsize, false);
		if (ret)
			return ret;

		/* Read the corresponding ECC bytes */
		ret = nand_change_read_column_op(chip, i, ecc_code,
						 eccbytes, false);
		if (ret)
			return ret;

		/* Correct the data */
		stat = chip->ecc.correct(chip, p, ecc_code, ecc_calc);
		if (stat == -EBADMSG)
			/* Check for empty pages with bitflips */
			stat = nand_check_erased_ecc_chunk(p, eccsize,
							   ecc_code, eccbytes,
							   NULL, 0,
							   eccstrength);

		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}

	/* Read oob */
	if (oob_required) {
		ret = nand_change_read_column_op(chip, mtd->writesize,
						 chip->oob_poi, mtd->oobsize,
						 false);
		if (ret)
			return ret;
	}

	return max_bitflips;
}

/* Sequencer read/write configuration */
static void stm32_fmc2_rw_page_init(struct nand_chip *chip, int page,
				    int raw, bool write_data)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 csqcfgr1, csqcfgr2, csqcfgr3;
	u32 csqar1, csqar2;
	u32 ecc_offset = mtd->writesize + FMC2_BBM_LEN;
	u32 pcr = readl_relaxed(fmc2->io_base + FMC2_PCR);

	if (write_data)
		pcr |= FMC2_PCR_WEN;
	else
		pcr &= ~FMC2_PCR_WEN;
	writel_relaxed(pcr, fmc2->io_base + FMC2_PCR);

	/*
	 * - Set Program Page/Page Read command
	 * - Enable DMA request data
	 * - Set timings
	 */
	csqcfgr1 = FMC2_CSQCFGR1_DMADEN | FMC2_CSQCFGR1_CMD1T;
	if (write_data)
		csqcfgr1 |= FMC2_CSQCFGR1_CMD1(NAND_CMD_SEQIN);
	else
		csqcfgr1 |= FMC2_CSQCFGR1_CMD1(NAND_CMD_READ0) |
			    FMC2_CSQCFGR1_CMD2EN |
			    FMC2_CSQCFGR1_CMD2(NAND_CMD_READSTART) |
			    FMC2_CSQCFGR1_CMD2T;

	/*
	 * - Set Random Data Input/Random Data Read command
	 * - Enable the sequencer to access the Spare data area
	 * - Enable  DMA request status decoding for read
	 * - Set timings
	 */
	if (write_data)
		csqcfgr2 = FMC2_CSQCFGR2_RCMD1(NAND_CMD_RNDIN);
	else
		csqcfgr2 = FMC2_CSQCFGR2_RCMD1(NAND_CMD_RNDOUT) |
			   FMC2_CSQCFGR2_RCMD2EN |
			   FMC2_CSQCFGR2_RCMD2(NAND_CMD_RNDOUTSTART) |
			   FMC2_CSQCFGR2_RCMD1T |
			   FMC2_CSQCFGR2_RCMD2T;
	if (!raw) {
		csqcfgr2 |= write_data ? 0 : FMC2_CSQCFGR2_DMASEN;
		csqcfgr2 |= FMC2_CSQCFGR2_SQSDTEN;
	}

	/*
	 * - Set the number of sectors to be written
	 * - Set timings
	 */
	csqcfgr3 = FMC2_CSQCFGR3_SNBR(chip->ecc.steps - 1);
	if (write_data) {
		csqcfgr3 |= FMC2_CSQCFGR3_RAC2T;
		if (chip->options & NAND_ROW_ADDR_3)
			csqcfgr3 |= FMC2_CSQCFGR3_AC5T;
		else
			csqcfgr3 |= FMC2_CSQCFGR3_AC4T;
	}

	/*
	 * Set the fourth first address cycles
	 * Byte 1 and byte 2 => column, we start at 0x0
	 * Byte 3 and byte 4 => page
	 */
	csqar1 = FMC2_CSQCAR1_ADDC3(page);
	csqar1 |= FMC2_CSQCAR1_ADDC4(page >> 8);

	/*
	 * - Set chip enable number
	 * - Set ECC byte offset in the spare area
	 * - Calculate the number of address cycles to be issued
	 * - Set byte 5 of address cycle if needed
	 */
	csqar2 = FMC2_CSQCAR2_NANDCEN(fmc2->cs_sel);
	if (chip->options & NAND_BUSWIDTH_16)
		csqar2 |= FMC2_CSQCAR2_SAO(ecc_offset >> 1);
	else
		csqar2 |= FMC2_CSQCAR2_SAO(ecc_offset);
	if (chip->options & NAND_ROW_ADDR_3) {
		csqcfgr1 |= FMC2_CSQCFGR1_ACYNBR(5);
		csqar2 |= FMC2_CSQCAR2_ADDC5(page >> 16);
	} else {
		csqcfgr1 |= FMC2_CSQCFGR1_ACYNBR(4);
	}

	writel_relaxed(csqcfgr1, fmc2->io_base + FMC2_CSQCFGR1);
	writel_relaxed(csqcfgr2, fmc2->io_base + FMC2_CSQCFGR2);
	writel_relaxed(csqcfgr3, fmc2->io_base + FMC2_CSQCFGR3);
	writel_relaxed(csqar1, fmc2->io_base + FMC2_CSQAR1);
	writel_relaxed(csqar2, fmc2->io_base + FMC2_CSQAR2);
}

static void stm32_fmc2_dma_callback(void *arg)
{
	complete((struct completion *)arg);
}

/* Read/write data from/to a page */
static int stm32_fmc2_xfer(struct nand_chip *chip, const u8 *buf,
			   int raw, bool write_data)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	struct dma_async_tx_descriptor *desc_data, *desc_ecc;
	struct scatterlist *sg;
	struct dma_chan *dma_ch = fmc2->dma_rx_ch;
	enum dma_data_direction dma_data_dir = DMA_FROM_DEVICE;
	enum dma_transfer_direction dma_transfer_dir = DMA_DEV_TO_MEM;
	u32 csqcr = readl_relaxed(fmc2->io_base + FMC2_CSQCR);
	int eccsteps = chip->ecc.steps;
	int eccsize = chip->ecc.size;
	const u8 *p = buf;
	int s, ret;

	/* Configure DMA data */
	if (write_data) {
		dma_data_dir = DMA_TO_DEVICE;
		dma_transfer_dir = DMA_MEM_TO_DEV;
		dma_ch = fmc2->dma_tx_ch;
	}

	for_each_sg(fmc2->dma_data_sg.sgl, sg, eccsteps, s) {
		sg_set_buf(sg, p, eccsize);
		p += eccsize;
	}

	ret = dma_map_sg(fmc2->dev, fmc2->dma_data_sg.sgl,
			 eccsteps, dma_data_dir);
	if (ret < 0)
		return ret;

	desc_data = dmaengine_prep_slave_sg(dma_ch, fmc2->dma_data_sg.sgl,
					    eccsteps, dma_transfer_dir,
					    DMA_PREP_INTERRUPT);
	if (!desc_data) {
		ret = -ENOMEM;
		goto err_unmap_data;
	}

	reinit_completion(&fmc2->dma_data_complete);
	reinit_completion(&fmc2->complete);
	desc_data->callback = stm32_fmc2_dma_callback;
	desc_data->callback_param = &fmc2->dma_data_complete;
	ret = dma_submit_error(dmaengine_submit(desc_data));
	if (ret)
		goto err_unmap_data;

	dma_async_issue_pending(dma_ch);

	if (!write_data && !raw) {
		/* Configure DMA ECC status */
		p = fmc2->ecc_buf;
		for_each_sg(fmc2->dma_ecc_sg.sgl, sg, eccsteps, s) {
			sg_set_buf(sg, p, fmc2->dma_ecc_len);
			p += fmc2->dma_ecc_len;
		}

		ret = dma_map_sg(fmc2->dev, fmc2->dma_ecc_sg.sgl,
				 eccsteps, dma_data_dir);
		if (ret < 0)
			goto err_unmap_data;

		desc_ecc = dmaengine_prep_slave_sg(fmc2->dma_ecc_ch,
						   fmc2->dma_ecc_sg.sgl,
						   eccsteps, dma_transfer_dir,
						   DMA_PREP_INTERRUPT);
		if (!desc_ecc) {
			ret = -ENOMEM;
			goto err_unmap_ecc;
		}

		reinit_completion(&fmc2->dma_ecc_complete);
		desc_ecc->callback = stm32_fmc2_dma_callback;
		desc_ecc->callback_param = &fmc2->dma_ecc_complete;
		ret = dma_submit_error(dmaengine_submit(desc_ecc));
		if (ret)
			goto err_unmap_ecc;

		dma_async_issue_pending(fmc2->dma_ecc_ch);
	}

	stm32_fmc2_clear_seq_irq(fmc2);
	stm32_fmc2_enable_seq_irq(fmc2);

	/* Start the transfer */
	csqcr |= FMC2_CSQCR_CSQSTART;
	writel_relaxed(csqcr, fmc2->io_base + FMC2_CSQCR);

	/* Wait end of sequencer transfer */
	if (!wait_for_completion_timeout(&fmc2->complete,
					 msecs_to_jiffies(FMC2_TIMEOUT_MS))) {
		dev_err(fmc2->dev, "seq timeout\n");
		stm32_fmc2_disable_seq_irq(fmc2);
		dmaengine_terminate_all(dma_ch);
		if (!write_data && !raw)
			dmaengine_terminate_all(fmc2->dma_ecc_ch);
		ret = -ETIMEDOUT;
		goto err_unmap_ecc;
	}

	/* Wait DMA data transfer completion */
	if (!wait_for_completion_timeout(&fmc2->dma_data_complete,
					 msecs_to_jiffies(FMC2_TIMEOUT_MS))) {
		dev_err(fmc2->dev, "data DMA timeout\n");
		dmaengine_terminate_all(dma_ch);
		ret = -ETIMEDOUT;
	}

	/* Wait DMA ECC transfer completion */
	if (!write_data && !raw) {
		if (!wait_for_completion_timeout(&fmc2->dma_ecc_complete,
					msecs_to_jiffies(FMC2_TIMEOUT_MS))) {
			dev_err(fmc2->dev, "ECC DMA timeout\n");
			dmaengine_terminate_all(fmc2->dma_ecc_ch);
			ret = -ETIMEDOUT;
		}
	}

err_unmap_ecc:
	if (!write_data && !raw)
		dma_unmap_sg(fmc2->dev, fmc2->dma_ecc_sg.sgl,
			     eccsteps, dma_data_dir);

err_unmap_data:
	dma_unmap_sg(fmc2->dev, fmc2->dma_data_sg.sgl, eccsteps, dma_data_dir);

	return ret;
}

static int stm32_fmc2_sequencer_write(struct nand_chip *chip,
				      const u8 *buf, int oob_required,
				      int page, int raw)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	/* Configure the sequencer */
	stm32_fmc2_rw_page_init(chip, page, raw, true);

	/* Write the page */
	ret = stm32_fmc2_xfer(chip, buf, raw, true);
	if (ret)
		return ret;

	/* Write oob */
	if (oob_required) {
		ret = nand_change_write_column_op(chip, mtd->writesize,
						  chip->oob_poi, mtd->oobsize,
						  false);
		if (ret)
			return ret;
	}

	return nand_prog_page_end_op(chip);
}

static int stm32_fmc2_sequencer_write_page(struct nand_chip *chip,
					   const u8 *buf,
					   int oob_required,
					   int page)
{
	int ret;

	/* Select the target */
	ret = stm32_fmc2_select_chip(chip, chip->cur_cs);
	if (ret)
		return ret;

	return stm32_fmc2_sequencer_write(chip, buf, oob_required, page, false);
}

static int stm32_fmc2_sequencer_write_page_raw(struct nand_chip *chip,
					       const u8 *buf,
					       int oob_required,
					       int page)
{
	int ret;

	/* Select the target */
	ret = stm32_fmc2_select_chip(chip, chip->cur_cs);
	if (ret)
		return ret;

	return stm32_fmc2_sequencer_write(chip, buf, oob_required, page, true);
}

/* Get a status indicating which sectors have errors */
static inline u16 stm32_fmc2_get_mapping_status(struct stm32_fmc2_nfc *fmc2)
{
	u32 csqemsr = readl_relaxed(fmc2->io_base + FMC2_CSQEMSR);

	return csqemsr & FMC2_CSQEMSR_SEM;
}

static int stm32_fmc2_sequencer_correct(struct nand_chip *chip, u8 *dat,
					u8 *read_ecc, u8 *calc_ecc)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	int eccstrength = chip->ecc.strength;
	int i, s, eccsize = chip->ecc.size;
	u32 *ecc_sta = (u32 *)fmc2->ecc_buf;
	u16 sta_map = stm32_fmc2_get_mapping_status(fmc2);
	unsigned int max_bitflips = 0;

	for (i = 0, s = 0; s < eccsteps; s++, i += eccbytes, dat += eccsize) {
		int stat = 0;

		if (eccstrength == FMC2_ECC_HAM) {
			/* Ecc_sta = FMC2_HECCR */
			if (sta_map & BIT(s)) {
				stm32_fmc2_ham_set_ecc(*ecc_sta, &calc_ecc[i]);
				stat = stm32_fmc2_ham_correct(chip, dat,
							      &read_ecc[i],
							      &calc_ecc[i]);
			}
			ecc_sta++;
		} else {
			/*
			 * Ecc_sta[0] = FMC2_BCHDSR0
			 * Ecc_sta[1] = FMC2_BCHDSR1
			 * Ecc_sta[2] = FMC2_BCHDSR2
			 * Ecc_sta[3] = FMC2_BCHDSR3
			 * Ecc_sta[4] = FMC2_BCHDSR4
			 */
			if (sta_map & BIT(s))
				stat = stm32_fmc2_bch_decode(eccsize, dat,
							     ecc_sta);
			ecc_sta += 5;
		}

		if (stat == -EBADMSG)
			/* Check for empty pages with bitflips */
			stat = nand_check_erased_ecc_chunk(dat, eccsize,
							   &read_ecc[i],
							   eccbytes,
							   NULL, 0,
							   eccstrength);

		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}

	return max_bitflips;
}

static int stm32_fmc2_sequencer_read_page(struct nand_chip *chip, u8 *buf,
					  int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	u8 *ecc_calc = chip->ecc.calc_buf;
	u8 *ecc_code = chip->ecc.code_buf;
	u16 sta_map;
	int ret;

	/* Select the target */
	ret = stm32_fmc2_select_chip(chip, chip->cur_cs);
	if (ret)
		return ret;

	/* Configure the sequencer */
	stm32_fmc2_rw_page_init(chip, page, 0, false);

	/* Read the page */
	ret = stm32_fmc2_xfer(chip, buf, 0, false);
	if (ret)
		return ret;

	sta_map = stm32_fmc2_get_mapping_status(fmc2);

	/* Check if errors happen */
	if (likely(!sta_map)) {
		if (oob_required)
			return nand_change_read_column_op(chip, mtd->writesize,
							  chip->oob_poi,
							  mtd->oobsize, false);

		return 0;
	}

	/* Read oob */
	ret = nand_change_read_column_op(chip, mtd->writesize,
					 chip->oob_poi, mtd->oobsize, false);
	if (ret)
		return ret;

	ret = mtd_ooblayout_get_eccbytes(mtd, ecc_code, chip->oob_poi, 0,
					 chip->ecc.total);
	if (ret)
		return ret;

	/* Correct data */
	return chip->ecc.correct(chip, buf, ecc_code, ecc_calc);
}

static int stm32_fmc2_sequencer_read_page_raw(struct nand_chip *chip, u8 *buf,
					      int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	/* Select the target */
	ret = stm32_fmc2_select_chip(chip, chip->cur_cs);
	if (ret)
		return ret;

	/* Configure the sequencer */
	stm32_fmc2_rw_page_init(chip, page, 1, false);

	/* Read the page */
	ret = stm32_fmc2_xfer(chip, buf, 1, false);
	if (ret)
		return ret;

	/* Read oob */
	if (oob_required)
		return nand_change_read_column_op(chip, mtd->writesize,
						  chip->oob_poi, mtd->oobsize,
						  false);

	return 0;
}

static irqreturn_t stm32_fmc2_irq(int irq, void *dev_id)
{
	struct stm32_fmc2_nfc *fmc2 = (struct stm32_fmc2_nfc *)dev_id;

	if (fmc2->irq_state == FMC2_IRQ_SEQ)
		/* Sequencer is used */
		stm32_fmc2_disable_seq_irq(fmc2);
	else if (fmc2->irq_state == FMC2_IRQ_BCH)
		/* BCH is used */
		stm32_fmc2_disable_bch_irq(fmc2);

	complete(&fmc2->complete);

	return IRQ_HANDLED;
}

static void stm32_fmc2_read_data(struct nand_chip *chip, void *buf,
				 unsigned int len, bool force_8bit)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	void __iomem *io_addr_r = fmc2->data_base[fmc2->cs_sel];

	if (force_8bit && chip->options & NAND_BUSWIDTH_16)
		/* Reconfigure bus width to 8-bit */
		stm32_fmc2_set_buswidth_16(fmc2, false);

	if (!IS_ALIGNED((uintptr_t)buf, sizeof(u32))) {
		if (!IS_ALIGNED((uintptr_t)buf, sizeof(u16)) && len) {
			*(u8 *)buf = readb_relaxed(io_addr_r);
			buf += sizeof(u8);
			len -= sizeof(u8);
		}

		if (!IS_ALIGNED((uintptr_t)buf, sizeof(u32)) &&
		    len >= sizeof(u16)) {
			*(u16 *)buf = readw_relaxed(io_addr_r);
			buf += sizeof(u16);
			len -= sizeof(u16);
		}
	}

	/* Buf is aligned */
	while (len >= sizeof(u32)) {
		*(u32 *)buf = readl_relaxed(io_addr_r);
		buf += sizeof(u32);
		len -= sizeof(u32);
	}

	/* Read remaining bytes */
	if (len >= sizeof(u16)) {
		*(u16 *)buf = readw_relaxed(io_addr_r);
		buf += sizeof(u16);
		len -= sizeof(u16);
	}

	if (len)
		*(u8 *)buf = readb_relaxed(io_addr_r);

	if (force_8bit && chip->options & NAND_BUSWIDTH_16)
		/* Reconfigure bus width to 16-bit */
		stm32_fmc2_set_buswidth_16(fmc2, true);
}

static void stm32_fmc2_write_data(struct nand_chip *chip, const void *buf,
				  unsigned int len, bool force_8bit)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	void __iomem *io_addr_w = fmc2->data_base[fmc2->cs_sel];

	if (force_8bit && chip->options & NAND_BUSWIDTH_16)
		/* Reconfigure bus width to 8-bit */
		stm32_fmc2_set_buswidth_16(fmc2, false);

	if (!IS_ALIGNED((uintptr_t)buf, sizeof(u32))) {
		if (!IS_ALIGNED((uintptr_t)buf, sizeof(u16)) && len) {
			writeb_relaxed(*(u8 *)buf, io_addr_w);
			buf += sizeof(u8);
			len -= sizeof(u8);
		}

		if (!IS_ALIGNED((uintptr_t)buf, sizeof(u32)) &&
		    len >= sizeof(u16)) {
			writew_relaxed(*(u16 *)buf, io_addr_w);
			buf += sizeof(u16);
			len -= sizeof(u16);
		}
	}

	/* Buf is aligned */
	while (len >= sizeof(u32)) {
		writel_relaxed(*(u32 *)buf, io_addr_w);
		buf += sizeof(u32);
		len -= sizeof(u32);
	}

	/* Write remaining bytes */
	if (len >= sizeof(u16)) {
		writew_relaxed(*(u16 *)buf, io_addr_w);
		buf += sizeof(u16);
		len -= sizeof(u16);
	}

	if (len)
		writeb_relaxed(*(u8 *)buf, io_addr_w);

	if (force_8bit && chip->options & NAND_BUSWIDTH_16)
		/* Reconfigure bus width to 16-bit */
		stm32_fmc2_set_buswidth_16(fmc2, true);
}

static int stm32_fmc2_exec_op(struct nand_chip *chip,
			      const struct nand_operation *op,
			      bool check_only)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id, i;
	int ret;

	ret = stm32_fmc2_select_chip(chip, op->cs);
	if (ret)
		return ret;

	if (check_only)
		return ret;

	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			writeb_relaxed(instr->ctx.cmd.opcode,
				       fmc2->cmd_base[fmc2->cs_sel]);
			break;

		case NAND_OP_ADDR_INSTR:
			for (i = 0; i < instr->ctx.addr.naddrs; i++)
				writeb_relaxed(instr->ctx.addr.addrs[i],
					       fmc2->addr_base[fmc2->cs_sel]);
			break;

		case NAND_OP_DATA_IN_INSTR:
			stm32_fmc2_read_data(chip, instr->ctx.data.buf.in,
					     instr->ctx.data.len,
					     instr->ctx.data.force_8bit);
			break;

		case NAND_OP_DATA_OUT_INSTR:
			stm32_fmc2_write_data(chip, instr->ctx.data.buf.out,
					      instr->ctx.data.len,
					      instr->ctx.data.force_8bit);
			break;

		case NAND_OP_WAITRDY_INSTR:
			ret = nand_soft_waitrdy(chip,
						instr->ctx.waitrdy.timeout_ms);
			break;
		}
	}

	return ret;
}

/* Controller initialization */
static void stm32_fmc2_init(struct stm32_fmc2_nfc *fmc2)
{
	u32 pcr = readl_relaxed(fmc2->io_base + FMC2_PCR);
	u32 bcr1 = readl_relaxed(fmc2->io_base + FMC2_BCR1);

	/* Set CS used to undefined */
	fmc2->cs_sel = -1;

	/* Enable wait feature and nand flash memory bank */
	pcr |= FMC2_PCR_PWAITEN;
	pcr |= FMC2_PCR_PBKEN;

	/* Set buswidth to 8 bits mode for identification */
	pcr &= ~FMC2_PCR_PWID_MASK;

	/* ECC logic is disabled */
	pcr &= ~FMC2_PCR_ECCEN;

	/* Default mode */
	pcr &= ~FMC2_PCR_ECCALG;
	pcr &= ~FMC2_PCR_BCHECC;
	pcr &= ~FMC2_PCR_WEN;

	/* Set default ECC sector size */
	pcr &= ~FMC2_PCR_ECCSS_MASK;
	pcr |= FMC2_PCR_ECCSS(FMC2_PCR_ECCSS_2048);

	/* Set default tclr/tar timings */
	pcr &= ~FMC2_PCR_TCLR_MASK;
	pcr |= FMC2_PCR_TCLR(FMC2_PCR_TCLR_DEFAULT);
	pcr &= ~FMC2_PCR_TAR_MASK;
	pcr |= FMC2_PCR_TAR(FMC2_PCR_TAR_DEFAULT);

	/* Enable FMC2 controller */
	bcr1 |= FMC2_BCR1_FMC2EN;

	writel_relaxed(bcr1, fmc2->io_base + FMC2_BCR1);
	writel_relaxed(pcr, fmc2->io_base + FMC2_PCR);
	writel_relaxed(FMC2_PMEM_DEFAULT, fmc2->io_base + FMC2_PMEM);
	writel_relaxed(FMC2_PATT_DEFAULT, fmc2->io_base + FMC2_PATT);
}

/* Controller timings */
static void stm32_fmc2_calc_timings(struct nand_chip *chip,
				    const struct nand_sdr_timings *sdrt)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	struct stm32_fmc2_nand *nand = to_fmc2_nand(chip);
	struct stm32_fmc2_timings *tims = &nand->timings;
	unsigned long hclk = clk_get_rate(fmc2->clk);
	unsigned long hclkp = NSEC_PER_SEC / (hclk / 1000);
	int tar, tclr, thiz, twait, tset_mem, tset_att, thold_mem, thold_att;

	tar = hclkp;
	if (tar < sdrt->tAR_min)
		tar = sdrt->tAR_min;
	tims->tar = DIV_ROUND_UP(tar, hclkp) - 1;
	if (tims->tar > FMC2_PCR_TIMING_MASK)
		tims->tar = FMC2_PCR_TIMING_MASK;

	tclr = hclkp;
	if (tclr < sdrt->tCLR_min)
		tclr = sdrt->tCLR_min;
	tims->tclr = DIV_ROUND_UP(tclr, hclkp) - 1;
	if (tims->tclr > FMC2_PCR_TIMING_MASK)
		tims->tclr = FMC2_PCR_TIMING_MASK;

	tims->thiz = FMC2_THIZ;
	thiz = (tims->thiz + 1) * hclkp;

	/*
	 * tWAIT > tRP
	 * tWAIT > tWP
	 * tWAIT > tREA + tIO
	 */
	twait = hclkp;
	if (twait < sdrt->tRP_min)
		twait = sdrt->tRP_min;
	if (twait < sdrt->tWP_min)
		twait = sdrt->tWP_min;
	if (twait < sdrt->tREA_max + FMC2_TIO)
		twait = sdrt->tREA_max + FMC2_TIO;
	tims->twait = DIV_ROUND_UP(twait, hclkp);
	if (tims->twait == 0)
		tims->twait = 1;
	else if (tims->twait > FMC2_PMEM_PATT_TIMING_MASK)
		tims->twait = FMC2_PMEM_PATT_TIMING_MASK;

	/*
	 * tSETUP_MEM > tCS - tWAIT
	 * tSETUP_MEM > tALS - tWAIT
	 * tSETUP_MEM > tDS - (tWAIT - tHIZ)
	 */
	tset_mem = hclkp;
	if (sdrt->tCS_min > twait && (tset_mem < sdrt->tCS_min - twait))
		tset_mem = sdrt->tCS_min - twait;
	if (sdrt->tALS_min > twait && (tset_mem < sdrt->tALS_min - twait))
		tset_mem = sdrt->tALS_min - twait;
	if (twait > thiz && (sdrt->tDS_min > twait - thiz) &&
	    (tset_mem < sdrt->tDS_min - (twait - thiz)))
		tset_mem = sdrt->tDS_min - (twait - thiz);
	tims->tset_mem = DIV_ROUND_UP(tset_mem, hclkp);
	if (tims->tset_mem == 0)
		tims->tset_mem = 1;
	else if (tims->tset_mem > FMC2_PMEM_PATT_TIMING_MASK)
		tims->tset_mem = FMC2_PMEM_PATT_TIMING_MASK;

	/*
	 * tHOLD_MEM > tCH
	 * tHOLD_MEM > tREH - tSETUP_MEM
	 * tHOLD_MEM > max(tRC, tWC) - (tSETUP_MEM + tWAIT)
	 */
	thold_mem = hclkp;
	if (thold_mem < sdrt->tCH_min)
		thold_mem = sdrt->tCH_min;
	if (sdrt->tREH_min > tset_mem &&
	    (thold_mem < sdrt->tREH_min - tset_mem))
		thold_mem = sdrt->tREH_min - tset_mem;
	if ((sdrt->tRC_min > tset_mem + twait) &&
	    (thold_mem < sdrt->tRC_min - (tset_mem + twait)))
		thold_mem = sdrt->tRC_min - (tset_mem + twait);
	if ((sdrt->tWC_min > tset_mem + twait) &&
	    (thold_mem < sdrt->tWC_min - (tset_mem + twait)))
		thold_mem = sdrt->tWC_min - (tset_mem + twait);
	tims->thold_mem = DIV_ROUND_UP(thold_mem, hclkp);
	if (tims->thold_mem == 0)
		tims->thold_mem = 1;
	else if (tims->thold_mem > FMC2_PMEM_PATT_TIMING_MASK)
		tims->thold_mem = FMC2_PMEM_PATT_TIMING_MASK;

	/*
	 * tSETUP_ATT > tCS - tWAIT
	 * tSETUP_ATT > tCLS - tWAIT
	 * tSETUP_ATT > tALS - tWAIT
	 * tSETUP_ATT > tRHW - tHOLD_MEM
	 * tSETUP_ATT > tDS - (tWAIT - tHIZ)
	 */
	tset_att = hclkp;
	if (sdrt->tCS_min > twait && (tset_att < sdrt->tCS_min - twait))
		tset_att = sdrt->tCS_min - twait;
	if (sdrt->tCLS_min > twait && (tset_att < sdrt->tCLS_min - twait))
		tset_att = sdrt->tCLS_min - twait;
	if (sdrt->tALS_min > twait && (tset_att < sdrt->tALS_min - twait))
		tset_att = sdrt->tALS_min - twait;
	if (sdrt->tRHW_min > thold_mem &&
	    (tset_att < sdrt->tRHW_min - thold_mem))
		tset_att = sdrt->tRHW_min - thold_mem;
	if (twait > thiz && (sdrt->tDS_min > twait - thiz) &&
	    (tset_att < sdrt->tDS_min - (twait - thiz)))
		tset_att = sdrt->tDS_min - (twait - thiz);
	tims->tset_att = DIV_ROUND_UP(tset_att, hclkp);
	if (tims->tset_att == 0)
		tims->tset_att = 1;
	else if (tims->tset_att > FMC2_PMEM_PATT_TIMING_MASK)
		tims->tset_att = FMC2_PMEM_PATT_TIMING_MASK;

	/*
	 * tHOLD_ATT > tALH
	 * tHOLD_ATT > tCH
	 * tHOLD_ATT > tCLH
	 * tHOLD_ATT > tCOH
	 * tHOLD_ATT > tDH
	 * tHOLD_ATT > tWB + tIO + tSYNC - tSETUP_MEM
	 * tHOLD_ATT > tADL - tSETUP_MEM
	 * tHOLD_ATT > tWH - tSETUP_MEM
	 * tHOLD_ATT > tWHR - tSETUP_MEM
	 * tHOLD_ATT > tRC - (tSETUP_ATT + tWAIT)
	 * tHOLD_ATT > tWC - (tSETUP_ATT + tWAIT)
	 */
	thold_att = hclkp;
	if (thold_att < sdrt->tALH_min)
		thold_att = sdrt->tALH_min;
	if (thold_att < sdrt->tCH_min)
		thold_att = sdrt->tCH_min;
	if (thold_att < sdrt->tCLH_min)
		thold_att = sdrt->tCLH_min;
	if (thold_att < sdrt->tCOH_min)
		thold_att = sdrt->tCOH_min;
	if (thold_att < sdrt->tDH_min)
		thold_att = sdrt->tDH_min;
	if ((sdrt->tWB_max + FMC2_TIO + FMC2_TSYNC > tset_mem) &&
	    (thold_att < sdrt->tWB_max + FMC2_TIO + FMC2_TSYNC - tset_mem))
		thold_att = sdrt->tWB_max + FMC2_TIO + FMC2_TSYNC - tset_mem;
	if (sdrt->tADL_min > tset_mem &&
	    (thold_att < sdrt->tADL_min - tset_mem))
		thold_att = sdrt->tADL_min - tset_mem;
	if (sdrt->tWH_min > tset_mem &&
	    (thold_att < sdrt->tWH_min - tset_mem))
		thold_att = sdrt->tWH_min - tset_mem;
	if (sdrt->tWHR_min > tset_mem &&
	    (thold_att < sdrt->tWHR_min - tset_mem))
		thold_att = sdrt->tWHR_min - tset_mem;
	if ((sdrt->tRC_min > tset_att + twait) &&
	    (thold_att < sdrt->tRC_min - (tset_att + twait)))
		thold_att = sdrt->tRC_min - (tset_att + twait);
	if ((sdrt->tWC_min > tset_att + twait) &&
	    (thold_att < sdrt->tWC_min - (tset_att + twait)))
		thold_att = sdrt->tWC_min - (tset_att + twait);
	tims->thold_att = DIV_ROUND_UP(thold_att, hclkp);
	if (tims->thold_att == 0)
		tims->thold_att = 1;
	else if (tims->thold_att > FMC2_PMEM_PATT_TIMING_MASK)
		tims->thold_att = FMC2_PMEM_PATT_TIMING_MASK;
}

static int stm32_fmc2_setup_interface(struct nand_chip *chip, int chipnr,
				      const struct nand_data_interface *conf)
{
	const struct nand_sdr_timings *sdrt;

	sdrt = nand_get_sdr_timings(conf);
	if (IS_ERR(sdrt))
		return PTR_ERR(sdrt);

	if (chipnr == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	stm32_fmc2_calc_timings(chip, sdrt);

	/* Apply timings */
	stm32_fmc2_timings_init(chip);

	return 0;
}

/* DMA configuration */
static int stm32_fmc2_dma_setup(struct stm32_fmc2_nfc *fmc2)
{
	int ret;

	fmc2->dma_tx_ch = dma_request_slave_channel(fmc2->dev, "tx");
	fmc2->dma_rx_ch = dma_request_slave_channel(fmc2->dev, "rx");
	fmc2->dma_ecc_ch = dma_request_slave_channel(fmc2->dev, "ecc");

	if (!fmc2->dma_tx_ch || !fmc2->dma_rx_ch || !fmc2->dma_ecc_ch) {
		dev_warn(fmc2->dev, "DMAs not defined in the device tree, polling mode is used\n");
		return 0;
	}

	ret = sg_alloc_table(&fmc2->dma_ecc_sg, FMC2_MAX_SG, GFP_KERNEL);
	if (ret)
		return ret;

	/* Allocate a buffer to store ECC status registers */
	fmc2->ecc_buf = devm_kzalloc(fmc2->dev, FMC2_MAX_ECC_BUF_LEN,
				     GFP_KERNEL);
	if (!fmc2->ecc_buf)
		return -ENOMEM;

	ret = sg_alloc_table(&fmc2->dma_data_sg, FMC2_MAX_SG, GFP_KERNEL);
	if (ret)
		return ret;

	init_completion(&fmc2->dma_data_complete);
	init_completion(&fmc2->dma_ecc_complete);

	return 0;
}

/* NAND callbacks setup */
static void stm32_fmc2_nand_callbacks_setup(struct nand_chip *chip)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);

	/*
	 * Specific callbacks to read/write a page depending on
	 * the mode (polling/sequencer) and the algo used (Hamming, BCH).
	 */
	if (fmc2->dma_tx_ch && fmc2->dma_rx_ch && fmc2->dma_ecc_ch) {
		/* DMA => use sequencer mode callbacks */
		chip->ecc.correct = stm32_fmc2_sequencer_correct;
		chip->ecc.write_page = stm32_fmc2_sequencer_write_page;
		chip->ecc.read_page = stm32_fmc2_sequencer_read_page;
		chip->ecc.write_page_raw = stm32_fmc2_sequencer_write_page_raw;
		chip->ecc.read_page_raw = stm32_fmc2_sequencer_read_page_raw;
	} else {
		/* No DMA => use polling mode callbacks */
		chip->ecc.hwctl = stm32_fmc2_hwctl;
		if (chip->ecc.strength == FMC2_ECC_HAM) {
			/* Hamming is used */
			chip->ecc.calculate = stm32_fmc2_ham_calculate;
			chip->ecc.correct = stm32_fmc2_ham_correct;
			chip->ecc.options |= NAND_ECC_GENERIC_ERASED_CHECK;
		} else {
			/* BCH is used */
			chip->ecc.calculate = stm32_fmc2_bch_calculate;
			chip->ecc.correct = stm32_fmc2_bch_correct;
			chip->ecc.read_page = stm32_fmc2_read_page;
		}
	}

	/* Specific configurations depending on the algo used */
	if (chip->ecc.strength == FMC2_ECC_HAM)
		chip->ecc.bytes = chip->options & NAND_BUSWIDTH_16 ? 4 : 3;
	else if (chip->ecc.strength == FMC2_ECC_BCH8)
		chip->ecc.bytes = chip->options & NAND_BUSWIDTH_16 ? 14 : 13;
	else
		chip->ecc.bytes = chip->options & NAND_BUSWIDTH_16 ? 8 : 7;
}

/* FMC2 layout */
static int stm32_fmc2_nand_ooblayout_ecc(struct mtd_info *mtd, int section,
					 struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section)
		return -ERANGE;

	oobregion->length = ecc->total;
	oobregion->offset = FMC2_BBM_LEN;

	return 0;
}

static int stm32_fmc2_nand_ooblayout_free(struct mtd_info *mtd, int section,
					  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section)
		return -ERANGE;

	oobregion->length = mtd->oobsize - ecc->total - FMC2_BBM_LEN;
	oobregion->offset = ecc->total + FMC2_BBM_LEN;

	return 0;
}

static const struct mtd_ooblayout_ops stm32_fmc2_nand_ooblayout_ops = {
	.ecc = stm32_fmc2_nand_ooblayout_ecc,
	.free = stm32_fmc2_nand_ooblayout_free,
};

/* FMC2 caps */
static int stm32_fmc2_calc_ecc_bytes(int step_size, int strength)
{
	/* Hamming */
	if (strength == FMC2_ECC_HAM)
		return 4;

	/* BCH8 */
	if (strength == FMC2_ECC_BCH8)
		return 14;

	/* BCH4 */
	return 8;
}

NAND_ECC_CAPS_SINGLE(stm32_fmc2_ecc_caps, stm32_fmc2_calc_ecc_bytes,
		     FMC2_ECC_STEP_SIZE,
		     FMC2_ECC_HAM, FMC2_ECC_BCH4, FMC2_ECC_BCH8);

/* FMC2 controller ops */
static int stm32_fmc2_attach_chip(struct nand_chip *chip)
{
	struct stm32_fmc2_nfc *fmc2 = to_stm32_nfc(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	/*
	 * Only NAND_ECC_HW mode is actually supported
	 * Hamming => ecc.strength = 1
	 * BCH4 => ecc.strength = 4
	 * BCH8 => ecc.strength = 8
	 * ECC sector size = 512
	 */
	if (chip->ecc.mode != NAND_ECC_HW) {
		dev_err(fmc2->dev, "nand_ecc_mode is not well defined in the DT\n");
		return -EINVAL;
	}

	ret = nand_ecc_choose_conf(chip, &stm32_fmc2_ecc_caps,
				   mtd->oobsize - FMC2_BBM_LEN);
	if (ret) {
		dev_err(fmc2->dev, "no valid ECC settings set\n");
		return ret;
	}

	if (mtd->writesize / chip->ecc.size > FMC2_MAX_SG) {
		dev_err(fmc2->dev, "nand page size is not supported\n");
		return -EINVAL;
	}

	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	/* NAND callbacks setup */
	stm32_fmc2_nand_callbacks_setup(chip);

	/* Define ECC layout */
	mtd_set_ooblayout(mtd, &stm32_fmc2_nand_ooblayout_ops);

	/* Configure bus width to 16-bit */
	if (chip->options & NAND_BUSWIDTH_16)
		stm32_fmc2_set_buswidth_16(fmc2, true);

	return 0;
}

static const struct nand_controller_ops stm32_fmc2_nand_controller_ops = {
	.attach_chip = stm32_fmc2_attach_chip,
	.exec_op = stm32_fmc2_exec_op,
	.setup_data_interface = stm32_fmc2_setup_interface,
};

/* FMC2 probe */
static int stm32_fmc2_parse_child(struct stm32_fmc2_nfc *fmc2,
				  struct device_node *dn)
{
	struct stm32_fmc2_nand *nand = &fmc2->nand;
	u32 cs;
	int ret, i;

	if (!of_get_property(dn, "reg", &nand->ncs))
		return -EINVAL;

	nand->ncs /= sizeof(u32);
	if (!nand->ncs) {
		dev_err(fmc2->dev, "invalid reg property size\n");
		return -EINVAL;
	}

	for (i = 0; i < nand->ncs; i++) {
		ret = of_property_read_u32_index(dn, "reg", i, &cs);
		if (ret) {
			dev_err(fmc2->dev, "could not retrieve reg property: %d\n",
				ret);
			return ret;
		}

		if (cs > FMC2_MAX_CE) {
			dev_err(fmc2->dev, "invalid reg value: %d\n", cs);
			return -EINVAL;
		}

		if (fmc2->cs_assigned & BIT(cs)) {
			dev_err(fmc2->dev, "cs already assigned: %d\n", cs);
			return -EINVAL;
		}

		fmc2->cs_assigned |= BIT(cs);
		nand->cs_used[i] = cs;
	}

	nand_set_flash_node(&nand->chip, dn);

	return 0;
}

static int stm32_fmc2_parse_dt(struct stm32_fmc2_nfc *fmc2)
{
	struct device_node *dn = fmc2->dev->of_node;
	struct device_node *child;
	int nchips = of_get_child_count(dn);
	int ret = 0;

	if (!nchips) {
		dev_err(fmc2->dev, "NAND chip not defined\n");
		return -EINVAL;
	}

	if (nchips > 1) {
		dev_err(fmc2->dev, "too many NAND chips defined\n");
		return -EINVAL;
	}

	for_each_child_of_node(dn, child) {
		ret = stm32_fmc2_parse_child(fmc2, child);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}
	}

	return ret;
}

static int stm32_fmc2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	struct stm32_fmc2_nfc *fmc2;
	struct stm32_fmc2_nand *nand;
	struct resource *res;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	int chip_cs, mem_region, ret, irq;

	fmc2 = devm_kzalloc(dev, sizeof(*fmc2), GFP_KERNEL);
	if (!fmc2)
		return -ENOMEM;

	fmc2->dev = dev;
	nand_controller_init(&fmc2->base);
	fmc2->base.ops = &stm32_fmc2_nand_controller_ops;

	ret = stm32_fmc2_parse_dt(fmc2);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fmc2->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(fmc2->io_base))
		return PTR_ERR(fmc2->io_base);

	fmc2->io_phys_addr = res->start;

	for (chip_cs = 0, mem_region = 1; chip_cs < FMC2_MAX_CE;
	     chip_cs++, mem_region += 3) {
		if (!(fmc2->cs_assigned & BIT(chip_cs)))
			continue;

		res = platform_get_resource(pdev, IORESOURCE_MEM, mem_region);
		fmc2->data_base[chip_cs] = devm_ioremap_resource(dev, res);
		if (IS_ERR(fmc2->data_base[chip_cs]))
			return PTR_ERR(fmc2->data_base[chip_cs]);

		fmc2->data_phys_addr[chip_cs] = res->start;

		res = platform_get_resource(pdev, IORESOURCE_MEM,
					    mem_region + 1);
		fmc2->cmd_base[chip_cs] = devm_ioremap_resource(dev, res);
		if (IS_ERR(fmc2->cmd_base[chip_cs]))
			return PTR_ERR(fmc2->cmd_base[chip_cs]);

		res = platform_get_resource(pdev, IORESOURCE_MEM,
					    mem_region + 2);
		fmc2->addr_base[chip_cs] = devm_ioremap_resource(dev, res);
		if (IS_ERR(fmc2->addr_base[chip_cs]))
			return PTR_ERR(fmc2->addr_base[chip_cs]);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		if (irq != -EPROBE_DEFER)
			dev_err(dev, "IRQ error missing or invalid\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, stm32_fmc2_irq, 0,
			       dev_name(dev), fmc2);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	init_completion(&fmc2->complete);

	fmc2->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(fmc2->clk))
		return PTR_ERR(fmc2->clk);

	ret = clk_prepare_enable(fmc2->clk);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		return ret;
	}

	rstc = devm_reset_control_get(dev, NULL);
	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		reset_control_deassert(rstc);
	}

	/* DMA setup */
	ret = stm32_fmc2_dma_setup(fmc2);
	if (ret)
		return ret;

	/* FMC2 init routine */
	stm32_fmc2_init(fmc2);

	nand = &fmc2->nand;
	chip = &nand->chip;
	mtd = nand_to_mtd(chip);
	mtd->dev.parent = dev;

	chip->controller = &fmc2->base;
	chip->options |= NAND_BUSWIDTH_AUTO | NAND_NO_SUBPAGE_WRITE |
			 NAND_USE_BOUNCE_BUFFER;

	/* Default ECC settings */
	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.size = FMC2_ECC_STEP_SIZE;
	chip->ecc.strength = FMC2_ECC_BCH8;

	/* Scan to find existence of the device */
	ret = nand_scan(chip, nand->ncs);
	if (ret)
		goto err_scan;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret)
		goto err_device_register;

	platform_set_drvdata(pdev, fmc2);

	return 0;

err_device_register:
	nand_cleanup(chip);

err_scan:
	if (fmc2->dma_ecc_ch)
		dma_release_channel(fmc2->dma_ecc_ch);
	if (fmc2->dma_tx_ch)
		dma_release_channel(fmc2->dma_tx_ch);
	if (fmc2->dma_rx_ch)
		dma_release_channel(fmc2->dma_rx_ch);

	sg_free_table(&fmc2->dma_data_sg);
	sg_free_table(&fmc2->dma_ecc_sg);

	clk_disable_unprepare(fmc2->clk);

	return ret;
}

static int stm32_fmc2_remove(struct platform_device *pdev)
{
	struct stm32_fmc2_nfc *fmc2 = platform_get_drvdata(pdev);
	struct stm32_fmc2_nand *nand = &fmc2->nand;

	nand_release(&nand->chip);

	if (fmc2->dma_ecc_ch)
		dma_release_channel(fmc2->dma_ecc_ch);
	if (fmc2->dma_tx_ch)
		dma_release_channel(fmc2->dma_tx_ch);
	if (fmc2->dma_rx_ch)
		dma_release_channel(fmc2->dma_rx_ch);

	sg_free_table(&fmc2->dma_data_sg);
	sg_free_table(&fmc2->dma_ecc_sg);

	clk_disable_unprepare(fmc2->clk);

	return 0;
}

static int __maybe_unused stm32_fmc2_suspend(struct device *dev)
{
	struct stm32_fmc2_nfc *fmc2 = dev_get_drvdata(dev);

	clk_disable_unprepare(fmc2->clk);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused stm32_fmc2_resume(struct device *dev)
{
	struct stm32_fmc2_nfc *fmc2 = dev_get_drvdata(dev);
	struct stm32_fmc2_nand *nand = &fmc2->nand;
	int chip_cs, ret;

	pinctrl_pm_select_default_state(dev);

	ret = clk_prepare_enable(fmc2->clk);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		return ret;
	}

	stm32_fmc2_init(fmc2);

	for (chip_cs = 0; chip_cs < FMC2_MAX_CE; chip_cs++) {
		if (!(fmc2->cs_assigned & BIT(chip_cs)))
			continue;

		nand_reset(&nand->chip, chip_cs);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(stm32_fmc2_pm_ops, stm32_fmc2_suspend,
			 stm32_fmc2_resume);

static const struct of_device_id stm32_fmc2_match[] = {
	{.compatible = "st,stm32mp15-fmc2"},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_fmc2_match);

static struct platform_driver stm32_fmc2_driver = {
	.probe	= stm32_fmc2_probe,
	.remove	= stm32_fmc2_remove,
	.driver	= {
		.name = "stm32_fmc2_nand",
		.of_match_table = stm32_fmc2_match,
		.pm = &stm32_fmc2_pm_ops,
	},
};
module_platform_driver(stm32_fmc2_driver);

MODULE_ALIAS("platform:stm32_fmc2_nand");
MODULE_AUTHOR("Christophe Kerello <christophe.kerello@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 FMC2 nand driver");
MODULE_LICENSE("GPL v2");
