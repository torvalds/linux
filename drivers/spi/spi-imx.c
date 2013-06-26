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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>

#include <linux/platform_data/spi-imx.h>

#define DRIVER_NAME "spi_imx"

#define MXC_CSPIRXDATA		0x00
#define MXC_CSPITXDATA		0x04
#define MXC_CSPICTRL		0x08
#define MXC_CSPIINT		0x0c
#define MXC_RESET		0x1c

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
	IMX1_CSPI,
	IMX21_CSPI,
	IMX27_CSPI,
	IMX31_CSPI,
	IMX35_CSPI,	/* CSPI on all i.mx except above */
	IMX51_ECSPI,	/* ECSPI on i.mx51 and later */
};

struct spi_imx_data;

struct spi_imx_devtype_data {
	void (*intctrl)(struct spi_imx_data *, int);
	int (*config)(struct spi_imx_data *, struct spi_imx_config *);
	void (*trigger)(struct spi_imx_data *);
	int (*rx_available)(struct spi_imx_data *);
	void (*reset)(struct spi_imx_data *);
	enum spi_imx_devtype devtype;
};

struct spi_imx_data {
	struct spi_bitbang bitbang;

	struct completion xfer_done;
	void __iomem *base;
	int irq;
	struct clk *clk_per;
	struct clk *clk_ipg;
	unsigned long spi_clk;

	unsigned int count;
	void (*tx)(struct spi_imx_data *);
	void (*rx)(struct spi_imx_data *);
	void *rx_buf;
	const void *tx_buf;
	unsigned int txfifo; /* number of words pushed in tx FIFO */

	const struct spi_imx_devtype_data *devtype_data;
	int chipselect[0];
};

static inline int is_imx27_cspi(struct spi_imx_data *d)
{
	return d->devtype_data->devtype == IMX27_CSPI;
}

static inline int is_imx35_cspi(struct spi_imx_data *d)
{
	return d->devtype_data->devtype == IMX35_CSPI;
}

static inline unsigned spi_imx_get_fifosize(struct spi_imx_data *d)
{
	return (d->devtype_data->devtype == IMX51_ECSPI) ? 64 : 8;
}

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
		unsigned int fspi, unsigned int max)
{
	int i;

	for (i = 2; i < max; i++)
		if (fspi * mxc_clkdivs[i] >= fin)
			return i;

	return max;
}

/* MX1, MX31, MX35, MX51 CSPI */
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

#define MX51_ECSPI_CTRL		0x08
#define MX51_ECSPI_CTRL_ENABLE		(1 <<  0)
#define MX51_ECSPI_CTRL_XCH		(1 <<  2)
#define MX51_ECSPI_CTRL_MODE_MASK	(0xf << 4)
#define MX51_ECSPI_CTRL_POSTDIV_OFFSET	8
#define MX51_ECSPI_CTRL_PREDIV_OFFSET	12
#define MX51_ECSPI_CTRL_CS(cs)		((cs) << 18)
#define MX51_ECSPI_CTRL_BL_OFFSET	20

#define MX51_ECSPI_CONFIG	0x0c
#define MX51_ECSPI_CONFIG_SCLKPHA(cs)	(1 << ((cs) +  0))
#define MX51_ECSPI_CONFIG_SCLKPOL(cs)	(1 << ((cs) +  4))
#define MX51_ECSPI_CONFIG_SBBCTRL(cs)	(1 << ((cs) +  8))
#define MX51_ECSPI_CONFIG_SSBPOL(cs)	(1 << ((cs) + 12))
#define MX51_ECSPI_CONFIG_SCLKCTL(cs)	(1 << ((cs) + 20))

#define MX51_ECSPI_INT		0x10
#define MX51_ECSPI_INT_TEEN		(1 <<  0)
#define MX51_ECSPI_INT_RREN		(1 <<  3)

#define MX51_ECSPI_STAT		0x18
#define MX51_ECSPI_STAT_RR		(1 <<  3)

/* MX51 eCSPI */
static unsigned int mx51_ecspi_clkdiv(unsigned int fin, unsigned int fspi)
{
	/*
	 * there are two 4-bit dividers, the pre-divider divides by
	 * $pre, the post-divider by 2^$post
	 */
	unsigned int pre, post;

	if (unlikely(fspi > fin))
		return 0;

	post = fls(fin) - fls(fspi);
	if (fin > fspi << post)
		post++;

	/* now we have: (fin <= fspi << post) with post being minimal */

	post = max(4U, post) - 4;
	if (unlikely(post > 0xf)) {
		pr_err("%s: cannot set clock freq: %u (base freq: %u)\n",
				__func__, fspi, fin);
		return 0xff;
	}

	pre = DIV_ROUND_UP(fin, fspi << post) - 1;

	pr_debug("%s: fin: %u, fspi: %u, post: %u, pre: %u\n",
			__func__, fin, fspi, post, pre);
	return (pre << MX51_ECSPI_CTRL_PREDIV_OFFSET) |
		(post << MX51_ECSPI_CTRL_POSTDIV_OFFSET);
}

static void __maybe_unused mx51_ecspi_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned val = 0;

	if (enable & MXC_INT_TE)
		val |= MX51_ECSPI_INT_TEEN;

	if (enable & MXC_INT_RR)
		val |= MX51_ECSPI_INT_RREN;

	writel(val, spi_imx->base + MX51_ECSPI_INT);
}

static void __maybe_unused mx51_ecspi_trigger(struct spi_imx_data *spi_imx)
{
	u32 reg;

	reg = readl(spi_imx->base + MX51_ECSPI_CTRL);
	reg |= MX51_ECSPI_CTRL_XCH;
	writel(reg, spi_imx->base + MX51_ECSPI_CTRL);
}

static int __maybe_unused mx51_ecspi_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	u32 ctrl = MX51_ECSPI_CTRL_ENABLE, cfg = 0;

	/*
	 * The hardware seems to have a race condition when changing modes. The
	 * current assumption is that the selection of the channel arrives
	 * earlier in the hardware than the mode bits when they are written at
	 * the same time.
	 * So set master mode for all channels as we do not support slave mode.
	 */
	ctrl |= MX51_ECSPI_CTRL_MODE_MASK;

	/* set clock speed */
	ctrl |= mx51_ecspi_clkdiv(spi_imx->spi_clk, config->speed_hz);

	/* set chip select to use */
	ctrl |= MX51_ECSPI_CTRL_CS(config->cs);

	ctrl |= (config->bpw - 1) << MX51_ECSPI_CTRL_BL_OFFSET;

	cfg |= MX51_ECSPI_CONFIG_SBBCTRL(config->cs);

	if (config->mode & SPI_CPHA)
		cfg |= MX51_ECSPI_CONFIG_SCLKPHA(config->cs);

	if (config->mode & SPI_CPOL) {
		cfg |= MX51_ECSPI_CONFIG_SCLKPOL(config->cs);
		cfg |= MX51_ECSPI_CONFIG_SCLKCTL(config->cs);
	}
	if (config->mode & SPI_CS_HIGH)
		cfg |= MX51_ECSPI_CONFIG_SSBPOL(config->cs);

	writel(ctrl, spi_imx->base + MX51_ECSPI_CTRL);
	writel(cfg, spi_imx->base + MX51_ECSPI_CONFIG);

	return 0;
}

static int __maybe_unused mx51_ecspi_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MX51_ECSPI_STAT) & MX51_ECSPI_STAT_RR;
}

static void __maybe_unused mx51_ecspi_reset(struct spi_imx_data *spi_imx)
{
	/* drain receive buffer */
	while (mx51_ecspi_rx_available(spi_imx))
		readl(spi_imx->base + MXC_CSPIRXDATA);
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

static int __maybe_unused mx31_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX31_CSPICTRL_ENABLE | MX31_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, config->speed_hz) <<
		MX31_CSPICTRL_DR_SHIFT;

	if (is_imx35_cspi(spi_imx)) {
		reg |= (config->bpw - 1) << MX35_CSPICTRL_BL_SHIFT;
		reg |= MX31_CSPICTRL_SSCTL;
	} else {
		reg |= (config->bpw - 1) << MX31_CSPICTRL_BC_SHIFT;
	}

	if (config->mode & SPI_CPHA)
		reg |= MX31_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX31_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX31_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) <<
			(is_imx35_cspi(spi_imx) ? MX35_CSPICTRL_CS_SHIFT :
						  MX31_CSPICTRL_CS_SHIFT);

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx31_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MX31_CSPISTATUS) & MX31_STATUS_RR;
}

static void __maybe_unused mx31_reset(struct spi_imx_data *spi_imx)
{
	/* drain receive buffer */
	while (readl(spi_imx->base + MX31_CSPISTATUS) & MX31_STATUS_RR)
		readl(spi_imx->base + MXC_CSPIRXDATA);
}

#define MX21_INTREG_RR		(1 << 4)
#define MX21_INTREG_TEEN	(1 << 9)
#define MX21_INTREG_RREN	(1 << 13)

#define MX21_CSPICTRL_POL	(1 << 5)
#define MX21_CSPICTRL_PHA	(1 << 6)
#define MX21_CSPICTRL_SSPOL	(1 << 8)
#define MX21_CSPICTRL_XCH	(1 << 9)
#define MX21_CSPICTRL_ENABLE	(1 << 10)
#define MX21_CSPICTRL_MASTER	(1 << 11)
#define MX21_CSPICTRL_DR_SHIFT	14
#define MX21_CSPICTRL_CS_SHIFT	19

static void __maybe_unused mx21_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX21_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX21_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void __maybe_unused mx21_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX21_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int __maybe_unused mx21_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX21_CSPICTRL_ENABLE | MX21_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];
	unsigned int max = is_imx27_cspi(spi_imx) ? 16 : 18;

	reg |= spi_imx_clkdiv_1(spi_imx->spi_clk, config->speed_hz, max) <<
		MX21_CSPICTRL_DR_SHIFT;
	reg |= config->bpw - 1;

	if (config->mode & SPI_CPHA)
		reg |= MX21_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX21_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX21_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) << MX21_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx21_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MXC_CSPIINT) & MX21_INTREG_RR;
}

static void __maybe_unused mx21_reset(struct spi_imx_data *spi_imx)
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

static struct spi_imx_devtype_data imx1_cspi_devtype_data = {
	.intctrl = mx1_intctrl,
	.config = mx1_config,
	.trigger = mx1_trigger,
	.rx_available = mx1_rx_available,
	.reset = mx1_reset,
	.devtype = IMX1_CSPI,
};

static struct spi_imx_devtype_data imx21_cspi_devtype_data = {
	.intctrl = mx21_intctrl,
	.config = mx21_config,
	.trigger = mx21_trigger,
	.rx_available = mx21_rx_available,
	.reset = mx21_reset,
	.devtype = IMX21_CSPI,
};

static struct spi_imx_devtype_data imx27_cspi_devtype_data = {
	/* i.mx27 cspi shares the functions with i.mx21 one */
	.intctrl = mx21_intctrl,
	.config = mx21_config,
	.trigger = mx21_trigger,
	.rx_available = mx21_rx_available,
	.reset = mx21_reset,
	.devtype = IMX27_CSPI,
};

static struct spi_imx_devtype_data imx31_cspi_devtype_data = {
	.intctrl = mx31_intctrl,
	.config = mx31_config,
	.trigger = mx31_trigger,
	.rx_available = mx31_rx_available,
	.reset = mx31_reset,
	.devtype = IMX31_CSPI,
};

static struct spi_imx_devtype_data imx35_cspi_devtype_data = {
	/* i.mx35 and later cspi shares the functions with i.mx31 one */
	.intctrl = mx31_intctrl,
	.config = mx31_config,
	.trigger = mx31_trigger,
	.rx_available = mx31_rx_available,
	.reset = mx31_reset,
	.devtype = IMX35_CSPI,
};

static struct spi_imx_devtype_data imx51_ecspi_devtype_data = {
	.intctrl = mx51_ecspi_intctrl,
	.config = mx51_ecspi_config,
	.trigger = mx51_ecspi_trigger,
	.rx_available = mx51_ecspi_rx_available,
	.reset = mx51_ecspi_reset,
	.devtype = IMX51_ECSPI,
};

static struct platform_device_id spi_imx_devtype[] = {
	{
		.name = "imx1-cspi",
		.driver_data = (kernel_ulong_t) &imx1_cspi_devtype_data,
	}, {
		.name = "imx21-cspi",
		.driver_data = (kernel_ulong_t) &imx21_cspi_devtype_data,
	}, {
		.name = "imx27-cspi",
		.driver_data = (kernel_ulong_t) &imx27_cspi_devtype_data,
	}, {
		.name = "imx31-cspi",
		.driver_data = (kernel_ulong_t) &imx31_cspi_devtype_data,
	}, {
		.name = "imx35-cspi",
		.driver_data = (kernel_ulong_t) &imx35_cspi_devtype_data,
	}, {
		.name = "imx51-ecspi",
		.driver_data = (kernel_ulong_t) &imx51_ecspi_devtype_data,
	}, {
		/* sentinel */
	}
};

static const struct of_device_id spi_imx_dt_ids[] = {
	{ .compatible = "fsl,imx1-cspi", .data = &imx1_cspi_devtype_data, },
	{ .compatible = "fsl,imx21-cspi", .data = &imx21_cspi_devtype_data, },
	{ .compatible = "fsl,imx27-cspi", .data = &imx27_cspi_devtype_data, },
	{ .compatible = "fsl,imx31-cspi", .data = &imx31_cspi_devtype_data, },
	{ .compatible = "fsl,imx35-cspi", .data = &imx35_cspi_devtype_data, },
	{ .compatible = "fsl,imx51-ecspi", .data = &imx51_ecspi_devtype_data, },
	{ /* sentinel */ }
};

static void spi_imx_chipselect(struct spi_device *spi, int is_active)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	int gpio = spi_imx->chipselect[spi->chip_select];
	int active = is_active != BITBANG_CS_INACTIVE;
	int dev_is_lowactive = !(spi->mode & SPI_CS_HIGH);

	if (!gpio_is_valid(gpio))
		return;

	gpio_set_value(gpio, dev_is_lowactive ^ active);
}

static void spi_imx_push(struct spi_imx_data *spi_imx)
{
	while (spi_imx->txfifo < spi_imx_get_fifosize(spi_imx)) {
		if (!spi_imx->count)
			break;
		spi_imx->tx(spi_imx);
		spi_imx->txfifo++;
	}

	spi_imx->devtype_data->trigger(spi_imx);
}

static irqreturn_t spi_imx_isr(int irq, void *dev_id)
{
	struct spi_imx_data *spi_imx = dev_id;

	while (spi_imx->devtype_data->rx_available(spi_imx)) {
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
		spi_imx->devtype_data->intctrl(
				spi_imx, MXC_INT_RR);
		return IRQ_HANDLED;
	}

	spi_imx->devtype_data->intctrl(spi_imx, 0);
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

	/* Initialize the functions for transfer */
	if (config.bpw <= 8) {
		spi_imx->rx = spi_imx_buf_rx_u8;
		spi_imx->tx = spi_imx_buf_tx_u8;
	} else if (config.bpw <= 16) {
		spi_imx->rx = spi_imx_buf_rx_u16;
		spi_imx->tx = spi_imx_buf_tx_u16;
	} else {
		spi_imx->rx = spi_imx_buf_rx_u32;
		spi_imx->tx = spi_imx_buf_tx_u32;
	}

	spi_imx->devtype_data->config(spi_imx, &config);

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

	spi_imx->devtype_data->intctrl(spi_imx, MXC_INT_TE);

	wait_for_completion(&spi_imx->xfer_done);

	return transfer->len;
}

static int spi_imx_setup(struct spi_device *spi)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	int gpio = spi_imx->chipselect[spi->chip_select];

	dev_dbg(&spi->dev, "%s: mode %d, %u bpw, %d hz\n", __func__,
		 spi->mode, spi->bits_per_word, spi->max_speed_hz);

	if (gpio_is_valid(gpio))
		gpio_direction_output(gpio, spi->mode & SPI_CS_HIGH ? 0 : 1);

	spi_imx_chipselect(spi, BITBANG_CS_INACTIVE);

	return 0;
}

static void spi_imx_cleanup(struct spi_device *spi)
{
}

static int spi_imx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
			of_match_device(spi_imx_dt_ids, &pdev->dev);
	struct spi_imx_master *mxc_platform_info =
			dev_get_platdata(&pdev->dev);
	struct spi_master *master;
	struct spi_imx_data *spi_imx;
	struct resource *res;
	struct pinctrl *pinctrl;
	int i, ret, num_cs;

	if (!np && !mxc_platform_info) {
		dev_err(&pdev->dev, "can't get the platform data\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "fsl,spi-num-chipselects", &num_cs);
	if (ret < 0) {
		if (mxc_platform_info)
			num_cs = mxc_platform_info->num_chipselect;
		else
			return ret;
	}

	master = spi_alloc_master(&pdev->dev,
			sizeof(struct spi_imx_data) + sizeof(int) * num_cs);
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 32);
	master->bus_num = pdev->id;
	master->num_chipselect = num_cs;

	spi_imx = spi_master_get_devdata(master);
	spi_imx->bitbang.master = spi_master_get(master);

	for (i = 0; i < master->num_chipselect; i++) {
		int cs_gpio = of_get_named_gpio(np, "cs-gpios", i);
		if (!gpio_is_valid(cs_gpio) && mxc_platform_info)
			cs_gpio = mxc_platform_info->chipselect[i];

		spi_imx->chipselect[i] = cs_gpio;
		if (!gpio_is_valid(cs_gpio))
			continue;

		ret = gpio_request(spi_imx->chipselect[i], DRIVER_NAME);
		if (ret) {
			dev_err(&pdev->dev, "can't get cs gpios\n");
			goto out_gpio_free;
		}
	}

	spi_imx->bitbang.chipselect = spi_imx_chipselect;
	spi_imx->bitbang.setup_transfer = spi_imx_setupxfer;
	spi_imx->bitbang.txrx_bufs = spi_imx_transfer;
	spi_imx->bitbang.master->setup = spi_imx_setup;
	spi_imx->bitbang.master->cleanup = spi_imx_cleanup;
	spi_imx->bitbang.master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	init_completion(&spi_imx->xfer_done);

	spi_imx->devtype_data = of_id ? of_id->data :
		(struct spi_imx_devtype_data *) pdev->id_entry->driver_data;

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
	if (spi_imx->irq < 0) {
		ret = -EINVAL;
		goto out_iounmap;
	}

	ret = request_irq(spi_imx->irq, spi_imx_isr, 0, DRIVER_NAME, spi_imx);
	if (ret) {
		dev_err(&pdev->dev, "can't get irq%d: %d\n", spi_imx->irq, ret);
		goto out_iounmap;
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		ret = PTR_ERR(pinctrl);
		goto out_free_irq;
	}

	spi_imx->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(spi_imx->clk_ipg)) {
		ret = PTR_ERR(spi_imx->clk_ipg);
		goto out_free_irq;
	}

	spi_imx->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(spi_imx->clk_per)) {
		ret = PTR_ERR(spi_imx->clk_per);
		goto out_free_irq;
	}

	clk_prepare_enable(spi_imx->clk_per);
	clk_prepare_enable(spi_imx->clk_ipg);

	spi_imx->spi_clk = clk_get_rate(spi_imx->clk_per);

	spi_imx->devtype_data->reset(spi_imx);

	spi_imx->devtype_data->intctrl(spi_imx, 0);

	master->dev.of_node = pdev->dev.of_node;
	ret = spi_bitbang_start(&spi_imx->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "bitbang start failed with %d\n", ret);
		goto out_clk_put;
	}

	dev_info(&pdev->dev, "probed\n");

	return ret;

out_clk_put:
	clk_disable_unprepare(spi_imx->clk_per);
	clk_disable_unprepare(spi_imx->clk_ipg);
out_free_irq:
	free_irq(spi_imx->irq, spi_imx);
out_iounmap:
	iounmap(spi_imx->base);
out_release_mem:
	release_mem_region(res->start, resource_size(res));
out_gpio_free:
	while (--i >= 0) {
		if (gpio_is_valid(spi_imx->chipselect[i]))
			gpio_free(spi_imx->chipselect[i]);
	}
	spi_master_put(master);
	kfree(master);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int spi_imx_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct spi_imx_data *spi_imx = spi_master_get_devdata(master);
	int i;

	spi_bitbang_stop(&spi_imx->bitbang);

	writel(0, spi_imx->base + MXC_CSPICTRL);
	clk_disable_unprepare(spi_imx->clk_per);
	clk_disable_unprepare(spi_imx->clk_ipg);
	free_irq(spi_imx->irq, spi_imx);
	iounmap(spi_imx->base);

	for (i = 0; i < master->num_chipselect; i++)
		if (gpio_is_valid(spi_imx->chipselect[i]))
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
		   .of_match_table = spi_imx_dt_ids,
		   },
	.id_table = spi_imx_devtype,
	.probe = spi_imx_probe,
	.remove = spi_imx_remove,
};
module_platform_driver(spi_imx_driver);

MODULE_DESCRIPTION("SPI Master Controller driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
