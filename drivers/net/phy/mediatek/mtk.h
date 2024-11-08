/* SPDX-License-Identifier: GPL-2.0
 *
 * Common definition for Mediatek Ethernet PHYs
 * Author: SkyLake Huang <SkyLake.Huang@mediatek.com>
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef _MTK_EPHY_H_
#define _MTK_EPHY_H_

#define MTK_EXT_PAGE_ACCESS			0x1f

/* Registers on MDIO_MMD_VEND2 */
#define MTK_PHY_LED0_ON_CTRL			0x24
#define MTK_PHY_LED1_ON_CTRL			0x26
#define   MTK_GPHY_LED_ON_MASK			GENMASK(6, 0)
#define   MTK_2P5GPHY_LED_ON_MASK		GENMASK(7, 0)
#define   MTK_PHY_LED_ON_LINK1000		BIT(0)
#define   MTK_PHY_LED_ON_LINK100		BIT(1)
#define   MTK_PHY_LED_ON_LINK10			BIT(2)
#define   MTK_PHY_LED_ON_LINKDOWN		BIT(3)
#define   MTK_PHY_LED_ON_FDX			BIT(4) /* Full duplex */
#define   MTK_PHY_LED_ON_HDX			BIT(5) /* Half duplex */
#define   MTK_PHY_LED_ON_FORCE_ON		BIT(6)
#define   MTK_PHY_LED_ON_LINK2500		BIT(7)
#define   MTK_PHY_LED_ON_POLARITY		BIT(14)
#define   MTK_PHY_LED_ON_ENABLE			BIT(15)

#define MTK_PHY_LED0_BLINK_CTRL			0x25
#define MTK_PHY_LED1_BLINK_CTRL			0x27
#define   MTK_PHY_LED_BLINK_1000TX		BIT(0)
#define   MTK_PHY_LED_BLINK_1000RX		BIT(1)
#define   MTK_PHY_LED_BLINK_100TX		BIT(2)
#define   MTK_PHY_LED_BLINK_100RX		BIT(3)
#define   MTK_PHY_LED_BLINK_10TX		BIT(4)
#define   MTK_PHY_LED_BLINK_10RX		BIT(5)
#define   MTK_PHY_LED_BLINK_COLLISION		BIT(6)
#define   MTK_PHY_LED_BLINK_RX_CRC_ERR		BIT(7)
#define   MTK_PHY_LED_BLINK_RX_IDLE_ERR		BIT(8)
#define   MTK_PHY_LED_BLINK_FORCE_BLINK		BIT(9)
#define   MTK_PHY_LED_BLINK_2500TX		BIT(10)
#define   MTK_PHY_LED_BLINK_2500RX		BIT(11)

#define MTK_GPHY_LED_ON_SET			(MTK_PHY_LED_ON_LINK1000 | \
						 MTK_PHY_LED_ON_LINK100 | \
						 MTK_PHY_LED_ON_LINK10)
#define MTK_GPHY_LED_RX_BLINK_SET		(MTK_PHY_LED_BLINK_1000RX | \
						 MTK_PHY_LED_BLINK_100RX | \
						 MTK_PHY_LED_BLINK_10RX)
#define MTK_GPHY_LED_TX_BLINK_SET		(MTK_PHY_LED_BLINK_1000RX | \
						 MTK_PHY_LED_BLINK_100RX | \
						 MTK_PHY_LED_BLINK_10RX)

#define MTK_2P5GPHY_LED_ON_SET			(MTK_PHY_LED_ON_LINK2500 | \
						 MTK_GPHY_LED_ON_SET)
#define MTK_2P5GPHY_LED_RX_BLINK_SET		(MTK_PHY_LED_BLINK_2500RX | \
						 MTK_GPHY_LED_RX_BLINK_SET)
#define MTK_2P5GPHY_LED_TX_BLINK_SET		(MTK_PHY_LED_BLINK_2500RX | \
						 MTK_GPHY_LED_TX_BLINK_SET)

#define MTK_PHY_LED_STATE_FORCE_ON	0
#define MTK_PHY_LED_STATE_FORCE_BLINK	1
#define MTK_PHY_LED_STATE_NETDEV	2

struct mtk_socphy_priv {
	unsigned long		led_state;
};

int mtk_phy_read_page(struct phy_device *phydev);
int mtk_phy_write_page(struct phy_device *phydev, int page);

int mtk_phy_led_hw_is_supported(struct phy_device *phydev, u8 index,
				unsigned long rules,
				unsigned long supported_triggers);
int mtk_phy_led_hw_ctrl_set(struct phy_device *phydev, u8 index,
			    unsigned long rules, u16 on_set,
			    u16 rx_blink_set, u16 tx_blink_set);
int mtk_phy_led_hw_ctrl_get(struct phy_device *phydev, u8 index,
			    unsigned long *rules, u16 on_set,
			    u16 rx_blink_set, u16 tx_blink_set);
int mtk_phy_led_num_dly_cfg(u8 index, unsigned long *delay_on,
			    unsigned long *delay_off, bool *blinking);
int mtk_phy_hw_led_on_set(struct phy_device *phydev, u8 index,
			  u16 led_on_mask, bool on);
int mtk_phy_hw_led_blink_set(struct phy_device *phydev, u8 index,
			     bool blinking);
void mtk_phy_leds_state_init(struct phy_device *phydev);

#endif /* _MTK_EPHY_H_ */
