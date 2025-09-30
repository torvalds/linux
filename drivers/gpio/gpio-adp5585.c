// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADP5585 GPIO driver
 *
 * Copyright 2022 NXP
 * Copyright 2024 Ideas on Board Oy
 * Copyright 2025 Analog Devices, Inc.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/adp5585.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

/*
 * Bank 0 covers pins "GPIO 1/R0" to "GPIO 6/R5", numbered 0 to 5 by the
 * driver, and bank 1 covers pins "GPIO 7/C0" to "GPIO 11/C4", numbered 6 to
 * 10. Some variants of the ADP5585 don't support "GPIO 6/R5". As the driver
 * uses identical GPIO numbering for all variants to avoid confusion, GPIO 5 is
 * marked as reserved in the device tree for variants that don't support it.
 */
#define ADP5585_BANK(n)			((n) >= 6 ? 1 : 0)
#define ADP5585_BIT(n)			((n) >= 6 ? BIT((n) - 6) : BIT(n))

/*
 * Bank 0 covers pins "GPIO 1/R0" to "GPIO 8/R7", numbered 0 to 7 by the
 * driver, bank 1 covers pins "GPIO 9/C0" to "GPIO 16/C7", numbered 8 to
 * 15 and bank 3 covers pins "GPIO 17/C8" to "GPIO 19/C10", numbered 16 to 18.
 */
#define ADP5589_BANK(n)			((n) >> 3)
#define ADP5589_BIT(n)			BIT((n) & 0x7)

struct adp5585_gpio_chip {
	int (*bank)(unsigned int off);
	int (*bit)(unsigned int off);
	unsigned int debounce_dis_a;
	unsigned int rpull_cfg_a;
	unsigned int gpo_data_a;
	unsigned int gpo_out_a;
	unsigned int gpio_dir_a;
	unsigned int gpi_stat_a;
	unsigned int gpi_int_lvl_a;
	unsigned int gpi_ev_a;
	unsigned int gpi_ev_min;
	unsigned int gpi_ev_max;
	bool has_bias_hole;
};

struct adp5585_gpio_dev {
	struct gpio_chip gpio_chip;
	struct notifier_block nb;
	const struct adp5585_gpio_chip *info;
	struct regmap *regmap;
	unsigned long irq_mask;
	unsigned long irq_en;
	unsigned long irq_active_high;
	/* used for irqchip bus locking */
	struct mutex bus_lock;
};

static int adp5585_gpio_bank(unsigned int off)
{
	return ADP5585_BANK(off);
}

static int adp5585_gpio_bit(unsigned int off)
{
	return ADP5585_BIT(off);
}

static int adp5589_gpio_bank(unsigned int off)
{
	return ADP5589_BANK(off);
}

static int adp5589_gpio_bit(unsigned int off)
{
	return ADP5589_BIT(off);
}

static int adp5585_gpio_get_direction(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int val;

	regmap_read(adp5585_gpio->regmap, info->gpio_dir_a + info->bank(off), &val);

	return val & info->bit(off) ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int adp5585_gpio_direction_input(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;

	return regmap_clear_bits(adp5585_gpio->regmap, info->gpio_dir_a + info->bank(off),
				 info->bit(off));
}

static int adp5585_gpio_direction_output(struct gpio_chip *chip, unsigned int off, int val)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bank = info->bank(off);
	unsigned int bit = info->bit(off);
	int ret;

	ret = regmap_update_bits(adp5585_gpio->regmap, info->gpo_data_a + bank,
				 bit, val ? bit : 0);
	if (ret)
		return ret;

	return regmap_set_bits(adp5585_gpio->regmap, info->gpio_dir_a + bank,
			       bit);
}

static int adp5585_gpio_get_value(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bank = info->bank(off);
	unsigned int bit = info->bit(off);
	unsigned int reg;
	unsigned int val;

	/*
	 * The input status register doesn't reflect the pin state when the
	 * GPIO is configured as an output. Check the direction, and read the
	 * input status from GPI_STATUS or output value from GPO_DATA_OUT
	 * accordingly.
	 *
	 * We don't need any locking, as concurrent access to the same GPIO
	 * isn't allowed by the GPIO API, so there's no risk of the
	 * .direction_input(), .direction_output() or .set() operations racing
	 * with this.
	 */
	regmap_read(adp5585_gpio->regmap, info->gpio_dir_a + bank, &val);
	reg = val & bit ? info->gpo_data_a : info->gpi_stat_a;
	regmap_read(adp5585_gpio->regmap, reg + bank, &val);

	return !!(val & bit);
}

static int adp5585_gpio_set_value(struct gpio_chip *chip, unsigned int off,
				  int val)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit = adp5585_gpio->info->bit(off);

	return regmap_update_bits(adp5585_gpio->regmap, info->gpo_data_a + info->bank(off),
				  bit, val ? bit : 0);
}

static int adp5585_gpio_set_bias(struct adp5585_gpio_dev *adp5585_gpio,
				 unsigned int off, unsigned int bias)
{
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit, reg, mask, val;

	/*
	 * The bias configuration fields are 2 bits wide and laid down in
	 * consecutive registers ADP5585_RPULL_CONFIG_*, with a hole of 4 bits
	 * after R5.
	 */
	bit = off * 2;
	if (info->has_bias_hole)
		bit += (off > 5 ? 4 : 0);
	reg = info->rpull_cfg_a + bit / 8;
	mask = ADP5585_Rx_PULL_CFG_MASK << (bit % 8);
	val = bias << (bit % 8);

	return regmap_update_bits(adp5585_gpio->regmap, reg, mask, val);
}

static int adp5585_gpio_set_drive(struct adp5585_gpio_dev *adp5585_gpio,
				  unsigned int off, enum pin_config_param drive)
{
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit = adp5585_gpio->info->bit(off);

	return regmap_update_bits(adp5585_gpio->regmap,
				  info->gpo_out_a + info->bank(off), bit,
				  drive == PIN_CONFIG_DRIVE_OPEN_DRAIN ? bit : 0);
}

static int adp5585_gpio_set_debounce(struct adp5585_gpio_dev *adp5585_gpio,
				     unsigned int off, unsigned int debounce)
{
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit = adp5585_gpio->info->bit(off);

	return regmap_update_bits(adp5585_gpio->regmap,
				  info->debounce_dis_a + info->bank(off), bit,
				  debounce ? 0 : bit);
}

static int adp5585_gpio_set_config(struct gpio_chip *chip, unsigned int off,
				   unsigned long config)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		return adp5585_gpio_set_bias(adp5585_gpio, off,
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_BIAS_PULL_DOWN:
		return adp5585_gpio_set_bias(adp5585_gpio, off, arg ?
					     ADP5585_Rx_PULL_CFG_PD_300K :
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_BIAS_PULL_UP:
		return adp5585_gpio_set_bias(adp5585_gpio, off, arg ?
					     ADP5585_Rx_PULL_CFG_PU_300K :
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return adp5585_gpio_set_drive(adp5585_gpio, off, param);

	case PIN_CONFIG_INPUT_DEBOUNCE:
		return adp5585_gpio_set_debounce(adp5585_gpio, off, arg);

	default:
		return -ENOTSUPP;
	};
}

static int adp5585_gpio_request(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	struct device *dev = chip->parent;
	struct adp5585_dev *adp5585 = dev_get_drvdata(dev->parent);
	const struct adp5585_regs *regs = adp5585->regs;
	int ret;

	ret = test_and_set_bit(off, adp5585->pin_usage);
	if (ret)
		return -EBUSY;

	/* make sure it's configured for GPIO */
	return regmap_clear_bits(adp5585_gpio->regmap,
				 regs->pin_cfg_a + info->bank(off),
				 info->bit(off));
}

static void adp5585_gpio_free(struct gpio_chip *chip, unsigned int off)
{
	struct device *dev = chip->parent;
	struct adp5585_dev *adp5585 = dev_get_drvdata(dev->parent);

	clear_bit(off, adp5585->pin_usage);
}

static int adp5585_gpio_key_event(struct notifier_block *nb, unsigned long key,
				  void *data)
{
	struct adp5585_gpio_dev *adp5585_gpio = container_of(nb, struct adp5585_gpio_dev, nb);
	struct device *dev = adp5585_gpio->gpio_chip.parent;
	unsigned long key_press = (unsigned long)data;
	unsigned int irq, irq_type;
	struct irq_data *irqd;
	bool active_high;
	unsigned int off;

	/* make sure the event is for me */
	if (key < adp5585_gpio->info->gpi_ev_min || key > adp5585_gpio->info->gpi_ev_max)
		return NOTIFY_DONE;

	off = key - adp5585_gpio->info->gpi_ev_min;
	active_high = test_bit(off, &adp5585_gpio->irq_active_high);

	irq = irq_find_mapping(adp5585_gpio->gpio_chip.irq.domain, off);
	if (!irq)
		return NOTIFY_BAD;

	irqd = irq_get_irq_data(irq);
	if (!irqd) {
		dev_err(dev, "Could not get irq(%u) data\n", irq);
		return NOTIFY_BAD;
	}

	dev_dbg_ratelimited(dev, "gpio-keys event(%u) press=%lu, a_high=%u\n",
			    off, key_press, active_high);

	if (!active_high)
		key_press = !key_press;

	irq_type = irqd_get_trigger_type(irqd);

	if ((irq_type & IRQ_TYPE_EDGE_RISING && key_press) ||
	    (irq_type & IRQ_TYPE_EDGE_FALLING && !key_press))
		handle_nested_irq(irq);

	return NOTIFY_STOP;
}

static void adp5585_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(gc);

	mutex_lock(&adp5585_gpio->bus_lock);
}

static void adp5585_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	bool active_high = test_bit(hwirq, &adp5585_gpio->irq_active_high);
	bool enabled = test_bit(hwirq, &adp5585_gpio->irq_en);
	bool masked = test_bit(hwirq, &adp5585_gpio->irq_mask);
	unsigned int bank = adp5585_gpio->info->bank(hwirq);
	unsigned int bit = adp5585_gpio->info->bit(hwirq);

	if (masked && !enabled)
		goto out_unlock;
	if (!masked && enabled)
		goto out_unlock;

	regmap_update_bits(adp5585_gpio->regmap, info->gpi_int_lvl_a + bank, bit,
			   active_high ? bit : 0);
	regmap_update_bits(adp5585_gpio->regmap, info->gpi_ev_a + bank, bit,
			   masked ? 0 : bit);
	assign_bit(hwirq, &adp5585_gpio->irq_en, !masked);

out_unlock:
	mutex_unlock(&adp5585_gpio->bus_lock);
}

static void adp5585_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	__set_bit(hwirq, &adp5585_gpio->irq_mask);
	gpiochip_disable_irq(gc, hwirq);
}

static void adp5585_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	__clear_bit(hwirq, &adp5585_gpio->irq_mask);
}

static int adp5585_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	if (!(type & IRQ_TYPE_EDGE_BOTH))
		return -EINVAL;

	assign_bit(hwirq, &adp5585_gpio->irq_active_high,
		   type == IRQ_TYPE_EDGE_RISING);

	irq_set_handler_locked(d, handle_edge_irq);
	return 0;
}

static const struct irq_chip adp5585_irq_chip = {
	.name = "adp5585",
	.irq_mask = adp5585_irq_mask,
	.irq_unmask = adp5585_irq_unmask,
	.irq_bus_lock = adp5585_irq_bus_lock,
	.irq_bus_sync_unlock = adp5585_irq_bus_sync_unlock,
	.irq_set_type = adp5585_irq_set_type,
	.flags = IRQCHIP_SKIP_SET_WAKE | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void adp5585_gpio_unreg_notifier(void *data)
{
	struct adp5585_gpio_dev *adp5585_gpio = data;
	struct device *dev = adp5585_gpio->gpio_chip.parent;
	struct adp5585_dev *adp5585 = dev_get_drvdata(dev->parent);

	blocking_notifier_chain_unregister(&adp5585->event_notifier,
					   &adp5585_gpio->nb);
}

static int adp5585_gpio_probe(struct platform_device *pdev)
{
	struct adp5585_dev *adp5585 = dev_get_drvdata(pdev->dev.parent);
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct adp5585_gpio_dev *adp5585_gpio;
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct gpio_chip *gc;
	int ret;

	adp5585_gpio = devm_kzalloc(dev, sizeof(*adp5585_gpio), GFP_KERNEL);
	if (!adp5585_gpio)
		return -ENOMEM;

	adp5585_gpio->regmap = adp5585->regmap;

	adp5585_gpio->info = (const struct adp5585_gpio_chip *)id->driver_data;
	if (!adp5585_gpio->info)
		return -ENODEV;

	device_set_of_node_from_dev(dev, dev->parent);

	gc = &adp5585_gpio->gpio_chip;
	gc->parent = dev;
	gc->get_direction = adp5585_gpio_get_direction;
	gc->direction_input = adp5585_gpio_direction_input;
	gc->direction_output = adp5585_gpio_direction_output;
	gc->get = adp5585_gpio_get_value;
	gc->set = adp5585_gpio_set_value;
	gc->set_config = adp5585_gpio_set_config;
	gc->request = adp5585_gpio_request;
	gc->free = adp5585_gpio_free;
	gc->can_sleep = true;

	gc->base = -1;
	gc->ngpio = adp5585->n_pins;
	gc->label = pdev->name;
	gc->owner = THIS_MODULE;

	if (device_property_present(dev->parent, "interrupt-controller")) {
		if (!adp5585->irq)
			return dev_err_probe(dev, -EINVAL,
					     "Unable to serve as interrupt controller without IRQ\n");

		girq = &adp5585_gpio->gpio_chip.irq;
		gpio_irq_chip_set_chip(girq, &adp5585_irq_chip);
		girq->handler = handle_bad_irq;
		girq->threaded = true;

		adp5585_gpio->nb.notifier_call = adp5585_gpio_key_event;
		ret = blocking_notifier_chain_register(&adp5585->event_notifier,
						       &adp5585_gpio->nb);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(dev, adp5585_gpio_unreg_notifier,
					       adp5585_gpio);
		if (ret)
			return ret;
	}

	/* everything masked by default */
	adp5585_gpio->irq_mask = ~0UL;

	ret = devm_mutex_init(dev, &adp5585_gpio->bus_lock);
	if (ret)
		return ret;
	ret = devm_gpiochip_add_data(dev, &adp5585_gpio->gpio_chip,
				     adp5585_gpio);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add GPIO chip\n");

	return 0;
}

static const struct adp5585_gpio_chip adp5585_gpio_chip_info = {
	.bank = adp5585_gpio_bank,
	.bit = adp5585_gpio_bit,
	.debounce_dis_a = ADP5585_DEBOUNCE_DIS_A,
	.rpull_cfg_a = ADP5585_RPULL_CONFIG_A,
	.gpo_data_a = ADP5585_GPO_DATA_OUT_A,
	.gpo_out_a = ADP5585_GPO_OUT_MODE_A,
	.gpio_dir_a = ADP5585_GPIO_DIRECTION_A,
	.gpi_stat_a = ADP5585_GPI_STATUS_A,
	.has_bias_hole = true,
	.gpi_ev_min = ADP5585_GPI_EVENT_START,
	.gpi_ev_max = ADP5585_GPI_EVENT_END,
	.gpi_int_lvl_a = ADP5585_GPI_INT_LEVEL_A,
	.gpi_ev_a = ADP5585_GPI_EVENT_EN_A,
};

static const struct adp5585_gpio_chip adp5589_gpio_chip_info = {
	.bank = adp5589_gpio_bank,
	.bit = adp5589_gpio_bit,
	.debounce_dis_a = ADP5589_DEBOUNCE_DIS_A,
	.rpull_cfg_a = ADP5589_RPULL_CONFIG_A,
	.gpo_data_a = ADP5589_GPO_DATA_OUT_A,
	.gpo_out_a = ADP5589_GPO_OUT_MODE_A,
	.gpio_dir_a = ADP5589_GPIO_DIRECTION_A,
	.gpi_stat_a = ADP5589_GPI_STATUS_A,
	.gpi_ev_min = ADP5589_GPI_EVENT_START,
	.gpi_ev_max = ADP5589_GPI_EVENT_END,
	.gpi_int_lvl_a = ADP5589_GPI_INT_LEVEL_A,
	.gpi_ev_a = ADP5589_GPI_EVENT_EN_A,
};

static const struct platform_device_id adp5585_gpio_id_table[] = {
	{ "adp5585-gpio", (kernel_ulong_t)&adp5585_gpio_chip_info },
	{ "adp5589-gpio", (kernel_ulong_t)&adp5589_gpio_chip_info },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(platform, adp5585_gpio_id_table);

static struct platform_driver adp5585_gpio_driver = {
	.driver	= {
		.name = "adp5585-gpio",
	},
	.probe = adp5585_gpio_probe,
	.id_table = adp5585_gpio_id_table,
};
module_platform_driver(adp5585_gpio_driver);

MODULE_AUTHOR("Haibo Chen <haibo.chen@nxp.com>");
MODULE_DESCRIPTION("GPIO ADP5585 Driver");
MODULE_LICENSE("GPL");
