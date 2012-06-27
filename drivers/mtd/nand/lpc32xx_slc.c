/*
 * NXP LPC32XX NAND SLC driver
 *
 * Authors:
 *    Kevin Wells <kevin.wells@nxp.com>
 *    Roland Stigge <stigge@antcom.de>
 *
 * Copyright © 2011 NXP Semiconductors
 * Copyright © 2012 Roland Stigge
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
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_mtd.h>
#include <linux/of_gpio.h>
#include <linux/amba/pl08x.h>

#define LPC32XX_MODNAME		"lpc32xx-nand"

/**********************************************************************
* SLC NAND controller register offsets
**********************************************************************/

#define SLC_DATA(x)		(x + 0x000)
#define SLC_ADDR(x)		(x + 0x004)
#define SLC_CMD(x)		(x + 0x008)
#define SLC_STOP(x)		(x + 0x00C)
#define SLC_CTRL(x)		(x + 0x010)
#define SLC_CFG(x)		(x + 0x014)
#define SLC_STAT(x)		(x + 0x018)
#define SLC_INT_STAT(x)		(x + 0x01C)
#define SLC_IEN(x)		(x + 0x020)
#define SLC_ISR(x)		(x + 0x024)
#define SLC_ICR(x)		(x + 0x028)
#define SLC_TAC(x)		(x + 0x02C)
#define SLC_TC(x)		(x + 0x030)
#define SLC_ECC(x)		(x + 0x034)
#define SLC_DMA_DATA(x)		(x + 0x038)

/**********************************************************************
* slc_ctrl register definitions
**********************************************************************/
#define SLCCTRL_SW_RESET	(1 << 2) /* Reset the NAND controller bit */
#define SLCCTRL_ECC_CLEAR	(1 << 1) /* Reset ECC bit */
#define SLCCTRL_DMA_START	(1 << 0) /* Start DMA channel bit */

/**********************************************************************
* slc_cfg register definitions
**********************************************************************/
#define SLCCFG_CE_LOW		(1 << 5) /* Force CE low bit */
#define SLCCFG_DMA_ECC		(1 << 4) /* Enable DMA ECC bit */
#define SLCCFG_ECC_EN		(1 << 3) /* ECC enable bit */
#define SLCCFG_DMA_BURST	(1 << 2) /* DMA burst bit */
#define SLCCFG_DMA_DIR		(1 << 1) /* DMA write(0)/read(1) bit */
#define SLCCFG_WIDTH		(1 << 0) /* External device width, 0=8bit */

/**********************************************************************
* slc_stat register definitions
**********************************************************************/
#define SLCSTAT_DMA_FIFO	(1 << 2) /* DMA FIFO has data bit */
#define SLCSTAT_SLC_FIFO	(1 << 1) /* SLC FIFO has data bit */
#define SLCSTAT_NAND_READY	(1 << 0) /* NAND device is ready bit */

/**********************************************************************
* slc_int_stat, slc_ien, slc_isr, and slc_icr register definitions
**********************************************************************/
#define SLCSTAT_INT_TC		(1 << 1) /* Transfer count bit */
#define SLCSTAT_INT_RDY_EN	(1 << 0) /* Ready interrupt bit */

/**********************************************************************
* slc_tac register definitions
**********************************************************************/
/* Clock setting for RDY write sample wait time in 2*n clocks */
#define SLCTAC_WDR(n)		(((n) & 0xF) << 28)
/* Write pulse width in clock cycles, 1 to 16 clocks */
#define SLCTAC_WWIDTH(n)	(((n) & 0xF) << 24)
/* Write hold time of control and data signals, 1 to 16 clocks */
#define SLCTAC_WHOLD(n)		(((n) & 0xF) << 20)
/* Write setup time of control and data signals, 1 to 16 clocks */
#define SLCTAC_WSETUP(n)	(((n) & 0xF) << 16)
/* Clock setting for RDY read sample wait time in 2*n clocks */
#define SLCTAC_RDR(n)		(((n) & 0xF) << 12)
/* Read pulse width in clock cycles, 1 to 16 clocks */
#define SLCTAC_RWIDTH(n)	(((n) & 0xF) << 8)
/* Read hold time of control and data signals, 1 to 16 clocks */
#define SLCTAC_RHOLD(n)		(((n) & 0xF) << 4)
/* Read setup time of control and data signals, 1 to 16 clocks */
#define SLCTAC_RSETUP(n)	(((n) & 0xF) << 0)

/**********************************************************************
* slc_ecc register definitions
**********************************************************************/
/* ECC line party fetch macro */
#define SLCECC_TO_LINEPAR(n)	(((n) >> 6) & 0x7FFF)
#define SLCECC_TO_COLPAR(n)	((n) & 0x3F)

/*
 * DMA requires storage space for the DMA local buffer and the hardware ECC
 * storage area. The DMA local buffer is only used if DMA mapping fails
 * during runtime.
 */
#define LPC32XX_DMA_DATA_SIZE		4096
#define LPC32XX_ECC_SAVE_SIZE		((4096 / 256) * 4)

/* Number of bytes used for ECC stored in NAND per 256 bytes */
#define LPC32XX_SLC_DEV_ECC_BYTES	3

/*
 * If the NAND base clock frequency can't be fetched, this frequency will be
 * used instead as the base. This rate is used to setup the timing registers
 * used for NAND accesses.
 */
#define LPC32XX_DEF_BUS_RATE		133250000

/* Milliseconds for DMA FIFO timeout (unlikely anyway) */
#define LPC32XX_DMA_TIMEOUT		100

/*
 * NAND ECC Layout for small page NAND devices
 * Note: For large and huge page devices, the default layouts are used
 */
static struct nand_ecclayout lpc32xx_nand_oob_16 = {
	.eccbytes = 6,
	.eccpos = {10, 11, 12, 13, 14, 15},
	.oobfree = {
		{ .offset = 0, .length = 4 },
		{ .offset = 6, .length = 4 },
	},
};

static u8 bbt_pattern[] = {'B', 'b', 't', '0' };
static u8 mirror_pattern[] = {'1', 't', 'b', 'B' };

/*
 * Small page FLASH BBT descriptors, marker at offset 0, version at offset 6
 * Note: Large page devices used the default layout
 */
static struct nand_bbt_descr bbt_smallpage_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	0,
	.len = 4,
	.veroffs = 6,
	.maxblocks = 4,
	.pattern = bbt_pattern
};

static struct nand_bbt_descr bbt_smallpage_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	0,
	.len = 4,
	.veroffs = 6,
	.maxblocks = 4,
	.pattern = mirror_pattern
};

/*
 * NAND platform configuration structure
 */
struct lpc32xx_nand_cfg_slc {
	uint32_t wdr_clks;
	uint32_t wwidth;
	uint32_t whold;
	uint32_t wsetup;
	uint32_t rdr_clks;
	uint32_t rwidth;
	uint32_t rhold;
	uint32_t rsetup;
	bool use_bbt;
	int wp_gpio;
	struct mtd_partition *parts;
	unsigned num_parts;
};

struct lpc32xx_nand_host {
	struct nand_chip	nand_chip;
	struct clk		*clk;
	struct mtd_info		mtd;
	void __iomem		*io_base;
	struct lpc32xx_nand_cfg_slc *ncfg;

	struct completion	comp;
	struct dma_chan		*dma_chan;
	uint32_t		dma_buf_len;
	struct dma_slave_config	dma_slave_config;
	struct scatterlist	sgl;

	/*
	 * DMA and CPU addresses of ECC work area and data buffer
	 */
	uint32_t		*ecc_buf;
	uint8_t			*data_buf;
	dma_addr_t		io_base_dma;
};

static void lpc32xx_nand_setup(struct lpc32xx_nand_host *host)
{
	uint32_t clkrate, tmp;

	/* Reset SLC controller */
	writel(SLCCTRL_SW_RESET, SLC_CTRL(host->io_base));
	udelay(1000);

	/* Basic setup */
	writel(0, SLC_CFG(host->io_base));
	writel(0, SLC_IEN(host->io_base));
	writel((SLCSTAT_INT_TC | SLCSTAT_INT_RDY_EN),
		SLC_ICR(host->io_base));

	/* Get base clock for SLC block */
	clkrate = clk_get_rate(host->clk);
	if (clkrate == 0)
		clkrate = LPC32XX_DEF_BUS_RATE;

	/* Compute clock setup values */
	tmp = SLCTAC_WDR(host->ncfg->wdr_clks) |
		SLCTAC_WWIDTH(1 + (clkrate / host->ncfg->wwidth)) |
		SLCTAC_WHOLD(1 + (clkrate / host->ncfg->whold)) |
		SLCTAC_WSETUP(1 + (clkrate / host->ncfg->wsetup)) |
		SLCTAC_RDR(host->ncfg->rdr_clks) |
		SLCTAC_RWIDTH(1 + (clkrate / host->ncfg->rwidth)) |
		SLCTAC_RHOLD(1 + (clkrate / host->ncfg->rhold)) |
		SLCTAC_RSETUP(1 + (clkrate / host->ncfg->rsetup));
	writel(tmp, SLC_TAC(host->io_base));
}

/*
 * Hardware specific access to control lines
 */
static void lpc32xx_nand_cmd_ctrl(struct mtd_info *mtd, int cmd,
	unsigned int ctrl)
{
	uint32_t tmp;
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;

	/* Does CE state need to be changed? */
	tmp = readl(SLC_CFG(host->io_base));
	if (ctrl & NAND_NCE)
		tmp |= SLCCFG_CE_LOW;
	else
		tmp &= ~SLCCFG_CE_LOW;
	writel(tmp, SLC_CFG(host->io_base));

	if (cmd != NAND_CMD_NONE) {
		if (ctrl & NAND_CLE)
			writel(cmd, SLC_CMD(host->io_base));
		else
			writel(cmd, SLC_ADDR(host->io_base));
	}
}

/*
 * Read the Device Ready pin
 */
static int lpc32xx_nand_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;
	int rdy = 0;

	if ((readl(SLC_STAT(host->io_base)) & SLCSTAT_NAND_READY) != 0)
		rdy = 1;

	return rdy;
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

/*
 * Prepares SLC for transfers with H/W ECC enabled
 */
static void lpc32xx_nand_ecc_enable(struct mtd_info *mtd, int mode)
{
	/* Hardware ECC is enabled automatically in hardware as needed */
}

/*
 * Calculates the ECC for the data
 */
static int lpc32xx_nand_ecc_calculate(struct mtd_info *mtd,
				      const unsigned char *buf,
				      unsigned char *code)
{
	/*
	 * ECC is calculated automatically in hardware during syndrome read
	 * and write operations, so it doesn't need to be calculated here.
	 */
	return 0;
}

/*
 * Read a single byte from NAND device
 */
static uint8_t lpc32xx_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;

	return (uint8_t)readl(SLC_DATA(host->io_base));
}

/*
 * Simple device read without ECC
 */
static void lpc32xx_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;

	/* Direct device read with no ECC */
	while (len-- > 0)
		*buf++ = (uint8_t)readl(SLC_DATA(host->io_base));
}

/*
 * Simple device write without ECC
 */
static void lpc32xx_nand_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;

	/* Direct device write with no ECC */
	while (len-- > 0)
		writel((uint32_t)*buf++, SLC_DATA(host->io_base));
}

/*
 * Verify data in buffer to data on device
 */
static int lpc32xx_verify_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;
	int i;

	/* DATA register must be read as 32 bits or it will fail */
	for (i = 0; i < len; i++) {
		if (buf[i] != (uint8_t)readl(SLC_DATA(host->io_base)))
			return -EFAULT;
	}

	return 0;
}

/*
 * Read the OOB data from the device without ECC using FIFO method
 */
static int lpc32xx_nand_read_oob_syndrome(struct mtd_info *mtd,
					  struct nand_chip *chip, int page)
{
	chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

/*
 * Write the OOB data to the device without ECC using FIFO method
 */
static int lpc32xx_nand_write_oob_syndrome(struct mtd_info *mtd,
	struct nand_chip *chip, int page)
{
	int status;

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);

	/* Send command to program the OOB data */
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);

	status = chip->waitfunc(mtd, chip);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

/*
 * Fills in the ECC fields in the OOB buffer with the hardware generated ECC
 */
static void lpc32xx_slc_ecc_copy(uint8_t *spare, const uint32_t *ecc, int count)
{
	int i;

	for (i = 0; i < (count * 3); i += 3) {
		uint32_t ce = ecc[i / 3];
		ce = ~(ce << 2) & 0xFFFFFF;
		spare[i + 2] = (uint8_t)(ce & 0xFF);
		ce >>= 8;
		spare[i + 1] = (uint8_t)(ce & 0xFF);
		ce >>= 8;
		spare[i] = (uint8_t)(ce & 0xFF);
	}
}

static void lpc32xx_dma_complete_func(void *completion)
{
	complete(completion);
}

static int lpc32xx_xmit_dma(struct mtd_info *mtd, dma_addr_t dma,
			    void *mem, int len, enum dma_transfer_direction dir)
{
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;
	struct dma_async_tx_descriptor *desc;
	int flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	int res;

	host->dma_slave_config.direction = dir;
	host->dma_slave_config.src_addr = dma;
	host->dma_slave_config.dst_addr = dma;
	host->dma_slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->dma_slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->dma_slave_config.src_maxburst = 4;
	host->dma_slave_config.dst_maxburst = 4;
	/* DMA controller does flow control: */
	host->dma_slave_config.device_fc = false;
	if (dmaengine_slave_config(host->dma_chan, &host->dma_slave_config)) {
		dev_err(mtd->dev.parent, "Failed to setup DMA slave\n");
		return -ENXIO;
	}

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

	init_completion(&host->comp);
	desc->callback = lpc32xx_dma_complete_func;
	desc->callback_param = &host->comp;

	dmaengine_submit(desc);
	dma_async_issue_pending(host->dma_chan);

	wait_for_completion_timeout(&host->comp, msecs_to_jiffies(1000));

	dma_unmap_sg(host->dma_chan->device->dev, &host->sgl, 1,
		     DMA_BIDIRECTIONAL);

	return 0;
out1:
	dma_unmap_sg(host->dma_chan->device->dev, &host->sgl, 1,
		     DMA_BIDIRECTIONAL);
	return -ENXIO;
}

/*
 * DMA read/write transfers with ECC support
 */
static int lpc32xx_xfer(struct mtd_info *mtd, uint8_t *buf, int eccsubpages,
			int read)
{
	struct nand_chip *chip = mtd->priv;
	struct lpc32xx_nand_host *host = chip->priv;
	int i, status = 0;
	unsigned long timeout;
	int res;
	enum dma_transfer_direction dir =
		read ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;
	uint8_t *dma_buf;
	bool dma_mapped;

	if ((void *)buf <= high_memory) {
		dma_buf = buf;
		dma_mapped = true;
	} else {
		dma_buf = host->data_buf;
		dma_mapped = false;
		if (!read)
			memcpy(host->data_buf, buf, mtd->writesize);
	}

	if (read) {
		writel(readl(SLC_CFG(host->io_base)) |
		       SLCCFG_DMA_DIR | SLCCFG_ECC_EN | SLCCFG_DMA_ECC |
		       SLCCFG_DMA_BURST, SLC_CFG(host->io_base));
	} else {
		writel((readl(SLC_CFG(host->io_base)) |
			SLCCFG_ECC_EN | SLCCFG_DMA_ECC | SLCCFG_DMA_BURST) &
		       ~SLCCFG_DMA_DIR,
			SLC_CFG(host->io_base));
	}

	/* Clear initial ECC */
	writel(SLCCTRL_ECC_CLEAR, SLC_CTRL(host->io_base));

	/* Transfer size is data area only */
	writel(mtd->writesize, SLC_TC(host->io_base));

	/* Start transfer in the NAND controller */
	writel(readl(SLC_CTRL(host->io_base)) | SLCCTRL_DMA_START,
	       SLC_CTRL(host->io_base));

	for (i = 0; i < chip->ecc.steps; i++) {
		/* Data */
		res = lpc32xx_xmit_dma(mtd, SLC_DMA_DATA(host->io_base_dma),
				       dma_buf + i * chip->ecc.size,
				       mtd->writesize / chip->ecc.steps, dir);
		if (res)
			return res;

		/* Always _read_ ECC */
		if (i == chip->ecc.steps - 1)
			break;
		if (!read) /* ECC availability delayed on write */
			udelay(10);
		res = lpc32xx_xmit_dma(mtd, SLC_ECC(host->io_base_dma),
				       &host->ecc_buf[i], 4, DMA_DEV_TO_MEM);
		if (res)
			return res;
	}

	/*
	 * According to NXP, the DMA can be finished here, but the NAND
	 * controller may still have buffered data. After porting to using the
	 * dmaengine DMA driver (amba-pl080), the condition (DMA_FIFO empty)
	 * appears to be always true, according to tests. Keeping the check for
	 * safety reasons for now.
	 */
	if (readl(SLC_STAT(host->io_base)) & SLCSTAT_DMA_FIFO) {
		dev_warn(mtd->dev.parent, "FIFO not empty!\n");
		timeout = jiffies + msecs_to_jiffies(LPC32XX_DMA_TIMEOUT);
		while ((readl(SLC_STAT(host->io_base)) & SLCSTAT_DMA_FIFO) &&
		       time_before(jiffies, timeout))
			cpu_relax();
		if (!time_before(jiffies, timeout)) {
			dev_err(mtd->dev.parent, "FIFO held data too long\n");
			status = -EIO;
		}
	}

	/* Read last calculated ECC value */
	if (!read)
		udelay(10);
	host->ecc_buf[chip->ecc.steps - 1] =
		readl(SLC_ECC(host->io_base));

	/* Flush DMA */
	dmaengine_terminate_all(host->dma_chan);

	if (readl(SLC_STAT(host->io_base)) & SLCSTAT_DMA_FIFO ||
	    readl(SLC_TC(host->io_base))) {
		/* Something is left in the FIFO, something is wrong */
		dev_err(mtd->dev.parent, "DMA FIFO failure\n");
		status = -EIO;
	}

	/* Stop DMA & HW ECC */
	writel(readl(SLC_CTRL(host->io_base)) & ~SLCCTRL_DMA_START,
	       SLC_CTRL(host->io_base));
	writel(readl(SLC_CFG(host->io_base)) &
	       ~(SLCCFG_DMA_DIR | SLCCFG_ECC_EN | SLCCFG_DMA_ECC |
		 SLCCFG_DMA_BURST), SLC_CFG(host->io_base));

	if (!dma_mapped && read)
		memcpy(buf, host->data_buf, mtd->writesize);

	return status;
}

/*
 * Read the data and OOB data from the device, use ECC correction with the
 * data, disable ECC for the OOB data
 */
static int lpc32xx_nand_read_page_syndrome(struct mtd_info *mtd,
					   struct nand_chip *chip, uint8_t *buf,
					   int oob_required, int page)
{
	struct lpc32xx_nand_host *host = chip->priv;
	int stat, i, status;
	uint8_t *oobecc, tmpecc[LPC32XX_ECC_SAVE_SIZE];

	/* Issue read command */
	chip->cmdfunc(mtd, NAND_CMD_READ0, 0, page);

	/* Read data and oob, calculate ECC */
	status = lpc32xx_xfer(mtd, buf, chip->ecc.steps, 1);

	/* Get OOB data */
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	/* Convert to stored ECC format */
	lpc32xx_slc_ecc_copy(tmpecc, (uint32_t *) host->ecc_buf, chip->ecc.steps);

	/* Pointer to ECC data retrieved from NAND spare area */
	oobecc = chip->oob_poi + chip->ecc.layout->eccpos[0];

	for (i = 0; i < chip->ecc.steps; i++) {
		stat = chip->ecc.correct(mtd, buf, oobecc,
					 &tmpecc[i * chip->ecc.bytes]);
		if (stat < 0)
			mtd->ecc_stats.failed++;
		else
			mtd->ecc_stats.corrected += stat;

		buf += chip->ecc.size;
		oobecc += chip->ecc.bytes;
	}

	return status;
}

/*
 * Read the data and OOB data from the device, no ECC correction with the
 * data or OOB data
 */
static int lpc32xx_nand_read_page_raw_syndrome(struct mtd_info *mtd,
					       struct nand_chip *chip,
					       uint8_t *buf, int oob_required,
					       int page)
{
	/* Issue read command */
	chip->cmdfunc(mtd, NAND_CMD_READ0, 0, page);

	/* Raw reads can just use the FIFO interface */
	chip->read_buf(mtd, buf, chip->ecc.size * chip->ecc.steps);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

/*
 * Write the data and OOB data to the device, use ECC with the data,
 * disable ECC for the OOB data
 */
static int lpc32xx_nand_write_page_syndrome(struct mtd_info *mtd,
					    struct nand_chip *chip,
					    const uint8_t *buf, int oob_required)
{
	struct lpc32xx_nand_host *host = chip->priv;
	uint8_t *pb = chip->oob_poi + chip->ecc.layout->eccpos[0];
	int error;

	/* Write data, calculate ECC on outbound data */
	error = lpc32xx_xfer(mtd, (uint8_t *)buf, chip->ecc.steps, 0);
	if (error)
		return error;

	/*
	 * The calculated ECC needs some manual work done to it before
	 * committing it to NAND. Process the calculated ECC and place
	 * the resultant values directly into the OOB buffer. */
	lpc32xx_slc_ecc_copy(pb, (uint32_t *)host->ecc_buf, chip->ecc.steps);

	/* Write ECC data to device */
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	return 0;
}

/*
 * Write the data and OOB data to the device, no ECC correction with the
 * data or OOB data
 */
static int lpc32xx_nand_write_page_raw_syndrome(struct mtd_info *mtd,
						struct nand_chip *chip,
						const uint8_t *buf,
						int oob_required)
{
	/* Raw writes can just use the FIFO interface */
	chip->write_buf(mtd, buf, chip->ecc.size * chip->ecc.steps);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	return 0;
}

static bool lpc32xx_dma_filter(struct dma_chan *chan, void *param)
{
	struct pl08x_dma_chan *ch =
		container_of(chan, struct pl08x_dma_chan, chan);

	/* In LPC32xx's PL080 DMA wiring, the SLC NAND DMA signal is #1 */
	if (ch->cd->min_signal == 1)
		return true;
	return false;
}

static int lpc32xx_nand_dma_setup(struct lpc32xx_nand_host *host)
{
	struct mtd_info *mtd = &host->mtd;
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	host->dma_chan = dma_request_channel(mask, lpc32xx_dma_filter, NULL);
	if (!host->dma_chan) {
		dev_err(mtd->dev.parent, "Failed to request DMA channel\n");
		return -EBUSY;
	}

	return 0;
}

#ifdef CONFIG_OF
static struct lpc32xx_nand_cfg_slc *lpc32xx_parse_dt(struct device *dev)
{
	struct lpc32xx_nand_cfg_slc *pdata;
	struct device_node *np = dev->of_node;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "could not allocate memory for platform data\n");
		return NULL;
	}

	of_property_read_u32(np, "nxp,wdr-clks", &pdata->wdr_clks);
	of_property_read_u32(np, "nxp,wwidth", &pdata->wwidth);
	of_property_read_u32(np, "nxp,whold", &pdata->whold);
	of_property_read_u32(np, "nxp,wsetup", &pdata->wsetup);
	of_property_read_u32(np, "nxp,rdr-clks", &pdata->rdr_clks);
	of_property_read_u32(np, "nxp,rwidth", &pdata->rwidth);
	of_property_read_u32(np, "nxp,rhold", &pdata->rhold);
	of_property_read_u32(np, "nxp,rsetup", &pdata->rsetup);

	if (!pdata->wdr_clks || !pdata->wwidth || !pdata->whold ||
	    !pdata->wsetup || !pdata->rdr_clks || !pdata->rwidth ||
	    !pdata->rhold || !pdata->rsetup) {
		dev_err(dev, "chip parameters not specified correctly\n");
		return NULL;
	}

	pdata->use_bbt = of_get_nand_on_flash_bbt(np);
	pdata->wp_gpio = of_get_named_gpio_flags(np, "gpios", 0, NULL);

	return pdata;
}
#else
static struct lpc32xx_nand_cfg_slc *lpc32xx_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

/*
 * Probe for NAND controller
 */
static int __devinit lpc32xx_nand_probe(struct platform_device *pdev)
{
	struct lpc32xx_nand_host *host;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	struct resource *rc;
	struct mtd_part_parser_data ppdata = {};
	int res;

	rc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (rc == NULL) {
		dev_err(&pdev->dev, "No memory resource found for device\n");
		return -EBUSY;
	}

	/* Allocate memory for the device structure (and zero it) */
	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		dev_err(&pdev->dev, "failed to allocate device structure\n");
		return -ENOMEM;
	}
	host->io_base_dma = rc->start;

	host->io_base = devm_request_and_ioremap(&pdev->dev, rc);
	if (host->io_base == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	if (pdev->dev.of_node)
		host->ncfg = lpc32xx_parse_dt(&pdev->dev);
	else
		host->ncfg = pdev->dev.platform_data;
	if (!host->ncfg) {
		dev_err(&pdev->dev, "Missing platform data\n");
		return -ENOENT;
	}
	if (gpio_is_valid(host->ncfg->wp_gpio) &&
			gpio_request(host->ncfg->wp_gpio, "NAND WP")) {
		dev_err(&pdev->dev, "GPIO not available\n");
		return -EBUSY;
	}
	lpc32xx_wp_disable(host);

	mtd = &host->mtd;
	chip = &host->nand_chip;
	chip->priv = host;
	mtd->priv = chip;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = &pdev->dev;

	/* Get NAND clock */
	host->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		dev_err(&pdev->dev, "Clock failure\n");
		res = -ENOENT;
		goto err_exit1;
	}
	clk_enable(host->clk);

	/* Set NAND IO addresses and command/ready functions */
	chip->IO_ADDR_R = SLC_DATA(host->io_base);
	chip->IO_ADDR_W = SLC_DATA(host->io_base);
	chip->cmd_ctrl = lpc32xx_nand_cmd_ctrl;
	chip->dev_ready = lpc32xx_nand_device_ready;
	chip->chip_delay = 20; /* 20us command delay time */

	/* Init NAND controller */
	lpc32xx_nand_setup(host);

	platform_set_drvdata(pdev, host);

	/* NAND callbacks for LPC32xx SLC hardware */
	chip->ecc.mode = NAND_ECC_HW_SYNDROME;
	chip->read_byte = lpc32xx_nand_read_byte;
	chip->read_buf = lpc32xx_nand_read_buf;
	chip->write_buf = lpc32xx_nand_write_buf;
	chip->ecc.read_page_raw = lpc32xx_nand_read_page_raw_syndrome;
	chip->ecc.read_page = lpc32xx_nand_read_page_syndrome;
	chip->ecc.write_page_raw = lpc32xx_nand_write_page_raw_syndrome;
	chip->ecc.write_page = lpc32xx_nand_write_page_syndrome;
	chip->ecc.write_oob = lpc32xx_nand_write_oob_syndrome;
	chip->ecc.read_oob = lpc32xx_nand_read_oob_syndrome;
	chip->ecc.calculate = lpc32xx_nand_ecc_calculate;
	chip->ecc.correct = nand_correct_data;
	chip->ecc.strength = 1;
	chip->ecc.hwctl = lpc32xx_nand_ecc_enable;
	chip->verify_buf = lpc32xx_verify_buf;

	/* bitflip_threshold's default is defined as ecc_strength anyway.
	 * Unfortunately, it is set only later at add_mtd_device(). Meanwhile
	 * being 0, it causes bad block table scanning errors in
	 * nand_scan_tail(), so preparing it here already. */
	mtd->bitflip_threshold = chip->ecc.strength;

	/*
	 * Allocate a large enough buffer for a single huge page plus
	 * extra space for the spare area and ECC storage area
	 */
	host->dma_buf_len = LPC32XX_DMA_DATA_SIZE + LPC32XX_ECC_SAVE_SIZE;
	host->data_buf = devm_kzalloc(&pdev->dev, host->dma_buf_len,
				      GFP_KERNEL);
	if (host->data_buf == NULL) {
		dev_err(&pdev->dev, "Error allocating memory\n");
		res = -ENOMEM;
		goto err_exit2;
	}

	res = lpc32xx_nand_dma_setup(host);
	if (res) {
		res = -EIO;
		goto err_exit2;
	}

	/* Find NAND device */
	if (nand_scan_ident(mtd, 1, NULL)) {
		res = -ENXIO;
		goto err_exit3;
	}

	/* OOB and ECC CPU and DMA work areas */
	host->ecc_buf = (uint32_t *)(host->data_buf + LPC32XX_DMA_DATA_SIZE);

	/*
	 * Small page FLASH has a unique OOB layout, but large and huge
	 * page FLASH use the standard layout. Small page FLASH uses a
	 * custom BBT marker layout.
	 */
	if (mtd->writesize <= 512)
		chip->ecc.layout = &lpc32xx_nand_oob_16;

	/* These sizes remain the same regardless of page size */
	chip->ecc.size = 256;
	chip->ecc.bytes = LPC32XX_SLC_DEV_ECC_BYTES;
	chip->ecc.prepad = chip->ecc.postpad = 0;

	/* Avoid extra scan if using BBT, setup BBT support */
	if (host->ncfg->use_bbt) {
		chip->options |= NAND_SKIP_BBTSCAN;
		chip->bbt_options |= NAND_BBT_USE_FLASH;

		/*
		 * Use a custom BBT marker setup for small page FLASH that
		 * won't interfere with the ECC layout. Large and huge page
		 * FLASH use the standard layout.
		 */
		if (mtd->writesize <= 512) {
			chip->bbt_td = &bbt_smallpage_main_descr;
			chip->bbt_md = &bbt_smallpage_mirror_descr;
		}
	}

	/*
	 * Fills out all the uninitialized function pointers with the defaults
	 */
	if (nand_scan_tail(mtd)) {
		res = -ENXIO;
		goto err_exit3;
	}

	/* Standard layout in FLASH for bad block tables */
	if (host->ncfg->use_bbt) {
		if (nand_default_bbt(mtd) < 0)
			dev_err(&pdev->dev,
			       "Error initializing default bad block tables\n");
	}

	mtd->name = "nxp_lpc3220_slc";
	ppdata.of_node = pdev->dev.of_node;
	res = mtd_device_parse_register(mtd, NULL, &ppdata, host->ncfg->parts,
					host->ncfg->num_parts);
	if (!res)
		return res;

	nand_release(mtd);

err_exit3:
	dma_release_channel(host->dma_chan);
err_exit2:
	clk_disable(host->clk);
	clk_put(host->clk);
	platform_set_drvdata(pdev, NULL);
err_exit1:
	lpc32xx_wp_enable(host);
	gpio_free(host->ncfg->wp_gpio);

	return res;
}

/*
 * Remove NAND device.
 */
static int __devexit lpc32xx_nand_remove(struct platform_device *pdev)
{
	uint32_t tmp;
	struct lpc32xx_nand_host *host = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &host->mtd;

	nand_release(mtd);
	dma_release_channel(host->dma_chan);

	/* Force CE high */
	tmp = readl(SLC_CTRL(host->io_base));
	tmp &= ~SLCCFG_CE_LOW;
	writel(tmp, SLC_CTRL(host->io_base));

	clk_disable(host->clk);
	clk_put(host->clk);
	platform_set_drvdata(pdev, NULL);
	lpc32xx_wp_enable(host);
	gpio_free(host->ncfg->wp_gpio);

	return 0;
}

#ifdef CONFIG_PM
static int lpc32xx_nand_resume(struct platform_device *pdev)
{
	struct lpc32xx_nand_host *host = platform_get_drvdata(pdev);

	/* Re-enable NAND clock */
	clk_enable(host->clk);

	/* Fresh init of NAND controller */
	lpc32xx_nand_setup(host);

	/* Disable write protect */
	lpc32xx_wp_disable(host);

	return 0;
}

static int lpc32xx_nand_suspend(struct platform_device *pdev, pm_message_t pm)
{
	uint32_t tmp;
	struct lpc32xx_nand_host *host = platform_get_drvdata(pdev);

	/* Force CE high */
	tmp = readl(SLC_CTRL(host->io_base));
	tmp &= ~SLCCFG_CE_LOW;
	writel(tmp, SLC_CTRL(host->io_base));

	/* Enable write protect for safety */
	lpc32xx_wp_enable(host);

	/* Disable clock */
	clk_disable(host->clk);

	return 0;
}

#else
#define lpc32xx_nand_resume NULL
#define lpc32xx_nand_suspend NULL
#endif

#if defined(CONFIG_OF)
static const struct of_device_id lpc32xx_nand_match[] = {
	{ .compatible = "nxp,lpc3220-slc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, lpc32xx_nand_match);
#endif

static struct platform_driver lpc32xx_nand_driver = {
	.probe		= lpc32xx_nand_probe,
	.remove		= __devexit_p(lpc32xx_nand_remove),
	.resume		= lpc32xx_nand_resume,
	.suspend	= lpc32xx_nand_suspend,
	.driver		= {
		.name	= LPC32XX_MODNAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(lpc32xx_nand_match),
	},
};

module_platform_driver(lpc32xx_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com>");
MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("NAND driver for the NXP LPC32XX SLC controller");
