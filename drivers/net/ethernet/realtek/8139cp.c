/* 8139cp.c: A Linux PCI Ethernet driver for the RealTek 8139C+ chips. */
/*
	Copyright 2001-2004 Jeff Garzik <jgarzik@pobox.com>

	Copyright (C) 2001, 2002 David S. Miller (davem@redhat.com) [tg3.c]
	Copyright (C) 2000, 2001 David S. Miller (davem@redhat.com) [sungem.c]
	Copyright 2001 Manfred Spraul				    [natsemi.c]
	Copyright 1999-2001 by Donald Becker.			    [natsemi.c]
       	Written 1997-2001 by Donald Becker.			    [8139too.c]
	Copyright 1998-2001 by Jes Sorensen, <jes@trained-monkey.org>. [acenic.c]

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	See the file COPYING in this distribution for more information.

	Contributors:

		Wake-on-LAN support - Felipe Damasio <felipewd@terra.com.br>
		PCI suspend/resume  - Felipe Damasio <felipewd@terra.com.br>
		LinkChg interrupt   - Felipe Damasio <felipewd@terra.com.br>

	TODO:
	* Test Tx checksumming thoroughly

	Low priority TODO:
	* Complete reset on PciErr
	* Consider Rx interrupt mitigation using TimerIntr
	* Investigate using skb->priority with h/w VLAN priority
	* Investigate using High Priority Tx Queue with skb->priority
	* Adjust Rx FIFO threshold and Max Rx DMA burst on Rx FIFO error
	* Adjust Tx FIFO threshold and Max Tx DMA burst on Tx FIFO error
	* Implement Tx software interrupt mitigation via
	  Tx descriptor bit
	* The real minimum of CP_MIN_MTU is 4 bytes.  However,
	  for this to be supported, one must(?) turn on packet padding.
	* Support external MII transceivers (patch available)

	NOTES:
	* TX checksumming is considered experimental.  It is off by
	  default, use ethtool to turn it on.

 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME		"8139cp"
#define DRV_VERSION		"1.3"
#define DRV_RELDATE		"Mar 22, 2004"


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/gfp.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/cache.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* These identify the driver base version and may not be removed. */
static char version[] =
DRV_NAME ": 10/100 PCI Ethernet driver v" DRV_VERSION " (" DRV_RELDATE ")\n";

MODULE_AUTHOR("Jeff Garzik <jgarzik@pobox.com>");
MODULE_DESCRIPTION("RealTek RTL-8139C+ series 10/100 PCI Ethernet driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");

static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC (debug, "8139cp: bitmapped message enable number");

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;
module_param(multicast_filter_limit, int, 0);
MODULE_PARM_DESC (multicast_filter_limit, "8139cp: maximum number of filtered multicast addresses");

#define CP_DEF_MSG_ENABLE	(NETIF_MSG_DRV		| \
				 NETIF_MSG_PROBE 	| \
				 NETIF_MSG_LINK)
#define CP_NUM_STATS		14	/* struct cp_dma_stats, plus one */
#define CP_STATS_SIZE		64	/* size in bytes of DMA stats block */
#define CP_REGS_SIZE		(0xff + 1)
#define CP_REGS_VER		1		/* version 1 */
#define CP_RX_RING_SIZE		64
#define CP_TX_RING_SIZE		64
#define CP_RING_BYTES		\
		((sizeof(struct cp_desc) * CP_RX_RING_SIZE) +	\
		 (sizeof(struct cp_desc) * CP_TX_RING_SIZE) +	\
		 CP_STATS_SIZE)
#define NEXT_TX(N)		(((N) + 1) & (CP_TX_RING_SIZE - 1))
#define NEXT_RX(N)		(((N) + 1) & (CP_RX_RING_SIZE - 1))
#define TX_BUFFS_AVAIL(CP)					\
	(((CP)->tx_tail <= (CP)->tx_head) ?			\
	  (CP)->tx_tail + (CP_TX_RING_SIZE - 1) - (CP)->tx_head :	\
	  (CP)->tx_tail - (CP)->tx_head - 1)

#define PKT_BUF_SZ		1536	/* Size of each temporary Rx buffer.*/
#define CP_INTERNAL_PHY		32

/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024, 7==end of packet. */
#define RX_FIFO_THRESH		5	/* Rx buffer level before first PCI xfer.  */
#define RX_DMA_BURST		4	/* Maximum PCI burst, '4' is 256 */
#define TX_DMA_BURST		6	/* Maximum PCI burst, '6' is 1024 */
#define TX_EARLY_THRESH		256	/* Early Tx threshold, in bytes */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT		(6*HZ)

/* hardware minimum and maximum for a single frame's data payload */
#define CP_MIN_MTU		60	/* TODO: allow lower, but pad */
#define CP_MAX_MTU		4096

enum {
	/* NIC register offsets */
	MAC0		= 0x00,	/* Ethernet hardware address. */
	MAR0		= 0x08,	/* Multicast filter. */
	StatsAddr	= 0x10,	/* 64-bit start addr of 64-byte DMA stats blk */
	TxRingAddr	= 0x20, /* 64-bit start addr of Tx ring */
	HiTxRingAddr	= 0x28, /* 64-bit start addr of high priority Tx ring */
	Cmd		= 0x37, /* Command register */
	IntrMask	= 0x3C, /* Interrupt mask */
	IntrStatus	= 0x3E, /* Interrupt status */
	TxConfig	= 0x40, /* Tx configuration */
	ChipVersion	= 0x43, /* 8-bit chip version, inside TxConfig */
	RxConfig	= 0x44, /* Rx configuration */
	RxMissed	= 0x4C,	/* 24 bits valid, write clears */
	Cfg9346		= 0x50, /* EEPROM select/control; Cfg reg [un]lock */
	Config1		= 0x52, /* Config1 */
	Config3		= 0x59, /* Config3 */
	Config4		= 0x5A, /* Config4 */
	MultiIntr	= 0x5C, /* Multiple interrupt select */
	BasicModeCtrl	= 0x62,	/* MII BMCR */
	BasicModeStatus	= 0x64, /* MII BMSR */
	NWayAdvert	= 0x66, /* MII ADVERTISE */
	NWayLPAR	= 0x68, /* MII LPA */
	NWayExpansion	= 0x6A, /* MII Expansion */
	TxDmaOkLowDesc  = 0x82, /* Low 16 bit address of a Tx descriptor. */
	Config5		= 0xD8,	/* Config5 */
	TxPoll		= 0xD9,	/* Tell chip to check Tx descriptors for work */
	RxMaxSize	= 0xDA, /* Max size of an Rx packet (8169 only) */
	CpCmd		= 0xE0, /* C+ Command register (C+ mode only) */
	IntrMitigate	= 0xE2,	/* rx/tx interrupt mitigation control */
	RxRingAddr	= 0xE4, /* 64-bit start addr of Rx ring */
	TxThresh	= 0xEC, /* Early Tx threshold */
	OldRxBufAddr	= 0x30, /* DMA address of Rx ring buffer (C mode) */
	OldTSD0		= 0x10, /* DMA address of first Tx desc (C mode) */

	/* Tx and Rx status descriptors */
	DescOwn		= (1 << 31), /* Descriptor is owned by NIC */
	RingEnd		= (1 << 30), /* End of descriptor ring */
	FirstFrag	= (1 << 29), /* First segment of a packet */
	LastFrag	= (1 << 28), /* Final segment of a packet */
	LargeSend	= (1 << 27), /* TCP Large Send Offload (TSO) */
	MSSShift	= 16,	     /* MSS value position */
	MSSMask		= 0x7ff,     /* MSS value: 11 bits */
	TxError		= (1 << 23), /* Tx error summary */
	RxError		= (1 << 20), /* Rx error summary */
	IPCS		= (1 << 18), /* Calculate IP checksum */
	UDPCS		= (1 << 17), /* Calculate UDP/IP checksum */
	TCPCS		= (1 << 16), /* Calculate TCP/IP checksum */
	TxVlanTag	= (1 << 17), /* Add VLAN tag */
	RxVlanTagged	= (1 << 16), /* Rx VLAN tag available */
	IPFail		= (1 << 15), /* IP checksum failed */
	UDPFail		= (1 << 14), /* UDP/IP checksum failed */
	TCPFail		= (1 << 13), /* TCP/IP checksum failed */
	NormalTxPoll	= (1 << 6),  /* One or more normal Tx packets to send */
	PID1		= (1 << 17), /* 2 protocol id bits:  0==non-IP, */
	PID0		= (1 << 16), /* 1==UDP/IP, 2==TCP/IP, 3==IP */
	RxProtoTCP	= 1,
	RxProtoUDP	= 2,
	RxProtoIP	= 3,
	TxFIFOUnder	= (1 << 25), /* Tx FIFO underrun */
	TxOWC		= (1 << 22), /* Tx Out-of-window collision */
	TxLinkFail	= (1 << 21), /* Link failed during Tx of packet */
	TxMaxCol	= (1 << 20), /* Tx aborted due to excessive collisions */
	TxColCntShift	= 16,	     /* Shift, to get 4-bit Tx collision cnt */
	TxColCntMask	= 0x01 | 0x02 | 0x04 | 0x08, /* 4-bit collision count */
	RxErrFrame	= (1 << 27), /* Rx frame alignment error */
	RxMcast		= (1 << 26), /* Rx multicast packet rcv'd */
	RxErrCRC	= (1 << 18), /* Rx CRC error */
	RxErrRunt	= (1 << 19), /* Rx error, packet < 64 bytes */
	RxErrLong	= (1 << 21), /* Rx error, packet > 4096 bytes */
	RxErrFIFO	= (1 << 22), /* Rx error, FIFO overflowed, pkt bad */

	/* StatsAddr register */
	DumpStats	= (1 << 3),  /* Begin stats dump */

	/* RxConfig register */
	RxCfgFIFOShift	= 13,	     /* Shift, to get Rx FIFO thresh value */
	RxCfgDMAShift	= 8,	     /* Shift, to get Rx Max DMA value */
	AcceptErr	= 0x20,	     /* Accept packets with CRC errors */
	AcceptRunt	= 0x10,	     /* Accept runt (<64 bytes) packets */
	AcceptBroadcast	= 0x08,	     /* Accept broadcast packets */
	AcceptMulticast	= 0x04,	     /* Accept multicast packets */
	AcceptMyPhys	= 0x02,	     /* Accept pkts with our MAC as dest */
	AcceptAllPhys	= 0x01,	     /* Accept all pkts w/ physical dest */

	/* IntrMask / IntrStatus registers */
	PciErr		= (1 << 15), /* System error on the PCI bus */
	TimerIntr	= (1 << 14), /* Asserted when TCTR reaches TimerInt value */
	LenChg		= (1 << 13), /* Cable length change */
	SWInt		= (1 << 8),  /* Software-requested interrupt */
	TxEmpty		= (1 << 7),  /* No Tx descriptors available */
	RxFIFOOvr	= (1 << 6),  /* Rx FIFO Overflow */
	LinkChg		= (1 << 5),  /* Packet underrun, or link change */
	RxEmpty		= (1 << 4),  /* No Rx descriptors available */
	TxErr		= (1 << 3),  /* Tx error */
	TxOK		= (1 << 2),  /* Tx packet sent */
	RxErr		= (1 << 1),  /* Rx error */
	RxOK		= (1 << 0),  /* Rx packet received */
	IntrResvd	= (1 << 10), /* reserved, according to RealTek engineers,
					but hardware likes to raise it */

	IntrAll		= PciErr | TimerIntr | LenChg | SWInt | TxEmpty |
			  RxFIFOOvr | LinkChg | RxEmpty | TxErr | TxOK |
			  RxErr | RxOK | IntrResvd,

	/* C mode command register */
	CmdReset	= (1 << 4),  /* Enable to reset; self-clearing */
	RxOn		= (1 << 3),  /* Rx mode enable */
	TxOn		= (1 << 2),  /* Tx mode enable */

	/* C+ mode command register */
	RxVlanOn	= (1 << 6),  /* Rx VLAN de-tagging enable */
	RxChkSum	= (1 << 5),  /* Rx checksum offload enable */
	PCIDAC		= (1 << 4),  /* PCI Dual Address Cycle (64-bit PCI) */
	PCIMulRW	= (1 << 3),  /* Enable PCI read/write multiple */
	CpRxOn		= (1 << 1),  /* Rx mode enable */
	CpTxOn		= (1 << 0),  /* Tx mode enable */

	/* Cfg9436 EEPROM control register */
	Cfg9346_Lock	= 0x00,	     /* Lock ConfigX/MII register access */
	Cfg9346_Unlock	= 0xC0,	     /* Unlock ConfigX/MII register access */

	/* TxConfig register */
	IFG		= (1 << 25) | (1 << 24), /* standard IEEE interframe gap */
	TxDMAShift	= 8,	     /* DMA burst value (0-7) is shift this many bits */

	/* Early Tx Threshold register */
	TxThreshMask	= 0x3f,	     /* Mask bits 5-0 */
	TxThreshMax	= 2048,	     /* Max early Tx threshold */

	/* Config1 register */
	DriverLoaded	= (1 << 5),  /* Software marker, driver is loaded */
	LWACT           = (1 << 4),  /* LWAKE active mode */
	PMEnable	= (1 << 0),  /* Enable various PM features of chip */

	/* Config3 register */
	PARMEnable	= (1 << 6),  /* Enable auto-loading of PHY parms */
	MagicPacket     = (1 << 5),  /* Wake up when receives a Magic Packet */
	LinkUp          = (1 << 4),  /* Wake up when the cable connection is re-established */

	/* Config4 register */
	LWPTN           = (1 << 1),  /* LWAKE Pattern */
	LWPME           = (1 << 4),  /* LANWAKE vs PMEB */

	/* Config5 register */
	BWF             = (1 << 6),  /* Accept Broadcast wakeup frame */
	MWF             = (1 << 5),  /* Accept Multicast wakeup frame */
	UWF             = (1 << 4),  /* Accept Unicast wakeup frame */
	LANWake         = (1 << 1),  /* Enable LANWake signal */
	PMEStatus	= (1 << 0),  /* PME status can be reset by PCI RST# */

	cp_norx_intr_mask = PciErr | LinkChg | TxOK | TxErr | TxEmpty,
	cp_rx_intr_mask = RxOK | RxErr | RxEmpty | RxFIFOOvr,
	cp_intr_mask = cp_rx_intr_mask | cp_norx_intr_mask,
};

static const unsigned int cp_rx_config =
	  (RX_FIFO_THRESH << RxCfgFIFOShift) |
	  (RX_DMA_BURST << RxCfgDMAShift);

struct cp_desc {
	__le32		opts1;
	__le32		opts2;
	__le64		addr;
};

struct cp_dma_stats {
	__le64			tx_ok;
	__le64			rx_ok;
	__le64			tx_err;
	__le32			rx_err;
	__le16			rx_fifo;
	__le16			frame_align;
	__le32			tx_ok_1col;
	__le32			tx_ok_mcol;
	__le64			rx_ok_phys;
	__le64			rx_ok_bcast;
	__le32			rx_ok_mcast;
	__le16			tx_abort;
	__le16			tx_underrun;
} __packed;

struct cp_extra_stats {
	unsigned long		rx_frags;
};

struct cp_private {
	void			__iomem *regs;
	struct net_device	*dev;
	spinlock_t		lock;
	u32			msg_enable;

	struct napi_struct	napi;

	struct pci_dev		*pdev;
	u32			rx_config;
	u16			cpcmd;

	struct cp_extra_stats	cp_stats;

	unsigned		rx_head		____cacheline_aligned;
	unsigned		rx_tail;
	struct cp_desc		*rx_ring;
	struct sk_buff		*rx_skb[CP_RX_RING_SIZE];

	unsigned		tx_head		____cacheline_aligned;
	unsigned		tx_tail;
	struct cp_desc		*tx_ring;
	struct sk_buff		*tx_skb[CP_TX_RING_SIZE];
	u32			tx_opts[CP_TX_RING_SIZE];

	unsigned		rx_buf_sz;
	unsigned		wol_enabled : 1; /* Is Wake-on-LAN enabled? */

	dma_addr_t		ring_dma;

	struct mii_if_info	mii_if;
};

#define cpr8(reg)	readb(cp->regs + (reg))
#define cpr16(reg)	readw(cp->regs + (reg))
#define cpr32(reg)	readl(cp->regs + (reg))
#define cpw8(reg,val)	writeb((val), cp->regs + (reg))
#define cpw16(reg,val)	writew((val), cp->regs + (reg))
#define cpw32(reg,val)	writel((val), cp->regs + (reg))
#define cpw8_f(reg,val) do {			\
	writeb((val), cp->regs + (reg));	\
	readb(cp->regs + (reg));		\
	} while (0)
#define cpw16_f(reg,val) do {			\
	writew((val), cp->regs + (reg));	\
	readw(cp->regs + (reg));		\
	} while (0)
#define cpw32_f(reg,val) do {			\
	writel((val), cp->regs + (reg));	\
	readl(cp->regs + (reg));		\
	} while (0)


static void __cp_set_rx_mode (struct net_device *dev);
static void cp_tx (struct cp_private *cp);
static void cp_clean_rings (struct cp_private *cp);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void cp_poll_controller(struct net_device *dev);
#endif
static int cp_get_eeprom_len(struct net_device *dev);
static int cp_get_eeprom(struct net_device *dev,
			 struct ethtool_eeprom *eeprom, u8 *data);
static int cp_set_eeprom(struct net_device *dev,
			 struct ethtool_eeprom *eeprom, u8 *data);

static struct {
	const char str[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "tx_ok" },
	{ "rx_ok" },
	{ "tx_err" },
	{ "rx_err" },
	{ "rx_fifo" },
	{ "frame_align" },
	{ "tx_ok_1col" },
	{ "tx_ok_mcol" },
	{ "rx_ok_phys" },
	{ "rx_ok_bcast" },
	{ "rx_ok_mcast" },
	{ "tx_abort" },
	{ "tx_underrun" },
	{ "rx_frags" },
};


static inline void cp_set_rxbufsize (struct cp_private *cp)
{
	unsigned int mtu = cp->dev->mtu;

	if (mtu > ETH_DATA_LEN)
		/* MTU + ethernet header + FCS + optional VLAN tag */
		cp->rx_buf_sz = mtu + ETH_HLEN + 8;
	else
		cp->rx_buf_sz = PKT_BUF_SZ;
}

static inline void cp_rx_skb (struct cp_private *cp, struct sk_buff *skb,
			      struct cp_desc *desc)
{
	u32 opts2 = le32_to_cpu(desc->opts2);

	skb->protocol = eth_type_trans (skb, cp->dev);

	cp->dev->stats.rx_packets++;
	cp->dev->stats.rx_bytes += skb->len;

	if (opts2 & RxVlanTagged)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), swab16(opts2 & 0xffff));

	napi_gro_receive(&cp->napi, skb);
}

static void cp_rx_err_acct (struct cp_private *cp, unsigned rx_tail,
			    u32 status, u32 len)
{
	netif_dbg(cp, rx_err, cp->dev, "rx err, slot %d status 0x%x len %d\n",
		  rx_tail, status, len);
	cp->dev->stats.rx_errors++;
	if (status & RxErrFrame)
		cp->dev->stats.rx_frame_errors++;
	if (status & RxErrCRC)
		cp->dev->stats.rx_crc_errors++;
	if ((status & RxErrRunt) || (status & RxErrLong))
		cp->dev->stats.rx_length_errors++;
	if ((status & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag))
		cp->dev->stats.rx_length_errors++;
	if (status & RxErrFIFO)
		cp->dev->stats.rx_fifo_errors++;
}

static inline unsigned int cp_rx_csum_ok (u32 status)
{
	unsigned int protocol = (status >> 16) & 0x3;

	if (((protocol == RxProtoTCP) && !(status & TCPFail)) ||
	    ((protocol == RxProtoUDP) && !(status & UDPFail)))
		return 1;
	else
		return 0;
}

static int cp_rx_poll(struct napi_struct *napi, int budget)
{
	struct cp_private *cp = container_of(napi, struct cp_private, napi);
	struct net_device *dev = cp->dev;
	unsigned int rx_tail = cp->rx_tail;
	int rx;

rx_status_loop:
	rx = 0;
	cpw16(IntrStatus, cp_rx_intr_mask);

	while (rx < budget) {
		u32 status, len;
		dma_addr_t mapping, new_mapping;
		struct sk_buff *skb, *new_skb;
		struct cp_desc *desc;
		const unsigned buflen = cp->rx_buf_sz;

		skb = cp->rx_skb[rx_tail];
		BUG_ON(!skb);

		desc = &cp->rx_ring[rx_tail];
		status = le32_to_cpu(desc->opts1);
		if (status & DescOwn)
			break;

		len = (status & 0x1fff) - 4;
		mapping = le64_to_cpu(desc->addr);

		if ((status & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag)) {
			/* we don't support incoming fragmented frames.
			 * instead, we attempt to ensure that the
			 * pre-allocated RX skbs are properly sized such
			 * that RX fragments are never encountered
			 */
			cp_rx_err_acct(cp, rx_tail, status, len);
			dev->stats.rx_dropped++;
			cp->cp_stats.rx_frags++;
			goto rx_next;
		}

		if (status & (RxError | RxErrFIFO)) {
			cp_rx_err_acct(cp, rx_tail, status, len);
			goto rx_next;
		}

		netif_dbg(cp, rx_status, dev, "rx slot %d status 0x%x len %d\n",
			  rx_tail, status, len);

		new_skb = napi_alloc_skb(napi, buflen);
		if (!new_skb) {
			dev->stats.rx_dropped++;
			goto rx_next;
		}

		new_mapping = dma_map_single(&cp->pdev->dev, new_skb->data, buflen,
					 PCI_DMA_FROMDEVICE);
		if (dma_mapping_error(&cp->pdev->dev, new_mapping)) {
			dev->stats.rx_dropped++;
			kfree_skb(new_skb);
			goto rx_next;
		}

		dma_unmap_single(&cp->pdev->dev, mapping,
				 buflen, PCI_DMA_FROMDEVICE);

		/* Handle checksum offloading for incoming packets. */
		if (cp_rx_csum_ok(status))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb_checksum_none_assert(skb);

		skb_put(skb, len);

		cp->rx_skb[rx_tail] = new_skb;

		cp_rx_skb(cp, skb, desc);
		rx++;
		mapping = new_mapping;

rx_next:
		cp->rx_ring[rx_tail].opts2 = 0;
		cp->rx_ring[rx_tail].addr = cpu_to_le64(mapping);
		if (rx_tail == (CP_RX_RING_SIZE - 1))
			desc->opts1 = cpu_to_le32(DescOwn | RingEnd |
						  cp->rx_buf_sz);
		else
			desc->opts1 = cpu_to_le32(DescOwn | cp->rx_buf_sz);
		rx_tail = NEXT_RX(rx_tail);
	}

	cp->rx_tail = rx_tail;

	/* if we did not reach work limit, then we're done with
	 * this round of polling
	 */
	if (rx < budget) {
		unsigned long flags;

		if (cpr16(IntrStatus) & cp_rx_intr_mask)
			goto rx_status_loop;

		napi_gro_flush(napi, false);
		spin_lock_irqsave(&cp->lock, flags);
		__napi_complete(napi);
		cpw16_f(IntrMask, cp_intr_mask);
		spin_unlock_irqrestore(&cp->lock, flags);
	}

	return rx;
}

static irqreturn_t cp_interrupt (int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct cp_private *cp;
	int handled = 0;
	u16 status;

	if (unlikely(dev == NULL))
		return IRQ_NONE;
	cp = netdev_priv(dev);

	spin_lock(&cp->lock);

	status = cpr16(IntrStatus);
	if (!status || (status == 0xFFFF))
		goto out_unlock;

	handled = 1;

	netif_dbg(cp, intr, dev, "intr, status %04x cmd %02x cpcmd %04x\n",
		  status, cpr8(Cmd), cpr16(CpCmd));

	cpw16(IntrStatus, status & ~cp_rx_intr_mask);

	/* close possible race's with dev_close */
	if (unlikely(!netif_running(dev))) {
		cpw16(IntrMask, 0);
		goto out_unlock;
	}

	if (status & (RxOK | RxErr | RxEmpty | RxFIFOOvr))
		if (napi_schedule_prep(&cp->napi)) {
			cpw16_f(IntrMask, cp_norx_intr_mask);
			__napi_schedule(&cp->napi);
		}

	if (status & (TxOK | TxErr | TxEmpty | SWInt))
		cp_tx(cp);
	if (status & LinkChg)
		mii_check_media(&cp->mii_if, netif_msg_link(cp), false);


	if (status & PciErr) {
		u16 pci_status;

		pci_read_config_word(cp->pdev, PCI_STATUS, &pci_status);
		pci_write_config_word(cp->pdev, PCI_STATUS, pci_status);
		netdev_err(dev, "PCI bus error, status=%04x, PCI status=%04x\n",
			   status, pci_status);

		/* TODO: reset hardware */
	}

out_unlock:
	spin_unlock(&cp->lock);

	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling receive - used by netconsole and other diagnostic tools
 * to allow network i/o with interrupts disabled.
 */
static void cp_poll_controller(struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	const int irq = cp->pdev->irq;

	disable_irq(irq);
	cp_interrupt(irq, dev);
	enable_irq(irq);
}
#endif

static void cp_tx (struct cp_private *cp)
{
	unsigned tx_head = cp->tx_head;
	unsigned tx_tail = cp->tx_tail;
	unsigned bytes_compl = 0, pkts_compl = 0;

	while (tx_tail != tx_head) {
		struct cp_desc *txd = cp->tx_ring + tx_tail;
		struct sk_buff *skb;
		u32 status;

		rmb();
		status = le32_to_cpu(txd->opts1);
		if (status & DescOwn)
			break;

		skb = cp->tx_skb[tx_tail];
		BUG_ON(!skb);

		dma_unmap_single(&cp->pdev->dev, le64_to_cpu(txd->addr),
				 cp->tx_opts[tx_tail] & 0xffff,
				 PCI_DMA_TODEVICE);

		if (status & LastFrag) {
			if (status & (TxError | TxFIFOUnder)) {
				netif_dbg(cp, tx_err, cp->dev,
					  "tx err, status 0x%x\n", status);
				cp->dev->stats.tx_errors++;
				if (status & TxOWC)
					cp->dev->stats.tx_window_errors++;
				if (status & TxMaxCol)
					cp->dev->stats.tx_aborted_errors++;
				if (status & TxLinkFail)
					cp->dev->stats.tx_carrier_errors++;
				if (status & TxFIFOUnder)
					cp->dev->stats.tx_fifo_errors++;
			} else {
				cp->dev->stats.collisions +=
					((status >> TxColCntShift) & TxColCntMask);
				cp->dev->stats.tx_packets++;
				cp->dev->stats.tx_bytes += skb->len;
				netif_dbg(cp, tx_done, cp->dev,
					  "tx done, slot %d\n", tx_tail);
			}
			bytes_compl += skb->len;
			pkts_compl++;
			dev_kfree_skb_irq(skb);
		}

		cp->tx_skb[tx_tail] = NULL;

		tx_tail = NEXT_TX(tx_tail);
	}

	cp->tx_tail = tx_tail;

	netdev_completed_queue(cp->dev, pkts_compl, bytes_compl);
	if (TX_BUFFS_AVAIL(cp) > (MAX_SKB_FRAGS + 1))
		netif_wake_queue(cp->dev);
}

static inline u32 cp_tx_vlan_tag(struct sk_buff *skb)
{
	return skb_vlan_tag_present(skb) ?
		TxVlanTag | swab16(skb_vlan_tag_get(skb)) : 0x00;
}

static void unwind_tx_frag_mapping(struct cp_private *cp, struct sk_buff *skb,
				   int first, int entry_last)
{
	int frag, index;
	struct cp_desc *txd;
	skb_frag_t *this_frag;
	for (frag = 0; frag+first < entry_last; frag++) {
		index = first+frag;
		cp->tx_skb[index] = NULL;
		txd = &cp->tx_ring[index];
		this_frag = &skb_shinfo(skb)->frags[frag];
		dma_unmap_single(&cp->pdev->dev, le64_to_cpu(txd->addr),
				 skb_frag_size(this_frag), PCI_DMA_TODEVICE);
	}
}

static netdev_tx_t cp_start_xmit (struct sk_buff *skb,
					struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned entry;
	u32 eor, opts1;
	unsigned long intr_flags;
	__le32 opts2;
	int mss = 0;

	spin_lock_irqsave(&cp->lock, intr_flags);

	/* This is a hard error, log it. */
	if (TX_BUFFS_AVAIL(cp) <= (skb_shinfo(skb)->nr_frags + 1)) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&cp->lock, intr_flags);
		netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	entry = cp->tx_head;
	eor = (entry == (CP_TX_RING_SIZE - 1)) ? RingEnd : 0;
	mss = skb_shinfo(skb)->gso_size;

	if (mss > MSSMask) {
		WARN_ONCE(1, "Net bug: GSO size %d too large for 8139CP\n",
			  mss);
		goto out_dma_error;
	}

	opts2 = cpu_to_le32(cp_tx_vlan_tag(skb));
	opts1 = DescOwn;
	if (mss)
		opts1 |= LargeSend | (mss << MSSShift);
	else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const struct iphdr *ip = ip_hdr(skb);
		if (ip->protocol == IPPROTO_TCP)
			opts1 |= IPCS | TCPCS;
		else if (ip->protocol == IPPROTO_UDP)
			opts1 |= IPCS | UDPCS;
		else {
			WARN_ONCE(1,
				  "Net bug: asked to checksum invalid Legacy IP packet\n");
			goto out_dma_error;
		}
	}

	if (skb_shinfo(skb)->nr_frags == 0) {
		struct cp_desc *txd = &cp->tx_ring[entry];
		u32 len;
		dma_addr_t mapping;

		len = skb->len;
		mapping = dma_map_single(&cp->pdev->dev, skb->data, len, PCI_DMA_TODEVICE);
		if (dma_mapping_error(&cp->pdev->dev, mapping))
			goto out_dma_error;

		txd->opts2 = opts2;
		txd->addr = cpu_to_le64(mapping);
		wmb();

		opts1 |= eor | len | FirstFrag | LastFrag;

		txd->opts1 = cpu_to_le32(opts1);
		wmb();

		cp->tx_skb[entry] = skb;
		cp->tx_opts[entry] = opts1;
		netif_dbg(cp, tx_queued, cp->dev, "tx queued, slot %d, skblen %d\n",
			  entry, skb->len);
	} else {
		struct cp_desc *txd;
		u32 first_len, first_eor, ctrl;
		dma_addr_t first_mapping;
		int frag, first_entry = entry;

		/* We must give this initial chunk to the device last.
		 * Otherwise we could race with the device.
		 */
		first_eor = eor;
		first_len = skb_headlen(skb);
		first_mapping = dma_map_single(&cp->pdev->dev, skb->data,
					       first_len, PCI_DMA_TODEVICE);
		if (dma_mapping_error(&cp->pdev->dev, first_mapping))
			goto out_dma_error;

		cp->tx_skb[entry] = skb;

		for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
			const skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];
			u32 len;
			dma_addr_t mapping;

			entry = NEXT_TX(entry);

			len = skb_frag_size(this_frag);
			mapping = dma_map_single(&cp->pdev->dev,
						 skb_frag_address(this_frag),
						 len, PCI_DMA_TODEVICE);
			if (dma_mapping_error(&cp->pdev->dev, mapping)) {
				unwind_tx_frag_mapping(cp, skb, first_entry, entry);
				goto out_dma_error;
			}

			eor = (entry == (CP_TX_RING_SIZE - 1)) ? RingEnd : 0;

			ctrl = opts1 | eor | len;

			if (frag == skb_shinfo(skb)->nr_frags - 1)
				ctrl |= LastFrag;

			txd = &cp->tx_ring[entry];
			txd->opts2 = opts2;
			txd->addr = cpu_to_le64(mapping);
			wmb();

			txd->opts1 = cpu_to_le32(ctrl);
			wmb();

			cp->tx_opts[entry] = ctrl;
			cp->tx_skb[entry] = skb;
		}

		txd = &cp->tx_ring[first_entry];
		txd->opts2 = opts2;
		txd->addr = cpu_to_le64(first_mapping);
		wmb();

		ctrl = opts1 | first_eor | first_len | FirstFrag;
		txd->opts1 = cpu_to_le32(ctrl);
		wmb();

		cp->tx_opts[first_entry] = ctrl;
		netif_dbg(cp, tx_queued, cp->dev, "tx queued, slots %d-%d, skblen %d\n",
			  first_entry, entry, skb->len);
	}
	cp->tx_head = NEXT_TX(entry);

	netdev_sent_queue(dev, skb->len);
	if (TX_BUFFS_AVAIL(cp) <= (MAX_SKB_FRAGS + 1))
		netif_stop_queue(dev);

out_unlock:
	spin_unlock_irqrestore(&cp->lock, intr_flags);

	cpw8(TxPoll, NormalTxPoll);

	return NETDEV_TX_OK;
out_dma_error:
	dev_kfree_skb_any(skb);
	cp->dev->stats.tx_dropped++;
	goto out_unlock;
}

/* Set or clear the multicast filter for this adaptor.
   This routine is not state sensitive and need not be SMP locked. */

static void __cp_set_rx_mode (struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	u32 mc_filter[2];	/* Multicast hash filter */
	int rx_mode;

	/* Note: do not reorder, GCC is clever about common statements. */
	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;
		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, dev) {
			int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;

			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}

	/* We can safely update without stopping the chip. */
	cp->rx_config = cp_rx_config | rx_mode;
	cpw32_f(RxConfig, cp->rx_config);

	cpw32_f (MAR0 + 0, mc_filter[0]);
	cpw32_f (MAR0 + 4, mc_filter[1]);
}

static void cp_set_rx_mode (struct net_device *dev)
{
	unsigned long flags;
	struct cp_private *cp = netdev_priv(dev);

	spin_lock_irqsave (&cp->lock, flags);
	__cp_set_rx_mode(dev);
	spin_unlock_irqrestore (&cp->lock, flags);
}

static void __cp_get_stats(struct cp_private *cp)
{
	/* only lower 24 bits valid; write any value to clear */
	cp->dev->stats.rx_missed_errors += (cpr32 (RxMissed) & 0xffffff);
	cpw32 (RxMissed, 0);
}

static struct net_device_stats *cp_get_stats(struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;

	/* The chip only need report frame silently dropped. */
	spin_lock_irqsave(&cp->lock, flags);
 	if (netif_running(dev) && netif_device_present(dev))
 		__cp_get_stats(cp);
	spin_unlock_irqrestore(&cp->lock, flags);

	return &dev->stats;
}

static void cp_stop_hw (struct cp_private *cp)
{
	cpw16(IntrStatus, ~(cpr16(IntrStatus)));
	cpw16_f(IntrMask, 0);
	cpw8(Cmd, 0);
	cpw16_f(CpCmd, 0);
	cpw16_f(IntrStatus, ~(cpr16(IntrStatus)));

	cp->rx_tail = 0;
	cp->tx_head = cp->tx_tail = 0;

	netdev_reset_queue(cp->dev);
}

static void cp_reset_hw (struct cp_private *cp)
{
	unsigned work = 1000;

	cpw8(Cmd, CmdReset);

	while (work--) {
		if (!(cpr8(Cmd) & CmdReset))
			return;

		schedule_timeout_uninterruptible(10);
	}

	netdev_err(cp->dev, "hardware reset timeout\n");
}

static inline void cp_start_hw (struct cp_private *cp)
{
	dma_addr_t ring_dma;

	cpw16(CpCmd, cp->cpcmd);

	/*
	 * These (at least TxRingAddr) need to be configured after the
	 * corresponding bits in CpCmd are enabled. Datasheet v1.6 §6.33
	 * (C+ Command Register) recommends that these and more be configured
	 * *after* the [RT]xEnable bits in CpCmd are set. And on some hardware
	 * it's been observed that the TxRingAddr is actually reset to garbage
	 * when C+ mode Tx is enabled in CpCmd.
	 */
	cpw32_f(HiTxRingAddr, 0);
	cpw32_f(HiTxRingAddr + 4, 0);

	ring_dma = cp->ring_dma;
	cpw32_f(RxRingAddr, ring_dma & 0xffffffff);
	cpw32_f(RxRingAddr + 4, (ring_dma >> 16) >> 16);

	ring_dma += sizeof(struct cp_desc) * CP_RX_RING_SIZE;
	cpw32_f(TxRingAddr, ring_dma & 0xffffffff);
	cpw32_f(TxRingAddr + 4, (ring_dma >> 16) >> 16);

	/*
	 * Strictly speaking, the datasheet says this should be enabled
	 * *before* setting the descriptor addresses. But what, then, would
	 * prevent it from doing DMA to random unconfigured addresses?
	 * This variant appears to work fine.
	 */
	cpw8(Cmd, RxOn | TxOn);

	netdev_reset_queue(cp->dev);
}

static void cp_enable_irq(struct cp_private *cp)
{
	cpw16_f(IntrMask, cp_intr_mask);
}

static void cp_init_hw (struct cp_private *cp)
{
	struct net_device *dev = cp->dev;

	cp_reset_hw(cp);

	cpw8_f (Cfg9346, Cfg9346_Unlock);

	/* Restore our idea of the MAC address. */
	cpw32_f (MAC0 + 0, le32_to_cpu (*(__le32 *) (dev->dev_addr + 0)));
	cpw32_f (MAC0 + 4, le32_to_cpu (*(__le32 *) (dev->dev_addr + 4)));

	cp_start_hw(cp);
	cpw8(TxThresh, 0x06); /* XXX convert magic num to a constant */

	__cp_set_rx_mode(dev);
	cpw32_f (TxConfig, IFG | (TX_DMA_BURST << TxDMAShift));

	cpw8(Config1, cpr8(Config1) | DriverLoaded | PMEnable);
	/* Disable Wake-on-LAN. Can be turned on with ETHTOOL_SWOL */
	cpw8(Config3, PARMEnable);
	cp->wol_enabled = 0;

	cpw8(Config5, cpr8(Config5) & PMEStatus);

	cpw16(MultiIntr, 0);

	cpw8_f(Cfg9346, Cfg9346_Lock);
}

static int cp_refill_rx(struct cp_private *cp)
{
	struct net_device *dev = cp->dev;
	unsigned i;

	for (i = 0; i < CP_RX_RING_SIZE; i++) {
		struct sk_buff *skb;
		dma_addr_t mapping;

		skb = netdev_alloc_skb_ip_align(dev, cp->rx_buf_sz);
		if (!skb)
			goto err_out;

		mapping = dma_map_single(&cp->pdev->dev, skb->data,
					 cp->rx_buf_sz, PCI_DMA_FROMDEVICE);
		if (dma_mapping_error(&cp->pdev->dev, mapping)) {
			kfree_skb(skb);
			goto err_out;
		}
		cp->rx_skb[i] = skb;

		cp->rx_ring[i].opts2 = 0;
		cp->rx_ring[i].addr = cpu_to_le64(mapping);
		if (i == (CP_RX_RING_SIZE - 1))
			cp->rx_ring[i].opts1 =
				cpu_to_le32(DescOwn | RingEnd | cp->rx_buf_sz);
		else
			cp->rx_ring[i].opts1 =
				cpu_to_le32(DescOwn | cp->rx_buf_sz);
	}

	return 0;

err_out:
	cp_clean_rings(cp);
	return -ENOMEM;
}

static void cp_init_rings_index (struct cp_private *cp)
{
	cp->rx_tail = 0;
	cp->tx_head = cp->tx_tail = 0;
}

static int cp_init_rings (struct cp_private *cp)
{
	memset(cp->tx_ring, 0, sizeof(struct cp_desc) * CP_TX_RING_SIZE);
	cp->tx_ring[CP_TX_RING_SIZE - 1].opts1 = cpu_to_le32(RingEnd);
	memset(cp->tx_opts, 0, sizeof(cp->tx_opts));

	cp_init_rings_index(cp);

	return cp_refill_rx (cp);
}

static int cp_alloc_rings (struct cp_private *cp)
{
	struct device *d = &cp->pdev->dev;
	void *mem;
	int rc;

	mem = dma_alloc_coherent(d, CP_RING_BYTES, &cp->ring_dma, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	cp->rx_ring = mem;
	cp->tx_ring = &cp->rx_ring[CP_RX_RING_SIZE];

	rc = cp_init_rings(cp);
	if (rc < 0)
		dma_free_coherent(d, CP_RING_BYTES, cp->rx_ring, cp->ring_dma);

	return rc;
}

static void cp_clean_rings (struct cp_private *cp)
{
	struct cp_desc *desc;
	unsigned i;

	for (i = 0; i < CP_RX_RING_SIZE; i++) {
		if (cp->rx_skb[i]) {
			desc = cp->rx_ring + i;
			dma_unmap_single(&cp->pdev->dev,le64_to_cpu(desc->addr),
					 cp->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(cp->rx_skb[i]);
		}
	}

	for (i = 0; i < CP_TX_RING_SIZE; i++) {
		if (cp->tx_skb[i]) {
			struct sk_buff *skb = cp->tx_skb[i];

			desc = cp->tx_ring + i;
			dma_unmap_single(&cp->pdev->dev,le64_to_cpu(desc->addr),
					 le32_to_cpu(desc->opts1) & 0xffff,
					 PCI_DMA_TODEVICE);
			if (le32_to_cpu(desc->opts1) & LastFrag)
				dev_kfree_skb_any(skb);
			cp->dev->stats.tx_dropped++;
		}
	}
	netdev_reset_queue(cp->dev);

	memset(cp->rx_ring, 0, sizeof(struct cp_desc) * CP_RX_RING_SIZE);
	memset(cp->tx_ring, 0, sizeof(struct cp_desc) * CP_TX_RING_SIZE);
	memset(cp->tx_opts, 0, sizeof(cp->tx_opts));

	memset(cp->rx_skb, 0, sizeof(struct sk_buff *) * CP_RX_RING_SIZE);
	memset(cp->tx_skb, 0, sizeof(struct sk_buff *) * CP_TX_RING_SIZE);
}

static void cp_free_rings (struct cp_private *cp)
{
	cp_clean_rings(cp);
	dma_free_coherent(&cp->pdev->dev, CP_RING_BYTES, cp->rx_ring,
			  cp->ring_dma);
	cp->rx_ring = NULL;
	cp->tx_ring = NULL;
}

static int cp_open (struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	const int irq = cp->pdev->irq;
	int rc;

	netif_dbg(cp, ifup, dev, "enabling interface\n");

	rc = cp_alloc_rings(cp);
	if (rc)
		return rc;

	napi_enable(&cp->napi);

	cp_init_hw(cp);

	rc = request_irq(irq, cp_interrupt, IRQF_SHARED, dev->name, dev);
	if (rc)
		goto err_out_hw;

	cp_enable_irq(cp);

	netif_carrier_off(dev);
	mii_check_media(&cp->mii_if, netif_msg_link(cp), true);
	netif_start_queue(dev);

	return 0;

err_out_hw:
	napi_disable(&cp->napi);
	cp_stop_hw(cp);
	cp_free_rings(cp);
	return rc;
}

static int cp_close (struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;

	napi_disable(&cp->napi);

	netif_dbg(cp, ifdown, dev, "disabling interface\n");

	spin_lock_irqsave(&cp->lock, flags);

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	cp_stop_hw(cp);

	spin_unlock_irqrestore(&cp->lock, flags);

	free_irq(cp->pdev->irq, dev);

	cp_free_rings(cp);
	return 0;
}

static void cp_tx_timeout(struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;
	int rc, i;

	netdev_warn(dev, "Transmit timeout, status %2x %4x %4x %4x\n",
		    cpr8(Cmd), cpr16(CpCmd),
		    cpr16(IntrStatus), cpr16(IntrMask));

	spin_lock_irqsave(&cp->lock, flags);

	netif_dbg(cp, tx_err, cp->dev, "TX ring head %d tail %d desc %x\n",
		  cp->tx_head, cp->tx_tail, cpr16(TxDmaOkLowDesc));
	for (i = 0; i < CP_TX_RING_SIZE; i++) {
		netif_dbg(cp, tx_err, cp->dev,
			  "TX slot %d @%p: %08x (%08x) %08x %llx %p\n",
			  i, &cp->tx_ring[i], le32_to_cpu(cp->tx_ring[i].opts1),
			  cp->tx_opts[i], le32_to_cpu(cp->tx_ring[i].opts2),
			  le64_to_cpu(cp->tx_ring[i].addr),
			  cp->tx_skb[i]);
	}

	cp_stop_hw(cp);
	cp_clean_rings(cp);
	rc = cp_init_rings(cp);
	cp_start_hw(cp);
	__cp_set_rx_mode(dev);
	cpw16_f(IntrMask, cp_norx_intr_mask);

	netif_wake_queue(dev);
	napi_schedule_irqoff(&cp->napi);

	spin_unlock_irqrestore(&cp->lock, flags);
}

static int cp_change_mtu(struct net_device *dev, int new_mtu)
{
	struct cp_private *cp = netdev_priv(dev);

	/* check for invalid MTU, according to hardware limits */
	if (new_mtu < CP_MIN_MTU || new_mtu > CP_MAX_MTU)
		return -EINVAL;

	/* if network interface not up, no need for complexity */
	if (!netif_running(dev)) {
		dev->mtu = new_mtu;
		cp_set_rxbufsize(cp);	/* set new rx buf size */
		return 0;
	}

	/* network IS up, close it, reset MTU, and come up again. */
	cp_close(dev);
	dev->mtu = new_mtu;
	cp_set_rxbufsize(cp);
	return cp_open(dev);
}

static const char mii_2_8139_map[8] = {
	BasicModeCtrl,
	BasicModeStatus,
	0,
	0,
	NWayAdvert,
	NWayLPAR,
	NWayExpansion,
	0
};

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct cp_private *cp = netdev_priv(dev);

	return location < 8 && mii_2_8139_map[location] ?
	       readw(cp->regs + mii_2_8139_map[location]) : 0;
}


static void mdio_write(struct net_device *dev, int phy_id, int location,
		       int value)
{
	struct cp_private *cp = netdev_priv(dev);

	if (location == 0) {
		cpw8(Cfg9346, Cfg9346_Unlock);
		cpw16(BasicModeCtrl, value);
		cpw8(Cfg9346, Cfg9346_Lock);
	} else if (location < 8 && mii_2_8139_map[location])
		cpw16(mii_2_8139_map[location], value);
}

/* Set the ethtool Wake-on-LAN settings */
static int netdev_set_wol (struct cp_private *cp,
			   const struct ethtool_wolinfo *wol)
{
	u8 options;

	options = cpr8 (Config3) & ~(LinkUp | MagicPacket);
	/* If WOL is being disabled, no need for complexity */
	if (wol->wolopts) {
		if (wol->wolopts & WAKE_PHY)	options |= LinkUp;
		if (wol->wolopts & WAKE_MAGIC)	options |= MagicPacket;
	}

	cpw8 (Cfg9346, Cfg9346_Unlock);
	cpw8 (Config3, options);
	cpw8 (Cfg9346, Cfg9346_Lock);

	options = 0; /* Paranoia setting */
	options = cpr8 (Config5) & ~(UWF | MWF | BWF);
	/* If WOL is being disabled, no need for complexity */
	if (wol->wolopts) {
		if (wol->wolopts & WAKE_UCAST)  options |= UWF;
		if (wol->wolopts & WAKE_BCAST)	options |= BWF;
		if (wol->wolopts & WAKE_MCAST)	options |= MWF;
	}

	cpw8 (Config5, options);

	cp->wol_enabled = (wol->wolopts) ? 1 : 0;

	return 0;
}

/* Get the ethtool Wake-on-LAN settings */
static void netdev_get_wol (struct cp_private *cp,
	             struct ethtool_wolinfo *wol)
{
	u8 options;

	wol->wolopts   = 0; /* Start from scratch */
	wol->supported = WAKE_PHY   | WAKE_BCAST | WAKE_MAGIC |
		         WAKE_MCAST | WAKE_UCAST;
	/* We don't need to go on if WOL is disabled */
	if (!cp->wol_enabled) return;

	options        = cpr8 (Config3);
	if (options & LinkUp)        wol->wolopts |= WAKE_PHY;
	if (options & MagicPacket)   wol->wolopts |= WAKE_MAGIC;

	options        = 0; /* Paranoia setting */
	options        = cpr8 (Config5);
	if (options & UWF)           wol->wolopts |= WAKE_UCAST;
	if (options & BWF)           wol->wolopts |= WAKE_BCAST;
	if (options & MWF)           wol->wolopts |= WAKE_MCAST;
}

static void cp_get_drvinfo (struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct cp_private *cp = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(cp->pdev), sizeof(info->bus_info));
}

static void cp_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *ring)
{
	ring->rx_max_pending = CP_RX_RING_SIZE;
	ring->tx_max_pending = CP_TX_RING_SIZE;
	ring->rx_pending = CP_RX_RING_SIZE;
	ring->tx_pending = CP_TX_RING_SIZE;
}

static int cp_get_regs_len(struct net_device *dev)
{
	return CP_REGS_SIZE;
}

static int cp_get_sset_count (struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return CP_NUM_STATS;
	default:
		return -EOPNOTSUPP;
	}
}

static int cp_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct cp_private *cp = netdev_priv(dev);
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&cp->lock, flags);
	rc = mii_ethtool_gset(&cp->mii_if, cmd);
	spin_unlock_irqrestore(&cp->lock, flags);

	return rc;
}

static int cp_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct cp_private *cp = netdev_priv(dev);
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&cp->lock, flags);
	rc = mii_ethtool_sset(&cp->mii_if, cmd);
	spin_unlock_irqrestore(&cp->lock, flags);

	return rc;
}

static int cp_nway_reset(struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	return mii_nway_restart(&cp->mii_if);
}

static u32 cp_get_msglevel(struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	return cp->msg_enable;
}

static void cp_set_msglevel(struct net_device *dev, u32 value)
{
	struct cp_private *cp = netdev_priv(dev);
	cp->msg_enable = value;
}

static int cp_set_features(struct net_device *dev, netdev_features_t features)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;

	if (!((dev->features ^ features) & NETIF_F_RXCSUM))
		return 0;

	spin_lock_irqsave(&cp->lock, flags);

	if (features & NETIF_F_RXCSUM)
		cp->cpcmd |= RxChkSum;
	else
		cp->cpcmd &= ~RxChkSum;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		cp->cpcmd |= RxVlanOn;
	else
		cp->cpcmd &= ~RxVlanOn;

	cpw16_f(CpCmd, cp->cpcmd);
	spin_unlock_irqrestore(&cp->lock, flags);

	return 0;
}

static void cp_get_regs(struct net_device *dev, struct ethtool_regs *regs,
		        void *p)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;

	if (regs->len < CP_REGS_SIZE)
		return /* -EINVAL */;

	regs->version = CP_REGS_VER;

	spin_lock_irqsave(&cp->lock, flags);
	memcpy_fromio(p, cp->regs, CP_REGS_SIZE);
	spin_unlock_irqrestore(&cp->lock, flags);
}

static void cp_get_wol (struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave (&cp->lock, flags);
	netdev_get_wol (cp, wol);
	spin_unlock_irqrestore (&cp->lock, flags);
}

static int cp_set_wol (struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;
	int rc;

	spin_lock_irqsave (&cp->lock, flags);
	rc = netdev_set_wol (cp, wol);
	spin_unlock_irqrestore (&cp->lock, flags);

	return rc;
}

static void cp_get_strings (struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		break;
	default:
		BUG();
		break;
	}
}

static void cp_get_ethtool_stats (struct net_device *dev,
				  struct ethtool_stats *estats, u64 *tmp_stats)
{
	struct cp_private *cp = netdev_priv(dev);
	struct cp_dma_stats *nic_stats;
	dma_addr_t dma;
	int i;

	nic_stats = dma_alloc_coherent(&cp->pdev->dev, sizeof(*nic_stats),
				       &dma, GFP_KERNEL);
	if (!nic_stats)
		return;

	/* begin NIC statistics dump */
	cpw32(StatsAddr + 4, (u64)dma >> 32);
	cpw32(StatsAddr, ((u64)dma & DMA_BIT_MASK(32)) | DumpStats);
	cpr32(StatsAddr);

	for (i = 0; i < 1000; i++) {
		if ((cpr32(StatsAddr) & DumpStats) == 0)
			break;
		udelay(10);
	}
	cpw32(StatsAddr, 0);
	cpw32(StatsAddr + 4, 0);
	cpr32(StatsAddr);

	i = 0;
	tmp_stats[i++] = le64_to_cpu(nic_stats->tx_ok);
	tmp_stats[i++] = le64_to_cpu(nic_stats->rx_ok);
	tmp_stats[i++] = le64_to_cpu(nic_stats->tx_err);
	tmp_stats[i++] = le32_to_cpu(nic_stats->rx_err);
	tmp_stats[i++] = le16_to_cpu(nic_stats->rx_fifo);
	tmp_stats[i++] = le16_to_cpu(nic_stats->frame_align);
	tmp_stats[i++] = le32_to_cpu(nic_stats->tx_ok_1col);
	tmp_stats[i++] = le32_to_cpu(nic_stats->tx_ok_mcol);
	tmp_stats[i++] = le64_to_cpu(nic_stats->rx_ok_phys);
	tmp_stats[i++] = le64_to_cpu(nic_stats->rx_ok_bcast);
	tmp_stats[i++] = le32_to_cpu(nic_stats->rx_ok_mcast);
	tmp_stats[i++] = le16_to_cpu(nic_stats->tx_abort);
	tmp_stats[i++] = le16_to_cpu(nic_stats->tx_underrun);
	tmp_stats[i++] = cp->cp_stats.rx_frags;
	BUG_ON(i != CP_NUM_STATS);

	dma_free_coherent(&cp->pdev->dev, sizeof(*nic_stats), nic_stats, dma);
}

static const struct ethtool_ops cp_ethtool_ops = {
	.get_drvinfo		= cp_get_drvinfo,
	.get_regs_len		= cp_get_regs_len,
	.get_sset_count		= cp_get_sset_count,
	.get_settings		= cp_get_settings,
	.set_settings		= cp_set_settings,
	.nway_reset		= cp_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= cp_get_msglevel,
	.set_msglevel		= cp_set_msglevel,
	.get_regs		= cp_get_regs,
	.get_wol		= cp_get_wol,
	.set_wol		= cp_set_wol,
	.get_strings		= cp_get_strings,
	.get_ethtool_stats	= cp_get_ethtool_stats,
	.get_eeprom_len		= cp_get_eeprom_len,
	.get_eeprom		= cp_get_eeprom,
	.set_eeprom		= cp_set_eeprom,
	.get_ringparam		= cp_get_ringparam,
};

static int cp_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct cp_private *cp = netdev_priv(dev);
	int rc;
	unsigned long flags;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irqsave(&cp->lock, flags);
	rc = generic_mii_ioctl(&cp->mii_if, if_mii(rq), cmd, NULL);
	spin_unlock_irqrestore(&cp->lock, flags);
	return rc;
}

static int cp_set_mac_address(struct net_device *dev, void *p)
{
	struct cp_private *cp = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	spin_lock_irq(&cp->lock);

	cpw8_f(Cfg9346, Cfg9346_Unlock);
	cpw32_f(MAC0 + 0, le32_to_cpu (*(__le32 *) (dev->dev_addr + 0)));
	cpw32_f(MAC0 + 4, le32_to_cpu (*(__le32 *) (dev->dev_addr + 4)));
	cpw8_f(Cfg9346, Cfg9346_Lock);

	spin_unlock_irq(&cp->lock);

	return 0;
}

/* Serial EEPROM section. */

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS			0x08	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x02	/* EEPROM chip data in. */
#define EE_WRITE_0		0x00
#define EE_WRITE_1		0x02
#define EE_DATA_READ	0x01	/* EEPROM chip data out. */
#define EE_ENB			(0x80 | EE_CS)

/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but 66Mhz may change this.
 */

#define eeprom_delay()	readb(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_EXTEND_CMD	(4)
#define EE_WRITE_CMD	(5)
#define EE_READ_CMD		(6)
#define EE_ERASE_CMD	(7)

#define EE_EWDS_ADDR	(0)
#define EE_WRAL_ADDR	(1)
#define EE_ERAL_ADDR	(2)
#define EE_EWEN_ADDR	(3)

#define CP_EEPROM_MAGIC PCI_DEVICE_ID_REALTEK_8139

static void eeprom_cmd_start(void __iomem *ee_addr)
{
	writeb (EE_ENB & ~EE_CS, ee_addr);
	writeb (EE_ENB, ee_addr);
	eeprom_delay ();
}

static void eeprom_cmd(void __iomem *ee_addr, int cmd, int cmd_len)
{
	int i;

	/* Shift the command bits out. */
	for (i = cmd_len - 1; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		writeb (EE_ENB | dataval, ee_addr);
		eeprom_delay ();
		writeb (EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay ();
	}
	writeb (EE_ENB, ee_addr);
	eeprom_delay ();
}

static void eeprom_cmd_end(void __iomem *ee_addr)
{
	writeb(0, ee_addr);
	eeprom_delay ();
}

static void eeprom_extend_cmd(void __iomem *ee_addr, int extend_cmd,
			      int addr_len)
{
	int cmd = (EE_EXTEND_CMD << addr_len) | (extend_cmd << (addr_len - 2));

	eeprom_cmd_start(ee_addr);
	eeprom_cmd(ee_addr, cmd, 3 + addr_len);
	eeprom_cmd_end(ee_addr);
}

static u16 read_eeprom (void __iomem *ioaddr, int location, int addr_len)
{
	int i;
	u16 retval = 0;
	void __iomem *ee_addr = ioaddr + Cfg9346;
	int read_cmd = location | (EE_READ_CMD << addr_len);

	eeprom_cmd_start(ee_addr);
	eeprom_cmd(ee_addr, read_cmd, 3 + addr_len);

	for (i = 16; i > 0; i--) {
		writeb (EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay ();
		retval =
		    (retval << 1) | ((readb (ee_addr) & EE_DATA_READ) ? 1 :
				     0);
		writeb (EE_ENB, ee_addr);
		eeprom_delay ();
	}

	eeprom_cmd_end(ee_addr);

	return retval;
}

static void write_eeprom(void __iomem *ioaddr, int location, u16 val,
			 int addr_len)
{
	int i;
	void __iomem *ee_addr = ioaddr + Cfg9346;
	int write_cmd = location | (EE_WRITE_CMD << addr_len);

	eeprom_extend_cmd(ee_addr, EE_EWEN_ADDR, addr_len);

	eeprom_cmd_start(ee_addr);
	eeprom_cmd(ee_addr, write_cmd, 3 + addr_len);
	eeprom_cmd(ee_addr, val, 16);
	eeprom_cmd_end(ee_addr);

	eeprom_cmd_start(ee_addr);
	for (i = 0; i < 20000; i++)
		if (readb(ee_addr) & EE_DATA_READ)
			break;
	eeprom_cmd_end(ee_addr);

	eeprom_extend_cmd(ee_addr, EE_EWDS_ADDR, addr_len);
}

static int cp_get_eeprom_len(struct net_device *dev)
{
	struct cp_private *cp = netdev_priv(dev);
	int size;

	spin_lock_irq(&cp->lock);
	size = read_eeprom(cp->regs, 0, 8) == 0x8129 ? 256 : 128;
	spin_unlock_irq(&cp->lock);

	return size;
}

static int cp_get_eeprom(struct net_device *dev,
			 struct ethtool_eeprom *eeprom, u8 *data)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned int addr_len;
	u16 val;
	u32 offset = eeprom->offset >> 1;
	u32 len = eeprom->len;
	u32 i = 0;

	eeprom->magic = CP_EEPROM_MAGIC;

	spin_lock_irq(&cp->lock);

	addr_len = read_eeprom(cp->regs, 0, 8) == 0x8129 ? 8 : 6;

	if (eeprom->offset & 1) {
		val = read_eeprom(cp->regs, offset, addr_len);
		data[i++] = (u8)(val >> 8);
		offset++;
	}

	while (i < len - 1) {
		val = read_eeprom(cp->regs, offset, addr_len);
		data[i++] = (u8)val;
		data[i++] = (u8)(val >> 8);
		offset++;
	}

	if (i < len) {
		val = read_eeprom(cp->regs, offset, addr_len);
		data[i] = (u8)val;
	}

	spin_unlock_irq(&cp->lock);
	return 0;
}

static int cp_set_eeprom(struct net_device *dev,
			 struct ethtool_eeprom *eeprom, u8 *data)
{
	struct cp_private *cp = netdev_priv(dev);
	unsigned int addr_len;
	u16 val;
	u32 offset = eeprom->offset >> 1;
	u32 len = eeprom->len;
	u32 i = 0;

	if (eeprom->magic != CP_EEPROM_MAGIC)
		return -EINVAL;

	spin_lock_irq(&cp->lock);

	addr_len = read_eeprom(cp->regs, 0, 8) == 0x8129 ? 8 : 6;

	if (eeprom->offset & 1) {
		val = read_eeprom(cp->regs, offset, addr_len) & 0xff;
		val |= (u16)data[i++] << 8;
		write_eeprom(cp->regs, offset, val, addr_len);
		offset++;
	}

	while (i < len - 1) {
		val = (u16)data[i++];
		val |= (u16)data[i++] << 8;
		write_eeprom(cp->regs, offset, val, addr_len);
		offset++;
	}

	if (i < len) {
		val = read_eeprom(cp->regs, offset, addr_len) & 0xff00;
		val |= (u16)data[i];
		write_eeprom(cp->regs, offset, val, addr_len);
	}

	spin_unlock_irq(&cp->lock);
	return 0;
}

/* Put the board into D3cold state and wait for WakeUp signal */
static void cp_set_d3_state (struct cp_private *cp)
{
	pci_enable_wake(cp->pdev, PCI_D0, 1); /* Enable PME# generation */
	pci_set_power_state (cp->pdev, PCI_D3hot);
}

static netdev_features_t cp_features_check(struct sk_buff *skb,
					   struct net_device *dev,
					   netdev_features_t features)
{
	if (skb_shinfo(skb)->gso_size > MSSMask)
		features &= ~NETIF_F_TSO;

	return vlan_features_check(skb, features);
}
static const struct net_device_ops cp_netdev_ops = {
	.ndo_open		= cp_open,
	.ndo_stop		= cp_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= cp_set_mac_address,
	.ndo_set_rx_mode	= cp_set_rx_mode,
	.ndo_get_stats		= cp_get_stats,
	.ndo_do_ioctl		= cp_ioctl,
	.ndo_start_xmit		= cp_start_xmit,
	.ndo_tx_timeout		= cp_tx_timeout,
	.ndo_set_features	= cp_set_features,
	.ndo_change_mtu		= cp_change_mtu,
	.ndo_features_check	= cp_features_check,

#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= cp_poll_controller,
#endif
};

static int cp_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct cp_private *cp;
	int rc;
	void __iomem *regs;
	resource_size_t pciaddr;
	unsigned int addr_len, i, pci_using_dac;

	pr_info_once("%s", version);

	if (pdev->vendor == PCI_VENDOR_ID_REALTEK &&
	    pdev->device == PCI_DEVICE_ID_REALTEK_8139 && pdev->revision < 0x20) {
		dev_info(&pdev->dev,
			 "This (id %04x:%04x rev %02x) is not an 8139C+ compatible chip, use 8139too\n",
			 pdev->vendor, pdev->device, pdev->revision);
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(struct cp_private));
	if (!dev)
		return -ENOMEM;
	SET_NETDEV_DEV(dev, &pdev->dev);

	cp = netdev_priv(dev);
	cp->pdev = pdev;
	cp->dev = dev;
	cp->msg_enable = (debug < 0 ? CP_DEF_MSG_ENABLE : debug);
	spin_lock_init (&cp->lock);
	cp->mii_if.dev = dev;
	cp->mii_if.mdio_read = mdio_read;
	cp->mii_if.mdio_write = mdio_write;
	cp->mii_if.phy_id = CP_INTERNAL_PHY;
	cp->mii_if.phy_id_mask = 0x1f;
	cp->mii_if.reg_num_mask = 0x1f;
	cp_set_rxbufsize(cp);

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out_free;

	rc = pci_set_mwi(pdev);
	if (rc)
		goto err_out_disable;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_mwi;

	pciaddr = pci_resource_start(pdev, 1);
	if (!pciaddr) {
		rc = -EIO;
		dev_err(&pdev->dev, "no MMIO resource\n");
		goto err_out_res;
	}
	if (pci_resource_len(pdev, 1) < CP_REGS_SIZE) {
		rc = -EIO;
		dev_err(&pdev->dev, "MMIO resource (%llx) too small\n",
		       (unsigned long long)pci_resource_len(pdev, 1));
		goto err_out_res;
	}

	/* Configure DMA attributes. */
	if ((sizeof(dma_addr_t) > 4) &&
	    !pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)) &&
	    !pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		pci_using_dac = 1;
	} else {
		pci_using_dac = 0;

		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(&pdev->dev,
				"No usable DMA configuration, aborting\n");
			goto err_out_res;
		}
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(&pdev->dev,
				"No usable consistent DMA configuration, aborting\n");
			goto err_out_res;
		}
	}

	cp->cpcmd = (pci_using_dac ? PCIDAC : 0) |
		    PCIMulRW | RxChkSum | CpRxOn | CpTxOn;

	dev->features |= NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_RXCSUM;

	regs = ioremap(pciaddr, CP_REGS_SIZE);
	if (!regs) {
		rc = -EIO;
		dev_err(&pdev->dev, "Cannot map PCI MMIO (%Lx@%Lx)\n",
			(unsigned long long)pci_resource_len(pdev, 1),
		       (unsigned long long)pciaddr);
		goto err_out_res;
	}
	cp->regs = regs;

	cp_stop_hw(cp);

	/* read MAC address from EEPROM */
	addr_len = read_eeprom (regs, 0, 8) == 0x8129 ? 8 : 6;
	for (i = 0; i < 3; i++)
		((__le16 *) (dev->dev_addr))[i] =
		    cpu_to_le16(read_eeprom (regs, i + 7, addr_len));

	dev->netdev_ops = &cp_netdev_ops;
	netif_napi_add(dev, &cp->napi, cp_rx_poll, 16);
	dev->ethtool_ops = &cp_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->features |= NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;

	if (pci_using_dac)
		dev->features |= NETIF_F_HIGHDMA;

	dev->hw_features |= NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;
	dev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_HIGHDMA;

	rc = register_netdev(dev);
	if (rc)
		goto err_out_iomap;

	netdev_info(dev, "RTL-8139C+ at 0x%p, %pM, IRQ %d\n",
		    regs, dev->dev_addr, pdev->irq);

	pci_set_drvdata(pdev, dev);

	/* enable busmastering and memory-write-invalidate */
	pci_set_master(pdev);

	if (cp->wol_enabled)
		cp_set_d3_state (cp);

	return 0;

err_out_iomap:
	iounmap(regs);
err_out_res:
	pci_release_regions(pdev);
err_out_mwi:
	pci_clear_mwi(pdev);
err_out_disable:
	pci_disable_device(pdev);
err_out_free:
	free_netdev(dev);
	return rc;
}

static void cp_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct cp_private *cp = netdev_priv(dev);

	unregister_netdev(dev);
	iounmap(cp->regs);
	if (cp->wol_enabled)
		pci_set_power_state (pdev, PCI_D0);
	pci_release_regions(pdev);
	pci_clear_mwi(pdev);
	pci_disable_device(pdev);
	free_netdev(dev);
}

#ifdef CONFIG_PM
static int cp_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;

	if (!netif_running(dev))
		return 0;

	netif_device_detach (dev);
	netif_stop_queue (dev);

	spin_lock_irqsave (&cp->lock, flags);

	/* Disable Rx and Tx */
	cpw16 (IntrMask, 0);
	cpw8  (Cmd, cpr8 (Cmd) & (~RxOn | ~TxOn));

	spin_unlock_irqrestore (&cp->lock, flags);

	pci_save_state(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev, state), cp->wol_enabled);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int cp_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct cp_private *cp = netdev_priv(dev);
	unsigned long flags;

	if (!netif_running(dev))
		return 0;

	netif_device_attach (dev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);

	/* FIXME: sh*t may happen if the Rx ring buffer is depleted */
	cp_init_rings_index (cp);
	cp_init_hw (cp);
	cp_enable_irq(cp);
	netif_start_queue (dev);

	spin_lock_irqsave (&cp->lock, flags);

	mii_check_media(&cp->mii_if, netif_msg_link(cp), false);

	spin_unlock_irqrestore (&cp->lock, flags);

	return 0;
}
#endif /* CONFIG_PM */

static const struct pci_device_id cp_pci_tbl[] = {
        { PCI_DEVICE(PCI_VENDOR_ID_REALTEK,     PCI_DEVICE_ID_REALTEK_8139), },
        { PCI_DEVICE(PCI_VENDOR_ID_TTTECH,      PCI_DEVICE_ID_TTTECH_MC322), },
        { },
};
MODULE_DEVICE_TABLE(pci, cp_pci_tbl);

static struct pci_driver cp_driver = {
	.name         = DRV_NAME,
	.id_table     = cp_pci_tbl,
	.probe        =	cp_init_one,
	.remove       = cp_remove_one,
#ifdef CONFIG_PM
	.resume       = cp_resume,
	.suspend      = cp_suspend,
#endif
};

module_pci_driver(cp_driver);
