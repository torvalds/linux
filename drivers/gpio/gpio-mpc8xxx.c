// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIOs on MPC512x/8349/8572/8610/QorIQ and compatible
 *
 * Copyright (C) 2008 Peter Korsgaard <jacmet@sunsite.dk>
 * Copyright (C) 2016 Freescale Semiconductor Inc.
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define MPC8XXX_GPIO_PINS	32

#define GPIO_DIR		0x00
#define GPIO_ODR		0x04
#define GPIO_DAT		0x08
#define GPIO_IER		0x0c
#define GPIO_IMR		0x10
#define GPIO_ICR		0x14
#define GPIO_ICR2		0x18
#define GPIO_IBE		0x18

struct mpc8xxx_gpio_chip {
	struct gpio_generic_chip chip;
	void __iomem *regs;
	raw_spinlock_t lock;

	int (*direction_output)(struct gpio_chip *chip,
				unsigned offset, int value);

	struct irq_domain *irq;
	int irqn;
};

/*
 * This hardware has a big endian bit assignment such that GPIO line 0 is
 * connected to bit 31, line 1 to bit 30 ... line 31 to bit 0.
 * This inline helper give the right bitmask for a certain line.
 */
static inline u32 mpc_pin2mask(unsigned int offset)
{
	return BIT(31 - offset);
}

/* Workaround GPIO 1 errata on MPC8572/MPC8536. The status of GPIOs
 * defined as output cannot be determined by reading GPDAT register,
 * so we use shadow data register instead. The status of input pins
 * is determined by reading GPDAT register.
 */
static int mpc8572_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	u32 val;
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = gpiochip_get_data(gc);
	u32 out_mask, out_shadow;

	out_mask = gpio_generic_read_reg(&mpc8xxx_gc->chip,
					 mpc8xxx_gc->regs + GPIO_DIR);
	val = gpio_generic_read_reg(&mpc8xxx_gc->chip,
				    mpc8xxx_gc->regs + GPIO_DAT) & ~out_mask;
	out_shadow = mpc8xxx_gc->chip.sdata & out_mask;

	return !!((val | out_shadow) & mpc_pin2mask(gpio));
}

static int mpc5121_gpio_dir_out(struct gpio_chip *gc,
				unsigned int gpio, int val)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = gpiochip_get_data(gc);
	/* GPIO 28..31 are input only on MPC5121 */
	if (gpio >= 28)
		return -EINVAL;

	return mpc8xxx_gc->direction_output(gc, gpio, val);
}

static int mpc5125_gpio_dir_out(struct gpio_chip *gc,
				unsigned int gpio, int val)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = gpiochip_get_data(gc);
	/* GPIO 0..3 are input only on MPC5125 */
	if (gpio <= 3)
		return -EINVAL;

	return mpc8xxx_gc->direction_output(gc, gpio, val);
}

static int mpc8xxx_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = gpiochip_get_data(gc);

	if (mpc8xxx_gc->irq && offset < MPC8XXX_GPIO_PINS)
		return irq_create_mapping(mpc8xxx_gc->irq, offset);
	else
		return -ENXIO;
}

static irqreturn_t mpc8xxx_gpio_irq_cascade(int irq, void *data)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = data;
	unsigned long mask;
	int i;

	mask = gpio_generic_read_reg(&mpc8xxx_gc->chip,
				     mpc8xxx_gc->regs + GPIO_IER) &
	       gpio_generic_read_reg(&mpc8xxx_gc->chip,
				     mpc8xxx_gc->regs + GPIO_IMR);
	for_each_set_bit(i, &mask, 32)
		generic_handle_domain_irq(mpc8xxx_gc->irq, 31 - i);

	return IRQ_HANDLED;
}

static void mpc8xxx_irq_unmask(struct irq_data *d)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct gpio_chip *gc = &mpc8xxx_gc->chip.gc;
	unsigned long flags;

	gpiochip_enable_irq(gc, hwirq);

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	gpio_generic_write_reg(&mpc8xxx_gc->chip,
			       mpc8xxx_gc->regs + GPIO_IMR,
		gpio_generic_read_reg(&mpc8xxx_gc->chip,
				      mpc8xxx_gc->regs + GPIO_IMR)
		| mpc_pin2mask(irqd_to_hwirq(d)));

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static void mpc8xxx_irq_mask(struct irq_data *d)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct gpio_chip *gc = &mpc8xxx_gc->chip.gc;
	unsigned long flags;

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	gpio_generic_write_reg(&mpc8xxx_gc->chip, mpc8xxx_gc->regs + GPIO_IMR,
		gpio_generic_read_reg(&mpc8xxx_gc->chip,
				      mpc8xxx_gc->regs + GPIO_IMR)
		& ~mpc_pin2mask(irqd_to_hwirq(d)));

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);

	gpiochip_disable_irq(gc, hwirq);
}

static void mpc8xxx_irq_ack(struct irq_data *d)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);

	gpio_generic_write_reg(&mpc8xxx_gc->chip, mpc8xxx_gc->regs + GPIO_IER,
		      mpc_pin2mask(irqd_to_hwirq(d)));
}

static int mpc8xxx_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		gpio_generic_write_reg(&mpc8xxx_gc->chip,
				       mpc8xxx_gc->regs + GPIO_ICR,
			gpio_generic_read_reg(&mpc8xxx_gc->chip,
					      mpc8xxx_gc->regs + GPIO_ICR)
			| mpc_pin2mask(irqd_to_hwirq(d)));
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		gpio_generic_write_reg(&mpc8xxx_gc->chip,
				       mpc8xxx_gc->regs + GPIO_ICR,
			gpio_generic_read_reg(&mpc8xxx_gc->chip,
					      mpc8xxx_gc->regs + GPIO_ICR)
			& ~mpc_pin2mask(irqd_to_hwirq(d)));
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int mpc512x_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	unsigned long gpio = irqd_to_hwirq(d);
	void __iomem *reg;
	unsigned int shift;
	unsigned long flags;

	if (gpio < 16) {
		reg = mpc8xxx_gc->regs + GPIO_ICR;
		shift = (15 - gpio) * 2;
	} else {
		reg = mpc8xxx_gc->regs + GPIO_ICR2;
		shift = (15 - (gpio % 16)) * 2;
	}

	switch (flow_type) {
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		gpio_generic_write_reg(&mpc8xxx_gc->chip, reg,
				       (gpio_generic_read_reg(&mpc8xxx_gc->chip,
							      reg) & ~(3 << shift))
			| (2 << shift));
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		gpio_generic_write_reg(&mpc8xxx_gc->chip, reg,
				       (gpio_generic_read_reg(&mpc8xxx_gc->chip,
							      reg) & ~(3 << shift))
			| (1 << shift));
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		gpio_generic_write_reg(&mpc8xxx_gc->chip, reg,
				       (gpio_generic_read_reg(&mpc8xxx_gc->chip,
							      reg) & ~(3 << shift)));
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip mpc8xxx_irq_chip = {
	.name		= "mpc8xxx-gpio",
	.irq_unmask	= mpc8xxx_irq_unmask,
	.irq_mask	= mpc8xxx_irq_mask,
	.irq_ack	= mpc8xxx_irq_ack,
	/* this might get overwritten in mpc8xxx_probe() */
	.irq_set_type	= mpc8xxx_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int mpc8xxx_gpio_irq_map(struct irq_domain *h, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, h->host_data);
	irq_set_chip_and_handler(irq, &mpc8xxx_irq_chip, handle_edge_irq);

	return 0;
}

static const struct irq_domain_ops mpc8xxx_gpio_irq_ops = {
	.map	= mpc8xxx_gpio_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

struct mpc8xxx_gpio_devtype {
	int (*gpio_dir_out)(struct gpio_chip *, unsigned int, int);
	int (*gpio_get)(struct gpio_chip *, unsigned int);
	int (*irq_set_type)(struct irq_data *, unsigned int);
};

static const struct mpc8xxx_gpio_devtype mpc512x_gpio_devtype = {
	.gpio_dir_out = mpc5121_gpio_dir_out,
	.irq_set_type = mpc512x_irq_set_type,
};

static const struct mpc8xxx_gpio_devtype mpc5125_gpio_devtype = {
	.gpio_dir_out = mpc5125_gpio_dir_out,
	.irq_set_type = mpc512x_irq_set_type,
};

static const struct mpc8xxx_gpio_devtype mpc8572_gpio_devtype = {
	.gpio_get = mpc8572_gpio_get,
};

static const struct mpc8xxx_gpio_devtype mpc8xxx_gpio_devtype_default = {
	.irq_set_type = mpc8xxx_irq_set_type,
};

static const struct of_device_id mpc8xxx_gpio_ids[] = {
	{ .compatible = "fsl,mpc8314-gpio", },
	{ .compatible = "fsl,mpc8349-gpio", },
	{ .compatible = "fsl,mpc8572-gpio", .data = &mpc8572_gpio_devtype, },
	{ .compatible = "fsl,mpc8610-gpio", },
	{ .compatible = "fsl,mpc5121-gpio", .data = &mpc512x_gpio_devtype, },
	{ .compatible = "fsl,mpc5125-gpio", .data = &mpc5125_gpio_devtype, },
	{ .compatible = "fsl,pq3-gpio",     },
	{ .compatible = "fsl,ls1028a-gpio", },
	{ .compatible = "fsl,ls1088a-gpio", },
	{ .compatible = "fsl,qoriq-gpio",   },
	{}
};

static int mpc8xxx_probe(struct platform_device *pdev)
{
	const struct mpc8xxx_gpio_devtype *devtype = NULL;
	struct gpio_generic_chip_config config;
	struct mpc8xxx_gpio_chip *mpc8xxx_gc;
	struct device *dev = &pdev->dev;
	struct fwnode_handle *fwnode;
	struct gpio_chip *gc;
	int ret;

	mpc8xxx_gc = devm_kzalloc(dev, sizeof(*mpc8xxx_gc), GFP_KERNEL);
	if (!mpc8xxx_gc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mpc8xxx_gc);

	raw_spin_lock_init(&mpc8xxx_gc->lock);

	mpc8xxx_gc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mpc8xxx_gc->regs))
		return PTR_ERR(mpc8xxx_gc->regs);

	gc = &mpc8xxx_gc->chip.gc;
	gc->parent = dev;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = mpc8xxx_gc->regs + GPIO_DAT,
		.dirout = mpc8xxx_gc->regs + GPIO_DIR,
		.flags = GPIO_GENERIC_BIG_ENDIAN
	};

	if (device_property_read_bool(dev, "little-endian")) {
		dev_dbg(dev, "GPIO registers are LITTLE endian\n");
	} else {
		config.flags |= GPIO_GENERIC_BIG_ENDIAN_BYTE_ORDER;
		dev_dbg(dev, "GPIO registers are BIG endian\n");
	}

	ret = gpio_generic_chip_init(&mpc8xxx_gc->chip, &config);
	if (ret)
		return ret;

	mpc8xxx_gc->direction_output = gc->direction_output;

	devtype = device_get_match_data(dev);
	if (!devtype)
		devtype = &mpc8xxx_gpio_devtype_default;

	/*
	 * It's assumed that only a single type of gpio controller is available
	 * on the current machine, so overwriting global data is fine.
	 */
	if (devtype->irq_set_type)
		mpc8xxx_irq_chip.irq_set_type = devtype->irq_set_type;

	if (devtype->gpio_dir_out)
		gc->direction_output = devtype->gpio_dir_out;
	if (devtype->gpio_get)
		gc->get = devtype->gpio_get;

	gc->to_irq = mpc8xxx_gpio_to_irq;

	/*
	 * The GPIO Input Buffer Enable register(GPIO_IBE) is used to control
	 * the input enable of each individual GPIO port.  When an individual
	 * GPIO port’s direction is set to input (GPIO_GPDIR[DRn=0]), the
	 * associated input enable must be set (GPIOxGPIE[IEn]=1) to propagate
	 * the port value to the GPIO Data Register.
	 */
	fwnode = dev_fwnode(dev);
	if (device_is_compatible(dev, "fsl,qoriq-gpio") ||
	    device_is_compatible(dev, "fsl,ls1028a-gpio") ||
	    device_is_compatible(dev, "fsl,ls1088a-gpio") ||
	    is_acpi_node(fwnode)) {
		gpio_generic_write_reg(&mpc8xxx_gc->chip,
				       mpc8xxx_gc->regs + GPIO_IBE, 0xffffffff);
		/* Also, latch state of GPIOs configured as output by bootloader. */
		mpc8xxx_gc->chip.sdata =
				gpio_generic_read_reg(&mpc8xxx_gc->chip,
						       mpc8xxx_gc->regs + GPIO_DAT) &
				 gpio_generic_read_reg(&mpc8xxx_gc->chip,
						       mpc8xxx_gc->regs + GPIO_DIR);
	}

	ret = devm_gpiochip_add_data(dev, gc, mpc8xxx_gc);
	if (ret) {
		dev_err(dev,
			"GPIO chip registration failed with status %d\n", ret);
		return ret;
	}

	mpc8xxx_gc->irqn = platform_get_irq(pdev, 0);
	if (mpc8xxx_gc->irqn < 0)
		return mpc8xxx_gc->irqn;

	mpc8xxx_gc->irq = irq_domain_create_linear(fwnode,
						   MPC8XXX_GPIO_PINS,
						   &mpc8xxx_gpio_irq_ops,
						   mpc8xxx_gc);

	if (!mpc8xxx_gc->irq)
		return 0;

	/* ack and mask all irqs */
	gpio_generic_write_reg(&mpc8xxx_gc->chip,
			       mpc8xxx_gc->regs + GPIO_IER, 0xffffffff);
	gpio_generic_write_reg(&mpc8xxx_gc->chip,
			       mpc8xxx_gc->regs + GPIO_IMR, 0);

	ret = devm_request_irq(dev, mpc8xxx_gc->irqn,
			       mpc8xxx_gpio_irq_cascade,
			       IRQF_NO_THREAD | IRQF_SHARED, "gpio-cascade",
			       mpc8xxx_gc);
	if (ret) {
		dev_err(dev, "failed to devm_request_irq(%d), ret = %d\n",
			mpc8xxx_gc->irqn, ret);
		goto err;
	}

	ret = devm_device_init_wakeup(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init wakeup\n");

	return 0;
err:
	irq_domain_remove(mpc8xxx_gc->irq);
	return ret;
}

static void mpc8xxx_remove(struct platform_device *pdev)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = platform_get_drvdata(pdev);

	if (mpc8xxx_gc->irq) {
		irq_set_chained_handler_and_data(mpc8xxx_gc->irqn, NULL, NULL);
		irq_domain_remove(mpc8xxx_gc->irq);
	}
}

static int mpc8xxx_suspend(struct device *dev)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = dev_get_drvdata(dev);

	if (mpc8xxx_gc->irqn && device_may_wakeup(dev))
		enable_irq_wake(mpc8xxx_gc->irqn);

	return 0;
}

static int mpc8xxx_resume(struct device *dev)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = dev_get_drvdata(dev);

	if (mpc8xxx_gc->irqn && device_may_wakeup(dev))
		disable_irq_wake(mpc8xxx_gc->irqn);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(mpc8xx_pm_ops,
				 mpc8xxx_suspend, mpc8xxx_resume, NULL);

#ifdef CONFIG_ACPI
static const struct acpi_device_id gpio_acpi_ids[] = {
	{"NXP0031",},
	{ }
};
MODULE_DEVICE_TABLE(acpi, gpio_acpi_ids);
#endif

static struct platform_driver mpc8xxx_plat_driver = {
	.probe		= mpc8xxx_probe,
	.remove		= mpc8xxx_remove,
	.driver		= {
		.name = "gpio-mpc8xxx",
		.of_match_table	= mpc8xxx_gpio_ids,
		.acpi_match_table = ACPI_PTR(gpio_acpi_ids),
		.pm = pm_ptr(&mpc8xx_pm_ops),
	},
};

static int __init mpc8xxx_init(void)
{
	return platform_driver_register(&mpc8xxx_plat_driver);
}

arch_initcall(mpc8xxx_init);
