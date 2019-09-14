// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx gpio driver for xps/axi_gpio IP.
 *
 * Copyright 2008 - 2013 Xilinx, Inc.
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
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
 * @gc: GPIO chip
 * @regs: register block
 * @gpio_width: GPIO width for every channel
 * @gpio_state: GPIO state shadow register
 * @gpio_dir: GPIO direction shadow register
 * @gpio_lock: Lock used for synchronization
 */
struct xgpio_instance {
	struct gpio_chip gc;
	void __iomem *regs;
	unsigned int gpio_width[2];
	u32 gpio_state[2];
	u32 gpio_dir[2];
	spinlock_t gpio_lock[2];
};

static inline int xgpio_index(struct xgpio_instance *chip, int gpio)
{
	if (gpio >= chip->gpio_width[0])
		return 1;

	return 0;
}

static inline int xgpio_regoffset(struct xgpio_instance *chip, int gpio)
{
	if (xgpio_index(chip, gpio))
		return XGPIO_CHANNEL_OFFSET;

	return 0;
}

static inline int xgpio_offset(struct xgpio_instance *chip, int gpio)
{
	if (xgpio_index(chip, gpio))
		return gpio - chip->gpio_width[0];

	return gpio;
}

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
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	u32 val;

	val = xgpio_readreg(chip->regs + XGPIO_DATA_OFFSET +
			    xgpio_regoffset(chip, gpio));

	return !!(val & BIT(xgpio_offset(chip, gpio)));
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
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index =  xgpio_index(chip, gpio);
	int offset =  xgpio_offset(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Write to GPIO signal and set its direction to output */
	if (val)
		chip->gpio_state[index] |= BIT(offset);
	else
		chip->gpio_state[index] &= ~BIT(offset);

	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
		       xgpio_regoffset(chip, gpio), chip->gpio_state[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
}

/**
 * xgpio_set_multiple - Write the specified signals of the GPIO device.
 * @gc:     Pointer to gpio_chip device structure.
 * @mask:   Mask of the GPIOS to modify.
 * @bits:   Value to be wrote on each GPIO
 *
 * This function writes the specified values into the specified signals of the
 * GPIO devices.
 */
static void xgpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
			       unsigned long *bits)
{
	unsigned long flags;
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index = xgpio_index(chip, 0);
	int offset, i;

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Write to GPIO signals */
	for (i = 0; i < gc->ngpio; i++) {
		if (*mask == 0)
			break;
		if (index !=  xgpio_index(chip, i)) {
			xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
				       xgpio_regoffset(chip, i),
				       chip->gpio_state[index]);
			spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
			index =  xgpio_index(chip, i);
			spin_lock_irqsave(&chip->gpio_lock[index], flags);
		}
		if (__test_and_clear_bit(i, mask)) {
			offset =  xgpio_offset(chip, i);
			if (test_bit(i, bits))
				chip->gpio_state[index] |= BIT(offset);
			else
				chip->gpio_state[index] &= ~BIT(offset);
		}
	}

	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
		       xgpio_regoffset(chip, i), chip->gpio_state[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
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
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index =  xgpio_index(chip, gpio);
	int offset =  xgpio_offset(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Set the GPIO bit in shadow register and set direction as input */
	chip->gpio_dir[index] |= BIT(offset);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET +
		       xgpio_regoffset(chip, gpio), chip->gpio_dir[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);

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
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index =  xgpio_index(chip, gpio);
	int offset =  xgpio_offset(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Write state of GPIO signal */
	if (val)
		chip->gpio_state[index] |= BIT(offset);
	else
		chip->gpio_state[index] &= ~BIT(offset);
	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
			xgpio_regoffset(chip, gpio), chip->gpio_state[index]);

	/* Clear the GPIO bit in shadow register and set direction as output */
	chip->gpio_dir[index] &= ~BIT(offset);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET +
			xgpio_regoffset(chip, gpio), chip->gpio_dir[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);

	return 0;
}

/**
 * xgpio_save_regs - Set initial values of GPIO pins
 * @chip: Pointer to GPIO instance
 */
static void xgpio_save_regs(struct xgpio_instance *chip)
{
	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET,	chip->gpio_state[0]);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET, chip->gpio_dir[0]);

	if (!chip->gpio_width[1])
		return;

	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET + XGPIO_CHANNEL_OFFSET,
		       chip->gpio_state[1]);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET + XGPIO_CHANNEL_OFFSET,
		       chip->gpio_dir[1]);
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
	struct xgpio_instance *chip;
	int status = 0;
	struct device_node *np = pdev->dev.of_node;
	u32 is_dual;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	platform_set_drvdata(pdev, chip);

	/* Update GPIO state shadow register with default value */
	of_property_read_u32(np, "xlnx,dout-default", &chip->gpio_state[0]);

	/* Update GPIO direction shadow register with default value */
	if (of_property_read_u32(np, "xlnx,tri-default", &chip->gpio_dir[0]))
		chip->gpio_dir[0] = 0xFFFFFFFF;

	/*
	 * Check device node and parent device node for device width
	 * and assume default width of 32
	 */
	if (of_property_read_u32(np, "xlnx,gpio-width", &chip->gpio_width[0]))
		chip->gpio_width[0] = 32;

	spin_lock_init(&chip->gpio_lock[0]);

	if (of_property_read_u32(np, "xlnx,is-dual", &is_dual))
		is_dual = 0;

	if (is_dual) {
		/* Update GPIO state shadow register with default value */
		of_property_read_u32(np, "xlnx,dout-default-2",
				     &chip->gpio_state[1]);

		/* Update GPIO direction shadow register with default value */
		if (of_property_read_u32(np, "xlnx,tri-default-2",
					 &chip->gpio_dir[1]))
			chip->gpio_dir[1] = 0xFFFFFFFF;

		/*
		 * Check device node and parent device node for device width
		 * and assume default width of 32
		 */
		if (of_property_read_u32(np, "xlnx,gpio2-width",
					 &chip->gpio_width[1]))
			chip->gpio_width[1] = 32;

		spin_lock_init(&chip->gpio_lock[1]);
	}

	chip->gc.base = -1;
	chip->gc.ngpio = chip->gpio_width[0] + chip->gpio_width[1];
	chip->gc.parent = &pdev->dev;
	chip->gc.direction_input = xgpio_dir_in;
	chip->gc.direction_output = xgpio_dir_out;
	chip->gc.get = xgpio_get;
	chip->gc.set = xgpio_set;
	chip->gc.set_multiple = xgpio_set_multiple;

	chip->gc.label = dev_name(&pdev->dev);

	chip->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->regs)) {
		dev_err(&pdev->dev, "failed to ioremap memory resource\n");
		return PTR_ERR(chip->regs);
	}

	xgpio_save_regs(chip);

	status = devm_gpiochip_add_data(&pdev->dev, &chip->gc, chip);
	if (status) {
		dev_err(&pdev->dev, "failed to add GPIO chip\n");
		return status;
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
