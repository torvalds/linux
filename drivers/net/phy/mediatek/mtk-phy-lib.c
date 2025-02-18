// SPDX-License-Identifier: GPL-2.0
#include <linux/phy.h>
#include <linux/module.h>

#include <linux/netdevice.h>

#include "mtk.h"

/* Difference between functions with mtk_tr* and __mtk_tr* prefixes is
 * mtk_tr* functions: wrapped by page switching operations
 * __mtk_tr* functions: no page switching operations
 */

static void __mtk_tr_access(struct phy_device *phydev, bool read, u8 ch_addr,
			    u8 node_addr, u8 data_addr)
{
	u16 tr_cmd = BIT(15); /* bit 14 & 0 are reserved */

	if (read)
		tr_cmd |= BIT(13);

	tr_cmd |= (((ch_addr & 0x3) << 11) |
		   ((node_addr & 0xf) << 7) |
		   ((data_addr & 0x3f) << 1));
	dev_dbg(&phydev->mdio.dev, "tr_cmd: 0x%x\n", tr_cmd);
	__phy_write(phydev, 0x10, tr_cmd);
}

static void __mtk_tr_read(struct phy_device *phydev, u8 ch_addr, u8 node_addr,
			  u8 data_addr, u16 *tr_high, u16 *tr_low)
{
	__mtk_tr_access(phydev, true, ch_addr, node_addr, data_addr);
	*tr_low = __phy_read(phydev, 0x11);
	*tr_high = __phy_read(phydev, 0x12);
	dev_dbg(&phydev->mdio.dev, "tr_high read: 0x%x, tr_low read: 0x%x\n",
		*tr_high, *tr_low);
}

static void __mtk_tr_write(struct phy_device *phydev, u8 ch_addr, u8 node_addr,
			   u8 data_addr, u32 tr_data)
{
	__phy_write(phydev, 0x11, tr_data & 0xffff);
	__phy_write(phydev, 0x12, tr_data >> 16);
	dev_dbg(&phydev->mdio.dev, "tr_high write: 0x%x, tr_low write: 0x%x\n",
		tr_data >> 16, tr_data & 0xffff);
	__mtk_tr_access(phydev, false, ch_addr, node_addr, data_addr);
}

void __mtk_tr_modify(struct phy_device *phydev, u8 ch_addr, u8 node_addr,
		     u8 data_addr, u32 mask, u32 set)
{
	u32 tr_data;
	u16 tr_high;
	u16 tr_low;

	__mtk_tr_read(phydev, ch_addr, node_addr, data_addr, &tr_high, &tr_low);
	tr_data = (tr_high << 16) | tr_low;
	tr_data = (tr_data & ~mask) | set;
	__mtk_tr_write(phydev, ch_addr, node_addr, data_addr, tr_data);
}
EXPORT_SYMBOL_GPL(__mtk_tr_modify);

void mtk_tr_modify(struct phy_device *phydev, u8 ch_addr, u8 node_addr,
		   u8 data_addr, u32 mask, u32 set)
{
	phy_select_page(phydev, MTK_PHY_PAGE_EXTENDED_52B5);
	__mtk_tr_modify(phydev, ch_addr, node_addr, data_addr, mask, set);
	phy_restore_page(phydev, MTK_PHY_PAGE_STANDARD, 0);
}
EXPORT_SYMBOL_GPL(mtk_tr_modify);

void __mtk_tr_set_bits(struct phy_device *phydev, u8 ch_addr, u8 node_addr,
		       u8 data_addr, u32 set)
{
	__mtk_tr_modify(phydev, ch_addr, node_addr, data_addr, 0, set);
}
EXPORT_SYMBOL_GPL(__mtk_tr_set_bits);

void __mtk_tr_clr_bits(struct phy_device *phydev, u8 ch_addr, u8 node_addr,
		       u8 data_addr, u32 clr)
{
	__mtk_tr_modify(phydev, ch_addr, node_addr, data_addr, clr, 0);
}
EXPORT_SYMBOL_GPL(__mtk_tr_clr_bits);

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
