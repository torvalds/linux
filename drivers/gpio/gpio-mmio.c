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
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/gpio/driver.h>

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
	if (gc->be_bits)
		return BIT(gc->bgpio_bits - 1 - line);
	return BIT(line);
}

static int bgpio_get_set(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long pinmask = bgpio_line2mask(gc, gpio);
	bool dir = !!(gc->bgpio_dir & pinmask);

	if (dir)
		return !!(gc->read_reg(gc->reg_set) & pinmask);
	else
		return !!(gc->read_reg(gc->reg_dat) & pinmask);
}

/*
 * This assumes that the bits in the GPIO register are in native endianness.
 * We only assign the function pointer if we have that.
 */
static int bgpio_get_set_multiple(struct gpio_chip *gc, unsigned long *mask,
				  unsigned long *bits)
{
	unsigned long get_mask = 0;
	unsigned long set_mask = 0;

	/* Make sure we first clear any bits that are zero when we read the register */
	*bits &= ~*mask;

	set_mask = *mask & gc->bgpio_dir;
	get_mask = *mask & ~gc->bgpio_dir;

	if (set_mask)
		*bits |= gc->read_reg(gc->reg_set) & set_mask;
	if (get_mask)
		*bits |= gc->read_reg(gc->reg_dat) & get_mask;

	return 0;
}

static int bgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	return !!(gc->read_reg(gc->reg_dat) & bgpio_line2mask(gc, gpio));
}

/*
 * This only works if the bits in the GPIO register are in native endianness.
 */
static int bgpio_get_multiple(struct gpio_chip *gc, unsigned long *mask,
			      unsigned long *bits)
{
	/* Make sure we first clear any bits that are zero when we read the register */
	*bits &= ~*mask;
	*bits |= gc->read_reg(gc->reg_dat) & *mask;
	return 0;
}

/*
 * With big endian mirrored bit order it becomes more tedious.
 */
static int bgpio_get_multiple_be(struct gpio_chip *gc, unsigned long *mask,
				 unsigned long *bits)
{
	unsigned long readmask = 0;
	unsigned long val;
	int bit;

	/* Make sure we first clear any bits that are zero when we read the register */
	*bits &= ~*mask;

	/* Create a mirrored mask */
	for_each_set_bit(bit, mask, gc->ngpio)
		readmask |= bgpio_line2mask(gc, bit);

	/* Read the register */
	val = gc->read_reg(gc->reg_dat) & readmask;

	/*
	 * Mirror the result into the "bits" result, this will give line 0
	 * in bit 0 ... line 31 in bit 31 for a 32bit register.
	 */
	for_each_set_bit(bit, &val, gc->ngpio)
		*bits |= bgpio_line2mask(gc, bit);

	return 0;
}

static void bgpio_set_none(struct gpio_chip *gc, unsigned int gpio, int val)
{
}

static void bgpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long mask = bgpio_line2mask(gc, gpio);
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);

	if (val)
		gc->bgpio_data |= mask;
	else
		gc->bgpio_data &= ~mask;

	gc->write_reg(gc->reg_dat, gc->bgpio_data);

	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void bgpio_set_with_clear(struct gpio_chip *gc, unsigned int gpio,
				 int val)
{
	unsigned long mask = bgpio_line2mask(gc, gpio);

	if (val)
		gc->write_reg(gc->reg_set, mask);
	else
		gc->write_reg(gc->reg_clr, mask);
}

static void bgpio_set_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long mask = bgpio_line2mask(gc, gpio);
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);

	if (val)
		gc->bgpio_data |= mask;
	else
		gc->bgpio_data &= ~mask;

	gc->write_reg(gc->reg_set, gc->bgpio_data);

	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void bgpio_multiple_get_masks(struct gpio_chip *gc,
				     unsigned long *mask, unsigned long *bits,
				     unsigned long *set_mask,
				     unsigned long *clear_mask)
{
	int i;

	*set_mask = 0;
	*clear_mask = 0;

	for_each_set_bit(i, mask, gc->bgpio_bits) {
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
	unsigned long flags;
	unsigned long set_mask, clear_mask;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);

	bgpio_multiple_get_masks(gc, mask, bits, &set_mask, &clear_mask);

	gc->bgpio_data |= set_mask;
	gc->bgpio_data &= ~clear_mask;

	gc->write_reg(reg, gc->bgpio_data);

	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void bgpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
			       unsigned long *bits)
{
	bgpio_set_multiple_single_reg(gc, mask, bits, gc->reg_dat);
}

static void bgpio_set_multiple_set(struct gpio_chip *gc, unsigned long *mask,
				   unsigned long *bits)
{
	bgpio_set_multiple_single_reg(gc, mask, bits, gc->reg_set);
}

static void bgpio_set_multiple_with_clear(struct gpio_chip *gc,
					  unsigned long *mask,
					  unsigned long *bits)
{
	unsigned long set_mask, clear_mask;

	bgpio_multiple_get_masks(gc, mask, bits, &set_mask, &clear_mask);

	if (set_mask)
		gc->write_reg(gc->reg_set, set_mask);
	if (clear_mask)
		gc->write_reg(gc->reg_clr, clear_mask);
}

static int bgpio_simple_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	return 0;
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

	return 0;
}

static int bgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);

	gc->bgpio_dir &= ~bgpio_line2mask(gc, gpio);

	if (gc->reg_dir_in)
		gc->write_reg(gc->reg_dir_in, ~gc->bgpio_dir);
	if (gc->reg_dir_out)
		gc->write_reg(gc->reg_dir_out, gc->bgpio_dir);

	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}

static int bgpio_get_dir(struct gpio_chip *gc, unsigned int gpio)
{
	/* Return 0 if output, 1 if input */
	if (gc->bgpio_dir_unreadable) {
		if (gc->bgpio_dir & bgpio_line2mask(gc, gpio))
			return GPIO_LINE_DIRECTION_OUT;
		return GPIO_LINE_DIRECTION_IN;
	}

	if (gc->reg_dir_out) {
		if (gc->read_reg(gc->reg_dir_out) & bgpio_line2mask(gc, gpio))
			return GPIO_LINE_DIRECTION_OUT;
		return GPIO_LINE_DIRECTION_IN;
	}

	if (gc->reg_dir_in)
		if (!(gc->read_reg(gc->reg_dir_in) & bgpio_line2mask(gc, gpio)))
			return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static void bgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);

	gc->bgpio_dir |= bgpio_line2mask(gc, gpio);

	if (gc->reg_dir_in)
		gc->write_reg(gc->reg_dir_in, ~gc->bgpio_dir);
	if (gc->reg_dir_out)
		gc->write_reg(gc->reg_dir_out, gc->bgpio_dir);

	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static int bgpio_dir_out_dir_first(struct gpio_chip *gc, unsigned int gpio,
				   int val)
{
	bgpio_dir_out(gc, gpio, val);
	gc->set(gc, gpio, val);
	return 0;
}

static int bgpio_dir_out_val_first(struct gpio_chip *gc, unsigned int gpio,
				   int val)
{
	gc->set(gc, gpio, val);
	bgpio_dir_out(gc, gpio, val);
	return 0;
}

static int bgpio_setup_accessors(struct device *dev,
				 struct gpio_chip *gc,
				 bool byte_be)
{

	switch (gc->bgpio_bits) {
	case 8:
		gc->read_reg	= bgpio_read8;
		gc->write_reg	= bgpio_write8;
		break;
	case 16:
		if (byte_be) {
			gc->read_reg	= bgpio_read16be;
			gc->write_reg	= bgpio_write16be;
		} else {
			gc->read_reg	= bgpio_read16;
			gc->write_reg	= bgpio_write16;
		}
		break;
	case 32:
		if (byte_be) {
			gc->read_reg	= bgpio_read32be;
			gc->write_reg	= bgpio_write32be;
		} else {
			gc->read_reg	= bgpio_read32;
			gc->write_reg	= bgpio_write32;
		}
		break;
#if BITS_PER_LONG >= 64
	case 64:
		if (byte_be) {
			dev_err(dev,
				"64 bit big endian byte order unsupported\n");
			return -EINVAL;
		} else {
			gc->read_reg	= bgpio_read64;
			gc->write_reg	= bgpio_write64;
		}
		break;
#endif /* BITS_PER_LONG >= 64 */
	default:
		dev_err(dev, "unsupported data width %u bits\n", gc->bgpio_bits);
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
static int bgpio_setup_io(struct gpio_chip *gc,
			  void __iomem *dat,
			  void __iomem *set,
			  void __iomem *clr,
			  unsigned long flags)
{

	gc->reg_dat = dat;
	if (!gc->reg_dat)
		return -EINVAL;

	if (set && clr) {
		gc->reg_set = set;
		gc->reg_clr = clr;
		gc->set = bgpio_set_with_clear;
		gc->set_multiple = bgpio_set_multiple_with_clear;
	} else if (set && !clr) {
		gc->reg_set = set;
		gc->set = bgpio_set_set;
		gc->set_multiple = bgpio_set_multiple_set;
	} else if (flags & BGPIOF_NO_OUTPUT) {
		gc->set = bgpio_set_none;
		gc->set_multiple = NULL;
	} else {
		gc->set = bgpio_set;
		gc->set_multiple = bgpio_set_multiple;
	}

	if (!(flags & BGPIOF_UNREADABLE_REG_SET) &&
	    (flags & BGPIOF_READ_OUTPUT_REG_SET)) {
		gc->get = bgpio_get_set;
		if (!gc->be_bits)
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
		if (gc->be_bits)
			gc->get_multiple = bgpio_get_multiple_be;
		else
			gc->get_multiple = bgpio_get_multiple;
	}

	return 0;
}

static int bgpio_setup_direction(struct gpio_chip *gc,
				 void __iomem *dirout,
				 void __iomem *dirin,
				 unsigned long flags)
{
	if (dirout || dirin) {
		gc->reg_dir_out = dirout;
		gc->reg_dir_in = dirin;
		if (flags & BGPIOF_NO_SET_ON_INPUT)
			gc->direction_output = bgpio_dir_out_dir_first;
		else
			gc->direction_output = bgpio_dir_out_val_first;
		gc->direction_input = bgpio_dir_in;
		gc->get_direction = bgpio_get_dir;
	} else {
		if (flags & BGPIOF_NO_OUTPUT)
			gc->direction_output = bgpio_dir_out_err;
		else
			gc->direction_output = bgpio_simple_dir_out;
		gc->direction_input = bgpio_simple_dir_in;
	}

	return 0;
}

static int bgpio_request(struct gpio_chip *chip, unsigned gpio_pin)
{
	if (gpio_pin < chip->ngpio)
		return 0;

	return -EINVAL;
}

/**
 * bgpio_init() - Initialize generic GPIO accessor functions
 * @gc: the GPIO chip to set up
 * @dev: the parent device of the new GPIO chip (compulsory)
 * @sz: the size (width) of the MMIO registers in bytes, typically 1, 2 or 4
 * @dat: MMIO address for the register to READ the value of the GPIO lines, it
 *	is expected that a 1 in the corresponding bit in this register means the
 *	line is asserted
 * @set: MMIO address for the register to SET the value of the GPIO lines, it is
 *	expected that we write the line with 1 in this register to drive the GPIO line
 *	high.
 * @clr: MMIO address for the register to CLEAR the value of the GPIO lines, it is
 *	expected that we write the line with 1 in this register to drive the GPIO line
 *	low. It is allowed to leave this address as NULL, in that case the SET register
 *	will be assumed to also clear the GPIO lines, by actively writing the line
 *	with 0.
 * @dirout: MMIO address for the register to set the line as OUTPUT. It is assumed
 *	that setting a line to 1 in this register will turn that line into an
 *	output line. Conversely, setting the line to 0 will turn that line into
 *	an input.
 * @dirin: MMIO address for the register to set this line as INPUT. It is assumed
 *	that setting a line to 1 in this register will turn that line into an
 *	input line. Conversely, setting the line to 0 will turn that line into
 *	an output.
 * @flags: Different flags that will affect the behaviour of the device, such as
 *	endianness etc.
 */
int bgpio_init(struct gpio_chip *gc, struct device *dev,
	       unsigned long sz, void __iomem *dat, void __iomem *set,
	       void __iomem *clr, void __iomem *dirout, void __iomem *dirin,
	       unsigned long flags)
{
	int ret;

	if (!is_power_of_2(sz))
		return -EINVAL;

	gc->bgpio_bits = sz * 8;
	if (gc->bgpio_bits > BITS_PER_LONG)
		return -EINVAL;

	raw_spin_lock_init(&gc->bgpio_lock);
	gc->parent = dev;
	gc->label = dev_name(dev);
	gc->base = -1;
	gc->request = bgpio_request;
	gc->be_bits = !!(flags & BGPIOF_BIG_ENDIAN);

	ret = gpiochip_get_ngpios(gc, dev);
	if (ret)
		gc->ngpio = gc->bgpio_bits;

	ret = bgpio_setup_io(gc, dat, set, clr, flags);
	if (ret)
		return ret;

	ret = bgpio_setup_accessors(dev, gc, flags & BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (ret)
		return ret;

	ret = bgpio_setup_direction(gc, dirout, dirin, flags);
	if (ret)
		return ret;

	gc->bgpio_data = gc->read_reg(gc->reg_dat);
	if (gc->set == bgpio_set_set &&
			!(flags & BGPIOF_UNREADABLE_REG_SET))
		gc->bgpio_data = gc->read_reg(gc->reg_set);

	if (flags & BGPIOF_UNREADABLE_REG_DIR)
		gc->bgpio_dir_unreadable = true;

	/*
	 * Inspect hardware to find initial direction setting.
	 */
	if ((gc->reg_dir_out || gc->reg_dir_in) &&
	    !(flags & BGPIOF_UNREADABLE_REG_DIR)) {
		if (gc->reg_dir_out)
			gc->bgpio_dir = gc->read_reg(gc->reg_dir_out);
		else if (gc->reg_dir_in)
			gc->bgpio_dir = ~gc->read_reg(gc->reg_dir_in);
		/*
		 * If we have two direction registers, synchronise
		 * input setting to output setting, the library
		 * can not handle a line being input and output at
		 * the same time.
		 */
		if (gc->reg_dir_out && gc->reg_dir_in)
			gc->write_reg(gc->reg_dir_in, ~gc->bgpio_dir);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(bgpio_init);

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
	{ }
};
MODULE_DEVICE_TABLE(of, bgpio_of_match);

static struct bgpio_pdata *bgpio_parse_fw(struct device *dev, unsigned long *flags)
{
	struct bgpio_pdata *pdata;

	if (!dev_fwnode(dev))
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->base = -1;

	if (device_is_big_endian(dev))
		*flags |= BGPIOF_BIG_ENDIAN_BYTE_ORDER;

	if (device_property_read_bool(dev, "no-output"))
		*flags |= BGPIOF_NO_OUTPUT;

	return pdata;
}

static int bgpio_pdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	void __iomem *dat;
	void __iomem *set;
	void __iomem *clr;
	void __iomem *dirout;
	void __iomem *dirin;
	unsigned long sz;
	unsigned long flags = 0;
	int err;
	struct gpio_chip *gc;
	struct bgpio_pdata *pdata;

	pdata = bgpio_parse_fw(dev, &flags);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	if (!pdata) {
		pdata = dev_get_platdata(dev);
		flags = pdev->id_entry->driver_data;
	}

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

	gc = devm_kzalloc(&pdev->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	err = bgpio_init(gc, dev, sz, dat, set, clr, dirout, dirin, flags);
	if (err)
		return err;

	if (pdata) {
		if (pdata->label)
			gc->label = pdata->label;
		gc->base = pdata->base;
		if (pdata->ngpio > 0)
			gc->ngpio = pdata->ngpio;
	}

	platform_set_drvdata(pdev, gc);

	return devm_gpiochip_add_data(&pdev->dev, gc, NULL);
}

static const struct platform_device_id bgpio_id_table[] = {
	{
		.name		= "basic-mmio-gpio",
		.driver_data	= 0,
	}, {
		.name		= "basic-mmio-gpio-be",
		.driver_data	= BGPIOF_BIG_ENDIAN,
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
