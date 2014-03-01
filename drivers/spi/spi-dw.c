/*
 * Designware SPI core controller driver (refer pxa2xx_spi.c)
 *
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "spi-dw.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#define START_STATE	((void *)0)
#define RUNNING_STATE	((void *)1)
#define DONE_STATE	((void *)2)
#define ERROR_STATE	((void *)-1)

#define QUEUE_RUNNING	0
#define QUEUE_STOPPED	1

#define MRST_SPI_DEASSERT	0
#define MRST_SPI_ASSERT		1

/* Slave spi_dev related */
struct chip_data {
	u16 cr0;
	u8 cs;			/* chip select pin */
	u8 n_bytes;		/* current is a 1/2/4 byte op */
	u8 tmode;		/* TR/TO/RO/EEPROM */
	u8 type;		/* SPI/SSP/MicroWire */

	u8 poll_mode;		/* 1 means use poll mode */

	u32 dma_width;
	u32 rx_threshold;
	u32 tx_threshold;
	u8 enable_dma;
	u8 bits_per_word;
	u16 clk_div;		/* baud rate divider */
	u32 speed_hz;		/* baud rate */
	void (*cs_control)(u32 command);
};

#ifdef CONFIG_DEBUG_FS
#define SPI_REGS_BUFSIZE	1024
static ssize_t  spi_show_regs(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct dw_spi *dws;
	char *buf;
	u32 len = 0;
	ssize_t ret;

	dws = file->private_data;

	buf = kzalloc(SPI_REGS_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return 0;

	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"MRST SPI0 registers:\n");
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"=================================\n");
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"CTRL0: \t\t0x%08x\n", dw_readl(dws, DW_SPI_CTRL0));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"CTRL1: \t\t0x%08x\n", dw_readl(dws, DW_SPI_CTRL1));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"SSIENR: \t0x%08x\n", dw_readl(dws, DW_SPI_SSIENR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"SER: \t\t0x%08x\n", dw_readl(dws, DW_SPI_SER));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"BAUDR: \t\t0x%08x\n", dw_readl(dws, DW_SPI_BAUDR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"TXFTLR: \t0x%08x\n", dw_readl(dws, DW_SPI_TXFLTR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"RXFTLR: \t0x%08x\n", dw_readl(dws, DW_SPI_RXFLTR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"TXFLR: \t\t0x%08x\n", dw_readl(dws, DW_SPI_TXFLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"RXFLR: \t\t0x%08x\n", dw_readl(dws, DW_SPI_RXFLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"SR: \t\t0x%08x\n", dw_readl(dws, DW_SPI_SR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"IMR: \t\t0x%08x\n", dw_readl(dws, DW_SPI_IMR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"ISR: \t\t0x%08x\n", dw_readl(dws, DW_SPI_ISR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"DMACR: \t\t0x%08x\n", dw_readl(dws, DW_SPI_DMACR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"DMATDLR: \t0x%08x\n", dw_readl(dws, DW_SPI_DMATDLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"DMARDLR: \t0x%08x\n", dw_readl(dws, DW_SPI_DMARDLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"=================================\n");

	ret =  simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

static const struct file_operations mrst_spi_regs_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= spi_show_regs,
	.llseek		= default_llseek,
};

static int mrst_spi_debugfs_init(struct dw_spi *dws)
{
	dws->debugfs = debugfs_create_dir("mrst_spi", NULL);
	if (!dws->debugfs)
		return -ENOMEM;

	debugfs_create_file("registers", S_IFREG | S_IRUGO,
		dws->debugfs, (void *)dws, &mrst_spi_regs_ops);
	return 0;
}

static void mrst_spi_debugfs_remove(struct dw_spi *dws)
{
	if (dws->debugfs)
		debugfs_remove_recursive(dws->debugfs);
}

#else
static inline int mrst_spi_debugfs_init(struct dw_spi *dws)
{
	return 0;
}

static inline void mrst_spi_debugfs_remove(struct dw_spi *dws)
{
}
#endif /* CONFIG_DEBUG_FS */

/* Return the max entries we can fill into tx fifo */
static inline u32 tx_max(struct dw_spi *dws)
{
	u32 tx_left, tx_room, rxtx_gap;

	tx_left = (dws->tx_end - dws->tx) / dws->n_bytes;
	tx_room = dws->fifo_len - dw_readw(dws, DW_SPI_TXFLR);

	/*
	 * Another concern is about the tx/rx mismatch, we
	 * though to use (dws->fifo_len - rxflr - txflr) as
	 * one maximum value for tx, but it doesn't cover the
	 * data which is out of tx/rx fifo and inside the
	 * shift registers. So a control from sw point of
	 * view is taken.
	 */
	rxtx_gap =  ((dws->rx_end - dws->rx) - (dws->tx_end - dws->tx))
			/ dws->n_bytes;

	return min3(tx_left, tx_room, (u32) (dws->fifo_len - rxtx_gap));
}

/* Return the max entries we should read out of rx fifo */
static inline u32 rx_max(struct dw_spi *dws)
{
	u32 rx_left = (dws->rx_end - dws->rx) / dws->n_bytes;

	return min(rx_left, (u32)dw_readw(dws, DW_SPI_RXFLR));
}

static void dw_writer(struct dw_spi *dws)
{
	u32 max = tx_max(dws);
	u16 txw = 0;

	while (max--) {
		/* Set the tx word if the transfer's original "tx" is not null */
		if (dws->tx_end - dws->len) {
			if (dws->n_bytes == 1)
				txw = *(u8 *)(dws->tx);
			else
				txw = *(u16 *)(dws->tx);
		}
		dw_writew(dws, DW_SPI_DR, txw);
		dws->tx += dws->n_bytes;
	}
}

static void dw_reader(struct dw_spi *dws)
{
	u32 max = rx_max(dws);
	u16 rxw;

	while (max--) {
		rxw = dw_readw(dws, DW_SPI_DR);
		/* Care rx only if the transfer's original "rx" is not null */
		if (dws->rx_end - dws->len) {
			if (dws->n_bytes == 1)
				*(u8 *)(dws->rx) = rxw;
			else
				*(u16 *)(dws->rx) = rxw;
		}
		dws->rx += dws->n_bytes;
	}
}

static void *next_transfer(struct dw_spi *dws)
{
	struct spi_message *msg = dws->cur_msg;
	struct spi_transfer *trans = dws->cur_transfer;

	/* Move to next transfer */
	if (trans->transfer_list.next != &msg->transfers) {
		dws->cur_transfer =
			list_entry(trans->transfer_list.next,
					struct spi_transfer,
					transfer_list);
		return RUNNING_STATE;
	} else
		return DONE_STATE;
}

/*
 * Note: first step is the protocol driver prepares
 * a dma-capable memory, and this func just need translate
 * the virt addr to physical
 */
static int map_dma_buffers(struct dw_spi *dws)
{
	if (!dws->cur_msg->is_dma_mapped
		|| !dws->dma_inited
		|| !dws->cur_chip->enable_dma
		|| !dws->dma_ops)
		return 0;

	if (dws->cur_transfer->tx_dma)
		dws->tx_dma = dws->cur_transfer->tx_dma;

	if (dws->cur_transfer->rx_dma)
		dws->rx_dma = dws->cur_transfer->rx_dma;

	return 1;
}

/* Caller already set message->status; dma and pio irqs are blocked */
static void giveback(struct dw_spi *dws)
{
	struct spi_transfer *last_transfer;
	unsigned long flags;
	struct spi_message *msg;

	spin_lock_irqsave(&dws->lock, flags);
	msg = dws->cur_msg;
	dws->cur_msg = NULL;
	dws->cur_transfer = NULL;
	dws->prev_chip = dws->cur_chip;
	dws->cur_chip = NULL;
	dws->dma_mapped = 0;
	queue_work(dws->workqueue, &dws->pump_messages);
	spin_unlock_irqrestore(&dws->lock, flags);

	last_transfer = list_entry(msg->transfers.prev,
					struct spi_transfer,
					transfer_list);

	if (!last_transfer->cs_change && dws->cs_control)
		dws->cs_control(MRST_SPI_DEASSERT);

	msg->state = NULL;
	if (msg->complete)
		msg->complete(msg->context);
}

static void int_error_stop(struct dw_spi *dws, const char *msg)
{
	/* Stop the hw */
	spi_enable_chip(dws, 0);

	dev_err(&dws->master->dev, "%s\n", msg);
	dws->cur_msg->state = ERROR_STATE;
	tasklet_schedule(&dws->pump_transfers);
}

void dw_spi_xfer_done(struct dw_spi *dws)
{
	/* Update total byte transferred return count actual bytes read */
	dws->cur_msg->actual_length += dws->len;

	/* Move to next transfer */
	dws->cur_msg->state = next_transfer(dws);

	/* Handle end of message */
	if (dws->cur_msg->state == DONE_STATE) {
		dws->cur_msg->status = 0;
		giveback(dws);
	} else
		tasklet_schedule(&dws->pump_transfers);
}
EXPORT_SYMBOL_GPL(dw_spi_xfer_done);

static irqreturn_t interrupt_transfer(struct dw_spi *dws)
{
	u16 irq_status = dw_readw(dws, DW_SPI_ISR);

	/* Error handling */
	if (irq_status & (SPI_INT_TXOI | SPI_INT_RXOI | SPI_INT_RXUI)) {
		dw_readw(dws, DW_SPI_TXOICR);
		dw_readw(dws, DW_SPI_RXOICR);
		dw_readw(dws, DW_SPI_RXUICR);
		int_error_stop(dws, "interrupt_transfer: fifo overrun/underrun");
		return IRQ_HANDLED;
	}

	dw_reader(dws);
	if (dws->rx_end == dws->rx) {
		spi_mask_intr(dws, SPI_INT_TXEI);
		dw_spi_xfer_done(dws);
		return IRQ_HANDLED;
	}
	if (irq_status & SPI_INT_TXEI) {
		spi_mask_intr(dws, SPI_INT_TXEI);
		dw_writer(dws);
		/* Enable TX irq always, it will be disabled when RX finished */
		spi_umask_intr(dws, SPI_INT_TXEI);
	}

	return IRQ_HANDLED;
}

static irqreturn_t dw_spi_irq(int irq, void *dev_id)
{
	struct dw_spi *dws = dev_id;
	u16 irq_status = dw_readw(dws, DW_SPI_ISR) & 0x3f;

	if (!irq_status)
		return IRQ_NONE;

	if (!dws->cur_msg) {
		spi_mask_intr(dws, SPI_INT_TXEI);
		return IRQ_HANDLED;
	}

	return dws->transfer_handler(dws);
}

/* Must be called inside pump_transfers() */
static void poll_transfer(struct dw_spi *dws)
{
	do {
		dw_writer(dws);
		dw_reader(dws);
		cpu_relax();
	} while (dws->rx_end > dws->rx);

	dw_spi_xfer_done(dws);
}

static void pump_transfers(unsigned long data)
{
	struct dw_spi *dws = (struct dw_spi *)data;
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct spi_device *spi = NULL;
	struct chip_data *chip = NULL;
	u8 bits = 0;
	u8 imask = 0;
	u8 cs_change = 0;
	u16 txint_level = 0;
	u16 clk_div = 0;
	u32 speed = 0;
	u32 cr0 = 0;

	/* Get current state information */
	message = dws->cur_msg;
	transfer = dws->cur_transfer;
	chip = dws->cur_chip;
	spi = message->spi;

	if (unlikely(!chip->clk_div))
		chip->clk_div = dws->max_freq / chip->speed_hz;

	if (message->state == ERROR_STATE) {
		message->status = -EIO;
		goto early_exit;
	}

	/* Handle end of message */
	if (message->state == DONE_STATE) {
		message->status = 0;
		goto early_exit;
	}

	/* Delay if requested at end of transfer*/
	if (message->state == RUNNING_STATE) {
		previous = list_entry(transfer->transfer_list.prev,
					struct spi_transfer,
					transfer_list);
		if (previous->delay_usecs)
			udelay(previous->delay_usecs);
	}

	dws->n_bytes = chip->n_bytes;
	dws->dma_width = chip->dma_width;
	dws->cs_control = chip->cs_control;

	dws->rx_dma = transfer->rx_dma;
	dws->tx_dma = transfer->tx_dma;
	dws->tx = (void *)transfer->tx_buf;
	dws->tx_end = dws->tx + transfer->len;
	dws->rx = transfer->rx_buf;
	dws->rx_end = dws->rx + transfer->len;
	dws->len = dws->cur_transfer->len;
	if (chip != dws->prev_chip)
		cs_change = 1;

	cr0 = chip->cr0;

	/* Handle per transfer options for bpw and speed */
	if (transfer->speed_hz) {
		speed = chip->speed_hz;

		if (transfer->speed_hz != speed) {
			speed = transfer->speed_hz;
			if (speed > dws->max_freq) {
				printk(KERN_ERR "MRST SPI0: unsupported"
					"freq: %dHz\n", speed);
				message->status = -EIO;
				goto early_exit;
			}

			/* clk_div doesn't support odd number */
			clk_div = dws->max_freq / speed;
			clk_div = (clk_div + 1) & 0xfffe;

			chip->speed_hz = speed;
			chip->clk_div = clk_div;
		}
	}
	if (transfer->bits_per_word) {
		bits = transfer->bits_per_word;
		dws->n_bytes = dws->dma_width = bits >> 3;
		cr0 = (bits - 1)
			| (chip->type << SPI_FRF_OFFSET)
			| (spi->mode << SPI_MODE_OFFSET)
			| (chip->tmode << SPI_TMOD_OFFSET);
	}
	message->state = RUNNING_STATE;

	/*
	 * Adjust transfer mode if necessary. Requires platform dependent
	 * chipselect mechanism.
	 */
	if (dws->cs_control) {
		if (dws->rx && dws->tx)
			chip->tmode = SPI_TMOD_TR;
		else if (dws->rx)
			chip->tmode = SPI_TMOD_RO;
		else
			chip->tmode = SPI_TMOD_TO;

		cr0 &= ~SPI_TMOD_MASK;
		cr0 |= (chip->tmode << SPI_TMOD_OFFSET);
	}

	/* Check if current transfer is a DMA transaction */
	dws->dma_mapped = map_dma_buffers(dws);

	/*
	 * Interrupt mode
	 * we only need set the TXEI IRQ, as TX/RX always happen syncronizely
	 */
	if (!dws->dma_mapped && !chip->poll_mode) {
		int templen = dws->len / dws->n_bytes;
		txint_level = dws->fifo_len / 2;
		txint_level = (templen > txint_level) ? txint_level : templen;

		imask |= SPI_INT_TXEI | SPI_INT_TXOI | SPI_INT_RXUI | SPI_INT_RXOI;
		dws->transfer_handler = interrupt_transfer;
	}

	/*
	 * Reprogram registers only if
	 *	1. chip select changes
	 *	2. clk_div is changed
	 *	3. control value changes
	 */
	if (dw_readw(dws, DW_SPI_CTRL0) != cr0 || cs_change || clk_div || imask) {
		spi_enable_chip(dws, 0);

		if (dw_readw(dws, DW_SPI_CTRL0) != cr0)
			dw_writew(dws, DW_SPI_CTRL0, cr0);

		spi_set_clk(dws, clk_div ? clk_div : chip->clk_div);
		spi_chip_sel(dws, spi->chip_select);

		/* Set the interrupt mask, for poll mode just disable all int */
		spi_mask_intr(dws, 0xff);
		if (imask)
			spi_umask_intr(dws, imask);
		if (txint_level)
			dw_writew(dws, DW_SPI_TXFLTR, txint_level);

		spi_enable_chip(dws, 1);
		if (cs_change)
			dws->prev_chip = chip;
	}

	if (dws->dma_mapped)
		dws->dma_ops->dma_transfer(dws, cs_change);

	if (chip->poll_mode)
		poll_transfer(dws);

	return;

early_exit:
	giveback(dws);
	return;
}

static void pump_messages(struct work_struct *work)
{
	struct dw_spi *dws =
		container_of(work, struct dw_spi, pump_messages);
	unsigned long flags;

	/* Lock queue and check for queue work */
	spin_lock_irqsave(&dws->lock, flags);
	if (list_empty(&dws->queue) || dws->run == QUEUE_STOPPED) {
		dws->busy = 0;
		spin_unlock_irqrestore(&dws->lock, flags);
		return;
	}

	/* Make sure we are not already running a message */
	if (dws->cur_msg) {
		spin_unlock_irqrestore(&dws->lock, flags);
		return;
	}

	/* Extract head of queue */
	dws->cur_msg = list_entry(dws->queue.next, struct spi_message, queue);
	list_del_init(&dws->cur_msg->queue);

	/* Initial message state*/
	dws->cur_msg->state = START_STATE;
	dws->cur_transfer = list_entry(dws->cur_msg->transfers.next,
						struct spi_transfer,
						transfer_list);
	dws->cur_chip = spi_get_ctldata(dws->cur_msg->spi);

	/* Mark as busy and launch transfers */
	tasklet_schedule(&dws->pump_transfers);

	dws->busy = 1;
	spin_unlock_irqrestore(&dws->lock, flags);
}

/* spi_device use this to queue in their spi_msg */
static int dw_spi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct dw_spi *dws = spi_master_get_devdata(spi->master);
	unsigned long flags;

	spin_lock_irqsave(&dws->lock, flags);

	if (dws->run == QUEUE_STOPPED) {
		spin_unlock_irqrestore(&dws->lock, flags);
		return -ESHUTDOWN;
	}

	msg->actual_length = 0;
	msg->status = -EINPROGRESS;
	msg->state = START_STATE;

	list_add_tail(&msg->queue, &dws->queue);

	if (dws->run == QUEUE_RUNNING && !dws->busy) {

		if (dws->cur_transfer || dws->cur_msg)
			queue_work(dws->workqueue,
					&dws->pump_messages);
		else {
			/* If no other data transaction in air, just go */
			spin_unlock_irqrestore(&dws->lock, flags);
			pump_messages(&dws->pump_messages);
			return 0;
		}
	}

	spin_unlock_irqrestore(&dws->lock, flags);
	return 0;
}

/* This may be called twice for each spi dev */
static int dw_spi_setup(struct spi_device *spi)
{
	struct dw_spi_chip *chip_info = NULL;
	struct chip_data *chip;

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (!chip) {
		chip = devm_kzalloc(&spi->dev, sizeof(struct chip_data),
				GFP_KERNEL);
		if (!chip)
			return -ENOMEM;
		spi_set_ctldata(spi, chip);
	}

	/*
	 * Protocol drivers may change the chip settings, so...
	 * if chip_info exists, use it
	 */
	chip_info = spi->controller_data;

	/* chip_info doesn't always exist */
	if (chip_info) {
		if (chip_info->cs_control)
			chip->cs_control = chip_info->cs_control;

		chip->poll_mode = chip_info->poll_mode;
		chip->type = chip_info->type;

		chip->rx_threshold = 0;
		chip->tx_threshold = 0;

		chip->enable_dma = chip_info->enable_dma;
	}

	if (spi->bits_per_word == 8) {
		chip->n_bytes = 1;
		chip->dma_width = 1;
	} else if (spi->bits_per_word == 16) {
		chip->n_bytes = 2;
		chip->dma_width = 2;
	}
	chip->bits_per_word = spi->bits_per_word;

	if (!spi->max_speed_hz) {
		dev_err(&spi->dev, "No max speed HZ parameter\n");
		return -EINVAL;
	}
	chip->speed_hz = spi->max_speed_hz;

	chip->tmode = 0; /* Tx & Rx */
	/* Default SPI mode is SCPOL = 0, SCPH = 0 */
	chip->cr0 = (chip->bits_per_word - 1)
			| (chip->type << SPI_FRF_OFFSET)
			| (spi->mode  << SPI_MODE_OFFSET)
			| (chip->tmode << SPI_TMOD_OFFSET);

	return 0;
}

static void dw_spi_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	kfree(chip);
}

static int init_queue(struct dw_spi *dws)
{
	INIT_LIST_HEAD(&dws->queue);
	spin_lock_init(&dws->lock);

	dws->run = QUEUE_STOPPED;
	dws->busy = 0;

	tasklet_init(&dws->pump_transfers,
			pump_transfers,	(unsigned long)dws);

	INIT_WORK(&dws->pump_messages, pump_messages);
	dws->workqueue = create_singlethread_workqueue(
					dev_name(dws->master->dev.parent));
	if (dws->workqueue == NULL)
		return -EBUSY;

	return 0;
}

static int start_queue(struct dw_spi *dws)
{
	unsigned long flags;

	spin_lock_irqsave(&dws->lock, flags);

	if (dws->run == QUEUE_RUNNING || dws->busy) {
		spin_unlock_irqrestore(&dws->lock, flags);
		return -EBUSY;
	}

	dws->run = QUEUE_RUNNING;
	dws->cur_msg = NULL;
	dws->cur_transfer = NULL;
	dws->cur_chip = NULL;
	dws->prev_chip = NULL;
	spin_unlock_irqrestore(&dws->lock, flags);

	queue_work(dws->workqueue, &dws->pump_messages);

	return 0;
}

static int stop_queue(struct dw_spi *dws)
{
	unsigned long flags;
	unsigned limit = 50;
	int status = 0;

	spin_lock_irqsave(&dws->lock, flags);
	dws->run = QUEUE_STOPPED;
	while ((!list_empty(&dws->queue) || dws->busy) && limit--) {
		spin_unlock_irqrestore(&dws->lock, flags);
		msleep(10);
		spin_lock_irqsave(&dws->lock, flags);
	}

	if (!list_empty(&dws->queue) || dws->busy)
		status = -EBUSY;
	spin_unlock_irqrestore(&dws->lock, flags);

	return status;
}

static int destroy_queue(struct dw_spi *dws)
{
	int status;

	status = stop_queue(dws);
	if (status != 0)
		return status;
	destroy_workqueue(dws->workqueue);
	return 0;
}

/* Restart the controller, disable all interrupts, clean rx fifo */
static void spi_hw_init(struct dw_spi *dws)
{
	spi_enable_chip(dws, 0);
	spi_mask_intr(dws, 0xff);
	spi_enable_chip(dws, 1);

	/*
	 * Try to detect the FIFO depth if not set by interface driver,
	 * the depth could be from 2 to 256 from HW spec
	 */
	if (!dws->fifo_len) {
		u32 fifo;
		for (fifo = 2; fifo <= 257; fifo++) {
			dw_writew(dws, DW_SPI_TXFLTR, fifo);
			if (fifo != dw_readw(dws, DW_SPI_TXFLTR))
				break;
		}

		dws->fifo_len = (fifo == 257) ? 0 : fifo;
		dw_writew(dws, DW_SPI_TXFLTR, 0);
	}
}

int dw_spi_add_host(struct device *dev, struct dw_spi *dws)
{
	struct spi_master *master;
	int ret;

	BUG_ON(dws == NULL);

	master = spi_alloc_master(dev, 0);
	if (!master)
		return -ENOMEM;

	dws->master = master;
	dws->type = SSI_MOTO_SPI;
	dws->prev_chip = NULL;
	dws->dma_inited = 0;
	dws->dma_addr = (dma_addr_t)(dws->paddr + 0x60);
	snprintf(dws->name, sizeof(dws->name), "dw_spi%d",
			dws->bus_num);

	ret = devm_request_irq(dev, dws->irq, dw_spi_irq, IRQF_SHARED,
			dws->name, dws);
	if (ret < 0) {
		dev_err(&master->dev, "can not get IRQ\n");
		goto err_free_master;
	}

	master->mode_bits = SPI_CPOL | SPI_CPHA;
	master->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16);
	master->bus_num = dws->bus_num;
	master->num_chipselect = dws->num_cs;
	master->cleanup = dw_spi_cleanup;
	master->setup = dw_spi_setup;
	master->transfer = dw_spi_transfer;

	/* Basic HW init */
	spi_hw_init(dws);

	if (dws->dma_ops && dws->dma_ops->dma_init) {
		ret = dws->dma_ops->dma_init(dws);
		if (ret) {
			dev_warn(&master->dev, "DMA init failed\n");
			dws->dma_inited = 0;
		}
	}

	/* Initial and start queue */
	ret = init_queue(dws);
	if (ret) {
		dev_err(&master->dev, "problem initializing queue\n");
		goto err_diable_hw;
	}
	ret = start_queue(dws);
	if (ret) {
		dev_err(&master->dev, "problem starting queue\n");
		goto err_diable_hw;
	}

	spi_master_set_devdata(master, dws);
	ret = devm_spi_register_master(dev, master);
	if (ret) {
		dev_err(&master->dev, "problem registering spi master\n");
		goto err_queue_alloc;
	}

	mrst_spi_debugfs_init(dws);
	return 0;

err_queue_alloc:
	destroy_queue(dws);
	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);
err_diable_hw:
	spi_enable_chip(dws, 0);
err_free_master:
	spi_master_put(master);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_add_host);

void dw_spi_remove_host(struct dw_spi *dws)
{
	int status = 0;

	if (!dws)
		return;
	mrst_spi_debugfs_remove(dws);

	/* Remove the queue */
	status = destroy_queue(dws);
	if (status != 0)
		dev_err(&dws->master->dev,
			"dw_spi_remove: workqueue will not complete, message memory not freed\n");

	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);
	spi_enable_chip(dws, 0);
	/* Disable clk */
	spi_set_clk(dws, 0);
}
EXPORT_SYMBOL_GPL(dw_spi_remove_host);

int dw_spi_suspend_host(struct dw_spi *dws)
{
	int ret = 0;

	ret = stop_queue(dws);
	if (ret)
		return ret;
	spi_enable_chip(dws, 0);
	spi_set_clk(dws, 0);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_suspend_host);

int dw_spi_resume_host(struct dw_spi *dws)
{
	int ret;

	spi_hw_init(dws);
	ret = start_queue(dws);
	if (ret)
		dev_err(&dws->master->dev, "fail to start queue (%d)\n", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_resume_host);

MODULE_AUTHOR("Feng Tang <feng.tang@intel.com>");
MODULE_DESCRIPTION("Driver for DesignWare SPI controller core");
MODULE_LICENSE("GPL v2");
