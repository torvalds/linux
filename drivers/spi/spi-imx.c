// SPDX-License-Identifier: GPL-2.0+
// Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
// Copyright (C) 2008 Juergen Beisert

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/property.h>

#include <linux/dma/imx-dma.h>

#define DRIVER_NAME "spi_imx"

static bool use_dma = true;
module_param(use_dma, bool, 0644);
MODULE_PARM_DESC(use_dma, "Enable usage of DMA when available (default)");

/* define polling limits */
static unsigned int polling_limit_us = 30;
module_param(polling_limit_us, uint, 0664);
MODULE_PARM_DESC(polling_limit_us,
		 "time in us to run a transfer in polling mode\n");

#define MXC_RPM_TIMEOUT		2000 /* 2000ms */

#define MXC_CSPIRXDATA		0x00
#define MXC_CSPITXDATA		0x04
#define MXC_CSPICTRL		0x08
#define MXC_CSPIINT		0x0c
#define MXC_RESET		0x1c

/* generic defines to abstract from the different register layouts */
#define MXC_INT_RR	(1 << 0) /* Receive data ready interrupt */
#define MXC_INT_TE	(1 << 1) /* Transmit FIFO empty interrupt */
#define MXC_INT_RDR	BIT(4) /* Receive date threshold interrupt */

/* The maximum bytes that a sdma BD can transfer. */
#define MAX_SDMA_BD_BYTES (1 << 15)
#define MX51_ECSPI_CTRL_MAX_BURST	512
/* The maximum bytes that IMX53_ECSPI can transfer in slave mode.*/
#define MX53_MAX_TRANSFER_BYTES		512

enum spi_imx_devtype {
	IMX1_CSPI,
	IMX21_CSPI,
	IMX27_CSPI,
	IMX31_CSPI,
	IMX35_CSPI,	/* CSPI on all i.mx except above */
	IMX51_ECSPI,	/* ECSPI on i.mx51 */
	IMX53_ECSPI,	/* ECSPI on i.mx53 and later */
};

struct spi_imx_data;

struct spi_imx_devtype_data {
	void (*intctrl)(struct spi_imx_data *spi_imx, int enable);
	int (*prepare_message)(struct spi_imx_data *spi_imx, struct spi_message *msg);
	int (*prepare_transfer)(struct spi_imx_data *spi_imx, struct spi_device *spi);
	void (*trigger)(struct spi_imx_data *spi_imx);
	int (*rx_available)(struct spi_imx_data *spi_imx);
	void (*reset)(struct spi_imx_data *spi_imx);
	void (*setup_wml)(struct spi_imx_data *spi_imx);
	void (*disable)(struct spi_imx_data *spi_imx);
	bool has_dmamode;
	bool has_slavemode;
	unsigned int fifo_size;
	bool dynamic_burst;
	/*
	 * ERR009165 fixed or not:
	 * https://www.nxp.com/docs/en/errata/IMX6DQCE.pdf
	 */
	bool tx_glitch_fixed;
	enum spi_imx_devtype devtype;
};

struct spi_imx_data {
	struct spi_controller *controller;
	struct device *dev;

	struct completion xfer_done;
	void __iomem *base;
	unsigned long base_phys;

	struct clk *clk_per;
	struct clk *clk_ipg;
	unsigned long spi_clk;
	unsigned int spi_bus_clk;

	unsigned int bits_per_word;
	unsigned int spi_drctl;

	unsigned int count, remainder;
	void (*tx)(struct spi_imx_data *spi_imx);
	void (*rx)(struct spi_imx_data *spi_imx);
	void *rx_buf;
	const void *tx_buf;
	unsigned int txfifo; /* number of words pushed in tx FIFO */
	unsigned int dynamic_burst;
	bool rx_only;

	/* Slave mode */
	bool slave_mode;
	bool slave_aborted;
	unsigned int slave_burst;

	/* DMA */
	bool usedma;
	u32 wml;
	struct completion dma_rx_completion;
	struct completion dma_tx_completion;

	const struct spi_imx_devtype_data *devtype_data;
};

static inline int is_imx27_cspi(struct spi_imx_data *d)
{
	return d->devtype_data->devtype == IMX27_CSPI;
}

static inline int is_imx35_cspi(struct spi_imx_data *d)
{
	return d->devtype_data->devtype == IMX35_CSPI;
}

static inline int is_imx51_ecspi(struct spi_imx_data *d)
{
	return d->devtype_data->devtype == IMX51_ECSPI;
}

static inline int is_imx53_ecspi(struct spi_imx_data *d)
{
	return d->devtype_data->devtype == IMX53_ECSPI;
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
									\
	spi_imx->remainder -= sizeof(type);				\
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
		unsigned int fspi, unsigned int max, unsigned int *fres)
{
	int i;

	for (i = 2; i < max; i++)
		if (fspi * mxc_clkdivs[i] >= fin)
			break;

	*fres = fin / mxc_clkdivs[i];
	return i;
}

/* MX1, MX31, MX35, MX51 CSPI */
static unsigned int spi_imx_clkdiv_2(unsigned int fin,
		unsigned int fspi, unsigned int *fres)
{
	int i, div = 4;

	for (i = 0; i < 7; i++) {
		if (fspi * div >= fin)
			goto out;
		div <<= 1;
	}

out:
	*fres = fin / div;
	return i;
}

static int spi_imx_bytes_per_word(const int bits_per_word)
{
	if (bits_per_word <= 8)
		return 1;
	else if (bits_per_word <= 16)
		return 2;
	else
		return 4;
}

static bool spi_imx_can_dma(struct spi_controller *controller, struct spi_device *spi,
			 struct spi_transfer *transfer)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(controller);

	if (!use_dma || controller->fallback)
		return false;

	if (!controller->dma_rx)
		return false;

	if (spi_imx->slave_mode)
		return false;

	if (transfer->len < spi_imx->devtype_data->fifo_size)
		return false;

	spi_imx->dynamic_burst = 0;

	return true;
}

#define MX51_ECSPI_CTRL		0x08
#define MX51_ECSPI_CTRL_ENABLE		(1 <<  0)
#define MX51_ECSPI_CTRL_XCH		(1 <<  2)
#define MX51_ECSPI_CTRL_SMC		(1 << 3)
#define MX51_ECSPI_CTRL_MODE_MASK	(0xf << 4)
#define MX51_ECSPI_CTRL_DRCTL(drctl)	((drctl) << 16)
#define MX51_ECSPI_CTRL_POSTDIV_OFFSET	8
#define MX51_ECSPI_CTRL_PREDIV_OFFSET	12
#define MX51_ECSPI_CTRL_CS(cs)		((cs) << 18)
#define MX51_ECSPI_CTRL_BL_OFFSET	20
#define MX51_ECSPI_CTRL_BL_MASK		(0xfff << 20)

#define MX51_ECSPI_CONFIG	0x0c
#define MX51_ECSPI_CONFIG_SCLKPHA(cs)	(1 << ((cs) +  0))
#define MX51_ECSPI_CONFIG_SCLKPOL(cs)	(1 << ((cs) +  4))
#define MX51_ECSPI_CONFIG_SBBCTRL(cs)	(1 << ((cs) +  8))
#define MX51_ECSPI_CONFIG_SSBPOL(cs)	(1 << ((cs) + 12))
#define MX51_ECSPI_CONFIG_SCLKCTL(cs)	(1 << ((cs) + 20))

#define MX51_ECSPI_INT		0x10
#define MX51_ECSPI_INT_TEEN		(1 <<  0)
#define MX51_ECSPI_INT_RREN		(1 <<  3)
#define MX51_ECSPI_INT_RDREN		(1 <<  4)

#define MX51_ECSPI_DMA		0x14
#define MX51_ECSPI_DMA_TX_WML(wml)	((wml) & 0x3f)
#define MX51_ECSPI_DMA_RX_WML(wml)	(((wml) & 0x3f) << 16)
#define MX51_ECSPI_DMA_RXT_WML(wml)	(((wml) & 0x3f) << 24)

#define MX51_ECSPI_DMA_TEDEN		(1 << 7)
#define MX51_ECSPI_DMA_RXDEN		(1 << 23)
#define MX51_ECSPI_DMA_RXTDEN		(1 << 31)

#define MX51_ECSPI_STAT		0x18
#define MX51_ECSPI_STAT_RR		(1 <<  3)

#define MX51_ECSPI_TESTREG	0x20
#define MX51_ECSPI_TESTREG_LBC	BIT(31)

static void spi_imx_buf_rx_swap_u32(struct spi_imx_data *spi_imx)
{
	unsigned int val = readl(spi_imx->base + MXC_CSPIRXDATA);

	if (spi_imx->rx_buf) {
#ifdef __LITTLE_ENDIAN
		unsigned int bytes_per_word;

		bytes_per_word = spi_imx_bytes_per_word(spi_imx->bits_per_word);
		if (bytes_per_word == 1)
			swab32s(&val);
		else if (bytes_per_word == 2)
			swahw32s(&val);
#endif
		*(u32 *)spi_imx->rx_buf = val;
		spi_imx->rx_buf += sizeof(u32);
	}

	spi_imx->remainder -= sizeof(u32);
}

static void spi_imx_buf_rx_swap(struct spi_imx_data *spi_imx)
{
	int unaligned;
	u32 val;

	unaligned = spi_imx->remainder % 4;

	if (!unaligned) {
		spi_imx_buf_rx_swap_u32(spi_imx);
		return;
	}

	if (spi_imx_bytes_per_word(spi_imx->bits_per_word) == 2) {
		spi_imx_buf_rx_u16(spi_imx);
		return;
	}

	val = readl(spi_imx->base + MXC_CSPIRXDATA);

	while (unaligned--) {
		if (spi_imx->rx_buf) {
			*(u8 *)spi_imx->rx_buf = (val >> (8 * unaligned)) & 0xff;
			spi_imx->rx_buf++;
		}
		spi_imx->remainder--;
	}
}

static void spi_imx_buf_tx_swap_u32(struct spi_imx_data *spi_imx)
{
	u32 val = 0;
#ifdef __LITTLE_ENDIAN
	unsigned int bytes_per_word;
#endif

	if (spi_imx->tx_buf) {
		val = *(u32 *)spi_imx->tx_buf;
		spi_imx->tx_buf += sizeof(u32);
	}

	spi_imx->count -= sizeof(u32);
#ifdef __LITTLE_ENDIAN
	bytes_per_word = spi_imx_bytes_per_word(spi_imx->bits_per_word);

	if (bytes_per_word == 1)
		swab32s(&val);
	else if (bytes_per_word == 2)
		swahw32s(&val);
#endif
	writel(val, spi_imx->base + MXC_CSPITXDATA);
}

static void spi_imx_buf_tx_swap(struct spi_imx_data *spi_imx)
{
	int unaligned;
	u32 val = 0;

	unaligned = spi_imx->count % 4;

	if (!unaligned) {
		spi_imx_buf_tx_swap_u32(spi_imx);
		return;
	}

	if (spi_imx_bytes_per_word(spi_imx->bits_per_word) == 2) {
		spi_imx_buf_tx_u16(spi_imx);
		return;
	}

	while (unaligned--) {
		if (spi_imx->tx_buf) {
			val |= *(u8 *)spi_imx->tx_buf << (8 * unaligned);
			spi_imx->tx_buf++;
		}
		spi_imx->count--;
	}

	writel(val, spi_imx->base + MXC_CSPITXDATA);
}

static void mx53_ecspi_rx_slave(struct spi_imx_data *spi_imx)
{
	u32 val = be32_to_cpu(readl(spi_imx->base + MXC_CSPIRXDATA));

	if (spi_imx->rx_buf) {
		int n_bytes = spi_imx->slave_burst % sizeof(val);

		if (!n_bytes)
			n_bytes = sizeof(val);

		memcpy(spi_imx->rx_buf,
		       ((u8 *)&val) + sizeof(val) - n_bytes, n_bytes);

		spi_imx->rx_buf += n_bytes;
		spi_imx->slave_burst -= n_bytes;
	}

	spi_imx->remainder -= sizeof(u32);
}

static void mx53_ecspi_tx_slave(struct spi_imx_data *spi_imx)
{
	u32 val = 0;
	int n_bytes = spi_imx->count % sizeof(val);

	if (!n_bytes)
		n_bytes = sizeof(val);

	if (spi_imx->tx_buf) {
		memcpy(((u8 *)&val) + sizeof(val) - n_bytes,
		       spi_imx->tx_buf, n_bytes);
		val = cpu_to_be32(val);
		spi_imx->tx_buf += n_bytes;
	}

	spi_imx->count -= n_bytes;

	writel(val, spi_imx->base + MXC_CSPITXDATA);
}

/* MX51 eCSPI */
static unsigned int mx51_ecspi_clkdiv(struct spi_imx_data *spi_imx,
				      unsigned int fspi, unsigned int *fres)
{
	/*
	 * there are two 4-bit dividers, the pre-divider divides by
	 * $pre, the post-divider by 2^$post
	 */
	unsigned int pre, post;
	unsigned int fin = spi_imx->spi_clk;

	fspi = min(fspi, fin);

	post = fls(fin) - fls(fspi);
	if (fin > fspi << post)
		post++;

	/* now we have: (fin <= fspi << post) with post being minimal */

	post = max(4U, post) - 4;
	if (unlikely(post > 0xf)) {
		dev_err(spi_imx->dev, "cannot set clock freq: %u (base freq: %u)\n",
				fspi, fin);
		return 0xff;
	}

	pre = DIV_ROUND_UP(fin, fspi << post) - 1;

	dev_dbg(spi_imx->dev, "%s: fin: %u, fspi: %u, post: %u, pre: %u\n",
			__func__, fin, fspi, post, pre);

	/* Resulting frequency for the SCLK line. */
	*fres = (fin / (pre + 1)) >> post;

	return (pre << MX51_ECSPI_CTRL_PREDIV_OFFSET) |
		(post << MX51_ECSPI_CTRL_POSTDIV_OFFSET);
}

static void mx51_ecspi_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX51_ECSPI_INT_TEEN;

	if (enable & MXC_INT_RR)
		val |= MX51_ECSPI_INT_RREN;

	if (enable & MXC_INT_RDR)
		val |= MX51_ECSPI_INT_RDREN;

	writel(val, spi_imx->base + MX51_ECSPI_INT);
}

static void mx51_ecspi_trigger(struct spi_imx_data *spi_imx)
{
	u32 reg;

	reg = readl(spi_imx->base + MX51_ECSPI_CTRL);
	reg |= MX51_ECSPI_CTRL_XCH;
	writel(reg, spi_imx->base + MX51_ECSPI_CTRL);
}

static void mx51_ecspi_disable(struct spi_imx_data *spi_imx)
{
	u32 ctrl;

	ctrl = readl(spi_imx->base + MX51_ECSPI_CTRL);
	ctrl &= ~MX51_ECSPI_CTRL_ENABLE;
	writel(ctrl, spi_imx->base + MX51_ECSPI_CTRL);
}

static int mx51_ecspi_prepare_message(struct spi_imx_data *spi_imx,
				      struct spi_message *msg)
{
	struct spi_device *spi = msg->spi;
	struct spi_transfer *xfer;
	u32 ctrl = MX51_ECSPI_CTRL_ENABLE;
	u32 min_speed_hz = ~0U;
	u32 testreg, delay;
	u32 cfg = readl(spi_imx->base + MX51_ECSPI_CONFIG);
	u32 current_cfg = cfg;

	/* set Master or Slave mode */
	if (spi_imx->slave_mode)
		ctrl &= ~MX51_ECSPI_CTRL_MODE_MASK;
	else
		ctrl |= MX51_ECSPI_CTRL_MODE_MASK;

	/*
	 * Enable SPI_RDY handling (falling edge/level triggered).
	 */
	if (spi->mode & SPI_READY)
		ctrl |= MX51_ECSPI_CTRL_DRCTL(spi_imx->spi_drctl);

	/* set chip select to use */
	ctrl |= MX51_ECSPI_CTRL_CS(spi->chip_select);

	/*
	 * The ctrl register must be written first, with the EN bit set other
	 * registers must not be written to.
	 */
	writel(ctrl, spi_imx->base + MX51_ECSPI_CTRL);

	testreg = readl(spi_imx->base + MX51_ECSPI_TESTREG);
	if (spi->mode & SPI_LOOP)
		testreg |= MX51_ECSPI_TESTREG_LBC;
	else
		testreg &= ~MX51_ECSPI_TESTREG_LBC;
	writel(testreg, spi_imx->base + MX51_ECSPI_TESTREG);

	/*
	 * eCSPI burst completion by Chip Select signal in Slave mode
	 * is not functional for imx53 Soc, config SPI burst completed when
	 * BURST_LENGTH + 1 bits are received
	 */
	if (spi_imx->slave_mode && is_imx53_ecspi(spi_imx))
		cfg &= ~MX51_ECSPI_CONFIG_SBBCTRL(spi->chip_select);
	else
		cfg |= MX51_ECSPI_CONFIG_SBBCTRL(spi->chip_select);

	if (spi->mode & SPI_CPOL) {
		cfg |= MX51_ECSPI_CONFIG_SCLKPOL(spi->chip_select);
		cfg |= MX51_ECSPI_CONFIG_SCLKCTL(spi->chip_select);
	} else {
		cfg &= ~MX51_ECSPI_CONFIG_SCLKPOL(spi->chip_select);
		cfg &= ~MX51_ECSPI_CONFIG_SCLKCTL(spi->chip_select);
	}

	if (spi->mode & SPI_CS_HIGH)
		cfg |= MX51_ECSPI_CONFIG_SSBPOL(spi->chip_select);
	else
		cfg &= ~MX51_ECSPI_CONFIG_SSBPOL(spi->chip_select);

	if (cfg == current_cfg)
		return 0;

	writel(cfg, spi_imx->base + MX51_ECSPI_CONFIG);

	/*
	 * Wait until the changes in the configuration register CONFIGREG
	 * propagate into the hardware. It takes exactly one tick of the
	 * SCLK clock, but we will wait two SCLK clock just to be sure. The
	 * effect of the delay it takes for the hardware to apply changes
	 * is noticable if the SCLK clock run very slow. In such a case, if
	 * the polarity of SCLK should be inverted, the GPIO ChipSelect might
	 * be asserted before the SCLK polarity changes, which would disrupt
	 * the SPI communication as the device on the other end would consider
	 * the change of SCLK polarity as a clock tick already.
	 *
	 * Because spi_imx->spi_bus_clk is only set in prepare_message
	 * callback, iterate over all the transfers in spi_message, find the
	 * one with lowest bus frequency, and use that bus frequency for the
	 * delay calculation. In case all transfers have speed_hz == 0, then
	 * min_speed_hz is ~0 and the resulting delay is zero.
	 */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (!xfer->speed_hz)
			continue;
		min_speed_hz = min(xfer->speed_hz, min_speed_hz);
	}

	delay = (2 * 1000000) / min_speed_hz;
	if (likely(delay < 10))	/* SCLK is faster than 200 kHz */
		udelay(delay);
	else			/* SCLK is _very_ slow */
		usleep_range(delay, delay + 10);

	return 0;
}

static void mx51_configure_cpha(struct spi_imx_data *spi_imx,
				struct spi_device *spi)
{
	bool cpha = (spi->mode & SPI_CPHA);
	bool flip_cpha = (spi->mode & SPI_RX_CPHA_FLIP) && spi_imx->rx_only;
	u32 cfg = readl(spi_imx->base + MX51_ECSPI_CONFIG);

	/* Flip cpha logical value iff flip_cpha */
	cpha ^= flip_cpha;

	if (cpha)
		cfg |= MX51_ECSPI_CONFIG_SCLKPHA(spi->chip_select);
	else
		cfg &= ~MX51_ECSPI_CONFIG_SCLKPHA(spi->chip_select);

	writel(cfg, spi_imx->base + MX51_ECSPI_CONFIG);
}

static int mx51_ecspi_prepare_transfer(struct spi_imx_data *spi_imx,
				       struct spi_device *spi)
{
	u32 ctrl = readl(spi_imx->base + MX51_ECSPI_CTRL);
	u32 clk;

	/* Clear BL field and set the right value */
	ctrl &= ~MX51_ECSPI_CTRL_BL_MASK;
	if (spi_imx->slave_mode && is_imx53_ecspi(spi_imx))
		ctrl |= (spi_imx->slave_burst * 8 - 1)
			<< MX51_ECSPI_CTRL_BL_OFFSET;
	else
		ctrl |= (spi_imx->bits_per_word - 1)
			<< MX51_ECSPI_CTRL_BL_OFFSET;

	/* set clock speed */
	ctrl &= ~(0xf << MX51_ECSPI_CTRL_POSTDIV_OFFSET |
		  0xf << MX51_ECSPI_CTRL_PREDIV_OFFSET);
	ctrl |= mx51_ecspi_clkdiv(spi_imx, spi_imx->spi_bus_clk, &clk);
	spi_imx->spi_bus_clk = clk;

	mx51_configure_cpha(spi_imx, spi);

	/*
	 * ERR009165: work in XHC mode instead of SMC as PIO on the chips
	 * before i.mx6ul.
	 */
	if (spi_imx->usedma && spi_imx->devtype_data->tx_glitch_fixed)
		ctrl |= MX51_ECSPI_CTRL_SMC;
	else
		ctrl &= ~MX51_ECSPI_CTRL_SMC;

	writel(ctrl, spi_imx->base + MX51_ECSPI_CTRL);

	return 0;
}

static void mx51_setup_wml(struct spi_imx_data *spi_imx)
{
	u32 tx_wml = 0;

	if (spi_imx->devtype_data->tx_glitch_fixed)
		tx_wml = spi_imx->wml;
	/*
	 * Configure the DMA register: setup the watermark
	 * and enable DMA request.
	 */
	writel(MX51_ECSPI_DMA_RX_WML(spi_imx->wml - 1) |
		MX51_ECSPI_DMA_TX_WML(tx_wml) |
		MX51_ECSPI_DMA_RXT_WML(spi_imx->wml) |
		MX51_ECSPI_DMA_TEDEN | MX51_ECSPI_DMA_RXDEN |
		MX51_ECSPI_DMA_RXTDEN, spi_imx->base + MX51_ECSPI_DMA);
}

static int mx51_ecspi_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MX51_ECSPI_STAT) & MX51_ECSPI_STAT_RR;
}

static void mx51_ecspi_reset(struct spi_imx_data *spi_imx)
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
#define MX31_CSPICTRL_SMC	(1 << 3)
#define MX31_CSPICTRL_POL	(1 << 4)
#define MX31_CSPICTRL_PHA	(1 << 5)
#define MX31_CSPICTRL_SSCTL	(1 << 6)
#define MX31_CSPICTRL_SSPOL	(1 << 7)
#define MX31_CSPICTRL_BC_SHIFT	8
#define MX35_CSPICTRL_BL_SHIFT	20
#define MX31_CSPICTRL_CS_SHIFT	24
#define MX35_CSPICTRL_CS_SHIFT	12
#define MX31_CSPICTRL_DR_SHIFT	16

#define MX31_CSPI_DMAREG	0x10
#define MX31_DMAREG_RH_DEN	(1<<4)
#define MX31_DMAREG_TH_DEN	(1<<1)

#define MX31_CSPISTATUS		0x14
#define MX31_STATUS_RR		(1 << 3)

#define MX31_CSPI_TESTREG	0x1C
#define MX31_TEST_LBC		(1 << 14)

/* These functions also work for the i.MX35, but be aware that
 * the i.MX35 has a slightly different register layout for bits
 * we do not use here.
 */
static void mx31_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX31_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX31_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void mx31_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX31_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int mx31_prepare_message(struct spi_imx_data *spi_imx,
				struct spi_message *msg)
{
	return 0;
}

static int mx31_prepare_transfer(struct spi_imx_data *spi_imx,
				 struct spi_device *spi)
{
	unsigned int reg = MX31_CSPICTRL_ENABLE | MX31_CSPICTRL_MASTER;
	unsigned int clk;

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, spi_imx->spi_bus_clk, &clk) <<
		MX31_CSPICTRL_DR_SHIFT;
	spi_imx->spi_bus_clk = clk;

	if (is_imx35_cspi(spi_imx)) {
		reg |= (spi_imx->bits_per_word - 1) << MX35_CSPICTRL_BL_SHIFT;
		reg |= MX31_CSPICTRL_SSCTL;
	} else {
		reg |= (spi_imx->bits_per_word - 1) << MX31_CSPICTRL_BC_SHIFT;
	}

	if (spi->mode & SPI_CPHA)
		reg |= MX31_CSPICTRL_PHA;
	if (spi->mode & SPI_CPOL)
		reg |= MX31_CSPICTRL_POL;
	if (spi->mode & SPI_CS_HIGH)
		reg |= MX31_CSPICTRL_SSPOL;
	if (!spi->cs_gpiod)
		reg |= (spi->chip_select) <<
			(is_imx35_cspi(spi_imx) ? MX35_CSPICTRL_CS_SHIFT :
						  MX31_CSPICTRL_CS_SHIFT);

	if (spi_imx->usedma)
		reg |= MX31_CSPICTRL_SMC;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	reg = readl(spi_imx->base + MX31_CSPI_TESTREG);
	if (spi->mode & SPI_LOOP)
		reg |= MX31_TEST_LBC;
	else
		reg &= ~MX31_TEST_LBC;
	writel(reg, spi_imx->base + MX31_CSPI_TESTREG);

	if (spi_imx->usedma) {
		/*
		 * configure DMA requests when RXFIFO is half full and
		 * when TXFIFO is half empty
		 */
		writel(MX31_DMAREG_RH_DEN | MX31_DMAREG_TH_DEN,
			spi_imx->base + MX31_CSPI_DMAREG);
	}

	return 0;
}

static int mx31_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MX31_CSPISTATUS) & MX31_STATUS_RR;
}

static void mx31_reset(struct spi_imx_data *spi_imx)
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

static void mx21_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX21_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX21_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void mx21_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX21_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int mx21_prepare_message(struct spi_imx_data *spi_imx,
				struct spi_message *msg)
{
	return 0;
}

static int mx21_prepare_transfer(struct spi_imx_data *spi_imx,
				 struct spi_device *spi)
{
	unsigned int reg = MX21_CSPICTRL_ENABLE | MX21_CSPICTRL_MASTER;
	unsigned int max = is_imx27_cspi(spi_imx) ? 16 : 18;
	unsigned int clk;

	reg |= spi_imx_clkdiv_1(spi_imx->spi_clk, spi_imx->spi_bus_clk, max, &clk)
		<< MX21_CSPICTRL_DR_SHIFT;
	spi_imx->spi_bus_clk = clk;

	reg |= spi_imx->bits_per_word - 1;

	if (spi->mode & SPI_CPHA)
		reg |= MX21_CSPICTRL_PHA;
	if (spi->mode & SPI_CPOL)
		reg |= MX21_CSPICTRL_POL;
	if (spi->mode & SPI_CS_HIGH)
		reg |= MX21_CSPICTRL_SSPOL;
	if (!spi->cs_gpiod)
		reg |= spi->chip_select << MX21_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int mx21_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MXC_CSPIINT) & MX21_INTREG_RR;
}

static void mx21_reset(struct spi_imx_data *spi_imx)
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

static void mx1_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX1_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX1_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void mx1_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX1_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int mx1_prepare_message(struct spi_imx_data *spi_imx,
			       struct spi_message *msg)
{
	return 0;
}

static int mx1_prepare_transfer(struct spi_imx_data *spi_imx,
				struct spi_device *spi)
{
	unsigned int reg = MX1_CSPICTRL_ENABLE | MX1_CSPICTRL_MASTER;
	unsigned int clk;

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, spi_imx->spi_bus_clk, &clk) <<
		MX1_CSPICTRL_DR_SHIFT;
	spi_imx->spi_bus_clk = clk;

	reg |= spi_imx->bits_per_word - 1;

	if (spi->mode & SPI_CPHA)
		reg |= MX1_CSPICTRL_PHA;
	if (spi->mode & SPI_CPOL)
		reg |= MX1_CSPICTRL_POL;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int mx1_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MXC_CSPIINT) & MX1_INTREG_RR;
}

static void mx1_reset(struct spi_imx_data *spi_imx)
{
	writel(1, spi_imx->base + MXC_RESET);
}

static struct spi_imx_devtype_data imx1_cspi_devtype_data = {
	.intctrl = mx1_intctrl,
	.prepare_message = mx1_prepare_message,
	.prepare_transfer = mx1_prepare_transfer,
	.trigger = mx1_trigger,
	.rx_available = mx1_rx_available,
	.reset = mx1_reset,
	.fifo_size = 8,
	.has_dmamode = false,
	.dynamic_burst = false,
	.has_slavemode = false,
	.devtype = IMX1_CSPI,
};

static struct spi_imx_devtype_data imx21_cspi_devtype_data = {
	.intctrl = mx21_intctrl,
	.prepare_message = mx21_prepare_message,
	.prepare_transfer = mx21_prepare_transfer,
	.trigger = mx21_trigger,
	.rx_available = mx21_rx_available,
	.reset = mx21_reset,
	.fifo_size = 8,
	.has_dmamode = false,
	.dynamic_burst = false,
	.has_slavemode = false,
	.devtype = IMX21_CSPI,
};

static struct spi_imx_devtype_data imx27_cspi_devtype_data = {
	/* i.mx27 cspi shares the functions with i.mx21 one */
	.intctrl = mx21_intctrl,
	.prepare_message = mx21_prepare_message,
	.prepare_transfer = mx21_prepare_transfer,
	.trigger = mx21_trigger,
	.rx_available = mx21_rx_available,
	.reset = mx21_reset,
	.fifo_size = 8,
	.has_dmamode = false,
	.dynamic_burst = false,
	.has_slavemode = false,
	.devtype = IMX27_CSPI,
};

static struct spi_imx_devtype_data imx31_cspi_devtype_data = {
	.intctrl = mx31_intctrl,
	.prepare_message = mx31_prepare_message,
	.prepare_transfer = mx31_prepare_transfer,
	.trigger = mx31_trigger,
	.rx_available = mx31_rx_available,
	.reset = mx31_reset,
	.fifo_size = 8,
	.has_dmamode = false,
	.dynamic_burst = false,
	.has_slavemode = false,
	.devtype = IMX31_CSPI,
};

static struct spi_imx_devtype_data imx35_cspi_devtype_data = {
	/* i.mx35 and later cspi shares the functions with i.mx31 one */
	.intctrl = mx31_intctrl,
	.prepare_message = mx31_prepare_message,
	.prepare_transfer = mx31_prepare_transfer,
	.trigger = mx31_trigger,
	.rx_available = mx31_rx_available,
	.reset = mx31_reset,
	.fifo_size = 8,
	.has_dmamode = true,
	.dynamic_burst = false,
	.has_slavemode = false,
	.devtype = IMX35_CSPI,
};

static struct spi_imx_devtype_data imx51_ecspi_devtype_data = {
	.intctrl = mx51_ecspi_intctrl,
	.prepare_message = mx51_ecspi_prepare_message,
	.prepare_transfer = mx51_ecspi_prepare_transfer,
	.trigger = mx51_ecspi_trigger,
	.rx_available = mx51_ecspi_rx_available,
	.reset = mx51_ecspi_reset,
	.setup_wml = mx51_setup_wml,
	.fifo_size = 64,
	.has_dmamode = true,
	.dynamic_burst = true,
	.has_slavemode = true,
	.disable = mx51_ecspi_disable,
	.devtype = IMX51_ECSPI,
};

static struct spi_imx_devtype_data imx53_ecspi_devtype_data = {
	.intctrl = mx51_ecspi_intctrl,
	.prepare_message = mx51_ecspi_prepare_message,
	.prepare_transfer = mx51_ecspi_prepare_transfer,
	.trigger = mx51_ecspi_trigger,
	.rx_available = mx51_ecspi_rx_available,
	.reset = mx51_ecspi_reset,
	.fifo_size = 64,
	.has_dmamode = true,
	.has_slavemode = true,
	.disable = mx51_ecspi_disable,
	.devtype = IMX53_ECSPI,
};

static struct spi_imx_devtype_data imx6ul_ecspi_devtype_data = {
	.intctrl = mx51_ecspi_intctrl,
	.prepare_message = mx51_ecspi_prepare_message,
	.prepare_transfer = mx51_ecspi_prepare_transfer,
	.trigger = mx51_ecspi_trigger,
	.rx_available = mx51_ecspi_rx_available,
	.reset = mx51_ecspi_reset,
	.setup_wml = mx51_setup_wml,
	.fifo_size = 64,
	.has_dmamode = true,
	.dynamic_burst = true,
	.has_slavemode = true,
	.tx_glitch_fixed = true,
	.disable = mx51_ecspi_disable,
	.devtype = IMX51_ECSPI,
};

static const struct of_device_id spi_imx_dt_ids[] = {
	{ .compatible = "fsl,imx1-cspi", .data = &imx1_cspi_devtype_data, },
	{ .compatible = "fsl,imx21-cspi", .data = &imx21_cspi_devtype_data, },
	{ .compatible = "fsl,imx27-cspi", .data = &imx27_cspi_devtype_data, },
	{ .compatible = "fsl,imx31-cspi", .data = &imx31_cspi_devtype_data, },
	{ .compatible = "fsl,imx35-cspi", .data = &imx35_cspi_devtype_data, },
	{ .compatible = "fsl,imx51-ecspi", .data = &imx51_ecspi_devtype_data, },
	{ .compatible = "fsl,imx53-ecspi", .data = &imx53_ecspi_devtype_data, },
	{ .compatible = "fsl,imx6ul-ecspi", .data = &imx6ul_ecspi_devtype_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spi_imx_dt_ids);

static void spi_imx_set_burst_len(struct spi_imx_data *spi_imx, int n_bits)
{
	u32 ctrl;

	ctrl = readl(spi_imx->base + MX51_ECSPI_CTRL);
	ctrl &= ~MX51_ECSPI_CTRL_BL_MASK;
	ctrl |= ((n_bits - 1) << MX51_ECSPI_CTRL_BL_OFFSET);
	writel(ctrl, spi_imx->base + MX51_ECSPI_CTRL);
}

static void spi_imx_push(struct spi_imx_data *spi_imx)
{
	unsigned int burst_len;

	/*
	 * Reload the FIFO when the remaining bytes to be transferred in the
	 * current burst is 0. This only applies when bits_per_word is a
	 * multiple of 8.
	 */
	if (!spi_imx->remainder) {
		if (spi_imx->dynamic_burst) {

			/* We need to deal unaligned data first */
			burst_len = spi_imx->count % MX51_ECSPI_CTRL_MAX_BURST;

			if (!burst_len)
				burst_len = MX51_ECSPI_CTRL_MAX_BURST;

			spi_imx_set_burst_len(spi_imx, burst_len * 8);

			spi_imx->remainder = burst_len;
		} else {
			spi_imx->remainder = spi_imx_bytes_per_word(spi_imx->bits_per_word);
		}
	}

	while (spi_imx->txfifo < spi_imx->devtype_data->fifo_size) {
		if (!spi_imx->count)
			break;
		if (spi_imx->dynamic_burst &&
		    spi_imx->txfifo >= DIV_ROUND_UP(spi_imx->remainder, 4))
			break;
		spi_imx->tx(spi_imx);
		spi_imx->txfifo++;
	}

	if (!spi_imx->slave_mode)
		spi_imx->devtype_data->trigger(spi_imx);
}

static irqreturn_t spi_imx_isr(int irq, void *dev_id)
{
	struct spi_imx_data *spi_imx = dev_id;

	while (spi_imx->txfifo &&
	       spi_imx->devtype_data->rx_available(spi_imx)) {
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

static int spi_imx_dma_configure(struct spi_controller *controller)
{
	int ret;
	enum dma_slave_buswidth buswidth;
	struct dma_slave_config rx = {}, tx = {};
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(controller);

	switch (spi_imx_bytes_per_word(spi_imx->bits_per_word)) {
	case 4:
		buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	case 2:
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 1:
		buswidth = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	default:
		return -EINVAL;
	}

	tx.direction = DMA_MEM_TO_DEV;
	tx.dst_addr = spi_imx->base_phys + MXC_CSPITXDATA;
	tx.dst_addr_width = buswidth;
	tx.dst_maxburst = spi_imx->wml;
	ret = dmaengine_slave_config(controller->dma_tx, &tx);
	if (ret) {
		dev_err(spi_imx->dev, "TX dma configuration failed with %d\n", ret);
		return ret;
	}

	rx.direction = DMA_DEV_TO_MEM;
	rx.src_addr = spi_imx->base_phys + MXC_CSPIRXDATA;
	rx.src_addr_width = buswidth;
	rx.src_maxburst = spi_imx->wml;
	ret = dmaengine_slave_config(controller->dma_rx, &rx);
	if (ret) {
		dev_err(spi_imx->dev, "RX dma configuration failed with %d\n", ret);
		return ret;
	}

	return 0;
}

static int spi_imx_setupxfer(struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(spi->controller);

	if (!t)
		return 0;

	if (!t->speed_hz) {
		if (!spi->max_speed_hz) {
			dev_err(&spi->dev, "no speed_hz provided!\n");
			return -EINVAL;
		}
		dev_dbg(&spi->dev, "using spi->max_speed_hz!\n");
		spi_imx->spi_bus_clk = spi->max_speed_hz;
	} else
		spi_imx->spi_bus_clk = t->speed_hz;

	spi_imx->bits_per_word = t->bits_per_word;

	/*
	 * Initialize the functions for transfer. To transfer non byte-aligned
	 * words, we have to use multiple word-size bursts, we can't use
	 * dynamic_burst in that case.
	 */
	if (spi_imx->devtype_data->dynamic_burst && !spi_imx->slave_mode &&
	    !(spi->mode & SPI_CS_WORD) &&
	    (spi_imx->bits_per_word == 8 ||
	    spi_imx->bits_per_word == 16 ||
	    spi_imx->bits_per_word == 32)) {

		spi_imx->rx = spi_imx_buf_rx_swap;
		spi_imx->tx = spi_imx_buf_tx_swap;
		spi_imx->dynamic_burst = 1;

	} else {
		if (spi_imx->bits_per_word <= 8) {
			spi_imx->rx = spi_imx_buf_rx_u8;
			spi_imx->tx = spi_imx_buf_tx_u8;
		} else if (spi_imx->bits_per_word <= 16) {
			spi_imx->rx = spi_imx_buf_rx_u16;
			spi_imx->tx = spi_imx_buf_tx_u16;
		} else {
			spi_imx->rx = spi_imx_buf_rx_u32;
			spi_imx->tx = spi_imx_buf_tx_u32;
		}
		spi_imx->dynamic_burst = 0;
	}

	if (spi_imx_can_dma(spi_imx->controller, spi, t))
		spi_imx->usedma = true;
	else
		spi_imx->usedma = false;

	spi_imx->rx_only = ((t->tx_buf == NULL)
			|| (t->tx_buf == spi->controller->dummy_tx));

	if (is_imx53_ecspi(spi_imx) && spi_imx->slave_mode) {
		spi_imx->rx = mx53_ecspi_rx_slave;
		spi_imx->tx = mx53_ecspi_tx_slave;
		spi_imx->slave_burst = t->len;
	}

	spi_imx->devtype_data->prepare_transfer(spi_imx, spi);

	return 0;
}

static void spi_imx_sdma_exit(struct spi_imx_data *spi_imx)
{
	struct spi_controller *controller = spi_imx->controller;

	if (controller->dma_rx) {
		dma_release_channel(controller->dma_rx);
		controller->dma_rx = NULL;
	}

	if (controller->dma_tx) {
		dma_release_channel(controller->dma_tx);
		controller->dma_tx = NULL;
	}
}

static int spi_imx_sdma_init(struct device *dev, struct spi_imx_data *spi_imx,
			     struct spi_controller *controller)
{
	int ret;

	spi_imx->wml = spi_imx->devtype_data->fifo_size / 2;

	/* Prepare for TX DMA: */
	controller->dma_tx = dma_request_chan(dev, "tx");
	if (IS_ERR(controller->dma_tx)) {
		ret = PTR_ERR(controller->dma_tx);
		dev_dbg(dev, "can't get the TX DMA channel, error %d!\n", ret);
		controller->dma_tx = NULL;
		goto err;
	}

	/* Prepare for RX : */
	controller->dma_rx = dma_request_chan(dev, "rx");
	if (IS_ERR(controller->dma_rx)) {
		ret = PTR_ERR(controller->dma_rx);
		dev_dbg(dev, "can't get the RX DMA channel, error %d\n", ret);
		controller->dma_rx = NULL;
		goto err;
	}

	init_completion(&spi_imx->dma_rx_completion);
	init_completion(&spi_imx->dma_tx_completion);
	controller->can_dma = spi_imx_can_dma;
	controller->max_dma_len = MAX_SDMA_BD_BYTES;
	spi_imx->controller->flags = SPI_CONTROLLER_MUST_RX |
					 SPI_CONTROLLER_MUST_TX;

	return 0;
err:
	spi_imx_sdma_exit(spi_imx);
	return ret;
}

static void spi_imx_dma_rx_callback(void *cookie)
{
	struct spi_imx_data *spi_imx = (struct spi_imx_data *)cookie;

	complete(&spi_imx->dma_rx_completion);
}

static void spi_imx_dma_tx_callback(void *cookie)
{
	struct spi_imx_data *spi_imx = (struct spi_imx_data *)cookie;

	complete(&spi_imx->dma_tx_completion);
}

static int spi_imx_calculate_timeout(struct spi_imx_data *spi_imx, int size)
{
	unsigned long timeout = 0;

	/* Time with actual data transfer and CS change delay related to HW */
	timeout = (8 + 4) * size / spi_imx->spi_bus_clk;

	/* Add extra second for scheduler related activities */
	timeout += 1;

	/* Double calculated timeout */
	return msecs_to_jiffies(2 * timeout * MSEC_PER_SEC);
}

static int spi_imx_dma_transfer(struct spi_imx_data *spi_imx,
				struct spi_transfer *transfer)
{
	struct dma_async_tx_descriptor *desc_tx, *desc_rx;
	unsigned long transfer_timeout;
	unsigned long timeout;
	struct spi_controller *controller = spi_imx->controller;
	struct sg_table *tx = &transfer->tx_sg, *rx = &transfer->rx_sg;
	struct scatterlist *last_sg = sg_last(rx->sgl, rx->nents);
	unsigned int bytes_per_word, i;
	int ret;

	/* Get the right burst length from the last sg to ensure no tail data */
	bytes_per_word = spi_imx_bytes_per_word(transfer->bits_per_word);
	for (i = spi_imx->devtype_data->fifo_size / 2; i > 0; i--) {
		if (!(sg_dma_len(last_sg) % (i * bytes_per_word)))
			break;
	}
	/* Use 1 as wml in case no available burst length got */
	if (i == 0)
		i = 1;

	spi_imx->wml =  i;

	ret = spi_imx_dma_configure(controller);
	if (ret)
		goto dma_failure_no_start;

	if (!spi_imx->devtype_data->setup_wml) {
		dev_err(spi_imx->dev, "No setup_wml()?\n");
		ret = -EINVAL;
		goto dma_failure_no_start;
	}
	spi_imx->devtype_data->setup_wml(spi_imx);

	/*
	 * The TX DMA setup starts the transfer, so make sure RX is configured
	 * before TX.
	 */
	desc_rx = dmaengine_prep_slave_sg(controller->dma_rx,
				rx->sgl, rx->nents, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_rx) {
		ret = -EINVAL;
		goto dma_failure_no_start;
	}

	desc_rx->callback = spi_imx_dma_rx_callback;
	desc_rx->callback_param = (void *)spi_imx;
	dmaengine_submit(desc_rx);
	reinit_completion(&spi_imx->dma_rx_completion);
	dma_async_issue_pending(controller->dma_rx);

	desc_tx = dmaengine_prep_slave_sg(controller->dma_tx,
				tx->sgl, tx->nents, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_tx) {
		dmaengine_terminate_all(controller->dma_tx);
		dmaengine_terminate_all(controller->dma_rx);
		return -EINVAL;
	}

	desc_tx->callback = spi_imx_dma_tx_callback;
	desc_tx->callback_param = (void *)spi_imx;
	dmaengine_submit(desc_tx);
	reinit_completion(&spi_imx->dma_tx_completion);
	dma_async_issue_pending(controller->dma_tx);

	transfer_timeout = spi_imx_calculate_timeout(spi_imx, transfer->len);

	/* Wait SDMA to finish the data transfer.*/
	timeout = wait_for_completion_timeout(&spi_imx->dma_tx_completion,
						transfer_timeout);
	if (!timeout) {
		dev_err(spi_imx->dev, "I/O Error in DMA TX\n");
		dmaengine_terminate_all(controller->dma_tx);
		dmaengine_terminate_all(controller->dma_rx);
		return -ETIMEDOUT;
	}

	timeout = wait_for_completion_timeout(&spi_imx->dma_rx_completion,
					      transfer_timeout);
	if (!timeout) {
		dev_err(&controller->dev, "I/O Error in DMA RX\n");
		spi_imx->devtype_data->reset(spi_imx);
		dmaengine_terminate_all(controller->dma_rx);
		return -ETIMEDOUT;
	}

	return 0;
/* fallback to pio */
dma_failure_no_start:
	transfer->error |= SPI_TRANS_FAIL_NO_START;
	return ret;
}

static int spi_imx_pio_transfer(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(spi->controller);
	unsigned long transfer_timeout;
	unsigned long timeout;

	spi_imx->tx_buf = transfer->tx_buf;
	spi_imx->rx_buf = transfer->rx_buf;
	spi_imx->count = transfer->len;
	spi_imx->txfifo = 0;
	spi_imx->remainder = 0;

	reinit_completion(&spi_imx->xfer_done);

	spi_imx_push(spi_imx);

	spi_imx->devtype_data->intctrl(spi_imx, MXC_INT_TE);

	transfer_timeout = spi_imx_calculate_timeout(spi_imx, transfer->len);

	timeout = wait_for_completion_timeout(&spi_imx->xfer_done,
					      transfer_timeout);
	if (!timeout) {
		dev_err(&spi->dev, "I/O Error in PIO\n");
		spi_imx->devtype_data->reset(spi_imx);
		return -ETIMEDOUT;
	}

	return 0;
}

static int spi_imx_poll_transfer(struct spi_device *spi,
				 struct spi_transfer *transfer)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(spi->controller);
	unsigned long timeout;

	spi_imx->tx_buf = transfer->tx_buf;
	spi_imx->rx_buf = transfer->rx_buf;
	spi_imx->count = transfer->len;
	spi_imx->txfifo = 0;
	spi_imx->remainder = 0;

	/* fill in the fifo before timeout calculations if we are
	 * interrupted here, then the data is getting transferred by
	 * the HW while we are interrupted
	 */
	spi_imx_push(spi_imx);

	timeout = spi_imx_calculate_timeout(spi_imx, transfer->len) + jiffies;
	while (spi_imx->txfifo) {
		/* RX */
		while (spi_imx->txfifo &&
		       spi_imx->devtype_data->rx_available(spi_imx)) {
			spi_imx->rx(spi_imx);
			spi_imx->txfifo--;
		}

		/* TX */
		if (spi_imx->count) {
			spi_imx_push(spi_imx);
			continue;
		}

		if (spi_imx->txfifo &&
		    time_after(jiffies, timeout)) {

			dev_err_ratelimited(&spi->dev,
					    "timeout period reached: jiffies: %lu- falling back to interrupt mode\n",
					    jiffies - timeout);

			/* fall back to interrupt mode */
			return spi_imx_pio_transfer(spi, transfer);
		}
	}

	return 0;
}

static int spi_imx_pio_transfer_slave(struct spi_device *spi,
				      struct spi_transfer *transfer)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(spi->controller);
	int ret = 0;

	if (is_imx53_ecspi(spi_imx) &&
	    transfer->len > MX53_MAX_TRANSFER_BYTES) {
		dev_err(&spi->dev, "Transaction too big, max size is %d bytes\n",
			MX53_MAX_TRANSFER_BYTES);
		return -EMSGSIZE;
	}

	spi_imx->tx_buf = transfer->tx_buf;
	spi_imx->rx_buf = transfer->rx_buf;
	spi_imx->count = transfer->len;
	spi_imx->txfifo = 0;
	spi_imx->remainder = 0;

	reinit_completion(&spi_imx->xfer_done);
	spi_imx->slave_aborted = false;

	spi_imx_push(spi_imx);

	spi_imx->devtype_data->intctrl(spi_imx, MXC_INT_TE | MXC_INT_RDR);

	if (wait_for_completion_interruptible(&spi_imx->xfer_done) ||
	    spi_imx->slave_aborted) {
		dev_dbg(&spi->dev, "interrupted\n");
		ret = -EINTR;
	}

	/* ecspi has a HW issue when works in Slave mode,
	 * after 64 words writtern to TXFIFO, even TXFIFO becomes empty,
	 * ECSPI_TXDATA keeps shift out the last word data,
	 * so we have to disable ECSPI when in slave mode after the
	 * transfer completes
	 */
	if (spi_imx->devtype_data->disable)
		spi_imx->devtype_data->disable(spi_imx);

	return ret;
}

static int spi_imx_transfer_one(struct spi_controller *controller,
				struct spi_device *spi,
				struct spi_transfer *transfer)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(spi->controller);
	unsigned long hz_per_byte, byte_limit;

	spi_imx_setupxfer(spi, transfer);
	transfer->effective_speed_hz = spi_imx->spi_bus_clk;

	/* flush rxfifo before transfer */
	while (spi_imx->devtype_data->rx_available(spi_imx))
		readl(spi_imx->base + MXC_CSPIRXDATA);

	if (spi_imx->slave_mode)
		return spi_imx_pio_transfer_slave(spi, transfer);

	/*
	 * If we decided in spi_imx_can_dma() that we want to do a DMA
	 * transfer, the SPI transfer has already been mapped, so we
	 * have to do the DMA transfer here.
	 */
	if (spi_imx->usedma)
		return spi_imx_dma_transfer(spi_imx, transfer);
	/*
	 * Calculate the estimated time in us the transfer runs. Find
	 * the number of Hz per byte per polling limit.
	 */
	hz_per_byte = polling_limit_us ? ((8 + 4) * USEC_PER_SEC) / polling_limit_us : 0;
	byte_limit = hz_per_byte ? transfer->effective_speed_hz / hz_per_byte : 1;

	/* run in polling mode for short transfers */
	if (transfer->len < byte_limit)
		return spi_imx_poll_transfer(spi, transfer);

	return spi_imx_pio_transfer(spi, transfer);
}

static int spi_imx_setup(struct spi_device *spi)
{
	dev_dbg(&spi->dev, "%s: mode %d, %u bpw, %d hz\n", __func__,
		 spi->mode, spi->bits_per_word, spi->max_speed_hz);

	return 0;
}

static void spi_imx_cleanup(struct spi_device *spi)
{
}

static int
spi_imx_prepare_message(struct spi_controller *controller, struct spi_message *msg)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(controller);
	int ret;

	ret = pm_runtime_resume_and_get(spi_imx->dev);
	if (ret < 0) {
		dev_err(spi_imx->dev, "failed to enable clock\n");
		return ret;
	}

	ret = spi_imx->devtype_data->prepare_message(spi_imx, msg);
	if (ret) {
		pm_runtime_mark_last_busy(spi_imx->dev);
		pm_runtime_put_autosuspend(spi_imx->dev);
	}

	return ret;
}

static int
spi_imx_unprepare_message(struct spi_controller *controller, struct spi_message *msg)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(controller);

	pm_runtime_mark_last_busy(spi_imx->dev);
	pm_runtime_put_autosuspend(spi_imx->dev);
	return 0;
}

static int spi_imx_slave_abort(struct spi_controller *controller)
{
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(controller);

	spi_imx->slave_aborted = true;
	complete(&spi_imx->xfer_done);

	return 0;
}

static int spi_imx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spi_controller *controller;
	struct spi_imx_data *spi_imx;
	struct resource *res;
	int ret, irq, spi_drctl;
	const struct spi_imx_devtype_data *devtype_data =
			of_device_get_match_data(&pdev->dev);
	bool slave_mode;
	u32 val;

	slave_mode = devtype_data->has_slavemode &&
			of_property_read_bool(np, "spi-slave");
	if (slave_mode)
		controller = spi_alloc_slave(&pdev->dev,
					     sizeof(struct spi_imx_data));
	else
		controller = spi_alloc_master(&pdev->dev,
					      sizeof(struct spi_imx_data));
	if (!controller)
		return -ENOMEM;

	ret = of_property_read_u32(np, "fsl,spi-rdy-drctl", &spi_drctl);
	if ((ret < 0) || (spi_drctl >= 0x3)) {
		/* '11' is reserved */
		spi_drctl = 0;
	}

	platform_set_drvdata(pdev, controller);

	controller->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 32);
	controller->bus_num = np ? -1 : pdev->id;
	controller->use_gpio_descriptors = true;

	spi_imx = spi_controller_get_devdata(controller);
	spi_imx->controller = controller;
	spi_imx->dev = &pdev->dev;
	spi_imx->slave_mode = slave_mode;

	spi_imx->devtype_data = devtype_data;

	/*
	 * Get number of chip selects from device properties. This can be
	 * coming from device tree or boardfiles, if it is not defined,
	 * a default value of 3 chip selects will be used, as all the legacy
	 * board files have <= 3 chip selects.
	 */
	if (!device_property_read_u32(&pdev->dev, "num-cs", &val))
		controller->num_chipselect = val;
	else
		controller->num_chipselect = 3;

	spi_imx->controller->transfer_one = spi_imx_transfer_one;
	spi_imx->controller->setup = spi_imx_setup;
	spi_imx->controller->cleanup = spi_imx_cleanup;
	spi_imx->controller->prepare_message = spi_imx_prepare_message;
	spi_imx->controller->unprepare_message = spi_imx_unprepare_message;
	spi_imx->controller->slave_abort = spi_imx_slave_abort;
	spi_imx->controller->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_NO_CS;

	if (is_imx35_cspi(spi_imx) || is_imx51_ecspi(spi_imx) ||
	    is_imx53_ecspi(spi_imx))
		spi_imx->controller->mode_bits |= SPI_LOOP | SPI_READY;

	if (is_imx51_ecspi(spi_imx) || is_imx53_ecspi(spi_imx))
		spi_imx->controller->mode_bits |= SPI_RX_CPHA_FLIP;

	if (is_imx51_ecspi(spi_imx) &&
	    device_property_read_u32(&pdev->dev, "cs-gpios", NULL))
		/*
		 * When using HW-CS implementing SPI_CS_WORD can be done by just
		 * setting the burst length to the word size. This is
		 * considerably faster than manually controlling the CS.
		 */
		spi_imx->controller->mode_bits |= SPI_CS_WORD;

	spi_imx->spi_drctl = spi_drctl;

	init_completion(&spi_imx->xfer_done);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spi_imx->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spi_imx->base)) {
		ret = PTR_ERR(spi_imx->base);
		goto out_controller_put;
	}
	spi_imx->base_phys = res->start;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto out_controller_put;
	}

	ret = devm_request_irq(&pdev->dev, irq, spi_imx_isr, 0,
			       dev_name(&pdev->dev), spi_imx);
	if (ret) {
		dev_err(&pdev->dev, "can't get irq%d: %d\n", irq, ret);
		goto out_controller_put;
	}

	spi_imx->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(spi_imx->clk_ipg)) {
		ret = PTR_ERR(spi_imx->clk_ipg);
		goto out_controller_put;
	}

	spi_imx->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(spi_imx->clk_per)) {
		ret = PTR_ERR(spi_imx->clk_per);
		goto out_controller_put;
	}

	ret = clk_prepare_enable(spi_imx->clk_per);
	if (ret)
		goto out_controller_put;

	ret = clk_prepare_enable(spi_imx->clk_ipg);
	if (ret)
		goto out_put_per;

	pm_runtime_set_autosuspend_delay(spi_imx->dev, MXC_RPM_TIMEOUT);
	pm_runtime_use_autosuspend(spi_imx->dev);
	pm_runtime_get_noresume(spi_imx->dev);
	pm_runtime_set_active(spi_imx->dev);
	pm_runtime_enable(spi_imx->dev);

	spi_imx->spi_clk = clk_get_rate(spi_imx->clk_per);
	/*
	 * Only validated on i.mx35 and i.mx6 now, can remove the constraint
	 * if validated on other chips.
	 */
	if (spi_imx->devtype_data->has_dmamode) {
		ret = spi_imx_sdma_init(&pdev->dev, spi_imx, controller);
		if (ret == -EPROBE_DEFER)
			goto out_runtime_pm_put;

		if (ret < 0)
			dev_dbg(&pdev->dev, "dma setup error %d, use pio\n",
				ret);
	}

	spi_imx->devtype_data->reset(spi_imx);

	spi_imx->devtype_data->intctrl(spi_imx, 0);

	controller->dev.of_node = pdev->dev.of_node;
	ret = spi_register_controller(controller);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "register controller failed\n");
		goto out_register_controller;
	}

	pm_runtime_mark_last_busy(spi_imx->dev);
	pm_runtime_put_autosuspend(spi_imx->dev);

	return ret;

out_register_controller:
	if (spi_imx->devtype_data->has_dmamode)
		spi_imx_sdma_exit(spi_imx);
out_runtime_pm_put:
	pm_runtime_dont_use_autosuspend(spi_imx->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(spi_imx->dev);

	clk_disable_unprepare(spi_imx->clk_ipg);
out_put_per:
	clk_disable_unprepare(spi_imx->clk_per);
out_controller_put:
	spi_controller_put(controller);

	return ret;
}

static int spi_imx_remove(struct platform_device *pdev)
{
	struct spi_controller *controller = platform_get_drvdata(pdev);
	struct spi_imx_data *spi_imx = spi_controller_get_devdata(controller);
	int ret;

	spi_unregister_controller(controller);

	ret = pm_runtime_resume_and_get(spi_imx->dev);
	if (ret < 0) {
		dev_err(spi_imx->dev, "failed to enable clock\n");
		return ret;
	}

	writel(0, spi_imx->base + MXC_CSPICTRL);

	pm_runtime_dont_use_autosuspend(spi_imx->dev);
	pm_runtime_put_sync(spi_imx->dev);
	pm_runtime_disable(spi_imx->dev);

	spi_imx_sdma_exit(spi_imx);

	return 0;
}

static int __maybe_unused spi_imx_runtime_resume(struct device *dev)
{
	struct spi_controller *controller = dev_get_drvdata(dev);
	struct spi_imx_data *spi_imx;
	int ret;

	spi_imx = spi_controller_get_devdata(controller);

	ret = clk_prepare_enable(spi_imx->clk_per);
	if (ret)
		return ret;

	ret = clk_prepare_enable(spi_imx->clk_ipg);
	if (ret) {
		clk_disable_unprepare(spi_imx->clk_per);
		return ret;
	}

	return 0;
}

static int __maybe_unused spi_imx_runtime_suspend(struct device *dev)
{
	struct spi_controller *controller = dev_get_drvdata(dev);
	struct spi_imx_data *spi_imx;

	spi_imx = spi_controller_get_devdata(controller);

	clk_disable_unprepare(spi_imx->clk_per);
	clk_disable_unprepare(spi_imx->clk_ipg);

	return 0;
}

static int __maybe_unused spi_imx_suspend(struct device *dev)
{
	pinctrl_pm_select_sleep_state(dev);
	return 0;
}

static int __maybe_unused spi_imx_resume(struct device *dev)
{
	pinctrl_pm_select_default_state(dev);
	return 0;
}

static const struct dev_pm_ops imx_spi_pm = {
	SET_RUNTIME_PM_OPS(spi_imx_runtime_suspend,
				spi_imx_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(spi_imx_suspend, spi_imx_resume)
};

static struct platform_driver spi_imx_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = spi_imx_dt_ids,
		   .pm = &imx_spi_pm,
	},
	.probe = spi_imx_probe,
	.remove = spi_imx_remove,
};
module_platform_driver(spi_imx_driver);

MODULE_DESCRIPTION("i.MX SPI Controller driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
