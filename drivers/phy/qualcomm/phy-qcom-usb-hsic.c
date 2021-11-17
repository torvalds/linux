// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (C) 2016 Linaro Ltd
 */
#include <linux/module.h>
#include <linux/ulpi/driver.h>
#include <linux/ulpi/regs.h>
#include <linux/phy/phy.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl-state.h>
#include <linux/delay.h>
#include <linux/clk.h>

#define ULPI_HSIC_CFG		0x30
#define ULPI_HSIC_IO_CAL	0x33

struct qcom_usb_hsic_phy {
	struct ulpi *ulpi;
	struct phy *phy;
	struct pinctrl *pctl;
	struct clk *phy_clk;
	struct clk *cal_clk;
	struct clk *cal_sleep_clk;
};

static int qcom_usb_hsic_phy_power_on(struct phy *phy)
{
	struct qcom_usb_hsic_phy *uphy = phy_get_drvdata(phy);
	struct ulpi *ulpi = uphy->ulpi;
	struct pinctrl_state *pins_default;
	int ret;

	ret = clk_prepare_enable(uphy->phy_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(uphy->cal_clk);
	if (ret)
		goto err_cal;

	ret = clk_prepare_enable(uphy->cal_sleep_clk);
	if (ret)
		goto err_sleep;

	/* Set periodic calibration interval to ~2.048sec in HSIC_IO_CAL_REG */
	ret = ulpi_write(ulpi, ULPI_HSIC_IO_CAL, 0xff);
	if (ret)
		goto err_ulpi;

	/* Enable periodic IO calibration in HSIC_CFG register */
	ret = ulpi_write(ulpi, ULPI_HSIC_CFG, 0xa8);
	if (ret)
		goto err_ulpi;

	/* Configure pins for HSIC functionality */
	pins_default = pinctrl_lookup_state(uphy->pctl, PINCTRL_STATE_DEFAULT);
	if (IS_ERR(pins_default))
		return PTR_ERR(pins_default);

	ret = pinctrl_select_state(uphy->pctl, pins_default);
	if (ret)
		goto err_ulpi;

	 /* Enable HSIC mode in HSIC_CFG register */
	ret = ulpi_write(ulpi, ULPI_SET(ULPI_HSIC_CFG), 0x01);
	if (ret)
		goto err_ulpi;

	/* Disable auto-resume */
	ret = ulpi_write(ulpi, ULPI_CLR(ULPI_IFC_CTRL),
			 ULPI_IFC_CTRL_AUTORESUME);
	if (ret)
		goto err_ulpi;

	return ret;
err_ulpi:
	clk_disable_unprepare(uphy->cal_sleep_clk);
err_sleep:
	clk_disable_unprepare(uphy->cal_clk);
err_cal:
	clk_disable_unprepare(uphy->phy_clk);
	return ret;
}

static int qcom_usb_hsic_phy_power_off(struct phy *phy)
{
	struct qcom_usb_hsic_phy *uphy = phy_get_drvdata(phy);

	clk_disable_unprepare(uphy->cal_sleep_clk);
	clk_disable_unprepare(uphy->cal_clk);
	clk_disable_unprepare(uphy->phy_clk);

	return 0;
}

static const struct phy_ops qcom_usb_hsic_phy_ops = {
	.power_on = qcom_usb_hsic_phy_power_on,
	.power_off = qcom_usb_hsic_phy_power_off,
	.owner = THIS_MODULE,
};

static int qcom_usb_hsic_phy_probe(struct ulpi *ulpi)
{
	struct qcom_usb_hsic_phy *uphy;
	struct phy_provider *p;
	struct clk *clk;

	uphy = devm_kzalloc(&ulpi->dev, sizeof(*uphy), GFP_KERNEL);
	if (!uphy)
		return -ENOMEM;
	ulpi_set_drvdata(ulpi, uphy);

	uphy->ulpi = ulpi;
	uphy->pctl = devm_pinctrl_get(&ulpi->dev);
	if (IS_ERR(uphy->pctl))
		return PTR_ERR(uphy->pctl);

	uphy->phy_clk = clk = devm_clk_get(&ulpi->dev, "phy");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	uphy->cal_clk = clk = devm_clk_get(&ulpi->dev, "cal");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	uphy->cal_sleep_clk = clk = devm_clk_get(&ulpi->dev, "cal_sleep");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	uphy->phy = devm_phy_create(&ulpi->dev, ulpi->dev.of_node,
				    &qcom_usb_hsic_phy_ops);
	if (IS_ERR(uphy->phy))
		return PTR_ERR(uphy->phy);
	phy_set_drvdata(uphy->phy, uphy);

	p = devm_of_phy_provider_register(&ulpi->dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(p);
}

static const struct of_device_id qcom_usb_hsic_phy_match[] = {
	{ .compatible = "qcom,usb-hsic-phy", },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_usb_hsic_phy_match);

static struct ulpi_driver qcom_usb_hsic_phy_driver = {
	.probe = qcom_usb_hsic_phy_probe,
	.driver = {
		.name = "qcom_usb_hsic_phy",
		.of_match_table = qcom_usb_hsic_phy_match,
	},
};
module_ulpi_driver(qcom_usb_hsic_phy_driver);

MODULE_DESCRIPTION("Qualcomm USB HSIC phy");
MODULE_LICENSE("GPL v2");
