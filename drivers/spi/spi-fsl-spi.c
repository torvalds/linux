// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale SPI controller driver.
 *
 * Maintainer: Kumar Gala
 *
 * Copyright (C) 2006 Polycom, Inc.
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * CPM SPI and QE buffer descriptors mode support:
 * Copyright (c) 2009  MontaVista Software, Inc.
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * GRLIB support:
 * Copyright (c) 2012 Aeroflex Gaisler AB.
 * Author: Andreas Larsson <andreas@gaisler.com>
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fsl_devices.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/types.h>

#ifdef CONFIG_FSL_SOC
#include <sysdev/fsl_soc.h>
#endif

/* Specific to the MPC8306/MPC8309 */
#define IMMR_SPI_CS_OFFSET 0x14c
#define SPI_BOOT_SEL_BIT   0x80000000

#include "spi-fsl-lib.h"
#include "spi-fsl-cpm.h"
#include "spi-fsl-spi.h"

#define TYPE_FSL	0
#define TYPE_GRLIB	1

struct fsl_spi_match_data {
	int type;
};

static struct fsl_spi_match_data of_fsl_spi_fsl_config = {
	.type = TYPE_FSL,
};

static struct fsl_spi_match_data of_fsl_spi_grlib_config = {
	.type = TYPE_GRLIB,
};

static const struct of_device_id of_fsl_spi_match[] = {
	{
		.compatible = "fsl,spi",
		.data = &of_fsl_spi_fsl_config,
	},
	{
		.compatible = "aeroflexgaisler,spictrl",
		.data = &of_fsl_spi_grlib_config,
	},
	{}
};
MODULE_DEVICE_TABLE(of, of_fsl_spi_match);

static int fsl_spi_get_type(struct device *dev)
{
	const struct of_device_id *match;

	if (dev->of_node) {
		match = of_match_node(of_fsl_spi_match, dev->of_node);
		if (match && match->data)
			return ((struct fsl_spi_match_data *)match->data)->type;
	}
	return TYPE_FSL;
}

static void fsl_spi_change_mode(struct spi_device *spi)
{
	struct mpc8xxx_spi *mspi = spi_master_get_devdata(spi->master);
	struct spi_mpc8xxx_cs *cs = spi->controller_state;
	struct fsl_spi_reg __iomem *reg_base = mspi->reg_base;
	__be32 __iomem *mode = &reg_base->mode;
	unsigned long flags;

	if (cs->hw_mode == mpc8xxx_spi_read_reg(mode))
		return;

	/* Turn off IRQs locally to minimize time that SPI is disabled. */
	local_irq_save(flags);

	/* Turn off SPI unit prior changing mode */
	mpc8xxx_spi_write_reg(mode, cs->hw_mode & ~SPMODE_ENABLE);

	/* When in CPM mode, we need to reinit tx and rx. */
	if (mspi->flags & SPI_CPM_MODE) {
		fsl_spi_cpm_reinit_txrx(mspi);
	}
	mpc8xxx_spi_write_reg(mode, cs->hw_mode);
	local_irq_restore(flags);
}

static void fsl_spi_qe_cpu_set_shifts(u32 *rx_shift, u32 *tx_shift,
				      int bits_per_word, int msb_first)
{
	*rx_shift = 0;
	*tx_shift = 0;
	if (msb_first) {
		if (bits_per_word <= 8) {
			*rx_shift = 16;
			*tx_shift = 24;
		} else if (bits_per_word <= 16) {
			*rx_shift = 16;
			*tx_shift = 16;
		}
	} else {
		if (bits_per_word <= 8)
			*rx_shift = 8;
	}
}

static void fsl_spi_grlib_set_shifts(u32 *rx_shift, u32 *tx_shift,
				     int bits_per_word, int msb_first)
{
	*rx_shift = 0;
	*tx_shift = 0;
	if (bits_per_word <= 16) {
		if (msb_first) {
			*rx_shift = 16; /* LSB in bit 16 */
			*tx_shift = 32 - bits_per_word; /* MSB in bit 31 */
		} else {
			*rx_shift = 16 - bits_per_word; /* MSB in bit 15 */
		}
	}
}

static void mspi_apply_cpu_mode_quirks(struct spi_mpc8xxx_cs *cs,
				       struct spi_device *spi,
				       struct mpc8xxx_spi *mpc8xxx_spi,
				       int bits_per_word)
{
	cs->rx_shift = 0;
	cs->tx_shift = 0;
	if (bits_per_word <= 8) {
		cs->get_rx = mpc8xxx_spi_rx_buf_u8;
		cs->get_tx = mpc8xxx_spi_tx_buf_u8;
	} else if (bits_per_word <= 16) {
		cs->get_rx = mpc8xxx_spi_rx_buf_u16;
		cs->get_tx = mpc8xxx_spi_tx_buf_u16;
	} else if (bits_per_word <= 32) {
		cs->get_rx = mpc8xxx_spi_rx_buf_u32;
		cs->get_tx = mpc8xxx_spi_tx_buf_u32;
	}

	if (mpc8xxx_spi->set_shifts)
		mpc8xxx_spi->set_shifts(&cs->rx_shift, &cs->tx_shift,
					bits_per_word,
					!(spi->mode & SPI_LSB_FIRST));

	mpc8xxx_spi->rx_shift = cs->rx_shift;
	mpc8xxx_spi->tx_shift = cs->tx_shift;
	mpc8xxx_spi->get_rx = cs->get_rx;
	mpc8xxx_spi->get_tx = cs->get_tx;
}

static int fsl_spi_setup_transfer(struct spi_device *spi,
					struct spi_transfer *t)
{
	struct mpc8xxx_spi *mpc8xxx_spi;
	int bits_per_word = 0;
	u8 pm;
	u32 hz = 0;
	struct spi_mpc8xxx_cs	*cs = spi->controller_state;

	mpc8xxx_spi = spi_master_get_devdata(spi->master);

	if (t) {
		bits_per_word = t->bits_per_word;
		hz = t->speed_hz;
	}

	/* spi_transfer level calls that work per-word */
	if (!bits_per_word)
		bits_per_word = spi->bits_per_word;

	if (!hz)
		hz = spi->max_speed_hz;

	if (!(mpc8xxx_spi->flags & SPI_CPM_MODE))
		mspi_apply_cpu_mode_quirks(cs, spi, mpc8xxx_spi, bits_per_word);

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
		WARN_ONCE(pm > 16,
			  "%s: Requested speed is too low: %d Hz. Will use %d Hz instead.\n",
			  dev_name(&spi->dev), hz, mpc8xxx_spi->spibrg / 1024);
		if (pm > 16)
			pm = 16;
	} else {
		pm = (mpc8xxx_spi->spibrg - 1) / (hz * 4) + 1;
	}
	if (pm)
		pm--;

	cs->hw_mode |= SPMODE_PM(pm);

	fsl_spi_change_mode(spi);
	return 0;
}

static int fsl_spi_cpu_bufs(struct mpc8xxx_spi *mspi,
				struct spi_transfer *t, unsigned int len)
{
	u32 word;
	struct fsl_spi_reg __iomem *reg_base = mspi->reg_base;

	mspi->count = len;

	/* enable rx ints */
	mpc8xxx_spi_write_reg(&reg_base->mask, SPIM_NE);

	/* transmit word */
	word = mspi->get_tx(mspi);
	mpc8xxx_spi_write_reg(&reg_base->transmit, word);

	return 0;
}

static int fsl_spi_bufs(struct spi_device *spi, struct spi_transfer *t,
			    bool is_dma_mapped)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(spi->master);
	struct fsl_spi_reg __iomem *reg_base;
	unsigned int len = t->len;
	u8 bits_per_word;
	int ret;

	reg_base = mpc8xxx_spi->reg_base;
	bits_per_word = spi->bits_per_word;
	if (t->bits_per_word)
		bits_per_word = t->bits_per_word;

	if (bits_per_word > 8)
		len /= 2;
	if (bits_per_word > 16)
		len /= 2;

	mpc8xxx_spi->tx = t->tx_buf;
	mpc8xxx_spi->rx = t->rx_buf;

	reinit_completion(&mpc8xxx_spi->done);

	if (mpc8xxx_spi->flags & SPI_CPM_MODE)
		ret = fsl_spi_cpm_bufs(mpc8xxx_spi, t, is_dma_mapped);
	else
		ret = fsl_spi_cpu_bufs(mpc8xxx_spi, t, len);
	if (ret)
		return ret;

	wait_for_completion(&mpc8xxx_spi->done);

	/* disable rx ints */
	mpc8xxx_spi_write_reg(&reg_base->mask, 0);

	if (mpc8xxx_spi->flags & SPI_CPM_MODE)
		fsl_spi_cpm_bufs_complete(mpc8xxx_spi);

	return mpc8xxx_spi->count;
}

static int fsl_spi_prepare_message(struct spi_controller *ctlr,
				   struct spi_message *m)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_controller_get_devdata(ctlr);
	struct spi_transfer *t;
	struct spi_transfer *first;

	first = list_first_entry(&m->transfers, struct spi_transfer,
				 transfer_list);

	/*
	 * In CPU mode, optimize large byte transfers to use larger
	 * bits_per_word values to reduce number of interrupts taken.
	 *
	 * Some glitches can appear on the SPI clock when the mode changes.
	 * Check that there is no speed change during the transfer and set it up
	 * now to change the mode without having a chip-select asserted.
	 */
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->speed_hz != first->speed_hz) {
			dev_err(&m->spi->dev,
				"speed_hz cannot change during message.\n");
			return -EINVAL;
		}
		if (!(mpc8xxx_spi->flags & SPI_CPM_MODE)) {
			if (t->len < 256 || t->bits_per_word != 8)
				continue;
			if ((t->len & 3) == 0)
				t->bits_per_word = 32;
			else if ((t->len & 1) == 0)
				t->bits_per_word = 16;
		} else {
			/*
			 * CPM/QE uses Little Endian for words > 8
			 * so transform 16 and 32 bits words into 8 bits
			 * Unfortnatly that doesn't work for LSB so
			 * reject these for now
			 * Note: 32 bits word, LSB works iff
			 * tfcr/rfcr is set to CPMFCR_GBL
			 */
			if (m->spi->mode & SPI_LSB_FIRST && t->bits_per_word > 8)
				return -EINVAL;
			if (t->bits_per_word == 16 || t->bits_per_word == 32)
				t->bits_per_word = 8; /* pretend its 8 bits */
			if (t->bits_per_word == 8 && t->len >= 256 &&
			    (mpc8xxx_spi->flags & SPI_CPM1))
				t->bits_per_word = 16;
		}
	}
	return fsl_spi_setup_transfer(m->spi, first);
}

static int fsl_spi_transfer_one(struct spi_controller *controller,
				struct spi_device *spi,
				struct spi_transfer *t)
{
	int status;

	status = fsl_spi_setup_transfer(spi, t);
	if (status < 0)
		return status;
	if (t->len)
		status = fsl_spi_bufs(spi, t, !!t->tx_dma || !!t->rx_dma);
	if (status > 0)
		return -EMSGSIZE;

	return status;
}

static int fsl_spi_unprepare_message(struct spi_controller *controller,
				     struct spi_message *msg)
{
	return fsl_spi_setup_transfer(msg->spi, NULL);
}

static int fsl_spi_setup(struct spi_device *spi)
{
	struct mpc8xxx_spi *mpc8xxx_spi;
	struct fsl_spi_reg __iomem *reg_base;
	bool initial_setup = false;
	int retval;
	u32 hw_mode;
	struct spi_mpc8xxx_cs *cs = spi_get_ctldata(spi);

	if (!spi->max_speed_hz)
		return -EINVAL;

	if (!cs) {
		cs = kzalloc(sizeof(*cs), GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi_set_ctldata(spi, cs);
		initial_setup = true;
	}
	mpc8xxx_spi = spi_master_get_devdata(spi->master);

	reg_base = mpc8xxx_spi->reg_base;

	hw_mode = cs->hw_mode; /* Save original settings */
	cs->hw_mode = mpc8xxx_spi_read_reg(&reg_base->mode);
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

	retval = fsl_spi_setup_transfer(spi, NULL);
	if (retval < 0) {
		cs->hw_mode = hw_mode; /* Restore settings */
		if (initial_setup)
			kfree(cs);
		return retval;
	}

	return 0;
}

static void fsl_spi_cleanup(struct spi_device *spi)
{
	struct spi_mpc8xxx_cs *cs = spi_get_ctldata(spi);

	kfree(cs);
	spi_set_ctldata(spi, NULL);
}

static void fsl_spi_cpu_irq(struct mpc8xxx_spi *mspi, u32 events)
{
	struct fsl_spi_reg __iomem *reg_base = mspi->reg_base;

	/* We need handle RX first */
	if (events & SPIE_NE) {
		u32 rx_data = mpc8xxx_spi_read_reg(&reg_base->receive);

		if (mspi->rx)
			mspi->get_rx(rx_data, mspi);
	}

	if ((events & SPIE_NF) == 0)
		/* spin until TX is done */
		while (((events =
			mpc8xxx_spi_read_reg(&reg_base->event)) &
						SPIE_NF) == 0)
			cpu_relax();

	/* Clear the events */
	mpc8xxx_spi_write_reg(&reg_base->event, events);

	mspi->count -= 1;
	if (mspi->count) {
		u32 word = mspi->get_tx(mspi);

		mpc8xxx_spi_write_reg(&reg_base->transmit, word);
	} else {
		complete(&mspi->done);
	}
}

static irqreturn_t fsl_spi_irq(s32 irq, void *context_data)
{
	struct mpc8xxx_spi *mspi = context_data;
	irqreturn_t ret = IRQ_NONE;
	u32 events;
	struct fsl_spi_reg __iomem *reg_base = mspi->reg_base;

	/* Get interrupt events(tx/rx) */
	events = mpc8xxx_spi_read_reg(&reg_base->event);
	if (events)
		ret = IRQ_HANDLED;

	dev_dbg(mspi->dev, "%s: events %x\n", __func__, events);

	if (mspi->flags & SPI_CPM_MODE)
		fsl_spi_cpm_irq(mspi, events);
	else
		fsl_spi_cpu_irq(mspi, events);

	return ret;
}

static void fsl_spi_grlib_cs_control(struct spi_device *spi, bool on)
{
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(spi->master);
	struct fsl_spi_reg __iomem *reg_base = mpc8xxx_spi->reg_base;
	u32 slvsel;
	u16 cs = spi_get_chipselect(spi, 0);

	if (cs < mpc8xxx_spi->native_chipselects) {
		slvsel = mpc8xxx_spi_read_reg(&reg_base->slvsel);
		slvsel = on ? (slvsel | (1 << cs)) : (slvsel & ~(1 << cs));
		mpc8xxx_spi_write_reg(&reg_base->slvsel, slvsel);
	}
}

static void fsl_spi_grlib_probe(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(master);
	struct fsl_spi_reg __iomem *reg_base = mpc8xxx_spi->reg_base;
	int mbits;
	u32 capabilities;

	capabilities = mpc8xxx_spi_read_reg(&reg_base->cap);

	mpc8xxx_spi->set_shifts = fsl_spi_grlib_set_shifts;
	mbits = SPCAP_MAXWLEN(capabilities);
	if (mbits)
		mpc8xxx_spi->max_bits_per_word = mbits + 1;

	mpc8xxx_spi->native_chipselects = 0;
	if (SPCAP_SSEN(capabilities)) {
		mpc8xxx_spi->native_chipselects = SPCAP_SSSZ(capabilities);
		mpc8xxx_spi_write_reg(&reg_base->slvsel, 0xffffffff);
	}
	master->num_chipselect = mpc8xxx_spi->native_chipselects;
	master->set_cs = fsl_spi_grlib_cs_control;
}

static void fsl_spi_cs_control(struct spi_device *spi, bool on)
{
	struct device *dev = spi->dev.parent->parent;
	struct fsl_spi_platform_data *pdata = dev_get_platdata(dev);
	struct mpc8xxx_spi_probe_info *pinfo = to_of_pinfo(pdata);

	if (WARN_ON_ONCE(!pinfo->immr_spi_cs))
		return;
	iowrite32be(on ? 0 : SPI_BOOT_SEL_BIT, pinfo->immr_spi_cs);
}

static struct spi_master *fsl_spi_probe(struct device *dev,
		struct resource *mem, unsigned int irq)
{
	struct fsl_spi_platform_data *pdata = dev_get_platdata(dev);
	struct spi_master *master;
	struct mpc8xxx_spi *mpc8xxx_spi;
	struct fsl_spi_reg __iomem *reg_base;
	u32 regval;
	int ret = 0;

	master = spi_alloc_master(dev, sizeof(struct mpc8xxx_spi));
	if (master == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	dev_set_drvdata(dev, master);

	mpc8xxx_spi_probe(dev, mem, irq);

	master->setup = fsl_spi_setup;
	master->cleanup = fsl_spi_cleanup;
	master->prepare_message = fsl_spi_prepare_message;
	master->transfer_one = fsl_spi_transfer_one;
	master->unprepare_message = fsl_spi_unprepare_message;
	master->use_gpio_descriptors = true;
	master->set_cs = fsl_spi_cs_control;

	mpc8xxx_spi = spi_master_get_devdata(master);
	mpc8xxx_spi->max_bits_per_word = 32;
	mpc8xxx_spi->type = fsl_spi_get_type(dev);

	ret = fsl_spi_cpm_init(mpc8xxx_spi);
	if (ret)
		goto err_cpm_init;

	mpc8xxx_spi->reg_base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(mpc8xxx_spi->reg_base)) {
		ret = PTR_ERR(mpc8xxx_spi->reg_base);
		goto err_probe;
	}

	if (mpc8xxx_spi->type == TYPE_GRLIB)
		fsl_spi_grlib_probe(dev);

	if (mpc8xxx_spi->flags & SPI_CPM_MODE)
		master->bits_per_word_mask =
			(SPI_BPW_RANGE_MASK(4, 8) | SPI_BPW_MASK(16) | SPI_BPW_MASK(32));
	else
		master->bits_per_word_mask =
			(SPI_BPW_RANGE_MASK(4, 16) | SPI_BPW_MASK(32));

	master->bits_per_word_mask &=
		SPI_BPW_RANGE_MASK(1, mpc8xxx_spi->max_bits_per_word);

	if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE)
		mpc8xxx_spi->set_shifts = fsl_spi_qe_cpu_set_shifts;

	if (mpc8xxx_spi->set_shifts)
		/* 8 bits per word and MSB first */
		mpc8xxx_spi->set_shifts(&mpc8xxx_spi->rx_shift,
					&mpc8xxx_spi->tx_shift, 8, 1);

	/* Register for SPI Interrupt */
	ret = devm_request_irq(dev, mpc8xxx_spi->irq, fsl_spi_irq,
			       0, "fsl_spi", mpc8xxx_spi);

	if (ret != 0)
		goto err_probe;

	reg_base = mpc8xxx_spi->reg_base;

	/* SPI controller initializations */
	mpc8xxx_spi_write_reg(&reg_base->mode, 0);
	mpc8xxx_spi_write_reg(&reg_base->mask, 0);
	mpc8xxx_spi_write_reg(&reg_base->command, 0);
	mpc8xxx_spi_write_reg(&reg_base->event, 0xffffffff);

	/* Enable SPI interface */
	regval = pdata->initial_spmode | SPMODE_INIT_VAL | SPMODE_ENABLE;
	if (mpc8xxx_spi->max_bits_per_word < 8) {
		regval &= ~SPMODE_LEN(0xF);
		regval |= SPMODE_LEN(mpc8xxx_spi->max_bits_per_word - 1);
	}
	if (mpc8xxx_spi->flags & SPI_QE_CPU_MODE)
		regval |= SPMODE_OP;

	mpc8xxx_spi_write_reg(&reg_base->mode, regval);

	ret = devm_spi_register_master(dev, master);
	if (ret < 0)
		goto err_probe;

	dev_info(dev, "at 0x%p (irq = %d), %s mode\n", reg_base,
		 mpc8xxx_spi->irq, mpc8xxx_spi_strmode(mpc8xxx_spi->flags));

	return master;

err_probe:
	fsl_spi_cpm_free(mpc8xxx_spi);
err_cpm_init:
	spi_master_put(master);
err:
	return ERR_PTR(ret);
}

static int of_fsl_spi_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct spi_master *master;
	struct resource mem;
	int irq, type;
	int ret;
	bool spisel_boot = false;
#if IS_ENABLED(CONFIG_FSL_SOC)
	struct mpc8xxx_spi_probe_info *pinfo = NULL;
#endif


	ret = of_mpc8xxx_spi_probe(ofdev);
	if (ret)
		return ret;

	type = fsl_spi_get_type(&ofdev->dev);
	if (type == TYPE_FSL) {
		struct fsl_spi_platform_data *pdata = dev_get_platdata(dev);
#if IS_ENABLED(CONFIG_FSL_SOC)
		pinfo = to_of_pinfo(pdata);

		spisel_boot = of_property_read_bool(np, "fsl,spisel_boot");
		if (spisel_boot) {
			pinfo->immr_spi_cs = ioremap(get_immrbase() + IMMR_SPI_CS_OFFSET, 4);
			if (!pinfo->immr_spi_cs)
				return -ENOMEM;
		}
#endif
		/*
		 * Handle the case where we have one hardwired (always selected)
		 * device on the first "chipselect". Else we let the core code
		 * handle any GPIOs or native chip selects and assign the
		 * appropriate callback for dealing with the CS lines. This isn't
		 * supported on the GRLIB variant.
		 */
		ret = gpiod_count(dev, "cs");
		if (ret < 0)
			ret = 0;
		if (ret == 0 && !spisel_boot)
			pdata->max_chipselect = 1;
		else
			pdata->max_chipselect = ret + spisel_boot;
	}

	ret = of_address_to_resource(np, 0, &mem);
	if (ret)
		goto unmap_out;

	irq = platform_get_irq(ofdev, 0);
	if (irq < 0) {
		ret = irq;
		goto unmap_out;
	}

	master = fsl_spi_probe(dev, &mem, irq);

	return PTR_ERR_OR_ZERO(master);

unmap_out:
#if IS_ENABLED(CONFIG_FSL_SOC)
	if (spisel_boot)
		iounmap(pinfo->immr_spi_cs);
#endif
	return ret;
}

static void of_fsl_spi_remove(struct platform_device *ofdev)
{
	struct spi_master *master = platform_get_drvdata(ofdev);
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(master);

	fsl_spi_cpm_free(mpc8xxx_spi);
}

static struct platform_driver of_fsl_spi_driver = {
	.driver = {
		.name = "fsl_spi",
		.of_match_table = of_fsl_spi_match,
	},
	.probe		= of_fsl_spi_probe,
	.remove_new	= of_fsl_spi_remove,
};

#ifdef CONFIG_MPC832x_RDB
/*
 * XXX XXX XXX
 * This is "legacy" platform driver, was used by the MPC8323E-RDB boards
 * only. The driver should go away soon, since newer MPC8323E-RDB's device
 * tree can work with OpenFirmware driver. But for now we support old trees
 * as well.
 */
static int plat_mpc8xxx_spi_probe(struct platform_device *pdev)
{
	struct resource *mem;
	int irq;
	struct spi_master *master;

	if (!dev_get_platdata(&pdev->dev))
		return -EINVAL;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	master = fsl_spi_probe(&pdev->dev, mem, irq);
	return PTR_ERR_OR_ZERO(master);
}

static void plat_mpc8xxx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct mpc8xxx_spi *mpc8xxx_spi = spi_master_get_devdata(master);

	fsl_spi_cpm_free(mpc8xxx_spi);
}

MODULE_ALIAS("platform:mpc8xxx_spi");
static struct platform_driver mpc8xxx_spi_driver = {
	.probe = plat_mpc8xxx_spi_probe,
	.remove_new = plat_mpc8xxx_spi_remove,
	.driver = {
		.name = "mpc8xxx_spi",
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

static int __init fsl_spi_init(void)
{
	legacy_driver_register();
	return platform_driver_register(&of_fsl_spi_driver);
}
module_init(fsl_spi_init);

static void __exit fsl_spi_exit(void)
{
	platform_driver_unregister(&of_fsl_spi_driver);
	legacy_driver_unregister();
}
module_exit(fsl_spi_exit);

MODULE_AUTHOR("Kumar Gala");
MODULE_DESCRIPTION("Simple Freescale SPI Driver");
MODULE_LICENSE("GPL");
