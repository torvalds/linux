// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for Exar XR17V35X chip
 *
 * Copyright (C) 2015 Sudip Mukherjee <sudip.mukherjee@codethink.co.uk>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define EXAR_OFFSET_MPIOLVL_LO 0x90
#define EXAR_OFFSET_MPIOSEL_LO 0x93
#define EXAR_OFFSET_MPIOLVL_HI 0x96
#define EXAR_OFFSET_MPIOSEL_HI 0x99

/*
 * The Device Configuration and UART Configuration Registers
 * for each UART channel take 1KB of memory address space.
 */
#define EXAR_UART_CHANNEL_SIZE 0x400

#define DRIVER_NAME "gpio_exar"

static DEFINE_IDA(ida_index);

struct exar_gpio_chip {
	struct gpio_chip gpio_chip;
	struct regmap *regmap;
	int index;
	char name[20];
	unsigned int first_pin;
	/*
	 * The offset to the cascaded device's (if existing)
	 * Device Configuration Registers.
	 */
	unsigned int cascaded_offset;
};

static unsigned int
exar_offset_to_sel_addr(struct exar_gpio_chip *exar_gpio, unsigned int offset)
{
	unsigned int pin = exar_gpio->first_pin + (offset % 16);
	unsigned int cascaded = offset / 16;
	unsigned int addr = pin / 8 ? EXAR_OFFSET_MPIOSEL_HI : EXAR_OFFSET_MPIOSEL_LO;

	return addr + (cascaded ? exar_gpio->cascaded_offset : 0);
}

static unsigned int
exar_offset_to_lvl_addr(struct exar_gpio_chip *exar_gpio, unsigned int offset)
{
	unsigned int pin = exar_gpio->first_pin + (offset % 16);
	unsigned int cascaded = offset / 16;
	unsigned int addr = pin / 8 ? EXAR_OFFSET_MPIOLVL_HI : EXAR_OFFSET_MPIOLVL_LO;

	return addr + (cascaded ? exar_gpio->cascaded_offset : 0);
}

static unsigned int
exar_offset_to_bit(struct exar_gpio_chip *exar_gpio, unsigned int offset)
{
	unsigned int pin = exar_gpio->first_pin + (offset % 16);

	return pin % 8;
}

static int exar_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct exar_gpio_chip *exar_gpio = gpiochip_get_data(chip);
	unsigned int addr = exar_offset_to_sel_addr(exar_gpio, offset);
	unsigned int bit = exar_offset_to_bit(exar_gpio, offset);

	if (regmap_test_bits(exar_gpio->regmap, addr, BIT(bit)))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int exar_get_value(struct gpio_chip *chip, unsigned int offset)
{
	struct exar_gpio_chip *exar_gpio = gpiochip_get_data(chip);
	unsigned int addr = exar_offset_to_lvl_addr(exar_gpio, offset);
	unsigned int bit = exar_offset_to_bit(exar_gpio, offset);

	return !!(regmap_test_bits(exar_gpio->regmap, addr, BIT(bit)));
}

static int exar_set_value(struct gpio_chip *chip, unsigned int offset,
			  int value)
{
	struct exar_gpio_chip *exar_gpio = gpiochip_get_data(chip);
	unsigned int addr = exar_offset_to_lvl_addr(exar_gpio, offset);
	unsigned int bit = exar_offset_to_bit(exar_gpio, offset);
	unsigned int bit_value = value ? BIT(bit) : 0;

	/*
	 * regmap_write_bits() forces value to be written when an external
	 * pull up/down might otherwise indicate value was already set.
	 */
	return regmap_write_bits(exar_gpio->regmap, addr, BIT(bit), bit_value);
}

static int exar_direction_output(struct gpio_chip *chip, unsigned int offset,
				 int value)
{
	struct exar_gpio_chip *exar_gpio = gpiochip_get_data(chip);
	unsigned int addr = exar_offset_to_sel_addr(exar_gpio, offset);
	unsigned int bit = exar_offset_to_bit(exar_gpio, offset);
	int ret;

	ret = exar_set_value(chip, offset, value);
	if (ret)
		return ret;

	return regmap_clear_bits(exar_gpio->regmap, addr, BIT(bit));
}

static int exar_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct exar_gpio_chip *exar_gpio = gpiochip_get_data(chip);
	unsigned int addr = exar_offset_to_sel_addr(exar_gpio, offset);
	unsigned int bit = exar_offset_to_bit(exar_gpio, offset);

	regmap_set_bits(exar_gpio->regmap, addr, BIT(bit));

	return 0;
}

static void exar_devm_ida_free(void *data)
{
	struct exar_gpio_chip *exar_gpio = data;

	ida_free(&ida_index, exar_gpio->index);
}

static const struct regmap_config exar_regmap_config = {
	.name		= "exar-gpio",
	.reg_bits	= 16,
	.val_bits	= 8,
	.io_port	= true,
};

static int gpio_exar_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pci_dev *pcidev = to_pci_dev(dev->parent);
	struct exar_gpio_chip *exar_gpio;
	u32 first_pin, ngpios;
	void __iomem *p;
	int index, ret;

	/*
	 * The UART driver must have mapped region 0 prior to registering this
	 * device - use it.
	 */
	p = pcim_iomap_table(pcidev)[0];
	if (!p)
		return -ENOMEM;

	ret = device_property_read_u32(dev, "exar,first-pin", &first_pin);
	if (ret)
		return ret;

	ret = device_property_read_u32(dev, "ngpios", &ngpios);
	if (ret)
		return ret;

	exar_gpio = devm_kzalloc(dev, sizeof(*exar_gpio), GFP_KERNEL);
	if (!exar_gpio)
		return -ENOMEM;

	/*
	 * If cascaded, secondary xr17v354 or xr17v358 have the same amount
	 * of MPIOs as their primaries and the last 4 bits of the primary's
	 * PCI Device ID is the number of its UART channels.
	 */
	if (pcidev->device & GENMASK(15, 12)) {
		ngpios += ngpios;
		exar_gpio->cascaded_offset = (pcidev->device & GENMASK(3, 0)) *
				EXAR_UART_CHANNEL_SIZE;
	}

	/*
	 * We don't need to check the return values of mmio regmap operations (unless
	 * the regmap has a clock attached which is not the case here).
	 */
	exar_gpio->regmap = devm_regmap_init_mmio(dev, p, &exar_regmap_config);
	if (IS_ERR(exar_gpio->regmap))
		return PTR_ERR(exar_gpio->regmap);

	index = ida_alloc(&ida_index, GFP_KERNEL);
	if (index < 0)
		return index;

	ret = devm_add_action_or_reset(dev, exar_devm_ida_free, exar_gpio);
	if (ret)
		return ret;

	sprintf(exar_gpio->name, "exar_gpio%d", index);
	exar_gpio->gpio_chip.label = exar_gpio->name;
	exar_gpio->gpio_chip.parent = dev;
	exar_gpio->gpio_chip.direction_output = exar_direction_output;
	exar_gpio->gpio_chip.direction_input = exar_direction_input;
	exar_gpio->gpio_chip.get_direction = exar_get_direction;
	exar_gpio->gpio_chip.get = exar_get_value;
	exar_gpio->gpio_chip.set = exar_set_value;
	exar_gpio->gpio_chip.base = -1;
	exar_gpio->gpio_chip.ngpio = ngpios;
	exar_gpio->index = index;
	exar_gpio->first_pin = first_pin;

	ret = devm_gpiochip_add_data(dev, &exar_gpio->gpio_chip, exar_gpio);
	if (ret)
		return ret;

	return 0;
}

static struct platform_driver gpio_exar_driver = {
	.probe	= gpio_exar_probe,
	.driver	= {
		.name = DRIVER_NAME,
	},
};

module_platform_driver(gpio_exar_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_DESCRIPTION("Exar GPIO driver");
MODULE_AUTHOR("Sudip Mukherjee <sudip.mukherjee@codethink.co.uk>");
MODULE_LICENSE("GPL");
