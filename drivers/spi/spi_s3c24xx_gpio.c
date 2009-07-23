/* linux/drivers/spi/spi_s3c24xx_gpio.c
 *
 * Copyright (c) 2006 Ben Dooks
 * Copyright (c) 2006 Simtec Electronics
 *
 * S3C24XX GPIO based SPI driver
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
#include <linux/gpio.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#include <mach/regs-gpio.h>
#include <mach/spi-gpio.h>
#include <mach/hardware.h>

struct s3c2410_spigpio {
	struct spi_bitbang		 bitbang;

	struct s3c2410_spigpio_info	*info;
	struct platform_device		*dev;
};

static inline struct s3c2410_spigpio *spidev_to_sg(struct spi_device *spi)
{
	return spi_master_get_devdata(spi->master);
}

static inline void setsck(struct spi_device *dev, int on)
{
	struct s3c2410_spigpio *sg = spidev_to_sg(dev);
	s3c2410_gpio_setpin(sg->info->pin_clk, on ? 1 : 0);
}

static inline void setmosi(struct spi_device *dev, int on)
{
	struct s3c2410_spigpio *sg = spidev_to_sg(dev);
	s3c2410_gpio_setpin(sg->info->pin_mosi, on ? 1 : 0);
}

static inline u32 getmiso(struct spi_device *dev)
{
	struct s3c2410_spigpio *sg = spidev_to_sg(dev);
	return s3c2410_gpio_getpin(sg->info->pin_miso) ? 1 : 0;
}

#define spidelay(x) ndelay(x)

#define	EXPAND_BITBANG_TXRX
#include <linux/spi/spi_bitbang.h>


static u32 s3c2410_spigpio_txrx_mode0(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 0, word, bits);
}

static u32 s3c2410_spigpio_txrx_mode1(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha1(spi, nsecs, 0, word, bits);
}

static u32 s3c2410_spigpio_txrx_mode2(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 1, word, bits);
}

static u32 s3c2410_spigpio_txrx_mode3(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha1(spi, nsecs, 1, word, bits);
}


static void s3c2410_spigpio_chipselect(struct spi_device *dev, int value)
{
	struct s3c2410_spigpio *sg = spidev_to_sg(dev);

	if (sg->info && sg->info->chip_select)
		(sg->info->chip_select)(sg->info, value);
}

static int s3c2410_spigpio_probe(struct platform_device *dev)
{
	struct s3c2410_spigpio_info *info;
	struct spi_master	*master;
	struct s3c2410_spigpio  *sp;
	int ret;

	master = spi_alloc_master(&dev->dev, sizeof(struct s3c2410_spigpio));
	if (master == NULL) {
		dev_err(&dev->dev, "failed to allocate spi master\n");
		ret = -ENOMEM;
		goto err;
	}

	sp = spi_master_get_devdata(master);

	platform_set_drvdata(dev, sp);

	/* copy in the plkatform data */
	info = sp->info = dev->dev.platform_data;

	/* setup spi bitbang adaptor */
	sp->bitbang.master = spi_master_get(master);
	sp->bitbang.master->bus_num = info->bus_num;
	sp->bitbang.master->num_chipselect = info->num_chipselect;
	sp->bitbang.chipselect = s3c2410_spigpio_chipselect;

	sp->bitbang.txrx_word[SPI_MODE_0] = s3c2410_spigpio_txrx_mode0;
	sp->bitbang.txrx_word[SPI_MODE_1] = s3c2410_spigpio_txrx_mode1;
	sp->bitbang.txrx_word[SPI_MODE_2] = s3c2410_spigpio_txrx_mode2;
	sp->bitbang.txrx_word[SPI_MODE_3] = s3c2410_spigpio_txrx_mode3;

	/* set state of spi pins, always assume that the clock is
	 * available, but do check the MOSI and MISO. */
	s3c2410_gpio_setpin(info->pin_clk, 0);
	s3c2410_gpio_cfgpin(info->pin_clk, S3C2410_GPIO_OUTPUT);

	if (info->pin_mosi < S3C2410_GPH10) {
		s3c2410_gpio_setpin(info->pin_mosi, 0);
		s3c2410_gpio_cfgpin(info->pin_mosi, S3C2410_GPIO_OUTPUT);
	}

	if (info->pin_miso != S3C2410_GPA0 && info->pin_miso < S3C2410_GPH10)
		s3c2410_gpio_cfgpin(info->pin_miso, S3C2410_GPIO_INPUT);

	ret = spi_bitbang_start(&sp->bitbang);
	if (ret)
		goto err_no_bitbang;

	return 0;

 err_no_bitbang:
	spi_master_put(sp->bitbang.master);
 err:
	return ret;

}

static int s3c2410_spigpio_remove(struct platform_device *dev)
{
	struct s3c2410_spigpio *sp = platform_get_drvdata(dev);

	spi_bitbang_stop(&sp->bitbang);
	spi_master_put(sp->bitbang.master);

	return 0;
}

/* all gpio should be held over suspend/resume, so we should
 * not need to deal with this
*/

#define s3c2410_spigpio_suspend NULL
#define s3c2410_spigpio_resume NULL

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:spi_s3c24xx_gpio");

static struct platform_driver s3c2410_spigpio_drv = {
	.probe		= s3c2410_spigpio_probe,
        .remove		= s3c2410_spigpio_remove,
        .suspend	= s3c2410_spigpio_suspend,
        .resume		= s3c2410_spigpio_resume,
        .driver		= {
		.name	= "spi_s3c24xx_gpio",
		.owner	= THIS_MODULE,
        },
};

static int __init s3c2410_spigpio_init(void)
{
        return platform_driver_register(&s3c2410_spigpio_drv);
}

static void __exit s3c2410_spigpio_exit(void)
{
        platform_driver_unregister(&s3c2410_spigpio_drv);
}

module_init(s3c2410_spigpio_init);
module_exit(s3c2410_spigpio_exit);

MODULE_DESCRIPTION("S3C24XX SPI Driver");
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
