/*
 * Marvell Orion SPI controller driver
 *
 * Author: Shadi Ammouri <shadi@marvell.com>
 * Copyright (C) 2007-2008 Marvell Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/sizes.h>
#include <asm/unaligned.h>

#define DRIVER_NAME			"orion_spi"

/* Runtime PM autosuspend timeout: PM is fairly light on this driver */
#define SPI_AUTOSUSPEND_TIMEOUT		200

#define ORION_NUM_CHIPSELECTS		1 /* only one slave is supported*/
#define ORION_SPI_WAIT_RDY_MAX_LOOP	2000 /* in usec */

#define ORION_SPI_IF_CTRL_REG		0x00
#define ORION_SPI_IF_CONFIG_REG		0x04
#define ORION_SPI_DATA_OUT_REG		0x08
#define ORION_SPI_DATA_IN_REG		0x0c
#define ORION_SPI_INT_CAUSE_REG		0x10

#define ORION_SPI_MODE_CPOL		(1 << 11)
#define ORION_SPI_MODE_CPHA		(1 << 12)
#define ORION_SPI_IF_8_16_BIT_MODE	(1 << 5)
#define ORION_SPI_CLK_PRESCALE_MASK	0x1F
#define ARMADA_SPI_CLK_PRESCALE_MASK	0xDF
#define ORION_SPI_MODE_MASK		(ORION_SPI_MODE_CPOL | \
					 ORION_SPI_MODE_CPHA)

enum orion_spi_type {
	ORION_SPI,
	ARMADA_SPI,
};

struct orion_spi_dev {
	enum orion_spi_type	typ;
	unsigned int		min_divisor;
	unsigned int		max_divisor;
	u32			prescale_mask;
};

struct orion_spi {
	struct spi_master	*master;
	void __iomem		*base;
	struct clk              *clk;
	const struct orion_spi_dev *devdata;
};

static inline void __iomem *spi_reg(struct orion_spi *orion_spi, u32 reg)
{
	return orion_spi->base + reg;
}

static inline void
orion_spi_setbits(struct orion_spi *orion_spi, u32 reg, u32 mask)
{
	void __iomem *reg_addr = spi_reg(orion_spi, reg);
	u32 val;

	val = readl(reg_addr);
	val |= mask;
	writel(val, reg_addr);
}

static inline void
orion_spi_clrbits(struct orion_spi *orion_spi, u32 reg, u32 mask)
{
	void __iomem *reg_addr = spi_reg(orion_spi, reg);
	u32 val;

	val = readl(reg_addr);
	val &= ~mask;
	writel(val, reg_addr);
}

static int orion_spi_baudrate_set(struct spi_device *spi, unsigned int speed)
{
	u32 tclk_hz;
	u32 rate;
	u32 prescale;
	u32 reg;
	struct orion_spi *orion_spi;
	const struct orion_spi_dev *devdata;

	orion_spi = spi_master_get_devdata(spi->master);
	devdata = orion_spi->devdata;

	tclk_hz = clk_get_rate(orion_spi->clk);

	if (devdata->typ == ARMADA_SPI) {
		unsigned int clk, spr, sppr, sppr2, err;
		unsigned int best_spr, best_sppr, best_err;

		best_err = speed;
		best_spr = 0;
		best_sppr = 0;

		/* Iterate over the valid range looking for best fit */
		for (sppr = 0; sppr < 8; sppr++) {
			sppr2 = 0x1 << sppr;

			spr = tclk_hz / sppr2;
			spr = DIV_ROUND_UP(spr, speed);
			if ((spr == 0) || (spr > 15))
				continue;

			clk = tclk_hz / (spr * sppr2);
			err = speed - clk;

			if (err < best_err) {
				best_spr = spr;
				best_sppr = sppr;
				best_err = err;
			}
		}

		if ((best_sppr == 0) && (best_spr == 0))
			return -EINVAL;

		prescale = ((best_sppr & 0x6) << 5) |
			((best_sppr & 0x1) << 4) | best_spr;
	} else {
		/*
		 * the supported rates are: 4,6,8...30
		 * round up as we look for equal or less speed
		 */
		rate = DIV_ROUND_UP(tclk_hz, speed);
		rate = roundup(rate, 2);

		/* check if requested speed is too small */
		if (rate > 30)
			return -EINVAL;

		if (rate < 4)
			rate = 4;

		/* Convert the rate to SPI clock divisor value.	*/
		prescale = 0x10 + rate/2;
	}

	reg = readl(spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));
	reg = ((reg & ~devdata->prescale_mask) | prescale);
	writel(reg, spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));

	return 0;
}

static void
orion_spi_mode_set(struct spi_device *spi)
{
	u32 reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);

	reg = readl(spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));
	reg &= ~ORION_SPI_MODE_MASK;
	if (spi->mode & SPI_CPOL)
		reg |= ORION_SPI_MODE_CPOL;
	if (spi->mode & SPI_CPHA)
		reg |= ORION_SPI_MODE_CPHA;
	writel(reg, spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));
}

/*
 * called only when no transfer is active on the bus
 */
static int
orion_spi_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct orion_spi *orion_spi;
	unsigned int speed = spi->max_speed_hz;
	unsigned int bits_per_word = spi->bits_per_word;
	int	rc;

	orion_spi = spi_master_get_devdata(spi->master);

	if ((t != NULL) && t->speed_hz)
		speed = t->speed_hz;

	if ((t != NULL) && t->bits_per_word)
		bits_per_word = t->bits_per_word;

	orion_spi_mode_set(spi);

	rc = orion_spi_baudrate_set(spi, speed);
	if (rc)
		return rc;

	if (bits_per_word == 16)
		orion_spi_setbits(orion_spi, ORION_SPI_IF_CONFIG_REG,
				  ORION_SPI_IF_8_16_BIT_MODE);
	else
		orion_spi_clrbits(orion_spi, ORION_SPI_IF_CONFIG_REG,
				  ORION_SPI_IF_8_16_BIT_MODE);

	return 0;
}

static void orion_spi_set_cs(struct orion_spi *orion_spi, int enable)
{
	if (enable)
		orion_spi_setbits(orion_spi, ORION_SPI_IF_CTRL_REG, 0x1);
	else
		orion_spi_clrbits(orion_spi, ORION_SPI_IF_CTRL_REG, 0x1);
}

static inline int orion_spi_wait_till_ready(struct orion_spi *orion_spi)
{
	int i;

	for (i = 0; i < ORION_SPI_WAIT_RDY_MAX_LOOP; i++) {
		if (readl(spi_reg(orion_spi, ORION_SPI_INT_CAUSE_REG)))
			return 1;

		udelay(1);
	}

	return -1;
}

static inline int
orion_spi_write_read_8bit(struct spi_device *spi,
			  const u8 **tx_buf, u8 **rx_buf)
{
	void __iomem *tx_reg, *rx_reg, *int_reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);
	tx_reg = spi_reg(orion_spi, ORION_SPI_DATA_OUT_REG);
	rx_reg = spi_reg(orion_spi, ORION_SPI_DATA_IN_REG);
	int_reg = spi_reg(orion_spi, ORION_SPI_INT_CAUSE_REG);

	/* clear the interrupt cause register */
	writel(0x0, int_reg);

	if (tx_buf && *tx_buf)
		writel(*(*tx_buf)++, tx_reg);
	else
		writel(0, tx_reg);

	if (orion_spi_wait_till_ready(orion_spi) < 0) {
		dev_err(&spi->dev, "TXS timed out\n");
		return -1;
	}

	if (rx_buf && *rx_buf)
		*(*rx_buf)++ = readl(rx_reg);

	return 1;
}

static inline int
orion_spi_write_read_16bit(struct spi_device *spi,
			   const u16 **tx_buf, u16 **rx_buf)
{
	void __iomem *tx_reg, *rx_reg, *int_reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);
	tx_reg = spi_reg(orion_spi, ORION_SPI_DATA_OUT_REG);
	rx_reg = spi_reg(orion_spi, ORION_SPI_DATA_IN_REG);
	int_reg = spi_reg(orion_spi, ORION_SPI_INT_CAUSE_REG);

	/* clear the interrupt cause register */
	writel(0x0, int_reg);

	if (tx_buf && *tx_buf)
		writel(__cpu_to_le16(get_unaligned((*tx_buf)++)), tx_reg);
	else
		writel(0, tx_reg);

	if (orion_spi_wait_till_ready(orion_spi) < 0) {
		dev_err(&spi->dev, "TXS timed out\n");
		return -1;
	}

	if (rx_buf && *rx_buf)
		put_unaligned(__le16_to_cpu(readl(rx_reg)), (*rx_buf)++);

	return 1;
}

static unsigned int
orion_spi_write_read(struct spi_device *spi, struct spi_transfer *xfer)
{
	unsigned int count;
	int word_len;

	word_len = spi->bits_per_word;
	count = xfer->len;

	if (word_len == 8) {
		const u8 *tx = xfer->tx_buf;
		u8 *rx = xfer->rx_buf;

		do {
			if (orion_spi_write_read_8bit(spi, &tx, &rx) < 0)
				goto out;
			count--;
		} while (count);
	} else if (word_len == 16) {
		const u16 *tx = xfer->tx_buf;
		u16 *rx = xfer->rx_buf;

		do {
			if (orion_spi_write_read_16bit(spi, &tx, &rx) < 0)
				goto out;
			count -= 2;
		} while (count);
	}

out:
	return xfer->len - count;
}

static int orion_spi_transfer_one_message(struct spi_master *master,
					   struct spi_message *m)
{
	struct orion_spi *orion_spi = spi_master_get_devdata(master);
	struct spi_device *spi = m->spi;
	struct spi_transfer *t = NULL;
	int par_override = 0;
	int status = 0;
	int cs_active = 0;

	/* Load defaults */
	status = orion_spi_setup_transfer(spi, NULL);

	if (status < 0)
		goto msg_done;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (par_override || t->speed_hz || t->bits_per_word) {
			par_override = 1;
			status = orion_spi_setup_transfer(spi, t);
			if (status < 0)
				break;
			if (!t->speed_hz && !t->bits_per_word)
				par_override = 0;
		}

		if (!cs_active) {
			orion_spi_set_cs(orion_spi, 1);
			cs_active = 1;
		}

		if (t->len)
			m->actual_length += orion_spi_write_read(spi, t);

		if (t->delay_usecs)
			udelay(t->delay_usecs);

		if (t->cs_change) {
			orion_spi_set_cs(orion_spi, 0);
			cs_active = 0;
		}
	}

msg_done:
	if (cs_active)
		orion_spi_set_cs(orion_spi, 0);

	m->status = status;
	spi_finalize_current_message(master);

	return 0;
}

static int orion_spi_reset(struct orion_spi *orion_spi)
{
	/* Verify that the CS is deasserted */
	orion_spi_set_cs(orion_spi, 0);

	return 0;
}

static const struct orion_spi_dev orion_spi_dev_data = {
	.typ = ORION_SPI,
	.min_divisor = 4,
	.max_divisor = 30,
	.prescale_mask = ORION_SPI_CLK_PRESCALE_MASK,
};

static const struct orion_spi_dev armada_spi_dev_data = {
	.typ = ARMADA_SPI,
	.min_divisor = 1,
	.max_divisor = 1920,
	.prescale_mask = ARMADA_SPI_CLK_PRESCALE_MASK,
};

static const struct of_device_id orion_spi_of_match_table[] = {
	{ .compatible = "marvell,orion-spi", .data = &orion_spi_dev_data, },
	{ .compatible = "marvell,armada-370-spi", .data = &armada_spi_dev_data, },
	{}
};
MODULE_DEVICE_TABLE(of, orion_spi_of_match_table);

static int orion_spi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	const struct orion_spi_dev *devdata;
	struct spi_master *master;
	struct orion_spi *spi;
	struct resource *r;
	unsigned long tclk_hz;
	int status = 0;

	master = spi_alloc_master(&pdev->dev, sizeof(*spi));
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num = pdev->id;
	if (pdev->dev.of_node) {
		u32 cell_index;

		if (!of_property_read_u32(pdev->dev.of_node, "cell-index",
					  &cell_index))
			master->bus_num = cell_index;
	}

	/* we support only mode 0, and no options */
	master->mode_bits = SPI_CPHA | SPI_CPOL;

	master->transfer_one_message = orion_spi_transfer_one_message;
	master->num_chipselect = ORION_NUM_CHIPSELECTS;
	master->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16);
	master->auto_runtime_pm = true;

	platform_set_drvdata(pdev, master);

	spi = spi_master_get_devdata(master);
	spi->master = master;

	of_id = of_match_device(orion_spi_of_match_table, &pdev->dev);
	devdata = (of_id) ? of_id->data : &orion_spi_dev_data;
	spi->devdata = devdata;

	spi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi->clk)) {
		status = PTR_ERR(spi->clk);
		goto out;
	}

	status = clk_prepare_enable(spi->clk);
	if (status)
		goto out;

	tclk_hz = clk_get_rate(spi->clk);
	master->max_speed_hz = DIV_ROUND_UP(tclk_hz, devdata->min_divisor);
	master->min_speed_hz = DIV_ROUND_UP(tclk_hz, devdata->max_divisor);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(spi->base)) {
		status = PTR_ERR(spi->base);
		goto out_rel_clk;
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, SPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_enable(&pdev->dev);

	status = orion_spi_reset(spi);
	if (status < 0)
		goto out_rel_pm;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	master->dev.of_node = pdev->dev.of_node;
	status = spi_register_master(master);
	if (status < 0)
		goto out_rel_pm;

	return status;

out_rel_pm:
	pm_runtime_disable(&pdev->dev);
out_rel_clk:
	clk_disable_unprepare(spi->clk);
out:
	spi_master_put(master);
	return status;
}


static int orion_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct orion_spi *spi = spi_master_get_devdata(master);

	pm_runtime_get_sync(&pdev->dev);
	clk_disable_unprepare(spi->clk);

	spi_unregister_master(master);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

MODULE_ALIAS("platform:" DRIVER_NAME);

#ifdef CONFIG_PM_RUNTIME
static int orion_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct orion_spi *spi = spi_master_get_devdata(master);

	clk_disable_unprepare(spi->clk);
	return 0;
}

static int orion_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct orion_spi *spi = spi_master_get_devdata(master);

	return clk_prepare_enable(spi->clk);
}
#endif

static const struct dev_pm_ops orion_spi_pm_ops = {
	SET_RUNTIME_PM_OPS(orion_spi_runtime_suspend,
			   orion_spi_runtime_resume,
			   NULL)
};

static struct platform_driver orion_spi_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.pm	= &orion_spi_pm_ops,
		.of_match_table = of_match_ptr(orion_spi_of_match_table),
	},
	.probe		= orion_spi_probe,
	.remove		= orion_spi_remove,
};

module_platform_driver(orion_spi_driver);

MODULE_DESCRIPTION("Orion SPI driver");
MODULE_AUTHOR("Shadi Ammouri <shadi@marvell.com>");
MODULE_LICENSE("GPL");
