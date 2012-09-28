/*
 * Copyright (c) 2006 Ben Dooks
 * Copyright 2006-2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/spi/s3c24xx.h>
#include <linux/module.h>

#include <plat/regs-spi.h>

#include <plat/fiq.h>
#include <asm/fiq.h>

#include "spi-s3c24xx-fiq.h"

/**
 * s3c24xx_spi_devstate - per device data
 * @hz: Last frequency calculated for @sppre field.
 * @mode: Last mode setting for the @spcon field.
 * @spcon: Value to write to the SPCON register.
 * @sppre: Value to write to the SPPRE register.
 */
struct s3c24xx_spi_devstate {
	unsigned int	hz;
	unsigned int	mode;
	u8		spcon;
	u8		sppre;
};

enum spi_fiq_mode {
	FIQ_MODE_NONE	= 0,
	FIQ_MODE_TX	= 1,
	FIQ_MODE_RX	= 2,
	FIQ_MODE_TXRX	= 3,
};

struct s3c24xx_spi {
	/* bitbang has to be first */
	struct spi_bitbang	 bitbang;
	struct completion	 done;

	void __iomem		*regs;
	int			 irq;
	int			 len;
	int			 count;

	struct fiq_handler	 fiq_handler;
	enum spi_fiq_mode	 fiq_mode;
	unsigned char		 fiq_inuse;
	unsigned char		 fiq_claimed;

	void			(*set_cs)(struct s3c2410_spi_info *spi,
					  int cs, int pol);

	/* data buffers */
	const unsigned char	*tx;
	unsigned char		*rx;

	struct clk		*clk;
	struct resource		*ioarea;
	struct spi_master	*master;
	struct spi_device	*curdev;
	struct device		*dev;
	struct s3c2410_spi_info *pdata;
};


#define SPCON_DEFAULT (S3C2410_SPCON_MSTR | S3C2410_SPCON_SMOD_INT)
#define SPPIN_DEFAULT (S3C2410_SPPIN_KEEP)

static inline struct s3c24xx_spi *to_hw(struct spi_device *sdev)
{
	return spi_master_get_devdata(sdev->master);
}

static void s3c24xx_spi_gpiocs(struct s3c2410_spi_info *spi, int cs, int pol)
{
	gpio_set_value(spi->pin_cs, pol);
}

static void s3c24xx_spi_chipsel(struct spi_device *spi, int value)
{
	struct s3c24xx_spi_devstate *cs = spi->controller_state;
	struct s3c24xx_spi *hw = to_hw(spi);
	unsigned int cspol = spi->mode & SPI_CS_HIGH ? 1 : 0;

	/* change the chipselect state and the state of the spi engine clock */

	switch (value) {
	case BITBANG_CS_INACTIVE:
		hw->set_cs(hw->pdata, spi->chip_select, cspol^1);
		writeb(cs->spcon, hw->regs + S3C2410_SPCON);
		break;

	case BITBANG_CS_ACTIVE:
		writeb(cs->spcon | S3C2410_SPCON_ENSCK,
		       hw->regs + S3C2410_SPCON);
		hw->set_cs(hw->pdata, spi->chip_select, cspol);
		break;
	}
}

static int s3c24xx_spi_update_state(struct spi_device *spi,
				    struct spi_transfer *t)
{
	struct s3c24xx_spi *hw = to_hw(spi);
	struct s3c24xx_spi_devstate *cs = spi->controller_state;
	unsigned int bpw;
	unsigned int hz;
	unsigned int div;
	unsigned long clk;

	bpw = t ? t->bits_per_word : spi->bits_per_word;
	hz  = t ? t->speed_hz : spi->max_speed_hz;

	if (!bpw)
		bpw = 8;

	if (!hz)
		hz = spi->max_speed_hz;

	if (bpw != 8) {
		dev_err(&spi->dev, "invalid bits-per-word (%d)\n", bpw);
		return -EINVAL;
	}

	if (spi->mode != cs->mode) {
		u8 spcon = SPCON_DEFAULT | S3C2410_SPCON_ENSCK;

		if (spi->mode & SPI_CPHA)
			spcon |= S3C2410_SPCON_CPHA_FMTB;

		if (spi->mode & SPI_CPOL)
			spcon |= S3C2410_SPCON_CPOL_HIGH;

		cs->mode = spi->mode;
		cs->spcon = spcon;
	}

	if (cs->hz != hz) {
		clk = clk_get_rate(hw->clk);
		div = DIV_ROUND_UP(clk, hz * 2) - 1;

		if (div > 255)
			div = 255;

		dev_dbg(&spi->dev, "pre-scaler=%d (wanted %d, got %ld)\n",
			div, hz, clk / (2 * (div + 1)));

		cs->hz = hz;
		cs->sppre = div;
	}

	return 0;
}

static int s3c24xx_spi_setupxfer(struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct s3c24xx_spi_devstate *cs = spi->controller_state;
	struct s3c24xx_spi *hw = to_hw(spi);
	int ret;

	ret = s3c24xx_spi_update_state(spi, t);
	if (!ret)
		writeb(cs->sppre, hw->regs + S3C2410_SPPRE);

	return ret;
}

static int s3c24xx_spi_setup(struct spi_device *spi)
{
	struct s3c24xx_spi_devstate *cs = spi->controller_state;
	struct s3c24xx_spi *hw = to_hw(spi);
	int ret;

	/* allocate settings on the first call */
	if (!cs) {
		cs = kzalloc(sizeof(struct s3c24xx_spi_devstate), GFP_KERNEL);
		if (!cs) {
			dev_err(&spi->dev, "no memory for controller state\n");
			return -ENOMEM;
		}

		cs->spcon = SPCON_DEFAULT;
		cs->hz = -1;
		spi->controller_state = cs;
	}

	/* initialise the state from the device */
	ret = s3c24xx_spi_update_state(spi, NULL);
	if (ret)
		return ret;

	spin_lock(&hw->bitbang.lock);
	if (!hw->bitbang.busy) {
		hw->bitbang.chipselect(spi, BITBANG_CS_INACTIVE);
		/* need to ndelay for 0.5 clocktick ? */
	}
	spin_unlock(&hw->bitbang.lock);

	return 0;
}

static void s3c24xx_spi_cleanup(struct spi_device *spi)
{
	kfree(spi->controller_state);
}

static inline unsigned int hw_txbyte(struct s3c24xx_spi *hw, int count)
{
	return hw->tx ? hw->tx[count] : 0;
}

#ifdef CONFIG_SPI_S3C24XX_FIQ
/* Support for FIQ based pseudo-DMA to improve the transfer speed.
 *
 * This code uses the assembly helper in spi_s3c24xx_spi.S which is
 * used by the FIQ core to move data between main memory and the peripheral
 * block. Since this is code running on the processor, there is no problem
 * with cache coherency of the buffers, so we can use any buffer we like.
 */

/**
 * struct spi_fiq_code - FIQ code and header
 * @length: The length of the code fragment, excluding this header.
 * @ack_offset: The offset from @data to the word to place the IRQ ACK bit at.
 * @data: The code itself to install as a FIQ handler.
 */
struct spi_fiq_code {
	u32	length;
	u32	ack_offset;
	u8	data[0];
};

extern struct spi_fiq_code s3c24xx_spi_fiq_txrx;
extern struct spi_fiq_code s3c24xx_spi_fiq_tx;
extern struct spi_fiq_code s3c24xx_spi_fiq_rx;

/**
 * ack_bit - turn IRQ into IRQ acknowledgement bit
 * @irq: The interrupt number
 *
 * Returns the bit to write to the interrupt acknowledge register.
 */
static inline u32 ack_bit(unsigned int irq)
{
	return 1 << (irq - IRQ_EINT0);
}

/**
 * s3c24xx_spi_tryfiq - attempt to claim and setup FIQ for transfer
 * @hw: The hardware state.
 *
 * Claim the FIQ handler (only one can be active at any one time) and
 * then setup the correct transfer code for this transfer.
 *
 * This call updates all the necessary state information if successful,
 * so the caller does not need to do anything more than start the transfer
 * as normal, since the IRQ will have been re-routed to the FIQ handler.
*/
void s3c24xx_spi_tryfiq(struct s3c24xx_spi *hw)
{
	struct pt_regs regs;
	enum spi_fiq_mode mode;
	struct spi_fiq_code *code;
	int ret;

	if (!hw->fiq_claimed) {
		/* try and claim fiq if we haven't got it, and if not
		 * then return and simply use another transfer method */

		ret = claim_fiq(&hw->fiq_handler);
		if (ret)
			return;
	}

	if (hw->tx && !hw->rx)
		mode = FIQ_MODE_TX;
	else if (hw->rx && !hw->tx)
		mode = FIQ_MODE_RX;
	else
		mode = FIQ_MODE_TXRX;

	regs.uregs[fiq_rspi] = (long)hw->regs;
	regs.uregs[fiq_rrx]  = (long)hw->rx;
	regs.uregs[fiq_rtx]  = (long)hw->tx + 1;
	regs.uregs[fiq_rcount] = hw->len - 1;
	regs.uregs[fiq_rirq] = (long)S3C24XX_VA_IRQ;

	set_fiq_regs(&regs);

	if (hw->fiq_mode != mode) {
		u32 *ack_ptr;

		hw->fiq_mode = mode;

		switch (mode) {
		case FIQ_MODE_TX:
			code = &s3c24xx_spi_fiq_tx;
			break;
		case FIQ_MODE_RX:
			code = &s3c24xx_spi_fiq_rx;
			break;
		case FIQ_MODE_TXRX:
			code = &s3c24xx_spi_fiq_txrx;
			break;
		default:
			code = NULL;
		}

		BUG_ON(!code);

		ack_ptr = (u32 *)&code->data[code->ack_offset];
		*ack_ptr = ack_bit(hw->irq);

		set_fiq_handler(&code->data, code->length);
	}

	s3c24xx_set_fiq(hw->irq, true);

	hw->fiq_mode = mode;
	hw->fiq_inuse = 1;
}

/**
 * s3c24xx_spi_fiqop - FIQ core code callback
 * @pw: Data registered with the handler
 * @release: Whether this is a release or a return.
 *
 * Called by the FIQ code when another module wants to use the FIQ, so
 * return whether we are currently using this or not and then update our
 * internal state.
 */
static int s3c24xx_spi_fiqop(void *pw, int release)
{
	struct s3c24xx_spi *hw = pw;
	int ret = 0;

	if (release) {
		if (hw->fiq_inuse)
			ret = -EBUSY;

		/* note, we do not need to unroute the FIQ, as the FIQ
		 * vector code de-routes it to signal the end of transfer */

		hw->fiq_mode = FIQ_MODE_NONE;
		hw->fiq_claimed = 0;
	} else {
		hw->fiq_claimed = 1;
	}

	return ret;
}

/**
 * s3c24xx_spi_initfiq - setup the information for the FIQ core
 * @hw: The hardware state.
 *
 * Setup the fiq_handler block to pass to the FIQ core.
 */
static inline void s3c24xx_spi_initfiq(struct s3c24xx_spi *hw)
{
	hw->fiq_handler.dev_id = hw;
	hw->fiq_handler.name = dev_name(hw->dev);
	hw->fiq_handler.fiq_op = s3c24xx_spi_fiqop;
}

/**
 * s3c24xx_spi_usefiq - return if we should be using FIQ.
 * @hw: The hardware state.
 *
 * Return true if the platform data specifies whether this channel is
 * allowed to use the FIQ.
 */
static inline bool s3c24xx_spi_usefiq(struct s3c24xx_spi *hw)
{
	return hw->pdata->use_fiq;
}

/**
 * s3c24xx_spi_usingfiq - return if channel is using FIQ
 * @spi: The hardware state.
 *
 * Return whether the channel is currently using the FIQ (separate from
 * whether the FIQ is claimed).
 */
static inline bool s3c24xx_spi_usingfiq(struct s3c24xx_spi *spi)
{
	return spi->fiq_inuse;
}
#else

static inline void s3c24xx_spi_initfiq(struct s3c24xx_spi *s) { }
static inline void s3c24xx_spi_tryfiq(struct s3c24xx_spi *s) { }
static inline bool s3c24xx_spi_usefiq(struct s3c24xx_spi *s) { return false; }
static inline bool s3c24xx_spi_usingfiq(struct s3c24xx_spi *s) { return false; }

#endif /* CONFIG_SPI_S3C24XX_FIQ */

static int s3c24xx_spi_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct s3c24xx_spi *hw = to_hw(spi);

	hw->tx = t->tx_buf;
	hw->rx = t->rx_buf;
	hw->len = t->len;
	hw->count = 0;

	init_completion(&hw->done);

	hw->fiq_inuse = 0;
	if (s3c24xx_spi_usefiq(hw) && t->len >= 3)
		s3c24xx_spi_tryfiq(hw);

	/* send the first byte */
	writeb(hw_txbyte(hw, 0), hw->regs + S3C2410_SPTDAT);

	wait_for_completion(&hw->done);
	return hw->count;
}

static irqreturn_t s3c24xx_spi_irq(int irq, void *dev)
{
	struct s3c24xx_spi *hw = dev;
	unsigned int spsta = readb(hw->regs + S3C2410_SPSTA);
	unsigned int count = hw->count;

	if (spsta & S3C2410_SPSTA_DCOL) {
		dev_dbg(hw->dev, "data-collision\n");
		complete(&hw->done);
		goto irq_done;
	}

	if (!(spsta & S3C2410_SPSTA_READY)) {
		dev_dbg(hw->dev, "spi not ready for tx?\n");
		complete(&hw->done);
		goto irq_done;
	}

	if (!s3c24xx_spi_usingfiq(hw)) {
		hw->count++;

		if (hw->rx)
			hw->rx[count] = readb(hw->regs + S3C2410_SPRDAT);

		count++;

		if (count < hw->len)
			writeb(hw_txbyte(hw, count), hw->regs + S3C2410_SPTDAT);
		else
			complete(&hw->done);
	} else {
		hw->count = hw->len;
		hw->fiq_inuse = 0;

		if (hw->rx)
			hw->rx[hw->len-1] = readb(hw->regs + S3C2410_SPRDAT);

		complete(&hw->done);
	}

 irq_done:
	return IRQ_HANDLED;
}

static void s3c24xx_spi_initialsetup(struct s3c24xx_spi *hw)
{
	/* for the moment, permanently enable the clock */

	clk_enable(hw->clk);

	/* program defaults into the registers */

	writeb(0xff, hw->regs + S3C2410_SPPRE);
	writeb(SPPIN_DEFAULT, hw->regs + S3C2410_SPPIN);
	writeb(SPCON_DEFAULT, hw->regs + S3C2410_SPCON);

	if (hw->pdata) {
		if (hw->set_cs == s3c24xx_spi_gpiocs)
			gpio_direction_output(hw->pdata->pin_cs, 1);

		if (hw->pdata->gpio_setup)
			hw->pdata->gpio_setup(hw->pdata, 1);
	}
}

static int __devinit s3c24xx_spi_probe(struct platform_device *pdev)
{
	struct s3c2410_spi_info *pdata;
	struct s3c24xx_spi *hw;
	struct spi_master *master;
	struct resource *res;
	int err = 0;

	master = spi_alloc_master(&pdev->dev, sizeof(struct s3c24xx_spi));
	if (master == NULL) {
		dev_err(&pdev->dev, "No memory for spi_master\n");
		err = -ENOMEM;
		goto err_nomem;
	}

	hw = spi_master_get_devdata(master);
	memset(hw, 0, sizeof(struct s3c24xx_spi));

	hw->master = spi_master_get(master);
	hw->pdata = pdata = pdev->dev.platform_data;
	hw->dev = &pdev->dev;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		err = -ENOENT;
		goto err_no_pdata;
	}

	platform_set_drvdata(pdev, hw);
	init_completion(&hw->done);

	/* initialise fiq handler */

	s3c24xx_spi_initfiq(hw);

	/* setup the master state. */

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	master->num_chipselect = hw->pdata->num_cs;
	master->bus_num = pdata->bus_num;

	/* setup the state for the bitbang driver */

	hw->bitbang.master         = hw->master;
	hw->bitbang.setup_transfer = s3c24xx_spi_setupxfer;
	hw->bitbang.chipselect     = s3c24xx_spi_chipsel;
	hw->bitbang.txrx_bufs      = s3c24xx_spi_txrx;

	hw->master->setup  = s3c24xx_spi_setup;
	hw->master->cleanup = s3c24xx_spi_cleanup;

	dev_dbg(hw->dev, "bitbang at %p\n", &hw->bitbang);

	/* find and map our resources */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
		err = -ENOENT;
		goto err_no_iores;
	}

	hw->ioarea = request_mem_region(res->start, resource_size(res),
					pdev->name);

	if (hw->ioarea == NULL) {
		dev_err(&pdev->dev, "Cannot reserve region\n");
		err = -ENXIO;
		goto err_no_iores;
	}

	hw->regs = ioremap(res->start, resource_size(res));
	if (hw->regs == NULL) {
		dev_err(&pdev->dev, "Cannot map IO\n");
		err = -ENXIO;
		goto err_no_iomap;
	}

	hw->irq = platform_get_irq(pdev, 0);
	if (hw->irq < 0) {
		dev_err(&pdev->dev, "No IRQ specified\n");
		err = -ENOENT;
		goto err_no_irq;
	}

	err = request_irq(hw->irq, s3c24xx_spi_irq, 0, pdev->name, hw);
	if (err) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		goto err_no_irq;
	}

	hw->clk = clk_get(&pdev->dev, "spi");
	if (IS_ERR(hw->clk)) {
		dev_err(&pdev->dev, "No clock for device\n");
		err = PTR_ERR(hw->clk);
		goto err_no_clk;
	}

	/* setup any gpio we can */

	if (!pdata->set_cs) {
		if (pdata->pin_cs < 0) {
			dev_err(&pdev->dev, "No chipselect pin\n");
			err = -EINVAL;
			goto err_register;
		}

		err = gpio_request(pdata->pin_cs, dev_name(&pdev->dev));
		if (err) {
			dev_err(&pdev->dev, "Failed to get gpio for cs\n");
			goto err_register;
		}

		hw->set_cs = s3c24xx_spi_gpiocs;
		gpio_direction_output(pdata->pin_cs, 1);
	} else
		hw->set_cs = pdata->set_cs;

	s3c24xx_spi_initialsetup(hw);

	/* register our spi controller */

	err = spi_bitbang_start(&hw->bitbang);
	if (err) {
		dev_err(&pdev->dev, "Failed to register SPI master\n");
		goto err_register;
	}

	return 0;

 err_register:
	if (hw->set_cs == s3c24xx_spi_gpiocs)
		gpio_free(pdata->pin_cs);

	clk_disable(hw->clk);
	clk_put(hw->clk);

 err_no_clk:
	free_irq(hw->irq, hw);

 err_no_irq:
	iounmap(hw->regs);

 err_no_iomap:
	release_resource(hw->ioarea);
	kfree(hw->ioarea);

 err_no_iores:
 err_no_pdata:
	spi_master_put(hw->master);

 err_nomem:
	return err;
}

static int __devexit s3c24xx_spi_remove(struct platform_device *dev)
{
	struct s3c24xx_spi *hw = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	spi_bitbang_stop(&hw->bitbang);

	clk_disable(hw->clk);
	clk_put(hw->clk);

	free_irq(hw->irq, hw);
	iounmap(hw->regs);

	if (hw->set_cs == s3c24xx_spi_gpiocs)
		gpio_free(hw->pdata->pin_cs);

	release_resource(hw->ioarea);
	kfree(hw->ioarea);

	spi_master_put(hw->master);
	return 0;
}


#ifdef CONFIG_PM

static int s3c24xx_spi_suspend(struct device *dev)
{
	struct s3c24xx_spi *hw = platform_get_drvdata(to_platform_device(dev));

	if (hw->pdata && hw->pdata->gpio_setup)
		hw->pdata->gpio_setup(hw->pdata, 0);

	clk_disable(hw->clk);
	return 0;
}

static int s3c24xx_spi_resume(struct device *dev)
{
	struct s3c24xx_spi *hw = platform_get_drvdata(to_platform_device(dev));

	s3c24xx_spi_initialsetup(hw);
	return 0;
}

static const struct dev_pm_ops s3c24xx_spi_pmops = {
	.suspend	= s3c24xx_spi_suspend,
	.resume		= s3c24xx_spi_resume,
};

#define S3C24XX_SPI_PMOPS &s3c24xx_spi_pmops
#else
#define S3C24XX_SPI_PMOPS NULL
#endif /* CONFIG_PM */

MODULE_ALIAS("platform:s3c2410-spi");
static struct platform_driver s3c24xx_spi_driver = {
	.probe		= s3c24xx_spi_probe,
	.remove		= __devexit_p(s3c24xx_spi_remove),
	.driver		= {
		.name	= "s3c2410-spi",
		.owner	= THIS_MODULE,
		.pm	= S3C24XX_SPI_PMOPS,
	},
};
module_platform_driver(s3c24xx_spi_driver);

MODULE_DESCRIPTION("S3C24XX SPI Driver");
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
