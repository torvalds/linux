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
#define TX_CMD_A_DATA_OFFSET_	(0x001F0000)	/* Data Start Offset */
#define TX_CMD_A_FIRST_SEG_	(0x00002000)	/* First Segment */
#define TX_CMD_A_LAST_SEG_	(0x00001000)	/* Last Segment */
#define TX_CMD_A_BUF_SIZE_	(0x000007FF)	/* Buffer Size */

#define TX_CMD_B_CSUM_ENABLE	(0x00004000)	/* TX Checksum Enable */
#define TX_CMD_B_ADD_CRC_DIS_	(0x00002000)	/* Add CRC Disable */
#define TX_CMD_B_DIS_PADDING_	(0x00001000)	/* Disable Frame Padding */
#define TX_CMD_B_FRAME_LENGTH_	(0x000007FF)	/* Frame Length (bytes) */

/* Rx status word */
#define RX_STS_FF_		(0x40000000)	/* Filter Fail */
#define RX_STS_FL_		(0x3FFF0000)	/* Frame Length */
#define RX_STS_ES_		(0x00008000)	/* Error Summary */
#define RX_STS_BF_		(0x00002000)	/* Broadcast Frame */
#define RX_STS_LE_		(0x00001000)	/* Length Error */
#define RX_STS_RF_		(0x00000800)	/* Runt Frame */
#define RX_STS_MF_		(0x00000400)	/* Multicast Frame */
#define RX_STS_TL_		(0x00000080)	/* Frame too long */
#define RX_STS_CS_		(0x00000040)	/* Collision Seen */
#define RX_STS_FT_		(0x00000020)	/* Frame Type */
#define RX_STS_RW_		(0x00000010)	/* Receive Watchdog */
#define RX_STS_ME_		(0x00000008)	/* MII Error */
#define RX_STS_DB_		(0x00000004)	/* Dribbling */
#define RX_STS_CRC_		(0x00000002)	/* CRC Error */

/* SCSRs - System Control and Status Registers */
/* Device ID and Revision Register */
#define ID_REV			(0x00)
#define ID_REV_CHIP_ID_MASK_	(0xFFFF0000)
#define ID_REV_CHIP_REV_MASK_	(0x0000FFFF)
#define ID_REV_CHIP_ID_9500_	(0x9500)
#define ID_REV_CHIP_ID_9500A_	(0x9E00)
#define ID_REV_CHIP_ID_9512_	(0xEC00)
#define ID_REV_CHIP_ID_9530_	(0x9530)
#define ID_REV_CHIP_ID_89530_	(0x9E08)
#define ID_REV_CHIP_ID_9730_	(0x9730)

/* Interrupt Status Register */
#define INT_STS			(0x08)
#define INT_STS_MAC_RTO_	(0x00040000)	/* MAC Reset Time Out */
#define INT_STS_TX_STOP_	(0x00020000)	/* TX Stopped */
#define INT_STS_RX_STOP_	(0x00010000)	/* RX Stopped */
#define INT_STS_PHY_INT_	(0x00008000)	/* PHY Interrupt */
#define INT_STS_TXE_		(0x00004000)	/* Transmitter Error */
#define INT_STS_TDFU_		(0x00002000)	/* TX Data FIFO Underrun */
#define INT_STS_TDFO_		(0x00001000)	/* TX Data FIFO Overrun */
#define INT_STS_RXDF_		(0x00000800)	/* RX Dropped Frame */
#define INT_STS_GPIOS_		(0x000007FF)	/* GPIOs Interrupts */
#define INT_STS_CLEAR_ALL_	(0xFFFFFFFF)

/* Receive Configuration Register */
#define RX_CFG			(0x0C)
#define RX_FIFO_FLUSH_		(0x00000001)	/* Receive FIFO Flush */

/* Transmit Configuration Register */
#define TX_CFG			(0x10)
#define TX_CFG_ON_		(0x00000004)	/* Transmitter Enable */
#define TX_CFG_STOP_		(0x00000002)	/* Stop Transmitter */
#define TX_CFG_FIFO_FLUSH_	(0x00000001)	/* Transmit FIFO Flush */

/* Hardware Configuration Register */
#define HW_CFG			(0x14)
#define HW_CFG_BIR_		(0x00001000)	/* Bulk In Empty Response */
#define HW_CFG_LEDB_		(0x00000800)	/* Activity LED 80ms Bypass */
#define HW_CFG_RXDOFF_		(0x00000600)	/* RX Data Offset */
#define HW_CFG_SBP_		(0x00000100)	/* Stall Bulk Out Pipe Dis. */
#define HW_CFG_IME_		(0x00000080)	/* Internal MII Visi. Enable */
#define HW_CFG_DRP_		(0x00000040)	/* Discard Errored RX Frame */
#define HW_CFG_MEF_		(0x00000020)	/* Mult. ETH Frames/USB pkt */
#define HW_CFG_ETC_		(0x00000010)	/* EEPROM Timeout Control */
#define HW_CFG_LRST_		(0x00000008)	/* Soft Lite Reset */
#define HW_CFG_PSEL_		(0x00000004)	/* External PHY Select */
#define HW_CFG_BCE_		(0x00000002)	/* Burst Cap Enable */
#define HW_CFG_SRST_		(0x00000001)	/* Soft Reset */

/* Receive FIFO Information Register */
#define RX_FIFO_INF		(0x18)
#define RX_FIFO_INF_USED_	(0x0000FFFF)	/* RX Data FIFO Used Space */

/* Transmit FIFO Information Register */
#define TX_FIFO_INF		(0x1C)
#define TX_FIFO_INF_FREE_	(0x0000FFFF)	/* TX Data FIFO Free Space */

/* Power Management Control Register */
#define PM_CTRL			(0x20)
#define PM_CTL_RES_CLR_WKP_STS	(0x00000200)	/* Resume Clears Wakeup STS */
#define PM_CTL_RES_CLR_WKP_EN	(0x00000100)	/* Resume Clears Wkp Enables */
#define PM_CTL_DEV_RDY_		(0x00000080)	/* Device Ready */
#define PM_CTL_SUS_MODE_	(0x00000060)	/* Suspend Mode */
#define PM_CTL_SUS_MODE_0	(0x00000000)
#define PM_CTL_SUS_MODE_1	(0x00000020)
#define PM_CTL_SUS_MODE_2	(0x00000040)
#define PM_CTL_SUS_MODE_3	(0x00000060)
#define PM_CTL_PHY_RST_		(0x00000010)	/* PHY Reset */
#define PM_CTL_WOL_EN_		(0x00000008)	/* Wake On Lan Enable */
#define PM_CTL_ED_EN_		(0x00000004)	/* Energy Detect Enable */
#define PM_CTL_WUPS_		(0x00000003)	/* Wake Up Status */
#define PM_CTL_WUPS_NO_		(0x00000000)	/* No Wake Up Event Detected */
#define PM_CTL_WUPS_ED_		(0x00000001)	/* Energy Detect */
#define PM_CTL_WUPS_WOL_	(0x00000002)	/* Wake On Lan */
#define PM_CTL_WUPS_MULTI_	(0x00000003)	/* Multiple Events Occurred */

/* LED General Purpose IO Configuration Register */
#define LED_GPIO_CFG		(0x24)
#define LED_GPIO_CFG_SPD_LED	(0x01000000)	/* GPIOz as Speed LED */
#define LED_GPIO_CFG_LNK_LED	(0x00100000)	/* GPIOy as Link LED */
#define LED_GPIO_CFG_FDX_LED	(0x00010000)	/* GPIOx as Full Duplex LED */

/* General Purpose IO Configuration Register */
#define GPIO_CFG		(0x28)

/* Automatic Flow Control Configuration Register */
#define AFC_CFG			(0x2C)
#define AFC_CFG_HI_		(0x00FF0000)	/* Auto Flow Ctrl High Level */
#define AFC_CFG_LO_		(0x0000FF00)	/* Auto Flow Ctrl Low Level */
#define AFC_CFG_BACK_DUR_	(0x000000F0)	/* Back Pressure Duration */
#define AFC_CFG_FC_MULT_	(0x00000008)	/* Flow Ctrl on Mcast Frame */
#define AFC_CFG_FC_BRD_		(0x00000004)	/* Flow Ctrl on Bcast Frame */
#define AFC_CFG_FC_ADD_		(0x00000002)	/* Flow Ctrl on Addr. Decode */
#define AFC_CFG_FC_ANY_		(0x00000001)	/* Flow Ctrl on Any Frame */
/* Hi watermark = 15.5Kb (~10 mtu pkts) */
/* low watermark = 3k (~2 mtu pkts) */
/* backpressure duration = ~ 350us */
/* Apply FC on any frame. */
#define AFC_CFG_DEFAULT		(0x00F830A1)

/* EEPROM Command Register */
#define E2P_CMD			(0x30)
#define E2P_CMD_BUSY_		(0x80000000)	/* E2P Controller Busy */
#define E2P_CMD_MASK_		(0x70000000)	/* Command Mask (see below) */
#define E2P_CMD_READ_		(0x00000000)	/* Read Location */
#define E2P_CMD_EWDS_		(0x10000000)	/* Erase/Write Disable */
#define E2P_CMD_EWEN_		(0x20000000)	/* Erase/Write Enable */
#define E2P_CMD_WRITE_		(0x30000000)	/* Write Location */
#define E2P_CMD_WRAL_		(0x40000000)	/* Write All */
#define E2P_CMD_ERASE_		(0x50000000)	/* Erase Location */
#define E2P_CMD_ERAL_		(0x60000000)	/* Erase All */
#define E2P_CMD_RELOAD_		(0x70000000)	/* Data Reload */
#define E2P_CMD_TIMEOUT_	(0x00000400)	/* Set if no resp within 30ms */
#define E2P_CMD_LOADED_		(0x00000200)	/* Valid EEPROM found */
#define E2P_CMD_ADDR_		(0x000001FF)	/* Byte aligned address */

#define MAX_EEPROM_SIZE		(512)

/* EEPROM Data Register */
#define E2P_DATA		(0x34)
#define E2P_DATA_MASK_		(0x000000FF)	/* EEPROM Data Mask */

/* Burst Cap Register */
#define BURST_CAP		(0x38)
#define BURST_CAP_MASK_		(0x000000FF)	/* Max burst sent by the UTX */

/* Configuration Straps Status Register */
#define	STRAP_STATUS			(0x3C)
#define	STRAP_STATUS_PWR_SEL_		(0x00000020) /* Device self-powered */
#define	STRAP_STATUS_AMDIX_EN_		(0x00000010) /* Auto-MDIX Enabled */
#define	STRAP_STATUS_PORT_SWAP_		(0x00000008) /* USBD+/USBD- Swapped */
#define	STRAP_STATUS_EEP_SIZE_		(0x00000004) /* EEPROM Size */
#define	STRAP_STATUS_RMT_WKP_		(0x00000002) /* Remote Wkp supported */
#define	STRAP_STATUS_EEP_DISABLE_	(0x00000001) /* EEPROM Disabled */

/* Data Port Select Register */
#define DP_SEL			(0x40)

/* Data Port Command Register */
#define DP_CMD			(0x44)

/* Data Port Address Register */
#define DP_ADDR			(0x48)

/* Data Port Data 0 Register */
#define DP_DATA0		(0x4C)

/* Data Port Data 1 Register */
#define DP_DATA1		(0x50)

/* General Purpose IO Wake Enable and Polarity Register */
#define GPIO_WAKE		(0x64)

/* Interrupt Endpoint Control Register */
#define INT_EP_CTL		(0x68)
#define INT_EP_CTL_INTEP_	(0x80000000)	/* Always TX Interrupt PKT */
#define INT_EP_CTL_MAC_RTO_	(0x00080000)	/* MAC Reset Time Out */
#define INT_EP_CTL_RX_FIFO_	(0x00040000)	/* RX FIFO Has Frame */
#define INT_EP_CTL_TX_STOP_	(0x00020000)	/* TX Stopped */
#define INT_EP_CTL_RX_STOP_	(0x00010000)	/* RX Stopped */
#define INT_EP_CTL_PHY_INT_	(0x00008000)	/* PHY Interrupt */
#define INT_EP_CTL_TXE_		(0x00004000)	/* TX Error */
#define INT_EP_CTL_TDFU_	(0x00002000)	/* TX Data FIFO Underrun */
#define INT_EP_CTL_TDFO_	(0x00001000)	/* TX Data FIFO Overrun */
#define INT_EP_CTL_RXDF_	(0x00000800)	/* RX Dropped Frame */
#define INT_EP_CTL_GPIOS_	(0x000007FF)	/* GPIOs Interrupt Enable */

/* Bulk In Delay Register (units of 16.667ns, until ~1092µs) */
#define BULK_IN_DLY		(0x6C)

/* MAC CSRs - MAC Control and Status Registers */
/* MAC Control Register */
#define MAC_CR			(0x100)
#define MAC_CR_RXALL_		(0x80000000)	/* Receive All Mode */
#define MAC_CR_RCVOWN_		(0x00800000)	/* Disable Receive Own */
#define MAC_CR_LOOPBK_		(0x00200000)	/* Loopback Operation Mode */
#define MAC_CR_FDPX_		(0x00100000)	/* Full Duplex Mode */
#define MAC_CR_MCPAS_		(0x00080000)	/* Pass All Multicast */
#define MAC_CR_PRMS_		(0x00040000)	/* Promiscuous Mode */
#define MAC_CR_INVFILT_		(0x00020000)	/* Inverse Filtering */
#define MAC_CR_PASSBAD_		(0x00010000)	/* Pass Bad Frames */
#define MAC_CR_HFILT_		(0x00008000)	/* Hash Only Filtering Mode */
#define MAC_CR_HPFILT_		(0x00002000)	/* Hash/Perfect Filt. Mode */
#define MAC_CR_LCOLL_		(0x00001000)	/* Late Collision Control */
#define MAC_CR_BCAST_		(0x00000800)	/* Disable Broadcast Frames */
#define MAC_CR_DISRTY_		(0x00000400)	/* Disable Retry */
#define MAC_CR_PADSTR_		(0x00000100)	/* Automatic Pad Stripping */
#define MAC_CR_BOLMT_MASK	(0x000000C0)	/* BackOff Limit */
#define MAC_CR_DFCHK_		(0x00000020)	/* Deferral Check */
#define MAC_CR_TXEN_		(0x00000008)	/* Transmitter Enable */
#define MAC_CR_RXEN_		(0x00000004)	/* Receiver Enable */

/* MAC Address High Register */
#define ADDRH			(0x104)

/* MAC Address Low Register */
#define ADDRL			(0x108)

/* Multicast Hash Table High Register */
#define HASHH			(0x10C)

/* Multicast Hash Table Low Register */
#define HASHL			(0x110)

/* MII Access Register */
#define MII_ADDR		(0x114)
#define MII_WRITE_		(0x02)
#define MII_BUSY_		(0x01)
#define MII_READ_		(0x00) /* ~of MII Write bit */

/* MII Data Register */
#define MII_DATA		(0x118)

/* Flow Control Register */
#define FLOW			(0x11C)
#define FLOW_FCPT_		(0xFFFF0000)	/* Pause Time */
#define FLOW_FCPASS_		(0x00000004)	/* Pass Control Frames */
#define FLOW_FCEN_		(0x00000002)	/* Flow Control Enable */
#define FLOW_FCBSY_		(0x00000001)	/* Flow Control Busy */

/* VLAN1 Tag Register */
#define VLAN1			(0x120)

/* VLAN2 Tag Register */
#define VLAN2			(0x124)

/* Wake Up Frame Filter Register */
#define WUFF			(0x128)
#define LAN9500_WUFF_NUM	(4)
#define LAN9500A_WUFF_NUM	(8)

/* Wake Up Control and Status Register */
#define WUCSR			(0x12C)
#define WUCSR_WFF_PTR_RST_	(0x80000000)	/* WFrame Filter Pointer Rst */
#define WUCSR_GUE_		(0x00000200)	/* Global Unicast Enable */
#define WUCSR_WUFR_		(0x00000040)	/* Wakeup Frame Received */
#define WUCSR_MPR_		(0x00000020)	/* Magic Packet Received */
#define WUCSR_WAKE_EN_		(0x00000004)	/* Wakeup Frame Enable */
#define WUCSR_MPEN_		(0x00000002)	/* Magic Packet Enable */

/* Checksum Offload Engine Control Register */
#define COE_CR			(0x130)
#define Tx_COE_EN_		(0x00010000)	/* TX Csum Offload Enable */
#define Rx_COE_MODE_		(0x00000002)	/* RX Csum Offload Mode */
#define Rx_COE_EN_		(0x00000001)	/* RX Csum Offload Enable */

/* Vendor-specific PHY Definitions (via MII access) */
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

/* Control/Status Indication Register */
#define SPECIAL_CTRL_STS		(27)
#define SPECIAL_CTRL_STS_OVRRD_AMDIX_	((u16)0x8000)
#define SPECIAL_CTRL_STS_AMDIX_ENABLE_	((u16)0x4000)
#define SPECIAL_CTRL_STS_AMDIX_STATE_	((u16)0x2000)

/* Interrupt Source Register */
#define PHY_INT_SRC			(29)
#define PHY_INT_SRC_ENERGY_ON_		((u16)0x0080)
#define PHY_INT_SRC_ANEG_COMP_		((u16)0x0040)
#define PHY_INT_SRC_REMOTE_FAULT_	((u16)0x0020)
#define PHY_INT_SRC_LINK_DOWN_		((u16)0x0010)

/* Interrupt Mask Register */
#define PHY_INT_MASK			(30)
#define PHY_INT_MASK_ENERGY_ON_		((u16)0x0080)
#define PHY_INT_MASK_ANEG_COMP_		((u16)0x0040)
#define PHY_INT_MASK_REMOTE_FAULT_	((u16)0x0020)
#define PHY_INT_MASK_LINK_DOWN_		((u16)0x0010)
#define PHY_INT_MASK_DEFAULT_		(PHY_INT_MASK_ANEG_COMP_ | \
					 PHY_INT_MASK_LINK_DOWN_)
/* PHY Special Control/Status Register */
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
#define INT_ENP_MAC_RTO_		((u32)BIT(18))	/* MAC Reset Time Out */
#define INT_ENP_TX_STOP_		((u32)BIT(17))	/* TX Stopped */
#define INT_ENP_RX_STOP_		((u32)BIT(16))	/* RX Stopped */
#define INT_ENP_PHY_INT_		((u32)BIT(15))	/* PHY Interrupt */
#define INT_ENP_TXE_			((u32)BIT(14))	/* TX Error */
#define INT_ENP_TDFU_			((u32)BIT(13))	/* TX FIFO Underrun */
#define INT_ENP_TDFO_			((u32)BIT(12))	/* TX FIFO Overrun */
#define INT_ENP_RXDF_			((u32)BIT(11))	/* RX Dropped Frame */

#endif /* _SMSC95XX_H */
