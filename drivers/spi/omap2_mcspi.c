/*
 * OMAP2 McSPI controller driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation
 * Author:	Samuel Ortiz <samuel.ortiz@nokia.com> and
 *		Juha Yrjölä <juha.yrjola@nokia.com>
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
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <linux/spi/spi.h>

#include <asm/arch/dma.h>
#include <asm/arch/clock.h>


#define OMAP2_MCSPI_MAX_FREQ		48000000

#define OMAP2_MCSPI_REVISION		0x00
#define OMAP2_MCSPI_SYSCONFIG		0x10
#define OMAP2_MCSPI_SYSSTATUS		0x14
#define OMAP2_MCSPI_IRQSTATUS		0x18
#define OMAP2_MCSPI_IRQENABLE		0x1c
#define OMAP2_MCSPI_WAKEUPENABLE	0x20
#define OMAP2_MCSPI_SYST		0x24
#define OMAP2_MCSPI_MODULCTRL		0x28

/* per-channel banks, 0x14 bytes each, first is: */
#define OMAP2_MCSPI_CHCONF0		0x2c
#define OMAP2_MCSPI_CHSTAT0		0x30
#define OMAP2_MCSPI_CHCTRL0		0x34
#define OMAP2_MCSPI_TX0			0x38
#define OMAP2_MCSPI_RX0			0x3c

/* per-register bitmasks: */

#define OMAP2_MCSPI_SYSCONFIG_AUTOIDLE	(1 << 0)
#define OMAP2_MCSPI_SYSCONFIG_SOFTRESET	(1 << 1)

#define OMAP2_MCSPI_SYSSTATUS_RESETDONE	(1 << 0)

#define OMAP2_MCSPI_MODULCTRL_SINGLE	(1 << 0)
#define OMAP2_MCSPI_MODULCTRL_MS	(1 << 2)
#define OMAP2_MCSPI_MODULCTRL_STEST	(1 << 3)

#define OMAP2_MCSPI_CHCONF_PHA		(1 << 0)
#define OMAP2_MCSPI_CHCONF_POL		(1 << 1)
#define OMAP2_MCSPI_CHCONF_CLKD_MASK	(0x0f << 2)
#define OMAP2_MCSPI_CHCONF_EPOL		(1 << 6)
#define OMAP2_MCSPI_CHCONF_WL_MASK	(0x1f << 7)
#define OMAP2_MCSPI_CHCONF_TRM_RX_ONLY	(0x01 << 12)
#define OMAP2_MCSPI_CHCONF_TRM_TX_ONLY	(0x02 << 12)
#define OMAP2_MCSPI_CHCONF_TRM_MASK	(0x03 << 12)
#define OMAP2_MCSPI_CHCONF_DMAW		(1 << 14)
#define OMAP2_MCSPI_CHCONF_DMAR		(1 << 15)
#define OMAP2_MCSPI_CHCONF_DPE0		(1 << 16)
#define OMAP2_MCSPI_CHCONF_DPE1		(1 << 17)
#define OMAP2_MCSPI_CHCONF_IS		(1 << 18)
#define OMAP2_MCSPI_CHCONF_TURBO	(1 << 19)
#define OMAP2_MCSPI_CHCONF_FORCE	(1 << 20)

#define OMAP2_MCSPI_CHSTAT_RXS		(1 << 0)
#define OMAP2_MCSPI_CHSTAT_TXS		(1 << 1)
#define OMAP2_MCSPI_CHSTAT_EOT		(1 << 2)

#define OMAP2_MCSPI_CHCTRL_EN		(1 << 0)


/* We have 2 DMA channels per CS, one for RX and one for TX */
struct omap2_mcspi_dma {
	int dma_tx_channel;
	int dma_rx_channel;

	int dma_tx_sync_dev;
	int dma_rx_sync_dev;

	struct completion dma_tx_completion;
	struct completion dma_rx_completion;
};

/* use PIO for small transfers, avoiding DMA setup/teardown overhead and
 * cache operations; better heuristics consider wordsize and bitrate.
 */
#define DMA_MIN_BYTES			8


struct omap2_mcspi {
	struct work_struct	work;
	/* lock protects queue and registers */
	spinlock_t		lock;
	struct list_head	msg_queue;
	struct spi_master	*master;
	struct clk		*ick;
	struct clk		*fck;
	/* Virtual base address of the controller */
	void __iomem		*base;
	/* SPI1 has 4 channels, while SPI2 has 2 */
	struct omap2_mcspi_dma	*dma_channels;
};

struct omap2_mcspi_cs {
	void __iomem		*base;
	int			word_len;
};

static struct workqueue_struct *omap2_mcspi_wq;

#define MOD_REG_BIT(val, mask, set) do { \
	if (set) \
		val |= mask; \
	else \
		val &= ~mask; \
} while (0)

static inline void mcspi_write_reg(struct spi_master *master,
		int idx, u32 val)
{
	struct omap2_mcspi *mcspi = spi_master_get_devdata(master);

	__raw_writel(val, mcspi->base + idx);
}

static inline u32 mcspi_read_reg(struct spi_master *master, int idx)
{
	struct omap2_mcspi *mcspi = spi_master_get_devdata(master);

	return __raw_readl(mcspi->base + idx);
}

static inline void mcspi_write_cs_reg(const struct spi_device *spi,
		int idx, u32 val)
{
	struct omap2_mcspi_cs	*cs = spi->controller_state;

	__raw_writel(val, cs->base +  idx);
}

static inline u32 mcspi_read_cs_reg(const struct spi_device *spi, int idx)
{
	struct omap2_mcspi_cs	*cs = spi->controller_state;

	return __raw_readl(cs->base + idx);
}

static void omap2_mcspi_set_dma_req(const struct spi_device *spi,
		int is_read, int enable)
{
	u32 l, rw;

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);

	if (is_read) /* 1 is read, 0 write */
		rw = OMAP2_MCSPI_CHCONF_DMAR;
	else
		rw = OMAP2_MCSPI_CHCONF_DMAW;

	MOD_REG_BIT(l, rw, enable);
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, l);
}

static void omap2_mcspi_set_enable(const struct spi_device *spi, int enable)
{
	u32 l;

	l = enable ? OMAP2_MCSPI_CHCTRL_EN : 0;
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCTRL0, l);
}

static void omap2_mcspi_force_cs(struct spi_device *spi, int cs_active)
{
	u32 l;

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);
	MOD_REG_BIT(l, OMAP2_MCSPI_CHCONF_FORCE, cs_active);
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, l);
}

static void omap2_mcspi_set_master_mode(struct spi_master *master)
{
	u32 l;

	/* setup when switching from (reset default) slave mode
	 * to single-channel master mode
	 */
	l = mcspi_read_reg(master, OMAP2_MCSPI_MODULCTRL);
	MOD_REG_BIT(l, OMAP2_MCSPI_MODULCTRL_STEST, 0);
	MOD_REG_BIT(l, OMAP2_MCSPI_MODULCTRL_MS, 0);
	MOD_REG_BIT(l, OMAP2_MCSPI_MODULCTRL_SINGLE, 1);
	mcspi_write_reg(master, OMAP2_MCSPI_MODULCTRL, l);
}

static unsigned
omap2_mcspi_txrx_dma(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_cs	*cs = spi->controller_state;
	struct omap2_mcspi_dma  *mcspi_dma;
	unsigned int		count, c;
	unsigned long		base, tx_reg, rx_reg;
	int			word_len, data_type, element_count;
	u8			* rx;
	const u8		* tx;

	mcspi = spi_master_get_devdata(spi->master);
	mcspi_dma = &mcspi->dma_channels[spi->chip_select];

	count = xfer->len;
	c = count;
	word_len = cs->word_len;

	base = (unsigned long) io_v2p(cs->base);
	tx_reg = base + OMAP2_MCSPI_TX0;
	rx_reg = base + OMAP2_MCSPI_RX0;
	rx = xfer->rx_buf;
	tx = xfer->tx_buf;

	if (word_len <= 8) {
		data_type = OMAP_DMA_DATA_TYPE_S8;
		element_count = count;
	} else if (word_len <= 16) {
		data_type = OMAP_DMA_DATA_TYPE_S16;
		element_count = count >> 1;
	} else /* word_len <= 32 */ {
		data_type = OMAP_DMA_DATA_TYPE_S32;
		element_count = count >> 2;
	}

	if (tx != NULL) {
		omap_set_dma_transfer_params(mcspi_dma->dma_tx_channel,
				data_type, element_count, 1,
				OMAP_DMA_SYNC_ELEMENT,
				mcspi_dma->dma_tx_sync_dev, 0);

		omap_set_dma_dest_params(mcspi_dma->dma_tx_channel, 0,
				OMAP_DMA_AMODE_CONSTANT,
				tx_reg, 0, 0);

		omap_set_dma_src_params(mcspi_dma->dma_tx_channel, 0,
				OMAP_DMA_AMODE_POST_INC,
				xfer->tx_dma, 0, 0);
	}

	if (rx != NULL) {
		omap_set_dma_transfer_params(mcspi_dma->dma_rx_channel,
				data_type, element_count, 1,
				OMAP_DMA_SYNC_ELEMENT,
				mcspi_dma->dma_rx_sync_dev, 1);

		omap_set_dma_src_params(mcspi_dma->dma_rx_channel, 0,
				OMAP_DMA_AMODE_CONSTANT,
				rx_reg, 0, 0);

		omap_set_dma_dest_params(mcspi_dma->dma_rx_channel, 0,
				OMAP_DMA_AMODE_POST_INC,
				xfer->rx_dma, 0, 0);
	}

	if (tx != NULL) {
		omap_start_dma(mcspi_dma->dma_tx_channel);
		omap2_mcspi_set_dma_req(spi, 0, 1);
	}

	if (rx != NULL) {
		omap_start_dma(mcspi_dma->dma_rx_channel);
		omap2_mcspi_set_dma_req(spi, 1, 1);
	}

	if (tx != NULL) {
		wait_for_completion(&mcspi_dma->dma_tx_completion);
		dma_unmap_single(NULL, xfer->tx_dma, count, DMA_TO_DEVICE);
	}

	if (rx != NULL) {
		wait_for_completion(&mcspi_dma->dma_rx_completion);
		dma_unmap_single(NULL, xfer->rx_dma, count, DMA_FROM_DEVICE);
	}
	return count;
}

static int mcspi_wait_for_reg_bit(void __iomem *reg, unsigned long bit)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(1000);
	while (!(__raw_readl(reg) & bit)) {
		if (time_after(jiffies, timeout))
			return -1;
		cpu_relax();
	}
	return 0;
}

static unsigned
omap2_mcspi_txrx_pio(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_cs	*cs = spi->controller_state;
	unsigned int		count, c;
	u32			l;
	void __iomem		*base = cs->base;
	void __iomem		*tx_reg;
	void __iomem		*rx_reg;
	void __iomem		*chstat_reg;
	int			word_len;

	mcspi = spi_master_get_devdata(spi->master);
	count = xfer->len;
	c = count;
	word_len = cs->word_len;

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);
	l &= ~OMAP2_MCSPI_CHCONF_TRM_MASK;

	/* We store the pre-calculated register addresses on stack to speed
	 * up the transfer loop. */
	tx_reg		= base + OMAP2_MCSPI_TX0;
	rx_reg		= base + OMAP2_MCSPI_RX0;
	chstat_reg	= base + OMAP2_MCSPI_CHSTAT0;

	if (word_len <= 8) {
		u8		*rx;
		const u8	*tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;

		do {
			c -= 1;
			if (tx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_TXS) < 0) {
					dev_err(&spi->dev, "TXS timed out\n");
					goto out;
				}
#ifdef VERBOSE
				dev_dbg(&spi->dev, "write-%d %02x\n",
						word_len, *tx);
#endif
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
					dev_err(&spi->dev, "RXS timed out\n");
					goto out;
				}
				/* prevent last RX_ONLY read from triggering
				 * more word i/o: switch to rx+tx
				 */
				if (c == 0 && tx == NULL)
					mcspi_write_cs_reg(spi,
							OMAP2_MCSPI_CHCONF0, l);
				*rx++ = __raw_readl(rx_reg);
#ifdef VERBOSE
				dev_dbg(&spi->dev, "read-%d %02x\n",
						word_len, *(rx - 1));
#endif
			}
		} while (c);
	} else if (word_len <= 16) {
		u16		*rx;
		const u16	*tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c -= 2;
			if (tx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_TXS) < 0) {
					dev_err(&spi->dev, "TXS timed out\n");
					goto out;
				}
#ifdef VERBOSE
				dev_dbg(&spi->dev, "write-%d %04x\n",
						word_len, *tx);
#endif
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
					dev_err(&spi->dev, "RXS timed out\n");
					goto out;
				}
				/* prevent last RX_ONLY read from triggering
				 * more word i/o: switch to rx+tx
				 */
				if (c == 0 && tx == NULL)
					mcspi_write_cs_reg(spi,
							OMAP2_MCSPI_CHCONF0, l);
				*rx++ = __raw_readl(rx_reg);
#ifdef VERBOSE
				dev_dbg(&spi->dev, "read-%d %04x\n",
						word_len, *(rx - 1));
#endif
			}
		} while (c);
	} else if (word_len <= 32) {
		u32		*rx;
		const u32	*tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		do {
			c -= 4;
			if (tx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_TXS) < 0) {
					dev_err(&spi->dev, "TXS timed out\n");
					goto out;
				}
#ifdef VERBOSE
				dev_dbg(&spi->dev, "write-%d %04x\n",
						word_len, *tx);
#endif
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
					dev_err(&spi->dev, "RXS timed out\n");
					goto out;
				}
				/* prevent last RX_ONLY read from triggering
				 * more word i/o: switch to rx+tx
				 */
				if (c == 0 && tx == NULL)
					mcspi_write_cs_reg(spi,
							OMAP2_MCSPI_CHCONF0, l);
				*rx++ = __raw_readl(rx_reg);
#ifdef VERBOSE
				dev_dbg(&spi->dev, "read-%d %04x\n",
						word_len, *(rx - 1));
#endif
			}
		} while (c);
	}

	/* for TX_ONLY mode, be sure all words have shifted out */
	if (xfer->rx_buf == NULL) {
		if (mcspi_wait_for_reg_bit(chstat_reg,
				OMAP2_MCSPI_CHSTAT_TXS) < 0) {
			dev_err(&spi->dev, "TXS timed out\n");
		} else if (mcspi_wait_for_reg_bit(chstat_reg,
				OMAP2_MCSPI_CHSTAT_EOT) < 0)
			dev_err(&spi->dev, "EOT timed out\n");
	}
out:
	return count - c;
}

/* called only when no transfer is active to this device */
static int omap2_mcspi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct omap2_mcspi_cs *cs = spi->controller_state;
	struct omap2_mcspi *mcspi;
	u32 l = 0, div = 0;
	u8 word_len = spi->bits_per_word;

	mcspi = spi_master_get_devdata(spi->master);

	if (t != NULL && t->bits_per_word)
		word_len = t->bits_per_word;

	cs->word_len = word_len;

	if (spi->max_speed_hz) {
		while (div <= 15 && (OMAP2_MCSPI_MAX_FREQ / (1 << div))
					> spi->max_speed_hz)
			div++;
	} else
		div = 15;

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);

	/* standard 4-wire master mode:  SCK, MOSI/out, MISO/in, nCS
	 * REVISIT: this controller could support SPI_3WIRE mode.
	 */
	l &= ~(OMAP2_MCSPI_CHCONF_IS|OMAP2_MCSPI_CHCONF_DPE1);
	l |= OMAP2_MCSPI_CHCONF_DPE0;

	/* wordlength */
	l &= ~OMAP2_MCSPI_CHCONF_WL_MASK;
	l |= (word_len - 1) << 7;

	/* set chipselect polarity; manage with FORCE */
	if (!(spi->mode & SPI_CS_HIGH))
		l |= OMAP2_MCSPI_CHCONF_EPOL;	/* active-low; normal */
	else
		l &= ~OMAP2_MCSPI_CHCONF_EPOL;

	/* set clock divisor */
	l &= ~OMAP2_MCSPI_CHCONF_CLKD_MASK;
	l |= div << 2;

	/* set SPI mode 0..3 */
	if (spi->mode & SPI_CPOL)
		l |= OMAP2_MCSPI_CHCONF_POL;
	else
		l &= ~OMAP2_MCSPI_CHCONF_POL;
	if (spi->mode & SPI_CPHA)
		l |= OMAP2_MCSPI_CHCONF_PHA;
	else
		l &= ~OMAP2_MCSPI_CHCONF_PHA;

	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, l);

	dev_dbg(&spi->dev, "setup: speed %d, sample %s edge, clk %s\n",
			OMAP2_MCSPI_MAX_FREQ / (1 << div),
			(spi->mode & SPI_CPHA) ? "trailing" : "leading",
			(spi->mode & SPI_CPOL) ? "inverted" : "normal");

	return 0;
}

static void omap2_mcspi_dma_rx_callback(int lch, u16 ch_status, void *data)
{
	struct spi_device	*spi = data;
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*mcspi_dma;

	mcspi = spi_master_get_devdata(spi->master);
	mcspi_dma = &(mcspi->dma_channels[spi->chip_select]);

	complete(&mcspi_dma->dma_rx_completion);

	/* We must disable the DMA RX request */
	omap2_mcspi_set_dma_req(spi, 1, 0);
}

static void omap2_mcspi_dma_tx_callback(int lch, u16 ch_status, void *data)
{
	struct spi_device	*spi = data;
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*mcspi_dma;

	mcspi = spi_master_get_devdata(spi->master);
	mcspi_dma = &(mcspi->dma_channels[spi->chip_select]);

	complete(&mcspi_dma->dma_tx_completion);

	/* We must disable the DMA TX request */
	omap2_mcspi_set_dma_req(spi, 0, 0);
}

static int omap2_mcspi_request_dma(struct spi_device *spi)
{
	struct spi_master	*master = spi->master;
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*mcspi_dma;

	mcspi = spi_master_get_devdata(master);
	mcspi_dma = mcspi->dma_channels + spi->chip_select;

	if (omap_request_dma(mcspi_dma->dma_rx_sync_dev, "McSPI RX",
			omap2_mcspi_dma_rx_callback, spi,
			&mcspi_dma->dma_rx_channel)) {
		dev_err(&spi->dev, "no RX DMA channel for McSPI\n");
		return -EAGAIN;
	}

	if (omap_request_dma(mcspi_dma->dma_tx_sync_dev, "McSPI TX",
			omap2_mcspi_dma_tx_callback, spi,
			&mcspi_dma->dma_tx_channel)) {
		omap_free_dma(mcspi_dma->dma_rx_channel);
		mcspi_dma->dma_rx_channel = -1;
		dev_err(&spi->dev, "no TX DMA channel for McSPI\n");
		return -EAGAIN;
	}

	init_completion(&mcspi_dma->dma_rx_completion);
	init_completion(&mcspi_dma->dma_tx_completion);

	return 0;
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH)

static int omap2_mcspi_setup(struct spi_device *spi)
{
	int			ret;
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*mcspi_dma;
	struct omap2_mcspi_cs	*cs = spi->controller_state;

	if (spi->mode & ~MODEBITS) {
		dev_dbg(&spi->dev, "setup: unsupported mode bits %x\n",
			spi->mode & ~MODEBITS);
		return -EINVAL;
	}

	if (spi->bits_per_word == 0)
		spi->bits_per_word = 8;
	else if (spi->bits_per_word < 4 || spi->bits_per_word > 32) {
		dev_dbg(&spi->dev, "setup: unsupported %d bit words\n",
			spi->bits_per_word);
		return -EINVAL;
	}

	mcspi = spi_master_get_devdata(spi->master);
	mcspi_dma = &mcspi->dma_channels[spi->chip_select];

	if (!cs) {
		cs = kzalloc(sizeof *cs, GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		cs->base = mcspi->base + spi->chip_select * 0x14;
		spi->controller_state = cs;
	}

	if (mcspi_dma->dma_rx_channel == -1
			|| mcspi_dma->dma_tx_channel == -1) {
		ret = omap2_mcspi_request_dma(spi);
		if (ret < 0)
			return ret;
	}

	clk_enable(mcspi->ick);
	clk_enable(mcspi->fck);
	ret = omap2_mcspi_setup_transfer(spi, NULL);
	clk_disable(mcspi->fck);
	clk_disable(mcspi->ick);

	return ret;
}

static void omap2_mcspi_cleanup(struct spi_device *spi)
{
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*mcspi_dma;

	mcspi = spi_master_get_devdata(spi->master);
	mcspi_dma = &mcspi->dma_channels[spi->chip_select];

	kfree(spi->controller_state);

	if (mcspi_dma->dma_rx_channel != -1) {
		omap_free_dma(mcspi_dma->dma_rx_channel);
		mcspi_dma->dma_rx_channel = -1;
	}
	if (mcspi_dma->dma_tx_channel != -1) {
		omap_free_dma(mcspi_dma->dma_tx_channel);
		mcspi_dma->dma_tx_channel = -1;
	}
}

static void omap2_mcspi_work(struct work_struct *work)
{
	struct omap2_mcspi	*mcspi;

	mcspi = container_of(work, struct omap2_mcspi, work);
	spin_lock_irq(&mcspi->lock);

	clk_enable(mcspi->ick);
	clk_enable(mcspi->fck);

	/* We only enable one channel at a time -- the one whose message is
	 * at the head of the queue -- although this controller would gladly
	 * arbitrate among multiple channels.  This corresponds to "single
	 * channel" master mode.  As a side effect, we need to manage the
	 * chipselect with the FORCE bit ... CS != channel enable.
	 */
	while (!list_empty(&mcspi->msg_queue)) {
		struct spi_message		*m;
		struct spi_device		*spi;
		struct spi_transfer		*t = NULL;
		int				cs_active = 0;
		struct omap2_mcspi_cs		*cs;
		int				par_override = 0;
		int				status = 0;
		u32				chconf;

		m = container_of(mcspi->msg_queue.next, struct spi_message,
				 queue);

		list_del_init(&m->queue);
		spin_unlock_irq(&mcspi->lock);

		spi = m->spi;
		cs = spi->controller_state;

		omap2_mcspi_set_enable(spi, 1);
		list_for_each_entry(t, &m->transfers, transfer_list) {
			if (t->tx_buf == NULL && t->rx_buf == NULL && t->len) {
				status = -EINVAL;
				break;
			}
			if (par_override || t->speed_hz || t->bits_per_word) {
				par_override = 1;
				status = omap2_mcspi_setup_transfer(spi, t);
				if (status < 0)
					break;
				if (!t->speed_hz && !t->bits_per_word)
					par_override = 0;
			}

			if (!cs_active) {
				omap2_mcspi_force_cs(spi, 1);
				cs_active = 1;
			}

			chconf = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);
			chconf &= ~OMAP2_MCSPI_CHCONF_TRM_MASK;
			if (t->tx_buf == NULL)
				chconf |= OMAP2_MCSPI_CHCONF_TRM_RX_ONLY;
			else if (t->rx_buf == NULL)
				chconf |= OMAP2_MCSPI_CHCONF_TRM_TX_ONLY;
			mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, chconf);

			if (t->len) {
				unsigned	count;

				/* RX_ONLY mode needs dummy data in TX reg */
				if (t->tx_buf == NULL)
					__raw_writel(0, cs->base
							+ OMAP2_MCSPI_TX0);

				if (m->is_dma_mapped || t->len >= DMA_MIN_BYTES)
					count = omap2_mcspi_txrx_dma(spi, t);
				else
					count = omap2_mcspi_txrx_pio(spi, t);
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
				omap2_mcspi_force_cs(spi, 0);
				cs_active = 0;
			}
		}

		/* Restore defaults if they were overriden */
		if (par_override) {
			par_override = 0;
			status = omap2_mcspi_setup_transfer(spi, NULL);
		}

		if (cs_active)
			omap2_mcspi_force_cs(spi, 0);

		omap2_mcspi_set_enable(spi, 0);

		m->status = status;
		m->complete(m->context);

		spin_lock_irq(&mcspi->lock);
	}

	clk_disable(mcspi->fck);
	clk_disable(mcspi->ick);

	spin_unlock_irq(&mcspi->lock);
}

static int omap2_mcspi_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct omap2_mcspi	*mcspi;
	unsigned long		flags;
	struct spi_transfer	*t;

	m->actual_length = 0;
	m->status = 0;

	/* reject invalid messages and transfers */
	if (list_empty(&m->transfers) || !m->complete)
		return -EINVAL;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		const void	*tx_buf = t->tx_buf;
		void		*rx_buf = t->rx_buf;
		unsigned	len = t->len;

		if (t->speed_hz > OMAP2_MCSPI_MAX_FREQ
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
		if (t->speed_hz && t->speed_hz < OMAP2_MCSPI_MAX_FREQ/(1<<16)) {
			dev_dbg(&spi->dev, "%d Hz max exceeds %d\n",
					t->speed_hz,
					OMAP2_MCSPI_MAX_FREQ/(1<<16));
			return -EINVAL;
		}

		if (m->is_dma_mapped || len < DMA_MIN_BYTES)
			continue;

		/* Do DMA mapping "early" for better error reporting and
		 * dcache use.  Note that if dma_unmap_single() ever starts
		 * to do real work on ARM, we'd need to clean up mappings
		 * for previous transfers on *ALL* exits of this loop...
		 */
		if (tx_buf != NULL) {
			t->tx_dma = dma_map_single(&spi->dev, (void *) tx_buf,
					len, DMA_TO_DEVICE);
			if (dma_mapping_error(t->tx_dma)) {
				dev_dbg(&spi->dev, "dma %cX %d bytes error\n",
						'T', len);
				return -EINVAL;
			}
		}
		if (rx_buf != NULL) {
			t->rx_dma = dma_map_single(&spi->dev, rx_buf, t->len,
					DMA_FROM_DEVICE);
			if (dma_mapping_error(t->rx_dma)) {
				dev_dbg(&spi->dev, "dma %cX %d bytes error\n",
						'R', len);
				if (tx_buf != NULL)
					dma_unmap_single(NULL, t->tx_dma,
							len, DMA_TO_DEVICE);
				return -EINVAL;
			}
		}
	}

	mcspi = spi_master_get_devdata(spi->master);

	spin_lock_irqsave(&mcspi->lock, flags);
	list_add_tail(&m->queue, &mcspi->msg_queue);
	queue_work(omap2_mcspi_wq, &mcspi->work);
	spin_unlock_irqrestore(&mcspi->lock, flags);

	return 0;
}

static int __init omap2_mcspi_reset(struct omap2_mcspi *mcspi)
{
	struct spi_master	*master = mcspi->master;
	u32			tmp;

	clk_enable(mcspi->ick);
	clk_enable(mcspi->fck);

	mcspi_write_reg(master, OMAP2_MCSPI_SYSCONFIG,
			OMAP2_MCSPI_SYSCONFIG_SOFTRESET);
	do {
		tmp = mcspi_read_reg(master, OMAP2_MCSPI_SYSSTATUS);
	} while (!(tmp & OMAP2_MCSPI_SYSSTATUS_RESETDONE));

	mcspi_write_reg(master, OMAP2_MCSPI_SYSCONFIG,
			/* (3 << 8) | (2 << 3) | */
			OMAP2_MCSPI_SYSCONFIG_AUTOIDLE);

	omap2_mcspi_set_master_mode(master);

	clk_disable(mcspi->fck);
	clk_disable(mcspi->ick);
	return 0;
}

static u8 __initdata spi1_rxdma_id [] = {
	OMAP24XX_DMA_SPI1_RX0,
	OMAP24XX_DMA_SPI1_RX1,
	OMAP24XX_DMA_SPI1_RX2,
	OMAP24XX_DMA_SPI1_RX3,
};

static u8 __initdata spi1_txdma_id [] = {
	OMAP24XX_DMA_SPI1_TX0,
	OMAP24XX_DMA_SPI1_TX1,
	OMAP24XX_DMA_SPI1_TX2,
	OMAP24XX_DMA_SPI1_TX3,
};

static u8 __initdata spi2_rxdma_id[] = {
	OMAP24XX_DMA_SPI2_RX0,
	OMAP24XX_DMA_SPI2_RX1,
};

static u8 __initdata spi2_txdma_id[] = {
	OMAP24XX_DMA_SPI2_TX0,
	OMAP24XX_DMA_SPI2_TX1,
};

#if defined(CONFIG_ARCH_OMAP2430) || defined(CONFIG_ARCH_OMAP34XX)
static u8 __initdata spi3_rxdma_id[] = {
	OMAP24XX_DMA_SPI3_RX0,
	OMAP24XX_DMA_SPI3_RX1,
};

static u8 __initdata spi3_txdma_id[] = {
	OMAP24XX_DMA_SPI3_TX0,
	OMAP24XX_DMA_SPI3_TX1,
};
#endif

#ifdef CONFIG_ARCH_OMAP3
static u8 __initdata spi4_rxdma_id[] = {
	OMAP34XX_DMA_SPI4_RX0,
};

static u8 __initdata spi4_txdma_id[] = {
	OMAP34XX_DMA_SPI4_TX0,
};
#endif

static int __init omap2_mcspi_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct omap2_mcspi	*mcspi;
	struct resource		*r;
	int			status = 0, i;
	const u8		*rxdma_id, *txdma_id;
	unsigned		num_chipselect;

	switch (pdev->id) {
	case 1:
		rxdma_id = spi1_rxdma_id;
		txdma_id = spi1_txdma_id;
		num_chipselect = 4;
		break;
	case 2:
		rxdma_id = spi2_rxdma_id;
		txdma_id = spi2_txdma_id;
		num_chipselect = 2;
		break;
#if defined(CONFIG_ARCH_OMAP2430) || defined(CONFIG_ARCH_OMAP3)
	case 3:
		rxdma_id = spi3_rxdma_id;
		txdma_id = spi3_txdma_id;
		num_chipselect = 2;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP3
	case 4:
		rxdma_id = spi4_rxdma_id;
		txdma_id = spi4_txdma_id;
		num_chipselect = 1;
		break;
#endif
	default:
		return -EINVAL;
	}

	master = spi_alloc_master(&pdev->dev, sizeof *mcspi);
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num = pdev->id;

	master->setup = omap2_mcspi_setup;
	master->transfer = omap2_mcspi_transfer;
	master->cleanup = omap2_mcspi_cleanup;
	master->num_chipselect = num_chipselect;

	dev_set_drvdata(&pdev->dev, master);

	mcspi = spi_master_get_devdata(master);
	mcspi->master = master;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		status = -ENODEV;
		goto err1;
	}
	if (!request_mem_region(r->start, (r->end - r->start) + 1,
			pdev->dev.bus_id)) {
		status = -EBUSY;
		goto err1;
	}

	mcspi->base = (void __iomem *) io_p2v(r->start);

	INIT_WORK(&mcspi->work, omap2_mcspi_work);

	spin_lock_init(&mcspi->lock);
	INIT_LIST_HEAD(&mcspi->msg_queue);

	mcspi->ick = clk_get(&pdev->dev, "mcspi_ick");
	if (IS_ERR(mcspi->ick)) {
		dev_dbg(&pdev->dev, "can't get mcspi_ick\n");
		status = PTR_ERR(mcspi->ick);
		goto err1a;
	}
	mcspi->fck = clk_get(&pdev->dev, "mcspi_fck");
	if (IS_ERR(mcspi->fck)) {
		dev_dbg(&pdev->dev, "can't get mcspi_fck\n");
		status = PTR_ERR(mcspi->fck);
		goto err2;
	}

	mcspi->dma_channels = kcalloc(master->num_chipselect,
			sizeof(struct omap2_mcspi_dma),
			GFP_KERNEL);

	if (mcspi->dma_channels == NULL)
		goto err3;

	for (i = 0; i < num_chipselect; i++) {
		mcspi->dma_channels[i].dma_rx_channel = -1;
		mcspi->dma_channels[i].dma_rx_sync_dev = rxdma_id[i];
		mcspi->dma_channels[i].dma_tx_channel = -1;
		mcspi->dma_channels[i].dma_tx_sync_dev = txdma_id[i];
	}

	if (omap2_mcspi_reset(mcspi) < 0)
		goto err4;

	status = spi_register_master(master);
	if (status < 0)
		goto err4;

	return status;

err4:
	kfree(mcspi->dma_channels);
err3:
	clk_put(mcspi->fck);
err2:
	clk_put(mcspi->ick);
err1a:
	release_mem_region(r->start, (r->end - r->start) + 1);
err1:
	spi_master_put(master);
	return status;
}

static int __exit omap2_mcspi_remove(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*dma_channels;
	struct resource		*r;

	master = dev_get_drvdata(&pdev->dev);
	mcspi = spi_master_get_devdata(master);
	dma_channels = mcspi->dma_channels;

	clk_put(mcspi->fck);
	clk_put(mcspi->ick);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(r->start, (r->end - r->start) + 1);

	spi_unregister_master(master);
	kfree(dma_channels);

	return 0;
}

static struct platform_driver omap2_mcspi_driver = {
	.driver = {
		.name =		"omap2_mcspi",
		.owner =	THIS_MODULE,
	},
	.remove =	__exit_p(omap2_mcspi_remove),
};


static int __init omap2_mcspi_init(void)
{
	omap2_mcspi_wq = create_singlethread_workqueue(
				omap2_mcspi_driver.driver.name);
	if (omap2_mcspi_wq == NULL)
		return -1;
	return platform_driver_probe(&omap2_mcspi_driver, omap2_mcspi_probe);
}
subsys_initcall(omap2_mcspi_init);

static void __exit omap2_mcspi_exit(void)
{
	platform_driver_unregister(&omap2_mcspi_driver);

	destroy_workqueue(omap2_mcspi_wq);
}
module_exit(omap2_mcspi_exit);

MODULE_LICENSE("GPL");
