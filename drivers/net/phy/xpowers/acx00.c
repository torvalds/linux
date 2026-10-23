// SPDX-License-Identifier: GPL-2.0-only
/*
 * X-Powers AC200/AC300 Ethernet PHY driver
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/slab.h>

#include "../phylib.h"
#include "acx00.h"

#define ACX00_EPHY_ID				0x00441400

#define ACX00_EPHY_CONFIG_CALIBRATION_MASK	GENMASK(3, 0)
#define ACX00_EPHY_CONFIG_VARIANT_AC300		BIT(8)
#define ACX00_EPHY_CONFIG_CALIBRATION_LOW	BIT(9)

#define ACX00_PAGE_SELECT_REG			0x1f
#define ACX00_PAGE_SELECT_MASK			GENMASK(12, 8)
#define ACX00_PAGE_0				0
#define ACX00_PAGE_1				1
#define ACX00_PAGE_2				2
#define ACX00_PAGE_6				6
#define ACX00_PAGE_8				8

#define ACX00_PAGE0_GLOBAL_CONFIG_REG		0x13
#define ACX00_PAGE0_XMII_RX_CLOCK_INVERT	BIT(12)
#define ACX00_PAGE0_MDI_MODE_MASK		GENMASK(1, 0)
#define ACX00_PAGE0_MDI_MODE_AUTO		2

#define ACX00_PAGE1_APS_CONTROL_REG		0x12
#define ACX00_PAGE1_APS_DISABLED_4S_VALUE	0x4824
#define ACX00_PAGE1_UAPS_CONTROL_REG		0x13
#define ACX00_PAGE1_UAPS_ENABLE			BIT(15)
#define ACX00_PAGE1_INTELLIGENT_EEE_CONTROL_REG	0x17
#define ACX00_PAGE1_INTELLIGENT_EEE_ENABLE	BIT(3)

#define ACX00_PAGE2_TX_DATA_CONTROL_REG		0x18
#define ACX00_PAGE2_10BT_FIR_SELECT_MASK		GENMASK(14, 12)
#define ACX00_PAGE2_10BT_FIR_SELECT_DEFAULT	0

#define ACX00_PAGE6_ADC_CONTROL_REG		0x10
#define ACX00_PAGE6_ADC_CONTROL_LOW_CAL_VALUE	0x5523
#define ACX00_PAGE6_AFE_RX_CONTROL_REG		0x13
#define ACX00_PAGE6_AFE_RX_CONTROL_VALUE		0xf000
#define ACX00_PAGE6_AFE_EQ_RX_DETECT_CONTROL_REG	0x14
#define AC200_PAGE6_AFE_EQ_RX_DETECT_VALUE	0x708f
#define AC300_PAGE6_AFE_EQ_RX_DETECT_VALUE	0x708b
#define ACX00_PAGE6_AFE_EQ_RX_DETECT_LOW_CAL_VALUE 0x7809
#define ACX00_PAGE6_TX_LEVEL_REG			0x15
#define ACX00_PAGE6_TX_LEVEL_100M_MASK		GENMASK(15, 8)
#define ACX00_PAGE6_TX_LEVEL_10M_MASK		GENMASK(7, 0)
#define ACX00_PAGE6_TX_LEVEL_VALUE(_100m, _10m) \
	(FIELD_PREP(ACX00_PAGE6_TX_LEVEL_100M_MASK, (_100m)) | \
	 FIELD_PREP(ACX00_PAGE6_TX_LEVEL_10M_MASK, (_10m)))
#define ACX00_PAGE6_TX_LEVEL_DEFAULT_VALUE \
	ACX00_PAGE6_TX_LEVEL_VALUE(0x15, 0x30)
#define ACX00_PAGE6_TX_LEVEL_LOW_CAL_VALUE \
	ACX00_PAGE6_TX_LEVEL_VALUE(0x35, 0x33)

#define ACX00_PAGE8_AFE_CONTROL_REG		0x18
#define ACX00_PAGE8_AFE_CONTROL_VALUE		0x00bc
#define ACX00_PAGE8_AUTO_CAL_CONTROL_REG		0x1d
#define ACX00_PAGE8_AUTO_CAL_TX_LEVEL_ADJUST_BYPASS BIT(11)
#define ACX00_PAGE8_AUTO_CAL_LOW_CAL_OPAQUE_BITS	0x0044
#define ACX00_PAGE8_AUTO_CAL_LOW_VALUE \
	(ACX00_PAGE8_AUTO_CAL_TX_LEVEL_ADJUST_BYPASS | \
	 ACX00_PAGE8_AUTO_CAL_LOW_CAL_OPAQUE_BITS)

/*
 * Another integration of this exact-ID PHY documents its digital vendor
 * register map, which also matches the observed ACx00 reset values. The ACx00
 * analog-page field encodings remain unpublished, so keep those as opaque
 * vendor initialization values instead of inventing bit definitions.
 */

struct acx00_ephy_priv {
	struct phy_device *phydev;
	struct acx00_ephy_control *control;
	/* Serializes package state and multi-register power sequences. */
	struct mutex state_lock;
	bool is_ac300;
	bool use_low_calibration_tuning;
	bool xmii_rx_clock_inverted;
};

static int acx00_ephy_read_page(struct phy_device *phydev)
{
	int ret;

	ret = __phy_read(phydev, ACX00_PAGE_SELECT_REG);
	if (ret < 0)
		return ret;

	return FIELD_GET(ACX00_PAGE_SELECT_MASK, ret);
}

static int acx00_ephy_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, ACX00_PAGE_SELECT_REG,
			   FIELD_PREP(ACX00_PAGE_SELECT_MASK, page));
}

static int acx00_ephy_control_power_on(struct acx00_ephy_priv *priv)
{
	lockdep_assert_held(&priv->state_lock);

	return priv->control->power_on(priv->control,
				       priv->phydev->mdio.addr);
}

static int acx00_ephy_control_power_off(struct acx00_ephy_priv *priv)
{
	lockdep_assert_held(&priv->state_lock);

	return priv->control->power_off(priv->control);
}

static int acx00_ephy_set_interface(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;

	lockdep_assert_held(&priv->state_lock);

	if (phydev->interface == PHY_INTERFACE_MODE_NA)
		return 0;
	if (phydev->interface != PHY_INTERFACE_MODE_MII &&
	    phydev->interface != PHY_INTERFACE_MODE_RMII)
		return -EINVAL;

	return priv->control->set_interface(priv->control,
					    phydev->interface);
}

static void acx00_ephy_control_power_off_warn(struct acx00_ephy_priv *priv)
{
	int ret;

	ret = acx00_ephy_control_power_off(priv);
	if (ret)
		phydev_warn(priv->phydev,
			    "failed to power off control block: %pe\n",
			    ERR_PTR(ret));
}

static void acx00_ephy_control_release(void *data)
{
	struct acx00_ephy_priv *priv = data;

	mutex_lock(&priv->state_lock);
	acx00_ephy_control_power_off_warn(priv);
	mutex_unlock(&priv->state_lock);
}

static int acx00_ephy_read_nvmem_u16(struct device_node *node,
				     const char *name, size_t min_len,
				     u16 *value)
{
	struct nvmem_cell *cell;
	size_t i;
	size_t len;
	u8 *buf;
	u16 val = 0;

	cell = of_nvmem_cell_get(node, name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	if (len < min_len || len > sizeof(*value)) {
		kfree(buf);
		return len > sizeof(*value) ? -ERANGE : -EINVAL;
	}

	for (i = 0; i < len; i++)
		val |= (u16)buf[i] << (8 * i);
	kfree(buf);
	*value = val;

	return 0;
}

static int acx00_ephy_init_package(struct phy_device *phydev,
				   struct acx00_ephy_priv *priv)
{
	struct device *dev = &phydev->mdio.dev;
	struct device_node *package_node;
	bool selectable;
	bool fixed_ac300;
	bool has_configuration;
	u8 calibration;
	u32 base_addr;
	u16 configuration = 0;
	int ret;

	package_node = of_get_parent(dev->of_node);
	if (!package_node)
		return -EINVAL;
	if (!of_node_name_eq(package_node, "ethernet-phy-package")) {
		ret = dev_err_probe(dev, -EINVAL,
				    "PHY is not in an Ethernet PHY package\n");
		goto out_put_node;
	}

	ret = of_property_read_u32(package_node, "reg", &base_addr);
	if (ret || base_addr != phydev->mdio.addr) {
		ret = dev_err_probe(dev, -EINVAL,
				    "package and link PHY addresses differ\n");
		goto out_put_node;
	}

	selectable = of_device_is_compatible(package_node,
					     "x-powers,acx00-ephy-package");
	fixed_ac300 = of_device_is_compatible(package_node,
					      "x-powers,ac300-ephy-package");
	if (!selectable && !fixed_ac300 &&
	    !of_device_is_compatible(package_node,
				     "x-powers,ac200-ephy-package")) {
		ret = dev_err_probe(dev, -EINVAL,
				    "unsupported Ethernet PHY package\n");
		goto out_put_node;
	}

	ret = devm_of_phy_package_join(dev, phydev, 0);
	if (ret)
		goto out_put_node;

	has_configuration =
		of_property_match_string(package_node, "nvmem-cell-names",
					 "configuration") >= 0;
	if (has_configuration) {
		ret = acx00_ephy_read_nvmem_u16(package_node, "configuration",
						selectable || fixed_ac300 ? 2 : 1,
						&configuration);
		if (ret) {
			ret = dev_err_probe(dev, ret,
					    "failed to read package configuration\n");
			goto out_put_node;
		}
	} else if (selectable || fixed_ac300) {
		ret = dev_err_probe(dev, -EINVAL,
				    "package configuration is required\n");
		goto out_put_node;
	}

	if (selectable) {
		priv->is_ac300 =
			configuration & ACX00_EPHY_CONFIG_VARIANT_AC300;
	} else {
		priv->is_ac300 = fixed_ac300;
		if (has_configuration &&
		    !!(configuration & ACX00_EPHY_CONFIG_VARIANT_AC300) !=
		    priv->is_ac300) {
			ret = dev_err_probe(dev, -EINVAL,
					    "configuration does not match package\n");
			goto out_put_node;
		}
	}

	calibration = FIELD_GET(ACX00_EPHY_CONFIG_CALIBRATION_MASK,
				configuration);

	priv->use_low_calibration_tuning =
		priv->is_ac300 &&
		!!(configuration & ACX00_EPHY_CONFIG_CALIBRATION_LOW);
	priv->xmii_rx_clock_inverted =
		of_property_read_bool(package_node,
				      "x-powers,xmii-rx-clock-inverted");

	if (priv->is_ac300)
		priv->control =
			ac300_ephy_ctl_create(phydev, package_node,
					      calibration);
	else
		priv->control =
			ac200_ephy_ctl_create(phydev, package_node,
					      has_configuration,
					      calibration);
	ret = PTR_ERR_OR_ZERO(priv->control);

out_put_node:
	of_node_put(package_node);
	return ret;
}

static int acx00_ephy_disable_autonomous_eee(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	int ret;

	mutex_lock(&priv->state_lock);
	ret = phy_modify_paged(phydev, ACX00_PAGE_1,
			       ACX00_PAGE1_INTELLIGENT_EEE_CONTROL_REG,
			       ACX00_PAGE1_INTELLIGENT_EEE_ENABLE, 0);
	mutex_unlock(&priv->state_lock);

	return ret;
}

static int acx00_ephy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct acx00_ephy_priv *priv;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phydev = phydev;
	mutex_init(&priv->state_lock);
	ret = acx00_ephy_init_package(phydev, priv);
	if (ret)
		return ret;

	phydev->priv = priv;
	ret = devm_add_action_or_reset(dev, acx00_ephy_control_release, priv);
	if (ret)
		return ret;

	mutex_lock(&priv->state_lock);
	ret = acx00_ephy_control_power_on(priv);
	mutex_unlock(&priv->state_lock);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to power on control block\n");

	return 0;
}

static int acx00_ephy_soft_reset(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	int ret;

	mutex_lock(&priv->state_lock);

	ret = acx00_ephy_set_interface(phydev);
	if (ret)
		goto out_power_off;

	ret = acx00_ephy_control_power_on(priv);
	if (ret)
		goto out_power_off;

	/* ACx00 can acknowledge reset in power-down without restarting. */
	ret = genphy_resume(phydev);
	if (ret)
		goto out_power_off;

	ret = genphy_soft_reset(phydev);
	if (ret)
		goto out_power_off;

	goto out_unlock;

out_power_off:
	acx00_ephy_control_power_off_warn(priv);
out_unlock:
	mutex_unlock(&priv->state_lock);

	return ret;
}

static int __acx00_ephy_config_init(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	u16 afe_eq_rx_detect = priv->is_ac300 ?
		AC300_PAGE6_AFE_EQ_RX_DETECT_VALUE :
		AC200_PAGE6_AFE_EQ_RX_DETECT_VALUE;
	u16 tx_level_value = ACX00_PAGE6_TX_LEVEL_DEFAULT_VALUE;
	u16 global_config;
	int oldpage;
	int ret;

	lockdep_assert_held(&priv->state_lock);

	global_config = FIELD_PREP(ACX00_PAGE0_MDI_MODE_MASK,
				   ACX00_PAGE0_MDI_MODE_AUTO);
	if (priv->xmii_rx_clock_inverted)
		global_config |= ACX00_PAGE0_XMII_RX_CLOCK_INVERT;

	ret = phy_modify_paged(phydev, ACX00_PAGE_0,
			       ACX00_PAGE0_GLOBAL_CONFIG_REG,
			       ACX00_PAGE0_XMII_RX_CLOCK_INVERT |
			       ACX00_PAGE0_MDI_MODE_MASK, global_config);
	if (ret)
		return ret;

	if (priv->is_ac300 && priv->use_low_calibration_tuning) {
		afe_eq_rx_detect =
			ACX00_PAGE6_AFE_EQ_RX_DETECT_LOW_CAL_VALUE;
		tx_level_value = ACX00_PAGE6_TX_LEVEL_LOW_CAL_VALUE;
	}

	oldpage = phy_select_page(phydev, ACX00_PAGE_1);
	if (oldpage < 0)
		return phy_restore_page(phydev, oldpage, oldpage);

	ret = __phy_write(phydev, ACX00_PAGE1_APS_CONTROL_REG,
			  ACX00_PAGE1_APS_DISABLED_4S_VALUE);
	if (ret)
		goto out_restore_page;
	ret = __phy_modify(phydev, ACX00_PAGE1_UAPS_CONTROL_REG,
			   ACX00_PAGE1_UAPS_ENABLE, 0);
	if (ret)
		goto out_restore_page;
	ret = __phy_modify(phydev, ACX00_PAGE1_INTELLIGENT_EEE_CONTROL_REG,
			   ACX00_PAGE1_INTELLIGENT_EEE_ENABLE, 0);
	if (ret)
		goto out_restore_page;

	ret = acx00_ephy_write_page(phydev, ACX00_PAGE_2);
	if (ret)
		goto out_restore_page;
	ret = __phy_modify(phydev, ACX00_PAGE2_TX_DATA_CONTROL_REG,
			   ACX00_PAGE2_10BT_FIR_SELECT_MASK,
			   FIELD_PREP(ACX00_PAGE2_10BT_FIR_SELECT_MASK,
				      ACX00_PAGE2_10BT_FIR_SELECT_DEFAULT));
	if (ret)
		goto out_restore_page;

	ret = acx00_ephy_write_page(phydev, ACX00_PAGE_6);
	if (ret)
		goto out_restore_page;
	ret = __phy_write(phydev, ACX00_PAGE6_AFE_EQ_RX_DETECT_CONTROL_REG,
			  afe_eq_rx_detect);
	if (ret)
		goto out_restore_page;
	ret = __phy_write(phydev, ACX00_PAGE6_AFE_RX_CONTROL_REG,
			  ACX00_PAGE6_AFE_RX_CONTROL_VALUE);
	if (ret)
		goto out_restore_page;
	if (priv->is_ac300 && priv->use_low_calibration_tuning) {
		ret = __phy_write(phydev, ACX00_PAGE6_ADC_CONTROL_REG,
				  ACX00_PAGE6_ADC_CONTROL_LOW_CAL_VALUE);
		if (ret)
			goto out_restore_page;
	}
	ret = __phy_write(phydev, ACX00_PAGE6_TX_LEVEL_REG, tx_level_value);
	if (ret)
		goto out_restore_page;

	ret = acx00_ephy_write_page(phydev, ACX00_PAGE_8);
	if (ret)
		goto out_restore_page;
	if (priv->is_ac300 && priv->use_low_calibration_tuning) {
		ret = __phy_write(phydev, ACX00_PAGE8_AUTO_CAL_CONTROL_REG,
				  ACX00_PAGE8_AUTO_CAL_LOW_VALUE);
		if (ret)
			goto out_restore_page;
	}
	ret = __phy_write(phydev, ACX00_PAGE8_AFE_CONTROL_REG,
			  ACX00_PAGE8_AFE_CONTROL_VALUE);

out_restore_page:
	ret = phy_restore_page(phydev, oldpage, ret);
	if (ret)
		return ret;

	/* Restore the standard EEE policy retained by phylib across resets. */
	ret = genphy_c45_an_config_eee_aneg(phydev);
	if (ret <= 0)
		return ret;

	return genphy_restart_aneg(phydev);
}

static int acx00_ephy_config_init(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	int ret;

	mutex_lock(&priv->state_lock);
	ret = __acx00_ephy_config_init(phydev);
	if (ret)
		acx00_ephy_control_power_off_warn(priv);
	mutex_unlock(&priv->state_lock);

	return ret;
}

static int __acx00_ephy_power_on_and_resume(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	int ret;

	lockdep_assert_held(&phydev->lock);
	lockdep_assert_held(&priv->state_lock);

	ret = acx00_ephy_set_interface(phydev);
	if (ret)
		goto out_power_off;

	ret = acx00_ephy_control_power_on(priv);
	if (ret)
		goto out_power_off;

	ret = genphy_resume(phydev);
	if (ret)
		goto out_power_off;

	/* Powering off the control block loses the vendor-page state. */
	ret = __acx00_ephy_config_init(phydev);
	if (ret)
		goto out_power_off;

	return 0;

out_power_off:
	acx00_ephy_control_power_off_warn(priv);

	return ret;
}

static int acx00_ephy_resume(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	int ret;

	mutex_lock(&priv->state_lock);
	ret = __acx00_ephy_power_on_and_resume(phydev);
	mutex_unlock(&priv->state_lock);

	return ret;
}

static int acx00_ephy_suspend(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	int resume_ret;
	int ret;

	/* The suspend callback is invoked without the phylib lock held. */
	mutex_lock(&phydev->lock);
	mutex_lock(&priv->state_lock);

	ret = genphy_suspend(phydev);
	if (ret)
		goto out_unlock;

	ret = acx00_ephy_control_power_off(priv);
	if (ret) {
		resume_ret = __acx00_ephy_power_on_and_resume(phydev);
		if (!resume_ret) {
			/*
			 * The package reset also loses the standard link settings.
			 * A failed suspend is not followed by resume, so restore
			 * advertisement or forced speed/duplex before returning.
			 */
			resume_ret = genphy_config_aneg(phydev);
			if (resume_ret)
				acx00_ephy_control_power_off_warn(priv);
		}
		if (resume_ret)
			phydev_warn(phydev,
				    "failed to recover from suspend error: %pe\n",
				    ERR_PTR(resume_ret));
	}

out_unlock:
	mutex_unlock(&priv->state_lock);
	mutex_unlock(&phydev->lock);

	return ret;
}

static int acx00_ephy_match_phy_device(struct phy_device *phydev,
				       const struct phy_driver *phydrv)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct device_node *package_node;
	bool match;

	if (!genphy_match_phy_device(phydev, phydrv) || !node)
		return 0;

	/* RK630 reports the same PHY ID, so also match the package identity. */
	package_node = of_get_parent(node);
	if (!package_node)
		return 0;
	match = of_device_is_compatible(package_node,
					"x-powers,ac200-ephy-package") ||
		of_device_is_compatible(package_node,
					"x-powers,ac300-ephy-package") ||
		of_device_is_compatible(package_node,
					"x-powers,acx00-ephy-package");
	of_node_put(package_node);

	return match;
}

static struct phy_driver acx00_ephy_driver[] = {
	{
		PHY_ID_MATCH_MODEL(ACX00_EPHY_ID),
		.name = "X-Powers AC200/AC300 EPHY",
		.match_phy_device = acx00_ephy_match_phy_device,
		.probe = acx00_ephy_probe,
		.read_page = acx00_ephy_read_page,
		.write_page = acx00_ephy_write_page,
		.soft_reset = acx00_ephy_soft_reset,
		.config_init = acx00_ephy_config_init,
		.disable_autonomous_eee = acx00_ephy_disable_autonomous_eee,
		.suspend = acx00_ephy_suspend,
		.resume = acx00_ephy_resume,
	},
};
module_phy_driver(acx00_ephy_driver);

static const struct mdio_device_id __maybe_unused acx00_ephy_tbl[] = {
	{ PHY_ID_MATCH_MODEL(ACX00_EPHY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, acx00_ephy_tbl);

MODULE_AUTHOR("James Hilliard <james.hilliard1@gmail.com>");
MODULE_DESCRIPTION("X-Powers AC200/AC300 Ethernet PHY driver");
MODULE_LICENSE("GPL");
