/*
 * Atmel MultiMedia Card Interface driver
 *
 * Copyright (C) 2004-2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/blkdev.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/stat.h>

#include <linux/mmc/host.h>
#include <linux/atmel-mci.h>

#include <asm/io.h>
#include <asm/unaligned.h>

#include <mach/board.h>

#include "atmel-mci-regs.h"

#define ATMCI_DATA_ERROR_FLAGS	(MCI_DCRCE | MCI_DTOE | MCI_OVRE | MCI_UNRE)
#define ATMCI_DMA_THRESHOLD	16

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_XFER_COMPLETE,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
};

enum atmel_mci_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_SENDING_DATA,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
	STATE_DATA_ERROR,
};

struct atmel_mci_dma {
#ifdef CONFIG_MMC_ATMELMCI_DMA
	struct dma_chan			*chan;
	struct dma_async_tx_descriptor	*data_desc;
#endif
};

/**
 * struct atmel_mci - MMC controller state shared between all slots
 * @lock: Spinlock protecting the queue and associated data.
 * @regs: Pointer to MMIO registers.
 * @sg: Scatterlist entry currently being processed by PIO code, if any.
 * @pio_offset: Offset into the current scatterlist entry.
 * @cur_slot: The slot which is currently using the controller.
 * @mrq: The request currently being processed on @cur_slot,
 *	or NULL if the controller is idle.
 * @cmd: The command currently being sent to the card, or NULL.
 * @data: The data currently being transferred, or NULL if no data
 *	transfer is in progress.
 * @dma: DMA client state.
 * @data_chan: DMA channel being used for the current data transfer.
 * @cmd_status: Snapshot of SR taken upon completion of the current
 *	command. Only valid when EVENT_CMD_COMPLETE is pending.
 * @data_status: Snapshot of SR taken upon completion of the current
 *	data transfer. Only valid when EVENT_DATA_COMPLETE or
 *	EVENT_DATA_ERROR is pending.
 * @stop_cmdr: Value to be loaded into CMDR when the stop command is
 *	to be sent.
 * @tasklet: Tasklet running the request state machine.
 * @pending_events: Bitmask of events flagged by the interrupt handler
 *	to be processed by the tasklet.
 * @completed_events: Bitmask of events which the state machine has
 *	processed.
 * @state: Tasklet state.
 * @queue: List of slots waiting for access to the controller.
 * @need_clock_update: Update the clock rate before the next request.
 * @need_reset: Reset controller before next request.
 * @mode_reg: Value of the MR register.
 * @bus_hz: The rate of @mck in Hz. This forms the basis for MMC bus
 *	rate and timeout calculations.
 * @mapbase: Physical address of the MMIO registers.
 * @mck: The peripheral bus clock hooked up to the MMC controller.
 * @pdev: Platform device associated with the MMC controller.
 * @slot: Slots sharing this MMC controller.
 *
 * Locking
 * =======
 *
 * @lock is a softirq-safe spinlock protecting @queue as well as
 * @cur_slot, @mrq and @state. These must always be updated
 * at the same time while holding @lock.
 *
 * @lock also protects mode_reg and need_clock_update since these are
 * used to synchronize mode register updates with the queue
 * processing.
 *
 * The @mrq field of struct atmel_mci_slot is also protected by @lock,
 * and must always be written at the same time as the slot is added to
 * @queue.
 *
 * @pending_events and @completed_events are accessed using atomic bit
 * operations, so they don't need any locking.
 *
 * None of the fields touched by the interrupt handler need any
 * locking. However, ordering is important: Before EVENT_DATA_ERROR or
 * EVENT_DATA_COMPLETE is set in @pending_events, all data-related
 * interrupts must be disabled and @data_status updated with a
 * snapshot of SR. Similarly, before EVENT_CMD_COMPLETE is set, the
 * CMDRDY interupt must be disabled and @cmd_status updated with a
 * snapshot of SR, and before EVENT_XFER_COMPLETE can be set, the
 * bytes_xfered field of @data must be written. This is ensured by
 * using barriers.
 */
struct atmel_mci {
	spinlock_t		lock;
	void __iomem		*regs;

	struct scatterlist	*sg;
	unsigned int		pio_offset;

	struct atmel_mci_slot	*cur_slot;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	struct atmel_mci_dma	dma;
	struct dma_chan		*data_chan;

	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;

	struct tasklet_struct	tasklet;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum atmel_mci_state	state;
	struct list_head	queue;

	bool			need_clock_update;
	bool			need_reset;
	u32			mode_reg;
	unsigned long		bus_hz;
	unsigned long		mapbase;
	struct clk		*mck;
	struct platform_device	*pdev;

	struct atmel_mci_slot	*slot[ATMEL_MCI_MAX_NR_SLOTS];
};

/**
 * struct atmel_mci_slot - MMC slot state
 * @mmc: The mmc_host representing this slot.
 * @host: The MMC controller this slot is using.
 * @sdc_reg: Value of SDCR to be written before using this slot.
 * @mrq: mmc_request currently being processed or waiting to be
 *	processed, or NULL when the slot is idle.
 * @queue_node: List node for placing this node in the @queue list of
 *	&struct atmel_mci.
 * @clock: Clock rate configured by set_ios(). Protected by host->lock.
 * @flags: Random state bits associated with the slot.
 * @detect_pin: GPIO pin used for card detection, or negative if not
 *	available.
 * @wp_pin: GPIO pin used for card write protect sending, or negative
 *	if not available.
 * @detect_timer: Timer used for debouncing @detect_pin interrupts.
 */
struct atmel_mci_slot {
	struct mmc_host		*mmc;
	struct atmel_mci	*host;

	u32			sdc_reg;

	struct mmc_request	*mrq;
	struct list_head	queue_node;

	unsigned int		clock;
	unsigned long		flags;
#define ATMCI_CARD_PRESENT	0
#define ATMCI_CARD_NEED_INIT	1
#define ATMCI_SHUTDOWN		2

	int			detect_pin;
	int			wp_pin;

	struct timer_list	detect_timer;
};

#define atmci_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define atmci_set_completed(host, event)			\
	set_bit(event, &host->completed_events)
#define atmci_set_pending(host, event)				\
	set_bit(event, &host->pending_events)

/*
 * The debugfs stuff below is mostly optimized away when
 * CONFIG_DEBUG_FS is not set.
 */
static int atmci_req_show(struct seq_file *s, void *v)
{
	struct atmel_mci_slot	*slot = s->private;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_command	*stop;
	struct mmc_data		*data;

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

static int atmci_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, atmci_req_show, inode->i_private);
}

static const struct file_operations atmci_req_fops = {
	.owner		= THIS_MODULE,
	.open		= atmci_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void atmci_show_status_reg(struct seq_file *s,
		const char *regname, u32 value)
{
	static const char	*sr_bit[] = {
		[0]	= "CMDRDY",
		[1]	= "RXRDY",
		[2]	= "TXRDY",
		[3]	= "BLKE",
		[4]	= "DTIP",
		[5]	= "NOTBUSY",
		[8]	= "SDIOIRQA",
		[9]	= "SDIOIRQB",
		[16]	= "RINDE",
		[17]	= "RDIRE",
		[18]	= "RCRCE",
		[19]	= "RENDE",
		[20]	= "RTOE",
		[21]	= "DCRCE",
		[22]	= "DTOE",
		[30]	= "OVRE",
		[31]	= "UNRE",
	};
	unsigned int		i;

	seq_printf(s, "%s:\t0x%08x", regname, value);
	for (i = 0; i < ARRAY_SIZE(sr_bit); i++) {
		if (value & (1 << i)) {
			if (sr_bit[i])
				seq_printf(s, " %s", sr_bit[i]);
			else
				seq_puts(s, " UNKNOWN");
		}
	}
	seq_putc(s, '\n');
}

static int atmci_regs_show(struct seq_file *s, void *v)
{
	struct atmel_mci	*host = s->private;
	u32			*buf;

	buf = kmalloc(MCI_REGS_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * Grab a more or less consistent snapshot. Note that we're
	 * not disabling interrupts, so IMR and SR may not be
	 * consistent.
	 */
	spin_lock_bh(&host->lock);
	clk_enable(host->mck);
	memcpy_fromio(buf, host->regs, MCI_REGS_SIZE);
	clk_disable(host->mck);
	spin_unlock_bh(&host->lock);

	seq_printf(s, "MR:\t0x%08x%s%s CLKDIV=%u\n",
			buf[MCI_MR / 4],
			buf[MCI_MR / 4] & MCI_MR_RDPROOF ? " RDPROOF" : "",
			buf[MCI_MR / 4] & MCI_MR_WRPROOF ? " WRPROOF" : "",
			buf[MCI_MR / 4] & 0xff);
	seq_printf(s, "DTOR:\t0x%08x\n", buf[MCI_DTOR / 4]);
	seq_printf(s, "SDCR:\t0x%08x\n", buf[MCI_SDCR / 4]);
	seq_printf(s, "ARGR:\t0x%08x\n", buf[MCI_ARGR / 4]);
	seq_printf(s, "BLKR:\t0x%08x BCNT=%u BLKLEN=%u\n",
			buf[MCI_BLKR / 4],
			buf[MCI_BLKR / 4] & 0xffff,
			(buf[MCI_BLKR / 4] >> 16) & 0xffff);

	/* Don't read RSPR and RDR; it will consume the data there */

	atmci_show_status_reg(s, "SR", buf[MCI_SR / 4]);
	atmci_show_status_reg(s, "IMR", buf[MCI_IMR / 4]);

	kfree(buf);

	return 0;
}

static int atmci_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, atmci_regs_show, inode->i_private);
}

static const struct file_operations atmci_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= atmci_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void atmci_init_debugfs(struct atmel_mci_slot *slot)
{
	struct mmc_host		*mmc = slot->mmc;
	struct atmel_mci	*host = slot->host;
	struct dentry		*root;
	struct dentry		*node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
			&atmci_regs_fops);
	if (IS_ERR(node))
		return;
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, slot, &atmci_req_fops);
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

static inline unsigned int ns_to_clocks(struct atmel_mci *host,
					unsigned int ns)
{
	return (ns * (host->bus_hz / 1000000) + 999) / 1000;
}

static void atmci_set_timeout(struct atmel_mci *host,
		struct atmel_mci_slot *slot, struct mmc_data *data)
{
	static unsigned	dtomul_to_shift[] = {
		0, 4, 7, 8, 10, 12, 16, 20
	};
	unsigned	timeout;
	unsigned	dtocyc;
	unsigned	dtomul;

	timeout = ns_to_clocks(host, data->timeout_ns) + data->timeout_clks;

	for (dtomul = 0; dtomul < 8; dtomul++) {
		unsigned shift = dtomul_to_shift[dtomul];
		dtocyc = (timeout + (1 << shift) - 1) >> shift;
		if (dtocyc < 15)
			break;
	}

	if (dtomul >= 8) {
		dtomul = 7;
		dtocyc = 15;
	}

	dev_vdbg(&slot->mmc->class_dev, "setting timeout to %u cycles\n",
			dtocyc << dtomul_to_shift[dtomul]);
	mci_writel(host, DTOR, (MCI_DTOMUL(dtomul) | MCI_DTOCYC(dtocyc)));
}

/*
 * Return mask with command flags to be enabled for this command.
 */
static u32 atmci_prepare_command(struct mmc_host *mmc,
				 struct mmc_command *cmd)
{
	struct mmc_data	*data;
	u32		cmdr;

	cmd->error = -EINPROGRESS;

	cmdr = MCI_CMDR_CMDNB(cmd->opcode);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			cmdr |= MCI_CMDR_RSPTYP_136BIT;
		else
			cmdr |= MCI_CMDR_RSPTYP_48BIT;
	}

	/*
	 * This should really be MAXLAT_5 for CMD2 and ACMD41, but
	 * it's too difficult to determine whether this is an ACMD or
	 * not. Better make it 64.
	 */
	cmdr |= MCI_CMDR_MAXLAT_64CYC;

	if (mmc->ios.bus_mode == MMC_BUSMODE_OPENDRAIN)
		cmdr |= MCI_CMDR_OPDCMD;

	data = cmd->data;
	if (data) {
		cmdr |= MCI_CMDR_START_XFER;
		if (data->flags & MMC_DATA_STREAM)
			cmdr |= MCI_CMDR_STREAM;
		else if (data->blocks > 1)
			cmdr |= MCI_CMDR_MULTI_BLOCK;
		else
			cmdr |= MCI_CMDR_BLOCK;

		if (data->flags & MMC_DATA_READ)
			cmdr |= MCI_CMDR_TRDIR_READ;
	}

	return cmdr;
}

static void atmci_start_command(struct atmel_mci *host,
		struct mmc_command *cmd, u32 cmd_flags)
{
	WARN_ON(host->cmd);
	host->cmd = cmd;

	dev_vdbg(&host->pdev->dev,
			"start command: ARGR=0x%08x CMDR=0x%08x\n",
			cmd->arg, cmd_flags);

	mci_writel(host, ARGR, cmd->arg);
	mci_writel(host, CMDR, cmd_flags);
}

static void send_stop_cmd(struct atmel_mci *host, struct mmc_data *data)
{
	atmci_start_command(host, data->stop, host->stop_cmdr);
	mci_writel(host, IER, MCI_CMDRDY);
}

#ifdef CONFIG_MMC_ATMELMCI_DMA
static void atmci_dma_cleanup(struct atmel_mci *host)
{
	struct mmc_data			*data = host->data;

	dma_unmap_sg(&host->pdev->dev, data->sg, data->sg_len,
		     ((data->flags & MMC_DATA_WRITE)
		      ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
}

static void atmci_stop_dma(struct atmel_mci *host)
{
	struct dma_chan *chan = host->data_chan;

	if (chan) {
		chan->device->device_terminate_all(chan);
		atmci_dma_cleanup(host);
	} else {
		/* Data transfer was stopped by the interrupt handler */
		atmci_set_pending(host, EVENT_XFER_COMPLETE);
		mci_writel(host, IER, MCI_NOTBUSY);
	}
}

/* This function is called by the DMA driver from tasklet context. */
static void atmci_dma_complete(void *arg)
{
	struct atmel_mci	*host = arg;
	struct mmc_data		*data = host->data;

	dev_vdbg(&host->pdev->dev, "DMA complete\n");

	atmci_dma_cleanup(host);

	/*
	 * If the card was removed, data will be NULL. No point trying
	 * to send the stop command or waiting for NBUSY in this case.
	 */
	if (data) {
		atmci_set_pending(host, EVENT_XFER_COMPLETE);
		tasklet_schedule(&host->tasklet);

		/*
		 * Regardless of what the documentation says, we have
		 * to wait for NOTBUSY even after block read
		 * operations.
		 *
		 * When the DMA transfer is complete, the controller
		 * may still be reading the CRC from the card, i.e.
		 * the data transfer is still in progress and we
		 * haven't seen all the potential error bits yet.
		 *
		 * The interrupt handler will schedule a different
		 * tasklet to finish things up when the data transfer
		 * is completely done.
		 *
		 * We may not complete the mmc request here anyway
		 * because the mmc layer may call back and cause us to
		 * violate the "don't submit new operations from the
		 * completion callback" rule of the dma engine
		 * framework.
		 */
		mci_writel(host, IER, MCI_NOTBUSY);
	}
}

static int
atmci_submit_data_dma(struct atmel_mci *host, struct mmc_data *data)
{
	struct dma_chan			*chan;
	struct dma_async_tx_descriptor	*desc;
	struct scatterlist		*sg;
	unsigned int			i;
	enum dma_data_direction		direction;

	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-word-aligned buffers or lengths. Also, we don't bother
	 * with all the DMA setup overhead for short transfers.
	 */
	if (data->blocks * data->blksz < ATMCI_DMA_THRESHOLD)
		return -EINVAL;
	if (data->blksz & 3)
		return -EINVAL;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3)
			return -EINVAL;
	}

	/* If we don't have a channel, we can't do DMA */
	chan = host->dma.chan;
	if (chan)
		host->data_chan = chan;

	if (!chan)
		return -ENODEV;

	if (data->flags & MMC_DATA_READ)
		direction = DMA_FROM_DEVICE;
	else
		direction = DMA_TO_DEVICE;

	desc = chan->device->device_prep_slave_sg(chan,
			data->sg, data->sg_len, direction,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -ENOMEM;

	host->dma.data_desc = desc;
	desc->callback = atmci_dma_complete;
	desc->callback_param = host;
	desc->tx_submit(desc);

	/* Go! */
	chan->device->device_issue_pending(chan);

	return 0;
}

#else /* CONFIG_MMC_ATMELMCI_DMA */

static int atmci_submit_data_dma(struct atmel_mci *host, struct mmc_data *data)
{
	return -ENOSYS;
}

static void atmci_stop_dma(struct atmel_mci *host)
{
	/* Data transfer was stopped by the interrupt handler */
	atmci_set_pending(host, EVENT_XFER_COMPLETE);
	mci_writel(host, IER, MCI_NOTBUSY);
}

#endif /* CONFIG_MMC_ATMELMCI_DMA */

/*
 * Returns a mask of interrupt flags to be enabled after the whole
 * request has been prepared.
 */
static u32 atmci_submit_data(struct atmel_mci *host, struct mmc_data *data)
{
	u32 iflags;

	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	iflags = ATMCI_DATA_ERROR_FLAGS;
	if (atmci_submit_data_dma(host, data)) {
		host->data_chan = NULL;

		/*
		 * Errata: MMC data write operation with less than 12
		 * bytes is impossible.
		 *
		 * Errata: MCI Transmit Data Register (TDR) FIFO
		 * corruption when length is not multiple of 4.
		 */
		if (data->blocks * data->blksz < 12
				|| (data->blocks * data->blksz) & 3)
			host->need_reset = true;

		host->sg = data->sg;
		host->pio_offset = 0;
		if (data->flags & MMC_DATA_READ)
			iflags |= MCI_RXRDY;
		else
			iflags |= MCI_TXRDY;
	}

	return iflags;
}

static void atmci_start_request(struct atmel_mci *host,
		struct atmel_mci_slot *slot)
{
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	u32			iflags;
	u32			cmdflags;

	mrq = slot->mrq;
	host->cur_slot = slot;
	host->mrq = mrq;

	host->pending_events = 0;
	host->completed_events = 0;
	host->data_status = 0;

	if (host->need_reset) {
		mci_writel(host, CR, MCI_CR_SWRST);
		mci_writel(host, CR, MCI_CR_MCIEN);
		mci_writel(host, MR, host->mode_reg);
		host->need_reset = false;
	}
	mci_writel(host, SDCR, slot->sdc_reg);

	iflags = mci_readl(host, IMR);
	if (iflags)
		dev_warn(&slot->mmc->class_dev, "WARNING: IMR=0x%08x\n",
				iflags);

	if (unlikely(test_and_clear_bit(ATMCI_CARD_NEED_INIT, &slot->flags))) {
		/* Send init sequence (74 clock cycles) */
		mci_writel(host, CMDR, MCI_CMDR_SPCMD_INIT);
		while (!(mci_readl(host, SR) & MCI_CMDRDY))
			cpu_relax();
	}
	data = mrq->data;
	if (data) {
		atmci_set_timeout(host, slot, data);

		/* Must set block count/size before sending command */
		mci_writel(host, BLKR, MCI_BCNT(data->blocks)
				| MCI_BLKLEN(data->blksz));
		dev_vdbg(&slot->mmc->class_dev, "BLKR=0x%08x\n",
			MCI_BCNT(data->blocks) | MCI_BLKLEN(data->blksz));
	}

	iflags = MCI_CMDRDY;
	cmd = mrq->cmd;
	cmdflags = atmci_prepare_command(slot->mmc, cmd);
	atmci_start_command(host, cmd, cmdflags);

	if (data)
		iflags |= atmci_submit_data(host, data);

	if (mrq->stop) {
		host->stop_cmdr = atmci_prepare_command(slot->mmc, mrq->stop);
		host->stop_cmdr |= MCI_CMDR_STOP_XFER;
		if (!(data->flags & MMC_DATA_WRITE))
			host->stop_cmdr |= MCI_CMDR_TRDIR_READ;
		if (data->flags & MMC_DATA_STREAM)
			host->stop_cmdr |= MCI_CMDR_STREAM;
		else
			host->stop_cmdr |= MCI_CMDR_MULTI_BLOCK;
	}

	/*
	 * We could have enabled interrupts earlier, but I suspect
	 * that would open up a nice can of interesting race
	 * conditions (e.g. command and data complete, but stop not
	 * prepared yet.)
	 */
	mci_writel(host, IER, iflags);
}

static void atmci_queue_request(struct atmel_mci *host,
		struct atmel_mci_slot *slot, struct mmc_request *mrq)
{
	dev_vdbg(&slot->mmc->class_dev, "queue request: state=%d\n",
			host->state);

	spin_lock_bh(&host->lock);
	slot->mrq = mrq;
	if (host->state == STATE_IDLE) {
		host->state = STATE_SENDING_CMD;
		atmci_start_request(host, slot);
	} else {
		list_add_tail(&slot->queue_node, &host->queue);
	}
	spin_unlock_bh(&host->lock);
}

static void atmci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct atmel_mci_slot	*slot = mmc_priv(mmc);
	struct atmel_mci	*host = slot->host;
	struct mmc_data		*data;

	WARN_ON(slot->mrq);

	/*
	 * We may "know" the card is gone even though there's still an
	 * electrical connection. If so, we really need to communicate
	 * this to the MMC core since there won't be any more
	 * interrupts as the card is completely removed. Otherwise,
	 * the MMC core might believe the card is still there even
	 * though the card was just removed very slowly.
	 */
	if (!test_bit(ATMCI_CARD_PRESENT, &slot->flags)) {
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}

	/* We don't support multiple blocks of weird lengths. */
	data = mrq->data;
	if (data && data->blocks > 1 && data->blksz & 3) {
		mrq->cmd->error = -EINVAL;
		mmc_request_done(mmc, mrq);
	}

	atmci_queue_request(host, slot, mrq);
}

static void atmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct atmel_mci_slot	*slot = mmc_priv(mmc);
	struct atmel_mci	*host = slot->host;
	unsigned int		i;

	slot->sdc_reg &= ~MCI_SDCBUS_MASK;
	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		slot->sdc_reg |= MCI_SDCBUS_1BIT;
		break;
	case MMC_BUS_WIDTH_4:
		slot->sdc_reg = MCI_SDCBUS_4BIT;
		break;
	}

	if (ios->clock) {
		unsigned int clock_min = ~0U;
		u32 clkdiv;

		spin_lock_bh(&host->lock);
		if (!host->mode_reg) {
			clk_enable(host->mck);
			mci_writel(host, CR, MCI_CR_SWRST);
			mci_writel(host, CR, MCI_CR_MCIEN);
		}

		/*
		 * Use mirror of ios->clock to prevent race with mmc
		 * core ios update when finding the minimum.
		 */
		slot->clock = ios->clock;
		for (i = 0; i < ATMEL_MCI_MAX_NR_SLOTS; i++) {
			if (host->slot[i] && host->slot[i]->clock
					&& host->slot[i]->clock < clock_min)
				clock_min = host->slot[i]->clock;
		}

		/* Calculate clock divider */
		clkdiv = DIV_ROUND_UP(host->bus_hz, 2 * clock_min) - 1;
		if (clkdiv > 255) {
			dev_warn(&mmc->class_dev,
				"clock %u too slow; using %lu\n",
				clock_min, host->bus_hz / (2 * 256));
			clkdiv = 255;
		}

		/*
		 * WRPROOF and RDPROOF prevent overruns/underruns by
		 * stopping the clock when the FIFO is full/empty.
		 * This state is not expected to last for long.
		 */
		host->mode_reg = MCI_MR_CLKDIV(clkdiv) | MCI_MR_WRPROOF
					| MCI_MR_RDPROOF;

		if (list_empty(&host->queue))
			mci_writel(host, MR, host->mode_reg);
		else
			host->need_clock_update = true;

		spin_unlock_bh(&host->lock);
	} else {
		bool any_slot_active = false;

		spin_lock_bh(&host->lock);
		slot->clock = 0;
		for (i = 0; i < ATMEL_MCI_MAX_NR_SLOTS; i++) {
			if (host->slot[i] && host->slot[i]->clock) {
				any_slot_active = true;
				break;
			}
		}
		if (!any_slot_active) {
			mci_writel(host, CR, MCI_CR_MCIDIS);
			if (host->mode_reg) {
				mci_readl(host, MR);
				clk_disable(host->mck);
			}
			host->mode_reg = 0;
		}
		spin_unlock_bh(&host->lock);
	}

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		set_bit(ATMCI_CARD_NEED_INIT, &slot->flags);
		break;
	default:
		/*
		 * TODO: None of the currently available AVR32-based
		 * boards allow MMC power to be turned off. Implement
		 * power control when this can be tested properly.
		 *
		 * We also need to hook this into the clock management
		 * somehow so that newly inserted cards aren't
		 * subjected to a fast clock before we have a chance
		 * to figure out what the maximum rate is. Currently,
		 * there's no way to avoid this, and there never will
		 * be for boards that don't support power control.
		 */
		break;
	}
}

static int atmci_get_ro(struct mmc_host *mmc)
{
	int			read_only = -ENOSYS;
	struct atmel_mci_slot	*slot = mmc_priv(mmc);

	if (gpio_is_valid(slot->wp_pin)) {
		read_only = gpio_get_value(slot->wp_pin);
		dev_dbg(&mmc->class_dev, "card is %s\n",
				read_only ? "read-only" : "read-write");
	}

	return read_only;
}

static int atmci_get_cd(struct mmc_host *mmc)
{
	int			present = -ENOSYS;
	struct atmel_mci_slot	*slot = mmc_priv(mmc);

	if (gpio_is_valid(slot->detect_pin)) {
		present = !gpio_get_value(slot->detect_pin);
		dev_dbg(&mmc->class_dev, "card is %spresent\n",
				present ? "" : "not ");
	}

	return present;
}

static const struct mmc_host_ops atmci_ops = {
	.request	= atmci_request,
	.set_ios	= atmci_set_ios,
	.get_ro		= atmci_get_ro,
	.get_cd		= atmci_get_cd,
};

/* Called with host->lock held */
static void atmci_request_end(struct atmel_mci *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	struct atmel_mci_slot	*slot = NULL;
	struct mmc_host		*prev_mmc = host->cur_slot->mmc;

	WARN_ON(host->cmd || host->data);

	/*
	 * Update the MMC clock rate if necessary. This may be
	 * necessary if set_ios() is called when a different slot is
	 * busy transfering data.
	 */
	if (host->need_clock_update)
		mci_writel(host, MR, host->mode_reg);

	host->cur_slot->mrq = NULL;
	host->mrq = NULL;
	if (!list_empty(&host->queue)) {
		slot = list_entry(host->queue.next,
				struct atmel_mci_slot, queue_node);
		list_del(&slot->queue_node);
		dev_vdbg(&host->pdev->dev, "list not empty: %s is next\n",
				mmc_hostname(slot->mmc));
		host->state = STATE_SENDING_CMD;
		atmci_start_request(host, slot);
	} else {
		dev_vdbg(&host->pdev->dev, "list empty\n");
		host->state = STATE_IDLE;
	}

	spin_unlock(&host->lock);
	mmc_request_done(prev_mmc, mrq);
	spin_lock(&host->lock);
}

static void atmci_command_complete(struct atmel_mci *host,
			struct mmc_command *cmd)
{
	u32		status = host->cmd_status;

	/* Read the response from the card (up to 16 bytes) */
	cmd->resp[0] = mci_readl(host, RSPR);
	cmd->resp[1] = mci_readl(host, RSPR);
	cmd->resp[2] = mci_readl(host, RSPR);
	cmd->resp[3] = mci_readl(host, RSPR);

	if (status & MCI_RTOE)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (status & MCI_RCRCE))
		cmd->error = -EILSEQ;
	else if (status & (MCI_RINDE | MCI_RDIRE | MCI_RENDE))
		cmd->error = -EIO;
	else
		cmd->error = 0;

	if (cmd->error) {
		dev_dbg(&host->pdev->dev,
			"command error: status=0x%08x\n", status);

		if (cmd->data) {
			host->data = NULL;
			atmci_stop_dma(host);
			mci_writel(host, IDR, MCI_NOTBUSY
					| MCI_TXRDY | MCI_RXRDY
					| ATMCI_DATA_ERROR_FLAGS);
		}
	}
}

static void atmci_detect_change(unsigned long data)
{
	struct atmel_mci_slot	*slot = (struct atmel_mci_slot *)data;
	bool			present;
	bool			present_old;

	/*
	 * atmci_cleanup_slot() sets the ATMCI_SHUTDOWN flag before
	 * freeing the interrupt. We must not re-enable the interrupt
	 * if it has been freed, and if we're shutting down, it
	 * doesn't really matter whether the card is present or not.
	 */
	smp_rmb();
	if (test_bit(ATMCI_SHUTDOWN, &slot->flags))
		return;

	enable_irq(gpio_to_irq(slot->detect_pin));
	present = !gpio_get_value(slot->detect_pin);
	present_old = test_bit(ATMCI_CARD_PRESENT, &slot->flags);

	dev_vdbg(&slot->mmc->class_dev, "detect change: %d (was %d)\n",
			present, present_old);

	if (present != present_old) {
		struct atmel_mci	*host = slot->host;
		struct mmc_request	*mrq;

		dev_dbg(&slot->mmc->class_dev, "card %s\n",
			present ? "inserted" : "removed");

		spin_lock(&host->lock);

		if (!present)
			clear_bit(ATMCI_CARD_PRESENT, &slot->flags);
		else
			set_bit(ATMCI_CARD_PRESENT, &slot->flags);

		/* Clean up queue if present */
		mrq = slot->mrq;
		if (mrq) {
			if (mrq == host->mrq) {
				/*
				 * Reset controller to terminate any ongoing
				 * commands or data transfers.
				 */
				mci_writel(host, CR, MCI_CR_SWRST);
				mci_writel(host, CR, MCI_CR_MCIEN);
				mci_writel(host, MR, host->mode_reg);

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
					atmci_stop_dma(host);
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

				atmci_request_end(host, mrq);
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
		spin_unlock(&host->lock);

		mmc_detect_change(slot->mmc, 0);
	}
}

static void atmci_tasklet_func(unsigned long priv)
{
	struct atmel_mci	*host = (struct atmel_mci *)priv;
	struct mmc_request	*mrq = host->mrq;
	struct mmc_data		*data = host->data;
	struct mmc_command	*cmd = host->cmd;
	enum atmel_mci_state	state = host->state;
	enum atmel_mci_state	prev_state;
	u32			status;

	spin_lock(&host->lock);

	state = host->state;

	dev_vdbg(&host->pdev->dev,
		"tasklet: state %u pending/completed/mask %lx/%lx/%x\n",
		state, host->pending_events, host->completed_events,
		mci_readl(host, IMR));

	do {
		prev_state = state;

		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:
			if (!atmci_test_and_clear_pending(host,
						EVENT_CMD_COMPLETE))
				break;

			host->cmd = NULL;
			atmci_set_completed(host, EVENT_CMD_COMPLETE);
			atmci_command_complete(host, mrq->cmd);
			if (!mrq->data || cmd->error) {
				atmci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_DATA;
			/* fall through */

		case STATE_SENDING_DATA:
			if (atmci_test_and_clear_pending(host,
						EVENT_DATA_ERROR)) {
				atmci_stop_dma(host);
				if (data->stop)
					send_stop_cmd(host, data);
				state = STATE_DATA_ERROR;
				break;
			}

			if (!atmci_test_and_clear_pending(host,
						EVENT_XFER_COMPLETE))
				break;

			atmci_set_completed(host, EVENT_XFER_COMPLETE);
			prev_state = state = STATE_DATA_BUSY;
			/* fall through */

		case STATE_DATA_BUSY:
			if (!atmci_test_and_clear_pending(host,
						EVENT_DATA_COMPLETE))
				break;

			host->data = NULL;
			atmci_set_completed(host, EVENT_DATA_COMPLETE);
			status = host->data_status;
			if (unlikely(status & ATMCI_DATA_ERROR_FLAGS)) {
				if (status & MCI_DTOE) {
					dev_dbg(&host->pdev->dev,
							"data timeout error\n");
					data->error = -ETIMEDOUT;
				} else if (status & MCI_DCRCE) {
					dev_dbg(&host->pdev->dev,
							"data CRC error\n");
					data->error = -EILSEQ;
				} else {
					dev_dbg(&host->pdev->dev,
						"data FIFO error (status=%08x)\n",
						status);
					data->error = -EIO;
				}
			} else {
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
			}

			if (!data->stop) {
				atmci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (!data->error)
				send_stop_cmd(host, data);
			/* fall through */

		case STATE_SENDING_STOP:
			if (!atmci_test_and_clear_pending(host,
						EVENT_CMD_COMPLETE))
				break;

			host->cmd = NULL;
			atmci_command_complete(host, mrq->stop);
			atmci_request_end(host, host->mrq);
			goto unlock;

		case STATE_DATA_ERROR:
			if (!atmci_test_and_clear_pending(host,
						EVENT_XFER_COMPLETE))
				break;

			state = STATE_DATA_BUSY;
			break;
		}
	} while (state != prev_state);

	host->state = state;

unlock:
	spin_unlock(&host->lock);
}

static void atmci_read_data_pio(struct atmel_mci *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			value;
	u32			status;
	unsigned int		nbytes = 0;

	do {
		value = mci_readl(host, RDR);
		if (likely(offset + 4 <= sg->length)) {
			put_unaligned(value, (u32 *)(buf + offset));

			offset += 4;
			nbytes += 4;

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
			memcpy(buf + offset, &value, remaining);
			nbytes += remaining;

			flush_dcache_page(sg_page(sg));
			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = 4 - remaining;
			buf = sg_virt(sg);
			memcpy(buf, (u8 *)&value + remaining, offset);
			nbytes += offset;
		}

		status = mci_readl(host, SR);
		if (status & ATMCI_DATA_ERROR_FLAGS) {
			mci_writel(host, IDR, (MCI_NOTBUSY | MCI_RXRDY
						| ATMCI_DATA_ERROR_FLAGS));
			host->data_status = status;
			data->bytes_xfered += nbytes;
			smp_wmb();
			atmci_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & MCI_RXRDY);

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	mci_writel(host, IDR, MCI_RXRDY);
	mci_writel(host, IER, MCI_NOTBUSY);
	data->bytes_xfered += nbytes;
	smp_wmb();
	atmci_set_pending(host, EVENT_XFER_COMPLETE);
}

static void atmci_write_data_pio(struct atmel_mci *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			value;
	u32			status;
	unsigned int		nbytes = 0;

	do {
		if (likely(offset + 4 <= sg->length)) {
			value = get_unaligned((u32 *)(buf + offset));
			mci_writel(host, TDR, value);

			offset += 4;
			nbytes += 4;
			if (offset == sg->length) {
				host->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
				buf = sg_virt(sg);
			}
		} else {
			unsigned int remaining = sg->length - offset;

			value = 0;
			memcpy(&value, buf + offset, remaining);
			nbytes += remaining;

			host->sg = sg = sg_next(sg);
			if (!sg) {
				mci_writel(host, TDR, value);
				goto done;
			}

			offset = 4 - remaining;
			buf = sg_virt(sg);
			memcpy((u8 *)&value + remaining, buf, offset);
			mci_writel(host, TDR, value);
			nbytes += offset;
		}

		status = mci_readl(host, SR);
		if (status & ATMCI_DATA_ERROR_FLAGS) {
			mci_writel(host, IDR, (MCI_NOTBUSY | MCI_TXRDY
						| ATMCI_DATA_ERROR_FLAGS));
			host->data_status = status;
			data->bytes_xfered += nbytes;
			smp_wmb();
			atmci_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & MCI_TXRDY);

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	mci_writel(host, IDR, MCI_TXRDY);
	mci_writel(host, IER, MCI_NOTBUSY);
	data->bytes_xfered += nbytes;
	smp_wmb();
	atmci_set_pending(host, EVENT_XFER_COMPLETE);
}

static void atmci_cmd_interrupt(struct atmel_mci *host, u32 status)
{
	mci_writel(host, IDR, MCI_CMDRDY);

	host->cmd_status = status;
	smp_wmb();
	atmci_set_pending(host, EVENT_CMD_COMPLETE);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t atmci_interrupt(int irq, void *dev_id)
{
	struct atmel_mci	*host = dev_id;
	u32			status, mask, pending;
	unsigned int		pass_count = 0;

	do {
		status = mci_readl(host, SR);
		mask = mci_readl(host, IMR);
		pending = status & mask;
		if (!pending)
			break;

		if (pending & ATMCI_DATA_ERROR_FLAGS) {
			mci_writel(host, IDR, ATMCI_DATA_ERROR_FLAGS
					| MCI_RXRDY | MCI_TXRDY);
			pending &= mci_readl(host, IMR);

			host->data_status = status;
			smp_wmb();
			atmci_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
		}
		if (pending & MCI_NOTBUSY) {
			mci_writel(host, IDR,
					ATMCI_DATA_ERROR_FLAGS | MCI_NOTBUSY);
			if (!host->data_status)
				host->data_status = status;
			smp_wmb();
			atmci_set_pending(host, EVENT_DATA_COMPLETE);
			tasklet_schedule(&host->tasklet);
		}
		if (pending & MCI_RXRDY)
			atmci_read_data_pio(host);
		if (pending & MCI_TXRDY)
			atmci_write_data_pio(host);

		if (pending & MCI_CMDRDY)
			atmci_cmd_interrupt(host, status);
	} while (pass_count++ < 5);

	return pass_count ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t atmci_detect_interrupt(int irq, void *dev_id)
{
	struct atmel_mci_slot	*slot = dev_id;

	/*
	 * Disable interrupts until the pin has stabilized and check
	 * the state then. Use mod_timer() since we may be in the
	 * middle of the timer routine when this interrupt triggers.
	 */
	disable_irq_nosync(irq);
	mod_timer(&slot->detect_timer, jiffies + msecs_to_jiffies(20));

	return IRQ_HANDLED;
}

static int __init atmci_init_slot(struct atmel_mci *host,
		struct mci_slot_pdata *slot_data, unsigned int id,
		u32 sdc_reg)
{
	struct mmc_host			*mmc;
	struct atmel_mci_slot		*slot;

	mmc = mmc_alloc_host(sizeof(struct atmel_mci_slot), &host->pdev->dev);
	if (!mmc)
		return -ENOMEM;

	slot = mmc_priv(mmc);
	slot->mmc = mmc;
	slot->host = host;
	slot->detect_pin = slot_data->detect_pin;
	slot->wp_pin = slot_data->wp_pin;
	slot->sdc_reg = sdc_reg;

	mmc->ops = &atmci_ops;
	mmc->f_min = DIV_ROUND_UP(host->bus_hz, 512);
	mmc->f_max = host->bus_hz / 2;
	mmc->ocr_avail	= MMC_VDD_32_33 | MMC_VDD_33_34;
	if (slot_data->bus_width >= 4)
		mmc->caps |= MMC_CAP_4_BIT_DATA;

	mmc->max_hw_segs = 64;
	mmc->max_phys_segs = 64;
	mmc->max_req_size = 32768 * 512;
	mmc->max_blk_size = 32768;
	mmc->max_blk_count = 512;

	/* Assume card is present initially */
	set_bit(ATMCI_CARD_PRESENT, &slot->flags);
	if (gpio_is_valid(slot->detect_pin)) {
		if (gpio_request(slot->detect_pin, "mmc_detect")) {
			dev_dbg(&mmc->class_dev, "no detect pin available\n");
			slot->detect_pin = -EBUSY;
		} else if (gpio_get_value(slot->detect_pin)) {
			clear_bit(ATMCI_CARD_PRESENT, &slot->flags);
		}
	}

	if (!gpio_is_valid(slot->detect_pin))
		mmc->caps |= MMC_CAP_NEEDS_POLL;

	if (gpio_is_valid(slot->wp_pin)) {
		if (gpio_request(slot->wp_pin, "mmc_wp")) {
			dev_dbg(&mmc->class_dev, "no WP pin available\n");
			slot->wp_pin = -EBUSY;
		}
	}

	host->slot[id] = slot;
	mmc_add_host(mmc);

	if (gpio_is_valid(slot->detect_pin)) {
		int ret;

		setup_timer(&slot->detect_timer, atmci_detect_change,
				(unsigned long)slot);

		ret = request_irq(gpio_to_irq(slot->detect_pin),
				atmci_detect_interrupt,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"mmc-detect", slot);
		if (ret) {
			dev_dbg(&mmc->class_dev,
				"could not request IRQ %d for detect pin\n",
				gpio_to_irq(slot->detect_pin));
			gpio_free(slot->detect_pin);
			slot->detect_pin = -EBUSY;
		}
	}

	atmci_init_debugfs(slot);

	return 0;
}

static void __exit atmci_cleanup_slot(struct atmel_mci_slot *slot,
		unsigned int id)
{
	/* Debugfs stuff is cleaned up by mmc core */

	set_bit(ATMCI_SHUTDOWN, &slot->flags);
	smp_wmb();

	mmc_remove_host(slot->mmc);

	if (gpio_is_valid(slot->detect_pin)) {
		int pin = slot->detect_pin;

		free_irq(gpio_to_irq(pin), slot);
		del_timer_sync(&slot->detect_timer);
		gpio_free(pin);
	}
	if (gpio_is_valid(slot->wp_pin))
		gpio_free(slot->wp_pin);

	slot->host->slot[id] = NULL;
	mmc_free_host(slot->mmc);
}

#ifdef CONFIG_MMC_ATMELMCI_DMA
static bool filter(struct dma_chan *chan, void *slave)
{
	struct dw_dma_slave *dws = slave;

	if (dws->dma_dev == chan->device->dev) {
		chan->private = dws;
		return true;
	} else
		return false;
}
#endif

static int __init atmci_probe(struct platform_device *pdev)
{
	struct mci_platform_data	*pdata;
	struct atmel_mci		*host;
	struct resource			*regs;
	unsigned int			nr_slots;
	int				irq;
	int				ret;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;
	pdata = pdev->dev.platform_data;
	if (!pdata)
		return -ENXIO;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	host = kzalloc(sizeof(struct atmel_mci), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->pdev = pdev;
	spin_lock_init(&host->lock);
	INIT_LIST_HEAD(&host->queue);

	host->mck = clk_get(&pdev->dev, "mci_clk");
	if (IS_ERR(host->mck)) {
		ret = PTR_ERR(host->mck);
		goto err_clk_get;
	}

	ret = -ENOMEM;
	host->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!host->regs)
		goto err_ioremap;

	clk_enable(host->mck);
	mci_writel(host, CR, MCI_CR_SWRST);
	host->bus_hz = clk_get_rate(host->mck);
	clk_disable(host->mck);

	host->mapbase = regs->start;

	tasklet_init(&host->tasklet, atmci_tasklet_func, (unsigned long)host);

	ret = request_irq(irq, atmci_interrupt, 0, pdev->dev.bus_id, host);
	if (ret)
		goto err_request_irq;

#ifdef CONFIG_MMC_ATMELMCI_DMA
	if (pdata->dma_slave.dma_dev) {
		struct dw_dma_slave *dws = &pdata->dma_slave;
		dma_cap_mask_t mask;

		dws->tx_reg = regs->start + MCI_TDR;
		dws->rx_reg = regs->start + MCI_RDR;

		/* Try to grab a DMA channel */
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);
		host->dma.chan = dma_request_channel(mask, filter, dws);
	}
	if (!host->dma.chan)
		dev_notice(&pdev->dev, "DMA not available, using PIO\n");
#endif /* CONFIG_MMC_ATMELMCI_DMA */

	platform_set_drvdata(pdev, host);

	/* We need at least one slot to succeed */
	nr_slots = 0;
	ret = -ENODEV;
	if (pdata->slot[0].bus_width) {
		ret = atmci_init_slot(host, &pdata->slot[0],
				MCI_SDCSEL_SLOT_A, 0);
		if (!ret)
			nr_slots++;
	}
	if (pdata->slot[1].bus_width) {
		ret = atmci_init_slot(host, &pdata->slot[1],
				MCI_SDCSEL_SLOT_B, 1);
		if (!ret)
			nr_slots++;
	}

	if (!nr_slots)
		goto err_init_slot;

	dev_info(&pdev->dev,
			"Atmel MCI controller at 0x%08lx irq %d, %u slots\n",
			host->mapbase, irq, nr_slots);

	return 0;

err_init_slot:
#ifdef CONFIG_MMC_ATMELMCI_DMA
	if (host->dma.chan)
		dma_release_channel(host->dma.chan);
#endif
	free_irq(irq, host);
err_request_irq:
	iounmap(host->regs);
err_ioremap:
	clk_put(host->mck);
err_clk_get:
	kfree(host);
	return ret;
}

static int __exit atmci_remove(struct platform_device *pdev)
{
	struct atmel_mci	*host = platform_get_drvdata(pdev);
	unsigned int		i;

	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < ATMEL_MCI_MAX_NR_SLOTS; i++) {
		if (host->slot[i])
			atmci_cleanup_slot(host->slot[i], i);
	}

	clk_enable(host->mck);
	mci_writel(host, IDR, ~0UL);
	mci_writel(host, CR, MCI_CR_MCIDIS);
	mci_readl(host, SR);
	clk_disable(host->mck);

#ifdef CONFIG_MMC_ATMELMCI_DMA
	if (host->dma.chan)
		dma_release_channel(host->dma.chan);
#endif

	free_irq(platform_get_irq(pdev, 0), host);
	iounmap(host->regs);

	clk_put(host->mck);
	kfree(host);

	return 0;
}

static struct platform_driver atmci_driver = {
	.remove		= __exit_p(atmci_remove),
	.driver		= {
		.name		= "atmel_mci",
	},
};

static int __init atmci_init(void)
{
	return platform_driver_probe(&atmci_driver, atmci_probe);
}

static void __exit atmci_exit(void)
{
	platform_driver_unregister(&atmci_driver);
}

late_initcall(atmci_init); /* try to load after dma driver when built-in */
module_exit(atmci_exit);

MODULE_DESCRIPTION("Atmel Multimedia Card Interface driver");
MODULE_AUTHOR("Haavard Skinnemoen <haavard.skinnemoen@atmel.com>");
MODULE_LICENSE("GPL v2");
