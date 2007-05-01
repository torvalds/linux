/*
=========================================================================
 r8169.c: A RealTek RTL-8169 Gigabit Ethernet driver for Linux kernel 2.4.x.
 --------------------------------------------------------------------

 History:
 Feb  4 2002	- created initially by ShuChen <shuchen@realtek.com.tw>.
 May 20 2002	- Add link status force-mode and TBI mode support.
	2004	- Massive updates. See kernel SCM system for details.
=========================================================================
  1. [DEPRECATED: use ethtool instead] The media can be forced in 5 modes.
	 Command: 'insmod r8169 media = SET_MEDIA'
	 Ex:	  'insmod r8169 media = 0x04' will force PHY to operate in 100Mpbs Half-duplex.

	 SET_MEDIA can be:
 		_10_Half	= 0x01
 		_10_Full	= 0x02
 		_100_Half	= 0x04
 		_100_Full	= 0x08
 		_1000_Full	= 0x10

  2. Support TBI mode.
=========================================================================
VERSION 1.1	<2002/10/4>

	The bit4:0 of MII register 4 is called "selector field", and have to be
	00001b to indicate support of IEEE std 802.3 during NWay process of
	exchanging Link Code Word (FLP).

VERSION 1.2	<2002/11/30>

	- Large style cleanup
	- Use ether_crc in stock kernel (linux/crc32.h)
	- Copy mc_filter setup code from 8139cp
	  (includes an optimization, and avoids set_bit use)

VERSION 1.6LK	<2004/04/14>

	- Merge of Realtek's version 1.6
	- Conversion to DMA API
	- Suspend/resume
	- Endianness
	- Misc Rx/Tx bugs

VERSION 2.2LK	<2005/01/25>

	- RX csum, TX csum/SG, TSO
	- VLAN
	- baby (< 7200) Jumbo frames support
	- Merge of Realtek's version 2.2 (new phy)
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_R8169_NAPI
#define NAPI_SUFFIX	"-NAPI"
#else
#define NAPI_SUFFIX	""
#endif

#define RTL8169_VERSION "2.2LK" NAPI_SUFFIX
#define MODULENAME "r8169"
#define PFX MODULENAME ": "

#ifdef RTL8169_DEBUG
#define assert(expr) \
	if (!(expr)) {					\
		printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
		#expr,__FILE__,__FUNCTION__,__LINE__);		\
	}
#define dprintk(fmt, args...)	do { printk(PFX fmt, ## args); } while (0)
#else
#define assert(expr) do {} while (0)
#define dprintk(fmt, args...)	do {} while (0)
#endif /* RTL8169_DEBUG */

#define R8169_MSG_DEFAULT \
	(NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN)

#define TX_BUFFS_AVAIL(tp) \
	(tp->dirty_tx + NUM_TX_DESC - tp->cur_tx - 1)

#ifdef CONFIG_R8169_NAPI
#define rtl8169_rx_skb			netif_receive_skb
#define rtl8169_rx_hwaccel_skb		vlan_hwaccel_receive_skb
#define rtl8169_rx_quota(count, quota)	min(count, quota)
#else
#define rtl8169_rx_skb			netif_rx
#define rtl8169_rx_hwaccel_skb		vlan_hwaccel_rx
#define rtl8169_rx_quota(count, quota)	count
#endif

/* media options */
#define MAX_UNITS 8
static int media[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int num_media = 0;

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static const int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC. */
static const int multicast_filter_limit = 32;

/* MAC address length */
#define MAC_ADDR_LEN	6

#define RX_FIFO_THRESH	7	/* 7 means NO threshold, Rx buffer level before first PCI xfer. */
#define RX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */
#define EarlyTxThld 	0x3F	/* 0x3F means NO early transmit */
#define RxPacketMaxSize	0x3FE8	/* 16K - 1 - ETH_HLEN - VLAN - CRC... */
#define SafeMtu		0x1c20	/* ... actually life sucks beyond ~7k */
#define InterFrameGap	0x03	/* 3 means InterFrameGap = the shortest one */

#define R8169_REGS_SIZE		256
#define R8169_NAPI_WEIGHT	64
#define NUM_TX_DESC	64	/* Number of Tx descriptor registers */
#define NUM_RX_DESC	256	/* Number of Rx descriptor registers */
#define RX_BUF_SIZE	1536	/* Rx Buffer size */
#define R8169_TX_RING_BYTES	(NUM_TX_DESC * sizeof(struct TxDesc))
#define R8169_RX_RING_BYTES	(NUM_RX_DESC * sizeof(struct RxDesc))

#define RTL8169_TX_TIMEOUT	(6*HZ)
#define RTL8169_PHY_TIMEOUT	(10*HZ)

/* write/read MMIO register */
#define RTL_W8(reg, val8)	writeb ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)	writew ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)	writel ((val32), ioaddr + (reg))
#define RTL_R8(reg)		readb (ioaddr + (reg))
#define RTL_R16(reg)		readw (ioaddr + (reg))
#define RTL_R32(reg)		((unsigned long) readl (ioaddr + (reg)))

enum mac_version {
	RTL_GIGA_MAC_VER_01 = 0x00,
	RTL_GIGA_MAC_VER_02 = 0x01,
	RTL_GIGA_MAC_VER_03 = 0x02,
	RTL_GIGA_MAC_VER_04 = 0x03,
	RTL_GIGA_MAC_VER_05 = 0x04,
	RTL_GIGA_MAC_VER_11 = 0x0b,
	RTL_GIGA_MAC_VER_12 = 0x0c,
	RTL_GIGA_MAC_VER_13 = 0x0d,
	RTL_GIGA_MAC_VER_14 = 0x0e,
	RTL_GIGA_MAC_VER_15 = 0x0f
};

enum phy_version {
	RTL_GIGA_PHY_VER_C = 0x03, /* PHY Reg 0x03 bit0-3 == 0x0000 */
	RTL_GIGA_PHY_VER_D = 0x04, /* PHY Reg 0x03 bit0-3 == 0x0000 */
	RTL_GIGA_PHY_VER_E = 0x05, /* PHY Reg 0x03 bit0-3 == 0x0000 */
	RTL_GIGA_PHY_VER_F = 0x06, /* PHY Reg 0x03 bit0-3 == 0x0001 */
	RTL_GIGA_PHY_VER_G = 0x07, /* PHY Reg 0x03 bit0-3 == 0x0002 */
	RTL_GIGA_PHY_VER_H = 0x08, /* PHY Reg 0x03 bit0-3 == 0x0003 */
};

#define _R(NAME,MAC,MASK) \
	{ .name = NAME, .mac_version = MAC, .RxConfigMask = MASK }

static const struct {
	const char *name;
	u8 mac_version;
	u32 RxConfigMask;	/* Clears the bits supported by this chip */
} rtl_chip_info[] = {
	_R("RTL8169",		RTL_GIGA_MAC_VER_01, 0xff7e1880),
	_R("RTL8169s/8110s",	RTL_GIGA_MAC_VER_02, 0xff7e1880),
	_R("RTL8169s/8110s",	RTL_GIGA_MAC_VER_03, 0xff7e1880),
	_R("RTL8169sb/8110sb",	RTL_GIGA_MAC_VER_04, 0xff7e1880),
	_R("RTL8169sc/8110sc",	RTL_GIGA_MAC_VER_05, 0xff7e1880),
	_R("RTL8168b/8111b",	RTL_GIGA_MAC_VER_11, 0xff7e1880), // PCI-E
	_R("RTL8168b/8111b",	RTL_GIGA_MAC_VER_12, 0xff7e1880), // PCI-E
	_R("RTL8101e",		RTL_GIGA_MAC_VER_13, 0xff7e1880), // PCI-E 8139
	_R("RTL8100e",		RTL_GIGA_MAC_VER_14, 0xff7e1880), // PCI-E 8139
	_R("RTL8100e",		RTL_GIGA_MAC_VER_15, 0xff7e1880)  // PCI-E 8139
};
#undef _R

enum cfg_version {
	RTL_CFG_0 = 0x00,
	RTL_CFG_1,
	RTL_CFG_2
};

static const struct {
	unsigned int region;
	unsigned int align;
} rtl_cfg_info[] = {
	[RTL_CFG_0] = { 1, NET_IP_ALIGN },
	[RTL_CFG_1] = { 2, NET_IP_ALIGN },
	[RTL_CFG_2] = { 2, 8 }
};

static struct pci_device_id rtl8169_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8129), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8136), 0, 0, RTL_CFG_2 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8167), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8168), 0, 0, RTL_CFG_2 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8169), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK,	0x4300), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(0x1259,			0xc107), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(0x16ec,			0x0116), 0, 0, RTL_CFG_0 },
	{ PCI_VENDOR_ID_LINKSYS,		0x1032,
		PCI_ANY_ID, 0x0024, 0, 0, RTL_CFG_0 },
	{0,},
};

MODULE_DEVICE_TABLE(pci, rtl8169_pci_tbl);

static int rx_copybreak = 200;
static int use_dac;
static struct {
	u32 msg_enable;
} debug = { -1 };

enum RTL8169_registers {
	MAC0 = 0,		/* Ethernet hardware address. */
	MAR0 = 8,		/* Multicast filter. */
	CounterAddrLow = 0x10,
	CounterAddrHigh = 0x14,
	TxDescStartAddrLow = 0x20,
	TxDescStartAddrHigh = 0x24,
	TxHDescStartAddrLow = 0x28,
	TxHDescStartAddrHigh = 0x2c,
	FLASH = 0x30,
	ERSR = 0x36,
	ChipCmd = 0x37,
	TxPoll = 0x38,
	IntrMask = 0x3C,
	IntrStatus = 0x3E,
	TxConfig = 0x40,
	RxConfig = 0x44,
	RxMissed = 0x4C,
	Cfg9346 = 0x50,
	Config0 = 0x51,
	Config1 = 0x52,
	Config2 = 0x53,
	Config3 = 0x54,
	Config4 = 0x55,
	Config5 = 0x56,
	MultiIntr = 0x5C,
	PHYAR = 0x60,
	TBICSR = 0x64,
	TBI_ANAR = 0x68,
	TBI_LPAR = 0x6A,
	PHYstatus = 0x6C,
	RxMaxSize = 0xDA,
	CPlusCmd = 0xE0,
	IntrMitigate = 0xE2,
	RxDescAddrLow = 0xE4,
	RxDescAddrHigh = 0xE8,
	EarlyTxThres = 0xEC,
	FuncEvent = 0xF0,
	FuncEventMask = 0xF4,
	FuncPresetState = 0xF8,
	FuncForceEvent = 0xFC,
};

enum RTL8169_register_content {
	/* InterruptStatusBits */
	SYSErr = 0x8000,
	PCSTimeout = 0x4000,
	SWInt = 0x0100,
	TxDescUnavail = 0x80,
	RxFIFOOver = 0x40,
	LinkChg = 0x20,
	RxOverflow = 0x10,
	TxErr = 0x08,
	TxOK = 0x04,
	RxErr = 0x02,
	RxOK = 0x01,

	/* RxStatusDesc */
	RxFOVF	= (1 << 23),
	RxRWT	= (1 << 22),
	RxRES	= (1 << 21),
	RxRUNT	= (1 << 20),
	RxCRC	= (1 << 19),

	/* ChipCmdBits */
	CmdReset = 0x10,
	CmdRxEnb = 0x08,
	CmdTxEnb = 0x04,
	RxBufEmpty = 0x01,

	/* Cfg9346Bits */
	Cfg9346_Lock = 0x00,
	Cfg9346_Unlock = 0xC0,

	/* rx_mode_bits */
	AcceptErr = 0x20,
	AcceptRunt = 0x10,
	AcceptBroadcast = 0x08,
	AcceptMulticast = 0x04,
	AcceptMyPhys = 0x02,
	AcceptAllPhys = 0x01,

	/* RxConfigBits */
	RxCfgFIFOShift = 13,
	RxCfgDMAShift = 8,

	/* TxConfigBits */
	TxInterFrameGapShift = 24,
	TxDMAShift = 8,	/* DMA burst value (0-7) is shift this many bits */

	/* Config1 register p.24 */
	PMEnable	= (1 << 0),	/* Power Management Enable */

	/* Config3 register p.25 */
	MagicPacket	= (1 << 5),	/* Wake up when receives a Magic Packet */
	LinkUp		= (1 << 4),	/* Wake up when the cable connection is re-established */

	/* Config5 register p.27 */
	BWF		= (1 << 6),	/* Accept Broadcast wakeup frame */
	MWF		= (1 << 5),	/* Accept Multicast wakeup frame */
	UWF		= (1 << 4),	/* Accept Unicast wakeup frame */
	LanWake		= (1 << 1),	/* LanWake enable/disable */
	PMEStatus	= (1 << 0),	/* PME status can be reset by PCI RST# */

	/* TBICSR p.28 */
	TBIReset	= 0x80000000,
	TBILoopback	= 0x40000000,
	TBINwEnable	= 0x20000000,
	TBINwRestart	= 0x10000000,
	TBILinkOk	= 0x02000000,
	TBINwComplete	= 0x01000000,

	/* CPlusCmd p.31 */
	RxVlan		= (1 << 6),
	RxChkSum	= (1 << 5),
	PCIDAC		= (1 << 4),
	PCIMulRW	= (1 << 3),

	/* rtl8169_PHYstatus */
	TBI_Enable = 0x80,
	TxFlowCtrl = 0x40,
	RxFlowCtrl = 0x20,
	_1000bpsF = 0x10,
	_100bps = 0x08,
	_10bps = 0x04,
	LinkStatus = 0x02,
	FullDup = 0x01,

	/* _MediaType */
	_10_Half = 0x01,
	_10_Full = 0x02,
	_100_Half = 0x04,
	_100_Full = 0x08,
	_1000_Full = 0x10,

	/* _TBICSRBit */
	TBILinkOK = 0x02000000,

	/* DumpCounterCommand */
	CounterDump = 0x8,
};

enum _DescStatusBit {
	DescOwn		= (1 << 31), /* Descriptor is owned by NIC */
	RingEnd		= (1 << 30), /* End of descriptor ring */
	FirstFrag	= (1 << 29), /* First segment of a packet */
	LastFrag	= (1 << 28), /* Final segment of a packet */

	/* Tx private */
	LargeSend	= (1 << 27), /* TCP Large Send Offload (TSO) */
	MSSShift	= 16,        /* MSS value position */
	MSSMask		= 0xfff,     /* MSS value + LargeSend bit: 12 bits */
	IPCS		= (1 << 18), /* Calculate IP checksum */
	UDPCS		= (1 << 17), /* Calculate UDP/IP checksum */
	TCPCS		= (1 << 16), /* Calculate TCP/IP checksum */
	TxVlanTag	= (1 << 17), /* Add VLAN tag */

	/* Rx private */
	PID1		= (1 << 18), /* Protocol ID bit 1/2 */
	PID0		= (1 << 17), /* Protocol ID bit 2/2 */

#define RxProtoUDP	(PID1)
#define RxProtoTCP	(PID0)
#define RxProtoIP	(PID1 | PID0)
#define RxProtoMask	RxProtoIP

	IPFail		= (1 << 16), /* IP checksum failed */
	UDPFail		= (1 << 15), /* UDP/IP checksum failed */
	TCPFail		= (1 << 14), /* TCP/IP checksum failed */
	RxVlanTag	= (1 << 16), /* VLAN tag available */
};

#define RsvdMask	0x3fffc000

struct TxDesc {
	u32 opts1;
	u32 opts2;
	u64 addr;
};

struct RxDesc {
	u32 opts1;
	u32 opts2;
	u64 addr;
};

struct ring_info {
	struct sk_buff	*skb;
	u32		len;
	u8		__pad[sizeof(void *) - sizeof(u32)];
};

struct rtl8169_private {
	void __iomem *mmio_addr;	/* memory map physical address */
	struct pci_dev *pci_dev;	/* Index of PCI device */
	struct net_device *dev;
	struct net_device_stats stats;	/* statistics of net device */
	spinlock_t lock;		/* spin lock flag */
	u32 msg_enable;
	int chipset;
	int mac_version;
	int phy_version;
	u32 cur_rx; /* Index into the Rx descriptor buffer of next Rx pkt. */
	u32 cur_tx; /* Index into the Tx descriptor buffer of next Rx pkt. */
	u32 dirty_rx;
	u32 dirty_tx;
	struct TxDesc *TxDescArray;	/* 256-aligned Tx descriptor ring */
	struct RxDesc *RxDescArray;	/* 256-aligned Rx descriptor ring */
	dma_addr_t TxPhyAddr;
	dma_addr_t RxPhyAddr;
	struct sk_buff *Rx_skbuff[NUM_RX_DESC];	/* Rx data buffers */
	struct ring_info tx_skb[NUM_TX_DESC];	/* Tx data buffers */
	unsigned align;
	unsigned rx_buf_sz;
	struct timer_list timer;
	u16 cp_cmd;
	u16 intr_mask;
	int phy_auto_nego_reg;
	int phy_1000_ctrl_reg;
#ifdef CONFIG_R8169_VLAN
	struct vlan_group *vlgrp;
#endif
	int (*set_speed)(struct net_device *, u8 autoneg, u16 speed, u8 duplex);
	void (*get_settings)(struct net_device *, struct ethtool_cmd *);
	void (*phy_reset_enable)(void __iomem *);
	unsigned int (*phy_reset_pending)(void __iomem *);
	unsigned int (*link_ok)(void __iomem *);
	struct delayed_work task;
	unsigned wol_enabled : 1;
};

MODULE_AUTHOR("Realtek and the Linux r8169 crew <netdev@vger.kernel.org>");
MODULE_DESCRIPTION("RealTek RTL-8169 Gigabit Ethernet driver");
module_param_array(media, int, &num_media, 0);
MODULE_PARM_DESC(media, "force phy operation. Deprecated by ethtool (8).");
module_param(rx_copybreak, int, 0);
MODULE_PARM_DESC(rx_copybreak, "Copy breakpoint for copy-only-tiny-frames");
module_param(use_dac, int, 0);
MODULE_PARM_DESC(use_dac, "Enable PCI DAC. Unsafe on 32 bit PCI slot.");
module_param_named(debug, debug.msg_enable, int, 0);
MODULE_PARM_DESC(debug, "Debug verbosity level (0=none, ..., 16=all)");
MODULE_LICENSE("GPL");
MODULE_VERSION(RTL8169_VERSION);

static int rtl8169_open(struct net_device *dev);
static int rtl8169_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t rtl8169_interrupt(int irq, void *dev_instance);
static int rtl8169_init_ring(struct net_device *dev);
static void rtl8169_hw_start(struct net_device *dev);
static int rtl8169_close(struct net_device *dev);
static void rtl8169_set_rx_mode(struct net_device *dev);
static void rtl8169_tx_timeout(struct net_device *dev);
static struct net_device_stats *rtl8169_get_stats(struct net_device *dev);
static int rtl8169_rx_interrupt(struct net_device *, struct rtl8169_private *,
				void __iomem *);
static int rtl8169_change_mtu(struct net_device *dev, int new_mtu);
static void rtl8169_down(struct net_device *dev);
static void rtl8169_rx_clear(struct rtl8169_private *tp);

#ifdef CONFIG_R8169_NAPI
static int rtl8169_poll(struct net_device *dev, int *budget);
#endif

static const u16 rtl8169_intr_mask =
	SYSErr | LinkChg | RxOverflow | RxFIFOOver | TxErr | TxOK | RxErr | RxOK;
static const u16 rtl8169_napi_event =
	RxOK | RxOverflow | RxFIFOOver | TxOK | TxErr;
static const unsigned int rtl8169_rx_config =
	(RX_FIFO_THRESH << RxCfgFIFOShift) | (RX_DMA_BURST << RxCfgDMAShift);

static void mdio_write(void __iomem *ioaddr, int RegAddr, int value)
{
	int i;

	RTL_W32(PHYAR, 0x80000000 | (RegAddr & 0xFF) << 16 | value);

	for (i = 20; i > 0; i--) {
		/* Check if the RTL8169 has completed writing to the specified MII register */
		if (!(RTL_R32(PHYAR) & 0x80000000))
			break;
		udelay(25);
	}
}

static int mdio_read(void __iomem *ioaddr, int RegAddr)
{
	int i, value = -1;

	RTL_W32(PHYAR, 0x0 | (RegAddr & 0xFF) << 16);

	for (i = 20; i > 0; i--) {
		/* Check if the RTL8169 has completed retrieving data from the specified MII register */
		if (RTL_R32(PHYAR) & 0x80000000) {
			value = (int) (RTL_R32(PHYAR) & 0xFFFF);
			break;
		}
		udelay(25);
	}
	return value;
}

static void rtl8169_irq_mask_and_ack(void __iomem *ioaddr)
{
	RTL_W16(IntrMask, 0x0000);

	RTL_W16(IntrStatus, 0xffff);
}

static void rtl8169_asic_down(void __iomem *ioaddr)
{
	RTL_W8(ChipCmd, 0x00);
	rtl8169_irq_mask_and_ack(ioaddr);
	RTL_R16(CPlusCmd);
}

static unsigned int rtl8169_tbi_reset_pending(void __iomem *ioaddr)
{
	return RTL_R32(TBICSR) & TBIReset;
}

static unsigned int rtl8169_xmii_reset_pending(void __iomem *ioaddr)
{
	return mdio_read(ioaddr, MII_BMCR) & BMCR_RESET;
}

static unsigned int rtl8169_tbi_link_ok(void __iomem *ioaddr)
{
	return RTL_R32(TBICSR) & TBILinkOk;
}

static unsigned int rtl8169_xmii_link_ok(void __iomem *ioaddr)
{
	return RTL_R8(PHYstatus) & LinkStatus;
}

static void rtl8169_tbi_reset_enable(void __iomem *ioaddr)
{
	RTL_W32(TBICSR, RTL_R32(TBICSR) | TBIReset);
}

static void rtl8169_xmii_reset_enable(void __iomem *ioaddr)
{
	unsigned int val;

	val = mdio_read(ioaddr, MII_BMCR) | BMCR_RESET;
	mdio_write(ioaddr, MII_BMCR, val & 0xffff);
}

static void rtl8169_check_link_status(struct net_device *dev,
				      struct rtl8169_private *tp, void __iomem *ioaddr)
{
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);
	if (tp->link_ok(ioaddr)) {
		netif_carrier_on(dev);
		if (netif_msg_ifup(tp))
			printk(KERN_INFO PFX "%s: link up\n", dev->name);
	} else {
		if (netif_msg_ifdown(tp))
			printk(KERN_INFO PFX "%s: link down\n", dev->name);
		netif_carrier_off(dev);
	}
	spin_unlock_irqrestore(&tp->lock, flags);
}

static void rtl8169_link_option(int idx, u8 *autoneg, u16 *speed, u8 *duplex)
{
	struct {
		u16 speed;
		u8 duplex;
		u8 autoneg;
		u8 media;
	} link_settings[] = {
		{ SPEED_10,	DUPLEX_HALF, AUTONEG_DISABLE,	_10_Half },
		{ SPEED_10,	DUPLEX_FULL, AUTONEG_DISABLE,	_10_Full },
		{ SPEED_100,	DUPLEX_HALF, AUTONEG_DISABLE,	_100_Half },
		{ SPEED_100,	DUPLEX_FULL, AUTONEG_DISABLE,	_100_Full },
		{ SPEED_1000,	DUPLEX_FULL, AUTONEG_DISABLE,	_1000_Full },
		/* Make TBI happy */
		{ SPEED_1000,	DUPLEX_FULL, AUTONEG_ENABLE,	0xff }
	}, *p;
	unsigned char option;

	option = ((idx < MAX_UNITS) && (idx >= 0)) ? media[idx] : 0xff;

	if ((option != 0xff) && !idx && netif_msg_drv(&debug))
		printk(KERN_WARNING PFX "media option is deprecated.\n");

	for (p = link_settings; p->media != 0xff; p++) {
		if (p->media == option)
			break;
	}
	*autoneg = p->autoneg;
	*speed = p->speed;
	*duplex = p->duplex;
}

static void rtl8169_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u8 options;

	wol->wolopts = 0;

#define WAKE_ANY (WAKE_PHY | WAKE_MAGIC | WAKE_UCAST | WAKE_BCAST | WAKE_MCAST)
	wol->supported = WAKE_ANY;

	spin_lock_irq(&tp->lock);

	options = RTL_R8(Config1);
	if (!(options & PMEnable))
		goto out_unlock;

	options = RTL_R8(Config3);
	if (options & LinkUp)
		wol->wolopts |= WAKE_PHY;
	if (options & MagicPacket)
		wol->wolopts |= WAKE_MAGIC;

	options = RTL_R8(Config5);
	if (options & UWF)
		wol->wolopts |= WAKE_UCAST;
	if (options & BWF)
		wol->wolopts |= WAKE_BCAST;
	if (options & MWF)
		wol->wolopts |= WAKE_MCAST;

out_unlock:
	spin_unlock_irq(&tp->lock);
}

static int rtl8169_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	int i;
	static struct {
		u32 opt;
		u16 reg;
		u8  mask;
	} cfg[] = {
		{ WAKE_ANY,   Config1, PMEnable },
		{ WAKE_PHY,   Config3, LinkUp },
		{ WAKE_MAGIC, Config3, MagicPacket },
		{ WAKE_UCAST, Config5, UWF },
		{ WAKE_BCAST, Config5, BWF },
		{ WAKE_MCAST, Config5, MWF },
		{ WAKE_ANY,   Config5, LanWake }
	};

	spin_lock_irq(&tp->lock);

	RTL_W8(Cfg9346, Cfg9346_Unlock);

	for (i = 0; i < ARRAY_SIZE(cfg); i++) {
		u8 options = RTL_R8(cfg[i].reg) & ~cfg[i].mask;
		if (wol->wolopts & cfg[i].opt)
			options |= cfg[i].mask;
		RTL_W8(cfg[i].reg, options);
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);

	tp->wol_enabled = (wol->wolopts) ? 1 : 0;

	spin_unlock_irq(&tp->lock);

	return 0;
}

static void rtl8169_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	strcpy(info->driver, MODULENAME);
	strcpy(info->version, RTL8169_VERSION);
	strcpy(info->bus_info, pci_name(tp->pci_dev));
}

static int rtl8169_get_regs_len(struct net_device *dev)
{
	return R8169_REGS_SIZE;
}

static int rtl8169_set_speed_tbi(struct net_device *dev,
				 u8 autoneg, u16 speed, u8 duplex)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	int ret = 0;
	u32 reg;

	reg = RTL_R32(TBICSR);
	if ((autoneg == AUTONEG_DISABLE) && (speed == SPEED_1000) &&
	    (duplex == DUPLEX_FULL)) {
		RTL_W32(TBICSR, reg & ~(TBINwEnable | TBINwRestart));
	} else if (autoneg == AUTONEG_ENABLE)
		RTL_W32(TBICSR, reg | TBINwEnable | TBINwRestart);
	else {
		if (netif_msg_link(tp)) {
			printk(KERN_WARNING "%s: "
			       "incorrect speed setting refused in TBI mode\n",
			       dev->name);
		}
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int rtl8169_set_speed_xmii(struct net_device *dev,
				  u8 autoneg, u16 speed, u8 duplex)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	int auto_nego, giga_ctrl;

	auto_nego = mdio_read(ioaddr, MII_ADVERTISE);
	auto_nego &= ~(ADVERTISE_10HALF | ADVERTISE_10FULL |
		       ADVERTISE_100HALF | ADVERTISE_100FULL);
	giga_ctrl = mdio_read(ioaddr, MII_CTRL1000);
	giga_ctrl &= ~(ADVERTISE_1000FULL | ADVERTISE_1000HALF);

	if (autoneg == AUTONEG_ENABLE) {
		auto_nego |= (ADVERTISE_10HALF | ADVERTISE_10FULL |
			      ADVERTISE_100HALF | ADVERTISE_100FULL);
		giga_ctrl |= ADVERTISE_1000FULL | ADVERTISE_1000HALF;
	} else {
		if (speed == SPEED_10)
			auto_nego |= ADVERTISE_10HALF | ADVERTISE_10FULL;
		else if (speed == SPEED_100)
			auto_nego |= ADVERTISE_100HALF | ADVERTISE_100FULL;
		else if (speed == SPEED_1000)
			giga_ctrl |= ADVERTISE_1000FULL | ADVERTISE_1000HALF;

		if (duplex == DUPLEX_HALF)
			auto_nego &= ~(ADVERTISE_10FULL | ADVERTISE_100FULL);

		if (duplex == DUPLEX_FULL)
			auto_nego &= ~(ADVERTISE_10HALF | ADVERTISE_100HALF);

		/* This tweak comes straight from Realtek's driver. */
		if ((speed == SPEED_100) && (duplex == DUPLEX_HALF) &&
		    (tp->mac_version == RTL_GIGA_MAC_VER_13)) {
			auto_nego = ADVERTISE_100HALF | ADVERTISE_CSMA;
		}
	}

	/* The 8100e/8101e do Fast Ethernet only. */
	if ((tp->mac_version == RTL_GIGA_MAC_VER_13) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_14) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_15)) {
		if ((giga_ctrl & (ADVERTISE_1000FULL | ADVERTISE_1000HALF)) &&
		    netif_msg_link(tp)) {
			printk(KERN_INFO "%s: PHY does not support 1000Mbps.\n",
			       dev->name);
		}
		giga_ctrl &= ~(ADVERTISE_1000FULL | ADVERTISE_1000HALF);
	}

	auto_nego |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;

	tp->phy_auto_nego_reg = auto_nego;
	tp->phy_1000_ctrl_reg = giga_ctrl;

	mdio_write(ioaddr, MII_ADVERTISE, auto_nego);
	mdio_write(ioaddr, MII_CTRL1000, giga_ctrl);
	mdio_write(ioaddr, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
	return 0;
}

static int rtl8169_set_speed(struct net_device *dev,
			     u8 autoneg, u16 speed, u8 duplex)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	ret = tp->set_speed(dev, autoneg, speed, duplex);

	if (netif_running(dev) && (tp->phy_1000_ctrl_reg & ADVERTISE_1000FULL))
		mod_timer(&tp->timer, jiffies + RTL8169_PHY_TIMEOUT);

	return ret;
}

static int rtl8169_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&tp->lock, flags);
	ret = rtl8169_set_speed(dev, cmd->autoneg, cmd->speed, cmd->duplex);
	spin_unlock_irqrestore(&tp->lock, flags);

	return ret;
}

static u32 rtl8169_get_rx_csum(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return tp->cp_cmd & RxChkSum;
}

static int rtl8169_set_rx_csum(struct net_device *dev, u32 data)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);

	if (data)
		tp->cp_cmd |= RxChkSum;
	else
		tp->cp_cmd &= ~RxChkSum;

	RTL_W16(CPlusCmd, tp->cp_cmd);
	RTL_R16(CPlusCmd);

	spin_unlock_irqrestore(&tp->lock, flags);

	return 0;
}

#ifdef CONFIG_R8169_VLAN

static inline u32 rtl8169_tx_vlan_tag(struct rtl8169_private *tp,
				      struct sk_buff *skb)
{
	return (tp->vlgrp && vlan_tx_tag_present(skb)) ?
		TxVlanTag | swab16(vlan_tx_tag_get(skb)) : 0x00;
}

static void rtl8169_vlan_rx_register(struct net_device *dev,
				     struct vlan_group *grp)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);
	tp->vlgrp = grp;
	if (tp->vlgrp)
		tp->cp_cmd |= RxVlan;
	else
		tp->cp_cmd &= ~RxVlan;
	RTL_W16(CPlusCmd, tp->cp_cmd);
	RTL_R16(CPlusCmd);
	spin_unlock_irqrestore(&tp->lock, flags);
}

static void rtl8169_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);
	vlan_group_set_device(tp->vlgrp, vid, NULL);
	spin_unlock_irqrestore(&tp->lock, flags);
}

static int rtl8169_rx_vlan_skb(struct rtl8169_private *tp, struct RxDesc *desc,
			       struct sk_buff *skb)
{
	u32 opts2 = le32_to_cpu(desc->opts2);
	int ret;

	if (tp->vlgrp && (opts2 & RxVlanTag)) {
		rtl8169_rx_hwaccel_skb(skb, tp->vlgrp,
				       swab16(opts2 & 0xffff));
		ret = 0;
	} else
		ret = -1;
	desc->opts2 = 0;
	return ret;
}

#else /* !CONFIG_R8169_VLAN */

static inline u32 rtl8169_tx_vlan_tag(struct rtl8169_private *tp,
				      struct sk_buff *skb)
{
	return 0;
}

static int rtl8169_rx_vlan_skb(struct rtl8169_private *tp, struct RxDesc *desc,
			       struct sk_buff *skb)
{
	return -1;
}

#endif

static void rtl8169_gset_tbi(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u32 status;

	cmd->supported =
		SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg | SUPPORTED_FIBRE;
	cmd->port = PORT_FIBRE;
	cmd->transceiver = XCVR_INTERNAL;

	status = RTL_R32(TBICSR);
	cmd->advertising = (status & TBINwEnable) ?  ADVERTISED_Autoneg : 0;
	cmd->autoneg = !!(status & TBINwEnable);

	cmd->speed = SPEED_1000;
	cmd->duplex = DUPLEX_FULL; /* Always set */
}

static void rtl8169_gset_xmii(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u8 status;

	cmd->supported = SUPPORTED_10baseT_Half |
			 SUPPORTED_10baseT_Full |
			 SUPPORTED_100baseT_Half |
			 SUPPORTED_100baseT_Full |
			 SUPPORTED_1000baseT_Full |
			 SUPPORTED_Autoneg |
			 SUPPORTED_TP;

	cmd->autoneg = 1;
	cmd->advertising = ADVERTISED_TP | ADVERTISED_Autoneg;

	if (tp->phy_auto_nego_reg & ADVERTISE_10HALF)
		cmd->advertising |= ADVERTISED_10baseT_Half;
	if (tp->phy_auto_nego_reg & ADVERTISE_10FULL)
		cmd->advertising |= ADVERTISED_10baseT_Full;
	if (tp->phy_auto_nego_reg & ADVERTISE_100HALF)
		cmd->advertising |= ADVERTISED_100baseT_Half;
	if (tp->phy_auto_nego_reg & ADVERTISE_100FULL)
		cmd->advertising |= ADVERTISED_100baseT_Full;
	if (tp->phy_1000_ctrl_reg & ADVERTISE_1000FULL)
		cmd->advertising |= ADVERTISED_1000baseT_Full;

	status = RTL_R8(PHYstatus);

	if (status & _1000bpsF)
		cmd->speed = SPEED_1000;
	else if (status & _100bps)
		cmd->speed = SPEED_100;
	else if (status & _10bps)
		cmd->speed = SPEED_10;

	if (status & TxFlowCtrl)
		cmd->advertising |= ADVERTISED_Asym_Pause;
	if (status & RxFlowCtrl)
		cmd->advertising |= ADVERTISED_Pause;

	cmd->duplex = ((status & _1000bpsF) || (status & FullDup)) ?
		      DUPLEX_FULL : DUPLEX_HALF;
}

static int rtl8169_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);

	tp->get_settings(dev, cmd);

	spin_unlock_irqrestore(&tp->lock, flags);
	return 0;
}

static void rtl8169_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			     void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned long flags;

	if (regs->len > R8169_REGS_SIZE)
		regs->len = R8169_REGS_SIZE;

	spin_lock_irqsave(&tp->lock, flags);
	memcpy_fromio(p, tp->mmio_addr, regs->len);
	spin_unlock_irqrestore(&tp->lock, flags);
}

static u32 rtl8169_get_msglevel(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return tp->msg_enable;
}

static void rtl8169_set_msglevel(struct net_device *dev, u32 value)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	tp->msg_enable = value;
}

static const char rtl8169_gstrings[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"rx_packets",
	"tx_errors",
	"rx_errors",
	"rx_missed",
	"align_errors",
	"tx_single_collisions",
	"tx_multi_collisions",
	"unicast",
	"broadcast",
	"multicast",
	"tx_aborted",
	"tx_underrun",
};

struct rtl8169_counters {
	u64	tx_packets;
	u64	rx_packets;
	u64	tx_errors;
	u32	rx_errors;
	u16	rx_missed;
	u16	align_errors;
	u32	tx_one_collision;
	u32	tx_multi_collision;
	u64	rx_unicast;
	u64	rx_broadcast;
	u32	rx_multicast;
	u16	tx_aborted;
	u16	tx_underun;
};

static int rtl8169_get_stats_count(struct net_device *dev)
{
	return ARRAY_SIZE(rtl8169_gstrings);
}

static void rtl8169_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct rtl8169_counters *counters;
	dma_addr_t paddr;
	u32 cmd;

	ASSERT_RTNL();

	counters = pci_alloc_consistent(tp->pci_dev, sizeof(*counters), &paddr);
	if (!counters)
		return;

	RTL_W32(CounterAddrHigh, (u64)paddr >> 32);
	cmd = (u64)paddr & DMA_32BIT_MASK;
	RTL_W32(CounterAddrLow, cmd);
	RTL_W32(CounterAddrLow, cmd | CounterDump);

	while (RTL_R32(CounterAddrLow) & CounterDump) {
		if (msleep_interruptible(1))
			break;
	}

	RTL_W32(CounterAddrLow, 0);
	RTL_W32(CounterAddrHigh, 0);

	data[0] = le64_to_cpu(counters->tx_packets);
	data[1] = le64_to_cpu(counters->rx_packets);
	data[2] = le64_to_cpu(counters->tx_errors);
	data[3] = le32_to_cpu(counters->rx_errors);
	data[4] = le16_to_cpu(counters->rx_missed);
	data[5] = le16_to_cpu(counters->align_errors);
	data[6] = le32_to_cpu(counters->tx_one_collision);
	data[7] = le32_to_cpu(counters->tx_multi_collision);
	data[8] = le64_to_cpu(counters->rx_unicast);
	data[9] = le64_to_cpu(counters->rx_broadcast);
	data[10] = le32_to_cpu(counters->rx_multicast);
	data[11] = le16_to_cpu(counters->tx_aborted);
	data[12] = le16_to_cpu(counters->tx_underun);

	pci_free_consistent(tp->pci_dev, sizeof(*counters), counters, paddr);
}

static void rtl8169_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(data, *rtl8169_gstrings, sizeof(rtl8169_gstrings));
		break;
	}
}


static const struct ethtool_ops rtl8169_ethtool_ops = {
	.get_drvinfo		= rtl8169_get_drvinfo,
	.get_regs_len		= rtl8169_get_regs_len,
	.get_link		= ethtool_op_get_link,
	.get_settings		= rtl8169_get_settings,
	.set_settings		= rtl8169_set_settings,
	.get_msglevel		= rtl8169_get_msglevel,
	.set_msglevel		= rtl8169_set_msglevel,
	.get_rx_csum		= rtl8169_get_rx_csum,
	.set_rx_csum		= rtl8169_set_rx_csum,
	.get_tx_csum		= ethtool_op_get_tx_csum,
	.set_tx_csum		= ethtool_op_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= ethtool_op_set_tso,
	.get_regs		= rtl8169_get_regs,
	.get_wol		= rtl8169_get_wol,
	.set_wol		= rtl8169_set_wol,
	.get_strings		= rtl8169_get_strings,
	.get_stats_count	= rtl8169_get_stats_count,
	.get_ethtool_stats	= rtl8169_get_ethtool_stats,
	.get_perm_addr		= ethtool_op_get_perm_addr,
};

static void rtl8169_write_gmii_reg_bit(void __iomem *ioaddr, int reg, int bitnum,
				       int bitval)
{
	int val;

	val = mdio_read(ioaddr, reg);
	val = (bitval == 1) ?
		val | (bitval << bitnum) :  val & ~(0x0001 << bitnum);
	mdio_write(ioaddr, reg, val & 0xffff);
}

static void rtl8169_get_mac_version(struct rtl8169_private *tp, void __iomem *ioaddr)
{
	const struct {
		u32 mask;
		int mac_version;
	} mac_info[] = {
		{ 0x38800000,	RTL_GIGA_MAC_VER_15 },
		{ 0x38000000,	RTL_GIGA_MAC_VER_12 },
		{ 0x34000000,	RTL_GIGA_MAC_VER_13 },
		{ 0x30800000,	RTL_GIGA_MAC_VER_14 },
		{ 0x30000000,	RTL_GIGA_MAC_VER_11 },
		{ 0x18000000,	RTL_GIGA_MAC_VER_05 },
		{ 0x10000000,	RTL_GIGA_MAC_VER_04 },
		{ 0x04000000,	RTL_GIGA_MAC_VER_03 },
		{ 0x00800000,	RTL_GIGA_MAC_VER_02 },
		{ 0x00000000,	RTL_GIGA_MAC_VER_01 }	/* Catch-all */
	}, *p = mac_info;
	u32 reg;

	reg = RTL_R32(TxConfig) & 0x7c800000;
	while ((reg & p->mask) != p->mask)
		p++;
	tp->mac_version = p->mac_version;
}

static void rtl8169_print_mac_version(struct rtl8169_private *tp)
{
	dprintk("mac_version = 0x%02x\n", tp->mac_version);
}

static void rtl8169_get_phy_version(struct rtl8169_private *tp, void __iomem *ioaddr)
{
	const struct {
		u16 mask;
		u16 set;
		int phy_version;
	} phy_info[] = {
		{ 0x000f, 0x0002, RTL_GIGA_PHY_VER_G },
		{ 0x000f, 0x0001, RTL_GIGA_PHY_VER_F },
		{ 0x000f, 0x0000, RTL_GIGA_PHY_VER_E },
		{ 0x0000, 0x0000, RTL_GIGA_PHY_VER_D } /* Catch-all */
	}, *p = phy_info;
	u16 reg;

	reg = mdio_read(ioaddr, MII_PHYSID2) & 0xffff;
	while ((reg & p->mask) != p->set)
		p++;
	tp->phy_version = p->phy_version;
}

static void rtl8169_print_phy_version(struct rtl8169_private *tp)
{
	struct {
		int version;
		char *msg;
		u32 reg;
	} phy_print[] = {
		{ RTL_GIGA_PHY_VER_G, "RTL_GIGA_PHY_VER_G", 0x0002 },
		{ RTL_GIGA_PHY_VER_F, "RTL_GIGA_PHY_VER_F", 0x0001 },
		{ RTL_GIGA_PHY_VER_E, "RTL_GIGA_PHY_VER_E", 0x0000 },
		{ RTL_GIGA_PHY_VER_D, "RTL_GIGA_PHY_VER_D", 0x0000 },
		{ 0, NULL, 0x0000 }
	}, *p;

	for (p = phy_print; p->msg; p++) {
		if (tp->phy_version == p->version) {
			dprintk("phy_version == %s (%04x)\n", p->msg, p->reg);
			return;
		}
	}
	dprintk("phy_version == Unknown\n");
}

static void rtl8169_hw_phy_config(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct {
		u16 regs[5]; /* Beware of bit-sign propagation */
	} phy_magic[5] = { {
		{ 0x0000,	//w 4 15 12 0
		  0x00a1,	//w 3 15 0 00a1
		  0x0008,	//w 2 15 0 0008
		  0x1020,	//w 1 15 0 1020
		  0x1000 } },{	//w 0 15 0 1000
		{ 0x7000,	//w 4 15 12 7
		  0xff41,	//w 3 15 0 ff41
		  0xde60,	//w 2 15 0 de60
		  0x0140,	//w 1 15 0 0140
		  0x0077 } },{	//w 0 15 0 0077
		{ 0xa000,	//w 4 15 12 a
		  0xdf01,	//w 3 15 0 df01
		  0xdf20,	//w 2 15 0 df20
		  0xff95,	//w 1 15 0 ff95
		  0xfa00 } },{	//w 0 15 0 fa00
		{ 0xb000,	//w 4 15 12 b
		  0xff41,	//w 3 15 0 ff41
		  0xde20,	//w 2 15 0 de20
		  0x0140,	//w 1 15 0 0140
		  0x00bb } },{	//w 0 15 0 00bb
		{ 0xf000,	//w 4 15 12 f
		  0xdf01,	//w 3 15 0 df01
		  0xdf20,	//w 2 15 0 df20
		  0xff95,	//w 1 15 0 ff95
		  0xbf00 }	//w 0 15 0 bf00
		}
	}, *p = phy_magic;
	int i;

	rtl8169_print_mac_version(tp);
	rtl8169_print_phy_version(tp);

	if (tp->mac_version <= RTL_GIGA_MAC_VER_01)
		return;
	if (tp->phy_version >= RTL_GIGA_PHY_VER_H)
		return;

	dprintk("MAC version != 0 && PHY version == 0 or 1\n");
	dprintk("Do final_reg2.cfg\n");

	/* Shazam ! */

	if (tp->mac_version == RTL_GIGA_MAC_VER_04) {
		mdio_write(ioaddr, 31, 0x0002);
		mdio_write(ioaddr,  1, 0x90d0);
		mdio_write(ioaddr, 31, 0x0000);
		return;
	}

	/* phy config for RTL8169s mac_version C chip */
	mdio_write(ioaddr, 31, 0x0001);			//w 31 2 0 1
	mdio_write(ioaddr, 21, 0x1000);			//w 21 15 0 1000
	mdio_write(ioaddr, 24, 0x65c7);			//w 24 15 0 65c7
	rtl8169_write_gmii_reg_bit(ioaddr, 4, 11, 0);	//w 4 11 11 0

	for (i = 0; i < ARRAY_SIZE(phy_magic); i++, p++) {
		int val, pos = 4;

		val = (mdio_read(ioaddr, pos) & 0x0fff) | (p->regs[0] & 0xffff);
		mdio_write(ioaddr, pos, val);
		while (--pos >= 0)
			mdio_write(ioaddr, pos, p->regs[4 - pos] & 0xffff);
		rtl8169_write_gmii_reg_bit(ioaddr, 4, 11, 1); //w 4 11 11 1
		rtl8169_write_gmii_reg_bit(ioaddr, 4, 11, 0); //w 4 11 11 0
	}
	mdio_write(ioaddr, 31, 0x0000); //w 31 2 0 0
}

static void rtl8169_phy_timer(unsigned long __opaque)
{
	struct net_device *dev = (struct net_device *)__opaque;
	struct rtl8169_private *tp = netdev_priv(dev);
	struct timer_list *timer = &tp->timer;
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long timeout = RTL8169_PHY_TIMEOUT;

	assert(tp->mac_version > RTL_GIGA_MAC_VER_01);
	assert(tp->phy_version < RTL_GIGA_PHY_VER_H);

	if (!(tp->phy_1000_ctrl_reg & ADVERTISE_1000FULL))
		return;

	spin_lock_irq(&tp->lock);

	if (tp->phy_reset_pending(ioaddr)) {
		/*
		 * A busy loop could burn quite a few cycles on nowadays CPU.
		 * Let's delay the execution of the timer for a few ticks.
		 */
		timeout = HZ/10;
		goto out_mod_timer;
	}

	if (tp->link_ok(ioaddr))
		goto out_unlock;

	if (netif_msg_link(tp))
		printk(KERN_WARNING "%s: PHY reset until link up\n", dev->name);

	tp->phy_reset_enable(ioaddr);

out_mod_timer:
	mod_timer(timer, jiffies + timeout);
out_unlock:
	spin_unlock_irq(&tp->lock);
}

static inline void rtl8169_delete_timer(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct timer_list *timer = &tp->timer;

	if ((tp->mac_version <= RTL_GIGA_MAC_VER_01) ||
	    (tp->phy_version >= RTL_GIGA_PHY_VER_H))
		return;

	del_timer_sync(timer);
}

static inline void rtl8169_request_timer(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct timer_list *timer = &tp->timer;

	if ((tp->mac_version <= RTL_GIGA_MAC_VER_01) ||
	    (tp->phy_version >= RTL_GIGA_PHY_VER_H))
		return;

	mod_timer(timer, jiffies + RTL8169_PHY_TIMEOUT);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void rtl8169_netpoll(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;

	disable_irq(pdev->irq);
	rtl8169_interrupt(pdev->irq, dev);
	enable_irq(pdev->irq);
}
#endif

static void rtl8169_release_board(struct pci_dev *pdev, struct net_device *dev,
				  void __iomem *ioaddr)
{
	iounmap(ioaddr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	free_netdev(dev);
}

static void rtl8169_phy_reset(struct net_device *dev,
			      struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int i;

	tp->phy_reset_enable(ioaddr);
	for (i = 0; i < 100; i++) {
		if (!tp->phy_reset_pending(ioaddr))
			return;
		msleep(1);
	}
	if (netif_msg_link(tp))
		printk(KERN_ERR "%s: PHY reset failed.\n", dev->name);
}

static void rtl8169_init_phy(struct net_device *dev, struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static int board_idx = -1;
	u8 autoneg, duplex;
	u16 speed;

	board_idx++;

	rtl8169_hw_phy_config(dev);

	dprintk("Set MAC Reg C+CR Offset 0x82h = 0x01h\n");
	RTL_W8(0x82, 0x01);

	if (tp->mac_version < RTL_GIGA_MAC_VER_03) {
		dprintk("Set PCI Latency=0x40\n");
		pci_write_config_byte(tp->pci_dev, PCI_LATENCY_TIMER, 0x40);
	}

	if (tp->mac_version == RTL_GIGA_MAC_VER_02) {
		dprintk("Set MAC Reg C+CR Offset 0x82h = 0x01h\n");
		RTL_W8(0x82, 0x01);
		dprintk("Set PHY Reg 0x0bh = 0x00h\n");
		mdio_write(ioaddr, 0x0b, 0x0000); //w 0x0b 15 0 0
	}

	rtl8169_link_option(board_idx, &autoneg, &speed, &duplex);

	rtl8169_phy_reset(dev, tp);

	rtl8169_set_speed(dev, autoneg, speed, duplex);

	if ((RTL_R8(PHYstatus) & TBI_Enable) && netif_msg_link(tp))
		printk(KERN_INFO PFX "%s: TBI auto-negotiating\n", dev->name);
}

static int rtl8169_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);

	if (!netif_running(dev))
		return -ENODEV;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = 32; /* Internal PHY */
		return 0;

	case SIOCGMIIREG:
		data->val_out = mdio_read(tp->mmio_addr, data->reg_num & 0x1f);
		return 0;

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		mdio_write(tp->mmio_addr, data->reg_num & 0x1f, data->val_in);
		return 0;
	}
	return -EOPNOTSUPP;
}

static int __devinit
rtl8169_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const unsigned int region = rtl_cfg_info[ent->driver_data].region;
	struct rtl8169_private *tp;
	struct net_device *dev;
	void __iomem *ioaddr;
	unsigned int pm_cap;
	int i, rc;

	if (netif_msg_drv(&debug)) {
		printk(KERN_INFO "%s Gigabit Ethernet driver %s loaded\n",
		       MODULENAME, RTL8169_VERSION);
	}

	dev = alloc_etherdev(sizeof (*tp));
	if (!dev) {
		if (netif_msg_drv(&debug))
			dev_err(&pdev->dev, "unable to alloc new ethernet\n");
		rc = -ENOMEM;
		goto out;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	tp = netdev_priv(dev);
	tp->dev = dev;
	tp->msg_enable = netif_msg_init(debug.msg_enable, R8169_MSG_DEFAULT);

	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	rc = pci_enable_device(pdev);
	if (rc < 0) {
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "enable failure\n");
		goto err_out_free_dev_1;
	}

	rc = pci_set_mwi(pdev);
	if (rc < 0)
		goto err_out_disable_2;

	/* save power state before pci_enable_device overwrites it */
	pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pm_cap) {
		u16 pwr_command, acpi_idle_state;

		pci_read_config_word(pdev, pm_cap + PCI_PM_CTRL, &pwr_command);
		acpi_idle_state = pwr_command & PCI_PM_CTRL_STATE_MASK;
	} else {
		if (netif_msg_probe(tp)) {
			dev_err(&pdev->dev,
				"PowerManagement capability not found.\n");
		}
	}

	/* make sure PCI base addr 1 is MMIO */
	if (!(pci_resource_flags(pdev, region) & IORESOURCE_MEM)) {
		if (netif_msg_probe(tp)) {
			dev_err(&pdev->dev,
				"region #%d not an MMIO resource, aborting\n",
				region);
		}
		rc = -ENODEV;
		goto err_out_mwi_3;
	}

	/* check for weird/broken PCI region reporting */
	if (pci_resource_len(pdev, region) < R8169_REGS_SIZE) {
		if (netif_msg_probe(tp)) {
			dev_err(&pdev->dev,
				"Invalid PCI region size(s), aborting\n");
		}
		rc = -ENODEV;
		goto err_out_mwi_3;
	}

	rc = pci_request_regions(pdev, MODULENAME);
	if (rc < 0) {
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "could not request regions.\n");
		goto err_out_mwi_3;
	}

	tp->cp_cmd = PCIMulRW | RxChkSum;

	if ((sizeof(dma_addr_t) > 4) &&
	    !pci_set_dma_mask(pdev, DMA_64BIT_MASK) && use_dac) {
		tp->cp_cmd |= PCIDAC;
		dev->features |= NETIF_F_HIGHDMA;
	} else {
		rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc < 0) {
			if (netif_msg_probe(tp)) {
				dev_err(&pdev->dev,
					"DMA configuration failed.\n");
			}
			goto err_out_free_res_4;
		}
	}

	pci_set_master(pdev);

	/* ioremap MMIO region */
	ioaddr = ioremap(pci_resource_start(pdev, region), R8169_REGS_SIZE);
	if (!ioaddr) {
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "cannot remap MMIO, aborting\n");
		rc = -EIO;
		goto err_out_free_res_4;
	}

	/* Unneeded ? Don't mess with Mrs. Murphy. */
	rtl8169_irq_mask_and_ack(ioaddr);

	/* Soft reset the chip. */
	RTL_W8(ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 100; i > 0; i--) {
		if ((RTL_R8(ChipCmd) & CmdReset) == 0)
			break;
		msleep_interruptible(1);
	}

	/* Identify chip attached to board */
	rtl8169_get_mac_version(tp, ioaddr);
	rtl8169_get_phy_version(tp, ioaddr);

	rtl8169_print_mac_version(tp);
	rtl8169_print_phy_version(tp);

	for (i = ARRAY_SIZE(rtl_chip_info) - 1; i >= 0; i--) {
		if (tp->mac_version == rtl_chip_info[i].mac_version)
			break;
	}
	if (i < 0) {
		/* Unknown chip: assume array element #0, original RTL-8169 */
		if (netif_msg_probe(tp)) {
			dev_printk(KERN_DEBUG, &pdev->dev,
				"unknown chip version, assuming %s\n",
				rtl_chip_info[0].name);
		}
		i++;
	}
	tp->chipset = i;

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	RTL_W8(Config1, RTL_R8(Config1) | PMEnable);
	RTL_W8(Config5, RTL_R8(Config5) & PMEStatus);
	RTL_W8(Cfg9346, Cfg9346_Lock);

	if (RTL_R8(PHYstatus) & TBI_Enable) {
		tp->set_speed = rtl8169_set_speed_tbi;
		tp->get_settings = rtl8169_gset_tbi;
		tp->phy_reset_enable = rtl8169_tbi_reset_enable;
		tp->phy_reset_pending = rtl8169_tbi_reset_pending;
		tp->link_ok = rtl8169_tbi_link_ok;

		tp->phy_1000_ctrl_reg = ADVERTISE_1000FULL; /* Implied by TBI */
	} else {
		tp->set_speed = rtl8169_set_speed_xmii;
		tp->get_settings = rtl8169_gset_xmii;
		tp->phy_reset_enable = rtl8169_xmii_reset_enable;
		tp->phy_reset_pending = rtl8169_xmii_reset_pending;
		tp->link_ok = rtl8169_xmii_link_ok;

		dev->do_ioctl = rtl8169_ioctl;
	}

	/* Get MAC address.  FIXME: read EEPROM */
	for (i = 0; i < MAC_ADDR_LEN; i++)
		dev->dev_addr[i] = RTL_R8(MAC0 + i);
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	dev->open = rtl8169_open;
	dev->hard_start_xmit = rtl8169_start_xmit;
	dev->get_stats = rtl8169_get_stats;
	SET_ETHTOOL_OPS(dev, &rtl8169_ethtool_ops);
	dev->stop = rtl8169_close;
	dev->tx_timeout = rtl8169_tx_timeout;
	dev->set_multicast_list = rtl8169_set_rx_mode;
	dev->watchdog_timeo = RTL8169_TX_TIMEOUT;
	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) ioaddr;
	dev->change_mtu = rtl8169_change_mtu;

#ifdef CONFIG_R8169_NAPI
	dev->poll = rtl8169_poll;
	dev->weight = R8169_NAPI_WEIGHT;
#endif

#ifdef CONFIG_R8169_VLAN
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	dev->vlan_rx_register = rtl8169_vlan_rx_register;
	dev->vlan_rx_kill_vid = rtl8169_vlan_rx_kill_vid;
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = rtl8169_netpoll;
#endif

	tp->intr_mask = 0xffff;
	tp->pci_dev = pdev;
	tp->mmio_addr = ioaddr;
	tp->align = rtl_cfg_info[ent->driver_data].align;

	init_timer(&tp->timer);
	tp->timer.data = (unsigned long) dev;
	tp->timer.function = rtl8169_phy_timer;

	spin_lock_init(&tp->lock);

	rc = register_netdev(dev);
	if (rc < 0)
		goto err_out_unmap_5;

	pci_set_drvdata(pdev, dev);

	if (netif_msg_probe(tp)) {
		printk(KERN_INFO "%s: %s at 0x%lx, "
		       "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x, "
		       "IRQ %d\n",
		       dev->name,
		       rtl_chip_info[tp->chipset].name,
		       dev->base_addr,
		       dev->dev_addr[0], dev->dev_addr[1],
		       dev->dev_addr[2], dev->dev_addr[3],
		       dev->dev_addr[4], dev->dev_addr[5], dev->irq);
	}

	rtl8169_init_phy(dev, tp);

out:
	return rc;

err_out_unmap_5:
	iounmap(ioaddr);
err_out_free_res_4:
	pci_release_regions(pdev);
err_out_mwi_3:
	pci_clear_mwi(pdev);
err_out_disable_2:
	pci_disable_device(pdev);
err_out_free_dev_1:
	free_netdev(dev);
	goto out;
}

static void __devexit
rtl8169_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	assert(dev != NULL);
	assert(tp != NULL);

	flush_scheduled_work();

	unregister_netdev(dev);
	rtl8169_release_board(pdev, dev, tp->mmio_addr);
	pci_set_drvdata(pdev, NULL);
}

static void rtl8169_set_rxbufsize(struct rtl8169_private *tp,
				  struct net_device *dev)
{
	unsigned int mtu = dev->mtu;

	tp->rx_buf_sz = (mtu > RX_BUF_SIZE) ? mtu + ETH_HLEN + 8 : RX_BUF_SIZE;
}

static int rtl8169_open(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	int retval = -ENOMEM;


	rtl8169_set_rxbufsize(tp, dev);

	/*
	 * Rx and Tx desscriptors needs 256 bytes alignment.
	 * pci_alloc_consistent provides more.
	 */
	tp->TxDescArray = pci_alloc_consistent(pdev, R8169_TX_RING_BYTES,
					       &tp->TxPhyAddr);
	if (!tp->TxDescArray)
		goto out;

	tp->RxDescArray = pci_alloc_consistent(pdev, R8169_RX_RING_BYTES,
					       &tp->RxPhyAddr);
	if (!tp->RxDescArray)
		goto err_free_tx_0;

	retval = rtl8169_init_ring(dev);
	if (retval < 0)
		goto err_free_rx_1;

	INIT_DELAYED_WORK(&tp->task, NULL);

	smp_mb();

	retval = request_irq(dev->irq, rtl8169_interrupt, IRQF_SHARED,
			     dev->name, dev);
	if (retval < 0)
		goto err_release_ring_2;

	rtl8169_hw_start(dev);

	rtl8169_request_timer(dev);

	rtl8169_check_link_status(dev, tp, tp->mmio_addr);
out:
	return retval;

err_release_ring_2:
	rtl8169_rx_clear(tp);
err_free_rx_1:
	pci_free_consistent(pdev, R8169_RX_RING_BYTES, tp->RxDescArray,
			    tp->RxPhyAddr);
err_free_tx_0:
	pci_free_consistent(pdev, R8169_TX_RING_BYTES, tp->TxDescArray,
			    tp->TxPhyAddr);
	goto out;
}

static void rtl8169_hw_reset(void __iomem *ioaddr)
{
	/* Disable interrupts */
	rtl8169_irq_mask_and_ack(ioaddr);

	/* Reset the chipset */
	RTL_W8(ChipCmd, CmdReset);

	/* PCI commit */
	RTL_R8(ChipCmd);
}

static void rtl8169_set_rx_tx_config_registers(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	u32 cfg = rtl8169_rx_config;

	cfg |= (RTL_R32(RxConfig) & rtl_chip_info[tp->chipset].RxConfigMask);
	RTL_W32(RxConfig, cfg);

	/* Set DMA burst size and Interframe Gap Time */
	RTL_W32(TxConfig, (TX_DMA_BURST << TxDMAShift) |
		(InterFrameGap << TxInterFrameGapShift));
}

static void rtl8169_hw_start(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_dev *pdev = tp->pci_dev;
	u16 cmd;
	u32 i;

	/* Soft reset the chip. */
	RTL_W8(ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 100; i > 0; i--) {
		if ((RTL_R8(ChipCmd) & CmdReset) == 0)
			break;
		msleep_interruptible(1);
	}

	if (tp->mac_version == RTL_GIGA_MAC_VER_05) {
		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) | PCIMulRW);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, 0x08);
	}

	if (tp->mac_version == RTL_GIGA_MAC_VER_13) {
		pci_write_config_word(pdev, 0x68, 0x00);
		pci_write_config_word(pdev, 0x69, 0x08);
	}

	/* Undocumented stuff. */
	if (tp->mac_version == RTL_GIGA_MAC_VER_05) {
		/* Realtek's r1000_n.c driver uses '&& 0x01' here. Well... */
		if ((RTL_R8(Config2) & 0x07) & 0x01)
			RTL_W32(0x7c, 0x0007ffff);

		RTL_W32(0x7c, 0x0007ff00);

		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		cmd = cmd & 0xef;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	if ((tp->mac_version == RTL_GIGA_MAC_VER_01) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_02) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_03) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_04))
		RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);

	RTL_W8(EarlyTxThres, EarlyTxThld);

	/* Low hurts. Let's disable the filtering. */
	RTL_W16(RxMaxSize, 16383);

	if ((tp->mac_version == RTL_GIGA_MAC_VER_01) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_02) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_03) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_04))
		rtl8169_set_rx_tx_config_registers(tp);

	cmd = RTL_R16(CPlusCmd);
	RTL_W16(CPlusCmd, cmd);

	tp->cp_cmd |= cmd | PCIMulRW;

	if ((tp->mac_version == RTL_GIGA_MAC_VER_02) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_03)) {
		dprintk(KERN_INFO PFX "Set MAC Reg C+CR Offset 0xE0. "
			"Bit-3 and bit-14 MUST be 1\n");
		tp->cp_cmd |= (1 << 14);
	}

	RTL_W16(CPlusCmd, tp->cp_cmd);

	/*
	 * Undocumented corner. Supposedly:
	 * (TxTimer << 12) | (TxPackets << 8) | (RxTimer << 4) | RxPackets
	 */
	RTL_W16(IntrMitigate, 0x0000);

	/*
	 * Magic spell: some iop3xx ARM board needs the TxDescAddrHigh
	 * register to be written before TxDescAddrLow to work.
	 * Switching from MMIO to I/O access fixes the issue as well.
	 */
	RTL_W32(TxDescStartAddrHigh, ((u64) tp->TxPhyAddr >> 32));
	RTL_W32(TxDescStartAddrLow, ((u64) tp->TxPhyAddr & DMA_32BIT_MASK));
	RTL_W32(RxDescAddrHigh, ((u64) tp->RxPhyAddr >> 32));
	RTL_W32(RxDescAddrLow, ((u64) tp->RxPhyAddr & DMA_32BIT_MASK));

	if ((tp->mac_version != RTL_GIGA_MAC_VER_01) &&
	    (tp->mac_version != RTL_GIGA_MAC_VER_02) &&
	    (tp->mac_version != RTL_GIGA_MAC_VER_03) &&
	    (tp->mac_version != RTL_GIGA_MAC_VER_04)) {
		RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);
		rtl8169_set_rx_tx_config_registers(tp);
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);

	/* Initially a 10 us delay. Turned it into a PCI commit. - FR */
	RTL_R8(IntrMask);

	RTL_W32(RxMissed, 0);

	rtl8169_set_rx_mode(dev);

	/* no early-rx interrupts */
	RTL_W16(MultiIntr, RTL_R16(MultiIntr) & 0xF000);

	/* Enable all known interrupts by setting the interrupt mask. */
	RTL_W16(IntrMask, rtl8169_intr_mask);

	netif_start_queue(dev);
}

static int rtl8169_change_mtu(struct net_device *dev, int new_mtu)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret = 0;

	if (new_mtu < ETH_ZLEN || new_mtu > SafeMtu)
		return -EINVAL;

	dev->mtu = new_mtu;

	if (!netif_running(dev))
		goto out;

	rtl8169_down(dev);

	rtl8169_set_rxbufsize(tp, dev);

	ret = rtl8169_init_ring(dev);
	if (ret < 0)
		goto out;

	netif_poll_enable(dev);

	rtl8169_hw_start(dev);

	rtl8169_request_timer(dev);

out:
	return ret;
}

static inline void rtl8169_make_unusable_by_asic(struct RxDesc *desc)
{
	desc->addr = 0x0badbadbadbadbadull;
	desc->opts1 &= ~cpu_to_le32(DescOwn | RsvdMask);
}

static void rtl8169_free_rx_skb(struct rtl8169_private *tp,
				struct sk_buff **sk_buff, struct RxDesc *desc)
{
	struct pci_dev *pdev = tp->pci_dev;

	pci_unmap_single(pdev, le64_to_cpu(desc->addr), tp->rx_buf_sz,
			 PCI_DMA_FROMDEVICE);
	dev_kfree_skb(*sk_buff);
	*sk_buff = NULL;
	rtl8169_make_unusable_by_asic(desc);
}

static inline void rtl8169_mark_to_asic(struct RxDesc *desc, u32 rx_buf_sz)
{
	u32 eor = le32_to_cpu(desc->opts1) & RingEnd;

	desc->opts1 = cpu_to_le32(DescOwn | eor | rx_buf_sz);
}

static inline void rtl8169_map_to_asic(struct RxDesc *desc, dma_addr_t mapping,
				       u32 rx_buf_sz)
{
	desc->addr = cpu_to_le64(mapping);
	wmb();
	rtl8169_mark_to_asic(desc, rx_buf_sz);
}

static int rtl8169_alloc_rx_skb(struct pci_dev *pdev, struct sk_buff **sk_buff,
				struct RxDesc *desc, int rx_buf_sz,
				unsigned int align)
{
	struct sk_buff *skb;
	dma_addr_t mapping;
	int ret = 0;

	skb = dev_alloc_skb(rx_buf_sz + align);
	if (!skb)
		goto err_out;

	skb_reserve(skb, (align - 1) & (unsigned long)skb->data);
	*sk_buff = skb;

	mapping = pci_map_single(pdev, skb->data, rx_buf_sz,
				 PCI_DMA_FROMDEVICE);

	rtl8169_map_to_asic(desc, mapping, rx_buf_sz);

out:
	return ret;

err_out:
	ret = -ENOMEM;
	rtl8169_make_unusable_by_asic(desc);
	goto out;
}

static void rtl8169_rx_clear(struct rtl8169_private *tp)
{
	int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if (tp->Rx_skbuff[i]) {
			rtl8169_free_rx_skb(tp, tp->Rx_skbuff + i,
					    tp->RxDescArray + i);
		}
	}
}

static u32 rtl8169_rx_fill(struct rtl8169_private *tp, struct net_device *dev,
			   u32 start, u32 end)
{
	u32 cur;

	for (cur = start; end - cur > 0; cur++) {
		int ret, i = cur % NUM_RX_DESC;

		if (tp->Rx_skbuff[i])
			continue;

		ret = rtl8169_alloc_rx_skb(tp->pci_dev, tp->Rx_skbuff + i,
			tp->RxDescArray + i, tp->rx_buf_sz, tp->align);
		if (ret < 0)
			break;
	}
	return cur - start;
}

static inline void rtl8169_mark_as_last_descriptor(struct RxDesc *desc)
{
	desc->opts1 |= cpu_to_le32(RingEnd);
}

static void rtl8169_init_ring_indexes(struct rtl8169_private *tp)
{
	tp->dirty_tx = tp->dirty_rx = tp->cur_tx = tp->cur_rx = 0;
}

static int rtl8169_init_ring(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_init_ring_indexes(tp);

	memset(tp->tx_skb, 0x0, NUM_TX_DESC * sizeof(struct ring_info));
	memset(tp->Rx_skbuff, 0x0, NUM_RX_DESC * sizeof(struct sk_buff *));

	if (rtl8169_rx_fill(tp, dev, 0, NUM_RX_DESC) != NUM_RX_DESC)
		goto err_out;

	rtl8169_mark_as_last_descriptor(tp->RxDescArray + NUM_RX_DESC - 1);

	return 0;

err_out:
	rtl8169_rx_clear(tp);
	return -ENOMEM;
}

static void rtl8169_unmap_tx_skb(struct pci_dev *pdev, struct ring_info *tx_skb,
				 struct TxDesc *desc)
{
	unsigned int len = tx_skb->len;

	pci_unmap_single(pdev, le64_to_cpu(desc->addr), len, PCI_DMA_TODEVICE);
	desc->opts1 = 0x00;
	desc->opts2 = 0x00;
	desc->addr = 0x00;
	tx_skb->len = 0;
}

static void rtl8169_tx_clear(struct rtl8169_private *tp)
{
	unsigned int i;

	for (i = tp->dirty_tx; i < tp->dirty_tx + NUM_TX_DESC; i++) {
		unsigned int entry = i % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		unsigned int len = tx_skb->len;

		if (len) {
			struct sk_buff *skb = tx_skb->skb;

			rtl8169_unmap_tx_skb(tp->pci_dev, tx_skb,
					     tp->TxDescArray + entry);
			if (skb) {
				dev_kfree_skb(skb);
				tx_skb->skb = NULL;
			}
			tp->stats.tx_dropped++;
		}
	}
	tp->cur_tx = tp->dirty_tx = 0;
}

static void rtl8169_schedule_work(struct net_device *dev, work_func_t task)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	PREPARE_DELAYED_WORK(&tp->task, task);
	schedule_delayed_work(&tp->task, 4);
}

static void rtl8169_wait_for_quiescence(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	synchronize_irq(dev->irq);

	/* Wait for any pending NAPI task to complete */
	netif_poll_disable(dev);

	rtl8169_irq_mask_and_ack(ioaddr);

	netif_poll_enable(dev);
}

static void rtl8169_reinit_task(struct work_struct *work)
{
	struct rtl8169_private *tp =
		container_of(work, struct rtl8169_private, task.work);
	struct net_device *dev = tp->dev;
	int ret;

	rtnl_lock();

	if (!netif_running(dev))
		goto out_unlock;

	rtl8169_wait_for_quiescence(dev);
	rtl8169_close(dev);

	ret = rtl8169_open(dev);
	if (unlikely(ret < 0)) {
		if (net_ratelimit()) {
			struct rtl8169_private *tp = netdev_priv(dev);

			if (netif_msg_drv(tp)) {
				printk(PFX KERN_ERR
				       "%s: reinit failure (status = %d)."
				       " Rescheduling.\n", dev->name, ret);
			}
		}
		rtl8169_schedule_work(dev, rtl8169_reinit_task);
	}

out_unlock:
	rtnl_unlock();
}

static void rtl8169_reset_task(struct work_struct *work)
{
	struct rtl8169_private *tp =
		container_of(work, struct rtl8169_private, task.work);
	struct net_device *dev = tp->dev;

	rtnl_lock();

	if (!netif_running(dev))
		goto out_unlock;

	rtl8169_wait_for_quiescence(dev);

	rtl8169_rx_interrupt(dev, tp, tp->mmio_addr);
	rtl8169_tx_clear(tp);

	if (tp->dirty_rx == tp->cur_rx) {
		rtl8169_init_ring_indexes(tp);
		rtl8169_hw_start(dev);
		netif_wake_queue(dev);
	} else {
		if (net_ratelimit()) {
			struct rtl8169_private *tp = netdev_priv(dev);

			if (netif_msg_intr(tp)) {
				printk(PFX KERN_EMERG
				       "%s: Rx buffers shortage\n", dev->name);
			}
		}
		rtl8169_schedule_work(dev, rtl8169_reset_task);
	}

out_unlock:
	rtnl_unlock();
}

static void rtl8169_tx_timeout(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_hw_reset(tp->mmio_addr);

	/* Let's wait a bit while any (async) irq lands on */
	rtl8169_schedule_work(dev, rtl8169_reset_task);
}

static int rtl8169_xmit_frags(struct rtl8169_private *tp, struct sk_buff *skb,
			      u32 opts1)
{
	struct skb_shared_info *info = skb_shinfo(skb);
	unsigned int cur_frag, entry;
	struct TxDesc *txd;

	entry = tp->cur_tx;
	for (cur_frag = 0; cur_frag < info->nr_frags; cur_frag++) {
		skb_frag_t *frag = info->frags + cur_frag;
		dma_addr_t mapping;
		u32 status, len;
		void *addr;

		entry = (entry + 1) % NUM_TX_DESC;

		txd = tp->TxDescArray + entry;
		len = frag->size;
		addr = ((void *) page_address(frag->page)) + frag->page_offset;
		mapping = pci_map_single(tp->pci_dev, addr, len, PCI_DMA_TODEVICE);

		/* anti gcc 2.95.3 bugware (sic) */
		status = opts1 | len | (RingEnd * !((entry + 1) % NUM_TX_DESC));

		txd->opts1 = cpu_to_le32(status);
		txd->addr = cpu_to_le64(mapping);

		tp->tx_skb[entry].len = len;
	}

	if (cur_frag) {
		tp->tx_skb[entry].skb = skb;
		txd->opts1 |= cpu_to_le32(LastFrag);
	}

	return cur_frag;
}

static inline u32 rtl8169_tso_csum(struct sk_buff *skb, struct net_device *dev)
{
	if (dev->features & NETIF_F_TSO) {
		u32 mss = skb_shinfo(skb)->gso_size;

		if (mss)
			return LargeSend | ((mss & MSSMask) << MSSShift);
	}
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const struct iphdr *ip = ip_hdr(skb);

		if (ip->protocol == IPPROTO_TCP)
			return IPCS | TCPCS;
		else if (ip->protocol == IPPROTO_UDP)
			return IPCS | UDPCS;
		WARN_ON(1);	/* we need a WARN() */
	}
	return 0;
}

static int rtl8169_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned int frags, entry = tp->cur_tx % NUM_TX_DESC;
	struct TxDesc *txd = tp->TxDescArray + entry;
	void __iomem *ioaddr = tp->mmio_addr;
	dma_addr_t mapping;
	u32 status, len;
	u32 opts1;
	int ret = NETDEV_TX_OK;

	if (unlikely(TX_BUFFS_AVAIL(tp) < skb_shinfo(skb)->nr_frags)) {
		if (netif_msg_drv(tp)) {
			printk(KERN_ERR
			       "%s: BUG! Tx Ring full when queue awake!\n",
			       dev->name);
		}
		goto err_stop;
	}

	if (unlikely(le32_to_cpu(txd->opts1) & DescOwn))
		goto err_stop;

	opts1 = DescOwn | rtl8169_tso_csum(skb, dev);

	frags = rtl8169_xmit_frags(tp, skb, opts1);
	if (frags) {
		len = skb_headlen(skb);
		opts1 |= FirstFrag;
	} else {
		len = skb->len;

		if (unlikely(len < ETH_ZLEN)) {
			if (skb_padto(skb, ETH_ZLEN))
				goto err_update_stats;
			len = ETH_ZLEN;
		}

		opts1 |= FirstFrag | LastFrag;
		tp->tx_skb[entry].skb = skb;
	}

	mapping = pci_map_single(tp->pci_dev, skb->data, len, PCI_DMA_TODEVICE);

	tp->tx_skb[entry].len = len;
	txd->addr = cpu_to_le64(mapping);
	txd->opts2 = cpu_to_le32(rtl8169_tx_vlan_tag(tp, skb));

	wmb();

	/* anti gcc 2.95.3 bugware (sic) */
	status = opts1 | len | (RingEnd * !((entry + 1) % NUM_TX_DESC));
	txd->opts1 = cpu_to_le32(status);

	dev->trans_start = jiffies;

	tp->cur_tx += frags + 1;

	smp_wmb();

	RTL_W8(TxPoll, 0x40);	/* set polling bit */

	if (TX_BUFFS_AVAIL(tp) < MAX_SKB_FRAGS) {
		netif_stop_queue(dev);
		smp_rmb();
		if (TX_BUFFS_AVAIL(tp) >= MAX_SKB_FRAGS)
			netif_wake_queue(dev);
	}

out:
	return ret;

err_stop:
	netif_stop_queue(dev);
	ret = NETDEV_TX_BUSY;
err_update_stats:
	tp->stats.tx_dropped++;
	goto out;
}

static void rtl8169_pcierr_interrupt(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	void __iomem *ioaddr = tp->mmio_addr;
	u16 pci_status, pci_cmd;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
	pci_read_config_word(pdev, PCI_STATUS, &pci_status);

	if (netif_msg_intr(tp)) {
		printk(KERN_ERR
		       "%s: PCI error (cmd = 0x%04x, status = 0x%04x).\n",
		       dev->name, pci_cmd, pci_status);
	}

	/*
	 * The recovery sequence below admits a very elaborated explanation:
	 * - it seems to work;
	 * - I did not see what else could be done;
	 * - it makes iop3xx happy.
	 *
	 * Feel free to adjust to your needs.
	 */
	if (pdev->broken_parity_status)
		pci_cmd &= ~PCI_COMMAND_PARITY;
	else
		pci_cmd |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY;

	pci_write_config_word(pdev, PCI_COMMAND, pci_cmd);

	pci_write_config_word(pdev, PCI_STATUS,
		pci_status & (PCI_STATUS_DETECTED_PARITY |
		PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_REC_MASTER_ABORT |
		PCI_STATUS_REC_TARGET_ABORT | PCI_STATUS_SIG_TARGET_ABORT));

	/* The infamous DAC f*ckup only happens at boot time */
	if ((tp->cp_cmd & PCIDAC) && !tp->dirty_rx && !tp->cur_rx) {
		if (netif_msg_intr(tp))
			printk(KERN_INFO "%s: disabling PCI DAC.\n", dev->name);
		tp->cp_cmd &= ~PCIDAC;
		RTL_W16(CPlusCmd, tp->cp_cmd);
		dev->features &= ~NETIF_F_HIGHDMA;
	}

	rtl8169_hw_reset(ioaddr);

	rtl8169_schedule_work(dev, rtl8169_reinit_task);
}

static void
rtl8169_tx_interrupt(struct net_device *dev, struct rtl8169_private *tp,
		     void __iomem *ioaddr)
{
	unsigned int dirty_tx, tx_left;

	assert(dev != NULL);
	assert(tp != NULL);
	assert(ioaddr != NULL);

	dirty_tx = tp->dirty_tx;
	smp_rmb();
	tx_left = tp->cur_tx - dirty_tx;

	while (tx_left > 0) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		u32 len = tx_skb->len;
		u32 status;

		rmb();
		status = le32_to_cpu(tp->TxDescArray[entry].opts1);
		if (status & DescOwn)
			break;

		tp->stats.tx_bytes += len;
		tp->stats.tx_packets++;

		rtl8169_unmap_tx_skb(tp->pci_dev, tx_skb, tp->TxDescArray + entry);

		if (status & LastFrag) {
			dev_kfree_skb_irq(tx_skb->skb);
			tx_skb->skb = NULL;
		}
		dirty_tx++;
		tx_left--;
	}

	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		smp_wmb();
		if (netif_queue_stopped(dev) &&
		    (TX_BUFFS_AVAIL(tp) >= MAX_SKB_FRAGS)) {
			netif_wake_queue(dev);
		}
	}
}

static inline int rtl8169_fragmented_frame(u32 status)
{
	return (status & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag);
}

static inline void rtl8169_rx_csum(struct sk_buff *skb, struct RxDesc *desc)
{
	u32 opts1 = le32_to_cpu(desc->opts1);
	u32 status = opts1 & RxProtoMask;

	if (((status == RxProtoTCP) && !(opts1 & TCPFail)) ||
	    ((status == RxProtoUDP) && !(opts1 & UDPFail)) ||
	    ((status == RxProtoIP) && !(opts1 & IPFail)))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb->ip_summed = CHECKSUM_NONE;
}

static inline int rtl8169_try_rx_copy(struct sk_buff **sk_buff, int pkt_size,
				      struct RxDesc *desc, int rx_buf_sz,
				      unsigned int align)
{
	int ret = -1;

	if (pkt_size < rx_copybreak) {
		struct sk_buff *skb;

		skb = dev_alloc_skb(pkt_size + align);
		if (skb) {
			skb_reserve(skb, (align - 1) & (unsigned long)skb->data);
			eth_copy_and_sum(skb, sk_buff[0]->data, pkt_size, 0);
			*sk_buff = skb;
			rtl8169_mark_to_asic(desc, rx_buf_sz);
			ret = 0;
		}
	}
	return ret;
}

static int
rtl8169_rx_interrupt(struct net_device *dev, struct rtl8169_private *tp,
		     void __iomem *ioaddr)
{
	unsigned int cur_rx, rx_left;
	unsigned int delta, count;

	assert(dev != NULL);
	assert(tp != NULL);
	assert(ioaddr != NULL);

	cur_rx = tp->cur_rx;
	rx_left = NUM_RX_DESC + tp->dirty_rx - cur_rx;
	rx_left = rtl8169_rx_quota(rx_left, (u32) dev->quota);

	for (; rx_left > 0; rx_left--, cur_rx++) {
		unsigned int entry = cur_rx % NUM_RX_DESC;
		struct RxDesc *desc = tp->RxDescArray + entry;
		u32 status;

		rmb();
		status = le32_to_cpu(desc->opts1);

		if (status & DescOwn)
			break;
		if (unlikely(status & RxRES)) {
			if (netif_msg_rx_err(tp)) {
				printk(KERN_INFO
				       "%s: Rx ERROR. status = %08x\n",
				       dev->name, status);
			}
			tp->stats.rx_errors++;
			if (status & (RxRWT | RxRUNT))
				tp->stats.rx_length_errors++;
			if (status & RxCRC)
				tp->stats.rx_crc_errors++;
			if (status & RxFOVF) {
				rtl8169_schedule_work(dev, rtl8169_reset_task);
				tp->stats.rx_fifo_errors++;
			}
			rtl8169_mark_to_asic(desc, tp->rx_buf_sz);
		} else {
			struct sk_buff *skb = tp->Rx_skbuff[entry];
			int pkt_size = (status & 0x00001FFF) - 4;
			void (*pci_action)(struct pci_dev *, dma_addr_t,
				size_t, int) = pci_dma_sync_single_for_device;

			/*
			 * The driver does not support incoming fragmented
			 * frames. They are seen as a symptom of over-mtu
			 * sized frames.
			 */
			if (unlikely(rtl8169_fragmented_frame(status))) {
				tp->stats.rx_dropped++;
				tp->stats.rx_length_errors++;
				rtl8169_mark_to_asic(desc, tp->rx_buf_sz);
				continue;
			}

			rtl8169_rx_csum(skb, desc);

			pci_dma_sync_single_for_cpu(tp->pci_dev,
				le64_to_cpu(desc->addr), tp->rx_buf_sz,
				PCI_DMA_FROMDEVICE);

			if (rtl8169_try_rx_copy(&skb, pkt_size, desc,
						tp->rx_buf_sz, tp->align)) {
				pci_action = pci_unmap_single;
				tp->Rx_skbuff[entry] = NULL;
			}

			pci_action(tp->pci_dev, le64_to_cpu(desc->addr),
				   tp->rx_buf_sz, PCI_DMA_FROMDEVICE);

			skb_put(skb, pkt_size);
			skb->protocol = eth_type_trans(skb, dev);

			if (rtl8169_rx_vlan_skb(tp, desc, skb) < 0)
				rtl8169_rx_skb(skb);

			dev->last_rx = jiffies;
			tp->stats.rx_bytes += pkt_size;
			tp->stats.rx_packets++;
		}
	}

	count = cur_rx - tp->cur_rx;
	tp->cur_rx = cur_rx;

	delta = rtl8169_rx_fill(tp, dev, tp->dirty_rx, tp->cur_rx);
	if (!delta && count && netif_msg_intr(tp))
		printk(KERN_INFO "%s: no Rx buffer allocated\n", dev->name);
	tp->dirty_rx += delta;

	/*
	 * FIXME: until there is periodic timer to try and refill the ring,
	 * a temporary shortage may definitely kill the Rx process.
	 * - disable the asic to try and avoid an overflow and kick it again
	 *   after refill ?
	 * - how do others driver handle this condition (Uh oh...).
	 */
	if ((tp->dirty_rx + NUM_RX_DESC == tp->cur_rx) && netif_msg_intr(tp))
		printk(KERN_EMERG "%s: Rx buffers exhausted\n", dev->name);

	return count;
}

/* The interrupt handler does all of the Rx thread work and cleans up after the Tx thread. */
static irqreturn_t
rtl8169_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct rtl8169_private *tp = netdev_priv(dev);
	int boguscnt = max_interrupt_work;
	void __iomem *ioaddr = tp->mmio_addr;
	int status;
	int handled = 0;

	do {
		status = RTL_R16(IntrStatus);

		/* hotplug/major error/no more work/shared irq */
		if ((status == 0xFFFF) || !status)
			break;

		handled = 1;

		if (unlikely(!netif_running(dev))) {
			rtl8169_asic_down(ioaddr);
			goto out;
		}

		status &= tp->intr_mask;
		RTL_W16(IntrStatus,
			(status & RxFIFOOver) ? (status | RxOverflow) : status);

		if (!(status & rtl8169_intr_mask))
			break;

		if (unlikely(status & SYSErr)) {
			rtl8169_pcierr_interrupt(dev);
			break;
		}

		if (status & LinkChg)
			rtl8169_check_link_status(dev, tp, ioaddr);

#ifdef CONFIG_R8169_NAPI
		RTL_W16(IntrMask, rtl8169_intr_mask & ~rtl8169_napi_event);
		tp->intr_mask = ~rtl8169_napi_event;

		if (likely(netif_rx_schedule_prep(dev)))
			__netif_rx_schedule(dev);
		else if (netif_msg_intr(tp)) {
			printk(KERN_INFO "%s: interrupt %04x taken in poll\n",
			       dev->name, status);
		}
		break;
#else
		/* Rx interrupt */
		if (status & (RxOK | RxOverflow | RxFIFOOver)) {
			rtl8169_rx_interrupt(dev, tp, ioaddr);
		}
		/* Tx interrupt */
		if (status & (TxOK | TxErr))
			rtl8169_tx_interrupt(dev, tp, ioaddr);
#endif

		boguscnt--;
	} while (boguscnt > 0);

	if (boguscnt <= 0) {
		if (netif_msg_intr(tp) && net_ratelimit() ) {
			printk(KERN_WARNING
			       "%s: Too much work at interrupt!\n", dev->name);
		}
		/* Clear all interrupt sources. */
		RTL_W16(IntrStatus, 0xffff);
	}
out:
	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_R8169_NAPI
static int rtl8169_poll(struct net_device *dev, int *budget)
{
	unsigned int work_done, work_to_do = min(*budget, dev->quota);
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	work_done = rtl8169_rx_interrupt(dev, tp, ioaddr);
	rtl8169_tx_interrupt(dev, tp, ioaddr);

	*budget -= work_done;
	dev->quota -= work_done;

	if (work_done < work_to_do) {
		netif_rx_complete(dev);
		tp->intr_mask = 0xffff;
		/*
		 * 20040426: the barrier is not strictly required but the
		 * behavior of the irq handler could be less predictable
		 * without it. Btw, the lack of flush for the posted pci
		 * write is safe - FR
		 */
		smp_wmb();
		RTL_W16(IntrMask, rtl8169_intr_mask);
	}

	return (work_done >= work_to_do);
}
#endif

static void rtl8169_down(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int poll_locked = 0;
	unsigned int intrmask;

	rtl8169_delete_timer(dev);

	netif_stop_queue(dev);

core_down:
	spin_lock_irq(&tp->lock);

	rtl8169_asic_down(ioaddr);

	/* Update the error counts. */
	tp->stats.rx_missed_errors += RTL_R32(RxMissed);
	RTL_W32(RxMissed, 0);

	spin_unlock_irq(&tp->lock);

	synchronize_irq(dev->irq);

	if (!poll_locked) {
		netif_poll_disable(dev);
		poll_locked++;
	}

	/* Give a racing hard_start_xmit a few cycles to complete. */
	synchronize_sched();  /* FIXME: should this be synchronize_irq()? */

	/*
	 * And now for the 50k$ question: are IRQ disabled or not ?
	 *
	 * Two paths lead here:
	 * 1) dev->close
	 *    -> netif_running() is available to sync the current code and the
	 *       IRQ handler. See rtl8169_interrupt for details.
	 * 2) dev->change_mtu
	 *    -> rtl8169_poll can not be issued again and re-enable the
	 *       interruptions. Let's simply issue the IRQ down sequence again.
	 *
	 * No loop if hotpluged or major error (0xffff).
	 */
	intrmask = RTL_R16(IntrMask);
	if (intrmask && (intrmask != 0xffff))
		goto core_down;

	rtl8169_tx_clear(tp);

	rtl8169_rx_clear(tp);
}

static int rtl8169_close(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;

	rtl8169_down(dev);

	free_irq(dev->irq, dev);

	netif_poll_enable(dev);

	pci_free_consistent(pdev, R8169_RX_RING_BYTES, tp->RxDescArray,
			    tp->RxPhyAddr);
	pci_free_consistent(pdev, R8169_TX_RING_BYTES, tp->TxDescArray,
			    tp->TxPhyAddr);
	tp->TxDescArray = NULL;
	tp->RxDescArray = NULL;

	return 0;
}

static void
rtl8169_set_rx_mode(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;
	u32 mc_filter[2];	/* Multicast hash filter */
	int i, rx_mode;
	u32 tmp = 0;

	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		if (netif_msg_link(tp)) {
			printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n",
			       dev->name);
		}
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((dev->mc_count > multicast_filter_limit)
		   || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct dev_mc_list *mclist;
		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {
			int bit_nr = ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}

	spin_lock_irqsave(&tp->lock, flags);

	tmp = rtl8169_rx_config | rx_mode |
	      (RTL_R32(RxConfig) & rtl_chip_info[tp->chipset].RxConfigMask);

	if ((tp->mac_version == RTL_GIGA_MAC_VER_11) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_12) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_13) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_14) ||
	    (tp->mac_version == RTL_GIGA_MAC_VER_15)) {
		mc_filter[0] = 0xffffffff;
		mc_filter[1] = 0xffffffff;
	}

	RTL_W32(RxConfig, tmp);
	RTL_W32(MAR0 + 0, mc_filter[0]);
	RTL_W32(MAR0 + 4, mc_filter[1]);

	spin_unlock_irqrestore(&tp->lock, flags);
}

/**
 *  rtl8169_get_stats - Get rtl8169 read/write statistics
 *  @dev: The Ethernet Device to get statistics for
 *
 *  Get TX/RX statistics for rtl8169
 */
static struct net_device_stats *rtl8169_get_stats(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;

	if (netif_running(dev)) {
		spin_lock_irqsave(&tp->lock, flags);
		tp->stats.rx_missed_errors += RTL_R32(RxMissed);
		RTL_W32(RxMissed, 0);
		spin_unlock_irqrestore(&tp->lock, flags);
	}

	return &tp->stats;
}

#ifdef CONFIG_PM

static int rtl8169_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	if (!netif_running(dev))
		goto out_pci_suspend;

	netif_device_detach(dev);
	netif_stop_queue(dev);

	spin_lock_irq(&tp->lock);

	rtl8169_asic_down(ioaddr);

	tp->stats.rx_missed_errors += RTL_R32(RxMissed);
	RTL_W32(RxMissed, 0);

	spin_unlock_irq(&tp->lock);

out_pci_suspend:
	pci_save_state(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev, state), tp->wol_enabled);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int rtl8169_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);

	if (!netif_running(dev))
		goto out;

	netif_device_attach(dev);

	rtl8169_schedule_work(dev, rtl8169_reset_task);
out:
	return 0;
}

#endif /* CONFIG_PM */

static struct pci_driver rtl8169_pci_driver = {
	.name		= MODULENAME,
	.id_table	= rtl8169_pci_tbl,
	.probe		= rtl8169_init_one,
	.remove		= __devexit_p(rtl8169_remove_one),
#ifdef CONFIG_PM
	.suspend	= rtl8169_suspend,
	.resume		= rtl8169_resume,
#endif
};

static int __init
rtl8169_init_module(void)
{
	return pci_register_driver(&rtl8169_pci_driver);
}

static void __exit
rtl8169_cleanup_module(void)
{
	pci_unregister_driver(&rtl8169_pci_driver);
}

module_init(rtl8169_init_module);
module_exit(rtl8169_cleanup_module);
