// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kontron PLD GPIO driver
 *
 * Copyright (c) 2010-2013 Kontron Europe GmbH
 * Author: Michael Brunner <michael.brunner@kontron.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/kempld.h>

#define KEMPLD_GPIO_MAX_NUM		16
#define KEMPLD_GPIO_MASK(x)		(BIT((x) % 8))
#define KEMPLD_GPIO_DIR			0x40
#define KEMPLD_GPIO_LVL			0x42
#define KEMPLD_GPIO_STS			0x44
#define KEMPLD_GPIO_EVT_LVL_EDGE	0x46
#define KEMPLD_GPIO_EVT_LOW_HIGH	0x48
#define KEMPLD_GPIO_IEN			0x4A
#define KEMPLD_GPIO_OUT_LVL		0x4E

/* The IRQ to use if none was configured in the BIOS */
static unsigned int gpio_irq;
module_param_hw(gpio_irq, uint, irq, 0444);
MODULE_PARM_DESC(gpio_irq, "Set legacy GPIO IRQ (1-15)");

struct kempld_gpio_data {
	struct gpio_chip		chip;
	struct kempld_device_data	*pld;
	u8				out_lvl_reg;

	struct mutex			irq_lock;
	u16				ien;
	u16				evt_low_high;
	u16				evt_lvl_edge;
};

/*
 * Set or clear GPIO bit
 * kempld_get_mutex must be called prior to calling this function.
 */
static void kempld_gpio_bitop(struct kempld_device_data *pld,
			      u8 reg, unsigned int bit, bool val)
{
	u8 status;

	status = kempld_read8(pld, reg + (bit / 8));
	if (val)
		status |= KEMPLD_GPIO_MASK(bit);
	else
		status &= ~KEMPLD_GPIO_MASK(bit);
	kempld_write8(pld, reg + (bit / 8), status);
}

static int kempld_gpio_get_bit(struct kempld_device_data *pld,
			       u8 reg, unsigned int bit)
{
	u8 status;

	kempld_get_mutex(pld);
	status = kempld_read8(pld, reg + (bit / 8));
	kempld_release_mutex(pld);

	return !!(status & KEMPLD_GPIO_MASK(bit));
}

static int kempld_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;

	return !!kempld_gpio_get_bit(pld, KEMPLD_GPIO_LVL, offset);
}

static int kempld_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
				    unsigned long *bits)
{
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;
	u8 reg = KEMPLD_GPIO_LVL;
	unsigned int shift;

	bits[0] &= ~mask[0];

	kempld_get_mutex(pld);

	/* Try to reduce to a single 8 bits access if possible */
	for (shift = 0; shift < gpio->chip.ngpio; shift += 8, reg++) {
		unsigned long msk = (mask[0] >> shift) & 0xff;

		if (!msk)
			continue;

		bits[0] |= (kempld_read8(pld, reg) & msk) << shift;
	}

	kempld_release_mutex(pld);

	return 0;
}

static int kempld_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;

	kempld_get_mutex(pld);
	kempld_gpio_bitop(pld, gpio->out_lvl_reg, offset, value);
	kempld_release_mutex(pld);

	return 0;
}

static int kempld_gpio_set_multiple(struct gpio_chip *chip,
				    unsigned long *mask, unsigned long *bits)
{
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;
	u8 reg = gpio->out_lvl_reg;
	unsigned int shift;

	kempld_get_mutex(pld);

	/* Try to reduce to a single 8 bits access if possible */
	for (shift = 0; shift < gpio->chip.ngpio; shift += 8, reg++) {
		u8 val, msk = mask[0] >> shift;

		if (!msk)
			continue;

		if (msk != 0xFF)
			val = kempld_read8(pld, reg) & ~msk;
		else
			val = 0;

		val |= (bits[0] >> shift) & msk;
		kempld_write8(pld, reg, val);
	}

	kempld_release_mutex(pld);

	return 0;
}

static int kempld_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;

	kempld_get_mutex(pld);
	kempld_gpio_bitop(pld, KEMPLD_GPIO_DIR, offset, 0);
	kempld_release_mutex(pld);

	return 0;
}

static int kempld_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;

	kempld_get_mutex(pld);
	kempld_gpio_bitop(pld, gpio->out_lvl_reg, offset, value);
	kempld_gpio_bitop(pld, KEMPLD_GPIO_DIR, offset, 1);
	kempld_release_mutex(pld);

	return 0;
}

static int kempld_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;

	if (kempld_gpio_get_bit(pld, KEMPLD_GPIO_DIR, offset))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int kempld_gpio_pincount(struct kempld_device_data *pld)
{
	u16 evt, evt_back;

	kempld_get_mutex(pld);

	/* Backup event register as it might be already initialized */
	evt_back = kempld_read16(pld, KEMPLD_GPIO_EVT_LVL_EDGE);
	/* Clear event register */
	kempld_write16(pld, KEMPLD_GPIO_EVT_LVL_EDGE, 0x0000);
	/* Read back event register */
	evt = kempld_read16(pld, KEMPLD_GPIO_EVT_LVL_EDGE);
	/* Restore event register */
	kempld_write16(pld, KEMPLD_GPIO_EVT_LVL_EDGE, evt_back);

	kempld_release_mutex(pld);

	return evt ? __ffs(evt) : 16;
}

static void kempld_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);

	gpio->ien &= ~BIT(irqd_to_hwirq(data));
	gpiochip_disable_irq(chip, irqd_to_hwirq(data));
}

static void kempld_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);

	gpiochip_enable_irq(chip, irqd_to_hwirq(data));
	gpio->ien |= BIT(irqd_to_hwirq(data));
}

static int kempld_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		gpio->evt_low_high |= BIT(data->hwirq);
		gpio->evt_lvl_edge |= BIT(data->hwirq);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		gpio->evt_low_high &= ~BIT(data->hwirq);
		gpio->evt_lvl_edge |= BIT(data->hwirq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		gpio->evt_low_high |= BIT(data->hwirq);
		gpio->evt_lvl_edge &= ~BIT(data->hwirq);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		gpio->evt_low_high &= ~BIT(data->hwirq);
		gpio->evt_lvl_edge &= ~BIT(data->hwirq);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void kempld_irq_bus_lock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);

	mutex_lock(&gpio->irq_lock);
}

static void kempld_irq_bus_sync_unlock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct kempld_gpio_data *gpio = gpiochip_get_data(chip);
	struct kempld_device_data *pld = gpio->pld;

	kempld_get_mutex(pld);
	kempld_write16(pld, KEMPLD_GPIO_EVT_LVL_EDGE, gpio->evt_lvl_edge);
	kempld_write16(pld, KEMPLD_GPIO_EVT_LOW_HIGH, gpio->evt_low_high);
	kempld_write16(pld, KEMPLD_GPIO_IEN, gpio->ien);
	kempld_release_mutex(pld);

	mutex_unlock(&gpio->irq_lock);
}

static const struct irq_chip kempld_irqchip = {
	.name			= "kempld-gpio",
	.irq_mask		= kempld_irq_mask,
	.irq_unmask		= kempld_irq_unmask,
	.irq_set_type		= kempld_irq_set_type,
	.irq_bus_lock		= kempld_irq_bus_lock,
	.irq_bus_sync_unlock	= kempld_irq_bus_sync_unlock,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static irqreturn_t kempld_gpio_irq_handler(int irq, void *data)
{
	struct kempld_gpio_data *gpio = data;
	struct gpio_chip *chip = &gpio->chip;
	unsigned int pin, child_irq;
	unsigned long status;

	kempld_get_mutex(gpio->pld);

	status = kempld_read16(gpio->pld, KEMPLD_GPIO_STS);
	if (status)
		kempld_write16(gpio->pld, KEMPLD_GPIO_STS, status);

	kempld_release_mutex(gpio->pld);

	status &= gpio->ien;
	if (!status)
		return IRQ_NONE;

	for_each_set_bit(pin, &status, chip->ngpio) {
		child_irq = irq_find_mapping(chip->irq.domain, pin);
		handle_nested_irq(child_irq);
	}

	return IRQ_HANDLED;
}

static int kempld_gpio_irq_init(struct device *dev,
				struct kempld_gpio_data *gpio)
{
	struct kempld_device_data *pld = gpio->pld;
	struct gpio_chip *chip = &gpio->chip;
	struct gpio_irq_chip *girq;
	unsigned int irq;
	int ret;

	/* Get the IRQ configured by the BIOS in the PLD */
	kempld_get_mutex(pld);
	irq = kempld_read8(pld, KEMPLD_IRQ_GPIO);
	kempld_release_mutex(pld);

	if (irq == 0xff) {
		dev_info(dev, "GPIO controller has no IRQ support\n");
		return 0;
	}

	/* Allow overriding the IRQ with the module parameter */
	if (gpio_irq > 0) {
		dev_warn(dev, "Forcing IRQ to %d\n", gpio_irq);
		irq &= ~KEMPLD_IRQ_GPIO_MASK;
		irq |= gpio_irq & KEMPLD_IRQ_GPIO_MASK;
	}

	if (!(irq & KEMPLD_IRQ_GPIO_MASK)) {
		dev_warn(dev, "No IRQ configured\n");
		return 0;
	}

	/* Get the current config, disable all child interrupts, clear them
	 * and set the parent IRQ
	 */
	kempld_get_mutex(pld);
	gpio->evt_low_high = kempld_read16(pld, KEMPLD_GPIO_EVT_LOW_HIGH);
	gpio->evt_lvl_edge = kempld_read16(pld, KEMPLD_GPIO_EVT_LVL_EDGE);
	kempld_write16(pld, KEMPLD_GPIO_IEN, 0);
	kempld_write16(pld, KEMPLD_GPIO_STS, 0xFFFF);
	kempld_write16(pld, KEMPLD_IRQ_GPIO, irq);
	kempld_release_mutex(pld);

	girq = &chip->irq;
	gpio_irq_chip_set_chip(girq, &kempld_irqchip);

	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;
	girq->threaded = true;

	mutex_init(&gpio->irq_lock);

	ret = devm_request_threaded_irq(dev, irq & KEMPLD_IRQ_GPIO_MASK,
					NULL, kempld_gpio_irq_handler,
					IRQF_ONESHOT, chip->label,
					gpio);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", irq);
		return ret;
	}

	return 0;
}

static int kempld_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kempld_device_data *pld = dev_get_drvdata(dev->parent);
	struct kempld_platform_data *pdata = dev_get_platdata(pld->dev);
	struct kempld_gpio_data *gpio;
	struct gpio_chip *chip;
	int ret;

	if (pld->info.spec_major < 2) {
		dev_err(dev,
			"Driver only supports GPIO devices compatible to PLD spec. rev. 2.0 or higher\n");
		return -ENODEV;
	}

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	/* Starting with version 2.8 there is a dedicated register for the
	 * output state, earlier versions share the register used to read
	 * the line level.
	 */
	if (pld->info.spec_major > 2 || pld->info.spec_minor >= 8)
		gpio->out_lvl_reg = KEMPLD_GPIO_OUT_LVL;
	else
		gpio->out_lvl_reg = KEMPLD_GPIO_LVL;

	gpio->pld = pld;

	platform_set_drvdata(pdev, gpio);

	chip = &gpio->chip;
	chip->label = "gpio-kempld";
	chip->owner = THIS_MODULE;
	chip->parent = dev;
	chip->can_sleep = true;
	if (pdata && pdata->gpio_base)
		chip->base = pdata->gpio_base;
	else
		chip->base = -1;
	chip->direction_input = kempld_gpio_direction_input;
	chip->direction_output = kempld_gpio_direction_output;
	chip->get_direction = kempld_gpio_get_direction;
	chip->get = kempld_gpio_get;
	chip->get_multiple = kempld_gpio_get_multiple;
	chip->set = kempld_gpio_set;
	chip->set_multiple = kempld_gpio_set_multiple;
	chip->ngpio = kempld_gpio_pincount(pld);
	if (chip->ngpio == 0) {
		dev_err(dev, "No GPIO pins detected\n");
		return -ENODEV;
	}

	ret = kempld_gpio_irq_init(dev, gpio);
	if (ret)
		return ret;

	ret = devm_gpiochip_add_data(dev, chip, gpio);
	if (ret) {
		dev_err(dev, "Could not register GPIO chip\n");
		return ret;
	}

	dev_info(dev, "GPIO functionality initialized with %d pins\n",
		 chip->ngpio);

	return 0;
}

static struct platform_driver kempld_gpio_driver = {
	.driver = {
		.name = "kempld-gpio",
	},
	.probe		= kempld_gpio_probe,
};

module_platform_driver(kempld_gpio_driver);

MODULE_DESCRIPTION("KEM PLD GPIO Driver");
MODULE_AUTHOR("Michael Brunner <michael.brunner@kontron.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:kempld-gpio");
