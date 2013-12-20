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
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/gpio.h>
#include <linux/slab.h>

/* Register Offset Definitions */
#define XGPIO_DATA_OFFSET	0x0 /* Data register */
#define XGPIO_TRI_OFFSET	0x4 /* I/O direction register */
#define XGPIO_GIER_OFFSET	0x11c /* Global Interrupt Enable */
#define XGPIO_GIER_IE		BIT(31)

#define XGPIO_IPISR_OFFSET	0x120 /* IP Interrupt Status */
#define XGPIO_IPIER_OFFSET	0x128 /* IP Interrupt Enable */

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
 * @mmchip: OF GPIO chip for memory mapped banks
 * @gpio_state: GPIO state shadow register
 * @gpio_dir: GPIO direction shadow register
 * @offset: GPIO channel offset
 * @irq_base: GPIO channel irq base address
 * @irq_enable: GPIO irq enable/disable bitfield
 * @gpio_lock: Lock used for synchronization
 * @irq_domain: irq_domain of the controller
 */
struct xgpio_instance {
	struct of_mm_gpio_chip mmchip;
	u32 gpio_state;
	u32 gpio_dir;
	u32 offset;
	int irq_base;
	u32 irq_enable;
	spinlock_t gpio_lock;
	struct irq_domain *irq_domain;
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
 * @mm_gc: Pointer to memory mapped GPIO chip structure
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
 * xgpio_xlate - Set initial values of GPIO pins
 * @gc: Pointer to gpio_chip device structure.
 * @gpiospec:  gpio specifier as found in the device tree
 * @flags: A flags pointer based on binding
 *
 * Return:
 * irq number otherwise -EINVAL
 */
static int xgpio_xlate(struct gpio_chip *gc,
		       const struct of_phandle_args *gpiospec, u32 *flags)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip = container_of(mm_gc, struct xgpio_instance,
						   mmchip);

	if (gpiospec->args[1] == chip->offset)
		return gpiospec->args[0];

	return -EINVAL;
}

/**
 * xgpio_irq_mask - Write the specified signal of the GPIO device.
 * @irq_data: per irq and chip data passed down to chip functions
 */
static void xgpio_irq_mask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	struct of_mm_gpio_chip *mm_gc = &chip->mmchip;
	u32 offset = irq_data->irq - chip->irq_base;
	u32 temp;

	pr_debug("%s: Disable %d irq, irq_enable_mask 0x%x\n",
		__func__, offset, chip->irq_enable);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	chip->irq_enable &= ~BIT(offset);

	if (!chip->irq_enable) {
		/* Enable per channel interrupt */
		temp = xgpio_readreg(mm_gc->regs + XGPIO_IPIER_OFFSET);
		temp &= chip->offset / XGPIO_CHANNEL_OFFSET + 1;
		xgpio_writereg(mm_gc->regs + XGPIO_IPIER_OFFSET, temp);

		/* Disable global interrupt if channel interrupts are unused */
		temp = xgpio_readreg(mm_gc->regs + XGPIO_IPIER_OFFSET);
		if (!temp)
			xgpio_writereg(mm_gc->regs + XGPIO_GIER_OFFSET,
				       ~XGPIO_GIER_IE);

	}
	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

/**
 * xgpio_irq_unmask - Write the specified signal of the GPIO device.
 * @irq_data: per irq and chip data passed down to chip functions
 */
static void xgpio_irq_unmask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	struct of_mm_gpio_chip *mm_gc = &chip->mmchip;
	u32 offset = irq_data->irq - chip->irq_base;
	u32 temp;

	pr_debug("%s: Enable %d irq, irq_enable_mask 0x%x\n",
		__func__, offset, chip->irq_enable);

	/* Setup pin as input */
	xgpio_dir_in(&mm_gc->gc, offset);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	chip->irq_enable |= BIT(offset);

	if (chip->irq_enable) {

		/* Enable per channel interrupt */
		temp = xgpio_readreg(mm_gc->regs + XGPIO_IPIER_OFFSET);
		temp |= chip->offset / XGPIO_CHANNEL_OFFSET + 1;
		xgpio_writereg(mm_gc->regs + XGPIO_IPIER_OFFSET, temp);

		/* Enable global interrupts */
		xgpio_writereg(mm_gc->regs + XGPIO_GIER_OFFSET, XGPIO_GIER_IE);
	}

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

/**
 * xgpio_set_irq_type - Write the specified signal of the GPIO device.
 * @irq_data: Per irq and chip data passed down to chip functions
 * @type: Interrupt type that is to be set for the gpio pin
 *
 * Return:
 * 0 if interrupt type is supported otherwise otherwise -EINVAL
 */
static int xgpio_set_irq_type(struct irq_data *irq_data, unsigned int type)
{
	/* Only rising edge case is supported now */
	if (type == IRQ_TYPE_EDGE_RISING)
		return 0;

	return -EINVAL;
}

/* irq chip descriptor */
static struct irq_chip xgpio_irqchip = {
	.name		= "xgpio",
	.irq_mask	= xgpio_irq_mask,
	.irq_unmask	= xgpio_irq_unmask,
	.irq_set_type	= xgpio_set_irq_type,
};

/**
 * xgpio_to_irq - Find out gpio to Linux irq mapping
 * @gc: Pointer to gpio_chip device structure.
 * @offset: Gpio pin offset
 *
 * Return:
 * irq number otherwise -EINVAL
 */
static int xgpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip = container_of(mm_gc, struct xgpio_instance,
						   mmchip);

	return irq_find_mapping(chip->irq_domain, offset);
}

/**
 * xgpio_irqhandler - Gpio interrupt service routine
 * @irq: gpio irq number
 * @desc: Pointer to interrupt description
 */
static void xgpio_irqhandler(unsigned int irq, struct irq_desc *desc)
{
	struct xgpio_instance *chip = (struct xgpio_instance *)
						irq_get_handler_data(irq);
	struct of_mm_gpio_chip *mm_gc = &chip->mmchip;
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	int offset;
	unsigned long val;

	chained_irq_enter(irqchip, desc);

	val = xgpio_readreg(mm_gc->regs + chip->offset);
	/* Only rising edge is supported */
	val &= chip->irq_enable;

	for_each_set_bit(offset, &val, chip->mmchip.gc.ngpio) {
		generic_handle_irq(chip->irq_base + offset);
	}

	xgpio_writereg(mm_gc->regs + XGPIO_IPISR_OFFSET,
		       chip->offset / XGPIO_CHANNEL_OFFSET + 1);

	chained_irq_exit(irqchip, desc);
}

static struct lock_class_key gpio_lock_class;

/**
 * xgpio_irq_setup - Allocate irq for gpio and setup appropriate functions
 * @np: Device node of the GPIO chip
 * @chip: Pointer to private gpio channel structure
 *
 * Return:
 * 0 if success, otherwise -1
 */
static int xgpio_irq_setup(struct device_node *np, struct xgpio_instance *chip)
{
	u32 pin_num;
	struct resource res;

	int ret = of_irq_to_resource(np, 0, &res);
	if (!ret) {
		pr_info("GPIO IRQ not connected\n");
		return 0;
	}

	chip->mmchip.gc.of_xlate = xgpio_xlate;
	chip->mmchip.gc.of_gpio_n_cells = 2;
	chip->mmchip.gc.to_irq = xgpio_to_irq;

	chip->irq_base = irq_alloc_descs(-1, 0, chip->mmchip.gc.ngpio, 0);
	if (chip->irq_base < 0) {
		pr_err("Couldn't allocate IRQ numbers\n");
		return -1;
	}
	chip->irq_domain = irq_domain_add_legacy(np, chip->mmchip.gc.ngpio,
						 chip->irq_base, 0,
						 &irq_domain_simple_ops, NULL);

	/*
	 * set the irq chip, handler and irq chip data for callbacks for
	 * each pin
	 */
	for (pin_num = 0; pin_num < chip->mmchip.gc.ngpio; pin_num++) {
		u32 gpio_irq = irq_find_mapping(chip->irq_domain, pin_num);
		irq_set_lockdep_class(gpio_irq, &gpio_lock_class);
		pr_debug("IRQ Base: %d, Pin %d = IRQ %d\n",
			chip->irq_base,	pin_num, gpio_irq);
		irq_set_chip_and_handler(gpio_irq, &xgpio_irqchip,
					 handle_simple_irq);
		irq_set_chip_data(gpio_irq, (void *)chip);
#ifdef CONFIG_ARCH_ZYNQ
		set_irq_flags(gpio_irq, IRQF_VALID);
#endif
	}
	irq_set_handler_data(res.start, (void *)chip);
	irq_set_chained_handler(res.start, xgpio_irqhandler);

	return 0;
}

/**
 * xgpio_of_probe - Probe method for the GPIO device.
 * @np: pointer to device tree node
 *
 * This function probes the GPIO device in the device tree. It initializes the
 * driver data structure.
 *
 * Return:
 * It returns 0, if the driver is bound to the GPIO device, or
 * a negative value if there is an error.
 */
static int xgpio_of_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct xgpio_instance *chip;
	int status = 0;
	const u32 *tree_info;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
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
		pr_err("%s: error in probe function with status %d\n",
		       np->full_name, status);
		return status;
	}

	status = xgpio_irq_setup(np, chip);
	if (status) {
		pr_err("%s: GPIO IRQ initialization failed %d\n",
		       np->full_name, status);
		return status;
	}

	pr_info("XGpio: %s: registered, base is %d\n", np->full_name,
							chip->mmchip.gc.base);

	tree_info = of_get_property(np, "xlnx,is-dual", NULL);
	if (tree_info && be32_to_cpup(tree_info)) {
		chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
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

		status = xgpio_irq_setup(np, chip);
		if (status) {
			pr_err("%s: GPIO IRQ initialization failed %d\n",
			      np->full_name, status);
			return status;
		}

		/* Call the OF gpio helper to setup and register the GPIO dev */
		status = of_mm_gpiochip_add(np, &chip->mmchip);
		if (status) {
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
MODULE_DEVICE_TABLE(of, xgpio_of_match);

static struct platform_driver xilinx_gpio_driver = {
	.probe = xgpio_of_probe,
	.driver = {
		.owner = THIS_MODULE,
		.name = "xilinx-gpio",
		.of_match_table = xgpio_of_match,
	},
};

static int __init xgpio_init(void)
{
	return platform_driver_register(&xilinx_gpio_driver);
}

/* Make sure we get initialized before anyone else tries to use us */
subsys_initcall(xgpio_init);
/* No exit call at the moment as we cannot unregister of GPIO chips */

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx GPIO driver");
MODULE_LICENSE("GPL");
