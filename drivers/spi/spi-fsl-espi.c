// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale eSPI controller driver.
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fsl_devices.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/pm_runtime.h>
#include <sysdev/fsl_soc.h>

/* eSPI Controller registers */
#define ESPI_SPMODE	0x00	/* eSPI mode register */
#define ESPI_SPIE	0x04	/* eSPI event register */
#define ESPI_SPIM	0x08	/* eSPI mask register */
#define ESPI_SPCOM	0x0c	/* eSPI command register */
#define ESPI_SPITF	0x10	/* eSPI transmit FIFO access register*/
#define ESPI_SPIRF	0x14	/* eSPI receive FIFO access register*/
#define ESPI_SPMODE0	0x20	/* eSPI cs0 mode register */

#define ESPI_SPMODEx(x)	(ESPI_SPMODE0 + (x) * 4)

/* eSPI Controller mode register definitions */
#define SPMODE_ENABLE		BIT(31)
#define SPMODE_LOOP		BIT(30)
#define SPMODE_TXTHR(x)		((x) << 8)
#define SPMODE_RXTHR(x)		((x) << 0)

/* eSPI Controller CS mode register definitions */
#define CSMODE_CI_INACTIVEHIGH	BIT(31)
#define CSMODE_CP_BEGIN_EDGECLK	BIT(30)
#define CSMODE_REV		BIT(29)
#define CSMODE_DIV16		BIT(28)
#define CSMODE_PM(x)		((x) << 24)
#define CSMODE_POL_1		BIT(20)
#define CSMODE_LEN(x)		((x) << 16)
#define CSMODE_BEF(x)		((x) << 12)
#define CSMODE_AFT(x)		((x) << 8)
#define CSMODE_CG(x)		((x) << 3)

#define FSL_ESPI_FIFO_SIZE	32
#define FSL_ESPI_RXTHR		15

/* Default mode/csmode for eSPI controller */
#define SPMODE_INIT_VAL (SPMODE_TXTHR(4) | SPMODE_RXTHR(FSL_ESPI_RXTHR))
#define CSMODE_INIT_VAL (CSMODE_POL_1 | CSMODE_BEF(0) \
		| CSMODE_AFT(0) | CSMODE_CG(1))

/* SPIE register values */
#define SPIE_RXCNT(reg)     ((reg >> 24) & 0x3F)
#define SPIE_TXCNT(reg)     ((reg >> 16) & 0x3F)
#define	SPIE_TXE		BIT(15)	/* TX FIFO empty */
#define	SPIE_DON		BIT(14)	/* TX done */
#define	SPIE_RXT		BIT(13)	/* RX FIFO threshold */
#define	SPIE_RXF		BIT(12)	/* RX FIFO full */
#define	SPIE_TXT		BIT(11)	/* TX FIFO threshold*/
#define	SPIE_RNE		BIT(9)	/* RX FIFO not empty */
#define	SPIE_TNF		BIT(8)	/* TX FIFO not full */

/* SPIM register values */
#define	SPIM_TXE		BIT(15)	/* TX FIFO empty */
#define	SPIM_DON		BIT(14)	/* TX done */
#define	SPIM_RXT		BIT(13)	/* RX FIFO threshold */
#define	SPIM_RXF		BIT(12)	/* RX FIFO full */
#define	SPIM_TXT		BIT(11)	/* TX FIFO threshold*/
#define	SPIM_RNE		BIT(9)	/* RX FIFO not empty */
#define	SPIM_TNF		BIT(8)	/* TX FIFO not full */

/* SPCOM register values */
#define SPCOM_CS(x)		((x) << 30)
#define SPCOM_DO		BIT(28) /* Dual output */
#define SPCOM_TO		BIT(27) /* TX only */
#define SPCOM_RXSKIP(x)		((x) << 16)
#define SPCOM_TRANLEN(x)	((x) << 0)

#define	SPCOM_TRANLEN_MAX	0x10000	/* Max transaction length */

#define AUTOSUSPEND_TIMEOUT 2000

struct fsl_espi {
	struct device *dev;
	void __iomem *reg_base;

	struct list_head *m_transfers;
	struct spi_transfer *tx_t;
	unsigned int tx_pos;
	bool tx_done;
	struct spi_transfer *rx_t;
	unsigned int rx_pos;
	bool rx_done;

	bool swab;
	unsigned int rxskip;

	spinlock_t lock;

	u32 spibrg;             /* SPIBRG input clock */

	struct completion done;
};

struct fsl_espi_cs {
	u32 hw_mode;
};

static inline u32 fsl_espi_read_reg(struct fsl_espi *espi, int offset)
{
	return ioread32be(espi->reg_base + offset);
}

static inline u16 fsl_espi_read_reg16(struct fsl_espi *espi, int offset)
{
	return ioread16be(espi->reg_base + offset);
}

static inline u8 fsl_espi_read_reg8(struct fsl_espi *espi, int offset)
{
	return ioread8(espi->reg_base + offset);
}

static inline void fsl_espi_write_reg(struct fsl_espi *espi, int offset,
				      u32 val)
{
	iowrite32be(val, espi->reg_base + offset);
}

static inline void fsl_espi_write_reg16(struct fsl_espi *espi, int offset,
					u16 val)
{
	iowrite16be(val, espi->reg_base + offset);
}

static inline void fsl_espi_write_reg8(struct fsl_espi *espi, int offset,
				       u8 val)
{
	iowrite8(val, espi->reg_base + offset);
}

static int fsl_espi_check_message(struct spi_message *m)
{
	struct fsl_espi *espi = spi_master_get_devdata(m->spi->master);
	struct spi_transfer *t, *first;

	if (m->frame_length > SPCOM_TRANLEN_MAX) {
		dev_err(espi->dev, "message too long, size is %u bytes\n",
			m->frame_length);
		return -EMSGSIZE;
	}

	first = list_first_entry(&m->transfers, struct spi_transfer,
				 transfer_list);

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (first->bits_per_word != t->bits_per_word ||
		    first->speed_hz != t->speed_hz) {
			dev_err(espi->dev, "bits_per_word/speed_hz should be the same for all transfers\n");
			return -EINVAL;
		}
	}

	/* ESPI supports MSB-first transfers for word size 8 / 16 only */
	if (!(m->spi->mode & SPI_LSB_FIRST) && first->bits_per_word != 8 &&
	    first->bits_per_word != 16) {
		dev_err(espi->dev,
			"MSB-first transfer not supported for wordsize %u\n",
			first->bits_per_word);
		return -EINVAL;
	}

	return 0;
}

static unsigned int fsl_espi_check_rxskip_mode(struct spi_message *m)
{
	struct spi_transfer *t;
	unsigned int i = 0, rxskip = 0;

	/*
	 * prerequisites for ESPI rxskip mode:
	 * - message has two transfers
	 * - first transfer is a write and second is a read
	 *
	 * In addition the current low-level transfer mechanism requires
	 * that the rxskip bytes fit into the TX FIFO. Else the transfer
	 * would hang because after the first FSL_ESPI_FIFO_SIZE bytes
	 * the TX FIFO isn't re-filled.
	 */
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (i == 0) {
			if (!t->tx_buf || t->rx_buf ||
			    t->len > FSL_ESPI_FIFO_SIZE)
				return 0;
			rxskip = t->len;
		} else if (i == 1) {
			if (t->tx_buf || !t->rx_buf)
				return 0;
		}
		i++;
	}

	return i == 2 ? rxskip : 0;
}

static void fsl_espi_fill_tx_fifo(struct fsl_espi *espi, u32 events)
{
	u32 tx_fifo_avail;
	unsigned int tx_left;
	const void *tx_buf;

	/* if events is zero transfer has not started and tx fifo is empty */
	tx_fifo_avail = events ? SPIE_TXCNT(events) :  FSL_ESPI_FIFO_SIZE;
start:
	tx_left = espi->tx_t->len - espi->tx_pos;
	tx_buf = espi->tx_t->tx_buf;
	while (tx_fifo_avail >= min(4U, tx_left) && tx_left) {
		if (tx_left >= 4) {
			if (!tx_buf)
				fsl_espi_write_reg(espi, ESPI_SPITF, 0);
			else if (espi->swab)
				fsl_espi_write_reg(espi, ESPI_SPITF,
					swahb32p(tx_buf + espi->tx_pos));
			else
				fsl_espi_write_reg(espi, ESPI_SPITF,
					*(u32 *)(tx_buf + espi->tx_pos));
			espi->tx_pos += 4;
			tx_left -= 4;
			tx_fifo_avail -= 4;
		} else if (tx_left >= 2 && tx_buf && espi->swab) {
			fsl_espi_write_reg16(espi, ESPI_SPITF,
					swab16p(tx_buf + espi->tx_pos));
			espi->tx_pos += 2;
			tx_left -= 2;
			tx_fifo_avail -= 2;
		} else {
			if (!tx_buf)
				fsl_espi_write_reg8(espi, ESPI_SPITF, 0);
			else
				fsl_espi_write_reg8(espi, ESPI_SPITF,
					*(u8 *)(tx_buf + espi->tx_pos));
			espi->tx_pos += 1;
			tx_left -= 1;
			tx_fifo_avail -= 1;
		}
	}

	if (!tx_left) {
		/* Last transfer finished, in rxskip mode only one is needed */
		if (list_is_last(&espi->tx_t->transfer_list,
		    espi->m_transfers) || espi->rxskip) {
			espi->tx_done = true;
			return;
		}
		espi->tx_t = list_next_entry(espi->tx_t, transfer_list);
		espi->tx_pos = 0;
		/* continue with next transfer if tx fifo is not full */
		if (tx_fifo_avail)
			goto start;
	}
}

static void fsl_espi_read_rx_fifo(struct fsl_espi *espi, u32 events)
{
	u32 rx_fifo_avail = SPIE_RXCNT(events);
	unsigned int rx_left;
	void *rx_buf;

start:
	rx_left = espi->rx_t->len - espi->rx_pos;
	rx_buf = espi->rx_t->rx_buf;
	while (rx_fifo_avail >= min(4U, rx_left) && rx_left) {
		if (rx_left >= 4) {
			u32 val = fsl_espi_read_reg(espi, ESPI_SPIRF);

			if (rx_buf && espi->swab)
				*(u32 *)(rx_buf + espi->rx_pos) = swahb32(val);
			else if (rx_buf)
				*(u32 *)(rx_buf + espi->rx_pos) = val;
			espi->rx_pos += 4;
			rx_left -= 4;
			rx_fifo_avail -= 4;
		} else if (rx_left >= 2 && rx_buf && espi->swab) {
			u16 val = fsl_espi_read_reg16(espi, ESPI_SPIRF);

			*(u16 *)(rx_buf + espi->rx_pos) = swab16(val);
			espi->rx_pos += 2;
			rx_left -= 2;
			rx_fifo_avail -= 2;
		} else {
			u8 val = fsl_espi_read_reg8(espi, ESPI_SPIRF);

			if (rx_buf)
				*(u8 *)(rx_buf + espi->rx_pos) = val;
			espi->rx_pos += 1;
			rx_left -= 1;
			rx_fifo_avail -= 1;
		}
	}

	if (!rx_left) {
		if (list_is_last(&espi->rx_t->transfer_list,
		    espi->m_transfers)) {
			espi->rx_done = true;
			return;
		}
		espi->rx_t = list_next_entry(espi->rx_t, transfer_list);
		espi->rx_pos = 0;
		/* continue with next transfer if rx fifo is not empty */
		if (rx_fifo_avail)
			goto start;
	}
}

static void fsl_espi_setup_transfer(struct spi_device *spi,
					struct spi_transfer *t)
{
	struct fsl_espi *espi = spi_master_get_devdata(spi->master);
	int bits_per_word = t ? t->bits_per_word : spi->bits_per_word;
	u32 pm, hz = t ? t->speed_hz : spi->max_speed_hz;
	struct fsl_espi_cs *cs = spi_get_ctldata(spi);
	u32 hw_mode_old = cs->hw_mode;

	/* mask out bits we are going to set */
	cs->hw_mode &= ~(CSMODE_LEN(0xF) | CSMODE_DIV16 | CSMODE_PM(0xF));

	cs->hw_mode |= CSMODE_LEN(bits_per_word - 1);

	pm = DIV_ROUND_UP(espi->spibrg, hz * 4) - 1;

	if (pm > 15) {
		cs->hw_mode |= CSMODE_DIV16;
		pm = DIV_ROUND_UP(espi->spibrg, hz * 16 * 4) - 1;
	}

	cs->hw_mode |= CSMODE_PM(pm);

	/* don't write the mode register if the mode doesn't change */
	if (cs->hw_mode != hw_mode_old)
		fsl_espi_write_reg(espi, ESPI_SPMODEx(spi->chip_select),
				   cs->hw_mode);
}

static int fsl_espi_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct fsl_espi *espi = spi_master_get_devdata(spi->master);
	unsigned int rx_len = t->len;
	u32 mask, spcom;
	int ret;

	reinit_completion(&espi->done);

	/* Set SPCOM[CS] and SPCOM[TRANLEN] field */
	spcom = SPCOM_CS(spi->chip_select);
	spcom |= SPCOM_TRANLEN(t->len - 1);

	/* configure RXSKIP mode */
	if (espi->rxskip) {
		spcom |= SPCOM_RXSKIP(espi->rxskip);
		rx_len = t->len - espi->rxskip;
		if (t->rx_nbits == SPI_NBITS_DUAL)
			spcom |= SPCOM_DO;
	}

	fsl_espi_write_reg(espi, ESPI_SPCOM, spcom);

	/* enable interrupts */
	mask = SPIM_DON;
	if (rx_len > FSL_ESPI_FIFO_SIZE)
		mask |= SPIM_RXT;
	fsl_espi_write_reg(espi, ESPI_SPIM, mask);

	/* Prevent filling the fifo from getting interrupted */
	spin_lock_irq(&espi->lock);
	fsl_espi_fill_tx_fifo(espi, 0);
	spin_unlock_irq(&espi->lock);

	/* Won't hang up forever, SPI bus sometimes got lost interrupts... */
	ret = wait_for_completion_timeout(&espi->done, 2 * HZ);
	if (ret == 0)
		dev_err(espi->dev, "Transfer timed out!\n");

	/* disable rx ints */
	fsl_espi_write_reg(espi, ESPI_SPIM, 0);

	return ret == 0 ? -ETIMEDOUT : 0;
}

static int fsl_espi_trans(struct spi_message *m, struct spi_transfer *trans)
{
	struct fsl_espi *espi = spi_master_get_devdata(m->spi->master);
	struct spi_device *spi = m->spi;
	int ret;

	/* In case of LSB-first and bits_per_word > 8 byte-swap all words */
	espi->swab = spi->mode & SPI_LSB_FIRST && trans->bits_per_word > 8;

	espi->m_transfers = &m->transfers;
	espi->tx_t = list_first_entry(&m->transfers, struct spi_transfer,
				      transfer_list);
	espi->tx_pos = 0;
	espi->tx_done = false;
	espi->rx_t = list_first_entry(&m->transfers, struct spi_transfer,
				      transfer_list);
	espi->rx_pos = 0;
	espi->rx_done = false;

	espi->rxskip = fsl_espi_check_rxskip_mode(m);
	if (trans->rx_nbits == SPI_NBITS_DUAL && !espi->rxskip) {
		dev_err(espi->dev, "Dual output mode requires RXSKIP mode!\n");
		return -EINVAL;
	}

	/* In RXSKIP mode skip first transfer for reads */
	if (espi->rxskip)
		espi->rx_t = list_next_entry(espi->rx_t, transfer_list);

	fsl_espi_setup_transfer(spi, trans);

	ret = fsl_espi_bufs(spi, trans);

	spi_transfer_delay_exec(trans);

	return ret;
}

static int fsl_espi_do_one_msg(struct spi_master *master,
			       struct spi_message *m)
{
	unsigned int delay_usecs = 0, rx_nbits = 0;
	unsigned int delay_nsecs = 0, delay_nsecs1 = 0;
	struct spi_transfer *t, trans = {};
	int ret;

	ret = fsl_espi_check_message(m);
	if (ret)
		goto out;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->delay_usecs) {
			if (t->delay_usecs > delay_usecs) {
				delay_usecs = t->delay_usecs;
				delay_nsecs = delay_usecs * 1000;
			}
		} else {
			delay_nsecs1 = spi_delay_to_ns(&t->delay, t);
			if (delay_nsecs1 > delay_nsecs)
				delay_nsecs = delay_nsecs1;
		}
		if (t->rx_nbits > rx_nbits)
			rx_nbits = t->rx_nbits;
	}

	t = list_first_entry(&m->transfers, struct spi_transfer,
			     transfer_list);

	trans.len = m->frame_length;
	trans.speed_hz = t->speed_hz;
	trans.bits_per_word = t->bits_per_word;
	trans.delay.value = delay_nsecs;
	trans.delay.unit = SPI_DELAY_UNIT_NSECS;
	trans.rx_nbits = rx_nbits;

	if (trans.len)
		ret = fsl_espi_trans(m, &trans);

	m->actual_length = ret ? 0 : trans.len;
out:
	if (m->status == -EINPROGRESS)
		m->status = ret;

	spi_finalize_current_message(master);

	return ret;
}

static int fsl_espi_setup(struct spi_device *spi)
{
	struct fsl_espi *espi;
	u32 loop_mode;
	struct fsl_espi_cs *cs = spi_get_ctldata(spi);

	if (!cs) {
		cs = kzalloc(sizeof(*cs), GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi_set_ctldata(spi, cs);
	}

	espi = spi_master_get_devdata(spi->master);

	pm_runtime_get_sync(espi->dev);

	cs->hw_mode = fsl_espi_read_reg(espi, ESPI_SPMODEx(spi->chip_select));
	/* mask out bits we are going to set */
	cs->hw_mode &= ~(CSMODE_CP_BEGIN_EDGECLK | CSMODE_CI_INACTIVEHIGH
			 | CSMODE_REV);

	if (spi->mode & SPI_CPHA)
		cs->hw_mode |= CSMODE_CP_BEGIN_EDGECLK;
	if (spi->mode & SPI_CPOL)
		cs->hw_mode |= CSMODE_CI_INACTIVEHIGH;
	if (!(spi->mode & SPI_LSB_FIRST))
		cs->hw_mode |= CSMODE_REV;

	/* Handle the loop mode */
	loop_mode = fsl_espi_read_reg(espi, ESPI_SPMODE);
	loop_mode &= ~SPMODE_LOOP;
	if (spi->mode & SPI_LOOP)
		loop_mode |= SPMODE_LOOP;
	fsl_espi_write_reg(espi, ESPI_SPMODE, loop_mode);

	fsl_espi_setup_transfer(spi, NULL);

	pm_runtime_mark_last_busy(espi->dev);
	pm_runtime_put_autosuspend(espi->dev);

	return 0;
}

static void fsl_espi_cleanup(struct spi_device *spi)
{
	struct fsl_espi_cs *cs = spi_get_ctldata(spi);

	kfree(cs);
	spi_set_ctldata(spi, NULL);
}

static void fsl_espi_cpu_irq(struct fsl_espi *espi, u32 events)
{
	if (!espi->rx_done)
		fsl_espi_read_rx_fifo(espi, events);

	if (!espi->tx_done)
		fsl_espi_fill_tx_fifo(espi, events);

	if (!espi->tx_done || !espi->rx_done)
		return;

	/* we're done, but check for errors before returning */
	events = fsl_espi_read_reg(espi, ESPI_SPIE);

	if (!(events & SPIE_DON))
		dev_err(espi->dev,
			"Transfer done but SPIE_DON isn't set!\n");

	if (SPIE_RXCNT(events) || SPIE_TXCNT(events) != FSL_ESPI_FIFO_SIZE) {
		dev_err(espi->dev, "Transfer done but rx/tx fifo's aren't empty!\n");
		dev_err(espi->dev, "SPIE_RXCNT = %d, SPIE_TXCNT = %d\n",
			SPIE_RXCNT(events), SPIE_TXCNT(events));
	}

	complete(&espi->done);
}

static irqreturn_t fsl_espi_irq(s32 irq, void *context_data)
{
	struct fsl_espi *espi = context_data;
	u32 events, mask;

	spin_lock(&espi->lock);

	/* Get interrupt events(tx/rx) */
	events = fsl_espi_read_reg(espi, ESPI_SPIE);
	mask = fsl_espi_read_reg(espi, ESPI_SPIM);
	if (!(events & mask)) {
		spin_unlock(&espi->lock);
		return IRQ_NONE;
	}

	dev_vdbg(espi->dev, "%s: events %x\n", __func__, events);

	fsl_espi_cpu_irq(espi, events);

	/* Clear the events */
	fsl_espi_write_reg(espi, ESPI_SPIE, events);

	spin_unlock(&espi->lock);

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int fsl_espi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct fsl_espi *espi = spi_master_get_devdata(master);
	u32 regval;

	regval = fsl_espi_read_reg(espi, ESPI_SPMODE);
	regval &= ~SPMODE_ENABLE;
	fsl_espi_write_reg(espi, ESPI_SPMODE, regval);

	return 0;
}

static int fsl_espi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct fsl_espi *espi = spi_master_get_devdata(master);
	u32 regval;

	regval = fsl_espi_read_reg(espi, ESPI_SPMODE);
	regval |= SPMODE_ENABLE;
	fsl_espi_write_reg(espi, ESPI_SPMODE, regval);

	return 0;
}
#endif

static size_t fsl_espi_max_message_size(struct spi_device *spi)
{
	return SPCOM_TRANLEN_MAX;
}

static void fsl_espi_init_regs(struct device *dev, bool initial)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct fsl_espi *espi = spi_master_get_devdata(master);
	struct device_node *nc;
	u32 csmode, cs, prop;
	int ret;

	/* SPI controller initializations */
	fsl_espi_write_reg(espi, ESPI_SPMODE, 0);
	fsl_espi_write_reg(espi, ESPI_SPIM, 0);
	fsl_espi_write_reg(espi, ESPI_SPCOM, 0);
	fsl_espi_write_reg(espi, ESPI_SPIE, 0xffffffff);

	/* Init eSPI CS mode register */
	for_each_available_child_of_node(master->dev.of_node, nc) {
		/* get chip select */
		ret = of_property_read_u32(nc, "reg", &cs);
		if (ret || cs >= master->num_chipselect)
			continue;

		csmode = CSMODE_INIT_VAL;

		/* check if CSBEF is set in device tree */
		ret = of_property_read_u32(nc, "fsl,csbef", &prop);
		if (!ret) {
			csmode &= ~(CSMODE_BEF(0xf));
			csmode |= CSMODE_BEF(prop);
		}

		/* check if CSAFT is set in device tree */
		ret = of_property_read_u32(nc, "fsl,csaft", &prop);
		if (!ret) {
			csmode &= ~(CSMODE_AFT(0xf));
			csmode |= CSMODE_AFT(prop);
		}

		fsl_espi_write_reg(espi, ESPI_SPMODEx(cs), csmode);

		if (initial)
			dev_info(dev, "cs=%u, init_csmode=0x%x\n", cs, csmode);
	}

	/* Enable SPI interface */
	fsl_espi_write_reg(espi, ESPI_SPMODE, SPMODE_INIT_VAL | SPMODE_ENABLE);
}

static int fsl_espi_probe(struct device *dev, struct resource *mem,
			  unsigned int irq, unsigned int num_cs)
{
	struct spi_master *master;
	struct fsl_espi *espi;
	int ret;

	master = spi_alloc_master(dev, sizeof(struct fsl_espi));
	if (!master)
		return -ENOMEM;

	dev_set_drvdata(dev, master);

	master->mode_bits = SPI_RX_DUAL | SPI_CPOL | SPI_CPHA | SPI_CS_HIGH |
			    SPI_LSB_FIRST | SPI_LOOP;
	master->dev.of_node = dev->of_node;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 16);
	master->setup = fsl_espi_setup;
	master->cleanup = fsl_espi_cleanup;
	master->transfer_one_message = fsl_espi_do_one_msg;
	master->auto_runtime_pm = true;
	master->max_message_size = fsl_espi_max_message_size;
	master->num_chipselect = num_cs;

	espi = spi_master_get_devdata(master);
	spin_lock_init(&espi->lock);

	espi->dev = dev;
	espi->spibrg = fsl_get_sys_freq();
	if (espi->spibrg == -1) {
		dev_err(dev, "Can't get sys frequency!\n");
		ret = -EINVAL;
		goto err_probe;
	}
	/* determined by clock divider fields DIV16/PM in register SPMODEx */
	master->min_speed_hz = DIV_ROUND_UP(espi->spibrg, 4 * 16 * 16);
	master->max_speed_hz = DIV_ROUND_UP(espi->spibrg, 4);

	init_completion(&espi->done);

	espi->reg_base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(espi->reg_base)) {
		ret = PTR_ERR(espi->reg_base);
		goto err_probe;
	}

	/* Register for SPI Interrupt */
	ret = devm_request_irq(dev, irq, fsl_espi_irq, 0, "fsl_espi", espi);
	if (ret)
		goto err_probe;

	fsl_espi_init_regs(dev, true);

	pm_runtime_set_autosuspend_delay(dev, AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	ret = devm_spi_register_master(dev, master);
	if (ret < 0)
		goto err_pm;

	dev_info(dev, "at 0x%p (irq = %u)\n", espi->reg_base, irq);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_pm:
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
err_probe:
	spi_master_put(master);
	return ret;
}

static int of_fsl_espi_get_chipselects(struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 num_cs;
	int ret;

	ret = of_property_read_u32(np, "fsl,espi-num-chipselects", &num_cs);
	if (ret) {
		dev_err(dev, "No 'fsl,espi-num-chipselects' property\n");
		return 0;
	}

	return num_cs;
}

static int of_fsl_espi_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct resource mem;
	unsigned int irq, num_cs;
	int ret;

	if (of_property_read_bool(np, "mode")) {
		dev_err(dev, "mode property is not supported on ESPI!\n");
		return -EINVAL;
	}

	num_cs = of_fsl_espi_get_chipselects(dev);
	if (!num_cs)
		return -EINVAL;

	ret = of_address_to_resource(np, 0, &mem);
	if (ret)
		return ret;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return -EINVAL;

	return fsl_espi_probe(dev, &mem, irq, num_cs);
}

static int of_fsl_espi_remove(struct platform_device *dev)
{
	pm_runtime_disable(&dev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int of_fsl_espi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	int ret;

	ret = spi_master_suspend(master);
	if (ret)
		return ret;

	return pm_runtime_force_suspend(dev);
}

static int of_fsl_espi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	int ret;

	fsl_espi_init_regs(dev, false);

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	return spi_master_resume(master);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops espi_pm = {
	SET_RUNTIME_PM_OPS(fsl_espi_runtime_suspend,
			   fsl_espi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(of_fsl_espi_suspend, of_fsl_espi_resume)
};

static const struct of_device_id of_fsl_espi_match[] = {
	{ .compatible = "fsl,mpc8536-espi" },
	{}
};
MODULE_DEVICE_TABLE(of, of_fsl_espi_match);

static struct platform_driver fsl_espi_driver = {
	.driver = {
		.name = "fsl_espi",
		.of_match_table = of_fsl_espi_match,
		.pm = &espi_pm,
	},
	.probe		= of_fsl_espi_probe,
	.remove		= of_fsl_espi_remove,
};
module_platform_driver(fsl_espi_driver);

MODULE_AUTHOR("Mingkai Hu");
MODULE_DESCRIPTION("Enhanced Freescale SPI Driver");
MODULE_LICENSE("GPL");
