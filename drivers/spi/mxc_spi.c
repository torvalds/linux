/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 Juergen Beisert
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation
 * 51 Franklin Street, Fifth Floor
 * Boston, MA  02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/types.h>

#include <mach/spi.h>

#define DRIVER_NAME "spi_imx"

#define MXC_CSPIRXDATA		0x00
#define MXC_CSPITXDATA		0x04
#define MXC_CSPICTRL		0x08
#define MXC_CSPIINT		0x0c
#define MXC_RESET		0x1c

/* generic defines to abstract from the different register layouts */
#define MXC_INT_RR	(1 << 0) /* Receive data ready interrupt */
#define MXC_INT_TE	(1 << 1) /* Transmit FIFO empty interrupt */

struct mxc_spi_config {
	unsigned int speed_hz;
	unsigned int bpw;
	unsigned int mode;
	int cs;
};

struct mxc_spi_data {
	struct spi_bitbang bitbang;

	struct completion xfer_done;
	void *base;
	int irq;
	struct clk *clk;
	unsigned long spi_clk;
	int *chipselect;

	unsigned int count;
	void (*tx)(struct mxc_spi_data *);
	void (*rx)(struct mxc_spi_data *);
	void *rx_buf;
	const void *tx_buf;
	unsigned int txfifo; /* number of words pushed in tx FIFO */

	/* SoC specific functions */
	void (*intctrl)(struct mxc_spi_data *, int);
	int (*config)(struct mxc_spi_data *, struct mxc_spi_config *);
	void (*trigger)(struct mxc_spi_data *);
	int (*rx_available)(struct mxc_spi_data *);
};

#define MXC_SPI_BUF_RX(type)						\
static void mxc_spi_buf_rx_##type(struct mxc_spi_data *mxc_spi)		\
{									\
	unsigned int val = readl(mxc_spi->base + MXC_CSPIRXDATA);	\
									\
	if (mxc_spi->rx_buf) {						\
		*(type *)mxc_spi->rx_buf = val;				\
		mxc_spi->rx_buf += sizeof(type);			\
	}								\
}

#define MXC_SPI_BUF_TX(type)						\
static void mxc_spi_buf_tx_##type(struct mxc_spi_data *mxc_spi)		\
{									\
	type val = 0;							\
									\
	if (mxc_spi->tx_buf) {						\
		val = *(type *)mxc_spi->tx_buf;				\
		mxc_spi->tx_buf += sizeof(type);			\
	}								\
									\
	mxc_spi->count -= sizeof(type);					\
									\
	writel(val, mxc_spi->base + MXC_CSPITXDATA);			\
}

MXC_SPI_BUF_RX(u8)
MXC_SPI_BUF_TX(u8)
MXC_SPI_BUF_RX(u16)
MXC_SPI_BUF_TX(u16)
MXC_SPI_BUF_RX(u32)
MXC_SPI_BUF_TX(u32)

/* First entry is reserved, second entry is valid only if SDHC_SPIEN is set
 * (which is currently not the case in this driver)
 */
static int mxc_clkdivs[] = {0, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192,
	256, 384, 512, 768, 1024};

/* MX21, MX27 */
static unsigned int mxc_spi_clkdiv_1(unsigned int fin,
		unsigned int fspi)
{
	int i, max;

	if (cpu_is_mx21())
		max = 18;
	else
		max = 16;

	for (i = 2; i < max; i++)
		if (fspi * mxc_clkdivs[i] >= fin)
			return i;

	return max;
}

/* MX1, MX31, MX35 */
static unsigned int mxc_spi_clkdiv_2(unsigned int fin,
		unsigned int fspi)
{
	int i, div = 4;

	for (i = 0; i < 7; i++) {
		if (fspi * div >= fin)
			return i;
		div <<= 1;
	}

	return 7;
}

#define MX31_INTREG_TEEN	(1 << 0)
#define MX31_INTREG_RREN	(1 << 3)

#define MX31_CSPICTRL_ENABLE	(1 << 0)
#define MX31_CSPICTRL_MASTER	(1 << 1)
#define MX31_CSPICTRL_XCH	(1 << 2)
#define MX31_CSPICTRL_POL	(1 << 4)
#define MX31_CSPICTRL_PHA	(1 << 5)
#define MX31_CSPICTRL_SSCTL	(1 << 6)
#define MX31_CSPICTRL_SSPOL	(1 << 7)
#define MX31_CSPICTRL_BC_SHIFT	8
#define MX35_CSPICTRL_BL_SHIFT	20
#define MX31_CSPICTRL_CS_SHIFT	24
#define MX35_CSPICTRL_CS_SHIFT	12
#define MX31_CSPICTRL_DR_SHIFT	16

#define MX31_CSPISTATUS		0x14
#define MX31_STATUS_RR		(1 << 3)

/* These functions also work for the i.MX35, but be aware that
 * the i.MX35 has a slightly different register layout for bits
 * we do not use here.
 */
static void mx31_intctrl(struct mxc_spi_data *mxc_spi, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX31_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX31_INTREG_RREN;

	writel(val, mxc_spi->base + MXC_CSPIINT);
}

static void mx31_trigger(struct mxc_spi_data *mxc_spi)
{
	unsigned int reg;

	reg = readl(mxc_spi->base + MXC_CSPICTRL);
	reg |= MX31_CSPICTRL_XCH;
	writel(reg, mxc_spi->base + MXC_CSPICTRL);
}

static int mx31_config(struct mxc_spi_data *mxc_spi,
		struct mxc_spi_config *config)
{
	unsigned int reg = MX31_CSPICTRL_ENABLE | MX31_CSPICTRL_MASTER;

	reg |= mxc_spi_clkdiv_2(mxc_spi->spi_clk, config->speed_hz) <<
		MX31_CSPICTRL_DR_SHIFT;

	if (cpu_is_mx31())
		reg |= (config->bpw - 1) << MX31_CSPICTRL_BC_SHIFT;
	else if (cpu_is_mx35()) {
		reg |= (config->bpw - 1) << MX35_CSPICTRL_BL_SHIFT;
		reg |= MX31_CSPICTRL_SSCTL;
	}

	if (config->mode & SPI_CPHA)
		reg |= MX31_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX31_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX31_CSPICTRL_SSPOL;
	if (config->cs < 0) {
		if (cpu_is_mx31())
			reg |= (config->cs + 32) << MX31_CSPICTRL_CS_SHIFT;
		else if (cpu_is_mx35())
			reg |= (config->cs + 32) << MX35_CSPICTRL_CS_SHIFT;
	}

	writel(reg, mxc_spi->base + MXC_CSPICTRL);

	return 0;
}

static int mx31_rx_available(struct mxc_spi_data *mxc_spi)
{
	return readl(mxc_spi->base + MX31_CSPISTATUS) & MX31_STATUS_RR;
}

#define MX27_INTREG_RR		(1 << 4)
#define MX27_INTREG_TEEN	(1 << 9)
#define MX27_INTREG_RREN	(1 << 13)

#define MX27_CSPICTRL_POL	(1 << 5)
#define MX27_CSPICTRL_PHA	(1 << 6)
#define MX27_CSPICTRL_SSPOL	(1 << 8)
#define MX27_CSPICTRL_XCH	(1 << 9)
#define MX27_CSPICTRL_ENABLE	(1 << 10)
#define MX27_CSPICTRL_MASTER	(1 << 11)
#define MX27_CSPICTRL_DR_SHIFT	14
#define MX27_CSPICTRL_CS_SHIFT	19

static void mx27_intctrl(struct mxc_spi_data *mxc_spi, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX27_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX27_INTREG_RREN;

	writel(val, mxc_spi->base + MXC_CSPIINT);
}

static void mx27_trigger(struct mxc_spi_data *mxc_spi)
{
	unsigned int reg;

	reg = readl(mxc_spi->base + MXC_CSPICTRL);
	reg |= MX27_CSPICTRL_XCH;
	writel(reg, mxc_spi->base + MXC_CSPICTRL);
}

static int mx27_config(struct mxc_spi_data *mxc_spi,
		struct mxc_spi_config *config)
{
	unsigned int reg = MX27_CSPICTRL_ENABLE | MX27_CSPICTRL_MASTER;

	reg |= mxc_spi_clkdiv_1(mxc_spi->spi_clk, config->speed_hz) <<
		MX27_CSPICTRL_DR_SHIFT;
	reg |= config->bpw - 1;

	if (config->mode & SPI_CPHA)
		reg |= MX27_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX27_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX27_CSPICTRL_SSPOL;
	if (config->cs < 0)
		reg |= (config->cs + 32) << MX27_CSPICTRL_CS_SHIFT;

	writel(reg, mxc_spi->base + MXC_CSPICTRL);

	return 0;
}

static int mx27_rx_available(struct mxc_spi_data *mxc_spi)
{
	return readl(mxc_spi->base + MXC_CSPIINT) & MX27_INTREG_RR;
}

#define MX1_INTREG_RR		(1 << 3)
#define MX1_INTREG_TEEN		(1 << 8)
#define MX1_INTREG_RREN		(1 << 11)

#define MX1_CSPICTRL_POL	(1 << 4)
#define MX1_CSPICTRL_PHA	(1 << 5)
#define MX1_CSPICTRL_XCH	(1 << 8)
#define MX1_CSPICTRL_ENABLE	(1 << 9)
#define MX1_CSPICTRL_MASTER	(1 << 10)
#define MX1_CSPICTRL_DR_SHIFT	13

static void mx1_intctrl(struct mxc_spi_data *mxc_spi, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX1_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX1_INTREG_RREN;

	writel(val, mxc_spi->base + MXC_CSPIINT);
}

static void mx1_trigger(struct mxc_spi_data *mxc_spi)
{
	unsigned int reg;

	reg = readl(mxc_spi->base + MXC_CSPICTRL);
	reg |= MX1_CSPICTRL_XCH;
	writel(reg, mxc_spi->base + MXC_CSPICTRL);
}

static int mx1_config(struct mxc_spi_data *mxc_spi,
		struct mxc_spi_config *config)
{
	unsigned int reg = MX1_CSPICTRL_ENABLE | MX1_CSPICTRL_MASTER;

	reg |= mxc_spi_clkdiv_2(mxc_spi->spi_clk, config->speed_hz) <<
		MX1_CSPICTRL_DR_SHIFT;
	reg |= config->bpw - 1;

	if (config->mode & SPI_CPHA)
		reg |= MX1_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX1_CSPICTRL_POL;

	writel(reg, mxc_spi->base + MXC_CSPICTRL);

	return 0;
}

static int mx1_rx_available(struct mxc_spi_data *mxc_spi)
{
	return readl(mxc_spi->base + MXC_CSPIINT) & MX1_INTREG_RR;
}

static void mxc_spi_chipselect(struct spi_device *spi, int is_active)
{
	struct mxc_spi_data *mxc_spi = spi_master_get_devdata(spi->master);
	unsigned int cs = 0;
	int gpio = mxc_spi->chipselect[spi->chip_select];
	struct mxc_spi_config config;

	if (spi->mode & SPI_CS_HIGH)
		cs = 1;

	if (is_active == BITBANG_CS_INACTIVE) {
		if (gpio >= 0)
			gpio_set_value(gpio, !cs);
		return;
	}

	config.bpw = spi->bits_per_word;
	config.speed_hz = spi->max_speed_hz;
	config.mode = spi->mode;
	config.cs = mxc_spi->chipselect[spi->chip_select];

	mxc_spi->config(mxc_spi, &config);

	/* Initialize the functions for transfer */
	if (config.bpw <= 8) {
		mxc_spi->rx = mxc_spi_buf_rx_u8;
		mxc_spi->tx = mxc_spi_buf_tx_u8;
	} else if (config.bpw <= 16) {
		mxc_spi->rx = mxc_spi_buf_rx_u16;
		mxc_spi->tx = mxc_spi_buf_tx_u16;
	} else if (config.bpw <= 32) {
		mxc_spi->rx = mxc_spi_buf_rx_u32;
		mxc_spi->tx = mxc_spi_buf_tx_u32;
	} else
		BUG();

	if (gpio >= 0)
		gpio_set_value(gpio, cs);

	return;
}

static void mxc_spi_push(struct mxc_spi_data *mxc_spi)
{
	while (mxc_spi->txfifo < 8) {
		if (!mxc_spi->count)
			break;
		mxc_spi->tx(mxc_spi);
		mxc_spi->txfifo++;
	}

	mxc_spi->trigger(mxc_spi);
}

static irqreturn_t mxc_spi_isr(int irq, void *dev_id)
{
	struct mxc_spi_data *mxc_spi = dev_id;

	while (mxc_spi->rx_available(mxc_spi)) {
		mxc_spi->rx(mxc_spi);
		mxc_spi->txfifo--;
	}

	if (mxc_spi->count) {
		mxc_spi_push(mxc_spi);
		return IRQ_HANDLED;
	}

	if (mxc_spi->txfifo) {
		/* No data left to push, but still waiting for rx data,
		 * enable receive data available interrupt.
		 */
		mxc_spi->intctrl(mxc_spi, MXC_INT_RR);
		return IRQ_HANDLED;
	}

	mxc_spi->intctrl(mxc_spi, 0);
	complete(&mxc_spi->xfer_done);

	return IRQ_HANDLED;
}

static int mxc_spi_setupxfer(struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct mxc_spi_data *mxc_spi = spi_master_get_devdata(spi->master);
	struct mxc_spi_config config;

	config.bpw = t ? t->bits_per_word : spi->bits_per_word;
	config.speed_hz  = t ? t->speed_hz : spi->max_speed_hz;
	config.mode = spi->mode;

	mxc_spi->config(mxc_spi, &config);

	return 0;
}

static int mxc_spi_transfer(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	struct mxc_spi_data *mxc_spi = spi_master_get_devdata(spi->master);

	mxc_spi->tx_buf = transfer->tx_buf;
	mxc_spi->rx_buf = transfer->rx_buf;
	mxc_spi->count = transfer->len;
	mxc_spi->txfifo = 0;

	init_completion(&mxc_spi->xfer_done);

	mxc_spi_push(mxc_spi);

	mxc_spi->intctrl(mxc_spi, MXC_INT_TE);

	wait_for_completion(&mxc_spi->xfer_done);

	return transfer->len;
}

static int mxc_spi_setup(struct spi_device *spi)
{
	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	pr_debug("%s: mode %d, %u bpw, %d hz\n", __func__,
		 spi->mode, spi->bits_per_word, spi->max_speed_hz);

	mxc_spi_chipselect(spi, BITBANG_CS_INACTIVE);

	return 0;
}

static void mxc_spi_cleanup(struct spi_device *spi)
{
}

static int __init mxc_spi_probe(struct platform_device *pdev)
{
	struct spi_imx_master *mxc_platform_info;
	struct spi_master *master;
	struct mxc_spi_data *mxc_spi;
	struct resource *res;
	int i, ret;

	mxc_platform_info = (struct spi_imx_master *)pdev->dev.platform_data;
	if (!mxc_platform_info) {
		dev_err(&pdev->dev, "can't get the platform data\n");
		return -EINVAL;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct mxc_spi_data));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	master->bus_num = pdev->id;
	master->num_chipselect = mxc_platform_info->num_chipselect;

	mxc_spi = spi_master_get_devdata(master);
	mxc_spi->bitbang.master = spi_master_get(master);
	mxc_spi->chipselect = mxc_platform_info->chipselect;

	for (i = 0; i < master->num_chipselect; i++) {
		if (mxc_spi->chipselect[i] < 0)
			continue;
		ret = gpio_request(mxc_spi->chipselect[i], DRIVER_NAME);
		if (ret) {
			i--;
			while (i > 0)
				if (mxc_spi->chipselect[i] >= 0)
					gpio_free(mxc_spi->chipselect[i--]);
			dev_err(&pdev->dev, "can't get cs gpios");
			goto out_master_put;
		}
		gpio_direction_output(mxc_spi->chipselect[i], 1);
	}

	mxc_spi->bitbang.chipselect = mxc_spi_chipselect;
	mxc_spi->bitbang.setup_transfer = mxc_spi_setupxfer;
	mxc_spi->bitbang.txrx_bufs = mxc_spi_transfer;
	mxc_spi->bitbang.master->setup = mxc_spi_setup;
	mxc_spi->bitbang.master->cleanup = mxc_spi_cleanup;

	init_completion(&mxc_spi->xfer_done);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get platform resource\n");
		ret = -ENOMEM;
		goto out_gpio_free;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto out_gpio_free;
	}

	mxc_spi->base = ioremap(res->start, resource_size(res));
	if (!mxc_spi->base) {
		ret = -EINVAL;
		goto out_release_mem;
	}

	mxc_spi->irq = platform_get_irq(pdev, 0);
	if (!mxc_spi->irq) {
		ret = -EINVAL;
		goto out_iounmap;
	}

	ret = request_irq(mxc_spi->irq, mxc_spi_isr, 0, DRIVER_NAME, mxc_spi);
	if (ret) {
		dev_err(&pdev->dev, "can't get irq%d: %d\n", mxc_spi->irq, ret);
		goto out_iounmap;
	}

	if (cpu_is_mx31() || cpu_is_mx35()) {
		mxc_spi->intctrl = mx31_intctrl;
		mxc_spi->config = mx31_config;
		mxc_spi->trigger = mx31_trigger;
		mxc_spi->rx_available = mx31_rx_available;
	} else  if (cpu_is_mx27() || cpu_is_mx21()) {
		mxc_spi->intctrl = mx27_intctrl;
		mxc_spi->config = mx27_config;
		mxc_spi->trigger = mx27_trigger;
		mxc_spi->rx_available = mx27_rx_available;
	} else if (cpu_is_mx1()) {
		mxc_spi->intctrl = mx1_intctrl;
		mxc_spi->config = mx1_config;
		mxc_spi->trigger = mx1_trigger;
		mxc_spi->rx_available = mx1_rx_available;
	} else
		BUG();

	mxc_spi->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(mxc_spi->clk)) {
		dev_err(&pdev->dev, "unable to get clock\n");
		ret = PTR_ERR(mxc_spi->clk);
		goto out_free_irq;
	}

	clk_enable(mxc_spi->clk);
	mxc_spi->spi_clk = clk_get_rate(mxc_spi->clk);

	if (!cpu_is_mx31() || !cpu_is_mx35())
		writel(1, mxc_spi->base + MXC_RESET);

	mxc_spi->intctrl(mxc_spi, 0);

	ret = spi_bitbang_start(&mxc_spi->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "bitbang start failed with %d\n", ret);
		goto out_clk_put;
	}

	dev_info(&pdev->dev, "probed\n");

	return ret;

out_clk_put:
	clk_disable(mxc_spi->clk);
	clk_put(mxc_spi->clk);
out_free_irq:
	free_irq(mxc_spi->irq, mxc_spi);
out_iounmap:
	iounmap(mxc_spi->base);
out_release_mem:
	release_mem_region(res->start, resource_size(res));
out_gpio_free:
	for (i = 0; i < master->num_chipselect; i++)
		if (mxc_spi->chipselect[i] >= 0)
			gpio_free(mxc_spi->chipselect[i]);
out_master_put:
	spi_master_put(master);
	kfree(master);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int __exit mxc_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mxc_spi_data *mxc_spi = spi_master_get_devdata(master);
	int i;

	spi_bitbang_stop(&mxc_spi->bitbang);

	writel(0, mxc_spi->base + MXC_CSPICTRL);
	clk_disable(mxc_spi->clk);
	clk_put(mxc_spi->clk);
	free_irq(mxc_spi->irq, mxc_spi);
	iounmap(mxc_spi->base);

	for (i = 0; i < master->num_chipselect; i++)
		if (mxc_spi->chipselect[i] >= 0)
			gpio_free(mxc_spi->chipselect[i]);

	spi_master_put(master);

	release_mem_region(res->start, resource_size(res));

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver mxc_spi_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   },
	.probe = mxc_spi_probe,
	.remove = __exit_p(mxc_spi_remove),
};

static int __init mxc_spi_init(void)
{
	return platform_driver_register(&mxc_spi_driver);
}

static void __exit mxc_spi_exit(void)
{
	platform_driver_unregister(&mxc_spi_driver);
}

module_init(mxc_spi_init);
module_exit(mxc_spi_exit);

MODULE_DESCRIPTION("SPI Master Controller driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
