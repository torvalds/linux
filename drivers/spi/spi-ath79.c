/*
 * SPI controller driver for the Atheros AR71XX/AR724X/AR913X SoCs
 *
 * Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
 *
 * This driver has been based on the spi-gpio.c:
 *	Copyright (C) 2006,2008 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/bitops.h>
#include <linux/gpio.h>

#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/ath79_spi_platform.h>

#define DRV_NAME	"ath79-spi"

struct ath79_spi {
	struct spi_bitbang	bitbang;
	u32			ioc_base;
	u32			reg_ctrl;
	void __iomem		*base;
};

static inline u32 ath79_spi_rr(struct ath79_spi *sp, unsigned reg)
{
	return ioread32(sp->base + reg);
}

static inline void ath79_spi_wr(struct ath79_spi *sp, unsigned reg, u32 val)
{
	iowrite32(val, sp->base + reg);
}

static inline struct ath79_spi *ath79_spidev_to_sp(struct spi_device *spi)
{
	return spi_master_get_devdata(spi->master);
}

static void ath79_spi_chipselect(struct spi_device *spi, int is_active)
{
	struct ath79_spi *sp = ath79_spidev_to_sp(spi);
	int cs_high = (spi->mode & SPI_CS_HIGH) ? is_active : !is_active;

	if (is_active) {
		/* set initial clock polarity */
		if (spi->mode & SPI_CPOL)
			sp->ioc_base |= AR71XX_SPI_IOC_CLK;
		else
			sp->ioc_base &= ~AR71XX_SPI_IOC_CLK;

		ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, sp->ioc_base);
	}

	if (spi->chip_select) {
		struct ath79_spi_controller_data *cdata = spi->controller_data;

		/* SPI is normally active-low */
		gpio_set_value(cdata->gpio, cs_high);
	} else {
		if (cs_high)
			sp->ioc_base |= AR71XX_SPI_IOC_CS0;
		else
			sp->ioc_base &= ~AR71XX_SPI_IOC_CS0;

		ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, sp->ioc_base);
	}

}

static int ath79_spi_setup_cs(struct spi_device *spi)
{
	struct ath79_spi *sp = ath79_spidev_to_sp(spi);
	struct ath79_spi_controller_data *cdata;

	cdata = spi->controller_data;
	if (spi->chip_select && !cdata)
		return -EINVAL;

	/* enable GPIO mode */
	ath79_spi_wr(sp, AR71XX_SPI_REG_FS, AR71XX_SPI_FS_GPIO);

	/* save CTRL register */
	sp->reg_ctrl = ath79_spi_rr(sp, AR71XX_SPI_REG_CTRL);
	sp->ioc_base = ath79_spi_rr(sp, AR71XX_SPI_REG_IOC);

	/* TODO: setup speed? */
	ath79_spi_wr(sp, AR71XX_SPI_REG_CTRL, 0x43);

	if (spi->chip_select) {
		int status = 0;

		status = gpio_request(cdata->gpio, dev_name(&spi->dev));
		if (status)
			return status;

		status = gpio_direction_output(cdata->gpio,
					       spi->mode & SPI_CS_HIGH);
		if (status) {
			gpio_free(cdata->gpio);
			return status;
		}
	} else {
		if (spi->mode & SPI_CS_HIGH)
			sp->ioc_base |= AR71XX_SPI_IOC_CS0;
		else
			sp->ioc_base &= ~AR71XX_SPI_IOC_CS0;
		ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, sp->ioc_base);
	}

	return 0;
}

static void ath79_spi_cleanup_cs(struct spi_device *spi)
{
	struct ath79_spi *sp = ath79_spidev_to_sp(spi);

	if (spi->chip_select) {
		struct ath79_spi_controller_data *cdata = spi->controller_data;
		gpio_free(cdata->gpio);
	}

	/* restore CTRL register */
	ath79_spi_wr(sp, AR71XX_SPI_REG_CTRL, sp->reg_ctrl);
	/* disable GPIO mode */
	ath79_spi_wr(sp, AR71XX_SPI_REG_FS, 0);
}

static int ath79_spi_setup(struct spi_device *spi)
{
	int status = 0;

	if (spi->bits_per_word > 32)
		return -EINVAL;

	if (!spi->controller_state) {
		status = ath79_spi_setup_cs(spi);
		if (status)
			return status;
	}

	status = spi_bitbang_setup(spi);
	if (status && !spi->controller_state)
		ath79_spi_cleanup_cs(spi);

	return status;
}

static void ath79_spi_cleanup(struct spi_device *spi)
{
	ath79_spi_cleanup_cs(spi);
	spi_bitbang_cleanup(spi);
}

static u32 ath79_spi_txrx_mode0(struct spi_device *spi, unsigned nsecs,
			       u32 word, u8 bits)
{
	struct ath79_spi *sp = ath79_spidev_to_sp(spi);
	u32 ioc = sp->ioc_base;

	/* clock starts at inactive polarity */
	for (word <<= (32 - bits); likely(bits); bits--) {
		u32 out;

		if (word & (1 << 31))
			out = ioc | AR71XX_SPI_IOC_DO;
		else
			out = ioc & ~AR71XX_SPI_IOC_DO;

		/* setup MSB (to slave) on trailing edge */
		ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, out);
		ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, out | AR71XX_SPI_IOC_CLK);

		word <<= 1;
	}

	return ath79_spi_rr(sp, AR71XX_SPI_REG_RDS);
}

static __devinit int ath79_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct ath79_spi *sp;
	struct ath79_spi_platform_data *pdata;
	struct resource	*r;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(*sp));
	if (master == NULL) {
		dev_err(&pdev->dev, "failed to allocate spi master\n");
		return -ENOMEM;
	}

	sp = spi_master_get_devdata(master);
	platform_set_drvdata(pdev, sp);

	pdata = pdev->dev.platform_data;

	master->setup = ath79_spi_setup;
	master->cleanup = ath79_spi_cleanup;
	if (pdata) {
		master->bus_num = pdata->bus_num;
		master->num_chipselect = pdata->num_chipselect;
	} else {
		master->bus_num = -1;
		master->num_chipselect = 1;
	}

	sp->bitbang.master = spi_master_get(master);
	sp->bitbang.chipselect = ath79_spi_chipselect;
	sp->bitbang.txrx_word[SPI_MODE_0] = ath79_spi_txrx_mode0;
	sp->bitbang.setup_transfer = spi_bitbang_setup_transfer;
	sp->bitbang.flags = SPI_CS_HIGH;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		ret = -ENOENT;
		goto err_put_master;
	}

	sp->base = ioremap(r->start, resource_size(r));
	if (!sp->base) {
		ret = -ENXIO;
		goto err_put_master;
	}

	ret = spi_bitbang_start(&sp->bitbang);
	if (ret)
		goto err_unmap;

	return 0;

err_unmap:
	iounmap(sp->base);
err_put_master:
	platform_set_drvdata(pdev, NULL);
	spi_master_put(sp->bitbang.master);

	return ret;
}

static __devexit int ath79_spi_remove(struct platform_device *pdev)
{
	struct ath79_spi *sp = platform_get_drvdata(pdev);

	spi_bitbang_stop(&sp->bitbang);
	iounmap(sp->base);
	platform_set_drvdata(pdev, NULL);
	spi_master_put(sp->bitbang.master);

	return 0;
}

static struct platform_driver ath79_spi_driver = {
	.probe		= ath79_spi_probe,
	.remove		= __devexit_p(ath79_spi_remove),
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(ath79_spi_driver);

MODULE_DESCRIPTION("SPI controller driver for Atheros AR71XX/AR724X/AR913X");
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
