/*
 * drivers/mtd/nand/pxa3xx_nand.c
 *
 * Copyright © 2005 Intel Corporation
 * Copyright © 2006 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * See Documentation/mtd/nand/pxa3xx-nand.txt for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_mtd.h>

#if defined(CONFIG_ARCH_PXA) || defined(CONFIG_ARCH_MMP)
#define ARCH_HAS_DMA
#endif

#ifdef ARCH_HAS_DMA
#include <mach/dma.h>
#endif

#include <linux/platform_data/mtd-nand-pxa3xx.h>

#define NAND_DEV_READY_TIMEOUT  50
#define	CHIP_DELAY_TIMEOUT	(2 * HZ/10)
#define NAND_STOP_DELAY		(2 * HZ/50)
#define PAGE_CHUNK_SIZE		(2048)

/*
 * Define a buffer size for the initial command that detects the flash device:
 * STATUS, READID and PARAM. The largest of these is the PARAM command,
 * needing 256 bytes.
 */
#define INIT_BUFFER_SIZE	256

/* registers and bit definitions */
#define NDCR		(0x00) /* Control register */
#define NDTR0CS0	(0x04) /* Timing Parameter 0 for CS0 */
#define NDTR1CS0	(0x0C) /* Timing Parameter 1 for CS0 */
#define NDSR		(0x14) /* Status Register */
#define NDPCR		(0x18) /* Page Count Register */
#define NDBDR0		(0x1C) /* Bad Block Register 0 */
#define NDBDR1		(0x20) /* Bad Block Register 1 */
#define NDECCCTRL	(0x28) /* ECC control */
#define NDDB		(0x40) /* Data Buffer */
#define NDCB0		(0x48) /* Command Buffer0 */
#define NDCB1		(0x4C) /* Command Buffer1 */
#define NDCB2		(0x50) /* Command Buffer2 */

#define NDCR_SPARE_EN		(0x1 << 31)
#define NDCR_ECC_EN		(0x1 << 30)
#define NDCR_DMA_EN		(0x1 << 29)
#define NDCR_ND_RUN		(0x1 << 28)
#define NDCR_DWIDTH_C		(0x1 << 27)
#define NDCR_DWIDTH_M		(0x1 << 26)
#define NDCR_PAGE_SZ		(0x1 << 24)
#define NDCR_NCSX		(0x1 << 23)
#define NDCR_ND_MODE		(0x3 << 21)
#define NDCR_NAND_MODE   	(0x0)
#define NDCR_CLR_PG_CNT		(0x1 << 20)
#define NDCR_STOP_ON_UNCOR	(0x1 << 19)
#define NDCR_RD_ID_CNT_MASK	(0x7 << 16)
#define NDCR_RD_ID_CNT(x)	(((x) << 16) & NDCR_RD_ID_CNT_MASK)

#define NDCR_RA_START		(0x1 << 15)
#define NDCR_PG_PER_BLK		(0x1 << 14)
#define NDCR_ND_ARB_EN		(0x1 << 12)
#define NDCR_INT_MASK           (0xFFF)

#define NDSR_MASK		(0xfff)
#define NDSR_ERR_CNT_OFF	(16)
#define NDSR_ERR_CNT_MASK       (0x1f)
#define NDSR_ERR_CNT(sr)	((sr >> NDSR_ERR_CNT_OFF) & NDSR_ERR_CNT_MASK)
#define NDSR_RDY                (0x1 << 12)
#define NDSR_FLASH_RDY          (0x1 << 11)
#define NDSR_CS0_PAGED		(0x1 << 10)
#define NDSR_CS1_PAGED		(0x1 << 9)
#define NDSR_CS0_CMDD		(0x1 << 8)
#define NDSR_CS1_CMDD		(0x1 << 7)
#define NDSR_CS0_BBD		(0x1 << 6)
#define NDSR_CS1_BBD		(0x1 << 5)
#define NDSR_UNCORERR		(0x1 << 4)
#define NDSR_CORERR		(0x1 << 3)
#define NDSR_WRDREQ		(0x1 << 2)
#define NDSR_RDDREQ		(0x1 << 1)
#define NDSR_WRCMDREQ		(0x1)

#define NDCB0_LEN_OVRD		(0x1 << 28)
#define NDCB0_ST_ROW_EN         (0x1 << 26)
#define NDCB0_AUTO_RS		(0x1 << 25)
#define NDCB0_CSEL		(0x1 << 24)
#define NDCB0_EXT_CMD_TYPE_MASK	(0x7 << 29)
#define NDCB0_EXT_CMD_TYPE(x)	(((x) << 29) & NDCB0_EXT_CMD_TYPE_MASK)
#define NDCB0_CMD_TYPE_MASK	(0x7 << 21)
#define NDCB0_CMD_TYPE(x)	(((x) << 21) & NDCB0_CMD_TYPE_MASK)
#define NDCB0_NC		(0x1 << 20)
#define NDCB0_DBC		(0x1 << 19)
#define NDCB0_ADDR_CYC_MASK	(0x7 << 16)
#define NDCB0_ADDR_CYC(x)	(((x) << 16) & NDCB0_ADDR_CYC_MASK)
#define NDCB0_CMD2_MASK		(0xff << 8)
#define NDCB0_CMD1_MASK		(0xff)
#define NDCB0_ADDR_CYC_SHIFT	(16)

#define EXT_CMD_TYPE_DISPATCH	6 /* Command dispatch */
#define EXT_CMD_TYPE_NAKED_RW	5 /* Naked read or Naked write */
#define EXT_CMD_TYPE_READ	4 /* Read */
#define EXT_CMD_TYPE_DISP_WR	4 /* Command dispatch with write */
#define EXT_CMD_TYPE_FINAL	3 /* Final command */
#define EXT_CMD_TYPE_LAST_RW	1 /* Last naked read/write */
#define EXT_CMD_TYPE_MONO	0 /* Monolithic read/write */

/* macros for registers read/write */
#define nand_writel(info, off, val)	\
	__raw_writel((val), (info)->mmio_base + (off))

#define nand_readl(info, off)		\
	__raw_readl((info)->mmio_base + (off))

/* error code and state */
enum {
	ERR_NONE	= 0,
	ERR_DMABUSERR	= -1,
	ERR_SENDCMD	= -2,
	ERR_UNCORERR	= -3,
	ERR_BBERR	= -4,
	ERR_CORERR	= -5,
};

enum {
	STATE_IDLE = 0,
	STATE_PREPARED,
	STATE_CMD_HANDLE,
	STATE_DMA_READING,
	STATE_DMA_WRITING,
	STATE_DMA_DONE,
	STATE_PIO_READING,
	STATE_PIO_WRITING,
	STATE_CMD_DONE,
	STATE_READY,
};

enum pxa3xx_nand_variant {
	PXA3XX_NAND_VARIANT_PXA,
	PXA3XX_NAND_VARIANT_ARMADA370,
};

struct pxa3xx_nand_host {
	struct nand_chip	chip;
	struct mtd_info         *mtd;
	void			*info_data;

	/* page size of attached chip */
	int			use_ecc;
	int			cs;

	/* calculated from pxa3xx_nand_flash data */
	unsigned int		col_addr_cycles;
	unsigned int		row_addr_cycles;
	size_t			read_id_bytes;

};

struct pxa3xx_nand_info {
	struct nand_hw_control	controller;
	struct platform_device	 *pdev;

	struct clk		*clk;
	void __iomem		*mmio_base;
	unsigned long		mmio_phys;
	struct completion	cmd_complete, dev_ready;

	unsigned int 		buf_start;
	unsigned int		buf_count;
	unsigned int		buf_size;
	unsigned int		data_buff_pos;
	unsigned int		oob_buff_pos;

	/* DMA information */
	int			drcmr_dat;
	int			drcmr_cmd;

	unsigned char		*data_buff;
	unsigned char		*oob_buff;
	dma_addr_t 		data_buff_phys;
	int 			data_dma_ch;
	struct pxa_dma_desc	*data_desc;
	dma_addr_t 		data_desc_addr;

	struct pxa3xx_nand_host *host[NUM_CHIP_SELECT];
	unsigned int		state;

	/*
	 * This driver supports NFCv1 (as found in PXA SoC)
	 * and NFCv2 (as found in Armada 370/XP SoC).
	 */
	enum pxa3xx_nand_variant variant;

	int			cs;
	int			use_ecc;	/* use HW ECC ? */
	int			ecc_bch;	/* using BCH ECC? */
	int			use_dma;	/* use DMA ? */
	int			use_spare;	/* use spare ? */
	int			need_wait;

	unsigned int		data_size;	/* data to be read from FIFO */
	unsigned int		chunk_size;	/* split commands chunk size */
	unsigned int		oob_size;
	unsigned int		spare_size;
	unsigned int		ecc_size;
	unsigned int		ecc_err_cnt;
	unsigned int		max_bitflips;
	int 			retcode;

	/* cached register value */
	uint32_t		reg_ndcr;
	uint32_t		ndtr0cs0;
	uint32_t		ndtr1cs0;

	/* generated NDCBx register values */
	uint32_t		ndcb0;
	uint32_t		ndcb1;
	uint32_t		ndcb2;
	uint32_t		ndcb3;
};

static bool use_dma = 1;
module_param(use_dma, bool, 0444);
MODULE_PARM_DESC(use_dma, "enable DMA for data transferring to/from NAND HW");

static struct pxa3xx_nand_timing timing[] = {
	{ 40, 80, 60, 100, 80, 100, 90000, 400, 40, },
	{ 10,  0, 20,  40, 30,  40, 11123, 110, 10, },
	{ 10, 25, 15,  25, 15,  30, 25000,  60, 10, },
	{ 10, 35, 15,  25, 15,  25, 25000,  60, 10, },
};

static struct pxa3xx_nand_flash builtin_flash_types[] = {
{ "DEFAULT FLASH",      0,   0, 2048,  8,  8,    0, &timing[0] },
{ "64MiB 16-bit",  0x46ec,  32,  512, 16, 16, 4096, &timing[1] },
{ "256MiB 8-bit",  0xdaec,  64, 2048,  8,  8, 2048, &timing[1] },
{ "4GiB 8-bit",    0xd7ec, 128, 4096,  8,  8, 8192, &timing[1] },
{ "128MiB 8-bit",  0xa12c,  64, 2048,  8,  8, 1024, &timing[2] },
{ "128MiB 16-bit", 0xb12c,  64, 2048, 16, 16, 1024, &timing[2] },
{ "512MiB 8-bit",  0xdc2c,  64, 2048,  8,  8, 4096, &timing[2] },
{ "512MiB 16-bit", 0xcc2c,  64, 2048, 16, 16, 4096, &timing[2] },
{ "256MiB 16-bit", 0xba20,  64, 2048, 16, 16, 2048, &timing[3] },
};

static u8 bbt_pattern[] = {'M', 'V', 'B', 'b', 't', '0' };
static u8 bbt_mirror_pattern[] = {'1', 't', 'b', 'B', 'V', 'M' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	8,
	.len = 6,
	.veroffs = 14,
	.maxblocks = 8,		/* Last 8 blocks in each chip */
	.pattern = bbt_pattern
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	8,
	.len = 6,
	.veroffs = 14,
	.maxblocks = 8,		/* Last 8 blocks in each chip */
	.pattern = bbt_mirror_pattern
};

static struct nand_ecclayout ecc_layout_2KB_bch4bit = {
	.eccbytes = 32,
	.eccpos = {
		32, 33, 34, 35, 36, 37, 38, 39,
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree = { {2, 30} }
};

static struct nand_ecclayout ecc_layout_4KB_bch4bit = {
	.eccbytes = 64,
	.eccpos = {
		32,  33,  34,  35,  36,  37,  38,  39,
		40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,
		56,  57,  58,  59,  60,  61,  62,  63,
		96,  97,  98,  99,  100, 101, 102, 103,
		104, 105, 106, 107, 108, 109, 110, 111,
		112, 113, 114, 115, 116, 117, 118, 119,
		120, 121, 122, 123, 124, 125, 126, 127},
	/* Bootrom looks in bytes 0 & 5 for bad blocks */
	.oobfree = { {6, 26}, { 64, 32} }
};

static struct nand_ecclayout ecc_layout_4KB_bch8bit = {
	.eccbytes = 128,
	.eccpos = {
		32,  33,  34,  35,  36,  37,  38,  39,
		40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,
		56,  57,  58,  59,  60,  61,  62,  63},
	.oobfree = { }
};

/* Define a default flash type setting serve as flash detecting only */
#define DEFAULT_FLASH_TYPE (&builtin_flash_types[0])

#define NDTR0_tCH(c)	(min((c), 7) << 19)
#define NDTR0_tCS(c)	(min((c), 7) << 16)
#define NDTR0_tWH(c)	(min((c), 7) << 11)
#define NDTR0_tWP(c)	(min((c), 7) << 8)
#define NDTR0_tRH(c)	(min((c), 7) << 3)
#define NDTR0_tRP(c)	(min((c), 7) << 0)

#define NDTR1_tR(c)	(min((c), 65535) << 16)
#define NDTR1_tWHR(c)	(min((c), 15) << 4)
#define NDTR1_tAR(c)	(min((c), 15) << 0)

/* convert nano-seconds to nand flash controller clock cycles */
#define ns2cycle(ns, clk)	(int)((ns) * (clk / 1000000) / 1000)

static struct of_device_id pxa3xx_nand_dt_ids[] = {
	{
		.compatible = "marvell,pxa3xx-nand",
		.data       = (void *)PXA3XX_NAND_VARIANT_PXA,
	},
	{
		.compatible = "marvell,armada370-nand",
		.data       = (void *)PXA3XX_NAND_VARIANT_ARMADA370,
	},
	{}
};
MODULE_DEVICE_TABLE(of, pxa3xx_nand_dt_ids);

static enum pxa3xx_nand_variant
pxa3xx_nand_get_variant(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(pxa3xx_nand_dt_ids, &pdev->dev);
	if (!of_id)
		return PXA3XX_NAND_VARIANT_PXA;
	return (enum pxa3xx_nand_variant)of_id->data;
}

static void pxa3xx_nand_set_timing(struct pxa3xx_nand_host *host,
				   const struct pxa3xx_nand_timing *t)
{
	struct pxa3xx_nand_info *info = host->info_data;
	unsigned long nand_clk = clk_get_rate(info->clk);
	uint32_t ndtr0, ndtr1;

	ndtr0 = NDTR0_tCH(ns2cycle(t->tCH, nand_clk)) |
		NDTR0_tCS(ns2cycle(t->tCS, nand_clk)) |
		NDTR0_tWH(ns2cycle(t->tWH, nand_clk)) |
		NDTR0_tWP(ns2cycle(t->tWP, nand_clk)) |
		NDTR0_tRH(ns2cycle(t->tRH, nand_clk)) |
		NDTR0_tRP(ns2cycle(t->tRP, nand_clk));

	ndtr1 = NDTR1_tR(ns2cycle(t->tR, nand_clk)) |
		NDTR1_tWHR(ns2cycle(t->tWHR, nand_clk)) |
		NDTR1_tAR(ns2cycle(t->tAR, nand_clk));

	info->ndtr0cs0 = ndtr0;
	info->ndtr1cs0 = ndtr1;
	nand_writel(info, NDTR0CS0, ndtr0);
	nand_writel(info, NDTR1CS0, ndtr1);
}

/*
 * Set the data and OOB size, depending on the selected
 * spare and ECC configuration.
 * Only applicable to READ0, READOOB and PAGEPROG commands.
 */
static void pxa3xx_set_datasize(struct pxa3xx_nand_info *info,
				struct mtd_info *mtd)
{
	int oob_enable = info->reg_ndcr & NDCR_SPARE_EN;

	info->data_size = mtd->writesize;
	if (!oob_enable)
		return;

	info->oob_size = info->spare_size;
	if (!info->use_ecc)
		info->oob_size += info->ecc_size;
}

/**
 * NOTE: it is a must to set ND_RUN firstly, then write
 * command buffer, otherwise, it does not work.
 * We enable all the interrupt at the same time, and
 * let pxa3xx_nand_irq to handle all logic.
 */
static void pxa3xx_nand_start(struct pxa3xx_nand_info *info)
{
	uint32_t ndcr;

	ndcr = info->reg_ndcr;

	if (info->use_ecc) {
		ndcr |= NDCR_ECC_EN;
		if (info->ecc_bch)
			nand_writel(info, NDECCCTRL, 0x1);
	} else {
		ndcr &= ~NDCR_ECC_EN;
		if (info->ecc_bch)
			nand_writel(info, NDECCCTRL, 0x0);
	}

	if (info->use_dma)
		ndcr |= NDCR_DMA_EN;
	else
		ndcr &= ~NDCR_DMA_EN;

	if (info->use_spare)
		ndcr |= NDCR_SPARE_EN;
	else
		ndcr &= ~NDCR_SPARE_EN;

	ndcr |= NDCR_ND_RUN;

	/* clear status bits and run */
	nand_writel(info, NDCR, 0);
	nand_writel(info, NDSR, NDSR_MASK);
	nand_writel(info, NDCR, ndcr);
}

static void pxa3xx_nand_stop(struct pxa3xx_nand_info *info)
{
	uint32_t ndcr;
	int timeout = NAND_STOP_DELAY;

	/* wait RUN bit in NDCR become 0 */
	ndcr = nand_readl(info, NDCR);
	while ((ndcr & NDCR_ND_RUN) && (timeout-- > 0)) {
		ndcr = nand_readl(info, NDCR);
		udelay(1);
	}

	if (timeout <= 0) {
		ndcr &= ~NDCR_ND_RUN;
		nand_writel(info, NDCR, ndcr);
	}
	/* clear status bits */
	nand_writel(info, NDSR, NDSR_MASK);
}

static void __maybe_unused
enable_int(struct pxa3xx_nand_info *info, uint32_t int_mask)
{
	uint32_t ndcr;

	ndcr = nand_readl(info, NDCR);
	nand_writel(info, NDCR, ndcr & ~int_mask);
}

static void disable_int(struct pxa3xx_nand_info *info, uint32_t int_mask)
{
	uint32_t ndcr;

	ndcr = nand_readl(info, NDCR);
	nand_writel(info, NDCR, ndcr | int_mask);
}

static void handle_data_pio(struct pxa3xx_nand_info *info)
{
	unsigned int do_bytes = min(info->data_size, info->chunk_size);

	switch (info->state) {
	case STATE_PIO_WRITING:
		__raw_writesl(info->mmio_base + NDDB,
			      info->data_buff + info->data_buff_pos,
			      DIV_ROUND_UP(do_bytes, 4));

		if (info->oob_size > 0)
			__raw_writesl(info->mmio_base + NDDB,
				      info->oob_buff + info->oob_buff_pos,
				      DIV_ROUND_UP(info->oob_size, 4));
		break;
	case STATE_PIO_READING:
		__raw_readsl(info->mmio_base + NDDB,
			     info->data_buff + info->data_buff_pos,
			     DIV_ROUND_UP(do_bytes, 4));

		if (info->oob_size > 0)
			__raw_readsl(info->mmio_base + NDDB,
				     info->oob_buff + info->oob_buff_pos,
				     DIV_ROUND_UP(info->oob_size, 4));
		break;
	default:
		dev_err(&info->pdev->dev, "%s: invalid state %d\n", __func__,
				info->state);
		BUG();
	}

	/* Update buffer pointers for multi-page read/write */
	info->data_buff_pos += do_bytes;
	info->oob_buff_pos += info->oob_size;
	info->data_size -= do_bytes;
}

#ifdef ARCH_HAS_DMA
static void start_data_dma(struct pxa3xx_nand_info *info)
{
	struct pxa_dma_desc *desc = info->data_desc;
	int dma_len = ALIGN(info->data_size + info->oob_size, 32);

	desc->ddadr = DDADR_STOP;
	desc->dcmd = DCMD_ENDIRQEN | DCMD_WIDTH4 | DCMD_BURST32 | dma_len;

	switch (info->state) {
	case STATE_DMA_WRITING:
		desc->dsadr = info->data_buff_phys;
		desc->dtadr = info->mmio_phys + NDDB;
		desc->dcmd |= DCMD_INCSRCADDR | DCMD_FLOWTRG;
		break;
	case STATE_DMA_READING:
		desc->dtadr = info->data_buff_phys;
		desc->dsadr = info->mmio_phys + NDDB;
		desc->dcmd |= DCMD_INCTRGADDR | DCMD_FLOWSRC;
		break;
	default:
		dev_err(&info->pdev->dev, "%s: invalid state %d\n", __func__,
				info->state);
		BUG();
	}

	DRCMR(info->drcmr_dat) = DRCMR_MAPVLD | info->data_dma_ch;
	DDADR(info->data_dma_ch) = info->data_desc_addr;
	DCSR(info->data_dma_ch) |= DCSR_RUN;
}

static void pxa3xx_nand_data_dma_irq(int channel, void *data)
{
	struct pxa3xx_nand_info *info = data;
	uint32_t dcsr;

	dcsr = DCSR(channel);
	DCSR(channel) = dcsr;

	if (dcsr & DCSR_BUSERR) {
		info->retcode = ERR_DMABUSERR;
	}

	info->state = STATE_DMA_DONE;
	enable_int(info, NDCR_INT_MASK);
	nand_writel(info, NDSR, NDSR_WRDREQ | NDSR_RDDREQ);
}
#else
static void start_data_dma(struct pxa3xx_nand_info *info)
{}
#endif

static irqreturn_t pxa3xx_nand_irq(int irq, void *devid)
{
	struct pxa3xx_nand_info *info = devid;
	unsigned int status, is_completed = 0, is_ready = 0;
	unsigned int ready, cmd_done;

	if (info->cs == 0) {
		ready           = NDSR_FLASH_RDY;
		cmd_done        = NDSR_CS0_CMDD;
	} else {
		ready           = NDSR_RDY;
		cmd_done        = NDSR_CS1_CMDD;
	}

	status = nand_readl(info, NDSR);

	if (status & NDSR_UNCORERR)
		info->retcode = ERR_UNCORERR;
	if (status & NDSR_CORERR) {
		info->retcode = ERR_CORERR;
		if (info->variant == PXA3XX_NAND_VARIANT_ARMADA370 &&
		    info->ecc_bch)
			info->ecc_err_cnt = NDSR_ERR_CNT(status);
		else
			info->ecc_err_cnt = 1;

		/*
		 * Each chunk composing a page is corrected independently,
		 * and we need to store maximum number of corrected bitflips
		 * to return it to the MTD layer in ecc.read_page().
		 */
		info->max_bitflips = max_t(unsigned int,
					   info->max_bitflips,
					   info->ecc_err_cnt);
	}
	if (status & (NDSR_RDDREQ | NDSR_WRDREQ)) {
		/* whether use dma to transfer data */
		if (info->use_dma) {
			disable_int(info, NDCR_INT_MASK);
			info->state = (status & NDSR_RDDREQ) ?
				      STATE_DMA_READING : STATE_DMA_WRITING;
			start_data_dma(info);
			goto NORMAL_IRQ_EXIT;
		} else {
			info->state = (status & NDSR_RDDREQ) ?
				      STATE_PIO_READING : STATE_PIO_WRITING;
			handle_data_pio(info);
		}
	}
	if (status & cmd_done) {
		info->state = STATE_CMD_DONE;
		is_completed = 1;
	}
	if (status & ready) {
		info->state = STATE_READY;
		is_ready = 1;
	}

	if (status & NDSR_WRCMDREQ) {
		nand_writel(info, NDSR, NDSR_WRCMDREQ);
		status &= ~NDSR_WRCMDREQ;
		info->state = STATE_CMD_HANDLE;

		/*
		 * Command buffer registers NDCB{0-2} (and optionally NDCB3)
		 * must be loaded by writing directly either 12 or 16
		 * bytes directly to NDCB0, four bytes at a time.
		 *
		 * Direct write access to NDCB1, NDCB2 and NDCB3 is ignored
		 * but each NDCBx register can be read.
		 */
		nand_writel(info, NDCB0, info->ndcb0);
		nand_writel(info, NDCB0, info->ndcb1);
		nand_writel(info, NDCB0, info->ndcb2);

		/* NDCB3 register is available in NFCv2 (Armada 370/XP SoC) */
		if (info->variant == PXA3XX_NAND_VARIANT_ARMADA370)
			nand_writel(info, NDCB0, info->ndcb3);
	}

	/* clear NDSR to let the controller exit the IRQ */
	nand_writel(info, NDSR, status);
	if (is_completed)
		complete(&info->cmd_complete);
	if (is_ready)
		complete(&info->dev_ready);
NORMAL_IRQ_EXIT:
	return IRQ_HANDLED;
}

static inline int is_buf_blank(uint8_t *buf, size_t len)
{
	for (; len > 0; len--)
		if (*buf++ != 0xff)
			return 0;
	return 1;
}

static void set_command_address(struct pxa3xx_nand_info *info,
		unsigned int page_size, uint16_t column, int page_addr)
{
	/* small page addr setting */
	if (page_size < PAGE_CHUNK_SIZE) {
		info->ndcb1 = ((page_addr & 0xFFFFFF) << 8)
				| (column & 0xFF);

		info->ndcb2 = 0;
	} else {
		info->ndcb1 = ((page_addr & 0xFFFF) << 16)
				| (column & 0xFFFF);

		if (page_addr & 0xFF0000)
			info->ndcb2 = (page_addr & 0xFF0000) >> 16;
		else
			info->ndcb2 = 0;
	}
}

static void prepare_start_command(struct pxa3xx_nand_info *info, int command)
{
	struct pxa3xx_nand_host *host = info->host[info->cs];
	struct mtd_info *mtd = host->mtd;

	/* reset data and oob column point to handle data */
	info->buf_start		= 0;
	info->buf_count		= 0;
	info->oob_size		= 0;
	info->data_buff_pos	= 0;
	info->oob_buff_pos	= 0;
	info->use_ecc		= 0;
	info->use_spare		= 1;
	info->retcode		= ERR_NONE;
	info->ecc_err_cnt	= 0;
	info->ndcb3		= 0;
	info->need_wait		= 0;

	switch (command) {
	case NAND_CMD_READ0:
	case NAND_CMD_PAGEPROG:
		info->use_ecc = 1;
	case NAND_CMD_READOOB:
		pxa3xx_set_datasize(info, mtd);
		break;
	case NAND_CMD_PARAM:
		info->use_spare = 0;
		break;
	default:
		info->ndcb1 = 0;
		info->ndcb2 = 0;
		break;
	}

	/*
	 * If we are about to issue a read command, or about to set
	 * the write address, then clean the data buffer.
	 */
	if (command == NAND_CMD_READ0 ||
	    command == NAND_CMD_READOOB ||
	    command == NAND_CMD_SEQIN) {

		info->buf_count = mtd->writesize + mtd->oobsize;
		memset(info->data_buff, 0xFF, info->buf_count);
	}

}

static int prepare_set_command(struct pxa3xx_nand_info *info, int command,
		int ext_cmd_type, uint16_t column, int page_addr)
{
	int addr_cycle, exec_cmd;
	struct pxa3xx_nand_host *host;
	struct mtd_info *mtd;

	host = info->host[info->cs];
	mtd = host->mtd;
	addr_cycle = 0;
	exec_cmd = 1;

	if (info->cs != 0)
		info->ndcb0 = NDCB0_CSEL;
	else
		info->ndcb0 = 0;

	if (command == NAND_CMD_SEQIN)
		exec_cmd = 0;

	addr_cycle = NDCB0_ADDR_CYC(host->row_addr_cycles
				    + host->col_addr_cycles);

	switch (command) {
	case NAND_CMD_READOOB:
	case NAND_CMD_READ0:
		info->buf_start = column;
		info->ndcb0 |= NDCB0_CMD_TYPE(0)
				| addr_cycle
				| NAND_CMD_READ0;

		if (command == NAND_CMD_READOOB)
			info->buf_start += mtd->writesize;

		/*
		 * Multiple page read needs an 'extended command type' field,
		 * which is either naked-read or last-read according to the
		 * state.
		 */
		if (mtd->writesize == PAGE_CHUNK_SIZE) {
			info->ndcb0 |= NDCB0_DBC | (NAND_CMD_READSTART << 8);
		} else if (mtd->writesize > PAGE_CHUNK_SIZE) {
			info->ndcb0 |= NDCB0_DBC | (NAND_CMD_READSTART << 8)
					| NDCB0_LEN_OVRD
					| NDCB0_EXT_CMD_TYPE(ext_cmd_type);
			info->ndcb3 = info->chunk_size +
				      info->oob_size;
		}

		set_command_address(info, mtd->writesize, column, page_addr);
		break;

	case NAND_CMD_SEQIN:

		info->buf_start = column;
		set_command_address(info, mtd->writesize, 0, page_addr);

		/*
		 * Multiple page programming needs to execute the initial
		 * SEQIN command that sets the page address.
		 */
		if (mtd->writesize > PAGE_CHUNK_SIZE) {
			info->ndcb0 |= NDCB0_CMD_TYPE(0x1)
				| NDCB0_EXT_CMD_TYPE(ext_cmd_type)
				| addr_cycle
				| command;
			/* No data transfer in this case */
			info->data_size = 0;
			exec_cmd = 1;
		}
		break;

	case NAND_CMD_PAGEPROG:
		if (is_buf_blank(info->data_buff,
					(mtd->writesize + mtd->oobsize))) {
			exec_cmd = 0;
			break;
		}

		/* Second command setting for large pages */
		if (mtd->writesize > PAGE_CHUNK_SIZE) {
			/*
			 * Multiple page write uses the 'extended command'
			 * field. This can be used to issue a command dispatch
			 * or a naked-write depending on the current stage.
			 */
			info->ndcb0 |= NDCB0_CMD_TYPE(0x1)
					| NDCB0_LEN_OVRD
					| NDCB0_EXT_CMD_TYPE(ext_cmd_type);
			info->ndcb3 = info->chunk_size +
				      info->oob_size;

			/*
			 * This is the command dispatch that completes a chunked
			 * page program operation.
			 */
			if (info->data_size == 0) {
				info->ndcb0 = NDCB0_CMD_TYPE(0x1)
					| NDCB0_EXT_CMD_TYPE(ext_cmd_type)
					| command;
				info->ndcb1 = 0;
				info->ndcb2 = 0;
				info->ndcb3 = 0;
			}
		} else {
			info->ndcb0 |= NDCB0_CMD_TYPE(0x1)
					| NDCB0_AUTO_RS
					| NDCB0_ST_ROW_EN
					| NDCB0_DBC
					| (NAND_CMD_PAGEPROG << 8)
					| NAND_CMD_SEQIN
					| addr_cycle;
		}
		break;

	case NAND_CMD_PARAM:
		info->buf_count = 256;
		info->ndcb0 |= NDCB0_CMD_TYPE(0)
				| NDCB0_ADDR_CYC(1)
				| NDCB0_LEN_OVRD
				| command;
		info->ndcb1 = (column & 0xFF);
		info->ndcb3 = 256;
		info->data_size = 256;
		break;

	case NAND_CMD_READID:
		info->buf_count = host->read_id_bytes;
		info->ndcb0 |= NDCB0_CMD_TYPE(3)
				| NDCB0_ADDR_CYC(1)
				| command;
		info->ndcb1 = (column & 0xFF);

		info->data_size = 8;
		break;
	case NAND_CMD_STATUS:
		info->buf_count = 1;
		info->ndcb0 |= NDCB0_CMD_TYPE(4)
				| NDCB0_ADDR_CYC(1)
				| command;

		info->data_size = 8;
		break;

	case NAND_CMD_ERASE1:
		info->ndcb0 |= NDCB0_CMD_TYPE(2)
				| NDCB0_AUTO_RS
				| NDCB0_ADDR_CYC(3)
				| NDCB0_DBC
				| (NAND_CMD_ERASE2 << 8)
				| NAND_CMD_ERASE1;
		info->ndcb1 = page_addr;
		info->ndcb2 = 0;

		break;
	case NAND_CMD_RESET:
		info->ndcb0 |= NDCB0_CMD_TYPE(5)
				| command;

		break;

	case NAND_CMD_ERASE2:
		exec_cmd = 0;
		break;

	default:
		exec_cmd = 0;
		dev_err(&info->pdev->dev, "non-supported command %x\n",
				command);
		break;
	}

	return exec_cmd;
}

static void nand_cmdfunc(struct mtd_info *mtd, unsigned command,
			 int column, int page_addr)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	int ret, exec_cmd;

	/*
	 * if this is a x16 device ,then convert the input
	 * "byte" address into a "word" address appropriate
	 * for indexing a word-oriented device
	 */
	if (info->reg_ndcr & NDCR_DWIDTH_M)
		column /= 2;

	/*
	 * There may be different NAND chip hooked to
	 * different chip select, so check whether
	 * chip select has been changed, if yes, reset the timing
	 */
	if (info->cs != host->cs) {
		info->cs = host->cs;
		nand_writel(info, NDTR0CS0, info->ndtr0cs0);
		nand_writel(info, NDTR1CS0, info->ndtr1cs0);
	}

	prepare_start_command(info, command);

	info->state = STATE_PREPARED;
	exec_cmd = prepare_set_command(info, command, 0, column, page_addr);

	if (exec_cmd) {
		init_completion(&info->cmd_complete);
		init_completion(&info->dev_ready);
		info->need_wait = 1;
		pxa3xx_nand_start(info);

		ret = wait_for_completion_timeout(&info->cmd_complete,
				CHIP_DELAY_TIMEOUT);
		if (!ret) {
			dev_err(&info->pdev->dev, "Wait time out!!!\n");
			/* Stop State Machine for next command cycle */
			pxa3xx_nand_stop(info);
		}
	}
	info->state = STATE_IDLE;
}

static void nand_cmdfunc_extended(struct mtd_info *mtd,
				  const unsigned command,
				  int column, int page_addr)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	int ret, exec_cmd, ext_cmd_type;

	/*
	 * if this is a x16 device then convert the input
	 * "byte" address into a "word" address appropriate
	 * for indexing a word-oriented device
	 */
	if (info->reg_ndcr & NDCR_DWIDTH_M)
		column /= 2;

	/*
	 * There may be different NAND chip hooked to
	 * different chip select, so check whether
	 * chip select has been changed, if yes, reset the timing
	 */
	if (info->cs != host->cs) {
		info->cs = host->cs;
		nand_writel(info, NDTR0CS0, info->ndtr0cs0);
		nand_writel(info, NDTR1CS0, info->ndtr1cs0);
	}

	/* Select the extended command for the first command */
	switch (command) {
	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
		ext_cmd_type = EXT_CMD_TYPE_MONO;
		break;
	case NAND_CMD_SEQIN:
		ext_cmd_type = EXT_CMD_TYPE_DISPATCH;
		break;
	case NAND_CMD_PAGEPROG:
		ext_cmd_type = EXT_CMD_TYPE_NAKED_RW;
		break;
	default:
		ext_cmd_type = 0;
		break;
	}

	prepare_start_command(info, command);

	/*
	 * Prepare the "is ready" completion before starting a command
	 * transaction sequence. If the command is not executed the
	 * completion will be completed, see below.
	 *
	 * We can do that inside the loop because the command variable
	 * is invariant and thus so is the exec_cmd.
	 */
	info->need_wait = 1;
	init_completion(&info->dev_ready);
	do {
		info->state = STATE_PREPARED;
		exec_cmd = prepare_set_command(info, command, ext_cmd_type,
					       column, page_addr);
		if (!exec_cmd) {
			info->need_wait = 0;
			complete(&info->dev_ready);
			break;
		}

		init_completion(&info->cmd_complete);
		pxa3xx_nand_start(info);

		ret = wait_for_completion_timeout(&info->cmd_complete,
				CHIP_DELAY_TIMEOUT);
		if (!ret) {
			dev_err(&info->pdev->dev, "Wait time out!!!\n");
			/* Stop State Machine for next command cycle */
			pxa3xx_nand_stop(info);
			break;
		}

		/* Check if the sequence is complete */
		if (info->data_size == 0 && command != NAND_CMD_PAGEPROG)
			break;

		/*
		 * After a splitted program command sequence has issued
		 * the command dispatch, the command sequence is complete.
		 */
		if (info->data_size == 0 &&
		    command == NAND_CMD_PAGEPROG &&
		    ext_cmd_type == EXT_CMD_TYPE_DISPATCH)
			break;

		if (command == NAND_CMD_READ0 || command == NAND_CMD_READOOB) {
			/* Last read: issue a 'last naked read' */
			if (info->data_size == info->chunk_size)
				ext_cmd_type = EXT_CMD_TYPE_LAST_RW;
			else
				ext_cmd_type = EXT_CMD_TYPE_NAKED_RW;

		/*
		 * If a splitted program command has no more data to transfer,
		 * the command dispatch must be issued to complete.
		 */
		} else if (command == NAND_CMD_PAGEPROG &&
			   info->data_size == 0) {
				ext_cmd_type = EXT_CMD_TYPE_DISPATCH;
		}
	} while (1);

	info->state = STATE_IDLE;
}

static int pxa3xx_nand_write_page_hwecc(struct mtd_info *mtd,
		struct nand_chip *chip, const uint8_t *buf, int oob_required)
{
	chip->write_buf(mtd, buf, mtd->writesize);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

static int pxa3xx_nand_read_page_hwecc(struct mtd_info *mtd,
		struct nand_chip *chip, uint8_t *buf, int oob_required,
		int page)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;

	chip->read_buf(mtd, buf, mtd->writesize);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	if (info->retcode == ERR_CORERR && info->use_ecc) {
		mtd->ecc_stats.corrected += info->ecc_err_cnt;

	} else if (info->retcode == ERR_UNCORERR) {
		/*
		 * for blank page (all 0xff), HW will calculate its ECC as
		 * 0, which is different from the ECC information within
		 * OOB, ignore such uncorrectable errors
		 */
		if (is_buf_blank(buf, mtd->writesize))
			info->retcode = ERR_NONE;
		else
			mtd->ecc_stats.failed++;
	}

	return info->max_bitflips;
}

static uint8_t pxa3xx_nand_read_byte(struct mtd_info *mtd)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	char retval = 0xFF;

	if (info->buf_start < info->buf_count)
		/* Has just send a new command? */
		retval = info->data_buff[info->buf_start++];

	return retval;
}

static u16 pxa3xx_nand_read_word(struct mtd_info *mtd)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	u16 retval = 0xFFFF;

	if (!(info->buf_start & 0x01) && info->buf_start < info->buf_count) {
		retval = *((u16 *)(info->data_buff+info->buf_start));
		info->buf_start += 2;
	}
	return retval;
}

static void pxa3xx_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	int real_len = min_t(size_t, len, info->buf_count - info->buf_start);

	memcpy(buf, info->data_buff + info->buf_start, real_len);
	info->buf_start += real_len;
}

static void pxa3xx_nand_write_buf(struct mtd_info *mtd,
		const uint8_t *buf, int len)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	int real_len = min_t(size_t, len, info->buf_count - info->buf_start);

	memcpy(info->data_buff + info->buf_start, buf, real_len);
	info->buf_start += real_len;
}

static void pxa3xx_nand_select_chip(struct mtd_info *mtd, int chip)
{
	return;
}

static int pxa3xx_nand_waitfunc(struct mtd_info *mtd, struct nand_chip *this)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	int ret;

	if (info->need_wait) {
		ret = wait_for_completion_timeout(&info->dev_ready,
				CHIP_DELAY_TIMEOUT);
		info->need_wait = 0;
		if (!ret) {
			dev_err(&info->pdev->dev, "Ready time out!!!\n");
			return NAND_STATUS_FAIL;
		}
	}

	/* pxa3xx_nand_send_command has waited for command complete */
	if (this->state == FL_WRITING || this->state == FL_ERASING) {
		if (info->retcode == ERR_NONE)
			return 0;
		else
			return NAND_STATUS_FAIL;
	}

	return NAND_STATUS_READY;
}

static int pxa3xx_nand_config_flash(struct pxa3xx_nand_info *info,
				    const struct pxa3xx_nand_flash *f)
{
	struct platform_device *pdev = info->pdev;
	struct pxa3xx_nand_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct pxa3xx_nand_host *host = info->host[info->cs];
	uint32_t ndcr = 0x0; /* enable all interrupts */

	if (f->page_size != 2048 && f->page_size != 512) {
		dev_err(&pdev->dev, "Current only support 2048 and 512 size\n");
		return -EINVAL;
	}

	if (f->flash_width != 16 && f->flash_width != 8) {
		dev_err(&pdev->dev, "Only support 8bit and 16 bit!\n");
		return -EINVAL;
	}

	/* calculate flash information */
	host->read_id_bytes = (f->page_size == 2048) ? 4 : 2;

	/* calculate addressing information */
	host->col_addr_cycles = (f->page_size == 2048) ? 2 : 1;

	if (f->num_blocks * f->page_per_block > 65536)
		host->row_addr_cycles = 3;
	else
		host->row_addr_cycles = 2;

	ndcr |= (pdata->enable_arbiter) ? NDCR_ND_ARB_EN : 0;
	ndcr |= (host->col_addr_cycles == 2) ? NDCR_RA_START : 0;
	ndcr |= (f->page_per_block == 64) ? NDCR_PG_PER_BLK : 0;
	ndcr |= (f->page_size == 2048) ? NDCR_PAGE_SZ : 0;
	ndcr |= (f->flash_width == 16) ? NDCR_DWIDTH_M : 0;
	ndcr |= (f->dfc_width == 16) ? NDCR_DWIDTH_C : 0;

	ndcr |= NDCR_RD_ID_CNT(host->read_id_bytes);
	ndcr |= NDCR_SPARE_EN; /* enable spare by default */

	info->reg_ndcr = ndcr;

	pxa3xx_nand_set_timing(host, f->timing);
	return 0;
}

static int pxa3xx_nand_detect_config(struct pxa3xx_nand_info *info)
{
	/*
	 * We set 0 by hard coding here, for we don't support keep_config
	 * when there is more than one chip attached to the controller
	 */
	struct pxa3xx_nand_host *host = info->host[0];
	uint32_t ndcr = nand_readl(info, NDCR);

	if (ndcr & NDCR_PAGE_SZ) {
		/* Controller's FIFO size */
		info->chunk_size = 2048;
		host->read_id_bytes = 4;
	} else {
		info->chunk_size = 512;
		host->read_id_bytes = 2;
	}

	/* Set an initial chunk size */
	info->reg_ndcr = ndcr & ~NDCR_INT_MASK;
	info->ndtr0cs0 = nand_readl(info, NDTR0CS0);
	info->ndtr1cs0 = nand_readl(info, NDTR1CS0);
	return 0;
}

#ifdef ARCH_HAS_DMA
static int pxa3xx_nand_init_buff(struct pxa3xx_nand_info *info)
{
	struct platform_device *pdev = info->pdev;
	int data_desc_offset = info->buf_size - sizeof(struct pxa_dma_desc);

	if (use_dma == 0) {
		info->data_buff = kmalloc(info->buf_size, GFP_KERNEL);
		if (info->data_buff == NULL)
			return -ENOMEM;
		return 0;
	}

	info->data_buff = dma_alloc_coherent(&pdev->dev, info->buf_size,
				&info->data_buff_phys, GFP_KERNEL);
	if (info->data_buff == NULL) {
		dev_err(&pdev->dev, "failed to allocate dma buffer\n");
		return -ENOMEM;
	}

	info->data_desc = (void *)info->data_buff + data_desc_offset;
	info->data_desc_addr = info->data_buff_phys + data_desc_offset;

	info->data_dma_ch = pxa_request_dma("nand-data", DMA_PRIO_LOW,
				pxa3xx_nand_data_dma_irq, info);
	if (info->data_dma_ch < 0) {
		dev_err(&pdev->dev, "failed to request data dma\n");
		dma_free_coherent(&pdev->dev, info->buf_size,
				info->data_buff, info->data_buff_phys);
		return info->data_dma_ch;
	}

	/*
	 * Now that DMA buffers are allocated we turn on
	 * DMA proper for I/O operations.
	 */
	info->use_dma = 1;
	return 0;
}

static void pxa3xx_nand_free_buff(struct pxa3xx_nand_info *info)
{
	struct platform_device *pdev = info->pdev;
	if (info->use_dma) {
		pxa_free_dma(info->data_dma_ch);
		dma_free_coherent(&pdev->dev, info->buf_size,
				  info->data_buff, info->data_buff_phys);
	} else {
		kfree(info->data_buff);
	}
}
#else
static int pxa3xx_nand_init_buff(struct pxa3xx_nand_info *info)
{
	info->data_buff = kmalloc(info->buf_size, GFP_KERNEL);
	if (info->data_buff == NULL)
		return -ENOMEM;
	return 0;
}

static void pxa3xx_nand_free_buff(struct pxa3xx_nand_info *info)
{
	kfree(info->data_buff);
}
#endif

static int pxa3xx_nand_sensing(struct pxa3xx_nand_info *info)
{
	struct mtd_info *mtd;
	struct nand_chip *chip;
	int ret;

	mtd = info->host[info->cs]->mtd;
	chip = mtd->priv;

	/* use the common timing to make a try */
	ret = pxa3xx_nand_config_flash(info, &builtin_flash_types[0]);
	if (ret)
		return ret;

	chip->cmdfunc(mtd, NAND_CMD_RESET, 0, 0);
	ret = chip->waitfunc(mtd, chip);
	if (ret & NAND_STATUS_FAIL)
		return -ENODEV;

	return 0;
}

static int pxa_ecc_init(struct pxa3xx_nand_info *info,
			struct nand_ecc_ctrl *ecc,
			int strength, int ecc_stepsize, int page_size)
{
	if (strength == 1 && ecc_stepsize == 512 && page_size == 2048) {
		info->chunk_size = 2048;
		info->spare_size = 40;
		info->ecc_size = 24;
		ecc->mode = NAND_ECC_HW;
		ecc->size = 512;
		ecc->strength = 1;
		return 1;

	} else if (strength == 1 && ecc_stepsize == 512 && page_size == 512) {
		info->chunk_size = 512;
		info->spare_size = 8;
		info->ecc_size = 8;
		ecc->mode = NAND_ECC_HW;
		ecc->size = 512;
		ecc->strength = 1;
		return 1;

	/*
	 * Required ECC: 4-bit correction per 512 bytes
	 * Select: 16-bit correction per 2048 bytes
	 */
	} else if (strength == 4 && ecc_stepsize == 512 && page_size == 2048) {
		info->ecc_bch = 1;
		info->chunk_size = 2048;
		info->spare_size = 32;
		info->ecc_size = 32;
		ecc->mode = NAND_ECC_HW;
		ecc->size = info->chunk_size;
		ecc->layout = &ecc_layout_2KB_bch4bit;
		ecc->strength = 16;
		return 1;

	} else if (strength == 4 && ecc_stepsize == 512 && page_size == 4096) {
		info->ecc_bch = 1;
		info->chunk_size = 2048;
		info->spare_size = 32;
		info->ecc_size = 32;
		ecc->mode = NAND_ECC_HW;
		ecc->size = info->chunk_size;
		ecc->layout = &ecc_layout_4KB_bch4bit;
		ecc->strength = 16;
		return 1;

	/*
	 * Required ECC: 8-bit correction per 512 bytes
	 * Select: 16-bit correction per 1024 bytes
	 */
	} else if (strength == 8 && ecc_stepsize == 512 && page_size == 4096) {
		info->ecc_bch = 1;
		info->chunk_size = 1024;
		info->spare_size = 0;
		info->ecc_size = 32;
		ecc->mode = NAND_ECC_HW;
		ecc->size = info->chunk_size;
		ecc->layout = &ecc_layout_4KB_bch8bit;
		ecc->strength = 16;
		return 1;
	}
	return 0;
}

static int pxa3xx_nand_scan(struct mtd_info *mtd)
{
	struct pxa3xx_nand_host *host = mtd->priv;
	struct pxa3xx_nand_info *info = host->info_data;
	struct platform_device *pdev = info->pdev;
	struct pxa3xx_nand_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct nand_flash_dev pxa3xx_flash_ids[2], *def = NULL;
	const struct pxa3xx_nand_flash *f = NULL;
	struct nand_chip *chip = mtd->priv;
	uint32_t id = -1;
	uint64_t chipsize;
	int i, ret, num;
	uint16_t ecc_strength, ecc_step;

	if (pdata->keep_config && !pxa3xx_nand_detect_config(info))
		goto KEEP_CONFIG;

	ret = pxa3xx_nand_sensing(info);
	if (ret) {
		dev_info(&info->pdev->dev, "There is no chip on cs %d!\n",
			 info->cs);

		return ret;
	}

	chip->cmdfunc(mtd, NAND_CMD_READID, 0, 0);
	id = *((uint16_t *)(info->data_buff));
	if (id != 0)
		dev_info(&info->pdev->dev, "Detect a flash id %x\n", id);
	else {
		dev_warn(&info->pdev->dev,
			 "Read out ID 0, potential timing set wrong!!\n");

		return -EINVAL;
	}

	num = ARRAY_SIZE(builtin_flash_types) + pdata->num_flash - 1;
	for (i = 0; i < num; i++) {
		if (i < pdata->num_flash)
			f = pdata->flash + i;
		else
			f = &builtin_flash_types[i - pdata->num_flash + 1];

		/* find the chip in default list */
		if (f->chip_id == id)
			break;
	}

	if (i >= (ARRAY_SIZE(builtin_flash_types) + pdata->num_flash - 1)) {
		dev_err(&info->pdev->dev, "ERROR!! flash not defined!!!\n");

		return -EINVAL;
	}

	ret = pxa3xx_nand_config_flash(info, f);
	if (ret) {
		dev_err(&info->pdev->dev, "ERROR! Configure failed\n");
		return ret;
	}

	pxa3xx_flash_ids[0].name = f->name;
	pxa3xx_flash_ids[0].dev_id = (f->chip_id >> 8) & 0xffff;
	pxa3xx_flash_ids[0].pagesize = f->page_size;
	chipsize = (uint64_t)f->num_blocks * f->page_per_block * f->page_size;
	pxa3xx_flash_ids[0].chipsize = chipsize >> 20;
	pxa3xx_flash_ids[0].erasesize = f->page_size * f->page_per_block;
	if (f->flash_width == 16)
		pxa3xx_flash_ids[0].options = NAND_BUSWIDTH_16;
	pxa3xx_flash_ids[1].name = NULL;
	def = pxa3xx_flash_ids;
KEEP_CONFIG:
	if (info->reg_ndcr & NDCR_DWIDTH_M)
		chip->options |= NAND_BUSWIDTH_16;

	/* Device detection must be done with ECC disabled */
	if (info->variant == PXA3XX_NAND_VARIANT_ARMADA370)
		nand_writel(info, NDECCCTRL, 0x0);

	if (nand_scan_ident(mtd, 1, def))
		return -ENODEV;

	if (pdata->flash_bbt) {
		/*
		 * We'll use a bad block table stored in-flash and don't
		 * allow writing the bad block marker to the flash.
		 */
		chip->bbt_options |= NAND_BBT_USE_FLASH |
				     NAND_BBT_NO_OOB_BBM;
		chip->bbt_td = &bbt_main_descr;
		chip->bbt_md = &bbt_mirror_descr;
	}

	/*
	 * If the page size is bigger than the FIFO size, let's check
	 * we are given the right variant and then switch to the extended
	 * (aka splitted) command handling,
	 */
	if (mtd->writesize > PAGE_CHUNK_SIZE) {
		if (info->variant == PXA3XX_NAND_VARIANT_ARMADA370) {
			chip->cmdfunc = nand_cmdfunc_extended;
		} else {
			dev_err(&info->pdev->dev,
				"unsupported page size on this variant\n");
			return -ENODEV;
		}
	}

	ecc_strength = chip->ecc_strength_ds;
	ecc_step = chip->ecc_step_ds;

	/* Set default ECC strength requirements on non-ONFI devices */
	if (ecc_strength < 1 && ecc_step < 1) {
		ecc_strength = 1;
		ecc_step = 512;
	}

	ret = pxa_ecc_init(info, &chip->ecc, ecc_strength,
			   ecc_step, mtd->writesize);
	if (!ret) {
		dev_err(&info->pdev->dev,
			"ECC strength %d at page size %d is not supported\n",
			chip->ecc_strength_ds, mtd->writesize);
		return -ENODEV;
	}

	/* calculate addressing information */
	if (mtd->writesize >= 2048)
		host->col_addr_cycles = 2;
	else
		host->col_addr_cycles = 1;

	/* release the initial buffer */
	kfree(info->data_buff);

	/* allocate the real data + oob buffer */
	info->buf_size = mtd->writesize + mtd->oobsize;
	ret = pxa3xx_nand_init_buff(info);
	if (ret)
		return ret;
	info->oob_buff = info->data_buff + mtd->writesize;

	if ((mtd->size >> chip->page_shift) > 65536)
		host->row_addr_cycles = 3;
	else
		host->row_addr_cycles = 2;
	return nand_scan_tail(mtd);
}

static int alloc_nand_resource(struct platform_device *pdev)
{
	struct pxa3xx_nand_platform_data *pdata;
	struct pxa3xx_nand_info *info;
	struct pxa3xx_nand_host *host;
	struct nand_chip *chip = NULL;
	struct mtd_info *mtd;
	struct resource *r;
	int ret, irq, cs;

	pdata = dev_get_platdata(&pdev->dev);
	info = devm_kzalloc(&pdev->dev, sizeof(*info) + (sizeof(*mtd) +
			    sizeof(*host)) * pdata->num_cs, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pdev = pdev;
	info->variant = pxa3xx_nand_get_variant(pdev);
	for (cs = 0; cs < pdata->num_cs; cs++) {
		mtd = (struct mtd_info *)((unsigned int)&info[1] +
		      (sizeof(*mtd) + sizeof(*host)) * cs);
		chip = (struct nand_chip *)(&mtd[1]);
		host = (struct pxa3xx_nand_host *)chip;
		info->host[cs] = host;
		host->mtd = mtd;
		host->cs = cs;
		host->info_data = info;
		mtd->priv = host;
		mtd->owner = THIS_MODULE;

		chip->ecc.read_page	= pxa3xx_nand_read_page_hwecc;
		chip->ecc.write_page	= pxa3xx_nand_write_page_hwecc;
		chip->controller        = &info->controller;
		chip->waitfunc		= pxa3xx_nand_waitfunc;
		chip->select_chip	= pxa3xx_nand_select_chip;
		chip->read_word		= pxa3xx_nand_read_word;
		chip->read_byte		= pxa3xx_nand_read_byte;
		chip->read_buf		= pxa3xx_nand_read_buf;
		chip->write_buf		= pxa3xx_nand_write_buf;
		chip->options		|= NAND_NO_SUBPAGE_WRITE;
		chip->cmdfunc		= nand_cmdfunc;
	}

	spin_lock_init(&chip->controller->lock);
	init_waitqueue_head(&chip->controller->wq);
	info->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed to get nand clock\n");
		return PTR_ERR(info->clk);
	}
	ret = clk_prepare_enable(info->clk);
	if (ret < 0)
		return ret;

	if (use_dma) {
		/*
		 * This is a dirty hack to make this driver work from
		 * devicetree bindings. It can be removed once we have
		 * a prober DMA controller framework for DT.
		 */
		if (pdev->dev.of_node &&
		    of_machine_is_compatible("marvell,pxa3xx")) {
			info->drcmr_dat = 97;
			info->drcmr_cmd = 99;
		} else {
			r = platform_get_resource(pdev, IORESOURCE_DMA, 0);
			if (r == NULL) {
				dev_err(&pdev->dev,
					"no resource defined for data DMA\n");
				ret = -ENXIO;
				goto fail_disable_clk;
			}
			info->drcmr_dat = r->start;

			r = platform_get_resource(pdev, IORESOURCE_DMA, 1);
			if (r == NULL) {
				dev_err(&pdev->dev,
					"no resource defined for cmd DMA\n");
				ret = -ENXIO;
				goto fail_disable_clk;
			}
			info->drcmr_cmd = r->start;
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		ret = -ENXIO;
		goto fail_disable_clk;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->mmio_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(info->mmio_base)) {
		ret = PTR_ERR(info->mmio_base);
		goto fail_disable_clk;
	}
	info->mmio_phys = r->start;

	/* Allocate a buffer to allow flash detection */
	info->buf_size = INIT_BUFFER_SIZE;
	info->data_buff = kmalloc(info->buf_size, GFP_KERNEL);
	if (info->data_buff == NULL) {
		ret = -ENOMEM;
		goto fail_disable_clk;
	}

	/* initialize all interrupts to be disabled */
	disable_int(info, NDSR_MASK);

	ret = request_irq(irq, pxa3xx_nand_irq, 0, pdev->name, info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		goto fail_free_buf;
	}

	platform_set_drvdata(pdev, info);

	return 0;

fail_free_buf:
	free_irq(irq, info);
	kfree(info->data_buff);
fail_disable_clk:
	clk_disable_unprepare(info->clk);
	return ret;
}

static int pxa3xx_nand_remove(struct platform_device *pdev)
{
	struct pxa3xx_nand_info *info = platform_get_drvdata(pdev);
	struct pxa3xx_nand_platform_data *pdata;
	int irq, cs;

	if (!info)
		return 0;

	pdata = dev_get_platdata(&pdev->dev);

	irq = platform_get_irq(pdev, 0);
	if (irq >= 0)
		free_irq(irq, info);
	pxa3xx_nand_free_buff(info);

	clk_disable_unprepare(info->clk);

	for (cs = 0; cs < pdata->num_cs; cs++)
		nand_release(info->host[cs]->mtd);
	return 0;
}

static int pxa3xx_nand_probe_dt(struct platform_device *pdev)
{
	struct pxa3xx_nand_platform_data *pdata;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
			of_match_device(pxa3xx_nand_dt_ids, &pdev->dev);

	if (!of_id)
		return 0;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	if (of_get_property(np, "marvell,nand-enable-arbiter", NULL))
		pdata->enable_arbiter = 1;
	if (of_get_property(np, "marvell,nand-keep-config", NULL))
		pdata->keep_config = 1;
	of_property_read_u32(np, "num-cs", &pdata->num_cs);
	pdata->flash_bbt = of_get_nand_on_flash_bbt(np);

	pdev->dev.platform_data = pdata;

	return 0;
}

static int pxa3xx_nand_probe(struct platform_device *pdev)
{
	struct pxa3xx_nand_platform_data *pdata;
	struct mtd_part_parser_data ppdata = {};
	struct pxa3xx_nand_info *info;
	int ret, cs, probe_success;

#ifndef ARCH_HAS_DMA
	if (use_dma) {
		use_dma = 0;
		dev_warn(&pdev->dev,
			 "This platform can't do DMA on this device\n");
	}
#endif
	ret = pxa3xx_nand_probe_dt(pdev);
	if (ret)
		return ret;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -ENODEV;
	}

	ret = alloc_nand_resource(pdev);
	if (ret) {
		dev_err(&pdev->dev, "alloc nand resource failed\n");
		return ret;
	}

	info = platform_get_drvdata(pdev);
	probe_success = 0;
	for (cs = 0; cs < pdata->num_cs; cs++) {
		struct mtd_info *mtd = info->host[cs]->mtd;

		/*
		 * The mtd name matches the one used in 'mtdparts' kernel
		 * parameter. This name cannot be changed or otherwise
		 * user's mtd partitions configuration would get broken.
		 */
		mtd->name = "pxa3xx_nand-0";
		info->cs = cs;
		ret = pxa3xx_nand_scan(mtd);
		if (ret) {
			dev_warn(&pdev->dev, "failed to scan nand at cs %d\n",
				cs);
			continue;
		}

		ppdata.of_node = pdev->dev.of_node;
		ret = mtd_device_parse_register(mtd, NULL,
						&ppdata, pdata->parts[cs],
						pdata->nr_parts[cs]);
		if (!ret)
			probe_success = 1;
	}

	if (!probe_success) {
		pxa3xx_nand_remove(pdev);
		return -ENODEV;
	}

	return 0;
}

#ifdef CONFIG_PM
static int pxa3xx_nand_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pxa3xx_nand_info *info = platform_get_drvdata(pdev);
	struct pxa3xx_nand_platform_data *pdata;
	struct mtd_info *mtd;
	int cs;

	pdata = dev_get_platdata(&pdev->dev);
	if (info->state) {
		dev_err(&pdev->dev, "driver busy, state = %d\n", info->state);
		return -EAGAIN;
	}

	for (cs = 0; cs < pdata->num_cs; cs++) {
		mtd = info->host[cs]->mtd;
		mtd_suspend(mtd);
	}

	return 0;
}

static int pxa3xx_nand_resume(struct platform_device *pdev)
{
	struct pxa3xx_nand_info *info = platform_get_drvdata(pdev);
	struct pxa3xx_nand_platform_data *pdata;
	struct mtd_info *mtd;
	int cs;

	pdata = dev_get_platdata(&pdev->dev);
	/* We don't want to handle interrupt without calling mtd routine */
	disable_int(info, NDCR_INT_MASK);

	/*
	 * Directly set the chip select to a invalid value,
	 * then the driver would reset the timing according
	 * to current chip select at the beginning of cmdfunc
	 */
	info->cs = 0xff;

	/*
	 * As the spec says, the NDSR would be updated to 0x1800 when
	 * doing the nand_clk disable/enable.
	 * To prevent it damaging state machine of the driver, clear
	 * all status before resume
	 */
	nand_writel(info, NDSR, NDSR_MASK);
	for (cs = 0; cs < pdata->num_cs; cs++) {
		mtd = info->host[cs]->mtd;
		mtd_resume(mtd);
	}

	return 0;
}
#else
#define pxa3xx_nand_suspend	NULL
#define pxa3xx_nand_resume	NULL
#endif

static struct platform_driver pxa3xx_nand_driver = {
	.driver = {
		.name	= "pxa3xx-nand",
		.of_match_table = pxa3xx_nand_dt_ids,
	},
	.probe		= pxa3xx_nand_probe,
	.remove		= pxa3xx_nand_remove,
	.suspend	= pxa3xx_nand_suspend,
	.resume		= pxa3xx_nand_resume,
};

module_platform_driver(pxa3xx_nand_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PXA3xx NAND controller driver");
