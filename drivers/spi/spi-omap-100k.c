// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OMAP7xx SPI 100k controller driver
 * Author: Fabrice Crohas <fcrohas@gmail.com>
 * from original omap1_mcspi driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation
 * Author:      Samuel Ortiz <samuel.ortiz@nokia.com> and
 *              Juha Yrj�l� <juha.yrjola@nokia.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
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

/* use PIO for small transfers, avoiding DMA setup/teardown overhead and
 * cache operations; better heuristics consider wordsize and bitrate.
 */
#define DMA_MIN_BYTES                   8

#define SPI_RUNNING	0
#define SPI_SHUTDOWN	1

struct omap1_spi100k {
	struct clk              *ick;
	struct clk              *fck;

	/* Virtual base address of the controller */
	void __iomem            *base;
};

struct omap1_spi100k_cs {
	void __iomem            *base;
	int                     word_len;
};

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
	writew(data , spi100k->base + SPI_TX_MSB);

	writew(SPI_CTRL_SEN(0) |
	       SPI_CTRL_WORD_SIZE(len) |
	       SPI_CTRL_WR,
	       spi100k->base + SPI_CTRL);

	/* Wait for bit ack send change */
	while ((readw(spi100k->base + SPI_STATUS) & SPI_STATUS_WE) != SPI_STATUS_WE)
		;
	udelay(1000);

	spi100k_disable_clock(master);
}

static int spi100k_read_data(struct spi_master *master, int len)
{
	int dataL;
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	/* Always do at least 16 bits */
	if (len <= 8)
		len = 16;

	spi100k_enable_clock(master);
	writew(SPI_CTRL_SEN(0) |
	       SPI_CTRL_WORD_SIZE(len) |
	       SPI_CTRL_RD,
	       spi100k->base + SPI_CTRL);

	while ((readw(spi100k->base + SPI_STATUS) & SPI_STATUS_RD) != SPI_STATUS_RD)
		;
	udelay(1000);

	dataL = readw(spi100k->base + SPI_RX_LSB);
	readw(spi100k->base + SPI_RX_MSB);
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
	struct omap1_spi100k_cs *cs = spi->controller_state;
	unsigned int            count, c;
	int                     word_len;

	count = xfer->len;
	c = count;
	word_len = cs->word_len;

	if (word_len <= 8) {
		u8              *rx;
		const u8        *tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c -= 1;
			if (xfer->tx_buf != NULL)
				spi100k_write_data(spi->master, word_len, *tx++);
			if (xfer->rx_buf != NULL)
				*rx++ = spi100k_read_data(spi->master, word_len);
		} while (c);
	} else if (word_len <= 16) {
		u16             *rx;
		const u16       *tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c -= 2;
			if (xfer->tx_buf != NULL)
				spi100k_write_data(spi->master, word_len, *tx++);
			if (xfer->rx_buf != NULL)
				*rx++ = spi100k_read_data(spi->master, word_len);
		} while (c);
	} else if (word_len <= 32) {
		u32             *rx;
		const u32       *tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c -= 4;
			if (xfer->tx_buf != NULL)
				spi100k_write_data(spi->master, word_len, *tx);
			if (xfer->rx_buf != NULL)
				*rx = spi100k_read_data(spi->master, word_len);
		} while (c);
	}
	return count - c;
}

/* called only when no transfer is active to this device */
static int omap1_spi100k_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct omap1_spi100k *spi100k = spi_master_get_devdata(spi->master);
	struct omap1_spi100k_cs *cs = spi->controller_state;
	u8 word_len;

	if (t != NULL)
		word_len = t->bits_per_word;
	else
		word_len = spi->bits_per_word;

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

	spi100k = spi_master_get_devdata(spi->master);

	if (!cs) {
		cs = devm_kzalloc(&spi->dev, sizeof(*cs), GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		cs->base = spi100k->base + spi->chip_select * 0x14;
		spi->controller_state = cs;
	}

	spi100k_open(spi->master);

	clk_prepare_enable(spi100k->ick);
	clk_prepare_enable(spi100k->fck);

	ret = omap1_spi100k_setup_transfer(spi, NULL);

	clk_disable_unprepare(spi100k->ick);
	clk_disable_unprepare(spi100k->fck);

	return ret;
}

static int omap1_spi100k_transfer_one_message(struct spi_master *master,
					      struct spi_message *m)
{
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);
	struct spi_device *spi = m->spi;
	struct spi_transfer *t = NULL;
	int cs_active = 0;
	int status = 0;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->tx_buf == NULL && t->rx_buf == NULL && t->len) {
			status = -EINVAL;
			break;
		}
		status = omap1_spi100k_setup_transfer(spi, t);
		if (status < 0)
			break;

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

		spi_transfer_delay_exec(t);

		/* ignore the "leave it on after last xfer" hint */

		if (t->cs_change) {
			omap1_spi100k_force_cs(spi100k, 0);
			cs_active = 0;
		}
	}

	status = omap1_spi100k_setup_transfer(spi, NULL);

	if (cs_active)
		omap1_spi100k_force_cs(spi100k, 0);

	m->status = status;

	spi_finalize_current_message(master);

	return status;
}

static int omap1_spi100k_probe(struct platform_device *pdev)
{
	struct spi_master       *master;
	struct omap1_spi100k    *spi100k;
	int                     status = 0;

	if (!pdev->id)
		return -EINVAL;

	master = spi_alloc_master(&pdev->dev, sizeof(*spi100k));
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num = pdev->id;

	master->setup = omap1_spi100k_setup;
	master->transfer_one_message = omap1_spi100k_transfer_one_message;
	master->num_chipselect = 2;
	master->mode_bits = MODEBITS;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	master->min_speed_hz = OMAP1_SPI100K_MAX_FREQ/(1<<16);
	master->max_speed_hz = OMAP1_SPI100K_MAX_FREQ;
	master->auto_runtime_pm = true;

	spi100k = spi_master_get_devdata(master);

	/*
	 * The memory region base address is taken as the platform_data.
	 * You should allocate this with ioremap() before initializing
	 * the SPI.
	 */
	spi100k->base = (void __iomem *)dev_get_platdata(&pdev->dev);

	spi100k->ick = devm_clk_get(&pdev->dev, "ick");
	if (IS_ERR(spi100k->ick)) {
		dev_dbg(&pdev->dev, "can't get spi100k_ick\n");
		status = PTR_ERR(spi100k->ick);
		goto err;
	}

	spi100k->fck = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(spi100k->fck)) {
		dev_dbg(&pdev->dev, "can't get spi100k_fck\n");
		status = PTR_ERR(spi100k->fck);
		goto err;
	}

	status = clk_prepare_enable(spi100k->ick);
	if (status != 0) {
		dev_err(&pdev->dev, "failed to enable ick: %d\n", status);
		goto err;
	}

	status = clk_prepare_enable(spi100k->fck);
	if (status != 0) {
		dev_err(&pdev->dev, "failed to enable fck: %d\n", status);
		goto err_ick;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);

	status = devm_spi_register_master(&pdev->dev, master);
	if (status < 0)
		goto err_fck;

	return status;

err_fck:
	clk_disable_unprepare(spi100k->fck);
err_ick:
	clk_disable_unprepare(spi100k->ick);
err:
	spi_master_put(master);
	return status;
}

static int omap1_spi100k_remove(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(spi100k->fck);
	clk_disable_unprepare(spi100k->ick);

	return 0;
}

#ifdef CONFIG_PM
static int omap1_spi100k_runtime_suspend(struct device *dev)
{
	struct spi_master *master = spi_master_get(dev_get_drvdata(dev));
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);

	clk_disable_unprepare(spi100k->ick);
	clk_disable_unprepare(spi100k->fck);

	return 0;
}

static int omap1_spi100k_runtime_resume(struct device *dev)
{
	struct spi_master *master = spi_master_get(dev_get_drvdata(dev));
	struct omap1_spi100k *spi100k = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(spi100k->ick);
	if (ret != 0) {
		dev_err(dev, "Failed to enable ick: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(spi100k->fck);
	if (ret != 0) {
		dev_err(dev, "Failed to enable fck: %d\n", ret);
		clk_disable_unprepare(spi100k->ick);
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops omap1_spi100k_pm = {
	SET_RUNTIME_PM_OPS(omap1_spi100k_runtime_suspend,
			   omap1_spi100k_runtime_resume, NULL)
};

static struct platform_driver omap1_spi100k_driver = {
	.driver = {
		.name		= "omap1_spi100k",
		.pm		= &omap1_spi100k_pm,
	},
	.probe		= omap1_spi100k_probe,
	.remove		= omap1_spi100k_remove,
};

module_platform_driver(omap1_spi100k_driver);

MODULE_DESCRIPTION("OMAP7xx SPI 100k controller driver");
MODULE_AUTHOR("Fabrice Crohas <fcrohas@gmail.com>");
MODULE_LICENSE("GPL");
