// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 * Copyright (C) 2018 Linaro Ltd.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* EIC registers definition */
#define SPRD_PMIC_EIC_DATA		0x0
#define SPRD_PMIC_EIC_DMSK		0x4
#define SPRD_PMIC_EIC_IEV		0x14
#define SPRD_PMIC_EIC_IE		0x18
#define SPRD_PMIC_EIC_RIS		0x1c
#define SPRD_PMIC_EIC_MIS		0x20
#define SPRD_PMIC_EIC_IC		0x24
#define SPRD_PMIC_EIC_TRIG		0x28
#define SPRD_PMIC_EIC_CTRL0		0x40

/*
 * The PMIC EIC controller only has one bank, and each bank now can contain
 * 16 EICs.
 */
#define SPRD_PMIC_EIC_PER_BANK_NR	16
#define SPRD_PMIC_EIC_NR		SPRD_PMIC_EIC_PER_BANK_NR
#define SPRD_PMIC_EIC_DATA_MASK		GENMASK(15, 0)
#define SPRD_PMIC_EIC_BIT(x)		((x) & (SPRD_PMIC_EIC_PER_BANK_NR - 1))
#define SPRD_PMIC_EIC_DBNC_MASK		GENMASK(11, 0)

/*
 * These registers are modified under the irq bus lock and cached to avoid
 * unnecessary writes in bus_sync_unlock.
 */
enum {
	REG_IEV,
	REG_IE,
	REG_TRIG,
	CACHE_NR_REGS
};

/**
 * struct sprd_pmic_eic - PMIC EIC controller
 * @chip: the gpio_chip structure.
 * @intc: the irq_chip structure.
 * @regmap: the regmap from the parent device.
 * @offset: the EIC controller's offset address of the PMIC.
 * @reg: the array to cache the EIC registers.
 * @buslock: for bus lock/sync and unlock.
 * @irq: the interrupt number of the PMIC EIC conteroller.
 */
struct sprd_pmic_eic {
	struct gpio_chip chip;
	struct irq_chip intc;
	struct regmap *map;
	u32 offset;
	u8 reg[CACHE_NR_REGS];
	struct mutex buslock;
	int irq;
};

static void sprd_pmic_eic_update(struct gpio_chip *chip, unsigned int offset,
				 u16 reg, unsigned int val)
{
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);
	u32 shift = SPRD_PMIC_EIC_BIT(offset);

	regmap_update_bits(pmic_eic->map, pmic_eic->offset + reg,
			   BIT(shift), val << shift);
}

static int sprd_pmic_eic_read(struct gpio_chip *chip, unsigned int offset,
			      u16 reg)
{
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);
	u32 value;
	int ret;

	ret = regmap_read(pmic_eic->map, pmic_eic->offset + reg, &value);
	if (ret)
		return ret;

	return !!(value & BIT(SPRD_PMIC_EIC_BIT(offset)));
}

static int sprd_pmic_eic_request(struct gpio_chip *chip, unsigned int offset)
{
	sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_DMSK, 1);
	return 0;
}

static void sprd_pmic_eic_free(struct gpio_chip *chip, unsigned int offset)
{
	sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_DMSK, 0);
}

static int sprd_pmic_eic_get(struct gpio_chip *chip, unsigned int offset)
{
	return sprd_pmic_eic_read(chip, offset, SPRD_PMIC_EIC_DATA);
}

static int sprd_pmic_eic_direction_input(struct gpio_chip *chip,
					 unsigned int offset)
{
	/* EICs are always input, nothing need to do here. */
	return 0;
}

static void sprd_pmic_eic_set(struct gpio_chip *chip, unsigned int offset,
			      int value)
{
	/* EICs are always input, nothing need to do here. */
}

static int sprd_pmic_eic_set_debounce(struct gpio_chip *chip,
				      unsigned int offset,
				      unsigned int debounce)
{
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);
	u32 reg, value;
	int ret;

	reg = SPRD_PMIC_EIC_CTRL0 + SPRD_PMIC_EIC_BIT(offset) * 0x4;
	ret = regmap_read(pmic_eic->map, pmic_eic->offset + reg, &value);
	if (ret)
		return ret;

	value &= ~SPRD_PMIC_EIC_DBNC_MASK;
	value |= (debounce / 1000) & SPRD_PMIC_EIC_DBNC_MASK;
	return regmap_write(pmic_eic->map, pmic_eic->offset + reg, value);
}

static int sprd_pmic_eic_set_config(struct gpio_chip *chip, unsigned int offset,
				    unsigned long config)
{
	unsigned long param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	if (param == PIN_CONFIG_INPUT_DEBOUNCE)
		return sprd_pmic_eic_set_debounce(chip, offset, arg);

	return -ENOTSUPP;
}

static void sprd_pmic_eic_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);

	pmic_eic->reg[REG_IE] = 0;
	pmic_eic->reg[REG_TRIG] = 0;
}

static void sprd_pmic_eic_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);

	pmic_eic->reg[REG_IE] = 1;
	pmic_eic->reg[REG_TRIG] = 1;
}

static int sprd_pmic_eic_irq_set_type(struct irq_data *data,
				      unsigned int flow_type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);

	switch (flow_type) {
	case IRQ_TYPE_LEVEL_HIGH:
		pmic_eic->reg[REG_IEV] = 1;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		pmic_eic->reg[REG_IEV] = 0;
		break;
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		/*
		 * Will set the trigger level according to current EIC level
		 * in irq_bus_sync_unlock() interface, so here nothing to do.
		 */
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static void sprd_pmic_eic_bus_lock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);

	mutex_lock(&pmic_eic->buslock);
}

static void sprd_pmic_eic_bus_sync_unlock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_pmic_eic *pmic_eic = gpiochip_get_data(chip);
	u32 trigger = irqd_get_trigger_type(data);
	u32 offset = irqd_to_hwirq(data);
	int state;

	/* Set irq type */
	if (trigger & IRQ_TYPE_EDGE_BOTH) {
		state = sprd_pmic_eic_get(chip, offset);
		if (state)
			sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_IEV, 0);
		else
			sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_IEV, 1);
	} else {
		sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_IEV,
				     pmic_eic->reg[REG_IEV]);
	}

	/* Set irq unmask */
	sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_IE,
			     pmic_eic->reg[REG_IE]);
	/* Generate trigger start pulse for debounce EIC */
	sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_TRIG,
			     pmic_eic->reg[REG_TRIG]);

	mutex_unlock(&pmic_eic->buslock);
}

static void sprd_pmic_eic_toggle_trigger(struct gpio_chip *chip,
					 unsigned int irq, unsigned int offset)
{
	u32 trigger = irq_get_trigger_type(irq);
	int state, post_state;

	if (!(trigger & IRQ_TYPE_EDGE_BOTH))
		return;

	state = sprd_pmic_eic_get(chip, offset);
retry:
	if (state)
		sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_IEV, 0);
	else
		sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_IEV, 1);

	post_state = sprd_pmic_eic_get(chip, offset);
	if (state != post_state) {
		dev_warn(chip->parent, "PMIC EIC level was changed.\n");
		state = post_state;
		goto retry;
	}

	/* Set irq unmask */
	sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_IE, 1);
	/* Generate trigger start pulse for debounce EIC */
	sprd_pmic_eic_update(chip, offset, SPRD_PMIC_EIC_TRIG, 1);
}

static irqreturn_t sprd_pmic_eic_irq_handler(int irq, void *data)
{
	struct sprd_pmic_eic *pmic_eic = data;
	struct gpio_chip *chip = &pmic_eic->chip;
	unsigned long status;
	u32 n, girq, val;
	int ret;

	ret = regmap_read(pmic_eic->map, pmic_eic->offset + SPRD_PMIC_EIC_MIS,
			  &val);
	if (ret)
		return IRQ_RETVAL(ret);

	status = val & SPRD_PMIC_EIC_DATA_MASK;

	for_each_set_bit(n, &status, chip->ngpio) {
		/* Clear the interrupt */
		sprd_pmic_eic_update(chip, n, SPRD_PMIC_EIC_IC, 1);

		girq = irq_find_mapping(chip->irq.domain, n);
		handle_nested_irq(girq);

		/*
		 * The PMIC EIC can only support level trigger, so we can
		 * toggle the level trigger to emulate the edge trigger.
		 */
		sprd_pmic_eic_toggle_trigger(chip, girq, n);
	}

	return IRQ_HANDLED;
}

static int sprd_pmic_eic_probe(struct platform_device *pdev)
{
	struct gpio_irq_chip *irq;
	struct sprd_pmic_eic *pmic_eic;
	int ret;

	pmic_eic = devm_kzalloc(&pdev->dev, sizeof(*pmic_eic), GFP_KERNEL);
	if (!pmic_eic)
		return -ENOMEM;

	mutex_init(&pmic_eic->buslock);

	pmic_eic->irq = platform_get_irq(pdev, 0);
	if (pmic_eic->irq < 0) {
		dev_err(&pdev->dev, "Failed to get PMIC EIC interrupt.\n");
		return pmic_eic->irq;
	}

	pmic_eic->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pmic_eic->map)
		return -ENODEV;

	ret = of_property_read_u32(pdev->dev.of_node, "reg", &pmic_eic->offset);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get PMIC EIC base address.\n");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, pmic_eic->irq, NULL,
					sprd_pmic_eic_irq_handler,
					IRQF_ONESHOT | IRQF_NO_SUSPEND,
					dev_name(&pdev->dev), pmic_eic);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request PMIC EIC IRQ.\n");
		return ret;
	}

	pmic_eic->chip.label = dev_name(&pdev->dev);
	pmic_eic->chip.ngpio = SPRD_PMIC_EIC_NR;
	pmic_eic->chip.base = -1;
	pmic_eic->chip.parent = &pdev->dev;
	pmic_eic->chip.of_node = pdev->dev.of_node;
	pmic_eic->chip.direction_input = sprd_pmic_eic_direction_input;
	pmic_eic->chip.request = sprd_pmic_eic_request;
	pmic_eic->chip.free = sprd_pmic_eic_free;
	pmic_eic->chip.set_config = sprd_pmic_eic_set_config;
	pmic_eic->chip.set = sprd_pmic_eic_set;
	pmic_eic->chip.get = sprd_pmic_eic_get;

	pmic_eic->intc.name = dev_name(&pdev->dev);
	pmic_eic->intc.irq_mask = sprd_pmic_eic_irq_mask;
	pmic_eic->intc.irq_unmask = sprd_pmic_eic_irq_unmask;
	pmic_eic->intc.irq_set_type = sprd_pmic_eic_irq_set_type;
	pmic_eic->intc.irq_bus_lock = sprd_pmic_eic_bus_lock;
	pmic_eic->intc.irq_bus_sync_unlock = sprd_pmic_eic_bus_sync_unlock;
	pmic_eic->intc.flags = IRQCHIP_SKIP_SET_WAKE;

	irq = &pmic_eic->chip.irq;
	irq->chip = &pmic_eic->intc;
	irq->threaded = true;

	ret = devm_gpiochip_add_data(&pdev->dev, &pmic_eic->chip, pmic_eic);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip %d.\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pmic_eic);
	return 0;
}

static const struct of_device_id sprd_pmic_eic_of_match[] = {
	{ .compatible = "sprd,sc2731-eic", },
	{ /* end of list */ }
};
MODULE_DEVICE_TABLE(of, sprd_pmic_eic_of_match);

static struct platform_driver sprd_pmic_eic_driver = {
	.probe = sprd_pmic_eic_probe,
	.driver = {
		.name = "sprd-pmic-eic",
		.of_match_table	= sprd_pmic_eic_of_match,
	},
};

module_platform_driver(sprd_pmic_eic_driver);

MODULE_DESCRIPTION("Spreadtrum PMIC EIC driver");
MODULE_LICENSE("GPL v2");
