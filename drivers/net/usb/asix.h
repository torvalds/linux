#ifndef	__LINUX_USBNET_ASIX_H
#define	__LINUX_USBNET_ASIX_H


#define AX88179_PHY_ID			0x03
#define AX_MCAST_FILTER_SIZE		8
#define AX_MAX_MCAST			64
#define AX_EEPROM_LEN			0x40
#define AX_RX_CHECKSUM			1
#define AX_TX_CHECKSUM			2

#define AX_BULKIN_24K			0x18;	/* 24k */

#define AX_ACCESS_MAC			0x01
#define AX_ACCESS_PHY			0x02
#define AX_ACCESS_WAKEUP		0x03
#define AX_ACCESS_EEPROM		0x04
#define AX_ACCESS_EFUSE			0x05
#define AX_RELOAD_EEPROM_EFUSE		0x06
#define AX_WRITE_EFUSE_EN		0x09
#define AX_WRITE_EFUSE_DIS		0x0A
#define AX_ACCESS_MFAB			0x10

#define PHYSICAL_LINK_STATUS		0x02
	#define	AX_USB_SS		0x04
	#define	AX_USB_HS		0x02
	#define	AX_USB_FS		0x01

#define GENERAL_STATUS			0x03
/* Check AX88179 version. UA1:Bit2 = 0,  UA2:Bit2 = 1 */
	#define	AX_SECLD		0x04



#define AX_SROM_ADDR			0x07
#define AX_SROM_CMD			0x0a
	#define EEP_RD			0x04	/* EEprom read command */
	#define EEP_WR			0x08	/* EEprom write command */
	#define EEP_BUSY		0x10	/* EEprom access module busy */


#define AX_SROM_DATA_LOW		0x08
#define AX_SROM_DATA_HIGH		0x09

#define AX_RX_CTL			0x0b
	#define AX_RX_CTL_DROPCRCERR		0x0100 /* Drop CRC error packet */
	#define AX_RX_CTL_IPE			0x0200 /* Enable IP header in receive buffer aligned on 32-bit aligment */
	#define AX_RX_CTL_TXPADCRC		0x0400 /* checksum value in rx header 3 */
	#define AX_RX_CTL_START			0x0080 /* Ethernet MAC start */
	#define AX_RX_CTL_AP			0x0020 /* Accept physcial address from Multicast array */
	#define AX_RX_CTL_AM			0x0010 /* Accetp Brocadcast frames*/
	#define AX_RX_CTL_AB			0x0008 /* HW auto-added 8-bytes data when meet USB bulk in transfer boundary (1024/512/64)*/
	#define AX_RX_CTL_HA8B			0x0004
	#define AX_RX_CTL_AMALL			0x0002 /* Accetp all multicast frames */
	#define AX_RX_CTL_PRO			0x0001 /* Promiscuous Mode */
	#define AX_RX_CTL_STOP			0x0000 /* Stop MAC */

#define AX_NODE_ID			0x10
#define AX_MULTI_FILTER_ARRY		0x16

#define AX_MEDIUM_STATUS_MODE			0x22
	#define AX_MEDIUM_GIGAMODE	0x01
	#define AX_MEDIUM_FULL_DUPLEX	0x02
	#define AX_MEDIUM_ALWAYS_ONE	0x04
	#define AX_MEDIUM_EN_125MHZ	0x08
	#define AX_MEDIUM_RXFLOW_CTRLEN	0x10
	#define AX_MEDIUM_TXFLOW_CTRLEN	0x20
	#define AX_MEDIUM_RECEIVE_EN	0x100
	#define AX_MEDIUM_PS		0x200
	#define AX_MEDIUM_JUMBO_EN	0x8040

#define AX_MONITOR_MODE			0x24
	#define AX_MONITOR_MODE_RWLC		0x02
	#define AX_MONITOR_MODE_RWMP		0x04
	#define AX_MONITOR_MODE_RWWF		0x08
	#define AX_MONITOR_MODE_RW_FLAG		0x10
	#define AX_MONITOR_MODE_PMEPOL		0x20
	#define AX_MONITOR_MODE_PMETYPE		0x40

#define AX_GPIO_CTRL			0x25
	#define AX_GPIO_CTRL_GPIO3EN		0x80
	#define AX_GPIO_CTRL_GPIO2EN		0x40
	#define AX_GPIO_CTRL_GPIO1EN		0x20

#define AX_PHYPWR_RSTCTL		0x26
	#define AX_PHYPWR_RSTCTL_BZ		0x0010
	#define AX_PHYPWR_RSTCTL_IPRL		0x0020
	#define AX_PHYPWR_RSTCTL_AUTODETACH	0x1000

#define AX_RX_BULKIN_QCTRL		0x2e
	#define AX_RX_BULKIN_QCTRL_TIME		0x01
	#define AX_RX_BULKIN_QCTRL_IFG		0x02
	#define AX_RX_BULKIN_QCTRL_SIZE		0x04

#define AX_RX_BULKIN_QTIMR_LOW		0x2f
#define AX_RX_BULKIN_QTIMR_HIGH			0x30
#define AX_RX_BULKIN_QSIZE			0x31
#define AX_RX_BULKIN_QIFG			0x32

#define AX_CLK_SELECT			0x33
	#define AX_CLK_SELECT_BCS		0x01
	#define AX_CLK_SELECT_ACS		0x02
	#define AX_CLK_SELECT_ACSREQ		0x10
	#define AX_CLK_SELECT_ULR		0x08

#define AX_RXCOE_CTL			0x34
	#define AX_RXCOE_IP			0x01
	#define AX_RXCOE_TCP			0x02
	#define AX_RXCOE_UDP			0x04
	#define AX_RXCOE_ICMP			0x08
	#define AX_RXCOE_IGMP			0x10
	#define AX_RXCOE_TCPV6			0x20
	#define AX_RXCOE_UDPV6			0x40
	#define AX_RXCOE_ICMV6			0x80

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22)
	#define AX_RXCOE_DEF_CSUM	(AX_RXCOE_IP	| AX_RXCOE_TCP  | \
					 AX_RXCOE_UDP	| AX_RXCOE_ICMV6 | \
					 AX_RXCOE_TCPV6	| AX_RXCOE_UDPV6)
#else
	#define AX_RXCOE_DEF_CSUM	(AX_RXCOE_IP	| AX_RXCOE_TCP | \
					 AX_RXCOE_UDP)
#endif

#define AX_TXCOE_CTL			0x35
	#define AX_TXCOE_IP			0x01
	#define AX_TXCOE_TCP			0x02
	#define AX_TXCOE_UDP			0x04
	#define AX_TXCOE_ICMP			0x08
	#define AX_TXCOE_IGMP			0x10
	#define AX_TXCOE_TCPV6			0x20
	#define AX_TXCOE_UDPV6			0x40
	#define AX_TXCOE_ICMV6			0x80
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22)
	#define AX_TXCOE_DEF_CSUM	(AX_TXCOE_TCP   | AX_TXCOE_UDP | \
					 AX_TXCOE_TCPV6 | AX_TXCOE_UDPV6)
#else
	#define AX_TXCOE_DEF_CSUM	(AX_TXCOE_TCP	| AX_TXCOE_UDP)
#endif

#define AX_PAUSE_WATERLVL_HIGH		0x54
#define AX_PAUSE_WATERLVL_LOW		0x55


#define AX_EEP_EFUSE_CORRECT		0x00
#define AX88179_EEPROM_MAGIC			0x17900b95


/*****************************************************************************/
/* GMII register definitions */
#define GMII_PHY_CONTROL			0x00	/* control reg */
	/* Bit definitions: GMII Control */
	#define GMII_CONTROL_RESET		0x8000	/* reset bit in control reg */
	#define GMII_CONTROL_LOOPBACK		0x4000	/* loopback bit in control reg */
	#define GMII_CONTROL_10MB		0x0000	/* 10 Mbit */
	#define GMII_CONTROL_100MB		0x2000	/* 100Mbit */
	#define GMII_CONTROL_1000MB		0x0040	/* 1000Mbit */
	#define GMII_CONTROL_SPEED_BITS		0x2040	/* speed bit mask */
	#define GMII_CONTROL_ENABLE_AUTO	0x1000	/* autonegotiate enable */
	#define GMII_CONTROL_POWER_DOWN		0x0800
	#define GMII_CONTROL_ISOLATE		0x0400	/* islolate bit */
	#define GMII_CONTROL_START_AUTO		0x0200	/* restart autonegotiate */
	#define GMII_CONTROL_FULL_DUPLEX	0x0100

#define GMII_PHY_STATUS				0x01	/* status reg */
	/* Bit definitions: GMII Status */
	#define GMII_STATUS_100MB_MASK		0xE000	/* any of these indicate 100 Mbit */
	#define GMII_STATUS_10MB_MASK		0x1800	/* either of these indicate 10 Mbit */
	#define GMII_STATUS_AUTO_DONE		0x0020	/* auto negotiation complete */
	#define GMII_STATUS_AUTO		0x0008	/* auto negotiation is available */
	#define GMII_STATUS_LINK_UP		0x0004	/* link status bit */
	#define GMII_STATUS_EXTENDED		0x0001	/* extended regs exist */
	#define GMII_STATUS_100T4		0x8000	/* capable of 100BT4 */
	#define GMII_STATUS_100TXFD		0x4000	/* capable of 100BTX full duplex */
	#define GMII_STATUS_100TX		0x2000	/* capable of 100BTX */
	#define GMII_STATUS_10TFD		0x1000	/* capable of 10BT full duplex */
	#define GMII_STATUS_10T			0x0800	/* capable of 10BT */

#define GMII_PHY_OUI				0x02	/* most of the OUI bits */
#define GMII_PHY_MODEL				0x03	/* model/rev bits, and rest of OUI */
#define GMII_PHY_ANAR				0x04	/* AN advertisement reg */
	/* Bit definitions: Auto-Negotiation Advertisement */
	#define GMII_ANAR_ASYM_PAUSE		0x0800	/* support asymetric pause */
	#define GMII_ANAR_PAUSE			0x0400	/* support pause packets */
	#define GMII_ANAR_100T4			0x0200	/* support 100BT4 */
	#define GMII_ANAR_100TXFD		0x0100	/* support 100BTX full duplex */
	#define GMII_ANAR_100TX			0x0080	/* support 100BTX half duplex */
	#define GMII_ANAR_10TFD			0x0040	/* support 10BT full duplex */
	#define GMII_ANAR_10T			0x0020	/* support 10BT half duplex */
	#define GMII_SELECTOR_FIELD		0x001F	/* selector field. */

#define GMII_PHY_ANLPAR				0x05	/* AN Link Partner */
	/* Bit definitions: Auto-Negotiation Link Partner Ability */
	#define GMII_ANLPAR_100T4		0x0200	/* support 100BT4 */
	#define GMII_ANLPAR_100TXFD		0x0100	/* support 100BTX full duplex */
	#define GMII_ANLPAR_100TX		0x0080	/* support 100BTX half duplex */
	#define GMII_ANLPAR_10TFD		0x0040	/* support 10BT full duplex */
	#define GMII_ANLPAR_10T			0x0020	/* support 10BT half duplex */
	#define GMII_ANLPAR_PAUSE		0x0400	/* support pause packets */
	#define GMII_ANLPAR_ASYM_PAUSE		0x0800	/* support asymetric pause */
	#define GMII_ANLPAR_ACK			0x4000	/* means LCB was successfully rx'd */
	#define GMII_SELECTOR_8023		0x0001;

#define GMII_PHY_ANER				0x06	/* AN expansion reg */
#define GMII_PHY_1000BT_CONTROL			0x09	/* control reg for 1000BT */
#define GMII_PHY_1000BT_STATUS			0x0A	/* status reg for 1000BT */

#define GMII_PHY_PHYSR				0x11	/* PHY specific status register */
	#define GMII_PHY_PHYSR_SMASK		0xc000
	#define GMII_PHY_PHYSR_GIGA		0x8000
	#define GMII_PHY_PHYSR_100		0x4000
	#define GMII_PHY_PHYSR_FULL		0x2000
	#define GMII_PHY_PHYSR_LINK		0x400

/* Bit definitions: 1000BaseT AUX Control */
#define GMII_1000_AUX_CTRL_MASTER_SLAVE		0x1000
#define GMII_1000_AUX_CTRL_FD_CAPABLE		0x0200	/* full duplex capable */
#define GMII_1000_AUX_CTRL_HD_CAPABLE		0x0100	/* half duplex capable */

/* Bit definitions: 1000BaseT AUX Status */
#define GMII_1000_AUX_STATUS_FD_CAPABLE		0x0800	/* full duplex capable */
#define GMII_1000_AUX_STATUS_HD_CAPABLE		0x0400	/* half duplex capable */

/*Cicada MII Registers */
#define GMII_AUX_CTRL_STATUS			0x1C
#define GMII_AUX_ANEG_CPLT			0x8000
#define GMII_AUX_FDX				0x0020
#define GMII_AUX_SPEED_1000			0x0010
#define GMII_AUX_SPEED_100			0x0008

#define GMII_LED_ACTIVE				0x1a
	#define GMII_LED_ACTIVE_MASK		0xff8f
	#define GMII_LED0_ACTIVE		(1 << 4)
	#define GMII_LED1_ACTIVE		(1 << 5)
	#define GMII_LED2_ACTIVE		(1 << 6)

#define GMII_LED_LINK				0x1c
	#define GMII_LED_LINK_MASK		0xf888
	#define GMII_LED0_LINK_10		(1 << 0)
	#define GMII_LED0_LINK_100		(1 << 1)
	#define GMII_LED0_LINK_1000		(1 << 2)
	#define GMII_LED1_LINK_10		(1 << 4)
	#define GMII_LED1_LINK_100		(1 << 5)
	#define GMII_LED1_LINK_1000		(1 << 6)
	#define GMII_LED2_LINK_10		(1 << 8)
	#define GMII_LED2_LINK_100		(1 << 9)
	#define GMII_LED2_LINK_1000		(1 << 10)

	#define	LED_VALID	(1 << 15) /* UA2 LED Setting */

	#define	LED0_ACTIVE	(1 << 0)
	#define	LED0_LINK_10	(1 << 1)
	#define	LED0_LINK_100	(1 << 2)
	#define	LED0_LINK_1000	(1 << 3)
	#define	LED0_FD		(1 << 4)
	#define LED0_USB3_MASK	0x001f

	#define	LED1_ACTIVE	(1 << 5)
	#define	LED1_LINK_10	(1 << 6)
	#define	LED1_LINK_100	(1 << 7)
	#define	LED1_LINK_1000	(1 << 8)
	#define	LED1_FD		(1 << 9)
	#define LED1_USB3_MASK	0x03e0

	#define	LED2_ACTIVE	(1 << 10)
	#define	LED2_LINK_1000	(1 << 13)
	#define	LED2_LINK_100	(1 << 12)
	#define	LED2_LINK_10	(1 << 11)
	#define	LED2_FD		(1 << 14)
	#define LED2_USB3_MASK	0x7c00

#define GMII_PHYPAGE				0x1e

#define GMII_PHY_PAGE_SELECT			0x1f
	#define GMII_PHY_PAGE_SELECT_EXT	0x0007
	#define GMII_PHY_PAGE_SELECT_PAGE0	0X0000
	#define GMII_PHY_PAGE_SELECT_PAGE1	0X0001
	#define GMII_PHY_PAGE_SELECT_PAGE2	0X0002
	#define GMII_PHY_PAGE_SELECT_PAGE3	0X0003
	#define GMII_PHY_PAGE_SELECT_PAGE4	0X0004
	#define GMII_PHY_PAGE_SELECT_PAGE5	0X0005
	#define GMII_PHY_PAGE_SELECT_PAGE6	0X0006
/******************************************************************************/

struct ax88179_data {
	u16 rxctl;
	u8  checksum;
} __attribute__ ((packed));

struct ax88179_int_data {
	__le16 res1;
#define AX_INT_PPLS_LINK	(1 << 0)
#define AX_INT_SPLS_LINK	(1 << 1)
#define AX_INT_CABOFF_UNPLUG	(1 << 7)
	u8 link;
	__le16 res2;
	u8 status;
	__le16 res3;
} __attribute__ ((packed));

#define AX_RXHDR_L4_ERR		(1 << 8)
#define AX_RXHDR_L3_ERR		(1 << 9)


#define AX_RXHDR_L4_TYPE_ICMP		2
#define AX_RXHDR_L4_TYPE_IGMP		3
#define AX_RXHDR_L4_TYPE_TCMPV6		5

#define AX_RXHDR_L3_TYPE_IP		1
#define AX_RXHDR_L3_TYPE_IPV6		2

#define AX_RXHDR_L4_TYPE_MASK			0x1c
#define AX_RXHDR_L4_TYPE_UDP			4
#define AX_RXHDR_L4_TYPE_TCP			16
#define AX_RXHDR_L3CSUM_ERR			2
#define AX_RXHDR_L4CSUM_ERR			1
#define AX_RXHDR_CRC_ERR			0x80000000
#define AX_RXHDR_DROP_ERR			0x40000000
#if 0
struct ax88179_rx_pkt_header {

	u8	l4_csum_err:1,
		l3_csum_err:1,
		l4_type:3,
		l3_type:2,
		ce:1;

	u8	vlan_ind:3,
		rx_ok:1,
		pri:3,
		bmc:1;

	u16	len:13,
		crc:1,
		mii:1,
		drop:1;

} __attribute__ ((packed));
#endif
static struct {unsigned char ctrl, timer_l, timer_h, size, ifg; }
AX88179_BULKIN_SIZE[] =	{
	{7, 0x4f, 0,	0x12, 0xff},
	{7, 0x20, 3,	0x16, 0xff},
	{7, 0xae, 7,	0x18, 0xff},
	{7, 0xcc, 0x4c, 0x18, 8},
};

static int ax88179_reset(struct usbnet *dev);
static int ax88179_link_reset(struct usbnet *dev);
static int ax88179_AutoDetach(struct usbnet *dev, int in_pm);

#endif /* __LINUX_USBNET_ASIX_H */

