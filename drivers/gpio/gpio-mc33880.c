// SPDX-License-Identifier: GPL-2.0-only
/*
 * MC33880 high-side/low-side switch GPIO driver
 * Copyright (c) 2009 Intel Corporation
 */

/* Supports:
 * Freescale MC33880 high-side/low-side switch
 */

#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/spi/mc33880.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>
#include <linux/module.h>

#define DRIVER_NAME "mc33880"

/*
 * Pin configurations, see MAX7301 datasheet page 6
 */
#define PIN_CONFIG_MASK 0x03
#define PIN_CONFIG_IN_PULLUP 0x03
#define PIN_CONFIG_IN_WO_PULLUP 0x02
#define PIN_CONFIG_OUT 0x01

#define PIN_NUMBER 8


/*
 * Some registers must be read back to modify.
 * To save time we cache them here in memory
 */
struct mc33880 {
	struct mutex	lock;	/* protect from simultaneous accesses */
	u8		port_config;
	struct gpio_chip chip;
	struct spi_device *spi;
};

static int mc33880_write_config(struct mc33880 *mc)
{
	return spi_write(mc->spi, &mc->port_config, sizeof(mc->port_config));
}


static int __mc33880_set(struct mc33880 *mc, unsigned offset, int value)
{
	if (value)
		mc->port_config |= 1 << offset;
	else
		mc->port_config &= ~(1 << offset);

	return mc33880_write_config(mc);
}


static void mc33880_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mc33880 *mc = gpiochip_get_data(chip);

	mutex_lock(&mc->lock);

	__mc33880_set(mc, offset, value);

	mutex_unlock(&mc->lock);
}

static int mc33880_probe(struct spi_device *spi)
{
	struct mc33880 *mc;
	struct mc33880_platform_data *pdata;
	int ret;

	pdata = dev_get_platdata(&spi->dev);
	if (!pdata || !pdata->base) {
		dev_dbg(&spi->dev, "incorrect or missing platform data\n");
		return -EINVAL;
	}

	/*
	 * bits_per_word cannot be configured in platform data
	 */
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	mc = devm_kzalloc(&spi->dev, sizeof(struct mc33880), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	mutex_init(&mc->lock);

	spi_set_drvdata(spi, mc);

	mc->spi = spi;

	mc->chip.label = DRIVER_NAME;
	mc->chip.set = mc33880_set;
	mc->chip.base = pdata->base;
	mc->chip.ngpio = PIN_NUMBER;
	mc->chip.can_sleep = true;
	mc->chip.parent = &spi->dev;
	mc->chip.owner = THIS_MODULE;

	mc->port_config = 0x00;
	/* write twice, because during initialisation the first setting
	 * is just for testing SPI communication, and the second is the
	 * "real" configuration
	 */
	ret = mc33880_write_config(mc);
	mc->port_config = 0x00;
	if (!ret)
		ret = mc33880_write_config(mc);

	if (ret) {
		dev_err(&spi->dev, "Failed writing to " DRIVER_NAME ": %d\n",
			ret);
		goto exit_destroy;
	}

	ret = gpiochip_add_data(&mc->chip, mc);
	if (ret)
		goto exit_destroy;

	return ret;

exit_destroy:
	mutex_destroy(&mc->lock);
	return ret;
}

static void mc33880_remove(struct spi_device *spi)
{
	struct mc33880 *mc;

	mc = spi_get_drvdata(spi);

	gpiochip_remove(&mc->chip);
	mutex_destroy(&mc->lock);
}

static struct spi_driver mc33880_driver = {
	.driver = {
		.name		= DRIVER_NAME,
	},
	.probe		= mc33880_probe,
	.remove		= mc33880_remove,
};

static int __init mc33880_init(void)
{
	return spi_register_driver(&mc33880_driver);
}
/* register after spi postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(mc33880_init);

static void __exit mc33880_exit(void)
{
	spi_unregister_driver(&mc33880_driver);
}
module_exit(mc33880_exit);

MODULE_AUTHOR("Mocean Laboratories <info@mocean-labs.com>");
MODULE_DESCRIPTION("MC33880 high-side/low-side switch GPIO driver");
MODULE_LICENSE("GPL v2");

