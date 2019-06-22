// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 * Copyright (C) 2018 Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

/* EIC registers definition */
#define SPRD_EIC_DBNC_DATA		0x0
#define SPRD_EIC_DBNC_DMSK		0x4
#define SPRD_EIC_DBNC_IEV		0x14
#define SPRD_EIC_DBNC_IE		0x18
#define SPRD_EIC_DBNC_RIS		0x1c
#define SPRD_EIC_DBNC_MIS		0x20
#define SPRD_EIC_DBNC_IC		0x24
#define SPRD_EIC_DBNC_TRIG		0x28
#define SPRD_EIC_DBNC_CTRL0		0x40

#define SPRD_EIC_LATCH_INTEN		0x0
#define SPRD_EIC_LATCH_INTRAW		0x4
#define SPRD_EIC_LATCH_INTMSK		0x8
#define SPRD_EIC_LATCH_INTCLR		0xc
#define SPRD_EIC_LATCH_INTPOL		0x10
#define SPRD_EIC_LATCH_INTMODE		0x14

#define SPRD_EIC_ASYNC_INTIE		0x0
#define SPRD_EIC_ASYNC_INTRAW		0x4
#define SPRD_EIC_ASYNC_INTMSK		0x8
#define SPRD_EIC_ASYNC_INTCLR		0xc
#define SPRD_EIC_ASYNC_INTMODE		0x10
#define SPRD_EIC_ASYNC_INTBOTH		0x14
#define SPRD_EIC_ASYNC_INTPOL		0x18
#define SPRD_EIC_ASYNC_DATA		0x1c

#define SPRD_EIC_SYNC_INTIE		0x0
#define SPRD_EIC_SYNC_INTRAW		0x4
#define SPRD_EIC_SYNC_INTMSK		0x8
#define SPRD_EIC_SYNC_INTCLR		0xc
#define SPRD_EIC_SYNC_INTMODE		0x10
#define SPRD_EIC_SYNC_INTBOTH		0x14
#define SPRD_EIC_SYNC_INTPOL		0x18
#define SPRD_EIC_SYNC_DATA		0x1c

/*
 * The digital-chip EIC controller can support maximum 3 banks, and each bank
 * contains 8 EICs.
 */
#define SPRD_EIC_MAX_BANK		3
#define SPRD_EIC_PER_BANK_NR		8
#define SPRD_EIC_DATA_MASK		GENMASK(7, 0)
#define SPRD_EIC_BIT(x)			((x) & (SPRD_EIC_PER_BANK_NR - 1))
#define SPRD_EIC_DBNC_MASK		GENMASK(11, 0)

/*
 * The Spreadtrum EIC (external interrupt controller) can be used only in
 * input mode to generate interrupts if detecting input signals.
 *
 * The Spreadtrum digital-chip EIC controller contains 4 sub-modules:
 * debounce EIC, latch EIC, async EIC and sync EIC,
 *
 * The debounce EIC is used to capture the input signals' stable status
 * (millisecond resolution) and a single-trigger mechanism is introduced
 * into this sub-module to enhance the input event detection reliability.
 * The debounce range is from 1ms to 4s with a step size of 1ms.
 *
 * The latch EIC is used to latch some special power down signals and
 * generate interrupts, since the latch EIC does not depend on the APB clock
 * to capture signals.
 *
 * The async EIC uses a 32k clock to capture the short signals (microsecond
 * resolution) to generate interrupts by level or edge trigger.
 *
 * The EIC-sync is similar with GPIO's input function, which is a synchronized
 * signal input register.
 */
enum sprd_eic_type {
	SPRD_EIC_DEBOUNCE,
	SPRD_EIC_LATCH,
	SPRD_EIC_ASYNC,
	SPRD_EIC_SYNC,
	SPRD_EIC_MAX,
};

struct sprd_eic {
	struct gpio_chip chip;
	struct irq_chip intc;
	void __iomem *base[SPRD_EIC_MAX_BANK];
	enum sprd_eic_type type;
	spinlock_t lock;
	int irq;
};

struct sprd_eic_variant_data {
	enum sprd_eic_type type;
	u32 num_eics;
};

static const char *sprd_eic_label_name[SPRD_EIC_MAX] = {
	"eic-debounce", "eic-latch", "eic-async",
	"eic-sync",
};

static const struct sprd_eic_variant_data sc9860_eic_dbnc_data = {
	.type = SPRD_EIC_DEBOUNCE,
	.num_eics = 8,
};

static const struct sprd_eic_variant_data sc9860_eic_latch_data = {
	.type = SPRD_EIC_LATCH,
	.num_eics = 8,
};

static const struct sprd_eic_variant_data sc9860_eic_async_data = {
	.type = SPRD_EIC_ASYNC,
	.num_eics = 8,
};

static const struct sprd_eic_variant_data sc9860_eic_sync_data = {
	.type = SPRD_EIC_SYNC,
	.num_eics = 8,
};

static inline void __iomem *sprd_eic_offset_base(struct sprd_eic *sprd_eic,
						 unsigned int bank)
{
	if (bank >= SPRD_EIC_MAX_BANK)
		return NULL;

	return sprd_eic->base[bank];
}

static void sprd_eic_update(struct gpio_chip *chip, unsigned int offset,
			    u16 reg, unsigned int val)
{
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	void __iomem *base =
		sprd_eic_offset_base(sprd_eic, offset / SPRD_EIC_PER_BANK_NR);
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&sprd_eic->lock, flags);
	tmp = readl_relaxed(base + reg);

	if (val)
		tmp |= BIT(SPRD_EIC_BIT(offset));
	else
		tmp &= ~BIT(SPRD_EIC_BIT(offset));

	writel_relaxed(tmp, base + reg);
	spin_unlock_irqrestore(&sprd_eic->lock, flags);
}

static int sprd_eic_read(struct gpio_chip *chip, unsigned int offset, u16 reg)
{
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	void __iomem *base =
		sprd_eic_offset_base(sprd_eic, offset / SPRD_EIC_PER_BANK_NR);

	return !!(readl_relaxed(base + reg) & BIT(SPRD_EIC_BIT(offset)));
}

static int sprd_eic_request(struct gpio_chip *chip, unsigned int offset)
{
	sprd_eic_update(chip, offset, SPRD_EIC_DBNC_DMSK, 1);
	return 0;
}

static void sprd_eic_free(struct gpio_chip *chip, unsigned int offset)
{
	sprd_eic_update(chip, offset, SPRD_EIC_DBNC_DMSK, 0);
}

static int sprd_eic_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);

	switch (sprd_eic->type) {
	case SPRD_EIC_DEBOUNCE:
		return sprd_eic_read(chip, offset, SPRD_EIC_DBNC_DATA);
	case SPRD_EIC_ASYNC:
		return sprd_eic_read(chip, offset, SPRD_EIC_ASYNC_DATA);
	case SPRD_EIC_SYNC:
		return sprd_eic_read(chip, offset, SPRD_EIC_SYNC_DATA);
	default:
		return -ENOTSUPP;
	}
}

static int sprd_eic_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	/* EICs are always input, nothing need to do here. */
	return 0;
}

static void sprd_eic_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	/* EICs are always input, nothing need to do here. */
}

static int sprd_eic_set_debounce(struct gpio_chip *chip, unsigned int offset,
				 unsigned int debounce)
{
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	void __iomem *base =
		sprd_eic_offset_base(sprd_eic, offset / SPRD_EIC_PER_BANK_NR);
	u32 reg = SPRD_EIC_DBNC_CTRL0 + SPRD_EIC_BIT(offset) * 0x4;
	u32 value = readl_relaxed(base + reg) & ~SPRD_EIC_DBNC_MASK;

	value |= (debounce / 1000) & SPRD_EIC_DBNC_MASK;
	writel_relaxed(value, base + reg);

	return 0;
}

static int sprd_eic_set_config(struct gpio_chip *chip, unsigned int offset,
			       unsigned long config)
{
	unsigned long param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	if (param == PIN_CONFIG_INPUT_DEBOUNCE)
		return sprd_eic_set_debounce(chip, offset, arg);

	return -ENOTSUPP;
}

static void sprd_eic_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	u32 offset = irqd_to_hwirq(data);

	switch (sprd_eic->type) {
	case SPRD_EIC_DEBOUNCE:
		sprd_eic_update(chip, offset, SPRD_EIC_DBNC_IE, 0);
		sprd_eic_update(chip, offset, SPRD_EIC_DBNC_TRIG, 0);
		break;
	case SPRD_EIC_LATCH:
		sprd_eic_update(chip, offset, SPRD_EIC_LATCH_INTEN, 0);
		break;
	case SPRD_EIC_ASYNC:
		sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTIE, 0);
		break;
	case SPRD_EIC_SYNC:
		sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTIE, 0);
		break;
	default:
		dev_err(chip->parent, "Unsupported EIC type.\n");
	}
}

static void sprd_eic_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	u32 offset = irqd_to_hwirq(data);

	switch (sprd_eic->type) {
	case SPRD_EIC_DEBOUNCE:
		sprd_eic_update(chip, offset, SPRD_EIC_DBNC_IE, 1);
		sprd_eic_update(chip, offset, SPRD_EIC_DBNC_TRIG, 1);
		break;
	case SPRD_EIC_LATCH:
		sprd_eic_update(chip, offset, SPRD_EIC_LATCH_INTEN, 1);
		break;
	case SPRD_EIC_ASYNC:
		sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTIE, 1);
		break;
	case SPRD_EIC_SYNC:
		sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTIE, 1);
		break;
	default:
		dev_err(chip->parent, "Unsupported EIC type.\n");
	}
}

static void sprd_eic_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	u32 offset = irqd_to_hwirq(data);

	switch (sprd_eic->type) {
	case SPRD_EIC_DEBOUNCE:
		sprd_eic_update(chip, offset, SPRD_EIC_DBNC_IC, 1);
		break;
	case SPRD_EIC_LATCH:
		sprd_eic_update(chip, offset, SPRD_EIC_LATCH_INTCLR, 1);
		break;
	case SPRD_EIC_ASYNC:
		sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTCLR, 1);
		break;
	case SPRD_EIC_SYNC:
		sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTCLR, 1);
		break;
	default:
		dev_err(chip->parent, "Unsupported EIC type.\n");
	}
}

static int sprd_eic_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	u32 offset = irqd_to_hwirq(data);
	int state;

	switch (sprd_eic->type) {
	case SPRD_EIC_DEBOUNCE:
		switch (flow_type) {
		case IRQ_TYPE_LEVEL_HIGH:
			sprd_eic_update(chip, offset, SPRD_EIC_DBNC_IEV, 1);
			break;
		case IRQ_TYPE_LEVEL_LOW:
			sprd_eic_update(chip, offset, SPRD_EIC_DBNC_IEV, 0);
			break;
		case IRQ_TYPE_EDGE_RISING:
		case IRQ_TYPE_EDGE_FALLING:
		case IRQ_TYPE_EDGE_BOTH:
			state = sprd_eic_get(chip, offset);
			if (state)
				sprd_eic_update(chip, offset,
						SPRD_EIC_DBNC_IEV, 0);
			else
				sprd_eic_update(chip, offset,
						SPRD_EIC_DBNC_IEV, 1);
			break;
		default:
			return -ENOTSUPP;
		}

		irq_set_handler_locked(data, handle_level_irq);
		break;
	case SPRD_EIC_LATCH:
		switch (flow_type) {
		case IRQ_TYPE_LEVEL_HIGH:
			sprd_eic_update(chip, offset, SPRD_EIC_LATCH_INTPOL, 0);
			break;
		case IRQ_TYPE_LEVEL_LOW:
			sprd_eic_update(chip, offset, SPRD_EIC_LATCH_INTPOL, 1);
			break;
		case IRQ_TYPE_EDGE_RISING:
		case IRQ_TYPE_EDGE_FALLING:
		case IRQ_TYPE_EDGE_BOTH:
			state = sprd_eic_get(chip, offset);
			if (state)
				sprd_eic_update(chip, offset,
						SPRD_EIC_LATCH_INTPOL, 0);
			else
				sprd_eic_update(chip, offset,
						SPRD_EIC_LATCH_INTPOL, 1);
			break;
		default:
			return -ENOTSUPP;
		}

		irq_set_handler_locked(data, handle_level_irq);
		break;
	case SPRD_EIC_ASYNC:
		switch (flow_type) {
		case IRQ_TYPE_EDGE_RISING:
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTMODE, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTPOL, 1);
			irq_set_handler_locked(data, handle_edge_irq);
			break;
		case IRQ_TYPE_EDGE_FALLING:
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTMODE, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTPOL, 0);
			irq_set_handler_locked(data, handle_edge_irq);
			break;
		case IRQ_TYPE_EDGE_BOTH:
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTMODE, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTBOTH, 1);
			irq_set_handler_locked(data, handle_edge_irq);
			break;
		case IRQ_TYPE_LEVEL_HIGH:
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTMODE, 1);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTPOL, 1);
			irq_set_handler_locked(data, handle_level_irq);
			break;
		case IRQ_TYPE_LEVEL_LOW:
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTMODE, 1);
			sprd_eic_update(chip, offset, SPRD_EIC_ASYNC_INTPOL, 0);
			irq_set_handler_locked(data, handle_level_irq);
			break;
		default:
			return -ENOTSUPP;
		}
		break;
	case SPRD_EIC_SYNC:
		switch (flow_type) {
		case IRQ_TYPE_EDGE_RISING:
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTMODE, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTPOL, 1);
			irq_set_handler_locked(data, handle_edge_irq);
			break;
		case IRQ_TYPE_EDGE_FALLING:
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTMODE, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTPOL, 0);
			irq_set_handler_locked(data, handle_edge_irq);
			break;
		case IRQ_TYPE_EDGE_BOTH:
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTMODE, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTBOTH, 1);
			irq_set_handler_locked(data, handle_edge_irq);
			break;
		case IRQ_TYPE_LEVEL_HIGH:
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTMODE, 1);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTPOL, 1);
			irq_set_handler_locked(data, handle_level_irq);
			break;
		case IRQ_TYPE_LEVEL_LOW:
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTBOTH, 0);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTMODE, 1);
			sprd_eic_update(chip, offset, SPRD_EIC_SYNC_INTPOL, 0);
			irq_set_handler_locked(data, handle_level_irq);
			break;
		default:
			return -ENOTSUPP;
		}
	default:
		dev_err(chip->parent, "Unsupported EIC type.\n");
		return -ENOTSUPP;
	}

	return 0;
}

static void sprd_eic_toggle_trigger(struct gpio_chip *chip, unsigned int irq,
				    unsigned int offset)
{
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	struct irq_data *data = irq_get_irq_data(irq);
	u32 trigger = irqd_get_trigger_type(data);
	int state, post_state;

	/*
	 * The debounce EIC and latch EIC can only support level trigger, so we
	 * can toggle the level trigger to emulate the edge trigger.
	 */
	if ((sprd_eic->type != SPRD_EIC_DEBOUNCE &&
	     sprd_eic->type != SPRD_EIC_LATCH) ||
	    !(trigger & IRQ_TYPE_EDGE_BOTH))
		return;

	sprd_eic_irq_mask(data);
	state = sprd_eic_get(chip, offset);

retry:
	switch (sprd_eic->type) {
	case SPRD_EIC_DEBOUNCE:
		if (state)
			sprd_eic_update(chip, offset, SPRD_EIC_DBNC_IEV, 0);
		else
			sprd_eic_update(chip, offset, SPRD_EIC_DBNC_IEV, 1);
		break;
	case SPRD_EIC_LATCH:
		if (state)
			sprd_eic_update(chip, offset, SPRD_EIC_LATCH_INTPOL, 0);
		else
			sprd_eic_update(chip, offset, SPRD_EIC_LATCH_INTPOL, 1);
		break;
	default:
		sprd_eic_irq_unmask(data);
		return;
	}

	post_state = sprd_eic_get(chip, offset);
	if (state != post_state) {
		dev_warn(chip->parent, "EIC level was changed.\n");
		state = post_state;
		goto retry;
	}

	sprd_eic_irq_unmask(data);
}

static int sprd_eic_match_chip_by_type(struct gpio_chip *chip, void *data)
{
	enum sprd_eic_type type = *(enum sprd_eic_type *)data;

	return !strcmp(chip->label, sprd_eic_label_name[type]);
}

static void sprd_eic_handle_one_type(struct gpio_chip *chip)
{
	struct sprd_eic *sprd_eic = gpiochip_get_data(chip);
	u32 bank, n, girq;

	for (bank = 0; bank * SPRD_EIC_PER_BANK_NR < chip->ngpio; bank++) {
		void __iomem *base = sprd_eic_offset_base(sprd_eic, bank);
		unsigned long reg;

		switch (sprd_eic->type) {
		case SPRD_EIC_DEBOUNCE:
			reg = readl_relaxed(base + SPRD_EIC_DBNC_MIS) &
				SPRD_EIC_DATA_MASK;
			break;
		case SPRD_EIC_LATCH:
			reg = readl_relaxed(base + SPRD_EIC_LATCH_INTMSK) &
				SPRD_EIC_DATA_MASK;
			break;
		case SPRD_EIC_ASYNC:
			reg = readl_relaxed(base + SPRD_EIC_ASYNC_INTMSK) &
				SPRD_EIC_DATA_MASK;
			break;
		case SPRD_EIC_SYNC:
			reg = readl_relaxed(base + SPRD_EIC_SYNC_INTMSK) &
				SPRD_EIC_DATA_MASK;
			break;
		default:
			dev_err(chip->parent, "Unsupported EIC type.\n");
			return;
		}

		for_each_set_bit(n, &reg, SPRD_EIC_PER_BANK_NR) {
			girq = irq_find_mapping(chip->irq.domain,
					bank * SPRD_EIC_PER_BANK_NR + n);

			generic_handle_irq(girq);
			sprd_eic_toggle_trigger(chip, girq, n);
		}
	}
}

static void sprd_eic_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *ic = irq_desc_get_chip(desc);
	struct gpio_chip *chip;
	enum sprd_eic_type type;

	chained_irq_enter(ic, desc);

	/*
	 * Since the digital-chip EIC 4 sub-modules (debounce, latch, async
	 * and sync) share one same interrupt line, we should iterate each
	 * EIC module to check if there are EIC interrupts were triggered.
	 */
	for (type = SPRD_EIC_DEBOUNCE; type < SPRD_EIC_MAX; type++) {
		chip = gpiochip_find(&type, sprd_eic_match_chip_by_type);
		if (!chip)
			continue;

		sprd_eic_handle_one_type(chip);
	}

	chained_irq_exit(ic, desc);
}

static int sprd_eic_probe(struct platform_device *pdev)
{
	const struct sprd_eic_variant_data *pdata;
	struct gpio_irq_chip *irq;
	struct sprd_eic *sprd_eic;
	struct resource *res;
	int ret, i;

	pdata = of_device_get_match_data(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "No matching driver data found.\n");
		return -EINVAL;
	}

	sprd_eic = devm_kzalloc(&pdev->dev, sizeof(*sprd_eic), GFP_KERNEL);
	if (!sprd_eic)
		return -ENOMEM;

	spin_lock_init(&sprd_eic->lock);
	sprd_eic->type = pdata->type;

	sprd_eic->irq = platform_get_irq(pdev, 0);
	if (sprd_eic->irq < 0) {
		dev_err(&pdev->dev, "Failed to get EIC interrupt.\n");
		return sprd_eic->irq;
	}

	for (i = 0; i < SPRD_EIC_MAX_BANK; i++) {
		/*
		 * We can have maximum 3 banks EICs, and each EIC has
		 * its own base address. But some platform maybe only
		 * have one bank EIC, thus base[1] and base[2] can be
		 * optional.
		 */
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			continue;

		sprd_eic->base[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(sprd_eic->base[i]))
			return PTR_ERR(sprd_eic->base[i]);
	}

	sprd_eic->chip.label = sprd_eic_label_name[sprd_eic->type];
	sprd_eic->chip.ngpio = pdata->num_eics;
	sprd_eic->chip.base = -1;
	sprd_eic->chip.parent = &pdev->dev;
	sprd_eic->chip.of_node = pdev->dev.of_node;
	sprd_eic->chip.direction_input = sprd_eic_direction_input;
	switch (sprd_eic->type) {
	case SPRD_EIC_DEBOUNCE:
		sprd_eic->chip.request = sprd_eic_request;
		sprd_eic->chip.free = sprd_eic_free;
		sprd_eic->chip.set_config = sprd_eic_set_config;
		sprd_eic->chip.set = sprd_eic_set;
		/* fall-through */
	case SPRD_EIC_ASYNC:
		/* fall-through */
	case SPRD_EIC_SYNC:
		sprd_eic->chip.get = sprd_eic_get;
		break;
	case SPRD_EIC_LATCH:
		/* fall-through */
	default:
		break;
	}

	sprd_eic->intc.name = dev_name(&pdev->dev);
	sprd_eic->intc.irq_ack = sprd_eic_irq_ack;
	sprd_eic->intc.irq_mask = sprd_eic_irq_mask;
	sprd_eic->intc.irq_unmask = sprd_eic_irq_unmask;
	sprd_eic->intc.irq_set_type = sprd_eic_irq_set_type;
	sprd_eic->intc.flags = IRQCHIP_SKIP_SET_WAKE;

	irq = &sprd_eic->chip.irq;
	irq->chip = &sprd_eic->intc;
	irq->handler = handle_bad_irq;
	irq->default_type = IRQ_TYPE_NONE;
	irq->parent_handler = sprd_eic_irq_handler;
	irq->parent_handler_data = sprd_eic;
	irq->num_parents = 1;
	irq->parents = &sprd_eic->irq;

	ret = devm_gpiochip_add_data(&pdev->dev, &sprd_eic->chip, sprd_eic);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip %d.\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, sprd_eic);
	return 0;
}

static const struct of_device_id sprd_eic_of_match[] = {
	{
		.compatible = "sprd,sc9860-eic-debounce",
		.data = &sc9860_eic_dbnc_data,
	},
	{
		.compatible = "sprd,sc9860-eic-latch",
		.data = &sc9860_eic_latch_data,
	},
	{
		.compatible = "sprd,sc9860-eic-async",
		.data = &sc9860_eic_async_data,
	},
	{
		.compatible = "sprd,sc9860-eic-sync",
		.data = &sc9860_eic_sync_data,
	},
	{
		/* end of list */
	}
};
MODULE_DEVICE_TABLE(of, sprd_eic_of_match);

static struct platform_driver sprd_eic_driver = {
	.probe = sprd_eic_probe,
	.driver = {
		.name = "sprd-eic",
		.of_match_table	= sprd_eic_of_match,
	},
};

module_platform_driver(sprd_eic_driver);

MODULE_DESCRIPTION("Spreadtrum EIC driver");
MODULE_LICENSE("GPL v2");
