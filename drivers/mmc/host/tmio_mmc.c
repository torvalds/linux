/*
 *  linux/drivers/mmc/tmio_mmc.c
 *
 *  Copyright (C) 2004 Ian Molton
 *  Copyright (C) 2007 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the MMC / SD / SDIO cell found in:
 *
 * TC6393XB TC6391XB TC6387XB T7L66XB
 *
 * This driver draws mainly on scattered spec sheets, Reverse engineering
 * of the toshiba e800  SD driver and some parts of the 2.4 ASIC3 driver (4 bit
 * support). (Further 4 bit support from a later datasheet).
 *
 * TODO:
 *   Investigate using a workqueue for PIO transfers
 *   Eliminate FIXMEs
 *   SDIO support
 *   Better Power management
 *   Handle MMC errors better
 *   double buffer support
 *
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>

#include "tmio_mmc.h"

/*
 * Fixme - documentation conflicts on what the clock values are for the
 * various dividers.
 * One document I have says that its a divisor of a 24MHz clock, another 33.
 * This probably depends on HCLK for a given platform, so we may need to
 * require HCLK be passed to us from the MFD core.
 *
 */

static void tmio_mmc_set_clock(struct tmio_mmc_host *host, int new_clock)
{
	void __iomem *cnf = host->cnf;
	void __iomem *ctl = host->ctl;
	u32 clk = 0, clock;

	if (new_clock) {
		for (clock = 46875, clk = 0x100; new_clock >= (clock<<1); ) {
			clock <<= 1;
			clk >>= 1;
		}
		if (clk & 0x1)
			clk = 0x20000;

		clk >>= 2;
		tmio_iowrite8((clk & 0x8000) ? 0 : 1, cnf + CNF_SD_CLK_MODE);
		clk |= 0x100;
	}

	tmio_iowrite16(clk, ctl + CTL_SD_CARD_CLK_CTL);
}

static void tmio_mmc_clk_stop(struct tmio_mmc_host *host)
{
	void __iomem *ctl = host->ctl;

	tmio_iowrite16(0x0000, ctl + CTL_CLK_AND_WAIT_CTL);
	msleep(10);
	tmio_iowrite16(tmio_ioread16(ctl + CTL_SD_CARD_CLK_CTL) & ~0x0100,
	       ctl + CTL_SD_CARD_CLK_CTL);
	msleep(10);
}

static void tmio_mmc_clk_start(struct tmio_mmc_host *host)
{
	void __iomem *ctl = host->ctl;

	tmio_iowrite16(tmio_ioread16(ctl + CTL_SD_CARD_CLK_CTL) | 0x0100,
	       ctl + CTL_SD_CARD_CLK_CTL);
	msleep(10);
	tmio_iowrite16(0x0100, ctl + CTL_CLK_AND_WAIT_CTL);
	msleep(10);
}

static void reset(struct tmio_mmc_host *host)
{
	void __iomem *ctl = host->ctl;

	/* FIXME - should we set stop clock reg here */
	tmio_iowrite16(0x0000, ctl + CTL_RESET_SD);
	tmio_iowrite16(0x0000, ctl + CTL_RESET_SDIO);
	msleep(10);
	tmio_iowrite16(0x0001, ctl + CTL_RESET_SD);
	tmio_iowrite16(0x0001, ctl + CTL_RESET_SDIO);
	msleep(10);
}

static void
tmio_mmc_finish_request(struct tmio_mmc_host *host)
{
	struct mmc_request *mrq = host->mrq;

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	mmc_request_done(host->mmc, mrq);
}

/* These are the bitmasks the tmio chip requires to implement the MMC response
 * types. Note that R1 and R6 are the same in this scheme. */
#define APP_CMD        0x0040
#define RESP_NONE      0x0300
#define RESP_R1        0x0400
#define RESP_R1B       0x0500
#define RESP_R2        0x0600
#define RESP_R3        0x0700
#define DATA_PRESENT   0x0800
#define TRANSFER_READ  0x1000
#define TRANSFER_MULTI 0x2000
#define SECURITY_CMD   0x4000

static int
tmio_mmc_start_command(struct tmio_mmc_host *host, struct mmc_command *cmd)
{
	void __iomem *ctl = host->ctl;
	struct mmc_data *data = host->data;
	int c = cmd->opcode;

	/* Command 12 is handled by hardware */
	if (cmd->opcode == 12 && !cmd->arg) {
		tmio_iowrite16(0x001, ctl + CTL_STOP_INTERNAL_ACTION);
		return 0;
	}

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE: c |= RESP_NONE; break;
	case MMC_RSP_R1:   c |= RESP_R1;   break;
	case MMC_RSP_R1B:  c |= RESP_R1B;  break;
	case MMC_RSP_R2:   c |= RESP_R2;   break;
	case MMC_RSP_R3:   c |= RESP_R3;   break;
	default:
		pr_debug("Unknown response type %d\n", mmc_resp_type(cmd));
		return -EINVAL;
	}

	host->cmd = cmd;

/* FIXME - this seems to be ok comented out but the spec suggest this bit should
 *         be set when issuing app commands.
 *	if(cmd->flags & MMC_FLAG_ACMD)
 *		c |= APP_CMD;
 */
	if (data) {
		c |= DATA_PRESENT;
		if (data->blocks > 1) {
			tmio_iowrite16(0x100, ctl + CTL_STOP_INTERNAL_ACTION);
			c |= TRANSFER_MULTI;
		}
		if (data->flags & MMC_DATA_READ)
			c |= TRANSFER_READ;
	}

	enable_mmc_irqs(ctl, TMIO_MASK_CMD);

	/* Fire off the command */
	tmio_iowrite32(cmd->arg, ctl + CTL_ARG_REG);
	tmio_iowrite16(c, ctl + CTL_SD_CMD);

	return 0;
}

/* This chip always returns (at least?) as much data as you ask for.
 * I'm unsure what happens if you ask for less than a block. This should be
 * looked into to ensure that a funny length read doesnt hose the controller.
 *
 */
static inline void tmio_mmc_pio_irq(struct tmio_mmc_host *host)
{
	void __iomem *ctl = host->ctl;
	struct mmc_data *data = host->data;
	unsigned short *buf;
	unsigned int count;
	unsigned long flags;

	if (!data) {
		pr_debug("Spurious PIO IRQ\n");
		return;
	}

	buf = (unsigned short *)(tmio_mmc_kmap_atomic(host, &flags) +
	      host->sg_off);

	count = host->sg_ptr->length - host->sg_off;
	if (count > data->blksz)
		count = data->blksz;

	pr_debug("count: %08x offset: %08x flags %08x\n",
	    count, host->sg_off, data->flags);

	/* Transfer the data */
	if (data->flags & MMC_DATA_READ)
		tmio_ioread16_rep(ctl + CTL_SD_DATA_PORT, buf, count >> 1);
	else
		tmio_iowrite16_rep(ctl + CTL_SD_DATA_PORT, buf, count >> 1);

	host->sg_off += count;

	tmio_mmc_kunmap_atomic(host, &flags);

	if (host->sg_off == host->sg_ptr->length)
		tmio_mmc_next_sg(host);

	return;
}

static inline void tmio_mmc_data_irq(struct tmio_mmc_host *host)
{
	void __iomem *ctl = host->ctl;
	struct mmc_data *data = host->data;
	struct mmc_command *stop = data->stop;

	host->data = NULL;

	if (!data) {
		pr_debug("Spurious data end IRQ\n");
		return;
	}

	/* FIXME - return correct transfer count on errors */
	if (!data->error)
		data->bytes_xfered = data->blocks * data->blksz;
	else
		data->bytes_xfered = 0;

	pr_debug("Completed data request\n");

	/*FIXME - other drivers allow an optional stop command of any given type
	 *        which we dont do, as the chip can auto generate them.
	 *        Perhaps we can be smarter about when to use auto CMD12 and
	 *        only issue the auto request when we know this is the desired
	 *        stop command, allowing fallback to the stop command the
	 *        upper layers expect. For now, we do what works.
	 */

	if (data->flags & MMC_DATA_READ)
		disable_mmc_irqs(ctl, TMIO_MASK_READOP);
	else
		disable_mmc_irqs(ctl, TMIO_MASK_WRITEOP);

	if (stop) {
		if (stop->opcode == 12 && !stop->arg)
			tmio_iowrite16(0x000, ctl + CTL_STOP_INTERNAL_ACTION);
		else
			BUG();
	}

	tmio_mmc_finish_request(host);
}

static inline void tmio_mmc_cmd_irq(struct tmio_mmc_host *host,
	unsigned int stat)
{
	void __iomem *ctl = host->ctl, *addr;
	struct mmc_command *cmd = host->cmd;
	int i;

	if (!host->cmd) {
		pr_debug("Spurious CMD irq\n");
		return;
	}

	host->cmd = NULL;

	/* This controller is sicker than the PXA one. Not only do we need to
	 * drop the top 8 bits of the first response word, we also need to
	 * modify the order of the response for short response command types.
	 */

	for (i = 3, addr = ctl + CTL_RESPONSE ; i >= 0 ; i--, addr += 4)
		cmd->resp[i] = tmio_ioread32(addr);

	if (cmd->flags &  MMC_RSP_136) {
		cmd->resp[0] = (cmd->resp[0] << 8) | (cmd->resp[1] >> 24);
		cmd->resp[1] = (cmd->resp[1] << 8) | (cmd->resp[2] >> 24);
		cmd->resp[2] = (cmd->resp[2] << 8) | (cmd->resp[3] >> 24);
		cmd->resp[3] <<= 8;
	} else if (cmd->flags & MMC_RSP_R3) {
		cmd->resp[0] = cmd->resp[3];
	}

	if (stat & TMIO_STAT_CMDTIMEOUT)
		cmd->error = -ETIMEDOUT;
	else if (stat & TMIO_STAT_CRCFAIL && cmd->flags & MMC_RSP_CRC)
		cmd->error = -EILSEQ;

	/* If there is data to handle we enable data IRQs here, and
	 * we will ultimatley finish the request in the data_end handler.
	 * If theres no data or we encountered an error, finish now.
	 */
	if (host->data && !cmd->error) {
		if (host->data->flags & MMC_DATA_READ)
			enable_mmc_irqs(ctl, TMIO_MASK_READOP);
		else
			enable_mmc_irqs(ctl, TMIO_MASK_WRITEOP);
	} else {
		tmio_mmc_finish_request(host);
	}

	return;
}


static irqreturn_t tmio_mmc_irq(int irq, void *devid)
{
	struct tmio_mmc_host *host = devid;
	void __iomem *ctl = host->ctl;
	unsigned int ireg, irq_mask, status;

	pr_debug("MMC IRQ begin\n");

	status = tmio_ioread32(ctl + CTL_STATUS);
	irq_mask = tmio_ioread32(ctl + CTL_IRQ_MASK);
	ireg = status & TMIO_MASK_IRQ & ~irq_mask;

	pr_debug_status(status);
	pr_debug_status(ireg);

	if (!ireg) {
		disable_mmc_irqs(ctl, status & ~irq_mask);

		pr_debug("tmio_mmc: Spurious irq, disabling! "
			"0x%08x 0x%08x 0x%08x\n", status, irq_mask, ireg);
		pr_debug_status(status);

		goto out;
	}

	while (ireg) {
		/* Card insert / remove attempts */
		if (ireg & (TMIO_STAT_CARD_INSERT | TMIO_STAT_CARD_REMOVE)) {
			ack_mmc_irqs(ctl, TMIO_STAT_CARD_INSERT |
				TMIO_STAT_CARD_REMOVE);
			mmc_detect_change(host->mmc, 0);
		}

		/* CRC and other errors */
/*		if (ireg & TMIO_STAT_ERR_IRQ)
 *			handled |= tmio_error_irq(host, irq, stat);
 */

		/* Command completion */
		if (ireg & TMIO_MASK_CMD) {
			ack_mmc_irqs(ctl, TMIO_MASK_CMD);
			tmio_mmc_cmd_irq(host, status);
		}

		/* Data transfer */
		if (ireg & (TMIO_STAT_RXRDY | TMIO_STAT_TXRQ)) {
			ack_mmc_irqs(ctl, TMIO_STAT_RXRDY | TMIO_STAT_TXRQ);
			tmio_mmc_pio_irq(host);
		}

		/* Data transfer completion */
		if (ireg & TMIO_STAT_DATAEND) {
			ack_mmc_irqs(ctl, TMIO_STAT_DATAEND);
			tmio_mmc_data_irq(host);
		}

		/* Check status - keep going until we've handled it all */
		status = tmio_ioread32(ctl + CTL_STATUS);
		irq_mask = tmio_ioread32(ctl + CTL_IRQ_MASK);
		ireg = status & TMIO_MASK_IRQ & ~irq_mask;

		pr_debug("Status at end of loop: %08x\n", status);
		pr_debug_status(status);
	}
	pr_debug("MMC IRQ end\n");

out:
	return IRQ_HANDLED;
}

static int tmio_mmc_start_data(struct tmio_mmc_host *host,
	struct mmc_data *data)
{
	void __iomem *ctl = host->ctl;

	pr_debug("setup data transfer: blocksize %08x  nr_blocks %d\n",
	    data->blksz, data->blocks);

	/* Hardware cannot perform 1 and 2 byte requests in 4 bit mode */
	if (data->blksz < 4 && host->mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
		printk(KERN_ERR "%s: %d byte block unsupported in 4 bit mode\n",
			mmc_hostname(host->mmc), data->blksz);
		return -EINVAL;
	}

	tmio_mmc_init_sg(host, data);
	host->data = data;

	/* Set transfer length / blocksize */
	tmio_iowrite16(data->blksz,  ctl + CTL_SD_XFER_LEN);
	tmio_iowrite16(data->blocks, ctl + CTL_XFER_BLK_COUNT);

	return 0;
}

/* Process requests from the MMC layer */
static void tmio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	int ret;

	if (host->mrq)
		pr_debug("request not null\n");

	host->mrq = mrq;

	if (mrq->data) {
		ret = tmio_mmc_start_data(host, mrq->data);
		if (ret)
			goto fail;
	}

	ret = tmio_mmc_start_command(host, mrq->cmd);

	if (!ret)
		return;

fail:
	mrq->cmd->error = ret;
	mmc_request_done(mmc, mrq);
}

/* Set MMC clock / power.
 * Note: This controller uses a simple divider scheme therefore it cannot
 * run a MMC card at full speed (20MHz). The max clock is 24MHz on SD, but as
 * MMC wont run that fast, it has to be clocked at 12MHz which is the next
 * slowest setting.
 */
static void tmio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	void __iomem *cnf = host->cnf;
	void __iomem *ctl = host->ctl;

	if (ios->clock)
		tmio_mmc_set_clock(host, ios->clock);

	/* Power sequence - OFF -> ON -> UP */
	switch (ios->power_mode) {
	case MMC_POWER_OFF: /* power down SD bus */
		tmio_iowrite8(0x00, cnf + CNF_PWR_CTL_2);
		tmio_mmc_clk_stop(host);
		break;
	case MMC_POWER_ON: /* power up SD bus */

		tmio_iowrite8(0x02, cnf + CNF_PWR_CTL_2);
		break;
	case MMC_POWER_UP: /* start bus clock */
		tmio_mmc_clk_start(host);
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		tmio_iowrite16(0x80e0, ctl + CTL_SD_MEM_CARD_OPT);
	break;
	case MMC_BUS_WIDTH_4:
		tmio_iowrite16(0x00e0, ctl + CTL_SD_MEM_CARD_OPT);
	break;
	}

	/* Let things settle. delay taken from winCE driver */
	udelay(140);
}

static int tmio_mmc_get_ro(struct mmc_host *mmc)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	void __iomem *ctl = host->ctl;

	return (tmio_ioread16(ctl + CTL_STATUS) & TMIO_STAT_WRPROTECT) ? 0 : 1;
}

static struct mmc_host_ops tmio_mmc_ops = {
	.request	= tmio_mmc_request,
	.set_ios	= tmio_mmc_set_ios,
	.get_ro         = tmio_mmc_get_ro,
};

#ifdef CONFIG_PM
static int tmio_mmc_suspend(struct platform_device *dev, pm_message_t state)
{
	struct mfd_cell	*cell = (struct mfd_cell *)dev->dev.platform_data;
	struct mmc_host *mmc = platform_get_drvdata(dev);
	int ret;

	ret = mmc_suspend_host(mmc, state);

	/* Tell MFD core it can disable us now.*/
	if (!ret && cell->disable)
		cell->disable(dev);

	return ret;
}

static int tmio_mmc_resume(struct platform_device *dev)
{
	struct mfd_cell	*cell = (struct mfd_cell *)dev->dev.platform_data;
	struct mmc_host *mmc = platform_get_drvdata(dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	void __iomem *cnf = host->cnf;
	int ret = 0;

	/* Enable the MMC/SD Control registers */
	tmio_iowrite16(SDCREN, cnf + CNF_CMD);
	tmio_iowrite32(dev->resource[0].start & 0xfffe, cnf + CNF_CTL_BASE);

	/* Tell the MFD core we are ready to be enabled */
	if (cell->enable) {
		ret = cell->enable(dev);
		if (ret)
			goto out;
	}

	mmc_resume_host(mmc);

out:
	return ret;
}
#else
#define tmio_mmc_suspend NULL
#define tmio_mmc_resume NULL
#endif

static int __devinit tmio_mmc_probe(struct platform_device *dev)
{
	struct mfd_cell	*cell = (struct mfd_cell *)dev->dev.platform_data;
	struct resource *res_ctl, *res_cnf;
	struct tmio_mmc_host *host;
	struct mmc_host *mmc;
	int ret = -ENOMEM;

	if (dev->num_resources != 3)
		goto out;

	res_ctl = platform_get_resource(dev, IORESOURCE_MEM, 0);
	res_cnf = platform_get_resource(dev, IORESOURCE_MEM, 1);
	if (!res_ctl || !res_cnf) {
		ret = -EINVAL;
		goto out;
	}

	mmc = mmc_alloc_host(sizeof(struct tmio_mmc_host), &dev->dev);
	if (!mmc)
		goto out;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	platform_set_drvdata(dev, mmc);

	host->ctl = ioremap(res_ctl->start, res_ctl->end - res_ctl->start);
	if (!host->ctl)
		goto host_free;

	host->cnf = ioremap(res_cnf->start, res_cnf->end - res_cnf->start);
	if (!host->cnf)
		goto unmap_ctl;

	mmc->ops = &tmio_mmc_ops;
	mmc->caps = MMC_CAP_4_BIT_DATA;
	mmc->f_min = 46875; /* 24000000 / 512 */
	mmc->f_max = 24000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/* Enable the MMC/SD Control registers */
	tmio_iowrite16(SDCREN, host->cnf + CNF_CMD);
	tmio_iowrite32(dev->resource[0].start & 0xfffe,
		host->cnf + CNF_CTL_BASE);

	/* Tell the MFD core we are ready to be enabled */
	if (cell->enable) {
		ret = cell->enable(dev);
		if (ret)
			goto unmap_cnf;
	}

	/* Disable SD power during suspend */
	tmio_iowrite8(0x01, host->cnf + CNF_PWR_CTL_3);

	/* The below is required but why? FIXME */
	tmio_iowrite8(0x1f, host->cnf + CNF_STOP_CLK_CTL);

	/* Power down SD bus*/
	tmio_iowrite8(0x0,  host->cnf + CNF_PWR_CTL_2);

	tmio_mmc_clk_stop(host);
	reset(host);

	ret = platform_get_irq(dev, 0);
	if (ret >= 0)
		host->irq = ret;
	else
		goto unmap_cnf;

	disable_mmc_irqs(host->ctl, TMIO_MASK_ALL);

	ret = request_irq(host->irq, tmio_mmc_irq, IRQF_DISABLED, "tmio-mmc",
		host);
	if (ret)
		goto unmap_cnf;

	set_irq_type(host->irq, IRQ_TYPE_EDGE_FALLING);

	mmc_add_host(mmc);

	printk(KERN_INFO "%s at 0x%08lx irq %d\n", mmc_hostname(host->mmc),
	       (unsigned long)host->ctl, host->irq);

	/* Unmask the IRQs we want to know about */
	enable_mmc_irqs(host->ctl,  TMIO_MASK_IRQ);

	return 0;

unmap_cnf:
	iounmap(host->cnf);
unmap_ctl:
	iounmap(host->ctl);
host_free:
	mmc_free_host(mmc);
out:
	return ret;
}

static int __devexit tmio_mmc_remove(struct platform_device *dev)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if (mmc) {
		struct tmio_mmc_host *host = mmc_priv(mmc);
		mmc_remove_host(mmc);
		mmc_free_host(mmc);
		free_irq(host->irq, host);
		iounmap(host->ctl);
		iounmap(host->cnf);
	}

	return 0;
}

/* ------------------- device registration ----------------------- */

static struct platform_driver tmio_mmc_driver = {
	.driver = {
		.name = "tmio-mmc",
		.owner = THIS_MODULE,
	},
	.probe = tmio_mmc_probe,
	.remove = __devexit_p(tmio_mmc_remove),
	.suspend = tmio_mmc_suspend,
	.resume = tmio_mmc_resume,
};


static int __init tmio_mmc_init(void)
{
	return platform_driver_register(&tmio_mmc_driver);
}

static void __exit tmio_mmc_exit(void)
{
	platform_driver_unregister(&tmio_mmc_driver);
}

module_init(tmio_mmc_init);
module_exit(tmio_mmc_exit);

MODULE_DESCRIPTION("Toshiba TMIO SD/MMC driver");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tmio-mmc");
