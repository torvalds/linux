// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC RTD1625 gpio driver
 *
 * Copyright (c) 2023-2026 Realtek Semiconductor Corp.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define RTD1625_GPIO_DIR BIT(0)
#define RTD1625_GPIO_OUT BIT(2)
#define RTD1625_GPIO_IN BIT(4)
#define RTD1625_GPIO_EDGE_INT_DP BIT(6)
#define RTD1625_GPIO_EDGE_INT_EN BIT(8)
#define RTD1625_GPIO_LEVEL_INT_EN BIT(16)
#define RTD1625_GPIO_LEVEL_INT_DP BIT(18)
#define RTD1625_GPIO_DEBOUNCE GENMASK(30, 28)
#define RTD1625_GPIO_DEBOUNCE_WREN BIT(31)

#define RTD1625_GPIO_WREN(x) ((x) << 1)

/* Write-enable masks for all GPIO configs and reserved hardware bits */
#define RTD1625_ISO_GPIO_WREN_ALL 0x8000aa8a
#define RTD1625_ISOM_GPIO_WREN_ALL 0x800aaa8a

#define RTD1625_GPIO_DEBOUNCE_1US 0
#define RTD1625_GPIO_DEBOUNCE_10US 1
#define RTD1625_GPIO_DEBOUNCE_100US 2
#define RTD1625_GPIO_DEBOUNCE_1MS 3
#define RTD1625_GPIO_DEBOUNCE_10MS 4
#define RTD1625_GPIO_DEBOUNCE_20MS 5
#define RTD1625_GPIO_DEBOUNCE_30MS 6
#define RTD1625_GPIO_DEBOUNCE_50MS 7

#define GPIO_CONTROL(gpio) ((gpio) * 4)

static struct lock_class_key rtd1625_gpio_irq_lock_class;
static struct lock_class_key rtd1625_gpio_irq_request_class;

enum rtd1625_irq_index {
	RTD1625_IRQ_ASSERT,
	RTD1625_IRQ_DEASSERT,
	RTD1625_IRQ_LEVEL,
	RTD1625_MAX_IRQS
};

/**
 * struct rtd1625_gpio_info - Specific GPIO register information
 * @num_gpios: The number of GPIOs
 * @irq_type_support: Supported IRQ types
 * @base_offset: Offset for GPIO controller register
 * @gpa_offset: Offset for GPIO assert interrupt status register
 * @gpda_offset: Offset for GPIO deassert interrupt status register
 * @level_offset: Offset of level interrupt status register
 * @write_en_all: Write-enable mask for all configurable bits
 */
struct rtd1625_gpio_info {
	unsigned int num_gpios;
	unsigned int irq_type_support;
	unsigned int base_offset;
	unsigned int gpa_offset;
	unsigned int gpda_offset;
	unsigned int level_offset;
	unsigned int write_en_all;
};

struct rtd1625_gpio {
	struct gpio_regmap *gpio_reg;
	const struct rtd1625_gpio_info *info;
	struct regmap *regmap;
	unsigned int irqs[RTD1625_MAX_IRQS];
	raw_spinlock_t lock;
	struct irq_domain *domain;
	unsigned int *save_regs;
};

static unsigned int rtd1625_gpio_gpa_offset(struct rtd1625_gpio *data, unsigned int offset)
{
	return data->info->gpa_offset + ((offset / 32) * 4);
}

static unsigned int rtd1625_gpio_gpda_offset(struct rtd1625_gpio *data, unsigned int offset)
{
	return data->info->gpda_offset + ((offset / 32) * 4);
}

static unsigned int rtd1625_gpio_level_offset(struct rtd1625_gpio *data, unsigned int offset)
{
	return data->info->level_offset + ((offset / 32) * 4);
}

static int rtd1625_reg_mask_xlate(struct gpio_regmap *gpio, enum gpio_regmap_operation op,
				  unsigned int base, unsigned int offset, unsigned int *reg,
				  unsigned int *mask)
{
	/* Each GPIO has its own dedicated 32-bit register */
	struct rtd1625_gpio *data = gpio_regmap_get_drvdata(gpio);
	int val = 0, ret = 0;
	*reg = base + offset * 4;

	switch (op) {
	case GPIO_REGMAP_SET_OP:
		*mask = RTD1625_GPIO_OUT;
		return 0;

	case GPIO_REGMAP_GET_OP:
		ret = regmap_read(data->regmap, *reg, &val);
		if (ret)
			return ret;

		if (val & RTD1625_GPIO_DIR)
			*mask = RTD1625_GPIO_OUT;
		else
			*mask = RTD1625_GPIO_IN;
		return 0;

	case GPIO_REGMAP_GET_DIR_OP:
	case GPIO_REGMAP_SET_DIR_OP:
		*mask = RTD1625_GPIO_DIR;
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static int rtd1625_value_xlate(struct gpio_regmap *gpio,
			       enum gpio_regmap_operation op,
			       unsigned int base, unsigned int offset,
			       unsigned int reg, unsigned int *mask,
			       unsigned int *val)
{
	switch (op) {
	case GPIO_REGMAP_SET_OP:
		*val |= RTD1625_GPIO_WREN(RTD1625_GPIO_OUT);
		*mask |= RTD1625_GPIO_WREN(RTD1625_GPIO_OUT);
		return 0;

	case GPIO_REGMAP_SET_DIR_OP:
		*val |= RTD1625_GPIO_WREN(RTD1625_GPIO_DIR);
		*mask |= RTD1625_GPIO_WREN(RTD1625_GPIO_DIR);
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static int rtd1625_gpio_set_debounce(struct rtd1625_gpio *data, unsigned int offset,
				     unsigned int debounce)
{
	u8 deb_val;
	u32 val;

	switch (debounce) {
	case 1:
		deb_val = RTD1625_GPIO_DEBOUNCE_1US;
		break;
	case 10:
		deb_val = RTD1625_GPIO_DEBOUNCE_10US;
		break;
	case 100:
		deb_val = RTD1625_GPIO_DEBOUNCE_100US;
		break;
	case 1000:
		deb_val = RTD1625_GPIO_DEBOUNCE_1MS;
		break;
	case 10000:
		deb_val = RTD1625_GPIO_DEBOUNCE_10MS;
		break;
	case 20000:
		deb_val = RTD1625_GPIO_DEBOUNCE_20MS;
		break;
	case 30000:
		deb_val = RTD1625_GPIO_DEBOUNCE_30MS;
		break;
	case 50000:
		deb_val = RTD1625_GPIO_DEBOUNCE_50MS;
		break;
	default:
		return -ENOTSUPP;
	}

	val = FIELD_PREP(RTD1625_GPIO_DEBOUNCE, deb_val) | RTD1625_GPIO_DEBOUNCE_WREN;

	guard(raw_spinlock_irqsave)(&data->lock);

	return regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(offset), val);
}

static int rtd1625_gpio_set_config(struct gpio_regmap *gpio, struct gpio_chip *chip,
				   unsigned int offset, unsigned long config)
{
	struct rtd1625_gpio *data = gpio_regmap_get_drvdata(gpio);
	u32 debounce;

	if (pinconf_to_config_param(config) == PIN_CONFIG_INPUT_DEBOUNCE) {
		debounce = pinconf_to_config_argument(config);
		return rtd1625_gpio_set_debounce(data, offset, debounce);
	}

	return gpiochip_generic_config(chip, offset, config);
}

static void rtd1625_gpio_irq_handle(struct irq_desc *desc)
{
	unsigned int (*get_reg_offset)(struct rtd1625_gpio *gpio, unsigned int offset);
	struct rtd1625_gpio *data = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int irq = irq_desc_get_irq(desc);
	struct irq_domain *domain = data->domain;
	unsigned int reg_offset, i, j, val;
	irq_hw_number_t hwirq;
	unsigned long status;
	u32 irq_type;
	int ret;

	if (irq == data->irqs[RTD1625_IRQ_ASSERT])
		get_reg_offset = &rtd1625_gpio_gpa_offset;
	else if (irq == data->irqs[RTD1625_IRQ_DEASSERT])
		get_reg_offset = &rtd1625_gpio_gpda_offset;
	else if (irq == data->irqs[RTD1625_IRQ_LEVEL])
		get_reg_offset = &rtd1625_gpio_level_offset;
	else
		return;

	chained_irq_enter(chip, desc);

	for (i = 0; i < data->info->num_gpios; i += 32) {
		reg_offset = get_reg_offset(data, i);
		ret = regmap_read(data->regmap, reg_offset, &val);
		if (ret) {
			pr_err_ratelimited("Failed to read IRQ status for GPIO %u: %d\n", i, ret);
			continue;
		}

		status = val;

		/*
		 * Hardware quirk: The controller fires both "assert" and "de-assert"
		 * interrupts simultaneously on any edge toggle.
		 * We must pre-clear edge interrupts here. If we drop an unwanted
		 * de-assert interrupt below, it will never reach the IRQ core
		 * (generic_handle_domain_irq), meaning ->irq_ack() won't be called.
		 * Failing to clear it here leads to an interrupt storm.
		 */
		if (irq != data->irqs[RTD1625_IRQ_LEVEL]) {
			ret = regmap_write(data->regmap, reg_offset, status);
			if (ret)
				pr_err_ratelimited("Failed to clear edge IRQ for GPIO %u: %d\n",
						   i, ret);
		}

		for_each_set_bit(j, &status, 32) {
			hwirq = i + j;
			irq_type = irq_get_trigger_type(irq_find_mapping(domain, hwirq));

			/*
			 * Filter out the hardware-forced de-assert interrupt unless
			 * the user explicitly requested IRQ_TYPE_EDGE_BOTH.
			 */
			if (irq == data->irqs[RTD1625_IRQ_DEASSERT] &&
			    irq_type != IRQ_TYPE_EDGE_BOTH)
				continue;

			generic_handle_domain_irq(domain, hwirq);
		}
	}

	chained_irq_exit(chip, desc);
}

static void rtd1625_gpio_ack_irq(struct irq_data *d)
{
	struct rtd1625_gpio *data = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 irq_type = irqd_get_trigger_type(d);
	u32 bit_mask = BIT(hwirq % 32);
	int reg_offset;

	if (irq_type & IRQ_TYPE_LEVEL_MASK) {
		reg_offset = rtd1625_gpio_level_offset(data, hwirq);
		regmap_write(data->regmap, reg_offset, bit_mask);
	}
}

static void rtd1625_gpio_enable_edge_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	int gpda_reg_offset = rtd1625_gpio_gpda_offset(data, hwirq);
	int gpa_reg_offset = rtd1625_gpio_gpa_offset(data, hwirq);
	u32 clr_mask = BIT(hwirq % 32);
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	regmap_write(data->regmap, gpa_reg_offset, clr_mask);
	regmap_write(data->regmap, gpda_reg_offset, clr_mask);
	val = RTD1625_GPIO_EDGE_INT_EN | RTD1625_GPIO_WREN(RTD1625_GPIO_EDGE_INT_EN);
	regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(hwirq), val);
}

static void rtd1625_gpio_disable_edge_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	val = RTD1625_GPIO_WREN(RTD1625_GPIO_EDGE_INT_EN);
	regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(hwirq), val);
}

static void rtd1625_gpio_enable_level_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	int level_reg_offset = rtd1625_gpio_level_offset(data, hwirq);
	u32 clr_mask = BIT(hwirq % 32);
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	regmap_write(data->regmap, level_reg_offset, clr_mask);
	val = RTD1625_GPIO_LEVEL_INT_EN | RTD1625_GPIO_WREN(RTD1625_GPIO_LEVEL_INT_EN);
	regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(hwirq), val);
}

static void rtd1625_gpio_disable_level_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	val = RTD1625_GPIO_WREN(RTD1625_GPIO_LEVEL_INT_EN);
	regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(hwirq), val);
}

static void rtd1625_gpio_enable_irq(struct irq_data *d)
{
	struct rtd1625_gpio *data = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 irq_type = irqd_get_trigger_type(d);

	gpio_regmap_enable_irq(data->gpio_reg, hwirq);

	if (irq_type & IRQ_TYPE_EDGE_BOTH)
		rtd1625_gpio_enable_edge_irq(data, hwirq);
	else if (irq_type & IRQ_TYPE_LEVEL_MASK)
		rtd1625_gpio_enable_level_irq(data, hwirq);
}

static void rtd1625_gpio_disable_irq(struct irq_data *d)
{
	struct rtd1625_gpio *data = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 irq_type = irqd_get_trigger_type(d);

	if (irq_type & IRQ_TYPE_EDGE_BOTH)
		rtd1625_gpio_disable_edge_irq(data, hwirq);
	else if (irq_type & IRQ_TYPE_LEVEL_MASK)
		rtd1625_gpio_disable_level_irq(data, hwirq);

	gpio_regmap_disable_irq(data->gpio_reg, hwirq);
}

static int rtd1625_gpio_irq_set_level_type(struct irq_data *d, bool level)
{
	u32 val = RTD1625_GPIO_WREN(RTD1625_GPIO_LEVEL_INT_DP);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct rtd1625_gpio *data;
	int ret;

	data = irq_data_get_irq_chip_data(d);
	if (!(data->info->irq_type_support & IRQ_TYPE_LEVEL_MASK))
		return -EINVAL;

	if (level)
		val |= RTD1625_GPIO_LEVEL_INT_DP;

	scoped_guard(raw_spinlock_irqsave, &data->lock) {
		ret = regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(hwirq),
				   val);
		if (ret)
			return ret;
	}

	irq_set_handler_locked(d, handle_level_irq);

	return 0;
}

static int rtd1625_gpio_irq_set_edge_type(struct irq_data *d, bool polarity)
{
	struct rtd1625_gpio *data = irq_data_get_irq_chip_data(d);
	u32 val = RTD1625_GPIO_WREN(RTD1625_GPIO_EDGE_INT_DP);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	int ret;

	if (!(data->info->irq_type_support & IRQ_TYPE_EDGE_BOTH))
		return -EINVAL;

	if (polarity)
		val |= RTD1625_GPIO_EDGE_INT_DP;

	scoped_guard(raw_spinlock_irqsave, &data->lock) {
		ret = regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(hwirq),
				   val);
		if (ret)
			return ret;
	}

	irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static int rtd1625_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		return rtd1625_gpio_irq_set_edge_type(d, 1);

	case IRQ_TYPE_EDGE_FALLING:
		return rtd1625_gpio_irq_set_edge_type(d, 0);

	case IRQ_TYPE_EDGE_BOTH:
		return rtd1625_gpio_irq_set_edge_type(d, 1);

	case IRQ_TYPE_LEVEL_HIGH:
		return rtd1625_gpio_irq_set_level_type(d, 0);

	case IRQ_TYPE_LEVEL_LOW:
		return rtd1625_gpio_irq_set_level_type(d, 1);

	default:
		return -EINVAL;
	}
}

static int rtd1625_gpio_irq_request_resources(struct irq_data *d)
{
	struct rtd1625_gpio *data = irq_data_get_irq_chip_data(d);

	return gpio_regmap_reqres_irq(data->gpio_reg, d->hwirq);
}

static void rtd1625_gpio_irq_release_resources(struct irq_data *d)
{
	struct rtd1625_gpio *data = irq_data_get_irq_chip_data(d);

	gpio_regmap_relres_irq(data->gpio_reg, d->hwirq);
}

static struct irq_chip rtd1625_iso_gpio_irq_chip = {
	.name = "rtd1625-gpio",
	.irq_ack = rtd1625_gpio_ack_irq,
	.irq_mask = rtd1625_gpio_disable_irq,
	.irq_unmask = rtd1625_gpio_enable_irq,
	.irq_set_type = rtd1625_gpio_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE | IRQCHIP_SKIP_SET_WAKE,
	.irq_request_resources = rtd1625_gpio_irq_request_resources,
	.irq_release_resources = rtd1625_gpio_irq_release_resources,
};

static int rtd1625_gpio_setup_irq(struct platform_device *pdev, struct rtd1625_gpio *data)
{
	unsigned int num_irqs;
	int irq;

	/* IRQ is optional; operate as basic GPIO if absent */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq == -ENXIO)
		return 0;
	if (irq < 0)
		return irq;

	num_irqs = (data->info->irq_type_support & IRQ_TYPE_LEVEL_MASK) ? 3 : 2;

	for (unsigned int i = 0; i < num_irqs; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		data->irqs[i] = irq;
		irq_set_chained_handler_and_data(data->irqs[i], rtd1625_gpio_irq_handle, data);
	}

	return 0;
}

static int rtd1625_gpio_irq_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct rtd1625_gpio *data = domain->host_data;

	irq_set_chip_data(irq, data);

	irq_set_chip_and_handler(irq, &rtd1625_iso_gpio_irq_chip, handle_bad_irq);

	irq_set_noprobe(irq);

	irq_set_lockdep_class(irq, &rtd1625_gpio_irq_lock_class,
			      &rtd1625_gpio_irq_request_class);

	return 0;
}

static const struct irq_domain_ops rtd1625_gpio_irq_domain_ops = {
	.map = rtd1625_gpio_irq_map,
	.xlate = irq_domain_xlate_twocell,
};

static const struct regmap_config rtd1625_gpio_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
#ifndef CONFIG_DEBUG_GPIO
	.disable_locking = true,
#endif
};

static int rtd1625_gpio_probe(struct platform_device *pdev)
{
	struct gpio_regmap_config config = {};
	struct irq_domain_info d_info = {};
	struct device *dev = &pdev->dev;
	struct gpio_regmap *gpio_reg;
	struct rtd1625_gpio *data;
	void __iomem *irq_base;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->info = device_get_match_data(dev);
	if (!data->info)
		return -ENODATA;

	irq_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(irq_base))
		return PTR_ERR(irq_base);

	data->regmap = devm_regmap_init_mmio(dev, irq_base, &rtd1625_gpio_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	raw_spin_lock_init(&data->lock);
	platform_set_drvdata(pdev, data);

	data->save_regs = devm_kcalloc(dev, data->info->num_gpios, sizeof(*data->save_regs),
				       GFP_KERNEL);
	if (!data->save_regs)
		return -ENOMEM;

	config.parent = dev;
	config.regmap = data->regmap;
	config.ngpio = data->info->num_gpios;
	config.reg_dat_base = data->info->base_offset;
	config.reg_set_base = data->info->base_offset;
	config.reg_dir_out_base = data->info->base_offset;

	config.reg_mask_xlate = rtd1625_reg_mask_xlate;
	config.set_config = rtd1625_gpio_set_config;
	config.value_xlate = rtd1625_value_xlate;

	d_info.fwnode = dev_fwnode(&pdev->dev);
	d_info.size = data->info->num_gpios;
	d_info.hwirq_max = data->info->num_gpios;
	d_info.ops = &rtd1625_gpio_irq_domain_ops;
	d_info.host_data = data;

	data->domain = devm_irq_domain_instantiate(dev, &d_info);
	if (IS_ERR(data->domain))
		return PTR_ERR(data->domain);

	ret = rtd1625_gpio_setup_irq(pdev, data);
	if (ret)
		return ret;

	config.irq_domain = data->domain;
	config.drvdata = data;

	gpio_reg = devm_gpio_regmap_register(dev, &config);
	if (IS_ERR(gpio_reg))
		return PTR_ERR(gpio_reg);

	data->gpio_reg = gpio_reg;

	return 0;
}

static const struct rtd1625_gpio_info rtd1625_iso_gpio_info = {
	.num_gpios        = 166,
	.irq_type_support = IRQ_TYPE_EDGE_BOTH,
	.base_offset      = 0x100,
	.gpa_offset       = 0x000,
	.gpda_offset      = 0x020,
	.write_en_all     = RTD1625_ISO_GPIO_WREN_ALL,
};

static const struct rtd1625_gpio_info rtd1625_isom_gpio_info = {
	.num_gpios        = 4,
	.irq_type_support = IRQ_TYPE_EDGE_BOTH | IRQ_TYPE_LEVEL_LOW |
			    IRQ_TYPE_LEVEL_HIGH,
	.base_offset      = 0x20,
	.gpa_offset       = 0x00,
	.gpda_offset      = 0x04,
	.level_offset     = 0x18,
	.write_en_all     = RTD1625_ISOM_GPIO_WREN_ALL,
};

static int rtd1625_gpio_suspend(struct device *dev)
{
	struct rtd1625_gpio *data = dev_get_drvdata(dev);
	const struct rtd1625_gpio_info *info = data->info;
	int ret;

	for (unsigned int i = 0; i < info->num_gpios; i++) {
		ret = regmap_read(data->regmap, data->info->base_offset + GPIO_CONTROL(i),
				  &data->save_regs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtd1625_gpio_resume(struct device *dev)
{
	struct rtd1625_gpio *data = dev_get_drvdata(dev);
	const struct rtd1625_gpio_info *info = data->info;
	int ret;

	for (unsigned int i = 0; i < info->num_gpios; i++) {
		ret = regmap_write(data->regmap, data->info->base_offset + GPIO_CONTROL(i),
				   data->save_regs[i] | info->write_en_all);
		if (ret)
			return ret;
	}

	return 0;
}

static DEFINE_NOIRQ_DEV_PM_OPS(rtd1625_gpio_pm_ops, rtd1625_gpio_suspend, rtd1625_gpio_resume);

static const struct of_device_id rtd1625_gpio_of_matches[] = {
	{ .compatible = "realtek,rtd1625-iso-gpio", .data = &rtd1625_iso_gpio_info },
	{ .compatible = "realtek,rtd1625-isom-gpio", .data = &rtd1625_isom_gpio_info },
	{ }
};
MODULE_DEVICE_TABLE(of, rtd1625_gpio_of_matches);

static struct platform_driver rtd1625_gpio_platform_driver = {
	.driver = {
		.name = "gpio-rtd1625",
		.of_match_table = rtd1625_gpio_of_matches,
		.pm = pm_sleep_ptr(&rtd1625_gpio_pm_ops),
	},
	.probe = rtd1625_gpio_probe,
};
module_platform_driver(rtd1625_gpio_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Realtek Semiconductor Corporation");
MODULE_DESCRIPTION("Realtek DHC SoC RTD1625 gpio driver");
