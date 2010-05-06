/*
 * MPC8xxx SPI controller driver.
 *
 * Maintainer: Kumar Gala
 *
 * Copyright (C) 2006 Polycom, Inc.
 *
 * CPM SPI and QE buffer descriptors mode support:
 * Copyright (c) 2009  MontaVista Software, Inc.
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_spi.h>
#include <linux/slab.h>

#include <sysdev/fsl_soc.h>
#include <asm/cpm.h>
#include <asm/qe.h>
#include <asm/irq.h>

/* CPM1 and CPM2 are mutually exclusive. */
#ifdef CONFIG_CPM1
#include <asm/cpm1.h>
#define CPM_SPI_CMD mk_cr_cmd(CPM_CR_CH_SPI, 0)
#else
#include <asm/cpm2.h>
#define CPM_SPI_CMD mk_cr_cmd(CPM_CR_SPI_PAGE, CPM_CR_SPI_SBLOCK, 0, 0)
#endif

/* SPI Controller registers */
struct mpc8xxx_spi_reg {
	u8 res1[0x20];
	__be32 mode;
	__be32 event;
	__be32 mask;
	__be32 command;
	__be32 transmit;
	__be32 receive;
};

/* SPI Parameter RAM */
struct spi_pram {
	__be16	rbase;	/* Rx Buffer descriptor base address */
	__be16	tbase;	/* Tx Buffer descriptor base address */
	u8	rfcr;	/* Rx function code */
	u8	tfcr;	/* Tx function code */
	__be16	mrblr;	/* Max receive buffer length */
	__be32	rstate;	/* Internal */
	__be32	rdp;	/* Internal */
	__be16	rbptr;	/* Internal */
	__be16	rbc;	/* Internal */
	__be32	rxtmp;	/* Internal */
	__be32	tstate;	/* Internal */
	__be32	tdp;	/* Internal */
	__be16	tbptr;	/* Internal */
	__be16	tbc;	/* Internal */
	__be32	txtmp;	/* Internal */
	__be32	res;	/* Tx temp. */
	__be16  rpbase;	/* Relocation pointer (CPM1 only) */
	__be16	res1;	/* Reserved */
};

/* SPI Controller mode register definitions */
#define	SPMODE_LOOP		(1 << 30)
#define	SPMODE_CI_INACTIVEHIGH	(1 << 29)
#define	SPMODE_CP_BEGIN_EDGECLK	(1 << 28)
#define	SPMODE_DIV16		(1 << 27)
#define	SPMODE_REV		(1 << 26)
#define	SPMODE_MS		(1 << 25)
#define	SPMODE_ENABLE		(1 << 24)
#define	SPMODE_LEN(x)		((x) << 20)
#define	SPMODE_PM(x)		((x) << 16)
#define	SPMODE_OP		(1 << 14)
#define	SPMODE_CG(x)		((x) << 7)

/*
 * Default for SPI Mode:
 * 	SPI MODE 0 (inactive low, phase middle, MSB, 8-bit length, slow clk
 */
#define	SPMODE_INIT_VAL (SPMODE_CI_INACTIVEHIGH | SPMODE_DIV16 | SPMODE_REV | \
			 SPMODE_MS | SPMODE_LEN(7) | SPMODE_PM(0xf))

/* SPIE register values */
#define	SPIE_NE		0x00000200	/* Not empty */
#define	SPIE_NF		0x00000100	/* Not full */

/* SPIM register values */
#define	SPIM_NE		0x00000200	/* Not empty */
#define	SPIM_NF		0x00000100	/* Not full */

#define	SPIE_TXB	0x00000200	/* Last char is written to tx fifo */
#define	SPIE_RXB	0x00000100	/* Last char is written to rx buf */

/* SPCOM register values */
#define	SPCOM_STR	(1 << 23)	/* Start transmit */

#define	SPI_PRAM_SIZE	0x100
#define	SPI_MRBLR	((unsigned int)PAGE_SIZE)

/* SPI Controller driver's private data. */
struct mpc8xxx_spi {
	struct device *dev;
	struct mpc8xxx_spi_reg __iomem *base;

	/* rx & tx bufs from the spi_transfer */
	const void *tx;
	void *rx;

	int subblock;
	struct spi_pram __iomem *pram;
	struct cpm_buf_desc __iomem *tx_bd;
	struct cpm_buf_desc __iomem *rx_bd;

	struct spi_transfer *xfer_in_progress;

	/* dma addresses for CPM transfers */
	dma_addr_t tx_dma;
	dma_addr_t rx_dma;
	bool map_tx_dma;
	bool map_rx_dma;

	dma_addr_t dma_dummy_tx;
	dma_addr_t dma_dummy_rx;

	/* functions to deal with different sized buffers */
	void (*get_rx) (u32 rx_data, struct mpc8xxx_spi *);
	u32(*get_tx) (struct mpc8xxx_spi *);

	unsigned int count;
	unsigned int irq;

	unsigned nsecs;		/* (clock cycle time)/2 */

	u32 spibrg;		/* SPIBRG input clock */
	u32 rx_shift;		/* RX data reg shift when in qe mode */
	u32 tx_shift;		/* TX data reg shift when in qe mode */

	unsigned int flags;

	struct workqueue_struct *workqueue;
	struct work_struct work;

	struct list_head queue;
	spinlock_t lock;

	struct completion done;
};

static void *mpc8xxx_dummy_rx;
static DEFINE_MUTEX(mpc8xxx_dummy_rx_lock);
static int mpc8xxx_dummy_rx_refcnt;

struct spi_mpc8xxx_cs {
	/* functions to deal with different sized buffers */
	void (*get_rx) (u32 rx_data, struct mpc8xxx_spi *);
	u32 (*get_tx) (struct mpc8xxx_spi *);
	u32 rx_shift;		/* RX data reg shift when in qe mode */
	u32 tx_shift;		/* TX data reg shift when in qe mode */
	u32 hw_mode;		/* Holds HW mode register settings */
};

static inline void mpc8xxx_spi_write_reg(__be32 __iomem *reg, u32 val)
{
	out_be32(reg, val);
}

static inline u32 mpc8xxx_spi_read_reg(__be32 __iomem *reg)
{
	return in_be32(reg);
}

#define MPC83XX_SPI_RX_BUF(type) 					  \
static									  \
void mpc8xxx_spi_rx_buf_##type(u32 data, struct mpc8xxx_spi *mpc8xxx_spi) \
{									  \
	type *rx = mpc8xxx_spi->rx;					  \
	*rx++ = (type)(data >> mpc8xxx_spi->rx_shift);			  \
	mpc8xxx_spi->rx = rx;						  \
}

#define MPC83XX_SPI_TX_BUF(type)				\
static								\
u32 mpc8xxx_spi_tx_buf_##type(struct mpc8xxx_spi *mpc8xxx_spi)	\
{								\
	u32 data;						\
	const type *tx = mpc8xxx_spi->tx;			\
	if (!tx)						\
		return 0;					\
	data = *tx++ << mpc8xxx_spi->tx_shift;			\
	mpc8xxx_spi->tx = tx;					\
	return data;						\
}

MPC83XX_SPI_RX_BUF(u8)
MPC83XX_SPI_RX_BUF(u16)
MPC83XX_SPI_RX_BUF(u32)
MPC83XX_SPI_TX_BUF(u8)
MPC83XX_SPI_TX_BUF(u16)
MPC83XX_SPI_TX_BUF(u32)

static void mpc8xxx_spi_change_mode(struct spi_device *spi)
{
	struct mpc8xxx_spi *mspi = spi_master_get_devdata(spi->master);
	struct spi_mpc8xxx_cs *cs = spi->controller_state;
	__be32 __iomem *mode = &mspi->base->mode;
	unsigned long flags;

	if (cs->hw_mode == mpc8xxx_spi_read_reg(mode))
		return;

	/* Turn off IRQs locally to minimize time that SPI is disabled. */
	local_irq_save(flags);

	/* Turn off SPI unit prior changing mode */
	mpc8xxx_spi_write_reg(mode, cs->hw_mode & ~SPMODE_ENABLE);
	mpc8xxx_spi_write_reg(mode, cs->hw_mode);

	/* When in CPM mode, we need to reinit tx and rx. */
	if (mspi->flags & SPI_CPM_MODE) {
		if (mspi->flags & SPI_QE) {
			qe_issue_cmd(QE_INIT_TX_RX, mspi->subblock,
				     QE_CR_PROTOCOL_UNSPECIFIED, 0);
		} else {
			cpm_command(CPM_SPI_CMD, CPM_CR_INIT_TRX);
			if (mspi->flags & SPI_CPM1) {
				out_be16(&mspi->pram->rbptr,
					 in_be16(&mspi->pram->rbase));
				out_be16(&mspi->pram->tbptr,
					 in_be16(&mspi->pram->tbase));
			}
		}
	}

	local_irq_restore(flags);
}

static void mpc8xxx_spi_chipselect(struct spi_device *spi, int value)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(spi->master);
	struct fsl_spi_platform_data *pdata = spi->dev.parent->platform_data;
	bool pol = spi->mode & SPI_CS_HIGH;
	struct spi_mpc8xxx_cs	*cs = spi->controller_state;

	if (value == BITBANG_CS_INACTIVE) {
		if (pdata->cs_control)
			pdata->cs_control(spi, !pol);
	}

	if (value == BITBANG_CS_ACTIVE) {
		mpc8xxx_spi->rx_shift = cs->rx_shift;
		mpc8xxx_spi->tx_shift = cs->tx_shift;
		mpc8xxx_spi->get_rx = cs->get_rx;
		mpc8xxx_spi->get_tx = cs->get_tx;

		mpc8xxx_spi_change_mode(spi);

		if (pdata->cs_control)
			pdata->cs_control(spi, pol);
	}
}

static
int mpc8xxx_spi_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct mpc8xxx_spi *mpc8xxx_spi;
	u8 bits_per_word, pm;
	u32 hz;
	struct spi_mpc8xxx_cs	*cs = spi->controller_state;

	mpc8xxx_spi = spi_master_get_devdata(spi->master);

	if (t) {
		bits_per_word = t->bits_per_word;
		hz = t->speed_hz;
	} else {
		bits_per_word = 0;
		hz = 0;
	}

	/* spi_transfer level calls that work per-word */
	if (!bits_per_word)
		bits_per_word = spi->bits_per_word;

	/* Make sure its a bit width we support [4..16, 32] */
	if ((bits_per_word < 4)
	    || ((bits_per_word > 16) && (bits_per_word != 32)))
		return -EINVAL;

	if (!hz)
		hz = spi->max_speed_hz;

	cs->rx_shift = 0;
	cs->tx_shift = 0;
	if (bits_per_word <= 8) {
		cs->get_rx = mpc8xxx_spi_rx_buf_u8;
		cs->get_tx = mpc8xxx_spi_tx_buf_u8;
		if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE) {
			cs->rx_shift = 16;
			cs->tx_shift = 24;
		}
	} else if (bits_per_word <= 16) {
		cs->get_rx = mpc8xxx_spi_rx_buf_u16;
		cs->get_tx = mpc8xxx_spi_tx_buf_u16;
		if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE) {
			cs->rx_shift = 16;
			cs->tx_shift = 16;
		}
	} else if (bits_per_word <= 32) {
		cs->get_rx = mpc8xxx_spi_rx_buf_u32;
		cs->get_tx = mpc8xxx_spi_tx_buf_u32;
	} else
		return -EINVAL;

	if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE &&
			spi->mode & SPI_LSB_FIRST) {
		cs->tx_shift = 0;
		if (bits_per_word <= 8)
			cs->rx_shift = 8;
		else
			cs->rx_shift = 0;
	}

	mpc8xxx_spi->rx_shift = cs->rx_shift;
	mpc8xxx_spi->tx_shift = cs->tx_shift;
	mpc8xxx_spi->get_rx = cs->get_rx;
	mpc8xxx_spi->get_tx = cs->get_tx;

	if (bits_per_word == 32)
		bits_per_word = 0;
	else
		bits_per_word = bits_per_word - 1;

	/* mask out bits we are going to set */
	cs->hw_mode &= ~(SPMODE_LEN(0xF) | SPMODE_DIV16
				  | SPMODE_PM(0xF));

	cs->hw_mode |= SPMODE_LEN(bits_per_word);

	if ((mpc8xxx_spi->spibrg / hz) > 64) {
		cs->hw_mode |= SPMODE_DIV16;
		pm = (mpc8xxx_spi->spibrg - 1) / (hz * 64) + 1;

		WARN_ONCE(pm > 16, "%s: Requested speed is too low: %d Hz. "
			  "Will use %d Hz instead.\n", dev_name(&spi->dev),
			  hz, mpc8xxx_spi->spibrg / 1024);
		if (pm > 16)
			pm = 16;
	} else
		pm = (mpc8xxx_spi->spibrg - 1) / (hz * 4) + 1;
	if (pm)
		pm--;

	cs->hw_mode |= SPMODE_PM(pm);

	mpc8xxx_spi_change_mode(spi);
	return 0;
}

static void mpc8xxx_spi_cpm_bufs_start(struct mpc8xxx_spi *mspi)
{
	struct cpm_buf_desc __iomem *tx_bd = mspi->tx_bd;
	struct cpm_buf_desc __iomem *rx_bd = mspi->rx_bd;
	unsigned int xfer_len = min(mspi->count, SPI_MRBLR);
	unsigned int xfer_ofs;

	xfer_ofs = mspi->xfer_in_progress->len - mspi->count;

	out_be32(&rx_bd->cbd_bufaddr, mspi->rx_dma + xfer_ofs);
	out_be16(&rx_bd->cbd_datlen, 0);
	out_be16(&rx_bd->cbd_sc, BD_SC_EMPTY | BD_SC_INTRPT | BD_SC_WRAP);

	out_be32(&tx_bd->cbd_bufaddr, mspi->tx_dma + xfer_ofs);
	out_be16(&tx_bd->cbd_datlen, xfer_len);
	out_be16(&tx_bd->cbd_sc, BD_SC_READY | BD_SC_INTRPT | BD_SC_WRAP |
				 BD_SC_LAST);

	/* start transfer */
	mpc8xxx_spi_write_reg(&mspi->base->command, SPCOM_STR);
}

static int mpc8xxx_spi_cpm_bufs(struct mpc8xxx_spi *mspi,
				struct spi_transfer *t, bool is_dma_mapped)
{
	struct device *dev = mspi->dev;

	if (is_dma_mapped) {
		mspi->map_tx_dma = 0;
		mspi->map_rx_dma = 0;
	} else {
		mspi->map_tx_dma = 1;
		mspi->map_rx_dma = 1;
	}

	if (!t->tx_buf) {
		mspi->tx_dma = mspi->dma_dummy_tx;
		mspi->map_tx_dma = 0;
	}

	if (!t->rx_buf) {
		mspi->rx_dma = mspi->dma_dummy_rx;
		mspi->map_rx_dma = 0;
	}

	if (mspi->map_tx_dma) {
		void *nonconst_tx = (void *)mspi->tx; /* shut up gcc */

		mspi->tx_dma = dma_map_single(dev, nonconst_tx, t->len,
					      DMA_TO_DEVICE);
		if (dma_mapping_error(dev, mspi->tx_dma)) {
			dev_err(dev, "unable to map tx dma\n");
			return -ENOMEM;
		}
	} else {
		mspi->tx_dma = t->tx_dma;
	}

	if (mspi->map_rx_dma) {
		mspi->rx_dma = dma_map_single(dev, mspi->rx, t->len,
					      DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, mspi->rx_dma)) {
			dev_err(dev, "unable to map rx dma\n");
			goto err_rx_dma;
		}
	} else {
		mspi->rx_dma = t->rx_dma;
	}

	/* enable rx ints */
	mpc8xxx_spi_write_reg(&mspi->base->mask, SPIE_RXB);

	mspi->xfer_in_progress = t;
	mspi->count = t->len;

	/* start CPM transfers */
	mpc8xxx_spi_cpm_bufs_start(mspi);

	return 0;

err_rx_dma:
	if (mspi->map_tx_dma)
		dma_unmap_single(dev, mspi->tx_dma, t->len, DMA_TO_DEVICE);
	return -ENOMEM;
}

static void mpc8xxx_spi_cpm_bufs_complete(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;
	struct spi_transfer *t = mspi->xfer_in_progress;

	if (mspi->map_tx_dma)
		dma_unmap_single(dev, mspi->tx_dma, t->len, DMA_TO_DEVICE);
	if (mspi->map_tx_dma)
		dma_unmap_single(dev, mspi->rx_dma, t->len, DMA_FROM_DEVICE);
	mspi->xfer_in_progress = NULL;
}

static int mpc8xxx_spi_cpu_bufs(struct mpc8xxx_spi *mspi,
				struct spi_transfer *t, unsigned int len)
{
	u32 word;

	mspi->count = len;

	/* enable rx ints */
	mpc8xxx_spi_write_reg(&mspi->base->mask, SPIM_NE);

	/* transmit word */
	word = mspi->get_tx(mspi);
	mpc8xxx_spi_write_reg(&mspi->base->transmit, word);

	return 0;
}

static int mpc8xxx_spi_bufs(struct spi_device *spi, struct spi_transfer *t,
			    bool is_dma_mapped)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(spi->master);
	unsigned int len = t->len;
	u8 bits_per_word;
	int ret;

	bits_per_word = spi->bits_per_word;
	if (t->bits_per_word)
		bits_per_word = t->bits_per_word;

	if (bits_per_word > 8) {
		/* invalid length? */
		if (len & 1)
			return -EINVAL;
		len /= 2;
	}
	if (bits_per_word > 16) {
		/* invalid length? */
		if (len & 1)
			return -EINVAL;
		len /= 2;
	}

	mpc8xxx_spi->tx = t->tx_buf;
	mpc8xxx_spi->rx = t->rx_buf;

	INIT_COMPLETION(mpc8xxx_spi->done);

	if (mpc8xxx_spi->flags & SPI_CPM_MODE)
		ret = mpc8xxx_spi_cpm_bufs(mpc8xxx_spi, t, is_dma_mapped);
	else
		ret = mpc8xxx_spi_cpu_bufs(mpc8xxx_spi, t, len);
	if (ret)
		return ret;

	wait_for_completion(&mpc8xxx_spi->done);

	/* disable rx ints */
	mpc8xxx_spi_write_reg(&mpc8xxx_spi->base->mask, 0);

	if (mpc8xxx_spi->flags & SPI_CPM_MODE)
		mpc8xxx_spi_cpm_bufs_complete(mpc8xxx_spi);

	return mpc8xxx_spi->count;
}

static void mpc8xxx_spi_do_one_msg(struct spi_message *m)
{
	struct spi_device *spi = m->spi;
	struct spi_transfer *t;
	unsigned int cs_change;
	const int nsecs = 50;
	int status;

	cs_change = 1;
	status = 0;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->bits_per_word || t->speed_hz) {
			/* Don't allow changes if CS is active */
			status = -EINVAL;

			if (cs_change)
				status = mpc8xxx_spi_setup_transfer(spi, t);
			if (status < 0)
				break;
		}

		if (cs_change) {
			mpc8xxx_spi_chipselect(spi, BITBANG_CS_ACTIVE);
			ndelay(nsecs);
		}
		cs_change = t->cs_change;
		if (t->len)
			status = mpc8xxx_spi_bufs(spi, t, m->is_dma_mapped);
		if (status) {
			status = -EMSGSIZE;
			break;
		}
		m->actual_length += t->len;

		if (t->delay_usecs)
			udelay(t->delay_usecs);

		if (cs_change) {
			ndelay(nsecs);
			mpc8xxx_spi_chipselect(spi, BITBANG_CS_INACTIVE);
			ndelay(nsecs);
		}
	}

	m->status = status;
	m->complete(m->context);

	if (status || !cs_change) {
		ndelay(nsecs);
		mpc8xxx_spi_chipselect(spi, BITBANG_CS_INACTIVE);
	}

	mpc8xxx_spi_setup_transfer(spi, NULL);
}

static void mpc8xxx_spi_work(struct work_struct *work)
{
	struct mpc8xxx_spi *mpc8xxx_spi = container_of(work, struct mpc8xxx_spi,
						       work);

	spin_lock_irq(&mpc8xxx_spi->lock);
	while (!list_empty(&mpc8xxx_spi->queue)) {
		struct spi_message *m = container_of(mpc8xxx_spi->queue.next,
						   struct spi_message, queue);

		list_del_init(&m->queue);
		spin_unlock_irq(&mpc8xxx_spi->lock);

		mpc8xxx_spi_do_one_msg(m);

		spin_lock_irq(&mpc8xxx_spi->lock);
	}
	spin_unlock_irq(&mpc8xxx_spi->lock);
}

static int mpc8xxx_spi_setup(struct spi_device *spi)
{
	struct mpc8xxx_spi *mpc8xxx_spi;
	int retval;
	u32 hw_mode;
	struct spi_mpc8xxx_cs	*cs = spi->controller_state;

	if (!spi->max_speed_hz)
		return -EINVAL;

	if (!cs) {
		cs = kzalloc(sizeof *cs, GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi->controller_state = cs;
	}
	mpc8xxx_spi = spi_master_get_devdata(spi->master);

	hw_mode = cs->hw_mode; /* Save orginal settings */
	cs->hw_mode = mpc8xxx_spi_read_reg(&mpc8xxx_spi->base->mode);
	/* mask out bits we are going to set */
	cs->hw_mode &= ~(SPMODE_CP_BEGIN_EDGECLK | SPMODE_CI_INACTIVEHIGH
			 | SPMODE_REV | SPMODE_LOOP);

	if (spi->mode & SPI_CPHA)
		cs->hw_mode |= SPMODE_CP_BEGIN_EDGECLK;
	if (spi->mode & SPI_CPOL)
		cs->hw_mode |= SPMODE_CI_INACTIVEHIGH;
	if (!(spi->mode & SPI_LSB_FIRST))
		cs->hw_mode |= SPMODE_REV;
	if (spi->mode & SPI_LOOP)
		cs->hw_mode |= SPMODE_LOOP;

	retval = mpc8xxx_spi_setup_transfer(spi, NULL);
	if (retval < 0) {
		cs->hw_mode = hw_mode; /* Restore settings */
		return retval;
	}
	return 0;
}

static void mpc8xxx_spi_cpm_irq(struct mpc8xxx_spi *mspi, u32 events)
{
	u16 len;

	dev_dbg(mspi->dev, "%s: bd datlen %d, count %d\n", __func__,
		in_be16(&mspi->rx_bd->cbd_datlen), mspi->count);

	len = in_be16(&mspi->rx_bd->cbd_datlen);
	if (len > mspi->count) {
		WARN_ON(1);
		len = mspi->count;
	}

	/* Clear the events */
	mpc8xxx_spi_write_reg(&mspi->base->event, events);

	mspi->count -= len;
	if (mspi->count)
		mpc8xxx_spi_cpm_bufs_start(mspi);
	else
		complete(&mspi->done);
}

static void mpc8xxx_spi_cpu_irq(struct mpc8xxx_spi *mspi, u32 events)
{
	/* We need handle RX first */
	if (events & SPIE_NE) {
		u32 rx_data = mpc8xxx_spi_read_reg(&mspi->base->receive);

		if (mspi->rx)
			mspi->get_rx(rx_data, mspi);
	}

	if ((events & SPIE_NF) == 0)
		/* spin until TX is done */
		while (((events =
			mpc8xxx_spi_read_reg(&mspi->base->event)) &
						SPIE_NF) == 0)
			cpu_relax();

	/* Clear the events */
	mpc8xxx_spi_write_reg(&mspi->base->event, events);

	mspi->count -= 1;
	if (mspi->count) {
		u32 word = mspi->get_tx(mspi);

		mpc8xxx_spi_write_reg(&mspi->base->transmit, word);
	} else {
		complete(&mspi->done);
	}
}

static irqreturn_t mpc8xxx_spi_irq(s32 irq, void *context_data)
{
	struct mpc8xxx_spi *mspi = context_data;
	irqreturn_t ret = IRQ_NONE;
	u32 events;

	/* Get interrupt events(tx/rx) */
	events = mpc8xxx_spi_read_reg(&mspi->base->event);
	if (events)
		ret = IRQ_HANDLED;

	dev_dbg(mspi->dev, "%s: events %x\n", __func__, events);

	if (mspi->flags & SPI_CPM_MODE)
		mpc8xxx_spi_cpm_irq(mspi, events);
	else
		mpc8xxx_spi_cpu_irq(mspi, events);

	return ret;
}

static int mpc8xxx_spi_transfer(struct spi_device *spi,
				struct spi_message *m)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(spi->master);
	unsigned long flags;

	m->actual_length = 0;
	m->status = -EINPROGRESS;

	spin_lock_irqsave(&mpc8xxx_spi->lock, flags);
	list_add_tail(&m->queue, &mpc8xxx_spi->queue);
	queue_work(mpc8xxx_spi->workqueue, &mpc8xxx_spi->work);
	spin_unlock_irqrestore(&mpc8xxx_spi->lock, flags);

	return 0;
}


static void mpc8xxx_spi_cleanup(struct spi_device *spi)
{
	kfree(spi->controller_state);
}

static void *mpc8xxx_spi_alloc_dummy_rx(void)
{
	mutex_lock(&mpc8xxx_dummy_rx_lock);

	if (!mpc8xxx_dummy_rx)
		mpc8xxx_dummy_rx = kmalloc(SPI_MRBLR, GFP_KERNEL);
	if (mpc8xxx_dummy_rx)
		mpc8xxx_dummy_rx_refcnt++;

	mutex_unlock(&mpc8xxx_dummy_rx_lock);

	return mpc8xxx_dummy_rx;
}

static void mpc8xxx_spi_free_dummy_rx(void)
{
	mutex_lock(&mpc8xxx_dummy_rx_lock);

	switch (mpc8xxx_dummy_rx_refcnt) {
	case 0:
		WARN_ON(1);
		break;
	case 1:
		kfree(mpc8xxx_dummy_rx);
		mpc8xxx_dummy_rx = NULL;
		/* fall through */
	default:
		mpc8xxx_dummy_rx_refcnt--;
		break;
	}

	mutex_unlock(&mpc8xxx_dummy_rx_lock);
}

static unsigned long mpc8xxx_spi_cpm_get_pram(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;
	struct device_node *np = dev_archdata_get_node(&dev->archdata);
	const u32 *iprop;
	int size;
	unsigned long spi_base_ofs;
	unsigned long pram_ofs = -ENOMEM;

	/* Can't use of_address_to_resource(), QE muram isn't at 0. */
	iprop = of_get_property(np, "reg", &size);

	/* QE with a fixed pram location? */
	if (mspi->flags & SPI_QE && iprop && size == sizeof(*iprop) * 4)
		return cpm_muram_alloc_fixed(iprop[2], SPI_PRAM_SIZE);

	/* QE but with a dynamic pram location? */
	if (mspi->flags & SPI_QE) {
		pram_ofs = cpm_muram_alloc(SPI_PRAM_SIZE, 64);
		qe_issue_cmd(QE_ASSIGN_PAGE_TO_DEVICE, mspi->subblock,
				QE_CR_PROTOCOL_UNSPECIFIED, pram_ofs);
		return pram_ofs;
	}

	/* CPM1 and CPM2 pram must be at a fixed addr. */
	if (!iprop || size != sizeof(*iprop) * 4)
		return -ENOMEM;

	spi_base_ofs = cpm_muram_alloc_fixed(iprop[2], 2);
	if (IS_ERR_VALUE(spi_base_ofs))
		return -ENOMEM;

	if (mspi->flags & SPI_CPM2) {
		pram_ofs = cpm_muram_alloc(SPI_PRAM_SIZE, 64);
		if (!IS_ERR_VALUE(pram_ofs)) {
			u16 __iomem *spi_base = cpm_muram_addr(spi_base_ofs);

			out_be16(spi_base, pram_ofs);
		}
	} else {
		struct spi_pram __iomem *pram = cpm_muram_addr(spi_base_ofs);
		u16 rpbase = in_be16(&pram->rpbase);

		/* Microcode relocation patch applied? */
		if (rpbase)
			pram_ofs = rpbase;
		else
			return spi_base_ofs;
	}

	cpm_muram_free(spi_base_ofs);
	return pram_ofs;
}

static int mpc8xxx_spi_cpm_init(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;
	struct device_node *np = dev_archdata_get_node(&dev->archdata);
	const u32 *iprop;
	int size;
	unsigned long pram_ofs;
	unsigned long bds_ofs;

	if (!(mspi->flags & SPI_CPM_MODE))
		return 0;

	if (!mpc8xxx_spi_alloc_dummy_rx())
		return -ENOMEM;

	if (mspi->flags & SPI_QE) {
		iprop = of_get_property(np, "cell-index", &size);
		if (iprop && size == sizeof(*iprop))
			mspi->subblock = *iprop;

		switch (mspi->subblock) {
		default:
			dev_warn(dev, "cell-index unspecified, assuming SPI1");
			/* fall through */
		case 0:
			mspi->subblock = QE_CR_SUBBLOCK_SPI1;
			break;
		case 1:
			mspi->subblock = QE_CR_SUBBLOCK_SPI2;
			break;
		}
	}

	pram_ofs = mpc8xxx_spi_cpm_get_pram(mspi);
	if (IS_ERR_VALUE(pram_ofs)) {
		dev_err(dev, "can't allocate spi parameter ram\n");
		goto err_pram;
	}

	bds_ofs = cpm_muram_alloc(sizeof(*mspi->tx_bd) +
				  sizeof(*mspi->rx_bd), 8);
	if (IS_ERR_VALUE(bds_ofs)) {
		dev_err(dev, "can't allocate bds\n");
		goto err_bds;
	}

	mspi->dma_dummy_tx = dma_map_single(dev, empty_zero_page, PAGE_SIZE,
					    DMA_TO_DEVICE);
	if (dma_mapping_error(dev, mspi->dma_dummy_tx)) {
		dev_err(dev, "unable to map dummy tx buffer\n");
		goto err_dummy_tx;
	}

	mspi->dma_dummy_rx = dma_map_single(dev, mpc8xxx_dummy_rx, SPI_MRBLR,
					    DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, mspi->dma_dummy_rx)) {
		dev_err(dev, "unable to map dummy rx buffer\n");
		goto err_dummy_rx;
	}

	mspi->pram = cpm_muram_addr(pram_ofs);

	mspi->tx_bd = cpm_muram_addr(bds_ofs);
	mspi->rx_bd = cpm_muram_addr(bds_ofs + sizeof(*mspi->tx_bd));

	/* Initialize parameter ram. */
	out_be16(&mspi->pram->tbase, cpm_muram_offset(mspi->tx_bd));
	out_be16(&mspi->pram->rbase, cpm_muram_offset(mspi->rx_bd));
	out_8(&mspi->pram->tfcr, CPMFCR_EB | CPMFCR_GBL);
	out_8(&mspi->pram->rfcr, CPMFCR_EB | CPMFCR_GBL);
	out_be16(&mspi->pram->mrblr, SPI_MRBLR);
	out_be32(&mspi->pram->rstate, 0);
	out_be32(&mspi->pram->rdp, 0);
	out_be16(&mspi->pram->rbptr, 0);
	out_be16(&mspi->pram->rbc, 0);
	out_be32(&mspi->pram->rxtmp, 0);
	out_be32(&mspi->pram->tstate, 0);
	out_be32(&mspi->pram->tdp, 0);
	out_be16(&mspi->pram->tbptr, 0);
	out_be16(&mspi->pram->tbc, 0);
	out_be32(&mspi->pram->txtmp, 0);

	return 0;

err_dummy_rx:
	dma_unmap_single(dev, mspi->dma_dummy_tx, PAGE_SIZE, DMA_TO_DEVICE);
err_dummy_tx:
	cpm_muram_free(bds_ofs);
err_bds:
	cpm_muram_free(pram_ofs);
err_pram:
	mpc8xxx_spi_free_dummy_rx();
	return -ENOMEM;
}

static void mpc8xxx_spi_cpm_free(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;

	dma_unmap_single(dev, mspi->dma_dummy_rx, SPI_MRBLR, DMA_FROM_DEVICE);
	dma_unmap_single(dev, mspi->dma_dummy_tx, PAGE_SIZE, DMA_TO_DEVICE);
	cpm_muram_free(cpm_muram_offset(mspi->tx_bd));
	cpm_muram_free(cpm_muram_offset(mspi->pram));
	mpc8xxx_spi_free_dummy_rx();
}

static const char *mpc8xxx_spi_strmode(unsigned int flags)
{
	if (flags & SPI_QE_CPU_MODE) {
		return "QE CPU";
	} else if (flags & SPI_CPM_MODE) {
		if (flags & SPI_QE)
			return "QE";
		else if (flags & SPI_CPM2)
			return "CPM2";
		else
			return "CPM1";
	}
	return "CPU";
}

static struct spi_master * __devinit
mpc8xxx_spi_probe(struct device *dev, struct resource *mem, unsigned int irq)
{
	struct fsl_spi_platform_data *pdata = dev->platform_data;
	struct spi_master *master;
	struct mpc8xxx_spi *mpc8xxx_spi;
	u32 regval;
	int ret = 0;

	master = spi_alloc_master(dev, sizeof(struct mpc8xxx_spi));
	if (master == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	dev_set_drvdata(dev, master);

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH
			| SPI_LSB_FIRST | SPI_LOOP;

	master->setup = mpc8xxx_spi_setup;
	master->transfer = mpc8xxx_spi_transfer;
	master->cleanup = mpc8xxx_spi_cleanup;

	mpc8xxx_spi = spi_master_get_devdata(master);
	mpc8xxx_spi->dev = dev;
	mpc8xxx_spi->get_rx = mpc8xxx_spi_rx_buf_u8;
	mpc8xxx_spi->get_tx = mpc8xxx_spi_tx_buf_u8;
	mpc8xxx_spi->flags = pdata->flags;
	mpc8xxx_spi->spibrg = pdata->sysclk;

	ret = mpc8xxx_spi_cpm_init(mpc8xxx_spi);
	if (ret)
		goto err_cpm_init;

	mpc8xxx_spi->rx_shift = 0;
	mpc8xxx_spi->tx_shift = 0;
	if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE) {
		mpc8xxx_spi->rx_shift = 16;
		mpc8xxx_spi->tx_shift = 24;
	}

	init_completion(&mpc8xxx_spi->done);

	mpc8xxx_spi->base = ioremap(mem->start, resource_size(mem));
	if (mpc8xxx_spi->base == NULL) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	mpc8xxx_spi->irq = irq;

	/* Register for SPI Interrupt */
	ret = request_irq(mpc8xxx_spi->irq, mpc8xxx_spi_irq,
			  0, "mpc8xxx_spi", mpc8xxx_spi);

	if (ret != 0)
		goto unmap_io;

	master->bus_num = pdata->bus_num;
	master->num_chipselect = pdata->max_chipselect;

	/* SPI controller initializations */
	mpc8xxx_spi_write_reg(&mpc8xxx_spi->base->mode, 0);
	mpc8xxx_spi_write_reg(&mpc8xxx_spi->base->mask, 0);
	mpc8xxx_spi_write_reg(&mpc8xxx_spi->base->command, 0);
	mpc8xxx_spi_write_reg(&mpc8xxx_spi->base->event, 0xffffffff);

	/* Enable SPI interface */
	regval = pdata->initial_spmode | SPMODE_INIT_VAL | SPMODE_ENABLE;
	if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE)
		regval |= SPMODE_OP;

	mpc8xxx_spi_write_reg(&mpc8xxx_spi->base->mode, regval);
	spin_lock_init(&mpc8xxx_spi->lock);
	init_completion(&mpc8xxx_spi->done);
	INIT_WORK(&mpc8xxx_spi->work, mpc8xxx_spi_work);
	INIT_LIST_HEAD(&mpc8xxx_spi->queue);

	mpc8xxx_spi->workqueue = create_singlethread_workqueue(
		dev_name(master->dev.parent));
	if (mpc8xxx_spi->workqueue == NULL) {
		ret = -EBUSY;
		goto free_irq;
	}

	ret = spi_register_master(master);
	if (ret < 0)
		goto unreg_master;

	dev_info(dev, "at 0x%p (irq = %d), %s mode\n", mpc8xxx_spi->base,
		 mpc8xxx_spi->irq, mpc8xxx_spi_strmode(mpc8xxx_spi->flags));

	return master;

unreg_master:
	destroy_workqueue(mpc8xxx_spi->workqueue);
free_irq:
	free_irq(mpc8xxx_spi->irq, mpc8xxx_spi);
unmap_io:
	iounmap(mpc8xxx_spi->base);
err_ioremap:
	mpc8xxx_spi_cpm_free(mpc8xxx_spi);
err_cpm_init:
	spi_master_put(master);
err:
	return ERR_PTR(ret);
}

static int __devexit mpc8xxx_spi_remove(struct device *dev)
{
	struct mpc8xxx_spi *mpc8xxx_spi;
	struct spi_master *master;

	master = dev_get_drvdata(dev);
	mpc8xxx_spi = spi_master_get_devdata(master);

	flush_workqueue(mpc8xxx_spi->workqueue);
	destroy_workqueue(mpc8xxx_spi->workqueue);
	spi_unregister_master(master);

	free_irq(mpc8xxx_spi->irq, mpc8xxx_spi);
	iounmap(mpc8xxx_spi->base);
	mpc8xxx_spi_cpm_free(mpc8xxx_spi);

	return 0;
}

struct mpc8xxx_spi_probe_info {
	struct fsl_spi_platform_data pdata;
	int *gpios;
	bool *alow_flags;
};

static struct mpc8xxx_spi_probe_info *
to_of_pinfo(struct fsl_spi_platform_data *pdata)
{
	return container_of(pdata, struct mpc8xxx_spi_probe_info, pdata);
}

static void mpc8xxx_spi_cs_control(struct spi_device *spi, bool on)
{
	struct device *dev = spi->dev.parent;
	struct mpc8xxx_spi_probe_info *pinfo = to_of_pinfo(dev->platform_data);
	u16 cs = spi->chip_select;
	int gpio = pinfo->gpios[cs];
	bool alow = pinfo->alow_flags[cs];

	gpio_set_value(gpio, on ^ alow);
}

static int of_mpc8xxx_spi_get_chipselects(struct device *dev)
{
	struct device_node *np = dev_archdata_get_node(&dev->archdata);
	struct fsl_spi_platform_data *pdata = dev->platform_data;
	struct mpc8xxx_spi_probe_info *pinfo = to_of_pinfo(pdata);
	unsigned int ngpios;
	int i = 0;
	int ret;

	ngpios = of_gpio_count(np);
	if (!ngpios) {
		/*
		 * SPI w/o chip-select line. One SPI device is still permitted
		 * though.
		 */
		pdata->max_chipselect = 1;
		return 0;
	}

	pinfo->gpios = kmalloc(ngpios * sizeof(*pinfo->gpios), GFP_KERNEL);
	if (!pinfo->gpios)
		return -ENOMEM;
	memset(pinfo->gpios, -1, ngpios * sizeof(*pinfo->gpios));

	pinfo->alow_flags = kzalloc(ngpios * sizeof(*pinfo->alow_flags),
				    GFP_KERNEL);
	if (!pinfo->alow_flags) {
		ret = -ENOMEM;
		goto err_alloc_flags;
	}

	for (; i < ngpios; i++) {
		int gpio;
		enum of_gpio_flags flags;

		gpio = of_get_gpio_flags(np, i, &flags);
		if (!gpio_is_valid(gpio)) {
			dev_err(dev, "invalid gpio #%d: %d\n", i, gpio);
			ret = gpio;
			goto err_loop;
		}

		ret = gpio_request(gpio, dev_name(dev));
		if (ret) {
			dev_err(dev, "can't request gpio #%d: %d\n", i, ret);
			goto err_loop;
		}

		pinfo->gpios[i] = gpio;
		pinfo->alow_flags[i] = flags & OF_GPIO_ACTIVE_LOW;

		ret = gpio_direction_output(pinfo->gpios[i],
					    pinfo->alow_flags[i]);
		if (ret) {
			dev_err(dev, "can't set output direction for gpio "
				"#%d: %d\n", i, ret);
			goto err_loop;
		}
	}

	pdata->max_chipselect = ngpios;
	pdata->cs_control = mpc8xxx_spi_cs_control;

	return 0;

err_loop:
	while (i >= 0) {
		if (gpio_is_valid(pinfo->gpios[i]))
			gpio_free(pinfo->gpios[i]);
		i--;
	}

	kfree(pinfo->alow_flags);
	pinfo->alow_flags = NULL;
err_alloc_flags:
	kfree(pinfo->gpios);
	pinfo->gpios = NULL;
	return ret;
}

static int of_mpc8xxx_spi_free_chipselects(struct device *dev)
{
	struct fsl_spi_platform_data *pdata = dev->platform_data;
	struct mpc8xxx_spi_probe_info *pinfo = to_of_pinfo(pdata);
	int i;

	if (!pinfo->gpios)
		return 0;

	for (i = 0; i < pdata->max_chipselect; i++) {
		if (gpio_is_valid(pinfo->gpios[i]))
			gpio_free(pinfo->gpios[i]);
	}

	kfree(pinfo->gpios);
	kfree(pinfo->alow_flags);
	return 0;
}

static int __devinit of_mpc8xxx_spi_probe(struct of_device *ofdev,
					  const struct of_device_id *ofid)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->node;
	struct mpc8xxx_spi_probe_info *pinfo;
	struct fsl_spi_platform_data *pdata;
	struct spi_master *master;
	struct resource mem;
	struct resource irq;
	const void *prop;
	int ret = -ENOMEM;

	pinfo = kzalloc(sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pdata = &pinfo->pdata;
	dev->platform_data = pdata;

	/* Allocate bus num dynamically. */
	pdata->bus_num = -1;

	/* SPI controller is either clocked from QE or SoC clock. */
	pdata->sysclk = get_brgfreq();
	if (pdata->sysclk == -1) {
		pdata->sysclk = fsl_get_sys_freq();
		if (pdata->sysclk == -1) {
			ret = -ENODEV;
			goto err_clk;
		}
	}

	prop = of_get_property(np, "mode", NULL);
	if (prop && !strcmp(prop, "cpu-qe"))
		pdata->flags = SPI_QE_CPU_MODE;
	else if (prop && !strcmp(prop, "qe"))
		pdata->flags = SPI_CPM_MODE | SPI_QE;
	else if (of_device_is_compatible(np, "fsl,cpm2-spi"))
		pdata->flags = SPI_CPM_MODE | SPI_CPM2;
	else if (of_device_is_compatible(np, "fsl,cpm1-spi"))
		pdata->flags = SPI_CPM_MODE | SPI_CPM1;

	ret = of_mpc8xxx_spi_get_chipselects(dev);
	if (ret)
		goto err;

	ret = of_address_to_resource(np, 0, &mem);
	if (ret)
		goto err;

	ret = of_irq_to_resource(np, 0, &irq);
	if (!ret) {
		ret = -EINVAL;
		goto err;
	}

	master = mpc8xxx_spi_probe(dev, &mem, irq.start);
	if (IS_ERR(master)) {
		ret = PTR_ERR(master);
		goto err;
	}

	of_register_spi_devices(master, np);

	return 0;

err:
	of_mpc8xxx_spi_free_chipselects(dev);
err_clk:
	kfree(pinfo);
	return ret;
}

static int __devexit of_mpc8xxx_spi_remove(struct of_device *ofdev)
{
	int ret;

	ret = mpc8xxx_spi_remove(&ofdev->dev);
	if (ret)
		return ret;
	of_mpc8xxx_spi_free_chipselects(&ofdev->dev);
	return 0;
}

static const struct of_device_id of_mpc8xxx_spi_match[] = {
	{ .compatible = "fsl,spi" },
	{},
};
MODULE_DEVICE_TABLE(of, of_mpc8xxx_spi_match);

static struct of_platform_driver of_mpc8xxx_spi_driver = {
	.name		= "mpc8xxx_spi",
	.match_table	= of_mpc8xxx_spi_match,
	.probe		= of_mpc8xxx_spi_probe,
	.remove		= __devexit_p(of_mpc8xxx_spi_remove),
};

#ifdef CONFIG_MPC832x_RDB
/*
 * 				XXX XXX XXX
 * This is "legacy" platform driver, was used by the MPC8323E-RDB boards
 * only. The driver should go away soon, since newer MPC8323E-RDB's device
 * tree can work with OpenFirmware driver. But for now we support old trees
 * as well.
 */
static int __devinit plat_mpc8xxx_spi_probe(struct platform_device *pdev)
{
	struct resource *mem;
	int irq;
	struct spi_master *master;

	if (!pdev->dev.platform_data)
		return -EINVAL;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -EINVAL;

	master = mpc8xxx_spi_probe(&pdev->dev, mem, irq);
	if (IS_ERR(master))
		return PTR_ERR(master);
	return 0;
}

static int __devexit plat_mpc8xxx_spi_remove(struct platform_device *pdev)
{
	return mpc8xxx_spi_remove(&pdev->dev);
}

MODULE_ALIAS("platform:mpc8xxx_spi");
static struct platform_driver mpc8xxx_spi_driver = {
	.probe = plat_mpc8xxx_spi_probe,
	.remove = __devexit_p(plat_mpc8xxx_spi_remove),
	.driver = {
		.name = "mpc8xxx_spi",
		.owner = THIS_MODULE,
	},
};

static bool legacy_driver_failed;

static void __init legacy_driver_register(void)
{
	legacy_driver_failed = platform_driver_register(&mpc8xxx_spi_driver);
}

static void __exit legacy_driver_unregister(void)
{
	if (legacy_driver_failed)
		return;
	platform_driver_unregister(&mpc8xxx_spi_driver);
}
#else
static void __init legacy_driver_register(void) {}
static void __exit legacy_driver_unregister(void) {}
#endif /* CONFIG_MPC832x_RDB */

static int __init mpc8xxx_spi_init(void)
{
	legacy_driver_register();
	return of_register_platform_driver(&of_mpc8xxx_spi_driver);
}

static void __exit mpc8xxx_spi_exit(void)
{
	of_unregister_platform_driver(&of_mpc8xxx_spi_driver);
	legacy_driver_unregister();
}

module_init(mpc8xxx_spi_init);
module_exit(mpc8xxx_spi_exit);

MODULE_AUTHOR("Kumar Gala");
MODULE_DESCRIPTION("Simple MPC8xxx SPI Driver");
MODULE_LICENSE("GPL");
