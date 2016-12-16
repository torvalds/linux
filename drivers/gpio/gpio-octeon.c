/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011, 2012 Cavium Inc.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-gpio-defs.h>

#define RX_DAT 0x80
#define TX_SET 0x88
#define TX_CLEAR 0x90
/*
 * The address offset of the GPIO configuration register for a given
 * line.
 */
static unsigned int bit_cfg_reg(unsigned int offset)
{
	/*
	 * The register stride is 8, with a discontinuity after the
	 * first 16.
	 */
	if (offset < 16)
		return 8 * offset;
	else
		return 8 * (offset - 16) + 0x100;
}

struct octeon_gpio {
	struct gpio_chip chip;
	u64 register_base;
};

static int octeon_gpio_dir_in(struct gpio_chip *chip, unsigned offset)
{
	struct octeon_gpio *gpio = gpiochip_get_data(chip);

	cvmx_write_csr(gpio->register_base + bit_cfg_reg(offset), 0);
	return 0;
}

static void octeon_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct octeon_gpio *gpio = gpiochip_get_data(chip);
	u64 mask = 1ull << offset;
	u64 reg = gpio->register_base + (value ? TX_SET : TX_CLEAR);
	cvmx_write_csr(reg, mask);
}

static int octeon_gpio_dir_out(struct gpio_chip *chip, unsigned offset,
			       int value)
{
	struct octeon_gpio *gpio = gpiochip_get_data(chip);
	union cvmx_gpio_bit_cfgx cfgx;

	octeon_gpio_set(chip, offset, value);

	cfgx.u64 = 0;
	cfgx.s.tx_oe = 1;

	cvmx_write_csr(gpio->register_base + bit_cfg_reg(offset), cfgx.u64);
	return 0;
}

static int octeon_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct octeon_gpio *gpio = gpiochip_get_data(chip);
	u64 read_bits = cvmx_read_csr(gpio->register_base + RX_DAT);

	return ((1ull << offset) & read_bits) != 0;
}

static int octeon_gpio_probe(struct platform_device *pdev)
{
	struct octeon_gpio *gpio;
	struct gpio_chip *chip;
	struct resource *res_mem;
	void __iomem *reg_base;
	int err = 0;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;
	chip = &gpio->chip;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg_base = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	gpio->register_base = (u64)reg_base;
	pdev->dev.platform_data = chip;
	chip->label = "octeon-gpio";
	chip->parent = &pdev->dev;
	chip->owner = THIS_MODULE;
	chip->base = 0;
	chip->can_sleep = false;
	chip->ngpio = 20;
	chip->direction_input = octeon_gpio_dir_in;
	chip->get = octeon_gpio_get;
	chip->direction_output = octeon_gpio_dir_out;
	chip->set = octeon_gpio_set;
	err = devm_gpiochip_add_data(&pdev->dev, chip, gpio);
	if (err)
		return err;

	dev_info(&pdev->dev, "OCTEON GPIO driver probed.\n");
	return 0;
}

static const struct of_device_id octeon_gpio_match[] = {
	{
		.compatible = "cavium,octeon-3860-gpio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, octeon_gpio_match);

static struct platform_driver octeon_gpio_driver = {
	.driver = {
		.name		= "octeon_gpio",
		.of_match_table = octeon_gpio_match,
	},
	.probe		= octeon_gpio_probe,
};

module_platform_driver(octeon_gpio_driver);

MODULE_DESCRIPTION("Cavium Inc. OCTEON GPIO Driver");
MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL");
