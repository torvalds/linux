/**
 * Copyright (C) 2016 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/ulpi/driver.h>
#include <linux/ulpi/regs.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>
#include <linux/extcon.h>
#include <linux/notifier.h>

#define ULPI_PWR_CLK_MNG_REG		0x88
# define ULPI_PWR_OTG_COMP_DISABLE	BIT(0)

#define ULPI_MISC_A			0x96
# define ULPI_MISC_A_VBUSVLDEXTSEL	BIT(1)
# define ULPI_MISC_A_VBUSVLDEXT		BIT(0)


struct ulpi_seq {
	u8 addr;
	u8 val;
};

struct qcom_usb_hs_phy {
	struct ulpi *ulpi;
	struct phy *phy;
	struct clk *ref_clk;
	struct clk *sleep_clk;
	struct regulator *v1p8;
	struct regulator *v3p3;
	struct reset_control *reset;
	struct ulpi_seq *init_seq;
	struct extcon_dev *vbus_edev;
	struct notifier_block vbus_notify;
};

static int qcom_usb_hs_phy_set_mode(struct phy *phy, enum phy_mode mode)
{
	struct qcom_usb_hs_phy *uphy = phy_get_drvdata(phy);
	u8 addr;
	int ret;

	if (!uphy->vbus_edev) {
		u8 val = 0;

		switch (mode) {
		case PHY_MODE_USB_OTG:
		case PHY_MODE_USB_HOST:
			val |= ULPI_INT_IDGRD;
		case PHY_MODE_USB_DEVICE:
			val |= ULPI_INT_SESS_VALID;
		default:
			break;
		}

		ret = ulpi_write(uphy->ulpi, ULPI_USB_INT_EN_RISE, val);
		if (ret)
			return ret;
		ret = ulpi_write(uphy->ulpi, ULPI_USB_INT_EN_FALL, val);
	} else {
		switch (mode) {
		case PHY_MODE_USB_OTG:
		case PHY_MODE_USB_DEVICE:
			addr = ULPI_SET(ULPI_MISC_A);
			break;
		case PHY_MODE_USB_HOST:
			addr = ULPI_CLR(ULPI_MISC_A);
			break;
		default:
			return -EINVAL;
		}

		ret = ulpi_write(uphy->ulpi, ULPI_SET(ULPI_PWR_CLK_MNG_REG),
				 ULPI_PWR_OTG_COMP_DISABLE);
		if (ret)
			return ret;
		ret = ulpi_write(uphy->ulpi, addr, ULPI_MISC_A_VBUSVLDEXTSEL);
	}

	return ret;
}

static int
qcom_usb_hs_phy_vbus_notifier(struct notifier_block *nb, unsigned long event,
			      void *ptr)
{
	struct qcom_usb_hs_phy *uphy;
	u8 addr;

	uphy = container_of(nb, struct qcom_usb_hs_phy, vbus_notify);

	if (event)
		addr = ULPI_SET(ULPI_MISC_A);
	else
		addr = ULPI_CLR(ULPI_MISC_A);

	return ulpi_write(uphy->ulpi, addr, ULPI_MISC_A_VBUSVLDEXT);
}

static int qcom_usb_hs_phy_power_on(struct phy *phy)
{
	struct qcom_usb_hs_phy *uphy = phy_get_drvdata(phy);
	struct ulpi *ulpi = uphy->ulpi;
	const struct ulpi_seq *seq;
	int ret, state;

	ret = clk_prepare_enable(uphy->ref_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(uphy->sleep_clk);
	if (ret)
		goto err_sleep;

	ret = regulator_set_load(uphy->v1p8, 50000);
	if (ret < 0)
		goto err_1p8;

	ret = regulator_enable(uphy->v1p8);
	if (ret)
		goto err_1p8;

	ret = regulator_set_voltage_triplet(uphy->v3p3, 3050000, 3300000,
					    3300000);
	if (ret)
		goto err_3p3;

	ret = regulator_set_load(uphy->v3p3, 50000);
	if (ret < 0)
		goto err_3p3;

	ret = regulator_enable(uphy->v3p3);
	if (ret)
		goto err_3p3;

	for (seq = uphy->init_seq; seq->addr; seq++) {
		ret = ulpi_write(ulpi, ULPI_EXT_VENDOR_SPECIFIC + seq->addr,
				 seq->val);
		if (ret)
			goto err_ulpi;
	}

	if (uphy->reset) {
		ret = reset_control_reset(uphy->reset);
		if (ret)
			goto err_ulpi;
	}

	if (uphy->vbus_edev) {
		state = extcon_get_cable_state_(uphy->vbus_edev, EXTCON_USB);
		/* setup initial state */
		qcom_usb_hs_phy_vbus_notifier(&uphy->vbus_notify, state,
					      uphy->vbus_edev);
		ret = extcon_register_notifier(uphy->vbus_edev, EXTCON_USB,
				&uphy->vbus_notify);
		if (ret)
			goto err_ulpi;
	}

	return 0;
err_ulpi:
	regulator_disable(uphy->v3p3);
err_3p3:
	regulator_disable(uphy->v1p8);
err_1p8:
	clk_disable_unprepare(uphy->sleep_clk);
err_sleep:
	clk_disable_unprepare(uphy->ref_clk);
	return ret;
}

static int qcom_usb_hs_phy_power_off(struct phy *phy)
{
	int ret;
	struct qcom_usb_hs_phy *uphy = phy_get_drvdata(phy);

	if (uphy->vbus_edev) {
		ret = extcon_unregister_notifier(uphy->vbus_edev, EXTCON_USB,
						 &uphy->vbus_notify);
		if (ret)
			return ret;
	}

	regulator_disable(uphy->v3p3);
	regulator_disable(uphy->v1p8);
	clk_disable_unprepare(uphy->sleep_clk);
	clk_disable_unprepare(uphy->ref_clk);

	return 0;
}

static const struct phy_ops qcom_usb_hs_phy_ops = {
	.power_on = qcom_usb_hs_phy_power_on,
	.power_off = qcom_usb_hs_phy_power_off,
	.set_mode = qcom_usb_hs_phy_set_mode,
	.owner = THIS_MODULE,
};

static int qcom_usb_hs_phy_probe(struct ulpi *ulpi)
{
	struct qcom_usb_hs_phy *uphy;
	struct phy_provider *p;
	struct clk *clk;
	struct regulator *reg;
	struct reset_control *reset;
	int size;
	int ret;

	uphy = devm_kzalloc(&ulpi->dev, sizeof(*uphy), GFP_KERNEL);
	if (!uphy)
		return -ENOMEM;
	ulpi_set_drvdata(ulpi, uphy);
	uphy->ulpi = ulpi;

	size = of_property_count_u8_elems(ulpi->dev.of_node, "qcom,init-seq");
	if (size < 0)
		size = 0;
	uphy->init_seq = devm_kmalloc_array(&ulpi->dev, (size / 2) + 1,
					   sizeof(*uphy->init_seq), GFP_KERNEL);
	if (!uphy->init_seq)
		return -ENOMEM;
	ret = of_property_read_u8_array(ulpi->dev.of_node, "qcom,init-seq",
					(u8 *)uphy->init_seq, size);
	if (ret && size)
		return ret;
	/* NUL terminate */
	uphy->init_seq[size / 2].addr = uphy->init_seq[size / 2].val = 0;

	uphy->ref_clk = clk = devm_clk_get(&ulpi->dev, "ref");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	uphy->sleep_clk = clk = devm_clk_get(&ulpi->dev, "sleep");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	uphy->v1p8 = reg = devm_regulator_get(&ulpi->dev, "v1p8");
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	uphy->v3p3 = reg = devm_regulator_get(&ulpi->dev, "v3p3");
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	uphy->reset = reset = devm_reset_control_get(&ulpi->dev, "por");
	if (IS_ERR(reset)) {
		if (PTR_ERR(reset) == -EPROBE_DEFER)
			return PTR_ERR(reset);
		uphy->reset = NULL;
	}

	uphy->phy = devm_phy_create(&ulpi->dev, ulpi->dev.of_node,
				    &qcom_usb_hs_phy_ops);
	if (IS_ERR(uphy->phy))
		return PTR_ERR(uphy->phy);

	uphy->vbus_edev = extcon_get_edev_by_phandle(&ulpi->dev, 0);
	if (IS_ERR(uphy->vbus_edev)) {
		if (PTR_ERR(uphy->vbus_edev) != -ENODEV)
			return PTR_ERR(uphy->vbus_edev);
		uphy->vbus_edev = NULL;
	}

	uphy->vbus_notify.notifier_call = qcom_usb_hs_phy_vbus_notifier;
	phy_set_drvdata(uphy->phy, uphy);

	p = devm_of_phy_provider_register(&ulpi->dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(p);
}

static const struct of_device_id qcom_usb_hs_phy_match[] = {
	{ .compatible = "qcom,usb-hs-phy", },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_usb_hs_phy_match);

static struct ulpi_driver qcom_usb_hs_phy_driver = {
	.probe = qcom_usb_hs_phy_probe,
	.driver = {
		.name = "qcom_usb_hs_phy",
		.of_match_table = qcom_usb_hs_phy_match,
	},
};
module_ulpi_driver(qcom_usb_hs_phy_driver);

MODULE_DESCRIPTION("Qualcomm USB HS phy");
MODULE_LICENSE("GPL v2");
