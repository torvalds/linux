// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic driver for memory-mapped GPIO controllers.
 *
 * Copyright 2008 MontaVista Software, Inc.
 * Copyright 2008,2010 Anton Vorontsov <cbouatmailru@gmail.com>
 *
 * ....``.```~~~~````.`.`.`.`.```````'',,,.........`````......`.......
 * ...``                                                         ```````..
 * ..The simplest form of a GPIO controller that the driver supports is``
 *  `.just a single "data" register, where GPIO state can be read and/or `
 *    `,..written. ,,..``~~~~ .....``.`.`.~~.```.`.........``````.```````
 *        `````````
                                    ___
_/~~|___/~|   . ```~~~~~~       ___/___\___     ,~.`.`.`.`````.~~...,,,,...
__________|~$@~~~        %~    /o*o*o*o*o*o\   .. Implementing such a GPIO .
o        `                     ~~~~\___/~~~~    ` controller in FPGA is ,.`
                                                 `....trivial..'~`.```.```
 *                                                    ```````
 *  .```````~~~~`..`.``.``.
 * .  The driver supports  `...       ,..```.`~~~```````````````....````.``,,
 * .   big-endian notation, just`.  .. A bit more sophisticated controllers ,
 *  . register the device with -be`. .with a pair of set/clear-bit registers ,
 *   `.. suffix.  ```~~`````....`.`   . affecting the data register and the .`
 *     ``.`.``...```                  ```.. output pins are also supported.`
 *                        ^^             `````.`````````.,``~``~``~~``````
 *                                                   .                  ^^
 *   ,..`.`.`...````````````......`.`.`.`.`.`..`.`.`..
 * .. The expectation is that in at least some cases .    ,-~~~-,
 *  .this will be used with roll-your-own ASIC/FPGA .`     \   /
 *  .logic in Verilog or VHDL. ~~~`````````..`````~~`       \ /
 *  ..````````......```````````                             \o_
 *                                                           |
 *                              ^^                          / \
 *
 *           ...`````~~`.....``.`..........``````.`.``.```........``.
 *            `  8, 16, 32 and 64 bits registers are supported, and``.
 *            . the number of GPIOs is determined by the width of   ~
 *             .. the registers. ,............```.`.`..`.`.~~~.`.`.`~
 *               `.......````.```
 */

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/log2.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>

#include "gpiolib.h"

static void bgpio_write8(void __iomem *reg, unsigned long data)
{
	writeb(data, reg);
}

static unsigned long bgpio_read8(void __iomem *reg)
{
	return readb(reg);
}

static void bgpio_write16(void __iomem *reg, unsigned long data)
{
	writew(data, reg);
}

static unsigned long bgpio_read16(void __iomem *reg)
{
	return readw(reg);
}

static void bgpio_write32(void __iomem *reg, unsigned long data)
{
	writel(data, reg);
}

static unsigned long bgpio_read32(void __iomem *reg)
{
	return readl(reg);
}

#if BITS_PER_LONG >= 64
static void bgpio_write64(void __iomem *reg, unsigned long data)
{
	writeq(data, reg);
}

static unsigned long bgpio_read64(void __iomem *reg)
{
	return readq(reg);
}
#endif /* BITS_PER_LONG >= 64 */

static void bgpio_write16be(void __iomem *reg, unsigned long data)
{
	iowrite16be(data, reg);
}

static unsigned long bgpio_read16be(void __iomem *reg)
{
	return ioread16be(reg);
}

static void bgpio_write32be(void __iomem *reg, unsigned long data)
{
	iowrite32be(data, reg);
}

static unsigned long bgpio_read32be(void __iomem *reg)
{
	return ioread32be(reg);
}

static unsigned long bgpio_line2mask(struct gpio_chip *gc, unsigned int line)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	if (chip->be_bits)
		return BIT(chip->bits - 1 - line);
	return BIT(line);
}

static int bgpio_get_set(struct gpio_chip *gc, unsigned int gpio)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long pinmask = bgpio_line2mask(gc, gpio);
	bool dir = !!(chip->sdir & pinmask);

	if (dir)
		return !!(chip->read_reg(chip->reg_set) & pinmask);

	return !!(chip->read_reg(chip->reg_dat) & pinmask);
}

/*
 * This assumes that the bits in the GPIO register are in native endianness.
 * We only assign the function pointer if we have that.
 */
static int bgpio_get_set_multiple(struct gpio_chip *gc, unsigned long *mask,
				  unsigned long *bits)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long get_mask = 0, set_mask = 0;

	/* Make sure we first clear any bits that are zero when we read the register */
	*bits &= ~*mask;

	set_mask = *mask & chip->sdir;
	get_mask = *mask & ~chip->sdir;

	if (set_mask)
		*bits |= chip->read_reg(chip->reg_set) & set_mask;
	if (get_mask)
		*bits |= chip->read_reg(chip->reg_dat) & get_mask;

	return 0;
}

static int bgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	return !!(chip->read_reg(chip->reg_dat) & bgpio_line2mask(gc, gpio));
}

/*
 * This only works if the bits in the GPIO register are in native endianness.
 */
static int bgpio_get_multiple(struct gpio_chip *gc, unsigned long *mask,
			      unsigned long *bits)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	/* Make sure we first clear any bits that are zero when we read the register */
	*bits &= ~*mask;
	*bits |= chip->read_reg(chip->reg_dat) & *mask;
	return 0;
}

/*
 * With big endian mirrored bit order it becomes more tedious.
 */
static int bgpio_get_multiple_be(struct gpio_chip *gc, unsigned long *mask,
				 unsigned long *bits)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long readmask = 0;
	unsigned long val;
	int bit;

	/* Make sure we first clear any bits that are zero when we read the register */
	*bits &= ~*mask;

	/* Create a mirrored mask */
	for_each_set_bit(bit, mask, gc->ngpio)
		readmask |= bgpio_line2mask(gc, bit);

	/* Read the register */
	val = chip->read_reg(chip->reg_dat) & readmask;

	/*
	 * Mirror the result into the "bits" result, this will give line 0
	 * in bit 0 ... line 31 in bit 31 for a 32bit register.
	 */
	for_each_set_bit(bit, &val, gc->ngpio)
		*bits |= bgpio_line2mask(gc, bit);

	return 0;
}

static int bgpio_set_none(struct gpio_chip *gc, unsigned int gpio, int val)
{
	return 0;
}

static int bgpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long mask = bgpio_line2mask(gc, gpio);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);

	if (val)
		chip->sdata |= mask;
	else
		chip->sdata &= ~mask;

	chip->write_reg(chip->reg_dat, chip->sdata);

	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int bgpio_set_with_clear(struct gpio_chip *gc, unsigned int gpio,
				int val)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long mask = bgpio_line2mask(gc, gpio);

	if (val)
		chip->write_reg(chip->reg_set, mask);
	else
		chip->write_reg(chip->reg_clr, mask);

	return 0;
}

static int bgpio_set_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long mask = bgpio_line2mask(gc, gpio), flags;

	raw_spin_lock_irqsave(&chip->lock, flags);

	if (val)
		chip->sdata |= mask;
	else
		chip->sdata &= ~mask;

	chip->write_reg(chip->reg_set, chip->sdata);

	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static void bgpio_multiple_get_masks(struct gpio_chip *gc,
				     unsigned long *mask, unsigned long *bits,
				     unsigned long *set_mask,
				     unsigned long *clear_mask)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	int i;

	*set_mask = 0;
	*clear_mask = 0;

	for_each_set_bit(i, mask, chip->bits) {
		if (test_bit(i, bits))
			*set_mask |= bgpio_line2mask(gc, i);
		else
			*clear_mask |= bgpio_line2mask(gc, i);
	}
}

static void bgpio_set_multiple_single_reg(struct gpio_chip *gc,
					  unsigned long *mask,
					  unsigned long *bits,
					  void __iomem *reg)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long flags, set_mask, clear_mask;

	raw_spin_lock_irqsave(&chip->lock, flags);

	bgpio_multiple_get_masks(gc, mask, bits, &set_mask, &clear_mask);

	chip->sdata |= set_mask;
	chip->sdata &= ~clear_mask;

	chip->write_reg(reg, chip->sdata);

	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

static int bgpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
			       unsigned long *bits)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	bgpio_set_multiple_single_reg(gc, mask, bits, chip->reg_dat);

	return 0;
}

static int bgpio_set_multiple_set(struct gpio_chip *gc, unsigned long *mask,
				  unsigned long *bits)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	bgpio_set_multiple_single_reg(gc, mask, bits, chip->reg_set);

	return 0;
}

static int bgpio_set_multiple_with_clear(struct gpio_chip *gc,
					 unsigned long *mask,
					 unsigned long *bits)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long set_mask, clear_mask;

	bgpio_multiple_get_masks(gc, mask, bits, &set_mask, &clear_mask);

	if (set_mask)
		chip->write_reg(chip->reg_set, set_mask);
	if (clear_mask)
		chip->write_reg(chip->reg_clr, clear_mask);

	return 0;
}

static int bgpio_dir_return(struct gpio_chip *gc, unsigned int gpio, bool dir_out)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	if (!chip->pinctrl)
		return 0;

	if (dir_out)
		return pinctrl_gpio_direction_output(gc, gpio);
	else
		return pinctrl_gpio_direction_input(gc, gpio);
}

static int bgpio_dir_in_err(struct gpio_chip *gc, unsigned int gpio)
{
	return -EINVAL;
}

static int bgpio_simple_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	return bgpio_dir_return(gc, gpio, false);
}

static int bgpio_dir_out_err(struct gpio_chip *gc, unsigned int gpio,
				int val)
{
	return -EINVAL;
}

static int bgpio_simple_dir_out(struct gpio_chip *gc, unsigned int gpio,
				int val)
{
	gc->set(gc, gpio, val);

	return bgpio_dir_return(gc, gpio, true);
}

static int bgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);

	chip->sdir &= ~bgpio_line2mask(gc, gpio);

	if (chip->reg_dir_in)
		chip->write_reg(chip->reg_dir_in, ~chip->sdir);
	if (chip->reg_dir_out)
		chip->write_reg(chip->reg_dir_out, chip->sdir);

	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return bgpio_dir_return(gc, gpio, false);
}

static int bgpio_get_dir(struct gpio_chip *gc, unsigned int gpio)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	/* Return 0 if output, 1 if input */
	if (chip->dir_unreadable) {
		if (chip->sdir & bgpio_line2mask(gc, gpio))
			return GPIO_LINE_DIRECTION_OUT;
		return GPIO_LINE_DIRECTION_IN;
	}

	if (chip->reg_dir_out) {
		if (chip->read_reg(chip->reg_dir_out) & bgpio_line2mask(gc, gpio))
			return GPIO_LINE_DIRECTION_OUT;
		return GPIO_LINE_DIRECTION_IN;
	}

	if (chip->reg_dir_in)
		if (!(chip->read_reg(chip->reg_dir_in) & bgpio_line2mask(gc, gpio)))
			return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static void bgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);

	chip->sdir |= bgpio_line2mask(gc, gpio);

	if (chip->reg_dir_in)
		chip->write_reg(chip->reg_dir_in, ~chip->sdir);
	if (chip->reg_dir_out)
		chip->write_reg(chip->reg_dir_out, chip->sdir);

	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

static int bgpio_dir_out_dir_first(struct gpio_chip *gc, unsigned int gpio,
				   int val)
{
	bgpio_dir_out(gc, gpio, val);
	gc->set(gc, gpio, val);
	return bgpio_dir_return(gc, gpio, true);
}

static int bgpio_dir_out_val_first(struct gpio_chip *gc, unsigned int gpio,
				   int val)
{
	gc->set(gc, gpio, val);
	bgpio_dir_out(gc, gpio, val);
	return bgpio_dir_return(gc, gpio, true);
}

static int bgpio_setup_accessors(struct device *dev,
				 struct gpio_generic_chip *chip,
				 bool byte_be)
{
	switch (chip->bits) {
	case 8:
		chip->read_reg	= bgpio_read8;
		chip->write_reg	= bgpio_write8;
		break;
	case 16:
		if (byte_be) {
			chip->read_reg	= bgpio_read16be;
			chip->write_reg	= bgpio_write16be;
		} else {
			chip->read_reg	= bgpio_read16;
			chip->write_reg	= bgpio_write16;
		}
		break;
	case 32:
		if (byte_be) {
			chip->read_reg	= bgpio_read32be;
			chip->write_reg	= bgpio_write32be;
		} else {
			chip->read_reg	= bgpio_read32;
			chip->write_reg	= bgpio_write32;
		}
		break;
#if BITS_PER_LONG >= 64
	case 64:
		if (byte_be) {
			dev_err(dev,
				"64 bit big endian byte order unsupported\n");
			return -EINVAL;
		} else {
			chip->read_reg	= bgpio_read64;
			chip->write_reg	= bgpio_write64;
		}
		break;
#endif /* BITS_PER_LONG >= 64 */
	default:
		dev_err(dev, "unsupported data width %u bits\n", chip->bits);
		return -EINVAL;
	}

	return 0;
}

/*
 * Create the device and allocate the resources.  For setting GPIO's there are
 * three supported configurations:
 *
 *	- single input/output register resource (named "dat").
 *	- set/clear pair (named "set" and "clr").
 *	- single output register resource and single input resource ("set" and
 *	dat").
 *
 * For the single output register, this drives a 1 by setting a bit and a zero
 * by clearing a bit.  For the set clr pair, this drives a 1 by setting a bit
 * in the set register and clears it by setting a bit in the clear register.
 * The configuration is detected by which resources are present.
 *
 * For setting the GPIO direction, there are three supported configurations:
 *
 *	- simple bidirection GPIO that requires no configuration.
 *	- an output direction register (named "dirout") where a 1 bit
 *	indicates the GPIO is an output.
 *	- an input direction register (named "dirin") where a 1 bit indicates
 *	the GPIO is an input.
 */
static int bgpio_setup_io(struct gpio_generic_chip *chip,
			  const struct gpio_generic_chip_config *cfg)
{
	struct gpio_chip *gc = &chip->gc;

	chip->reg_dat = cfg->dat;
	if (!chip->reg_dat)
		return -EINVAL;

	if (cfg->set && cfg->clr) {
		chip->reg_set = cfg->set;
		chip->reg_clr = cfg->clr;
		gc->set = bgpio_set_with_clear;
		gc->set_multiple = bgpio_set_multiple_with_clear;
	} else if (cfg->set && !cfg->clr) {
		chip->reg_set = cfg->set;
		gc->set = bgpio_set_set;
		gc->set_multiple = bgpio_set_multiple_set;
	} else if (cfg->flags & GPIO_GENERIC_NO_OUTPUT) {
		gc->set = bgpio_set_none;
		gc->set_multiple = NULL;
	} else {
		gc->set = bgpio_set;
		gc->set_multiple = bgpio_set_multiple;
	}

	if (!(cfg->flags & GPIO_GENERIC_UNREADABLE_REG_SET) &&
	    (cfg->flags & GPIO_GENERIC_READ_OUTPUT_REG_SET)) {
		gc->get = bgpio_get_set;
		if (!chip->be_bits)
			gc->get_multiple = bgpio_get_set_multiple;
		/*
		 * We deliberately avoid assigning the ->get_multiple() call
		 * for big endian mirrored registers which are ALSO reflecting
		 * their value in the set register when used as output. It is
		 * simply too much complexity, let the GPIO core fall back to
		 * reading each line individually in that fringe case.
		 */
	} else {
		gc->get = bgpio_get;
		if (chip->be_bits)
			gc->get_multiple = bgpio_get_multiple_be;
		else
			gc->get_multiple = bgpio_get_multiple;
	}

	return 0;
}

static int bgpio_setup_direction(struct gpio_generic_chip *chip,
				 const struct gpio_generic_chip_config *cfg)
{
	struct gpio_chip *gc = &chip->gc;

	if (cfg->dirout || cfg->dirin) {
		chip->reg_dir_out = cfg->dirout;
		chip->reg_dir_in = cfg->dirin;
		if (cfg->flags & GPIO_GENERIC_NO_SET_ON_INPUT)
			gc->direction_output = bgpio_dir_out_dir_first;
		else
			gc->direction_output = bgpio_dir_out_val_first;
		gc->direction_input = bgpio_dir_in;
		gc->get_direction = bgpio_get_dir;
	} else {
		if (cfg->flags & GPIO_GENERIC_NO_OUTPUT)
			gc->direction_output = bgpio_dir_out_err;
		else
			gc->direction_output = bgpio_simple_dir_out;

		if (cfg->flags & GPIO_GENERIC_NO_INPUT)
			gc->direction_input = bgpio_dir_in_err;
		else
			gc->direction_input = bgpio_simple_dir_in;
	}

	return 0;
}

static int bgpio_request(struct gpio_chip *gc, unsigned int gpio_pin)
{
	struct gpio_generic_chip *chip = to_gpio_generic_chip(gc);

	if (gpio_pin >= gc->ngpio)
		return -EINVAL;

	if (chip->pinctrl)
		return gpiochip_generic_request(gc, gpio_pin);

	return 0;
}

/**
 * gpio_generic_chip_init() - Initialize a generic GPIO chip.
 * @chip: Generic GPIO chip to set up.
 * @cfg: Generic GPIO chip configuration.
 *
 * Returns 0 on success, negative error number on failure.
 */
int gpio_generic_chip_init(struct gpio_generic_chip *chip,
			   const struct gpio_generic_chip_config *cfg)
{
	struct gpio_chip *gc = &chip->gc;
	unsigned long flags = cfg->flags;
	struct device *dev = cfg->dev;
	int ret;

	if (!is_power_of_2(cfg->sz))
		return -EINVAL;

	chip->bits = cfg->sz * 8;
	if (chip->bits > BITS_PER_LONG)
		return -EINVAL;

	raw_spin_lock_init(&chip->lock);
	gc->parent = dev;
	gc->label = dev_name(dev);
	gc->base = -1;
	gc->request = bgpio_request;
	chip->be_bits = !!(flags & GPIO_GENERIC_BIG_ENDIAN);

	ret = gpiochip_get_ngpios(gc, dev);
	if (ret)
		gc->ngpio = chip->bits;

	ret = bgpio_setup_io(chip, cfg);
	if (ret)
		return ret;

	ret = bgpio_setup_accessors(dev, chip,
				    flags & GPIO_GENERIC_BIG_ENDIAN_BYTE_ORDER);
	if (ret)
		return ret;

	ret = bgpio_setup_direction(chip, cfg);
	if (ret)
		return ret;

	if (flags & GPIO_GENERIC_PINCTRL_BACKEND) {
		chip->pinctrl = true;
		/* Currently this callback is only used for pincontrol */
		gc->free = gpiochip_generic_free;
	}

	chip->sdata = chip->read_reg(chip->reg_dat);
	if (gc->set == bgpio_set_set &&
			!(flags & GPIO_GENERIC_UNREADABLE_REG_SET))
		chip->sdata = chip->read_reg(chip->reg_set);

	if (flags & GPIO_GENERIC_UNREADABLE_REG_DIR)
		chip->dir_unreadable = true;

	/*
	 * Inspect hardware to find initial direction setting.
	 */
	if ((chip->reg_dir_out || chip->reg_dir_in) &&
	    !(flags & GPIO_GENERIC_UNREADABLE_REG_DIR)) {
		if (chip->reg_dir_out)
			chip->sdir = chip->read_reg(chip->reg_dir_out);
		else if (chip->reg_dir_in)
			chip->sdir = ~chip->read_reg(chip->reg_dir_in);
		/*
		 * If we have two direction registers, synchronise
		 * input setting to output setting, the library
		 * can not handle a line being input and output at
		 * the same time.
		 */
		if (chip->reg_dir_out && chip->reg_dir_in)
			chip->write_reg(chip->reg_dir_in, ~chip->sdir);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gpio_generic_chip_init);

#if IS_ENABLED(CONFIG_GPIO_GENERIC_PLATFORM)

static void __iomem *bgpio_map(struct platform_device *pdev,
			       const char *name,
			       resource_size_t sane_sz)
{
	struct resource *r;
	resource_size_t sz;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!r)
		return NULL;

	sz = resource_size(r);
	if (sz != sane_sz)
		return IOMEM_ERR_PTR(-EINVAL);

	return devm_ioremap_resource(&pdev->dev, r);
}

static const struct of_device_id bgpio_of_match[] = {
	{ .compatible = "brcm,bcm6345-gpio" },
	{ .compatible = "wd,mbl-gpio" },
	{ .compatible = "ni,169445-nand-gpio" },
	{ .compatible = "intel,ixp4xx-expansion-bus-mmio-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, bgpio_of_match);

static int bgpio_pdev_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct gpio_generic_chip *gen_gc;
	struct device *dev = &pdev->dev;
	struct resource *r;
	void __iomem *dat;
	void __iomem *set;
	void __iomem *clr;
	void __iomem *dirout;
	void __iomem *dirin;
	unsigned long sz;
	unsigned long flags = 0;
	unsigned int base;
	int err;
	const char *label;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dat");
	if (!r)
		return -EINVAL;

	sz = resource_size(r);

	dat = bgpio_map(pdev, "dat", sz);
	if (IS_ERR(dat))
		return PTR_ERR(dat);

	set = bgpio_map(pdev, "set", sz);
	if (IS_ERR(set))
		return PTR_ERR(set);

	clr = bgpio_map(pdev, "clr", sz);
	if (IS_ERR(clr))
		return PTR_ERR(clr);

	dirout = bgpio_map(pdev, "dirout", sz);
	if (IS_ERR(dirout))
		return PTR_ERR(dirout);

	dirin = bgpio_map(pdev, "dirin", sz);
	if (IS_ERR(dirin))
		return PTR_ERR(dirin);

	gen_gc = devm_kzalloc(&pdev->dev, sizeof(*gen_gc), GFP_KERNEL);
	if (!gen_gc)
		return -ENOMEM;

	if (device_is_big_endian(dev))
		flags |= GPIO_GENERIC_BIG_ENDIAN_BYTE_ORDER;

	if (device_property_read_bool(dev, "no-output"))
		flags |= GPIO_GENERIC_NO_OUTPUT;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = sz,
		.dat = dat,
		.set = set,
		.clr = clr,
		.dirout = dirout,
		.dirin = dirin,
		.flags = flags,
	};

	err = gpio_generic_chip_init(gen_gc, &config);
	if (err)
		return err;

	err = device_property_read_string(dev, "label", &label);
	if (!err)
		gen_gc->gc.label = label;

	/*
	 * This property *must not* be used in device-tree sources, it's only
	 * meant to be passed to the driver from board files and MFD core.
	 */
	err = device_property_read_u32(dev, "gpio-mmio,base", &base);
	if (!err && base <= INT_MAX)
		gen_gc->gc.base = base;

	platform_set_drvdata(pdev, &gen_gc->gc);

	return devm_gpiochip_add_data(&pdev->dev, &gen_gc->gc, NULL);
}

static const struct platform_device_id bgpio_id_table[] = {
	{
		.name		= "basic-mmio-gpio",
		.driver_data	= 0,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, bgpio_id_table);

static struct platform_driver bgpio_driver = {
	.driver = {
		.name = "basic-mmio-gpio",
		.of_match_table = bgpio_of_match,
	},
	.id_table = bgpio_id_table,
	.probe = bgpio_pdev_probe,
};

module_platform_driver(bgpio_driver);

#endif /* CONFIG_GPIO_GENERIC_PLATFORM */

MODULE_DESCRIPTION("Driver for basic memory-mapped GPIO controllers");
MODULE_AUTHOR("Anton Vorontsov <cbouatmailru@gmail.com>");
MODULE_LICENSE("GPL");
