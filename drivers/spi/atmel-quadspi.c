// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Atmel QSPI Controller
 *
 * Copyright (C) 2015 Atmel Corporation
 * Copyright (C) 2018 Cryptera A/S
 *
 * Author: Cyrille Pitchen <cyrille.pitchen@atmel.com>
 * Author: Piotr Bugalski <bugalski.piotr@gmail.com>
 *
 * This driver is based on drivers/mtd/spi-nor/fsl-quadspi.c from Freescale.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi-mem.h>

/* QSPI register offsets */
#define QSPI_CR      0x0000  /* Control Register */
#define QSPI_MR      0x0004  /* Mode Register */
#define QSPI_RD      0x0008  /* Receive Data Register */
#define QSPI_TD      0x000c  /* Transmit Data Register */
#define QSPI_SR      0x0010  /* Status Register */
#define QSPI_IER     0x0014  /* Interrupt Enable Register */
#define QSPI_IDR     0x0018  /* Interrupt Disable Register */
#define QSPI_IMR     0x001c  /* Interrupt Mask Register */
#define QSPI_SCR     0x0020  /* Serial Clock Register */
#define QSPI_SR2     0x0024  /* SAMA7G5 Status Register */

#define QSPI_IAR     0x0030  /* Instruction Address Register */
#define QSPI_ICR     0x0034  /* Instruction Code Register */
#define QSPI_WICR    0x0034  /* Write Instruction Code Register */
#define QSPI_IFR     0x0038  /* Instruction Frame Register */
#define QSPI_RICR    0x003C  /* Read Instruction Code Register */

#define QSPI_SMR     0x0040  /* Scrambling Mode Register */
#define QSPI_SKR     0x0044  /* Scrambling Key Register */

#define QSPI_REFRESH	0x0050	/* Refresh Register */
#define QSPI_WRACNT	0x0054	/* Write Access Counter Register */
#define QSPI_DLLCFG	0x0058	/* DLL Configuration Register */
#define QSPI_PCALCFG	0x005C	/* Pad Calibration Configuration Register */
#define QSPI_PCALBP	0x0060	/* Pad Calibration Bypass Register */
#define QSPI_TOUT	0x0064	/* Timeout Register */

#define QSPI_WPMR    0x00E4  /* Write Protection Mode Register */
#define QSPI_WPSR    0x00E8  /* Write Protection Status Register */

#define QSPI_VERSION 0x00FC  /* Version Register */

#define SAMA7G5_QSPI0_MAX_SPEED_HZ	200000000
#define SAMA7G5_QSPI1_SDR_MAX_SPEED_HZ	133000000
#define SAM9X7_QSPI_MAX_SPEED_HZ	100000000

/* Bitfields in QSPI_CR (Control Register) */
#define QSPI_CR_QSPIEN                  BIT(0)
#define QSPI_CR_QSPIDIS                 BIT(1)
#define QSPI_CR_DLLON			BIT(2)
#define QSPI_CR_DLLOFF			BIT(3)
#define QSPI_CR_STPCAL			BIT(4)
#define QSPI_CR_SRFRSH			BIT(5)
#define QSPI_CR_SWRST                   BIT(7)
#define QSPI_CR_UPDCFG			BIT(8)
#define QSPI_CR_STTFR			BIT(9)
#define QSPI_CR_RTOUT			BIT(10)
#define QSPI_CR_LASTXFER                BIT(24)

/* Bitfields in QSPI_MR (Mode Register) */
#define QSPI_MR_SMM                     BIT(0)
#define QSPI_MR_LLB                     BIT(1)
#define QSPI_MR_WDRBT                   BIT(2)
#define QSPI_MR_SMRM                    BIT(3)
#define QSPI_MR_DQSDLYEN		BIT(3)
#define QSPI_MR_CSMODE_MASK             GENMASK(5, 4)
#define QSPI_MR_CSMODE_NOT_RELOADED     (0 << 4)
#define QSPI_MR_CSMODE_LASTXFER         (1 << 4)
#define QSPI_MR_CSMODE_SYSTEMATICALLY   (2 << 4)
#define QSPI_MR_NBBITS_MASK             GENMASK(11, 8)
#define QSPI_MR_NBBITS(n)               ((((n) - 8) << 8) & QSPI_MR_NBBITS_MASK)
#define QSPI_MR_OENSD			BIT(15)
#define QSPI_MR_DLYBCT_MASK             GENMASK(23, 16)
#define QSPI_MR_DLYBCT(n)               (((n) << 16) & QSPI_MR_DLYBCT_MASK)
#define QSPI_MR_DLYCS_MASK              GENMASK(31, 24)
#define QSPI_MR_DLYCS(n)                (((n) << 24) & QSPI_MR_DLYCS_MASK)

/* Bitfields in QSPI_SR/QSPI_IER/QSPI_IDR/QSPI_IMR  */
#define QSPI_SR_RDRF                    BIT(0)
#define QSPI_SR_TDRE                    BIT(1)
#define QSPI_SR_TXEMPTY                 BIT(2)
#define QSPI_SR_OVRES                   BIT(3)
#define QSPI_SR_CSR                     BIT(8)
#define QSPI_SR_CSS                     BIT(9)
#define QSPI_SR_INSTRE                  BIT(10)
#define QSPI_SR_LWRA			BIT(11)
#define QSPI_SR_QITF			BIT(12)
#define QSPI_SR_QITR			BIT(13)
#define QSPI_SR_CSFA			BIT(14)
#define QSPI_SR_CSRA			BIT(15)
#define QSPI_SR_RFRSHD			BIT(16)
#define QSPI_SR_TOUT			BIT(17)
#define QSPI_SR_QSPIENS                 BIT(24)

#define QSPI_SR_CMD_COMPLETED	(QSPI_SR_INSTRE | QSPI_SR_CSR)

/* Bitfields in QSPI_SCR (Serial Clock Register) */
#define QSPI_SCR_CPOL                   BIT(0)
#define QSPI_SCR_CPHA                   BIT(1)
#define QSPI_SCR_SCBR_MASK              GENMASK(15, 8)
#define QSPI_SCR_SCBR(n)                (((n) << 8) & QSPI_SCR_SCBR_MASK)
#define QSPI_SCR_DLYBS_MASK             GENMASK(23, 16)
#define QSPI_SCR_DLYBS(n)               (((n) << 16) & QSPI_SCR_DLYBS_MASK)

/* Bitfields in QSPI_SR2 (SAMA7G5 Status Register) */
#define QSPI_SR2_SYNCBSY		BIT(0)
#define QSPI_SR2_QSPIENS		BIT(1)
#define QSPI_SR2_CSS			BIT(2)
#define QSPI_SR2_RBUSY			BIT(3)
#define QSPI_SR2_HIDLE			BIT(4)
#define QSPI_SR2_DLOCK			BIT(5)
#define QSPI_SR2_CALBSY			BIT(6)

/* Bitfields in QSPI_IAR (Instruction Address Register) */
#define QSPI_IAR_ADDR			GENMASK(31, 0)

/* Bitfields in QSPI_ICR (Read/Write Instruction Code Register) */
#define QSPI_ICR_INST_MASK              GENMASK(7, 0)
#define QSPI_ICR_INST(inst)             (((inst) << 0) & QSPI_ICR_INST_MASK)
#define QSPI_ICR_INST_MASK_SAMA7G5	GENMASK(15, 0)
#define QSPI_ICR_OPT_MASK               GENMASK(23, 16)
#define QSPI_ICR_OPT(opt)               (((opt) << 16) & QSPI_ICR_OPT_MASK)

/* Bitfields in QSPI_IFR (Instruction Frame Register) */
#define QSPI_IFR_WIDTH_MASK             GENMASK(2, 0)
#define QSPI_IFR_WIDTH_SINGLE_BIT_SPI   (0 << 0)
#define QSPI_IFR_WIDTH_DUAL_OUTPUT      (1 << 0)
#define QSPI_IFR_WIDTH_QUAD_OUTPUT      (2 << 0)
#define QSPI_IFR_WIDTH_DUAL_IO          (3 << 0)
#define QSPI_IFR_WIDTH_QUAD_IO          (4 << 0)
#define QSPI_IFR_WIDTH_DUAL_CMD         (5 << 0)
#define QSPI_IFR_WIDTH_QUAD_CMD         (6 << 0)
#define QSPI_IFR_WIDTH_OCT_OUTPUT	(7 << 0)
#define QSPI_IFR_WIDTH_OCT_IO		(8 << 0)
#define QSPI_IFR_WIDTH_OCT_CMD		(9 << 0)
#define QSPI_IFR_INSTEN                 BIT(4)
#define QSPI_IFR_ADDREN                 BIT(5)
#define QSPI_IFR_OPTEN                  BIT(6)
#define QSPI_IFR_DATAEN                 BIT(7)
#define QSPI_IFR_OPTL_MASK              GENMASK(9, 8)
#define QSPI_IFR_OPTL_1BIT              (0 << 8)
#define QSPI_IFR_OPTL_2BIT              (1 << 8)
#define QSPI_IFR_OPTL_4BIT              (2 << 8)
#define QSPI_IFR_OPTL_8BIT              (3 << 8)
#define QSPI_IFR_ADDRL                  BIT(10)
#define QSPI_IFR_ADDRL_SAMA7G5		GENMASK(11, 10)
#define QSPI_IFR_TFRTYP_MEM		BIT(12)
#define QSPI_IFR_SAMA5D2_WRITE_TRSFR	BIT(13)
#define QSPI_IFR_CRM                    BIT(14)
#define QSPI_IFR_DDREN			BIT(15)
#define QSPI_IFR_NBDUM_MASK             GENMASK(20, 16)
#define QSPI_IFR_NBDUM(n)               (((n) << 16) & QSPI_IFR_NBDUM_MASK)
#define QSPI_IFR_END			BIT(22)
#define QSPI_IFR_SMRM			BIT(23)
#define QSPI_IFR_APBTFRTYP_READ		BIT(24)	/* Defined in SAM9X60 */
#define QSPI_IFR_DQSEN			BIT(25)
#define QSPI_IFR_DDRCMDEN		BIT(26)
#define QSPI_IFR_HFWBEN			BIT(27)
#define QSPI_IFR_PROTTYP		GENMASK(29, 28)
#define QSPI_IFR_PROTTYP_STD_SPI	0
#define QSPI_IFR_PROTTYP_TWIN_QUAD	1
#define QSPI_IFR_PROTTYP_OCTAFLASH	2
#define QSPI_IFR_PROTTYP_HYPERFLASH	3

/* Bitfields in QSPI_SMR (Scrambling Mode Register) */
#define QSPI_SMR_SCREN                  BIT(0)
#define QSPI_SMR_RVDIS                  BIT(1)
#define QSPI_SMR_SCRKL                  BIT(2)

/* Bitfields in QSPI_REFRESH (Refresh Register) */
#define QSPI_REFRESH_DELAY_COUNTER	GENMASK(31, 0)

/* Bitfields in QSPI_WRACNT (Write Access Counter Register) */
#define QSPI_WRACNT_NBWRA		GENMASK(31, 0)

/* Bitfields in QSPI_DLLCFG (DLL Configuration Register) */
#define QSPI_DLLCFG_RANGE		BIT(0)

/* Bitfields in QSPI_PCALCFG (DLL Pad Calibration Configuration Register) */
#define QSPI_PCALCFG_AAON		BIT(0)
#define QSPI_PCALCFG_DAPCAL		BIT(1)
#define QSPI_PCALCFG_DIFFPM		BIT(2)
#define QSPI_PCALCFG_CLKDIV		GENMASK(6, 4)
#define QSPI_PCALCFG_CALCNT		GENMASK(16, 8)
#define QSPI_PCALCFG_CALP		GENMASK(27, 24)
#define QSPI_PCALCFG_CALN		GENMASK(31, 28)

/* Bitfields in QSPI_PCALBP (DLL Pad Calibration Bypass Register) */
#define QSPI_PCALBP_BPEN		BIT(0)
#define QSPI_PCALBP_CALPBP		GENMASK(11, 8)
#define QSPI_PCALBP_CALNBP		GENMASK(19, 16)

/* Bitfields in QSPI_TOUT (Timeout Register) */
#define QSPI_TOUT_TCNTM			GENMASK(15, 0)

/* Bitfields in QSPI_WPMR (Write Protection Mode Register) */
#define QSPI_WPMR_WPEN                  BIT(0)
#define QSPI_WPMR_WPITEN		BIT(1)
#define QSPI_WPMR_WPCREN		BIT(2)
#define QSPI_WPMR_WPKEY_MASK            GENMASK(31, 8)
#define QSPI_WPMR_WPKEY(wpkey)          (((wpkey) << 8) & QSPI_WPMR_WPKEY_MASK)

/* Bitfields in QSPI_WPSR (Write Protection Status Register) */
#define QSPI_WPSR_WPVS                  BIT(0)
#define QSPI_WPSR_WPVSRC_MASK           GENMASK(15, 8)
#define QSPI_WPSR_WPVSRC(src)           (((src) << 8) & QSPI_WPSR_WPVSRC)

#define ATMEL_QSPI_TIMEOUT		1000	/* ms */
#define ATMEL_QSPI_SYNC_TIMEOUT		300	/* ms */
#define QSPI_DLLCFG_THRESHOLD_FREQ	90000000U
#define QSPI_CALIB_TIME			2000	/* 2 us */

/* Use PIO for small transfers. */
#define ATMEL_QSPI_DMA_MIN_BYTES	16
/**
 * struct atmel_qspi_pcal - Pad Calibration Clock Division
 * @pclk_rate: peripheral clock rate.
 * @pclk_div: calibration clock division. The clock applied to the calibration
 *           cell is divided by pclk_div + 1.
 */
struct atmel_qspi_pcal {
	u32 pclk_rate;
	u8 pclk_div;
};

#define ATMEL_QSPI_PCAL_ARRAY_SIZE	8
static const struct atmel_qspi_pcal pcal[ATMEL_QSPI_PCAL_ARRAY_SIZE] = {
	{25000000, 0},
	{50000000, 1},
	{75000000, 2},
	{100000000, 3},
	{125000000, 4},
	{150000000, 5},
	{175000000, 6},
	{200000000, 7},
};

struct atmel_qspi_caps {
	u32 max_speed_hz;
	bool has_qspick;
	bool has_gclk;
	bool has_ricr;
	bool octal;
	bool has_dma;
	bool has_2xgclk;
	bool has_padcalib;
	bool has_dllon;
};

struct atmel_qspi_ops;

struct atmel_qspi {
	void __iomem		*regs;
	void __iomem		*mem;
	struct clk		*pclk;
	struct clk		*qspick;
	struct clk		*gclk;
	struct platform_device	*pdev;
	const struct atmel_qspi_caps *caps;
	const struct atmel_qspi_ops *ops;
	resource_size_t		mmap_size;
	u32			pending;
	u32			irq_mask;
	u32			mr;
	u32			scr;
	u32			target_max_speed_hz;
	struct completion	cmd_completion;
	struct completion	dma_completion;
	dma_addr_t		mmap_phys_base;
	struct dma_chan		*rx_chan;
	struct dma_chan		*tx_chan;
};

struct atmel_qspi_ops {
	int (*set_cfg)(struct atmel_qspi *aq, const struct spi_mem_op *op,
		       u32 *offset);
	int (*transfer)(struct spi_mem *mem, const struct spi_mem_op *op,
			u32 offset);
};

struct atmel_qspi_mode {
	u8 cmd_buswidth;
	u8 addr_buswidth;
	u8 data_buswidth;
	u32 config;
};

static const struct atmel_qspi_mode atmel_qspi_modes[] = {
	{ 1, 1, 1, QSPI_IFR_WIDTH_SINGLE_BIT_SPI },
	{ 1, 1, 2, QSPI_IFR_WIDTH_DUAL_OUTPUT },
	{ 1, 1, 4, QSPI_IFR_WIDTH_QUAD_OUTPUT },
	{ 1, 2, 2, QSPI_IFR_WIDTH_DUAL_IO },
	{ 1, 4, 4, QSPI_IFR_WIDTH_QUAD_IO },
	{ 2, 2, 2, QSPI_IFR_WIDTH_DUAL_CMD },
	{ 4, 4, 4, QSPI_IFR_WIDTH_QUAD_CMD },
};

static const struct atmel_qspi_mode atmel_qspi_sama7g5_modes[] = {
	{ 1, 1, 1, QSPI_IFR_WIDTH_SINGLE_BIT_SPI },
	{ 1, 1, 2, QSPI_IFR_WIDTH_DUAL_OUTPUT },
	{ 1, 1, 4, QSPI_IFR_WIDTH_QUAD_OUTPUT },
	{ 1, 2, 2, QSPI_IFR_WIDTH_DUAL_IO },
	{ 1, 4, 4, QSPI_IFR_WIDTH_QUAD_IO },
	{ 2, 2, 2, QSPI_IFR_WIDTH_DUAL_CMD },
	{ 4, 4, 4, QSPI_IFR_WIDTH_QUAD_CMD },
	{ 1, 1, 8, QSPI_IFR_WIDTH_OCT_OUTPUT },
	{ 1, 8, 8, QSPI_IFR_WIDTH_OCT_IO },
	{ 8, 8, 8, QSPI_IFR_WIDTH_OCT_CMD },
};

#ifdef VERBOSE_DEBUG
static const char *atmel_qspi_reg_name(u32 offset, char *tmp, size_t sz)
{
	switch (offset) {
	case QSPI_CR:
		return "CR";
	case QSPI_MR:
		return "MR";
	case QSPI_RD:
		return "RD";
	case QSPI_TD:
		return "TD";
	case QSPI_SR:
		return "SR";
	case QSPI_IER:
		return "IER";
	case QSPI_IDR:
		return "IDR";
	case QSPI_IMR:
		return "IMR";
	case QSPI_SCR:
		return "SCR";
	case QSPI_SR2:
		return "SR2";
	case QSPI_IAR:
		return "IAR";
	case QSPI_ICR:
		return "ICR/WICR";
	case QSPI_IFR:
		return "IFR";
	case QSPI_RICR:
		return "RICR";
	case QSPI_SMR:
		return "SMR";
	case QSPI_SKR:
		return "SKR";
	case QSPI_REFRESH:
		return "REFRESH";
	case QSPI_WRACNT:
		return "WRACNT";
	case QSPI_DLLCFG:
		return "DLLCFG";
	case QSPI_PCALCFG:
		return "PCALCFG";
	case QSPI_PCALBP:
		return "PCALBP";
	case QSPI_TOUT:
		return "TOUT";
	case QSPI_WPMR:
		return "WPMR";
	case QSPI_WPSR:
		return "WPSR";
	case QSPI_VERSION:
		return "VERSION";
	default:
		snprintf(tmp, sz, "0x%02x", offset);
		break;
	}

	return tmp;
}
#endif /* VERBOSE_DEBUG */

static u32 atmel_qspi_read(struct atmel_qspi *aq, u32 offset)
{
	u32 value = readl_relaxed(aq->regs + offset);

#ifdef VERBOSE_DEBUG
	char tmp[8];

	dev_vdbg(&aq->pdev->dev, "read 0x%08x from %s\n", value,
		 atmel_qspi_reg_name(offset, tmp, sizeof(tmp)));
#endif /* VERBOSE_DEBUG */

	return value;
}

static void atmel_qspi_write(u32 value, struct atmel_qspi *aq, u32 offset)
{
#ifdef VERBOSE_DEBUG
	char tmp[8];

	dev_vdbg(&aq->pdev->dev, "write 0x%08x into %s\n", value,
		 atmel_qspi_reg_name(offset, tmp, sizeof(tmp)));
#endif /* VERBOSE_DEBUG */

	writel_relaxed(value, aq->regs + offset);
}

static int atmel_qspi_reg_sync(struct atmel_qspi *aq)
{
	u32 val;
	int ret;

	ret = readl_poll_timeout(aq->regs + QSPI_SR2, val,
				 !(val & QSPI_SR2_SYNCBSY), 40,
				 ATMEL_QSPI_SYNC_TIMEOUT);
	return ret;
}

static int atmel_qspi_update_config(struct atmel_qspi *aq)
{
	int ret;

	ret = atmel_qspi_reg_sync(aq);
	if (ret)
		return ret;
	atmel_qspi_write(QSPI_CR_UPDCFG, aq, QSPI_CR);
	return atmel_qspi_reg_sync(aq);
}

static inline bool atmel_qspi_is_compatible(const struct spi_mem_op *op,
					    const struct atmel_qspi_mode *mode)
{
	if (op->cmd.buswidth != mode->cmd_buswidth)
		return false;

	if (op->addr.nbytes && op->addr.buswidth != mode->addr_buswidth)
		return false;

	if (op->data.nbytes && op->data.buswidth != mode->data_buswidth)
		return false;

	return true;
}

static int atmel_qspi_find_mode(const struct spi_mem_op *op)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(atmel_qspi_modes); i++)
		if (atmel_qspi_is_compatible(op, &atmel_qspi_modes[i]))
			return i;

	return -EOPNOTSUPP;
}

static int atmel_qspi_sama7g5_find_mode(const struct spi_mem_op *op)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(atmel_qspi_sama7g5_modes); i++)
		if (atmel_qspi_is_compatible(op, &atmel_qspi_sama7g5_modes[i]))
			return i;

	return -EOPNOTSUPP;
}

static bool atmel_qspi_supports_op(struct spi_mem *mem,
				   const struct spi_mem_op *op)
{
	struct atmel_qspi *aq = spi_controller_get_devdata(mem->spi->controller);
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if (aq->caps->octal) {
		if (atmel_qspi_sama7g5_find_mode(op) < 0)
			return false;
		else
			return true;
	}

	if (atmel_qspi_find_mode(op) < 0)
		return false;

	/* special case not supported by hardware */
	if (op->addr.nbytes == 2 && op->cmd.buswidth != op->addr.buswidth &&
	    op->dummy.nbytes == 0)
		return false;

	return true;
}

/*
 * If the QSPI controller is set in regular SPI mode, set it in
 * Serial Memory Mode (SMM).
 */
static int atmel_qspi_set_serial_memory_mode(struct atmel_qspi *aq)
{
	int ret = 0;

	if (!(aq->mr & QSPI_MR_SMM)) {
		aq->mr |= QSPI_MR_SMM;
		atmel_qspi_write(aq->mr, aq, QSPI_MR);

		if (aq->caps->has_gclk)
			ret = atmel_qspi_update_config(aq);
	}

	return ret;
}

static int atmel_qspi_set_cfg(struct atmel_qspi *aq,
			      const struct spi_mem_op *op, u32 *offset)
{
	u32 iar, icr, ifr;
	u32 dummy_cycles = 0;
	int mode;

	iar = 0;
	icr = QSPI_ICR_INST(op->cmd.opcode);
	ifr = QSPI_IFR_INSTEN;

	mode = atmel_qspi_find_mode(op);
	if (mode < 0)
		return mode;
	ifr |= atmel_qspi_modes[mode].config;

	if (op->dummy.nbytes)
		dummy_cycles = op->dummy.nbytes * 8 / op->dummy.buswidth;

	/*
	 * The controller allows 24 and 32-bit addressing while NAND-flash
	 * requires 16-bit long. Handling 8-bit long addresses is done using
	 * the option field. For the 16-bit addresses, the workaround depends
	 * of the number of requested dummy bits. If there are 8 or more dummy
	 * cycles, the address is shifted and sent with the first dummy byte.
	 * Otherwise opcode is disabled and the first byte of the address
	 * contains the command opcode (works only if the opcode and address
	 * use the same buswidth). The limitation is when the 16-bit address is
	 * used without enough dummy cycles and the opcode is using a different
	 * buswidth than the address.
	 */
	if (op->addr.buswidth) {
		switch (op->addr.nbytes) {
		case 0:
			break;
		case 1:
			ifr |= QSPI_IFR_OPTEN | QSPI_IFR_OPTL_8BIT;
			icr |= QSPI_ICR_OPT(op->addr.val & 0xff);
			break;
		case 2:
			if (dummy_cycles < 8 / op->addr.buswidth) {
				ifr &= ~QSPI_IFR_INSTEN;
				ifr |= QSPI_IFR_ADDREN;
				iar = (op->cmd.opcode << 16) |
					(op->addr.val & 0xffff);
			} else {
				ifr |= QSPI_IFR_ADDREN;
				iar = (op->addr.val << 8) & 0xffffff;
				dummy_cycles -= 8 / op->addr.buswidth;
			}
			break;
		case 3:
			ifr |= QSPI_IFR_ADDREN;
			iar = op->addr.val & 0xffffff;
			break;
		case 4:
			ifr |= QSPI_IFR_ADDREN | QSPI_IFR_ADDRL;
			iar = op->addr.val & 0x7ffffff;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	/* offset of the data access in the QSPI memory space */
	*offset = iar;

	/* Set number of dummy cycles */
	if (dummy_cycles)
		ifr |= QSPI_IFR_NBDUM(dummy_cycles);

	/* Set data enable and data transfer type. */
	if (op->data.nbytes) {
		ifr |= QSPI_IFR_DATAEN;

		if (op->addr.nbytes)
			ifr |= QSPI_IFR_TFRTYP_MEM;
	}

	mode = atmel_qspi_set_serial_memory_mode(aq);
	if (mode < 0)
		return mode;

	/* Clear pending interrupts */
	(void)atmel_qspi_read(aq, QSPI_SR);

	/* Set QSPI Instruction Frame registers. */
	if (op->addr.nbytes && !op->data.nbytes)
		atmel_qspi_write(iar, aq, QSPI_IAR);

	if (aq->caps->has_ricr) {
		if (op->data.dir == SPI_MEM_DATA_IN)
			atmel_qspi_write(icr, aq, QSPI_RICR);
		else
			atmel_qspi_write(icr, aq, QSPI_WICR);
	} else {
		if (op->data.nbytes && op->data.dir == SPI_MEM_DATA_OUT)
			ifr |= QSPI_IFR_SAMA5D2_WRITE_TRSFR;

		atmel_qspi_write(icr, aq, QSPI_ICR);
	}

	atmel_qspi_write(ifr, aq, QSPI_IFR);

	return 0;
}

static int atmel_qspi_wait_for_completion(struct atmel_qspi *aq, u32 irq_mask)
{
	int err = 0;
	u32 sr;

	/* Poll INSTRuction End status */
	sr = atmel_qspi_read(aq, QSPI_SR);
	if ((sr & irq_mask) == irq_mask)
		return 0;

	/* Wait for INSTRuction End interrupt */
	reinit_completion(&aq->cmd_completion);
	aq->pending = sr & irq_mask;
	aq->irq_mask = irq_mask;
	atmel_qspi_write(irq_mask, aq, QSPI_IER);
	if (!wait_for_completion_timeout(&aq->cmd_completion,
					 msecs_to_jiffies(ATMEL_QSPI_TIMEOUT)))
		err = -ETIMEDOUT;
	atmel_qspi_write(irq_mask, aq, QSPI_IDR);

	return err;
}

static int atmel_qspi_transfer(struct spi_mem *mem,
			       const struct spi_mem_op *op, u32 offset)
{
	struct atmel_qspi *aq = spi_controller_get_devdata(mem->spi->controller);

	/* Skip to the final steps if there is no data */
	if (!op->data.nbytes)
		return atmel_qspi_wait_for_completion(aq,
						      QSPI_SR_CMD_COMPLETED);

	/* Dummy read of QSPI_IFR to synchronize APB and AHB accesses */
	(void)atmel_qspi_read(aq, QSPI_IFR);

	/* Send/Receive data */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		memcpy_fromio(op->data.buf.in, aq->mem + offset,
			      op->data.nbytes);

		/* Synchronize AHB and APB accesses again */
		rmb();
	} else {
		memcpy_toio(aq->mem + offset, op->data.buf.out,
			    op->data.nbytes);

		/* Synchronize AHB and APB accesses again */
		wmb();
	}

	/* Release the chip-select */
	atmel_qspi_write(QSPI_CR_LASTXFER, aq, QSPI_CR);

	return atmel_qspi_wait_for_completion(aq, QSPI_SR_CMD_COMPLETED);
}

static int atmel_qspi_sama7g5_set_cfg(struct atmel_qspi *aq,
				      const struct spi_mem_op *op, u32 *offset)
{
	u32 iar, icr, ifr;
	int mode, ret;

	iar = 0;
	icr = FIELD_PREP(QSPI_ICR_INST_MASK_SAMA7G5, op->cmd.opcode);
	ifr = QSPI_IFR_INSTEN;

	mode = atmel_qspi_sama7g5_find_mode(op);
	if (mode < 0)
		return mode;
	ifr |= atmel_qspi_sama7g5_modes[mode].config;

	if (op->dummy.buswidth && op->dummy.nbytes) {
		if (op->addr.dtr && op->dummy.dtr && op->data.dtr)
			ifr |= QSPI_IFR_NBDUM(op->dummy.nbytes * 8 /
					      (2 * op->dummy.buswidth));
		else
			ifr |= QSPI_IFR_NBDUM(op->dummy.nbytes * 8 /
					      op->dummy.buswidth);
	}

	if (op->addr.buswidth && op->addr.nbytes) {
		ifr |= FIELD_PREP(QSPI_IFR_ADDRL_SAMA7G5, op->addr.nbytes - 1) |
		       QSPI_IFR_ADDREN;
		iar = FIELD_PREP(QSPI_IAR_ADDR, op->addr.val);
	}

	if (op->addr.dtr && op->dummy.dtr && op->data.dtr) {
		ifr |= QSPI_IFR_DDREN;
		if (op->cmd.dtr)
			ifr |= QSPI_IFR_DDRCMDEN;

		ifr |= QSPI_IFR_DQSEN;
	}

	if (op->cmd.buswidth == 8 || op->addr.buswidth == 8 ||
	    op->data.buswidth == 8)
		ifr |= FIELD_PREP(QSPI_IFR_PROTTYP, QSPI_IFR_PROTTYP_OCTAFLASH);

	/* offset of the data access in the QSPI memory space */
	*offset = iar;

	/* Set data enable */
	if (op->data.nbytes) {
		ifr |= QSPI_IFR_DATAEN;

		if (op->addr.nbytes)
			ifr |= QSPI_IFR_TFRTYP_MEM;
	}

	ret = atmel_qspi_set_serial_memory_mode(aq);
	if (ret < 0)
		return ret;

	/* Clear pending interrupts */
	(void)atmel_qspi_read(aq, QSPI_SR);

	/* Set QSPI Instruction Frame registers */
	if (op->addr.nbytes && !op->data.nbytes)
		atmel_qspi_write(iar, aq, QSPI_IAR);

	if (op->data.dir == SPI_MEM_DATA_IN) {
		atmel_qspi_write(icr, aq, QSPI_RICR);
	} else {
		atmel_qspi_write(icr, aq, QSPI_WICR);
		if (op->data.nbytes)
			atmel_qspi_write(FIELD_PREP(QSPI_WRACNT_NBWRA,
						    op->data.nbytes),
					 aq, QSPI_WRACNT);
	}

	atmel_qspi_write(ifr, aq, QSPI_IFR);

	return atmel_qspi_update_config(aq);
}

static void atmel_qspi_dma_callback(void *param)
{
	struct atmel_qspi *aq = param;

	complete(&aq->dma_completion);
}

static int atmel_qspi_dma_xfer(struct atmel_qspi *aq, struct dma_chan *chan,
			       dma_addr_t dma_dst, dma_addr_t dma_src,
			       unsigned int len)
{
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t cookie;
	int ret;

	tx = dmaengine_prep_dma_memcpy(chan, dma_dst, dma_src, len,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx) {
		dev_err(&aq->pdev->dev, "device_prep_dma_memcpy error\n");
		return -EIO;
	}

	reinit_completion(&aq->dma_completion);
	tx->callback = atmel_qspi_dma_callback;
	tx->callback_param = aq;
	cookie = tx->tx_submit(tx);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(&aq->pdev->dev, "dma_submit_error %d\n", cookie);
		return ret;
	}

	dma_async_issue_pending(chan);
	ret = wait_for_completion_timeout(&aq->dma_completion,
					  msecs_to_jiffies(20 * ATMEL_QSPI_TIMEOUT));
	if (ret == 0) {
		dmaengine_terminate_sync(chan);
		dev_err(&aq->pdev->dev, "DMA wait_for_completion_timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int atmel_qspi_dma_rx_xfer(struct spi_mem *mem,
				  const struct spi_mem_op *op,
				  struct sg_table *sgt, loff_t loff)
{
	struct atmel_qspi *aq =
		spi_controller_get_devdata(mem->spi->controller);
	struct scatterlist *sg;
	dma_addr_t dma_src;
	unsigned int i, len;
	int ret;

	dma_src = aq->mmap_phys_base + loff;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		len = sg_dma_len(sg);
		ret = atmel_qspi_dma_xfer(aq, aq->rx_chan, sg_dma_address(sg),
					  dma_src, len);
		if (ret)
			return ret;
		dma_src += len;
	}

	return 0;
}

static int atmel_qspi_dma_tx_xfer(struct spi_mem *mem,
				  const struct spi_mem_op *op,
				  struct sg_table *sgt, loff_t loff)
{
	struct atmel_qspi *aq =
		spi_controller_get_devdata(mem->spi->controller);
	struct scatterlist *sg;
	dma_addr_t dma_dst;
	unsigned int i, len;
	int ret;

	dma_dst = aq->mmap_phys_base + loff;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		len = sg_dma_len(sg);
		ret = atmel_qspi_dma_xfer(aq, aq->tx_chan, dma_dst,
					  sg_dma_address(sg), len);
		if (ret)
			return ret;
		dma_dst += len;
	}

	return 0;
}

static int atmel_qspi_dma_transfer(struct spi_mem *mem,
				   const struct spi_mem_op *op, loff_t loff)
{
	struct sg_table sgt;
	int ret;

	ret = spi_controller_dma_map_mem_op_data(mem->spi->controller, op,
						 &sgt);
	if (ret)
		return ret;

	if (op->data.dir == SPI_MEM_DATA_IN)
		ret = atmel_qspi_dma_rx_xfer(mem, op, &sgt, loff);
	else
		ret = atmel_qspi_dma_tx_xfer(mem, op, &sgt, loff);

	spi_controller_dma_unmap_mem_op_data(mem->spi->controller, op, &sgt);

	return ret;
}

static int atmel_qspi_sama7g5_transfer(struct spi_mem *mem,
				       const struct spi_mem_op *op, u32 offset)
{
	struct atmel_qspi *aq =
		spi_controller_get_devdata(mem->spi->controller);
	u32 val;
	int ret;

	if (!op->data.nbytes) {
		/* Start the transfer. */
		ret = atmel_qspi_reg_sync(aq);
		if (ret)
			return ret;
		atmel_qspi_write(QSPI_CR_STTFR, aq, QSPI_CR);

		return atmel_qspi_wait_for_completion(aq, QSPI_SR_CSRA);
	}

	/* Send/Receive data. */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		if (aq->rx_chan && op->addr.nbytes &&
		    op->data.nbytes > ATMEL_QSPI_DMA_MIN_BYTES) {
			ret = atmel_qspi_dma_transfer(mem, op, offset);
			if (ret)
				return ret;
		} else {
			memcpy_fromio(op->data.buf.in, aq->mem + offset,
				      op->data.nbytes);
		}

		if (op->addr.nbytes) {
			ret = readl_poll_timeout(aq->regs + QSPI_SR2, val,
						 !(val & QSPI_SR2_RBUSY), 40,
						 ATMEL_QSPI_SYNC_TIMEOUT);
			if (ret)
				return ret;
		}
	} else {
		if (aq->tx_chan && op->addr.nbytes &&
		    op->data.nbytes > ATMEL_QSPI_DMA_MIN_BYTES) {
			ret = atmel_qspi_dma_transfer(mem, op, offset);
			if (ret)
				return ret;
		} else {
			memcpy_toio(aq->mem + offset, op->data.buf.out,
				    op->data.nbytes);
		}

		ret = atmel_qspi_wait_for_completion(aq, QSPI_SR_LWRA);
		if (ret)
			return ret;
	}

	/* Release the chip-select. */
	ret = atmel_qspi_reg_sync(aq);
	if (ret)
		return ret;
	atmel_qspi_write(QSPI_CR_LASTXFER, aq, QSPI_CR);

	return atmel_qspi_wait_for_completion(aq, QSPI_SR_CSRA);
}

static int atmel_qspi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct atmel_qspi *aq = spi_controller_get_devdata(mem->spi->controller);
	u32 offset;
	int err;

	/*
	 * Check if the address exceeds the MMIO window size. An improvement
	 * would be to add support for regular SPI mode and fall back to it
	 * when the flash memories overrun the controller's memory space.
	 */
	if (op->addr.val + op->data.nbytes > aq->mmap_size)
		return -EOPNOTSUPP;

	if (op->addr.nbytes > 4)
		return -EOPNOTSUPP;

	err = pm_runtime_resume_and_get(&aq->pdev->dev);
	if (err < 0)
		return err;

	err = aq->ops->set_cfg(aq, op, &offset);
	if (err)
		goto pm_runtime_put;

	err = aq->ops->transfer(mem, op, offset);

pm_runtime_put:
	pm_runtime_put_autosuspend(&aq->pdev->dev);
	return err;
}

static const char *atmel_qspi_get_name(struct spi_mem *spimem)
{
	return dev_name(spimem->spi->dev.parent);
}

static const struct spi_controller_mem_ops atmel_qspi_mem_ops = {
	.supports_op = atmel_qspi_supports_op,
	.exec_op = atmel_qspi_exec_op,
	.get_name = atmel_qspi_get_name
};

static int atmel_qspi_set_pad_calibration(struct atmel_qspi *aq)
{
	unsigned long pclk_rate;
	u32 status, val;
	int i, ret;
	u8 pclk_div = 0;

	pclk_rate = clk_get_rate(aq->pclk);
	if (!pclk_rate)
		return -EINVAL;

	for (i = 0; i < ATMEL_QSPI_PCAL_ARRAY_SIZE; i++) {
		if (pclk_rate <= pcal[i].pclk_rate) {
			pclk_div = pcal[i].pclk_div;
			break;
		}
	}

	/*
	 * Use the biggest divider in case the peripheral clock exceeds
	 * 200MHZ.
	 */
	if (pclk_rate > pcal[ATMEL_QSPI_PCAL_ARRAY_SIZE - 1].pclk_rate)
		pclk_div = pcal[ATMEL_QSPI_PCAL_ARRAY_SIZE - 1].pclk_div;

	/* Disable QSPI while configuring the pad calibration. */
	status = atmel_qspi_read(aq, QSPI_SR2);
	if (status & QSPI_SR2_QSPIENS) {
		ret = atmel_qspi_reg_sync(aq);
		if (ret)
			return ret;
		atmel_qspi_write(QSPI_CR_QSPIDIS, aq, QSPI_CR);
	}

	/*
	 * The analog circuitry is not shut down at the end of the calibration
	 * and the start-up time is only required for the first calibration
	 * sequence, thus increasing performance. Set the delay between the Pad
	 * calibration analog circuitry and the calibration request to 2us.
	 */
	atmel_qspi_write(QSPI_PCALCFG_AAON |
			 FIELD_PREP(QSPI_PCALCFG_CLKDIV, pclk_div) |
			 FIELD_PREP(QSPI_PCALCFG_CALCNT,
				    2 * (pclk_rate / 1000000)),
			 aq, QSPI_PCALCFG);

	/* DLL On + start calibration. */
	if (aq->caps->has_dllon)
		atmel_qspi_write(QSPI_CR_DLLON | QSPI_CR_STPCAL, aq, QSPI_CR);
	/* If there is no DLL support only start calibration. */
	else
		atmel_qspi_write(QSPI_CR_STPCAL, aq, QSPI_CR);

	/*
	 * Check DLL clock lock and synchronization status before updating
	 * configuration.
	 */
	if (aq->caps->has_dllon)
		ret =  readl_poll_timeout(aq->regs + QSPI_SR2, val,
					  (val & QSPI_SR2_DLOCK) &&
					  !(val & QSPI_SR2_CALBSY), 40,
					  ATMEL_QSPI_TIMEOUT);
	else
		ret =  readl_poll_timeout(aq->regs + QSPI_SR2, val,
					  !(val & QSPI_SR2_CALBSY), 40,
					  ATMEL_QSPI_TIMEOUT);

	/* Refresh analogic blocks every 1 ms.*/
	atmel_qspi_write(FIELD_PREP(QSPI_REFRESH_DELAY_COUNTER,
				    aq->target_max_speed_hz / 1000),
			 aq, QSPI_REFRESH);

	return ret;
}

static int atmel_qspi_set_gclk(struct atmel_qspi *aq)
{
	u32 status, val;
	int ret;

	/* Disable DLL before setting GCLK */
	if (aq->caps->has_dllon) {
		status = atmel_qspi_read(aq, QSPI_SR2);
		if (status & QSPI_SR2_DLOCK) {
			atmel_qspi_write(QSPI_CR_DLLOFF, aq, QSPI_CR);
			ret = readl_poll_timeout(aq->regs + QSPI_SR2, val,
						 !(val & QSPI_SR2_DLOCK), 40,
						 ATMEL_QSPI_TIMEOUT);
			if (ret)
				return ret;
		}

		if (aq->target_max_speed_hz > QSPI_DLLCFG_THRESHOLD_FREQ)
			atmel_qspi_write(QSPI_DLLCFG_RANGE, aq, QSPI_DLLCFG);
		else
			atmel_qspi_write(0, aq, QSPI_DLLCFG);
	}

	if (aq->caps->has_2xgclk)
		ret = clk_set_rate(aq->gclk, 2 * aq->target_max_speed_hz);
	else
		ret = clk_set_rate(aq->gclk, aq->target_max_speed_hz);

	if (ret) {
		dev_err(&aq->pdev->dev, "Failed to set generic clock rate.\n");
		return ret;
	}

	/* Enable the QSPI generic clock */
	ret = clk_prepare_enable(aq->gclk);
	if (ret)
		dev_err(&aq->pdev->dev, "Failed to enable generic clock.\n");

	return ret;
}

static int atmel_qspi_sama7g5_init(struct atmel_qspi *aq)
{
	u32 val;
	int ret;

	ret = atmel_qspi_set_gclk(aq);
	if (ret)
		return ret;

	/*
	 * Check if the SoC supports pad calibration in Octal SPI mode.
	 * Proceed only if both the capabilities are true.
	 */
	if (aq->caps->octal && aq->caps->has_padcalib) {
		ret = atmel_qspi_set_pad_calibration(aq);
		if (ret)
			return ret;
	/* Start DLL on only if the SoC supports the same */
	} else if (aq->caps->has_dllon) {
		atmel_qspi_write(QSPI_CR_DLLON, aq, QSPI_CR);
		ret =  readl_poll_timeout(aq->regs + QSPI_SR2, val,
					  (val & QSPI_SR2_DLOCK), 40,
					  ATMEL_QSPI_TIMEOUT);
	}

	/* Set the QSPI controller by default in Serial Memory Mode */
	aq->mr |= QSPI_MR_DQSDLYEN;
	ret = atmel_qspi_set_serial_memory_mode(aq);
	if (ret < 0)
		return ret;

	/* Enable the QSPI controller. */
	atmel_qspi_write(QSPI_CR_QSPIEN, aq, QSPI_CR);
	ret = readl_poll_timeout(aq->regs + QSPI_SR2, val,
				 val & QSPI_SR2_QSPIENS, 40,
				 ATMEL_QSPI_SYNC_TIMEOUT);
	if (ret)
		return ret;

	if (aq->caps->octal) {
		ret = readl_poll_timeout(aq->regs + QSPI_SR, val,
					 val & QSPI_SR_RFRSHD, 40,
					 ATMEL_QSPI_TIMEOUT);
	}

	atmel_qspi_write(QSPI_TOUT_TCNTM, aq, QSPI_TOUT);
	return ret;
}

static int atmel_qspi_sama7g5_setup(struct spi_device *spi)
{
	struct atmel_qspi *aq = spi_controller_get_devdata(spi->controller);

	/* The controller can communicate with a single peripheral device (target). */
	aq->target_max_speed_hz = spi->max_speed_hz;

	return atmel_qspi_sama7g5_init(aq);
}

static int atmel_qspi_setup(struct spi_device *spi)
{
	struct spi_controller *ctrl = spi->controller;
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);
	unsigned long src_rate;
	u32 scbr;
	int ret;

	if (ctrl->busy)
		return -EBUSY;

	if (!spi->max_speed_hz)
		return -EINVAL;

	if (aq->caps->has_gclk)
		return atmel_qspi_sama7g5_setup(spi);

	src_rate = clk_get_rate(aq->pclk);
	if (!src_rate)
		return -EINVAL;

	/* Compute the QSPI baudrate */
	scbr = DIV_ROUND_UP(src_rate, spi->max_speed_hz);
	if (scbr > 0)
		scbr--;

	ret = pm_runtime_resume_and_get(ctrl->dev.parent);
	if (ret < 0)
		return ret;

	aq->scr &= ~QSPI_SCR_SCBR_MASK;
	aq->scr |= QSPI_SCR_SCBR(scbr);
	atmel_qspi_write(aq->scr, aq, QSPI_SCR);

	pm_runtime_put_autosuspend(ctrl->dev.parent);

	return 0;
}

static int atmel_qspi_set_cs_timing(struct spi_device *spi)
{
	struct spi_controller *ctrl = spi->controller;
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);
	unsigned long clk_rate;
	u32 cs_inactive;
	u32 cs_setup;
	u32 cs_hold;
	int delay;
	int ret;

	clk_rate = clk_get_rate(aq->pclk);
	if (!clk_rate)
		return -EINVAL;

	/* hold */
	delay = spi_delay_to_ns(&spi->cs_hold, NULL);
	if (aq->mr & QSPI_MR_SMM) {
		if (delay > 0)
			dev_warn(&aq->pdev->dev,
				 "Ignoring cs_hold, must be 0 in Serial Memory Mode.\n");
		cs_hold = 0;
	} else {
		delay = spi_delay_to_ns(&spi->cs_hold, NULL);
		if (delay < 0)
			return delay;

		cs_hold = DIV_ROUND_UP((delay * DIV_ROUND_UP(clk_rate, 1000000)), 32000);
	}

	/* setup */
	delay = spi_delay_to_ns(&spi->cs_setup, NULL);
	if (delay < 0)
		return delay;

	cs_setup = DIV_ROUND_UP((delay * DIV_ROUND_UP(clk_rate, 1000000)),
				1000);

	/* inactive */
	delay = spi_delay_to_ns(&spi->cs_inactive, NULL);
	if (delay < 0)
		return delay;
	cs_inactive = DIV_ROUND_UP((delay * DIV_ROUND_UP(clk_rate, 1000000)), 1000);

	ret = pm_runtime_resume_and_get(ctrl->dev.parent);
	if (ret < 0)
		return ret;

	aq->scr &= ~QSPI_SCR_DLYBS_MASK;
	aq->scr |= QSPI_SCR_DLYBS(cs_setup);
	atmel_qspi_write(aq->scr, aq, QSPI_SCR);

	aq->mr &= ~(QSPI_MR_DLYBCT_MASK | QSPI_MR_DLYCS_MASK);
	aq->mr |= QSPI_MR_DLYBCT(cs_hold) | QSPI_MR_DLYCS(cs_inactive);
	atmel_qspi_write(aq->mr, aq, QSPI_MR);

	pm_runtime_put_autosuspend(ctrl->dev.parent);

	return 0;
}

static int atmel_qspi_init(struct atmel_qspi *aq)
{
	int ret;

	if (aq->caps->has_gclk) {
		ret = atmel_qspi_reg_sync(aq);
		if (ret)
			return ret;
		atmel_qspi_write(QSPI_CR_SWRST, aq, QSPI_CR);
		return 0;
	}

	/* Reset the QSPI controller */
	atmel_qspi_write(QSPI_CR_SWRST, aq, QSPI_CR);

	/* Set the QSPI controller by default in Serial Memory Mode */
	ret = atmel_qspi_set_serial_memory_mode(aq);
	if (ret < 0)
		return ret;

	/* Enable the QSPI controller */
	atmel_qspi_write(QSPI_CR_QSPIEN, aq, QSPI_CR);
	return 0;
}

static irqreturn_t atmel_qspi_interrupt(int irq, void *dev_id)
{
	struct atmel_qspi *aq = dev_id;
	u32 status, mask, pending;

	status = atmel_qspi_read(aq, QSPI_SR);
	mask = atmel_qspi_read(aq, QSPI_IMR);
	pending = status & mask;

	if (!pending)
		return IRQ_NONE;

	aq->pending |= pending;
	if ((aq->pending & aq->irq_mask) == aq->irq_mask)
		complete(&aq->cmd_completion);

	return IRQ_HANDLED;
}

static int atmel_qspi_dma_init(struct spi_controller *ctrl)
{
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);
	int ret;

	aq->rx_chan = devm_dma_request_chan(&aq->pdev->dev, "rx");
	if (IS_ERR(aq->rx_chan)) {
		ret = dev_err_probe(&aq->pdev->dev, PTR_ERR(aq->rx_chan),
				    "RX DMA channel is not available\n");
		aq->rx_chan = NULL;
		return ret;
	}

	aq->tx_chan = devm_dma_request_chan(&aq->pdev->dev, "tx");
	if (IS_ERR(aq->tx_chan)) {
		ret = dev_err_probe(&aq->pdev->dev, PTR_ERR(aq->tx_chan),
				    "TX DMA channel is not available\n");
		aq->rx_chan = NULL;
		aq->tx_chan = NULL;
		return ret;
	}

	ctrl->dma_rx = aq->rx_chan;
	ctrl->dma_tx = aq->tx_chan;
	init_completion(&aq->dma_completion);

	dev_info(&aq->pdev->dev, "Using %s (tx) and %s (rx) for DMA transfers\n",
		 dma_chan_name(aq->tx_chan), dma_chan_name(aq->rx_chan));

	return 0;
}

static const struct atmel_qspi_ops atmel_qspi_ops = {
	.set_cfg = atmel_qspi_set_cfg,
	.transfer = atmel_qspi_transfer,
};

static const struct atmel_qspi_ops atmel_qspi_sama7g5_ops = {
	.set_cfg = atmel_qspi_sama7g5_set_cfg,
	.transfer = atmel_qspi_sama7g5_transfer,
};

static int atmel_qspi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctrl;
	struct atmel_qspi *aq;
	struct resource *res;
	int irq, err = 0;

	ctrl = devm_spi_alloc_host(&pdev->dev, sizeof(*aq));
	if (!ctrl)
		return -ENOMEM;

	aq = spi_controller_get_devdata(ctrl);

	aq->caps = of_device_get_match_data(&pdev->dev);
	if (!aq->caps) {
		dev_err(&pdev->dev, "Could not retrieve QSPI caps\n");
		return -EINVAL;
	}

	init_completion(&aq->cmd_completion);
	aq->pdev = pdev;

	ctrl->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD | SPI_TX_DUAL | SPI_TX_QUAD;
	if (aq->caps->octal)
		ctrl->mode_bits |= SPI_RX_OCTAL | SPI_TX_OCTAL;

	if (aq->caps->has_gclk)
		aq->ops = &atmel_qspi_sama7g5_ops;
	else
		aq->ops = &atmel_qspi_ops;

	ctrl->max_speed_hz = aq->caps->max_speed_hz;
	ctrl->setup = atmel_qspi_setup;
	ctrl->set_cs_timing = atmel_qspi_set_cs_timing;
	ctrl->bus_num = -1;
	ctrl->mem_ops = &atmel_qspi_mem_ops;
	ctrl->num_chipselect = 1;
	ctrl->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, ctrl);

	/* Map the registers */
	aq->regs = devm_platform_ioremap_resource_byname(pdev, "qspi_base");
	if (IS_ERR(aq->regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(aq->regs),
				     "missing registers\n");

	/* Map the AHB memory */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi_mmap");
	aq->mem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(aq->mem))
		return dev_err_probe(&pdev->dev, PTR_ERR(aq->mem),
				     "missing AHB memory\n");

	aq->mmap_size = resource_size(res);
	aq->mmap_phys_base = (dma_addr_t)res->start;

	/* Get the peripheral clock */
	aq->pclk = devm_clk_get_enabled(&pdev->dev, "pclk");
	if (IS_ERR(aq->pclk))
		aq->pclk = devm_clk_get_enabled(&pdev->dev, NULL);

	if (IS_ERR(aq->pclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(aq->pclk),
				     "missing peripheral clock\n");

	if (aq->caps->has_qspick) {
		/* Get the QSPI system clock */
		aq->qspick = devm_clk_get_enabled(&pdev->dev, "qspick");
		if (IS_ERR(aq->qspick)) {
			dev_err(&pdev->dev, "missing system clock\n");
			err = PTR_ERR(aq->qspick);
			return err;
		}

	} else if (aq->caps->has_gclk) {
		/* Get the QSPI generic clock */
		aq->gclk = devm_clk_get(&pdev->dev, "gclk");
		if (IS_ERR(aq->gclk)) {
			dev_err(&pdev->dev, "missing Generic clock\n");
			err = PTR_ERR(aq->gclk);
			return err;
		}
	}

	if (aq->caps->has_dma) {
		err = atmel_qspi_dma_init(ctrl);
		if (err == -EPROBE_DEFER)
			return err;
	}

	/* Request the IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, atmel_qspi_interrupt,
			       0, dev_name(&pdev->dev), aq);
	if (err)
		return err;

	pm_runtime_set_autosuspend_delay(&pdev->dev, 500);
	pm_runtime_use_autosuspend(&pdev->dev);
	devm_pm_runtime_set_active_enabled(&pdev->dev);
	devm_pm_runtime_get_noresume(&pdev->dev);

	err = atmel_qspi_init(aq);
	if (err)
		return err;

	err = spi_register_controller(ctrl);
	if (err)
		return err;

	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;
}

static int atmel_qspi_sama7g5_suspend(struct atmel_qspi *aq)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout(aq->regs + QSPI_SR2, val,
				 !(val & QSPI_SR2_RBUSY) &&
				 (val & QSPI_SR2_HIDLE), 40,
				 ATMEL_QSPI_SYNC_TIMEOUT);
	if (ret)
		return ret;

	atmel_qspi_write(QSPI_CR_QSPIDIS, aq, QSPI_CR);
	ret = readl_poll_timeout(aq->regs + QSPI_SR2, val,
				 !(val & QSPI_SR2_QSPIENS), 40,
				 ATMEL_QSPI_SYNC_TIMEOUT);
	if (ret)
		return ret;

	clk_disable_unprepare(aq->gclk);

	if (aq->caps->has_dllon) {
		atmel_qspi_write(QSPI_CR_DLLOFF, aq, QSPI_CR);
		ret = readl_poll_timeout(aq->regs + QSPI_SR2, val,
					 !(val & QSPI_SR2_DLOCK), 40,
					 ATMEL_QSPI_TIMEOUT);
		if (ret)
			return ret;
	}

	if (aq->caps->has_padcalib)
		return readl_poll_timeout(aq->regs + QSPI_SR2, val,
					  !(val & QSPI_SR2_CALBSY), 40,
					  ATMEL_QSPI_TIMEOUT);
	return 0;
}

static void atmel_qspi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctrl = platform_get_drvdata(pdev);
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);
	int ret;

	spi_unregister_controller(ctrl);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret >= 0) {
		if (aq->caps->has_gclk) {
			ret = atmel_qspi_sama7g5_suspend(aq);
			if (ret)
				dev_warn(&pdev->dev, "Failed to de-init device on remove: %d\n", ret);
			return;
		}

		atmel_qspi_write(QSPI_CR_QSPIDIS, aq, QSPI_CR);
	} else {
		/*
		 * atmel_qspi_runtime_{suspend,resume} just disable and enable
		 * the two clks respectively. So after resume failed these are
		 * off, and we skip hardware access and disabling these clks again.
		 */
		dev_warn(&pdev->dev, "Failed to resume device on remove\n");
	}
}

static int __maybe_unused atmel_qspi_suspend(struct device *dev)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (aq->caps->has_gclk) {
		ret = atmel_qspi_sama7g5_suspend(aq);
		clk_disable_unprepare(aq->pclk);
		return ret;
	}

	atmel_qspi_write(QSPI_CR_QSPIDIS, aq, QSPI_CR);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_force_suspend(dev);

	clk_unprepare(aq->qspick);
	clk_unprepare(aq->pclk);

	return 0;
}

static int __maybe_unused atmel_qspi_resume(struct device *dev)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);
	int ret;

	ret = clk_prepare(aq->pclk);
	if (ret)
		return ret;

	ret = clk_prepare(aq->qspick);
	if (ret) {
		clk_unprepare(aq->pclk);
		return ret;
	}

	if (aq->caps->has_gclk)
		return atmel_qspi_sama7g5_init(aq);

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	atmel_qspi_init(aq);

	atmel_qspi_write(aq->scr, aq, QSPI_SCR);

	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int __maybe_unused atmel_qspi_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);

	clk_disable(aq->qspick);
	clk_disable(aq->pclk);

	return 0;
}

static int __maybe_unused atmel_qspi_runtime_resume(struct device *dev)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct atmel_qspi *aq = spi_controller_get_devdata(ctrl);
	int ret;

	ret = clk_enable(aq->pclk);
	if (ret)
		return ret;

	ret = clk_enable(aq->qspick);
	if (ret)
		clk_disable(aq->pclk);

	return ret;
}

static const struct dev_pm_ops __maybe_unused atmel_qspi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(atmel_qspi_suspend, atmel_qspi_resume)
	SET_RUNTIME_PM_OPS(atmel_qspi_runtime_suspend,
			   atmel_qspi_runtime_resume, NULL)
};

static const struct atmel_qspi_caps atmel_sama5d2_qspi_caps = {};

static const struct atmel_qspi_caps atmel_sam9x60_qspi_caps = {
	.has_qspick = true,
	.has_ricr = true,
};

static const struct atmel_qspi_caps atmel_sam9x7_ospi_caps = {
	.max_speed_hz = SAM9X7_QSPI_MAX_SPEED_HZ,
	.has_gclk = true,
	.octal = true,
	.has_dma = true,
	.has_2xgclk = true,
	.has_padcalib = false,
	.has_dllon = false,
};

static const struct atmel_qspi_caps atmel_sama7d65_ospi_caps = {
	.max_speed_hz = SAMA7G5_QSPI0_MAX_SPEED_HZ,
	.has_gclk = true,
	.octal = true,
	.has_dma = true,
	.has_2xgclk = true,
	.has_padcalib = true,
	.has_dllon = false,
};

static const struct atmel_qspi_caps atmel_sama7d65_qspi_caps = {
	.max_speed_hz = SAMA7G5_QSPI1_SDR_MAX_SPEED_HZ,
	.has_gclk = true,
	.has_dma = true,
	.has_2xgclk = true,
	.has_dllon = false,
};

static const struct atmel_qspi_caps atmel_sama7g5_ospi_caps = {
	.max_speed_hz = SAMA7G5_QSPI0_MAX_SPEED_HZ,
	.has_gclk = true,
	.octal = true,
	.has_dma = true,
	.has_padcalib = true,
	.has_dllon = true,
};

static const struct atmel_qspi_caps atmel_sama7g5_qspi_caps = {
	.max_speed_hz = SAMA7G5_QSPI1_SDR_MAX_SPEED_HZ,
	.has_gclk = true,
	.has_dma = true,
	.has_dllon = true,
};

static const struct of_device_id atmel_qspi_dt_ids[] = {
	{
		.compatible = "atmel,sama5d2-qspi",
		.data = &atmel_sama5d2_qspi_caps,
	},
	{
		.compatible = "microchip,sam9x60-qspi",
		.data = &atmel_sam9x60_qspi_caps,
	},
	{
		.compatible = "microchip,sama7g5-ospi",
		.data = &atmel_sama7g5_ospi_caps,
	},
	{
		.compatible = "microchip,sama7g5-qspi",
		.data = &atmel_sama7g5_qspi_caps,
	},
	{
		.compatible = "microchip,sam9x7-ospi",
		.data = &atmel_sam9x7_ospi_caps,
	},
	{
		.compatible = "microchip,sama7d65-ospi",
		.data = &atmel_sama7d65_ospi_caps,
	},
	{
		.compatible = "microchip,sama7d65-qspi",
		.data = &atmel_sama7d65_qspi_caps,
	},


	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_qspi_dt_ids);

static struct platform_driver atmel_qspi_driver = {
	.driver = {
		.name	= "atmel_qspi",
		.of_match_table	= atmel_qspi_dt_ids,
		.pm	= pm_ptr(&atmel_qspi_pm_ops),
	},
	.probe		= atmel_qspi_probe,
	.remove		= atmel_qspi_remove,
};
module_platform_driver(atmel_qspi_driver);

MODULE_AUTHOR("Cyrille Pitchen <cyrille.pitchen@atmel.com>");
MODULE_AUTHOR("Piotr Bugalski <bugalski.piotr@gmail.com");
MODULE_DESCRIPTION("Atmel QSPI Controller driver");
MODULE_LICENSE("GPL v2");
