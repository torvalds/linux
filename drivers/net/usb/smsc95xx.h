 /***************************************************************************
 *
 * Copyright (C) 2007-2008 SMSC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *****************************************************************************/

#ifndef _SMSC95XX_H
#define _SMSC95XX_H

/* Tx command words */
#define TX_CMD_A_DATA_OFFSET_		(0x001F0000)
#define TX_CMD_A_FIRST_SEG_		(0x00002000)
#define TX_CMD_A_LAST_SEG_		(0x00001000)
#define TX_CMD_A_BUF_SIZE_		(0x000007FF)

#define TX_CMD_B_CSUM_ENABLE		(0x00004000)
#define TX_CMD_B_ADD_CRC_DISABLE_	(0x00002000)
#define TX_CMD_B_DISABLE_PADDING_	(0x00001000)
#define TX_CMD_B_PKT_BYTE_LENGTH_	(0x000007FF)

/* Rx status word */
#define RX_STS_FF_			(0x40000000)	/* Filter Fail */
#define RX_STS_FL_			(0x3FFF0000)	/* Frame Length */
#define RX_STS_ES_			(0x00008000)	/* Error Summary */
#define RX_STS_BF_			(0x00002000)	/* Broadcast Frame */
#define RX_STS_LE_			(0x00001000)	/* Length Error */
#define RX_STS_RF_			(0x00000800)	/* Runt Frame */
#define RX_STS_MF_			(0x00000400)	/* Multicast Frame */
#define RX_STS_TL_			(0x00000080)	/* Frame too long */
#define RX_STS_CS_			(0x00000040)	/* Collision Seen */
#define RX_STS_FT_			(0x00000020)	/* Frame Type */
#define RX_STS_RW_			(0x00000010)	/* Receive Watchdog */
#define RX_STS_ME_			(0x00000008)	/* Mii Error */
#define RX_STS_DB_			(0x00000004)	/* Dribbling */
#define RX_STS_CRC_			(0x00000002)	/* CRC Error */

/* SCSRs */
#define ID_REV				(0x00)
#define ID_REV_CHIP_ID_MASK_		(0xFFFF0000)
#define ID_REV_CHIP_REV_MASK_		(0x0000FFFF)
#define ID_REV_CHIP_ID_9500_		(0x9500)
#define ID_REV_CHIP_ID_9500A_		(0x9E00)
#define ID_REV_CHIP_ID_9512_		(0xEC00)
#define ID_REV_CHIP_ID_9530_		(0x9530)
#define ID_REV_CHIP_ID_89530_		(0x9E08)
#define ID_REV_CHIP_ID_9730_		(0x9730)

#define INT_STS				(0x08)
#define INT_STS_TX_STOP_		(0x00020000)
#define INT_STS_RX_STOP_		(0x00010000)
#define INT_STS_PHY_INT_		(0x00008000)
#define INT_STS_TXE_			(0x00004000)
#define INT_STS_TDFU_			(0x00002000)
#define INT_STS_TDFO_			(0x00001000)
#define INT_STS_RXDF_			(0x00000800)
#define INT_STS_GPIOS_			(0x000007FF)
#define INT_STS_CLEAR_ALL_		(0xFFFFFFFF)

#define RX_CFG				(0x0C)
#define RX_FIFO_FLUSH_			(0x00000001)

#define TX_CFG				(0x10)
#define TX_CFG_ON_			(0x00000004)
#define TX_CFG_STOP_			(0x00000002)
#define TX_CFG_FIFO_FLUSH_		(0x00000001)

#define HW_CFG				(0x14)
#define HW_CFG_BIR_			(0x00001000)
#define HW_CFG_LEDB_			(0x00000800)
#define HW_CFG_RXDOFF_			(0x00000600)
#define HW_CFG_DRP_			(0x00000040)
#define HW_CFG_MEF_			(0x00000020)
#define HW_CFG_LRST_			(0x00000008)
#define HW_CFG_PSEL_			(0x00000004)
#define HW_CFG_BCE_			(0x00000002)
#define HW_CFG_SRST_			(0x00000001)

#define RX_FIFO_INF			(0x18)

#define PM_CTRL				(0x20)
#define PM_CTL_RES_CLR_WKP_STS		(0x00000200)
#define PM_CTL_DEV_RDY_			(0x00000080)
#define PM_CTL_SUS_MODE_		(0x00000060)
#define PM_CTL_SUS_MODE_0		(0x00000000)
#define PM_CTL_SUS_MODE_1		(0x00000020)
#define PM_CTL_SUS_MODE_2		(0x00000040)
#define PM_CTL_SUS_MODE_3		(0x00000060)
#define PM_CTL_PHY_RST_			(0x00000010)
#define PM_CTL_WOL_EN_			(0x00000008)
#define PM_CTL_ED_EN_			(0x00000004)
#define PM_CTL_WUPS_			(0x00000003)
#define PM_CTL_WUPS_NO_			(0x00000000)
#define PM_CTL_WUPS_ED_			(0x00000001)
#define PM_CTL_WUPS_WOL_		(0x00000002)
#define PM_CTL_WUPS_MULTI_		(0x00000003)

#define LED_GPIO_CFG			(0x24)
#define LED_GPIO_CFG_SPD_LED		(0x01000000)
#define LED_GPIO_CFG_LNK_LED		(0x00100000)
#define LED_GPIO_CFG_FDX_LED		(0x00010000)

#define GPIO_CFG			(0x28)

#define AFC_CFG				(0x2C)

/* Hi watermark = 15.5Kb (~10 mtu pkts) */
/* low watermark = 3k (~2 mtu pkts) */
/* backpressure duration = ~ 350us */
/* Apply FC on any frame. */
#define AFC_CFG_DEFAULT			(0x00F830A1)

#define E2P_CMD				(0x30)
#define E2P_CMD_BUSY_			(0x80000000)
#define E2P_CMD_MASK_			(0x70000000)
#define E2P_CMD_READ_			(0x00000000)
#define E2P_CMD_EWDS_			(0x10000000)
#define E2P_CMD_EWEN_			(0x20000000)
#define E2P_CMD_WRITE_			(0x30000000)
#define E2P_CMD_WRAL_			(0x40000000)
#define E2P_CMD_ERASE_			(0x50000000)
#define E2P_CMD_ERAL_			(0x60000000)
#define E2P_CMD_RELOAD_			(0x70000000)
#define E2P_CMD_TIMEOUT_		(0x00000400)
#define E2P_CMD_LOADED_			(0x00000200)
#define E2P_CMD_ADDR_			(0x000001FF)

#define MAX_EEPROM_SIZE			(512)

#define E2P_DATA			(0x34)
#define E2P_DATA_MASK_			(0x000000FF)

#define BURST_CAP			(0x38)

#define	STRAP_STATUS			(0x3C)
#define	STRAP_STATUS_PWR_SEL_		(0x00000020)
#define	STRAP_STATUS_AMDIX_EN_		(0x00000010)
#define	STRAP_STATUS_PORT_SWAP_		(0x00000008)
#define	STRAP_STATUS_EEP_SIZE_		(0x00000004)
#define	STRAP_STATUS_RMT_WKP_		(0x00000002)
#define	STRAP_STATUS_EEP_DISABLE_	(0x00000001)

#define GPIO_WAKE			(0x64)

#define INT_EP_CTL			(0x68)
#define INT_EP_CTL_INTEP_		(0x80000000)
#define INT_EP_CTL_MACRTO_		(0x00080000)
#define INT_EP_CTL_TX_STOP_		(0x00020000)
#define INT_EP_CTL_RX_STOP_		(0x00010000)
#define INT_EP_CTL_PHY_INT_		(0x00008000)
#define INT_EP_CTL_TXE_			(0x00004000)
#define INT_EP_CTL_TDFU_		(0x00002000)
#define INT_EP_CTL_TDFO_		(0x00001000)
#define INT_EP_CTL_RXDF_		(0x00000800)
#define INT_EP_CTL_GPIOS_		(0x000007FF)

#define BULK_IN_DLY			(0x6C)

/* MAC CSRs */
#define MAC_CR				(0x100)
#define MAC_CR_RXALL_			(0x80000000)
#define MAC_CR_RCVOWN_			(0x00800000)
#define MAC_CR_LOOPBK_			(0x00200000)
#define MAC_CR_FDPX_			(0x00100000)
#define MAC_CR_MCPAS_			(0x00080000)
#define MAC_CR_PRMS_			(0x00040000)
#define MAC_CR_INVFILT_			(0x00020000)
#define MAC_CR_PASSBAD_			(0x00010000)
#define MAC_CR_HFILT_			(0x00008000)
#define MAC_CR_HPFILT_			(0x00002000)
#define MAC_CR_LCOLL_			(0x00001000)
#define MAC_CR_BCAST_			(0x00000800)
#define MAC_CR_DISRTY_			(0x00000400)
#define MAC_CR_PADSTR_			(0x00000100)
#define MAC_CR_BOLMT_MASK		(0x000000C0)
#define MAC_CR_DFCHK_			(0x00000020)
#define MAC_CR_TXEN_			(0x00000008)
#define MAC_CR_RXEN_			(0x00000004)

#define ADDRH				(0x104)

#define ADDRL				(0x108)

#define HASHH				(0x10C)

#define HASHL				(0x110)

#define MII_ADDR			(0x114)
#define MII_WRITE_			(0x02)
#define MII_BUSY_			(0x01)
#define MII_READ_			(0x00) /* ~of MII Write bit */

#define MII_DATA			(0x118)

#define FLOW				(0x11C)
#define FLOW_FCPT_			(0xFFFF0000)
#define FLOW_FCPASS_			(0x00000004)
#define FLOW_FCEN_			(0x00000002)
#define FLOW_FCBSY_			(0x00000001)

#define VLAN1				(0x120)

#define VLAN2				(0x124)

#define WUFF				(0x128)
#define LAN9500_WUFF_NUM		(4)
#define LAN9500A_WUFF_NUM		(8)

#define WUCSR				(0x12C)
#define WUCSR_WFF_PTR_RST_		(0x80000000)
#define WUCSR_GUE_			(0x00000200)
#define WUCSR_WUFR_			(0x00000040)
#define WUCSR_MPR_			(0x00000020)
#define WUCSR_WAKE_EN_			(0x00000004)
#define WUCSR_MPEN_			(0x00000002)

#define COE_CR				(0x130)
#define Tx_COE_EN_			(0x00010000)
#define Rx_COE_MODE_			(0x00000002)
#define Rx_COE_EN_			(0x00000001)

/* Vendor-specific PHY Definitions */

/* EDPD NLP / crossover time configuration (LAN9500A only) */
#define PHY_EDPD_CONFIG			(16)
#define PHY_EDPD_CONFIG_TX_NLP_EN_	((u16)0x8000)
#define PHY_EDPD_CONFIG_TX_NLP_1000_	((u16)0x0000)
#define PHY_EDPD_CONFIG_TX_NLP_768_	((u16)0x2000)
#define PHY_EDPD_CONFIG_TX_NLP_512_	((u16)0x4000)
#define PHY_EDPD_CONFIG_TX_NLP_256_	((u16)0x6000)
#define PHY_EDPD_CONFIG_RX_1_NLP_	((u16)0x1000)
#define PHY_EDPD_CONFIG_RX_NLP_64_	((u16)0x0000)
#define PHY_EDPD_CONFIG_RX_NLP_256_	((u16)0x0400)
#define PHY_EDPD_CONFIG_RX_NLP_512_	((u16)0x0800)
#define PHY_EDPD_CONFIG_RX_NLP_1000_	((u16)0x0C00)
#define PHY_EDPD_CONFIG_EXT_CROSSOVER_	((u16)0x0001)
#define PHY_EDPD_CONFIG_DEFAULT		(PHY_EDPD_CONFIG_TX_NLP_EN_ | \
					 PHY_EDPD_CONFIG_TX_NLP_768_ | \
					 PHY_EDPD_CONFIG_RX_1_NLP_)

/* Mode Control/Status Register */
#define PHY_MODE_CTRL_STS		(17)
#define MODE_CTRL_STS_EDPWRDOWN_	((u16)0x2000)
#define MODE_CTRL_STS_ENERGYON_		((u16)0x0002)

#define SPECIAL_CTRL_STS		(27)
#define SPECIAL_CTRL_STS_OVRRD_AMDIX_	((u16)0x8000)
#define SPECIAL_CTRL_STS_AMDIX_ENABLE_	((u16)0x4000)
#define SPECIAL_CTRL_STS_AMDIX_STATE_	((u16)0x2000)

#define PHY_INT_SRC			(29)
#define PHY_INT_SRC_ENERGY_ON_		((u16)0x0080)
#define PHY_INT_SRC_ANEG_COMP_		((u16)0x0040)
#define PHY_INT_SRC_REMOTE_FAULT_	((u16)0x0020)
#define PHY_INT_SRC_LINK_DOWN_		((u16)0x0010)

#define PHY_INT_MASK			(30)
#define PHY_INT_MASK_ENERGY_ON_		((u16)0x0080)
#define PHY_INT_MASK_ANEG_COMP_		((u16)0x0040)
#define PHY_INT_MASK_REMOTE_FAULT_	((u16)0x0020)
#define PHY_INT_MASK_LINK_DOWN_		((u16)0x0010)
#define PHY_INT_MASK_DEFAULT_		(PHY_INT_MASK_ANEG_COMP_ | \
					 PHY_INT_MASK_LINK_DOWN_)

#define PHY_SPECIAL			(31)
#define PHY_SPECIAL_SPD_		((u16)0x001C)
#define PHY_SPECIAL_SPD_10HALF_		((u16)0x0004)
#define PHY_SPECIAL_SPD_10FULL_		((u16)0x0014)
#define PHY_SPECIAL_SPD_100HALF_	((u16)0x0008)
#define PHY_SPECIAL_SPD_100FULL_	((u16)0x0018)

/* USB Vendor Requests */
#define USB_VENDOR_REQUEST_WRITE_REGISTER	0xA0
#define USB_VENDOR_REQUEST_READ_REGISTER	0xA1
#define USB_VENDOR_REQUEST_GET_STATS		0xA2

/* Interrupt Endpoint status word bitfields */
#define INT_ENP_TX_STOP_		((u32)BIT(17))
#define INT_ENP_RX_STOP_		((u32)BIT(16))
#define INT_ENP_PHY_INT_		((u32)BIT(15))
#define INT_ENP_TXE_			((u32)BIT(14))
#define INT_ENP_TDFU_			((u32)BIT(13))
#define INT_ENP_TDFO_			((u32)BIT(12))
#define INT_ENP_RXDF_			((u32)BIT(11))

#endif /* _SMSC95XX_H */
