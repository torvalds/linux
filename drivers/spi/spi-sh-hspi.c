/*
 * SuperH HSPI bus driver
 *
 * Copyright (C) 2011  Kuninori Morimoto
 *
 * Based on spi-sh.c:
 * Based on pxa2xx_spi.c:
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Copyright (C) 2005 Stephen Street / StreetFire Sound Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/spi/sh_hspi.h>

#define SPCR	0x00
#define SPSR	0x04
#define SPSCR	0x08
#define SPTBR	0x0C
#define SPRBR	0x10
#define SPCR2	0x14

/* SPSR */
#define RXFL	(1 << 2)

struct hspi_priv {
	void __iomem *addr;
	struct spi_master *master;
	struct device *dev;
	struct clk *clk;
};

/*
 *		basic function
 */
static void hspi_write(struct hspi_priv *hspi, int reg, u32 val)
{
	iowrite32(val, hspi->addr + reg);
}

static u32 hspi_read(struct hspi_priv *hspi, int reg)
{
	return ioread32(hspi->addr + reg);
}

static void hspi_bit_set(struct hspi_priv *hspi, int reg, u32 mask, u32 set)
{
	u32 val = hspi_read(hspi, reg);

	val &= ~mask;
	val |= set & mask;

	hspi_write(hspi, reg, val);
}

/*
 *		transfer function
 */
static int hspi_status_check_timeout(struct hspi_priv *hspi, u32 mask, u32 val)
{
	int t = 256;

	while (t--) {
		if ((mask & hspi_read(hspi, SPSR)) == val)
			return 0;

		udelay(10);
	}

	dev_err(hspi->dev, "timeout\n");
	return -ETIMEDOUT;
}

/*
 *		spi master function
 */

#define hspi_hw_cs_enable(hspi)		hspi_hw_cs_ctrl(hspi, 0)
#define hspi_hw_cs_disable(hspi)	hspi_hw_cs_ctrl(hspi, 1)
static void hspi_hw_cs_ctrl(struct hspi_priv *hspi, int hi)
{
	hspi_bit_set(hspi, SPSCR, (1 << 6), (hi) << 6);
}

static void hspi_hw_setup(struct hspi_priv *hspi,
			  struct spi_message *msg,
			  struct spi_transfer *t)
{
	struct spi_device *spi = msg->spi;
	struct device *dev = hspi->dev;
	u32 spcr, idiv_clk;
	u32 rate, best_rate, min, tmp;

	/*
	 * find best IDIV/CLKCx settings
	 */
	min = ~0;
	best_rate = 0;
	spcr = 0;
	for (idiv_clk = 0x00; idiv_clk <= 0x3F; idiv_clk++) {
		rate = clk_get_rate(hspi->clk);

		/* IDIV calculation */
		if (idiv_clk & (1 << 5))
			rate /= 128;
		else
			rate /= 16;

		/* CLKCx calculation */
		rate /= (((idiv_clk & 0x1F) + 1) * 2);

		/* save best settings */
		tmp = abs(t->speed_hz - rate);
		if (tmp < min) {
			min = tmp;
			spcr = idiv_clk;
			best_rate = rate;
		}
	}

	if (spi->mode & SPI_CPHA)
		spcr |= 1 << 7;
	if (spi->mode & SPI_CPOL)
		spcr |= 1 << 6;

	dev_dbg(dev, "speed %d/%d\n", t->speed_hz, best_rate);

	hspi_write(hspi, SPCR, spcr);
	hspi_write(hspi, SPSR, 0x0);
	hspi_write(hspi, SPSCR, 0x21);	/* master mode / CS control */
}

static int hspi_transfer_one_message(struct spi_master *master,
				     struct spi_message *msg)
{
	struct hspi_priv *hspi = spi_master_get_devdata(master);
	struct spi_transfer *t;
	u32 tx;
	u32 rx;
	int ret, i;
	unsigned int cs_change;
	const int nsecs = 50;

	dev_dbg(hspi->dev, "%s\n", __func__);

	cs_change = 1;
	ret = 0;
	list_for_each_entry(t, &msg->transfers, transfer_list) {

		if (cs_change) {
			hspi_hw_setup(hspi, msg, t);
			hspi_hw_cs_enable(hspi);
			ndelay(nsecs);
		}
		cs_change = t->cs_change;

		for (i = 0; i < t->len; i++) {

			/* wait remains */
			ret = hspi_status_check_timeout(hspi, 0x1, 0);
			if (ret < 0)
				break;

			tx = 0;
			if (t->tx_buf)
				tx = (u32)((u8 *)t->tx_buf)[i];

			hspi_write(hspi, SPTBR, tx);

			/* wait receive */
			ret = hspi_status_check_timeout(hspi, 0x4, 0x4);
			if (ret < 0)
				break;

			rx = hspi_read(hspi, SPRBR);
			if (t->rx_buf)
				((u8 *)t->rx_buf)[i] = (u8)rx;

		}

		msg->actual_length += t->len;

		if (t->delay_usecs)
			udelay(t->delay_usecs);

		if (cs_change) {
			ndelay(nsecs);
			hspi_hw_cs_disable(hspi);
			ndelay(nsecs);
		}
	}

	msg->status = ret;
	if (!cs_change) {
		ndelay(nsecs);
		hspi_hw_cs_disable(hspi);
	}
	spi_finalize_current_message(master);

	return ret;
}

static int hspi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct spi_master *master;
	struct hspi_priv *hspi;
	struct clk *clk;
	int ret;

	/* get base addr */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(*hspi));
	if (!master) {
		dev_err(&pdev->dev, "spi_alloc_master error.\n");
		return -ENOMEM;
	}

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "couldn't get clock\n");
		ret = -EINVAL;
		goto error0;
	}

	hspi = spi_master_get_devdata(master);
	platform_set_drvdata(pdev, hspi);

	/* init hspi */
	hspi->master	= master;
	hspi->dev	= &pdev->dev;
	hspi->clk	= clk;
	hspi->addr	= devm_ioremap(hspi->dev,
				       res->start, resource_size(res));
	if (!hspi->addr) {
		dev_err(&pdev->dev, "ioremap error.\n");
		ret = -ENOMEM;
		goto error1;
	}

	pm_runtime_enable(&pdev->dev);

	master->bus_num		= pdev->id;
	master->mode_bits	= SPI_CPOL | SPI_CPHA;
	master->dev.of_node	= pdev->dev.of_node;
	master->auto_runtime_pm = true;
	master->transfer_one_message		= hspi_transfer_one_message;
	master->bits_per_word_mask = SPI_BPW_MASK(8);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_master error.\n");
		goto error2;
	}

	return 0;

 error2:
	pm_runtime_disable(&pdev->dev);
 error1:
	clk_put(clk);
 error0:
	spi_master_put(master);

	return ret;
}

static int hspi_remove(struct platform_device *pdev)
{
	struct hspi_priv *hspi = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	clk_put(hspi->clk);

	return 0;
}

static const struct of_device_id hspi_of_match[] = {
	{ .compatible = "renesas,hspi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hspi_of_match);

static struct platform_driver hspi_driver = {
	.probe = hspi_probe,
	.remove = hspi_remove,
	.driver = {
		.name = "sh-hspi",
		.of_match_table = hspi_of_match,
	},
};
module_platform_driver(hspi_driver);

MODULE_DESCRIPTION("SuperH HSPI bus driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_ALIAS("platform:sh-hspi");
