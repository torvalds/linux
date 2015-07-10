/*
 * Copyright 2012-2016 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Marek Vasut <marex@denx.de>
 * on behalf of DENX Software Engineering GmbH
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/usb/chipidea.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>
#include <linux/busfreq-imx.h>

#include "ci.h"
#include "ci_hdrc_imx.h"

struct ci_hdrc_imx_platform_flag {
	unsigned int flags;
	bool runtime_pm;
};

static const struct ci_hdrc_imx_platform_flag imx27_usb_data = {
		CI_HDRC_DISABLE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx28_usb_data = {
	.flags = CI_HDRC_IMX28_WRITE_FIX |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6q_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6sl_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_HOST_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6sx_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_HOST_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6ul_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx7d_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM,
};

static const struct of_device_id ci_hdrc_imx_dt_ids[] = {
	{ .compatible = "fsl,imx28-usb", .data = &imx28_usb_data},
	{ .compatible = "fsl,imx27-usb", .data = &imx27_usb_data},
	{ .compatible = "fsl,imx6q-usb", .data = &imx6q_usb_data},
	{ .compatible = "fsl,imx6sl-usb", .data = &imx6sl_usb_data},
	{ .compatible = "fsl,imx6sx-usb", .data = &imx6sx_usb_data},
	{ .compatible = "fsl,imx6ul-usb", .data = &imx6ul_usb_data},
	{ .compatible = "fsl,imx7d-usb", .data = &imx7d_usb_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ci_hdrc_imx_dt_ids);

struct ci_hdrc_imx_data {
	struct usb_phy *phy;
	struct platform_device *ci_pdev;
	struct clk *clk;
	struct imx_usbmisc_data *usbmisc_data;
	bool supports_runtime_pm;
	bool in_lpm;
	bool imx_usb_charger_detection;
	struct usb_charger charger;
	struct regmap *anatop;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_hsic_active;
	struct regulator *hsic_pad_regulator;
	const struct ci_hdrc_imx_platform_flag *data;
	/* SoC before i.mx6 (except imx23/imx28) needs three clks */
	bool need_three_clks;
	struct clk *clk_ipg;
	struct clk *clk_ahb;
	struct clk *clk_per;
	/* --------------------------------- */
};

static char *imx_usb_charger_supplied_to[] = {
	"imx_usb_charger",
};

static enum power_supply_property imx_usb_charger_power_props[] = {
	POWER_SUPPLY_PROP_PRESENT,	/* Charger detected */
	POWER_SUPPLY_PROP_ONLINE,	/* VBUS online */
	POWER_SUPPLY_PROP_CURRENT_MAX,	/* Maximum current in mA */
};

static inline bool is_imx6q_con(struct ci_hdrc_imx_data *imx_data)
{
	return imx_data->data == &imx6q_usb_data;
}

static inline bool is_imx6sl_con(struct ci_hdrc_imx_data *imx_data)
{
	return imx_data->data == &imx6sl_usb_data;
}

static inline bool is_imx6sx_con(struct ci_hdrc_imx_data *imx_data)
{
	return imx_data->data == &imx6sx_usb_data;
}

static inline bool is_imx7d_con(struct ci_hdrc_imx_data *imx_data)
{
	return imx_data->data == &imx7d_usb_data;
}

static inline bool imx_has_hsic_con(struct ci_hdrc_imx_data *imx_data)
{
	return is_imx6q_con(imx_data) ||  is_imx6sl_con(imx_data)
		|| is_imx6sx_con(imx_data) || is_imx7d_con(imx_data);
}

/* Common functions shared by usbmisc drivers */

static struct imx_usbmisc_data *usbmisc_get_init_data(struct device *dev)
{
	struct platform_device *misc_pdev;
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	struct imx_usbmisc_data *data;
	int ret;

	/*
	 * In case the fsl,usbmisc property is not present this device doesn't
	 * need usbmisc. Return NULL (which is no error here)
	 */
	if (!of_get_property(np, "fsl,usbmisc", NULL))
		return NULL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	ret = of_parse_phandle_with_args(np, "fsl,usbmisc", "#index-cells",
					0, &args);
	if (ret) {
		dev_err(dev, "Failed to parse property fsl,usbmisc, errno %d\n",
			ret);
		return ERR_PTR(ret);
	}

	data->index = args.args[0];

	misc_pdev = of_find_device_by_node(args.np);
	of_node_put(args.np);

	if (!misc_pdev || !platform_get_drvdata(misc_pdev))
		return ERR_PTR(-EPROBE_DEFER);

	data->dev = &misc_pdev->dev;

	if (of_find_property(np, "disable-over-current", NULL))
		data->disable_oc = 1;

	if (of_find_property(np, "external-vbus-divider", NULL))
		data->evdo = 1;

	if (of_find_property(np, "osc-clkgate-delay", NULL)) {
		ret = of_property_read_u32(np, "osc-clkgate-delay",
			&data->osc_clkgate_delay);
		if (ret) {
			dev_err(dev,
				"failed to get osc-clkgate-delay value\n");
			return ERR_PTR(ret);
		}
		/*
		 * 0 <= osc_clkgate_delay <=7
		 * - 0x0 (default) is 0.5ms,
		 * - 0x1-0x7: 1-7ms
		 */
		if (data->osc_clkgate_delay > 7) {
			dev_err(dev,
				"value of osc-clkgate-delay is incorrect\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return data;
}

/* End of common functions shared by usbmisc drivers*/
static int imx_get_clks(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	data->clk_ipg = devm_clk_get(dev, "ipg");
	if (IS_ERR(data->clk_ipg)) {
		/* If the platform only needs one clocks */
		data->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(data->clk)) {
			ret = PTR_ERR(data->clk);
			dev_err(dev,
				"Failed to get clks, err=%ld,%ld\n",
				PTR_ERR(data->clk), PTR_ERR(data->clk_ipg));
			return ret;
		}
		return ret;
	}

	data->clk_ahb = devm_clk_get(dev, "ahb");
	if (IS_ERR(data->clk_ahb)) {
		ret = PTR_ERR(data->clk_ahb);
		dev_err(dev,
			"Failed to get ahb clock, err=%d\n", ret);
		return ret;
	}

	data->clk_per = devm_clk_get(dev, "per");
	if (IS_ERR(data->clk_per)) {
		ret = PTR_ERR(data->clk_per);
		dev_err(dev,
			"Failed to get per clock, err=%d\n", ret);
		return ret;
	}

	data->need_three_clks = true;
	return ret;
}

static int imx_prepare_enable_clks(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	if (data->need_three_clks) {
		ret = clk_prepare_enable(data->clk_ipg);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable ipg clk, err=%d\n",
				ret);
			return ret;
		}

		ret = clk_prepare_enable(data->clk_ahb);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable ahb clk, err=%d\n",
				ret);
			clk_disable_unprepare(data->clk_ipg);
			return ret;
		}

		ret = clk_prepare_enable(data->clk_per);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable per clk, err=%d\n",
				ret);
			clk_disable_unprepare(data->clk_ahb);
			clk_disable_unprepare(data->clk_ipg);
			return ret;
		}
	} else {
		ret = clk_prepare_enable(data->clk);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable clk, err=%d\n",
				ret);
			return ret;
		}
	}

	return ret;
}

static void imx_disable_unprepare_clks(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);

	if (data->need_three_clks) {
		clk_disable_unprepare(data->clk_per);
		clk_disable_unprepare(data->clk_ahb);
		clk_disable_unprepare(data->clk_ipg);
	} else {
		clk_disable_unprepare(data->clk);
	}
}

static int ci_hdrc_imx_notify_event(struct ci_hdrc *ci, unsigned event)
{
	struct device *dev = ci->dev->parent;
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	switch (event) {
	case CI_HDRC_CONTROLLER_VBUS_EVENT:
		if (data->usbmisc_data && ci->vbus_active) {
			if (data->imx_usb_charger_detection) {
				ret = imx_usbmisc_charger_detection(
					data->usbmisc_data, true);
				if (!ret && data->charger.psy_desc.type !=
							POWER_SUPPLY_TYPE_USB)
					ret = CI_HDRC_NOTIFY_RET_DEFER_EVENT;
			}
		} else if (data->usbmisc_data && !ci->vbus_active) {
			if (data->imx_usb_charger_detection)
				ret = imx_usbmisc_charger_detection(
					data->usbmisc_data, false);
		}
		break;
	case CI_HDRC_CONTROLLER_CHARGER_POST_EVENT:
		if (!data->imx_usb_charger_detection)
			return ret;
		imx_usbmisc_charger_secondary_detection(data->usbmisc_data);
		break;
	case CI_HDRC_IMX_HSIC_ACTIVE_EVENT:
		if (!IS_ERR(data->pinctrl) &&
			!IS_ERR(data->pinctrl_hsic_active)) {
			ret = pinctrl_select_state(data->pinctrl,
					data->pinctrl_hsic_active);
			if (ret)
				dev_err(dev,
					"hsic_active select failed, err=%d\n",
					ret);
			return ret;
		}
		break;
	case CI_HDRC_IMX_HSIC_SUSPEND_EVENT:
		if (data->usbmisc_data) {
			ret = imx_usbmisc_hsic_set_connect(data->usbmisc_data);
			if (ret)
				dev_err(dev,
					"hsic_set_connect failed, err=%d\n",
					ret);
			return ret;
		}
		break;
	case CI_HDRC_IMX_TERM_SELECT_OVERRIDE_FS:
		if (data->usbmisc_data)
			return imx_usbmisc_term_select_override(
					data->usbmisc_data, true, 1);
		break;
	case CI_HDRC_IMX_TERM_SELECT_OVERRIDE_OFF:
		if (data->usbmisc_data)
			return imx_usbmisc_term_select_override(
					data->usbmisc_data, false, 0);
		break;
	default:
		dev_dbg(dev, "unknown event\n");
	}

	return ret;
}

static int imx_usb_charger_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct usb_charger *charger =
		container_of(psy->desc, struct usb_charger, psy_desc);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->online;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = charger->max_current;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * imx_usb_register_charger - register a USB charger
 * @charger: the charger to be initialized
 * @name: name for the power supply

 * Registers a power supply for the charger. The USB Controller
 * driver will call this after filling struct usb_charger.
 */
static int imx_usb_register_charger(struct usb_charger *charger,
		const char *name)
{
	struct power_supply_desc	*desc = &charger->psy_desc;

	if (!charger->dev)
		return -EINVAL;

	if (name)
		desc->name = name;
	else
		desc->name = "imx_usb_charger";

	charger->bc = BATTERY_CHARGING_SPEC_1_2;
	mutex_init(&charger->lock);

	desc->type		= POWER_SUPPLY_TYPE_MAINS;
	desc->properties	= imx_usb_charger_power_props;
	desc->num_properties	= ARRAY_SIZE(imx_usb_charger_power_props);
	desc->get_property	= imx_usb_charger_get_property;

	charger->psy = devm_power_supply_register(charger->dev,
						&charger->psy_desc, NULL);
	if (IS_ERR(charger->psy))
		return PTR_ERR(charger->psy);

	charger->psy->supplied_to	= imx_usb_charger_supplied_to;
	charger->psy->num_supplicants	= sizeof(imx_usb_charger_supplied_to)
					/ sizeof(char *);

	return 0;
}

static int ci_hdrc_imx_probe(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data;
	struct ci_hdrc_platform_data pdata = {
		.name		= dev_name(&pdev->dev),
		.capoffset	= DEF_CAPOFFSET,
		.notify_event	= ci_hdrc_imx_notify_event,
	};
	int ret;
	const struct of_device_id *of_id =
			of_match_device(ci_hdrc_imx_dt_ids, &pdev->dev);
	const struct ci_hdrc_imx_platform_flag *imx_platform_flag = of_id->data;
	struct device_node *np = pdev->dev.of_node;
	struct pinctrl_state *pinctrl_hsic_idle;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->data = imx_platform_flag;
	data->usbmisc_data = usbmisc_get_init_data(&pdev->dev);
	if (IS_ERR(data->usbmisc_data))
		return PTR_ERR(data->usbmisc_data);

	data->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(data->pinctrl)) {
		dev_dbg(&pdev->dev, "pinctrl get failed, err=%ld\n",
						PTR_ERR(data->pinctrl));
	} else {
		pinctrl_hsic_idle = pinctrl_lookup_state(data->pinctrl, "idle");
		if (IS_ERR(pinctrl_hsic_idle)) {
			dev_dbg(&pdev->dev,
				"pinctrl_hsic_idle lookup failed, err=%ld\n",
						PTR_ERR(pinctrl_hsic_idle));
		} else {
			ret = pinctrl_select_state(data->pinctrl,
						pinctrl_hsic_idle);
			if (ret) {
				dev_err(&pdev->dev,
					"hsic_idle select failed, err=%d\n",
									ret);
				return ret;
			}
		}

		data->pinctrl_hsic_active = pinctrl_lookup_state(data->pinctrl,
								"active");
		if (IS_ERR(data->pinctrl_hsic_active))
			dev_dbg(&pdev->dev,
				"pinctrl_hsic_active lookup failed, err=%ld\n",
					PTR_ERR(data->pinctrl_hsic_active));
	}

	ret = imx_get_clks(&pdev->dev);
	if (ret)
		return ret;

	request_bus_freq(BUS_FREQ_HIGH);
	ret = imx_prepare_enable_clks(&pdev->dev);
	if (ret)
		goto err_bus_freq;

	data->phy = devm_usb_get_phy_by_phandle(&pdev->dev, "fsl,usbphy", 0);
	if (IS_ERR(data->phy)) {
		ret = PTR_ERR(data->phy);
		/* Return -EINVAL if no usbphy is available */
		if (ret == -ENODEV)
			ret = -EINVAL;
		goto err_clk;
	}

	pdata.usb_phy = data->phy;
	pdata.flags |= imx_platform_flag->flags;
	if (pdata.flags & CI_HDRC_SUPPORTS_RUNTIME_PM)
		data->supports_runtime_pm = true;

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto err_clk;

	if (data->usbmisc_data && data->usbmisc_data->index > 1 &&
					(imx_has_hsic_con(data))) {
		pdata.flags |= CI_HDRC_IMX_IS_HSIC;
		data->hsic_pad_regulator = devm_regulator_get(&pdev->dev,
									"pad");
		if (PTR_ERR(data->hsic_pad_regulator) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_clk;
		} else if (PTR_ERR(data->hsic_pad_regulator) == -ENODEV) {
			/* no pad regualator is needed */
			data->hsic_pad_regulator = NULL;
		} else if (IS_ERR(data->hsic_pad_regulator)) {
			dev_err(&pdev->dev,
				"Get hsic pad regulator error: %ld\n",
					PTR_ERR(data->hsic_pad_regulator));
			ret = PTR_ERR(data->hsic_pad_regulator);
			goto err_clk;
		}

		if (data->hsic_pad_regulator) {
			ret = regulator_enable(data->hsic_pad_regulator);
			if (ret) {
				dev_err(&pdev->dev,
					"Fail to enable hsic pad regulator\n");
				goto err_clk;
			}
		}
	}

	if (of_find_property(np, "fsl,anatop", NULL) && data->usbmisc_data) {
		data->anatop = syscon_regmap_lookup_by_phandle(np,
							"fsl,anatop");
		if (IS_ERR(data->anatop)) {
			dev_dbg(&pdev->dev,
				"failed to find regmap for anatop\n");
			ret = PTR_ERR(data->anatop);
			goto disable_hsic_regulator;
		}
		data->usbmisc_data->anatop = data->anatop;
	}

	if (of_find_property(np, "imx-usb-charger-detection", NULL) &&
							data->usbmisc_data) {
		data->imx_usb_charger_detection = true;
		data->charger.dev = &pdev->dev;
		data->usbmisc_data->charger = &data->charger;
		ret = imx_usb_register_charger(&data->charger,
						"imx_usb_charger");
		if (ret && ret != -ENODEV)
			goto disable_hsic_regulator;
		if (!ret)
			dev_dbg(&pdev->dev,
					"USB Charger is created\n");
	}

	ret = imx_usbmisc_init(data->usbmisc_data);
	if (ret) {
		dev_err(&pdev->dev, "usbmisc init failed, ret=%d\n", ret);
		goto disable_hsic_regulator;
	}

	data->ci_pdev = ci_hdrc_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&pdata);
	if (IS_ERR(data->ci_pdev)) {
		ret = PTR_ERR(data->ci_pdev);
		dev_err(&pdev->dev,
			"Can't register ci_hdrc platform device, err=%d\n",
			ret);
		goto disable_hsic_regulator;
	}

	ret = imx_usbmisc_init_post(data->usbmisc_data);
	if (ret) {
		dev_err(&pdev->dev, "usbmisc post failed, ret=%d\n", ret);
		goto disable_device;
	}

	ret = imx_usbmisc_set_wakeup(data->usbmisc_data, false);
	if (ret) {
		dev_err(&pdev->dev, "usbmisc set_wakeup failed, ret=%d\n", ret);
		goto disable_device;
	}

	/* usbmisc needs to know dr mode to choose wakeup setting */
	if (data->usbmisc_data)
		data->usbmisc_data->available_role =
			ci_hdrc_query_available_role(data->ci_pdev);

	if (data->supports_runtime_pm) {
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
	}

	device_set_wakeup_capable(&pdev->dev, true);

	return 0;

disable_device:
	ci_hdrc_remove_device(data->ci_pdev);
disable_hsic_regulator:
	if (data->hsic_pad_regulator)
		ret = regulator_disable(data->hsic_pad_regulator);
err_clk:
	imx_disable_unprepare_clks(&pdev->dev);
err_bus_freq:
	release_bus_freq(BUS_FREQ_HIGH);
	return ret;
}

static int ci_hdrc_imx_remove(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data = platform_get_drvdata(pdev);

	if (data->supports_runtime_pm) {
		pm_runtime_get_sync(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);
	}
	ci_hdrc_remove_device(data->ci_pdev);
	imx_disable_unprepare_clks(&pdev->dev);
	release_bus_freq(BUS_FREQ_HIGH);
	if (data->hsic_pad_regulator)
		regulator_disable(data->hsic_pad_regulator);

	return 0;
}

#ifdef CONFIG_PM
static int imx_controller_suspend(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "at %s\n", __func__);

	if (data->usbmisc_data) {
		ret = imx_usbmisc_hsic_set_clk(data->usbmisc_data, false);
		if (ret) {
			dev_err(dev,
				"usbmisc hsic_set_clk failed, ret=%d\n", ret);
			return ret;
		}
	}

	imx_disable_unprepare_clks(dev);
	release_bus_freq(BUS_FREQ_HIGH);
	data->in_lpm = true;

	return 0;
}

static int imx_controller_resume(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(dev, "at %s\n", __func__);

	if (!data->in_lpm) {
		WARN_ON(1);
		return 0;
	}

	request_bus_freq(BUS_FREQ_HIGH);
	ret = imx_prepare_enable_clks(dev);
	if (ret)
		return ret;

	data->in_lpm = false;

	ret = imx_usbmisc_power_lost_check(data->usbmisc_data);
	/* re-init if resume from power lost */
	if (ret > 0) {
		ret = imx_usbmisc_init(data->usbmisc_data);
		if (ret) {
			dev_err(dev, "usbmisc init failed, ret=%d\n", ret);
			goto clk_disable;
		}
	}

	ret = imx_usbmisc_set_wakeup(data->usbmisc_data, false);
	if (ret) {
		dev_err(dev, "usbmisc set_wakeup failed, ret=%d\n", ret);
		goto clk_disable;
	}

	ret = imx_usbmisc_hsic_set_clk(data->usbmisc_data, true);
	if (ret) {
		dev_err(dev, "usbmisc hsic_set_clk failed, ret=%d\n", ret);
		goto hsic_set_clk_fail;
	}

	return 0;

hsic_set_clk_fail:
	imx_usbmisc_set_wakeup(data->usbmisc_data, true);
clk_disable:
	imx_disable_unprepare_clks(dev);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int ci_hdrc_imx_suspend(struct device *dev)
{
	int ret;

	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);

	if (data->in_lpm)
		/* The core's suspend doesn't run */
		return 0;

	if (device_may_wakeup(dev)) {
		ret = imx_usbmisc_set_wakeup(data->usbmisc_data, true);
		if (ret) {
			dev_err(dev, "usbmisc set_wakeup failed, ret=%d\n",
					ret);
			return ret;
		}
	}

	return imx_controller_suspend(dev);
}

static int ci_hdrc_imx_resume(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret;

	ret = imx_controller_resume(dev);
	if (!ret && data->supports_runtime_pm) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static int ci_hdrc_imx_runtime_suspend(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret;

	if (data->in_lpm) {
		WARN_ON(1);
		return 0;
	}

	ret = imx_usbmisc_set_wakeup(data->usbmisc_data, true);
	if (ret) {
		dev_err(dev, "usbmisc set_wakeup failed, ret=%d\n", ret);
		return ret;
	}

	return imx_controller_suspend(dev);
}

static int ci_hdrc_imx_runtime_resume(struct device *dev)
{
	return imx_controller_resume(dev);
}

#endif /* CONFIG_PM */

static const struct dev_pm_ops ci_hdrc_imx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ci_hdrc_imx_suspend, ci_hdrc_imx_resume)
	SET_RUNTIME_PM_OPS(ci_hdrc_imx_runtime_suspend,
			ci_hdrc_imx_runtime_resume, NULL)
};
static struct platform_driver ci_hdrc_imx_driver = {
	.probe = ci_hdrc_imx_probe,
	.remove = ci_hdrc_imx_remove,
	.driver = {
		.name = "imx_usb",
		.of_match_table = ci_hdrc_imx_dt_ids,
		.pm = &ci_hdrc_imx_pm_ops,
	 },
};

module_platform_driver(ci_hdrc_imx_driver);

MODULE_ALIAS("platform:imx-usb");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CI HDRC i.MX USB binding");
MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
