/*
 * linux/drivers/mmc/host/tmio_mmc_pio.c
 *
 * Copyright (C) 2011 Guennadi Liakhovetski
 * Copyright (C) 2007 Ian Molton
 * Copyright (C) 2004 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the MMC / SD / SDIO IP found in:
 *
 * TC6393XB, TC6391XB, TC6387XB, T7L66XB, ASIC3, SH-Mobile SoCs
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

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/tmio.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#include "tmio_mmc.h"

static u16 sd_ctrl_read16(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift));
}

static void sd_ctrl_read16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	readsw(host->ctl + (addr << host->bus_shift), buf, count);
}

static u32 sd_ctrl_read32(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift)) |
	       readw(host->ctl + ((addr + 2) << host->bus_shift)) << 16;
}

static void sd_ctrl_write16(struct tmio_mmc_host *host, int addr, u16 val)
{
	writew(val, host->ctl + (addr << host->bus_shift));
}

static void sd_ctrl_write16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	writesw(host->ctl + (addr << host->bus_shift), buf, count);
}

static void sd_ctrl_write32(struct tmio_mmc_host *host, int addr, u32 val)
{
	writew(val, host->ctl + (addr << host->bus_shift));
	writew(val >> 16, host->ctl + ((addr + 2) << host->bus_shift));
}

void tmio_mmc_enable_mmc_irqs(struct tmio_mmc_host *host, u32 i)
{
	u32 mask = sd_ctrl_read32(host, CTL_IRQ_MASK) & ~(i & TMIO_MASK_IRQ);
	sd_ctrl_write32(host, CTL_IRQ_MASK, mask);
}

void tmio_mmc_disable_mmc_irqs(struct tmio_mmc_host *host, u32 i)
{
	u32 mask = sd_ctrl_read32(host, CTL_IRQ_MASK) | (i & TMIO_MASK_IRQ);
	sd_ctrl_write32(host, CTL_IRQ_MASK, mask);
}

static void tmio_mmc_ack_mmc_irqs(struct tmio_mmc_host *host, u32 i)
{
	sd_ctrl_write32(host, CTL_STATUS, ~i);
}

static void tmio_mmc_init_sg(struct tmio_mmc_host *host, struct mmc_data *data)
{
	host->sg_len = data->sg_len;
	host->sg_ptr = data->sg;
	host->sg_orig = data->sg;
	host->sg_off = 0;
}

static int tmio_mmc_next_sg(struct tmio_mmc_host *host)
{
	host->sg_ptr = sg_next(host->sg_ptr);
	host->sg_off = 0;
	return --host->sg_len;
}

#ifdef CONFIG_MMC_DEBUG

#define STATUS_TO_TEXT(a, status, i) \
	do { \
		if (status & TMIO_STAT_##a) { \
			if (i++) \
				printk(" | "); \
			printk(#a); \
		} \
	} while (0)

static void pr_debug_status(u32 status)
{
	int i = 0;
	printk(KERN_DEBUG "status: %08x = ", status);
	STATUS_TO_TEXT(CARD_REMOVE, status, i);
	STATUS_TO_TEXT(CARD_INSERT, status, i);
	STATUS_TO_TEXT(SIGSTATE, status, i);
	STATUS_TO_TEXT(WRPROTECT, status, i);
	STATUS_TO_TEXT(CARD_REMOVE_A, status, i);
	STATUS_TO_TEXT(CARD_INSERT_A, status, i);
	STATUS_TO_TEXT(SIGSTATE_A, status, i);
	STATUS_TO_TEXT(CMD_IDX_ERR, status, i);
	STATUS_TO_TEXT(STOPBIT_ERR, status, i);
	STATUS_TO_TEXT(ILL_FUNC, status, i);
	STATUS_TO_TEXT(CMD_BUSY, status, i);
	STATUS_TO_TEXT(CMDRESPEND, status, i);
	STATUS_TO_TEXT(DATAEND, status, i);
	STATUS_TO_TEXT(CRCFAIL, status, i);
	STATUS_TO_TEXT(DATATIMEOUT, status, i);
	STATUS_TO_TEXT(CMDTIMEOUT, status, i);
	STATUS_TO_TEXT(RXOVERFLOW, status, i);
	STATUS_TO_TEXT(TXUNDERRUN, status, i);
	STATUS_TO_TEXT(RXRDY, status, i);
	STATUS_TO_TEXT(TXRQ, status, i);
	STATUS_TO_TEXT(ILL_ACCESS, status, i);
	printk("\n");
}

#else
#define pr_debug_status(s)  do { } while (0)
#endif

static void tmio_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);

	if (enable) {
		host->sdio_irq_enabled = 1;
		sd_ctrl_write16(host, CTL_TRANSACTION_CTL, 0x0001);
		sd_ctrl_write16(host, CTL_SDIO_IRQ_MASK,
			(TMIO_SDIO_MASK_ALL & ~TMIO_SDIO_STAT_IOIRQ));
	} else {
		sd_ctrl_write16(host, CTL_SDIO_IRQ_MASK, TMIO_SDIO_MASK_ALL);
		sd_ctrl_write16(host, CTL_TRANSACTION_CTL, 0x0000);
		host->sdio_irq_enabled = 0;
	}
}

static void tmio_mmc_set_clock(struct tmio_mmc_host *host, int new_clock)
{
	u32 clk = 0, clock;

	if (new_clock) {
		for (clock = host->mmc->f_min, clk = 0x80000080;
			new_clock >= (clock<<1); clk >>= 1)
			clock <<= 1;
		clk |= 0x100;
	}

	if (host->set_clk_div)
		host->set_clk_div(host->pdev, (clk>>22) & 1);

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, clk & 0x1ff);
}

static void tmio_mmc_clk_stop(struct tmio_mmc_host *host)
{
	struct resource *res = platform_get_resource(host->pdev, IORESOURCE_MEM, 0);

	/* implicit BUG_ON(!res) */
	if (resource_size(res) > 0x100) {
		sd_ctrl_write16(host, CTL_CLK_AND_WAIT_CTL, 0x0000);
		msleep(10);
	}

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));
	msleep(10);
}

static void tmio_mmc_clk_start(struct tmio_mmc_host *host)
{
	struct resource *res = platform_get_resource(host->pdev, IORESOURCE_MEM, 0);

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x0100 |
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));
	msleep(10);

	/* implicit BUG_ON(!res) */
	if (resource_size(res) > 0x100) {
		sd_ctrl_write16(host, CTL_CLK_AND_WAIT_CTL, 0x0100);
		msleep(10);
	}
}

static void tmio_mmc_reset(struct tmio_mmc_host *host)
{
	struct resource *res = platform_get_resource(host->pdev, IORESOURCE_MEM, 0);

	/* FIXME - should we set stop clock reg here */
	sd_ctrl_write16(host, CTL_RESET_SD, 0x0000);
	/* implicit BUG_ON(!res) */
	if (resource_size(res) > 0x100)
		sd_ctrl_write16(host, CTL_RESET_SDIO, 0x0000);
	msleep(10);
	sd_ctrl_write16(host, CTL_RESET_SD, 0x0001);
	if (resource_size(res) > 0x100)
		sd_ctrl_write16(host, CTL_RESET_SDIO, 0x0001);
	msleep(10);
}

static void tmio_mmc_reset_work(struct work_struct *work)
{
	struct tmio_mmc_host *host = container_of(work, struct tmio_mmc_host,
						  delayed_reset_work.work);
	struct mmc_request *mrq;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	mrq = host->mrq;

	/*
	 * is request already finished? Since we use a non-blocking
	 * cancel_delayed_work(), it can happen, that a .set_ios() call preempts
	 * us, so, have to check for IS_ERR(host->mrq)
	 */
	if (IS_ERR_OR_NULL(mrq)
	    || time_is_after_jiffies(host->last_req_ts +
		msecs_to_jiffies(2000))) {
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	dev_warn(&host->pdev->dev,
		"timeout waiting for hardware interrupt (CMD%u)\n",
		mrq->cmd->opcode);

	if (host->data)
		host->data->error = -ETIMEDOUT;
	else if (host->cmd)
		host->cmd->error = -ETIMEDOUT;
	else
		mrq->cmd->error = -ETIMEDOUT;

	host->cmd = NULL;
	host->data = NULL;
	host->force_pio = false;

	spin_unlock_irqrestore(&host->lock, flags);

	tmio_mmc_reset(host);

	/* Ready for new calls */
	host->mrq = NULL;

	mmc_request_done(host->mmc, mrq);
}

/* called with host->lock held, interrupts disabled */
static void tmio_mmc_finish_request(struct tmio_mmc_host *host)
{
	struct mmc_request *mrq = host->mrq;

	if (!mrq)
		return;

	host->cmd = NULL;
	host->data = NULL;
	host->force_pio = false;

	cancel_delayed_work(&host->delayed_reset_work);

	host->mrq = NULL;

	/* FIXME: mmc_request_done() can schedule! */
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

static int tmio_mmc_start_command(struct tmio_mmc_host *host, struct mmc_command *cmd)
{
	struct mmc_data *data = host->data;
	int c = cmd->opcode;

	/* Command 12 is handled by hardware */
	if (cmd->opcode == 12 && !cmd->arg) {
		sd_ctrl_write16(host, CTL_STOP_INTERNAL_ACTION, 0x001);
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

/* FIXME - this seems to be ok commented out but the spec suggest this bit
 *         should be set when issuing app commands.
 *	if(cmd->flags & MMC_FLAG_ACMD)
 *		c |= APP_CMD;
 */
	if (data) {
		c |= DATA_PRESENT;
		if (data->blocks > 1) {
			sd_ctrl_write16(host, CTL_STOP_INTERNAL_ACTION, 0x100);
			c |= TRANSFER_MULTI;
		}
		if (data->flags & MMC_DATA_READ)
			c |= TRANSFER_READ;
	}

	tmio_mmc_enable_mmc_irqs(host, TMIO_MASK_CMD);

	/* Fire off the command */
	sd_ctrl_write32(host, CTL_ARG_REG, cmd->arg);
	sd_ctrl_write16(host, CTL_SD_CMD, c);

	return 0;
}

/*
 * This chip always returns (at least?) as much data as you ask for.
 * I'm unsure what happens if you ask for less than a block. This should be
 * looked into to ensure that a funny length read doesn't hose the controller.
 */
static void tmio_mmc_pio_irq(struct tmio_mmc_host *host)
{
	struct mmc_data *data = host->data;
	void *sg_virt;
	unsigned short *buf;
	unsigned int count;
	unsigned long flags;

	if ((host->chan_tx || host->chan_rx) && !host->force_pio) {
		pr_err("PIO IRQ in DMA mode!\n");
		return;
	} else if (!data) {
		pr_debug("Spurious PIO IRQ\n");
		return;
	}

	sg_virt = tmio_mmc_kmap_atomic(host->sg_ptr, &flags);
	buf = (unsigned short *)(sg_virt + host->sg_off);

	count = host->sg_ptr->length - host->sg_off;
	if (count > data->blksz)
		count = data->blksz;

	pr_debug("count: %08x offset: %08x flags %08x\n",
		 count, host->sg_off, data->flags);

	/* Transfer the data */
	if (data->flags & MMC_DATA_READ)
		sd_ctrl_read16_rep(host, CTL_SD_DATA_PORT, buf, count >> 1);
	else
		sd_ctrl_write16_rep(host, CTL_SD_DATA_PORT, buf, count >> 1);

	host->sg_off += count;

	tmio_mmc_kunmap_atomic(host->sg_ptr, &flags, sg_virt);

	if (host->sg_off == host->sg_ptr->length)
		tmio_mmc_next_sg(host);

	return;
}

static void tmio_mmc_check_bounce_buffer(struct tmio_mmc_host *host)
{
	if (host->sg_ptr == &host->bounce_sg) {
		unsigned long flags;
		void *sg_vaddr = tmio_mmc_kmap_atomic(host->sg_orig, &flags);
		memcpy(sg_vaddr, host->bounce_buf, host->bounce_sg.length);
		tmio_mmc_kunmap_atomic(host->sg_orig, &flags, sg_vaddr);
	}
}

/* needs to be called with host->lock held */
void tmio_mmc_do_data_irq(struct tmio_mmc_host *host)
{
	struct mmc_data *data = host->data;
	struct mmc_command *stop;

	host->data = NULL;

	if (!data) {
		dev_warn(&host->pdev->dev, "Spurious data end IRQ\n");
		return;
	}
	stop = data->stop;

	/* FIXME - return correct transfer count on errors */
	if (!data->error)
		data->bytes_xfered = data->blocks * data->blksz;
	else
		data->bytes_xfered = 0;

	pr_debug("Completed data request\n");

	/*
	 * FIXME: other drivers allow an optional stop command of any given type
	 *        which we dont do, as the chip can auto generate them.
	 *        Perhaps we can be smarter about when to use auto CMD12 and
	 *        only issue the auto request when we know this is the desired
	 *        stop command, allowing fallback to the stop command the
	 *        upper layers expect. For now, we do what works.
	 */

	if (data->flags & MMC_DATA_READ) {
		if (host->chan_rx && !host->force_pio)
			tmio_mmc_check_bounce_buffer(host);
		dev_dbg(&host->pdev->dev, "Complete Rx request %p\n",
			host->mrq);
	} else {
		dev_dbg(&host->pdev->dev, "Complete Tx request %p\n",
			host->mrq);
	}

	if (stop) {
		if (stop->opcode == 12 && !stop->arg)
			sd_ctrl_write16(host, CTL_STOP_INTERNAL_ACTION, 0x000);
		else
			BUG();
	}

	tmio_mmc_finish_request(host);
}

static void tmio_mmc_data_irq(struct tmio_mmc_host *host)
{
	struct mmc_data *data;
	spin_lock(&host->lock);
	data = host->data;

	if (!data)
		goto out;

	if (host->chan_tx && (data->flags & MMC_DATA_WRITE) && !host->force_pio) {
		/*
		 * Has all data been written out yet? Testing on SuperH showed,
		 * that in most cases the first interrupt comes already with the
		 * BUSY status bit clear, but on some operations, like mount or
		 * in the beginning of a write / sync / umount, there is one
		 * DATAEND interrupt with the BUSY bit set, in this cases
		 * waiting for one more interrupt fixes the problem.
		 */
		if (!(sd_ctrl_read32(host, CTL_STATUS) & TMIO_STAT_CMD_BUSY)) {
			tmio_mmc_disable_mmc_irqs(host, TMIO_STAT_DATAEND);
			tasklet_schedule(&host->dma_complete);
		}
	} else if (host->chan_rx && (data->flags & MMC_DATA_READ) && !host->force_pio) {
		tmio_mmc_disable_mmc_irqs(host, TMIO_STAT_DATAEND);
		tasklet_schedule(&host->dma_complete);
	} else {
		tmio_mmc_do_data_irq(host);
		tmio_mmc_disable_mmc_irqs(host, TMIO_MASK_READOP | TMIO_MASK_WRITEOP);
	}
out:
	spin_unlock(&host->lock);
}

static void tmio_mmc_cmd_irq(struct tmio_mmc_host *host,
	unsigned int stat)
{
	struct mmc_command *cmd = host->cmd;
	int i, addr;

	spin_lock(&host->lock);

	if (!host->cmd) {
		pr_debug("Spurious CMD irq\n");
		goto out;
	}

	host->cmd = NULL;

	/* This controller is sicker than the PXA one. Not only do we need to
	 * drop the top 8 bits of the first response word, we also need to
	 * modify the order of the response for short response command types.
	 */

	for (i = 3, addr = CTL_RESPONSE ; i >= 0 ; i--, addr += 4)
		cmd->resp[i] = sd_ctrl_read32(host, addr);

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
		if (host->data->flags & MMC_DATA_READ) {
			if (host->force_pio || !host->chan_rx)
				tmio_mmc_enable_mmc_irqs(host, TMIO_MASK_READOP);
			else
				tasklet_schedule(&host->dma_issue);
		} else {
			if (host->force_pio || !host->chan_tx)
				tmio_mmc_enable_mmc_irqs(host, TMIO_MASK_WRITEOP);
			else
				tasklet_schedule(&host->dma_issue);
		}
	} else {
		tmio_mmc_finish_request(host);
	}

out:
	spin_unlock(&host->lock);
}

irqreturn_t tmio_mmc_irq(int irq, void *devid)
{
	struct tmio_mmc_host *host = devid;
	struct tmio_mmc_data *pdata = host->pdata;
	unsigned int ireg, irq_mask, status;
	unsigned int sdio_ireg, sdio_irq_mask, sdio_status;

	pr_debug("MMC IRQ begin\n");

	status = sd_ctrl_read32(host, CTL_STATUS);
	irq_mask = sd_ctrl_read32(host, CTL_IRQ_MASK);
	ireg = status & TMIO_MASK_IRQ & ~irq_mask;

	sdio_ireg = 0;
	if (!ireg && pdata->flags & TMIO_MMC_SDIO_IRQ) {
		sdio_status = sd_ctrl_read16(host, CTL_SDIO_STATUS);
		sdio_irq_mask = sd_ctrl_read16(host, CTL_SDIO_IRQ_MASK);
		sdio_ireg = sdio_status & TMIO_SDIO_MASK_ALL & ~sdio_irq_mask;

		sd_ctrl_write16(host, CTL_SDIO_STATUS, sdio_status & ~TMIO_SDIO_MASK_ALL);

		if (sdio_ireg && !host->sdio_irq_enabled) {
			pr_warning("tmio_mmc: Spurious SDIO IRQ, disabling! 0x%04x 0x%04x 0x%04x\n",
				   sdio_status, sdio_irq_mask, sdio_ireg);
			tmio_mmc_enable_sdio_irq(host->mmc, 0);
			goto out;
		}

		if (host->mmc->caps & MMC_CAP_SDIO_IRQ &&
			sdio_ireg & TMIO_SDIO_STAT_IOIRQ)
			mmc_signal_sdio_irq(host->mmc);

		if (sdio_ireg)
			goto out;
	}

	pr_debug_status(status);
	pr_debug_status(ireg);

	if (!ireg) {
		tmio_mmc_disable_mmc_irqs(host, status & ~irq_mask);

		pr_warning("tmio_mmc: Spurious irq, disabling! "
			"0x%08x 0x%08x 0x%08x\n", status, irq_mask, ireg);
		pr_debug_status(status);

		goto out;
	}

	while (ireg) {
		/* Card insert / remove attempts */
		if (ireg & (TMIO_STAT_CARD_INSERT | TMIO_STAT_CARD_REMOVE)) {
			tmio_mmc_ack_mmc_irqs(host, TMIO_STAT_CARD_INSERT |
				TMIO_STAT_CARD_REMOVE);
			mmc_detect_change(host->mmc, msecs_to_jiffies(100));
		}

		/* CRC and other errors */
/*		if (ireg & TMIO_STAT_ERR_IRQ)
 *			handled |= tmio_error_irq(host, irq, stat);
 */

		/* Command completion */
		if (ireg & (TMIO_STAT_CMDRESPEND | TMIO_STAT_CMDTIMEOUT)) {
			tmio_mmc_ack_mmc_irqs(host,
				     TMIO_STAT_CMDRESPEND |
				     TMIO_STAT_CMDTIMEOUT);
			tmio_mmc_cmd_irq(host, status);
		}

		/* Data transfer */
		if (ireg & (TMIO_STAT_RXRDY | TMIO_STAT_TXRQ)) {
			tmio_mmc_ack_mmc_irqs(host, TMIO_STAT_RXRDY | TMIO_STAT_TXRQ);
			tmio_mmc_pio_irq(host);
		}

		/* Data transfer completion */
		if (ireg & TMIO_STAT_DATAEND) {
			tmio_mmc_ack_mmc_irqs(host, TMIO_STAT_DATAEND);
			tmio_mmc_data_irq(host);
		}

		/* Check status - keep going until we've handled it all */
		status = sd_ctrl_read32(host, CTL_STATUS);
		irq_mask = sd_ctrl_read32(host, CTL_IRQ_MASK);
		ireg = status & TMIO_MASK_IRQ & ~irq_mask;

		pr_debug("Status at end of loop: %08x\n", status);
		pr_debug_status(status);
	}
	pr_debug("MMC IRQ end\n");

out:
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(tmio_mmc_irq);

static int tmio_mmc_start_data(struct tmio_mmc_host *host,
	struct mmc_data *data)
{
	struct tmio_mmc_data *pdata = host->pdata;

	pr_debug("setup data transfer: blocksize %08x  nr_blocks %d\n",
		 data->blksz, data->blocks);

	/* Some hardware cannot perform 2 byte requests in 4 bit mode */
	if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
		int blksz_2bytes = pdata->flags & TMIO_MMC_BLKSZ_2BYTES;

		if (data->blksz < 2 || (data->blksz < 4 && !blksz_2bytes)) {
			pr_err("%s: %d byte block unsupported in 4 bit mode\n",
			       mmc_hostname(host->mmc), data->blksz);
			return -EINVAL;
		}
	}

	tmio_mmc_init_sg(host, data);
	host->data = data;

	/* Set transfer length / blocksize */
	sd_ctrl_write16(host, CTL_SD_XFER_LEN, data->blksz);
	sd_ctrl_write16(host, CTL_XFER_BLK_COUNT, data->blocks);

	tmio_mmc_start_dma(host, data);

	return 0;
}

/* Process requests from the MMC layer */
static void tmio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&host->lock, flags);

	if (host->mrq) {
		pr_debug("request not null\n");
		if (IS_ERR(host->mrq)) {
			spin_unlock_irqrestore(&host->lock, flags);
			mrq->cmd->error = -EAGAIN;
			mmc_request_done(mmc, mrq);
			return;
		}
	}

	host->last_req_ts = jiffies;
	wmb();
	host->mrq = mrq;

	spin_unlock_irqrestore(&host->lock, flags);

	if (mrq->data) {
		ret = tmio_mmc_start_data(host, mrq->data);
		if (ret)
			goto fail;
	}

	ret = tmio_mmc_start_command(host, mrq->cmd);
	if (!ret) {
		schedule_delayed_work(&host->delayed_reset_work,
				      msecs_to_jiffies(2000));
		return;
	}

fail:
	host->force_pio = false;
	host->mrq = NULL;
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
	struct tmio_mmc_data *pdata = host->pdata;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (host->mrq) {
		if (IS_ERR(host->mrq)) {
			dev_dbg(&host->pdev->dev,
				"%s.%d: concurrent .set_ios(), clk %u, mode %u\n",
				current->comm, task_pid_nr(current),
				ios->clock, ios->power_mode);
			host->mrq = ERR_PTR(-EINTR);
		} else {
			dev_dbg(&host->pdev->dev,
				"%s.%d: CMD%u active since %lu, now %lu!\n",
				current->comm, task_pid_nr(current),
				host->mrq->cmd->opcode, host->last_req_ts, jiffies);
		}
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	host->mrq = ERR_PTR(-EBUSY);

	spin_unlock_irqrestore(&host->lock, flags);

	if (ios->clock)
		tmio_mmc_set_clock(host, ios->clock);

	/* Power sequence - OFF -> UP -> ON */
	if (ios->power_mode == MMC_POWER_UP) {
		if ((pdata->flags & TMIO_MMC_HAS_COLD_CD) && !pdata->power) {
			pm_runtime_get_sync(&host->pdev->dev);
			pdata->power = true;
		}
		/* power up SD bus */
		if (host->set_pwr)
			host->set_pwr(host->pdev, 1);
	} else if (ios->power_mode == MMC_POWER_OFF || !ios->clock) {
		/* power down SD bus */
		if (ios->power_mode == MMC_POWER_OFF) {
			if (host->set_pwr)
				host->set_pwr(host->pdev, 0);
			if ((pdata->flags & TMIO_MMC_HAS_COLD_CD) &&
			    pdata->power) {
				pdata->power = false;
				pm_runtime_put(&host->pdev->dev);
			}
		}
		tmio_mmc_clk_stop(host);
	} else {
		/* start bus clock */
		tmio_mmc_clk_start(host);
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		sd_ctrl_write16(host, CTL_SD_MEM_CARD_OPT, 0x80e0);
	break;
	case MMC_BUS_WIDTH_4:
		sd_ctrl_write16(host, CTL_SD_MEM_CARD_OPT, 0x00e0);
	break;
	}

	/* Let things settle. delay taken from winCE driver */
	udelay(140);
	if (PTR_ERR(host->mrq) == -EINTR)
		dev_dbg(&host->pdev->dev,
			"%s.%d: IOS interrupted: clk %u, mode %u",
			current->comm, task_pid_nr(current),
			ios->clock, ios->power_mode);
	host->mrq = NULL;
}

static int tmio_mmc_get_ro(struct mmc_host *mmc)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct tmio_mmc_data *pdata = host->pdata;

	return !((pdata->flags & TMIO_MMC_WRPROTECT_DISABLE) ||
		 (sd_ctrl_read32(host, CTL_STATUS) & TMIO_STAT_WRPROTECT));
}

static int tmio_mmc_get_cd(struct mmc_host *mmc)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct tmio_mmc_data *pdata = host->pdata;

	if (!pdata->get_cd)
		return -ENOSYS;
	else
		return pdata->get_cd(host->pdev);
}

static const struct mmc_host_ops tmio_mmc_ops = {
	.request	= tmio_mmc_request,
	.set_ios	= tmio_mmc_set_ios,
	.get_ro         = tmio_mmc_get_ro,
	.get_cd		= tmio_mmc_get_cd,
	.enable_sdio_irq = tmio_mmc_enable_sdio_irq,
};

int __devinit tmio_mmc_host_probe(struct tmio_mmc_host **host,
				  struct platform_device *pdev,
				  struct tmio_mmc_data *pdata)
{
	struct tmio_mmc_host *_host;
	struct mmc_host *mmc;
	struct resource *res_ctl;
	int ret;
	u32 irq_mask = TMIO_MASK_CMD;

	res_ctl = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_ctl)
		return -EINVAL;

	mmc = mmc_alloc_host(sizeof(struct tmio_mmc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	pdata->dev = &pdev->dev;
	_host = mmc_priv(mmc);
	_host->pdata = pdata;
	_host->mmc = mmc;
	_host->pdev = pdev;
	platform_set_drvdata(pdev, mmc);

	_host->set_pwr = pdata->set_pwr;
	_host->set_clk_div = pdata->set_clk_div;

	/* SD control register space size is 0x200, 0x400 for bus_shift=1 */
	_host->bus_shift = resource_size(res_ctl) >> 10;

	_host->ctl = ioremap(res_ctl->start, resource_size(res_ctl));
	if (!_host->ctl) {
		ret = -ENOMEM;
		goto host_free;
	}

	mmc->ops = &tmio_mmc_ops;
	mmc->caps = MMC_CAP_4_BIT_DATA | pdata->capabilities;
	mmc->f_max = pdata->hclk;
	mmc->f_min = mmc->f_max / 512;
	mmc->max_segs = 32;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = (PAGE_CACHE_SIZE / mmc->max_blk_size) *
		mmc->max_segs;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;
	if (pdata->ocr_mask)
		mmc->ocr_avail = pdata->ocr_mask;
	else
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	pdata->power = false;
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume(&pdev->dev);
	if (ret < 0)
		goto pm_disable;

	tmio_mmc_clk_stop(_host);
	tmio_mmc_reset(_host);

	tmio_mmc_disable_mmc_irqs(_host, TMIO_MASK_ALL);
	if (pdata->flags & TMIO_MMC_SDIO_IRQ)
		tmio_mmc_enable_sdio_irq(mmc, 0);

	spin_lock_init(&_host->lock);

	/* Init delayed work for request timeouts */
	INIT_DELAYED_WORK(&_host->delayed_reset_work, tmio_mmc_reset_work);

	/* See if we also get DMA */
	tmio_mmc_request_dma(_host, pdata);

	/* We have to keep the device powered for its card detection to work */
	if (!(pdata->flags & TMIO_MMC_HAS_COLD_CD))
		pm_runtime_get_noresume(&pdev->dev);

	mmc_add_host(mmc);

	/* Unmask the IRQs we want to know about */
	if (!_host->chan_rx)
		irq_mask |= TMIO_MASK_READOP;
	if (!_host->chan_tx)
		irq_mask |= TMIO_MASK_WRITEOP;

	tmio_mmc_enable_mmc_irqs(_host, irq_mask);

	*host = _host;

	return 0;

pm_disable:
	pm_runtime_disable(&pdev->dev);
	iounmap(_host->ctl);
host_free:
	mmc_free_host(mmc);

	return ret;
}
EXPORT_SYMBOL(tmio_mmc_host_probe);

void tmio_mmc_host_remove(struct tmio_mmc_host *host)
{
	struct platform_device *pdev = host->pdev;

	/*
	 * We don't have to manipulate pdata->power here: if there is a card in
	 * the slot, the runtime PM is active and our .runtime_resume() will not
	 * be run. If there is no card in the slot and the platform can suspend
	 * the controller, the runtime PM is suspended and pdata->power == false,
	 * so, our .runtime_resume() will not try to detect a card in the slot.
	 */
	if (host->pdata->flags & TMIO_MMC_HAS_COLD_CD)
		pm_runtime_get_sync(&pdev->dev);

	mmc_remove_host(host->mmc);
	cancel_delayed_work_sync(&host->delayed_reset_work);
	tmio_mmc_release_dma(host);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	iounmap(host->ctl);
	mmc_free_host(host->mmc);
}
EXPORT_SYMBOL(tmio_mmc_host_remove);

#ifdef CONFIG_PM
int tmio_mmc_host_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	int ret = mmc_suspend_host(mmc);

	if (!ret)
		tmio_mmc_disable_mmc_irqs(host, TMIO_MASK_ALL);

	host->pm_error = pm_runtime_put_sync(dev);

	return ret;
}
EXPORT_SYMBOL(tmio_mmc_host_suspend);

int tmio_mmc_host_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);

	/* The MMC core will perform the complete set up */
	host->pdata->power = false;

	if (!host->pm_error)
		pm_runtime_get_sync(dev);

	tmio_mmc_reset(mmc_priv(mmc));
	tmio_mmc_request_dma(host, host->pdata);

	return mmc_resume_host(mmc);
}
EXPORT_SYMBOL(tmio_mmc_host_resume);

#endif	/* CONFIG_PM */

int tmio_mmc_host_runtime_suspend(struct device *dev)
{
	return 0;
}
EXPORT_SYMBOL(tmio_mmc_host_runtime_suspend);

int tmio_mmc_host_runtime_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct tmio_mmc_data *pdata = host->pdata;

	tmio_mmc_reset(host);

	if (pdata->power) {
		/* Only entered after a card-insert interrupt */
		tmio_mmc_set_ios(mmc, &mmc->ios);
		mmc_detect_change(mmc, msecs_to_jiffies(100));
	}

	return 0;
}
EXPORT_SYMBOL(tmio_mmc_host_runtime_resume);

MODULE_LICENSE("GPL v2");
