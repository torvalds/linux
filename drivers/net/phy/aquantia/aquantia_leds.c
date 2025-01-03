// SPDX-License-Identifier: GPL-2.0
/* LED driver for Aquantia PHY
 *
 * Author: Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/phy.h>

#include "aquantia.h"

int aqr_phy_led_brightness_set(struct phy_device *phydev,
			       u8 index, enum led_brightness value)
{
	if (index >= AQR_MAX_LEDS)
		return -EINVAL;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, AQR_LED_PROV(index),
			      VEND1_GLOBAL_LED_PROV_LINK_MASK |
			      VEND1_GLOBAL_LED_PROV_FORCE_ON |
			      VEND1_GLOBAL_LED_PROV_RX_ACT |
			      VEND1_GLOBAL_LED_PROV_TX_ACT,
			      value ? VEND1_GLOBAL_LED_PROV_FORCE_ON : 0);
}

static const unsigned long supported_triggers = (BIT(TRIGGER_NETDEV_LINK) |
						 BIT(TRIGGER_NETDEV_LINK_100) |
						 BIT(TRIGGER_NETDEV_LINK_1000) |
						 BIT(TRIGGER_NETDEV_LINK_2500) |
						 BIT(TRIGGER_NETDEV_LINK_5000) |
						 BIT(TRIGGER_NETDEV_LINK_10000)  |
						 BIT(TRIGGER_NETDEV_RX) |
						 BIT(TRIGGER_NETDEV_TX));

int aqr_phy_led_hw_is_supported(struct phy_device *phydev, u8 index,
				unsigned long rules)
{
	if (index >= AQR_MAX_LEDS)
		return -EINVAL;

	/* All combinations of the supported triggers are allowed */
	if (rules & ~supported_triggers)
		return -EOPNOTSUPP;

	return 0;
}

int aqr_phy_led_hw_control_get(struct phy_device *phydev, u8 index,
			       unsigned long *rules)
{
	int val;

	if (index >= AQR_MAX_LEDS)
		return -EINVAL;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, AQR_LED_PROV(index));
	if (val < 0)
		return val;

	*rules = 0;
	if (val & VEND1_GLOBAL_LED_PROV_LINK100)
		*rules |= BIT(TRIGGER_NETDEV_LINK_100);

	if (val & VEND1_GLOBAL_LED_PROV_LINK1000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_1000);

	if (val & VEND1_GLOBAL_LED_PROV_LINK2500)
		*rules |= BIT(TRIGGER_NETDEV_LINK_2500);

	if (val & VEND1_GLOBAL_LED_PROV_LINK5000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_5000);

	if (val & VEND1_GLOBAL_LED_PROV_LINK10000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_10000);

	if (val & VEND1_GLOBAL_LED_PROV_RX_ACT)
		*rules |= BIT(TRIGGER_NETDEV_RX);

	if (val & VEND1_GLOBAL_LED_PROV_TX_ACT)
		*rules |= BIT(TRIGGER_NETDEV_TX);

	return 0;
}

int aqr_phy_led_hw_control_set(struct phy_device *phydev, u8 index,
			       unsigned long rules)
{
	u16 val = 0;

	if (index >= AQR_MAX_LEDS)
		return -EINVAL;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK)))
		val |= VEND1_GLOBAL_LED_PROV_LINK100;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_1000) | BIT(TRIGGER_NETDEV_LINK)))
		val |= VEND1_GLOBAL_LED_PROV_LINK1000;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_2500) | BIT(TRIGGER_NETDEV_LINK)))
		val |= VEND1_GLOBAL_LED_PROV_LINK2500;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_5000) | BIT(TRIGGER_NETDEV_LINK)))
		val |= VEND1_GLOBAL_LED_PROV_LINK5000;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_10000) | BIT(TRIGGER_NETDEV_LINK)))
		val |= VEND1_GLOBAL_LED_PROV_LINK10000;

	if (rules & BIT(TRIGGER_NETDEV_RX))
		val |= VEND1_GLOBAL_LED_PROV_RX_ACT;

	if (rules & BIT(TRIGGER_NETDEV_TX))
		val |= VEND1_GLOBAL_LED_PROV_TX_ACT;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, AQR_LED_PROV(index),
			      VEND1_GLOBAL_LED_PROV_LINK_MASK |
			      VEND1_GLOBAL_LED_PROV_FORCE_ON |
			      VEND1_GLOBAL_LED_PROV_RX_ACT |
			      VEND1_GLOBAL_LED_PROV_TX_ACT, val);
}

int aqr_phy_led_active_low_set(struct phy_device *phydev, int index, bool enable)
{
	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, AQR_LED_DRIVE(index),
			      VEND1_GLOBAL_LED_DRIVE_VDD,
			      enable ? 0 : VEND1_GLOBAL_LED_DRIVE_VDD);
}

int aqr_phy_led_polarity_set(struct phy_device *phydev, int index, unsigned long modes)
{
	bool force_active_low = false, force_active_high = false;
	struct aqr107_priv *priv = phydev->priv;
	u32 mode;

	if (index >= AQR_MAX_LEDS)
		return -EINVAL;

	for_each_set_bit(mode, &modes, __PHY_LED_MODES_NUM) {
		switch (mode) {
		case PHY_LED_ACTIVE_LOW:
			force_active_low = true;
			break;
		case PHY_LED_ACTIVE_HIGH:
			force_active_high = true;
			break;
		default:
			return -EINVAL;
		}
	}

	/* Save LED driver vdd state to restore on SW reset */
	if (force_active_low)
		priv->leds_active_low |= BIT(index);

	if (force_active_high)
		priv->leds_active_high |= BIT(index);

	if (force_active_high || force_active_low)
		return aqr_phy_led_active_low_set(phydev, index, force_active_low);

	unreachable();
}
