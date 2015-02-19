/* CoreChip-sz SR9800 one chip USB 2.0 Ethernet Devices
 *
 * Author : Liu Junliang <liujunliang_ljl@163.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef	_SR9800_H
#define	_SR9800_H

/* SR9800 spec. command table on Linux Platform */

/* command : Software Station Management Control Reg */
#define SR_CMD_SET_SW_MII		0x06
/* command : PHY Read Reg */
#define SR_CMD_READ_MII_REG		0x07
/* command : PHY Write Reg */
#define SR_CMD_WRITE_MII_REG		0x08
/* command : Hardware Station Management Control Reg */
#define SR_CMD_SET_HW_MII		0x0a
/* command : SROM Read Reg */
#define SR_CMD_READ_EEPROM		0x0b
/* command : SROM Write Reg */
#define SR_CMD_WRITE_EEPROM		0x0c
/* command : SROM Write Enable Reg */
#define SR_CMD_WRITE_ENABLE		0x0d
/* command : SROM Write Disable Reg */
#define SR_CMD_WRITE_DISABLE		0x0e
/* command : RX Control Read Reg */
#define SR_CMD_READ_RX_CTL		0x0f
#define		SR_RX_CTL_PRO			(1 << 0)
#define		SR_RX_CTL_AMALL			(1 << 1)
#define		SR_RX_CTL_SEP			(1 << 2)
#define		SR_RX_CTL_AB			(1 << 3)
#define		SR_RX_CTL_AM			(1 << 4)
#define		SR_RX_CTL_AP			(1 << 5)
#define		SR_RX_CTL_ARP			(1 << 6)
#define		SR_RX_CTL_SO			(1 << 7)
#define		SR_RX_CTL_RH1M			(1 << 8)
#define		SR_RX_CTL_RH2M			(1 << 9)
#define		SR_RX_CTL_RH3M			(1 << 10)
/* command : RX Control Write Reg */
#define SR_CMD_WRITE_RX_CTL		0x10
/* command : IPG0/IPG1/IPG2 Control Read Reg */
#define SR_CMD_READ_IPG012		0x11
/* command : IPG0/IPG1/IPG2 Control Write Reg */
#define SR_CMD_WRITE_IPG012		0x12
/* command : Node ID Read Reg */
#define SR_CMD_READ_NODE_ID		0x13
/* command : Node ID Write Reg */
#define SR_CMD_WRITE_NODE_ID		0x14
/* command : Multicast Filter Array Read Reg */
#define	SR_CMD_READ_MULTI_FILTER	0x15
/* command : Multicast Filter Array Write Reg */
#define SR_CMD_WRITE_MULTI_FILTER	0x16
/* command : Eth/HomePNA PHY Address Reg */
#define SR_CMD_READ_PHY_ID		0x19
/* command : Medium Status Read Reg */
#define SR_CMD_READ_MEDIUM_STATUS	0x1a
#define		SR_MONITOR_LINK			(1 << 1)
#define		SR_MONITOR_MAGIC		(1 << 2)
#define		SR_MONITOR_HSFS			(1 << 4)
/* command : Medium Status Write Reg */
#define SR_CMD_WRITE_MEDIUM_MODE	0x1b
#define		SR_MEDIUM_GM			(1 << 0)
#define		SR_MEDIUM_FD			(1 << 1)
#define		SR_MEDIUM_AC			(1 << 2)
#define		SR_MEDIUM_ENCK			(1 << 3)
#define		SR_MEDIUM_RFC			(1 << 4)
#define		SR_MEDIUM_TFC			(1 << 5)
#define		SR_MEDIUM_JFE			(1 << 6)
#define		SR_MEDIUM_PF			(1 << 7)
#define		SR_MEDIUM_RE			(1 << 8)
#define		SR_MEDIUM_PS			(1 << 9)
#define		SR_MEDIUM_RSV			(1 << 10)
#define		SR_MEDIUM_SBP			(1 << 11)
#define		SR_MEDIUM_SM			(1 << 12)
/* command : Monitor Mode Status Read Reg */
#define SR_CMD_READ_MONITOR_MODE	0x1c
/* command : Monitor Mode Status Write Reg */
#define SR_CMD_WRITE_MONITOR_MODE	0x1d
/* command : GPIO Status Read Reg */
#define SR_CMD_READ_GPIOS		0x1e
#define		SR_GPIO_GPO0EN		(1 << 0) /* GPIO0 Output enable */
#define		SR_GPIO_GPO_0		(1 << 1) /* GPIO0 Output value */
#define		SR_GPIO_GPO1EN		(1 << 2) /* GPIO1 Output enable */
#define		SR_GPIO_GPO_1		(1 << 3) /* GPIO1 Output value */
#define		SR_GPIO_GPO2EN		(1 << 4) /* GPIO2 Output enable */
#define		SR_GPIO_GPO_2		(1 << 5) /* GPIO2 Output value */
#define		SR_GPIO_RESERVED	(1 << 6) /* Reserved */
#define		SR_GPIO_RSE		(1 << 7) /* Reload serial EEPROM */
/* command : GPIO Status Write Reg */
#define SR_CMD_WRITE_GPIOS		0x1f
/* command : Eth PHY Power and Reset Control Reg */
#define SR_CMD_SW_RESET			0x20
#define		SR_SWRESET_CLEAR		0x00
#define		SR_SWRESET_RR			(1 << 0)
#define		SR_SWRESET_RT			(1 << 1)
#define		SR_SWRESET_PRTE			(1 << 2)
#define		SR_SWRESET_PRL			(1 << 3)
#define		SR_SWRESET_BZ			(1 << 4)
#define		SR_SWRESET_IPRL			(1 << 5)
#define		SR_SWRESET_IPPD			(1 << 6)
/* command : Software Interface Selection Status Read Reg */
#define SR_CMD_SW_PHY_STATUS		0x21
/* command : Software Interface Selection Status Write Reg */
#define SR_CMD_SW_PHY_SELECT		0x22
/* command : BULK in Buffer Size Reg */
#define	SR_CMD_BULKIN_SIZE		0x2A
/* command : LED_MUX Control Reg */
#define	SR_CMD_LED_MUX			0x70
#define		SR_LED_MUX_TX_ACTIVE		(1 << 0)
#define		SR_LED_MUX_RX_ACTIVE		(1 << 1)
#define		SR_LED_MUX_COLLISION		(1 << 2)
#define		SR_LED_MUX_DUP_COL		(1 << 3)
#define		SR_LED_MUX_DUP			(1 << 4)
#define		SR_LED_MUX_SPEED		(1 << 5)
#define		SR_LED_MUX_LINK_ACTIVE		(1 << 6)
#define		SR_LED_MUX_LINK			(1 << 7)

/* Register Access Flags */
#define SR_REQ_RD_REG   (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE)
#define SR_REQ_WR_REG   (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE)

/* Multicast Filter Array size & Max Number */
#define	SR_MCAST_FILTER_SIZE		8
#define	SR_MAX_MCAST			64

/* IPG0/1/2 Default Value */
#define	SR9800_IPG0_DEFAULT		0x15
#define	SR9800_IPG1_DEFAULT		0x0c
#define	SR9800_IPG2_DEFAULT		0x12

/* Medium Status Default Mode */
#define SR9800_MEDIUM_DEFAULT	\
	(SR_MEDIUM_FD | SR_MEDIUM_RFC | \
	 SR_MEDIUM_TFC | SR_MEDIUM_PS | \
	 SR_MEDIUM_AC | SR_MEDIUM_RE)

/* RX Control Default Setting */
#define SR_DEFAULT_RX_CTL	\
	(SR_RX_CTL_SO | SR_RX_CTL_AB | SR_RX_CTL_RH1M)

/* EEPROM Magic Number & EEPROM Size */
#define SR_EEPROM_MAGIC			0xdeadbeef
#define SR9800_EEPROM_LEN		0xff

/* SR9800 Driver Version and Driver Name */
#define DRIVER_VERSION			"11-Nov-2013"
#define DRIVER_NAME			"CoreChips"
#define	DRIVER_FLAG		\
	(FLAG_ETHER | FLAG_FRAMING_AX | FLAG_LINK_INTR |  FLAG_MULTI_PACKET)

/* SR9800 BULKIN Buffer Size */
#define SR9800_MAX_BULKIN_2K		0
#define SR9800_MAX_BULKIN_4K		1
#define SR9800_MAX_BULKIN_6K		2
#define SR9800_MAX_BULKIN_8K		3
#define SR9800_MAX_BULKIN_16K		4
#define SR9800_MAX_BULKIN_20K		5
#define SR9800_MAX_BULKIN_24K		6
#define SR9800_MAX_BULKIN_32K		7

struct {unsigned short size, byte_cnt, threshold; } SR9800_BULKIN_SIZE[] = {
	/* 2k */
	{2048, 0x8000, 0x8001},
	/* 4k */
	{4096, 0x8100, 0x8147},
	/* 6k */
	{6144, 0x8200, 0x81EB},
	/* 8k */
	{8192, 0x8300, 0x83D7},
	/* 16 */
	{16384, 0x8400, 0x851E},
	/* 20k */
	{20480, 0x8500, 0x8666},
	/* 24k */
	{24576, 0x8600, 0x87AE},
	/* 32k */
	{32768, 0x8700, 0x8A3D},
};

/* This structure cannot exceed sizeof(unsigned long [5]) AKA 20 bytes */
struct sr_data {
	u8 multi_filter[SR_MCAST_FILTER_SIZE];
	u8 mac_addr[ETH_ALEN];
	u8 phymode;
	u8 ledmode;
	u8 eeprom_len;
};

struct sr9800_int_data {
	__le16 res1;
	u8 link;
	__le16 res2;
	u8 status;
	__le16 res3;
} __packed;

#endif	/* _SR9800_H */
