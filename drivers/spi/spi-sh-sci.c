// SPDX-License-Identifier: GPL-2.0-only
/*
 * SH SCI SPI interface
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * Based on S3C24XX GPIO based SPI driver, which is:
 *   Copyright (c) 2006 Ben Dooks
 *   Copyright (c) 2006 Simtec Electronics
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/module.h>

#include <asm/spi.h>
#include <asm/io.h>

struct sh_sci_spi {
	struct spi_bitbang bitbang;

	void __iomem *membase;
	unsigned char val;
	struct sh_spi_info *info;
	struct platform_device *dev;
};

#define SCSPTR(sp)	(sp->membase + 0x1c)
#define PIN_SCK		(1 << 2)
#define PIN_TXD		(1 << 0)
#define PIN_RXD		PIN_TXD
#define PIN_INIT	((1 << 1) | (1 << 3) | PIN_SCK | PIN_TXD)

static inline void setbits(struct sh_sci_spi *sp, int bits, int on)
{
	/*
	 * We are the only user of SCSPTR so no locking is required.
	 * Reading bit 2 and 0 in SCSPTR gives pin state as input.
	 * Writing the same bits sets the output value.
	 * This makes regular read-modify-write difficult so we
	 * use sp->val to keep track of the latest register value.
	 */

	if (on)
		sp->val |= bits;
	else
		sp->val &= ~bits;

	iowrite8(sp->val, SCSPTR(sp));
}

static inline void setsck(struct spi_device *dev, int on)
{
	setbits(spi_master_get_devdata(dev->master), PIN_SCK, on);
}

static inline void setmosi(struct spi_device *dev, int on)
{
	setbits(spi_master_get_devdata(dev->master), PIN_TXD, on);
}

static inline u32 getmiso(struct spi_device *dev)
{
	struct sh_sci_spi *sp = spi_master_get_devdata(dev->master);

	return (ioread8(SCSPTR(sp)) & PIN_RXD) ? 1 : 0;
}

#define spidelay(x) ndelay(x)

#include "spi-bitbang-txrx.h"

static u32 sh_sci_spi_txrx_mode0(struct spi_device *spi,
				 unsigned nsecs, u32 word, u8 bits,
				 unsigned flags)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 0, flags, word, bits);
}

static u32 sh_sci_spi_txrx_mode1(struct spi_device *spi,
				 unsigned nsecs, u32 word, u8 bits,
				 unsigned flags)
{
	return bitbang_txrx_be_cpha1(spi, nsecs, 0, flags, word, bits);
}

static u32 sh_sci_spi_txrx_mode2(struct spi_device *spi,
				 unsigned nsecs, u32 word, u8 bits,
				 unsigned flags)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 1, flags, word, bits);
}

static u32 sh_sci_spi_txrx_mode3(struct spi_device *spi,
				 unsigned nsecs, u32 word, u8 bits,
				 unsigned flags)
{
	return bitbang_txrx_be_cpha1(spi, nsecs, 1, flags, word, bits);
}

static void sh_sci_spi_chipselect(struct spi_device *dev, int value)
{
	struct sh_sci_spi *sp = spi_master_get_devdata(dev->master);

	if (sp->info->chip_select)
		(sp->info->chip_select)(sp->info, dev->chip_select, value);
}

static int sh_sci_spi_probe(struct platform_device *dev)
{
	struct resource	*r;
	struct spi_master *master;
	struct sh_sci_spi *sp;
	int ret;

	master = spi_alloc_master(&dev->dev, sizeof(struct sh_sci_spi));
	if (master == NULL) {
		dev_err(&dev->dev, "failed to allocate spi master\n");
		ret = -ENOMEM;
		goto err0;
	}

	sp = spi_master_get_devdata(master);

	platform_set_drvdata(dev, sp);
	sp->info = dev_get_platdata(&dev->dev);
	if (!sp->info) {
		dev_err(&dev->dev, "platform data is missing\n");
		ret = -ENOENT;
		goto err1;
	}

	/* setup spi bitbang adaptor */
	sp->bitbang.master = master;
	sp->bitbang.master->bus_num = sp->info->bus_num;
	sp->bitbang.master->num_chipselect = sp->info->num_chipselect;
	sp->bitbang.chipselect = sh_sci_spi_chipselect;

	sp->bitbang.txrx_word[SPI_MODE_0] = sh_sci_spi_txrx_mode0;
	sp->bitbang.txrx_word[SPI_MODE_1] = sh_sci_spi_txrx_mode1;
	sp->bitbang.txrx_word[SPI_MODE_2] = sh_sci_spi_txrx_mode2;
	sp->bitbang.txrx_word[SPI_MODE_3] = sh_sci_spi_txrx_mode3;

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		ret = -ENOENT;
		goto err1;
	}
	sp->membase = ioremap(r->start, resource_size(r));
	if (!sp->membase) {
		ret = -ENXIO;
		goto err1;
	}
	sp->val = ioread8(SCSPTR(sp));
	setbits(sp, PIN_INIT, 1);

	ret = spi_bitbang_start(&sp->bitbang);
	if (!ret)
		return 0;

	setbits(sp, PIN_INIT, 0);
	iounmap(sp->membase);
 err1:
	spi_master_put(sp->bitbang.master);
 err0:
	return ret;
}

static int sh_sci_spi_remove(struct platform_device *dev)
{
	struct sh_sci_spi *sp = platform_get_drvdata(dev);

	spi_bitbang_stop(&sp->bitbang);
	setbits(sp, PIN_INIT, 0);
	iounmap(sp->membase);
	spi_master_put(sp->bitbang.master);
	return 0;
}

static struct platform_driver sh_sci_spi_drv = {
	.probe		= sh_sci_spi_probe,
	.remove		= sh_sci_spi_remove,
	.driver		= {
		.name	= "spi_sh_sci",
	},
};
module_platform_driver(sh_sci_spi_drv);

MODULE_DESCRIPTION("SH SCI SPI Driver");
MODULE_AUTHOR("Magnus Damm <damm@opensource.se>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spi_sh_sci");
