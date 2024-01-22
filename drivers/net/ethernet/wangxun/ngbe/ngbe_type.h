/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _NGBE_TYPE_H_
#define _NGBE_TYPE_H_

#include <linux/types.h>
#include <linux/netdevice.h>

/************ NGBE_register.h ************/
/* Device IDs */
#define NGBE_DEV_ID_EM_WX1860AL_W		0x0100
#define NGBE_DEV_ID_EM_WX1860A2			0x0101
#define NGBE_DEV_ID_EM_WX1860A2S		0x0102
#define NGBE_DEV_ID_EM_WX1860A4			0x0103
#define NGBE_DEV_ID_EM_WX1860A4S		0x0104
#define NGBE_DEV_ID_EM_WX1860AL2		0x0105
#define NGBE_DEV_ID_EM_WX1860AL2S		0x0106
#define NGBE_DEV_ID_EM_WX1860AL4		0x0107
#define NGBE_DEV_ID_EM_WX1860AL4S		0x0108
#define NGBE_DEV_ID_EM_WX1860LC			0x0109
#define NGBE_DEV_ID_EM_WX1860A1			0x010a
#define NGBE_DEV_ID_EM_WX1860A1L		0x010b

/* Subsystem ID */
#define NGBE_SUBID_M88E1512_SFP			0x0003
#define NGBE_SUBID_OCP_CARD			0x0040
#define NGBE_SUBID_LY_M88E1512_SFP		0x0050
#define NGBE_SUBID_M88E1512_RJ45		0x0051
#define NGBE_SUBID_M88E1512_MIX			0x0052
#define NGBE_SUBID_YT8521S_SFP			0x0060
#define NGBE_SUBID_INTERNAL_YT8521S_SFP		0x0061
#define NGBE_SUBID_YT8521S_SFP_GPIO		0x0062
#define NGBE_SUBID_INTERNAL_YT8521S_SFP_GPIO	0x0064
#define NGBE_SUBID_LY_YT8521S_SFP		0x0070
#define NGBE_SUBID_RGMII_FPGA			0x0080

#define NGBE_OEM_MASK				0x00FF

#define NGBE_NCSI_SUP				0x8000
#define NGBE_NCSI_MASK				0x8000
#define NGBE_WOL_SUP				0x4000
#define NGBE_WOL_MASK				0x4000

/**************** EM Registers ****************************/
/* chip control Registers */
#define NGBE_MIS_PRB_CTL			0x10010
/* FMGR Registers */
#define NGBE_SPI_ILDR_STATUS			0x10120
#define NGBE_SPI_ILDR_STATUS_PERST		BIT(0) /* PCIE_PERST is done */
#define NGBE_SPI_ILDR_STATUS_PWRRST		BIT(1) /* Power on reset is done */

/* Checksum and EEPROM pointers */
#define NGBE_CALSUM_COMMAND			0xE9
#define NGBE_CALSUM_CAP_STATUS			0x10224
#define NGBE_EEPROM_VERSION_STORE_REG		0x1022C
#define NGBE_SAN_MAC_ADDR_PTR			0x18
#define NGBE_DEVICE_CAPS			0x1C
#define NGBE_EEPROM_VERSION_L			0x1D
#define NGBE_EEPROM_VERSION_H			0x1E

/* GPIO Registers */
#define NGBE_GPIO_DR				0x14800
#define NGBE_GPIO_DDR				0x14804
/*GPIO bit */
#define NGBE_GPIO_DR_0				BIT(0) /* SDP0 Data Value */
#define NGBE_GPIO_DR_1				BIT(1) /* SDP1 Data Value */
#define NGBE_GPIO_DDR_0				BIT(0) /* SDP0 IO direction */
#define NGBE_GPIO_DDR_1				BIT(1) /* SDP1 IO direction */

/* Extended Interrupt Enable Set */
#define NGBE_PX_MISC_IEN_DEV_RST		BIT(10)
#define NGBE_PX_MISC_IEN_ETH_LK			BIT(18)
#define NGBE_PX_MISC_IEN_INT_ERR		BIT(20)
#define NGBE_PX_MISC_IEN_GPIO			BIT(26)
#define NGBE_PX_MISC_IEN_MASK ( \
				NGBE_PX_MISC_IEN_DEV_RST | \
				NGBE_PX_MISC_IEN_ETH_LK | \
				NGBE_PX_MISC_IEN_INT_ERR | \
				NGBE_PX_MISC_IEN_GPIO)

#define NGBE_INTR_ALL				0x1FF
#define NGBE_INTR_MISC				BIT(0)

#define NGBE_PHY_CONFIG(reg_offset)		(0x14000 + ((reg_offset) * 4))
#define NGBE_CFG_LAN_SPEED			0x14440
#define NGBE_CFG_PORT_ST			0x14404

/* Wake up registers */
#define NGBE_PSR_WKUP_CTL			0x15B80
/* Wake Up Filter Control Bit */
#define NGBE_PSR_WKUP_CTL_LNKC			BIT(0) /* Link Status Change Wakeup Enable*/
#define NGBE_PSR_WKUP_CTL_MAG			BIT(1) /* Magic Packet Wakeup Enable */
#define NGBE_PSR_WKUP_CTL_EX			BIT(2) /* Directed Exact Wakeup Enable */
#define NGBE_PSR_WKUP_CTL_MC			BIT(3) /* Directed Multicast Wakeup Enable*/
#define NGBE_PSR_WKUP_CTL_BC			BIT(4) /* Broadcast Wakeup Enable */
#define NGBE_PSR_WKUP_CTL_ARP			BIT(5) /* ARP Request Packet Wakeup Enable*/
#define NGBE_PSR_WKUP_CTL_IPV4			BIT(6) /* Directed IPv4 Pkt Wakeup Enable */
#define NGBE_PSR_WKUP_CTL_IPV6			BIT(7) /* Directed IPv6 Pkt Wakeup Enable */

#define NGBE_FW_EEPROM_CHECKSUM_CMD		0xE9
#define NGBE_FW_NVM_DATA_OFFSET			3
#define NGBE_FW_CMD_DEFAULT_CHECKSUM		0xFF /* checksum always 0xFF */
#define NGBE_FW_CMD_ST_PASS			0x80658383
#define NGBE_FW_CMD_ST_FAIL			0x70657376

#define NGBE_MAX_FDIR_INDICES			7
#define NGBE_MAX_RSS_INDICES			8

#define NGBE_MAX_RX_QUEUES			(NGBE_MAX_FDIR_INDICES + 1)
#define NGBE_MAX_TX_QUEUES			(NGBE_MAX_FDIR_INDICES + 1)

#define NGBE_ETH_LENGTH_OF_ADDRESS		6
#define NGBE_MAX_MSIX_VECTORS			0x09
#define NGBE_RAR_ENTRIES			32
#define NGBE_RX_PB_SIZE				42
#define NGBE_MC_TBL_SIZE			128
#define NGBE_SP_VFT_TBL_SIZE			128
#define NGBE_TDB_PB_SZ				(20 * 1024) /* 160KB Packet Buffer */

/* TX/RX descriptor defines */
#define NGBE_DEFAULT_TXD			512 /* default ring size */
#define NGBE_DEFAULT_TX_WORK			256
#define NGBE_MAX_TXD				8192
#define NGBE_MIN_TXD				128

#define NGBE_DEFAULT_RXD			512 /* default ring size */
#define NGBE_DEFAULT_RX_WORK			256
#define NGBE_MAX_RXD				8192
#define NGBE_MIN_RXD				128

extern char ngbe_driver_name[];

void ngbe_down(struct wx *wx);
void ngbe_up(struct wx *wx);
int ngbe_setup_tc(struct net_device *dev, u8 tc);

#endif /* _NGBE_TYPE_H_ */
