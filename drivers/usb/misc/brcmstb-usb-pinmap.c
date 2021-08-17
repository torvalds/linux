// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Broadcom */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/gpio/consumer.h>

struct out_pin {
	u32 enable_mask;
	u32 value_mask;
	u32 changed_mask;
	u32 clr_changed_mask;
	struct gpio_desc *gpiod;
	const char *name;
};

struct in_pin {
	u32 enable_mask;
	u32 value_mask;
	struct gpio_desc *gpiod;
	const char *name;
	struct brcmstb_usb_pinmap_data *pdata;
};

struct brcmstb_usb_pinmap_data {
	void __iomem *regs;
	int in_count;
	struct in_pin *in_pins;
	int out_count;
	struct out_pin *out_pins;
};


static void pinmap_set(void __iomem *reg, u32 mask)
{
	u32 val;

	val = readl(reg);
	val |= mask;
	writel(val, reg);
}

static void pinmap_unset(void __iomem *reg, u32 mask)
{
	u32 val;

	val = readl(reg);
	val &= ~mask;
	writel(val, reg);
}

static void sync_in_pin(struct in_pin *pin)
{
	u32 val;

	val = gpiod_get_value(pin->gpiod);
	if (val)
		pinmap_set(pin->pdata->regs, pin->value_mask);
	else
		pinmap_unset(pin->pdata->regs, pin->value_mask);
}

/*
 * Interrupt from override register, propagate from override bit
 * to GPIO.
 */
static irqreturn_t brcmstb_usb_pinmap_ovr_isr(int irq, void *dev_id)
{
	struct brcmstb_usb_pinmap_data *pdata = dev_id;
	struct out_pin *pout;
	u32 val;
	u32 bit;
	int x;

	pr_debug("%s: reg: 0x%x\n", __func__, readl(pdata->regs));
	pout = pdata->out_pins;
	for (x = 0; x < pdata->out_count; x++) {
		val = readl(pdata->regs);
		if (val & pout->changed_mask) {
			pinmap_set(pdata->regs, pout->clr_changed_mask);
			pinmap_unset(pdata->regs, pout->clr_changed_mask);
			bit = val & pout->value_mask;
			gpiod_set_value(pout->gpiod, bit ? 1 : 0);
			pr_debug("%s: %s bit changed state to %d\n",
				 __func__, pout->name, bit ? 1 : 0);
		}
	}
	return IRQ_HANDLED;
}

/*
 * Interrupt from GPIO, propagate from GPIO to override bit.
 */
static irqreturn_t brcmstb_usb_pinmap_gpio_isr(int irq, void *dev_id)
{
	struct in_pin *pin = dev_id;

	pr_debug("%s: %s pin changed state\n", __func__, pin->name);
	sync_in_pin(pin);
	return IRQ_HANDLED;
}


static void get_pin_counts(struct device_node *dn, int *in_count,
			   int *out_count)
{
	int in;
	int out;

	*in_count = 0;
	*out_count = 0;
	in = of_property_count_strings(dn, "brcm,in-functions");
	if (in < 0)
		return;
	out = of_property_count_strings(dn, "brcm,out-functions");
	if (out < 0)
		return;
	*in_count = in;
	*out_count = out;
}

static int parse_pins(struct device *dev, struct device_node *dn,
		      struct brcmstb_usb_pinmap_data *pdata)
{
	struct out_pin *pout;
	struct in_pin *pin;
	int index;
	int res;
	int x;

	pin = pdata->in_pins;
	for (x = 0, index = 0; x < pdata->in_count; x++) {
		pin->gpiod = devm_gpiod_get_index(dev, "in", x, GPIOD_IN);
		if (IS_ERR(pin->gpiod)) {
			dev_err(dev, "Error getting gpio %s\n", pin->name);
			return PTR_ERR(pin->gpiod);

		}
		res = of_property_read_string_index(dn, "brcm,in-functions", x,
						    &pin->name);
		if (res < 0) {
			dev_err(dev, "Error getting brcm,in-functions for %s\n",
				pin->name);
			return res;
		}
		res = of_property_read_u32_index(dn, "brcm,in-masks", index++,
						 &pin->enable_mask);
		if (res < 0) {
			dev_err(dev, "Error getting 1st brcm,in-masks for %s\n",
				pin->name);
			return res;
		}
		res = of_property_read_u32_index(dn, "brcm,in-masks", index++,
						 &pin->value_mask);
		if (res < 0) {
			dev_err(dev, "Error getting 2nd brcm,in-masks for %s\n",
				pin->name);
			return res;
		}
		pin->pdata = pdata;
		pin++;
	}
	pout = pdata->out_pins;
	for (x = 0, index = 0; x < pdata->out_count; x++) {
		pout->gpiod = devm_gpiod_get_index(dev, "out", x,
						   GPIOD_OUT_HIGH);
		if (IS_ERR(pout->gpiod)) {
			dev_err(dev, "Error getting gpio %s\n", pin->name);
			return PTR_ERR(pout->gpiod);
		}
		res = of_property_read_string_index(dn, "brcm,out-functions", x,
						    &pout->name);
		if (res < 0) {
			dev_err(dev, "Error getting brcm,out-functions for %s\n",
				pout->name);
			return res;
		}
		res = of_property_read_u32_index(dn, "brcm,out-masks", index++,
						 &pout->enable_mask);
		if (res < 0) {
			dev_err(dev, "Error getting 1st brcm,out-masks for %s\n",
				pout->name);
			return res;
		}
		res = of_property_read_u32_index(dn, "brcm,out-masks", index++,
						 &pout->value_mask);
		if (res < 0) {
			dev_err(dev, "Error getting 2nd brcm,out-masks for %s\n",
				pout->name);
			return res;
		}
		res = of_property_read_u32_index(dn, "brcm,out-masks", index++,
						 &pout->changed_mask);
		if (res < 0) {
			dev_err(dev, "Error getting 3rd brcm,out-masks for %s\n",
				pout->name);
			return res;
		}
		res = of_property_read_u32_index(dn, "brcm,out-masks", index++,
						 &pout->clr_changed_mask);
		if (res < 0) {
			dev_err(dev, "Error getting 4th out-masks for %s\n",
				pout->name);
			return res;
		}
		pout++;
	}
	return 0;
}

static void sync_all_pins(struct brcmstb_usb_pinmap_data *pdata)
{
	struct out_pin *pout;
	struct in_pin *pin;
	int val;
	int x;

	/*
	 * Enable the override, clear any changed condition and
	 * propagate the state to the GPIO for all out pins.
	 */
	pout = pdata->out_pins;
	for (x = 0; x < pdata->out_count; x++) {
		pinmap_set(pdata->regs, pout->enable_mask);
		pinmap_set(pdata->regs, pout->clr_changed_mask);
		pinmap_unset(pdata->regs, pout->clr_changed_mask);
		val = readl(pdata->regs) & pout->value_mask;
		gpiod_set_value(pout->gpiod, val ? 1 : 0);
		pout++;
	}

	/* sync and enable all in pins. */
	pin = pdata->in_pins;
	for (x = 0; x < pdata->in_count; x++) {
		sync_in_pin(pin);
		pinmap_set(pdata->regs, pin->enable_mask);
		pin++;
	}
}

static int __init brcmstb_usb_pinmap_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct brcmstb_usb_pinmap_data *pdata;
	struct in_pin *pin;
	struct resource *r;
	int out_count;
	int in_count;
	int err;
	int irq;
	int x;

	get_pin_counts(dn, &in_count, &out_count);
	if ((in_count + out_count) == 0)
		return -EINVAL;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -EINVAL;

	pdata = devm_kzalloc(&pdev->dev,
			     sizeof(*pdata) +
			     (sizeof(struct in_pin) * in_count) +
			     (sizeof(struct out_pin) * out_count), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->in_count = in_count;
	pdata->out_count = out_count;
	pdata->in_pins = (struct in_pin *)(pdata + 1);
	pdata->out_pins = (struct out_pin *)(pdata->in_pins + in_count);

	pdata->regs = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!pdata->regs)
		return -ENOMEM;
	platform_set_drvdata(pdev, pdata);

	err = parse_pins(&pdev->dev, dn, pdata);
	if (err)
		return err;

	sync_all_pins(pdata);

	if (out_count) {

		/* Enable interrupt for out pins */
		irq = platform_get_irq(pdev, 0);
		err = devm_request_irq(&pdev->dev, irq,
				       brcmstb_usb_pinmap_ovr_isr,
				       IRQF_TRIGGER_RISING,
				       pdev->name, pdata);
		if (err < 0) {
			dev_err(&pdev->dev, "Error requesting IRQ\n");
			return err;
		}
	}

	for (x = 0, pin = pdata->in_pins; x < pdata->in_count; x++, pin++) {
		irq = gpiod_to_irq(pin->gpiod);
		if (irq < 0) {
			dev_err(&pdev->dev, "Error getting IRQ for %s pin\n",
				pin->name);
			return irq;
		}
		err = devm_request_irq(&pdev->dev, irq,
				       brcmstb_usb_pinmap_gpio_isr,
				       IRQF_SHARED | IRQF_TRIGGER_RISING |
				       IRQF_TRIGGER_FALLING,
				       pdev->name, pin);
		if (err < 0) {
			dev_err(&pdev->dev, "Error requesting IRQ for %s pin\n",
				pin->name);
			return err;
		}
	}

	dev_dbg(&pdev->dev, "Driver probe succeeded\n");
	dev_dbg(&pdev->dev, "In pin count: %d, out pin count: %d\n",
		pdata->in_count, pdata->out_count);
	return 0;
}


static const struct of_device_id brcmstb_usb_pinmap_of_match[] = {
	{ .compatible = "brcm,usb-pinmap" },
	{ },
};

static struct platform_driver brcmstb_usb_pinmap_driver = {
	.driver = {
		.name	= "brcm-usb-pinmap",
		.of_match_table = brcmstb_usb_pinmap_of_match,
	},
};

static int __init brcmstb_usb_pinmap_init(void)
{
	return platform_driver_probe(&brcmstb_usb_pinmap_driver,
				     brcmstb_usb_pinmap_probe);
}

module_init(brcmstb_usb_pinmap_init);
MODULE_AUTHOR("Al Cooper <alcooperx@gmail.com>");
MODULE_DESCRIPTION("Broadcom USB Pinmap Driver");
MODULE_LICENSE("GPL");
