/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SPL2SW_REGISTER_H__
#define __SPL2SW_REGISTER_H__

/* Register L2SW */
#define L2SW_SW_INT_STATUS_0		0x0
#define L2SW_SW_INT_MASK_0		0x4
#define L2SW_FL_CNTL_TH			0x8
#define L2SW_CPU_FL_CNTL_TH		0xc
#define L2SW_PRI_FL_CNTL		0x10
#define L2SW_VLAN_PRI_TH		0x14
#define L2SW_EN_TOS_BUS			0x18
#define L2SW_TOS_MAP0			0x1c
#define L2SW_TOS_MAP1			0x20
#define L2SW_TOS_MAP2			0x24
#define L2SW_TOS_MAP3			0x28
#define L2SW_TOS_MAP4			0x2c
#define L2SW_TOS_MAP5			0x30
#define L2SW_TOS_MAP6			0x34
#define L2SW_TOS_MAP7			0x38
#define L2SW_GLOBAL_QUE_STATUS		0x3c
#define L2SW_ADDR_TBL_SRCH		0x40
#define L2SW_ADDR_TBL_ST		0x44
#define L2SW_MAC_AD_SER0		0x48
#define L2SW_MAC_AD_SER1		0x4c
#define L2SW_WT_MAC_AD0			0x50
#define L2SW_W_MAC_15_0			0x54
#define L2SW_W_MAC_47_16		0x58
#define L2SW_PVID_CONFIG0		0x5c
#define L2SW_PVID_CONFIG1		0x60
#define L2SW_VLAN_MEMSET_CONFIG0	0x64
#define L2SW_VLAN_MEMSET_CONFIG1	0x68
#define L2SW_PORT_ABILITY		0x6c
#define L2SW_PORT_ST			0x70
#define L2SW_CPU_CNTL			0x74
#define L2SW_PORT_CNTL0			0x78
#define L2SW_PORT_CNTL1			0x7c
#define L2SW_PORT_CNTL2			0x80
#define L2SW_SW_GLB_CNTL		0x84
#define L2SW_L2SW_SW_RESET		0x88
#define L2SW_LED_PORT0			0x8c
#define L2SW_LED_PORT1			0x90
#define L2SW_LED_PORT2			0x94
#define L2SW_LED_PORT3			0x98
#define L2SW_LED_PORT4			0x9c
#define L2SW_WATCH_DOG_TRIG_RST		0xa0
#define L2SW_WATCH_DOG_STOP_CPU		0xa4
#define L2SW_PHY_CNTL_REG0		0xa8
#define L2SW_PHY_CNTL_REG1		0xac
#define L2SW_MAC_FORCE_MODE		0xb0
#define L2SW_VLAN_GROUP_CONFIG0		0xb4
#define L2SW_VLAN_GROUP_CONFIG1		0xb8
#define L2SW_FLOW_CTRL_TH3		0xbc
#define L2SW_QUEUE_STATUS_0		0xc0
#define L2SW_DEBUG_CNTL			0xc4
#define L2SW_RESERVED_1			0xc8
#define L2SW_MEM_TEST_INFO		0xcc
#define L2SW_SW_INT_STATUS_1		0xd0
#define L2SW_SW_INT_MASK_1		0xd4
#define L2SW_SW_GLOBAL_SIGNAL		0xd8

#define L2SW_CPU_TX_TRIG		0x208
#define L2SW_TX_HBASE_ADDR_0		0x20c
#define L2SW_TX_LBASE_ADDR_0		0x210
#define L2SW_RX_HBASE_ADDR_0		0x214
#define L2SW_RX_LBASE_ADDR_0		0x218
#define L2SW_TX_HW_ADDR_0		0x21c
#define L2SW_TX_LW_ADDR_0		0x220
#define L2SW_RX_HW_ADDR_0		0x224
#define L2SW_RX_LW_ADDR_0		0x228
#define L2SW_CPU_PORT_CNTL_REG_0	0x22c
#define L2SW_TX_HBASE_ADDR_1		0x230
#define L2SW_TX_LBASE_ADDR_1		0x234
#define L2SW_RX_HBASE_ADDR_1		0x238
#define L2SW_RX_LBASE_ADDR_1		0x23c
#define L2SW_TX_HW_ADDR_1		0x240
#define L2SW_TX_LW_ADDR_1		0x244
#define L2SW_RX_HW_ADDR_1		0x248
#define L2SW_RX_LW_ADDR_1		0x24c
#define L2SW_CPU_PORT_CNTL_REG_1	0x250

#endif
