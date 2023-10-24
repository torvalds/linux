// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx gpio driver for xps/axi_gpio IP.
 *
 * Copyright 2008 - 2013 Xilinx, Inc.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

/* Register Offset Definitions */
#define XGPIO_DATA_OFFSET   (0x0)	/* Data register  */
#define XGPIO_TRI_OFFSET    (0x4)	/* I/O direction register  */

#define XGPIO_CHANNEL0_OFFSET	0x0
#define XGPIO_CHANNEL1_OFFSET	0x8

#define XGPIO_GIER_OFFSET	0x11c /* Global Interrupt Enable */
#define XGPIO_GIER_IE		BIT(31)
#define XGPIO_IPISR_OFFSET	0x120 /* IP Interrupt Status */
#define XGPIO_IPIER_OFFSET	0x128 /* IP Interrupt Enable */

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
 * @hw_map: GPIO pin mapping on hardware side
 * @sw_map: GPIO pin mapping on software side
 * @state: GPIO write state shadow register
 * @last_irq_read: GPIO read state register from last interrupt
 * @dir: GPIO direction shadow register
 * @gpio_lock: Lock used for synchronization
 * @irq: IRQ used by GPIO device
 * @irqchip: IRQ chip
 * @enable: GPIO IRQ enable/disable bitfield
 * @rising_edge: GPIO IRQ rising edge enable/disable bitfield
 * @falling_edge: GPIO IRQ falling edge enable/disable bitfield
 * @clk: clock resource for this driver
 */
struct xgpio_instance {
	struct gpio_chip gc;
	void __iomem *regs;
	DECLARE_BITMAP(hw_map, 64);
	DECLARE_BITMAP(sw_map, 64);
	DECLARE_BITMAP(state, 64);
	DECLARE_BITMAP(last_irq_read, 64);
	DECLARE_BITMAP(dir, 64);
	spinlock_t gpio_lock;	/* For serializing operations */
	int irq;
	DECLARE_BITMAP(enable, 64);
	DECLARE_BITMAP(rising_edge, 64);
	DECLARE_BITMAP(falling_edge, 64);
	struct clk *clk;
};

static inline int xgpio_from_bit(struct xgpio_instance *chip, int bit)
{
	return bitmap_bitremap(bit, chip->hw_map, chip->sw_map, 64);
}

static inline int xgpio_to_bit(struct xgpio_instance *chip, int gpio)
{
	return bitmap_bitremap(gpio, chip->sw_map, chip->hw_map, 64);
}

static inline u32 xgpio_get_value32(const unsigned long *map, int bit)
{
	const size_t index = BIT_WORD(bit);
	const unsigned long offset = (bit % BITS_PER_LONG) & BIT(5);

	return (map[index] >> offset) & 0xFFFFFFFFul;
}

static inline void xgpio_set_value32(unsigned long *map, int bit, u32 v)
{
	const size_t index = BIT_WORD(bit);
	const unsigned long offset = (bit % BITS_PER_LONG) & BIT(5);

	map[index] &= ~(0xFFFFFFFFul << offset);
	map[index] |= (unsigned long)v << offset;
}

static inline int xgpio_regoffset(struct xgpio_instance *chip, int ch)
{
	switch (ch) {
	case 0:
		return XGPIO_CHANNEL0_OFFSET;
	case 1:
		return XGPIO_CHANNEL1_OFFSET;
	default:
		return -EINVAL;
	}
}

static void xgpio_read_ch(struct xgpio_instance *chip, int reg, int bit, unsigned long *a)
{
	void __iomem *addr = chip->regs + reg + xgpio_regoffset(chip, bit / 32);

	xgpio_set_value32(a, bit, xgpio_readreg(addr));
}

static void xgpio_write_ch(struct xgpio_instance *chip, int reg, int bit, unsigned long *a)
{
	void __iomem *addr = chip->regs + reg + xgpio_regoffset(chip, bit / 32);

	xgpio_writereg(addr, xgpio_get_value32(a, bit));
}

static void xgpio_read_ch_all(struct xgpio_instance *chip, int reg, unsigned long *a)
{
	int bit, lastbit = xgpio_to_bit(chip, chip->gc.ngpio - 1);

	for (bit = 0; bit <= lastbit ; bit += 32)
		xgpio_read_ch(chip, reg, bit, a);
}

static void xgpio_write_ch_all(struct xgpio_instance *chip, int reg, unsigned long *a)
{
	int bit, lastbit = xgpio_to_bit(chip, chip->gc.ngpio - 1);

	for (bit = 0; bit <= lastbit ; bit += 32)
		xgpio_write_ch(chip, reg, bit, a);
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
	int bit = xgpio_to_bit(chip, gpio);
	DECLARE_BITMAP(state, 64);

	xgpio_read_ch(chip, XGPIO_DATA_OFFSET, bit, state);

	return test_bit(bit, state);
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
	int bit = xgpio_to_bit(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write to GPIO signal and set its direction to output */
	__assign_bit(bit, chip->state, val);

	xgpio_write_ch(chip, XGPIO_DATA_OFFSET, bit, chip->state);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
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
	DECLARE_BITMAP(hw_mask, 64);
	DECLARE_BITMAP(hw_bits, 64);
	DECLARE_BITMAP(state, 64);
	unsigned long flags;
	struct xgpio_instance *chip = gpiochip_get_data(gc);

	bitmap_remap(hw_mask, mask, chip->sw_map, chip->hw_map, 64);
	bitmap_remap(hw_bits, bits, chip->sw_map, chip->hw_map, 64);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	bitmap_replace(state, chip->state, hw_bits, hw_mask, 64);

	xgpio_write_ch_all(chip, XGPIO_DATA_OFFSET, state);

	bitmap_copy(chip->state, state, 64);

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
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int bit = xgpio_to_bit(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Set the GPIO bit in shadow register and set direction as input */
	__set_bit(bit, chip->dir);
	xgpio_write_ch(chip, XGPIO_TRI_OFFSET, bit, chip->dir);

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
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int bit = xgpio_to_bit(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write state of GPIO signal */
	__assign_bit(bit, chip->state, val);
	xgpio_write_ch(chip, XGPIO_DATA_OFFSET, bit, chip->state);

	/* Clear the GPIO bit in shadow register and set direction as output */
	__clear_bit(bit, chip->dir);
	xgpio_write_ch(chip, XGPIO_TRI_OFFSET, bit, chip->dir);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

/**
 * xgpio_save_regs - Set initial values of GPIO pins
 * @chip: Pointer to GPIO instance
 */
static void xgpio_save_regs(struct xgpio_instance *chip)
{
	xgpio_write_ch_all(chip, XGPIO_DATA_OFFSET, chip->state);
	xgpio_write_ch_all(chip, XGPIO_TRI_OFFSET, chip->dir);
}

static int xgpio_request(struct gpio_chip *chip, unsigned int offset)
{
	int ret;

	ret = pm_runtime_get_sync(chip->parent);
	/*
	 * If the device is already active pm_runtime_get() will return 1 on
	 * success, but gpio_request still needs to return 0.
	 */
	return ret < 0 ? ret : 0;
}

static void xgpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pm_runtime_put(chip->parent);
}

static int __maybe_unused xgpio_suspend(struct device *dev)
{
	struct xgpio_instance *gpio = dev_get_drvdata(dev);
	struct irq_data *data = irq_get_irq_data(gpio->irq);

	if (!data) {
		dev_dbg(dev, "IRQ not connected\n");
		return pm_runtime_force_suspend(dev);
	}

	if (!irqd_is_wakeup_set(data))
		return pm_runtime_force_suspend(dev);

	return 0;
}

/**
 * xgpio_remove - Remove method for the GPIO device.
 * @pdev: pointer to the platform device
 *
 * This function remove gpiochips and frees all the allocated resources.
 *
 * Return: 0 always
 */
static int xgpio_remove(struct platform_device *pdev)
{
	struct xgpio_instance *gpio = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(gpio->clk);

	return 0;
}

/**
 * xgpio_irq_ack - Acknowledge a child GPIO interrupt.
 * @irq_data: per IRQ and chip data passed down to chip functions
 * This currently does nothing, but irq_ack is unconditionally called by
 * handle_edge_irq and therefore must be defined.
 */
static void xgpio_irq_ack(struct irq_data *irq_data)
{
}

static int __maybe_unused xgpio_resume(struct device *dev)
{
	struct xgpio_instance *gpio = dev_get_drvdata(dev);
	struct irq_data *data = irq_get_irq_data(gpio->irq);

	if (!data) {
		dev_dbg(dev, "IRQ not connected\n");
		return pm_runtime_force_resume(dev);
	}

	if (!irqd_is_wakeup_set(data))
		return pm_runtime_force_resume(dev);

	return 0;
}

static int __maybe_unused xgpio_runtime_suspend(struct device *dev)
{
	struct xgpio_instance *gpio = dev_get_drvdata(dev);

	clk_disable(gpio->clk);

	return 0;
}

static int __maybe_unused xgpio_runtime_resume(struct device *dev)
{
	struct xgpio_instance *gpio = dev_get_drvdata(dev);

	return clk_enable(gpio->clk);
}

static const struct dev_pm_ops xgpio_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xgpio_suspend, xgpio_resume)
	SET_RUNTIME_PM_OPS(xgpio_runtime_suspend,
			   xgpio_runtime_resume, NULL)
};

/**
 * xgpio_irq_mask - Write the specified signal of the GPIO device.
 * @irq_data: per IRQ and chip data passed down to chip functions
 */
static void xgpio_irq_mask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	int irq_offset = irqd_to_hwirq(irq_data);
	int bit = xgpio_to_bit(chip, irq_offset);
	u32 mask = BIT(bit / 32), temp;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	__clear_bit(bit, chip->enable);

	if (xgpio_get_value32(chip->enable, bit) == 0) {
		/* Disable per channel interrupt */
		temp = xgpio_readreg(chip->regs + XGPIO_IPIER_OFFSET);
		temp &= ~mask;
		xgpio_writereg(chip->regs + XGPIO_IPIER_OFFSET, temp);
	}
	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	gpiochip_disable_irq(&chip->gc, irq_offset);
}

/**
 * xgpio_irq_unmask - Write the specified signal of the GPIO device.
 * @irq_data: per IRQ and chip data passed down to chip functions
 */
static void xgpio_irq_unmask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	int irq_offset = irqd_to_hwirq(irq_data);
	int bit = xgpio_to_bit(chip, irq_offset);
	u32 old_enable = xgpio_get_value32(chip->enable, bit);
	u32 mask = BIT(bit / 32), val;

	gpiochip_enable_irq(&chip->gc, irq_offset);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	__set_bit(bit, chip->enable);

	if (old_enable == 0) {
		/* Clear any existing per-channel interrupts */
		val = xgpio_readreg(chip->regs + XGPIO_IPISR_OFFSET);
		val &= mask;
		xgpio_writereg(chip->regs + XGPIO_IPISR_OFFSET, val);

		/* Update GPIO IRQ read data before enabling interrupt*/
		xgpio_read_ch(chip, XGPIO_DATA_OFFSET, bit, chip->last_irq_read);

		/* Enable per channel interrupt */
		val = xgpio_readreg(chip->regs + XGPIO_IPIER_OFFSET);
		val |= mask;
		xgpio_writereg(chip->regs + XGPIO_IPIER_OFFSET, val);
	}

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

/**
 * xgpio_set_irq_type - Write the specified signal of the GPIO device.
 * @irq_data: Per IRQ and chip data passed down to chip functions
 * @type: Interrupt type that is to be set for the gpio pin
 *
 * Return:
 * 0 if interrupt type is supported otherwise -EINVAL
 */
static int xgpio_set_irq_type(struct irq_data *irq_data, unsigned int type)
{
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	int irq_offset = irqd_to_hwirq(irq_data);
	int bit = xgpio_to_bit(chip, irq_offset);

	/*
	 * The Xilinx GPIO hardware provides a single interrupt status
	 * indication for any state change in a given GPIO channel (bank).
	 * Therefore, only rising edge or falling edge triggers are
	 * supported.
	 */
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		__set_bit(bit, chip->rising_edge);
		__set_bit(bit, chip->falling_edge);
		break;
	case IRQ_TYPE_EDGE_RISING:
		__set_bit(bit, chip->rising_edge);
		__clear_bit(bit, chip->falling_edge);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		__clear_bit(bit, chip->rising_edge);
		__set_bit(bit, chip->falling_edge);
		break;
	default:
		return -EINVAL;
	}

	irq_set_handler_locked(irq_data, handle_edge_irq);
	return 0;
}

/**
 * xgpio_irqhandler - Gpio interrupt service routine
 * @desc: Pointer to interrupt description
 */
static void xgpio_irqhandler(struct irq_desc *desc)
{
	struct xgpio_instance *chip = irq_desc_get_handler_data(desc);
	struct gpio_chip *gc = &chip->gc;
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	DECLARE_BITMAP(rising, 64);
	DECLARE_BITMAP(falling, 64);
	DECLARE_BITMAP(all, 64);
	int irq_offset;
	u32 status;
	u32 bit;

	status = xgpio_readreg(chip->regs + XGPIO_IPISR_OFFSET);
	xgpio_writereg(chip->regs + XGPIO_IPISR_OFFSET, status);

	chained_irq_enter(irqchip, desc);

	spin_lock(&chip->gpio_lock);

	xgpio_read_ch_all(chip, XGPIO_DATA_OFFSET, all);

	bitmap_complement(rising, chip->last_irq_read, 64);
	bitmap_and(rising, rising, all, 64);
	bitmap_and(rising, rising, chip->enable, 64);
	bitmap_and(rising, rising, chip->rising_edge, 64);

	bitmap_complement(falling, all, 64);
	bitmap_and(falling, falling, chip->last_irq_read, 64);
	bitmap_and(falling, falling, chip->enable, 64);
	bitmap_and(falling, falling, chip->falling_edge, 64);

	bitmap_copy(chip->last_irq_read, all, 64);
	bitmap_or(all, rising, falling, 64);

	spin_unlock(&chip->gpio_lock);

	dev_dbg(gc->parent, "IRQ rising %*pb falling %*pb\n", 64, rising, 64, falling);

	for_each_set_bit(bit, all, 64) {
		irq_offset = xgpio_from_bit(chip, bit);
		generic_handle_domain_irq(gc->irq.domain, irq_offset);
	}

	chained_irq_exit(irqchip, desc);
}

static const struct irq_chip xgpio_irq_chip = {
	.name = "gpio-xilinx",
	.irq_ack = xgpio_irq_ack,
	.irq_mask = xgpio_irq_mask,
	.irq_unmask = xgpio_irq_unmask,
	.irq_set_type = xgpio_set_irq_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

/**
 * xgpio_probe - Probe method for the GPIO device.
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
	u32 is_dual = 0;
	u32 width[2];
	u32 state[2];
	u32 dir[2];
	struct gpio_irq_chip *girq;
	u32 temp;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	platform_set_drvdata(pdev, chip);

	/* First, check if the device is dual-channel */
	of_property_read_u32(np, "xlnx,is-dual", &is_dual);

	/* Setup defaults */
	memset32(width, 0, ARRAY_SIZE(width));
	memset32(state, 0, ARRAY_SIZE(state));
	memset32(dir, 0xFFFFFFFF, ARRAY_SIZE(dir));

	/* Update GPIO state shadow register with default value */
	of_property_read_u32(np, "xlnx,dout-default", &state[0]);
	of_property_read_u32(np, "xlnx,dout-default-2", &state[1]);

	bitmap_from_arr32(chip->state, state, 64);

	/* Update GPIO direction shadow register with default value */
	of_property_read_u32(np, "xlnx,tri-default", &dir[0]);
	of_property_read_u32(np, "xlnx,tri-default-2", &dir[1]);

	bitmap_from_arr32(chip->dir, dir, 64);

	/*
	 * Check device node and parent device node for device width
	 * and assume default width of 32
	 */
	if (of_property_read_u32(np, "xlnx,gpio-width", &width[0]))
		width[0] = 32;

	if (width[0] > 32)
		return -EINVAL;

	if (is_dual && of_property_read_u32(np, "xlnx,gpio2-width", &width[1]))
		width[1] = 32;

	if (width[1] > 32)
		return -EINVAL;

	/* Setup software pin mapping */
	bitmap_set(chip->sw_map, 0, width[0] + width[1]);

	/* Setup hardware pin mapping */
	bitmap_set(chip->hw_map,  0, width[0]);
	bitmap_set(chip->hw_map, 32, width[1]);

	spin_lock_init(&chip->gpio_lock);

	chip->gc.base = -1;
	chip->gc.ngpio = bitmap_weight(chip->hw_map, 64);
	chip->gc.parent = &pdev->dev;
	chip->gc.direction_input = xgpio_dir_in;
	chip->gc.direction_output = xgpio_dir_out;
	chip->gc.get = xgpio_get;
	chip->gc.set = xgpio_set;
	chip->gc.request = xgpio_request;
	chip->gc.free = xgpio_free;
	chip->gc.set_multiple = xgpio_set_multiple;

	chip->gc.label = dev_name(&pdev->dev);

	chip->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->regs)) {
		dev_err(&pdev->dev, "failed to ioremap memory resource\n");
		return PTR_ERR(chip->regs);
	}

	chip->clk = devm_clk_get_optional(&pdev->dev, NULL);
	if (IS_ERR(chip->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(chip->clk), "input clock not found.\n");

	status = clk_prepare_enable(chip->clk);
	if (status < 0) {
		dev_err(&pdev->dev, "Failed to prepare clk\n");
		return status;
	}
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	xgpio_save_regs(chip);

	chip->irq = platform_get_irq_optional(pdev, 0);
	if (chip->irq <= 0)
		goto skip_irq;

	/* Disable per-channel interrupts */
	xgpio_writereg(chip->regs + XGPIO_IPIER_OFFSET, 0);
	/* Clear any existing per-channel interrupts */
	temp = xgpio_readreg(chip->regs + XGPIO_IPISR_OFFSET);
	xgpio_writereg(chip->regs + XGPIO_IPISR_OFFSET, temp);
	/* Enable global interrupts */
	xgpio_writereg(chip->regs + XGPIO_GIER_OFFSET, XGPIO_GIER_IE);

	girq = &chip->gc.irq;
	gpio_irq_chip_set_chip(girq, &xgpio_irq_chip);
	girq->parent_handler = xgpio_irqhandler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&pdev->dev, 1,
				     sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents) {
		status = -ENOMEM;
		goto err_pm_put;
	}
	girq->parents[0] = chip->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;

skip_irq:
	status = devm_gpiochip_add_data(&pdev->dev, &chip->gc, chip);
	if (status) {
		dev_err(&pdev->dev, "failed to add GPIO chip\n");
		goto err_pm_put;
	}

	pm_runtime_put(&pdev->dev);
	return 0;

err_pm_put:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	clk_disable_unprepare(chip->clk);
	return status;
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
			.pm = &xgpio_dev_pm_ops,
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
