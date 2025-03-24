// SPDX-License-Identifier: GPL-2.0
#include <linux/phy.h>
#include <linux/module.h>

#include <linux/netdevice.h>

#include "mtk.h"

int mtk_phy_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, MTK_EXT_PAGE_ACCESS);
}
EXPORT_SYMBOL_GPL(mtk_phy_read_page);

int mtk_phy_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, MTK_EXT_PAGE_ACCESS, page);
}
EXPORT_SYMBOL_GPL(mtk_phy_write_page);

int mtk_phy_led_hw_is_supported(struct phy_device *phydev, u8 index,
				unsigned long rules,
				unsigned long supported_triggers)
{
	if (index > 1)
		return -EINVAL;

	/* All combinations of the supported triggers are allowed */
	if (rules & ~supported_triggers)
		return -EOPNOTSUPP;

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_phy_led_hw_is_supported);

int mtk_phy_led_hw_ctrl_get(struct phy_device *phydev, u8 index,
			    unsigned long *rules, u16 on_set,
			    u16 rx_blink_set, u16 tx_blink_set)
{
	unsigned int bit_blink = MTK_PHY_LED_STATE_FORCE_BLINK +
				 (index ? 16 : 0);
	unsigned int bit_netdev = MTK_PHY_LED_STATE_NETDEV + (index ? 16 : 0);
	unsigned int bit_on = MTK_PHY_LED_STATE_FORCE_ON + (index ? 16 : 0);
	struct mtk_socphy_priv *priv = phydev->priv;
	int on, blink;

	if (index > 1)
		return -EINVAL;

	on = phy_read_mmd(phydev, MDIO_MMD_VEND2,
			  index ? MTK_PHY_LED1_ON_CTRL : MTK_PHY_LED0_ON_CTRL);

	if (on < 0)
		return -EIO;

	blink = phy_read_mmd(phydev, MDIO_MMD_VEND2,
			     index ? MTK_PHY_LED1_BLINK_CTRL :
				     MTK_PHY_LED0_BLINK_CTRL);
	if (blink < 0)
		return -EIO;

	if ((on & (on_set | MTK_PHY_LED_ON_FDX |
		   MTK_PHY_LED_ON_HDX | MTK_PHY_LED_ON_LINKDOWN)) ||
	    (blink & (rx_blink_set | tx_blink_set)))
		set_bit(bit_netdev, &priv->led_state);
	else
		clear_bit(bit_netdev, &priv->led_state);

	if (on & MTK_PHY_LED_ON_FORCE_ON)
		set_bit(bit_on, &priv->led_state);
	else
		clear_bit(bit_on, &priv->led_state);

	if (blink & MTK_PHY_LED_BLINK_FORCE_BLINK)
		set_bit(bit_blink, &priv->led_state);
	else
		clear_bit(bit_blink, &priv->led_state);

	if (!rules)
		return 0;

	if (on & on_set)
		*rules |= BIT(TRIGGER_NETDEV_LINK);

	if (on & MTK_PHY_LED_ON_LINK10)
		*rules |= BIT(TRIGGER_NETDEV_LINK_10);

	if (on & MTK_PHY_LED_ON_LINK100)
		*rules |= BIT(TRIGGER_NETDEV_LINK_100);

	if (on & MTK_PHY_LED_ON_LINK1000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_1000);

	if (on & MTK_PHY_LED_ON_LINK2500)
		*rules |= BIT(TRIGGER_NETDEV_LINK_2500);

	if (on & MTK_PHY_LED_ON_FDX)
		*rules |= BIT(TRIGGER_NETDEV_FULL_DUPLEX);

	if (on & MTK_PHY_LED_ON_HDX)
		*rules |= BIT(TRIGGER_NETDEV_HALF_DUPLEX);

	if (blink & rx_blink_set)
		*rules |= BIT(TRIGGER_NETDEV_RX);

	if (blink & tx_blink_set)
		*rules |= BIT(TRIGGER_NETDEV_TX);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_phy_led_hw_ctrl_get);

int mtk_phy_led_hw_ctrl_set(struct phy_device *phydev, u8 index,
			    unsigned long rules, u16 on_set,
			    u16 rx_blink_set, u16 tx_blink_set)
{
	unsigned int bit_netdev = MTK_PHY_LED_STATE_NETDEV + (index ? 16 : 0);
	struct mtk_socphy_priv *priv = phydev->priv;
	u16 on = 0, blink = 0;
	int ret;

	if (index > 1)
		return -EINVAL;

	if (rules & BIT(TRIGGER_NETDEV_FULL_DUPLEX))
		on |= MTK_PHY_LED_ON_FDX;

	if (rules & BIT(TRIGGER_NETDEV_HALF_DUPLEX))
		on |= MTK_PHY_LED_ON_HDX;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_10) | BIT(TRIGGER_NETDEV_LINK)))
		on |= MTK_PHY_LED_ON_LINK10;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK)))
		on |= MTK_PHY_LED_ON_LINK100;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_1000) | BIT(TRIGGER_NETDEV_LINK)))
		on |= MTK_PHY_LED_ON_LINK1000;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_2500) | BIT(TRIGGER_NETDEV_LINK)))
		on |= MTK_PHY_LED_ON_LINK2500;

	if (rules & BIT(TRIGGER_NETDEV_RX)) {
		if (on & on_set) {
			if (on & MTK_PHY_LED_ON_LINK10)
				blink |= MTK_PHY_LED_BLINK_10RX;
			if (on & MTK_PHY_LED_ON_LINK100)
				blink |= MTK_PHY_LED_BLINK_100RX;
			if (on & MTK_PHY_LED_ON_LINK1000)
				blink |= MTK_PHY_LED_BLINK_1000RX;
			if (on & MTK_PHY_LED_ON_LINK2500)
				blink |= MTK_PHY_LED_BLINK_2500RX;
		} else {
			blink |= rx_blink_set;
		}
	}

	if (rules & BIT(TRIGGER_NETDEV_TX)) {
		if (on & on_set) {
			if (on & MTK_PHY_LED_ON_LINK10)
				blink |= MTK_PHY_LED_BLINK_10TX;
			if (on & MTK_PHY_LED_ON_LINK100)
				blink |= MTK_PHY_LED_BLINK_100TX;
			if (on & MTK_PHY_LED_ON_LINK1000)
				blink |= MTK_PHY_LED_BLINK_1000TX;
			if (on & MTK_PHY_LED_ON_LINK2500)
				blink |= MTK_PHY_LED_BLINK_2500TX;
		} else {
			blink |= tx_blink_set;
		}
	}

	if (blink || on)
		set_bit(bit_netdev, &priv->led_state);
	else
		clear_bit(bit_netdev, &priv->led_state);

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, index ?
			     MTK_PHY_LED1_ON_CTRL : MTK_PHY_LED0_ON_CTRL,
			     MTK_PHY_LED_ON_FDX | MTK_PHY_LED_ON_HDX | on_set,
			     on);

	if (ret)
		return ret;

	return phy_write_mmd(phydev, MDIO_MMD_VEND2, index ?
			     MTK_PHY_LED1_BLINK_CTRL :
			     MTK_PHY_LED0_BLINK_CTRL, blink);
}
EXPORT_SYMBOL_GPL(mtk_phy_led_hw_ctrl_set);

int mtk_phy_led_num_dly_cfg(u8 index, unsigned long *delay_on,
			    unsigned long *delay_off, bool *blinking)
{
	if (index > 1)
		return -EINVAL;

	if (delay_on && delay_off && (*delay_on > 0) && (*delay_off > 0)) {
		*blinking = true;
		*delay_on = 50;
		*delay_off = 50;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_phy_led_num_dly_cfg);

int mtk_phy_hw_led_on_set(struct phy_device *phydev, u8 index,
			  u16 led_on_mask, bool on)
{
	unsigned int bit_on = MTK_PHY_LED_STATE_FORCE_ON + (index ? 16 : 0);
	struct mtk_socphy_priv *priv = phydev->priv;
	bool changed;

	if (on)
		changed = !test_and_set_bit(bit_on, &priv->led_state);
	else
		changed = !!test_and_clear_bit(bit_on, &priv->led_state);

	changed |= !!test_and_clear_bit(MTK_PHY_LED_STATE_NETDEV +
					(index ? 16 : 0), &priv->led_state);
	if (changed)
		return phy_modify_mmd(phydev, MDIO_MMD_VEND2, index ?
				      MTK_PHY_LED1_ON_CTRL :
				      MTK_PHY_LED0_ON_CTRL,
				      led_on_mask,
				      on ? MTK_PHY_LED_ON_FORCE_ON : 0);
	else
		return 0;
}
EXPORT_SYMBOL_GPL(mtk_phy_hw_led_on_set);

int mtk_phy_hw_led_blink_set(struct phy_device *phydev, u8 index, bool blinking)
{
	unsigned int bit_blink = MTK_PHY_LED_STATE_FORCE_BLINK +
				 (index ? 16 : 0);
	struct mtk_socphy_priv *priv = phydev->priv;
	bool changed;

	if (blinking)
		changed = !test_and_set_bit(bit_blink, &priv->led_state);
	else
		changed = !!test_and_clear_bit(bit_blink, &priv->led_state);

	changed |= !!test_bit(MTK_PHY_LED_STATE_NETDEV +
			      (index ? 16 : 0), &priv->led_state);
	if (changed)
		return phy_write_mmd(phydev, MDIO_MMD_VEND2, index ?
				     MTK_PHY_LED1_BLINK_CTRL :
				     MTK_PHY_LED0_BLINK_CTRL,
				     blinking ?
				     MTK_PHY_LED_BLINK_FORCE_BLINK : 0);
	else
		return 0;
}
EXPORT_SYMBOL_GPL(mtk_phy_hw_led_blink_set);

void mtk_phy_leds_state_init(struct phy_device *phydev)
{
	int i;

	for (i = 0; i < 2; ++i)
		phydev->drv->led_hw_control_get(phydev, i, NULL);
}
EXPORT_SYMBOL_GPL(mtk_phy_leds_state_init);

MODULE_DESCRIPTION("MediaTek Ethernet PHY driver common");
MODULE_AUTHOR("Sky Huang <SkyLake.Huang@mediatek.com>");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_LICENSE("GPL");
