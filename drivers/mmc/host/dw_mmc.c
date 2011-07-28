/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *  (Based on NXP driver for lpc 31xx)
 *
 * Copyright (C) 2009 NXP Semiconductors
 * Copyright (C) 2009, 2010 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/blkdev.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/bitops.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include "dw_mmc.h"

/* Common flag combinations */
#define DW_MCI_DATA_ERROR_FLAGS	(SDMMC_INT_DTO | SDMMC_INT_DCRC | \
				 SDMMC_INT_HTO | SDMMC_INT_SBE  | \
				 SDMMC_INT_EBE)
#define DW_MCI_CMD_ERROR_FLAGS	(SDMMC_INT_RTO | SDMMC_INT_RCRC | \
				 SDMMC_INT_RESP_ERR)
#define DW_MCI_ERROR_FLAGS	(DW_MCI_DATA_ERROR_FLAGS | \
				 DW_MCI_CMD_ERROR_FLAGS  | SDMMC_INT_HLE)
#define DW_MCI_SEND_STATUS	1
#define DW_MCI_RECV_STATUS	2
#define DW_MCI_DMA_THRESHOLD	16

#ifdef CONFIG_MMC_DW_IDMAC
struct idmac_desc {
	u32		des0;	/* Control Descriptor */
#define IDMAC_DES0_DIC	BIT(1)
#define IDMAC_DES0_LD	BIT(2)
#define IDMAC_DES0_FD	BIT(3)
#define IDMAC_DES0_CH	BIT(4)
#define IDMAC_DES0_ER	BIT(5)
#define IDMAC_DES0_CES	BIT(30)
#define IDMAC_DES0_OWN	BIT(31)

	u32		des1;	/* Buffer sizes */
#define IDMAC_SET_BUFFER1_SIZE(d, s) \
	((d)->des1 = ((d)->des1 & 0x03ffc000) | ((s) & 0x3fff))

	u32		des2;	/* buffer 1 physical address */

	u32		des3;	/* buffer 2 physical address */
};
#endif /* CONFIG_MMC_DW_IDMAC */

/**
 * struct dw_mci_slot - MMC slot state
 * @mmc: The mmc_host representing this slot.
 * @host: The MMC controller this slot is using.
 * @ctype: Card type for this slot.
 * @mrq: mmc_request currently being processed or waiting to be
 *	processed, or NULL when the slot is idle.
 * @queue_node: List node for placing this node in the @queue list of
 *	&struct dw_mci.
 * @clock: Clock rate configured by set_ios(). Protected by host->lock.
 * @flags: Random state bits associated with the slot.
 * @id: Number of this slot.
 * @last_detect_state: Most recently observed card detect state.
 */
struct dw_mci_slot {
	struct mmc_host		*mmc;
	struct dw_mci		*host;

	u32			ctype;

	struct mmc_request	*mrq;
	struct list_head	queue_node;

	unsigned int		clock;
	unsigned long		flags;
#define DW_MMC_CARD_PRESENT	0
#define DW_MMC_CARD_NEED_INIT	1
	int			id;
	int			last_detect_state;
};

static struct workqueue_struct *dw_mci_card_workqueue;

#if defined(CONFIG_DEBUG_FS)
static int dw_mci_req_show(struct seq_file *s, void *v)
{
	struct dw_mci_slot *slot = s->private;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_command *stop;
	struct mmc_data	*data;

	/* Make sure we get a consistent snapshot */
	spin_lock_bh(&slot->host->lock);
	mrq = slot->mrq;

	if (mrq) {
		cmd = mrq->cmd;
		data = mrq->data;
		stop = mrq->stop;

		if (cmd)
			seq_printf(s,
				   "CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				   cmd->opcode, cmd->arg, cmd->flags,
				   cmd->resp[0], cmd->resp[1], cmd->resp[2],
				   cmd->resp[2], cmd->error);
		if (data)
			seq_printf(s, "DATA %u / %u * %u flg %x err %d\n",
				   data->bytes_xfered, data->blocks,
				   data->blksz, data->flags, data->error);
		if (stop)
			seq_printf(s,
				   "CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				   stop->opcode, stop->arg, stop->flags,
				   stop->resp[0], stop->resp[1], stop->resp[2],
				   stop->resp[2], stop->error);
	}

	spin_unlock_bh(&slot->host->lock);

	return 0;
}

static int dw_mci_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, dw_mci_req_show, inode->i_private);
}

static const struct file_operations dw_mci_req_fops = {
	.owner		= THIS_MODULE,
	.open		= dw_mci_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int dw_mci_regs_show(struct seq_file *s, void *v)
{
	seq_printf(s, "STATUS:\t0x%08x\n", SDMMC_STATUS);
	seq_printf(s, "RINTSTS:\t0x%08x\n", SDMMC_RINTSTS);
	seq_printf(s, "CMD:\t0x%08x\n", SDMMC_CMD);
	seq_printf(s, "CTRL:\t0x%08x\n", SDMMC_CTRL);
	seq_printf(s, "INTMASK:\t0x%08x\n", SDMMC_INTMASK);
	seq_printf(s, "CLKENA:\t0x%08x\n", SDMMC_CLKENA);

	return 0;
}

static int dw_mci_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, dw_mci_regs_show, inode->i_private);
}

static const struct file_operations dw_mci_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= dw_mci_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void dw_mci_init_debugfs(struct dw_mci_slot *slot)
{
	struct mmc_host	*mmc = slot->mmc;
	struct dw_mci *host = slot->host;
	struct dentry *root;
	struct dentry *node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
				   &dw_mci_regs_fops);
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, slot,
				   &dw_mci_req_fops);
	if (!node)
		goto err;

	node = debugfs_create_u32("state", S_IRUSR, root, (u32 *)&host->state);
	if (!node)
		goto err;

	node = debugfs_create_x32("pending_events", S_IRUSR, root,
				  (u32 *)&host->pending_events);
	if (!node)
		goto err;

	node = debugfs_create_x32("completed_events", S_IRUSR, root,
				  (u32 *)&host->completed_events);
	if (!node)
		goto err;

	return;

err:
	dev_err(&mmc->class_dev, "failed to initialize debugfs for slot\n");
}
#endif /* defined(CONFIG_DEBUG_FS) */

static void dw_mci_set_timeout(struct dw_mci *host)
{
	/* timeout (maximum) */
	mci_writel(host, TMOUT, 0xffffffff);
}

static u32 dw_mci_prepare_command(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct mmc_data	*data;
	u32 cmdr;
	cmd->error = -EINPROGRESS;

	cmdr = cmd->opcode;

	if (cmdr == MMC_STOP_TRANSMISSION)
		cmdr |= SDMMC_CMD_STOP;
	else
		cmdr |= SDMMC_CMD_PRV_DAT_WAIT;

	if (cmd->flags & MMC_RSP_PRESENT) {
		/* We expect a response, so set this bit */
		cmdr |= SDMMC_CMD_RESP_EXP;
		if (cmd->flags & MMC_RSP_136)
			cmdr |= SDMMC_CMD_RESP_LONG;
	}

	if (cmd->flags & MMC_RSP_CRC)
		cmdr |= SDMMC_CMD_RESP_CRC;

	data = cmd->data;
	if (data) {
		cmdr |= SDMMC_CMD_DAT_EXP;
		if (data->flags & MMC_DATA_STREAM)
			cmdr |= SDMMC_CMD_STRM_MODE;
		if (data->flags & MMC_DATA_WRITE)
			cmdr |= SDMMC_CMD_DAT_WR;
	}

	return cmdr;
}

static void dw_mci_start_command(struct dw_mci *host,
				 struct mmc_command *cmd, u32 cmd_flags)
{
	host->cmd = cmd;
	dev_vdbg(&host->pdev->dev,
		 "start command: ARGR=0x%08x CMDR=0x%08x\n",
		 cmd->arg, cmd_flags);

	mci_writel(host, CMDARG, cmd->arg);
	wmb();

	mci_writel(host, CMD, cmd_flags | SDMMC_CMD_START);
}

static void send_stop_cmd(struct dw_mci *host, struct mmc_data *data)
{
	dw_mci_start_command(host, data->stop, host->stop_cmdr);
}

/* DMA interface functions */
static void dw_mci_stop_dma(struct dw_mci *host)
{
	if (host->using_dma) {
		host->dma_ops->stop(host);
		host->dma_ops->cleanup(host);
	} else {
		/* Data transfer was stopped by the interrupt handler */
		set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
	}
}

#ifdef CONFIG_MMC_DW_IDMAC
static void dw_mci_dma_cleanup(struct dw_mci *host)
{
	struct mmc_data *data = host->data;

	if (data)
		dma_unmap_sg(&host->pdev->dev, data->sg, data->sg_len,
			     ((data->flags & MMC_DATA_WRITE)
			      ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
}

static void dw_mci_idmac_stop_dma(struct dw_mci *host)
{
	u32 temp;

	/* Disable and reset the IDMAC interface */
	temp = mci_readl(host, CTRL);
	temp &= ~SDMMC_CTRL_USE_IDMAC;
	temp |= SDMMC_CTRL_DMA_RESET;
	mci_writel(host, CTRL, temp);

	/* Stop the IDMAC running */
	temp = mci_readl(host, BMOD);
	temp &= ~(SDMMC_IDMAC_ENABLE | SDMMC_IDMAC_FB);
	mci_writel(host, BMOD, temp);
}

static void dw_mci_idmac_complete_dma(struct dw_mci *host)
{
	struct mmc_data *data = host->data;

	dev_vdbg(&host->pdev->dev, "DMA complete\n");

	host->dma_ops->cleanup(host);

	/*
	 * If the card was removed, data will be NULL. No point in trying to
	 * send the stop command or waiting for NBUSY in this case.
	 */
	if (data) {
		set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
		tasklet_schedule(&host->tasklet);
	}
}

static void dw_mci_translate_sglist(struct dw_mci *host, struct mmc_data *data,
				    unsigned int sg_len)
{
	int i;
	struct idmac_desc *desc = host->sg_cpu;

	for (i = 0; i < sg_len; i++, desc++) {
		unsigned int length = sg_dma_len(&data->sg[i]);
		u32 mem_addr = sg_dma_address(&data->sg[i]);

		/* Set the OWN bit and disable interrupts for this descriptor */
		desc->des0 = IDMAC_DES0_OWN | IDMAC_DES0_DIC | IDMAC_DES0_CH;

		/* Buffer length */
		IDMAC_SET_BUFFER1_SIZE(desc, length);

		/* Physical address to DMA to/from */
		desc->des2 = mem_addr;
	}

	/* Set first descriptor */
	desc = host->sg_cpu;
	desc->des0 |= IDMAC_DES0_FD;

	/* Set last descriptor */
	desc = host->sg_cpu + (i - 1) * sizeof(struct idmac_desc);
	desc->des0 &= ~(IDMAC_DES0_CH | IDMAC_DES0_DIC);
	desc->des0 |= IDMAC_DES0_LD;

	wmb();
}

static void dw_mci_idmac_start_dma(struct dw_mci *host, unsigned int sg_len)
{
	u32 temp;

	dw_mci_translate_sglist(host, host->data, sg_len);

	/* Select IDMAC interface */
	temp = mci_readl(host, CTRL);
	temp |= SDMMC_CTRL_USE_IDMAC;
	mci_writel(host, CTRL, temp);

	wmb();

	/* Enable the IDMAC */
	temp = mci_readl(host, BMOD);
	temp |= SDMMC_IDMAC_ENABLE | SDMMC_IDMAC_FB;
	mci_writel(host, BMOD, temp);

	/* Start it running */
	mci_writel(host, PLDMND, 1);
}

static int dw_mci_idmac_init(struct dw_mci *host)
{
	struct idmac_desc *p;
	int i;

	/* Number of descriptors in the ring buffer */
	host->ring_size = PAGE_SIZE / sizeof(struct idmac_desc);

	/* Forward link the descriptor list */
	for (i = 0, p = host->sg_cpu; i < host->ring_size - 1; i++, p++)
		p->des3 = host->sg_dma + (sizeof(struct idmac_desc) * (i + 1));

	/* Set the last descriptor as the end-of-ring descriptor */
	p->des3 = host->sg_dma;
	p->des0 = IDMAC_DES0_ER;

	/* Mask out interrupts - get Tx & Rx complete only */
	mci_writel(host, IDINTEN, SDMMC_IDMAC_INT_NI | SDMMC_IDMAC_INT_RI |
		   SDMMC_IDMAC_INT_TI);

	/* Set the descriptor base address */
	mci_writel(host, DBADDR, host->sg_dma);
	return 0;
}

static struct dw_mci_dma_ops dw_mci_idmac_ops = {
	.init = dw_mci_idmac_init,
	.start = dw_mci_idmac_start_dma,
	.stop = dw_mci_idmac_stop_dma,
	.complete = dw_mci_idmac_complete_dma,
	.cleanup = dw_mci_dma_cleanup,
};
#endif /* CONFIG_MMC_DW_IDMAC */

static int dw_mci_submit_data_dma(struct dw_mci *host, struct mmc_data *data)
{
	struct scatterlist *sg;
	unsigned int i, direction, sg_len;
	u32 temp;

	host->using_dma = 0;

	/* If we don't have a channel, we can't do DMA */
	if (!host->use_dma)
		return -ENODEV;

	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-word-aligned buffers or lengths. Also, we don't bother
	 * with all the DMA setup overhead for short transfers.
	 */
	if (data->blocks * data->blksz < DW_MCI_DMA_THRESHOLD)
		return -EINVAL;
	if (data->blksz & 3)
		return -EINVAL;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3)
			return -EINVAL;
	}

	host->using_dma = 1;

	if (data->flags & MMC_DATA_READ)
		direction = DMA_FROM_DEVICE;
	else
		direction = DMA_TO_DEVICE;

	sg_len = dma_map_sg(&host->pdev->dev, data->sg, data->sg_len,
			    direction);

	dev_vdbg(&host->pdev->dev,
		 "sd sg_cpu: %#lx sg_dma: %#lx sg_len: %d\n",
		 (unsigned long)host->sg_cpu, (unsigned long)host->sg_dma,
		 sg_len);

	/* Enable the DMA interface */
	temp = mci_readl(host, CTRL);
	temp |= SDMMC_CTRL_DMA_ENABLE;
	mci_writel(host, CTRL, temp);

	/* Disable RX/TX IRQs, let DMA handle it */
	temp = mci_readl(host, INTMASK);
	temp  &= ~(SDMMC_INT_RXDR | SDMMC_INT_TXDR);
	mci_writel(host, INTMASK, temp);

	host->dma_ops->start(host, sg_len);

	return 0;
}

static void dw_mci_submit_data(struct dw_mci *host, struct mmc_data *data)
{
	u32 temp;

	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	if (data->flags & MMC_DATA_READ)
		host->dir_status = DW_MCI_RECV_STATUS;
	else
		host->dir_status = DW_MCI_SEND_STATUS;

	if (dw_mci_submit_data_dma(host, data)) {
		host->sg = data->sg;
		host->pio_offset = 0;
		host->part_buf_start = 0;
		host->part_buf_count = 0;

		mci_writel(host, RINTSTS, SDMMC_INT_TXDR | SDMMC_INT_RXDR);
		temp = mci_readl(host, INTMASK);
		temp |= SDMMC_INT_TXDR | SDMMC_INT_RXDR;
		mci_writel(host, INTMASK, temp);

		temp = mci_readl(host, CTRL);
		temp &= ~SDMMC_CTRL_DMA_ENABLE;
		mci_writel(host, CTRL, temp);
	}
}

static void mci_send_cmd(struct dw_mci_slot *slot, u32 cmd, u32 arg)
{
	struct dw_mci *host = slot->host;
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int cmd_status = 0;

	mci_writel(host, CMDARG, arg);
	wmb();
	mci_writel(host, CMD, SDMMC_CMD_START | cmd);

	while (time_before(jiffies, timeout)) {
		cmd_status = mci_readl(host, CMD);
		if (!(cmd_status & SDMMC_CMD_START))
			return;
	}
	dev_err(&slot->mmc->class_dev,
		"Timeout sending command (cmd %#x arg %#x status %#x)\n",
		cmd, arg, cmd_status);
}

static void dw_mci_setup_bus(struct dw_mci_slot *slot)
{
	struct dw_mci *host = slot->host;
	u32 div;

	if (slot->clock != host->current_speed) {
		if (host->bus_hz % slot->clock)
			/*
			 * move the + 1 after the divide to prevent
			 * over-clocking the card.
			 */
			div = ((host->bus_hz / slot->clock) >> 1) + 1;
		else
			div = (host->bus_hz  / slot->clock) >> 1;

		dev_info(&slot->mmc->class_dev,
			 "Bus speed (slot %d) = %dHz (slot req %dHz, actual %dHZ"
			 " div = %d)\n", slot->id, host->bus_hz, slot->clock,
			 div ? ((host->bus_hz / div) >> 1) : host->bus_hz, div);

		/* disable clock */
		mci_writel(host, CLKENA, 0);
		mci_writel(host, CLKSRC, 0);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		/* set clock to desired speed */
		mci_writel(host, CLKDIV, div);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		/* enable clock */
		mci_writel(host, CLKENA, SDMMC_CLKEN_ENABLE |
			   SDMMC_CLKEN_LOW_PWR);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		host->current_speed = slot->clock;
	}

	/* Set the current slot bus width */
	mci_writel(host, CTYPE, (slot->ctype << slot->id));
}

static void dw_mci_start_request(struct dw_mci *host,
				 struct dw_mci_slot *slot)
{
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data	*data;
	u32 cmdflags;

	mrq = slot->mrq;
	if (host->pdata->select_slot)
		host->pdata->select_slot(slot->id);

	/* Slot specific timing and width adjustment */
	dw_mci_setup_bus(slot);

	host->cur_slot = slot;
	host->mrq = mrq;

	host->pending_events = 0;
	host->completed_events = 0;
	host->data_status = 0;

	data = mrq->data;
	if (data) {
		dw_mci_set_timeout(host);
		mci_writel(host, BYTCNT, data->blksz*data->blocks);
		mci_writel(host, BLKSIZ, data->blksz);
	}

	cmd = mrq->cmd;
	cmdflags = dw_mci_prepare_command(slot->mmc, cmd);

	/* this is the first command, send the initialization clock */
	if (test_and_clear_bit(DW_MMC_CARD_NEED_INIT, &slot->flags))
		cmdflags |= SDMMC_CMD_INIT;

	if (data) {
		dw_mci_submit_data(host, data);
		wmb();
	}

	dw_mci_start_command(host, cmd, cmdflags);

	if (mrq->stop)
		host->stop_cmdr = dw_mci_prepare_command(slot->mmc, mrq->stop);
}

/* must be called with host->lock held */
static void dw_mci_queue_request(struct dw_mci *host, struct dw_mci_slot *slot,
				 struct mmc_request *mrq)
{
	dev_vdbg(&slot->mmc->class_dev, "queue request: state=%d\n",
		 host->state);

	slot->mrq = mrq;

	if (host->state == STATE_IDLE) {
		host->state = STATE_SENDING_CMD;
		dw_mci_start_request(host, slot);
	} else {
		list_add_tail(&slot->queue_node, &host->queue);
	}
}

static void dw_mci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;

	WARN_ON(slot->mrq);

	/*
	 * The check for card presence and queueing of the request must be
	 * atomic, otherwise the card could be removed in between and the
	 * request wouldn't fail until another card was inserted.
	 */
	spin_lock_bh(&host->lock);

	if (!test_bit(DW_MMC_CARD_PRESENT, &slot->flags)) {
		spin_unlock_bh(&host->lock);
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}

	dw_mci_queue_request(host, slot, mrq);

	spin_unlock_bh(&host->lock);
}

static void dw_mci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	u32 regs;

	/* set default 1 bit mode */
	slot->ctype = SDMMC_CTYPE_1BIT;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		slot->ctype = SDMMC_CTYPE_1BIT;
		break;
	case MMC_BUS_WIDTH_4:
		slot->ctype = SDMMC_CTYPE_4BIT;
		break;
	case MMC_BUS_WIDTH_8:
		slot->ctype = SDMMC_CTYPE_8BIT;
		break;
	}

	/* DDR mode set */
	if (ios->ddr) {
		regs = mci_readl(slot->host, UHS_REG);
		regs |= (0x1 << slot->id) << 16;
		mci_writel(slot->host, UHS_REG, regs);
	}

	if (ios->clock) {
		/*
		 * Use mirror of ios->clock to prevent race with mmc
		 * core ios update when finding the minimum.
		 */
		slot->clock = ios->clock;
	}

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		set_bit(DW_MMC_CARD_NEED_INIT, &slot->flags);
		break;
	default:
		break;
	}
}

static int dw_mci_get_ro(struct mmc_host *mmc)
{
	int read_only;
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci_board *brd = slot->host->pdata;

	/* Use platform get_ro function, else try on board write protect */
	if (brd->get_ro)
		read_only = brd->get_ro(slot->id);
	else
		read_only =
			mci_readl(slot->host, WRTPRT) & (1 << slot->id) ? 1 : 0;

	dev_dbg(&mmc->class_dev, "card is %s\n",
		read_only ? "read-only" : "read-write");

	return read_only;
}

static int dw_mci_get_cd(struct mmc_host *mmc)
{
	int present;
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci_board *brd = slot->host->pdata;

	/* Use platform get_cd function, else try onboard card detect */
	if (brd->quirks & DW_MCI_QUIRK_BROKEN_CARD_DETECTION)
		present = 1;
	else if (brd->get_cd)
		present = !brd->get_cd(slot->id);
	else
		present = (mci_readl(slot->host, CDETECT) & (1 << slot->id))
			== 0 ? 1 : 0;

	if (present)
		dev_dbg(&mmc->class_dev, "card is present\n");
	else
		dev_dbg(&mmc->class_dev, "card is not present\n");

	return present;
}

static const struct mmc_host_ops dw_mci_ops = {
	.request	= dw_mci_request,
	.set_ios	= dw_mci_set_ios,
	.get_ro		= dw_mci_get_ro,
	.get_cd		= dw_mci_get_cd,
};

static void dw_mci_request_end(struct dw_mci *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	struct dw_mci_slot *slot;
	struct mmc_host	*prev_mmc = host->cur_slot->mmc;

	WARN_ON(host->cmd || host->data);

	host->cur_slot->mrq = NULL;
	host->mrq = NULL;
	if (!list_empty(&host->queue)) {
		slot = list_entry(host->queue.next,
				  struct dw_mci_slot, queue_node);
		list_del(&slot->queue_node);
		dev_vdbg(&host->pdev->dev, "list not empty: %s is next\n",
			 mmc_hostname(slot->mmc));
		host->state = STATE_SENDING_CMD;
		dw_mci_start_request(host, slot);
	} else {
		dev_vdbg(&host->pdev->dev, "list empty\n");
		host->state = STATE_IDLE;
	}

	spin_unlock(&host->lock);
	mmc_request_done(prev_mmc, mrq);
	spin_lock(&host->lock);
}

static void dw_mci_command_complete(struct dw_mci *host, struct mmc_command *cmd)
{
	u32 status = host->cmd_status;

	host->cmd_status = 0;

	/* Read the response from the card (up to 16 bytes) */
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[3] = mci_readl(host, RESP0);
			cmd->resp[2] = mci_readl(host, RESP1);
			cmd->resp[1] = mci_readl(host, RESP2);
			cmd->resp[0] = mci_readl(host, RESP3);
		} else {
			cmd->resp[0] = mci_readl(host, RESP0);
			cmd->resp[1] = 0;
			cmd->resp[2] = 0;
			cmd->resp[3] = 0;
		}
	}

	if (status & SDMMC_INT_RTO)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (status & SDMMC_INT_RCRC))
		cmd->error = -EILSEQ;
	else if (status & SDMMC_INT_RESP_ERR)
		cmd->error = -EIO;
	else
		cmd->error = 0;

	if (cmd->error) {
		/* newer ip versions need a delay between retries */
		if (host->quirks & DW_MCI_QUIRK_RETRY_DELAY)
			mdelay(20);

		if (cmd->data) {
			host->data = NULL;
			dw_mci_stop_dma(host);
		}
	}
}

static void dw_mci_tasklet_func(unsigned long priv)
{
	struct dw_mci *host = (struct dw_mci *)priv;
	struct mmc_data	*data;
	struct mmc_command *cmd;
	enum dw_mci_state state;
	enum dw_mci_state prev_state;
	u32 status, ctrl;

	spin_lock(&host->lock);

	state = host->state;
	data = host->data;

	do {
		prev_state = state;

		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE,
						&host->pending_events))
				break;

			cmd = host->cmd;
			host->cmd = NULL;
			set_bit(EVENT_CMD_COMPLETE, &host->completed_events);
			dw_mci_command_complete(host, host->mrq->cmd);
			if (!host->mrq->data || cmd->error) {
				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_DATA;
			/* fall through */

		case STATE_SENDING_DATA:
			if (test_and_clear_bit(EVENT_DATA_ERROR,
					       &host->pending_events)) {
				dw_mci_stop_dma(host);
				if (data->stop)
					send_stop_cmd(host, data);
				state = STATE_DATA_ERROR;
				break;
			}

			if (!test_and_clear_bit(EVENT_XFER_COMPLETE,
						&host->pending_events))
				break;

			set_bit(EVENT_XFER_COMPLETE, &host->completed_events);
			prev_state = state = STATE_DATA_BUSY;
			/* fall through */

		case STATE_DATA_BUSY:
			if (!test_and_clear_bit(EVENT_DATA_COMPLETE,
						&host->pending_events))
				break;

			host->data = NULL;
			set_bit(EVENT_DATA_COMPLETE, &host->completed_events);
			status = host->data_status;

			if (status & DW_MCI_DATA_ERROR_FLAGS) {
				if (status & SDMMC_INT_DTO) {
					data->error = -ETIMEDOUT;
				} else if (status & SDMMC_INT_DCRC) {
					data->error = -EILSEQ;
				} else if (status & SDMMC_INT_EBE &&
					   host->dir_status ==
							DW_MCI_SEND_STATUS) {
					/*
					 * No data CRC status was returned.
					 * The number of bytes transferred will
					 * be exaggerated in PIO mode.
					 */
					data->bytes_xfered = 0;
					data->error = -ETIMEDOUT;
				} else {
					dev_err(&host->pdev->dev,
						"data FIFO error "
						"(status=%08x)\n",
						status);
					data->error = -EIO;
				}
				/*
				 * After an error, there may be data lingering
				 * in the FIFO, so reset it - doing so
				 * generates a block interrupt, hence setting
				 * the scatter-gather pointer to NULL.
				 */
				host->sg = NULL;
				ctrl = mci_readl(host, CTRL);
				ctrl |= SDMMC_CTRL_FIFO_RESET;
				mci_writel(host, CTRL, ctrl);
			} else {
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
			}

			if (!data->stop) {
				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (!data->error)
				send_stop_cmd(host, data);
			/* fall through */

		case STATE_SENDING_STOP:
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE,
						&host->pending_events))
				break;

			host->cmd = NULL;
			dw_mci_command_complete(host, host->mrq->stop);
			dw_mci_request_end(host, host->mrq);
			goto unlock;

		case STATE_DATA_ERROR:
			if (!test_and_clear_bit(EVENT_XFER_COMPLETE,
						&host->pending_events))
				break;

			state = STATE_DATA_BUSY;
			break;
		}
	} while (state != prev_state);

	host->state = state;
unlock:
	spin_unlock(&host->lock);

}

/* push final bytes to part_buf, only use during push */
static void dw_mci_set_part_bytes(struct dw_mci *host, void *buf, int cnt)
{
	memcpy((void *)&host->part_buf, buf, cnt);
	host->part_buf_count = cnt;
}

/* append bytes to part_buf, only use during push */
static int dw_mci_push_part_bytes(struct dw_mci *host, void *buf, int cnt)
{
	cnt = min(cnt, (1 << host->data_shift) - host->part_buf_count);
	memcpy((void *)&host->part_buf + host->part_buf_count, buf, cnt);
	host->part_buf_count += cnt;
	return cnt;
}

/* pull first bytes from part_buf, only use during pull */
static int dw_mci_pull_part_bytes(struct dw_mci *host, void *buf, int cnt)
{
	cnt = min(cnt, (int)host->part_buf_count);
	if (cnt) {
		memcpy(buf, (void *)&host->part_buf + host->part_buf_start,
		       cnt);
		host->part_buf_count -= cnt;
		host->part_buf_start += cnt;
	}
	return cnt;
}

/* pull final bytes from the part_buf, assuming it's just been filled */
static void dw_mci_pull_final_bytes(struct dw_mci *host, void *buf, int cnt)
{
	memcpy(buf, &host->part_buf, cnt);
	host->part_buf_start = cnt;
	host->part_buf_count = (1 << host->data_shift) - cnt;
}

static void dw_mci_push_data16(struct dw_mci *host, void *buf, int cnt)
{
	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		buf += len;
		cnt -= len;
		if (!sg_next(host->sg) || host->part_buf_count == 2) {
			mci_writew(host, DATA, host->part_buf16);
			host->part_buf_count = 0;
		}
	}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x1)) {
		while (cnt >= 2) {
			u16 aligned_buf[64];
			int len = min(cnt & -2, (int)sizeof(aligned_buf));
			int items = len >> 1;
			int i;
			/* memcpy from input buffer into aligned buffer */
			memcpy(aligned_buf, buf, len);
			buf += len;
			cnt -= len;
			/* push data from aligned buffer into fifo */
			for (i = 0; i < items; ++i)
				mci_writew(host, DATA, aligned_buf[i]);
		}
	} else
#endif
	{
		u16 *pdata = buf;
		for (; cnt >= 2; cnt -= 2)
			mci_writew(host, DATA, *pdata++);
		buf = pdata;
	}
	/* put anything remaining in the part_buf */
	if (cnt) {
		dw_mci_set_part_bytes(host, buf, cnt);
		if (!sg_next(host->sg))
			mci_writew(host, DATA, host->part_buf16);
	}
}

static void dw_mci_pull_data16(struct dw_mci *host, void *buf, int cnt)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x1)) {
		while (cnt >= 2) {
			/* pull data from fifo into aligned buffer */
			u16 aligned_buf[64];
			int len = min(cnt & -2, (int)sizeof(aligned_buf));
			int items = len >> 1;
			int i;
			for (i = 0; i < items; ++i)
				aligned_buf[i] = mci_readw(host, DATA);
			/* memcpy from aligned buffer into output buffer */
			memcpy(buf, aligned_buf, len);
			buf += len;
			cnt -= len;
		}
	} else
#endif
	{
		u16 *pdata = buf;
		for (; cnt >= 2; cnt -= 2)
			*pdata++ = mci_readw(host, DATA);
		buf = pdata;
	}
	if (cnt) {
		host->part_buf16 = mci_readw(host, DATA);
		dw_mci_pull_final_bytes(host, buf, cnt);
	}
}

static void dw_mci_push_data32(struct dw_mci *host, void *buf, int cnt)
{
	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		buf += len;
		cnt -= len;
		if (!sg_next(host->sg) || host->part_buf_count == 4) {
			mci_writel(host, DATA, host->part_buf32);
			host->part_buf_count = 0;
		}
	}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x3)) {
		while (cnt >= 4) {
			u32 aligned_buf[32];
			int len = min(cnt & -4, (int)sizeof(aligned_buf));
			int items = len >> 2;
			int i;
			/* memcpy from input buffer into aligned buffer */
			memcpy(aligned_buf, buf, len);
			buf += len;
			cnt -= len;
			/* push data from aligned buffer into fifo */
			for (i = 0; i < items; ++i)
				mci_writel(host, DATA, aligned_buf[i]);
		}
	} else
#endif
	{
		u32 *pdata = buf;
		for (; cnt >= 4; cnt -= 4)
			mci_writel(host, DATA, *pdata++);
		buf = pdata;
	}
	/* put anything remaining in the part_buf */
	if (cnt) {
		dw_mci_set_part_bytes(host, buf, cnt);
		if (!sg_next(host->sg))
			mci_writel(host, DATA, host->part_buf32);
	}
}

static void dw_mci_pull_data32(struct dw_mci *host, void *buf, int cnt)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x3)) {
		while (cnt >= 4) {
			/* pull data from fifo into aligned buffer */
			u32 aligned_buf[32];
			int len = min(cnt & -4, (int)sizeof(aligned_buf));
			int items = len >> 2;
			int i;
			for (i = 0; i < items; ++i)
				aligned_buf[i] = mci_readl(host, DATA);
			/* memcpy from aligned buffer into output buffer */
			memcpy(buf, aligned_buf, len);
			buf += len;
			cnt -= len;
		}
	} else
#endif
	{
		u32 *pdata = buf;
		for (; cnt >= 4; cnt -= 4)
			*pdata++ = mci_readl(host, DATA);
		buf = pdata;
	}
	if (cnt) {
		host->part_buf32 = mci_readl(host, DATA);
		dw_mci_pull_final_bytes(host, buf, cnt);
	}
}

static void dw_mci_push_data64(struct dw_mci *host, void *buf, int cnt)
{
	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		buf += len;
		cnt -= len;
		if (!sg_next(host->sg) || host->part_buf_count == 8) {
			mci_writew(host, DATA, host->part_buf);
			host->part_buf_count = 0;
		}
	}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x7)) {
		while (cnt >= 8) {
			u64 aligned_buf[16];
			int len = min(cnt & -8, (int)sizeof(aligned_buf));
			int items = len >> 3;
			int i;
			/* memcpy from input buffer into aligned buffer */
			memcpy(aligned_buf, buf, len);
			buf += len;
			cnt -= len;
			/* push data from aligned buffer into fifo */
			for (i = 0; i < items; ++i)
				mci_writeq(host, DATA, aligned_buf[i]);
		}
	} else
#endif
	{
		u64 *pdata = buf;
		for (; cnt >= 8; cnt -= 8)
			mci_writeq(host, DATA, *pdata++);
		buf = pdata;
	}
	/* put anything remaining in the part_buf */
	if (cnt) {
		dw_mci_set_part_bytes(host, buf, cnt);
		if (!sg_next(host->sg))
			mci_writeq(host, DATA, host->part_buf);
	}
}

static void dw_mci_pull_data64(struct dw_mci *host, void *buf, int cnt)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x7)) {
		while (cnt >= 8) {
			/* pull data from fifo into aligned buffer */
			u64 aligned_buf[16];
			int len = min(cnt & -8, (int)sizeof(aligned_buf));
			int items = len >> 3;
			int i;
			for (i = 0; i < items; ++i)
				aligned_buf[i] = mci_readq(host, DATA);
			/* memcpy from aligned buffer into output buffer */
			memcpy(buf, aligned_buf, len);
			buf += len;
			cnt -= len;
		}
	} else
#endif
	{
		u64 *pdata = buf;
		for (; cnt >= 8; cnt -= 8)
			*pdata++ = mci_readq(host, DATA);
		buf = pdata;
	}
	if (cnt) {
		host->part_buf = mci_readq(host, DATA);
		dw_mci_pull_final_bytes(host, buf, cnt);
	}
}

static void dw_mci_pull_data(struct dw_mci *host, void *buf, int cnt)
{
	int len;

	/* get remaining partial bytes */
	len = dw_mci_pull_part_bytes(host, buf, cnt);
	if (unlikely(len == cnt))
		return;
	buf += len;
	cnt -= len;

	/* get the rest of the data */
	host->pull_data(host, buf, cnt);
}

static void dw_mci_read_data_pio(struct dw_mci *host)
{
	struct scatterlist *sg = host->sg;
	void *buf = sg_virt(sg);
	unsigned int offset = host->pio_offset;
	struct mmc_data	*data = host->data;
	int shift = host->data_shift;
	u32 status;
	unsigned int nbytes = 0, len;

	do {
		len = host->part_buf_count +
			(SDMMC_GET_FCNT(mci_readl(host, STATUS)) << shift);
		if (offset + len <= sg->length) {
			dw_mci_pull_data(host, (void *)(buf + offset), len);

			offset += len;
			nbytes += len;

			if (offset == sg->length) {
				flush_dcache_page(sg_page(sg));
				host->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
				buf = sg_virt(sg);
			}
		} else {
			unsigned int remaining = sg->length - offset;
			dw_mci_pull_data(host, (void *)(buf + offset),
					 remaining);
			nbytes += remaining;

			flush_dcache_page(sg_page(sg));
			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = len - remaining;
			buf = sg_virt(sg);
			dw_mci_pull_data(host, buf, offset);
			nbytes += offset;
		}

		status = mci_readl(host, MINTSTS);
		mci_writel(host, RINTSTS, SDMMC_INT_RXDR);
		if (status & DW_MCI_DATA_ERROR_FLAGS) {
			host->data_status = status;
			data->bytes_xfered += nbytes;
			smp_wmb();

			set_bit(EVENT_DATA_ERROR, &host->pending_events);

			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & SDMMC_INT_RXDR); /*if the RXDR is ready read again*/
	host->pio_offset = offset;
	data->bytes_xfered += nbytes;
	return;

done:
	data->bytes_xfered += nbytes;
	smp_wmb();
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static void dw_mci_write_data_pio(struct dw_mci *host)
{
	struct scatterlist *sg = host->sg;
	void *buf = sg_virt(sg);
	unsigned int offset = host->pio_offset;
	struct mmc_data	*data = host->data;
	int shift = host->data_shift;
	u32 status;
	unsigned int nbytes = 0, len;

	do {
		len = ((host->fifo_depth -
			SDMMC_GET_FCNT(mci_readl(host, STATUS))) << shift)
			- host->part_buf_count;
		if (offset + len <= sg->length) {
			host->push_data(host, (void *)(buf + offset), len);

			offset += len;
			nbytes += len;
			if (offset == sg->length) {
				host->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
				buf = sg_virt(sg);
			}
		} else {
			unsigned int remaining = sg->length - offset;

			host->push_data(host, (void *)(buf + offset),
					remaining);
			nbytes += remaining;

			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = len - remaining;
			buf = sg_virt(sg);
			host->push_data(host, (void *)buf, offset);
			nbytes += offset;
		}

		status = mci_readl(host, MINTSTS);
		mci_writel(host, RINTSTS, SDMMC_INT_TXDR);
		if (status & DW_MCI_DATA_ERROR_FLAGS) {
			host->data_status = status;
			data->bytes_xfered += nbytes;

			smp_wmb();

			set_bit(EVENT_DATA_ERROR, &host->pending_events);

			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & SDMMC_INT_TXDR); /* if TXDR write again */
	host->pio_offset = offset;
	data->bytes_xfered += nbytes;
	return;

done:
	data->bytes_xfered += nbytes;
	smp_wmb();
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static void dw_mci_cmd_interrupt(struct dw_mci *host, u32 status)
{
	if (!host->cmd_status)
		host->cmd_status = status;

	smp_wmb();

	set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t dw_mci_interrupt(int irq, void *dev_id)
{
	struct dw_mci *host = dev_id;
	u32 status, pending;
	unsigned int pass_count = 0;

	do {
		status = mci_readl(host, RINTSTS);
		pending = mci_readl(host, MINTSTS); /* read-only mask reg */

		/*
		 * DTO fix - version 2.10a and below, and only if internal DMA
		 * is configured.
		 */
		if (host->quirks & DW_MCI_QUIRK_IDMAC_DTO) {
			if (!pending &&
			    ((mci_readl(host, STATUS) >> 17) & 0x1fff))
				pending |= SDMMC_INT_DATA_OVER;
		}

		if (!pending)
			break;

		if (pending & DW_MCI_CMD_ERROR_FLAGS) {
			mci_writel(host, RINTSTS, DW_MCI_CMD_ERROR_FLAGS);
			host->cmd_status = status;
			smp_wmb();
			set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
		}

		if (pending & DW_MCI_DATA_ERROR_FLAGS) {
			/* if there is an error report DATA_ERROR */
			mci_writel(host, RINTSTS, DW_MCI_DATA_ERROR_FLAGS);
			host->data_status = status;
			smp_wmb();
			set_bit(EVENT_DATA_ERROR, &host->pending_events);
			if (!(pending & (SDMMC_INT_DTO | SDMMC_INT_DCRC |
					 SDMMC_INT_SBE | SDMMC_INT_EBE)))
				tasklet_schedule(&host->tasklet);
		}

		if (pending & SDMMC_INT_DATA_OVER) {
			mci_writel(host, RINTSTS, SDMMC_INT_DATA_OVER);
			if (!host->data_status)
				host->data_status = status;
			smp_wmb();
			if (host->dir_status == DW_MCI_RECV_STATUS) {
				if (host->sg != NULL)
					dw_mci_read_data_pio(host);
			}
			set_bit(EVENT_DATA_COMPLETE, &host->pending_events);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & SDMMC_INT_RXDR) {
			mci_writel(host, RINTSTS, SDMMC_INT_RXDR);
			if (host->dir_status == DW_MCI_RECV_STATUS && host->sg)
				dw_mci_read_data_pio(host);
		}

		if (pending & SDMMC_INT_TXDR) {
			mci_writel(host, RINTSTS, SDMMC_INT_TXDR);
			if (host->dir_status == DW_MCI_SEND_STATUS && host->sg)
				dw_mci_write_data_pio(host);
		}

		if (pending & SDMMC_INT_CMD_DONE) {
			mci_writel(host, RINTSTS, SDMMC_INT_CMD_DONE);
			dw_mci_cmd_interrupt(host, status);
		}

		if (pending & SDMMC_INT_CD) {
			mci_writel(host, RINTSTS, SDMMC_INT_CD);
			queue_work(dw_mci_card_workqueue, &host->card_work);
		}

	} while (pass_count++ < 5);

#ifdef CONFIG_MMC_DW_IDMAC
	/* Handle DMA interrupts */
	pending = mci_readl(host, IDSTS);
	if (pending & (SDMMC_IDMAC_INT_TI | SDMMC_IDMAC_INT_RI)) {
		mci_writel(host, IDSTS, SDMMC_IDMAC_INT_TI | SDMMC_IDMAC_INT_RI);
		mci_writel(host, IDSTS, SDMMC_IDMAC_INT_NI);
		set_bit(EVENT_DATA_COMPLETE, &host->pending_events);
		host->dma_ops->complete(host);
	}
#endif

	return IRQ_HANDLED;
}

static void dw_mci_work_routine_card(struct work_struct *work)
{
	struct dw_mci *host = container_of(work, struct dw_mci, card_work);
	int i;

	for (i = 0; i < host->num_slots; i++) {
		struct dw_mci_slot *slot = host->slot[i];
		struct mmc_host *mmc = slot->mmc;
		struct mmc_request *mrq;
		int present;
		u32 ctrl;

		present = dw_mci_get_cd(mmc);
		while (present != slot->last_detect_state) {
			dev_dbg(&slot->mmc->class_dev, "card %s\n",
				present ? "inserted" : "removed");

			/* Power up slot (before spin_lock, may sleep) */
			if (present != 0 && host->pdata->setpower)
				host->pdata->setpower(slot->id, mmc->ocr_avail);

			spin_lock_bh(&host->lock);

			/* Card change detected */
			slot->last_detect_state = present;

			/* Mark card as present if applicable */
			if (present != 0)
				set_bit(DW_MMC_CARD_PRESENT, &slot->flags);

			/* Clean up queue if present */
			mrq = slot->mrq;
			if (mrq) {
				if (mrq == host->mrq) {
					host->data = NULL;
					host->cmd = NULL;

					switch (host->state) {
					case STATE_IDLE:
						break;
					case STATE_SENDING_CMD:
						mrq->cmd->error = -ENOMEDIUM;
						if (!mrq->data)
							break;
						/* fall through */
					case STATE_SENDING_DATA:
						mrq->data->error = -ENOMEDIUM;
						dw_mci_stop_dma(host);
						break;
					case STATE_DATA_BUSY:
					case STATE_DATA_ERROR:
						if (mrq->data->error == -EINPROGRESS)
							mrq->data->error = -ENOMEDIUM;
						if (!mrq->stop)
							break;
						/* fall through */
					case STATE_SENDING_STOP:
						mrq->stop->error = -ENOMEDIUM;
						break;
					}

					dw_mci_request_end(host, mrq);
				} else {
					list_del(&slot->queue_node);
					mrq->cmd->error = -ENOMEDIUM;
					if (mrq->data)
						mrq->data->error = -ENOMEDIUM;
					if (mrq->stop)
						mrq->stop->error = -ENOMEDIUM;

					spin_unlock(&host->lock);
					mmc_request_done(slot->mmc, mrq);
					spin_lock(&host->lock);
				}
			}

			/* Power down slot */
			if (present == 0) {
				clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);

				/*
				 * Clear down the FIFO - doing so generates a
				 * block interrupt, hence setting the
				 * scatter-gather pointer to NULL.
				 */
				host->sg = NULL;

				ctrl = mci_readl(host, CTRL);
				ctrl |= SDMMC_CTRL_FIFO_RESET;
				mci_writel(host, CTRL, ctrl);

#ifdef CONFIG_MMC_DW_IDMAC
				ctrl = mci_readl(host, BMOD);
				ctrl |= 0x01; /* Software reset of DMA */
				mci_writel(host, BMOD, ctrl);
#endif

			}

			spin_unlock_bh(&host->lock);

			/* Power down slot (after spin_unlock, may sleep) */
			if (present == 0 && host->pdata->setpower)
				host->pdata->setpower(slot->id, 0);

			present = dw_mci_get_cd(mmc);
		}

		mmc_detect_change(slot->mmc,
			msecs_to_jiffies(host->pdata->detect_delay_ms));
	}
}

static int __init dw_mci_init_slot(struct dw_mci *host, unsigned int id)
{
	struct mmc_host *mmc;
	struct dw_mci_slot *slot;

	mmc = mmc_alloc_host(sizeof(struct dw_mci_slot), &host->pdev->dev);
	if (!mmc)
		return -ENOMEM;

	slot = mmc_priv(mmc);
	slot->id = id;
	slot->mmc = mmc;
	slot->host = host;

	mmc->ops = &dw_mci_ops;
	mmc->f_min = DIV_ROUND_UP(host->bus_hz, 510);
	mmc->f_max = host->bus_hz;

	if (host->pdata->get_ocr)
		mmc->ocr_avail = host->pdata->get_ocr(id);
	else
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/*
	 * Start with slot power disabled, it will be enabled when a card
	 * is detected.
	 */
	if (host->pdata->setpower)
		host->pdata->setpower(id, 0);

	if (host->pdata->caps)
		mmc->caps = host->pdata->caps;
	else
		mmc->caps = 0;

	if (host->pdata->get_bus_wd)
		if (host->pdata->get_bus_wd(slot->id) >= 4)
			mmc->caps |= MMC_CAP_4_BIT_DATA;

	if (host->pdata->quirks & DW_MCI_QUIRK_HIGHSPEED)
		mmc->caps |= MMC_CAP_SD_HIGHSPEED;

#ifdef CONFIG_MMC_DW_IDMAC
	mmc->max_segs = host->ring_size;
	mmc->max_blk_size = 65536;
	mmc->max_blk_count = host->ring_size;
	mmc->max_seg_size = 0x1000;
	mmc->max_req_size = mmc->max_seg_size * mmc->max_blk_count;
#else
	if (host->pdata->blk_settings) {
		mmc->max_segs = host->pdata->blk_settings->max_segs;
		mmc->max_blk_size = host->pdata->blk_settings->max_blk_size;
		mmc->max_blk_count = host->pdata->blk_settings->max_blk_count;
		mmc->max_req_size = host->pdata->blk_settings->max_req_size;
		mmc->max_seg_size = host->pdata->blk_settings->max_seg_size;
	} else {
		/* Useful defaults if platform data is unset. */
		mmc->max_segs = 64;
		mmc->max_blk_size = 65536; /* BLKSIZ is 16 bits */
		mmc->max_blk_count = 512;
		mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
		mmc->max_seg_size = mmc->max_req_size;
	}
#endif /* CONFIG_MMC_DW_IDMAC */

	host->vmmc = regulator_get(mmc_dev(mmc), "vmmc");
	if (IS_ERR(host->vmmc)) {
		printk(KERN_INFO "%s: no vmmc regulator found\n", mmc_hostname(mmc));
		host->vmmc = NULL;
	} else
		regulator_enable(host->vmmc);

	if (dw_mci_get_cd(mmc))
		set_bit(DW_MMC_CARD_PRESENT, &slot->flags);
	else
		clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);

	host->slot[id] = slot;
	mmc_add_host(mmc);

#if defined(CONFIG_DEBUG_FS)
	dw_mci_init_debugfs(slot);
#endif

	/* Card initially undetected */
	slot->last_detect_state = 0;

	/*
	 * Card may have been plugged in prior to boot so we
	 * need to run the detect tasklet
	 */
	queue_work(dw_mci_card_workqueue, &host->card_work);

	return 0;
}

static void dw_mci_cleanup_slot(struct dw_mci_slot *slot, unsigned int id)
{
	/* Shutdown detect IRQ */
	if (slot->host->pdata->exit)
		slot->host->pdata->exit(id);

	/* Debugfs stuff is cleaned up by mmc core */
	mmc_remove_host(slot->mmc);
	slot->host->slot[id] = NULL;
	mmc_free_host(slot->mmc);
}

static void dw_mci_init_dma(struct dw_mci *host)
{
	/* Alloc memory for sg translation */
	host->sg_cpu = dma_alloc_coherent(&host->pdev->dev, PAGE_SIZE,
					  &host->sg_dma, GFP_KERNEL);
	if (!host->sg_cpu) {
		dev_err(&host->pdev->dev, "%s: could not alloc DMA memory\n",
			__func__);
		goto no_dma;
	}

	/* Determine which DMA interface to use */
#ifdef CONFIG_MMC_DW_IDMAC
	host->dma_ops = &dw_mci_idmac_ops;
	dev_info(&host->pdev->dev, "Using internal DMA controller.\n");
#endif

	if (!host->dma_ops)
		goto no_dma;

	if (host->dma_ops->init) {
		if (host->dma_ops->init(host)) {
			dev_err(&host->pdev->dev, "%s: Unable to initialize "
				"DMA Controller.\n", __func__);
			goto no_dma;
		}
	} else {
		dev_err(&host->pdev->dev, "DMA initialization not found.\n");
		goto no_dma;
	}

	host->use_dma = 1;
	return;

no_dma:
	dev_info(&host->pdev->dev, "Using PIO mode.\n");
	host->use_dma = 0;
	return;
}

static bool mci_wait_reset(struct device *dev, struct dw_mci *host)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int ctrl;

	mci_writel(host, CTRL, (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET |
				SDMMC_CTRL_DMA_RESET));

	/* wait till resets clear */
	do {
		ctrl = mci_readl(host, CTRL);
		if (!(ctrl & (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET |
			      SDMMC_CTRL_DMA_RESET)))
			return true;
	} while (time_before(jiffies, timeout));

	dev_err(dev, "Timeout resetting block (ctrl %#x)\n", ctrl);

	return false;
}

static int dw_mci_probe(struct platform_device *pdev)
{
	struct dw_mci *host;
	struct resource	*regs;
	struct dw_mci_board *pdata;
	int irq, ret, i, width;
	u32 fifo_size;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	host = kzalloc(sizeof(struct dw_mci), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->pdev = pdev;
	host->pdata = pdata = pdev->dev.platform_data;
	if (!pdata || !pdata->init) {
		dev_err(&pdev->dev,
			"Platform data must supply init function\n");
		ret = -ENODEV;
		goto err_freehost;
	}

	if (!pdata->select_slot && pdata->num_slots > 1) {
		dev_err(&pdev->dev,
			"Platform data must supply select_slot function\n");
		ret = -ENODEV;
		goto err_freehost;
	}

	if (!pdata->bus_hz) {
		dev_err(&pdev->dev,
			"Platform data must supply bus speed\n");
		ret = -ENODEV;
		goto err_freehost;
	}

	host->bus_hz = pdata->bus_hz;
	host->quirks = pdata->quirks;

	spin_lock_init(&host->lock);
	INIT_LIST_HEAD(&host->queue);

	ret = -ENOMEM;
	host->regs = ioremap(regs->start, resource_size(regs));
	if (!host->regs)
		goto err_freehost;

	host->dma_ops = pdata->dma_ops;
	dw_mci_init_dma(host);

	/*
	 * Get the host data width - this assumes that HCON has been set with
	 * the correct values.
	 */
	i = (mci_readl(host, HCON) >> 7) & 0x7;
	if (!i) {
		host->push_data = dw_mci_push_data16;
		host->pull_data = dw_mci_pull_data16;
		width = 16;
		host->data_shift = 1;
	} else if (i == 2) {
		host->push_data = dw_mci_push_data64;
		host->pull_data = dw_mci_pull_data64;
		width = 64;
		host->data_shift = 3;
	} else {
		/* Check for a reserved value, and warn if it is */
		WARN((i != 1),
		     "HCON reports a reserved host data width!\n"
		     "Defaulting to 32-bit access.\n");
		host->push_data = dw_mci_push_data32;
		host->pull_data = dw_mci_pull_data32;
		width = 32;
		host->data_shift = 2;
	}

	/* Reset all blocks */
	if (!mci_wait_reset(&pdev->dev, host)) {
		ret = -ENODEV;
		goto err_dmaunmap;
	}

	/* Clear the interrupts for the host controller */
	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	mci_writel(host, INTMASK, 0); /* disable all mmc interrupt first */

	/* Put in max timeout */
	mci_writel(host, TMOUT, 0xFFFFFFFF);

	/*
	 * FIFO threshold settings  RxMark  = fifo_size / 2 - 1,
	 *                          Tx Mark = fifo_size / 2 DMA Size = 8
	 */
	if (!host->pdata->fifo_depth) {
		/*
		 * Power-on value of RX_WMark is FIFO_DEPTH-1, but this may
		 * have been overwritten by the bootloader, just like we're
		 * about to do, so if you know the value for your hardware, you
		 * should put it in the platform data.
		 */
		fifo_size = mci_readl(host, FIFOTH);
		fifo_size = 1 + ((fifo_size >> 16) & 0x7ff);
	} else {
		fifo_size = host->pdata->fifo_depth;
	}
	host->fifo_depth = fifo_size;
	host->fifoth_val = ((0x2 << 28) | ((fifo_size/2 - 1) << 16) |
			((fifo_size/2) << 0));
	mci_writel(host, FIFOTH, host->fifoth_val);

	/* disable clock to CIU */
	mci_writel(host, CLKENA, 0);
	mci_writel(host, CLKSRC, 0);

	tasklet_init(&host->tasklet, dw_mci_tasklet_func, (unsigned long)host);
	dw_mci_card_workqueue = alloc_workqueue("dw-mci-card",
			WQ_MEM_RECLAIM | WQ_NON_REENTRANT, 1);
	if (!dw_mci_card_workqueue)
		goto err_dmaunmap;
	INIT_WORK(&host->card_work, dw_mci_work_routine_card);

	ret = request_irq(irq, dw_mci_interrupt, 0, "dw-mci", host);
	if (ret)
		goto err_workqueue;

	platform_set_drvdata(pdev, host);

	if (host->pdata->num_slots)
		host->num_slots = host->pdata->num_slots;
	else
		host->num_slots = ((mci_readl(host, HCON) >> 1) & 0x1F) + 1;

	/* We need at least one slot to succeed */
	for (i = 0; i < host->num_slots; i++) {
		ret = dw_mci_init_slot(host, i);
		if (ret) {
			ret = -ENODEV;
			goto err_init_slot;
		}
	}

	/*
	 * Enable interrupts for command done, data over, data empty, card det,
	 * receive ready and error such as transmit, receive timeout, crc error
	 */
	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	mci_writel(host, INTMASK, SDMMC_INT_CMD_DONE | SDMMC_INT_DATA_OVER |
		   SDMMC_INT_TXDR | SDMMC_INT_RXDR |
		   DW_MCI_ERROR_FLAGS | SDMMC_INT_CD);
	mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE); /* Enable mci interrupt */

	dev_info(&pdev->dev, "DW MMC controller at irq %d, "
		 "%d bit host data width, "
		 "%u deep fifo\n",
		 irq, width, fifo_size);
	if (host->quirks & DW_MCI_QUIRK_IDMAC_DTO)
		dev_info(&pdev->dev, "Internal DMAC interrupt fix enabled.\n");

	return 0;

err_init_slot:
	/* De-init any initialized slots */
	while (i > 0) {
		if (host->slot[i])
			dw_mci_cleanup_slot(host->slot[i], i);
		i--;
	}
	free_irq(irq, host);

err_workqueue:
	destroy_workqueue(dw_mci_card_workqueue);

err_dmaunmap:
	if (host->use_dma && host->dma_ops->exit)
		host->dma_ops->exit(host);
	dma_free_coherent(&host->pdev->dev, PAGE_SIZE,
			  host->sg_cpu, host->sg_dma);
	iounmap(host->regs);

	if (host->vmmc) {
		regulator_disable(host->vmmc);
		regulator_put(host->vmmc);
	}


err_freehost:
	kfree(host);
	return ret;
}

static int __exit dw_mci_remove(struct platform_device *pdev)
{
	struct dw_mci *host = platform_get_drvdata(pdev);
	int i;

	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	mci_writel(host, INTMASK, 0); /* disable all mmc interrupt first */

	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < host->num_slots; i++) {
		dev_dbg(&pdev->dev, "remove slot %d\n", i);
		if (host->slot[i])
			dw_mci_cleanup_slot(host->slot[i], i);
	}

	/* disable clock to CIU */
	mci_writel(host, CLKENA, 0);
	mci_writel(host, CLKSRC, 0);

	free_irq(platform_get_irq(pdev, 0), host);
	destroy_workqueue(dw_mci_card_workqueue);
	dma_free_coherent(&pdev->dev, PAGE_SIZE, host->sg_cpu, host->sg_dma);

	if (host->use_dma && host->dma_ops->exit)
		host->dma_ops->exit(host);

	if (host->vmmc) {
		regulator_disable(host->vmmc);
		regulator_put(host->vmmc);
	}

	iounmap(host->regs);

	kfree(host);
	return 0;
}

#ifdef CONFIG_PM
/*
 * TODO: we should probably disable the clock to the card in the suspend path.
 */
static int dw_mci_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	int i, ret;
	struct dw_mci *host = platform_get_drvdata(pdev);

	for (i = 0; i < host->num_slots; i++) {
		struct dw_mci_slot *slot = host->slot[i];
		if (!slot)
			continue;
		ret = mmc_suspend_host(slot->mmc);
		if (ret < 0) {
			while (--i >= 0) {
				slot = host->slot[i];
				if (slot)
					mmc_resume_host(host->slot[i]->mmc);
			}
			return ret;
		}
	}

	if (host->vmmc)
		regulator_disable(host->vmmc);

	return 0;
}

static int dw_mci_resume(struct platform_device *pdev)
{
	int i, ret;
	struct dw_mci *host = platform_get_drvdata(pdev);

	if (host->vmmc)
		regulator_enable(host->vmmc);

	if (host->dma_ops->init)
		host->dma_ops->init(host);

	if (!mci_wait_reset(&pdev->dev, host)) {
		ret = -ENODEV;
		return ret;
	}

	/* Restore the old value at FIFOTH register */
	mci_writel(host, FIFOTH, host->fifoth_val);

	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	mci_writel(host, INTMASK, SDMMC_INT_CMD_DONE | SDMMC_INT_DATA_OVER |
		   SDMMC_INT_TXDR | SDMMC_INT_RXDR |
		   DW_MCI_ERROR_FLAGS | SDMMC_INT_CD);
	mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE);

	for (i = 0; i < host->num_slots; i++) {
		struct dw_mci_slot *slot = host->slot[i];
		if (!slot)
			continue;
		ret = mmc_resume_host(host->slot[i]->mmc);
		if (ret < 0)
			return ret;
	}

	return 0;
}
#else
#define dw_mci_suspend	NULL
#define dw_mci_resume	NULL
#endif /* CONFIG_PM */

static struct platform_driver dw_mci_driver = {
	.remove		= __exit_p(dw_mci_remove),
	.suspend	= dw_mci_suspend,
	.resume		= dw_mci_resume,
	.driver		= {
		.name		= "dw_mmc",
	},
};

static int __init dw_mci_init(void)
{
	return platform_driver_probe(&dw_mci_driver, dw_mci_probe);
}

static void __exit dw_mci_exit(void)
{
	platform_driver_unregister(&dw_mci_driver);
}

module_init(dw_mci_init);
module_exit(dw_mci_exit);

MODULE_DESCRIPTION("DW Multimedia Card Interface driver");
MODULE_AUTHOR("NXP Semiconductor VietNam");
MODULE_AUTHOR("Imagination Technologies Ltd");
MODULE_LICENSE("GPL v2");
