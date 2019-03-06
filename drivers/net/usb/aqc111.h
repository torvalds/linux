/* SPDX-License-Identifier: GPL-2.0-or-later
 * Aquantia Corp. Aquantia AQtion USB to 5GbE Controller
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 * Copyright (C) 2005 Phil Chang <pchang23@sbcglobal.net>
 * Copyright (C) 2002-2003 TiVo Inc.
 * Copyright (C) 2017-2018 ASIX
 * Copyright (C) 2018 Aquantia Corp.
 */

#ifndef __LINUX_USBNET_AQC111_H
#define __LINUX_USBNET_AQC111_H

#define URB_SIZE	(1024 * 62)

#define AQ_MCAST_FILTER_SIZE		8
#define AQ_MAX_MCAST			64

#define AQ_ACCESS_MAC			0x01
#define AQ_FLASH_PARAMETERS		0x20
#define AQ_PHY_POWER			0x31
#define AQ_WOL_CFG			0x60
#define AQ_PHY_OPS			0x61

#define AQ_USB_PHY_SET_TIMEOUT		10000
#define AQ_USB_SET_TIMEOUT		4000

/* Feature. ********************************************/
#define AQ_SUPPORT_FEATURE	(NETIF_F_SG | NETIF_F_IP_CSUM |\
				 NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM |\
				 NETIF_F_TSO | NETIF_F_HW_VLAN_CTAG_TX |\
				 NETIF_F_HW_VLAN_CTAG_RX)

#define AQ_SUPPORT_HW_FEATURE	(NETIF_F_SG | NETIF_F_IP_CSUM |\
				 NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM |\
				 NETIF_F_TSO | NETIF_F_HW_VLAN_CTAG_FILTER)

#define AQ_SUPPORT_VLAN_FEATURE (NETIF_F_SG | NETIF_F_IP_CSUM |\
				 NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM |\
				 NETIF_F_TSO)

/* SFR Reg. ********************************************/

#define SFR_GENERAL_STATUS		0x03
#define SFR_CHIP_STATUS			0x05
#define SFR_RX_CTL			0x0B
	#define SFR_RX_CTL_TXPADCRC		0x0400
	#define SFR_RX_CTL_IPE			0x0200
	#define SFR_RX_CTL_DROPCRCERR		0x0100
	#define SFR_RX_CTL_START		0x0080
	#define SFR_RX_CTL_RF_WAK		0x0040
	#define SFR_RX_CTL_AP			0x0020
	#define SFR_RX_CTL_AM			0x0010
	#define SFR_RX_CTL_AB			0x0008
	#define SFR_RX_CTL_AMALL		0x0002
	#define SFR_RX_CTL_PRO			0x0001
	#define SFR_RX_CTL_STOP			0x0000
#define SFR_INTER_PACKET_GAP_0		0x0D
#define SFR_NODE_ID			0x10
#define SFR_MULTI_FILTER_ARRY		0x16
#define SFR_MEDIUM_STATUS_MODE		0x22
	#define SFR_MEDIUM_XGMIIMODE		0x0001
	#define SFR_MEDIUM_FULL_DUPLEX		0x0002
	#define SFR_MEDIUM_RXFLOW_CTRLEN	0x0010
	#define SFR_MEDIUM_TXFLOW_CTRLEN	0x0020
	#define SFR_MEDIUM_JUMBO_EN		0x0040
	#define SFR_MEDIUM_RECEIVE_EN		0x0100
#define SFR_MONITOR_MODE		0x24
	#define SFR_MONITOR_MODE_EPHYRW		0x01
	#define SFR_MONITOR_MODE_RWLC		0x02
	#define SFR_MONITOR_MODE_RWMP		0x04
	#define SFR_MONITOR_MODE_RWWF		0x08
	#define SFR_MONITOR_MODE_RW_FLAG	0x10
	#define SFR_MONITOR_MODE_PMEPOL		0x20
	#define SFR_MONITOR_MODE_PMETYPE	0x40
#define SFR_PHYPWR_RSTCTL		0x26
	#define SFR_PHYPWR_RSTCTL_BZ		0x0010
	#define SFR_PHYPWR_RSTCTL_IPRL		0x0020
#define SFR_VLAN_ID_ADDRESS		0x2A
#define SFR_VLAN_ID_CONTROL		0x2B
	#define SFR_VLAN_CONTROL_WE		0x0001
	#define SFR_VLAN_CONTROL_RD		0x0002
	#define SFR_VLAN_CONTROL_VSO		0x0010
	#define SFR_VLAN_CONTROL_VFE		0x0020
#define SFR_VLAN_ID_DATA0		0x2C
#define SFR_VLAN_ID_DATA1		0x2D
#define SFR_RX_BULKIN_QCTRL		0x2E
	#define SFR_RX_BULKIN_QCTRL_TIME	0x01
	#define SFR_RX_BULKIN_QCTRL_IFG		0x02
	#define SFR_RX_BULKIN_QCTRL_SIZE	0x04
#define SFR_RX_BULKIN_QTIMR_LOW		0x2F
#define SFR_RX_BULKIN_QTIMR_HIGH	0x30
#define SFR_RX_BULKIN_QSIZE		0x31
#define SFR_RX_BULKIN_QIFG		0x32
#define SFR_RXCOE_CTL			0x34
	#define SFR_RXCOE_IP			0x01
	#define SFR_RXCOE_TCP			0x02
	#define SFR_RXCOE_UDP			0x04
	#define SFR_RXCOE_ICMP			0x08
	#define SFR_RXCOE_IGMP			0x10
	#define SFR_RXCOE_TCPV6			0x20
	#define SFR_RXCOE_UDPV6			0x40
	#define SFR_RXCOE_ICMV6			0x80
#define SFR_TXCOE_CTL			0x35
	#define SFR_TXCOE_IP			0x01
	#define SFR_TXCOE_TCP			0x02
	#define SFR_TXCOE_UDP			0x04
	#define SFR_TXCOE_ICMP			0x08
	#define SFR_TXCOE_IGMP			0x10
	#define SFR_TXCOE_TCPV6			0x20
	#define SFR_TXCOE_UDPV6			0x40
	#define SFR_TXCOE_ICMV6			0x80
#define SFR_BM_INT_MASK			0x41
#define SFR_BMRX_DMA_CONTROL		0x43
	#define SFR_BMRX_DMA_EN			0x80
#define SFR_BMTX_DMA_CONTROL		0x46
#define SFR_PAUSE_WATERLVL_LOW		0x54
#define SFR_PAUSE_WATERLVL_HIGH		0x55
#define SFR_ARC_CTRL			0x9E
#define SFR_SWP_CTRL			0xB1
#define SFR_TX_PAUSE_RESEND_T		0xB2
#define SFR_ETH_MAC_PATH		0xB7
	#define SFR_RX_PATH_READY		0x01
#define SFR_BULK_OUT_CTRL		0xB9
	#define SFR_BULK_OUT_FLUSH_EN		0x01
	#define SFR_BULK_OUT_EFF_EN		0x02

#define AQ_FW_VER_MAJOR			0xDA
#define AQ_FW_VER_MINOR			0xDB
#define AQ_FW_VER_REV			0xDC

/*PHY_OPS**********************************************************************/

#define AQ_ADV_100M	BIT(0)
#define AQ_ADV_1G	BIT(1)
#define AQ_ADV_2G5	BIT(2)
#define AQ_ADV_5G	BIT(3)
#define AQ_ADV_MASK	0x0F

#define AQ_PAUSE	BIT(16)
#define AQ_ASYM_PAUSE	BIT(17)
#define AQ_LOW_POWER	BIT(18)
#define AQ_PHY_POWER_EN	BIT(19)
#define AQ_WOL		BIT(20)
#define AQ_DOWNSHIFT	BIT(21)

#define AQ_DSH_RETRIES_SHIFT	0x18
#define AQ_DSH_RETRIES_MASK	0xF000000

#define AQ_WOL_FLAG_MP			0x2

/******************************************************************************/

struct aqc111_wol_cfg {
	u8 hw_addr[6];
	u8 flags;
	u8 rsvd[283];
} __packed;

#define WOL_CFG_SIZE sizeof(struct aqc111_wol_cfg)

struct aqc111_data {
	u16 rxctl;
	u8 rx_checksum;
	u8 link_speed;
	u8 link;
	u8 autoneg;
	u32 advertised_speed;
	struct {
		u8 major;
		u8 minor;
		u8 rev;
	} fw_ver;
	u32 phy_cfg;
	u8 wol_flags;
};

#define AQ_LS_MASK		0x8000
#define AQ_SPEED_MASK		0x7F00
#define AQ_SPEED_SHIFT		0x0008
#define AQ_INT_SPEED_5G		0x000F
#define AQ_INT_SPEED_2_5G	0x0010
#define AQ_INT_SPEED_1G		0x0011
#define AQ_INT_SPEED_100M	0x0013

/* TX Descriptor */
#define AQ_TX_DESC_LEN_MASK	0x1FFFFF
#define AQ_TX_DESC_DROP_PADD	BIT(28)
#define AQ_TX_DESC_VLAN		BIT(29)
#define AQ_TX_DESC_MSS_MASK	0x7FFF
#define AQ_TX_DESC_MSS_SHIFT	0x20
#define AQ_TX_DESC_VLAN_MASK	0xFFFF
#define AQ_TX_DESC_VLAN_SHIFT	0x30

#define AQ_RX_HW_PAD			0x02

/* RX Packet Descriptor */
#define AQ_RX_PD_L4_ERR		BIT(0)
#define AQ_RX_PD_L3_ERR		BIT(1)
#define AQ_RX_PD_L4_TYPE_MASK	0x1C
#define AQ_RX_PD_L4_UDP		0x04
#define AQ_RX_PD_L4_TCP		0x10
#define AQ_RX_PD_L3_TYPE_MASK	0x60
#define AQ_RX_PD_L3_IP		0x20
#define AQ_RX_PD_L3_IP6		0x40

#define AQ_RX_PD_VLAN		BIT(10)
#define AQ_RX_PD_RX_OK		BIT(11)
#define AQ_RX_PD_DROP		BIT(31)
#define AQ_RX_PD_LEN_MASK	0x7FFF0000
#define AQ_RX_PD_LEN_SHIFT	0x10
#define AQ_RX_PD_VLAN_SHIFT	0x20

/* RX Descriptor header */
#define AQ_RX_DH_PKT_CNT_MASK		0x1FFF
#define AQ_RX_DH_DESC_OFFSET_MASK	0xFFFFE000
#define AQ_RX_DH_DESC_OFFSET_SHIFT	0x0D

static struct {
	unsigned char ctrl;
	unsigned char timer_l;
	unsigned char timer_h;
	unsigned char size;
	unsigned char ifg;
} AQC111_BULKIN_SIZE[] = {
	/* xHCI & EHCI & OHCI */
	{7, 0x00, 0x01, 0x1E, 0xFF},/* 10G, 5G, 2.5G, 1G */
	{7, 0xA0, 0x00, 0x14, 0x00},/* 100M */
	/* Jumbo packet */
	{7, 0x00, 0x01, 0x18, 0xFF},
};

#endif /* __LINUX_USBNET_AQC111_H */
