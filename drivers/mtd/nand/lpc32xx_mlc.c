/*
 * Driver for NAND MLC Controller in LPC32xx
 *
 * Author: Roland Stigge <stigge@antcom.de>
 *
 * Copyright © 2011 WORK Microwave GmbH
 * Copyright © 2011, 2012 Roland Stigge
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * NAND Flash Controller Operation:
 * - Read: Auto Decode
 * - Write: Auto Encode
 * - Tested Page Sizes: 2048, 4096
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mtd/lpc32xx_mlc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mtd/nand_ecc.h>

#define DRV_NAME "lpc32xx_mlc"

/**********************************************************************
* MLC NAND controller register offsets
**********************************************************************/

#define MLC_BUFF(x)			(x + 0x00000)
#define MLC_DATA(x)			(x + 0x08000)
#define MLC_CMD(x)			(x + 0x10000)
#define MLC_ADDR(x)			(x + 0x10004)
#define MLC_ECC_ENC_REG(x)		(x + 0x10008)
#define MLC_ECC_DEC_REG(x)		(x + 0x1000C)
#define MLC_ECC_AUTO_ENC_REG(x)		(x + 0x10010)
#define MLC_ECC_AUTO_DEC_REG(x)		(x + 0x10014)
#define MLC_RPR(x)			(x + 0x10018)
#define MLC_WPR(x)			(x + 0x1001C)
#define MLC_RUBP(x)			(x + 0x10020)
#define MLC_ROBP(x)			(x + 0x10024)
#define MLC_SW_WP_ADD_LOW(x)		(x + 0x10028)
#define MLC_SW_WP_ADD_HIG(x)		(x + 0x1002C)
#define MLC_ICR(x)			(x + 0x10030)
#define MLC_TIME_REG(x)			(x + 0x10034)
#define MLC_IRQ_MR(x)			(x + 0x10038)
#define MLC_IRQ_SR(x)			(x + 0x1003C)
#define MLC_LOCK_PR(x)			(x + 0x10044)
#define MLC_ISR(x)			(x + 0x10048)
#define MLC_CEH(x)			(x + 0x1004C)

/**********************************************************************
* MLC_CMD bit definitions
**********************************************************************/
#define MLCCMD_RESET			0xFF

/**********************************************************************
* MLC_ICR bit definitions
**********************************************************************/
#define MLCICR_WPROT			(1 << 3)
#define MLCICR_LARGEBLOCK		(1 << 2)
#define MLCICR_LONGADDR			(1 << 1)
#define MLCICR_16BIT			(1 << 0)  /* unsupported by LPC32x0! */

/**********************************************************************
* MLC_TIME_REG bit definitions
**********************************************************************/
#define MLCTIMEREG_TCEA_DELAY(n)	(((n) & 0x03) << 24)
#define MLCTIMEREG_BUSY_DELAY(n)	(((n) & 0x1F) << 19)
#define MLCTIMEREG_NAND_TA(n)		(((n) & 0x07) << 16)
#define MLCTIMEREG_RD_HIGH(n)		(((n) & 0x0F) << 12)
#define MLCTIMEREG_RD_LOW(n)		(((n) & 0x0F) << 8)
#define MLCTIMEREG_WR_HIGH(n)		(((n) & 0x0F) << 4)
#define MLCTIMEREG_WR_LOW(n)		(((n) & 0x0F) << 0)

/**********************************************************************
* MLC_IRQ_MR and MLC_IRQ_SR bit definitions
**********************************************************************/
#define MLCIRQ_NAND_READY		(1 << 5)
#define MLCIRQ_CONTROLLER_READY		(1 << 4)
#define MLCIRQ_DECODE_FAILURE		(1 << 3)
#define MLCIRQ_DECODE_ERROR		(1 << 2)
#define MLCIRQ_ECC_READY		(1 << 1)
#define MLCIRQ_WRPROT_FAULT		(1 << 0)

/**********************************************************************
* MLC_LOCK_PR bit definitions
**********************************************************************/
#define MLCLOCKPR_MAGIC			0xA25E

/**********************************************************************
* MLC_ISR bit definitions
**********************************************************************/
#define MLCISR_DECODER_FAILURE		(1 << 6)
#define MLCISR_ERRORS			((1 << 4) | (1 << 5))
#define MLCISR_ERRORS_DETECTED		(1 << 3)
#define MLCISR_ECC_READY		(1 << 2)
#define MLCISR_CONTROLLER_READY		(1 << 1)
#define MLCISR_NAND_READY		(1 << 0)

/**********************************************************************
* MLC_CEH bit definitions
**********************************************************************/
#define MLCCEH_NORMAL			(1 << 0)

struct lpc32xx_nand_cfg_mlc {
	uint32_t tcea_delay;
	uint32_t busy_delay;
	uint32_t nand_ta;
	uint32_t rd_high;
	uint32_t rd_low;
	uint32_t wr_high;
	uint32_t wr_low;
	int wp_gpio;
	struct mtd_partition *parts;
	unsigned num_parts;
};

static int lpc32xx_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);

	if (section >= nand_chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = ((section + 1) * 16) - nand_chip->ecc.bytes;
	oobregion->length = nand_chip->ecc.bytes;

	return 0;
}

static int lpc32xx_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);

	if (section >= nand_chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = 16 * section;
	oobregion->length = 16 - nand_chip->ecc.bytes;

	return 0;
}

static const struct mtd_ooblayout_ops lpc32xx_ooblayout_ops = {
	.ecc = lpc32xx_ooblayout_ecc,
	.free = lpc32xx_ooblayout_free,
};

static struct nand_bbt_descr lpc32xx_nand_bbt = {
	.options = NAND_BBT_ABSPAGE | NAND_BBT_2BIT | NAND_BBT_NO_OOB |
		   NAND_BBT_WRITE,
	.pages = { 524224, 0, 0, 0, 0, 0, 0, 0 },
};

static struct nand_bbt_descr lpc32xx_nand_bbt_mirror = {
	.options = NAND_BBT_ABSPAGE | NAND_BBT_2BIT | NAND_BBT_NO_OOB |
		   NAND_BBT_WRITE,
	.pages = { 524160, 0, 0, 0, 0, 0, 0, 0 },
};

struct lpc32xx_nand_host {
	struct nand_chip	nand_chip;
	struct lpc32xx_mlc_platform_data *pdata;
	struct clk		*clk;
	void __iomem		*io_base;
	int			irq;
	struct lpc32xx_nand_cfg_mlc	*ncfg;
	struct completion       comp_nand;
	struct completion       comp_controller;
	uint32_t llptr;
	/*
	 * Physical addresses of ECC buffer, DMA data buffers, OOB data buffer
	 */
	dma_addr_t		oob_buf_phy;
	/*
	 * Virtual addresses of ECC buffer, DMA data buffers, OOB data buffer
	 */
	uint8_t			*oob_buf;
	/* Physical address of DMA base address */
	dma_addr_t		io_base_phy;

	struct completion	comp_dma;
	struct dma_chan		*dma_chan;
	struct dma_slave_config	dma_slave_config;
	struct scatterlist	sgl;
	uint8_t			*dma_buf;
	uint8_t			*dummy_buf;
	int			mlcsubpages; /* number of 512bytes-subpages */
};

/*
 * Activate/Deactivate DMA Operation:
 *
 * Using the PL080 DMA Controller for transferring the 512 byte subpages
 * instead of doing readl() / writel() in a loop slows it down significantly.
 * Measurements via getnstimeofday() upon 512 byte subpage reads reveal:
 *
 * - readl() of 128 x 32 bits in a loop: ~20us
 * - DMA read of 512 bytes (32 bit, 4...128 words bursts): ~60us
 * - DMA read of 512 bytes (32 bit, no bursts): ~100us
 *
 * This applies to the transfer itself. In the DMA case: only the
 * wait_for_completion() (DMA setup _not_ included).
 *
 * Note that the 512 bytes subpage transfer is done directly from/to a
 * FIFO/buffer inside the NAND controller. Most of the time (~400-800us for a
 * 2048 bytes page) is spent waiting for the NAND IRQ, anyway. (The NAND
 * controller transferring data between its internal buffer to/from the NAND
 * chip.)
 *
 * Therefore, using the PL080 DMA is disabled by default, for now.
 *
 */
static int use_dma;

static void lpc32xx_nand_setup(struct lpc32xx_nand_host *host)
{
	uint32_t clkrate, tmp;

	/* Reset MLC controller */
	writel(MLCCMD_RESET, MLC_CMD(host->io_base));
	udelay(1000);

	/* Get base clock for MLC block */
	clkrate = clk_get_rate(host->clk);
	if (clkrate == 0)
		clkrate = 104000000;

	/* Unlock MLC_ICR
	 * (among others, will be locked again automatically) */
	writew(MLCLOCKPR_MAGIC, MLC_LOCK_PR(host->io_base));

	/* Configure MLC Controller: Large Block, 5 Byte Address */
	tmp = MLCICR_LARGEBLOCK | MLCICR_LONGADDR;
	writel(tmp, MLC_ICR(host->io_base));

	/* Unlock MLC_TIME_REG
	 * (among others, will be locked again automatically) */
	writew(MLCLOCKPR_MAGIC, MLC_LOCK_PR(host->io_base));

	/* Compute clock setup values, see LPC and NAND manual */
	tmp = 0;
	tmp |= MLCTIMEREG_TCEA_DELAY(clkrate / host->ncfg->tcea_delay + 1);
	tmp |= MLCTIMEREG_BUSY_DELAY(clkrate / host->ncfg->busy_delay + 1);
	tmp |= MLCTIMEREG_NAND_TA(clkrate / host->ncfg->nand_ta + 1);
	tmp |= MLCTIMEREG_RD_HIGH(clkrate / host->ncfg->rd_high + 1);
	tmp |= MLCTIMEREG_RD_LOW(clkrate / host->ncfg->rd_low);
	tmp |= MLCTIMEREG_WR_HIGH(clkrate / host->ncfg->wr_high + 1);
	tmp |= MLCTIMEREG_WR_LOW(clkrate / host->ncfg->wr_low);
	writel(tmp, MLC_TIME_REG(host->io_base));

	/* Enable IRQ for CONTROLLER_READY and NAND_READY */
	writeb(MLCIRQ_CONTROLLER_READY | MLCIRQ_NAND_READY,
			MLC_IRQ_MR(host->io_base));

	/* Normal nCE operation: nCE controlled by controller */
	writel(MLCCEH_NORMAL, MLC_CEH(host->io_base));
}

/*
 * Hardware specific access to control lines
 */
static void lpc32xx_nand_cmd_ctrl(struct mtd_info *mtd, int cmd,
				  unsigned int ctrl)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct lpc32xx_nand_host *host = nand_get_controller_data(nand_chip);

	if (cmd != NAND_CMD_NONE) {
		if (ctrl & NAND_CLE)
			writel(cmd, MLC_CMD(host->io_base));
		else
			writel(cmd, MLC_ADDR(host->io_base));
	}
}

/*
 * Read Device Ready (NAND device _and_ controller ready)
 */
static int lpc32xx_nand_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct lpc32xx_nand_host *host = nand_get_controller_data(nand_chip);

	if ((readb(MLC_ISR(host->io_base)) &
	     (MLCISR_CONTROLLER_READY | MLCISR_NAND_READY)) ==
	    (MLCISR_CONTROLLER_READY | MLCISR_NAND_READY))
		return  1;

	return 0;
}

static irqreturn_t lpc3xxx_nand_irq(int irq, struct lpc32xx_nand_host *host)
{
	uint8_t sr;

	/* Clear interrupt flag by reading status */
	sr = readb(MLC_IRQ_SR(host->io_base));
	if (sr & MLCIRQ_NAND_READY)
		complete(&host->comp_nand);
	if (sr & MLCIRQ_CONTROLLER_READY)
		complete(&host->comp_controller);

	return IRQ_HANDLED;
}

static int lpc32xx_waitfunc_nand(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct lpc32xx_nand_host *host = nand_get_controller_data(chip);

	if (readb(MLC_ISR(host->io_base)) & MLCISR_NAND_READY)
		goto exit;

	wait_for_completion(&host->comp_nand);

	while (!(readb(MLC_ISR(host->io_base)) & MLCISR_NAND_READY)) {
		/* Seems to be delayed sometimes by controller */
		dev_dbg(&mtd->dev, "Warning: NAND not ready.\n");
		cpu_relax();
	}

exit:
	return NAND_STATUS_READY;
}

static int lpc32xx_waitfunc_controller(struct mtd_info *mtd,
				       struct nand_chip *chip)
{
	struct lpc32xx_nand_host *host = nand_get_controller_data(chip);

	if (readb(MLC_ISR(host->io_base)) & MLCISR_CONTROLLER_READY)
		goto exit;

	wait_for_completion(&host->comp_controller);

	while (!(readb(MLC_ISR(host->io_base)) &
		 MLCISR_CONTROLLER_READY)) {
		dev_dbg(&mtd->dev, "Warning: Controller not ready.\n");
		cpu_relax();
	}

exit:
	return NAND_STATUS_READY;
}

static int lpc32xx_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	lpc32xx_waitfunc_nand(mtd, chip);
	lpc32xx_waitfunc_controller(mtd, chip);

	return NAND_STATUS_READY;
}

/*
 * Enable NAND write protect
 */
static void lpc32xx_wp_enable(struct lpc32xx_nand_host *host)
{
	if (gpio_is_valid(host->ncfg->wp_gpio))
		gpio_set_value(host->ncfg->wp_gpio, 0);
}

/*
 * Disable NAND write protect
 */
static void lpc32xx_wp_disable(struct lpc32xx_nand_host *host)
{
	if (gpio_is_valid(host->ncfg->wp_gpio))
		gpio_set_value(host->ncfg->wp_gpio, 1);
}

static void lpc32xx_dma_complete_func(void *completion)
{
	complete(completion);
}

static int lpc32xx_xmit_dma(struct mtd_info *mtd, void *mem, int len,
			    enum dma_transfer_direction dir)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct lpc32xx_nand_host *host = nand_get_controller_data(chip);
	struct dma_async_tx_descriptor *desc;
	int flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	int res;

	sg_init_one(&host->sgl, mem, len);

	res = dma_map_sg(host->dma_chan->device->dev, &host->sgl, 1,
			 DMA_BIDIRECTIONAL);
	if (res != 1) {
		dev_err(mtd->dev.parent, "Failed to map sg list\n");
		return -ENXIO;
	}
	desc = dmaengine_prep_slave_sg(host->dma_chan, &host->sgl, 1, dir,
				       flags);
	if (!desc) {
		dev_err(mtd->dev.parent, "Failed to prepare slave sg\n");
		goto out1;
	}

	init_completion(&host->comp_dma);
	desc->callback = lpc32xx_dma_complete_func;
	desc->callback_param = &host->comp_dma;

	dmaengine_submit(desc);
	dma_async_issue_pending(host->dma_chan);

	wait_for_completion_timeout(&host->comp_dma, msecs_to_jiffies(1000));

	dma_unmap_sg(host->dma_chan->device->dev, &host->sgl, 1,
		     DMA_BIDIRECTIONAL);
	return 0;
out1:
	dma_unmap_sg(host->dma_chan->device->dev, &host->sgl, 1,
		     DMA_BIDIRECTIONAL);
	return -ENXIO;
}

static int lpc32xx_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			     uint8_t *buf, int oob_required, int page)
{
	struct lpc32xx_nand_host *host = nand_get_controller_data(chip);
	int i, j;
	uint8_t *oobbuf = chip->oob_poi;
	uint32_t mlc_isr;
	int res;
	uint8_t *dma_buf;
	bool dma_mapped;

	if ((void *)buf <= high_memory) {
		dma_buf = buf;
		dma_mapped = true;
	} else {
		dma_buf = host->dma_buf;
		dma_mapped = false;
	}

	/* Writing Command and Address */
	chip->cmdfunc(mtd, NAND_CMD_READ0, 0, page);

	/* For all sub-pages */
	for (i = 0; i < host->mlcsubpages; i++) {
		/* Start Auto Decode Command */
		writeb(0x00, MLC_ECC_AUTO_DEC_REG(host->io_base));

		/* Wait for Controller Ready */
		lpc32xx_waitfunc_controller(mtd, chip);

		/* Check ECC Error status */
		mlc_isr = readl(MLC_ISR(host->io_base));
		if (mlc_isr & MLCISR_DECODER_FAILURE) {
			mtd->ecc_stats.failed++;
			dev_warn(&mtd->dev, "%s: DECODER_FAILURE\n", __func__);
		} else if (mlc_isr & MLCISR_ERRORS_DETECTED) {
			mtd->ecc_stats.corrected += ((mlc_isr >> 4) & 0x3) + 1;
		}

		/* Read 512 + 16 Bytes */
		if (use_dma) {
			res = lpc32xx_xmit_dma(mtd, dma_buf + i * 512, 512,
					       DMA_DEV_TO_MEM);
			if (res)
				return res;
		} else {
			for (j = 0; j < (512 >> 2); j++) {
				*((uint32_t *)(buf)) =
					readl(MLC_BUFF(host->io_base));
				buf += 4;
			}
		}
		for (j = 0; j < (16 >> 2); j++) {
			*((uint32_t *)(oobbuf)) =
				readl(MLC_BUFF(host->io_base));
			oobbuf += 4;
		}
	}

	if (use_dma && !dma_mapped)
		memcpy(buf, dma_buf, mtd->writesize);

	return 0;
}

static int lpc32xx_write_page_lowlevel(struct mtd_info *mtd,
				       struct nand_chip *chip,
				       const uint8_t *buf, int oob_required,
				       int page)
{
	struct lpc32xx_nand_host *host = nand_get_controller_data(chip);
	const uint8_t *oobbuf = chip->oob_poi;
	uint8_t *dma_buf = (uint8_t *)buf;
	int res;
	int i, j;

	if (use_dma && (void *)buf >= high_memory) {
		dma_buf = host->dma_buf;
		memcpy(dma_buf, buf, mtd->writesize);
	}

	for (i = 0; i < host->mlcsubpages; i++) {
		/* Start Encode */
		writeb(0x00, MLC_ECC_ENC_REG(host->io_base));

		/* Write 512 + 6 Bytes to Buffer */
		if (use_dma) {
			res = lpc32xx_xmit_dma(mtd, dma_buf + i * 512, 512,
					       DMA_MEM_TO_DEV);
			if (res)
				return res;
		} else {
			for (j = 0; j < (512 >> 2); j++) {
				writel(*((uint32_t *)(buf)),
				       MLC_BUFF(host->io_base));
				buf += 4;
			}
		}
		writel(*((uint32_t *)(oobbuf)), MLC_BUFF(host->io_base));
		oobbuf += 4;
		writew(*((uint16_t *)(oobbuf)), MLC_BUFF(host->io_base));
		oobbuf += 12;

		/* Auto Encode w/ Bit 8 = 0 (see LPC MLC Controller manual) */
		writeb(0x00, MLC_ECC_AUTO_ENC_REG(host->io_base));

		/* Wait for Controller Ready */
		lpc32xx_waitfunc_controller(mtd, chip);
	}
	return 0;
}

static int lpc32xx_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
			    int page)
{
	struct lpc32xx_nand_host *host = nand_get_controller_data(chip);

	/* Read whole page - necessary with MLC controller! */
	lpc32xx_read_page(mtd, chip, host->dummy_buf, 1, page);

	return 0;
}

static int lpc32xx_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
			      int page)
{
	/* None, write_oob conflicts with the automatic LPC MLC ECC decoder! */
	return 0;
}

/* Prepares MLC for transfers with H/W ECC enabled: always enabled anyway */
static void lpc32xx_ecc_enable(struct mtd_info *mtd, int mode)
{
	/* Always enabled! */
}

static int lpc32xx_dma_setup(struct lpc32xx_nand_host *host)
{
	struct mtd_info *mtd = nand_to_mtd(&host->nand_chip);
	dma_cap_mask_t mask;

	if (!host->pdata || !host->pdata->dma_filter) {
		dev_err(mtd->dev.parent, "no DMA platform data\n");
		return -ENOENT;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	host->dma_chan = dma_request_channel(mask, host->pdata->dma_filter,
					     "nand-mlc");
	if (!host->dma_chan) {
		dev_err(mtd->dev.parent, "Failed to request DMA channel\n");
		return -EBUSY;
	}

	/*
	 * Set direction to a sensible value even if the dmaengine driver
	 * should ignore it. With the default (DMA_MEM_TO_MEM), the amba-pl08x
	 * driver criticizes it as "alien transfer direction".
	 */
	host->dma_slave_config.direction = DMA_DEV_TO_MEM;
	host->dma_slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->dma_slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->dma_slave_config.src_maxburst = 128;
	host->dma_slave_config.dst_maxburst = 128;
	/* DMA controller does flow control: */
	host->dma_slave_config.device_fc = false;
	host->dma_slave_config.src_addr = MLC_BUFF(host->io_base_phy);
	host->dma_slave_config.dst_addr = MLC_BUFF(host->io_base_phy);
	if (dmaengine_slave_config(host->dma_chan, &host->dma_slave_config)) {
		dev_err(mtd->dev.parent, "Failed to setup DMA slave\n");
		goto out1;
	}

	return 0;
out1:
	dma_release_channel(host->dma_chan);
	return -ENXIO;
}

static struct lpc32xx_nand_cfg_mlc *lpc32xx_parse_dt(struct device *dev)
{
	struct lpc32xx_nand_cfg_mlc *ncfg;
	struct device_node *np = dev->of_node;

	ncfg = devm_kzalloc(dev, sizeof(*ncfg), GFP_KERNEL);
	if (!ncfg)
		return NULL;

	of_property_read_u32(np, "nxp,tcea-delay", &ncfg->tcea_delay);
	of_property_read_u32(np, "nxp,busy-delay", &ncfg->busy_delay);
	of_property_read_u32(np, "nxp,nand-ta", &ncfg->nand_ta);
	of_property_read_u32(np, "nxp,rd-high", &ncfg->rd_high);
	of_property_read_u32(np, "nxp,rd-low", &ncfg->rd_low);
	of_property_read_u32(np, "nxp,wr-high", &ncfg->wr_high);
	of_property_read_u32(np, "nxp,wr-low", &ncfg->wr_low);

	if (!ncfg->tcea_delay || !ncfg->busy_delay || !ncfg->nand_ta ||
	    !ncfg->rd_high || !ncfg->rd_low || !ncfg->wr_high ||
	    !ncfg->wr_low) {
		dev_err(dev, "chip parameters not specified correctly\n");
		return NULL;
	}

	ncfg->wp_gpio = of_get_named_gpio(np, "gpios", 0);

	return ncfg;
}

/*
 * Probe for NAND controller
 */
static int lpc32xx_nand_probe(struct platform_device *pdev)
{
	struct lpc32xx_nand_host *host;
	struct mtd_info *mtd;
	struct nand_chip *nand_chip;
	struct resource *rc;
	int res;

	/* Allocate memory for the device structure (and zero it) */
	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	rc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->io_base = devm_ioremap_resource(&pdev->dev, rc);
	if (IS_ERR(host->io_base))
		return PTR_ERR(host->io_base);
	
	host->io_base_phy = rc->start;

	nand_chip = &host->nand_chip;
	mtd = nand_to_mtd(nand_chip);
	if (pdev->dev.of_node)
		host->ncfg = lpc32xx_parse_dt(&pdev->dev);
	if (!host->ncfg) {
		dev_err(&pdev->dev,
			"Missing or bad NAND config from device tree\n");
		return -ENOENT;
	}
	if (host->ncfg->wp_gpio == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (gpio_is_valid(host->ncfg->wp_gpio) &&
			gpio_request(host->ncfg->wp_gpio, "NAND WP")) {
		dev_err(&pdev->dev, "GPIO not available\n");
		return -EBUSY;
	}
	lpc32xx_wp_disable(host);

	host->pdata = dev_get_platdata(&pdev->dev);

	/* link the private data structures */
	nand_set_controller_data(nand_chip, host);
	nand_set_flash_node(nand_chip, pdev->dev.of_node);
	mtd->dev.parent = &pdev->dev;

	/* Get NAND clock */
	host->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		dev_err(&pdev->dev, "Clock initialization failure\n");
		res = -ENOENT;
		goto err_exit1;
	}
	clk_prepare_enable(host->clk);

	nand_chip->cmd_ctrl = lpc32xx_nand_cmd_ctrl;
	nand_chip->dev_ready = lpc32xx_nand_device_ready;
	nand_chip->chip_delay = 25; /* us */
	nand_chip->IO_ADDR_R = MLC_DATA(host->io_base);
	nand_chip->IO_ADDR_W = MLC_DATA(host->io_base);

	/* Init NAND controller */
	lpc32xx_nand_setup(host);

	platform_set_drvdata(pdev, host);

	/* Initialize function pointers */
	nand_chip->ecc.hwctl = lpc32xx_ecc_enable;
	nand_chip->ecc.read_page_raw = lpc32xx_read_page;
	nand_chip->ecc.read_page = lpc32xx_read_page;
	nand_chip->ecc.write_page_raw = lpc32xx_write_page_lowlevel;
	nand_chip->ecc.write_page = lpc32xx_write_page_lowlevel;
	nand_chip->ecc.write_oob = lpc32xx_write_oob;
	nand_chip->ecc.read_oob = lpc32xx_read_oob;
	nand_chip->ecc.strength = 4;
	nand_chip->ecc.bytes = 10;
	nand_chip->waitfunc = lpc32xx_waitfunc;

	nand_chip->options = NAND_NO_SUBPAGE_WRITE;
	nand_chip->bbt_options = NAND_BBT_USE_FLASH | NAND_BBT_NO_OOB;
	nand_chip->bbt_td = &lpc32xx_nand_bbt;
	nand_chip->bbt_md = &lpc32xx_nand_bbt_mirror;

	if (use_dma) {
		res = lpc32xx_dma_setup(host);
		if (res) {
			res = -EIO;
			goto err_exit2;
		}
	}

	/*
	 * Scan to find existance of the device and
	 * Get the type of NAND device SMALL block or LARGE block
	 */
	if (nand_scan_ident(mtd, 1, NULL)) {
		res = -ENXIO;
		goto err_exit3;
	}

	host->dma_buf = devm_kzalloc(&pdev->dev, mtd->writesize, GFP_KERNEL);
	if (!host->dma_buf) {
		res = -ENOMEM;
		goto err_exit3;
	}

	host->dummy_buf = devm_kzalloc(&pdev->dev, mtd->writesize, GFP_KERNEL);
	if (!host->dummy_buf) {
		res = -ENOMEM;
		goto err_exit3;
	}

	nand_chip->ecc.mode = NAND_ECC_HW;
	nand_chip->ecc.size = 512;
	mtd_set_ooblayout(mtd, &lpc32xx_ooblayout_ops);
	host->mlcsubpages = mtd->writesize / 512;

	/* initially clear interrupt status */
	readb(MLC_IRQ_SR(host->io_base));

	init_completion(&host->comp_nand);
	init_completion(&host->comp_controller);

	host->irq = platform_get_irq(pdev, 0);
	if ((host->irq < 0) || (host->irq >= NR_IRQS)) {
		dev_err(&pdev->dev, "failed to get platform irq\n");
		res = -EINVAL;
		goto err_exit3;
	}

	if (request_irq(host->irq, (irq_handler_t)&lpc3xxx_nand_irq,
			IRQF_TRIGGER_HIGH, DRV_NAME, host)) {
		dev_err(&pdev->dev, "Error requesting NAND IRQ\n");
		res = -ENXIO;
		goto err_exit3;
	}

	/*
	 * Fills out all the uninitialized function pointers with the defaults
	 * And scans for a bad block table if appropriate.
	 */
	if (nand_scan_tail(mtd)) {
		res = -ENXIO;
		goto err_exit4;
	}

	mtd->name = DRV_NAME;

	res = mtd_device_register(mtd, host->ncfg->parts,
				  host->ncfg->num_parts);
	if (!res)
		return res;

	nand_release(mtd);

err_exit4:
	free_irq(host->irq, host);
err_exit3:
	if (use_dma)
		dma_release_channel(host->dma_chan);
err_exit2:
	clk_disable_unprepare(host->clk);
	clk_put(host->clk);
err_exit1:
	lpc32xx_wp_enable(host);
	gpio_free(host->ncfg->wp_gpio);

	return res;
}

/*
 * Remove NAND device
 */
static int lpc32xx_nand_remove(struct platform_device *pdev)
{
	struct lpc32xx_nand_host *host = platform_get_drvdata(pdev);
	struct mtd_info *mtd = nand_to_mtd(&host->nand_chip);

	nand_release(mtd);
	free_irq(host->irq, host);
	if (use_dma)
		dma_release_channel(host->dma_chan);

	clk_disable_unprepare(host->clk);
	clk_put(host->clk);

	lpc32xx_wp_enable(host);
	gpio_free(host->ncfg->wp_gpio);

	return 0;
}

#ifdef CONFIG_PM
static int lpc32xx_nand_resume(struct platform_device *pdev)
{
	struct lpc32xx_nand_host *host = platform_get_drvdata(pdev);

	/* Re-enable NAND clock */
	clk_prepare_enable(host->clk);

	/* Fresh init of NAND controller */
	lpc32xx_nand_setup(host);

	/* Disable write protect */
	lpc32xx_wp_disable(host);

	return 0;
}

static int lpc32xx_nand_suspend(struct platform_device *pdev, pm_message_t pm)
{
	struct lpc32xx_nand_host *host = platform_get_drvdata(pdev);

	/* Enable write protect for safety */
	lpc32xx_wp_enable(host);

	/* Disable clock */
	clk_disable_unprepare(host->clk);
	return 0;
}

#else
#define lpc32xx_nand_resume NULL
#define lpc32xx_nand_suspend NULL
#endif

static const struct of_device_id lpc32xx_nand_match[] = {
	{ .compatible = "nxp,lpc3220-mlc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, lpc32xx_nand_match);

static struct platform_driver lpc32xx_nand_driver = {
	.probe		= lpc32xx_nand_probe,
	.remove		= lpc32xx_nand_remove,
	.resume		= lpc32xx_nand_resume,
	.suspend	= lpc32xx_nand_suspend,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = lpc32xx_nand_match,
	},
};

module_platform_driver(lpc32xx_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("NAND driver for the NXP LPC32XX MLC controller");
