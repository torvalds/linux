/*
 * Xilinx gpio driver for xps/axi_gpio IP.
 *
 * Copyright 2008 - 2013 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>

/* Register Offset Definitions */
#define XGPIO_DATA_OFFSET   (0x0)	/* Data register  */
#define XGPIO_TRI_OFFSET    (0x4)	/* I/O direction register  */

#define XGPIO_CHANNEL_OFFSET	0x8

/* Read/Write access to the GPIO registers */
#ifdef CONFIG_ARCH_ZYNQ
# define xgpio_readreg(offset)		readl(offset)
# define xgpio_writereg(offset, val)	writel(val, offset)
#else
# define xgpio_readreg(offset)		__raw_readl(offset)
# define xgpio_writereg(offset, val)	__raw_writel(val, offset)
#endif

/**
 * struct xgpio_instance - Stores information about GPIO device
 * struct of_mm_gpio_chip mmchip: OF GPIO chip for memory mapped banks
 * gpio_state: GPIO state shadow register
 * gpio_dir: GPIO direction shadow register
 * offset: GPIO channel offset
 * gpio_lock: Lock used for synchronization
 */
struct xgpio_instance {
	struct of_mm_gpio_chip mmchip;
	u32 gpio_state;
	u32 gpio_dir;
	u32 offset;
	spinlock_t gpio_lock;
};

/**
 * xgpio_get - Read the specified signal of the GPIO device.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * This function reads the specified signal of the GPIO device. It returns 0 if
 * the signal clear, 1 if signal is set or negative value on error.
 */
static int xgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);

	void __iomem *regs = mm_gc->regs + chip->offset;

	return !!(xgpio_readreg(regs + XGPIO_DATA_OFFSET) & BIT(gpio));
}

/**
 * xgpio_set - Write the specified signal of the GPIO device.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * This function writes the specified value in to the specified signal of the
 * GPIO device.
 */
static void xgpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long flags;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	void __iomem *regs = mm_gc->regs;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write to GPIO signal and set its direction to output */
	if (val)
		chip->gpio_state |= BIT(gpio);
	else
		chip->gpio_state &= ~BIT(gpio);

	xgpio_writereg(regs + chip->offset + XGPIO_DATA_OFFSET,
							 chip->gpio_state);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

/**
 * xgpio_dir_in - Set the direction of the specified GPIO signal as input.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * This function sets the direction of specified GPIO signal as input.
 * It returns 0 if direction of GPIO signals is set as input otherwise it
 * returns negative error value.
 */
static int xgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long flags;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	void __iomem *regs = mm_gc->regs;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Set the GPIO bit in shadow register and set direction as input */
	chip->gpio_dir |= BIT(gpio);
	xgpio_writereg(regs + chip->offset + XGPIO_TRI_OFFSET, chip->gpio_dir);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

/**
 * xgpio_dir_out - Set the direction of the specified GPIO signal as output.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * This function sets the direction of specified GPIO signal as output. If all
 * GPIO signals of GPIO chip is configured as input then it returns
 * error otherwise it returns 0.
 */
static int xgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long flags;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	void __iomem *regs = mm_gc->regs;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write state of GPIO signal */
	if (val)
		chip->gpio_state |= BIT(gpio);
	else
		chip->gpio_state &= ~BIT(gpio);
	xgpio_writereg(regs + chip->offset + XGPIO_DATA_OFFSET,
		       chip->gpio_state);

	/* Clear the GPIO bit in shadow register and set direction as output */
	chip->gpio_dir &= ~BIT(gpio);
	xgpio_writereg(regs + chip->offset + XGPIO_TRI_OFFSET, chip->gpio_dir);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

/**
 * xgpio_save_regs - Set initial values of GPIO pins
 * @mm_gc: pointer to memory mapped GPIO chip structure
 */
static void xgpio_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);

	xgpio_writereg(mm_gc->regs + chip->offset + XGPIO_DATA_OFFSET,
							chip->gpio_state);
	xgpio_writereg(mm_gc->regs + chip->offset + XGPIO_TRI_OFFSET,
							 chip->gpio_dir);
}

/**
 * xgpio_of_probe - Probe method for the GPIO device.
 * @np: pointer to device tree node
 *
 * This function probes the GPIO device in the device tree. It initializes the
 * driver data structure. It returns 0, if the driver is bound to the GPIO
 * device, or a negative value if there is an error.
 */
static int xgpio_of_probe(struct device_node *np)
{
	struct xgpio_instance *chip;
	int status = 0;
	const u32 *tree_info;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	/* Update GPIO state shadow register with default value */
	of_property_read_u32(np, "xlnx,dout-default", &chip->gpio_state);

	/* By default, all pins are inputs */
	chip->gpio_dir = 0xFFFFFFFF;

	/* Update GPIO direction shadow register with default value */
	of_property_read_u32(np, "xlnx,tri-default", &chip->gpio_dir);

	/* By default assume full GPIO controller */
	chip->mmchip.gc.ngpio = 32;

	/* Check device node and parent device node for device width */
	of_property_read_u32(np, "xlnx,gpio-width",
			      (u32 *)&chip->mmchip.gc.ngpio);

	spin_lock_init(&chip->gpio_lock);

	chip->mmchip.gc.direction_input = xgpio_dir_in;
	chip->mmchip.gc.direction_output = xgpio_dir_out;
	chip->mmchip.gc.get = xgpio_get;
	chip->mmchip.gc.set = xgpio_set;

	chip->mmchip.save_regs = xgpio_save_regs;

	/* Call the OF gpio helper to setup and register the GPIO device */
	status = of_mm_gpiochip_add(np, &chip->mmchip);
	if (status) {
		kfree(chip);
		pr_err("%s: error in probe function with status %d\n",
		       np->full_name, status);
		return status;
	}

	pr_info("XGpio: %s: registered, base is %d\n", np->full_name,
							chip->mmchip.gc.base);

	tree_info = of_get_property(np, "xlnx,is-dual", NULL);
	if (tree_info && be32_to_cpup(tree_info)) {
		chip = kzalloc(sizeof(*chip), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;

		/* Add dual channel offset */
		chip->offset = XGPIO_CHANNEL_OFFSET;

		/* Update GPIO state shadow register with default value */
		of_property_read_u32(np, "xlnx,dout-default-2",
				     &chip->gpio_state);

		/* By default, all pins are inputs */
		chip->gpio_dir = 0xFFFFFFFF;

		/* Update GPIO direction shadow register with default value */
		of_property_read_u32(np, "xlnx,tri-default-2", &chip->gpio_dir);

		/* By default assume full GPIO controller */
		chip->mmchip.gc.ngpio = 32;

		/* Check device node and parent device node for device width */
		of_property_read_u32(np, "xlnx,gpio2-width",
				     (u32 *)&chip->mmchip.gc.ngpio);

		spin_lock_init(&chip->gpio_lock);

		chip->mmchip.gc.direction_input = xgpio_dir_in;
		chip->mmchip.gc.direction_output = xgpio_dir_out;
		chip->mmchip.gc.get = xgpio_get;
		chip->mmchip.gc.set = xgpio_set;

		chip->mmchip.save_regs = xgpio_save_regs;

		/* Call the OF gpio helper to setup and register the GPIO dev */
		status = of_mm_gpiochip_add(np, &chip->mmchip);
		if (status) {
			kfree(chip);
			pr_err("%s: error in probe function with status %d\n",
			np->full_name, status);
			return status;
		}
		pr_info("XGpio: %s: dual channel registered, base is %d\n",
					np->full_name, chip->mmchip.gc.base);
	}

	return 0;
}

static struct of_device_id xgpio_of_match[] = {
	{ .compatible = "xlnx,xps-gpio-1.00.a", },
	{ /* end of list */ },
};

static int __init xgpio_init(void)
{
	struct device_node *np;

	for_each_matching_node(np, xgpio_of_match)
		xgpio_of_probe(np);

	return 0;
}

/* Make sure we get initialized before anyone else tries to use us */
subsys_initcall(xgpio_init);
/* No exit call at the moment as we cannot unregister of GPIO chips */

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx GPIO driver");
MODULE_LICENSE("GPL");
