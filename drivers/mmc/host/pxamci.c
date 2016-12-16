/*
 *  linux/drivers/mmc/host/pxa.c - PXA MMCI driver
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This hardware is really sick:
 *   - No way to clear interrupts.
 *   - Have to turn off the clock whenever we touch the device.
 *   - Doesn't tell you how many data blocks were transferred.
 *  Yuck!
 *
 *	1 and 3 byte data transfers not supported
 *	max block length up to 1023
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/pxa-dma.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/gfp.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>

#include <asm/sizes.h>

#include <mach/hardware.h>
#include <linux/platform_data/mmc-pxamci.h>

#include "pxamci.h"

#define DRIVER_NAME	"pxa2xx-mci"

#define NR_SG	1
#define CLKRT_OFF	(~0)

#define mmc_has_26MHz()		(cpu_is_pxa300() || cpu_is_pxa310() \
				|| cpu_is_pxa935())

struct pxamci_host {
	struct mmc_host		*mmc;
	spinlock_t		lock;
	struct resource		*res;
	void __iomem		*base;
	struct clk		*clk;
	unsigned long		clkrate;
	int			irq;
	unsigned int		clkrt;
	unsigned int		cmdat;
	unsigned int		imask;
	unsigned int		power_mode;
	struct pxamci_platform_data *pdata;

	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	struct dma_chan		*dma_chan_rx;
	struct dma_chan		*dma_chan_tx;
	dma_cookie_t		dma_cookie;
	dma_addr_t		sg_dma;
	unsigned int		dma_len;

	unsigned int		dma_dir;
	unsigned int		dma_drcmrrx;
	unsigned int		dma_drcmrtx;

	struct regulator	*vcc;
};

static inline void pxamci_init_ocr(struct pxamci_host *host)
{
#ifdef CONFIG_REGULATOR
	host->vcc = devm_regulator_get_optional(mmc_dev(host->mmc), "vmmc");

	if (IS_ERR(host->vcc))
		host->vcc = NULL;
	else {
		host->mmc->ocr_avail = mmc_regulator_get_ocrmask(host->vcc);
		if (host->pdata && host->pdata->ocr_mask)
			dev_warn(mmc_dev(host->mmc),
				"ocr_mask/setpower will not be used\n");
	}
#endif
	if (host->vcc == NULL) {
		/* fall-back to platform data */
		host->mmc->ocr_avail = host->pdata ?
			host->pdata->ocr_mask :
			MMC_VDD_32_33 | MMC_VDD_33_34;
	}
}

static inline int pxamci_set_power(struct pxamci_host *host,
				    unsigned char power_mode,
				    unsigned int vdd)
{
	int on;

	if (host->vcc) {
		int ret;

		if (power_mode == MMC_POWER_UP) {
			ret = mmc_regulator_set_ocr(host->mmc, host->vcc, vdd);
			if (ret)
				return ret;
		} else if (power_mode == MMC_POWER_OFF) {
			ret = mmc_regulator_set_ocr(host->mmc, host->vcc, 0);
			if (ret)
				return ret;
		}
	}
	if (!host->vcc && host->pdata &&
	    gpio_is_valid(host->pdata->gpio_power)) {
		on = ((1 << vdd) & host->pdata->ocr_mask);
		gpio_set_value(host->pdata->gpio_power,
			       !!on ^ host->pdata->gpio_power_invert);
	}
	if (!host->vcc && host->pdata && host->pdata->setpower)
		return host->pdata->setpower(mmc_dev(host->mmc), vdd);

	return 0;
}

static void pxamci_stop_clock(struct pxamci_host *host)
{
	if (readl(host->base + MMC_STAT) & STAT_CLK_EN) {
		unsigned long timeout = 10000;
		unsigned int v;

		writel(STOP_CLOCK, host->base + MMC_STRPCL);

		do {
			v = readl(host->base + MMC_STAT);
			if (!(v & STAT_CLK_EN))
				break;
			udelay(1);
		} while (timeout--);

		if (v & STAT_CLK_EN)
			dev_err(mmc_dev(host->mmc), "unable to stop clock\n");
	}
}

static void pxamci_enable_irq(struct pxamci_host *host, unsigned int mask)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->imask &= ~mask;
	writel(host->imask, host->base + MMC_I_MASK);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void pxamci_disable_irq(struct pxamci_host *host, unsigned int mask)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->imask |= mask;
	writel(host->imask, host->base + MMC_I_MASK);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void pxamci_dma_irq(void *param);

static void pxamci_setup_data(struct pxamci_host *host, struct mmc_data *data)
{
	struct dma_async_tx_descriptor *tx;
	enum dma_data_direction direction;
	struct dma_slave_config	config;
	struct dma_chan *chan;
	unsigned int nob = data->blocks;
	unsigned long long clks;
	unsigned int timeout;
	int ret;

	host->data = data;

	writel(nob, host->base + MMC_NOB);
	writel(data->blksz, host->base + MMC_BLKLEN);

	clks = (unsigned long long)data->timeout_ns * host->clkrate;
	do_div(clks, 1000000000UL);
	timeout = (unsigned int)clks + (data->timeout_clks << host->clkrt);
	writel((timeout + 255) / 256, host->base + MMC_RDTO);

	memset(&config, 0, sizeof(config));
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	config.src_addr = host->res->start + MMC_RXFIFO;
	config.dst_addr = host->res->start + MMC_TXFIFO;
	config.src_maxburst = 32;
	config.dst_maxburst = 32;

	if (data->flags & MMC_DATA_READ) {
		host->dma_dir = DMA_FROM_DEVICE;
		direction = DMA_DEV_TO_MEM;
		chan = host->dma_chan_rx;
	} else {
		host->dma_dir = DMA_TO_DEVICE;
		direction = DMA_MEM_TO_DEV;
		chan = host->dma_chan_tx;
	}

	config.direction = direction;

	ret = dmaengine_slave_config(chan, &config);
	if (ret < 0) {
		dev_err(mmc_dev(host->mmc), "dma slave config failed\n");
		return;
	}

	host->dma_len = dma_map_sg(chan->device->dev, data->sg, data->sg_len,
				   host->dma_dir);

	tx = dmaengine_prep_slave_sg(chan, data->sg, host->dma_len, direction,
				     DMA_PREP_INTERRUPT);
	if (!tx) {
		dev_err(mmc_dev(host->mmc), "prep_slave_sg() failed\n");
		return;
	}

	if (!(data->flags & MMC_DATA_READ)) {
		tx->callback = pxamci_dma_irq;
		tx->callback_param = host;
	}

	host->dma_cookie = dmaengine_submit(tx);

	/*
	 * workaround for erratum #91:
	 * only start DMA now if we are doing a read,
	 * otherwise we wait until CMD/RESP has finished
	 * before starting DMA.
	 */
	if (!cpu_is_pxa27x() || data->flags & MMC_DATA_READ)
		dma_async_issue_pending(chan);
}

static void pxamci_start_cmd(struct pxamci_host *host, struct mmc_command *cmd, unsigned int cmdat)
{
	WARN_ON(host->cmd != NULL);
	host->cmd = cmd;

	if (cmd->flags & MMC_RSP_BUSY)
		cmdat |= CMDAT_BUSY;

#define RSP_TYPE(x)	((x) & ~(MMC_RSP_BUSY|MMC_RSP_OPCODE))
	switch (RSP_TYPE(mmc_resp_type(cmd))) {
	case RSP_TYPE(MMC_RSP_R1): /* r1, r1b, r6, r7 */
		cmdat |= CMDAT_RESP_SHORT;
		break;
	case RSP_TYPE(MMC_RSP_R3):
		cmdat |= CMDAT_RESP_R3;
		break;
	case RSP_TYPE(MMC_RSP_R2):
		cmdat |= CMDAT_RESP_R2;
		break;
	default:
		break;
	}

	writel(cmd->opcode, host->base + MMC_CMD);
	writel(cmd->arg >> 16, host->base + MMC_ARGH);
	writel(cmd->arg & 0xffff, host->base + MMC_ARGL);
	writel(cmdat, host->base + MMC_CMDAT);
	writel(host->clkrt, host->base + MMC_CLKRT);

	writel(START_CLOCK, host->base + MMC_STRPCL);

	pxamci_enable_irq(host, END_CMD_RES);
}

static void pxamci_finish_request(struct pxamci_host *host, struct mmc_request *mrq)
{
	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;
	mmc_request_done(host->mmc, mrq);
}

static int pxamci_cmd_done(struct pxamci_host *host, unsigned int stat)
{
	struct mmc_command *cmd = host->cmd;
	int i;
	u32 v;

	if (!cmd)
		return 0;

	host->cmd = NULL;

	/*
	 * Did I mention this is Sick.  We always need to
	 * discard the upper 8 bits of the first 16-bit word.
	 */
	v = readl(host->base + MMC_RES) & 0xffff;
	for (i = 0; i < 4; i++) {
		u32 w1 = readl(host->base + MMC_RES) & 0xffff;
		u32 w2 = readl(host->base + MMC_RES) & 0xffff;
		cmd->resp[i] = v << 24 | w1 << 8 | w2 >> 8;
		v = w2;
	}

	if (stat & STAT_TIME_OUT_RESPONSE) {
		cmd->error = -ETIMEDOUT;
	} else if (stat & STAT_RES_CRC_ERR && cmd->flags & MMC_RSP_CRC) {
		/*
		 * workaround for erratum #42:
		 * Intel PXA27x Family Processor Specification Update Rev 001
		 * A bogus CRC error can appear if the msb of a 136 bit
		 * response is a one.
		 */
		if (cpu_is_pxa27x() &&
		    (cmd->flags & MMC_RSP_136 && cmd->resp[0] & 0x80000000))
			pr_debug("ignoring CRC from command %d - *risky*\n", cmd->opcode);
		else
			cmd->error = -EILSEQ;
	}

	pxamci_disable_irq(host, END_CMD_RES);
	if (host->data && !cmd->error) {
		pxamci_enable_irq(host, DATA_TRAN_DONE);
		/*
		 * workaround for erratum #91, if doing write
		 * enable DMA late
		 */
		if (cpu_is_pxa27x() && host->data->flags & MMC_DATA_WRITE)
			dma_async_issue_pending(host->dma_chan_tx);
	} else {
		pxamci_finish_request(host, host->mrq);
	}

	return 1;
}

static int pxamci_data_done(struct pxamci_host *host, unsigned int stat)
{
	struct mmc_data *data = host->data;
	struct dma_chan *chan;

	if (!data)
		return 0;

	if (data->flags & MMC_DATA_READ)
		chan = host->dma_chan_rx;
	else
		chan = host->dma_chan_tx;
	dma_unmap_sg(chan->device->dev,
		     data->sg, data->sg_len, host->dma_dir);

	if (stat & STAT_READ_TIME_OUT)
		data->error = -ETIMEDOUT;
	else if (stat & (STAT_CRC_READ_ERROR|STAT_CRC_WRITE_ERROR))
		data->error = -EILSEQ;

	/*
	 * There appears to be a hardware design bug here.  There seems to
	 * be no way to find out how much data was transferred to the card.
	 * This means that if there was an error on any block, we mark all
	 * data blocks as being in error.
	 */
	if (!data->error)
		data->bytes_xfered = data->blocks * data->blksz;
	else
		data->bytes_xfered = 0;

	pxamci_disable_irq(host, DATA_TRAN_DONE);

	host->data = NULL;
	if (host->mrq->stop) {
		pxamci_stop_clock(host);
		pxamci_start_cmd(host, host->mrq->stop, host->cmdat);
	} else {
		pxamci_finish_request(host, host->mrq);
	}

	return 1;
}

static irqreturn_t pxamci_irq(int irq, void *devid)
{
	struct pxamci_host *host = devid;
	unsigned int ireg;
	int handled = 0;

	ireg = readl(host->base + MMC_I_REG) & ~readl(host->base + MMC_I_MASK);

	if (ireg) {
		unsigned stat = readl(host->base + MMC_STAT);

		pr_debug("PXAMCI: irq %08x stat %08x\n", ireg, stat);

		if (ireg & END_CMD_RES)
			handled |= pxamci_cmd_done(host, stat);
		if (ireg & DATA_TRAN_DONE)
			handled |= pxamci_data_done(host, stat);
		if (ireg & SDIO_INT) {
			mmc_signal_sdio_irq(host->mmc);
			handled = 1;
		}
	}

	return IRQ_RETVAL(handled);
}

static void pxamci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct pxamci_host *host = mmc_priv(mmc);
	unsigned int cmdat;

	WARN_ON(host->mrq != NULL);

	host->mrq = mrq;

	pxamci_stop_clock(host);

	cmdat = host->cmdat;
	host->cmdat &= ~CMDAT_INIT;

	if (mrq->data) {
		pxamci_setup_data(host, mrq->data);

		cmdat &= ~CMDAT_BUSY;
		cmdat |= CMDAT_DATAEN | CMDAT_DMAEN;
		if (mrq->data->flags & MMC_DATA_WRITE)
			cmdat |= CMDAT_WRITE;
	}

	pxamci_start_cmd(host, mrq->cmd, cmdat);
}

static int pxamci_get_ro(struct mmc_host *mmc)
{
	struct pxamci_host *host = mmc_priv(mmc);

	if (host->pdata && gpio_is_valid(host->pdata->gpio_card_ro))
		return mmc_gpio_get_ro(mmc);
	if (host->pdata && host->pdata->get_ro)
		return !!host->pdata->get_ro(mmc_dev(mmc));
	/*
	 * Board doesn't support read only detection; let the mmc core
	 * decide what to do.
	 */
	return -ENOSYS;
}

static void pxamci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct pxamci_host *host = mmc_priv(mmc);

	if (ios->clock) {
		unsigned long rate = host->clkrate;
		unsigned int clk = rate / ios->clock;

		if (host->clkrt == CLKRT_OFF)
			clk_prepare_enable(host->clk);

		if (ios->clock == 26000000) {
			/* to support 26MHz */
			host->clkrt = 7;
		} else {
			/* to handle (19.5MHz, 26MHz) */
			if (!clk)
				clk = 1;

			/*
			 * clk might result in a lower divisor than we
			 * desire.  check for that condition and adjust
			 * as appropriate.
			 */
			if (rate / clk > ios->clock)
				clk <<= 1;
			host->clkrt = fls(clk) - 1;
		}

		/*
		 * we write clkrt on the next command
		 */
	} else {
		pxamci_stop_clock(host);
		if (host->clkrt != CLKRT_OFF) {
			host->clkrt = CLKRT_OFF;
			clk_disable_unprepare(host->clk);
		}
	}

	if (host->power_mode != ios->power_mode) {
		int ret;

		host->power_mode = ios->power_mode;

		ret = pxamci_set_power(host, ios->power_mode, ios->vdd);
		if (ret) {
			dev_err(mmc_dev(mmc), "unable to set power\n");
			/*
			 * The .set_ios() function in the mmc_host_ops
			 * struct return void, and failing to set the
			 * power should be rare so we print an error and
			 * return here.
			 */
			return;
		}

		if (ios->power_mode == MMC_POWER_ON)
			host->cmdat |= CMDAT_INIT;
	}

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		host->cmdat |= CMDAT_SD_4DAT;
	else
		host->cmdat &= ~CMDAT_SD_4DAT;

	dev_dbg(mmc_dev(mmc), "PXAMCI: clkrt = %x cmdat = %x\n",
		host->clkrt, host->cmdat);
}

static void pxamci_enable_sdio_irq(struct mmc_host *host, int enable)
{
	struct pxamci_host *pxa_host = mmc_priv(host);

	if (enable)
		pxamci_enable_irq(pxa_host, SDIO_INT);
	else
		pxamci_disable_irq(pxa_host, SDIO_INT);
}

static const struct mmc_host_ops pxamci_ops = {
	.request		= pxamci_request,
	.get_cd			= mmc_gpio_get_cd,
	.get_ro			= pxamci_get_ro,
	.set_ios		= pxamci_set_ios,
	.enable_sdio_irq	= pxamci_enable_sdio_irq,
};

static void pxamci_dma_irq(void *param)
{
	struct pxamci_host *host = param;
	struct dma_tx_state state;
	enum dma_status status;
	struct dma_chan *chan;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	if (!host->data)
		goto out_unlock;

	if (host->data->flags & MMC_DATA_READ)
		chan = host->dma_chan_rx;
	else
		chan = host->dma_chan_tx;

	status = dmaengine_tx_status(chan, host->dma_cookie, &state);

	if (likely(status == DMA_COMPLETE)) {
		writel(BUF_PART_FULL, host->base + MMC_PRTBUF);
	} else {
		pr_err("%s: DMA error on %s channel\n", mmc_hostname(host->mmc),
			host->data->flags & MMC_DATA_READ ? "rx" : "tx");
		host->data->error = -EIO;
		pxamci_data_done(host, 0);
	}

out_unlock:
	spin_unlock_irqrestore(&host->lock, flags);
}

static irqreturn_t pxamci_detect_irq(int irq, void *devid)
{
	struct pxamci_host *host = mmc_priv(devid);

	mmc_detect_change(devid, msecs_to_jiffies(host->pdata->detect_delay_ms));
	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static const struct of_device_id pxa_mmc_dt_ids[] = {
        { .compatible = "marvell,pxa-mmc" },
        { }
};

MODULE_DEVICE_TABLE(of, pxa_mmc_dt_ids);

static int pxamci_of_init(struct platform_device *pdev)
{
        struct device_node *np = pdev->dev.of_node;
        struct pxamci_platform_data *pdata;
        u32 tmp;

        if (!np)
                return 0;

        pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
        if (!pdata)
                return -ENOMEM;

	pdata->gpio_card_detect =
		of_get_named_gpio(np, "cd-gpios", 0);
	pdata->gpio_card_ro =
		of_get_named_gpio(np, "wp-gpios", 0);

	/* pxa-mmc specific */
	pdata->gpio_power =
		of_get_named_gpio(np, "pxa-mmc,gpio-power", 0);

	if (of_property_read_u32(np, "pxa-mmc,detect-delay-ms", &tmp) == 0)
		pdata->detect_delay_ms = tmp;

        pdev->dev.platform_data = pdata;

        return 0;
}
#else
static int pxamci_of_init(struct platform_device *pdev)
{
        return 0;
}
#endif

static int pxamci_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct pxamci_host *host = NULL;
	struct resource *r, *dmarx, *dmatx;
	struct pxad_param param_rx, param_tx;
	int ret, irq, gpio_cd = -1, gpio_ro = -1, gpio_power = -1;
	dma_cap_mask_t mask;

	ret = pxamci_of_init(pdev);
	if (ret)
		return ret;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mmc = mmc_alloc_host(sizeof(struct pxamci_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto out;
	}

	mmc->ops = &pxamci_ops;

	/*
	 * We can do SG-DMA, but we don't because we never know how much
	 * data we successfully wrote to the card.
	 */
	mmc->max_segs = NR_SG;

	/*
	 * Our hardware DMA can handle a maximum of one page per SG entry.
	 */
	mmc->max_seg_size = PAGE_SIZE;

	/*
	 * Block length register is only 10 bits before PXA27x.
	 */
	mmc->max_blk_size = cpu_is_pxa25x() ? 1023 : 2048;

	/*
	 * Block count register is 16 bits.
	 */
	mmc->max_blk_count = 65535;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->pdata = pdev->dev.platform_data;
	host->clkrt = CLKRT_OFF;

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		host->clk = NULL;
		goto out;
	}

	host->clkrate = clk_get_rate(host->clk);

	/*
	 * Calculate minimum clock rate, rounding up.
	 */
	mmc->f_min = (host->clkrate + 63) / 64;
	mmc->f_max = (mmc_has_26MHz()) ? 26000000 : host->clkrate;

	pxamci_init_ocr(host);

	mmc->caps = 0;
	host->cmdat = 0;
	if (!cpu_is_pxa25x()) {
		mmc->caps |= MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ;
		host->cmdat |= CMDAT_SDIO_INT_EN;
		if (mmc_has_26MHz())
			mmc->caps |= MMC_CAP_MMC_HIGHSPEED |
				     MMC_CAP_SD_HIGHSPEED;
	}

	spin_lock_init(&host->lock);
	host->res = r;
	host->irq = irq;
	host->imask = MMC_I_MASK_ALL;

	host->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto out;
	}

	/*
	 * Ensure that the host controller is shut down, and setup
	 * with our defaults.
	 */
	pxamci_stop_clock(host);
	writel(0, host->base + MMC_SPI);
	writel(64, host->base + MMC_RESTO);
	writel(host->imask, host->base + MMC_I_MASK);

	ret = devm_request_irq(&pdev->dev, host->irq, pxamci_irq, 0,
			       DRIVER_NAME, host);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, mmc);

	if (!pdev->dev.of_node) {
		dmarx = platform_get_resource(pdev, IORESOURCE_DMA, 0);
		dmatx = platform_get_resource(pdev, IORESOURCE_DMA, 1);
		if (!dmarx || !dmatx) {
			ret = -ENXIO;
			goto out;
		}
		param_rx.prio = PXAD_PRIO_LOWEST;
		param_rx.drcmr = dmarx->start;
		param_tx.prio = PXAD_PRIO_LOWEST;
		param_tx.drcmr = dmatx->start;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	host->dma_chan_rx =
		dma_request_slave_channel_compat(mask, pxad_filter_fn,
						 &param_rx, &pdev->dev, "rx");
	if (host->dma_chan_rx == NULL) {
		dev_err(&pdev->dev, "unable to request rx dma channel\n");
		ret = -ENODEV;
		goto out;
	}

	host->dma_chan_tx =
		dma_request_slave_channel_compat(mask, pxad_filter_fn,
						 &param_tx,  &pdev->dev, "tx");
	if (host->dma_chan_tx == NULL) {
		dev_err(&pdev->dev, "unable to request tx dma channel\n");
		ret = -ENODEV;
		goto out;
	}

	if (host->pdata) {
		gpio_cd = host->pdata->gpio_card_detect;
		gpio_ro = host->pdata->gpio_card_ro;
		gpio_power = host->pdata->gpio_power;
	}
	if (gpio_is_valid(gpio_power)) {
		ret = devm_gpio_request(&pdev->dev, gpio_power,
					"mmc card power");
		if (ret) {
			dev_err(&pdev->dev, "Failed requesting gpio_power %d\n",
				gpio_power);
			goto out;
		}
		gpio_direction_output(gpio_power,
				      host->pdata->gpio_power_invert);
	}
	if (gpio_is_valid(gpio_ro)) {
		ret = mmc_gpio_request_ro(mmc, gpio_ro);
		if (ret) {
			dev_err(&pdev->dev, "Failed requesting gpio_ro %d\n",
				gpio_ro);
			goto out;
		} else {
			mmc->caps2 |= host->pdata->gpio_card_ro_invert ?
				0 : MMC_CAP2_RO_ACTIVE_HIGH;
		}
	}

	if (gpio_is_valid(gpio_cd))
		ret = mmc_gpio_request_cd(mmc, gpio_cd, 0);
	if (ret) {
		dev_err(&pdev->dev, "Failed requesting gpio_cd %d\n", gpio_cd);
		goto out;
	}

	if (host->pdata && host->pdata->init)
		host->pdata->init(&pdev->dev, pxamci_detect_irq, mmc);

	if (gpio_is_valid(gpio_power) && host->pdata->setpower)
		dev_warn(&pdev->dev, "gpio_power and setpower() both defined\n");
	if (gpio_is_valid(gpio_ro) && host->pdata->get_ro)
		dev_warn(&pdev->dev, "gpio_ro and get_ro() both defined\n");

	mmc_add_host(mmc);

	return 0;

out:
	if (host) {
		if (host->dma_chan_rx)
			dma_release_channel(host->dma_chan_rx);
		if (host->dma_chan_tx)
			dma_release_channel(host->dma_chan_tx);
	}
	if (mmc)
		mmc_free_host(mmc);
	return ret;
}

static int pxamci_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	int gpio_cd = -1, gpio_ro = -1, gpio_power = -1;

	if (mmc) {
		struct pxamci_host *host = mmc_priv(mmc);

		mmc_remove_host(mmc);

		if (host->pdata) {
			gpio_cd = host->pdata->gpio_card_detect;
			gpio_ro = host->pdata->gpio_card_ro;
			gpio_power = host->pdata->gpio_power;
		}
		if (host->pdata && host->pdata->exit)
			host->pdata->exit(&pdev->dev, mmc);

		pxamci_stop_clock(host);
		writel(TXFIFO_WR_REQ|RXFIFO_RD_REQ|CLK_IS_OFF|STOP_CMD|
		       END_CMD_RES|PRG_DONE|DATA_TRAN_DONE,
		       host->base + MMC_I_MASK);

		dmaengine_terminate_all(host->dma_chan_rx);
		dmaengine_terminate_all(host->dma_chan_tx);
		dma_release_channel(host->dma_chan_rx);
		dma_release_channel(host->dma_chan_tx);

		mmc_free_host(mmc);
	}
	return 0;
}

static struct platform_driver pxamci_driver = {
	.probe		= pxamci_probe,
	.remove		= pxamci_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(pxa_mmc_dt_ids),
	},
};

module_platform_driver(pxamci_driver);

MODULE_DESCRIPTION("PXA Multimedia Card Interface Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-mci");
