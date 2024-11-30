// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Allwinner sun4i low res adc attached tablet keys driver
 *
 * Copyright (C) 2014 Hans de Goede <hdegoede@redhat.com>
 */

/*
 * Allwinnner sunxi SoCs have a lradc which is specifically designed to have
 * various (tablet) keys (ie home, back, search, etc). attached to it using
 * a resistor network. This driver is for the keys on such boards.
 *
 * There are 2 channels, currently this driver only supports channel 0 since
 * there are no boards known to use channel 1.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define LRADC_CTRL		0x00
#define LRADC_INTC		0x04
#define LRADC_INTS		0x08
#define LRADC_DATA0		0x0c
#define LRADC_DATA1		0x10

/* LRADC_CTRL bits */
#define FIRST_CONVERT_DLY(x)	((x) << 24) /* 8 bits */
#define CHAN_SELECT(x)		((x) << 22) /* 2 bits */
#define CONTINUE_TIME_SEL(x)	((x) << 16) /* 4 bits */
#define KEY_MODE_SEL(x)		((x) << 12) /* 2 bits */
#define LEVELA_B_CNT(x)		((x) << 8)  /* 4 bits */
#define HOLD_KEY_EN(x)		((x) << 7)
#define HOLD_EN(x)		((x) << 6)
#define LEVELB_VOL(x)		((x) << 4)  /* 2 bits */
#define SAMPLE_RATE(x)		((x) << 2)  /* 2 bits */
#define ENABLE(x)		((x) << 0)

/* LRADC_INTC and LRADC_INTS bits */
#define CHAN1_KEYUP_IRQ		BIT(12)
#define CHAN1_ALRDY_HOLD_IRQ	BIT(11)
#define CHAN1_HOLD_IRQ		BIT(10)
#define	CHAN1_KEYDOWN_IRQ	BIT(9)
#define CHAN1_DATA_IRQ		BIT(8)
#define CHAN0_KEYUP_IRQ		BIT(4)
#define CHAN0_ALRDY_HOLD_IRQ	BIT(3)
#define CHAN0_HOLD_IRQ		BIT(2)
#define	CHAN0_KEYDOWN_IRQ	BIT(1)
#define CHAN0_DATA_IRQ		BIT(0)

/* struct lradc_variant - Describe sun4i-a10-lradc-keys hardware variant
 * @divisor_numerator:		The numerator of lradc Vref internally divisor
 * @divisor_denominator:	The denominator of lradc Vref internally divisor
 * @has_clock_reset:		If the binding requires a clock and reset
 */
struct lradc_variant {
	u8 divisor_numerator;
	u8 divisor_denominator;
	bool has_clock_reset;
};

static const struct lradc_variant lradc_variant_a10 = {
	.divisor_numerator = 2,
	.divisor_denominator = 3
};

static const struct lradc_variant r_lradc_variant_a83t = {
	.divisor_numerator = 3,
	.divisor_denominator = 4
};

static const struct lradc_variant lradc_variant_r329 = {
	.divisor_numerator = 3,
	.divisor_denominator = 4,
	.has_clock_reset = true,
};

struct sun4i_lradc_keymap {
	u32 voltage;
	u32 keycode;
};

struct sun4i_lradc_data {
	struct device *dev;
	struct input_dev *input;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *reset;
	struct regulator *vref_supply;
	struct sun4i_lradc_keymap *chan0_map;
	const struct lradc_variant *variant;
	u32 chan0_map_count;
	u32 chan0_keycode;
	u32 vref;
};

static irqreturn_t sun4i_lradc_irq(int irq, void *dev_id)
{
	struct sun4i_lradc_data *lradc = dev_id;
	u32 i, ints, val, voltage, diff, keycode = 0, closest = 0xffffffff;

	ints  = readl(lradc->base + LRADC_INTS);

	/*
	 * lradc supports only one keypress at a time, release does not give
	 * any info as to which key was released, so we cache the keycode.
	 */

	if (ints & CHAN0_KEYUP_IRQ) {
		input_report_key(lradc->input, lradc->chan0_keycode, 0);
		lradc->chan0_keycode = 0;
	}

	if ((ints & CHAN0_KEYDOWN_IRQ) && lradc->chan0_keycode == 0) {
		val = readl(lradc->base + LRADC_DATA0) & 0x3f;
		voltage = val * lradc->vref / 63;

		for (i = 0; i < lradc->chan0_map_count; i++) {
			diff = abs(lradc->chan0_map[i].voltage - voltage);
			if (diff < closest) {
				closest = diff;
				keycode = lradc->chan0_map[i].keycode;
			}
		}

		lradc->chan0_keycode = keycode;
		input_report_key(lradc->input, lradc->chan0_keycode, 1);
	}

	input_sync(lradc->input);

	writel(ints, lradc->base + LRADC_INTS);

	return IRQ_HANDLED;
}

static int sun4i_lradc_open(struct input_dev *dev)
{
	struct sun4i_lradc_data *lradc = input_get_drvdata(dev);
	int error;

	error = regulator_enable(lradc->vref_supply);
	if (error)
		return error;

	error = reset_control_deassert(lradc->reset);
	if (error)
		goto err_disable_reg;

	error = clk_prepare_enable(lradc->clk);
	if (error)
		goto err_assert_reset;

	lradc->vref = regulator_get_voltage(lradc->vref_supply) *
		      lradc->variant->divisor_numerator /
		      lradc->variant->divisor_denominator;
	/*
	 * Set sample time to 4 ms / 250 Hz. Wait 2 * 4 ms for key to
	 * stabilize on press, wait (1 + 1) * 4 ms for key release
	 */
	writel(FIRST_CONVERT_DLY(2) | LEVELA_B_CNT(1) | HOLD_EN(1) |
		SAMPLE_RATE(0) | ENABLE(1), lradc->base + LRADC_CTRL);

	writel(CHAN0_KEYUP_IRQ | CHAN0_KEYDOWN_IRQ, lradc->base + LRADC_INTC);

	return 0;

err_assert_reset:
	reset_control_assert(lradc->reset);
err_disable_reg:
	regulator_disable(lradc->vref_supply);

	return error;
}

static void sun4i_lradc_close(struct input_dev *dev)
{
	struct sun4i_lradc_data *lradc = input_get_drvdata(dev);

	/* Disable lradc, leave other settings unchanged */
	writel(FIRST_CONVERT_DLY(2) | LEVELA_B_CNT(1) | HOLD_EN(1) |
		SAMPLE_RATE(2), lradc->base + LRADC_CTRL);
	writel(0, lradc->base + LRADC_INTC);

	clk_disable_unprepare(lradc->clk);
	reset_control_assert(lradc->reset);
	regulator_disable(lradc->vref_supply);
}

static int sun4i_lradc_load_dt_keymap(struct device *dev,
				      struct sun4i_lradc_data *lradc)
{
	struct device_node *np;
	int i;
	int error;

	np = dev->of_node;
	if (!np)
		return -EINVAL;

	lradc->chan0_map_count = of_get_child_count(np);
	if (lradc->chan0_map_count == 0) {
		dev_err(dev, "keymap is missing in device tree\n");
		return -EINVAL;
	}

	lradc->chan0_map = devm_kmalloc_array(dev, lradc->chan0_map_count,
					      sizeof(struct sun4i_lradc_keymap),
					      GFP_KERNEL);
	if (!lradc->chan0_map)
		return -ENOMEM;

	i = 0;
	for_each_child_of_node_scoped(np, pp) {
		struct sun4i_lradc_keymap *map = &lradc->chan0_map[i];
		u32 channel;

		error = of_property_read_u32(pp, "channel", &channel);
		if (error || channel != 0) {
			dev_err(dev, "%pOFn: Inval channel prop\n", pp);
			return -EINVAL;
		}

		error = of_property_read_u32(pp, "voltage", &map->voltage);
		if (error) {
			dev_err(dev, "%pOFn: Inval voltage prop\n", pp);
			return -EINVAL;
		}

		error = of_property_read_u32(pp, "linux,code", &map->keycode);
		if (error) {
			dev_err(dev, "%pOFn: Inval linux,code prop\n", pp);
			return -EINVAL;
		}

		i++;
	}

	return 0;
}

static int sun4i_lradc_probe(struct platform_device *pdev)
{
	struct sun4i_lradc_data *lradc;
	struct device *dev = &pdev->dev;
	int error, i, irq;

	lradc = devm_kzalloc(dev, sizeof(struct sun4i_lradc_data), GFP_KERNEL);
	if (!lradc)
		return -ENOMEM;

	error = sun4i_lradc_load_dt_keymap(dev, lradc);
	if (error)
		return error;

	lradc->variant = of_device_get_match_data(&pdev->dev);
	if (!lradc->variant) {
		dev_err(&pdev->dev, "Missing sun4i-a10-lradc-keys variant\n");
		return -EINVAL;
	}

	if (lradc->variant->has_clock_reset) {
		lradc->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(lradc->clk))
			return PTR_ERR(lradc->clk);

		lradc->reset = devm_reset_control_get_exclusive(dev, NULL);
		if (IS_ERR(lradc->reset))
			return PTR_ERR(lradc->reset);
	}

	lradc->vref_supply = devm_regulator_get(dev, "vref");
	if (IS_ERR(lradc->vref_supply))
		return PTR_ERR(lradc->vref_supply);

	lradc->dev = dev;
	lradc->input = devm_input_allocate_device(dev);
	if (!lradc->input)
		return -ENOMEM;

	lradc->input->name = pdev->name;
	lradc->input->phys = "sun4i_lradc/input0";
	lradc->input->open = sun4i_lradc_open;
	lradc->input->close = sun4i_lradc_close;
	lradc->input->id.bustype = BUS_HOST;
	lradc->input->id.vendor = 0x0001;
	lradc->input->id.product = 0x0001;
	lradc->input->id.version = 0x0100;

	__set_bit(EV_KEY, lradc->input->evbit);
	for (i = 0; i < lradc->chan0_map_count; i++)
		__set_bit(lradc->chan0_map[i].keycode, lradc->input->keybit);

	input_set_drvdata(lradc->input, lradc);

	lradc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(lradc->base))
		return PTR_ERR(lradc->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	error = devm_request_irq(dev, irq, sun4i_lradc_irq, 0,
				 "sun4i-a10-lradc-keys", lradc);
	if (error)
		return error;

	error = input_register_device(lradc->input);
	if (error)
		return error;

	if (device_property_read_bool(dev, "wakeup-source")) {
		error = dev_pm_set_wake_irq(dev, irq);
		if (error)
			dev_warn(dev,
				 "Failed to set IRQ %d as a wake IRQ: %d\n",
				 irq, error);
		else
			device_set_wakeup_capable(dev, true);
	}

	return 0;
}

static const struct of_device_id sun4i_lradc_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-lradc-keys",
		.data = &lradc_variant_a10 },
	{ .compatible = "allwinner,sun8i-a83t-r-lradc",
		.data = &r_lradc_variant_a83t },
	{ .compatible = "allwinner,sun50i-r329-lradc",
		.data = &lradc_variant_r329 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun4i_lradc_of_match);

static struct platform_driver sun4i_lradc_driver = {
	.driver = {
		.name	= "sun4i-a10-lradc-keys",
		.of_match_table = of_match_ptr(sun4i_lradc_of_match),
	},
	.probe	= sun4i_lradc_probe,
};

module_platform_driver(sun4i_lradc_driver);

MODULE_DESCRIPTION("Allwinner sun4i low res adc attached tablet keys driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
