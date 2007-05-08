/*
 *  linux/drivers/mmc/mmci.c - ARM PrimeCell MMCI PL180/1 driver
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mmc/host.h>
#include <linux/amba/bus.h>
#include <linux/clk.h>

#include <asm/cacheflush.h>
#include <asm/div64.h>
#include <asm/io.h>
#include <asm/scatterlist.h>
#include <asm/sizes.h>
#include <asm/mach/mmc.h>

#include "mmci.h"

#define DRIVER_NAME "mmci-pl18x"

#define DBG(host,fmt,args...)	\
	pr_debug("%s: %s: " fmt, mmc_hostname(host->mmc), __func__ , args)

static unsigned int fmax = 515633;

static void
mmci_request_end(struct mmci_host *host, struct mmc_request *mrq)
{
	writel(0, host->base + MMCICOMMAND);

	BUG_ON(host->data);

	host->mrq = NULL;
	host->cmd = NULL;

	if (mrq->data)
		mrq->data->bytes_xfered = host->data_xfered;

	/*
	 * Need to drop the host lock here; mmc_request_done may call
	 * back into the driver...
	 */
	spin_unlock(&host->lock);
	mmc_request_done(host->mmc, mrq);
	spin_lock(&host->lock);
}

static void mmci_stop_data(struct mmci_host *host)
{
	writel(0, host->base + MMCIDATACTRL);
	writel(0, host->base + MMCIMASK1);
	host->data = NULL;
}

static void mmci_start_data(struct mmci_host *host, struct mmc_data *data)
{
	unsigned int datactrl, timeout, irqmask;
	unsigned long long clks;
	void __iomem *base;
	int blksz_bits;

	DBG(host, "blksz %04x blks %04x flags %08x\n",
	    data->blksz, data->blocks, data->flags);

	host->data = data;
	host->size = data->blksz;
	host->data_xfered = 0;

	mmci_init_sg(host, data);

	clks = (unsigned long long)data->timeout_ns * host->cclk;
	do_div(clks, 1000000000UL);

	timeout = data->timeout_clks + (unsigned int)clks;

	base = host->base;
	writel(timeout, base + MMCIDATATIMER);
	writel(host->size, base + MMCIDATALENGTH);

	blksz_bits = ffs(data->blksz) - 1;
	BUG_ON(1 << blksz_bits != data->blksz);

	datactrl = MCI_DPSM_ENABLE | blksz_bits << 4;
	if (data->flags & MMC_DATA_READ) {
		datactrl |= MCI_DPSM_DIRECTION;
		irqmask = MCI_RXFIFOHALFFULLMASK;

		/*
		 * If we have less than a FIFOSIZE of bytes to transfer,
		 * trigger a PIO interrupt as soon as any data is available.
		 */
		if (host->size < MCI_FIFOSIZE)
			irqmask |= MCI_RXDATAAVLBLMASK;
	} else {
		/*
		 * We don't actually need to include "FIFO empty" here
		 * since its implicit in "FIFO half empty".
		 */
		irqmask = MCI_TXFIFOHALFEMPTYMASK;
	}

	writel(datactrl, base + MMCIDATACTRL);
	writel(readl(base + MMCIMASK0) & ~MCI_DATAENDMASK, base + MMCIMASK0);
	writel(irqmask, base + MMCIMASK1);
}

static void
mmci_start_command(struct mmci_host *host, struct mmc_command *cmd, u32 c)
{
	void __iomem *base = host->base;

	DBG(host, "op %02x arg %08x flags %08x\n",
	    cmd->opcode, cmd->arg, cmd->flags);

	if (readl(base + MMCICOMMAND) & MCI_CPSM_ENABLE) {
		writel(0, base + MMCICOMMAND);
		udelay(1);
	}

	c |= cmd->opcode | MCI_CPSM_ENABLE;
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			c |= MCI_CPSM_LONGRSP;
		c |= MCI_CPSM_RESPONSE;
	}
	if (/*interrupt*/0)
		c |= MCI_CPSM_INTERRUPT;

	host->cmd = cmd;

	writel(cmd->arg, base + MMCIARGUMENT);
	writel(c, base + MMCICOMMAND);
}

static void
mmci_data_irq(struct mmci_host *host, struct mmc_data *data,
	      unsigned int status)
{
	if (status & MCI_DATABLOCKEND) {
		host->data_xfered += data->blksz;
	}
	if (status & (MCI_DATACRCFAIL|MCI_DATATIMEOUT|MCI_TXUNDERRUN|MCI_RXOVERRUN)) {
		if (status & MCI_DATACRCFAIL)
			data->error = MMC_ERR_BADCRC;
		else if (status & MCI_DATATIMEOUT)
			data->error = MMC_ERR_TIMEOUT;
		else if (status & (MCI_TXUNDERRUN|MCI_RXOVERRUN))
			data->error = MMC_ERR_FIFO;
		status |= MCI_DATAEND;

		/*
		 * We hit an error condition.  Ensure that any data
		 * partially written to a page is properly coherent.
		 */
		if (host->sg_len && data->flags & MMC_DATA_READ)
			flush_dcache_page(host->sg_ptr->page);
	}
	if (status & MCI_DATAEND) {
		mmci_stop_data(host);

		if (!data->stop) {
			mmci_request_end(host, data->mrq);
		} else {
			mmci_start_command(host, data->stop, 0);
		}
	}
}

static void
mmci_cmd_irq(struct mmci_host *host, struct mmc_command *cmd,
	     unsigned int status)
{
	void __iomem *base = host->base;

	host->cmd = NULL;

	cmd->resp[0] = readl(base + MMCIRESPONSE0);
	cmd->resp[1] = readl(base + MMCIRESPONSE1);
	cmd->resp[2] = readl(base + MMCIRESPONSE2);
	cmd->resp[3] = readl(base + MMCIRESPONSE3);

	if (status & MCI_CMDTIMEOUT) {
		cmd->error = MMC_ERR_TIMEOUT;
	} else if (status & MCI_CMDCRCFAIL && cmd->flags & MMC_RSP_CRC) {
		cmd->error = MMC_ERR_BADCRC;
	}

	if (!cmd->data || cmd->error != MMC_ERR_NONE) {
		if (host->data)
			mmci_stop_data(host);
		mmci_request_end(host, cmd->mrq);
	} else if (!(cmd->data->flags & MMC_DATA_READ)) {
		mmci_start_data(host, cmd->data);
	}
}

static int mmci_pio_read(struct mmci_host *host, char *buffer, unsigned int remain)
{
	void __iomem *base = host->base;
	char *ptr = buffer;
	u32 status;

	do {
		int count = host->size - (readl(base + MMCIFIFOCNT) << 2);

		if (count > remain)
			count = remain;

		if (count <= 0)
			break;

		readsl(base + MMCIFIFO, ptr, count >> 2);

		ptr += count;
		remain -= count;

		if (remain == 0)
			break;

		status = readl(base + MMCISTATUS);
	} while (status & MCI_RXDATAAVLBL);

	return ptr - buffer;
}

static int mmci_pio_write(struct mmci_host *host, char *buffer, unsigned int remain, u32 status)
{
	void __iomem *base = host->base;
	char *ptr = buffer;

	do {
		unsigned int count, maxcnt;

		maxcnt = status & MCI_TXFIFOEMPTY ? MCI_FIFOSIZE : MCI_FIFOHALFSIZE;
		count = min(remain, maxcnt);

		writesl(base + MMCIFIFO, ptr, count >> 2);

		ptr += count;
		remain -= count;

		if (remain == 0)
			break;

		status = readl(base + MMCISTATUS);
	} while (status & MCI_TXFIFOHALFEMPTY);

	return ptr - buffer;
}

/*
 * PIO data transfer IRQ handler.
 */
static irqreturn_t mmci_pio_irq(int irq, void *dev_id)
{
	struct mmci_host *host = dev_id;
	void __iomem *base = host->base;
	u32 status;

	status = readl(base + MMCISTATUS);

	DBG(host, "irq1 %08x\n", status);

	do {
		unsigned long flags;
		unsigned int remain, len;
		char *buffer;

		/*
		 * For write, we only need to test the half-empty flag
		 * here - if the FIFO is completely empty, then by
		 * definition it is more than half empty.
		 *
		 * For read, check for data available.
		 */
		if (!(status & (MCI_TXFIFOHALFEMPTY|MCI_RXDATAAVLBL)))
			break;

		/*
		 * Map the current scatter buffer.
		 */
		buffer = mmci_kmap_atomic(host, &flags) + host->sg_off;
		remain = host->sg_ptr->length - host->sg_off;

		len = 0;
		if (status & MCI_RXACTIVE)
			len = mmci_pio_read(host, buffer, remain);
		if (status & MCI_TXACTIVE)
			len = mmci_pio_write(host, buffer, remain, status);

		/*
		 * Unmap the buffer.
		 */
		mmci_kunmap_atomic(host, buffer, &flags);

		host->sg_off += len;
		host->size -= len;
		remain -= len;

		if (remain)
			break;

		/*
		 * If we were reading, and we have completed this
		 * page, ensure that the data cache is coherent.
		 */
		if (status & MCI_RXACTIVE)
			flush_dcache_page(host->sg_ptr->page);

		if (!mmci_next_sg(host))
			break;

		status = readl(base + MMCISTATUS);
	} while (1);

	/*
	 * If we're nearing the end of the read, switch to
	 * "any data available" mode.
	 */
	if (status & MCI_RXACTIVE && host->size < MCI_FIFOSIZE)
		writel(MCI_RXDATAAVLBLMASK, base + MMCIMASK1);

	/*
	 * If we run out of data, disable the data IRQs; this
	 * prevents a race where the FIFO becomes empty before
	 * the chip itself has disabled the data path, and
	 * stops us racing with our data end IRQ.
	 */
	if (host->size == 0) {
		writel(0, base + MMCIMASK1);
		writel(readl(base + MMCIMASK0) | MCI_DATAENDMASK, base + MMCIMASK0);
	}

	return IRQ_HANDLED;
}

/*
 * Handle completion of command and data transfers.
 */
static irqreturn_t mmci_irq(int irq, void *dev_id)
{
	struct mmci_host *host = dev_id;
	u32 status;
	int ret = 0;

	spin_lock(&host->lock);

	do {
		struct mmc_command *cmd;
		struct mmc_data *data;

		status = readl(host->base + MMCISTATUS);
		status &= readl(host->base + MMCIMASK0);
		writel(status, host->base + MMCICLEAR);

		DBG(host, "irq0 %08x\n", status);

		data = host->data;
		if (status & (MCI_DATACRCFAIL|MCI_DATATIMEOUT|MCI_TXUNDERRUN|
			      MCI_RXOVERRUN|MCI_DATAEND|MCI_DATABLOCKEND) && data)
			mmci_data_irq(host, data, status);

		cmd = host->cmd;
		if (status & (MCI_CMDCRCFAIL|MCI_CMDTIMEOUT|MCI_CMDSENT|MCI_CMDRESPEND) && cmd)
			mmci_cmd_irq(host, cmd, status);

		ret = 1;
	} while (status);

	spin_unlock(&host->lock);

	return IRQ_RETVAL(ret);
}

static void mmci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmci_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);

	spin_lock_irq(&host->lock);

	host->mrq = mrq;

	if (mrq->data && mrq->data->flags & MMC_DATA_READ)
		mmci_start_data(host, mrq->data);

	mmci_start_command(host, mrq->cmd, 0);

	spin_unlock_irq(&host->lock);
}

static void mmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmci_host *host = mmc_priv(mmc);
	u32 clk = 0, pwr = 0;

	if (ios->clock) {
		if (ios->clock >= host->mclk) {
			clk = MCI_CLK_BYPASS;
			host->cclk = host->mclk;
		} else {
			clk = host->mclk / (2 * ios->clock) - 1;
			if (clk > 256)
				clk = 255;
			host->cclk = host->mclk / (2 * (clk + 1));
		}
		clk |= MCI_CLK_ENABLE;
	}

	if (host->plat->translate_vdd)
		pwr |= host->plat->translate_vdd(mmc_dev(mmc), ios->vdd);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		break;
	case MMC_POWER_UP:
		pwr |= MCI_PWR_UP;
		break;
	case MMC_POWER_ON:
		pwr |= MCI_PWR_ON;
		break;
	}

	if (ios->bus_mode == MMC_BUSMODE_OPENDRAIN)
		pwr |= MCI_ROD;

	writel(clk, host->base + MMCICLOCK);

	if (host->pwr != pwr) {
		host->pwr = pwr;
		writel(pwr, host->base + MMCIPOWER);
	}
}

static const struct mmc_host_ops mmci_ops = {
	.request	= mmci_request,
	.set_ios	= mmci_set_ios,
};

static void mmci_check_status(unsigned long data)
{
	struct mmci_host *host = (struct mmci_host *)data;
	unsigned int status;

	status = host->plat->status(mmc_dev(host->mmc));
	if (status ^ host->oldstat)
		mmc_detect_change(host->mmc, 0);

	host->oldstat = status;
	mod_timer(&host->timer, jiffies + HZ);
}

static int mmci_probe(struct amba_device *dev, void *id)
{
	struct mmc_platform_data *plat = dev->dev.platform_data;
	struct mmci_host *host;
	struct mmc_host *mmc;
	int ret;

	/* must have platform data */
	if (!plat) {
		ret = -EINVAL;
		goto out;
	}

	ret = amba_request_regions(dev, DRIVER_NAME);
	if (ret)
		goto out;

	mmc = mmc_alloc_host(sizeof(struct mmci_host), &dev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto rel_regions;
	}

	host = mmc_priv(mmc);
	host->clk = clk_get(&dev->dev, "MCLK");
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		host->clk = NULL;
		goto host_free;
	}

	ret = clk_enable(host->clk);
	if (ret)
		goto clk_free;

	host->plat = plat;
	host->mclk = clk_get_rate(host->clk);
	host->mmc = mmc;
	host->base = ioremap(dev->res.start, SZ_4K);
	if (!host->base) {
		ret = -ENOMEM;
		goto clk_disable;
	}

	mmc->ops = &mmci_ops;
	mmc->f_min = (host->mclk + 511) / 512;
	mmc->f_max = min(host->mclk, fmax);
	mmc->ocr_avail = plat->ocr_mask;
	mmc->caps = MMC_CAP_MULTIWRITE;

	/*
	 * We can do SGIO
	 */
	mmc->max_hw_segs = 16;
	mmc->max_phys_segs = NR_SG;

	/*
	 * Since we only have a 16-bit data length register, we must
	 * ensure that we don't exceed 2^16-1 bytes in a single request.
	 */
	mmc->max_req_size = 65535;

	/*
	 * Set the maximum segment size.  Since we aren't doing DMA
	 * (yet) we are only limited by the data length register.
	 */
	mmc->max_seg_size = mmc->max_req_size;

	/*
	 * Block size can be up to 2048 bytes, but must be a power of two.
	 */
	mmc->max_blk_size = 2048;

	/*
	 * No limit on the number of blocks transferred.
	 */
	mmc->max_blk_count = mmc->max_req_size;

	spin_lock_init(&host->lock);

	writel(0, host->base + MMCIMASK0);
	writel(0, host->base + MMCIMASK1);
	writel(0xfff, host->base + MMCICLEAR);

	ret = request_irq(dev->irq[0], mmci_irq, IRQF_SHARED, DRIVER_NAME " (cmd)", host);
	if (ret)
		goto unmap;

	ret = request_irq(dev->irq[1], mmci_pio_irq, IRQF_SHARED, DRIVER_NAME " (pio)", host);
	if (ret)
		goto irq0_free;

	writel(MCI_IRQENABLE, host->base + MMCIMASK0);

	amba_set_drvdata(dev, mmc);

	mmc_add_host(mmc);

	printk(KERN_INFO "%s: MMCI rev %x cfg %02x at 0x%016llx irq %d,%d\n",
		mmc_hostname(mmc), amba_rev(dev), amba_config(dev),
		(unsigned long long)dev->res.start, dev->irq[0], dev->irq[1]);

	init_timer(&host->timer);
	host->timer.data = (unsigned long)host;
	host->timer.function = mmci_check_status;
	host->timer.expires = jiffies + HZ;
	add_timer(&host->timer);

	return 0;

 irq0_free:
	free_irq(dev->irq[0], host);
 unmap:
	iounmap(host->base);
 clk_disable:
	clk_disable(host->clk);
 clk_free:
	clk_put(host->clk);
 host_free:
	mmc_free_host(mmc);
 rel_regions:
	amba_release_regions(dev);
 out:
	return ret;
}

static int mmci_remove(struct amba_device *dev)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);

	amba_set_drvdata(dev, NULL);

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		del_timer_sync(&host->timer);

		mmc_remove_host(mmc);

		writel(0, host->base + MMCIMASK0);
		writel(0, host->base + MMCIMASK1);

		writel(0, host->base + MMCICOMMAND);
		writel(0, host->base + MMCIDATACTRL);

		free_irq(dev->irq[0], host);
		free_irq(dev->irq[1], host);

		iounmap(host->base);
		clk_disable(host->clk);
		clk_put(host->clk);

		mmc_free_host(mmc);

		amba_release_regions(dev);
	}

	return 0;
}

#ifdef CONFIG_PM
static int mmci_suspend(struct amba_device *dev, pm_message_t state)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);
	int ret = 0;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		ret = mmc_suspend_host(mmc, state);
		if (ret == 0)
			writel(0, host->base + MMCIMASK0);
	}

	return ret;
}

static int mmci_resume(struct amba_device *dev)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);
	int ret = 0;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		writel(MCI_IRQENABLE, host->base + MMCIMASK0);

		ret = mmc_resume_host(mmc);
	}

	return ret;
}
#else
#define mmci_suspend	NULL
#define mmci_resume	NULL
#endif

static struct amba_id mmci_ids[] = {
	{
		.id	= 0x00041180,
		.mask	= 0x000fffff,
	},
	{
		.id	= 0x00041181,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

static struct amba_driver mmci_driver = {
	.drv		= {
		.name	= DRIVER_NAME,
	},
	.probe		= mmci_probe,
	.remove		= mmci_remove,
	.suspend	= mmci_suspend,
	.resume		= mmci_resume,
	.id_table	= mmci_ids,
};

static int __init mmci_init(void)
{
	return amba_driver_register(&mmci_driver);
}

static void __exit mmci_exit(void)
{
	amba_driver_unregister(&mmci_driver);
}

module_init(mmci_init);
module_exit(mmci_exit);
module_param(fmax, uint, 0444);

MODULE_DESCRIPTION("ARM PrimeCell PL180/181 Multimedia Card Interface driver");
MODULE_LICENSE("GPL");
