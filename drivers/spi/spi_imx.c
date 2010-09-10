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
#include <linux/slab.h>
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

#define MX3_CSPISTAT		0x14
#define MX3_CSPISTAT_RR		(1 << 3)

/* generic defines to abstract from the different register layouts */
#define MXC_INT_RR	(1 << 0) /* Receive data ready interrupt */
#define MXC_INT_TE	(1 << 1) /* Transmit FIFO empty interrupt */

struct spi_imx_config {
	unsigned int speed_hz;
	unsigned int bpw;
	unsigned int mode;
	u8 cs;
};

enum spi_imx_devtype {
	SPI_IMX_VER_IMX1,
	SPI_IMX_VER_0_0,
	SPI_IMX_VER_0_4,
	SPI_IMX_VER_0_5,
	SPI_IMX_VER_0_7,
	SPI_IMX_VER_AUTODETECT,
};

struct spi_imx_data;

struct spi_imx_devtype_data {
	void (*intctrl)(struct spi_imx_data *, int);
	int (*config)(struct spi_imx_data *, struct spi_imx_config *);
	void (*trigger)(struct spi_imx_data *);
	int (*rx_available)(struct spi_imx_data *);
	void (*reset)(struct spi_imx_data *);
};

struct spi_imx_data {
	struct spi_bitbang bitbang;

	struct completion xfer_done;
	void *base;
	int irq;
	struct clk *clk;
	unsigned long spi_clk;
	int *chipselect;

	unsigned int count;
	void (*tx)(struct spi_imx_data *);
	void (*rx)(struct spi_imx_data *);
	void *rx_buf;
	const void *tx_buf;
	unsigned int txfifo; /* number of words pushed in tx FIFO */

	struct spi_imx_devtype_data devtype_data;
};

#define MXC_SPI_BUF_RX(type)						\
static void spi_imx_buf_rx_##type(struct spi_imx_data *spi_imx)		\
{									\
	unsigned int val = readl(spi_imx->base + MXC_CSPIRXDATA);	\
									\
	if (spi_imx->rx_buf) {						\
		*(type *)spi_imx->rx_buf = val;				\
		spi_imx->rx_buf += sizeof(type);			\
	}								\
}

#define MXC_SPI_BUF_TX(type)						\
static void spi_imx_buf_tx_##type(struct spi_imx_data *spi_imx)		\
{									\
	type val = 0;							\
									\
	if (spi_imx->tx_buf) {						\
		val = *(type *)spi_imx->tx_buf;				\
		spi_imx->tx_buf += sizeof(type);			\
	}								\
									\
	spi_imx->count -= sizeof(type);					\
									\
	writel(val, spi_imx->base + MXC_CSPITXDATA);			\
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
static unsigned int spi_imx_clkdiv_1(unsigned int fin,
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
static unsigned int spi_imx_clkdiv_2(unsigned int fin,
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
static void __maybe_unused mx31_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX31_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX31_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void __maybe_unused mx31_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX31_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int __maybe_unused spi_imx0_4_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX31_CSPICTRL_ENABLE | MX31_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, config->speed_hz) <<
		MX31_CSPICTRL_DR_SHIFT;

	reg |= (config->bpw - 1) << MX31_CSPICTRL_BC_SHIFT;

	if (config->mode & SPI_CPHA)
		reg |= MX31_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX31_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX31_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) << MX31_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused spi_imx0_7_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX31_CSPICTRL_ENABLE | MX31_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, config->speed_hz) <<
		MX31_CSPICTRL_DR_SHIFT;

	reg |= (config->bpw - 1) << MX35_CSPICTRL_BL_SHIFT;
	reg |= MX31_CSPICTRL_SSCTL;

	if (config->mode & SPI_CPHA)
		reg |= MX31_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX31_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX31_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) << MX35_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx31_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MX31_CSPISTATUS) & MX31_STATUS_RR;
}

static void __maybe_unused spi_imx0_4_reset(struct spi_imx_data *spi_imx)
{
	/* drain receive buffer */
	while (readl(spi_imx->base + MX3_CSPISTAT) & MX3_CSPISTAT_RR)
		readl(spi_imx->base + MXC_CSPIRXDATA);
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

static void __maybe_unused mx27_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX27_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX27_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void __maybe_unused mx27_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX27_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int __maybe_unused mx27_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX27_CSPICTRL_ENABLE | MX27_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];

	reg |= spi_imx_clkdiv_1(spi_imx->spi_clk, config->speed_hz) <<
		MX27_CSPICTRL_DR_SHIFT;
	reg |= config->bpw - 1;

	if (config->mode & SPI_CPHA)
		reg |= MX27_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX27_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX27_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) << MX27_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx27_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MXC_CSPIINT) & MX27_INTREG_RR;
}

static void __maybe_unused spi_imx0_0_reset(struct spi_imx_data *spi_imx)
{
	writel(1, spi_imx->base + MXC_RESET);
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

static void __maybe_unused mx1_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX1_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX1_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void __maybe_unused mx1_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX1_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int __maybe_unused mx1_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX1_CSPICTRL_ENABLE | MX1_CSPICTRL_MASTER;

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, config->speed_hz) <<
		MX1_CSPICTRL_DR_SHIFT;
	reg |= config->bpw - 1;

	if (config->mode & SPI_CPHA)
		reg |= MX1_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX1_CSPICTRL_POL;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx1_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MXC_CSPIINT) & MX1_INTREG_RR;
}

static void __maybe_unused mx1_reset(struct spi_imx_data *spi_imx)
{
	writel(1, spi_imx->base + MXC_RESET);
}

/*
 * These version numbers are taken from the Freescale driver.  Unfortunately it
 * doesn't support i.MX1, so this entry doesn't match the scheme. :-(
 */
static struct spi_imx_devtype_data spi_imx_devtype_data[] __devinitdata = {
#ifdef CONFIG_SPI_IMX_VER_IMX1
	[SPI_IMX_VER_IMX1] = {
		.intctrl = mx1_intctrl,
		.config = mx1_config,
		.trigger = mx1_trigger,
		.rx_available = mx1_rx_available,
		.reset = mx1_reset,
	},
#endif
#ifdef CONFIG_SPI_IMX_VER_0_0
	[SPI_IMX_VER_0_0] = {
		.intctrl = mx27_intctrl,
		.config = mx27_config,
		.trigger = mx27_trigger,
		.rx_available = mx27_rx_available,
		.reset = spi_imx0_0_reset,
	},
#endif
#ifdef CONFIG_SPI_IMX_VER_0_4
	[SPI_IMX_VER_0_4] = {
		.intctrl = mx31_intctrl,
		.config = spi_imx0_4_config,
		.trigger = mx31_trigger,
		.rx_available = mx31_rx_available,
		.reset = spi_imx0_4_reset,
	},
#endif
#ifdef CONFIG_SPI_IMX_VER_0_7
	[SPI_IMX_VER_0_7] = {
		.intctrl = mx31_intctrl,
		.config = spi_imx0_7_config,
		.trigger = mx31_trigger,
		.rx_available = mx31_rx_available,
		.reset = spi_imx0_4_reset,
	},
#endif
};

static void spi_imx_chipselect(struct spi_device *spi, int is_active)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	int gpio = spi_imx->chipselect[spi->chip_select];
	int active = is_active != BITBANG_CS_INACTIVE;
	int dev_is_lowactive = !(spi->mode & SPI_CS_HIGH);

	if (gpio < 0)
		return;

	gpio_set_value(gpio, dev_is_lowactive ^ active);
}

static void spi_imx_push(struct spi_imx_data *spi_imx)
{
	while (spi_imx->txfifo < 8) {
		if (!spi_imx->count)
			break;
		spi_imx->tx(spi_imx);
		spi_imx->txfifo++;
	}

	spi_imx->devtype_data.trigger(spi_imx);
}

static irqreturn_t spi_imx_isr(int irq, void *dev_id)
{
	struct spi_imx_data *spi_imx = dev_id;

	while (spi_imx->devtype_data.rx_available(spi_imx)) {
		spi_imx->rx(spi_imx);
		spi_imx->txfifo--;
	}

	if (spi_imx->count) {
		spi_imx_push(spi_imx);
		return IRQ_HANDLED;
	}

	if (spi_imx->txfifo) {
		/* No data left to push, but still waiting for rx data,
		 * enable receive data available interrupt.
		 */
		spi_imx->devtype_data.intctrl(
				spi_imx, MXC_INT_RR);
		return IRQ_HANDLED;
	}

	spi_imx->devtype_data.intctrl(spi_imx, 0);
	complete(&spi_imx->xfer_done);

	return IRQ_HANDLED;
}

static int spi_imx_setupxfer(struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	struct spi_imx_config config;

	config.bpw = t ? t->bits_per_word : spi->bits_per_word;
	config.speed_hz  = t ? t->speed_hz : spi->max_speed_hz;
	config.mode = spi->mode;
	config.cs = spi->chip_select;

	if (!config.speed_hz)
		config.speed_hz = spi->max_speed_hz;
	if (!config.bpw)
		config.bpw = spi->bits_per_word;
	if (!config.speed_hz)
		config.speed_hz = spi->max_speed_hz;

	/* Initialize the functions for transfer */
	if (config.bpw <= 8) {
		spi_imx->rx = spi_imx_buf_rx_u8;
		spi_imx->tx = spi_imx_buf_tx_u8;
	} else if (config.bpw <= 16) {
		spi_imx->rx = spi_imx_buf_rx_u16;
		spi_imx->tx = spi_imx_buf_tx_u16;
	} else if (config.bpw <= 32) {
		spi_imx->rx = spi_imx_buf_rx_u32;
		spi_imx->tx = spi_imx_buf_tx_u32;
	} else
		BUG();

	spi_imx->devtype_data.config(spi_imx, &config);

	return 0;
}

static int spi_imx_transfer(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);

	spi_imx->tx_buf = transfer->tx_buf;
	spi_imx->rx_buf = transfer->rx_buf;
	spi_imx->count = transfer->len;
	spi_imx->txfifo = 0;

	init_completion(&spi_imx->xfer_done);

	spi_imx_push(spi_imx);

	spi_imx->devtype_data.intctrl(spi_imx, MXC_INT_TE);

	wait_for_completion(&spi_imx->xfer_done);

	return transfer->len;
}

static int spi_imx_setup(struct spi_device *spi)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	int gpio = spi_imx->chipselect[spi->chip_select];

	dev_dbg(&spi->dev, "%s: mode %d, %u bpw, %d hz\n", __func__,
		 spi->mode, spi->bits_per_word, spi->max_speed_hz);

	if (gpio >= 0)
		gpio_direction_output(gpio, spi->mode & SPI_CS_HIGH ? 0 : 1);

	spi_imx_chipselect(spi, BITBANG_CS_INACTIVE);

	return 0;
}

static void spi_imx_cleanup(struct spi_device *spi)
{
}

static struct platform_device_id spi_imx_devtype[] = {
	{
		.name = DRIVER_NAME,
		.driver_data = SPI_IMX_VER_AUTODETECT,
	}, {
		.name = "imx1-cspi",
		.driver_data = SPI_IMX_VER_IMX1,
	}, {
		.name = "imx21-cspi",
		.driver_data = SPI_IMX_VER_0_0,
	}, {
		.name = "imx25-cspi",
		.driver_data = SPI_IMX_VER_0_7,
	}, {
		.name = "imx27-cspi",
		.driver_data = SPI_IMX_VER_0_0,
	}, {
		.name = "imx31-cspi",
		.driver_data = SPI_IMX_VER_0_4,
	}, {
		.name = "imx35-cspi",
		.driver_data = SPI_IMX_VER_0_7,
	}, {
		/* sentinel */
	}
};

static int __devinit spi_imx_probe(struct platform_device *pdev)
{
	struct spi_imx_master *mxc_platform_info;
	struct spi_master *master;
	struct spi_imx_data *spi_imx;
	struct resource *res;
	int i, ret;

	mxc_platform_info = dev_get_platdata(&pdev->dev);
	if (!mxc_platform_info) {
		dev_err(&pdev->dev, "can't get the platform data\n");
		return -EINVAL;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct spi_imx_data));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	master->bus_num = pdev->id;
	master->num_chipselect = mxc_platform_info->num_chipselect;

	spi_imx = spi_master_get_devdata(master);
	spi_imx->bitbang.master = spi_master_get(master);
	spi_imx->chipselect = mxc_platform_info->chipselect;

	for (i = 0; i < master->num_chipselect; i++) {
		if (spi_imx->chipselect[i] < 0)
			continue;
		ret = gpio_request(spi_imx->chipselect[i], DRIVER_NAME);
		if (ret) {
			while (i > 0) {
				i--;
				if (spi_imx->chipselect[i] >= 0)
					gpio_free(spi_imx->chipselect[i]);
			}
			dev_err(&pdev->dev, "can't get cs gpios\n");
			goto out_master_put;
		}
	}

	spi_imx->bitbang.chipselect = spi_imx_chipselect;
	spi_imx->bitbang.setup_transfer = spi_imx_setupxfer;
	spi_imx->bitbang.txrx_bufs = spi_imx_transfer;
	spi_imx->bitbang.master->setup = spi_imx_setup;
	spi_imx->bitbang.master->cleanup = spi_imx_cleanup;
	spi_imx->bitbang.master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	init_completion(&spi_imx->xfer_done);

	if (pdev->id_entry->driver_data == SPI_IMX_VER_AUTODETECT) {
		if (cpu_is_mx25() || cpu_is_mx35())
			spi_imx->devtype_data =
				spi_imx_devtype_data[SPI_IMX_VER_0_7];
		else if (cpu_is_mx25() || cpu_is_mx31() || cpu_is_mx35())
			spi_imx->devtype_data =
				spi_imx_devtype_data[SPI_IMX_VER_0_4];
		else if (cpu_is_mx27() || cpu_is_mx21())
			spi_imx->devtype_data =
				spi_imx_devtype_data[SPI_IMX_VER_0_0];
		else if (cpu_is_mx1())
			spi_imx->devtype_data =
				spi_imx_devtype_data[SPI_IMX_VER_IMX1];
		else
			BUG();
	} else
		spi_imx->devtype_data =
			spi_imx_devtype_data[pdev->id_entry->driver_data];

	if (!spi_imx->devtype_data.intctrl) {
		dev_err(&pdev->dev, "no support for this device compiled in\n");
		ret = -ENODEV;
		goto out_gpio_free;
	}

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

	spi_imx->base = ioremap(res->start, resource_size(res));
	if (!spi_imx->base) {
		ret = -EINVAL;
		goto out_release_mem;
	}

	spi_imx->irq = platform_get_irq(pdev, 0);
	if (spi_imx->irq <= 0) {
		ret = -EINVAL;
		goto out_iounmap;
	}

	ret = request_irq(spi_imx->irq, spi_imx_isr, 0, DRIVER_NAME, spi_imx);
	if (ret) {
		dev_err(&pdev->dev, "can't get irq%d: %d\n", spi_imx->irq, ret);
		goto out_iounmap;
	}

	spi_imx->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi_imx->clk)) {
		dev_err(&pdev->dev, "unable to get clock\n");
		ret = PTR_ERR(spi_imx->clk);
		goto out_free_irq;
	}

	clk_enable(spi_imx->clk);
	spi_imx->spi_clk = clk_get_rate(spi_imx->clk);

	spi_imx->devtype_data.reset(spi_imx);

	spi_imx->devtype_data.intctrl(spi_imx, 0);

	ret = spi_bitbang_start(&spi_imx->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "bitbang start failed with %d\n", ret);
		goto out_clk_put;
	}

	dev_info(&pdev->dev, "probed\n");

	return ret;

out_clk_put:
	clk_disable(spi_imx->clk);
	clk_put(spi_imx->clk);
out_free_irq:
	free_irq(spi_imx->irq, spi_imx);
out_iounmap:
	iounmap(spi_imx->base);
out_release_mem:
	release_mem_region(res->start, resource_size(res));
out_gpio_free:
	for (i = 0; i < master->num_chipselect; i++)
		if (spi_imx->chipselect[i] >= 0)
			gpio_free(spi_imx->chipselect[i]);
out_master_put:
	spi_master_put(master);
	kfree(master);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int __devexit spi_imx_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct spi_imx_data *spi_imx = spi_master_get_devdata(master);
	int i;

	spi_bitbang_stop(&spi_imx->bitbang);

	writel(0, spi_imx->base + MXC_CSPICTRL);
	clk_disable(spi_imx->clk);
	clk_put(spi_imx->clk);
	free_irq(spi_imx->irq, spi_imx);
	iounmap(spi_imx->base);

	for (i = 0; i < master->num_chipselect; i++)
		if (spi_imx->chipselect[i] >= 0)
			gpio_free(spi_imx->chipselect[i]);

	spi_master_put(master);

	release_mem_region(res->start, resource_size(res));

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver spi_imx_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   },
	.id_table = spi_imx_devtype,
	.probe = spi_imx_probe,
	.remove = __devexit_p(spi_imx_remove),
};

static int __init spi_imx_init(void)
{
	return platform_driver_register(&spi_imx_driver);
}

static void __exit spi_imx_exit(void)
{
	platform_driver_unregister(&spi_imx_driver);
}

module_init(spi_imx_init);
module_exit(spi_imx_exit);

MODULE_DESCRIPTION("SPI Master Controller driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
