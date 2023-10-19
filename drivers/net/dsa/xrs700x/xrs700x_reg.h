/* SPDX-License-Identifier: GPL-2.0 */

/* Register Base Addresses */
#define XRS_DEVICE_ID_BASE		0x0
#define XRS_GPIO_BASE			0x10000
#define XRS_PORT_OFFSET			0x10000
#define XRS_PORT_BASE(x)		(0x200000 + XRS_PORT_OFFSET * (x))
#define XRS_RTC_BASE			0x280000
#define XRS_TS_OFFSET			0x8000
#define XRS_TS_BASE(x)			(0x290000 + XRS_TS_OFFSET * (x))
#define XRS_SWITCH_CONF_BASE		0x300000

/* Device Identification Registers */
#define XRS_DEV_ID0			(XRS_DEVICE_ID_BASE + 0)
#define XRS_DEV_ID1			(XRS_DEVICE_ID_BASE + 2)
#define XRS_INT_ID0			(XRS_DEVICE_ID_BASE + 4)
#define XRS_INT_ID1			(XRS_DEVICE_ID_BASE + 6)
#define XRS_REV_ID			(XRS_DEVICE_ID_BASE + 8)

/* GPIO Registers */
#define XRS_CONFIG0			(XRS_GPIO_BASE + 0x1000)
#define XRS_INPUT_STATUS0		(XRS_GPIO_BASE + 0x1002)
#define XRS_CONFIG1			(XRS_GPIO_BASE + 0x1004)
#define XRS_INPUT_STATUS1		(XRS_GPIO_BASE + 0x1006)
#define XRS_CONFIG2			(XRS_GPIO_BASE + 0x1008)
#define XRS_INPUT_STATUS2		(XRS_GPIO_BASE + 0x100a)

/* Port Configuration Registers */
#define XRS_PORT_GEN_BASE(x)		(XRS_PORT_BASE(x) + 0x0)
#define XRS_PORT_HSR_BASE(x)		(XRS_PORT_BASE(x) + 0x2000)
#define XRS_PORT_PTP_BASE(x)		(XRS_PORT_BASE(x) + 0x4000)
#define XRS_PORT_CNT_BASE(x)		(XRS_PORT_BASE(x) + 0x6000)
#define XRS_PORT_IPO_BASE(x)		(XRS_PORT_BASE(x) + 0x8000)

/* Port Configuration Registers - General and State */
#define XRS_PORT_STATE(x)		(XRS_PORT_GEN_BASE(x) + 0x0)
#define XRS_PORT_FORWARDING		0
#define XRS_PORT_LEARNING		1
#define XRS_PORT_DISABLED		2
#define XRS_PORT_MODE_NORMAL		0
#define XRS_PORT_MODE_MANAGEMENT	1
#define XRS_PORT_SPEED_1000		0x12
#define XRS_PORT_SPEED_100		0x20
#define XRS_PORT_SPEED_10		0x30
#define XRS_PORT_VLAN(x)		(XRS_PORT_GEN_BASE(x) + 0x10)
#define XRS_PORT_VLAN0_MAPPING(x)	(XRS_PORT_GEN_BASE(x) + 0x12)
#define XRS_PORT_FWD_MASK(x)		(XRS_PORT_GEN_BASE(x) + 0x14)
#define XRS_PORT_VLAN_PRIO(x)		(XRS_PORT_GEN_BASE(x) + 0x16)

/* Port Configuration Registers - HSR/PRP */
#define XRS_HSR_CFG(x)			(XRS_PORT_HSR_BASE(x) + 0x0)
#define XRS_HSR_CFG_HSR_PRP		BIT(0)
#define XRS_HSR_CFG_HSR			0
#define XRS_HSR_CFG_PRP			BIT(8)
#define XRS_HSR_CFG_LANID_A		0
#define XRS_HSR_CFG_LANID_B		BIT(10)

/* Port Configuration Registers - PTP */
#define XRS_PTP_RX_SYNC_DELAY_NS_LO(x)	(XRS_PORT_PTP_BASE(x) + 0x2)
#define XRS_PTP_RX_SYNC_DELAY_NS_HI(x)	(XRS_PORT_PTP_BASE(x) + 0x4)
#define XRS_PTP_RX_EVENT_DELAY_NS(x)	(XRS_PORT_PTP_BASE(x) + 0xa)
#define XRS_PTP_TX_EVENT_DELAY_NS(x)	(XRS_PORT_PTP_BASE(x) + 0x12)

/* Port Configuration Registers - Counter */
#define XRS_CNT_CTRL(x)			(XRS_PORT_CNT_BASE(x) + 0x0)
#define XRS_RX_GOOD_OCTETS_L		(XRS_PORT_CNT_BASE(0) + 0x200)
#define XRS_RX_GOOD_OCTETS_H		(XRS_PORT_CNT_BASE(0) + 0x202)
#define XRS_RX_BAD_OCTETS_L		(XRS_PORT_CNT_BASE(0) + 0x204)
#define XRS_RX_BAD_OCTETS_H		(XRS_PORT_CNT_BASE(0) + 0x206)
#define XRS_RX_UNICAST_L		(XRS_PORT_CNT_BASE(0) + 0x208)
#define XRS_RX_UNICAST_H		(XRS_PORT_CNT_BASE(0) + 0x20a)
#define XRS_RX_BROADCAST_L		(XRS_PORT_CNT_BASE(0) + 0x20c)
#define XRS_RX_BROADCAST_H		(XRS_PORT_CNT_BASE(0) + 0x20e)
#define XRS_RX_MULTICAST_L		(XRS_PORT_CNT_BASE(0) + 0x210)
#define XRS_RX_MULTICAST_H		(XRS_PORT_CNT_BASE(0) + 0x212)
#define XRS_RX_UNDERSIZE_L		(XRS_PORT_CNT_BASE(0) + 0x214)
#define XRS_RX_UNDERSIZE_H		(XRS_PORT_CNT_BASE(0) + 0x216)
#define XRS_RX_FRAGMENTS_L		(XRS_PORT_CNT_BASE(0) + 0x218)
#define XRS_RX_FRAGMENTS_H		(XRS_PORT_CNT_BASE(0) + 0x21a)
#define XRS_RX_OVERSIZE_L		(XRS_PORT_CNT_BASE(0) + 0x21c)
#define XRS_RX_OVERSIZE_H		(XRS_PORT_CNT_BASE(0) + 0x21e)
#define XRS_RX_JABBER_L			(XRS_PORT_CNT_BASE(0) + 0x220)
#define XRS_RX_JABBER_H			(XRS_PORT_CNT_BASE(0) + 0x222)
#define XRS_RX_ERR_L			(XRS_PORT_CNT_BASE(0) + 0x224)
#define XRS_RX_ERR_H			(XRS_PORT_CNT_BASE(0) + 0x226)
#define XRS_RX_CRC_L			(XRS_PORT_CNT_BASE(0) + 0x228)
#define XRS_RX_CRC_H			(XRS_PORT_CNT_BASE(0) + 0x22a)
#define XRS_RX_64_L			(XRS_PORT_CNT_BASE(0) + 0x22c)
#define XRS_RX_64_H			(XRS_PORT_CNT_BASE(0) + 0x22e)
#define XRS_RX_65_127_L			(XRS_PORT_CNT_BASE(0) + 0x230)
#define XRS_RX_65_127_H			(XRS_PORT_CNT_BASE(0) + 0x232)
#define XRS_RX_128_255_L		(XRS_PORT_CNT_BASE(0) + 0x234)
#define XRS_RX_128_255_H		(XRS_PORT_CNT_BASE(0) + 0x236)
#define XRS_RX_256_511_L		(XRS_PORT_CNT_BASE(0) + 0x238)
#define XRS_RX_256_511_H		(XRS_PORT_CNT_BASE(0) + 0x23a)
#define XRS_RX_512_1023_L		(XRS_PORT_CNT_BASE(0) + 0x23c)
#define XRS_RX_512_1023_H		(XRS_PORT_CNT_BASE(0) + 0x23e)
#define XRS_RX_1024_1536_L		(XRS_PORT_CNT_BASE(0) + 0x240)
#define XRS_RX_1024_1536_H		(XRS_PORT_CNT_BASE(0) + 0x242)
#define XRS_RX_HSR_PRP_L		(XRS_PORT_CNT_BASE(0) + 0x244)
#define XRS_RX_HSR_PRP_H		(XRS_PORT_CNT_BASE(0) + 0x246)
#define XRS_RX_WRONGLAN_L		(XRS_PORT_CNT_BASE(0) + 0x248)
#define XRS_RX_WRONGLAN_H		(XRS_PORT_CNT_BASE(0) + 0x24a)
#define XRS_RX_DUPLICATE_L		(XRS_PORT_CNT_BASE(0) + 0x24c)
#define XRS_RX_DUPLICATE_H		(XRS_PORT_CNT_BASE(0) + 0x24e)
#define XRS_TX_OCTETS_L			(XRS_PORT_CNT_BASE(0) + 0x280)
#define XRS_TX_OCTETS_H			(XRS_PORT_CNT_BASE(0) + 0x282)
#define XRS_TX_UNICAST_L		(XRS_PORT_CNT_BASE(0) + 0x284)
#define XRS_TX_UNICAST_H		(XRS_PORT_CNT_BASE(0) + 0x286)
#define XRS_TX_BROADCAST_L		(XRS_PORT_CNT_BASE(0) + 0x288)
#define XRS_TX_BROADCAST_H		(XRS_PORT_CNT_BASE(0) + 0x28a)
#define XRS_TX_MULTICAST_L		(XRS_PORT_CNT_BASE(0) + 0x28c)
#define XRS_TX_MULTICAST_H		(XRS_PORT_CNT_BASE(0) + 0x28e)
#define XRS_TX_HSR_PRP_L		(XRS_PORT_CNT_BASE(0) + 0x290)
#define XRS_TX_HSR_PRP_H		(XRS_PORT_CNT_BASE(0) + 0x292)
#define XRS_PRIQ_DROP_L			(XRS_PORT_CNT_BASE(0) + 0x2c0)
#define XRS_PRIQ_DROP_H			(XRS_PORT_CNT_BASE(0) + 0x2c2)
#define XRS_EARLY_DROP_L		(XRS_PORT_CNT_BASE(0) + 0x2c4)
#define XRS_EARLY_DROP_H		(XRS_PORT_CNT_BASE(0) + 0x2c6)

/* Port Configuration Registers - Inbound Policy 0 - 15 */
#define XRS_ETH_ADDR_CFG(x, p)		(XRS_PORT_IPO_BASE(x) + \
					 (p) * 0x20 + 0x0)
#define XRS_ETH_ADDR_FWD_ALLOW(x, p)	(XRS_PORT_IPO_BASE(x) + \
					 (p) * 0x20 + 0x2)
#define XRS_ETH_ADDR_FWD_MIRROR(x, p)	(XRS_PORT_IPO_BASE(x) + \
					 (p) * 0x20 + 0x4)
#define XRS_ETH_ADDR_0(x, p)		(XRS_PORT_IPO_BASE(x) + \
					 (p) * 0x20 + 0x8)
#define XRS_ETH_ADDR_1(x, p)		(XRS_PORT_IPO_BASE(x) + \
					 (p) * 0x20 + 0xa)
#define XRS_ETH_ADDR_2(x, p)		(XRS_PORT_IPO_BASE(x) + \
					 (p) * 0x20 + 0xc)

/* RTC Registers */
#define XRS_CUR_NSEC0			(XRS_RTC_BASE + 0x1004)
#define XRS_CUR_NSEC1			(XRS_RTC_BASE + 0x1006)
#define XRS_CUR_SEC0			(XRS_RTC_BASE + 0x1008)
#define XRS_CUR_SEC1			(XRS_RTC_BASE + 0x100a)
#define XRS_CUR_SEC2			(XRS_RTC_BASE + 0x100c)
#define XRS_TIME_CC0			(XRS_RTC_BASE + 0x1010)
#define XRS_TIME_CC1			(XRS_RTC_BASE + 0x1012)
#define XRS_TIME_CC2			(XRS_RTC_BASE + 0x1014)
#define XRS_STEP_SIZE0			(XRS_RTC_BASE + 0x1020)
#define XRS_STEP_SIZE1			(XRS_RTC_BASE + 0x1022)
#define XRS_STEP_SIZE2			(XRS_RTC_BASE + 0x1024)
#define XRS_ADJUST_NSEC0		(XRS_RTC_BASE + 0x1034)
#define XRS_ADJUST_NSEC1		(XRS_RTC_BASE + 0x1036)
#define XRS_ADJUST_SEC0			(XRS_RTC_BASE + 0x1038)
#define XRS_ADJUST_SEC1			(XRS_RTC_BASE + 0x103a)
#define XRS_ADJUST_SEC2			(XRS_RTC_BASE + 0x103c)
#define XRS_TIME_CMD			(XRS_RTC_BASE + 0x1040)

/* Time Stamper Registers */
#define XRS_TS_CTRL(x)			(XRS_TS_BASE(x) + 0x1000)
#define XRS_TS_INT_MASK(x)		(XRS_TS_BASE(x) + 0x1008)
#define XRS_TS_INT_STATUS(x)		(XRS_TS_BASE(x) + 0x1010)
#define XRS_TS_NSEC0(x)			(XRS_TS_BASE(x) + 0x1104)
#define XRS_TS_NSEC1(x)			(XRS_TS_BASE(x) + 0x1106)
#define XRS_TS_SEC0(x)			(XRS_TS_BASE(x) + 0x1108)
#define XRS_TS_SEC1(x)			(XRS_TS_BASE(x) + 0x110a)
#define XRS_TS_SEC2(x)			(XRS_TS_BASE(x) + 0x110c)
#define XRS_PNCT0(x)			(XRS_TS_BASE(x) + 0x1110)
#define XRS_PNCT1(x)			(XRS_TS_BASE(x) + 0x1112)

/* Switch Configuration Registers */
#define XRS_SWITCH_GEN_BASE		(XRS_SWITCH_CONF_BASE + 0x0)
#define XRS_SWITCH_TS_BASE		(XRS_SWITCH_CONF_BASE + 0x2000)
#define XRS_SWITCH_VLAN_BASE		(XRS_SWITCH_CONF_BASE + 0x4000)

/* Switch Configuration Registers - General */
#define XRS_GENERAL			(XRS_SWITCH_GEN_BASE + 0x10)
#define XRS_GENERAL_TIME_TRAILER	BIT(9)
#define XRS_GENERAL_MOD_SYNC		BIT(10)
#define XRS_GENERAL_CUT_THRU		BIT(13)
#define XRS_GENERAL_CLR_MAC_TBL		BIT(14)
#define XRS_GENERAL_RESET		BIT(15)
#define XRS_MT_CLEAR_MASK		(XRS_SWITCH_GEN_BASE + 0x12)
#define XRS_ADDRESS_AGING		(XRS_SWITCH_GEN_BASE + 0x20)
#define XRS_TS_CTRL_TX			(XRS_SWITCH_GEN_BASE + 0x28)
#define XRS_TS_CTRL_RX			(XRS_SWITCH_GEN_BASE + 0x2a)
#define XRS_INT_MASK			(XRS_SWITCH_GEN_BASE + 0x2c)
#define XRS_INT_STATUS			(XRS_SWITCH_GEN_BASE + 0x2e)
#define XRS_MAC_TABLE0			(XRS_SWITCH_GEN_BASE + 0x200)
#define XRS_MAC_TABLE1			(XRS_SWITCH_GEN_BASE + 0x202)
#define XRS_MAC_TABLE2			(XRS_SWITCH_GEN_BASE + 0x204)
#define XRS_MAC_TABLE3			(XRS_SWITCH_GEN_BASE + 0x206)

/* Switch Configuration Registers - Frame Timestamp */
#define XRS_TX_TS_NS_LO(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + 0x0)
#define XRS_TX_TS_NS_HI(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + 0x2)
#define XRS_TX_TS_S_LO(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + 0x4)
#define XRS_TX_TS_S_HI(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + 0x6)
#define XRS_TX_TS_HDR(t, h)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + \
					 0x2 * (h) + 0xe)
#define XRS_RX_TS_NS_LO(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + \
					 0x200)
#define XRS_RX_TS_NS_HI(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + \
					 0x202)
#define XRS_RX_TS_S_LO(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + \
					 0x204)
#define XRS_RX_TS_S_HI(t)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + \
					 0x206)
#define XRS_RX_TS_HDR(t, h)		(XRS_SWITCH_TS_BASE + 0x80 * (t) + \
					 0x2 * (h) + 0xe)

/* Switch Configuration Registers - VLAN */
#define XRS_VLAN(v)			(XRS_SWITCH_VLAN_BASE + 0x2 * (v))
