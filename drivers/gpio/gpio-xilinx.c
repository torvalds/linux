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
#if defined(CONFIG_ARCH_ZYNQ) || defined(CONFIG_X86)
# define xgpio_readreg(offset)		readl(offset)
# define xgpio_writereg(offset, val)	writel(val, offset)
#else
# define xgpio_readreg(offset)		__raw_readl(offset)
# define xgpio_writereg(offset, val)	__raw_writel(val, offset)
#endif

/**
 * struct xgpio_instance - Stores information about GPIO device
 * @mmchip: OF GPIO chip for memory mapped banks
 * @gpio_state: GPIO state shadow register
 * @gpio_dir: GPIO direction shadow register
 * @gpio_lock: Lock used for synchronization
 * @inited: True if the port has been inited
 */
struct xgpio_instance {
	struct of_mm_gpio_chip mmchip;
	u32 gpio_state;
	u32 gpio_dir;
	spinlock_t gpio_lock;
	bool inited;
};

struct xgpio {
	struct xgpio_instance port[2];
};

/**
 * xgpio_get - Read the specified signal of the GPIO device.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * This function reads the specified signal of the GPIO device.
 *
 * Return:
 * 0 if direction of GPIO signals is set as input otherwise it
 * returns negative error value.
 */
static int xgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);

	return !!(xgpio_readreg(mm_gc->regs + XGPIO_DATA_OFFSET) & BIT(gpio));
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

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write to GPIO signal and set its direction to output */
	if (val)
		chip->gpio_state |= BIT(gpio);
	else
		chip->gpio_state &= ~BIT(gpio);

	xgpio_writereg(mm_gc->regs + XGPIO_DATA_OFFSET, chip->gpio_state);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

/**
 * xgpio_dir_in - Set the direction of the specified GPIO signal as input.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * Return:
 * 0 - if direction of GPIO signals is set as input
 * otherwise it returns negative error value.
 */
static int xgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long flags;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Set the GPIO bit in shadow register and set direction as input */
	chip->gpio_dir |= BIT(gpio);
	xgpio_writereg(mm_gc->regs + XGPIO_TRI_OFFSET, chip->gpio_dir);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

/**
 * xgpio_dir_out - Set the direction of the specified GPIO signal as output.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * This function sets the direction of specified GPIO signal as output.
 *
 * Return:
 * If all GPIO signals of GPIO chip is configured as input then it returns
 * error otherwise it returns 0.
 */
static int xgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long flags;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write state of GPIO signal */
	if (val)
		chip->gpio_state |= BIT(gpio);
	else
		chip->gpio_state &= ~BIT(gpio);
	xgpio_writereg(mm_gc->regs + XGPIO_DATA_OFFSET, chip->gpio_state);

	/* Clear the GPIO bit in shadow register and set direction as output */
	chip->gpio_dir &= ~BIT(gpio);
	xgpio_writereg(mm_gc->regs + XGPIO_TRI_OFFSET, chip->gpio_dir);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

/**
 * xgpio_save_regs - Set initial values of GPIO pins
 * @mm_gc: Pointer to memory mapped GPIO chip structure
 */
static void xgpio_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);

	xgpio_writereg(mm_gc->regs + XGPIO_DATA_OFFSET,	chip->gpio_state);
	xgpio_writereg(mm_gc->regs + XGPIO_TRI_OFFSET, chip->gpio_dir);
}

/**
 * xgpio_remove - Remove method for the GPIO device.
 * @pdev: pointer to the platform device
 *
 * This function remove gpiochips and frees all the allocated resources.
 */
static int xgpio_remove(struct platform_device *pdev)
{
	struct xgpio *xgpio = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < 2; i++) {
		if (!xgpio->port[i].inited)
			continue;
		gpiochip_remove(&xgpio->port[i].mmchip.gc);

		if (i == 1)
			xgpio->port[i].mmchip.regs -= XGPIO_CHANNEL_OFFSET;

		iounmap(xgpio->port[i].mmchip.regs);
		kfree(xgpio->port[i].mmchip.gc.label);
	}

	return 0;
}

/**
 * xgpio_of_probe - Probe method for the GPIO device.
 * @pdev: pointer to the platform device
 *
 * Return:
 * It returns 0, if the driver is bound to the GPIO device, or
 * a negative value if there is an error.
 */
static int xgpio_probe(struct platform_device *pdev)
{
	struct xgpio *xgpio;
	struct xgpio_instance *chip;
	int status = 0;
	struct device_node *np = pdev->dev.of_node;
	const u32 *tree_info;
	u32 ngpio;

	xgpio = devm_kzalloc(&pdev->dev, sizeof(*xgpio), GFP_KERNEL);
	if (!xgpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, xgpio);

	chip = &xgpio->port[0];

	/* Update GPIO state shadow register with default value */
	of_property_read_u32(np, "xlnx,dout-default", &chip->gpio_state);

	/* By default, all pins are inputs */
	chip->gpio_dir = 0xFFFFFFFF;

	/* Update GPIO direction shadow register with default value */
	of_property_read_u32(np, "xlnx,tri-default", &chip->gpio_dir);

	/*
	 * Check device node and parent device node for device width
	 * and assume default width of 32
	 */
	if (of_property_read_u32(np, "xlnx,gpio-width", &ngpio))
		ngpio = 32;
	chip->mmchip.gc.ngpio = (u16)ngpio;

	spin_lock_init(&chip->gpio_lock);

	chip->mmchip.gc.dev = &pdev->dev;
	chip->mmchip.gc.direction_input = xgpio_dir_in;
	chip->mmchip.gc.direction_output = xgpio_dir_out;
	chip->mmchip.gc.get = xgpio_get;
	chip->mmchip.gc.set = xgpio_set;

	chip->mmchip.save_regs = xgpio_save_regs;

	/* Call the OF gpio helper to setup and register the GPIO device */
	status = of_mm_gpiochip_add(np, &chip->mmchip);
	if (status) {
		pr_err("%s: error in probe function with status %d\n",
		       np->full_name, status);
		return status;
	}
	chip->inited = true;

	pr_info("XGpio: %s: registered, base is %d\n", np->full_name,
							chip->mmchip.gc.base);

	tree_info = of_get_property(np, "xlnx,is-dual", NULL);
	if (tree_info && be32_to_cpup(tree_info)) {
		chip = &xgpio->port[1];

		/* Update GPIO state shadow register with default value */
		of_property_read_u32(np, "xlnx,dout-default-2",
				     &chip->gpio_state);

		/* By default, all pins are inputs */
		chip->gpio_dir = 0xFFFFFFFF;

		/* Update GPIO direction shadow register with default value */
		of_property_read_u32(np, "xlnx,tri-default-2", &chip->gpio_dir);

		/*
		 * Check device node and parent device node for device width
		 * and assume default width of 32
		 */
		if (of_property_read_u32(np, "xlnx,gpio2-width", &ngpio))
			ngpio = 32;
		chip->mmchip.gc.ngpio = (u16)ngpio;

		spin_lock_init(&chip->gpio_lock);

		chip->mmchip.gc.dev = &pdev->dev;
		chip->mmchip.gc.direction_input = xgpio_dir_in;
		chip->mmchip.gc.direction_output = xgpio_dir_out;
		chip->mmchip.gc.get = xgpio_get;
		chip->mmchip.gc.set = xgpio_set;

		chip->mmchip.save_regs = xgpio_save_regs;

		/* Call the OF gpio helper to setup and register the GPIO dev */
		status = of_mm_gpiochip_add(np, &chip->mmchip);
		if (status) {
			xgpio_remove(pdev);
			pr_err("%s: error in probe function with status %d\n",
			np->full_name, status);
			return status;
		}

		/* Add dual channel offset */
		chip->mmchip.regs += XGPIO_CHANNEL_OFFSET;
		chip->inited = true;

		pr_info("XGpio: %s: dual channel registered, base is %d\n",
					np->full_name, chip->mmchip.gc.base);
	}

	return 0;
}

static const struct of_device_id xgpio_of_match[] = {
	{ .compatible = "xlnx,xps-gpio-1.00.a", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xgpio_of_match);

static struct platform_driver xgpio_plat_driver = {
	.probe		= xgpio_probe,
	.remove		= xgpio_remove,
	.driver		= {
			.name = "gpio-xilinx",
			.of_match_table	= xgpio_of_match,
	},
};

static int __init xgpio_init(void)
{
	return platform_driver_register(&xgpio_plat_driver);
}

subsys_initcall(xgpio_init);

static void __exit xgpio_exit(void)
{
	platform_driver_unregister(&xgpio_plat_driver);
}
module_exit(xgpio_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx GPIO driver");
MODULE_LICENSE("GPL");
