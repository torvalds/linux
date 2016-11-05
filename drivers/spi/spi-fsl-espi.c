/*
 * Freescale eSPI controller driver.
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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

#include "spi-fsl-lib.h"

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

/* Default mode/csmode for eSPI controller */
#define SPMODE_INIT_VAL (SPMODE_TXTHR(4) | SPMODE_RXTHR(3))
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

static inline u32 fsl_espi_read_reg(struct mpc8xxx_spi *mspi, int offset)
{
	return ioread32be(mspi->reg_base + offset);
}

static inline u8 fsl_espi_read_reg8(struct mpc8xxx_spi *mspi, int offset)
{
	return ioread8(mspi->reg_base + offset);
}

static inline void fsl_espi_write_reg(struct mpc8xxx_spi *mspi, int offset,
				      u32 val)
{
	iowrite32be(val, mspi->reg_base + offset);
}

static inline void fsl_espi_write_reg8(struct mpc8xxx_spi *mspi, int offset,
				       u8 val)
{
	iowrite8(val, mspi->reg_base + offset);
}

static void fsl_espi_copy_to_buf(struct spi_message *m,
				 struct mpc8xxx_spi *mspi)
{
	struct spi_transfer *t;
	u8 *buf = mspi->local_buf;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->tx_buf)
			memcpy(buf, t->tx_buf, t->len);
		else
			memset(buf, 0, t->len);
		buf += t->len;
	}
}

static void fsl_espi_copy_from_buf(struct spi_message *m,
				   struct mpc8xxx_spi *mspi)
{
	struct spi_transfer *t;
	u8 *buf = mspi->local_buf;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->rx_buf)
			memcpy(t->rx_buf, buf, t->len);
		buf += t->len;
	}
}

static int fsl_espi_check_message(struct spi_message *m)
{
	struct mpc8xxx_spi *mspi = spi_master_get_devdata(m->spi->master);
	struct spi_transfer *t, *first;

	if (m->frame_length > SPCOM_TRANLEN_MAX) {
		dev_err(mspi->dev, "message too long, size is %u bytes\n",
			m->frame_length);
		return -EMSGSIZE;
	}

	first = list_first_entry(&m->transfers, struct spi_transfer,
				 transfer_list);
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (first->bits_per_word != t->bits_per_word ||
		    first->speed_hz != t->speed_hz) {
			dev_err(mspi->dev, "bits_per_word/speed_hz should be the same for all transfers\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void fsl_espi_change_mode(struct spi_device *spi)
{
	struct mpc8xxx_spi *mspi = spi_master_get_devdata(spi->master);
	struct spi_mpc8xxx_cs *cs = spi->controller_state;
	u32 tmp;
	unsigned long flags;

	/* Turn off IRQs locally to minimize time that SPI is disabled. */
	local_irq_save(flags);

	/* Turn off SPI unit prior changing mode */
	tmp = fsl_espi_read_reg(mspi, ESPI_SPMODE);
	fsl_espi_write_reg(mspi, ESPI_SPMODE, tmp & ~SPMODE_ENABLE);
	fsl_espi_write_reg(mspi, ESPI_SPMODEx(spi->chip_select),
			      cs->hw_mode);
	fsl_espi_write_reg(mspi, ESPI_SPMODE, tmp);

	local_irq_restore(flags);
}

static u32 fsl_espi_tx_buf_lsb(struct mpc8xxx_spi *mpc8xxx_spi)
{
	u32 data;
	u16 data_h;
	u16 data_l;
	const u32 *tx = mpc8xxx_spi->tx;

	if (!tx)
		return 0;

	data = *tx++ << mpc8xxx_spi->tx_shift;
	data_l = data & 0xffff;
	data_h = (data >> 16) & 0xffff;
	swab16s(&data_l);
	swab16s(&data_h);
	data = data_h | data_l;

	mpc8xxx_spi->tx = tx;
	return data;
}

static void fsl_espi_setup_transfer(struct spi_device *spi,
					struct spi_transfer *t)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(spi->master);
	int bits_per_word = t ? t->bits_per_word : spi->bits_per_word;
	u32 hz = t ? t->speed_hz : spi->max_speed_hz;
	u8 pm;
	struct spi_mpc8xxx_cs *cs = spi->controller_state;

	cs->rx_shift = 0;
	cs->tx_shift = 0;
	cs->get_rx = mpc8xxx_spi_rx_buf_u32;
	cs->get_tx = mpc8xxx_spi_tx_buf_u32;
	if (bits_per_word <= 8) {
		cs->rx_shift = 8 - bits_per_word;
	} else {
		cs->rx_shift = 16 - bits_per_word;
		if (spi->mode & SPI_LSB_FIRST)
			cs->get_tx = fsl_espi_tx_buf_lsb;
	}

	mpc8xxx_spi->rx_shift = cs->rx_shift;
	mpc8xxx_spi->tx_shift = cs->tx_shift;
	mpc8xxx_spi->get_rx = cs->get_rx;
	mpc8xxx_spi->get_tx = cs->get_tx;

	/* mask out bits we are going to set */
	cs->hw_mode &= ~(CSMODE_LEN(0xF) | CSMODE_DIV16 | CSMODE_PM(0xF));

	cs->hw_mode |= CSMODE_LEN(bits_per_word - 1);

	if ((mpc8xxx_spi->spibrg / hz) > 64) {
		cs->hw_mode |= CSMODE_DIV16;
		pm = DIV_ROUND_UP(mpc8xxx_spi->spibrg, hz * 16 * 4);

		WARN_ONCE(pm > 33, "%s: Requested speed is too low: %d Hz. "
			  "Will use %d Hz instead.\n", dev_name(&spi->dev),
				hz, mpc8xxx_spi->spibrg / (4 * 16 * (32 + 1)));
		if (pm > 33)
			pm = 33;
	} else {
		pm = DIV_ROUND_UP(mpc8xxx_spi->spibrg, hz * 4);
	}
	if (pm)
		pm--;
	if (pm < 2)
		pm = 2;

	cs->hw_mode |= CSMODE_PM(pm);

	fsl_espi_change_mode(spi);
}

static int fsl_espi_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(spi->master);
	u32 word;
	int ret;

	mpc8xxx_spi->len = t->len;
	mpc8xxx_spi->count = roundup(t->len, 4) / 4;

	mpc8xxx_spi->tx = t->tx_buf;
	mpc8xxx_spi->rx = t->rx_buf;

	reinit_completion(&mpc8xxx_spi->done);

	/* Set SPCOM[CS] and SPCOM[TRANLEN] field */
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPCOM,
		(SPCOM_CS(spi->chip_select) | SPCOM_TRANLEN(t->len - 1)));

	/* enable rx ints */
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPIM, SPIM_RNE);

	/* transmit word */
	word = mpc8xxx_spi->get_tx(mpc8xxx_spi);
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPITF, word);

	/* Won't hang up forever, SPI bus sometimes got lost interrupts... */
	ret = wait_for_completion_timeout(&mpc8xxx_spi->done, 2 * HZ);
	if (ret == 0)
		dev_err(mpc8xxx_spi->dev,
			"Transaction hanging up (left %d bytes)\n",
			mpc8xxx_spi->count);

	/* disable rx ints */
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPIM, 0);

	return mpc8xxx_spi->count > 0 ? -EMSGSIZE : 0;
}

static int fsl_espi_trans(struct spi_message *m, struct spi_transfer *trans)
{
	struct mpc8xxx_spi *mspi = spi_master_get_devdata(m->spi->master);
	struct spi_device *spi = m->spi;
	int ret;

	fsl_espi_copy_to_buf(m, mspi);
	fsl_espi_setup_transfer(spi, trans);

	ret = fsl_espi_bufs(spi, trans);

	if (trans->delay_usecs)
		udelay(trans->delay_usecs);

	fsl_espi_setup_transfer(spi, NULL);

	if (!ret)
		fsl_espi_copy_from_buf(m, mspi);

	return ret;
}

static int fsl_espi_do_one_msg(struct spi_master *master,
			       struct spi_message *m)
{
	struct mpc8xxx_spi *mspi = spi_master_get_devdata(m->spi->master);
	unsigned int delay_usecs = 0;
	struct spi_transfer *t, trans = {};
	int ret;

	ret = fsl_espi_check_message(m);
	if (ret)
		goto out;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->delay_usecs > delay_usecs)
			delay_usecs = t->delay_usecs;
	}

	t = list_first_entry(&m->transfers, struct spi_transfer,
			     transfer_list);

	trans.len = m->frame_length;
	trans.speed_hz = t->speed_hz;
	trans.bits_per_word = t->bits_per_word;
	trans.delay_usecs = delay_usecs;
	trans.tx_buf = mspi->local_buf;
	trans.rx_buf = mspi->local_buf;

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
	struct mpc8xxx_spi *mpc8xxx_spi;
	u32 loop_mode;
	struct spi_mpc8xxx_cs *cs = spi_get_ctldata(spi);

	if (!spi->max_speed_hz)
		return -EINVAL;

	if (!cs) {
		cs = kzalloc(sizeof(*cs), GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi_set_ctldata(spi, cs);
	}

	mpc8xxx_spi = spi_master_get_devdata(spi->master);

	pm_runtime_get_sync(mpc8xxx_spi->dev);

	cs->hw_mode = fsl_espi_read_reg(mpc8xxx_spi,
					   ESPI_SPMODEx(spi->chip_select));
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
	loop_mode = fsl_espi_read_reg(mpc8xxx_spi, ESPI_SPMODE);
	loop_mode &= ~SPMODE_LOOP;
	if (spi->mode & SPI_LOOP)
		loop_mode |= SPMODE_LOOP;
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODE, loop_mode);

	fsl_espi_setup_transfer(spi, NULL);

	pm_runtime_mark_last_busy(mpc8xxx_spi->dev);
	pm_runtime_put_autosuspend(mpc8xxx_spi->dev);

	return 0;
}

static void fsl_espi_cleanup(struct spi_device *spi)
{
	struct spi_mpc8xxx_cs *cs = spi_get_ctldata(spi);

	kfree(cs);
	spi_set_ctldata(spi, NULL);
}

static void fsl_espi_cpu_irq(struct mpc8xxx_spi *mspi, u32 events)
{
	/* We need handle RX first */
	if (events & SPIE_RNE) {
		u32 rx_data, tmp;
		u8 rx_data_8;
		int rx_nr_bytes = 4;
		int ret;

		/* Spin until RX is done */
		if (SPIE_RXCNT(events) < min(4, mspi->len)) {
			ret = spin_event_timeout(
				!(SPIE_RXCNT(events =
				fsl_espi_read_reg(mspi, ESPI_SPIE)) <
						min(4, mspi->len)),
						10000, 0); /* 10 msec */
			if (!ret)
				dev_err(mspi->dev,
					 "tired waiting for SPIE_RXCNT\n");
		}

		if (mspi->len >= 4) {
			rx_data = fsl_espi_read_reg(mspi, ESPI_SPIRF);
		} else if (mspi->len <= 0) {
			dev_err(mspi->dev,
				"unexpected RX(SPIE_RNE) interrupt occurred,\n"
				"(local rxlen %d bytes, reg rxlen %d bytes)\n",
				min(4, mspi->len), SPIE_RXCNT(events));
			rx_nr_bytes = 0;
		} else {
			rx_nr_bytes = mspi->len;
			tmp = mspi->len;
			rx_data = 0;
			while (tmp--) {
				rx_data_8 = fsl_espi_read_reg8(mspi,
							       ESPI_SPIRF);
				rx_data |= (rx_data_8 << (tmp * 8));
			}

			rx_data <<= (4 - mspi->len) * 8;
		}

		mspi->len -= rx_nr_bytes;

		if (rx_nr_bytes && mspi->rx)
			mspi->get_rx(rx_data, mspi);
	}

	if (!(events & SPIE_TNF)) {
		int ret;

		/* spin until TX is done */
		ret = spin_event_timeout(((events = fsl_espi_read_reg(
				mspi, ESPI_SPIE)) & SPIE_TNF), 1000, 0);
		if (!ret) {
			dev_err(mspi->dev, "tired waiting for SPIE_TNF\n");
			complete(&mspi->done);
			return;
		}
	}

	mspi->count -= 1;
	if (mspi->count) {
		u32 word = mspi->get_tx(mspi);

		fsl_espi_write_reg(mspi, ESPI_SPITF, word);
	} else {
		complete(&mspi->done);
	}
}

static irqreturn_t fsl_espi_irq(s32 irq, void *context_data)
{
	struct mpc8xxx_spi *mspi = context_data;
	u32 events;

	/* Get interrupt events(tx/rx) */
	events = fsl_espi_read_reg(mspi, ESPI_SPIE);
	if (!events)
		return IRQ_NONE;

	dev_vdbg(mspi->dev, "%s: events %x\n", __func__, events);

	fsl_espi_cpu_irq(mspi, events);

	/* Clear the events */
	fsl_espi_write_reg(mspi, ESPI_SPIE, events);

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int fsl_espi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(master);
	u32 regval;

	regval = fsl_espi_read_reg(mpc8xxx_spi, ESPI_SPMODE);
	regval &= ~SPMODE_ENABLE;
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODE, regval);

	return 0;
}

static int fsl_espi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(master);
	u32 regval;

	regval = fsl_espi_read_reg(mpc8xxx_spi, ESPI_SPMODE);
	regval |= SPMODE_ENABLE;
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODE, regval);

	return 0;
}
#endif

static size_t fsl_espi_max_message_size(struct spi_device *spi)
{
	return SPCOM_TRANLEN_MAX;
}

static int fsl_espi_probe(struct device *dev, struct resource *mem,
			  unsigned int irq)
{
	struct fsl_spi_platform_data *pdata = dev_get_platdata(dev);
	struct spi_master *master;
	struct mpc8xxx_spi *mpc8xxx_spi;
	struct device_node *nc;
	const __be32 *prop;
	u32 regval, csmode;
	int i, len, ret;

	master = spi_alloc_master(dev, sizeof(struct mpc8xxx_spi));
	if (!master)
		return -ENOMEM;

	dev_set_drvdata(dev, master);

	mpc8xxx_spi_probe(dev, mem, irq);

	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 16);
	master->setup = fsl_espi_setup;
	master->cleanup = fsl_espi_cleanup;
	master->transfer_one_message = fsl_espi_do_one_msg;
	master->auto_runtime_pm = true;
	master->max_message_size = fsl_espi_max_message_size;

	mpc8xxx_spi = spi_master_get_devdata(master);

	mpc8xxx_spi->local_buf =
		devm_kmalloc(dev, SPCOM_TRANLEN_MAX, GFP_KERNEL);
	if (!mpc8xxx_spi->local_buf) {
		ret = -ENOMEM;
		goto err_probe;
	}

	mpc8xxx_spi->reg_base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(mpc8xxx_spi->reg_base)) {
		ret = PTR_ERR(mpc8xxx_spi->reg_base);
		goto err_probe;
	}

	/* Register for SPI Interrupt */
	ret = devm_request_irq(dev, mpc8xxx_spi->irq, fsl_espi_irq,
			  0, "fsl_espi", mpc8xxx_spi);
	if (ret)
		goto err_probe;

	if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE) {
		mpc8xxx_spi->rx_shift = 16;
		mpc8xxx_spi->tx_shift = 24;
	}

	/* SPI controller initializations */
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODE, 0);
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPIM, 0);
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPCOM, 0);
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPIE, 0xffffffff);

	/* Init eSPI CS mode register */
	for_each_available_child_of_node(master->dev.of_node, nc) {
		/* get chip select */
		prop = of_get_property(nc, "reg", &len);
		if (!prop || len < sizeof(*prop))
			continue;
		i = be32_to_cpup(prop);
		if (i < 0 || i >= pdata->max_chipselect)
			continue;

		csmode = CSMODE_INIT_VAL;
		/* check if CSBEF is set in device tree */
		prop = of_get_property(nc, "fsl,csbef", &len);
		if (prop && len >= sizeof(*prop)) {
			csmode &= ~(CSMODE_BEF(0xf));
			csmode |= CSMODE_BEF(be32_to_cpup(prop));
		}
		/* check if CSAFT is set in device tree */
		prop = of_get_property(nc, "fsl,csaft", &len);
		if (prop && len >= sizeof(*prop)) {
			csmode &= ~(CSMODE_AFT(0xf));
			csmode |= CSMODE_AFT(be32_to_cpup(prop));
		}
		fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODEx(i), csmode);

		dev_info(dev, "cs=%d, init_csmode=0x%x\n", i, csmode);
	}

	/* Enable SPI interface */
	regval = pdata->initial_spmode | SPMODE_INIT_VAL | SPMODE_ENABLE;

	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODE, regval);

	pm_runtime_set_autosuspend_delay(dev, AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	ret = devm_spi_register_master(dev, master);
	if (ret < 0)
		goto err_pm;

	dev_info(dev, "at 0x%p (irq = %d)\n", mpc8xxx_spi->reg_base,
		 mpc8xxx_spi->irq);

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
	struct fsl_spi_platform_data *pdata = dev_get_platdata(dev);
	const u32 *prop;
	int len;

	prop = of_get_property(np, "fsl,espi-num-chipselects", &len);
	if (!prop || len < sizeof(*prop)) {
		dev_err(dev, "No 'fsl,espi-num-chipselects' property\n");
		return -EINVAL;
	}

	pdata->max_chipselect = *prop;
	pdata->cs_control = NULL;

	return 0;
}

static int of_fsl_espi_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct resource mem;
	unsigned int irq;
	int ret;

	ret = of_mpc8xxx_spi_probe(ofdev);
	if (ret)
		return ret;

	ret = of_fsl_espi_get_chipselects(dev);
	if (ret)
		return ret;

	ret = of_address_to_resource(np, 0, &mem);
	if (ret)
		return ret;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return -EINVAL;

	return fsl_espi_probe(dev, &mem, irq);
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
	if (ret) {
		dev_warn(dev, "cannot suspend master\n");
		return ret;
	}

	ret = pm_runtime_force_suspend(dev);
	if (ret < 0)
		return ret;

	return 0;
}

static int of_fsl_espi_resume(struct device *dev)
{
	struct fsl_spi_platform_data *pdata = dev_get_platdata(dev);
	struct spi_master *master = dev_get_drvdata(dev);
	struct mpc8xxx_spi *mpc8xxx_spi;
	u32 regval;
	int i, ret;

	mpc8xxx_spi = spi_master_get_devdata(master);

	/* SPI controller initializations */
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODE, 0);
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPIM, 0);
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPCOM, 0);
	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPIE, 0xffffffff);

	/* Init eSPI CS mode register */
	for (i = 0; i < pdata->max_chipselect; i++)
		fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODEx(i),
				      CSMODE_INIT_VAL);

	/* Enable SPI interface */
	regval = pdata->initial_spmode | SPMODE_INIT_VAL | SPMODE_ENABLE;

	fsl_espi_write_reg(mpc8xxx_spi, ESPI_SPMODE, regval);

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
