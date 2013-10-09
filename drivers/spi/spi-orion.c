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

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/sizes.h>
#include <asm/unaligned.h>

#define DRIVER_NAME			"orion_spi"

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
#define ORION_SPI_MODE_MASK		(ORION_SPI_MODE_CPOL | \
					 ORION_SPI_MODE_CPHA)

struct orion_spi {
	struct spi_master	*master;
	void __iomem		*base;
	unsigned int		max_speed;
	unsigned int		min_speed;
	struct clk              *clk;
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

static int orion_spi_set_transfer_size(struct orion_spi *orion_spi, int size)
{
	if (size == 16) {
		orion_spi_setbits(orion_spi, ORION_SPI_IF_CONFIG_REG,
				  ORION_SPI_IF_8_16_BIT_MODE);
	} else if (size == 8) {
		orion_spi_clrbits(orion_spi, ORION_SPI_IF_CONFIG_REG,
				  ORION_SPI_IF_8_16_BIT_MODE);
	} else {
		pr_debug("Bad bits per word value %d (only 8 or 16 are "
			 "allowed).\n", size);
		return -EINVAL;
	}

	return 0;
}

static int orion_spi_baudrate_set(struct spi_device *spi, unsigned int speed)
{
	u32 tclk_hz;
	u32 rate;
	u32 prescale;
	u32 reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);

	tclk_hz = clk_get_rate(orion_spi->clk);

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

	reg = readl(spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));
	reg = ((reg & ~ORION_SPI_CLK_PRESCALE_MASK) | prescale);
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

	return orion_spi_set_transfer_size(orion_spi, bits_per_word);
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
		else
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
	struct orion_spi *orion_spi;
	unsigned int count;
	int word_len;

	orion_spi = spi_master_get_devdata(spi->master);
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
		/* make sure buffer length is even when working in 16
		 * bit mode*/
		if ((t->bits_per_word == 16) && (t->len & 1)) {
			dev_err(&spi->dev,
				"message rejected : "
				"odd data length %d while in 16 bit mode\n",
				t->len);
			status = -EIO;
			goto msg_done;
		}

		if (t->speed_hz && t->speed_hz < orion_spi->min_speed) {
			dev_err(&spi->dev,
				"message rejected : "
				"device min speed (%d Hz) exceeds "
				"required transfer speed (%d Hz)\n",
				orion_spi->min_speed, t->speed_hz);
			status = -EIO;
			goto msg_done;
		}

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

static int orion_spi_setup(struct spi_device *spi)
{
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);

	if ((spi->max_speed_hz == 0)
			|| (spi->max_speed_hz > orion_spi->max_speed))
		spi->max_speed_hz = orion_spi->max_speed;

	if (spi->max_speed_hz < orion_spi->min_speed) {
		dev_err(&spi->dev, "setup: requested speed too low %d Hz\n",
			spi->max_speed_hz);
		return -EINVAL;
	}

	/*
	 * baudrate & width will be set orion_spi_setup_transfer
	 */
	return 0;
}

static int orion_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct orion_spi *spi;
	struct resource *r;
	unsigned long tclk_hz;
	int status = 0;
	const u32 *iprop;
	int size;

	master = spi_alloc_master(&pdev->dev, sizeof *spi);
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num = pdev->id;
	if (pdev->dev.of_node) {
		iprop = of_get_property(pdev->dev.of_node, "cell-index",
					&size);
		if (iprop && size == sizeof(*iprop))
			master->bus_num = *iprop;
	}

	/* we support only mode 0, and no options */
	master->mode_bits = SPI_CPHA | SPI_CPOL;

	master->setup = orion_spi_setup;
	master->transfer_one_message = orion_spi_transfer_one_message;
	master->num_chipselect = ORION_NUM_CHIPSELECTS;

	platform_set_drvdata(pdev, master);

	spi = spi_master_get_devdata(master);
	spi->master = master;

	spi->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi->clk)) {
		status = PTR_ERR(spi->clk);
		goto out;
	}

	clk_prepare(spi->clk);
	clk_enable(spi->clk);
	tclk_hz = clk_get_rate(spi->clk);
	spi->max_speed = DIV_ROUND_UP(tclk_hz, 4);
	spi->min_speed = DIV_ROUND_UP(tclk_hz, 30);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(spi->base)) {
		status = PTR_ERR(spi->base);
		goto out_rel_clk;
	}

	if (orion_spi_reset(spi) < 0)
		goto out_rel_clk;

	master->dev.of_node = pdev->dev.of_node;
	status = devm_spi_register_master(&pdev->dev, master);
	if (status < 0)
		goto out_rel_clk;

	return status;

out_rel_clk:
	clk_disable_unprepare(spi->clk);
	clk_put(spi->clk);
out:
	spi_master_put(master);
	return status;
}


static int orion_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master;
	struct orion_spi *spi;

	master = platform_get_drvdata(pdev);
	spi = spi_master_get_devdata(master);

	clk_disable_unprepare(spi->clk);
	clk_put(spi->clk);

	return 0;
}

MODULE_ALIAS("platform:" DRIVER_NAME);

static const struct of_device_id orion_spi_of_match_table[] = {
	{ .compatible = "marvell,orion-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, orion_spi_of_match_table);

static struct platform_driver orion_spi_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(orion_spi_of_match_table),
	},
	.probe		= orion_spi_probe,
	.remove		= orion_spi_remove,
};

module_platform_driver(orion_spi_driver);

MODULE_DESCRIPTION("Orion SPI driver");
MODULE_AUTHOR("Shadi Ammouri <shadi@marvell.com>");
MODULE_LICENSE("GPL");
