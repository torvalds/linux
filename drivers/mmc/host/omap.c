/*
 *  linux/drivers/mmc/host/omap.c
 *
 *  Copyright (C) 2004 Nokia Corporation
 *  Written by Tuukka Tikkanen and Juha Yrjölä<juha.yrjola@nokia.com>
 *  Misc hacks here and there by Tony Lindgren <tony@atomide.com>
 *  Other hacks (DMA, SD, etc) by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/clk.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/platform_data/mmc-omap.h>


#define	OMAP_MMC_REG_CMD	0x00
#define	OMAP_MMC_REG_ARGL	0x01
#define	OMAP_MMC_REG_ARGH	0x02
#define	OMAP_MMC_REG_CON	0x03
#define	OMAP_MMC_REG_STAT	0x04
#define	OMAP_MMC_REG_IE		0x05
#define	OMAP_MMC_REG_CTO	0x06
#define	OMAP_MMC_REG_DTO	0x07
#define	OMAP_MMC_REG_DATA	0x08
#define	OMAP_MMC_REG_BLEN	0x09
#define	OMAP_MMC_REG_NBLK	0x0a
#define	OMAP_MMC_REG_BUF	0x0b
#define	OMAP_MMC_REG_SDIO	0x0d
#define	OMAP_MMC_REG_REV	0x0f
#define	OMAP_MMC_REG_RSP0	0x10
#define	OMAP_MMC_REG_RSP1	0x11
#define	OMAP_MMC_REG_RSP2	0x12
#define	OMAP_MMC_REG_RSP3	0x13
#define	OMAP_MMC_REG_RSP4	0x14
#define	OMAP_MMC_REG_RSP5	0x15
#define	OMAP_MMC_REG_RSP6	0x16
#define	OMAP_MMC_REG_RSP7	0x17
#define	OMAP_MMC_REG_IOSR	0x18
#define	OMAP_MMC_REG_SYSC	0x19
#define	OMAP_MMC_REG_SYSS	0x1a

#define	OMAP_MMC_STAT_CARD_ERR		(1 << 14)
#define	OMAP_MMC_STAT_CARD_IRQ		(1 << 13)
#define	OMAP_MMC_STAT_OCR_BUSY		(1 << 12)
#define	OMAP_MMC_STAT_A_EMPTY		(1 << 11)
#define	OMAP_MMC_STAT_A_FULL		(1 << 10)
#define	OMAP_MMC_STAT_CMD_CRC		(1 <<  8)
#define	OMAP_MMC_STAT_CMD_TOUT		(1 <<  7)
#define	OMAP_MMC_STAT_DATA_CRC		(1 <<  6)
#define	OMAP_MMC_STAT_DATA_TOUT		(1 <<  5)
#define	OMAP_MMC_STAT_END_BUSY		(1 <<  4)
#define	OMAP_MMC_STAT_END_OF_DATA	(1 <<  3)
#define	OMAP_MMC_STAT_CARD_BUSY		(1 <<  2)
#define	OMAP_MMC_STAT_END_OF_CMD	(1 <<  0)

#define mmc_omap7xx()	(host->features & MMC_OMAP7XX)
#define mmc_omap15xx()	(host->features & MMC_OMAP15XX)
#define mmc_omap16xx()	(host->features & MMC_OMAP16XX)
#define MMC_OMAP1_MASK	(MMC_OMAP7XX | MMC_OMAP15XX | MMC_OMAP16XX)
#define mmc_omap1()	(host->features & MMC_OMAP1_MASK)
#define mmc_omap2()	(!mmc_omap1())

#define OMAP_MMC_REG(host, reg)		(OMAP_MMC_REG_##reg << (host)->reg_shift)
#define OMAP_MMC_READ(host, reg)	__raw_readw((host)->virt_base + OMAP_MMC_REG(host, reg))
#define OMAP_MMC_WRITE(host, reg, val)	__raw_writew((val), (host)->virt_base + OMAP_MMC_REG(host, reg))

/*
 * Command types
 */
#define OMAP_MMC_CMDTYPE_BC	0
#define OMAP_MMC_CMDTYPE_BCR	1
#define OMAP_MMC_CMDTYPE_AC	2
#define OMAP_MMC_CMDTYPE_ADTC	3

#define DRIVER_NAME "mmci-omap"

/* Specifies how often in millisecs to poll for card status changes
 * when the cover switch is open */
#define OMAP_MMC_COVER_POLL_DELAY	500

struct mmc_omap_host;

struct mmc_omap_slot {
	int			id;
	unsigned int		vdd;
	u16			saved_con;
	u16			bus_mode;
	unsigned int		fclk_freq;

	struct tasklet_struct	cover_tasklet;
	struct timer_list       cover_timer;
	unsigned		cover_open;

	struct mmc_request      *mrq;
	struct mmc_omap_host    *host;
	struct mmc_host		*mmc;
	struct omap_mmc_slot_data *pdata;
};

struct mmc_omap_host {
	int			initialized;
	struct mmc_request *	mrq;
	struct mmc_command *	cmd;
	struct mmc_data *	data;
	struct mmc_host *	mmc;
	struct device *		dev;
	unsigned char		id; /* 16xx chips have 2 MMC blocks */
	struct clk *		iclk;
	struct clk *		fclk;
	struct dma_chan		*dma_rx;
	u32			dma_rx_burst;
	struct dma_chan		*dma_tx;
	u32			dma_tx_burst;
	void __iomem		*virt_base;
	unsigned int		phys_base;
	int			irq;
	unsigned char		bus_mode;
	unsigned int		reg_shift;

	struct work_struct	cmd_abort_work;
	unsigned		abort:1;
	struct timer_list	cmd_abort_timer;

	struct work_struct      slot_release_work;
	struct mmc_omap_slot    *next_slot;
	struct work_struct      send_stop_work;
	struct mmc_data		*stop_data;

	unsigned int		sg_len;
	int			sg_idx;
	u16 *			buffer;
	u32			buffer_bytes_left;
	u32			total_bytes_left;

	unsigned		features;
	unsigned		brs_received:1, dma_done:1;
	unsigned		dma_in_use:1;
	spinlock_t		dma_lock;

	struct mmc_omap_slot    *slots[OMAP_MMC_MAX_SLOTS];
	struct mmc_omap_slot    *current_slot;
	spinlock_t              slot_lock;
	wait_queue_head_t       slot_wq;
	int                     nr_slots;

	struct timer_list       clk_timer;
	spinlock_t		clk_lock;     /* for changing enabled state */
	unsigned int            fclk_enabled:1;
	struct workqueue_struct *mmc_omap_wq;

	struct omap_mmc_platform_data *pdata;
};


static void mmc_omap_fclk_offdelay(struct mmc_omap_slot *slot)
{
	unsigned long tick_ns;

	if (slot != NULL && slot->host->fclk_enabled && slot->fclk_freq > 0) {
		tick_ns = DIV_ROUND_UP(NSEC_PER_SEC, slot->fclk_freq);
		ndelay(8 * tick_ns);
	}
}

static void mmc_omap_fclk_enable(struct mmc_omap_host *host, unsigned int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&host->clk_lock, flags);
	if (host->fclk_enabled != enable) {
		host->fclk_enabled = enable;
		if (enable)
			clk_enable(host->fclk);
		else
			clk_disable(host->fclk);
	}
	spin_unlock_irqrestore(&host->clk_lock, flags);
}

static void mmc_omap_select_slot(struct mmc_omap_slot *slot, int claimed)
{
	struct mmc_omap_host *host = slot->host;
	unsigned long flags;

	if (claimed)
		goto no_claim;
	spin_lock_irqsave(&host->slot_lock, flags);
	while (host->mmc != NULL) {
		spin_unlock_irqrestore(&host->slot_lock, flags);
		wait_event(host->slot_wq, host->mmc == NULL);
		spin_lock_irqsave(&host->slot_lock, flags);
	}
	host->mmc = slot->mmc;
	spin_unlock_irqrestore(&host->slot_lock, flags);
no_claim:
	del_timer(&host->clk_timer);
	if (host->current_slot != slot || !claimed)
		mmc_omap_fclk_offdelay(host->current_slot);

	if (host->current_slot != slot) {
		OMAP_MMC_WRITE(host, CON, slot->saved_con & 0xFC00);
		if (host->pdata->switch_slot != NULL)
			host->pdata->switch_slot(mmc_dev(slot->mmc), slot->id);
		host->current_slot = slot;
	}

	if (claimed) {
		mmc_omap_fclk_enable(host, 1);

		/* Doing the dummy read here seems to work around some bug
		 * at least in OMAP24xx silicon where the command would not
		 * start after writing the CMD register. Sigh. */
		OMAP_MMC_READ(host, CON);

		OMAP_MMC_WRITE(host, CON, slot->saved_con);
	} else
		mmc_omap_fclk_enable(host, 0);
}

static void mmc_omap_start_request(struct mmc_omap_host *host,
				   struct mmc_request *req);

static void mmc_omap_slot_release_work(struct work_struct *work)
{
	struct mmc_omap_host *host = container_of(work, struct mmc_omap_host,
						  slot_release_work);
	struct mmc_omap_slot *next_slot = host->next_slot;
	struct mmc_request *rq;

	host->next_slot = NULL;
	mmc_omap_select_slot(next_slot, 1);

	rq = next_slot->mrq;
	next_slot->mrq = NULL;
	mmc_omap_start_request(host, rq);
}

static void mmc_omap_release_slot(struct mmc_omap_slot *slot, int clk_enabled)
{
	struct mmc_omap_host *host = slot->host;
	unsigned long flags;
	int i;

	BUG_ON(slot == NULL || host->mmc == NULL);

	if (clk_enabled)
		/* Keeps clock running for at least 8 cycles on valid freq */
		mod_timer(&host->clk_timer, jiffies  + HZ/10);
	else {
		del_timer(&host->clk_timer);
		mmc_omap_fclk_offdelay(slot);
		mmc_omap_fclk_enable(host, 0);
	}

	spin_lock_irqsave(&host->slot_lock, flags);
	/* Check for any pending requests */
	for (i = 0; i < host->nr_slots; i++) {
		struct mmc_omap_slot *new_slot;

		if (host->slots[i] == NULL || host->slots[i]->mrq == NULL)
			continue;

		BUG_ON(host->next_slot != NULL);
		new_slot = host->slots[i];
		/* The current slot should not have a request in queue */
		BUG_ON(new_slot == host->current_slot);

		host->next_slot = new_slot;
		host->mmc = new_slot->mmc;
		spin_unlock_irqrestore(&host->slot_lock, flags);
		queue_work(host->mmc_omap_wq, &host->slot_release_work);
		return;
	}

	host->mmc = NULL;
	wake_up(&host->slot_wq);
	spin_unlock_irqrestore(&host->slot_lock, flags);
}

static inline
int mmc_omap_cover_is_open(struct mmc_omap_slot *slot)
{
	if (slot->pdata->get_cover_state)
		return slot->pdata->get_cover_state(mmc_dev(slot->mmc),
						    slot->id);
	return 0;
}

static ssize_t
mmc_omap_show_cover_switch(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct mmc_host *mmc = container_of(dev, struct mmc_host, class_dev);
	struct mmc_omap_slot *slot = mmc_priv(mmc);

	return sprintf(buf, "%s\n", mmc_omap_cover_is_open(slot) ? "open" :
		       "closed");
}

static DEVICE_ATTR(cover_switch, S_IRUGO, mmc_omap_show_cover_switch, NULL);

static ssize_t
mmc_omap_show_slot_name(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct mmc_host *mmc = container_of(dev, struct mmc_host, class_dev);
	struct mmc_omap_slot *slot = mmc_priv(mmc);

	return sprintf(buf, "%s\n", slot->pdata->name);
}

static DEVICE_ATTR(slot_name, S_IRUGO, mmc_omap_show_slot_name, NULL);

static void
mmc_omap_start_command(struct mmc_omap_host *host, struct mmc_command *cmd)
{
	u32 cmdreg;
	u32 resptype;
	u32 cmdtype;
	u16 irq_mask;

	host->cmd = cmd;

	resptype = 0;
	cmdtype = 0;

	/* Our hardware needs to know exact type */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
		/* resp 1, 1b, 6, 7 */
		resptype = 1;
		break;
	case MMC_RSP_R2:
		resptype = 2;
		break;
	case MMC_RSP_R3:
		resptype = 3;
		break;
	default:
		dev_err(mmc_dev(host->mmc), "Invalid response type: %04x\n", mmc_resp_type(cmd));
		break;
	}

	if (mmc_cmd_type(cmd) == MMC_CMD_ADTC) {
		cmdtype = OMAP_MMC_CMDTYPE_ADTC;
	} else if (mmc_cmd_type(cmd) == MMC_CMD_BC) {
		cmdtype = OMAP_MMC_CMDTYPE_BC;
	} else if (mmc_cmd_type(cmd) == MMC_CMD_BCR) {
		cmdtype = OMAP_MMC_CMDTYPE_BCR;
	} else {
		cmdtype = OMAP_MMC_CMDTYPE_AC;
	}

	cmdreg = cmd->opcode | (resptype << 8) | (cmdtype << 12);

	if (host->current_slot->bus_mode == MMC_BUSMODE_OPENDRAIN)
		cmdreg |= 1 << 6;

	if (cmd->flags & MMC_RSP_BUSY)
		cmdreg |= 1 << 11;

	if (host->data && !(host->data->flags & MMC_DATA_WRITE))
		cmdreg |= 1 << 15;

	mod_timer(&host->cmd_abort_timer, jiffies + HZ/2);

	OMAP_MMC_WRITE(host, CTO, 200);
	OMAP_MMC_WRITE(host, ARGL, cmd->arg & 0xffff);
	OMAP_MMC_WRITE(host, ARGH, cmd->arg >> 16);
	irq_mask = OMAP_MMC_STAT_A_EMPTY    | OMAP_MMC_STAT_A_FULL    |
		   OMAP_MMC_STAT_CMD_CRC    | OMAP_MMC_STAT_CMD_TOUT  |
		   OMAP_MMC_STAT_DATA_CRC   | OMAP_MMC_STAT_DATA_TOUT |
		   OMAP_MMC_STAT_END_OF_CMD | OMAP_MMC_STAT_CARD_ERR  |
		   OMAP_MMC_STAT_END_OF_DATA;
	if (cmd->opcode == MMC_ERASE)
		irq_mask &= ~OMAP_MMC_STAT_DATA_TOUT;
	OMAP_MMC_WRITE(host, IE, irq_mask);
	OMAP_MMC_WRITE(host, CMD, cmdreg);
}

static void
mmc_omap_release_dma(struct mmc_omap_host *host, struct mmc_data *data,
		     int abort)
{
	enum dma_data_direction dma_data_dir;
	struct device *dev = mmc_dev(host->mmc);
	struct dma_chan *c;

	if (data->flags & MMC_DATA_WRITE) {
		dma_data_dir = DMA_TO_DEVICE;
		c = host->dma_tx;
	} else {
		dma_data_dir = DMA_FROM_DEVICE;
		c = host->dma_rx;
	}
	if (c) {
		if (data->error) {
			dmaengine_terminate_all(c);
			/* Claim nothing transferred on error... */
			data->bytes_xfered = 0;
		}
		dev = c->device->dev;
	}
	dma_unmap_sg(dev, data->sg, host->sg_len, dma_data_dir);
}

static void mmc_omap_send_stop_work(struct work_struct *work)
{
	struct mmc_omap_host *host = container_of(work, struct mmc_omap_host,
						  send_stop_work);
	struct mmc_omap_slot *slot = host->current_slot;
	struct mmc_data *data = host->stop_data;
	unsigned long tick_ns;

	tick_ns = DIV_ROUND_UP(NSEC_PER_SEC, slot->fclk_freq);
	ndelay(8*tick_ns);

	mmc_omap_start_command(host, data->stop);
}

static void
mmc_omap_xfer_done(struct mmc_omap_host *host, struct mmc_data *data)
{
	if (host->dma_in_use)
		mmc_omap_release_dma(host, data, data->error);

	host->data = NULL;
	host->sg_len = 0;

	/* NOTE:  MMC layer will sometimes poll-wait CMD13 next, issuing
	 * dozens of requests until the card finishes writing data.
	 * It'd be cheaper to just wait till an EOFB interrupt arrives...
	 */

	if (!data->stop) {
		struct mmc_host *mmc;

		host->mrq = NULL;
		mmc = host->mmc;
		mmc_omap_release_slot(host->current_slot, 1);
		mmc_request_done(mmc, data->mrq);
		return;
	}

	host->stop_data = data;
	queue_work(host->mmc_omap_wq, &host->send_stop_work);
}

static void
mmc_omap_send_abort(struct mmc_omap_host *host, int maxloops)
{
	struct mmc_omap_slot *slot = host->current_slot;
	unsigned int restarts, passes, timeout;
	u16 stat = 0;

	/* Sending abort takes 80 clocks. Have some extra and round up */
	timeout = DIV_ROUND_UP(120 * USEC_PER_SEC, slot->fclk_freq);
	restarts = 0;
	while (restarts < maxloops) {
		OMAP_MMC_WRITE(host, STAT, 0xFFFF);
		OMAP_MMC_WRITE(host, CMD, (3 << 12) | (1 << 7));

		passes = 0;
		while (passes < timeout) {
			stat = OMAP_MMC_READ(host, STAT);
			if (stat & OMAP_MMC_STAT_END_OF_CMD)
				goto out;
			udelay(1);
			passes++;
		}

		restarts++;
	}
out:
	OMAP_MMC_WRITE(host, STAT, stat);
}

static void
mmc_omap_abort_xfer(struct mmc_omap_host *host, struct mmc_data *data)
{
	if (host->dma_in_use)
		mmc_omap_release_dma(host, data, 1);

	host->data = NULL;
	host->sg_len = 0;

	mmc_omap_send_abort(host, 10000);
}

static void
mmc_omap_end_of_data(struct mmc_omap_host *host, struct mmc_data *data)
{
	unsigned long flags;
	int done;

	if (!host->dma_in_use) {
		mmc_omap_xfer_done(host, data);
		return;
	}
	done = 0;
	spin_lock_irqsave(&host->dma_lock, flags);
	if (host->dma_done)
		done = 1;
	else
		host->brs_received = 1;
	spin_unlock_irqrestore(&host->dma_lock, flags);
	if (done)
		mmc_omap_xfer_done(host, data);
}

static void
mmc_omap_dma_done(struct mmc_omap_host *host, struct mmc_data *data)
{
	unsigned long flags;
	int done;

	done = 0;
	spin_lock_irqsave(&host->dma_lock, flags);
	if (host->brs_received)
		done = 1;
	else
		host->dma_done = 1;
	spin_unlock_irqrestore(&host->dma_lock, flags);
	if (done)
		mmc_omap_xfer_done(host, data);
}

static void
mmc_omap_cmd_done(struct mmc_omap_host *host, struct mmc_command *cmd)
{
	host->cmd = NULL;

	del_timer(&host->cmd_abort_timer);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* response type 2 */
			cmd->resp[3] =
				OMAP_MMC_READ(host, RSP0) |
				(OMAP_MMC_READ(host, RSP1) << 16);
			cmd->resp[2] =
				OMAP_MMC_READ(host, RSP2) |
				(OMAP_MMC_READ(host, RSP3) << 16);
			cmd->resp[1] =
				OMAP_MMC_READ(host, RSP4) |
				(OMAP_MMC_READ(host, RSP5) << 16);
			cmd->resp[0] =
				OMAP_MMC_READ(host, RSP6) |
				(OMAP_MMC_READ(host, RSP7) << 16);
		} else {
			/* response types 1, 1b, 3, 4, 5, 6 */
			cmd->resp[0] =
				OMAP_MMC_READ(host, RSP6) |
				(OMAP_MMC_READ(host, RSP7) << 16);
		}
	}

	if (host->data == NULL || cmd->error) {
		struct mmc_host *mmc;

		if (host->data != NULL)
			mmc_omap_abort_xfer(host, host->data);
		host->mrq = NULL;
		mmc = host->mmc;
		mmc_omap_release_slot(host->current_slot, 1);
		mmc_request_done(mmc, cmd->mrq);
	}
}

/*
 * Abort stuck command. Can occur when card is removed while it is being
 * read.
 */
static void mmc_omap_abort_command(struct work_struct *work)
{
	struct mmc_omap_host *host = container_of(work, struct mmc_omap_host,
						  cmd_abort_work);
	BUG_ON(!host->cmd);

	dev_dbg(mmc_dev(host->mmc), "Aborting stuck command CMD%d\n",
		host->cmd->opcode);

	if (host->cmd->error == 0)
		host->cmd->error = -ETIMEDOUT;

	if (host->data == NULL) {
		struct mmc_command *cmd;
		struct mmc_host    *mmc;

		cmd = host->cmd;
		host->cmd = NULL;
		mmc_omap_send_abort(host, 10000);

		host->mrq = NULL;
		mmc = host->mmc;
		mmc_omap_release_slot(host->current_slot, 1);
		mmc_request_done(mmc, cmd->mrq);
	} else
		mmc_omap_cmd_done(host, host->cmd);

	host->abort = 0;
	enable_irq(host->irq);
}

static void
mmc_omap_cmd_timer(unsigned long data)
{
	struct mmc_omap_host *host = (struct mmc_omap_host *) data;
	unsigned long flags;

	spin_lock_irqsave(&host->slot_lock, flags);
	if (host->cmd != NULL && !host->abort) {
		OMAP_MMC_WRITE(host, IE, 0);
		disable_irq(host->irq);
		host->abort = 1;
		queue_work(host->mmc_omap_wq, &host->cmd_abort_work);
	}
	spin_unlock_irqrestore(&host->slot_lock, flags);
}

/* PIO only */
static void
mmc_omap_sg_to_buf(struct mmc_omap_host *host)
{
	struct scatterlist *sg;

	sg = host->data->sg + host->sg_idx;
	host->buffer_bytes_left = sg->length;
	host->buffer = sg_virt(sg);
	if (host->buffer_bytes_left > host->total_bytes_left)
		host->buffer_bytes_left = host->total_bytes_left;
}

static void
mmc_omap_clk_timer(unsigned long data)
{
	struct mmc_omap_host *host = (struct mmc_omap_host *) data;

	mmc_omap_fclk_enable(host, 0);
}

/* PIO only */
static void
mmc_omap_xfer_data(struct mmc_omap_host *host, int write)
{
	int n, nwords;

	if (host->buffer_bytes_left == 0) {
		host->sg_idx++;
		BUG_ON(host->sg_idx == host->sg_len);
		mmc_omap_sg_to_buf(host);
	}
	n = 64;
	if (n > host->buffer_bytes_left)
		n = host->buffer_bytes_left;

	/* Round up to handle odd number of bytes to transfer */
	nwords = DIV_ROUND_UP(n, 2);

	host->buffer_bytes_left -= n;
	host->total_bytes_left -= n;
	host->data->bytes_xfered += n;

	if (write) {
		__raw_writesw(host->virt_base + OMAP_MMC_REG(host, DATA),
			      host->buffer, nwords);
	} else {
		__raw_readsw(host->virt_base + OMAP_MMC_REG(host, DATA),
			     host->buffer, nwords);
	}

	host->buffer += nwords;
}

#ifdef CONFIG_MMC_DEBUG
static void mmc_omap_report_irq(struct mmc_omap_host *host, u16 status)
{
	static const char *mmc_omap_status_bits[] = {
		"EOC", "CD", "CB", "BRS", "EOFB", "DTO", "DCRC", "CTO",
		"CCRC", "CRW", "AF", "AE", "OCRB", "CIRQ", "CERR"
	};
	int i;
	char res[64], *buf = res;

	buf += sprintf(buf, "MMC IRQ 0x%x:", status);

	for (i = 0; i < ARRAY_SIZE(mmc_omap_status_bits); i++)
		if (status & (1 << i))
			buf += sprintf(buf, " %s", mmc_omap_status_bits[i]);
	dev_vdbg(mmc_dev(host->mmc), "%s\n", res);
}
#else
static void mmc_omap_report_irq(struct mmc_omap_host *host, u16 status)
{
}
#endif


static irqreturn_t mmc_omap_irq(int irq, void *dev_id)
{
	struct mmc_omap_host * host = (struct mmc_omap_host *)dev_id;
	u16 status;
	int end_command;
	int end_transfer;
	int transfer_error, cmd_error;

	if (host->cmd == NULL && host->data == NULL) {
		status = OMAP_MMC_READ(host, STAT);
		dev_info(mmc_dev(host->slots[0]->mmc),
			 "Spurious IRQ 0x%04x\n", status);
		if (status != 0) {
			OMAP_MMC_WRITE(host, STAT, status);
			OMAP_MMC_WRITE(host, IE, 0);
		}
		return IRQ_HANDLED;
	}

	end_command = 0;
	end_transfer = 0;
	transfer_error = 0;
	cmd_error = 0;

	while ((status = OMAP_MMC_READ(host, STAT)) != 0) {
		int cmd;

		OMAP_MMC_WRITE(host, STAT, status);
		if (host->cmd != NULL)
			cmd = host->cmd->opcode;
		else
			cmd = -1;
		dev_dbg(mmc_dev(host->mmc), "MMC IRQ %04x (CMD %d): ",
			status, cmd);
		mmc_omap_report_irq(host, status);

		if (host->total_bytes_left) {
			if ((status & OMAP_MMC_STAT_A_FULL) ||
			    (status & OMAP_MMC_STAT_END_OF_DATA))
				mmc_omap_xfer_data(host, 0);
			if (status & OMAP_MMC_STAT_A_EMPTY)
				mmc_omap_xfer_data(host, 1);
		}

		if (status & OMAP_MMC_STAT_END_OF_DATA)
			end_transfer = 1;

		if (status & OMAP_MMC_STAT_DATA_TOUT) {
			dev_dbg(mmc_dev(host->mmc), "data timeout (CMD%d)\n",
				cmd);
			if (host->data) {
				host->data->error = -ETIMEDOUT;
				transfer_error = 1;
			}
		}

		if (status & OMAP_MMC_STAT_DATA_CRC) {
			if (host->data) {
				host->data->error = -EILSEQ;
				dev_dbg(mmc_dev(host->mmc),
					 "data CRC error, bytes left %d\n",
					host->total_bytes_left);
				transfer_error = 1;
			} else {
				dev_dbg(mmc_dev(host->mmc), "data CRC error\n");
			}
		}

		if (status & OMAP_MMC_STAT_CMD_TOUT) {
			/* Timeouts are routine with some commands */
			if (host->cmd) {
				struct mmc_omap_slot *slot =
					host->current_slot;
				if (slot == NULL ||
				    !mmc_omap_cover_is_open(slot))
					dev_err(mmc_dev(host->mmc),
						"command timeout (CMD%d)\n",
						cmd);
				host->cmd->error = -ETIMEDOUT;
				end_command = 1;
				cmd_error = 1;
			}
		}

		if (status & OMAP_MMC_STAT_CMD_CRC) {
			if (host->cmd) {
				dev_err(mmc_dev(host->mmc),
					"command CRC error (CMD%d, arg 0x%08x)\n",
					cmd, host->cmd->arg);
				host->cmd->error = -EILSEQ;
				end_command = 1;
				cmd_error = 1;
			} else
				dev_err(mmc_dev(host->mmc),
					"command CRC error without cmd?\n");
		}

		if (status & OMAP_MMC_STAT_CARD_ERR) {
			dev_dbg(mmc_dev(host->mmc),
				"ignoring card status error (CMD%d)\n",
				cmd);
			end_command = 1;
		}

		/*
		 * NOTE: On 1610 the END_OF_CMD may come too early when
		 * starting a write
		 */
		if ((status & OMAP_MMC_STAT_END_OF_CMD) &&
		    (!(status & OMAP_MMC_STAT_A_EMPTY))) {
			end_command = 1;
		}
	}

	if (cmd_error && host->data) {
		del_timer(&host->cmd_abort_timer);
		host->abort = 1;
		OMAP_MMC_WRITE(host, IE, 0);
		disable_irq_nosync(host->irq);
		queue_work(host->mmc_omap_wq, &host->cmd_abort_work);
		return IRQ_HANDLED;
	}

	if (end_command && host->cmd)
		mmc_omap_cmd_done(host, host->cmd);
	if (host->data != NULL) {
		if (transfer_error)
			mmc_omap_xfer_done(host, host->data);
		else if (end_transfer)
			mmc_omap_end_of_data(host, host->data);
	}

	return IRQ_HANDLED;
}

void omap_mmc_notify_cover_event(struct device *dev, int num, int is_closed)
{
	int cover_open;
	struct mmc_omap_host *host = dev_get_drvdata(dev);
	struct mmc_omap_slot *slot = host->slots[num];

	BUG_ON(num >= host->nr_slots);

	/* Other subsystems can call in here before we're initialised. */
	if (host->nr_slots == 0 || !host->slots[num])
		return;

	cover_open = mmc_omap_cover_is_open(slot);
	if (cover_open != slot->cover_open) {
		slot->cover_open = cover_open;
		sysfs_notify(&slot->mmc->class_dev.kobj, NULL, "cover_switch");
	}

	tasklet_hi_schedule(&slot->cover_tasklet);
}

static void mmc_omap_cover_timer(unsigned long arg)
{
	struct mmc_omap_slot *slot = (struct mmc_omap_slot *) arg;
	tasklet_schedule(&slot->cover_tasklet);
}

static void mmc_omap_cover_handler(unsigned long param)
{
	struct mmc_omap_slot *slot = (struct mmc_omap_slot *)param;
	int cover_open = mmc_omap_cover_is_open(slot);

	mmc_detect_change(slot->mmc, 0);
	if (!cover_open)
		return;

	/*
	 * If no card is inserted, we postpone polling until
	 * the cover has been closed.
	 */
	if (slot->mmc->card == NULL || !mmc_card_present(slot->mmc->card))
		return;

	mod_timer(&slot->cover_timer,
		  jiffies + msecs_to_jiffies(OMAP_MMC_COVER_POLL_DELAY));
}

static void mmc_omap_dma_callback(void *priv)
{
	struct mmc_omap_host *host = priv;
	struct mmc_data *data = host->data;

	/* If we got to the end of DMA, assume everything went well */
	data->bytes_xfered += data->blocks * data->blksz;

	mmc_omap_dma_done(host, data);
}

static inline void set_cmd_timeout(struct mmc_omap_host *host, struct mmc_request *req)
{
	u16 reg;

	reg = OMAP_MMC_READ(host, SDIO);
	reg &= ~(1 << 5);
	OMAP_MMC_WRITE(host, SDIO, reg);
	/* Set maximum timeout */
	OMAP_MMC_WRITE(host, CTO, 0xff);
}

static inline void set_data_timeout(struct mmc_omap_host *host, struct mmc_request *req)
{
	unsigned int timeout, cycle_ns;
	u16 reg;

	cycle_ns = 1000000000 / host->current_slot->fclk_freq;
	timeout = req->data->timeout_ns / cycle_ns;
	timeout += req->data->timeout_clks;

	/* Check if we need to use timeout multiplier register */
	reg = OMAP_MMC_READ(host, SDIO);
	if (timeout > 0xffff) {
		reg |= (1 << 5);
		timeout /= 1024;
	} else
		reg &= ~(1 << 5);
	OMAP_MMC_WRITE(host, SDIO, reg);
	OMAP_MMC_WRITE(host, DTO, timeout);
}

static void
mmc_omap_prepare_data(struct mmc_omap_host *host, struct mmc_request *req)
{
	struct mmc_data *data = req->data;
	int i, use_dma = 1, block_size;
	struct scatterlist *sg;
	unsigned sg_len;

	host->data = data;
	if (data == NULL) {
		OMAP_MMC_WRITE(host, BLEN, 0);
		OMAP_MMC_WRITE(host, NBLK, 0);
		OMAP_MMC_WRITE(host, BUF, 0);
		host->dma_in_use = 0;
		set_cmd_timeout(host, req);
		return;
	}

	block_size = data->blksz;

	OMAP_MMC_WRITE(host, NBLK, data->blocks - 1);
	OMAP_MMC_WRITE(host, BLEN, block_size - 1);
	set_data_timeout(host, req);

	/* cope with calling layer confusion; it issues "single
	 * block" writes using multi-block scatterlists.
	 */
	sg_len = (data->blocks == 1) ? 1 : data->sg_len;

	/* Only do DMA for entire blocks */
	for_each_sg(data->sg, sg, sg_len, i) {
		if ((sg->length % block_size) != 0) {
			use_dma = 0;
			break;
		}
	}

	host->sg_idx = 0;
	if (use_dma) {
		enum dma_data_direction dma_data_dir;
		struct dma_async_tx_descriptor *tx;
		struct dma_chan *c;
		u32 burst, *bp;
		u16 buf;

		/*
		 * FIFO is 16x2 bytes on 15xx, and 32x2 bytes on 16xx
		 * and 24xx. Use 16 or 32 word frames when the
		 * blocksize is at least that large. Blocksize is
		 * usually 512 bytes; but not for some SD reads.
		 */
		burst = mmc_omap15xx() ? 32 : 64;
		if (burst > data->blksz)
			burst = data->blksz;

		burst >>= 1;

		if (data->flags & MMC_DATA_WRITE) {
			c = host->dma_tx;
			bp = &host->dma_tx_burst;
			buf = 0x0f80 | (burst - 1) << 0;
			dma_data_dir = DMA_TO_DEVICE;
		} else {
			c = host->dma_rx;
			bp = &host->dma_rx_burst;
			buf = 0x800f | (burst - 1) << 8;
			dma_data_dir = DMA_FROM_DEVICE;
		}

		if (!c)
			goto use_pio;

		/* Only reconfigure if we have a different burst size */
		if (*bp != burst) {
			struct dma_slave_config cfg;

			cfg.src_addr = host->phys_base + OMAP_MMC_REG(host, DATA);
			cfg.dst_addr = host->phys_base + OMAP_MMC_REG(host, DATA);
			cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
			cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
			cfg.src_maxburst = burst;
			cfg.dst_maxburst = burst;

			if (dmaengine_slave_config(c, &cfg))
				goto use_pio;

			*bp = burst;
		}

		host->sg_len = dma_map_sg(c->device->dev, data->sg, sg_len,
					  dma_data_dir);
		if (host->sg_len == 0)
			goto use_pio;

		tx = dmaengine_prep_slave_sg(c, data->sg, host->sg_len,
			data->flags & MMC_DATA_WRITE ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!tx)
			goto use_pio;

		OMAP_MMC_WRITE(host, BUF, buf);

		tx->callback = mmc_omap_dma_callback;
		tx->callback_param = host;
		dmaengine_submit(tx);
		host->brs_received = 0;
		host->dma_done = 0;
		host->dma_in_use = 1;
		return;
	}
 use_pio:

	/* Revert to PIO? */
	OMAP_MMC_WRITE(host, BUF, 0x1f1f);
	host->total_bytes_left = data->blocks * block_size;
	host->sg_len = sg_len;
	mmc_omap_sg_to_buf(host);
	host->dma_in_use = 0;
}

static void mmc_omap_start_request(struct mmc_omap_host *host,
				   struct mmc_request *req)
{
	BUG_ON(host->mrq != NULL);

	host->mrq = req;

	/* only touch fifo AFTER the controller readies it */
	mmc_omap_prepare_data(host, req);
	mmc_omap_start_command(host, req->cmd);
	if (host->dma_in_use) {
		struct dma_chan *c = host->data->flags & MMC_DATA_WRITE ?
				host->dma_tx : host->dma_rx;

		dma_async_issue_pending(c);
	}
}

static void mmc_omap_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct mmc_omap_slot *slot = mmc_priv(mmc);
	struct mmc_omap_host *host = slot->host;
	unsigned long flags;

	spin_lock_irqsave(&host->slot_lock, flags);
	if (host->mmc != NULL) {
		BUG_ON(slot->mrq != NULL);
		slot->mrq = req;
		spin_unlock_irqrestore(&host->slot_lock, flags);
		return;
	} else
		host->mmc = mmc;
	spin_unlock_irqrestore(&host->slot_lock, flags);
	mmc_omap_select_slot(slot, 1);
	mmc_omap_start_request(host, req);
}

static void mmc_omap_set_power(struct mmc_omap_slot *slot, int power_on,
				int vdd)
{
	struct mmc_omap_host *host;

	host = slot->host;

	if (slot->pdata->set_power != NULL)
		slot->pdata->set_power(mmc_dev(slot->mmc), slot->id, power_on,
					vdd);
	if (mmc_omap2()) {
		u16 w;

		if (power_on) {
			w = OMAP_MMC_READ(host, CON);
			OMAP_MMC_WRITE(host, CON, w | (1 << 11));
		} else {
			w = OMAP_MMC_READ(host, CON);
			OMAP_MMC_WRITE(host, CON, w & ~(1 << 11));
		}
	}
}

static int mmc_omap_calc_divisor(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmc_omap_slot *slot = mmc_priv(mmc);
	struct mmc_omap_host *host = slot->host;
	int func_clk_rate = clk_get_rate(host->fclk);
	int dsor;

	if (ios->clock == 0)
		return 0;

	dsor = func_clk_rate / ios->clock;
	if (dsor < 1)
		dsor = 1;

	if (func_clk_rate / dsor > ios->clock)
		dsor++;

	if (dsor > 250)
		dsor = 250;

	slot->fclk_freq = func_clk_rate / dsor;

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		dsor |= 1 << 15;

	return dsor;
}

static void mmc_omap_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmc_omap_slot *slot = mmc_priv(mmc);
	struct mmc_omap_host *host = slot->host;
	int i, dsor;
	int clk_enabled;

	mmc_omap_select_slot(slot, 0);

	dsor = mmc_omap_calc_divisor(mmc, ios);

	if (ios->vdd != slot->vdd)
		slot->vdd = ios->vdd;

	clk_enabled = 0;
	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		mmc_omap_set_power(slot, 0, ios->vdd);
		break;
	case MMC_POWER_UP:
		/* Cannot touch dsor yet, just power up MMC */
		mmc_omap_set_power(slot, 1, ios->vdd);
		goto exit;
	case MMC_POWER_ON:
		mmc_omap_fclk_enable(host, 1);
		clk_enabled = 1;
		dsor |= 1 << 11;
		break;
	}

	if (slot->bus_mode != ios->bus_mode) {
		if (slot->pdata->set_bus_mode != NULL)
			slot->pdata->set_bus_mode(mmc_dev(mmc), slot->id,
						  ios->bus_mode);
		slot->bus_mode = ios->bus_mode;
	}

	/* On insanely high arm_per frequencies something sometimes
	 * goes somehow out of sync, and the POW bit is not being set,
	 * which results in the while loop below getting stuck.
	 * Writing to the CON register twice seems to do the trick. */
	for (i = 0; i < 2; i++)
		OMAP_MMC_WRITE(host, CON, dsor);
	slot->saved_con = dsor;
	if (ios->power_mode == MMC_POWER_ON) {
		/* worst case at 400kHz, 80 cycles makes 200 microsecs */
		int usecs = 250;

		/* Send clock cycles, poll completion */
		OMAP_MMC_WRITE(host, IE, 0);
		OMAP_MMC_WRITE(host, STAT, 0xffff);
		OMAP_MMC_WRITE(host, CMD, 1 << 7);
		while (usecs > 0 && (OMAP_MMC_READ(host, STAT) & 1) == 0) {
			udelay(1);
			usecs--;
		}
		OMAP_MMC_WRITE(host, STAT, 1);
	}

exit:
	mmc_omap_release_slot(slot, clk_enabled);
}

static const struct mmc_host_ops mmc_omap_ops = {
	.request	= mmc_omap_request,
	.set_ios	= mmc_omap_set_ios,
};

static int mmc_omap_new_slot(struct mmc_omap_host *host, int id)
{
	struct mmc_omap_slot *slot = NULL;
	struct mmc_host *mmc;
	int r;

	mmc = mmc_alloc_host(sizeof(struct mmc_omap_slot), host->dev);
	if (mmc == NULL)
		return -ENOMEM;

	slot = mmc_priv(mmc);
	slot->host = host;
	slot->mmc = mmc;
	slot->id = id;
	slot->pdata = &host->pdata->slots[id];

	host->slots[id] = slot;

	mmc->caps = 0;
	if (host->pdata->slots[id].wires >= 4)
		mmc->caps |= MMC_CAP_4_BIT_DATA | MMC_CAP_ERASE;

	mmc->ops = &mmc_omap_ops;
	mmc->f_min = 400000;

	if (mmc_omap2())
		mmc->f_max = 48000000;
	else
		mmc->f_max = 24000000;
	if (host->pdata->max_freq)
		mmc->f_max = min(host->pdata->max_freq, mmc->f_max);
	mmc->ocr_avail = slot->pdata->ocr_mask;

	/* Use scatterlist DMA to reduce per-transfer costs.
	 * NOTE max_seg_size assumption that small blocks aren't
	 * normally used (except e.g. for reading SD registers).
	 */
	mmc->max_segs = 32;
	mmc->max_blk_size = 2048;	/* BLEN is 11 bits (+1) */
	mmc->max_blk_count = 2048;	/* NBLK is 11 bits (+1) */
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;

	if (slot->pdata->get_cover_state != NULL) {
		setup_timer(&slot->cover_timer, mmc_omap_cover_timer,
			    (unsigned long)slot);
		tasklet_init(&slot->cover_tasklet, mmc_omap_cover_handler,
			     (unsigned long)slot);
	}

	r = mmc_add_host(mmc);
	if (r < 0)
		goto err_remove_host;

	if (slot->pdata->name != NULL) {
		r = device_create_file(&mmc->class_dev,
					&dev_attr_slot_name);
		if (r < 0)
			goto err_remove_host;
	}

	if (slot->pdata->get_cover_state != NULL) {
		r = device_create_file(&mmc->class_dev,
					&dev_attr_cover_switch);
		if (r < 0)
			goto err_remove_slot_name;
		tasklet_schedule(&slot->cover_tasklet);
	}

	return 0;

err_remove_slot_name:
	if (slot->pdata->name != NULL)
		device_remove_file(&mmc->class_dev, &dev_attr_slot_name);
err_remove_host:
	mmc_remove_host(mmc);
	mmc_free_host(mmc);
	return r;
}

static void mmc_omap_remove_slot(struct mmc_omap_slot *slot)
{
	struct mmc_host *mmc = slot->mmc;

	if (slot->pdata->name != NULL)
		device_remove_file(&mmc->class_dev, &dev_attr_slot_name);
	if (slot->pdata->get_cover_state != NULL)
		device_remove_file(&mmc->class_dev, &dev_attr_cover_switch);

	tasklet_kill(&slot->cover_tasklet);
	del_timer_sync(&slot->cover_timer);
	flush_workqueue(slot->host->mmc_omap_wq);

	mmc_remove_host(mmc);
	mmc_free_host(mmc);
}

static int mmc_omap_probe(struct platform_device *pdev)
{
	struct omap_mmc_platform_data *pdata = pdev->dev.platform_data;
	struct mmc_omap_host *host = NULL;
	struct resource *res;
	int i, ret = 0;
	int irq;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "platform data missing\n");
		return -ENXIO;
	}
	if (pdata->nr_slots == 0) {
		dev_err(&pdev->dev, "no slots\n");
		return -EPROBE_DEFER;
	}

	host = devm_kzalloc(&pdev->dev, sizeof(struct mmc_omap_host),
			    GFP_KERNEL);
	if (host == NULL)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->virt_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->virt_base))
		return PTR_ERR(host->virt_base);

	INIT_WORK(&host->slot_release_work, mmc_omap_slot_release_work);
	INIT_WORK(&host->send_stop_work, mmc_omap_send_stop_work);

	INIT_WORK(&host->cmd_abort_work, mmc_omap_abort_command);
	setup_timer(&host->cmd_abort_timer, mmc_omap_cmd_timer,
		    (unsigned long) host);

	spin_lock_init(&host->clk_lock);
	setup_timer(&host->clk_timer, mmc_omap_clk_timer, (unsigned long) host);

	spin_lock_init(&host->dma_lock);
	spin_lock_init(&host->slot_lock);
	init_waitqueue_head(&host->slot_wq);

	host->pdata = pdata;
	host->features = host->pdata->slots[0].features;
	host->dev = &pdev->dev;
	platform_set_drvdata(pdev, host);

	host->id = pdev->id;
	host->irq = irq;
	host->phys_base = res->start;
	host->iclk = clk_get(&pdev->dev, "ick");
	if (IS_ERR(host->iclk))
		return PTR_ERR(host->iclk);
	clk_enable(host->iclk);

	host->fclk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(host->fclk)) {
		ret = PTR_ERR(host->fclk);
		goto err_free_iclk;
	}

	host->dma_tx_burst = -1;
	host->dma_rx_burst = -1;

	host->dma_tx = dma_request_chan(&pdev->dev, "tx");
	if (IS_ERR(host->dma_tx)) {
		ret = PTR_ERR(host->dma_tx);
		if (ret == -EPROBE_DEFER) {
			clk_put(host->fclk);
			goto err_free_iclk;
		}

		host->dma_tx = NULL;
		dev_warn(host->dev, "TX DMA channel request failed\n");
	}

	host->dma_rx = dma_request_chan(&pdev->dev, "rx");
	if (IS_ERR(host->dma_rx)) {
		ret = PTR_ERR(host->dma_rx);
		if (ret == -EPROBE_DEFER) {
			if (host->dma_tx)
				dma_release_channel(host->dma_tx);
			clk_put(host->fclk);
			goto err_free_iclk;
		}

		host->dma_rx = NULL;
		dev_warn(host->dev, "RX DMA channel request failed\n");
	}

	ret = request_irq(host->irq, mmc_omap_irq, 0, DRIVER_NAME, host);
	if (ret)
		goto err_free_dma;

	if (pdata->init != NULL) {
		ret = pdata->init(&pdev->dev);
		if (ret < 0)
			goto err_free_irq;
	}

	host->nr_slots = pdata->nr_slots;
	host->reg_shift = (mmc_omap7xx() ? 1 : 2);

	host->mmc_omap_wq = alloc_workqueue("mmc_omap", 0, 0);
	if (!host->mmc_omap_wq) {
		ret = -ENOMEM;
		goto err_plat_cleanup;
	}

	for (i = 0; i < pdata->nr_slots; i++) {
		ret = mmc_omap_new_slot(host, i);
		if (ret < 0) {
			while (--i >= 0)
				mmc_omap_remove_slot(host->slots[i]);

			goto err_destroy_wq;
		}
	}

	return 0;

err_destroy_wq:
	destroy_workqueue(host->mmc_omap_wq);
err_plat_cleanup:
	if (pdata->cleanup)
		pdata->cleanup(&pdev->dev);
err_free_irq:
	free_irq(host->irq, host);
err_free_dma:
	if (host->dma_tx)
		dma_release_channel(host->dma_tx);
	if (host->dma_rx)
		dma_release_channel(host->dma_rx);
	clk_put(host->fclk);
err_free_iclk:
	clk_disable(host->iclk);
	clk_put(host->iclk);
	return ret;
}

static int mmc_omap_remove(struct platform_device *pdev)
{
	struct mmc_omap_host *host = platform_get_drvdata(pdev);
	int i;

	BUG_ON(host == NULL);

	for (i = 0; i < host->nr_slots; i++)
		mmc_omap_remove_slot(host->slots[i]);

	if (host->pdata->cleanup)
		host->pdata->cleanup(&pdev->dev);

	mmc_omap_fclk_enable(host, 0);
	free_irq(host->irq, host);
	clk_put(host->fclk);
	clk_disable(host->iclk);
	clk_put(host->iclk);

	if (host->dma_tx)
		dma_release_channel(host->dma_tx);
	if (host->dma_rx)
		dma_release_channel(host->dma_rx);

	destroy_workqueue(host->mmc_omap_wq);

	return 0;
}

#if IS_BUILTIN(CONFIG_OF)
static const struct of_device_id mmc_omap_match[] = {
	{ .compatible = "ti,omap2420-mmc", },
	{ },
};
MODULE_DEVICE_TABLE(of, mmc_omap_match);
#endif

static struct platform_driver mmc_omap_driver = {
	.probe		= mmc_omap_probe,
	.remove		= mmc_omap_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(mmc_omap_match),
	},
};

module_platform_driver(mmc_omap_driver);
MODULE_DESCRIPTION("OMAP Multimedia Card driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Juha Yrjölä");
