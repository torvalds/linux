/*
 * Designware SPI core controller driver (refer spi_dw.c)
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
#include <linux/clk.h>


#include "spi-rockchip-core.h"

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
	
	u8 slave_enable;
	u32 dma_width;
	u32 rx_threshold;
	u32 tx_threshold;
	u8 enable_dma;
	u8 bits_per_word;
	u16 clk_div;		/* baud rate divider */
	u32 speed_hz;		/* baud rate */
	void (*cs_control)(struct dw_spi *dws, u32 cs, u8 flag);
};

#ifdef CONFIG_DEBUG_FS
#define SPI_REGS_BUFSIZE	1024

static ssize_t spi_write_proc_data(struct file *file, const char __user *buffer,
			   size_t count, loff_t *data)
{	
	struct dw_spi *dws;
	char *buf;
	ssize_t ret;
	int reg = 0,value = 0;
	
	dws = file->private_data;

	buf = kzalloc(32, GFP_KERNEL);
	if (!buf)
	return 0;
	
	ret = copy_from_user(buf, buffer, count);
	if (ret)
	{
		return ret; 
	}

	if((strstr(buf, "debug") != NULL) || (strstr(buf, "DEBUG") != NULL))
	{		
		atomic_set(&dws->debug_flag, 1);		
		kfree(buf);
		printk("%s:open debug\n",__func__);
		return count;
	}
	else if((strstr(buf, "stop") != NULL) || (strstr(buf, "STOP") != NULL))
	{		
		atomic_set(&dws->debug_flag, 0);
		printk("%s:close debug\n",__func__);
	}
	else if((strstr(buf, "=") != NULL))
	{
		printk("%s:invalid command\n",__func__);	
		return count;
	}

	sscanf(buf, "0x%x=0x%x", &reg, &value);

	if((reg >= SPIM_CTRLR0) && (reg <= SPIM_DMARDLR))	
	{
		dw_writew(dws, reg, value);
		printk("%s:write data[0x%x] to reg[0x%x] succesfully\n",__func__, value, reg);
	}
	else
	{
		printk("%s:data[0x%x] or reg[0x%x] is out of range\n",__func__, value, reg);
	}
	
	kfree(buf);
		
	return count; 
}

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
			"CTRL0: \t\t0x%08x\n", dw_readl(dws, SPIM_CTRLR0));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"CTRL1: \t\t0x%08x\n", dw_readl(dws, SPIM_CTRLR1));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"SSIENR: \t0x%08x\n", dw_readl(dws, SPIM_SSIENR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"SER: \t\t0x%08x\n", dw_readl(dws, SPIM_SER));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"BAUDR: \t\t0x%08x\n", dw_readl(dws, SPIM_BAUDR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"TXFTLR: \t0x%08x\n", dw_readl(dws, SPIM_TXFTLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"RXFTLR: \t0x%08x\n", dw_readl(dws, SPIM_RXFTLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"TXFLR: \t\t0x%08x\n", dw_readl(dws, SPIM_TXFLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"RXFLR: \t\t0x%08x\n", dw_readl(dws, SPIM_RXFLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"SR: \t\t0x%08x\n", dw_readl(dws, SPIM_SR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"IMR: \t\t0x%08x\n", dw_readl(dws, SPIM_IMR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"ISR: \t\t0x%08x\n", dw_readl(dws, SPIM_ISR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"DMACR: \t\t0x%08x\n", dw_readl(dws, SPIM_DMACR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"DMATDLR: \t0x%08x\n", dw_readl(dws, SPIM_DMATDLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"DMARDLR: \t0x%08x\n", dw_readl(dws, SPIM_DMARDLR));
	len += snprintf(buf + len, SPI_REGS_BUFSIZE - len,
			"=================================\n");

	ret =  simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

static const struct file_operations spi_regs_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= spi_show_regs,
	.write		= spi_write_proc_data,
	.llseek		= default_llseek,
};

static int spi_debugfs_init(struct dw_spi *dws)
{
	dws->debugfs = debugfs_create_dir("spi", NULL);
	if (!dws->debugfs)
		return -ENOMEM;

	debugfs_create_file("registers", S_IFREG | S_IRUGO,
		dws->debugfs, (void *)dws, &spi_regs_ops);
	return 0;
}

static void spi_debugfs_remove(struct dw_spi *dws)
{
	if (dws->debugfs)
		debugfs_remove_recursive(dws->debugfs);
}

#else
static inline int spi_debugfs_init(struct dw_spi *dws)
{
	return 0;
}

static inline void spi_debugfs_remove(struct dw_spi *dws)
{
}
#endif /* CONFIG_DEBUG_FS */


static void wait_till_not_busy(struct dw_spi *dws)
{
	unsigned long end = jiffies + 1 + usecs_to_jiffies(1000);
	//if spi was slave, it is SR_BUSY always.  
	if(dws->cur_chip) {
		if(dws->cur_chip->slave_enable == 1)
			return;
	}
	
	while (time_before(jiffies, end)) {
		if (!(dw_readw(dws, SPIM_SR) & SR_BUSY))
			return;
	}
	dev_err(&dws->master->dev,
		"DW SPI: Status keeps busy for 1000us after a read/write!\n");
}


static void flush(struct dw_spi *dws)
{
	while (!(dw_readw(dws, SPIM_SR) & SR_RF_EMPT))
		dw_readw(dws, SPIM_RXDR);

	wait_till_not_busy(dws);
}


/* Return the max entries we can fill into tx fifo */
static inline u32 tx_max(struct dw_spi *dws)
{
	u32 tx_left, tx_room;

	tx_left = (dws->tx_end - dws->tx) / dws->n_bytes;
	tx_room = dws->fifo_len - dw_readw(dws, SPIM_TXFLR);

	/*
	 * Another concern is about the tx/rx mismatch, we
	 * though to use (dws->fifo_len - rxflr - txflr) as
	 * one maximum value for tx, but it doesn't cover the
	 * data which is out of tx/rx fifo and inside the
	 * shift registers. So a control from sw point of
	 * view is taken.
	 */
	//rxtx_gap =  ((dws->rx_end - dws->rx) - (dws->tx_end - dws->tx))
	//		/ dws->n_bytes;

	return min(tx_left, tx_room);
}

/* Return the max entries we should read out of rx fifo */
static inline u32 rx_max(struct dw_spi *dws)
{
	u32 rx_left = (dws->rx_end - dws->rx) / dws->n_bytes;

	return min(rx_left, (u32)dw_readw(dws, SPIM_RXFLR));
}

static void dw_writer(struct dw_spi *dws)
{
	u32 max = tx_max(dws);
	u16 txw = 0;	
	
	DBG_SPI("%dbyte tx:",dws->n_bytes);
	while (max--) {
		/* Set the tx word if the transfer's original "tx" is not null */
		if (dws->tx_end - dws->len) {
			if (dws->n_bytes == 1)
			{
				txw = *(u8 *)(dws->tx);	
				DBG_SPI("0x%02x,", *(u8 *)(dws->tx));
			}
			else
			{
				txw = *(u16 *)(dws->tx);
				DBG_SPI("0x%02x,", *(u16 *)(dws->tx));
			}
		}
		dw_writew(dws, SPIM_TXDR, txw);
		dws->tx += dws->n_bytes;
	}
	
	//it is neccessary
	wait_till_not_busy(dws);
	
	DBG_SPI("\n");
}

static void dw_reader(struct dw_spi *dws)
{
	u32 max = rx_max(dws);
	u16 rxw;
	
	DBG_SPI("%dbyte rx:",dws->n_bytes);

	while (max--) {
		rxw = dw_readw(dws, SPIM_RXDR);
		/* Care rx only if the transfer's original "rx" is not null */
		if (dws->rx_end - dws->len) {
			if (dws->n_bytes == 1)
			{
				*(u8 *)(dws->rx) = rxw;
				DBG_SPI("0x%02x,", *(u8 *)(dws->rx));
			}
			else
			{
				*(u16 *)(dws->rx) = rxw;
				DBG_SPI("0x%02x,", *(u16 *)(dws->rx));
			}
		}
		
		dws->rx += dws->n_bytes;
	}
	
	DBG_SPI("\n");
}

static int reader_all(struct dw_spi *dws)
{
	while (!(dw_readw(dws, SPIM_SR) & SR_RF_EMPT)
		&& (dws->rx < dws->rx_end)) {
			dw_reader(dws);		
			wait_till_not_busy(dws);
		}

	return dws->rx == dws->rx_end;
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
	if (!dws->dma_inited
		|| !dws->cur_chip->enable_dma
		|| !dws->dma_ops)
		return 0;

	if (dws->cur_transfer->tx_dma)
		dws->tx_dma = dws->cur_transfer->tx_dma;

	if (dws->cur_transfer->rx_dma)
		dws->rx_dma = dws->cur_transfer->rx_dma;
	
	DBG_SPI("%s:line=%d\n",__func__,__LINE__);
	return 1;
}

/* Caller already set message->status; dma and pio irqs are blocked */
static void giveback(struct dw_spi *dws)
{
	struct spi_transfer *last_transfer;
	unsigned long flags;
	struct spi_message *msg;	
	struct spi_message *next_msg;
	
	spin_lock_irqsave(&dws->lock, flags);
	msg = dws->cur_msg;
	dws->cur_msg = NULL;
	dws->cur_transfer = NULL;
	dws->prev_chip = dws->cur_chip;
	dws->cur_chip = NULL;
	dws->dma_mapped = 0;
	dws->state = 0;
	//queue_work(dws->workqueue, &dws->pump_messages);

	/*it is important to close intterrupt*/
	spi_mask_intr(dws, 0xff);
	//rk29xx_writew(dws, SPIM_DMACR, 0);
	
	spin_unlock_irqrestore(&dws->lock, flags);

	last_transfer = list_entry(msg->transfers.prev,
					struct spi_transfer,
					transfer_list);

	if (!last_transfer->cs_change && dws->cs_control)
		dws->cs_control(dws, msg->spi->chip_select, MRST_SPI_DEASSERT);

	msg->state = NULL;

	/* get a pointer to the next message, if any */
	next_msg = spi_get_next_queued_message(dws->master);

	/* see if the next and current messages point
	* to the same chip
	*/
	if (next_msg && next_msg->spi != msg->spi)
	next_msg = NULL;
	
	dws->cur_chip = NULL;
	spi_finalize_current_message(dws->master);
	
	DBG_SPI("%s:line=%d,tx_left=%d\n",__func__,__LINE__, (dws->tx_end - dws->tx) / dws->n_bytes);
}


static void int_error_stop(struct dw_spi *dws, const char *msg)
{
	/* Stop the hw */
	flush(dws);
	spi_enable_chip(dws, 0);

	dev_err(&dws->master->dev, "%s\n", msg);
	dws->cur_msg->state = ERROR_STATE;
	tasklet_schedule(&dws->pump_transfers);	
	DBG_SPI("%s:line=%d\n",__func__,__LINE__);
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
	
	DBG_SPI("%s:line=%d\n",__func__,__LINE__);
}
EXPORT_SYMBOL_GPL(dw_spi_xfer_done);

static irqreturn_t interrupt_transfer(struct dw_spi *dws)
{
	u16 irq_status;
	u32 int_level = dws->fifo_len / 2;
	u32 left;


	irq_status = dw_readw(dws, SPIM_ISR) & 0x1f;
	
	DBG_SPI("%s:line=%d,irq_status=0x%x\n",__func__,__LINE__,irq_status);
	
	/* Error handling */
	if (irq_status & (SPI_INT_TXOI | SPI_INT_RXOI | SPI_INT_RXUI)) {
		dw_writew(dws, SPIM_ICR, SPI_CLEAR_INT_TXOI | SPI_CLEAR_INT_RXOI | SPI_CLEAR_INT_RXUI);
		printk("%s:irq_status=0x%x\n",__func__,irq_status);
		int_error_stop(dws, "interrupt_transfer: fifo overrun/underrun");
		return IRQ_HANDLED;
	}

	if (irq_status & SPI_INT_TXEI) 
	{
		spi_mask_intr(dws, SPI_INT_TXEI);
		dw_writer(dws);

		if (dws->rx) {
		    reader_all(dws);
		}

		/* Re-enable the IRQ if there is still data left to tx */
		if (dws->tx_end > dws->tx)
			spi_umask_intr(dws, SPI_INT_TXEI);
		else
			dw_spi_xfer_done(dws);
	}

	if (irq_status & SPI_INT_RXFI) {
		spi_mask_intr(dws, SPI_INT_RXFI);
		
		reader_all(dws);

		/* Re-enable the IRQ if there is still data left to rx */
		if (dws->rx_end > dws->rx) {
			left = ((dws->rx_end - dws->rx) / dws->n_bytes) - 1;
		    left = (left > int_level) ? int_level : left;

			dw_writew(dws, SPIM_RXFTLR, left);
			spi_umask_intr(dws, SPI_INT_RXFI);
		}
		else {
			dw_spi_xfer_done(dws);
		}
		
	}

	return IRQ_HANDLED;
}


static irqreturn_t dw_spi_irq(int irq, void *dev_id)
{
	struct dw_spi *dws = dev_id;
	u16 irq_status = dw_readw(dws, SPIM_ISR)&0x3f;

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
	DBG_SPI("%s:len=%d\n",__func__, dws->len);
	
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
	u16 rxint_level = 0;
	u16 clk_div = 0;
	u32 speed = 0;
	u32 cr0 = 0;	
	u16 dma_ctrl = 0;


	/* Get current state information */
	message = dws->cur_msg;
	transfer = dws->cur_transfer;
	chip = dws->cur_chip;
	spi = message->spi;

	if (unlikely(!chip->clk_div))
	{
		chip->clk_div = dws->max_freq / chip->speed_hz;
		chip->clk_div = (chip->clk_div + 1) & 0xfffe;
		chip->speed_hz = dws->max_freq / chip->clk_div;
	}


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
	dws->cs_change = transfer->cs_change;
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

			chip->speed_hz = dws->max_freq / clk_div;
			chip->clk_div = clk_div;
		}
	}
	DBG_SPI("%s:len=%d,clk_div=%d,speed_hz=%d\n",__func__,dws->len,chip->clk_div,chip->speed_hz);
	if (transfer->bits_per_word) {
		bits = transfer->bits_per_word;

		switch (bits) {
		case 8:
		case 16:
			dws->n_bytes = dws->dma_width = bits >> 3;
			break;
		default:
			printk(KERN_ERR "MRST SPI0: unsupported bits:"
				"%db\n", bits);
			message->status = -EIO;
			goto early_exit;
		}

		cr0 =((dws->n_bytes) << SPI_DFS_OFFSET)
			| (SPI_HALF_WORLD_OFF << SPI_HALF_WORLD_TX_OFFSET)
			| (SPI_SSN_DELAY_ONE << SPI_SSN_DELAY_OFFSET)
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


		cr0 &= ~(0x3 << SPI_MODE_OFFSET);		
		cr0 &= ~(0x3 << SPI_TMOD_OFFSET);
		cr0 &= ~(0x1 << SPI_OPMOD_OFFSET);	
		cr0 |= (spi->mode << SPI_MODE_OFFSET);
		cr0 |= (chip->tmode << SPI_TMOD_OFFSET);
		cr0 |= ((chip->slave_enable & 1) << SPI_OPMOD_OFFSET);
	}

	/* Check if current transfer is a DMA transaction */
	dws->dma_mapped = map_dma_buffers(dws);

	/*
	 * Interrupt mode
	 * we only need set the TXEI IRQ, as TX/RX always happen syncronizely
	 */
	if (!dws->dma_mapped && !chip->poll_mode) {	
		int templen ;
		
		if (chip->tmode == SPI_TMOD_RO) {
			templen = dws->len / dws->n_bytes - 1;
			rxint_level = dws->fifo_len / 2;
			rxint_level = (templen > rxint_level) ? rxint_level : templen;
			imask |= SPI_INT_RXFI;
		}
		else {	
			templen = dws->len / dws->n_bytes;
			txint_level = dws->fifo_len / 2;
			txint_level = (templen > txint_level) ? txint_level : templen;
			imask |= SPI_INT_TXEI | SPI_INT_TXOI;
		}
		dws->transfer_handler = interrupt_transfer;
	}

	/*
	 * Reprogram registers only if
	 *	1. chip select changes
	 *	2. clk_div is changed
	 *	3. control value changes
	 */
	//if (dw_readw(dws, SPIM_CTRLR0) != cr0 || cs_change || clk_div || imask) 		
	if(dws->tx || dws->rx)
	{
		spi_enable_chip(dws, 0);
		if (dw_readl(dws, SPIM_CTRLR0) != cr0)
			dw_writel(dws, SPIM_CTRLR0, cr0);


		spi_set_clk(dws, clk_div ? clk_div : chip->clk_div);		
		spi_chip_sel(dws, spi->chip_select);

		dw_writew(dws, SPIM_CTRLR1, dws->len-1);

		if (txint_level != dw_readl(dws, SPIM_TXFTLR))
			dw_writew(dws, SPIM_TXFTLR, txint_level);
			
		if (rxint_level != dw_readl(dws, SPIM_RXFTLR))
		{
			dw_writew(dws, SPIM_RXFTLR, rxint_level);
			DBG_SPI("%s:rxint_level=%d\n",__func__,rxint_level);
		}

		/* setup DMA related registers */
		if(dws->dma_mapped)
		{
			/* Set the interrupt mask, for poll mode just diable all int */
			spi_mask_intr(dws, 0xff);		
			if(dws->tx)
			{
				dma_ctrl |= SPI_DMACR_TX_ENABLE;		
				dw_writew(dws, SPIM_DMATDLR, 8);
				dw_writew(dws, SPIM_CTRLR1, dws->len-1);	
			}
			
			if (dws->rx)
			{
				dma_ctrl |= SPI_DMACR_RX_ENABLE;	
				dw_writew(dws, SPIM_DMARDLR, 0);			
				dw_writew(dws, SPIM_CTRLR1, dws->len-1);	
			}
			dw_writew(dws, SPIM_DMACR, dma_ctrl);

			DBG_SPI("%s:dma_ctrl=0x%x\n",__func__,dw_readw(dws, SPIM_DMACR));
			
		}
		
		spi_enable_chip(dws, 1);

		DBG_SPI("%s:ctrl0=0x%x\n",__func__,dw_readw(dws, SPIM_CTRLR0));

		/* Set the interrupt mask, for poll mode just diable all int */
		spi_mask_intr(dws, 0xff);
		if (imask)
			spi_umask_intr(dws, imask);
		
		if (cs_change)
			dws->prev_chip = chip;

	}
	else
	{
		printk("%s:warning tx and rx is null\n",__func__);
	}

	/*dma should be ready before spi_enable_chip*/
	if (dws->dma_mapped)
	dws->dma_ops->dma_transfer(dws, cs_change); 

	if (chip->poll_mode)
		poll_transfer(dws);

	return;

early_exit:
	giveback(dws);
	return;
}

static int dw_spi_transfer_one_message(struct spi_master *master,
					   struct spi_message *msg)
{
	struct dw_spi *dws = spi_master_get_devdata(master);
	int ret = 0;
	
	dws->cur_msg = msg;
	/* Initial message state*/
	dws->cur_msg->state = START_STATE;
	dws->cur_transfer = list_entry(dws->cur_msg->transfers.next,
						struct spi_transfer,
						transfer_list);

	/* prepare to setup the SSP, in pump_transfers, using the per
	 * chip configuration */
	dws->cur_chip = spi_get_ctldata(dws->cur_msg->spi);
	
	dws->dma_mapped = map_dma_buffers(dws);	
	INIT_COMPLETION(dws->xfer_completion);
	
	/* Mark as busy and launch transfers */
	tasklet_schedule(&dws->pump_transfers);
	
	DBG_SPI("%s:line=%d\n",__func__,__LINE__);
	if (dws->dma_mapped)
	{
		ret = wait_for_completion_timeout(&dws->xfer_completion,
							msecs_to_jiffies(2000));
		if(ret == 0)
		{
			dev_err(&dws->master->dev, "dma transfer timeout\n");			
			giveback(dws);
			return 0;
		}
		
		DBG_SPI("%s:wait %d\n",__func__, ret);
	}
		
	return 0;
}

static int dw_spi_prepare_transfer(struct spi_master *master)
{
	struct dw_spi *dws = spi_master_get_devdata(master);

	//pm_runtime_get_sync(&dws->pdev->dev);
	
	DBG_SPI("%s:line=%d\n",__func__,__LINE__);
	return 0;
}

static int dw_spi_unprepare_transfer(struct spi_master *master)
{
	struct dw_spi *dws = spi_master_get_devdata(master);

	/* Disable the SSP now */
	//write_SSCR0(read_SSCR0(dws->ioaddr) & ~SSCR0_SSE,
	//	    dws->ioaddr);

	//pm_runtime_mark_last_busy(&dws->pdev->dev);
	//pm_runtime_put_autosuspend(&dws->pdev->dev);
	
	DBG_SPI("%s:line=%d\n",__func__,__LINE__);
	return 0;
}

/* This may be called twice for each spi dev */
static int dw_spi_setup(struct spi_device *spi)
{
	struct dw_spi_chip *chip_info = NULL;
	struct chip_data *chip;

	if (spi->bits_per_word != 8 && spi->bits_per_word != 16)
		return -EINVAL;

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (!chip) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;

		chip->cs_control = spi_cs_control;
		chip->enable_dma = 0; 
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

	if (spi->bits_per_word <= 8) {
		chip->n_bytes = 1;
		chip->dma_width = 1;
	} else if (spi->bits_per_word <= 16) {
		chip->n_bytes = 2;
		chip->dma_width = 2;
	} else {
		/* Never take >16b case for MRST SPIC */
		dev_err(&spi->dev, "invalid wordsize\n");
		return -EINVAL;
	}
	chip->bits_per_word = spi->bits_per_word;

	if (!spi->max_speed_hz) {
		dev_err(&spi->dev, "No max speed HZ parameter\n");
		return -EINVAL;
	}
	chip->speed_hz = spi->max_speed_hz;

	chip->tmode = 0; /* Tx & Rx */
	/* Default SPI mode is SCPOL = 0, SCPH = 0 */
	chip->cr0 = ((chip->n_bytes) << SPI_DFS_OFFSET)
		    | (SPI_HALF_WORLD_OFF << SPI_HALF_WORLD_TX_OFFSET)
			| (SPI_SSN_DELAY_ONE << SPI_SSN_DELAY_OFFSET)
			| (chip->type << SPI_FRF_OFFSET)
			| (spi->mode  << SPI_MODE_OFFSET)
			| (chip->tmode << SPI_TMOD_OFFSET);

	spi_set_ctldata(spi, chip);
	
	//printk("%s:line=%d\n",__func__,__LINE__);
	return 0;
}

static void dw_spi_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	kfree(chip);
}


/* Restart the controller, disable all interrupts, clean rx fifo */
static void spi_hw_init(struct dw_spi *dws)
{
	spi_enable_chip(dws, 0);
	spi_mask_intr(dws, 0xff);

	/*
	 * Try to detect the FIFO depth if not set by interface driver,
	 * the depth could be from 2 to 32 from HW spec
	 */
	if (!dws->fifo_len) {
		u32 fifo;
		for (fifo = 2; fifo <= 31; fifo++) {
			dw_writew(dws, SPIM_TXFTLR, fifo);
			if (fifo != dw_readw(dws, SPIM_TXFTLR))
				break;
		}

		dws->fifo_len = (fifo == 31) ? 0 : fifo;
		dw_writew(dws, SPIM_TXFTLR, 0);
	}
	
	//spi_enable_chip(dws, 1);
	flush(dws);
	DBG_SPI("%s:fifo_len=%d\n",__func__, dws->fifo_len);
}

int dw_spi_add_host(struct dw_spi *dws)
{
	struct spi_master *master;
	int ret;

	BUG_ON(dws == NULL);

	master = spi_alloc_master(dws->parent_dev, 0);
	if (!master) {
		ret = -ENOMEM;
		goto exit;
	}

	dws->master = master;
	dws->type = SSI_MOTO_SPI;
	dws->prev_chip = NULL;
	dws->dma_inited = 0;
	dws->tx_dma_addr = (dma_addr_t)(dws->paddr + SPIM_TXDR);	
	dws->rx_dma_addr = (dma_addr_t)(dws->paddr + SPIM_RXDR);
	snprintf(dws->name, sizeof(dws->name), "dw_spi%d",
			dws->bus_num);

	ret = request_irq(dws->irq, dw_spi_irq, IRQF_SHARED,
			dws->name, dws);
	if (ret < 0) {
		dev_err(&master->dev, "can not get IRQ\n");
		goto err_free_master;
	}
	
	master->dev.parent = dws->parent_dev;
	master->dev.of_node = dws->parent_dev->of_node;	
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LOOP;
	master->bus_num = dws->bus_num;
	master->num_chipselect = dws->num_cs;
	master->cleanup = dw_spi_cleanup;
	master->setup = dw_spi_setup;
	master->transfer_one_message = dw_spi_transfer_one_message;
	master->prepare_transfer_hardware = dw_spi_prepare_transfer;
	master->unprepare_transfer_hardware = dw_spi_unprepare_transfer;
	
	spin_lock_init(&dws->lock);
	tasklet_init(&dws->pump_transfers,
			pump_transfers,	(unsigned long)dws);


	/* Basic HW init */
	spi_hw_init(dws);

	if (dws->dma_ops && dws->dma_ops->dma_init) {
		ret = dws->dma_ops->dma_init(dws);
		if (ret) {
			dev_warn(&master->dev, "DMA init failed,ret=%d\n",ret);
			dws->dma_inited = 0;
		}
	}
	
	spi_master_set_devdata(master, dws);
	ret = spi_register_master(master);
	if (ret) {
		dev_err(&master->dev, "problem registering spi master\n");
		goto err_queue_alloc;
	}

	spi_debugfs_init(dws);

	
	DBG_SPI("%s:bus_num=%d\n",__func__, dws->bus_num);
	return 0;

err_queue_alloc:
	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);
/* err_diable_hw: */
	spi_enable_chip(dws, 0);
	free_irq(dws->irq, dws);
err_free_master:
	spi_master_put(master);
exit:
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_add_host);

void dw_spi_remove_host(struct dw_spi *dws)
{
	if (!dws)
		return;
	
	spi_debugfs_remove(dws);

	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);
	
	spi_enable_chip(dws, 0);
	/* Disable clk */
	spi_set_clk(dws, 0);
	free_irq(dws->irq, dws);

	/* Disconnect from the SPI framework */
	spi_unregister_master(dws->master);

	
	DBG_SPI("%s:bus_num=%d\n",__func__, dws->bus_num);
}
EXPORT_SYMBOL_GPL(dw_spi_remove_host);

int dw_spi_suspend_host(struct dw_spi *dws)
{
	int ret = 0;
	
	ret = spi_master_suspend(dws->master);
	if (ret != 0)
	return ret;
	
	spi_enable_chip(dws, 0);
	spi_set_clk(dws, 0);
	
	clk_disable_unprepare(dws->clk_spi);
	
	DBG_SPI("%s:bus_num=%d\n",__func__, dws->bus_num);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_suspend_host);

int dw_spi_resume_host(struct dw_spi *dws)
{
	int ret;

	/* Enable the SPI clock */
	clk_prepare_enable(dws->clk_spi);
	
	spi_hw_init(dws);

	/* Start the queue running */
	ret = spi_master_resume(dws->master);
	if (ret != 0) {
		printk("%s:problem starting queue (%d)\n", __func__, ret);
		return ret;
	}
	
	DBG_SPI("%s:bus_num=%d\n",__func__, dws->bus_num);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_resume_host);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_DESCRIPTION("Driver for DesignWare SPI controller core");
MODULE_LICENSE("GPL v2");
