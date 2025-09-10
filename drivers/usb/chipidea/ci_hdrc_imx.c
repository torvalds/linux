// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2025 NXP
 * Copyright (C) 2012 Marek Vasut <marex@denx.de>
 * on behalf of DENX Software Engineering GmbH
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/usb/chipidea.h>
#include <linux/usb/of.h>
#include <linux/clk.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_qos.h>

#include "ci.h"
#include "ci_hdrc_imx.h"

struct ci_hdrc_imx_platform_flag {
	unsigned int flags;
};

static const struct ci_hdrc_imx_platform_flag imx23_usb_data = {
	.flags = CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx27_usb_data = {
	.flags = CI_HDRC_DISABLE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx28_usb_data = {
	.flags = CI_HDRC_IMX28_WRITE_FIX |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx6q_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx6sl_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_HOST_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx6sx_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_HOST_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx6ul_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_DEVICE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx7d_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM,
};

static const struct ci_hdrc_imx_platform_flag imx7ulp_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_HAS_PORTSC_PEC_MISSED |
		CI_HDRC_PMQOS,
};

static const struct ci_hdrc_imx_platform_flag imx8ulp_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_HAS_PORTSC_PEC_MISSED,
};

static const struct ci_hdrc_imx_platform_flag s32g_usb_data = {
	.flags = CI_HDRC_DISABLE_HOST_STREAMING,
};

static const struct of_device_id ci_hdrc_imx_dt_ids[] = {
	{ .compatible = "fsl,imx23-usb", .data = &imx23_usb_data},
	{ .compatible = "fsl,imx28-usb", .data = &imx28_usb_data},
	{ .compatible = "fsl,imx27-usb", .data = &imx27_usb_data},
	{ .compatible = "fsl,imx6q-usb", .data = &imx6q_usb_data},
	{ .compatible = "fsl,imx6sl-usb", .data = &imx6sl_usb_data},
	{ .compatible = "fsl,imx6sx-usb", .data = &imx6sx_usb_data},
	{ .compatible = "fsl,imx6ul-usb", .data = &imx6ul_usb_data},
	{ .compatible = "fsl,imx7d-usb", .data = &imx7d_usb_data},
	{ .compatible = "fsl,imx7ulp-usb", .data = &imx7ulp_usb_data},
	{ .compatible = "fsl,imx8ulp-usb", .data = &imx8ulp_usb_data},
	{ .compatible = "nxp,s32g2-usb", .data = &s32g_usb_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ci_hdrc_imx_dt_ids);

struct ci_hdrc_imx_data {
	struct usb_phy *phy;
	struct platform_device *ci_pdev;
	struct clk *clk;
	struct clk *clk_wakeup;
	struct imx_usbmisc_data *usbmisc_data;
	int wakeup_irq;
	bool supports_runtime_pm;
	bool override_phy_control;
	bool in_lpm;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_hsic_active;
	struct regulator *hsic_pad_regulator;
	/* SoC before i.mx6 (except imx23/imx28) needs three clks */
	bool need_three_clks;
	struct clk *clk_ipg;
	struct clk *clk_ahb;
	struct clk *clk_per;
	/* --------------------------------- */
	struct pm_qos_request pm_qos_req;
	const struct ci_hdrc_imx_platform_flag *plat_data;
};

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
	if (!of_property_present(np, "fsl,usbmisc"))
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

	if (!misc_pdev)
		return ERR_PTR(-EPROBE_DEFER);

	if (!platform_get_drvdata(misc_pdev)) {
		put_device(&misc_pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}
	data->dev = &misc_pdev->dev;

	/*
	 * Check the various over current related properties. If over current
	 * detection is disabled we're not interested in the polarity.
	 */
	if (of_property_read_bool(np, "disable-over-current")) {
		data->disable_oc = 1;
	} else if (of_property_read_bool(np, "over-current-active-high")) {
		data->oc_pol_active_low = 0;
		data->oc_pol_configured = 1;
	} else if (of_property_read_bool(np, "over-current-active-low")) {
		data->oc_pol_active_low = 1;
		data->oc_pol_configured = 1;
	} else {
		dev_warn(dev, "No over current polarity defined\n");
	}

	data->pwr_pol = of_property_read_bool(np, "power-active-high");
	data->evdo = of_property_read_bool(np, "external-vbus-divider");

	if (of_usb_get_phy_mode(np) == USBPHY_INTERFACE_MODE_ULPI)
		data->ulpi = 1;

	if (of_property_read_u32(np, "samsung,picophy-pre-emp-curr-control",
			&data->emp_curr_control))
		data->emp_curr_control = -1;
	if (of_property_read_u32(np, "samsung,picophy-dc-vol-level-adjust",
			&data->dc_vol_level_adjust))
		data->dc_vol_level_adjust = -1;
	if (of_property_read_u32(np, "fsl,picophy-rise-fall-time-adjust",
			&data->rise_fall_time_adjust))
		data->rise_fall_time_adjust = -1;

	return data;
}

/* End of common functions shared by usbmisc drivers*/
static int imx_get_clks(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	data->clk_ipg = devm_clk_get(dev, "ipg");
	if (IS_ERR(data->clk_ipg)) {
		/* If the platform only needs one primary clock */
		data->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(data->clk)) {
			ret = PTR_ERR(data->clk);
			dev_err(dev,
				"Failed to get clks, err=%ld,%ld\n",
				PTR_ERR(data->clk), PTR_ERR(data->clk_ipg));
			return ret;
		}
		/* Get wakeup clock. Not all of the platforms need to
		 * handle this clock. So make it optional.
		 */
		data->clk_wakeup = devm_clk_get_optional(dev, "usb_wakeup");
		if (IS_ERR(data->clk_wakeup))
			ret = dev_err_probe(dev, PTR_ERR(data->clk_wakeup),
					"Failed to get wakeup clk\n");
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

static int ci_hdrc_imx_notify_event(struct ci_hdrc *ci, unsigned int event)
{
	struct device *dev = ci->dev->parent;
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;
	struct imx_usbmisc_data *mdata = data->usbmisc_data;

	switch (event) {
	case CI_HDRC_IMX_HSIC_ACTIVE_EVENT:
		if (data->pinctrl) {
			ret = pinctrl_select_state(data->pinctrl,
					data->pinctrl_hsic_active);
			if (ret)
				dev_err(dev,
					"hsic_active select failed, err=%d\n",
					ret);
		}
		break;
	case CI_HDRC_IMX_HSIC_SUSPEND_EVENT:
		ret = imx_usbmisc_hsic_set_connect(mdata);
		if (ret)
			dev_err(dev,
				"hsic_set_connect failed, err=%d\n", ret);
		break;
	case CI_HDRC_CONTROLLER_VBUS_EVENT:
		if (ci->vbus_active)
			ret = imx_usbmisc_charger_detection(mdata, true);
		else
			ret = imx_usbmisc_charger_detection(mdata, false);
		if (ci->usb_phy)
			schedule_work(&ci->usb_phy->chg_work);
		break;
	case CI_HDRC_CONTROLLER_PULLUP_EVENT:
		if (ci->role == CI_ROLE_GADGET)
			imx_usbmisc_pullup(data->usbmisc_data,
					   ci->gadget.connected);
		break;
	default:
		break;
	}

	return ret;
}

static irqreturn_t ci_wakeup_irq_handler(int irq, void *data)
{
	struct ci_hdrc_imx_data *imx_data = data;

	disable_irq_nosync(irq);
	pm_runtime_resume(&imx_data->ci_pdev->dev);

	return IRQ_HANDLED;
}

static void ci_hdrc_imx_disable_regulator(void *arg)
{
	struct ci_hdrc_imx_data *data = arg;

	regulator_disable(data->hsic_pad_regulator);
}

static int ci_hdrc_imx_probe(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data;
	struct ci_hdrc_platform_data pdata = {
		.name		= dev_name(&pdev->dev),
		.capoffset	= DEF_CAPOFFSET,
		.flags		= CI_HDRC_HAS_SHORT_PKT_LIMIT,
		.notify_event	= ci_hdrc_imx_notify_event,
	};
	int ret;
	const struct ci_hdrc_imx_platform_flag *imx_platform_flag;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;

	imx_platform_flag = of_device_get_match_data(&pdev->dev);

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->plat_data = imx_platform_flag;
	pdata.flags |= imx_platform_flag->flags;
	platform_set_drvdata(pdev, data);
	data->usbmisc_data = usbmisc_get_init_data(dev);
	if (IS_ERR(data->usbmisc_data))
		return PTR_ERR(data->usbmisc_data);

	if ((of_usb_get_phy_mode(dev->of_node) == USBPHY_INTERFACE_MODE_HSIC)
		&& data->usbmisc_data) {
		pdata.flags |= CI_HDRC_IMX_IS_HSIC;
		data->usbmisc_data->hsic = 1;
		data->pinctrl = devm_pinctrl_get(dev);
		if (PTR_ERR(data->pinctrl) == -ENODEV)
			data->pinctrl = NULL;
		else if (IS_ERR(data->pinctrl)) {
			ret = dev_err_probe(dev, PTR_ERR(data->pinctrl),
					     "pinctrl get failed\n");
			goto err_put;
		}

		data->hsic_pad_regulator =
				devm_regulator_get_optional(dev, "hsic");
		if (PTR_ERR(data->hsic_pad_regulator) == -ENODEV) {
			/* no pad regulator is needed */
			data->hsic_pad_regulator = NULL;
		} else if (IS_ERR(data->hsic_pad_regulator)) {
			ret = dev_err_probe(dev, PTR_ERR(data->hsic_pad_regulator),
					     "Get HSIC pad regulator error\n");
			goto err_put;
		}

		if (data->hsic_pad_regulator) {
			ret = regulator_enable(data->hsic_pad_regulator);
			if (ret) {
				dev_err(dev,
					"Failed to enable HSIC pad regulator\n");
				goto err_put;
			}
			ret = devm_add_action_or_reset(dev,
					ci_hdrc_imx_disable_regulator, data);
			if (ret) {
				dev_err(dev,
					"Failed to add regulator devm action\n");
				goto err_put;
			}
		}
	}

	/* HSIC pinctrl handling */
	if (data->pinctrl) {
		struct pinctrl_state *pinctrl_hsic_idle;

		pinctrl_hsic_idle = pinctrl_lookup_state(data->pinctrl, "idle");
		if (IS_ERR(pinctrl_hsic_idle)) {
			dev_err(dev,
				"pinctrl_hsic_idle lookup failed, err=%ld\n",
					PTR_ERR(pinctrl_hsic_idle));
			ret = PTR_ERR(pinctrl_hsic_idle);
			goto err_put;
		}

		ret = pinctrl_select_state(data->pinctrl, pinctrl_hsic_idle);
		if (ret) {
			dev_err(dev, "hsic_idle select failed, err=%d\n", ret);
			goto err_put;
		}

		data->pinctrl_hsic_active = pinctrl_lookup_state(data->pinctrl,
								"active");
		if (IS_ERR(data->pinctrl_hsic_active)) {
			dev_err(dev,
				"pinctrl_hsic_active lookup failed, err=%ld\n",
					PTR_ERR(data->pinctrl_hsic_active));
			ret = PTR_ERR(data->pinctrl_hsic_active);
			goto err_put;
		}
	}

	if (pdata.flags & CI_HDRC_PMQOS)
		cpu_latency_qos_add_request(&data->pm_qos_req, 0);

	ret = imx_get_clks(dev);
	if (ret)
		goto qos_remove_request;

	ret = imx_prepare_enable_clks(dev);
	if (ret)
		goto qos_remove_request;

	ret = clk_prepare_enable(data->clk_wakeup);
	if (ret)
		goto err_wakeup_clk;

	data->phy = devm_usb_get_phy_by_phandle(dev, "fsl,usbphy", 0);
	if (IS_ERR(data->phy)) {
		ret = PTR_ERR(data->phy);
		if (ret != -ENODEV) {
			dev_err_probe(dev, ret, "Failed to parse fsl,usbphy\n");
			goto err_clk;
		}
		data->phy = devm_usb_get_phy_by_phandle(dev, "phys", 0);
		if (IS_ERR(data->phy)) {
			ret = PTR_ERR(data->phy);
			if (ret == -ENODEV) {
				data->phy = NULL;
			} else {
				dev_err_probe(dev, ret, "Failed to parse phys\n");
				goto err_clk;
			}
		}
	}

	pdata.usb_phy = data->phy;
	if (data->usbmisc_data)
		data->usbmisc_data->usb_phy = data->phy;

	if ((of_device_is_compatible(np, "fsl,imx53-usb") ||
	     of_device_is_compatible(np, "fsl,imx51-usb")) && pdata.usb_phy &&
	    of_usb_get_phy_mode(np) == USBPHY_INTERFACE_MODE_ULPI) {
		pdata.flags |= CI_HDRC_OVERRIDE_PHY_CONTROL;
		data->override_phy_control = true;
		ret = usb_phy_init(pdata.usb_phy);
		if (ret) {
			dev_err(dev, "Failed to init phy\n");
			goto err_clk;
		}
	}

	if (pdata.flags & CI_HDRC_SUPPORTS_RUNTIME_PM)
		data->supports_runtime_pm = true;

	data->wakeup_irq = platform_get_irq_optional(pdev, 1);
	if (data->wakeup_irq > 0) {
		ret = devm_request_threaded_irq(dev, data->wakeup_irq,
						NULL, ci_wakeup_irq_handler,
						IRQF_ONESHOT | IRQF_NO_AUTOEN,
						pdata.name, data);
		if (ret)
			goto err_clk;
	}

	ret = imx_usbmisc_init(data->usbmisc_data);
	if (ret) {
		dev_err(dev, "usbmisc init failed, ret=%d\n", ret);
		goto phy_shutdown;
	}

	data->ci_pdev = ci_hdrc_add_device(dev,
				pdev->resource, pdev->num_resources,
				&pdata);
	if (IS_ERR(data->ci_pdev)) {
		ret = PTR_ERR(data->ci_pdev);
		dev_err_probe(dev, ret, "ci_hdrc_add_device failed\n");
		goto phy_shutdown;
	}

	if (data->usbmisc_data) {
		if (!IS_ERR(pdata.id_extcon.edev) ||
		    of_property_read_bool(np, "usb-role-switch"))
			data->usbmisc_data->ext_id = 1;

		if (!IS_ERR(pdata.vbus_extcon.edev) ||
		    of_property_read_bool(np, "usb-role-switch"))
			data->usbmisc_data->ext_vbus = 1;

		/* usbmisc needs to know dr mode to choose wakeup setting */
		data->usbmisc_data->available_role =
			ci_hdrc_query_available_role(data->ci_pdev);
	}

	ret = imx_usbmisc_init_post(data->usbmisc_data);
	if (ret) {
		dev_err(dev, "usbmisc post failed, ret=%d\n", ret);
		goto disable_device;
	}

	if (data->supports_runtime_pm) {
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	device_set_wakeup_capable(dev, true);

	return 0;

disable_device:
	ci_hdrc_remove_device(data->ci_pdev);
phy_shutdown:
	if (data->override_phy_control)
		usb_phy_shutdown(data->phy);
err_clk:
	clk_disable_unprepare(data->clk_wakeup);
err_wakeup_clk:
	imx_disable_unprepare_clks(dev);
qos_remove_request:
	if (pdata.flags & CI_HDRC_PMQOS)
		cpu_latency_qos_remove_request(&data->pm_qos_req);
	data->ci_pdev = NULL;
err_put:
	if (data->usbmisc_data)
		put_device(data->usbmisc_data->dev);
	return ret;
}

static void ci_hdrc_imx_remove(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data = platform_get_drvdata(pdev);

	if (data->supports_runtime_pm) {
		pm_runtime_get_sync(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);
	}
	if (data->ci_pdev)
		ci_hdrc_remove_device(data->ci_pdev);
	if (data->override_phy_control)
		usb_phy_shutdown(data->phy);
	if (data->ci_pdev) {
		imx_disable_unprepare_clks(&pdev->dev);
		clk_disable_unprepare(data->clk_wakeup);
		if (data->plat_data->flags & CI_HDRC_PMQOS)
			cpu_latency_qos_remove_request(&data->pm_qos_req);
	}
	if (data->usbmisc_data)
		put_device(data->usbmisc_data->dev);
}

static void ci_hdrc_imx_shutdown(struct platform_device *pdev)
{
	ci_hdrc_imx_remove(pdev);
}

static int imx_controller_suspend(struct device *dev,
						 pm_message_t msg)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(dev, "at %s\n", __func__);

	ret = imx_usbmisc_suspend(data->usbmisc_data,
				  PMSG_IS_AUTO(msg) || device_may_wakeup(dev));
	if (ret) {
		dev_err(dev,
			"usbmisc suspend failed, ret=%d\n", ret);
		return ret;
	}

	imx_disable_unprepare_clks(dev);

	if (data->wakeup_irq > 0)
		enable_irq(data->wakeup_irq);

	if (data->plat_data->flags & CI_HDRC_PMQOS)
		cpu_latency_qos_remove_request(&data->pm_qos_req);

	data->in_lpm = true;

	return 0;
}

static int imx_controller_resume(struct device *dev,
						pm_message_t msg)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(dev, "at %s\n", __func__);

	if (!data->in_lpm) {
		WARN_ON(1);
		return 0;
	}

	if (data->plat_data->flags & CI_HDRC_PMQOS)
		cpu_latency_qos_add_request(&data->pm_qos_req, 0);

	if (data->wakeup_irq > 0 &&
	    !irqd_irq_disabled(irq_get_irq_data(data->wakeup_irq)))
		disable_irq_nosync(data->wakeup_irq);

	ret = imx_prepare_enable_clks(dev);
	if (ret)
		return ret;

	data->in_lpm = false;

	ret = imx_usbmisc_resume(data->usbmisc_data,
				 PMSG_IS_AUTO(msg) || device_may_wakeup(dev));
	if (ret) {
		dev_err(dev, "usbmisc resume failed, ret=%d\n", ret);
		goto clk_disable;
	}

	return 0;

clk_disable:
	imx_disable_unprepare_clks(dev);
	return ret;
}

static int ci_hdrc_imx_suspend(struct device *dev)
{
	int ret;

	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);

	if (data->in_lpm)
		/* The core's suspend doesn't run */
		return 0;

	ret = imx_controller_suspend(dev, PMSG_SUSPEND);
	if (ret)
		return ret;

	pinctrl_pm_select_sleep_state(dev);

	if (data->wakeup_irq > 0 && device_may_wakeup(dev))
		enable_irq_wake(data->wakeup_irq);

	return ret;
}

static int ci_hdrc_imx_resume(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret;

	if (data->wakeup_irq > 0 && device_may_wakeup(dev))
		disable_irq_wake(data->wakeup_irq);

	pinctrl_pm_select_default_state(dev);
	ret = imx_controller_resume(dev, PMSG_RESUME);
	if (!ret && data->supports_runtime_pm) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return ret;
}

static int ci_hdrc_imx_runtime_suspend(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);

	if (data->in_lpm) {
		WARN_ON(1);
		return 0;
	}

	return imx_controller_suspend(dev, PMSG_AUTO_SUSPEND);
}

static int ci_hdrc_imx_runtime_resume(struct device *dev)
{
	return imx_controller_resume(dev, PMSG_AUTO_RESUME);
}

static const struct dev_pm_ops ci_hdrc_imx_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(ci_hdrc_imx_suspend, ci_hdrc_imx_resume)
	RUNTIME_PM_OPS(ci_hdrc_imx_runtime_suspend, ci_hdrc_imx_runtime_resume, NULL)
};
static struct platform_driver ci_hdrc_imx_driver = {
	.probe = ci_hdrc_imx_probe,
	.remove = ci_hdrc_imx_remove,
	.shutdown = ci_hdrc_imx_shutdown,
	.driver = {
		.name = "imx_usb",
		.of_match_table = ci_hdrc_imx_dt_ids,
		.pm = pm_ptr(&ci_hdrc_imx_pm_ops),
	 },
};

module_platform_driver(ci_hdrc_imx_driver);

MODULE_ALIAS("platform:imx-usb");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CI HDRC i.MX USB binding");
MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
