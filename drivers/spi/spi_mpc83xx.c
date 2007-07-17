/*
 * MPC83xx SPI controller driver.
 *
 * Maintainer: Kumar Gala
 *
 * Copyright (C) 2006 Polycom, Inc.
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
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>

#include <asm/irq.h>
#include <asm/io.h>

/* SPI Controller registers */
struct mpc83xx_spi_reg {
	u8 res1[0x20];
	__be32 mode;
	__be32 event;
	__be32 mask;
	__be32 command;
	__be32 transmit;
	__be32 receive;
};

/* SPI Controller mode register definitions */
#define	SPMODE_CI_INACTIVEHIGH	(1 << 29)
#define	SPMODE_CP_BEGIN_EDGECLK	(1 << 28)
#define	SPMODE_DIV16		(1 << 27)
#define	SPMODE_REV		(1 << 26)
#define	SPMODE_MS		(1 << 25)
#define	SPMODE_ENABLE		(1 << 24)
#define	SPMODE_LEN(x)		((x) << 20)
#define	SPMODE_PM(x)		((x) << 16)

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

/* SPI Controller driver's private data. */
struct mpc83xx_spi {
	/* bitbang has to be first */
	struct spi_bitbang bitbang;
	struct completion done;

	struct mpc83xx_spi_reg __iomem *base;

	/* rx & tx bufs from the spi_transfer */
	const void *tx;
	void *rx;

	/* functions to deal with different sized buffers */
	void (*get_rx) (u32 rx_data, struct mpc83xx_spi *);
	u32(*get_tx) (struct mpc83xx_spi *);

	unsigned int count;
	u32 irq;

	unsigned nsecs;		/* (clock cycle time)/2 */

	u32 sysclk;
	void (*activate_cs) (u8 cs, u8 polarity);
	void (*deactivate_cs) (u8 cs, u8 polarity);
};

static inline void mpc83xx_spi_write_reg(__be32 __iomem * reg, u32 val)
{
	out_be32(reg, val);
}

static inline u32 mpc83xx_spi_read_reg(__be32 __iomem * reg)
{
	return in_be32(reg);
}

#define MPC83XX_SPI_RX_BUF(type) 					  \
void mpc83xx_spi_rx_buf_##type(u32 data, struct mpc83xx_spi *mpc83xx_spi) \
{									  \
	type * rx = mpc83xx_spi->rx;					  \
	*rx++ = (type)data;						  \
	mpc83xx_spi->rx = rx;						  \
}

#define MPC83XX_SPI_TX_BUF(type)				\
u32 mpc83xx_spi_tx_buf_##type(struct mpc83xx_spi *mpc83xx_spi)	\
{								\
	u32 data;						\
	const type * tx = mpc83xx_spi->tx;			\
	if (!tx)						\
		return 0;					\
	data = *tx++;						\
	mpc83xx_spi->tx = tx;					\
	return data;						\
}

MPC83XX_SPI_RX_BUF(u8)
MPC83XX_SPI_RX_BUF(u16)
MPC83XX_SPI_RX_BUF(u32)
MPC83XX_SPI_TX_BUF(u8)
MPC83XX_SPI_TX_BUF(u16)
MPC83XX_SPI_TX_BUF(u32)

static void mpc83xx_spi_chipselect(struct spi_device *spi, int value)
{
	struct mpc83xx_spi *mpc83xx_spi;
	u8 pol = spi->mode & SPI_CS_HIGH ? 1 : 0;

	mpc83xx_spi = spi_master_get_devdata(spi->master);

	if (value == BITBANG_CS_INACTIVE) {
		if (mpc83xx_spi->deactivate_cs)
			mpc83xx_spi->deactivate_cs(spi->chip_select, pol);
	}

	if (value == BITBANG_CS_ACTIVE) {
		u32 regval = mpc83xx_spi_read_reg(&mpc83xx_spi->base->mode);
		u32 len = spi->bits_per_word;
		if (len == 32)
			len = 0;
		else
			len = len - 1;

		/* mask out bits we are going to set */
		regval &= ~0x38ff0000;

		if (spi->mode & SPI_CPHA)
			regval |= SPMODE_CP_BEGIN_EDGECLK;
		if (spi->mode & SPI_CPOL)
			regval |= SPMODE_CI_INACTIVEHIGH;

		regval |= SPMODE_LEN(len);

		if ((mpc83xx_spi->sysclk / spi->max_speed_hz) >= 64) {
			u8 pm = mpc83xx_spi->sysclk / (spi->max_speed_hz * 64);
			regval |= SPMODE_PM(pm) | SPMODE_DIV16;
		} else {
			u8 pm = mpc83xx_spi->sysclk / (spi->max_speed_hz * 4);
			regval |= SPMODE_PM(pm);
		}

		mpc83xx_spi_write_reg(&mpc83xx_spi->base->mode, regval);
		if (mpc83xx_spi->activate_cs)
			mpc83xx_spi->activate_cs(spi->chip_select, pol);
	}
}

static
int mpc83xx_spi_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct mpc83xx_spi *mpc83xx_spi;
	u32 regval;
	u8 bits_per_word;
	u32 hz;

	mpc83xx_spi = spi_master_get_devdata(spi->master);

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

	if (bits_per_word <= 8) {
		mpc83xx_spi->get_rx = mpc83xx_spi_rx_buf_u8;
		mpc83xx_spi->get_tx = mpc83xx_spi_tx_buf_u8;
	} else if (bits_per_word <= 16) {
		mpc83xx_spi->get_rx = mpc83xx_spi_rx_buf_u16;
		mpc83xx_spi->get_tx = mpc83xx_spi_tx_buf_u16;
	} else if (bits_per_word <= 32) {
		mpc83xx_spi->get_rx = mpc83xx_spi_rx_buf_u32;
		mpc83xx_spi->get_tx = mpc83xx_spi_tx_buf_u32;
	} else
		return -EINVAL;

	/* nsecs = (clock period)/2 */
	if (!hz)
		hz = spi->max_speed_hz;
	mpc83xx_spi->nsecs = (1000000000 / 2) / hz;
	if (mpc83xx_spi->nsecs > MAX_UDELAY_MS * 1000)
		return -EINVAL;

	if (bits_per_word == 32)
		bits_per_word = 0;
	else
		bits_per_word = bits_per_word - 1;

	regval = mpc83xx_spi_read_reg(&mpc83xx_spi->base->mode);

	/* Mask out bits_per_wordgth */
	regval &= 0xff0fffff;
	regval |= SPMODE_LEN(bits_per_word);

	mpc83xx_spi_write_reg(&mpc83xx_spi->base->mode, regval);

	return 0;
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH)

static int mpc83xx_spi_setup(struct spi_device *spi)
{
	struct spi_bitbang *bitbang;
	struct mpc83xx_spi *mpc83xx_spi;
	int retval;

	if (spi->mode & ~MODEBITS) {
		dev_dbg(&spi->dev, "setup: unsupported mode bits %x\n",
			spi->mode & ~MODEBITS);
		return -EINVAL;
	}

	if (!spi->max_speed_hz)
		return -EINVAL;

	bitbang = spi_master_get_devdata(spi->master);
	mpc83xx_spi = spi_master_get_devdata(spi->master);

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	retval = mpc83xx_spi_setup_transfer(spi, NULL);
	if (retval < 0)
		return retval;

	dev_dbg(&spi->dev, "%s, mode %d, %u bits/w, %u nsec\n",
		__FUNCTION__, spi->mode & (SPI_CPOL | SPI_CPHA),
		spi->bits_per_word, 2 * mpc83xx_spi->nsecs);

	/* NOTE we _need_ to call chipselect() early, ideally with adapter
	 * setup, unless the hardware defaults cooperate to avoid confusion
	 * between normal (active low) and inverted chipselects.
	 */

	/* deselect chip (low or high) */
	spin_lock(&bitbang->lock);
	if (!bitbang->busy) {
		bitbang->chipselect(spi, BITBANG_CS_INACTIVE);
		ndelay(mpc83xx_spi->nsecs);
	}
	spin_unlock(&bitbang->lock);

	return 0;
}

static int mpc83xx_spi_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct mpc83xx_spi *mpc83xx_spi;
	u32 word;

	mpc83xx_spi = spi_master_get_devdata(spi->master);

	mpc83xx_spi->tx = t->tx_buf;
	mpc83xx_spi->rx = t->rx_buf;
	mpc83xx_spi->count = t->len;
	INIT_COMPLETION(mpc83xx_spi->done);

	/* enable rx ints */
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->mask, SPIM_NE);

	/* transmit word */
	word = mpc83xx_spi->get_tx(mpc83xx_spi);
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->transmit, word);

	wait_for_completion(&mpc83xx_spi->done);

	/* disable rx ints */
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->mask, 0);

	return t->len - mpc83xx_spi->count;
}

irqreturn_t mpc83xx_spi_irq(s32 irq, void *context_data)
{
	struct mpc83xx_spi *mpc83xx_spi = context_data;
	u32 event;
	irqreturn_t ret = IRQ_NONE;

	/* Get interrupt events(tx/rx) */
	event = mpc83xx_spi_read_reg(&mpc83xx_spi->base->event);

	/* We need handle RX first */
	if (event & SPIE_NE) {
		u32 rx_data = mpc83xx_spi_read_reg(&mpc83xx_spi->base->receive);

		if (mpc83xx_spi->rx)
			mpc83xx_spi->get_rx(rx_data, mpc83xx_spi);

		ret = IRQ_HANDLED;
	}

	if ((event & SPIE_NF) == 0)
		/* spin until TX is done */
		while (((event =
			 mpc83xx_spi_read_reg(&mpc83xx_spi->base->event)) &
						SPIE_NF) == 0)
			 cpu_relax();

	mpc83xx_spi->count -= 1;
	if (mpc83xx_spi->count) {
		if (mpc83xx_spi->tx) {
			u32 word = mpc83xx_spi->get_tx(mpc83xx_spi);
			mpc83xx_spi_write_reg(&mpc83xx_spi->base->transmit,
					      word);
		}
	} else {
		complete(&mpc83xx_spi->done);
	}

	/* Clear the events */
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->event, event);

	return ret;
}

static int __init mpc83xx_spi_probe(struct platform_device *dev)
{
	struct spi_master *master;
	struct mpc83xx_spi *mpc83xx_spi;
	struct fsl_spi_platform_data *pdata;
	struct resource *r;
	u32 regval;
	int ret = 0;

	/* Get resources(memory, IRQ) associated with the device */
	master = spi_alloc_master(&dev->dev, sizeof(struct mpc83xx_spi));

	if (master == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	platform_set_drvdata(dev, master);
	pdata = dev->dev.platform_data;

	if (pdata == NULL) {
		ret = -ENODEV;
		goto free_master;
	}

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		ret = -ENODEV;
		goto free_master;
	}

	mpc83xx_spi = spi_master_get_devdata(master);
	mpc83xx_spi->bitbang.master = spi_master_get(master);
	mpc83xx_spi->bitbang.chipselect = mpc83xx_spi_chipselect;
	mpc83xx_spi->bitbang.setup_transfer = mpc83xx_spi_setup_transfer;
	mpc83xx_spi->bitbang.txrx_bufs = mpc83xx_spi_bufs;
	mpc83xx_spi->sysclk = pdata->sysclk;
	mpc83xx_spi->activate_cs = pdata->activate_cs;
	mpc83xx_spi->deactivate_cs = pdata->deactivate_cs;
	mpc83xx_spi->get_rx = mpc83xx_spi_rx_buf_u8;
	mpc83xx_spi->get_tx = mpc83xx_spi_tx_buf_u8;

	mpc83xx_spi->bitbang.master->setup = mpc83xx_spi_setup;
	init_completion(&mpc83xx_spi->done);

	mpc83xx_spi->base = ioremap(r->start, r->end - r->start + 1);
	if (mpc83xx_spi->base == NULL) {
		ret = -ENOMEM;
		goto put_master;
	}

	mpc83xx_spi->irq = platform_get_irq(dev, 0);

	if (mpc83xx_spi->irq < 0) {
		ret = -ENXIO;
		goto unmap_io;
	}

	/* Register for SPI Interrupt */
	ret = request_irq(mpc83xx_spi->irq, mpc83xx_spi_irq,
			  0, "mpc83xx_spi", mpc83xx_spi);

	if (ret != 0)
		goto unmap_io;

	master->bus_num = pdata->bus_num;
	master->num_chipselect = pdata->max_chipselect;

	/* SPI controller initializations */
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->mode, 0);
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->mask, 0);
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->command, 0);
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->event, 0xffffffff);

	/* Enable SPI interface */
	regval = pdata->initial_spmode | SPMODE_INIT_VAL | SPMODE_ENABLE;
	mpc83xx_spi_write_reg(&mpc83xx_spi->base->mode, regval);

	ret = spi_bitbang_start(&mpc83xx_spi->bitbang);

	if (ret != 0)
		goto free_irq;

	printk(KERN_INFO
	       "%s: MPC83xx SPI Controller driver at 0x%p (irq = %d)\n",
	       dev->dev.bus_id, mpc83xx_spi->base, mpc83xx_spi->irq);

	return ret;

free_irq:
	free_irq(mpc83xx_spi->irq, mpc83xx_spi);
unmap_io:
	iounmap(mpc83xx_spi->base);
put_master:
	spi_master_put(master);
free_master:
	kfree(master);
err:
	return ret;
}

static int __devexit mpc83xx_spi_remove(struct platform_device *dev)
{
	struct mpc83xx_spi *mpc83xx_spi;
	struct spi_master *master;

	master = platform_get_drvdata(dev);
	mpc83xx_spi = spi_master_get_devdata(master);

	spi_bitbang_stop(&mpc83xx_spi->bitbang);
	free_irq(mpc83xx_spi->irq, mpc83xx_spi);
	iounmap(mpc83xx_spi->base);
	spi_master_put(mpc83xx_spi->bitbang.master);

	return 0;
}

static struct platform_driver mpc83xx_spi_driver = {
	.probe = mpc83xx_spi_probe,
	.remove = __devexit_p(mpc83xx_spi_remove),
	.driver = {
		   .name = "mpc83xx_spi",
	},
};

static int __init mpc83xx_spi_init(void)
{
	return platform_driver_register(&mpc83xx_spi_driver);
}

static void __exit mpc83xx_spi_exit(void)
{
	platform_driver_unregister(&mpc83xx_spi_driver);
}

module_init(mpc83xx_spi_init);
module_exit(mpc83xx_spi_exit);

MODULE_AUTHOR("Kumar Gala");
MODULE_DESCRIPTION("Simple MPC83xx SPI Driver");
MODULE_LICENSE("GPL");
