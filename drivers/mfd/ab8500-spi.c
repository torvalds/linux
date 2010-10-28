/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/mfd/ab8500.h>

/*
 * This funtion writes to any AB8500 registers using
 * SPI protocol &  before it writes it packs the data
 * in the below 24 bit frame format
 *
 *	 *|------------------------------------|
 *	 *| 23|22...18|17.......10|9|8|7......0|
 *	 *| r/w  bank       adr          data  |
 *	 * ------------------------------------
 *
 * This function shouldn't be called from interrupt
 * context
 */
static int ab8500_spi_write(struct ab8500 *ab8500, u16 addr, u8 data)
{
	struct spi_device *spi = container_of(ab8500->dev, struct spi_device,
					      dev);
	unsigned long spi_data = addr << 10 | data;
	struct spi_transfer xfer;
	struct spi_message msg;

	ab8500->tx_buf[0] = spi_data;
	ab8500->rx_buf[0] = 0;

	xfer.tx_buf	= ab8500->tx_buf;
	xfer.rx_buf	= NULL;
	xfer.len	= sizeof(unsigned long);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(spi, &msg);
}

static int ab8500_spi_read(struct ab8500 *ab8500, u16 addr)
{
	struct spi_device *spi = container_of(ab8500->dev, struct spi_device,
					      dev);
	unsigned long spi_data = 1 << 23 | addr << 10;
	struct spi_transfer xfer;
	struct spi_message msg;
	int ret;

	ab8500->tx_buf[0] = spi_data;
	ab8500->rx_buf[0] = 0;

	xfer.tx_buf	= ab8500->tx_buf;
	xfer.rx_buf	= ab8500->rx_buf;
	xfer.len	= sizeof(unsigned long);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(spi, &msg);
	if (!ret)
		/*
		 * Only the 8 lowermost bytes are
		 * defined with value, the rest may
		 * vary depending on chip/board noise.
		 */
		ret = ab8500->rx_buf[0] & 0xFFU;

	return ret;
}

static int __devinit ab8500_spi_probe(struct spi_device *spi)
{
	struct ab8500 *ab8500;
	int ret;

	spi->bits_per_word = 24;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	ab8500 = kzalloc(sizeof *ab8500, GFP_KERNEL);
	if (!ab8500)
		return -ENOMEM;

	ab8500->dev = &spi->dev;
	ab8500->irq = spi->irq;

	ab8500->read = ab8500_spi_read;
	ab8500->write = ab8500_spi_write;

	spi_set_drvdata(spi, ab8500);

	ret = ab8500_init(ab8500);
	if (ret)
		kfree(ab8500);

	return ret;
}

static int __devexit ab8500_spi_remove(struct spi_device *spi)
{
	struct ab8500 *ab8500 = spi_get_drvdata(spi);

	ab8500_exit(ab8500);
	kfree(ab8500);

	return 0;
}

static struct spi_driver ab8500_spi_driver = {
	.driver = {
		.name = "ab8500-spi",
		.owner = THIS_MODULE,
	},
	.probe	= ab8500_spi_probe,
	.remove	= __devexit_p(ab8500_spi_remove)
};

static int __init ab8500_spi_init(void)
{
	return spi_register_driver(&ab8500_spi_driver);
}
subsys_initcall(ab8500_spi_init);

static void __exit ab8500_spi_exit(void)
{
	spi_unregister_driver(&ab8500_spi_driver);
}
module_exit(ab8500_spi_exit);

MODULE_AUTHOR("Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com");
MODULE_DESCRIPTION("AB8500 SPI");
MODULE_LICENSE("GPL v2");
