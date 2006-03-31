/*
 *  linux/drivers/mmc/pxa.c - PXA MMCI driver
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
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/mmc/protocol.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/scatterlist.h>
#include <asm/sizes.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/mmc.h>

#include "pxamci.h"

#ifdef CONFIG_MMC_DEBUG
#define DBG(x...)	printk(KERN_DEBUG x)
#else
#define DBG(x...)	do { } while (0)
#endif

#define DRIVER_NAME	"pxa2xx-mci"

#define NR_SG	1

struct pxamci_host {
	struct mmc_host		*mmc;
	spinlock_t		lock;
	struct resource		*res;
	void __iomem		*base;
	int			irq;
	int			dma;
	unsigned int		clkrt;
	unsigned int		cmdat;
	unsigned int		imask;
	unsigned int		power_mode;
	struct pxamci_platform_data *pdata;

	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	dma_addr_t		sg_dma;
	struct pxa_dma_desc	*sg_cpu;
	unsigned int		dma_len;

	unsigned int		dma_dir;
};

static inline unsigned int ns_to_clocks(unsigned int ns)
{
	return (ns * (CLOCKRATE / 1000000) + 999) / 1000;
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

static void pxamci_setup_data(struct pxamci_host *host, struct mmc_data *data)
{
	unsigned int nob = data->blocks;
	unsigned int timeout;
	u32 dcmd;
	int i;

	host->data = data;

	if (data->flags & MMC_DATA_STREAM)
		nob = 0xffff;

	writel(nob, host->base + MMC_NOB);
	writel(1 << data->blksz_bits, host->base + MMC_BLKLEN);

	timeout = ns_to_clocks(data->timeout_ns) + data->timeout_clks;
	writel((timeout + 255) / 256, host->base + MMC_RDTO);

	if (data->flags & MMC_DATA_READ) {
		host->dma_dir = DMA_FROM_DEVICE;
		dcmd = DCMD_INCTRGADDR | DCMD_FLOWTRG;
		DRCMRTXMMC = 0;
		DRCMRRXMMC = host->dma | DRCMR_MAPVLD;
	} else {
		host->dma_dir = DMA_TO_DEVICE;
		dcmd = DCMD_INCSRCADDR | DCMD_FLOWSRC;
		DRCMRRXMMC = 0;
		DRCMRTXMMC = host->dma | DRCMR_MAPVLD;
	}

	dcmd |= DCMD_BURST32 | DCMD_WIDTH1;

	host->dma_len = dma_map_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
				   host->dma_dir);

	for (i = 0; i < host->dma_len; i++) {
		if (data->flags & MMC_DATA_READ) {
			host->sg_cpu[i].dsadr = host->res->start + MMC_RXFIFO;
			host->sg_cpu[i].dtadr = sg_dma_address(&data->sg[i]);
		} else {
			host->sg_cpu[i].dsadr = sg_dma_address(&data->sg[i]);
			host->sg_cpu[i].dtadr = host->res->start + MMC_TXFIFO;
		}
		host->sg_cpu[i].dcmd = dcmd | sg_dma_len(&data->sg[i]);
		host->sg_cpu[i].ddadr = host->sg_dma + (i + 1) *
					sizeof(struct pxa_dma_desc);
	}
	host->sg_cpu[host->dma_len - 1].ddadr = DDADR_STOP;
	wmb();

	DDADR(host->dma) = host->sg_dma;
	DCSR(host->dma) = DCSR_RUN;
}

static void pxamci_start_cmd(struct pxamci_host *host, struct mmc_command *cmd, unsigned int cmdat)
{
	WARN_ON(host->cmd != NULL);
	host->cmd = cmd;

	if (cmd->flags & MMC_RSP_BUSY)
		cmdat |= CMDAT_BUSY;

#define RSP_TYPE(x)	((x) & ~(MMC_RSP_BUSY|MMC_RSP_OPCODE))
	switch (RSP_TYPE(mmc_resp_type(cmd))) {
	case RSP_TYPE(MMC_RSP_R1): /* r1, r1b, r6 */
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
	DBG("PXAMCI: request done\n");
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
		cmd->error = MMC_ERR_TIMEOUT;
	} else if (stat & STAT_RES_CRC_ERR && cmd->flags & MMC_RSP_CRC) {
#ifdef CONFIG_PXA27x
		/*
		 * workaround for erratum #42:
		 * Intel PXA27x Family Processor Specification Update Rev 001
		 */
		if (cmd->opcode == MMC_ALL_SEND_CID ||
		    cmd->opcode == MMC_SEND_CSD ||
		    cmd->opcode == MMC_SEND_CID) {
			/* a bogus CRC error can appear if the msb of
			   the 15 byte response is a one */
			if ((cmd->resp[0] & 0x80000000) == 0)
				cmd->error = MMC_ERR_BADCRC;
		} else {
			DBG("ignoring CRC from command %d - *risky*\n",cmd->opcode);
		}
#else
		cmd->error = MMC_ERR_BADCRC;
#endif
	}

	pxamci_disable_irq(host, END_CMD_RES);
	if (host->data && cmd->error == MMC_ERR_NONE) {
		pxamci_enable_irq(host, DATA_TRAN_DONE);
	} else {
		pxamci_finish_request(host, host->mrq);
	}

	return 1;
}

static int pxamci_data_done(struct pxamci_host *host, unsigned int stat)
{
	struct mmc_data *data = host->data;

	if (!data)
		return 0;

	DCSR(host->dma) = 0;
	dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->dma_len,
		     host->dma_dir);

	if (stat & STAT_READ_TIME_OUT)
		data->error = MMC_ERR_TIMEOUT;
	else if (stat & (STAT_CRC_READ_ERROR|STAT_CRC_WRITE_ERROR))
		data->error = MMC_ERR_BADCRC;

	/*
	 * There appears to be a hardware design bug here.  There seems to
	 * be no way to find out how much data was transferred to the card.
	 * This means that if there was an error on any block, we mark all
	 * data blocks as being in error.
	 */
	if (data->error == MMC_ERR_NONE)
		data->bytes_xfered = data->blocks << data->blksz_bits;
	else
		data->bytes_xfered = 0;

	pxamci_disable_irq(host, DATA_TRAN_DONE);

	host->data = NULL;
	if (host->mrq->stop && data->error == MMC_ERR_NONE) {
		pxamci_stop_clock(host);
		pxamci_start_cmd(host, host->mrq->stop, 0);
	} else {
		pxamci_finish_request(host, host->mrq);
	}

	return 1;
}

static irqreturn_t pxamci_irq(int irq, void *devid, struct pt_regs *regs)
{
	struct pxamci_host *host = devid;
	unsigned int ireg;
	int handled = 0;

	ireg = readl(host->base + MMC_I_REG);

	DBG("PXAMCI: irq %08x\n", ireg);

	if (ireg) {
		unsigned stat = readl(host->base + MMC_STAT);

		DBG("PXAMCI: stat %08x\n", stat);

		if (ireg & END_CMD_RES)
			handled |= pxamci_cmd_done(host, stat);
		if (ireg & DATA_TRAN_DONE)
			handled |= pxamci_data_done(host, stat);
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

		if (mrq->data->flags & MMC_DATA_STREAM)
			cmdat |= CMDAT_STREAM;
	}

	pxamci_start_cmd(host, mrq->cmd, cmdat);
}

static int pxamci_get_ro(struct mmc_host *mmc)
{
	struct pxamci_host *host = mmc_priv(mmc);

	if (host->pdata && host->pdata->get_ro)
		return host->pdata->get_ro(mmc->dev);
	/* Host doesn't support read only detection so assume writeable */
	return 0;
}

static void pxamci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct pxamci_host *host = mmc_priv(mmc);

	DBG("pxamci_set_ios: clock %u power %u vdd %u.%02u\n",
	    ios->clock, ios->power_mode, ios->vdd / 100,
	    ios->vdd % 100);

	if (ios->clock) {
		unsigned int clk = CLOCKRATE / ios->clock;
		if (CLOCKRATE / clk > ios->clock)
			clk <<= 1;
		host->clkrt = fls(clk) - 1;
		pxa_set_cken(CKEN12_MMC, 1);

		/*
		 * we write clkrt on the next command
		 */
	} else {
		pxamci_stop_clock(host);
		pxa_set_cken(CKEN12_MMC, 0);
	}

	if (host->power_mode != ios->power_mode) {
		host->power_mode = ios->power_mode;

		if (host->pdata && host->pdata->setpower)
			host->pdata->setpower(mmc->dev, ios->vdd);

		if (ios->power_mode == MMC_POWER_ON)
			host->cmdat |= CMDAT_INIT;
	}

	DBG("pxamci_set_ios: clkrt = %x cmdat = %x\n",
	    host->clkrt, host->cmdat);
}

static struct mmc_host_ops pxamci_ops = {
	.request	= pxamci_request,
	.get_ro		= pxamci_get_ro,
	.set_ios	= pxamci_set_ios,
};

static void pxamci_dma_irq(int dma, void *devid, struct pt_regs *regs)
{
	printk(KERN_ERR "DMA%d: IRQ???\n", dma);
	DCSR(dma) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
}

static irqreturn_t pxamci_detect_irq(int irq, void *devid, struct pt_regs *regs)
{
	struct pxamci_host *host = mmc_priv(devid);

	mmc_detect_change(devid, host->pdata->detect_delay);
	return IRQ_HANDLED;
}

static int pxamci_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct pxamci_host *host = NULL;
	struct resource *r;
	int ret, irq;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!r || irq < 0)
		return -ENXIO;

	r = request_mem_region(r->start, SZ_4K, DRIVER_NAME);
	if (!r)
		return -EBUSY;

	mmc = mmc_alloc_host(sizeof(struct pxamci_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto out;
	}

	mmc->ops = &pxamci_ops;
	mmc->f_min = CLOCKRATE_MIN;
	mmc->f_max = CLOCKRATE_MAX;

	/*
	 * We can do SG-DMA, but we don't because we never know how much
	 * data we successfully wrote to the card.
	 */
	mmc->max_phys_segs = NR_SG;

	/*
	 * Our hardware DMA can handle a maximum of one page per SG entry.
	 */
	mmc->max_seg_size = PAGE_SIZE;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dma = -1;
	host->pdata = pdev->dev.platform_data;
	mmc->ocr_avail = host->pdata ?
			 host->pdata->ocr_mask :
			 MMC_VDD_32_33|MMC_VDD_33_34;

	host->sg_cpu = dma_alloc_coherent(&pdev->dev, PAGE_SIZE, &host->sg_dma, GFP_KERNEL);
	if (!host->sg_cpu) {
		ret = -ENOMEM;
		goto out;
	}

	spin_lock_init(&host->lock);
	host->res = r;
	host->irq = irq;
	host->imask = MMC_I_MASK_ALL;

	host->base = ioremap(r->start, SZ_4K);
	if (!host->base) {
		ret = -ENOMEM;
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

	host->dma = pxa_request_dma(DRIVER_NAME, DMA_PRIO_LOW,
				    pxamci_dma_irq, host);
	if (host->dma < 0) {
		ret = -EBUSY;
		goto out;
	}

	ret = request_irq(host->irq, pxamci_irq, 0, DRIVER_NAME, host);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, mmc);

	if (host->pdata && host->pdata->init)
		host->pdata->init(&pdev->dev, pxamci_detect_irq, mmc);

	mmc_add_host(mmc);

	return 0;

 out:
	if (host) {
		if (host->dma >= 0)
			pxa_free_dma(host->dma);
		if (host->base)
			iounmap(host->base);
		if (host->sg_cpu)
			dma_free_coherent(&pdev->dev, PAGE_SIZE, host->sg_cpu, host->sg_dma);
	}
	if (mmc)
		mmc_free_host(mmc);
	release_resource(r);
	return ret;
}

static int pxamci_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (mmc) {
		struct pxamci_host *host = mmc_priv(mmc);

		if (host->pdata && host->pdata->exit)
			host->pdata->exit(&pdev->dev, mmc);

		mmc_remove_host(mmc);

		pxamci_stop_clock(host);
		writel(TXFIFO_WR_REQ|RXFIFO_RD_REQ|CLK_IS_OFF|STOP_CMD|
		       END_CMD_RES|PRG_DONE|DATA_TRAN_DONE,
		       host->base + MMC_I_MASK);

		DRCMRRXMMC = 0;
		DRCMRTXMMC = 0;

		free_irq(host->irq, host);
		pxa_free_dma(host->dma);
		iounmap(host->base);
		dma_free_coherent(&pdev->dev, PAGE_SIZE, host->sg_cpu, host->sg_dma);

		release_resource(host->res);

		mmc_free_host(mmc);
	}
	return 0;
}

#ifdef CONFIG_PM
static int pxamci_suspend(struct platform_device *dev, pm_message_t state)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);
	int ret = 0;

	if (mmc)
		ret = mmc_suspend_host(mmc, state);

	return ret;
}

static int pxamci_resume(struct platform_device *dev)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);
	int ret = 0;

	if (mmc)
		ret = mmc_resume_host(mmc);

	return ret;
}
#else
#define pxamci_suspend	NULL
#define pxamci_resume	NULL
#endif

static struct platform_driver pxamci_driver = {
	.probe		= pxamci_probe,
	.remove		= pxamci_remove,
	.suspend	= pxamci_suspend,
	.resume		= pxamci_resume,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __init pxamci_init(void)
{
	return platform_driver_register(&pxamci_driver);
}

static void __exit pxamci_exit(void)
{
	platform_driver_unregister(&pxamci_driver);
}

module_init(pxamci_init);
module_exit(pxamci_exit);

MODULE_DESCRIPTION("PXA Multimedia Card Interface Driver");
MODULE_LICENSE("GPL");
