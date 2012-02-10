/*drivers/serial/rk29xx_spim.c - driver for rk29xx spim device 
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <linux/dma-mapping.h>
#include <asm/dma.h>
#include <linux/preempt.h>
#include "rk29_spim.h"
#include <linux/spi/spi.h>
#include <mach/board.h>

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

/*原有的spi驱动效率比较低，
无法满足大数据量的传输；
QUICK_TRANSFER用于快速传输，同时可指定半双工或全双工，
默认使用半双工
*/

//#define QUICK_TRANSFER         

#if 0
#define DBG   printk
#else
#define DBG(x...)
#endif

#define DMA_MIN_BYTES 8


#define START_STATE	((void *)0)
#define RUNNING_STATE	((void *)1)
#define DONE_STATE	((void *)2)
#define ERROR_STATE	((void *)-1)

#define QUEUE_RUNNING	0
#define QUEUE_STOPPED	1

#define MRST_SPI_DEASSERT	0
#define MRST_SPI_ASSERT		1  ///CS0
#define MRST_SPI_ASSERT1	2  ///CS1

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
	u8 enable_dma:1;
	u8 bits_per_word;
	u16 clk_div;		/* baud rate divider */
	u32 speed_hz;		/* baud rate */
	int (*write)(struct rk29xx_spi *dws);
	int (*read)(struct rk29xx_spi *dws);
	void (*cs_control)(struct rk29xx_spi *dws, u32 cs, u8 flag);
};

#define SUSPND    (1<<0)
#define SPIBUSY   (1<<1)
#define RXBUSY    (1<<2)
#define TXBUSY    (1<<3)

//
#ifdef CONFIG_LCD_USE_SPIM_CONTROL
void rk29_lcd_spim_spin_lock(void)
{
#ifdef CONFIG_LCD_USE_SPI0
	disable_irq(IRQ_SPI0);
#endif

#ifdef CONFIG_LCD_USE_SPI1
	disable_irq(IRQ_SPI1);
#endif

	preempt_disable();
}

void rk29_lcd_spim_spin_unlock(void)
{
	preempt_enable();
	
#ifdef CONFIG_LCD_USE_SPI0
	enable_irq(IRQ_SPI0);
#endif

#ifdef CONFIG_LCD_USE_SPI1
	enable_irq(IRQ_SPI1);
#endif
}
#else
void rk29_lcd_spim_spin_lock(void)
{
     return;
}

void rk29_lcd_spim_spin_unlock(void)
{
     return;
}
#endif


static void spi_dump_regs(struct rk29xx_spi *dws) {
	DBG("MRST SPI0 registers:\n");
	DBG("=================================\n");
	DBG("CTRL0: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_CTRLR0));
	DBG("CTRL1: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_CTRLR1));
	DBG("SSIENR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_ENR));
	DBG("SER: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_SER));
	DBG("BAUDR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_BAUDR));
	DBG("TXFTLR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_TXFTLR));
	DBG("RXFTLR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_RXFTLR));
	DBG("TXFLR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_TXFLR));
	DBG("RXFLR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_RXFLR));
	DBG("SR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_SR));
	DBG("IMR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_IMR));
	DBG("ISR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_ISR));
	DBG("DMACR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_DMACR));
	DBG("DMATDLR: \t0x%08x\n", rk29xx_readl(dws, SPIM_DMATDLR));
	DBG("DMARDLR: \t0x%08x\n", rk29xx_readl(dws, SPIM_DMARDLR));
	DBG("=================================\n");

}

#ifdef CONFIG_DEBUG_FS
static int spi_show_regs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#define SPI_REGS_BUFSIZE	1024
static ssize_t  spi_show_regs(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct rk29xx_spi *dws;
	char *buf;
	u32 len = 0;
	ssize_t ret;

	dws = file->private_data;

	buf = kzalloc(SPI_REGS_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return 0;

	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"MRST SPI0 registers:\n");
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"=================================\n");
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"CTRL0: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_CTRLR0));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"CTRL1: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_CTRLR1));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"SSIENR: \t0x%08x\n", rk29xx_readl(dws, SPIM_ENR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"SER: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_SER));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"BAUDR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_BAUDR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"TXFTLR: \t0x%08x\n", rk29xx_readl(dws, SPIM_TXFTLR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"RXFTLR: \t0x%08x\n", rk29xx_readl(dws, SPIM_RXFTLR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"TXFLR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_TXFLR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"RXFLR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_RXFLR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"SR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_SR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"IMR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_IMR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"ISR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_ISR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"DMACR: \t\t0x%08x\n", rk29xx_readl(dws, SPIM_DMACR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"DMATDLR: \t0x%08x\n", rk29xx_readl(dws, SPIM_DMATDLR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"DMARDLR: \t0x%08x\n", rk29xx_readl(dws, SPIM_DMARDLR));
	len += printk(buf + len, SPI_REGS_BUFSIZE - len,
			"=================================\n");

	ret =  simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

static const struct file_operations mrst_spi_regs_ops = {
	.owner		= THIS_MODULE,
	.open		= spi_show_regs_open,
	.read		= spi_show_regs,
};

static int mrst_spi_debugfs_init(struct rk29xx_spi *dws)
{
	dws->debugfs = debugfs_create_dir("mrst_spi", NULL);
	if (!dws->debugfs)
		return -ENOMEM;

	debugfs_create_file("registers", S_IFREG | S_IRUGO,
		dws->debugfs, (void *)dws, &mrst_spi_regs_ops);
	return 0;
}

static void mrst_spi_debugfs_remove(struct rk29xx_spi *dws)
{
	if (dws->debugfs)
		debugfs_remove_recursive(dws->debugfs);
}

#else
static inline int mrst_spi_debugfs_init(struct rk29xx_spi *dws)
{
	return 0;
}

static inline void mrst_spi_debugfs_remove(struct rk29xx_spi *dws)
{
}
#endif /* CONFIG_DEBUG_FS */

static void wait_till_not_busy(struct rk29xx_spi *dws)
{
	unsigned long end = jiffies + 1 + usecs_to_jiffies(1000);

	while (time_before(jiffies, end)) {
		if (!(rk29xx_readw(dws, SPIM_SR) & SR_BUSY))
			return;
	}
	dev_err(&dws->master->dev,
		"DW SPI: Status keeps busy for 1000us after a read/write!\n");
}

#if defined(QUICK_TRANSFER)
static void wait_till_tf_empty(struct rk29xx_spi *dws)
{
	unsigned long end = jiffies + 1 + usecs_to_jiffies(1000);

	while (time_before(jiffies, end)) {
		if (rk29xx_readw(dws, SPIM_SR) & SR_TF_EMPT)
			return;
	}
	dev_err(&dws->master->dev,
		"DW SPI: Status keeps busy for 1000us after a read/write!\n");
}
#endif

static void flush(struct rk29xx_spi *dws)
{
	while (!(rk29xx_readw(dws, SPIM_SR) & SR_RF_EMPT))
		rk29xx_readw(dws, SPIM_RXDR);

	wait_till_not_busy(dws);
}

static void spi_cs_control(struct rk29xx_spi *dws, u32 cs, u8 flag)
{
	#if 1
	if (flag)
		rk29xx_writel(dws, SPIM_SER, 1 << cs);
	else 		
		rk29xx_writel(dws, SPIM_SER, 0);
	return;
	#else
	struct rk29xx_spi_platform_data *pdata = dws->master->dev.platform_data;
	struct spi_cs_gpio *cs_gpios = pdata->chipselect_gpios;

	if (flag == 0) {
		gpio_direction_output(cs_gpios[cs].cs_gpio, GPIO_HIGH);
	}
	else {
		gpio_direction_output(cs_gpios[cs].cs_gpio, GPIO_LOW);
	}
	#endif
}

static int null_writer(struct rk29xx_spi *dws)
{
	u8 n_bytes = dws->n_bytes;

	if ((rk29xx_readw(dws, SPIM_SR) & SR_TF_FULL)
		|| (dws->tx == dws->tx_end))
		return 0;
	rk29xx_writew(dws, SPIM_TXDR, 0);
	dws->tx += n_bytes;
	//wait_till_not_busy(dws);

	return 1;
}

static int null_reader(struct rk29xx_spi *dws)
{
	u8 n_bytes = dws->n_bytes;
	DBG("func: %s, line: %d\n", __FUNCTION__, __LINE__);
	while ((!(rk29xx_readw(dws, SPIM_SR) & SR_RF_EMPT))
		&& (dws->rx < dws->rx_end)) {
		rk29xx_readw(dws, SPIM_RXDR);
		dws->rx += n_bytes;
	}
	wait_till_not_busy(dws);
	return dws->rx == dws->rx_end;
}

static int u8_writer(struct rk29xx_spi *dws)
{	
	spi_dump_regs(dws);
	DBG("tx: 0x%02x\n", *(u8 *)(dws->tx));
	if ((rk29xx_readw(dws, SPIM_SR) & SR_TF_FULL)
		|| (dws->tx == dws->tx_end))
		return 0;
	rk29xx_writew(dws, SPIM_TXDR, *(u8 *)(dws->tx));
	++dws->tx;
	//wait_till_not_busy(dws);

	return 1;
}

static int u8_reader(struct rk29xx_spi *dws)
{
    spi_dump_regs(dws);
	while (!(rk29xx_readw(dws, SPIM_SR) & SR_RF_EMPT)
		&& (dws->rx < dws->rx_end)) {
		*(u8 *)(dws->rx) = rk29xx_readw(dws, SPIM_RXDR) & 0xFFU;
		DBG("rx: 0x%02x\n", *(u8 *)(dws->rx));
		++dws->rx;
	}

	wait_till_not_busy(dws);
	return dws->rx == dws->rx_end;
}

static int u16_writer(struct rk29xx_spi *dws)
{
	if ((rk29xx_readw(dws, SPIM_SR) & SR_TF_FULL)
		|| (dws->tx == dws->tx_end))
		return 0;

	rk29xx_writew(dws, SPIM_TXDR, *(u16 *)(dws->tx));
	dws->tx += 2;
	//wait_till_not_busy(dws);

	return 1;
}

static int u16_reader(struct rk29xx_spi *dws)
{
	u16 temp;

	while (!(rk29xx_readw(dws, SPIM_SR) & SR_RF_EMPT)
		&& (dws->rx < dws->rx_end)) {
		temp = rk29xx_readw(dws, SPIM_RXDR);
		*(u16 *)(dws->rx) = temp;
		//DBG("rx: 0x%04x\n", *(u16 *)(dws->rx));
		dws->rx += 2;
	}

	wait_till_not_busy(dws);
	return dws->rx == dws->rx_end;
}

static void *next_transfer(struct rk29xx_spi *dws)
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

static void rk29_spi_dma_rxcb(void *buf_id,
				 int size, enum rk29_dma_buffresult res)
{
	struct rk29xx_spi *dws = buf_id;
	unsigned long flags;

	spin_lock_irqsave(&dws->lock, flags);

	if (res == RK29_RES_OK)
		dws->state &= ~RXBUSY;
	else
		dev_err(&dws->master->dev, "DmaAbrtRx-%d, size: %d\n", res, size);

	/* If the other done */
	if (!(dws->state & TXBUSY))
		complete(&dws->xfer_completion);

	spin_unlock_irqrestore(&dws->lock, flags);
}

static void rk29_spi_dma_txcb(void *buf_id,
				 int size, enum rk29_dma_buffresult res)
{
	struct rk29xx_spi *dws = buf_id;
	unsigned long flags;

	DBG("func: %s, line: %d\n", __FUNCTION__, __LINE__);

	spin_lock_irqsave(&dws->lock, flags);

	if (res == RK29_RES_OK)
		dws->state &= ~TXBUSY;
	else
		dev_err(&dws->master->dev, "DmaAbrtTx-%d, size: %d \n", res, size);

	/* If the other done */
	if (!(dws->state & RXBUSY)) 
		complete(&dws->xfer_completion);

	spin_unlock_irqrestore(&dws->lock, flags);
}


static struct rk29_dma_client rk29_spi_dma_client = {
	.name = "rk29xx-spi-dma",
};

static int acquire_dma(struct rk29xx_spi *dws)
{	
	if (dws->dma_inited) {
		return 0;
	}

	if(rk29_dma_request(dws->rx_dmach, 
		&rk29_spi_dma_client, NULL) < 0) {
		dev_err(&dws->master->dev, "dws->rx_dmach : %d, cannot get RxDMA\n", dws->rx_dmach);
		return -1;
	}

	if (rk29_dma_request(dws->tx_dmach,
					&rk29_spi_dma_client, NULL) < 0) {
		dev_err(&dws->master->dev, "dws->tx_dmach : %d, cannot get TxDMA\n", dws->tx_dmach);
		rk29_dma_free(dws->rx_dmach, &rk29_spi_dma_client);
		return -1;
	}
	
    dws->dma_inited = 1;
	return 0;
}

static void release_dma(struct rk29xx_spi *dws)
{
	if(!dws && dws->dma_inited) {
		rk29_dma_free(dws->rx_dmach, &rk29_spi_dma_client);
		rk29_dma_free(dws->tx_dmach, &rk29_spi_dma_client);
	}
}

/*
 * Note: first step is the protocol driver prepares
 * a dma-capable memory, and this func just need translate
 * the virt addr to physical
 */
static int map_dma_buffers(struct rk29xx_spi *dws)
{
	if (!dws->cur_msg->is_dma_mapped || !dws->dma_inited
		|| !dws->cur_chip->enable_dma)
		return -1;

	if (dws->cur_transfer->tx_dma) {
		dws->tx_dma = dws->cur_transfer->tx_dma;
		if (rk29_dma_set_buffdone_fn(dws->tx_dmach, rk29_spi_dma_txcb)) {
			dev_err(&dws->master->dev, "rk29_dma_set_buffdone_fn fail\n");
			return -1;
		}
		if (rk29_dma_devconfig(dws->tx_dmach, RK29_DMASRC_MEM,
					dws->sfr_start + SPIM_TXDR)) {
			dev_err(&dws->master->dev, "rk29_dma_devconfig fail\n");
			return -1;
		}
	}

	if (dws->cur_transfer->rx_dma) {
		dws->rx_dma = dws->cur_transfer->rx_dma;
		if (rk29_dma_set_buffdone_fn(dws->rx_dmach, rk29_spi_dma_rxcb)) {
			dev_err(&dws->master->dev, "rk29_dma_set_buffdone_fn fail\n");
			return -1;
		}
		if (rk29_dma_devconfig(dws->rx_dmach, RK29_DMASRC_HW,
					dws->sfr_start + SPIM_RXDR)) {
			dev_err(&dws->master->dev, "rk29_dma_devconfig fail\n");
			return -1;
		}
	}

	return 0;
}

/* Caller already set message->status; dma and pio irqs are blocked */
static void giveback(struct rk29xx_spi *dws)
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
		dws->cs_control(dws,msg->spi->chip_select, MRST_SPI_DEASSERT);

	msg->state = NULL;
	if (msg->complete)
		msg->complete(msg->context);
}

static void int_error_stop(struct rk29xx_spi *dws, const char *msg)
{
	/* Stop and reset hw */
	flush(dws);
	spi_enable_chip(dws, 0);

	dev_err(&dws->master->dev, "%s\n", msg);
	dws->cur_msg->state = ERROR_STATE;
	tasklet_schedule(&dws->pump_transfers);
}

static void transfer_complete(struct rk29xx_spi *dws)
{
	/* Update total byte transfered return count actual bytes read */
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

static irqreturn_t interrupt_transfer(struct rk29xx_spi *dws)
{
	u16 irq_status, irq_mask = 0x1f;
	u32 int_level = dws->fifo_len / 2;
	u32 left;
	
	irq_status = rk29xx_readw(dws, SPIM_ISR) & irq_mask;
	/* Error handling */
	if (irq_status & (SPI_INT_TXOI | SPI_INT_RXOI | SPI_INT_RXUI)) {
		rk29xx_writew(dws, SPIM_ICR, SPI_CLEAR_INT_TXOI | SPI_CLEAR_INT_RXOI | SPI_CLEAR_INT_RXUI);
		int_error_stop(dws, "interrupt_transfer: fifo overrun");
		return IRQ_HANDLED;
	}

	if (irq_status & SPI_INT_TXEI) {
		spi_mask_intr(dws, SPI_INT_TXEI);

		left = (dws->tx_end - dws->tx) / dws->n_bytes;
		left = (left > int_level) ? int_level : left;

		while (left--) {
			dws->write(dws);
			wait_till_not_busy(dws);
		}
		if (dws->rx) {
		    dws->read(dws);
		}

		/* Re-enable the IRQ if there is still data left to tx */
		if (dws->tx_end > dws->tx)
			spi_umask_intr(dws, SPI_INT_TXEI);
		else
			transfer_complete(dws);
	}

	if (irq_status & SPI_INT_RXFI) {
		spi_mask_intr(dws, SPI_INT_RXFI);
		
		dws->read(dws);

		/* Re-enable the IRQ if there is still data left to rx */
		if (dws->rx_end > dws->rx) {
			left = ((dws->rx_end - dws->rx) / dws->n_bytes) - 1;
		    left = (left > int_level) ? int_level : left;

			rk29xx_writew(dws, SPIM_RXFTLR, left);
			spi_umask_intr(dws, SPI_INT_RXFI);
		}
		else {
			transfer_complete(dws);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t rk29xx_spi_irq(int irq, void *dev_id)
{
	struct rk29xx_spi *dws = dev_id;

	if (!dws->cur_msg) {
		spi_mask_intr(dws, SPI_INT_TXEI);
		/* Never fail */
		return IRQ_HANDLED;
	}

	return dws->transfer_handler(dws);
}

/* Must be called inside pump_transfers() */
static void poll_transfer(struct rk29xx_spi *dws)
{
	while (dws->write(dws)) {
		wait_till_not_busy(dws);
		dws->read(dws);
	}
	transfer_complete(dws);
}
static void spi_chip_sel(struct rk29xx_spi *dws, u16 cs)
{
    if(cs >= dws->master->num_chipselect)
		return;

	if (dws->cs_control){
	    dws->cs_control(dws, cs, MRST_SPI_ASSERT);
	}
	rk29xx_writel(dws, SPIM_SER, 1 << cs);
}

static void pump_transfers(unsigned long data)
{
	struct rk29xx_spi *dws = (struct rk29xx_spi *)data;
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct spi_device *spi = NULL;
	struct chip_data *chip = NULL;
	u8 bits = 0;
	u8 spi_dfs = 0;
	u8 imask = 0;
	u8 cs_change = 0;
	u16 txint_level = 0;
	u16 rxint_level = 0;
	u16 clk_div = 0;
	u32 speed = 0;
	u32 cr0 = 0;

	DBG(KERN_INFO "pump_transfers\n");

	/* Get current state information */
	message = dws->cur_msg;
	transfer = dws->cur_transfer;
	chip = dws->cur_chip;
	spi = message->spi;	
	if (unlikely(!chip->clk_div))
		chip->clk_div = clk_get_rate(dws->clock_spim) / chip->speed_hz;	
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
	dws->write = dws->tx ? chip->write : null_writer;
	dws->read = dws->rx ? chip->read : null_reader;
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
			if (speed > clk_get_rate(dws->clock_spim)) {
				dev_err(&dws->master->dev, "MRST SPI0: unsupported"
					"freq: %dHz\n", speed);
				message->status = -EIO;
				goto early_exit;
			}

			/* clk_div doesn't support odd number */
			clk_div = clk_get_rate(dws->clock_spim) / speed;
			clk_div = (clk_div + 1) & 0xfffe;

			chip->speed_hz = speed;
			chip->clk_div = clk_div;
		}
	}
	
	if (transfer->bits_per_word) {
		bits = transfer->bits_per_word;

		switch (bits) {
		case 8:
			dws->n_bytes = 1;
			dws->dma_width = 1;
			dws->read = (dws->read != null_reader) ?
					u8_reader : null_reader;
			dws->write = (dws->write != null_writer) ?
					u8_writer : null_writer;
			spi_dfs = SPI_DFS_8BIT;
			break;
		case 16:
			dws->n_bytes = 2;
			dws->dma_width = 2;
			dws->read = (dws->read != null_reader) ?
					u16_reader : null_reader;
			dws->write = (dws->write != null_writer) ?
					u16_writer : null_writer;
			spi_dfs = SPI_DFS_16BIT;
			break;
		default:
			dev_err(&dws->master->dev, "MRST SPI0: unsupported bits:"
				"%db\n", bits);
			message->status = -EIO;
			goto early_exit;
		}

		cr0 = (spi_dfs << SPI_DFS_OFFSET)
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
		cr0 |= (chip->tmode << SPI_TMOD_OFFSET);
	} 

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
			imask |= SPI_INT_TXEI;
		}
		dws->transfer_handler = interrupt_transfer;
	}

	/*
	 * Reprogram registers only if
	 *	1. chip select changes
	 *	2. clk_div is changed
	 *	3. control value changes
	 */
	if (rk29xx_readw(dws, SPIM_CTRLR0) != cr0 || cs_change || clk_div || imask) {
		spi_enable_chip(dws, 0);
		if (rk29xx_readw(dws, SPIM_CTRLR0) != cr0)
			rk29xx_writew(dws, SPIM_CTRLR0, cr0);

		spi_set_clk(dws, clk_div ? clk_div : chip->clk_div);		
		spi_chip_sel(dws, spi->chip_select);

        rk29xx_writew(dws, SPIM_CTRLR1, dws->len-1);
		spi_enable_chip(dws, 1);

		if (txint_level)
			rk29xx_writew(dws, SPIM_TXFTLR, txint_level);
		if (rxint_level)
			rk29xx_writew(dws, SPIM_RXFTLR, rxint_level);
		/* Set the interrupt mask, for poll mode just diable all int */
		spi_mask_intr(dws, 0xff);
		if (imask)
			spi_umask_intr(dws, imask);
		
		if (cs_change)
			dws->prev_chip = chip;
	} 

	if (chip->poll_mode)
		poll_transfer(dws);

	return;

early_exit:
	giveback(dws);
	return;
}

static void dma_transfer(struct rk29xx_spi *dws) //int cs_change)
{
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct spi_device *spi = NULL;
	struct chip_data *chip = NULL;
	unsigned long val;
	int ms;
	int iRet;
	int burst;
	u8 bits = 0;
	u8 spi_dfs = 0;
	u8 cs_change = 0;
	u16 clk_div = 0;
	u32 speed = 0;
	u32 cr0 = 0;
	u32 dmacr = 0;

	DBG(KERN_INFO "dma_transfer\n");

	if (acquire_dma(dws)) {
		dev_err(&dws->master->dev, "acquire dma failed\n");
		goto err_out;
	}

	if (map_dma_buffers(dws)) {
		dev_err(&dws->master->dev, "acquire dma failed\n");
		goto err_out;
	}

	/* Get current state information */
	message = dws->cur_msg;
	transfer = dws->cur_transfer;
	chip = dws->cur_chip;
	spi = message->spi;	
	if (unlikely(!chip->clk_div))
		chip->clk_div = clk_get_rate(dws->clock_spim) / chip->speed_hz;	
	if (message->state == ERROR_STATE) {
		message->status = -EIO;
		goto err_out;
	}

	/* Handle end of message */
	if (message->state == DONE_STATE) {
		message->status = 0;
		goto err_out;
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
	dws->write = dws->tx ? chip->write : null_writer;
	dws->read = dws->rx ? chip->read : null_reader;
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
			if (speed > clk_get_rate(dws->clock_spim)) {
				dev_err(&dws->master->dev, "MRST SPI0: unsupported"
					"freq: %dHz\n", speed);
				message->status = -EIO;
				goto err_out;
			}

			/* clk_div doesn't support odd number */
			clk_div = clk_get_rate(dws->clock_spim) / speed;
			clk_div = (clk_div + 1) & 0xfffe;

			chip->speed_hz = speed;
			chip->clk_div = clk_div;
		}
	}

	if (transfer->bits_per_word) {
		bits = transfer->bits_per_word;

		switch (bits) {
		case 8:
			dws->n_bytes = 1;
			dws->dma_width = 1;
			spi_dfs = SPI_DFS_8BIT;
			break;
		case 16:
			dws->n_bytes = 2;
			dws->dma_width = 2;
			spi_dfs = SPI_DFS_16BIT;
			break;
		default:
			dev_err(&dws->master->dev, "MRST SPI0: unsupported bits:"
				"%db\n", bits);
			message->status = -EIO;
			goto err_out;
		}

		cr0 = (spi_dfs << SPI_DFS_OFFSET)
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
		cr0 |= (chip->tmode << SPI_TMOD_OFFSET);
	}

	/*
	 * Reprogram registers only if
	 *	1. chip select changes
	 *	2. clk_div is changed
	 *	3. control value changes
	 */
	if (rk29xx_readw(dws, SPIM_CTRLR0) != cr0 || cs_change || clk_div) {
		spi_enable_chip(dws, 0);
		if (rk29xx_readw(dws, SPIM_CTRLR0) != cr0) {
			rk29xx_writew(dws, SPIM_CTRLR0, cr0);
		}

		spi_set_clk(dws, clk_div ? clk_div : chip->clk_div);		
		spi_chip_sel(dws, spi->chip_select);
		/* Set the interrupt mask, for poll mode just diable all int */
		spi_mask_intr(dws, 0xff);
		
		if (transfer->tx_buf != NULL) {
			dmacr |= SPI_DMACR_TX_ENABLE;
			rk29xx_writew(dws, SPIM_DMATDLR, 0);
		}
		if (transfer->rx_buf != NULL) {
			dmacr |= SPI_DMACR_RX_ENABLE;
			rk29xx_writew(dws, SPIM_DMARDLR, 0);
			rk29xx_writew(dws, SPIM_CTRLR1, transfer->len-1);
		}
		rk29xx_writew(dws, SPIM_DMACR, dmacr);
		spi_enable_chip(dws, 1);
		if (cs_change)
			dws->prev_chip = chip;
	} 

	INIT_COMPLETION(dws->xfer_completion);

	spi_dump_regs(dws);
	DBG("dws->tx_dmach: %d, dws->rx_dmach: %d, transfer->tx_dma: 0x%x\n", dws->tx_dmach, dws->rx_dmach, (unsigned int)transfer->tx_dma);
	if (transfer->tx_buf != NULL) {
		dws->state |= TXBUSY;
		/*if (transfer->len & 0x3) {
			burst = 1;
		}
		else {
			burst = 4;
		}
		if (rk29_dma_config(dws->tx_dmach, burst)) {*/
		if (rk29_dma_config(dws->tx_dmach, 1, 1)) {//there is not dma burst but bitwide, set it 1 alwayss
			dev_err(&dws->master->dev, "function: %s, line: %d\n", __FUNCTION__, __LINE__);
			goto err_out;
		}
		
		rk29_dma_ctrl(dws->tx_dmach, RK29_DMAOP_FLUSH);	
		
		iRet = rk29_dma_enqueue(dws->tx_dmach, (void *)dws,
					transfer->tx_dma, transfer->len);
		if (iRet) {
			dev_err(&dws->master->dev, "function: %s, line: %d, iRet: %d(dws->tx_dmach: %d, transfer->tx_dma: 0x%x)\n", __FUNCTION__, __LINE__, iRet, 
				dws->tx_dmach, (unsigned int)transfer->tx_dma);
			goto err_out;
		}
		
		if (rk29_dma_ctrl(dws->tx_dmach, RK29_DMAOP_START)) {
			dev_err(&dws->master->dev, "function: %s, line: %d\n", __FUNCTION__, __LINE__);
			goto err_out;
		}
	}

	wait_till_not_busy(dws);

	if (transfer->rx_buf != NULL) {
		dws->state |= RXBUSY;
		if (rk29_dma_config(dws->rx_dmach, 1, 1)) {
			dev_err(&dws->master->dev, "function: %s, line: %d\n", __FUNCTION__, __LINE__);
			goto err_out;
		}

		rk29_dma_ctrl(dws->rx_dmach, RK29_DMAOP_FLUSH);	
		
		iRet = rk29_dma_enqueue(dws->rx_dmach, (void *)dws,
					transfer->rx_dma, transfer->len);
		if (iRet) {
			dev_err(&dws->master->dev, "function: %s, line: %d\n", __FUNCTION__, __LINE__);
			goto err_out;
		}
		
		if (rk29_dma_ctrl(dws->rx_dmach, RK29_DMAOP_START)) {
			dev_err(&dws->master->dev, "function: %s, line: %d\n", __FUNCTION__, __LINE__);
			goto err_out;
		}
	}

	/* millisecs to xfer 'len' bytes @ 'cur_speed' */
	ms = transfer->len * 8 / dws->cur_chip->speed_hz;
	ms += 10; 

	val = msecs_to_jiffies(ms) + 10;
	if (!wait_for_completion_timeout(&dws->xfer_completion, val)) {
		if (transfer->rx_buf != NULL && (dws->state & RXBUSY)) {
			rk29_dma_ctrl(dws->rx_dmach, RK29_DMAOP_FLUSH);
			dws->state &= ~RXBUSY;
			dev_err(&dws->master->dev, "function: %s, line: %d\n", __FUNCTION__, __LINE__);
			goto NEXT_TRANSFER;
		}
		if (transfer->tx_buf != NULL && (dws->state & TXBUSY)) {
			rk29_dma_ctrl(dws->tx_dmach, RK29_DMAOP_FLUSH);
			dws->state &= ~TXBUSY;
			dev_err(&dws->master->dev, "function: %s, line: %d\n", __FUNCTION__, __LINE__);
			goto NEXT_TRANSFER;
		}
	}

	wait_till_not_busy(dws);

NEXT_TRANSFER:
	/* Update total byte transfered return count actual bytes read */
	dws->cur_msg->actual_length += dws->len;

	/* Move to next transfer */
	dws->cur_msg->state = next_transfer(dws);

	/* Handle end of message */
	if (dws->cur_msg->state == DONE_STATE) {
		dws->cur_msg->status = 0;
		giveback(dws);
	} else
		dma_transfer(dws);
	
	return;

err_out:
	giveback(dws);
	return;

}

static void pump_messages(struct work_struct *work)
{
	struct rk29xx_spi *dws =
		container_of(work, struct rk29xx_spi, pump_messages);
	unsigned long flags;

	DBG(KERN_INFO "pump_messages\n");

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
    dws->prev_chip = NULL; //每个pump message时强制更新cs dxj
    
	/* Mark as busy and launch transfers */
	if(dws->cur_msg->is_dma_mapped /*&& dws->cur_transfer->len > DMA_MIN_BYTES*/) {
		dws->busy = 1;
	    spin_unlock_irqrestore(&dws->lock, flags);
		dma_transfer(dws);
		return;
	}
	else {
		tasklet_schedule(&dws->pump_transfers);
	}

	dws->busy = 1;
	spin_unlock_irqrestore(&dws->lock, flags);
}

#if defined(QUICK_TRANSFER)
static void do_read(struct rk29xx_spi *dws)
{
	int count = 0;

	spi_enable_chip(dws, 0);
	rk29xx_writew(dws, SPIM_CTRLR1, dws->rx_end-dws->rx-1);
	spi_enable_chip(dws, 1);		
	rk29xx_writew(dws, SPIM_TXDR, 0);
	while (1) {
		if (dws->read(dws))
			break;
		if (count++ == 0x20) {
			dev_err(&dws->master->dev, "+++++++++++spi receive data time out+++++++++++++\n");
			break;
		}
		
	}
}

static void do_write(struct rk29xx_spi *dws)
{
	while (dws->tx<dws->tx_end) {
		dws->write(dws);
	}
}

/* Caller already set message->status; dma and pio irqs are blocked */
static void msg_giveback(struct rk29xx_spi *dws)
{
	struct spi_transfer *last_transfer;
	struct spi_message *msg;

	DBG("+++++++++++++++enter %s++++++++++++++++++\n", __func__);

	msg = dws->cur_msg;
	dws->cur_msg = NULL;
	dws->cur_transfer = NULL;
	dws->prev_chip = dws->cur_chip;
	dws->cur_chip = NULL;
	dws->dma_mapped = 0;
	dws->busy = 0;

	last_transfer = list_entry(msg->transfers.prev,
					struct spi_transfer,
					transfer_list);

	if (!last_transfer->cs_change && dws->cs_control)
		dws->cs_control(dws,msg->spi->chip_select,MRST_SPI_DEASSERT);

	msg->state = NULL;	
}

/* Must be called inside pump_transfers() */
static int do_full_transfer(struct rk29xx_spi *dws)
{
	if ((dws->read(dws))) {
		goto comple;
	}
	
	while (dws->tx<dws->tx_end){
		dws->write(dws);		
		dws->read(dws);
	}
	
	if (dws->rx < dws->rx_end) {
		dws->read(dws);
	}

comple:
	
	dws->cur_msg->actual_length += dws->len;
	
	/* Move to next transfer */
	dws->cur_msg->state = next_transfer(dws);
					
	if (dws->cur_msg->state == DONE_STATE) {
		dws->cur_msg->status = 0;
		//msg_giveback(dws);
		return 0;
	}
	else {
		return -1;
	}
	
}


/* Must be called inside pump_transfers() */
static int do_half_transfer(struct rk29xx_spi *dws)
{
	if (dws->rx) {
		if (dws->tx) {
			do_write(dws);
		}
		wait_till_tf_empty(dws);
		wait_till_not_busy(dws);
		do_read(dws);
	}
	else {
		do_write(dws);
		wait_till_tf_empty(dws);
		wait_till_not_busy(dws);
	}
	
	dws->cur_msg->actual_length += dws->len;
	
	/* Move to next transfer */
	dws->cur_msg->state = next_transfer(dws);
					
	if (dws->cur_msg->state == DONE_STATE) {
		dws->cur_msg->status = 0;
		//msg_giveback(dws);
		return 0;
	}
	else {
		return -1;
	}
}


static int rk29xx_pump_transfers(struct rk29xx_spi *dws, int mode)
{
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct spi_device *spi = NULL;
	struct chip_data *chip = NULL;
	u8 bits = 0;
	u8 spi_dfs = 0;
	u8 cs_change = 0;
	u16 clk_div = 0;
	u32 speed = 0;
	u32 cr0 = 0;
	u32 dmacr = 0;
	
	DBG(KERN_INFO "+++++++++++++++enter %s++++++++++++++++++\n", __func__);

	/* Get current state information */
	message = dws->cur_msg;
	transfer = dws->cur_transfer;
	chip = dws->cur_chip;
	spi = message->spi;	

	if (unlikely(!chip->clk_div))
		chip->clk_div = clk_get_rate(dws->clock_spim) / chip->speed_hz;
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
	dws->write = dws->tx ? chip->write : null_writer;
	dws->read = dws->rx ? chip->read : null_reader;
	if (dws->rx && dws->tx) {
		int temp_len = transfer->len;
		int len;
		unsigned char *tx_buf;
		for (len=0; *tx_buf++ != 0; len++);
		dws->tx_end = dws->tx + len;
		dws->rx_end = dws->rx + temp_len - len;
	}
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
			if (speed > clk_get_rate(dws->clock_spim)) {
				dev_err(&dws->master->dev, "MRST SPI0: unsupported"
					"freq: %dHz\n", speed);
				message->status = -EIO;
				goto early_exit;
			}

			/* clk_div doesn't support odd number */
			clk_div = clk_get_rate(dws->clock_spim) / speed;
			clk_div = (clk_div + 1) & 0xfffe;

			chip->speed_hz = speed;
			chip->clk_div = clk_div;
		}
	}
	if (transfer->bits_per_word) {
		bits = transfer->bits_per_word;

		switch (bits) {
		case 8:
			dws->n_bytes = 1;
			dws->dma_width = 1;
			dws->read = (dws->read != null_reader) ?
					u8_reader : null_reader;
			dws->write = (dws->write != null_writer) ?
					u8_writer : null_writer;
			spi_dfs = SPI_DFS_8BIT;
			break;
		case 16:
			dws->n_bytes = 2;
			dws->dma_width = 2;
			dws->read = (dws->read != null_reader) ?
					u16_reader : null_reader;
			dws->write = (dws->write != null_writer) ?
					u16_writer : null_writer;
			spi_dfs = SPI_DFS_16BIT;
			break;
		default:
			dev_err(&dws->master->dev, "MRST SPI0: unsupported bits:"
				"%db\n", bits);
			message->status = -EIO;
			goto early_exit;
		}

		cr0 = (spi_dfs << SPI_DFS_OFFSET)
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
		cr0 |= (chip->tmode << SPI_TMOD_OFFSET);
	}
	
	/* Check if current transfer is a DMA transaction */
	dws->dma_mapped = map_dma_buffers(dws);

	/*
	 * Reprogram registers only if
	 *	1. chip select changes
	 *	2. clk_div is changed
	 *	3. control value changes
	 */
	spi_enable_chip(dws, 0);
	if (rk29xx_readw(dws, SPIM_CTRLR0) != cr0)
		rk29xx_writew(dws, SPIM_CTRLR0, cr0);

    DBG(KERN_INFO "clk_div: 0x%x, chip->clk_div: 0x%x\n", clk_div, chip->clk_div);
	spi_set_clk(dws, clk_div ? clk_div : chip->clk_div);		
	spi_chip_sel(dws, spi->chip_select);		
	rk29xx_writew(dws, SPIM_CTRLR1, 0);//add by lyx
	if(dws->dma_mapped ) {
		dmacr = rk29xx_readw(dws, SPIM_DMACR);
		dmacr = dmacr | SPI_DMACR_TX_ENABLE;
		if (mode) 
			dmacr = dmacr | SPI_DMACR_RX_ENABLE;
		rk29xx_writew(dws, SPIM_DMACR, dmacr);
	}
	spi_enable_chip(dws, 1);
	if (cs_change)
		dws->prev_chip = chip;
	
	if (mode)
		return do_full_transfer(dws);
	else
		return do_half_transfer(dws);	
	
early_exit:
	
	//msg_giveback(dws);
	
	return 0;
}

static void rk29xx_pump_messages(struct rk29xx_spi *dws, int mode)
{
	DBG(KERN_INFO "+++++++++++++++enter %s++++++++++++++++++\n", __func__);
	
	while (!acquire_dma(dws))
			msleep(10);

	if (list_empty(&dws->queue) || dws->run == QUEUE_STOPPED) {
		dws->busy = 0;
		return;
	}

	/* Make sure we are not already running a message */
	if (dws->cur_msg) {
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
    dws->prev_chip = NULL; //每个pump message时强制更新cs dxj
    
	/* Mark as busy and launch transfers */
	dws->busy = 1;

	while (rk29xx_pump_transfers(dws, mode)) ;
}

/* spi_device use this to queue in their spi_msg */
static int rk29xx_spi_quick_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct rk29xx_spi *dws = spi_master_get_devdata(spi->master);
	unsigned long flags;
	struct rk29xx_spi_chip *chip_info = spi->controller_data;
	struct spi_message *mmsg;
	
	DBG(KERN_INFO "+++++++++++++++enter %s++++++++++++++++++\n", __func__);
	
	spin_lock_irqsave(&dws->lock, flags);

	if (dws->run == QUEUE_STOPPED) {
		spin_unlock_irqrestore(&dws->lock, flags);
		return -ESHUTDOWN;
	}

	msg->actual_length = 0;
	msg->status = -EINPROGRESS;
	msg->state = START_STATE;

	list_add_tail(&msg->queue, &dws->queue);

	if (chip_info && (chip_info->transfer_mode == rk29xx_SPI_FULL_DUPLEX)) {
		rk29xx_pump_messages(dws,1);
	}
	else {		
		rk29xx_pump_messages(dws,0);
	}

	mmsg = dws->cur_msg;
	msg_giveback(dws);
	
	spin_unlock_irqrestore(&dws->lock, flags);

	if (mmsg->complete)
		mmsg->complete(mmsg->context);
	
	return 0;
}

#else

/* spi_device use this to queue in their spi_msg */
static int rk29xx_spi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct rk29xx_spi *dws = spi_master_get_devdata(spi->master);
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

#endif

/* This may be called twice for each spi dev */
static int rk29xx_spi_setup(struct spi_device *spi)
{
	struct rk29xx_spi_chip *chip_info = NULL;
	struct chip_data *chip;
	u8 spi_dfs = 0;

	if (spi->bits_per_word != 8 && spi->bits_per_word != 16)
		return -EINVAL;

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (!chip) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;

		chip->cs_control = spi_cs_control;
		chip->enable_dma = 1;  //0;
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
		chip->read = u8_reader;
		chip->write = u8_writer;
		spi_dfs = SPI_DFS_8BIT;
	} else if (spi->bits_per_word == 16) {
		chip->n_bytes = 2;
		chip->dma_width = 2;
		chip->read = u16_reader;
		chip->write = u16_writer;
		spi_dfs = SPI_DFS_16BIT;
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
	chip->cr0 = (spi_dfs << SPI_DFS_OFFSET)
	        | (SPI_HALF_WORLD_OFF << SPI_HALF_WORLD_TX_OFFSET)
			| (SPI_SSN_DELAY_ONE << SPI_SSN_DELAY_OFFSET)
			| (chip->type << SPI_FRF_OFFSET)
			| (spi->mode  << SPI_MODE_OFFSET)
			| (chip->tmode << SPI_TMOD_OFFSET);

	spi_set_ctldata(spi, chip);
	return 0;
}

static void rk29xx_spi_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	kfree(chip);
}

static int __devinit init_queue(struct rk29xx_spi *dws)
{
	INIT_LIST_HEAD(&dws->queue);
	spin_lock_init(&dws->lock);

	dws->run = QUEUE_STOPPED;
	dws->busy = 0;

	init_completion(&dws->xfer_completion);

	tasklet_init(&dws->pump_transfers,
			pump_transfers,	(unsigned long)dws);

	INIT_WORK(&dws->pump_messages, pump_messages);
	dws->workqueue = create_singlethread_workqueue(
					dev_name(dws->master->dev.parent));
	if (dws->workqueue == NULL)
		return -EBUSY;

	return 0;
}

static int start_queue(struct rk29xx_spi *dws)
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

static int stop_queue(struct rk29xx_spi *dws)
{
	unsigned long flags;
	unsigned limit = 50;
	int status = 0;

	spin_lock_irqsave(&dws->lock, flags);
	dws->run = QUEUE_STOPPED;
	while (!list_empty(&dws->queue) && dws->busy && limit--) {
		spin_unlock_irqrestore(&dws->lock, flags);
		msleep(10);
		spin_lock_irqsave(&dws->lock, flags);
	}

	if (!list_empty(&dws->queue) || dws->busy)
		status = -EBUSY;
	spin_unlock_irqrestore(&dws->lock, flags);

	return status;
}

static int destroy_queue(struct rk29xx_spi *dws)
{
	int status;

	status = stop_queue(dws);
	if (status != 0)
		return status;
	destroy_workqueue(dws->workqueue);
	return 0;
}

/* Restart the controller, disable all interrupts, clean rx fifo */
static void spi_hw_init(struct rk29xx_spi *dws)
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
			rk29xx_writew(dws, SPIM_TXFTLR, fifo);
			if (fifo != rk29xx_readw(dws, SPIM_TXFTLR))
				break;
		}

		dws->fifo_len = (fifo == 31) ? 0 : fifo;
		rk29xx_writew(dws, SPIM_TXFTLR, 0);
	}
	
	spi_enable_chip(dws, 1);
	//flush(dws);
}

/* cpufreq driver support */
#ifdef CONFIG_CPU_FREQ

static int rk29xx_spim_cpufreq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
        struct rk29xx_spi *info;
        unsigned long newclk;

        info = container_of(nb, struct rk29xx_spi, freq_transition);
        newclk = clk_get_rate(info->clock_spim);

        return 0;
}

static inline int rk29xx_spim_cpufreq_register(struct rk29xx_spi *info)
{
        info->freq_transition.notifier_call = rk29xx_spim_cpufreq_transition;

        return cpufreq_register_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk29xx_spim_cpufreq_deregister(struct rk29xx_spi *info)
{
        cpufreq_unregister_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int rk29xx_spim_cpufreq_register(struct rk29xx_spi *info)
{
        return 0;
}

static inline void rk29xx_spim_cpufreq_deregister(struct rk29xx_spi *info)
{
}
#endif
static int __init rk29xx_spim_probe(struct platform_device *pdev)
{
	struct resource		*regs, *dmatx_res, *dmarx_res;
	struct rk29xx_spi   *dws;
	struct spi_master   *master;
	int			irq; 
	int ret;
	struct rk29xx_spi_platform_data *pdata = pdev->dev.platform_data;

	if (pdata && pdata->io_init) {
		ret = pdata->io_init(pdata->chipselect_gpios, pdata->num_chipselect);
		if (ret) {			
			return -ENXIO;	
		}
	}
	
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;
	dmatx_res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (dmatx_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI-Tx dma resource\n");
		return -ENXIO;
	}

	dmarx_res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (dmarx_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI-Rx dma resource\n");
		return -ENXIO;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;			
	/* setup spi core then atmel-specific driver state */
	ret = -ENOMEM;	
	master = spi_alloc_master(&pdev->dev, sizeof *dws);
	if (!master) {
		ret = -ENOMEM;
		goto exit;
	}

	platform_set_drvdata(pdev, master);
	dws = spi_master_get_devdata(master);
	dws->clock_spim = clk_get(&pdev->dev, "spi");
	clk_enable(dws->clock_spim);
	if (IS_ERR(dws->clock_spim)) {
		dev_err(&pdev->dev, "clk_get for spi fail(%p)\n", dws->clock_spim);
		return PTR_ERR(dws->clock_spim);
	}
	
	dws->regs = ioremap(regs->start, (regs->end - regs->start) + 1);
	if (!dws->regs){
    	release_mem_region(regs->start, (regs->end - regs->start) + 1);
		return -EBUSY;
	}
	DBG(KERN_INFO "dws->regs: %p\n", dws->regs);
    	dws->irq = irq;
	dws->irq_polarity = IRQF_TRIGGER_NONE;
	dws->master = master;
	dws->type = SSI_MOTO_SPI;
	dws->prev_chip = NULL;
	dws->sfr_start = regs->start;
	dws->tx_dmach = dmatx_res->start;
	dws->rx_dmach = dmarx_res->start;
	dws->dma_inited = 0;  ///0;
	///dws->dma_addr = (dma_addr_t)(dws->paddr + 0x60);
	ret = request_irq(dws->irq, rk29xx_spi_irq, dws->irq_polarity,
			"rk29xx_spim", dws);
	if (ret < 0) {
		dev_err(&master->dev, "can not get IRQ\n");
		goto err_free_master;
	}

	master->mode_bits = SPI_CPOL | SPI_CPHA;
	master->bus_num = pdev->id;
	master->num_chipselect = pdata->num_chipselect;
	master->dev.platform_data = pdata;
	master->cleanup = rk29xx_spi_cleanup;
	master->setup = rk29xx_spi_setup;
	#if defined(QUICK_TRANSFER)
	master->transfer = rk29xx_spi_quick_transfer;
	#else
	master->transfer = rk29xx_spi_transfer;
	#endif
	
	dws->pdev = pdev;
	/* Basic HW init */
	spi_hw_init(dws);
	flush(dws);
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
	ret = spi_register_master(master);
	if (ret) {
		dev_err(&master->dev, "problem registering spi master\n");
		goto err_queue_alloc;
	}

    ret =rk29xx_spim_cpufreq_register(dws);
    if (ret < 0) {
        dev_err(&master->dev, "rk29xx spim failed to init cpufreq support\n");
        goto err_queue_alloc;
    }
	printk(KERN_INFO "rk29xx_spim: driver initialized, fifo_len=%d,bus_num=%d\n", dws->fifo_len,master->bus_num);
	mrst_spi_debugfs_init(dws);
	return 0;

err_queue_alloc:
	destroy_queue(dws);
err_diable_hw:
	spi_enable_chip(dws, 0);
	free_irq(dws->irq, dws);
err_free_master:
	spi_master_put(master);
	iounmap(dws->regs);
exit:
	return ret;
}

static void __exit rk29xx_spim_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct rk29xx_spi *dws = spi_master_get_devdata(master);
	int status = 0;

	if (!dws)
		return;
	rk29xx_spim_cpufreq_deregister(dws);
	mrst_spi_debugfs_remove(dws);

	release_dma(dws);

	/* Remove the queue */
	status = destroy_queue(dws);
	if (status != 0)
		dev_err(&dws->master->dev, "rk29xx_spi_remove: workqueue will not "
			"complete, message memory not freed\n");
	clk_put(dws->clock_spim);
	clk_disable(dws->clock_spim);
	spi_enable_chip(dws, 0);
	/* Disable clk */
	spi_set_clk(dws, 0);
	free_irq(dws->irq, dws);

	/* Disconnect from the SPI framework */
	spi_unregister_master(dws->master);
	iounmap(dws->regs);
}


#ifdef	CONFIG_PM

static int rk29xx_spim_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct rk29xx_spi *dws = spi_master_get_devdata(master);
	struct rk29xx_spi_platform_data *pdata = pdev->dev.platform_data;
	int status;
	
	flush(dws);
	status = stop_queue(dws);
	if (status != 0)
		return status;
	clk_disable(dws->clock_spim);
	if (pdata && pdata->io_fix_leakage_bug)
 	{
		pdata->io_fix_leakage_bug( );
	}
	return 0;
}

static int rk29xx_spim_resume(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct rk29xx_spi *dws = spi_master_get_devdata(master);
	struct rk29xx_spi_platform_data *pdata = pdev->dev.platform_data;
	int ret;
	
	clk_enable(dws->clock_spim);	
	spi_hw_init(dws);
	ret = start_queue(dws);
	if (ret)
		dev_err(&dws->master->dev, "fail to start queue (%d)\n", ret);
	if (pdata && pdata->io_resume_leakage_bug)
 	{
		pdata->io_resume_leakage_bug( ); 
	}
	return ret;
}

#else
#define	rk29xx_spim_suspend	NULL
#define	rk29xx_spim_resume	NULL
#endif

static struct platform_driver rk29xx_platform_spim_driver = {
	.remove		= __exit_p(rk29xx_spim_remove),
	.driver		= {
		.name	= "rk29xx_spim",
		.owner	= THIS_MODULE,
	},
	.suspend	= rk29xx_spim_suspend,
	.resume		= rk29xx_spim_resume,
};

static int __init rk29xx_spim_init(void)
{
	int ret;
	ret = platform_driver_probe(&rk29xx_platform_spim_driver, rk29xx_spim_probe);	
	return ret;
}

static void __exit rk29xx_spim_exit(void)
{
	platform_driver_unregister(&rk29xx_platform_spim_driver);
}

arch_initcall_sync(rk29xx_spim_init);
module_exit(rk29xx_spim_exit);

MODULE_AUTHOR("www.rock-chips.com");
MODULE_DESCRIPTION("Rockchip RK29xx spim port driver");
MODULE_LICENSE("GPL");;

