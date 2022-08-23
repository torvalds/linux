// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the Diamond Systems GPIO-MM
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This driver supports the following Diamond Systems devices: GPIO-MM and
 * GPIO-MM-12.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "gpio-i8255.h"

MODULE_IMPORT_NS(I8255);

#define GPIOMM_EXTENT 8
#define MAX_NUM_GPIOMM max_num_isa_dev(GPIOMM_EXTENT)

static unsigned int base[MAX_NUM_GPIOMM];
static unsigned int num_gpiomm;
module_param_hw_array(base, uint, ioport, &num_gpiomm, 0);
MODULE_PARM_DESC(base, "Diamond Systems GPIO-MM base addresses");

#define GPIOMM_NUM_PPI 2

/**
 * struct gpiomm_gpio - GPIO device private data structure
 * @chip:		instance of the gpio_chip
 * @ppi_state:		Programmable Peripheral Interface group states
 * @ppi:		Programmable Peripheral Interface groups
 */
struct gpiomm_gpio {
	struct gpio_chip chip;
	struct i8255_state ppi_state[GPIOMM_NUM_PPI];
	struct i8255 __iomem *ppi;
};

static int gpiomm_gpio_get_direction(struct gpio_chip *chip,
	unsigned int offset)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);

	if (i8255_get_direction(gpiommgpio->ppi_state, offset))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int gpiomm_gpio_direction_input(struct gpio_chip *chip,
	unsigned int offset)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);

	i8255_direction_input(gpiommgpio->ppi, gpiommgpio->ppi_state, offset);

	return 0;
}

static int gpiomm_gpio_direction_output(struct gpio_chip *chip,
	unsigned int offset, int value)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);

	i8255_direction_output(gpiommgpio->ppi, gpiommgpio->ppi_state, offset,
			       value);

	return 0;
}

static int gpiomm_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);

	return i8255_get(gpiommgpio->ppi, offset);
}

static int gpiomm_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
	unsigned long *bits)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);

	i8255_get_multiple(gpiommgpio->ppi, mask, bits, chip->ngpio);

	return 0;
}

static void gpiomm_gpio_set(struct gpio_chip *chip, unsigned int offset,
	int value)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);

	i8255_set(gpiommgpio->ppi, gpiommgpio->ppi_state, offset, value);
}

static void gpiomm_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);

	i8255_set_multiple(gpiommgpio->ppi, gpiommgpio->ppi_state, mask, bits,
			   chip->ngpio);
}

#define GPIOMM_NGPIO 48
static const char *gpiomm_names[GPIOMM_NGPIO] = {
	"Port 1A0", "Port 1A1", "Port 1A2", "Port 1A3", "Port 1A4", "Port 1A5",
	"Port 1A6", "Port 1A7", "Port 1B0", "Port 1B1", "Port 1B2", "Port 1B3",
	"Port 1B4", "Port 1B5", "Port 1B6", "Port 1B7", "Port 1C0", "Port 1C1",
	"Port 1C2", "Port 1C3", "Port 1C4", "Port 1C5", "Port 1C6", "Port 1C7",
	"Port 2A0", "Port 2A1", "Port 2A2", "Port 2A3", "Port 2A4", "Port 2A5",
	"Port 2A6", "Port 2A7", "Port 2B0", "Port 2B1", "Port 2B2", "Port 2B3",
	"Port 2B4", "Port 2B5", "Port 2B6", "Port 2B7", "Port 2C0", "Port 2C1",
	"Port 2C2", "Port 2C3", "Port 2C4", "Port 2C5", "Port 2C6", "Port 2C7",
};

static void gpiomm_init_dio(struct i8255 __iomem *const ppi,
			    struct i8255_state *const ppi_state)
{
	const unsigned long ngpio = 24;
	const unsigned long mask = GENMASK(ngpio - 1, 0);
	const unsigned long bits = 0;
	unsigned long i;

	/* Initialize all GPIO to output 0 */
	for (i = 0; i < GPIOMM_NUM_PPI; i++) {
		i8255_mode0_output(&ppi[i]);
		i8255_set_multiple(&ppi[i], &ppi_state[i], &mask, &bits, ngpio);
	}
}

static int gpiomm_probe(struct device *dev, unsigned int id)
{
	struct gpiomm_gpio *gpiommgpio;
	const char *const name = dev_name(dev);
	int err;

	gpiommgpio = devm_kzalloc(dev, sizeof(*gpiommgpio), GFP_KERNEL);
	if (!gpiommgpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], GPIOMM_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + GPIOMM_EXTENT);
		return -EBUSY;
	}

	gpiommgpio->ppi = devm_ioport_map(dev, base[id], GPIOMM_EXTENT);
	if (!gpiommgpio->ppi)
		return -ENOMEM;

	gpiommgpio->chip.label = name;
	gpiommgpio->chip.parent = dev;
	gpiommgpio->chip.owner = THIS_MODULE;
	gpiommgpio->chip.base = -1;
	gpiommgpio->chip.ngpio = GPIOMM_NGPIO;
	gpiommgpio->chip.names = gpiomm_names;
	gpiommgpio->chip.get_direction = gpiomm_gpio_get_direction;
	gpiommgpio->chip.direction_input = gpiomm_gpio_direction_input;
	gpiommgpio->chip.direction_output = gpiomm_gpio_direction_output;
	gpiommgpio->chip.get = gpiomm_gpio_get;
	gpiommgpio->chip.get_multiple = gpiomm_gpio_get_multiple;
	gpiommgpio->chip.set = gpiomm_gpio_set;
	gpiommgpio->chip.set_multiple = gpiomm_gpio_set_multiple;

	i8255_state_init(gpiommgpio->ppi_state, GPIOMM_NUM_PPI);
	gpiomm_init_dio(gpiommgpio->ppi, gpiommgpio->ppi_state);

	err = devm_gpiochip_add_data(dev, &gpiommgpio->chip, gpiommgpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	return 0;
}

static struct isa_driver gpiomm_driver = {
	.probe = gpiomm_probe,
	.driver = {
		.name = "gpio-mm"
	},
};

module_isa_driver(gpiomm_driver, num_gpiomm);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Diamond Systems GPIO-MM GPIO driver");
MODULE_LICENSE("GPL v2");
