#ifndef	__LINUX_USBNET_ASIX_H
#define	__LINUX_USBNET_ASIX_H

/*
 * Turn on this flag if the implementation of your USB host controller
 * cannot handle non-double word aligned buffer.
 * When turn on this flag, driver will fixup egress packet aligned on double
 * word boundary before deliver to USB host controller. And will Disable the
 * function "skb_reserve (skb, NET_IP_ALIGN)" to retain the buffer aligned on
 * double word alignment for ingress packets.
 */
#define AX_FORCE_BUFF_ALIGN		1

#define AX_MONITOR_MODE			0x01
#define AX_MONITOR_LINK			0x02
#define AX_MONITOR_MAGIC		0x04
#define AX_MONITOR_HSFS			0x10

/* AX88172 Medium Status Register values */
#define AX_MEDIUM_FULL_DUPLEX		0x02
#define AX_MEDIUM_TX_ABORT_ALLOW	0x04
#define AX_MEDIUM_FLOW_CONTROL_EN	0x10
#define AX_MCAST_FILTER_SIZE		8
#define AX_MAX_MCAST			64

#define AX_EEPROM_LEN			0x40

#define AX_SWRESET_CLEAR		0x00
#define AX_SWRESET_RR			0x01
#define AX_SWRESET_RT			0x02
#define AX_SWRESET_PRTE			0x04
#define AX_SWRESET_PRL			0x08
#define AX_SWRESET_BZ			0x10
#define AX_SWRESET_IPRL			0x20
#define AX_SWRESET_IPPD			0x40
#define AX_SWRESET_IPOSC		0x0080
#define AX_SWRESET_IPPSL_0		0x0100
#define AX_SWRESET_IPPSL_1		0x0200
#define AX_SWRESET_IPCOPS		0x0400
#define AX_SWRESET_IPCOPSC		0x0800
#define AX_SWRESET_AUTODETACH		0x1000
#define AX_SWRESET_WOLLP		0x8000

#define AX88772_IPG0_DEFAULT		0x15
#define AX88772_IPG1_DEFAULT		0x0c
#define AX88772_IPG2_DEFAULT		0x0E

#define AX88772A_IPG0_DEFAULT		0x15
#define AX88772A_IPG1_DEFAULT		0x16
#define AX88772A_IPG2_DEFAULT		0x1A

#define AX88772_MEDIUM_FULL_DUPLEX	0x0002
#define AX88772_MEDIUM_RESERVED		0x0004
#define AX88772_MEDIUM_RX_FC_ENABLE	0x0010
#define AX88772_MEDIUM_TX_FC_ENABLE	0x0020
#define AX88772_MEDIUM_PAUSE_FORMAT	0x0080
#define AX88772_MEDIUM_RX_ENABLE	0x0100
#define AX88772_MEDIUM_100MB		0x0200
#define AX88772_MEDIUM_DEFAULT	\
	(AX88772_MEDIUM_FULL_DUPLEX | AX88772_MEDIUM_RX_FC_ENABLE | \
	 AX88772_MEDIUM_TX_FC_ENABLE | AX88772_MEDIUM_100MB | \
	 AX88772_MEDIUM_RESERVED | AX88772_MEDIUM_RX_ENABLE )

#define AX_CMD_SET_SW_MII		0x06
#define AX_CMD_READ_MII_REG		0x07
#define AX_CMD_WRITE_MII_REG		0x08
#define AX_CMD_SET_HW_MII		0x0a
#define AX_CMD_READ_EEPROM		0x0b
#define AX_CMD_WRITE_EEPROM		0x0c
#define AX_CMD_WRITE_EEPROM_EN		0x0d
#define AX_CMD_WRITE_EEPROM_DIS		0x0e
#define AX_CMD_WRITE_RX_CTL		0x10
#define AX_CMD_READ_IPG012		0x11
#define AX_CMD_WRITE_IPG0		0x12
#define AX_CMD_WRITE_IPG1		0x13
#define AX_CMD_WRITE_IPG2		0x14
#define AX_CMD_WRITE_MULTI_FILTER	0x16
#define AX_CMD_READ_NODE_ID		0x17
#define AX_CMD_READ_PHY_ID		0x19
#define AX_CMD_READ_MEDIUM_MODE		0x1a
#define AX_CMD_WRITE_MEDIUM_MODE	0x1b
#define AX_CMD_READ_MONITOR_MODE	0x1c
#define AX_CMD_WRITE_MONITOR_MODE	0x1d
#define AX_CMD_WRITE_GPIOS		0x1f
#define AX_CMD_SW_RESET 		0x20
#define AX_CMD_SW_PHY_STATUS		0x21
#define AX_CMD_SW_PHY_SELECT		0x22
	#define AX_PHYSEL_PSEL		(1 << 0)
	#define AX_PHYSEL_ASEL		(1 << 1)
	#define AX_PHYSEL_SSMII		(1 << 2)
	#define AX_PHYSEL_SSRMII	(2 << 2)
	#define AX_PHYSEL_SSRRMII	(3 << 2)
	#define AX_PHYSEL_SSEN		(1 << 4)
#define AX88772_CMD_READ_NODE_ID	0x13
#define AX88772_CMD_WRITE_NODE_ID	0x14
#define AX_CMD_READ_RXCOE_CTL		0x2b
#define AX_CMD_WRITE_RXCOE_CTL		0x2c
#define AX_CMD_READ_TXCOE_CTL		0x2d
#define AX_CMD_WRITE_TXCOE_CTL		0x2e

#define REG_LENGTH			2
#define PHY_ID_MASK			0x1f

#define AX_RXCOE_IPCE			0x0001
#define AX_RXCOE_IPVE			0x0002
#define AX_RXCOE_V6VE			0x0004
#define AX_RXCOE_TCPE			0x0008
#define AX_RXCOE_UDPE			0x0010
#define AX_RXCOE_ICMP			0x0020
#define AX_RXCOE_IGMP			0x0040
#define AX_RXCOE_ICV6			0x0080
#define AX_RXCOE_TCPV6			0x0100
#define AX_RXCOE_UDPV6			0x0200
#define AX_RXCOE_ICMV6			0x0400
#define AX_RXCOE_IGMV6			0x0800
#define AX_RXCOE_ICV6V6			0x1000
#define AX_RXCOE_FOPC			0x8000
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
#define AX_RXCOE_DEF_CSUM		(AX_RXCOE_IPCE | AX_RXCOE_IPVE | \
					 AX_RXCOE_V6VE | AX_RXCOE_TCPE | \
					 AX_RXCOE_UDPE |  AX_RXCOE_ICV6 | \
					 AX_RXCOE_TCPV6 | AX_RXCOE_UDPV6)
#else
#define AX_RXCOE_DEF_CSUM		(AX_RXCOE_IPCE | AX_RXCOE_IPVE | \
					 AX_RXCOE_TCPE | AX_RXCOE_UDPE)
#endif

#define AX_RXCOE_64TE			0x0100
#define AX_RXCOE_PPPOE			0x0200
#define AX_RXCOE_RPCE			0x8000

#define AX_TXCOE_IP			0x0001
#define AX_TXCOE_TCP			0x0002
#define AX_TXCOE_UDP			0x0004
#define AX_TXCOE_ICMP			0x0008
#define AX_TXCOE_IGMP			0x0010
#define AX_TXCOE_ICV6			0x0020

#define AX_TXCOE_TCPV6			0x0100
#define AX_TXCOE_UDPV6			0x0200
#define AX_TXCOE_ICMV6			0x0400
#define AX_TXCOE_IGMV6			0x0800
#define AX_TXCOE_ICV6V6			0x1000
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
#define AX_TXCOE_DEF_CSUM		(AX_TXCOE_TCP | AX_TXCOE_UDP | \
					 AX_TXCOE_TCPV6 | AX_TXCOE_UDPV6)
#else
#define AX_TXCOE_DEF_CSUM		(AX_TXCOE_TCP | AX_TXCOE_UDP)
#endif

#define AX_TXCOE_64TE			0x0001
#define AX_TXCOE_PPPE			0x0002

#define AX88772B_MAX_BULKIN_2K		0
#define AX88772B_MAX_BULKIN_4K		1
#define AX88772B_MAX_BULKIN_6K		2
#define AX88772B_MAX_BULKIN_8K		3
#define AX88772B_MAX_BULKIN_16K		4
#define AX88772B_MAX_BULKIN_20K		5
#define AX88772B_MAX_BULKIN_24K		6
#define AX88772B_MAX_BULKIN_32K		7
struct {unsigned short size, byte_cnt,threshold;} AX88772B_BULKIN_SIZE[] =
{
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


#define AX_RX_CTL_RH1M			0x0100		/* Enable RX-Header mode 0 */
#define AX_RX_CTL_RH2M			0x0200		/* Enable IP header in receive buffer aligned on 32-bit aligment */
#define AX_RX_CTL_RH3M			0x0400		/* checksum value in rx header 3 */
#define AX_RX_HEADER_DEFAULT		(AX_RX_CTL_RH1M | AX_RX_CTL_RH2M)

#define AX_RX_CTL_MFB			0x0300		/* Maximum Frame size 16384bytes */
#define AX_RX_CTL_START			0x0080		/* Ethernet MAC start */
#define AX_RX_CTL_AP			0x0020		/* Accept physcial address from Multicast array */
#define AX_RX_CTL_AM			0x0010	
#define AX_RX_CTL_AB			0x0008		/* Accetp Brocadcast frames*/
#define AX_RX_CTL_SEP			0x0004		/* Save error packets */	
#define AX_RX_CTL_AMALL			0x0002		/* Accetp all multicast frames */
#define AX_RX_CTL_PRO			0x0001		/* Promiscuous Mode */
#define AX_RX_CTL_STOP			0x0000		/* Stop MAC */

#define AX_MONITOR_MODE 		0x01
#define AX_MONITOR_LINK 		0x02
#define AX_MONITOR_MAGIC		0x04
#define AX_MONITOR_HSFS 		0x10

#define AX_MCAST_FILTER_SIZE		8
#define AX_MAX_MCAST			64
#define AX_INTERRUPT_BUFSIZE		8

#define AX_EEPROM_LEN			0x40
#define AX_EEPROM_MAGIC 		0xdeadbeef
#define EEPROMMASK			0x7f

/* GPIO REGISTER */
#define AXGPIOS_GPO0EN			0X01 // 1 << 0
#define AXGPIOS_GPO0			0X02 // 1 << 1
#define AXGPIOS_GPO1EN			0X04 // 1 << 2
#define AXGPIOS_GPO1			0X08 // 1 << 3
#define AXGPIOS_GPO2EN			0X10 // 1 << 4
#define AXGPIOS_GPO2			0X20 // 1 << 5
#define AXGPIOS_RSE			0X80 // 1 << 7

/* TX-header format */
#define AX_TX_HDR_CPHI			0x4000
#define AX_TX_HDR_DICF			0x8000

// GMII register definitions
#define GMII_PHY_CONTROL		0x00	// control reg
#define GMII_PHY_STATUS			0x01	// status reg
#define GMII_PHY_OUI			0x02	// most of the OUI bits
#define GMII_PHY_MODEL			0x03	// model/rev bits, and rest of OUI
#define GMII_PHY_ANAR			0x04	// AN advertisement reg
#define GMII_PHY_ANLPAR			0x05	// AN Link Partner
#define GMII_PHY_ANER			0x06	// AN expansion reg
#define GMII_PHY_1000BT_CONTROL		0x09	// control reg for 1000BT
#define GMII_PHY_1000BT_STATUS		0x0A	// status reg for 1000BT

// Bit definitions: GMII Control
#define GMII_CONTROL_RESET		0x8000	// reset bit in control reg
#define GMII_CONTROL_LOOPBACK		0x4000	// loopback bit in control reg
#define GMII_CONTROL_10MB		0x0000	// 10 Mbit
#define GMII_CONTROL_100MB		0x2000	// 100Mbit
#define GMII_CONTROL_1000MB		0x0040	// 1000Mbit
#define GMII_CONTROL_SPEED_BITS		0x2040	// speed bit mask
#define GMII_CONTROL_ENABLE_AUTO	0x1000	// autonegotiate enable
#define GMII_CONTROL_POWER_DOWN		0x0800
#define GMII_CONTROL_ISOLATE		0x0400	// islolate bit
#define GMII_CONTROL_START_AUTO		0x0200	// restart autonegotiate
#define GMII_CONTROL_FULL_DUPLEX	0x0100

// Bit definitions: GMII Status
#define GMII_STATUS_100MB_MASK		0xE000	// any of these indicate 100 Mbit
#define GMII_STATUS_10MB_MASK		0x1800	// either of these indicate 10 Mbit
#define GMII_STATUS_AUTO_DONE		0x0020	// auto negotiation complete
#define GMII_STATUS_AUTO		0x0008	// auto negotiation is available
#define GMII_STATUS_LINK_UP		0x0004	// link status bit
#define GMII_STATUS_EXTENDED		0x0001	// extended regs exist
#define GMII_STATUS_100T4		0x8000	// capable of 100BT4
#define GMII_STATUS_100TXFD		0x4000	// capable of 100BTX full duplex
#define GMII_STATUS_100TX		0x2000	// capable of 100BTX
#define GMII_STATUS_10TFD		0x1000	// capable of 10BT full duplex
#define GMII_STATUS_10T			0x0800	// capable of 10BT

// Bit definitions: Auto-Negotiation Advertisement
#define GMII_ANAR_ASYM_PAUSE		0x0800	// support asymetric pause
#define GMII_ANAR_PAUSE			0x0400	// support pause packets
#define GMII_ANAR_100T4			0x0200	// support 100BT4
#define GMII_ANAR_100TXFD		0x0100	// support 100BTX full duplex
#define GMII_ANAR_100TX			0x0080	// support 100BTX half duplex
#define GMII_ANAR_10TFD			0x0040	// support 10BT full duplex
#define GMII_ANAR_10T			0x0020	// support 10BT half duplex
#define GMII_SELECTOR_FIELD		0x001F	// selector field.

// Bit definitions: Auto-Negotiation Link Partner Ability
#define GMII_ANLPAR_100T4		0x0200	// support 100BT4
#define GMII_ANLPAR_100TXFD		0x0100	// support 100BTX full duplex
#define GMII_ANLPAR_100TX		0x0080	// support 100BTX half duplex
#define GMII_ANLPAR_10TFD		0x0040	// support 10BT full duplex
#define GMII_ANLPAR_10T			0x0020	// support 10BT half duplex
#define GMII_ANLPAR_PAUSE		0x0400	// support pause packets
#define GMII_ANLPAR_ASYM_PAUSE		0x0800	// support asymetric pause
#define GMII_ANLPAR_ACK			0x4000	// means LCB was successfully rx'd
#define GMII_SELECTOR_8023		0x0001;

// Bit definitions: 1000BaseT AUX Control
#define GMII_1000_AUX_CTRL_MASTER_SLAVE		0x1000
#define GMII_1000_AUX_CTRL_FD_CAPABLE		0x0200	// full duplex capable
#define GMII_1000_AUX_CTRL_HD_CAPABLE		0x0100	// half duplex capable

// Bit definitions: 1000BaseT AUX Status
#define GMII_1000_AUX_STATUS_FD_CAPABLE		0x0800	// full duplex capable
#define GMII_1000_AUX_STATUS_HD_CAPABLE 	0x0400	// half duplex capable

// Cicada MII Registers
#define GMII_AUX_CTRL_STATUS			0x1C
#define GMII_AUX_ANEG_CPLT			0x8000
#define GMII_AUX_FDX				0x0020
#define GMII_AUX_SPEED_1000			0x0010
#define GMII_AUX_SPEED_100			0x0008

#ifndef ADVERTISE_PAUSE_CAP
#define ADVERTISE_PAUSE_CAP			0x0400
#endif

#ifndef MII_STAT1000
#define MII_STAT1000				0x000A
#endif

#ifndef LPA_1000FULL
#define LPA_1000FULL				0x0800
#endif

// medium mode register
#define MEDIUM_GIGA_MODE			0x0001
#define MEDIUM_FULL_DUPLEX_MODE			0x0002
#define MEDIUM_TX_ABORT_MODE			0x0004
#define MEDIUM_ENABLE_125MHZ			0x0008
#define MEDIUM_ENABLE_RX_FLOWCTRL		0x0010
#define MEDIUM_ENABLE_TX_FLOWCTRL		0x0020
#define MEDIUM_ENABLE_JUMBO_FRAME		0x0040
#define MEDIUM_CHECK_PAUSE_FRAME_MODE		0x0080
#define MEDIUM_ENABLE_RECEIVE			0x0100
#define MEDIUM_MII_100M_MODE			0x0200
#define MEDIUM_ENABLE_JAM_PATTERN		0x0400
#define MEDIUM_ENABLE_STOP_BACKPRESSURE		0x0800
#define MEDIUM_ENABLE_SUPPER_MAC_SUPPORT	0x1000

/* PHY mode */
#define PHY_MODE_MARVELL		0
#define PHY_MODE_CICADA_FAMILY		1
#define PHY_MODE_CICADA_V1		1
#define PHY_MODE_AGERE_FAMILY		2
#define PHY_MODE_AGERE_V0		2
#define PHY_MODE_CICADA_V2		5
#define PHY_MODE_AGERE_V0_GMII		6
#define PHY_MODE_CICADA_V2_ASIX		9
#define PHY_MODE_VSC8601		10
#define PHY_MODE_RTL8211CL		12
#define PHY_MODE_RTL8211BN		13
#define PHY_MODE_RTL8251CL		14
#define PHY_MODE_ATTANSIC_V0		0x40
#define PHY_MODE_ATTANSIC_FAMILY	0x40
#define PHY_MODE_MAC_TO_MAC_GMII	0x7C

/*  */
#define LED_MODE_MARVELL		0
#define LED_MODE_CAMEO			1

#define MARVELL_LED_CTRL		0x18
#define MARVELL_MANUAL_LED		0x19

#define PHY_IDENTIFIER			0x0002
#define PHY_AGERE_IDENTIFIER		0x0282
#define PHY_CICADA_IDENTIFIER		0x000f
#define PHY_MARVELL_IDENTIFIER		0x0141

#define PHY_MARVELL_STATUS		0x001b
#define MARVELL_STATUS_HWCFG		0x0004		/* SGMII without clock */

#define PHY_MARVELL_CTRL		0x0014
#define MARVELL_CTRL_RXDELAY		0x0080
#define MARVELL_CTRL_TXDELAY		0x0002

#define PHY_CICADA_EXTPAGE		0x001f
#define CICADA_EXTPAGE_EN		0x0001
#define CICADA_EXTPAGE_DIS		0x0000


struct {unsigned short value, offset; } CICADA_FAMILY_HWINIT[] =
{
	{0x0001, 0x001f}, {0x1c25, 0x0017}, {0x2a30, 0x001f}, {0x234c, 0x0010},
	{0x2a30, 0x001f}, {0x0212, 0x0008}, {0x52b5, 0x001f}, {0xa7fa, 0x0000},
	{0x0012, 0x0002}, {0x3002, 0x0001}, {0x87fa, 0x0000}, {0x52b5, 0x001f},
	{0xafac, 0x0000}, {0x000d, 0x0002}, {0x001c, 0x0001}, {0x8fac, 0x0000},
	{0x2a30, 0x001f}, {0x0012, 0x0008}, {0x2a30, 0x001f}, {0x0400, 0x0014},
	{0x2a30, 0x001f}, {0x0212, 0x0008}, {0x52b5, 0x001f}, {0xa760, 0x0000},
	{0x0000, 0x0002}, {0xfaff, 0x0001}, {0x8760, 0x0000}, {0x52b5, 0x001f},
	{0xa760, 0x0000}, {0x0000, 0x0002}, {0xfaff, 0x0001}, {0x8760, 0x0000},
	{0x52b5, 0x001f}, {0xafae, 0x0000}, {0x0004, 0x0002}, {0x0671, 0x0001},
	{0x8fae, 0x0000}, {0x2a30, 0x001f}, {0x0012, 0x0008}, {0x0000, 0x001f},
};

struct {unsigned short value, offset; } CICADA_V2_HWINIT[] =
{
	{0x2a30, 0x001f}, {0x0212, 0x0008}, {0x52b5, 0x001f}, {0x000f, 0x0002},
	{0x472a, 0x0001}, {0x8fa4, 0x0000}, {0x2a30, 0x001f}, {0x0212, 0x0008},
	{0x0000, 0x001f},
};

struct {unsigned short value, offset; } CICADA_V2_ASIX_HWINIT[] =
{
	{0x2a30, 0x001f}, {0x0212, 0x0008}, {0x52b5, 0x001f}, {0x0012, 0x0002},
	{0x3002, 0x0001}, {0x87fa, 0x0000}, {0x52b5, 0x001f}, {0x000f, 0x0002},
	{0x472a, 0x0001}, {0x8fa4, 0x0000}, {0x2a30, 0x001f}, {0x0212, 0x0008},
	{0x0000, 0x001f},
};

struct {unsigned short value, offset; } AGERE_FAMILY_HWINIT[] =
{
	{0x0800, 0x0000}, {0x0007, 0x0012}, {0x8805, 0x0010}, {0xb03e, 0x0011},
	{0x8808, 0x0010}, {0xe110, 0x0011}, {0x8806, 0x0010}, {0xb03e, 0x0011},
	{0x8807, 0x0010}, {0xff00, 0x0011}, {0x880e, 0x0010}, {0xb4d3, 0x0011},
	{0x880f, 0x0010}, {0xb4d3, 0x0011}, {0x8810, 0x0010}, {0xb4d3, 0x0011},
	{0x8817, 0x0010}, {0x1c00, 0x0011}, {0x300d, 0x0010}, {0x0001, 0x0011},
	{0x0002, 0x0012},
};

struct ax88178_data {
	u16	EepromData;
	u16	MediaLink;
	int	UseGpio0;
	int	UseRgmii;
	u8	PhyMode;
	u8	LedMode;
	u8	BuffaloOld;
};

enum watchdog_state {
	AX_NOP = 0,
	CHK_LINK,			/* Routine A */
	CHK_CABLE_EXIST,		/* Called by A */
	CHK_CABLE_EXIST_AGAIN,		/* Routine B */
	PHY_POWER_UP,			/* Called by B */
	PHY_POWER_UP_BH,
	PHY_POWER_DOWN,
	CHK_CABLE_STATUS,		/* Routine C */
	WAIT_AUTONEG_COMPLETE,
	AX_SET_RX_CFG,
};

struct ax88772b_data {
	struct usbnet *dev;
	struct workqueue_struct *ax_work;
	struct work_struct check_link;
	unsigned long time_to_chk;
	u16 psc;
	u8 pw_enabled;
	u8 Event;
	u8 checksum;
};

struct ax88772a_data {
	struct usbnet *dev;
	struct workqueue_struct *ax_work;
	struct work_struct check_link;
	unsigned long autoneg_start;
#define AX88772B_WATCHDOG	(6 * HZ)
	u8 Event;
	u8 TickToExpire;
	u8 DlyIndex;
	u8 DlySel;
	u16 EepromData;
};

struct ax88772_data {
	struct usbnet *dev;
	struct workqueue_struct *ax_work;
	struct work_struct check_link;
	unsigned long autoneg_start;
	u8 Event;
	u8 TickToExpire;
};

#define AX_RX_CHECKSUM		1
#define AX_TX_CHECKSUM		2

/* This structure cannot exceed sizeof(unsigned long [5]) AKA 20 bytes */
struct ax8817x_data {
	u8 multi_filter[AX_MCAST_FILTER_SIZE];
	int (*resume) (struct usb_interface *intf);
	int (*suspend) (struct usb_interface *intf,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,10)
					pm_message_t message);
#else
					u32 message);
#endif
};

struct ax88172_int_data {
	u16 res1;
#define AX_INT_PPLS_LINK		(1 << 0)
#define AX_INT_SPLS_LINK		(1 << 1)
#define AX_INT_CABOFF_UNPLUG		(1 << 7)
	u8 link;
	u16 res2;
	u8 status;
	u16 res3;
} __attribute__ ((packed));

#define AX_RXHDR_L4_ERR		(1 << 8)
#define AX_RXHDR_L3_ERR		(1 << 9)

#define AX_RXHDR_L4_TYPE_UDP		1
#define AX_RXHDR_L4_TYPE_ICMP		2
#define AX_RXHDR_L4_TYPE_IGMP		3
#define AX_RXHDR_L4_TYPE_TCP		4
#define AX_RXHDR_L4_TYPE_TCMPV6	5
#define AX_RXHDR_L4_TYPE_MASK		7

#define AX_RXHDR_L3_TYPE_IP		1
#define AX_RXHDR_L3_TYPE_IPV6		2

struct ax88772b_rx_header {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u16	len:11,
		res1:1,
		crc:1,
		mii:1,
		runt:1,
		mc_bc:1;

	u16	len_bar:11,
		res2:5;

	u8	vlan_ind:3,
		vlan_tag_striped:1,
		pri:3,
		res3:1;

	u8	l4_csum_err:1,
		l3_csum_err:1,
		l4_type:3,
		l3_type:2,
		ce:1;
#elif defined (__BIG_ENDIAN_BITFIELD)
	u16	mc_bc:1,
		runt:1,
		mii:1,
		crc:1,
		res1:1,
		len:11;

	u16	res2:5,
		len_bar:11;

	u8	res3:1,
		pri:3,
		vlan_tag_striped:1,
		vlan_ind:3;

	u8	ce:1,
		l3_type:2,
		l4_type:3,
		l3_csum_err:1,
		l4_csum_err:1;
#else
#error	"Please fix <asm/byteorder.h>"
#endif

} __attribute__ ((packed));

#endif /* __LINUX_USBNET_ASIX_H */

