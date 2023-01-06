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
#define NGBE_SPI_ILDR_STATUS_LAN_SW_RST(_i)	BIT((_i) + 9) /* lan soft reset done */

/* Checksum and EEPROM pointers */
#define NGBE_CALSUM_COMMAND			0xE9
#define NGBE_CALSUM_CAP_STATUS			0x10224
#define NGBE_EEPROM_VERSION_STORE_REG		0x1022C
#define NGBE_SAN_MAC_ADDR_PTR			0x18
#define NGBE_DEVICE_CAPS			0x1C
#define NGBE_EEPROM_VERSION_L			0x1D
#define NGBE_EEPROM_VERSION_H			0x1E

/* Media-dependent registers. */
#define NGBE_MDIO_CLAUSE_SELECT			0x11220

/* GPIO Registers */
#define NGBE_GPIO_DR				0x14800
#define NGBE_GPIO_DDR				0x14804
/*GPIO bit */
#define NGBE_GPIO_DR_0				BIT(0) /* SDP0 Data Value */
#define NGBE_GPIO_DR_1				BIT(1) /* SDP1 Data Value */
#define NGBE_GPIO_DDR_0				BIT(0) /* SDP0 IO direction */
#define NGBE_GPIO_DDR_1				BIT(1) /* SDP1 IO direction */

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

#define NGBE_MAX_RX_QUEUES			(NGBE_MAX_FDIR_INDICES + 1)
#define NGBE_MAX_TX_QUEUES			(NGBE_MAX_FDIR_INDICES + 1)

#define NGBE_ETH_LENGTH_OF_ADDRESS		6
#define NGBE_MAX_MSIX_VECTORS			0x09
#define NGBE_RAR_ENTRIES			32

/* TX/RX descriptor defines */
#define NGBE_DEFAULT_TXD			512 /* default ring size */
#define NGBE_DEFAULT_TX_WORK			256
#define NGBE_MAX_TXD				8192
#define NGBE_MIN_TXD				128

#define NGBE_DEFAULT_RXD			512 /* default ring size */
#define NGBE_DEFAULT_RX_WORK			256
#define NGBE_MAX_RXD				8192
#define NGBE_MIN_RXD				128

enum ngbe_phy_type {
	ngbe_phy_unknown = 0,
	ngbe_phy_none,
	ngbe_phy_internal,
	ngbe_phy_m88e1512,
	ngbe_phy_m88e1512_sfi,
	ngbe_phy_m88e1512_unknown,
	ngbe_phy_yt8521s,
	ngbe_phy_yt8521s_sfi,
	ngbe_phy_internal_yt8521s_sfi,
	ngbe_phy_generic
};

enum ngbe_media_type {
	ngbe_media_type_unknown = 0,
	ngbe_media_type_fiber,
	ngbe_media_type_copper,
	ngbe_media_type_backplane,
};

enum ngbe_mac_type {
	ngbe_mac_type_unknown = 0,
	ngbe_mac_type_mdi,
	ngbe_mac_type_rgmii
};

struct ngbe_phy_info {
	enum ngbe_phy_type type;
	enum ngbe_media_type media_type;

	u32 addr;
	u32 id;

	bool reset_if_overtemp;

};

/* board specific private data structure */
struct ngbe_adapter {
	u8 __iomem *io_addr;    /* Mainly for iounmap use */
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	struct wx wx;
	struct ngbe_phy_info phy;
	enum ngbe_mac_type mac_type;

	bool wol_enabled;
	bool ncsi_enabled;
	bool gpio_ctrl;

	u16 msg_enable;

	/* Tx fast path data */
	int num_tx_queues;
	u16 tx_itr_setting;
	u16 tx_work_limit;

	/* Rx fast path data */
	int num_rx_queues;
	u16 rx_itr_setting;
	u16 rx_work_limit;

	int num_q_vectors;      /* current number of q_vectors for device */
	int max_q_vectors;      /* upper limit of q_vectors for device */

	u32 tx_ring_count;
	u32 rx_ring_count;

#define NGBE_MAX_RETA_ENTRIES 128
	u8 rss_indir_tbl[NGBE_MAX_RETA_ENTRIES];

#define NGBE_RSS_KEY_SIZE     40  /* size of RSS Hash Key in bytes */
	u32 *rss_key;
	u32 wol;

	u16 bd_number;
};

extern char ngbe_driver_name[];

#endif /* _NGBE_TYPE_H_ */
