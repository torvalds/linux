/*
 * OMAP2 McSPI controller driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation
 * Author:	Samuel Ortiz <samuel.ortiz@nokia.com> and
 *		Juha Yrj�l� <juha.yrjola@nokia.com>
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
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <linux/spi/spi.h>

#include <plat/dma.h>
#include <plat/clock.h>
#include <plat/mcspi.h>

#define OMAP2_MCSPI_MAX_FREQ		48000000

/* OMAP2 has 3 SPI controllers, while OMAP3 has 4 */
#define OMAP2_MCSPI_MAX_CTRL 		4

#define OMAP2_MCSPI_REVISION		0x00
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

#define OMAP2_MCSPI_MODULCTRL_SINGLE	BIT(0)
#define OMAP2_MCSPI_MODULCTRL_MS	BIT(2)
#define OMAP2_MCSPI_MODULCTRL_STEST	BIT(3)

#define OMAP2_MCSPI_CHCONF_PHA		BIT(0)
#define OMAP2_MCSPI_CHCONF_POL		BIT(1)
#define OMAP2_MCSPI_CHCONF_CLKD_MASK	(0x0f << 2)
#define OMAP2_MCSPI_CHCONF_EPOL		BIT(6)
#define OMAP2_MCSPI_CHCONF_WL_MASK	(0x1f << 7)
#define OMAP2_MCSPI_CHCONF_TRM_RX_ONLY	BIT(12)
#define OMAP2_MCSPI_CHCONF_TRM_TX_ONLY	BIT(13)
#define OMAP2_MCSPI_CHCONF_TRM_MASK	(0x03 << 12)
#define OMAP2_MCSPI_CHCONF_DMAW		BIT(14)
#define OMAP2_MCSPI_CHCONF_DMAR		BIT(15)
#define OMAP2_MCSPI_CHCONF_DPE0		BIT(16)
#define OMAP2_MCSPI_CHCONF_DPE1		BIT(17)
#define OMAP2_MCSPI_CHCONF_IS		BIT(18)
#define OMAP2_MCSPI_CHCONF_TURBO	BIT(19)
#define OMAP2_MCSPI_CHCONF_FORCE	BIT(20)

#define OMAP2_MCSPI_CHSTAT_RXS		BIT(0)
#define OMAP2_MCSPI_CHSTAT_TXS		BIT(1)
#define OMAP2_MCSPI_CHSTAT_EOT		BIT(2)

#define OMAP2_MCSPI_CHCTRL_EN		BIT(0)

#define OMAP2_MCSPI_WAKEUPENABLE_WKEN	BIT(0)

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
#define DMA_MIN_BYTES			160


struct omap2_mcspi {
	struct work_struct	work;
	/* lock protects queue and registers */
	spinlock_t		lock;
	struct list_head	msg_queue;
	struct spi_master	*master;
	/* Virtual base address of the controller */
	void __iomem		*base;
	unsigned long		phys;
	/* SPI1 has 4 channels, while SPI2 has 2 */
	struct omap2_mcspi_dma	*dma_channels;
	struct  device		*dev;
};

struct omap2_mcspi_cs {
	void __iomem		*base;
	unsigned long		phys;
	int			word_len;
	struct list_head	node;
	/* Context save and restore shadow register */
	u32			chconf0;
};

/* used for context save and restore, structure members to be updated whenever
 * corresponding registers are modified.
 */
struct omap2_mcspi_regs {
	u32 modulctrl;
	u32 wakeupenable;
	struct list_head cs;
};

static struct omap2_mcspi_regs omap2_mcspi_ctx[OMAP2_MCSPI_MAX_CTRL];

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

static inline u32 mcspi_cached_chconf0(const struct spi_device *spi)
{
	struct omap2_mcspi_cs *cs = spi->controller_state;

	return cs->chconf0;
}

static inline void mcspi_write_chconf0(const struct spi_device *spi, u32 val)
{
	struct omap2_mcspi_cs *cs = spi->controller_state;

	cs->chconf0 = val;
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, val);
	mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);
}

static void omap2_mcspi_set_dma_req(const struct spi_device *spi,
		int is_read, int enable)
{
	u32 l, rw;

	l = mcspi_cached_chconf0(spi);

	if (is_read) /* 1 is read, 0 write */
		rw = OMAP2_MCSPI_CHCONF_DMAR;
	else
		rw = OMAP2_MCSPI_CHCONF_DMAW;

	MOD_REG_BIT(l, rw, enable);
	mcspi_write_chconf0(spi, l);
}

static void omap2_mcspi_set_enable(const struct spi_device *spi, int enable)
{
	u32 l;

	l = enable ? OMAP2_MCSPI_CHCTRL_EN : 0;
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCTRL0, l);
	/* Flash post-writes */
	mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCTRL0);
}

static void omap2_mcspi_force_cs(struct spi_device *spi, int cs_active)
{
	u32 l;

	l = mcspi_cached_chconf0(spi);
	MOD_REG_BIT(l, OMAP2_MCSPI_CHCONF_FORCE, cs_active);
	mcspi_write_chconf0(spi, l);
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

	omap2_mcspi_ctx[master->bus_num - 1].modulctrl = l;
}

static void omap2_mcspi_restore_ctx(struct omap2_mcspi *mcspi)
{
	struct spi_master *spi_cntrl;
	struct omap2_mcspi_cs *cs;
	spi_cntrl = mcspi->master;

	/* McSPI: context restore */
	mcspi_write_reg(spi_cntrl, OMAP2_MCSPI_MODULCTRL,
			omap2_mcspi_ctx[spi_cntrl->bus_num - 1].modulctrl);

	mcspi_write_reg(spi_cntrl, OMAP2_MCSPI_WAKEUPENABLE,
			omap2_mcspi_ctx[spi_cntrl->bus_num - 1].wakeupenable);

	list_for_each_entry(cs, &omap2_mcspi_ctx[spi_cntrl->bus_num - 1].cs,
			node)
		__raw_writel(cs->chconf0, cs->base + OMAP2_MCSPI_CHCONF0);
}
static void omap2_mcspi_disable_clocks(struct omap2_mcspi *mcspi)
{
	pm_runtime_put_sync(mcspi->dev);
}

static int omap2_mcspi_enable_clocks(struct omap2_mcspi *mcspi)
{
	return pm_runtime_get_sync(mcspi->dev);
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
omap2_mcspi_txrx_dma(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_cs	*cs = spi->controller_state;
	struct omap2_mcspi_dma  *mcspi_dma;
	unsigned int		count, c;
	unsigned long		base, tx_reg, rx_reg;
	int			word_len, data_type, element_count;
	int			elements = 0;
	u32			l;
	u8			* rx;
	const u8		* tx;
	void __iomem		*chstat_reg;

	mcspi = spi_master_get_devdata(spi->master);
	mcspi_dma = &mcspi->dma_channels[spi->chip_select];
	l = mcspi_cached_chconf0(spi);

	chstat_reg = cs->base + OMAP2_MCSPI_CHSTAT0;

	count = xfer->len;
	c = count;
	word_len = cs->word_len;

	base = cs->phys;
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
		elements = element_count - 1;
		if (l & OMAP2_MCSPI_CHCONF_TURBO)
			elements--;

		omap_set_dma_transfer_params(mcspi_dma->dma_rx_channel,
				data_type, elements, 1,
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
		dma_unmap_single(&spi->dev, xfer->tx_dma, count, DMA_TO_DEVICE);

		/* for TX_ONLY mode, be sure all words have shifted out */
		if (rx == NULL) {
			if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_TXS) < 0)
				dev_err(&spi->dev, "TXS timed out\n");
			else if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_EOT) < 0)
				dev_err(&spi->dev, "EOT timed out\n");
		}
	}

	if (rx != NULL) {
		wait_for_completion(&mcspi_dma->dma_rx_completion);
		dma_unmap_single(&spi->dev, xfer->rx_dma, count, DMA_FROM_DEVICE);
		omap2_mcspi_set_enable(spi, 0);

		if (l & OMAP2_MCSPI_CHCONF_TURBO) {

			if (likely(mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHSTAT0)
				   & OMAP2_MCSPI_CHSTAT_RXS)) {
				u32 w;

				w = mcspi_read_cs_reg(spi, OMAP2_MCSPI_RX0);
				if (word_len <= 8)
					((u8 *)xfer->rx_buf)[elements++] = w;
				else if (word_len <= 16)
					((u16 *)xfer->rx_buf)[elements++] = w;
				else /* word_len <= 32 */
					((u32 *)xfer->rx_buf)[elements++] = w;
			} else {
				dev_err(&spi->dev,
					"DMA RX penultimate word empty");
				count -= (word_len <= 8)  ? 2 :
					(word_len <= 16) ? 4 :
					/* word_len <= 32 */ 8;
				omap2_mcspi_set_enable(spi, 1);
				return count;
			}
		}

		if (likely(mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHSTAT0)
				& OMAP2_MCSPI_CHSTAT_RXS)) {
			u32 w;

			w = mcspi_read_cs_reg(spi, OMAP2_MCSPI_RX0);
			if (word_len <= 8)
				((u8 *)xfer->rx_buf)[elements] = w;
			else if (word_len <= 16)
				((u16 *)xfer->rx_buf)[elements] = w;
			else /* word_len <= 32 */
				((u32 *)xfer->rx_buf)[elements] = w;
		} else {
			dev_err(&spi->dev, "DMA RX last word empty");
			count -= (word_len <= 8)  ? 1 :
				 (word_len <= 16) ? 2 :
			       /* word_len <= 32 */ 4;
		}
		omap2_mcspi_set_enable(spi, 1);
	}
	return count;
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

	l = mcspi_cached_chconf0(spi);

	/* We store the pre-calculated register addresses on stack to speed
	 * up the transfer loop. */
	tx_reg		= base + OMAP2_MCSPI_TX0;
	rx_reg		= base + OMAP2_MCSPI_RX0;
	chstat_reg	= base + OMAP2_MCSPI_CHSTAT0;

	if (c < (word_len>>3))
		return 0;

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
				dev_vdbg(&spi->dev, "write-%d %02x\n",
						word_len, *tx);
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
					dev_err(&spi->dev, "RXS timed out\n");
					goto out;
				}

				if (c == 1 && tx == NULL &&
				    (l & OMAP2_MCSPI_CHCONF_TURBO)) {
					omap2_mcspi_set_enable(spi, 0);
					*rx++ = __raw_readl(rx_reg);
					dev_vdbg(&spi->dev, "read-%d %02x\n",
						    word_len, *(rx - 1));
					if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
						dev_err(&spi->dev,
							"RXS timed out\n");
						goto out;
					}
					c = 0;
				} else if (c == 0 && tx == NULL) {
					omap2_mcspi_set_enable(spi, 0);
				}

				*rx++ = __raw_readl(rx_reg);
				dev_vdbg(&spi->dev, "read-%d %02x\n",
						word_len, *(rx - 1));
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
				dev_vdbg(&spi->dev, "write-%d %04x\n",
						word_len, *tx);
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
					dev_err(&spi->dev, "RXS timed out\n");
					goto out;
				}

				if (c == 2 && tx == NULL &&
				    (l & OMAP2_MCSPI_CHCONF_TURBO)) {
					omap2_mcspi_set_enable(spi, 0);
					*rx++ = __raw_readl(rx_reg);
					dev_vdbg(&spi->dev, "read-%d %04x\n",
						    word_len, *(rx - 1));
					if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
						dev_err(&spi->dev,
							"RXS timed out\n");
						goto out;
					}
					c = 0;
				} else if (c == 0 && tx == NULL) {
					omap2_mcspi_set_enable(spi, 0);
				}

				*rx++ = __raw_readl(rx_reg);
				dev_vdbg(&spi->dev, "read-%d %04x\n",
						word_len, *(rx - 1));
			}
		} while (c >= 2);
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
				dev_vdbg(&spi->dev, "write-%d %08x\n",
						word_len, *tx);
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
					dev_err(&spi->dev, "RXS timed out\n");
					goto out;
				}

				if (c == 4 && tx == NULL &&
				    (l & OMAP2_MCSPI_CHCONF_TURBO)) {
					omap2_mcspi_set_enable(spi, 0);
					*rx++ = __raw_readl(rx_reg);
					dev_vdbg(&spi->dev, "read-%d %08x\n",
						    word_len, *(rx - 1));
					if (mcspi_wait_for_reg_bit(chstat_reg,
						OMAP2_MCSPI_CHSTAT_RXS) < 0) {
						dev_err(&spi->dev,
							"RXS timed out\n");
						goto out;
					}
					c = 0;
				} else if (c == 0 && tx == NULL) {
					omap2_mcspi_set_enable(spi, 0);
				}

				*rx++ = __raw_readl(rx_reg);
				dev_vdbg(&spi->dev, "read-%d %08x\n",
						word_len, *(rx - 1));
			}
		} while (c >= 4);
	}

	/* for TX_ONLY mode, be sure all words have shifted out */
	if (xfer->rx_buf == NULL) {
		if (mcspi_wait_for_reg_bit(chstat_reg,
				OMAP2_MCSPI_CHSTAT_TXS) < 0) {
			dev_err(&spi->dev, "TXS timed out\n");
		} else if (mcspi_wait_for_reg_bit(chstat_reg,
				OMAP2_MCSPI_CHSTAT_EOT) < 0)
			dev_err(&spi->dev, "EOT timed out\n");

		/* disable chan to purge rx datas received in TX_ONLY transfer,
		 * otherwise these rx datas will affect the direct following
		 * RX_ONLY transfer.
		 */
		omap2_mcspi_set_enable(spi, 0);
	}
out:
	omap2_mcspi_set_enable(spi, 1);
	return count - c;
}

static u32 omap2_mcspi_calc_divisor(u32 speed_hz)
{
	u32 div;

	for (div = 0; div < 15; div++)
		if (speed_hz >= (OMAP2_MCSPI_MAX_FREQ >> div))
			return div;

	return 15;
}

/* called only when no transfer is active to this device */
static int omap2_mcspi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct omap2_mcspi_cs *cs = spi->controller_state;
	struct omap2_mcspi *mcspi;
	struct spi_master *spi_cntrl;
	u32 l = 0, div = 0;
	u8 word_len = spi->bits_per_word;
	u32 speed_hz = spi->max_speed_hz;

	mcspi = spi_master_get_devdata(spi->master);
	spi_cntrl = mcspi->master;

	if (t != NULL && t->bits_per_word)
		word_len = t->bits_per_word;

	cs->word_len = word_len;

	if (t && t->speed_hz)
		speed_hz = t->speed_hz;

	speed_hz = min_t(u32, speed_hz, OMAP2_MCSPI_MAX_FREQ);
	div = omap2_mcspi_calc_divisor(speed_hz);

	l = mcspi_cached_chconf0(spi);

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

	mcspi_write_chconf0(spi, l);

	dev_dbg(&spi->dev, "setup: speed %d, sample %s edge, clk %s\n",
			OMAP2_MCSPI_MAX_FREQ >> div,
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

static int omap2_mcspi_setup(struct spi_device *spi)
{
	int			ret;
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*mcspi_dma;
	struct omap2_mcspi_cs	*cs = spi->controller_state;

	if (spi->bits_per_word < 4 || spi->bits_per_word > 32) {
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
		cs->phys = mcspi->phys + spi->chip_select * 0x14;
		cs->chconf0 = 0;
		spi->controller_state = cs;
		/* Link this to context save list */
		list_add_tail(&cs->node,
			&omap2_mcspi_ctx[mcspi->master->bus_num - 1].cs);
	}

	if (mcspi_dma->dma_rx_channel == -1
			|| mcspi_dma->dma_tx_channel == -1) {
		ret = omap2_mcspi_request_dma(spi);
		if (ret < 0)
			return ret;
	}

	ret = omap2_mcspi_enable_clocks(mcspi);
	if (ret < 0)
		return ret;

	ret = omap2_mcspi_setup_transfer(spi, NULL);
	omap2_mcspi_disable_clocks(mcspi);

	return ret;
}

static void omap2_mcspi_cleanup(struct spi_device *spi)
{
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*mcspi_dma;
	struct omap2_mcspi_cs	*cs;

	mcspi = spi_master_get_devdata(spi->master);

	if (spi->controller_state) {
		/* Unlink controller state from context save list */
		cs = spi->controller_state;
		list_del(&cs->node);

		kfree(spi->controller_state);
	}

	if (spi->chip_select < spi->master->num_chipselect) {
		mcspi_dma = &mcspi->dma_channels[spi->chip_select];

		if (mcspi_dma->dma_rx_channel != -1) {
			omap_free_dma(mcspi_dma->dma_rx_channel);
			mcspi_dma->dma_rx_channel = -1;
		}
		if (mcspi_dma->dma_tx_channel != -1) {
			omap_free_dma(mcspi_dma->dma_tx_channel);
			mcspi_dma->dma_tx_channel = -1;
		}
	}
}

static void omap2_mcspi_work(struct work_struct *work)
{
	struct omap2_mcspi	*mcspi;

	mcspi = container_of(work, struct omap2_mcspi, work);

	if (omap2_mcspi_enable_clocks(mcspi) < 0)
		return;

	spin_lock_irq(&mcspi->lock);

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
		struct omap2_mcspi_device_config *cd;
		int				par_override = 0;
		int				status = 0;
		u32				chconf;

		m = container_of(mcspi->msg_queue.next, struct spi_message,
				 queue);

		list_del_init(&m->queue);
		spin_unlock_irq(&mcspi->lock);

		spi = m->spi;
		cs = spi->controller_state;
		cd = spi->controller_data;

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

			chconf = mcspi_cached_chconf0(spi);
			chconf &= ~OMAP2_MCSPI_CHCONF_TRM_MASK;
			chconf &= ~OMAP2_MCSPI_CHCONF_TURBO;

			if (t->tx_buf == NULL)
				chconf |= OMAP2_MCSPI_CHCONF_TRM_RX_ONLY;
			else if (t->rx_buf == NULL)
				chconf |= OMAP2_MCSPI_CHCONF_TRM_TX_ONLY;

			if (cd && cd->turbo_mode && t->tx_buf == NULL) {
				/* Turbo mode is for more than one word */
				if (t->len > ((cs->word_len + 7) >> 3))
					chconf |= OMAP2_MCSPI_CHCONF_TURBO;
			}

			mcspi_write_chconf0(spi, chconf);

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

	spin_unlock_irq(&mcspi->lock);

	omap2_mcspi_disable_clocks(mcspi);
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
		if (t->speed_hz && t->speed_hz < (OMAP2_MCSPI_MAX_FREQ >> 15)) {
			dev_dbg(&spi->dev, "speed_hz %d below minimum %d Hz\n",
				t->speed_hz,
				OMAP2_MCSPI_MAX_FREQ >> 15);
			return -EINVAL;
		}

		if (m->is_dma_mapped || len < DMA_MIN_BYTES)
			continue;

		if (tx_buf != NULL) {
			t->tx_dma = dma_map_single(&spi->dev, (void *) tx_buf,
					len, DMA_TO_DEVICE);
			if (dma_mapping_error(&spi->dev, t->tx_dma)) {
				dev_dbg(&spi->dev, "dma %cX %d bytes error\n",
						'T', len);
				return -EINVAL;
			}
		}
		if (rx_buf != NULL) {
			t->rx_dma = dma_map_single(&spi->dev, rx_buf, t->len,
					DMA_FROM_DEVICE);
			if (dma_mapping_error(&spi->dev, t->rx_dma)) {
				dev_dbg(&spi->dev, "dma %cX %d bytes error\n",
						'R', len);
				if (tx_buf != NULL)
					dma_unmap_single(&spi->dev, t->tx_dma,
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

static int __init omap2_mcspi_master_setup(struct omap2_mcspi *mcspi)
{
	struct spi_master	*master = mcspi->master;
	u32			tmp;
	int ret = 0;

	ret = omap2_mcspi_enable_clocks(mcspi);
	if (ret < 0)
		return ret;

	tmp = OMAP2_MCSPI_WAKEUPENABLE_WKEN;
	mcspi_write_reg(master, OMAP2_MCSPI_WAKEUPENABLE, tmp);
	omap2_mcspi_ctx[master->bus_num - 1].wakeupenable = tmp;

	omap2_mcspi_set_master_mode(master);
	omap2_mcspi_disable_clocks(mcspi);
	return 0;
}

static int omap_mcspi_runtime_resume(struct device *dev)
{
	struct omap2_mcspi	*mcspi;
	struct spi_master	*master;

	master = dev_get_drvdata(dev);
	mcspi = spi_master_get_devdata(master);
	omap2_mcspi_restore_ctx(mcspi);

	return 0;
}


static int __init omap2_mcspi_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct omap2_mcspi_platform_config *pdata = pdev->dev.platform_data;
	struct omap2_mcspi	*mcspi;
	struct resource		*r;
	int			status = 0, i;

	master = spi_alloc_master(&pdev->dev, sizeof *mcspi);
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	if (pdev->id != -1)
		master->bus_num = pdev->id;

	master->setup = omap2_mcspi_setup;
	master->transfer = omap2_mcspi_transfer;
	master->cleanup = omap2_mcspi_cleanup;
	master->num_chipselect = pdata->num_cs;

	dev_set_drvdata(&pdev->dev, master);

	mcspi = spi_master_get_devdata(master);
	mcspi->master = master;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		status = -ENODEV;
		goto err1;
	}

	r->start += pdata->regs_offset;
	r->end += pdata->regs_offset;
	mcspi->phys = r->start;
	if (!request_mem_region(r->start, resource_size(r),
				dev_name(&pdev->dev))) {
		status = -EBUSY;
		goto err1;
	}

	mcspi->base = ioremap(r->start, resource_size(r));
	if (!mcspi->base) {
		dev_dbg(&pdev->dev, "can't ioremap MCSPI\n");
		status = -ENOMEM;
		goto err2;
	}

	mcspi->dev = &pdev->dev;
	INIT_WORK(&mcspi->work, omap2_mcspi_work);

	spin_lock_init(&mcspi->lock);
	INIT_LIST_HEAD(&mcspi->msg_queue);
	INIT_LIST_HEAD(&omap2_mcspi_ctx[master->bus_num - 1].cs);

	mcspi->dma_channels = kcalloc(master->num_chipselect,
			sizeof(struct omap2_mcspi_dma),
			GFP_KERNEL);

	if (mcspi->dma_channels == NULL)
		goto err2;

	for (i = 0; i < master->num_chipselect; i++) {
		char dma_ch_name[14];
		struct resource *dma_res;

		sprintf(dma_ch_name, "rx%d", i);
		dma_res = platform_get_resource_byname(pdev, IORESOURCE_DMA,
							dma_ch_name);
		if (!dma_res) {
			dev_dbg(&pdev->dev, "cannot get DMA RX channel\n");
			status = -ENODEV;
			break;
		}

		mcspi->dma_channels[i].dma_rx_channel = -1;
		mcspi->dma_channels[i].dma_rx_sync_dev = dma_res->start;
		sprintf(dma_ch_name, "tx%d", i);
		dma_res = platform_get_resource_byname(pdev, IORESOURCE_DMA,
							dma_ch_name);
		if (!dma_res) {
			dev_dbg(&pdev->dev, "cannot get DMA TX channel\n");
			status = -ENODEV;
			break;
		}

		mcspi->dma_channels[i].dma_tx_channel = -1;
		mcspi->dma_channels[i].dma_tx_sync_dev = dma_res->start;
	}

	pm_runtime_enable(&pdev->dev);

	if (status || omap2_mcspi_master_setup(mcspi) < 0)
		goto err3;

	status = spi_register_master(master);
	if (status < 0)
		goto err4;

	return status;

err4:
	spi_master_put(master);
err3:
	kfree(mcspi->dma_channels);
err2:
	release_mem_region(r->start, resource_size(r));
	iounmap(mcspi->base);
err1:
	return status;
}

static int __exit omap2_mcspi_remove(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct omap2_mcspi	*mcspi;
	struct omap2_mcspi_dma	*dma_channels;
	struct resource		*r;
	void __iomem *base;

	master = dev_get_drvdata(&pdev->dev);
	mcspi = spi_master_get_devdata(master);
	dma_channels = mcspi->dma_channels;

	omap2_mcspi_disable_clocks(mcspi);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(r->start, resource_size(r));

	base = mcspi->base;
	spi_unregister_master(master);
	iounmap(base);
	kfree(dma_channels);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:omap2_mcspi");

#ifdef	CONFIG_SUSPEND
/*
 * When SPI wake up from off-mode, CS is in activate state. If it was in
 * unactive state when driver was suspend, then force it to unactive state at
 * wake up.
 */
static int omap2_mcspi_resume(struct device *dev)
{
	struct spi_master	*master = dev_get_drvdata(dev);
	struct omap2_mcspi	*mcspi = spi_master_get_devdata(master);
	struct omap2_mcspi_cs *cs;

	omap2_mcspi_enable_clocks(mcspi);
	list_for_each_entry(cs, &omap2_mcspi_ctx[master->bus_num - 1].cs,
			    node) {
		if ((cs->chconf0 & OMAP2_MCSPI_CHCONF_FORCE) == 0) {

			/*
			 * We need to toggle CS state for OMAP take this
			 * change in account.
			 */
			MOD_REG_BIT(cs->chconf0, OMAP2_MCSPI_CHCONF_FORCE, 1);
			__raw_writel(cs->chconf0, cs->base + OMAP2_MCSPI_CHCONF0);
			MOD_REG_BIT(cs->chconf0, OMAP2_MCSPI_CHCONF_FORCE, 0);
			__raw_writel(cs->chconf0, cs->base + OMAP2_MCSPI_CHCONF0);
		}
	}
	omap2_mcspi_disable_clocks(mcspi);
	return 0;
}
#else
#define	omap2_mcspi_resume	NULL
#endif

static const struct dev_pm_ops omap2_mcspi_pm_ops = {
	.resume = omap2_mcspi_resume,
	.runtime_resume	= omap_mcspi_runtime_resume,
};

static struct platform_driver omap2_mcspi_driver = {
	.driver = {
		.name =		"omap2_mcspi",
		.owner =	THIS_MODULE,
		.pm =		&omap2_mcspi_pm_ops
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
