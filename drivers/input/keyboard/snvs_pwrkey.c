// SPDX-License-Identifier: GPL-2.0+
//
// Driver for the IMX SNVS ON/OFF Power Key
// Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define SNVS_HPVIDR1_REG	0xBF8
#define SNVS_LPSR_REG		0x4C	/* LP Status Register */
#define SNVS_LPCR_REG		0x38	/* LP Control Register */
#define SNVS_HPSR_REG		0x14
#define SNVS_HPSR_BTN		BIT(6)
#define SNVS_LPSR_SPO		BIT(18)
#define SNVS_LPCR_DEP_EN	BIT(5)
#define SNVS_LPCR_BPT_SHIFT	16
#define SNVS_LPCR_BPT_MASK	(3 << SNVS_LPCR_BPT_SHIFT)

#define DEBOUNCE_TIME		30
#define REPEAT_INTERVAL		60

struct pwrkey_drv_data {
	struct regmap *snvs;
	int irq;
	int keycode;
	int keystate;  /* 1:pressed */
	int wakeup;
	bool suspended;     /* Track suspend state */
	bool pending_press; /* Key pressed during suspend, report from timer callback */
	spinlock_t lock;    /* Protects keystate, suspended and pending_press */
	struct timer_list check_timer;
	struct input_dev *input;
	u8 minor_rev;
};

static void imx_imx_snvs_check_for_events(struct timer_list *t)
{
	struct pwrkey_drv_data *pdata = timer_container_of(pdata, t,
							   check_timer);
	struct input_dev *input = pdata->input;
	bool state_changed = false;
	bool pending_press;
	u32 state;

	regmap_read(pdata->snvs, SNVS_HPSR_REG, &state);
	state = state & SNVS_HPSR_BTN ? 1 : 0;

	scoped_guard(spinlock_irqsave, &pdata->lock) {
		pending_press = pdata->pending_press;
		if (pending_press) {
			pdata->pending_press = false;
			pdata->keystate = 1;
		}
		/* only report new event if status changed */
		if (state ^ pdata->keystate) {
			pdata->keystate = state;
			state_changed = true;
		}
	}

	/*
	 * Report a press event latched during suspend. If the key is still
	 * held, state_changed will be 0 (keystate already set to 1 above),
	 * so no duplicate press is reported. If already released,
	 * state_changed will fire next to report the release.
	 */
	if (pending_press) {
		input_report_key(input, pdata->keycode, 1);
		input_sync(input);
	}

	if (state_changed) {
		input_event(input, EV_KEY, pdata->keycode, state);
		input_sync(input);
		pm_relax(pdata->input->dev.parent);
	}

	/* repeat check if pressed long */
	if (state) {
		mod_timer(&pdata->check_timer,
			  jiffies + msecs_to_jiffies(REPEAT_INTERVAL));
	}
}

static irqreturn_t imx_snvs_pwrkey_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct pwrkey_drv_data *pdata = platform_get_drvdata(pdev);
	struct input_dev *input = pdata->input;
	u32 lp_status;

	pm_wakeup_event(input->dev.parent, 0);

	regmap_read(pdata->snvs, SNVS_LPSR_REG, &lp_status);
	if (lp_status & SNVS_LPSR_SPO) {
		if (pdata->minor_rev == 0) {
			/*
			 * The first generation i.MX6 SoCs only sends an
			 * interrupt on button release. To mimic power-key
			 * usage, we'll prepend a press event.
			 */
			input_report_key(input, pdata->keycode, 1);
			input_sync(input);
			input_report_key(input, pdata->keycode, 0);
			input_sync(input);
			pm_relax(input->dev.parent);
		} else {
			/*
			 * If the key is pressed during suspend, latch it so
			 * the timer callback can report the press event in
			 * softirq context, avoiding out-of-order events.
			 */
			scoped_guard(spinlock_irqsave, &pdata->lock) {
				if (pdata->suspended)
					pdata->pending_press = true;
			}
			mod_timer(&pdata->check_timer,
				  jiffies + msecs_to_jiffies(DEBOUNCE_TIME));
		}
	}

	/* clear SPO status */
	regmap_write(pdata->snvs, SNVS_LPSR_REG, SNVS_LPSR_SPO);

	return IRQ_HANDLED;
}

static void imx_snvs_pwrkey_act(void *pdata)
{
	struct pwrkey_drv_data *pd = pdata;

	timer_delete_sync(&pd->check_timer);
}

static int imx_snvs_pwrkey_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwrkey_drv_data *pdata;
	struct input_dev *input;
	struct device_node *np;
	struct clk *clk;
	int error;
	unsigned int val;
	unsigned int bpt;
	u32 vid;

	/* Get SNVS register Page */
	np = dev->of_node;
	if (!np)
		return dev_err_probe(dev, -ENODEV, "Device tree node not found\n");

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->snvs = syscon_regmap_lookup_by_phandle(np, "regmap");
	if (IS_ERR(pdata->snvs))
		return dev_err_probe(dev, PTR_ERR(pdata->snvs), "Can't get snvs syscon\n");

	if (of_property_read_u32(np, "linux,keycode", &pdata->keycode)) {
		pdata->keycode = KEY_POWER;
		dev_warn(dev, "KEY_POWER without setting in dts\n");
	}

	clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk),
				     "Failed to get snvs clock (%pe)\n", clk);

	pdata->wakeup = of_property_read_bool(np, "wakeup-source");

	pdata->irq = platform_get_irq(pdev, 0);
	if (pdata->irq < 0)
		return pdata->irq;

	spin_lock_init(&pdata->lock);
	error = of_property_read_u32(np, "power-off-time-sec", &val);
	if (!error) {
		switch (val) {
		case 0:
			bpt = 0x3;
			break;
		case 5:
		case 10:
		case 15:
			bpt = (val / 5) - 1;
			break;
		default:
			return dev_err_probe(dev, -EINVAL,
					     "power-off-time-sec %d out of range\n", val);
		}

		regmap_update_bits(pdata->snvs, SNVS_LPCR_REG, SNVS_LPCR_BPT_MASK,
				   bpt << SNVS_LPCR_BPT_SHIFT);
	}

	regmap_read(pdata->snvs, SNVS_HPVIDR1_REG, &vid);
	pdata->minor_rev = vid & 0xff;

	regmap_update_bits(pdata->snvs, SNVS_LPCR_REG, SNVS_LPCR_DEP_EN, SNVS_LPCR_DEP_EN);

	/* clear the unexpected interrupt before driver ready */
	regmap_write(pdata->snvs, SNVS_LPSR_REG, SNVS_LPSR_SPO);

	timer_setup(&pdata->check_timer, imx_imx_snvs_check_for_events, 0);

	input = devm_input_allocate_device(dev);
	if (!input)
		return dev_err_probe(dev, -ENOMEM, "failed to allocate the input device\n");

	input->name = pdev->name;
	input->phys = "snvs-pwrkey/input0";
	input->id.bustype = BUS_HOST;

	input_set_capability(input, EV_KEY, pdata->keycode);

	/* input customer action to cancel release timer */
	error = devm_add_action(dev, imx_snvs_pwrkey_act, pdata);
	if (error)
		return dev_err_probe(dev, error, "failed to register remove action\n");

	pdata->input = input;
	platform_set_drvdata(pdev, pdata);

	error = devm_request_irq(dev, pdata->irq,
				 imx_snvs_pwrkey_interrupt,
				 0, pdev->name, pdev);
	if (error)
		return dev_err_probe(dev, error, "interrupt not available.\n");

	error = input_register_device(input);
	if (error < 0)
		return dev_err_probe(dev, error, "failed to register input device\n");

	device_init_wakeup(dev, pdata->wakeup);
	error = dev_pm_set_wake_irq(dev, pdata->irq);
	if (error)
		dev_err(dev, "irq wake enable failed.\n");

	return 0;
}

static int imx_snvs_pwrkey_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pwrkey_drv_data *pdata = platform_get_drvdata(pdev);

	guard(spinlock_irq)(&pdata->lock);
	pdata->suspended = true;

	return 0;
}

static int imx_snvs_pwrkey_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pwrkey_drv_data *pdata = platform_get_drvdata(pdev);

	guard(spinlock_irq)(&pdata->lock);
	pdata->suspended = false;

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(imx_snvs_pwrkey_pm_ops,
				imx_snvs_pwrkey_suspend,
				imx_snvs_pwrkey_resume);

static const struct of_device_id imx_snvs_pwrkey_ids[] = {
	{ .compatible = "fsl,sec-v4.0-pwrkey" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_snvs_pwrkey_ids);

static struct platform_driver imx_snvs_pwrkey_driver = {
	.driver = {
		.name = "snvs_pwrkey",
		.of_match_table = imx_snvs_pwrkey_ids,
		.pm = pm_ptr(&imx_snvs_pwrkey_pm_ops),
	},
	.probe = imx_snvs_pwrkey_probe,
};
module_platform_driver(imx_snvs_pwrkey_driver);

MODULE_AUTHOR("Freescale Semiconductor");
MODULE_DESCRIPTION("i.MX snvs power key Driver");
MODULE_LICENSE("GPL");
