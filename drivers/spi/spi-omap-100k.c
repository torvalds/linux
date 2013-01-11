/*
 * OMAP7xx SPI 100k controller driver
 * Author: Fabrice Crohas <fcrohas@gmail.com>
 * from original omap1_mcspi driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation
 * Author:      Samuel Ortiz <samuel.ortiz@nokia.com> and
 *              Juha Yrj�l� <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include <linux/spi/spi.h>

#define OMAP1_SPI100K_MAX_FREQ          48000000

#define ICR_SPITAS      (OMAP7XX_ICR_BASE + 0x12)

#define SPI_SETUP1      0x00
#define SPI_SETUP2      0x02
#define SPI_CTRL        0x04
#define SPI_STATUS      0x06
#define SPI_TX_LSB      0x08
#define SPI_TX_MSB      0x0a
#define SPI_RX_LSB      0x0c
#define SPI_RX_MSB      0x0e

#define SPI_SETUP1_INT_READ_ENABLE      (1UL << 5)
#define SPI_SETUP1_INT_WRITE_ENABLE     (1UL << 4)
#define SPI_SETUP1_CLOCK_DIVISOR(x)     ((x) << 1)
#define SPI_SETUP1_CLOCK_ENABLE         (1UL << 0)

#define SPI_SETUP2_ACTIVE_EDGE_FALLING  (0UL << 0)
#define SPI_SETUP2_ACTIVE_EDGE_RISING   (1UL << 0)
#define SPI_SETUP2_NEGATIVE_LEVEL       (0UL << 5)
#define SPI_SETUP2_POSITIVE_LEVEL       (1UL << 5)
#define SPI_SETUP2_LEVEL_TRIGGER        (0UL << 10)
#define SPI_SETUP2_EDGE_TRIGGER         (1UL << 10)

#define SPI_CTRL_SEN(x)                 ((x) << 7)
#define SPI_CTRL_WORD_SIZE(x)           (((x) - 1) << 2)
#define SPI_CTRL_WR                     (1UL << 1)
#define SPI_CTRL_RD                     (1UL << 0)

#define SPI_STATUS_WE                   (1UL << 1)
#define SPI_STATUS_RD                   (1UL << 0)

#define WRITE 0
#define READ  1


/* use PIO for small transfers, avoiding DMA setup/teardown overhead and
 * cache operations; better heuristics consider wordsize and bitrate.
 */
#define DMA_MIN_BYTES                   8

#define SPI_RUNNING	0
#define SPI_SHUTDOWN	1

struct omap1_spi100k {
	struct work_struct      work;

	/* lock protects queue and registers */
	spinlock_t              lock;
	struct list_head        msg_queue;
	struct spi_master       *master;
	struct clk              *ick;
	struct clk              *fck;

	/* Virtual base address of the controller */
	void __iomem            *base;

	/* State of the SPI */
	unsigned int		state;
};

struct omap1_spi100k_cs {
	void __iomem            *base;
	int                     word_len;
};

static struct workqueue_struct *omap1_spi100k_wq;

#define MOD_REG_BIT(val, mask, set) do { \
	if (set) \
		val |= mask; \
	else \
		val &= ~mask; \
} while (0)

static void spi100k_enable_clock(struct spi_master *master)
{
	unsigned int val;
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	/* enable SPI */
	val = readw(spi100k->base + SPI_SETUP1);
	val |= SPI_SETUP1_CLOCK_ENABLE;
	writew(val, spi100k->base + SPI_SETUP1);
}

static void spi100k_disable_clock(struct spi_master *master)
{
	unsigned int val;
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	/* disable SPI */
	val = readw(spi100k->base + SPI_SETUP1);
	val &= ~SPI_SETUP1_CLOCK_ENABLE;
	writew(val, spi100k->base + SPI_SETUP1);
}

static void spi100k_write_data(struct spi_master *master, int len, int data)
{
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	/* write 16-bit word, shifting 8-bit data if necessary */
	if (len <= 8) {
		data <<= 8;
		len = 16;
	}

	spi100k_enable_clock(master);
	writew( data , spi100k->base + SPI_TX_MSB);

	writew(SPI_CTRL_SEN(0) |
	       SPI_CTRL_WORD_SIZE(len) |
	       SPI_CTRL_WR,
	       spi100k->base + SPI_CTRL);

	/* Wait for bit ack send change */
	while((readw(spi100k->base + SPI_STATUS) & SPI_STATUS_WE) != SPI_STATUS_WE);
	udelay(1000);

	spi100k_disable_clock(master);
}

static int spi100k_read_data(struct spi_master *master, int len)
{
	int dataH,dataL;
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	/* Always do at least 16 bits */
	if (len <= 8)
		len = 16;

	spi100k_enable_clock(master);
	writew(SPI_CTRL_SEN(0) |
	       SPI_CTRL_WORD_SIZE(len) |
	       SPI_CTRL_RD,
	       spi100k->base + SPI_CTRL);

	while((readw(spi100k->base + SPI_STATUS) & SPI_STATUS_RD) != SPI_STATUS_RD);
	udelay(1000);

	dataL = readw(spi100k->base + SPI_RX_LSB);
	dataH = readw(spi100k->base + SPI_RX_MSB);
	spi100k_disable_clock(master);

	return dataL;
}

static void spi100k_open(struct spi_master *master)
{
	/* get control of SPI */
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	writew(SPI_SETUP1_INT_READ_ENABLE |
	       SPI_SETUP1_INT_WRITE_ENABLE |
	       SPI_SETUP1_CLOCK_DIVISOR(0), spi100k->base + SPI_SETUP1);

	/* configure clock and interrupts */
	writew(SPI_SETUP2_ACTIVE_EDGE_FALLING |
	       SPI_SETUP2_NEGATIVE_LEVEL |
	       SPI_SETUP2_LEVEL_TRIGGER, spi100k->base + SPI_SETUP2);
}

static void omap1_spi100k_force_cs(struct omap1_spi100k *spi100k, int enable)
{
	if (enable)
		writew(0x05fc, spi100k->base + SPI_CTRL);
	else
		writew(0x05fd, spi100k->base + SPI_CTRL);
}

static unsigned
omap1_spi100k_txrx_pio(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct omap1_spi100k    *spi100k;
	struct omap1_spi100k_cs *cs = spi->controller_state;
	unsigned int            count, c;
	int                     word_len;

	spi100k = spi_master_get_devdata(spi->master);
	count = xfer->len;
	c = count;
	word_len = cs->word_len;

	if (word_len <= 8) {
		u8              *rx;
		const u8        *tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c-=1;
			if (xfer->tx_buf != NULL)
				spi100k_write_data(spi->master, word_len, *tx++);
			if (xfer->rx_buf != NULL)
				*rx++ = spi100k_read_data(spi->master, word_len);
		} while(c);
	} else if (word_len <= 16) {
		u16             *rx;
		const u16       *tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c-=2;
			if (xfer->tx_buf != NULL)
				spi100k_write_data(spi->master,word_len, *tx++);
			if (xfer->rx_buf != NULL)
				*rx++ = spi100k_read_data(spi->master,word_len);
		} while(c);
	} else if (word_len <= 32) {
		u32             *rx;
		const u32       *tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c-=4;
			if (xfer->tx_buf != NULL)
				spi100k_write_data(spi->master,word_len, *tx);
			if (xfer->rx_buf != NULL)
				*rx = spi100k_read_data(spi->master,word_len);
		} while(c);
	}
	return count - c;
}

/* called only when no transfer is active to this device */
static int omap1_spi100k_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct omap1_spi100k *spi100k = spi_master_get_devdata(spi->master);
	struct omap1_spi100k_cs *cs = spi->controller_state;
	u8 word_len = spi->bits_per_word;

	if (t != NULL && t->bits_per_word)
		word_len = t->bits_per_word;
	if (!word_len)
		word_len = 8;

	if (spi->bits_per_word > 32)
		return -EINVAL;
	cs->word_len = word_len;

	/* SPI init before transfer */
	writew(0x3e , spi100k->base + SPI_SETUP1);
	writew(0x00 , spi100k->base + SPI_STATUS);
	writew(0x3e , spi100k->base + SPI_CTRL);

	return 0;
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH)

static int omap1_spi100k_setup(struct spi_device *spi)
{
	int                     ret;
	struct omap1_spi100k    *spi100k;
	struct omap1_spi100k_cs *cs = spi->controller_state;

	if (spi->bits_per_word < 4 || spi->bits_per_word > 32) {
		 dev_dbg(&spi->dev, "setup: unsupported %d bit words\n",
			spi->bits_per_word);
		 return -EINVAL;
	}

	spi100k = spi_master_get_devdata(spi->master);

	if (!cs) {
		cs = kzalloc(sizeof *cs, GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		cs->base = spi100k->base + spi->chip_select * 0x14;
		spi->controller_state = cs;
	}

	spi100k_open(spi->master);

	clk_enable(spi100k->ick);
	clk_enable(spi100k->fck);

	ret = omap1_spi100k_setup_transfer(spi, NULL);

	clk_disable(spi100k->ick);
	clk_disable(spi100k->fck);

	return ret;
}

static void omap1_spi100k_work(struct work_struct *work)
{
	struct omap1_spi100k    *spi100k;
	int status = 0;

	spi100k = container_of(work, struct omap1_spi100k, work);
	spin_lock_irq(&spi100k->lock);

	clk_enable(spi100k->ick);
	clk_enable(spi100k->fck);

	/* We only enable one channel at a time -- the one whose message is
	 * at the head of the queue -- although this controller would gladly
	 * arbitrate among multiple channels.  This corresponds to "single
	 * channel" master mode.  As a side effect, we need to manage the
	 * chipselect with the FORCE bit ... CS != channel enable.
	 */
	 while (!list_empty(&spi100k->msg_queue)) {
		struct spi_message              *m;
		struct spi_device               *spi;
		struct spi_transfer             *t = NULL;
		int                             cs_active = 0;
		struct omap1_spi100k_cs         *cs;
		int                             par_override = 0;

		m = container_of(spi100k->msg_queue.next, struct spi_message,
				 queue);

		list_del_init(&m->queue);
		spin_unlock_irq(&spi100k->lock);

		spi = m->spi;
		cs = spi->controller_state;

		list_for_each_entry(t, &m->transfers, transfer_list) {
			if (t->tx_buf == NULL && t->rx_buf == NULL && t->len) {
				status = -EINVAL;
				break;
			}
			if (par_override || t->speed_hz || t->bits_per_word) {
				par_override = 1;
				status = omap1_spi100k_setup_transfer(spi, t);
				if (status < 0)
					break;
				if (!t->speed_hz && !t->bits_per_word)
					par_override = 0;
			}

			if (!cs_active) {
				omap1_spi100k_force_cs(spi100k, 1);
				cs_active = 1;
			}

			if (t->len) {
				unsigned count;

				count = omap1_spi100k_txrx_pio(spi, t);
				m->actual_length += count;

				if (count != t->len) {
					status = -EIO;
					break;
				}
			}

			if (t->delay_usecs)
				udelay(t->delay_usecs);

			/* ignore the "leave it on after last xfer" hint */

			if (t->cs_change) {
				omap1_spi100k_force_cs(spi100k, 0);
				cs_active = 0;
			}
		}

		/* Restore defaults if they were overriden */
		if (par_override) {
			par_override = 0;
			status = omap1_spi100k_setup_transfer(spi, NULL);
		}

		if (cs_active)
			omap1_spi100k_force_cs(spi100k, 0);

		m->status = status;
		m->complete(m->context);

		spin_lock_irq(&spi100k->lock);
	}

	clk_disable(spi100k->ick);
	clk_disable(spi100k->fck);
	spin_unlock_irq(&spi100k->lock);

	if (status < 0)
		printk(KERN_WARNING "spi transfer failed with %d\n", status);
}

static int omap1_spi100k_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct omap1_spi100k    *spi100k;
	unsigned long           flags;
	struct spi_transfer     *t;

	m->actual_length = 0;
	m->status = -EINPROGRESS;

	spi100k = spi_master_get_devdata(spi->master);

	/* Don't accept new work if we're shutting down */
	if (spi100k->state == SPI_SHUTDOWN)
		return -ESHUTDOWN;

	/* reject invalid messages and transfers */
	if (list_empty(&m->transfers) || !m->complete)
		return -EINVAL;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		const void      *tx_buf = t->tx_buf;
		void            *rx_buf = t->rx_buf;
		unsigned        len = t->len;

		if (t->speed_hz > OMAP1_SPI100K_MAX_FREQ
				|| (len && !(rx_buf || tx_buf))
				|| (t->bits_per_word &&
					(  t->bits_per_word < 4
					|| t->bits_per_word > 32))) {
			dev_dbg(&spi->dev, "transfer: %d Hz, %d %s%s, %d bpw\n",
					t->speed_hz,
					len,
					tx_buf ? "tx" : "",
					rx_buf ? "rx" : "",
					t->bits_per_word);
			return -EINVAL;
		}

		if (t->speed_hz && t->speed_hz < OMAP1_SPI100K_MAX_FREQ/(1<<16)) {
			dev_dbg(&spi->dev, "%d Hz max exceeds %d\n",
					t->speed_hz,
					OMAP1_SPI100K_MAX_FREQ/(1<<16));
			return -EINVAL;
		}

	}

	spin_lock_irqsave(&spi100k->lock, flags);
	list_add_tail(&m->queue, &spi100k->msg_queue);
	queue_work(omap1_spi100k_wq, &spi100k->work);
	spin_unlock_irqrestore(&spi100k->lock, flags);

	return 0;
}

static int __init omap1_spi100k_reset(struct omap1_spi100k *spi100k)
{
	return 0;
}

static int omap1_spi100k_probe(struct platform_device *pdev)
{
	struct spi_master       *master;
	struct omap1_spi100k    *spi100k;
	int                     status = 0;

	if (!pdev->id)
		return -EINVAL;

	master = spi_alloc_master(&pdev->dev, sizeof *spi100k);
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
	       master->bus_num = pdev->id;

	master->setup = omap1_spi100k_setup;
	master->transfer = omap1_spi100k_transfer;
	master->cleanup = NULL;
	master->num_chipselect = 2;
	master->mode_bits = MODEBITS;

	dev_set_drvdata(&pdev->dev, master);

	spi100k = spi_master_get_devdata(master);
	spi100k->master = master;

	/*
	 * The memory region base address is taken as the platform_data.
	 * You should allocate this with ioremap() before initializing
	 * the SPI.
	 */
	spi100k->base = (void __iomem *) pdev->dev.platform_data;

	INIT_WORK(&spi100k->work, omap1_spi100k_work);

	spin_lock_init(&spi100k->lock);
	INIT_LIST_HEAD(&spi100k->msg_queue);
	spi100k->ick = clk_get(&pdev->dev, "ick");
	if (IS_ERR(spi100k->ick)) {
		dev_dbg(&pdev->dev, "can't get spi100k_ick\n");
		status = PTR_ERR(spi100k->ick);
		goto err1;
	}

	spi100k->fck = clk_get(&pdev->dev, "fck");
	if (IS_ERR(spi100k->fck)) {
		dev_dbg(&pdev->dev, "can't get spi100k_fck\n");
		status = PTR_ERR(spi100k->fck);
		goto err2;
	}

	if (omap1_spi100k_reset(spi100k) < 0)
		goto err3;

	status = spi_register_master(master);
	if (status < 0)
		goto err3;

	spi100k->state = SPI_RUNNING;

	return status;

err3:
	clk_put(spi100k->fck);
err2:
	clk_put(spi100k->ick);
err1:
	spi_master_put(master);
	return status;
}

static int __exit omap1_spi100k_remove(struct platform_device *pdev)
{
	struct spi_master       *master;
	struct omap1_spi100k    *spi100k;
	struct resource         *r;
	unsigned		limit = 500;
	unsigned long		flags;
	int			status = 0;

	master = dev_get_drvdata(&pdev->dev);
	spi100k = spi_master_get_devdata(master);

	spin_lock_irqsave(&spi100k->lock, flags);

	spi100k->state = SPI_SHUTDOWN;
	while (!list_empty(&spi100k->msg_queue) && limit--) {
		spin_unlock_irqrestore(&spi100k->lock, flags);
		msleep(10);
		spin_lock_irqsave(&spi100k->lock, flags);
	}

	if (!list_empty(&spi100k->msg_queue))
		status = -EBUSY;

	spin_unlock_irqrestore(&spi100k->lock, flags);

	if (status != 0)
		return status;

	clk_put(spi100k->fck);
	clk_put(spi100k->ick);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	spi_unregister_master(master);

	return 0;
}

static struct platform_driver omap1_spi100k_driver = {
	.driver = {
		.name		= "omap1_spi100k",
		.owner		= THIS_MODULE,
	},
	.remove		= __exit_p(omap1_spi100k_remove),
};


static int __init omap1_spi100k_init(void)
{
	omap1_spi100k_wq = create_singlethread_workqueue(
			omap1_spi100k_driver.driver.name);

	if (omap1_spi100k_wq == NULL)
		return -1;

	return platform_driver_probe(&omap1_spi100k_driver, omap1_spi100k_probe);
}

static void __exit omap1_spi100k_exit(void)
{
	platform_driver_unregister(&omap1_spi100k_driver);

	destroy_workqueue(omap1_spi100k_wq);
}

module_init(omap1_spi100k_init);
module_exit(omap1_spi100k_exit);

MODULE_DESCRIPTION("OMAP7xx SPI 100k controller driver");
MODULE_AUTHOR("Fabrice Crohas <fcrohas@gmail.com>");
MODULE_LICENSE("GPL");

