/*
 * forcedeth: Ethernet driver for NVIDIA nForce media access controllers.
 *
 * Note: This driver is a cleanroom reimplementation based on reverse
 *      engineered documentation written by Carl-Daniel Hailfinger
 *      and Andrew de Quincey. It's neither supported nor endorsed
 *      by NVIDIA Corp. Use at your own risk.
 *
 * NVIDIA, nForce and other NVIDIA marks are trademarks or registered
 * trademarks of NVIDIA Corporation in the United States and other
 * countries.
 *
 * Copyright (C) 2003,4 Manfred Spraul
 * Copyright (C) 2004 Andrew de Quincey (wol support)
 * Copyright (C) 2004 Carl-Daniel Hailfinger (invalid MAC handling, insane
 *		IRQ rate fixes, bigendian fixes, cleanups, verification)
 * Copyright (c) 2004 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Changelog:
 * 	0.01: 05 Oct 2003: First release that compiles without warnings.
 * 	0.02: 05 Oct 2003: Fix bug for nv_drain_tx: do not try to free NULL skbs.
 * 			   Check all PCI BARs for the register window.
 * 			   udelay added to mii_rw.
 * 	0.03: 06 Oct 2003: Initialize dev->irq.
 * 	0.04: 07 Oct 2003: Initialize np->lock, reduce handled irqs, add printks.
 * 	0.05: 09 Oct 2003: printk removed again, irq status print tx_timeout.
 * 	0.06: 10 Oct 2003: MAC Address read updated, pff flag generation updated,
 * 			   irq mask updated
 * 	0.07: 14 Oct 2003: Further irq mask updates.
 * 	0.08: 20 Oct 2003: rx_desc.Length initialization added, nv_alloc_rx refill
 * 			   added into irq handler, NULL check for drain_ring.
 * 	0.09: 20 Oct 2003: Basic link speed irq implementation. Only handle the
 * 			   requested interrupt sources.
 * 	0.10: 20 Oct 2003: First cleanup for release.
 * 	0.11: 21 Oct 2003: hexdump for tx added, rx buffer sizes increased.
 * 			   MAC Address init fix, set_multicast cleanup.
 * 	0.12: 23 Oct 2003: Cleanups for release.
 * 	0.13: 25 Oct 2003: Limit for concurrent tx packets increased to 10.
 * 			   Set link speed correctly. start rx before starting
 * 			   tx (nv_start_rx sets the link speed).
 * 	0.14: 25 Oct 2003: Nic dependant irq mask.
 * 	0.15: 08 Nov 2003: fix smp deadlock with set_multicast_list during
 * 			   open.
 * 	0.16: 15 Nov 2003: include file cleanup for ppc64, rx buffer size
 * 			   increased to 1628 bytes.
 * 	0.17: 16 Nov 2003: undo rx buffer size increase. Substract 1 from
 * 			   the tx length.
 * 	0.18: 17 Nov 2003: fix oops due to late initialization of dev_stats
 * 	0.19: 29 Nov 2003: Handle RxNoBuf, detect & handle invalid mac
 * 			   addresses, really stop rx if already running
 * 			   in nv_start_rx, clean up a bit.
 * 	0.20: 07 Dec 2003: alloc fixes
 * 	0.21: 12 Jan 2004: additional alloc fix, nic polling fix.
 *	0.22: 19 Jan 2004: reprogram timer to a sane rate, avoid lockup
 *			   on close.
 *	0.23: 26 Jan 2004: various small cleanups
 *	0.24: 27 Feb 2004: make driver even less anonymous in backtraces
 *	0.25: 09 Mar 2004: wol support
 *	0.26: 03 Jun 2004: netdriver specific annotation, sparse-related fixes
 *	0.27: 19 Jun 2004: Gigabit support, new descriptor rings,
 *			   added CK804/MCP04 device IDs, code fixes
 *			   for registers, link status and other minor fixes.
 *	0.28: 21 Jun 2004: Big cleanup, making driver mostly endian safe
 *	0.29: 31 Aug 2004: Add backup timer for link change notification.
 *	0.30: 25 Sep 2004: rx checksum support for nf 250 Gb. Add rx reset
 *			   into nv_close, otherwise reenabling for wol can
 *			   cause DMA to kfree'd memory.
 *	0.31: 14 Nov 2004: ethtool support for getting/setting link
 *	                   capabilities.
 *	0.32: 16 Apr 2005: RX_ERROR4 handling added.
 *	0.33: 16 May 2005: Support for MCP51 added.
 *	0.34: 18 Jun 2005: Add DEV_NEED_LINKTIMER to all nForce nics.
 *	0.35: 26 Jun 2005: Support for MCP55 added.
 *	0.36: 28 Jun 2005: Add jumbo frame support.
 *	0.37: 10 Jul 2005: Additional ethtool support, cleanup of pci id list
 *	0.38: 16 Jul 2005: tx irq rewrite: Use global flags instead of
 *			   per-packet flags.
 *      0.39: 18 Jul 2005: Add 64bit descriptor support.
 *      0.40: 19 Jul 2005: Add support for mac address change.
 *      0.41: 30 Jul 2005: Write back original MAC in nv_close instead
 *			   of nv_remove
 *      0.42: 06 Aug 2005: Fix lack of link speed initialization
 *			   in the second (and later) nv_open call
 *
 * Known bugs:
 * We suspect that on some hardware no TX done interrupts are generated.
 * This means recovery from netif_stop_queue only happens if the hw timer
 * interrupt fires (100 times/second, configurable with NVREG_POLL_DEFAULT)
 * and the timer is active in the IRQMask, or if a rx packet arrives by chance.
 * If your hardware reliably generates tx done interrupts, then you can remove
 * DEV_NEED_TIMERIRQ from the driver_data flags.
 * DEV_NEED_TIMERIRQ will not harm you on sane hardware, only generating a few
 * superfluous timer interrupts from the nic.
 */
#define FORCEDETH_VERSION		"0.41"
#define DRV_NAME			"forcedeth"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/mii.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/if_vlan.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#if 0
#define dprintk			printk
#else
#define dprintk(x...)		do { } while (0)
#endif


/*
 * Hardware access:
 */

#define DEV_NEED_TIMERIRQ	0x0001  /* set the timer irq flag in the irq mask */
#define DEV_NEED_LINKTIMER	0x0002	/* poll link settings. Relies on the timer irq */
#define DEV_HAS_LARGEDESC	0x0004	/* device supports jumbo frames and needs packet format 2 */
#define DEV_HAS_HIGH_DMA        0x0008  /* device supports 64bit dma */

enum {
	NvRegIrqStatus = 0x000,
#define NVREG_IRQSTAT_MIIEVENT	0x040
#define NVREG_IRQSTAT_MASK		0x1ff
	NvRegIrqMask = 0x004,
#define NVREG_IRQ_RX_ERROR		0x0001
#define NVREG_IRQ_RX			0x0002
#define NVREG_IRQ_RX_NOBUF		0x0004
#define NVREG_IRQ_TX_ERR		0x0008
#define NVREG_IRQ_TX_OK			0x0010
#define NVREG_IRQ_TIMER			0x0020
#define NVREG_IRQ_LINK			0x0040
#define NVREG_IRQ_TX_ERROR		0x0080
#define NVREG_IRQ_TX1			0x0100
#define NVREG_IRQMASK_WANTED		0x00df

#define NVREG_IRQ_UNKNOWN	(~(NVREG_IRQ_RX_ERROR|NVREG_IRQ_RX|NVREG_IRQ_RX_NOBUF|NVREG_IRQ_TX_ERR| \
					NVREG_IRQ_TX_OK|NVREG_IRQ_TIMER|NVREG_IRQ_LINK|NVREG_IRQ_TX_ERROR| \
					NVREG_IRQ_TX1))

	NvRegUnknownSetupReg6 = 0x008,
#define NVREG_UNKSETUP6_VAL		3

/*
 * NVREG_POLL_DEFAULT is the interval length of the timer source on the nic
 * NVREG_POLL_DEFAULT=97 would result in an interval length of 1 ms
 */
	NvRegPollingInterval = 0x00c,
#define NVREG_POLL_DEFAULT	970
	NvRegMisc1 = 0x080,
#define NVREG_MISC1_HD		0x02
#define NVREG_MISC1_FORCE	0x3b0f3c

	NvRegTransmitterControl = 0x084,
#define NVREG_XMITCTL_START	0x01
	NvRegTransmitterStatus = 0x088,
#define NVREG_XMITSTAT_BUSY	0x01

	NvRegPacketFilterFlags = 0x8c,
#define NVREG_PFF_ALWAYS	0x7F0008
#define NVREG_PFF_PROMISC	0x80
#define NVREG_PFF_MYADDR	0x20

	NvRegOffloadConfig = 0x90,
#define NVREG_OFFLOAD_HOMEPHY	0x601
#define NVREG_OFFLOAD_NORMAL	RX_NIC_BUFSIZE
	NvRegReceiverControl = 0x094,
#define NVREG_RCVCTL_START	0x01
	NvRegReceiverStatus = 0x98,
#define NVREG_RCVSTAT_BUSY	0x01

	NvRegRandomSeed = 0x9c,
#define NVREG_RNDSEED_MASK	0x00ff
#define NVREG_RNDSEED_FORCE	0x7f00
#define NVREG_RNDSEED_FORCE2	0x2d00
#define NVREG_RNDSEED_FORCE3	0x7400

	NvRegUnknownSetupReg1 = 0xA0,
#define NVREG_UNKSETUP1_VAL	0x16070f
	NvRegUnknownSetupReg2 = 0xA4,
#define NVREG_UNKSETUP2_VAL	0x16
	NvRegMacAddrA = 0xA8,
	NvRegMacAddrB = 0xAC,
	NvRegMulticastAddrA = 0xB0,
#define NVREG_MCASTADDRA_FORCE	0x01
	NvRegMulticastAddrB = 0xB4,
	NvRegMulticastMaskA = 0xB8,
	NvRegMulticastMaskB = 0xBC,

	NvRegPhyInterface = 0xC0,
#define PHY_RGMII		0x10000000

	NvRegTxRingPhysAddr = 0x100,
	NvRegRxRingPhysAddr = 0x104,
	NvRegRingSizes = 0x108,
#define NVREG_RINGSZ_TXSHIFT 0
#define NVREG_RINGSZ_RXSHIFT 16
	NvRegUnknownTransmitterReg = 0x10c,
	NvRegLinkSpeed = 0x110,
#define NVREG_LINKSPEED_FORCE 0x10000
#define NVREG_LINKSPEED_10	1000
#define NVREG_LINKSPEED_100	100
#define NVREG_LINKSPEED_1000	50
#define NVREG_LINKSPEED_MASK	(0xFFF)
	NvRegUnknownSetupReg5 = 0x130,
#define NVREG_UNKSETUP5_BIT31	(1<<31)
	NvRegUnknownSetupReg3 = 0x13c,
#define NVREG_UNKSETUP3_VAL1	0x200010
	NvRegTxRxControl = 0x144,
#define NVREG_TXRXCTL_KICK	0x0001
#define NVREG_TXRXCTL_BIT1	0x0002
#define NVREG_TXRXCTL_BIT2	0x0004
#define NVREG_TXRXCTL_IDLE	0x0008
#define NVREG_TXRXCTL_RESET	0x0010
#define NVREG_TXRXCTL_RXCHECK	0x0400
	NvRegMIIStatus = 0x180,
#define NVREG_MIISTAT_ERROR		0x0001
#define NVREG_MIISTAT_LINKCHANGE	0x0008
#define NVREG_MIISTAT_MASK		0x000f
#define NVREG_MIISTAT_MASK2		0x000f
	NvRegUnknownSetupReg4 = 0x184,
#define NVREG_UNKSETUP4_VAL	8

	NvRegAdapterControl = 0x188,
#define NVREG_ADAPTCTL_START	0x02
#define NVREG_ADAPTCTL_LINKUP	0x04
#define NVREG_ADAPTCTL_PHYVALID	0x40000
#define NVREG_ADAPTCTL_RUNNING	0x100000
#define NVREG_ADAPTCTL_PHYSHIFT	24
	NvRegMIISpeed = 0x18c,
#define NVREG_MIISPEED_BIT8	(1<<8)
#define NVREG_MIIDELAY	5
	NvRegMIIControl = 0x190,
#define NVREG_MIICTL_INUSE	0x08000
#define NVREG_MIICTL_WRITE	0x00400
#define NVREG_MIICTL_ADDRSHIFT	5
	NvRegMIIData = 0x194,
	NvRegWakeUpFlags = 0x200,
#define NVREG_WAKEUPFLAGS_VAL		0x7770
#define NVREG_WAKEUPFLAGS_BUSYSHIFT	24
#define NVREG_WAKEUPFLAGS_ENABLESHIFT	16
#define NVREG_WAKEUPFLAGS_D3SHIFT	12
#define NVREG_WAKEUPFLAGS_D2SHIFT	8
#define NVREG_WAKEUPFLAGS_D1SHIFT	4
#define NVREG_WAKEUPFLAGS_D0SHIFT	0
#define NVREG_WAKEUPFLAGS_ACCEPT_MAGPAT		0x01
#define NVREG_WAKEUPFLAGS_ACCEPT_WAKEUPPAT	0x02
#define NVREG_WAKEUPFLAGS_ACCEPT_LINKCHANGE	0x04
#define NVREG_WAKEUPFLAGS_ENABLE	0x1111

	NvRegPatternCRC = 0x204,
	NvRegPatternMask = 0x208,
	NvRegPowerCap = 0x268,
#define NVREG_POWERCAP_D3SUPP	(1<<30)
#define NVREG_POWERCAP_D2SUPP	(1<<26)
#define NVREG_POWERCAP_D1SUPP	(1<<25)
	NvRegPowerState = 0x26c,
#define NVREG_POWERSTATE_POWEREDUP	0x8000
#define NVREG_POWERSTATE_VALID		0x0100
#define NVREG_POWERSTATE_MASK		0x0003
#define NVREG_POWERSTATE_D0		0x0000
#define NVREG_POWERSTATE_D1		0x0001
#define NVREG_POWERSTATE_D2		0x0002
#define NVREG_POWERSTATE_D3		0x0003
};

/* Big endian: should work, but is untested */
struct ring_desc {
	u32 PacketBuffer;
	u32 FlagLen;
};

struct ring_desc_ex {
	u32 PacketBufferHigh;
	u32 PacketBufferLow;
	u32 Reserved;
	u32 FlagLen;
};

typedef union _ring_type {
	struct ring_desc* orig;
	struct ring_desc_ex* ex;
} ring_type;

#define FLAG_MASK_V1 0xffff0000
#define FLAG_MASK_V2 0xffffc000
#define LEN_MASK_V1 (0xffffffff ^ FLAG_MASK_V1)
#define LEN_MASK_V2 (0xffffffff ^ FLAG_MASK_V2)

#define NV_TX_LASTPACKET	(1<<16)
#define NV_TX_RETRYERROR	(1<<19)
#define NV_TX_FORCED_INTERRUPT	(1<<24)
#define NV_TX_DEFERRED		(1<<26)
#define NV_TX_CARRIERLOST	(1<<27)
#define NV_TX_LATECOLLISION	(1<<28)
#define NV_TX_UNDERFLOW		(1<<29)
#define NV_TX_ERROR		(1<<30)
#define NV_TX_VALID		(1<<31)

#define NV_TX2_LASTPACKET	(1<<29)
#define NV_TX2_RETRYERROR	(1<<18)
#define NV_TX2_FORCED_INTERRUPT	(1<<30)
#define NV_TX2_DEFERRED		(1<<25)
#define NV_TX2_CARRIERLOST	(1<<26)
#define NV_TX2_LATECOLLISION	(1<<27)
#define NV_TX2_UNDERFLOW	(1<<28)
/* error and valid are the same for both */
#define NV_TX2_ERROR		(1<<30)
#define NV_TX2_VALID		(1<<31)

#define NV_RX_DESCRIPTORVALID	(1<<16)
#define NV_RX_MISSEDFRAME	(1<<17)
#define NV_RX_SUBSTRACT1	(1<<18)
#define NV_RX_ERROR1		(1<<23)
#define NV_RX_ERROR2		(1<<24)
#define NV_RX_ERROR3		(1<<25)
#define NV_RX_ERROR4		(1<<26)
#define NV_RX_CRCERR		(1<<27)
#define NV_RX_OVERFLOW		(1<<28)
#define NV_RX_FRAMINGERR	(1<<29)
#define NV_RX_ERROR		(1<<30)
#define NV_RX_AVAIL		(1<<31)

#define NV_RX2_CHECKSUMMASK	(0x1C000000)
#define NV_RX2_CHECKSUMOK1	(0x10000000)
#define NV_RX2_CHECKSUMOK2	(0x14000000)
#define NV_RX2_CHECKSUMOK3	(0x18000000)
#define NV_RX2_DESCRIPTORVALID	(1<<29)
#define NV_RX2_SUBSTRACT1	(1<<25)
#define NV_RX2_ERROR1		(1<<18)
#define NV_RX2_ERROR2		(1<<19)
#define NV_RX2_ERROR3		(1<<20)
#define NV_RX2_ERROR4		(1<<21)
#define NV_RX2_CRCERR		(1<<22)
#define NV_RX2_OVERFLOW		(1<<23)
#define NV_RX2_FRAMINGERR	(1<<24)
/* error and avail are the same for both */
#define NV_RX2_ERROR		(1<<30)
#define NV_RX2_AVAIL		(1<<31)

/* Miscelaneous hardware related defines: */
#define NV_PCI_REGSZ		0x270

/* various timeout delays: all in usec */
#define NV_TXRX_RESET_DELAY	4
#define NV_TXSTOP_DELAY1	10
#define NV_TXSTOP_DELAY1MAX	500000
#define NV_TXSTOP_DELAY2	100
#define NV_RXSTOP_DELAY1	10
#define NV_RXSTOP_DELAY1MAX	500000
#define NV_RXSTOP_DELAY2	100
#define NV_SETUP5_DELAY		5
#define NV_SETUP5_DELAYMAX	50000
#define NV_POWERUP_DELAY	5
#define NV_POWERUP_DELAYMAX	5000
#define NV_MIIBUSY_DELAY	50
#define NV_MIIPHY_DELAY	10
#define NV_MIIPHY_DELAYMAX	10000

#define NV_WAKEUPPATTERNS	5
#define NV_WAKEUPMASKENTRIES	4

/* General driver defaults */
#define NV_WATCHDOG_TIMEO	(5*HZ)

#define RX_RING		128
#define TX_RING		64
/* 
 * If your nic mysteriously hangs then try to reduce the limits
 * to 1/0: It might be required to set NV_TX_LASTPACKET in the
 * last valid ring entry. But this would be impossible to
 * implement - probably a disassembly error.
 */
#define TX_LIMIT_STOP	63
#define TX_LIMIT_START	62

/* rx/tx mac addr + type + vlan + align + slack*/
#define NV_RX_HEADERS		(64)
/* even more slack. */
#define NV_RX_ALLOC_PAD		(64)

/* maximum mtu size */
#define NV_PKTLIMIT_1	ETH_DATA_LEN	/* hard limit not known */
#define NV_PKTLIMIT_2	9100	/* Actual limit according to NVidia: 9202 */

#define OOM_REFILL	(1+HZ/20)
#define POLL_WAIT	(1+HZ/100)
#define LINK_TIMEOUT	(3*HZ)

/* 
 * desc_ver values:
 * This field has two purposes:
 * - Newer nics uses a different ring layout. The layout is selected by
 *   comparing np->desc_ver with DESC_VER_xy.
 * - It contains bits that are forced on when writing to NvRegTxRxControl.
 */
#define DESC_VER_1	0x0
#define DESC_VER_2	(0x02100|NVREG_TXRXCTL_RXCHECK)
#define DESC_VER_3      (0x02200|NVREG_TXRXCTL_RXCHECK)

/* PHY defines */
#define PHY_OUI_MARVELL	0x5043
#define PHY_OUI_CICADA	0x03f1
#define PHYID1_OUI_MASK	0x03ff
#define PHYID1_OUI_SHFT	6
#define PHYID2_OUI_MASK	0xfc00
#define PHYID2_OUI_SHFT	10
#define PHY_INIT1	0x0f000
#define PHY_INIT2	0x0e00
#define PHY_INIT3	0x01000
#define PHY_INIT4	0x0200
#define PHY_INIT5	0x0004
#define PHY_INIT6	0x02000
#define PHY_GIGABIT	0x0100

#define PHY_TIMEOUT	0x1
#define PHY_ERROR	0x2

#define PHY_100	0x1
#define PHY_1000	0x2
#define PHY_HALF	0x100

/* FIXME: MII defines that should be added to <linux/mii.h> */
#define MII_1000BT_CR	0x09
#define MII_1000BT_SR	0x0a
#define ADVERTISE_1000FULL	0x0200
#define ADVERTISE_1000HALF	0x0100
#define LPA_1000FULL	0x0800
#define LPA_1000HALF	0x0400


/*
 * SMP locking:
 * All hardware access under dev->priv->lock, except the performance
 * critical parts:
 * - rx is (pseudo-) lockless: it relies on the single-threading provided
 *	by the arch code for interrupts.
 * - tx setup is lockless: it relies on dev->xmit_lock. Actual submission
 *	needs dev->priv->lock :-(
 * - set_multicast_list: preparation lockless, relies on dev->xmit_lock.
 */

/* in dev: base, irq */
struct fe_priv {
	spinlock_t lock;

	/* General data:
	 * Locking: spin_lock(&np->lock); */
	struct net_device_stats stats;
	int in_shutdown;
	u32 linkspeed;
	int duplex;
	int autoneg;
	int fixed_mode;
	int phyaddr;
	int wolenabled;
	unsigned int phy_oui;
	u16 gigabit;

	/* General data: RO fields */
	dma_addr_t ring_addr;
	struct pci_dev *pci_dev;
	u32 orig_mac[2];
	u32 irqmask;
	u32 desc_ver;

	void __iomem *base;

	/* rx specific fields.
	 * Locking: Within irq hander or disable_irq+spin_lock(&np->lock);
	 */
	ring_type rx_ring;
	unsigned int cur_rx, refill_rx;
	struct sk_buff *rx_skbuff[RX_RING];
	dma_addr_t rx_dma[RX_RING];
	unsigned int rx_buf_sz;
	unsigned int pkt_limit;
	struct timer_list oom_kick;
	struct timer_list nic_poll;

	/* media detection workaround.
	 * Locking: Within irq hander or disable_irq+spin_lock(&np->lock);
	 */
	int need_linktimer;
	unsigned long link_timeout;
	/*
	 * tx specific fields.
	 */
	ring_type tx_ring;
	unsigned int next_tx, nic_tx;
	struct sk_buff *tx_skbuff[TX_RING];
	dma_addr_t tx_dma[TX_RING];
	u32 tx_flags;
};

/*
 * Maximum number of loops until we assume that a bit in the irq mask
 * is stuck. Overridable with module param.
 */
static int max_interrupt_work = 5;

static inline struct fe_priv *get_nvpriv(struct net_device *dev)
{
	return netdev_priv(dev);
}

static inline u8 __iomem *get_hwbase(struct net_device *dev)
{
	return get_nvpriv(dev)->base;
}

static inline void pci_push(u8 __iomem *base)
{
	/* force out pending posted writes */
	readl(base);
}

static inline u32 nv_descr_getlength(struct ring_desc *prd, u32 v)
{
	return le32_to_cpu(prd->FlagLen)
		& ((v == DESC_VER_1) ? LEN_MASK_V1 : LEN_MASK_V2);
}

static inline u32 nv_descr_getlength_ex(struct ring_desc_ex *prd, u32 v)
{
	return le32_to_cpu(prd->FlagLen) & LEN_MASK_V2;
}

static int reg_delay(struct net_device *dev, int offset, u32 mask, u32 target,
				int delay, int delaymax, const char *msg)
{
	u8 __iomem *base = get_hwbase(dev);

	pci_push(base);
	do {
		udelay(delay);
		delaymax -= delay;
		if (delaymax < 0) {
			if (msg)
				printk(msg);
			return 1;
		}
	} while ((readl(base + offset) & mask) != target);
	return 0;
}

#define MII_READ	(-1)
/* mii_rw: read/write a register on the PHY.
 *
 * Caller must guarantee serialization
 */
static int mii_rw(struct net_device *dev, int addr, int miireg, int value)
{
	u8 __iomem *base = get_hwbase(dev);
	u32 reg;
	int retval;

	writel(NVREG_MIISTAT_MASK, base + NvRegMIIStatus);

	reg = readl(base + NvRegMIIControl);
	if (reg & NVREG_MIICTL_INUSE) {
		writel(NVREG_MIICTL_INUSE, base + NvRegMIIControl);
		udelay(NV_MIIBUSY_DELAY);
	}

	reg = (addr << NVREG_MIICTL_ADDRSHIFT) | miireg;
	if (value != MII_READ) {
		writel(value, base + NvRegMIIData);
		reg |= NVREG_MIICTL_WRITE;
	}
	writel(reg, base + NvRegMIIControl);

	if (reg_delay(dev, NvRegMIIControl, NVREG_MIICTL_INUSE, 0,
			NV_MIIPHY_DELAY, NV_MIIPHY_DELAYMAX, NULL)) {
		dprintk(KERN_DEBUG "%s: mii_rw of reg %d at PHY %d timed out.\n",
				dev->name, miireg, addr);
		retval = -1;
	} else if (value != MII_READ) {
		/* it was a write operation - fewer failures are detectable */
		dprintk(KERN_DEBUG "%s: mii_rw wrote 0x%x to reg %d at PHY %d\n",
				dev->name, value, miireg, addr);
		retval = 0;
	} else if (readl(base + NvRegMIIStatus) & NVREG_MIISTAT_ERROR) {
		dprintk(KERN_DEBUG "%s: mii_rw of reg %d at PHY %d failed.\n",
				dev->name, miireg, addr);
		retval = -1;
	} else {
		retval = readl(base + NvRegMIIData);
		dprintk(KERN_DEBUG "%s: mii_rw read from reg %d at PHY %d: 0x%x.\n",
				dev->name, miireg, addr, retval);
	}

	return retval;
}

static int phy_reset(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u32 miicontrol;
	unsigned int tries = 0;

	miicontrol = mii_rw(dev, np->phyaddr, MII_BMCR, MII_READ);
	miicontrol |= BMCR_RESET;
	if (mii_rw(dev, np->phyaddr, MII_BMCR, miicontrol)) {
		return -1;
	}

	/* wait for 500ms */
	msleep(500);

	/* must wait till reset is deasserted */
	while (miicontrol & BMCR_RESET) {
		msleep(10);
		miicontrol = mii_rw(dev, np->phyaddr, MII_BMCR, MII_READ);
		/* FIXME: 100 tries seem excessive */
		if (tries++ > 100)
			return -1;
	}
	return 0;
}

static int phy_init(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);
	u32 phyinterface, phy_reserved, mii_status, mii_control, mii_control_1000,reg;

	/* set advertise register */
	reg = mii_rw(dev, np->phyaddr, MII_ADVERTISE, MII_READ);
	reg |= (ADVERTISE_10HALF|ADVERTISE_10FULL|ADVERTISE_100HALF|ADVERTISE_100FULL|0x800|0x400);
	if (mii_rw(dev, np->phyaddr, MII_ADVERTISE, reg)) {
		printk(KERN_INFO "%s: phy write to advertise failed.\n", pci_name(np->pci_dev));
		return PHY_ERROR;
	}

	/* get phy interface type */
	phyinterface = readl(base + NvRegPhyInterface);

	/* see if gigabit phy */
	mii_status = mii_rw(dev, np->phyaddr, MII_BMSR, MII_READ);
	if (mii_status & PHY_GIGABIT) {
		np->gigabit = PHY_GIGABIT;
		mii_control_1000 = mii_rw(dev, np->phyaddr, MII_1000BT_CR, MII_READ);
		mii_control_1000 &= ~ADVERTISE_1000HALF;
		if (phyinterface & PHY_RGMII)
			mii_control_1000 |= ADVERTISE_1000FULL;
		else
			mii_control_1000 &= ~ADVERTISE_1000FULL;

		if (mii_rw(dev, np->phyaddr, MII_1000BT_CR, mii_control_1000)) {
			printk(KERN_INFO "%s: phy init failed.\n", pci_name(np->pci_dev));
			return PHY_ERROR;
		}
	}
	else
		np->gigabit = 0;

	/* reset the phy */
	if (phy_reset(dev)) {
		printk(KERN_INFO "%s: phy reset failed\n", pci_name(np->pci_dev));
		return PHY_ERROR;
	}

	/* phy vendor specific configuration */
	if ((np->phy_oui == PHY_OUI_CICADA) && (phyinterface & PHY_RGMII) ) {
		phy_reserved = mii_rw(dev, np->phyaddr, MII_RESV1, MII_READ);
		phy_reserved &= ~(PHY_INIT1 | PHY_INIT2);
		phy_reserved |= (PHY_INIT3 | PHY_INIT4);
		if (mii_rw(dev, np->phyaddr, MII_RESV1, phy_reserved)) {
			printk(KERN_INFO "%s: phy init failed.\n", pci_name(np->pci_dev));
			return PHY_ERROR;
		}
		phy_reserved = mii_rw(dev, np->phyaddr, MII_NCONFIG, MII_READ);
		phy_reserved |= PHY_INIT5;
		if (mii_rw(dev, np->phyaddr, MII_NCONFIG, phy_reserved)) {
			printk(KERN_INFO "%s: phy init failed.\n", pci_name(np->pci_dev));
			return PHY_ERROR;
		}
	}
	if (np->phy_oui == PHY_OUI_CICADA) {
		phy_reserved = mii_rw(dev, np->phyaddr, MII_SREVISION, MII_READ);
		phy_reserved |= PHY_INIT6;
		if (mii_rw(dev, np->phyaddr, MII_SREVISION, phy_reserved)) {
			printk(KERN_INFO "%s: phy init failed.\n", pci_name(np->pci_dev));
			return PHY_ERROR;
		}
	}

	/* restart auto negotiation */
	mii_control = mii_rw(dev, np->phyaddr, MII_BMCR, MII_READ);
	mii_control |= (BMCR_ANRESTART | BMCR_ANENABLE);
	if (mii_rw(dev, np->phyaddr, MII_BMCR, mii_control)) {
		return PHY_ERROR;
	}

	return 0;
}

static void nv_start_rx(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);

	dprintk(KERN_DEBUG "%s: nv_start_rx\n", dev->name);
	/* Already running? Stop it. */
	if (readl(base + NvRegReceiverControl) & NVREG_RCVCTL_START) {
		writel(0, base + NvRegReceiverControl);
		pci_push(base);
	}
	writel(np->linkspeed, base + NvRegLinkSpeed);
	pci_push(base);
	writel(NVREG_RCVCTL_START, base + NvRegReceiverControl);
	dprintk(KERN_DEBUG "%s: nv_start_rx to duplex %d, speed 0x%08x.\n",
				dev->name, np->duplex, np->linkspeed);
	pci_push(base);
}

static void nv_stop_rx(struct net_device *dev)
{
	u8 __iomem *base = get_hwbase(dev);

	dprintk(KERN_DEBUG "%s: nv_stop_rx\n", dev->name);
	writel(0, base + NvRegReceiverControl);
	reg_delay(dev, NvRegReceiverStatus, NVREG_RCVSTAT_BUSY, 0,
			NV_RXSTOP_DELAY1, NV_RXSTOP_DELAY1MAX,
			KERN_INFO "nv_stop_rx: ReceiverStatus remained busy");

	udelay(NV_RXSTOP_DELAY2);
	writel(0, base + NvRegLinkSpeed);
}

static void nv_start_tx(struct net_device *dev)
{
	u8 __iomem *base = get_hwbase(dev);

	dprintk(KERN_DEBUG "%s: nv_start_tx\n", dev->name);
	writel(NVREG_XMITCTL_START, base + NvRegTransmitterControl);
	pci_push(base);
}

static void nv_stop_tx(struct net_device *dev)
{
	u8 __iomem *base = get_hwbase(dev);

	dprintk(KERN_DEBUG "%s: nv_stop_tx\n", dev->name);
	writel(0, base + NvRegTransmitterControl);
	reg_delay(dev, NvRegTransmitterStatus, NVREG_XMITSTAT_BUSY, 0,
			NV_TXSTOP_DELAY1, NV_TXSTOP_DELAY1MAX,
			KERN_INFO "nv_stop_tx: TransmitterStatus remained busy");

	udelay(NV_TXSTOP_DELAY2);
	writel(0, base + NvRegUnknownTransmitterReg);
}

static void nv_txrx_reset(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);

	dprintk(KERN_DEBUG "%s: nv_txrx_reset\n", dev->name);
	writel(NVREG_TXRXCTL_BIT2 | NVREG_TXRXCTL_RESET | np->desc_ver, base + NvRegTxRxControl);
	pci_push(base);
	udelay(NV_TXRX_RESET_DELAY);
	writel(NVREG_TXRXCTL_BIT2 | np->desc_ver, base + NvRegTxRxControl);
	pci_push(base);
}

/*
 * nv_get_stats: dev->get_stats function
 * Get latest stats value from the nic.
 * Called with read_lock(&dev_base_lock) held for read -
 * only synchronized against unregister_netdevice.
 */
static struct net_device_stats *nv_get_stats(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);

	/* It seems that the nic always generates interrupts and doesn't
	 * accumulate errors internally. Thus the current values in np->stats
	 * are already up to date.
	 */
	return &np->stats;
}

/*
 * nv_alloc_rx: fill rx ring entries.
 * Return 1 if the allocations for the skbs failed and the
 * rx engine is without Available descriptors
 */
static int nv_alloc_rx(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	unsigned int refill_rx = np->refill_rx;
	int nr;

	while (np->cur_rx != refill_rx) {
		struct sk_buff *skb;

		nr = refill_rx % RX_RING;
		if (np->rx_skbuff[nr] == NULL) {

			skb = dev_alloc_skb(np->rx_buf_sz + NV_RX_ALLOC_PAD);
			if (!skb)
				break;

			skb->dev = dev;
			np->rx_skbuff[nr] = skb;
		} else {
			skb = np->rx_skbuff[nr];
		}
		np->rx_dma[nr] = pci_map_single(np->pci_dev, skb->data, skb->len,
						PCI_DMA_FROMDEVICE);
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2) {
			np->rx_ring.orig[nr].PacketBuffer = cpu_to_le32(np->rx_dma[nr]);
			wmb();
			np->rx_ring.orig[nr].FlagLen = cpu_to_le32(np->rx_buf_sz | NV_RX_AVAIL);
		} else {
			np->rx_ring.ex[nr].PacketBufferHigh = cpu_to_le64(np->rx_dma[nr]) >> 32;
			np->rx_ring.ex[nr].PacketBufferLow = cpu_to_le64(np->rx_dma[nr]) & 0x0FFFFFFFF;
			wmb();
			np->rx_ring.ex[nr].FlagLen = cpu_to_le32(np->rx_buf_sz | NV_RX2_AVAIL);
		}
		dprintk(KERN_DEBUG "%s: nv_alloc_rx: Packet %d marked as Available\n",
					dev->name, refill_rx);
		refill_rx++;
	}
	np->refill_rx = refill_rx;
	if (np->cur_rx - refill_rx == RX_RING)
		return 1;
	return 0;
}

static void nv_do_rx_refill(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct fe_priv *np = get_nvpriv(dev);

	disable_irq(dev->irq);
	if (nv_alloc_rx(dev)) {
		spin_lock(&np->lock);
		if (!np->in_shutdown)
			mod_timer(&np->oom_kick, jiffies + OOM_REFILL);
		spin_unlock(&np->lock);
	}
	enable_irq(dev->irq);
}

static void nv_init_rx(struct net_device *dev) 
{
	struct fe_priv *np = get_nvpriv(dev);
	int i;

	np->cur_rx = RX_RING;
	np->refill_rx = 0;
	for (i = 0; i < RX_RING; i++)
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
			np->rx_ring.orig[i].FlagLen = 0;
	        else
			np->rx_ring.ex[i].FlagLen = 0;
}

static void nv_init_tx(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	int i;

	np->next_tx = np->nic_tx = 0;
	for (i = 0; i < TX_RING; i++)
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
			np->tx_ring.orig[i].FlagLen = 0;
	        else
			np->tx_ring.ex[i].FlagLen = 0;
}

static int nv_init_ring(struct net_device *dev)
{
	nv_init_tx(dev);
	nv_init_rx(dev);
	return nv_alloc_rx(dev);
}

static void nv_drain_tx(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	int i;
	for (i = 0; i < TX_RING; i++) {
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
			np->tx_ring.orig[i].FlagLen = 0;
		else
			np->tx_ring.ex[i].FlagLen = 0;
		if (np->tx_skbuff[i]) {
			pci_unmap_single(np->pci_dev, np->tx_dma[i],
						np->tx_skbuff[i]->len,
						PCI_DMA_TODEVICE);
			dev_kfree_skb(np->tx_skbuff[i]);
			np->tx_skbuff[i] = NULL;
			np->stats.tx_dropped++;
		}
	}
}

static void nv_drain_rx(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	int i;
	for (i = 0; i < RX_RING; i++) {
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
			np->rx_ring.orig[i].FlagLen = 0;
		else
			np->rx_ring.ex[i].FlagLen = 0;
		wmb();
		if (np->rx_skbuff[i]) {
			pci_unmap_single(np->pci_dev, np->rx_dma[i],
						np->rx_skbuff[i]->len,
						PCI_DMA_FROMDEVICE);
			dev_kfree_skb(np->rx_skbuff[i]);
			np->rx_skbuff[i] = NULL;
		}
	}
}

static void drain_ring(struct net_device *dev)
{
	nv_drain_tx(dev);
	nv_drain_rx(dev);
}

/*
 * nv_start_xmit: dev->hard_start_xmit function
 * Called with dev->xmit_lock held.
 */
static int nv_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	int nr = np->next_tx % TX_RING;

	np->tx_skbuff[nr] = skb;
	np->tx_dma[nr] = pci_map_single(np->pci_dev, skb->data,skb->len,
					PCI_DMA_TODEVICE);

	if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
		np->tx_ring.orig[nr].PacketBuffer = cpu_to_le32(np->tx_dma[nr]);
	else {
		np->tx_ring.ex[nr].PacketBufferHigh = cpu_to_le64(np->tx_dma[nr]) >> 32;
		np->tx_ring.ex[nr].PacketBufferLow = cpu_to_le64(np->tx_dma[nr]) & 0x0FFFFFFFF;
	}

	spin_lock_irq(&np->lock);
	wmb();
	if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
		np->tx_ring.orig[nr].FlagLen = cpu_to_le32( (skb->len-1) | np->tx_flags );
	else
		np->tx_ring.ex[nr].FlagLen = cpu_to_le32( (skb->len-1) | np->tx_flags );
	dprintk(KERN_DEBUG "%s: nv_start_xmit: packet packet %d queued for transmission.\n",
				dev->name, np->next_tx);
	{
		int j;
		for (j=0; j<64; j++) {
			if ((j%16) == 0)
				dprintk("\n%03x:", j);
			dprintk(" %02x", ((unsigned char*)skb->data)[j]);
		}
		dprintk("\n");
	}

	np->next_tx++;

	dev->trans_start = jiffies;
	if (np->next_tx - np->nic_tx >= TX_LIMIT_STOP)
		netif_stop_queue(dev);
	spin_unlock_irq(&np->lock);
	writel(NVREG_TXRXCTL_KICK|np->desc_ver, get_hwbase(dev) + NvRegTxRxControl);
	pci_push(get_hwbase(dev));
	return 0;
}

/*
 * nv_tx_done: check for completed packets, release the skbs.
 *
 * Caller must own np->lock.
 */
static void nv_tx_done(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u32 Flags;
	int i;

	while (np->nic_tx != np->next_tx) {
		i = np->nic_tx % TX_RING;

		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
			Flags = le32_to_cpu(np->tx_ring.orig[i].FlagLen);
		else
			Flags = le32_to_cpu(np->tx_ring.ex[i].FlagLen);

		dprintk(KERN_DEBUG "%s: nv_tx_done: looking at packet %d, Flags 0x%x.\n",
					dev->name, np->nic_tx, Flags);
		if (Flags & NV_TX_VALID)
			break;
		if (np->desc_ver == DESC_VER_1) {
			if (Flags & (NV_TX_RETRYERROR|NV_TX_CARRIERLOST|NV_TX_LATECOLLISION|
							NV_TX_UNDERFLOW|NV_TX_ERROR)) {
				if (Flags & NV_TX_UNDERFLOW)
					np->stats.tx_fifo_errors++;
				if (Flags & NV_TX_CARRIERLOST)
					np->stats.tx_carrier_errors++;
				np->stats.tx_errors++;
			} else {
				np->stats.tx_packets++;
				np->stats.tx_bytes += np->tx_skbuff[i]->len;
			}
		} else {
			if (Flags & (NV_TX2_RETRYERROR|NV_TX2_CARRIERLOST|NV_TX2_LATECOLLISION|
							NV_TX2_UNDERFLOW|NV_TX2_ERROR)) {
				if (Flags & NV_TX2_UNDERFLOW)
					np->stats.tx_fifo_errors++;
				if (Flags & NV_TX2_CARRIERLOST)
					np->stats.tx_carrier_errors++;
				np->stats.tx_errors++;
			} else {
				np->stats.tx_packets++;
				np->stats.tx_bytes += np->tx_skbuff[i]->len;
			}
		}
		pci_unmap_single(np->pci_dev, np->tx_dma[i],
					np->tx_skbuff[i]->len,
					PCI_DMA_TODEVICE);
		dev_kfree_skb_irq(np->tx_skbuff[i]);
		np->tx_skbuff[i] = NULL;
		np->nic_tx++;
	}
	if (np->next_tx - np->nic_tx < TX_LIMIT_START)
		netif_wake_queue(dev);
}

/*
 * nv_tx_timeout: dev->tx_timeout function
 * Called with dev->xmit_lock held.
 */
static void nv_tx_timeout(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);

	printk(KERN_INFO "%s: Got tx_timeout. irq: %08x\n", dev->name,
			readl(base + NvRegIrqStatus) & NVREG_IRQSTAT_MASK);

	{
		int i;

		printk(KERN_INFO "%s: Ring at %lx: next %d nic %d\n",
				dev->name, (unsigned long)np->ring_addr,
				np->next_tx, np->nic_tx);
		printk(KERN_INFO "%s: Dumping tx registers\n", dev->name);
		for (i=0;i<0x400;i+= 32) {
			printk(KERN_INFO "%3x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
					i,
					readl(base + i + 0), readl(base + i + 4),
					readl(base + i + 8), readl(base + i + 12),
					readl(base + i + 16), readl(base + i + 20),
					readl(base + i + 24), readl(base + i + 28));
		}
		printk(KERN_INFO "%s: Dumping tx ring\n", dev->name);
		for (i=0;i<TX_RING;i+= 4) {
			if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2) {
				printk(KERN_INFO "%03x: %08x %08x // %08x %08x // %08x %08x // %08x %08x\n",
				       i, 
				       le32_to_cpu(np->tx_ring.orig[i].PacketBuffer),
				       le32_to_cpu(np->tx_ring.orig[i].FlagLen),
				       le32_to_cpu(np->tx_ring.orig[i+1].PacketBuffer),
				       le32_to_cpu(np->tx_ring.orig[i+1].FlagLen),
				       le32_to_cpu(np->tx_ring.orig[i+2].PacketBuffer),
				       le32_to_cpu(np->tx_ring.orig[i+2].FlagLen),
				       le32_to_cpu(np->tx_ring.orig[i+3].PacketBuffer),
				       le32_to_cpu(np->tx_ring.orig[i+3].FlagLen));
			} else {
				printk(KERN_INFO "%03x: %08x %08x %08x // %08x %08x %08x // %08x %08x %08x // %08x %08x %08x\n",
				       i, 
				       le32_to_cpu(np->tx_ring.ex[i].PacketBufferHigh),
				       le32_to_cpu(np->tx_ring.ex[i].PacketBufferLow),
				       le32_to_cpu(np->tx_ring.ex[i].FlagLen),
				       le32_to_cpu(np->tx_ring.ex[i+1].PacketBufferHigh),
				       le32_to_cpu(np->tx_ring.ex[i+1].PacketBufferLow),
				       le32_to_cpu(np->tx_ring.ex[i+1].FlagLen),
				       le32_to_cpu(np->tx_ring.ex[i+2].PacketBufferHigh),
				       le32_to_cpu(np->tx_ring.ex[i+2].PacketBufferLow),
				       le32_to_cpu(np->tx_ring.ex[i+2].FlagLen),
				       le32_to_cpu(np->tx_ring.ex[i+3].PacketBufferHigh),
				       le32_to_cpu(np->tx_ring.ex[i+3].PacketBufferLow),
				       le32_to_cpu(np->tx_ring.ex[i+3].FlagLen));
			}
		}
	}

	spin_lock_irq(&np->lock);

	/* 1) stop tx engine */
	nv_stop_tx(dev);

	/* 2) check that the packets were not sent already: */
	nv_tx_done(dev);

	/* 3) if there are dead entries: clear everything */
	if (np->next_tx != np->nic_tx) {
		printk(KERN_DEBUG "%s: tx_timeout: dead entries!\n", dev->name);
		nv_drain_tx(dev);
		np->next_tx = np->nic_tx = 0;
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
			writel((u32) (np->ring_addr + RX_RING*sizeof(struct ring_desc)), base + NvRegTxRingPhysAddr);
		else
			writel((u32) (np->ring_addr + RX_RING*sizeof(struct ring_desc_ex)), base + NvRegTxRingPhysAddr);
		netif_wake_queue(dev);
	}

	/* 4) restart tx engine */
	nv_start_tx(dev);
	spin_unlock_irq(&np->lock);
}

/*
 * Called when the nic notices a mismatch between the actual data len on the
 * wire and the len indicated in the 802 header
 */
static int nv_getlen(struct net_device *dev, void *packet, int datalen)
{
	int hdrlen;	/* length of the 802 header */
	int protolen;	/* length as stored in the proto field */

	/* 1) calculate len according to header */
	if ( ((struct vlan_ethhdr *)packet)->h_vlan_proto == __constant_htons(ETH_P_8021Q)) {
		protolen = ntohs( ((struct vlan_ethhdr *)packet)->h_vlan_encapsulated_proto );
		hdrlen = VLAN_HLEN;
	} else {
		protolen = ntohs( ((struct ethhdr *)packet)->h_proto);
		hdrlen = ETH_HLEN;
	}
	dprintk(KERN_DEBUG "%s: nv_getlen: datalen %d, protolen %d, hdrlen %d\n",
				dev->name, datalen, protolen, hdrlen);
	if (protolen > ETH_DATA_LEN)
		return datalen; /* Value in proto field not a len, no checks possible */

	protolen += hdrlen;
	/* consistency checks: */
	if (datalen > ETH_ZLEN) {
		if (datalen >= protolen) {
			/* more data on wire than in 802 header, trim of
			 * additional data.
			 */
			dprintk(KERN_DEBUG "%s: nv_getlen: accepting %d bytes.\n",
					dev->name, protolen);
			return protolen;
		} else {
			/* less data on wire than mentioned in header.
			 * Discard the packet.
			 */
			dprintk(KERN_DEBUG "%s: nv_getlen: discarding long packet.\n",
					dev->name);
			return -1;
		}
	} else {
		/* short packet. Accept only if 802 values are also short */
		if (protolen > ETH_ZLEN) {
			dprintk(KERN_DEBUG "%s: nv_getlen: discarding short packet.\n",
					dev->name);
			return -1;
		}
		dprintk(KERN_DEBUG "%s: nv_getlen: accepting %d bytes.\n",
				dev->name, datalen);
		return datalen;
	}
}

static void nv_rx_process(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u32 Flags;

	for (;;) {
		struct sk_buff *skb;
		int len;
		int i;
		if (np->cur_rx - np->refill_rx >= RX_RING)
			break;	/* we scanned the whole ring - do not continue */

		i = np->cur_rx % RX_RING;
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2) {
			Flags = le32_to_cpu(np->rx_ring.orig[i].FlagLen);
			len = nv_descr_getlength(&np->rx_ring.orig[i], np->desc_ver);
		} else {
			Flags = le32_to_cpu(np->rx_ring.ex[i].FlagLen);
			len = nv_descr_getlength_ex(&np->rx_ring.ex[i], np->desc_ver);
		}

		dprintk(KERN_DEBUG "%s: nv_rx_process: looking at packet %d, Flags 0x%x.\n",
					dev->name, np->cur_rx, Flags);

		if (Flags & NV_RX_AVAIL)
			break;	/* still owned by hardware, */

		/*
		 * the packet is for us - immediately tear down the pci mapping.
		 * TODO: check if a prefetch of the first cacheline improves
		 * the performance.
		 */
		pci_unmap_single(np->pci_dev, np->rx_dma[i],
				np->rx_skbuff[i]->len,
				PCI_DMA_FROMDEVICE);

		{
			int j;
			dprintk(KERN_DEBUG "Dumping packet (flags 0x%x).",Flags);
			for (j=0; j<64; j++) {
				if ((j%16) == 0)
					dprintk("\n%03x:", j);
				dprintk(" %02x", ((unsigned char*)np->rx_skbuff[i]->data)[j]);
			}
			dprintk("\n");
		}
		/* look at what we actually got: */
		if (np->desc_ver == DESC_VER_1) {
			if (!(Flags & NV_RX_DESCRIPTORVALID))
				goto next_pkt;

			if (Flags & NV_RX_MISSEDFRAME) {
				np->stats.rx_missed_errors++;
				np->stats.rx_errors++;
				goto next_pkt;
			}
			if (Flags & (NV_RX_ERROR1|NV_RX_ERROR2|NV_RX_ERROR3)) {
				np->stats.rx_errors++;
				goto next_pkt;
			}
			if (Flags & NV_RX_CRCERR) {
				np->stats.rx_crc_errors++;
				np->stats.rx_errors++;
				goto next_pkt;
			}
			if (Flags & NV_RX_OVERFLOW) {
				np->stats.rx_over_errors++;
				np->stats.rx_errors++;
				goto next_pkt;
			}
			if (Flags & NV_RX_ERROR4) {
				len = nv_getlen(dev, np->rx_skbuff[i]->data, len);
				if (len < 0) {
					np->stats.rx_errors++;
					goto next_pkt;
				}
			}
			/* framing errors are soft errors. */
			if (Flags & NV_RX_FRAMINGERR) {
				if (Flags & NV_RX_SUBSTRACT1) {
					len--;
				}
			}
		} else {
			if (!(Flags & NV_RX2_DESCRIPTORVALID))
				goto next_pkt;

			if (Flags & (NV_RX2_ERROR1|NV_RX2_ERROR2|NV_RX2_ERROR3)) {
				np->stats.rx_errors++;
				goto next_pkt;
			}
			if (Flags & NV_RX2_CRCERR) {
				np->stats.rx_crc_errors++;
				np->stats.rx_errors++;
				goto next_pkt;
			}
			if (Flags & NV_RX2_OVERFLOW) {
				np->stats.rx_over_errors++;
				np->stats.rx_errors++;
				goto next_pkt;
			}
			if (Flags & NV_RX2_ERROR4) {
				len = nv_getlen(dev, np->rx_skbuff[i]->data, len);
				if (len < 0) {
					np->stats.rx_errors++;
					goto next_pkt;
				}
			}
			/* framing errors are soft errors */
			if (Flags & NV_RX2_FRAMINGERR) {
				if (Flags & NV_RX2_SUBSTRACT1) {
					len--;
				}
			}
			Flags &= NV_RX2_CHECKSUMMASK;
			if (Flags == NV_RX2_CHECKSUMOK1 ||
					Flags == NV_RX2_CHECKSUMOK2 ||
					Flags == NV_RX2_CHECKSUMOK3) {
				dprintk(KERN_DEBUG "%s: hw checksum hit!.\n", dev->name);
				np->rx_skbuff[i]->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				dprintk(KERN_DEBUG "%s: hwchecksum miss!.\n", dev->name);
			}
		}
		/* got a valid packet - forward it to the network core */
		skb = np->rx_skbuff[i];
		np->rx_skbuff[i] = NULL;

		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, dev);
		dprintk(KERN_DEBUG "%s: nv_rx_process: packet %d with %d bytes, proto %d accepted.\n",
					dev->name, np->cur_rx, len, skb->protocol);
		netif_rx(skb);
		dev->last_rx = jiffies;
		np->stats.rx_packets++;
		np->stats.rx_bytes += len;
next_pkt:
		np->cur_rx++;
	}
}

static void set_bufsize(struct net_device *dev)
{
	struct fe_priv *np = netdev_priv(dev);

	if (dev->mtu <= ETH_DATA_LEN)
		np->rx_buf_sz = ETH_DATA_LEN + NV_RX_HEADERS;
	else
		np->rx_buf_sz = dev->mtu + NV_RX_HEADERS;
}

/*
 * nv_change_mtu: dev->change_mtu function
 * Called with dev_base_lock held for read.
 */
static int nv_change_mtu(struct net_device *dev, int new_mtu)
{
	struct fe_priv *np = get_nvpriv(dev);
	int old_mtu;

	if (new_mtu < 64 || new_mtu > np->pkt_limit)
		return -EINVAL;

	old_mtu = dev->mtu;
	dev->mtu = new_mtu;

	/* return early if the buffer sizes will not change */
	if (old_mtu <= ETH_DATA_LEN && new_mtu <= ETH_DATA_LEN)
		return 0;
	if (old_mtu == new_mtu)
		return 0;

	/* synchronized against open : rtnl_lock() held by caller */
	if (netif_running(dev)) {
		u8 __iomem *base = get_hwbase(dev);
		/*
		 * It seems that the nic preloads valid ring entries into an
		 * internal buffer. The procedure for flushing everything is
		 * guessed, there is probably a simpler approach.
		 * Changing the MTU is a rare event, it shouldn't matter.
		 */
		disable_irq(dev->irq);
		spin_lock_bh(&dev->xmit_lock);
		spin_lock(&np->lock);
		/* stop engines */
		nv_stop_rx(dev);
		nv_stop_tx(dev);
		nv_txrx_reset(dev);
		/* drain rx queue */
		nv_drain_rx(dev);
		nv_drain_tx(dev);
		/* reinit driver view of the rx queue */
		nv_init_rx(dev);
		nv_init_tx(dev);
		/* alloc new rx buffers */
		set_bufsize(dev);
		if (nv_alloc_rx(dev)) {
			if (!np->in_shutdown)
				mod_timer(&np->oom_kick, jiffies + OOM_REFILL);
		}
		/* reinit nic view of the rx queue */
		writel(np->rx_buf_sz, base + NvRegOffloadConfig);
		writel((u32) np->ring_addr, base + NvRegRxRingPhysAddr);
		if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
			writel((u32) (np->ring_addr + RX_RING*sizeof(struct ring_desc)), base + NvRegTxRingPhysAddr);
		else
			writel((u32) (np->ring_addr + RX_RING*sizeof(struct ring_desc_ex)), base + NvRegTxRingPhysAddr);
		writel( ((RX_RING-1) << NVREG_RINGSZ_RXSHIFT) + ((TX_RING-1) << NVREG_RINGSZ_TXSHIFT),
			base + NvRegRingSizes);
		pci_push(base);
		writel(NVREG_TXRXCTL_KICK|np->desc_ver, get_hwbase(dev) + NvRegTxRxControl);
		pci_push(base);

		/* restart rx engine */
		nv_start_rx(dev);
		nv_start_tx(dev);
		spin_unlock(&np->lock);
		spin_unlock_bh(&dev->xmit_lock);
		enable_irq(dev->irq);
	}
	return 0;
}

static void nv_copy_mac_to_hw(struct net_device *dev)
{
	u8 __iomem *base = get_hwbase(dev);
	u32 mac[2];

	mac[0] = (dev->dev_addr[0] << 0) + (dev->dev_addr[1] << 8) +
			(dev->dev_addr[2] << 16) + (dev->dev_addr[3] << 24);
	mac[1] = (dev->dev_addr[4] << 0) + (dev->dev_addr[5] << 8);

	writel(mac[0], base + NvRegMacAddrA);
	writel(mac[1], base + NvRegMacAddrB);
}

/*
 * nv_set_mac_address: dev->set_mac_address function
 * Called with rtnl_lock() held.
 */
static int nv_set_mac_address(struct net_device *dev, void *addr)
{
	struct fe_priv *np = get_nvpriv(dev);
	struct sockaddr *macaddr = (struct sockaddr*)addr;

	if(!is_valid_ether_addr(macaddr->sa_data))
		return -EADDRNOTAVAIL;

	/* synchronized against open : rtnl_lock() held by caller */
	memcpy(dev->dev_addr, macaddr->sa_data, ETH_ALEN);

	if (netif_running(dev)) {
		spin_lock_bh(&dev->xmit_lock);
		spin_lock_irq(&np->lock);

		/* stop rx engine */
		nv_stop_rx(dev);

		/* set mac address */
		nv_copy_mac_to_hw(dev);

		/* restart rx engine */
		nv_start_rx(dev);
		spin_unlock_irq(&np->lock);
		spin_unlock_bh(&dev->xmit_lock);
	} else {
		nv_copy_mac_to_hw(dev);
	}
	return 0;
}

/*
 * nv_set_multicast: dev->set_multicast function
 * Called with dev->xmit_lock held.
 */
static void nv_set_multicast(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);
	u32 addr[2];
	u32 mask[2];
	u32 pff;

	memset(addr, 0, sizeof(addr));
	memset(mask, 0, sizeof(mask));

	if (dev->flags & IFF_PROMISC) {
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		pff = NVREG_PFF_PROMISC;
	} else {
		pff = NVREG_PFF_MYADDR;

		if (dev->flags & IFF_ALLMULTI || dev->mc_list) {
			u32 alwaysOff[2];
			u32 alwaysOn[2];

			alwaysOn[0] = alwaysOn[1] = alwaysOff[0] = alwaysOff[1] = 0xffffffff;
			if (dev->flags & IFF_ALLMULTI) {
				alwaysOn[0] = alwaysOn[1] = alwaysOff[0] = alwaysOff[1] = 0;
			} else {
				struct dev_mc_list *walk;

				walk = dev->mc_list;
				while (walk != NULL) {
					u32 a, b;
					a = le32_to_cpu(*(u32 *) walk->dmi_addr);
					b = le16_to_cpu(*(u16 *) (&walk->dmi_addr[4]));
					alwaysOn[0] &= a;
					alwaysOff[0] &= ~a;
					alwaysOn[1] &= b;
					alwaysOff[1] &= ~b;
					walk = walk->next;
				}
			}
			addr[0] = alwaysOn[0];
			addr[1] = alwaysOn[1];
			mask[0] = alwaysOn[0] | alwaysOff[0];
			mask[1] = alwaysOn[1] | alwaysOff[1];
		}
	}
	addr[0] |= NVREG_MCASTADDRA_FORCE;
	pff |= NVREG_PFF_ALWAYS;
	spin_lock_irq(&np->lock);
	nv_stop_rx(dev);
	writel(addr[0], base + NvRegMulticastAddrA);
	writel(addr[1], base + NvRegMulticastAddrB);
	writel(mask[0], base + NvRegMulticastMaskA);
	writel(mask[1], base + NvRegMulticastMaskB);
	writel(pff, base + NvRegPacketFilterFlags);
	dprintk(KERN_INFO "%s: reconfiguration for multicast lists.\n",
		dev->name);
	nv_start_rx(dev);
	spin_unlock_irq(&np->lock);
}

static int nv_update_linkspeed(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);
	int adv, lpa;
	int newls = np->linkspeed;
	int newdup = np->duplex;
	int mii_status;
	int retval = 0;
	u32 control_1000, status_1000, phyreg;

	/* BMSR_LSTATUS is latched, read it twice:
	 * we want the current value.
	 */
	mii_rw(dev, np->phyaddr, MII_BMSR, MII_READ);
	mii_status = mii_rw(dev, np->phyaddr, MII_BMSR, MII_READ);

	if (!(mii_status & BMSR_LSTATUS)) {
		dprintk(KERN_DEBUG "%s: no link detected by phy - falling back to 10HD.\n",
				dev->name);
		newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
		newdup = 0;
		retval = 0;
		goto set_speed;
	}

	if (np->autoneg == 0) {
		dprintk(KERN_DEBUG "%s: nv_update_linkspeed: autoneg off, PHY set to 0x%04x.\n",
				dev->name, np->fixed_mode);
		if (np->fixed_mode & LPA_100FULL) {
			newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_100;
			newdup = 1;
		} else if (np->fixed_mode & LPA_100HALF) {
			newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_100;
			newdup = 0;
		} else if (np->fixed_mode & LPA_10FULL) {
			newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
			newdup = 1;
		} else {
			newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
			newdup = 0;
		}
		retval = 1;
		goto set_speed;
	}
	/* check auto negotiation is complete */
	if (!(mii_status & BMSR_ANEGCOMPLETE)) {
		/* still in autonegotiation - configure nic for 10 MBit HD and wait. */
		newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
		newdup = 0;
		retval = 0;
		dprintk(KERN_DEBUG "%s: autoneg not completed - falling back to 10HD.\n", dev->name);
		goto set_speed;
	}

	retval = 1;
	if (np->gigabit == PHY_GIGABIT) {
		control_1000 = mii_rw(dev, np->phyaddr, MII_1000BT_CR, MII_READ);
		status_1000 = mii_rw(dev, np->phyaddr, MII_1000BT_SR, MII_READ);

		if ((control_1000 & ADVERTISE_1000FULL) &&
			(status_1000 & LPA_1000FULL)) {
			dprintk(KERN_DEBUG "%s: nv_update_linkspeed: GBit ethernet detected.\n",
				dev->name);
			newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_1000;
			newdup = 1;
			goto set_speed;
		}
	}

	adv = mii_rw(dev, np->phyaddr, MII_ADVERTISE, MII_READ);
	lpa = mii_rw(dev, np->phyaddr, MII_LPA, MII_READ);
	dprintk(KERN_DEBUG "%s: nv_update_linkspeed: PHY advertises 0x%04x, lpa 0x%04x.\n",
				dev->name, adv, lpa);

	/* FIXME: handle parallel detection properly */
	lpa = lpa & adv;
	if (lpa & LPA_100FULL) {
		newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_100;
		newdup = 1;
	} else if (lpa & LPA_100HALF) {
		newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_100;
		newdup = 0;
	} else if (lpa & LPA_10FULL) {
		newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
		newdup = 1;
	} else if (lpa & LPA_10HALF) {
		newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
		newdup = 0;
	} else {
		dprintk(KERN_DEBUG "%s: bad ability %04x - falling back to 10HD.\n", dev->name, lpa);
		newls = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
		newdup = 0;
	}

set_speed:
	if (np->duplex == newdup && np->linkspeed == newls)
		return retval;

	dprintk(KERN_INFO "%s: changing link setting from %d/%d to %d/%d.\n",
			dev->name, np->linkspeed, np->duplex, newls, newdup);

	np->duplex = newdup;
	np->linkspeed = newls;

	if (np->gigabit == PHY_GIGABIT) {
		phyreg = readl(base + NvRegRandomSeed);
		phyreg &= ~(0x3FF00);
		if ((np->linkspeed & 0xFFF) == NVREG_LINKSPEED_10)
			phyreg |= NVREG_RNDSEED_FORCE3;
		else if ((np->linkspeed & 0xFFF) == NVREG_LINKSPEED_100)
			phyreg |= NVREG_RNDSEED_FORCE2;
		else if ((np->linkspeed & 0xFFF) == NVREG_LINKSPEED_1000)
			phyreg |= NVREG_RNDSEED_FORCE;
		writel(phyreg, base + NvRegRandomSeed);
	}

	phyreg = readl(base + NvRegPhyInterface);
	phyreg &= ~(PHY_HALF|PHY_100|PHY_1000);
	if (np->duplex == 0)
		phyreg |= PHY_HALF;
	if ((np->linkspeed & NVREG_LINKSPEED_MASK) == NVREG_LINKSPEED_100)
		phyreg |= PHY_100;
	else if ((np->linkspeed & NVREG_LINKSPEED_MASK) == NVREG_LINKSPEED_1000)
		phyreg |= PHY_1000;
	writel(phyreg, base + NvRegPhyInterface);

	writel(NVREG_MISC1_FORCE | ( np->duplex ? 0 : NVREG_MISC1_HD),
		base + NvRegMisc1);
	pci_push(base);
	writel(np->linkspeed, base + NvRegLinkSpeed);
	pci_push(base);

	return retval;
}

static void nv_linkchange(struct net_device *dev)
{
	if (nv_update_linkspeed(dev)) {
		if (netif_carrier_ok(dev)) {
			nv_stop_rx(dev);
		} else {
			netif_carrier_on(dev);
			printk(KERN_INFO "%s: link up.\n", dev->name);
		}
		nv_start_rx(dev);
	} else {
		if (netif_carrier_ok(dev)) {
			netif_carrier_off(dev);
			printk(KERN_INFO "%s: link down.\n", dev->name);
			nv_stop_rx(dev);
		}
	}
}

static void nv_link_irq(struct net_device *dev)
{
	u8 __iomem *base = get_hwbase(dev);
	u32 miistat;

	miistat = readl(base + NvRegMIIStatus);
	writel(NVREG_MIISTAT_MASK, base + NvRegMIIStatus);
	dprintk(KERN_INFO "%s: link change irq, status 0x%x.\n", dev->name, miistat);

	if (miistat & (NVREG_MIISTAT_LINKCHANGE))
		nv_linkchange(dev);
	dprintk(KERN_DEBUG "%s: link change notification done.\n", dev->name);
}

static irqreturn_t nv_nic_irq(int foo, void *data, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) data;
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);
	u32 events;
	int i;

	dprintk(KERN_DEBUG "%s: nv_nic_irq\n", dev->name);

	for (i=0; ; i++) {
		events = readl(base + NvRegIrqStatus) & NVREG_IRQSTAT_MASK;
		writel(NVREG_IRQSTAT_MASK, base + NvRegIrqStatus);
		pci_push(base);
		dprintk(KERN_DEBUG "%s: irq: %08x\n", dev->name, events);
		if (!(events & np->irqmask))
			break;

		if (events & (NVREG_IRQ_TX1|NVREG_IRQ_TX_OK|NVREG_IRQ_TX_ERROR|NVREG_IRQ_TX_ERR)) {
			spin_lock(&np->lock);
			nv_tx_done(dev);
			spin_unlock(&np->lock);
		}

		if (events & (NVREG_IRQ_RX_ERROR|NVREG_IRQ_RX|NVREG_IRQ_RX_NOBUF)) {
			nv_rx_process(dev);
			if (nv_alloc_rx(dev)) {
				spin_lock(&np->lock);
				if (!np->in_shutdown)
					mod_timer(&np->oom_kick, jiffies + OOM_REFILL);
				spin_unlock(&np->lock);
			}
		}

		if (events & NVREG_IRQ_LINK) {
			spin_lock(&np->lock);
			nv_link_irq(dev);
			spin_unlock(&np->lock);
		}
		if (np->need_linktimer && time_after(jiffies, np->link_timeout)) {
			spin_lock(&np->lock);
			nv_linkchange(dev);
			spin_unlock(&np->lock);
			np->link_timeout = jiffies + LINK_TIMEOUT;
		}
		if (events & (NVREG_IRQ_TX_ERR)) {
			dprintk(KERN_DEBUG "%s: received irq with events 0x%x. Probably TX fail.\n",
						dev->name, events);
		}
		if (events & (NVREG_IRQ_UNKNOWN)) {
			printk(KERN_DEBUG "%s: received irq with unknown events 0x%x. Please report\n",
						dev->name, events);
		}
		if (i > max_interrupt_work) {
			spin_lock(&np->lock);
			/* disable interrupts on the nic */
			writel(0, base + NvRegIrqMask);
			pci_push(base);

			if (!np->in_shutdown)
				mod_timer(&np->nic_poll, jiffies + POLL_WAIT);
			printk(KERN_DEBUG "%s: too many iterations (%d) in nv_nic_irq.\n", dev->name, i);
			spin_unlock(&np->lock);
			break;
		}

	}
	dprintk(KERN_DEBUG "%s: nv_nic_irq completed\n", dev->name);

	return IRQ_RETVAL(i);
}

static void nv_do_nic_poll(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);

	disable_irq(dev->irq);
	/* FIXME: Do we need synchronize_irq(dev->irq) here? */
	/*
	 * reenable interrupts on the nic, we have to do this before calling
	 * nv_nic_irq because that may decide to do otherwise
	 */
	writel(np->irqmask, base + NvRegIrqMask);
	pci_push(base);
	nv_nic_irq((int) 0, (void *) data, (struct pt_regs *) NULL);
	enable_irq(dev->irq);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void nv_poll_controller(struct net_device *dev)
{
	nv_do_nic_poll((unsigned long) dev);
}
#endif

static void nv_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct fe_priv *np = get_nvpriv(dev);
	strcpy(info->driver, "forcedeth");
	strcpy(info->version, FORCEDETH_VERSION);
	strcpy(info->bus_info, pci_name(np->pci_dev));
}

static void nv_get_wol(struct net_device *dev, struct ethtool_wolinfo *wolinfo)
{
	struct fe_priv *np = get_nvpriv(dev);
	wolinfo->supported = WAKE_MAGIC;

	spin_lock_irq(&np->lock);
	if (np->wolenabled)
		wolinfo->wolopts = WAKE_MAGIC;
	spin_unlock_irq(&np->lock);
}

static int nv_set_wol(struct net_device *dev, struct ethtool_wolinfo *wolinfo)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);

	spin_lock_irq(&np->lock);
	if (wolinfo->wolopts == 0) {
		writel(0, base + NvRegWakeUpFlags);
		np->wolenabled = 0;
	}
	if (wolinfo->wolopts & WAKE_MAGIC) {
		writel(NVREG_WAKEUPFLAGS_ENABLE, base + NvRegWakeUpFlags);
		np->wolenabled = 1;
	}
	spin_unlock_irq(&np->lock);
	return 0;
}

static int nv_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct fe_priv *np = netdev_priv(dev);
	int adv;

	spin_lock_irq(&np->lock);
	ecmd->port = PORT_MII;
	if (!netif_running(dev)) {
		/* We do not track link speed / duplex setting if the
		 * interface is disabled. Force a link check */
		nv_update_linkspeed(dev);
	}
	switch(np->linkspeed & (NVREG_LINKSPEED_MASK)) {
		case NVREG_LINKSPEED_10:
			ecmd->speed = SPEED_10;
			break;
		case NVREG_LINKSPEED_100:
			ecmd->speed = SPEED_100;
			break;
		case NVREG_LINKSPEED_1000:
			ecmd->speed = SPEED_1000;
			break;
	}
	ecmd->duplex = DUPLEX_HALF;
	if (np->duplex)
		ecmd->duplex = DUPLEX_FULL;

	ecmd->autoneg = np->autoneg;

	ecmd->advertising = ADVERTISED_MII;
	if (np->autoneg) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		adv = mii_rw(dev, np->phyaddr, MII_ADVERTISE, MII_READ);
	} else {
		adv = np->fixed_mode;
	}
	if (adv & ADVERTISE_10HALF)
		ecmd->advertising |= ADVERTISED_10baseT_Half;
	if (adv & ADVERTISE_10FULL)
		ecmd->advertising |= ADVERTISED_10baseT_Full;
	if (adv & ADVERTISE_100HALF)
		ecmd->advertising |= ADVERTISED_100baseT_Half;
	if (adv & ADVERTISE_100FULL)
		ecmd->advertising |= ADVERTISED_100baseT_Full;
	if (np->autoneg && np->gigabit == PHY_GIGABIT) {
		adv = mii_rw(dev, np->phyaddr, MII_1000BT_CR, MII_READ);
		if (adv & ADVERTISE_1000FULL)
			ecmd->advertising |= ADVERTISED_1000baseT_Full;
	}

	ecmd->supported = (SUPPORTED_Autoneg |
		SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
		SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
		SUPPORTED_MII);
	if (np->gigabit == PHY_GIGABIT)
		ecmd->supported |= SUPPORTED_1000baseT_Full;

	ecmd->phy_address = np->phyaddr;
	ecmd->transceiver = XCVR_EXTERNAL;

	/* ignore maxtxpkt, maxrxpkt for now */
	spin_unlock_irq(&np->lock);
	return 0;
}

static int nv_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct fe_priv *np = netdev_priv(dev);

	if (ecmd->port != PORT_MII)
		return -EINVAL;
	if (ecmd->transceiver != XCVR_EXTERNAL)
		return -EINVAL;
	if (ecmd->phy_address != np->phyaddr) {
		/* TODO: support switching between multiple phys. Should be
		 * trivial, but not enabled due to lack of test hardware. */
		return -EINVAL;
	}
	if (ecmd->autoneg == AUTONEG_ENABLE) {
		u32 mask;

		mask = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
			  ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full;
		if (np->gigabit == PHY_GIGABIT)
			mask |= ADVERTISED_1000baseT_Full;

		if ((ecmd->advertising & mask) == 0)
			return -EINVAL;

	} else if (ecmd->autoneg == AUTONEG_DISABLE) {
		/* Note: autonegotiation disable, speed 1000 intentionally
		 * forbidden - noone should need that. */

		if (ecmd->speed != SPEED_10 && ecmd->speed != SPEED_100)
			return -EINVAL;
		if (ecmd->duplex != DUPLEX_HALF && ecmd->duplex != DUPLEX_FULL)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	spin_lock_irq(&np->lock);
	if (ecmd->autoneg == AUTONEG_ENABLE) {
		int adv, bmcr;

		np->autoneg = 1;

		/* advertise only what has been requested */
		adv = mii_rw(dev, np->phyaddr, MII_ADVERTISE, MII_READ);
		adv &= ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
		if (ecmd->advertising & ADVERTISED_10baseT_Half)
			adv |= ADVERTISE_10HALF;
		if (ecmd->advertising & ADVERTISED_10baseT_Full)
			adv |= ADVERTISE_10FULL;
		if (ecmd->advertising & ADVERTISED_100baseT_Half)
			adv |= ADVERTISE_100HALF;
		if (ecmd->advertising & ADVERTISED_100baseT_Full)
			adv |= ADVERTISE_100FULL;
		mii_rw(dev, np->phyaddr, MII_ADVERTISE, adv);

		if (np->gigabit == PHY_GIGABIT) {
			adv = mii_rw(dev, np->phyaddr, MII_1000BT_CR, MII_READ);
			adv &= ~ADVERTISE_1000FULL;
			if (ecmd->advertising & ADVERTISED_1000baseT_Full)
				adv |= ADVERTISE_1000FULL;
			mii_rw(dev, np->phyaddr, MII_1000BT_CR, adv);
		}

		bmcr = mii_rw(dev, np->phyaddr, MII_BMCR, MII_READ);
		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
		mii_rw(dev, np->phyaddr, MII_BMCR, bmcr);

	} else {
		int adv, bmcr;

		np->autoneg = 0;

		adv = mii_rw(dev, np->phyaddr, MII_ADVERTISE, MII_READ);
		adv &= ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
		if (ecmd->speed == SPEED_10 && ecmd->duplex == DUPLEX_HALF)
			adv |= ADVERTISE_10HALF;
		if (ecmd->speed == SPEED_10 && ecmd->duplex == DUPLEX_FULL)
			adv |= ADVERTISE_10FULL;
		if (ecmd->speed == SPEED_100 && ecmd->duplex == DUPLEX_HALF)
			adv |= ADVERTISE_100HALF;
		if (ecmd->speed == SPEED_100 && ecmd->duplex == DUPLEX_FULL)
			adv |= ADVERTISE_100FULL;
		mii_rw(dev, np->phyaddr, MII_ADVERTISE, adv);
		np->fixed_mode = adv;

		if (np->gigabit == PHY_GIGABIT) {
			adv = mii_rw(dev, np->phyaddr, MII_1000BT_CR, MII_READ);
			adv &= ~ADVERTISE_1000FULL;
			mii_rw(dev, np->phyaddr, MII_1000BT_CR, adv);
		}

		bmcr = mii_rw(dev, np->phyaddr, MII_BMCR, MII_READ);
		bmcr |= ~(BMCR_ANENABLE|BMCR_SPEED100|BMCR_FULLDPLX);
		if (adv & (ADVERTISE_10FULL|ADVERTISE_100FULL))
			bmcr |= BMCR_FULLDPLX;
		if (adv & (ADVERTISE_100HALF|ADVERTISE_100FULL))
			bmcr |= BMCR_SPEED100;
		mii_rw(dev, np->phyaddr, MII_BMCR, bmcr);

		if (netif_running(dev)) {
			/* Wait a bit and then reconfigure the nic. */
			udelay(10);
			nv_linkchange(dev);
		}
	}
	spin_unlock_irq(&np->lock);

	return 0;
}

#define FORCEDETH_REGS_VER	1
#define FORCEDETH_REGS_SIZE	0x400 /* 256 32-bit registers */

static int nv_get_regs_len(struct net_device *dev)
{
	return FORCEDETH_REGS_SIZE;
}

static void nv_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *buf)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);
	u32 *rbuf = buf;
	int i;

	regs->version = FORCEDETH_REGS_VER;
	spin_lock_irq(&np->lock);
	for (i=0;i<FORCEDETH_REGS_SIZE/sizeof(u32);i++)
		rbuf[i] = readl(base + i*sizeof(u32));
	spin_unlock_irq(&np->lock);
}

static int nv_nway_reset(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	int ret;

	spin_lock_irq(&np->lock);
	if (np->autoneg) {
		int bmcr;

		bmcr = mii_rw(dev, np->phyaddr, MII_BMCR, MII_READ);
		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
		mii_rw(dev, np->phyaddr, MII_BMCR, bmcr);

		ret = 0;
	} else {
		ret = -EINVAL;
	}
	spin_unlock_irq(&np->lock);

	return ret;
}

static struct ethtool_ops ops = {
	.get_drvinfo = nv_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_wol = nv_get_wol,
	.set_wol = nv_set_wol,
	.get_settings = nv_get_settings,
	.set_settings = nv_set_settings,
	.get_regs_len = nv_get_regs_len,
	.get_regs = nv_get_regs,
	.nway_reset = nv_nway_reset,
};

static int nv_open(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base = get_hwbase(dev);
	int ret, oom, i;

	dprintk(KERN_DEBUG "nv_open: begin\n");

	/* 1) erase previous misconfiguration */
	/* 4.1-1: stop adapter: ignored, 4.3 seems to be overkill */
	writel(NVREG_MCASTADDRA_FORCE, base + NvRegMulticastAddrA);
	writel(0, base + NvRegMulticastAddrB);
	writel(0, base + NvRegMulticastMaskA);
	writel(0, base + NvRegMulticastMaskB);
	writel(0, base + NvRegPacketFilterFlags);

	writel(0, base + NvRegTransmitterControl);
	writel(0, base + NvRegReceiverControl);

	writel(0, base + NvRegAdapterControl);

	/* 2) initialize descriptor rings */
	set_bufsize(dev);
	oom = nv_init_ring(dev);

	writel(0, base + NvRegLinkSpeed);
	writel(0, base + NvRegUnknownTransmitterReg);
	nv_txrx_reset(dev);
	writel(0, base + NvRegUnknownSetupReg6);

	np->in_shutdown = 0;

	/* 3) set mac address */
	nv_copy_mac_to_hw(dev);

	/* 4) give hw rings */
	writel((u32) np->ring_addr, base + NvRegRxRingPhysAddr);
	if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
		writel((u32) (np->ring_addr + RX_RING*sizeof(struct ring_desc)), base + NvRegTxRingPhysAddr);
	else
		writel((u32) (np->ring_addr + RX_RING*sizeof(struct ring_desc_ex)), base + NvRegTxRingPhysAddr);
	writel( ((RX_RING-1) << NVREG_RINGSZ_RXSHIFT) + ((TX_RING-1) << NVREG_RINGSZ_TXSHIFT),
		base + NvRegRingSizes);

	/* 5) continue setup */
	writel(np->linkspeed, base + NvRegLinkSpeed);
	writel(NVREG_UNKSETUP3_VAL1, base + NvRegUnknownSetupReg3);
	writel(np->desc_ver, base + NvRegTxRxControl);
	pci_push(base);
	writel(NVREG_TXRXCTL_BIT1|np->desc_ver, base + NvRegTxRxControl);
	reg_delay(dev, NvRegUnknownSetupReg5, NVREG_UNKSETUP5_BIT31, NVREG_UNKSETUP5_BIT31,
			NV_SETUP5_DELAY, NV_SETUP5_DELAYMAX,
			KERN_INFO "open: SetupReg5, Bit 31 remained off\n");

	writel(0, base + NvRegUnknownSetupReg4);
	writel(NVREG_IRQSTAT_MASK, base + NvRegIrqStatus);
	writel(NVREG_MIISTAT_MASK2, base + NvRegMIIStatus);

	/* 6) continue setup */
	writel(NVREG_MISC1_FORCE | NVREG_MISC1_HD, base + NvRegMisc1);
	writel(readl(base + NvRegTransmitterStatus), base + NvRegTransmitterStatus);
	writel(NVREG_PFF_ALWAYS, base + NvRegPacketFilterFlags);
	writel(np->rx_buf_sz, base + NvRegOffloadConfig);

	writel(readl(base + NvRegReceiverStatus), base + NvRegReceiverStatus);
	get_random_bytes(&i, sizeof(i));
	writel(NVREG_RNDSEED_FORCE | (i&NVREG_RNDSEED_MASK), base + NvRegRandomSeed);
	writel(NVREG_UNKSETUP1_VAL, base + NvRegUnknownSetupReg1);
	writel(NVREG_UNKSETUP2_VAL, base + NvRegUnknownSetupReg2);
	writel(NVREG_POLL_DEFAULT, base + NvRegPollingInterval);
	writel(NVREG_UNKSETUP6_VAL, base + NvRegUnknownSetupReg6);
	writel((np->phyaddr << NVREG_ADAPTCTL_PHYSHIFT)|NVREG_ADAPTCTL_PHYVALID|NVREG_ADAPTCTL_RUNNING,
			base + NvRegAdapterControl);
	writel(NVREG_MIISPEED_BIT8|NVREG_MIIDELAY, base + NvRegMIISpeed);
	writel(NVREG_UNKSETUP4_VAL, base + NvRegUnknownSetupReg4);
	writel(NVREG_WAKEUPFLAGS_VAL, base + NvRegWakeUpFlags);

	i = readl(base + NvRegPowerState);
	if ( (i & NVREG_POWERSTATE_POWEREDUP) == 0)
		writel(NVREG_POWERSTATE_POWEREDUP|i, base + NvRegPowerState);

	pci_push(base);
	udelay(10);
	writel(readl(base + NvRegPowerState) | NVREG_POWERSTATE_VALID, base + NvRegPowerState);

	writel(0, base + NvRegIrqMask);
	pci_push(base);
	writel(NVREG_MIISTAT_MASK2, base + NvRegMIIStatus);
	writel(NVREG_IRQSTAT_MASK, base + NvRegIrqStatus);
	pci_push(base);

	ret = request_irq(dev->irq, &nv_nic_irq, SA_SHIRQ, dev->name, dev);
	if (ret)
		goto out_drain;

	/* ask for interrupts */
	writel(np->irqmask, base + NvRegIrqMask);

	spin_lock_irq(&np->lock);
	writel(NVREG_MCASTADDRA_FORCE, base + NvRegMulticastAddrA);
	writel(0, base + NvRegMulticastAddrB);
	writel(0, base + NvRegMulticastMaskA);
	writel(0, base + NvRegMulticastMaskB);
	writel(NVREG_PFF_ALWAYS|NVREG_PFF_MYADDR, base + NvRegPacketFilterFlags);
	/* One manual link speed update: Interrupts are enabled, future link
	 * speed changes cause interrupts and are handled by nv_link_irq().
	 */
	{
		u32 miistat;
		miistat = readl(base + NvRegMIIStatus);
		writel(NVREG_MIISTAT_MASK, base + NvRegMIIStatus);
		dprintk(KERN_INFO "startup: got 0x%08x.\n", miistat);
	}
	/* set linkspeed to invalid value, thus force nv_update_linkspeed
	 * to init hw */
	np->linkspeed = 0;
	ret = nv_update_linkspeed(dev);
	nv_start_rx(dev);
	nv_start_tx(dev);
	netif_start_queue(dev);
	if (ret) {
		netif_carrier_on(dev);
	} else {
		printk("%s: no link during initialization.\n", dev->name);
		netif_carrier_off(dev);
	}
	if (oom)
		mod_timer(&np->oom_kick, jiffies + OOM_REFILL);
	spin_unlock_irq(&np->lock);

	return 0;
out_drain:
	drain_ring(dev);
	return ret;
}

static int nv_close(struct net_device *dev)
{
	struct fe_priv *np = get_nvpriv(dev);
	u8 __iomem *base;

	spin_lock_irq(&np->lock);
	np->in_shutdown = 1;
	spin_unlock_irq(&np->lock);
	synchronize_irq(dev->irq);

	del_timer_sync(&np->oom_kick);
	del_timer_sync(&np->nic_poll);

	netif_stop_queue(dev);
	spin_lock_irq(&np->lock);
	nv_stop_tx(dev);
	nv_stop_rx(dev);
	nv_txrx_reset(dev);

	/* disable interrupts on the nic or we will lock up */
	base = get_hwbase(dev);
	writel(0, base + NvRegIrqMask);
	pci_push(base);
	dprintk(KERN_INFO "%s: Irqmask is zero again\n", dev->name);

	spin_unlock_irq(&np->lock);

	free_irq(dev->irq, dev);

	drain_ring(dev);

	if (np->wolenabled)
		nv_start_rx(dev);

	/* special op: write back the misordered MAC address - otherwise
	 * the next nv_probe would see a wrong address.
	 */
	writel(np->orig_mac[0], base + NvRegMacAddrA);
	writel(np->orig_mac[1], base + NvRegMacAddrB);

	/* FIXME: power down nic */

	return 0;
}

static int __devinit nv_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	struct net_device *dev;
	struct fe_priv *np;
	unsigned long addr;
	u8 __iomem *base;
	int err, i;

	dev = alloc_etherdev(sizeof(struct fe_priv));
	err = -ENOMEM;
	if (!dev)
		goto out;

	np = get_nvpriv(dev);
	np->pci_dev = pci_dev;
	spin_lock_init(&np->lock);
	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pci_dev->dev);

	init_timer(&np->oom_kick);
	np->oom_kick.data = (unsigned long) dev;
	np->oom_kick.function = &nv_do_rx_refill;	/* timer handler */
	init_timer(&np->nic_poll);
	np->nic_poll.data = (unsigned long) dev;
	np->nic_poll.function = &nv_do_nic_poll;	/* timer handler */

	err = pci_enable_device(pci_dev);
	if (err) {
		printk(KERN_INFO "forcedeth: pci_enable_dev failed (%d) for device %s\n",
				err, pci_name(pci_dev));
		goto out_free;
	}

	pci_set_master(pci_dev);

	err = pci_request_regions(pci_dev, DRV_NAME);
	if (err < 0)
		goto out_disable;

	err = -EINVAL;
	addr = 0;
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		dprintk(KERN_DEBUG "%s: resource %d start %p len %ld flags 0x%08lx.\n",
				pci_name(pci_dev), i, (void*)pci_resource_start(pci_dev, i),
				pci_resource_len(pci_dev, i),
				pci_resource_flags(pci_dev, i));
		if (pci_resource_flags(pci_dev, i) & IORESOURCE_MEM &&
				pci_resource_len(pci_dev, i) >= NV_PCI_REGSZ) {
			addr = pci_resource_start(pci_dev, i);
			break;
		}
	}
	if (i == DEVICE_COUNT_RESOURCE) {
		printk(KERN_INFO "forcedeth: Couldn't find register window for device %s.\n",
					pci_name(pci_dev));
		goto out_relreg;
	}

	/* handle different descriptor versions */
	if (id->driver_data & DEV_HAS_HIGH_DMA) {
		/* packet format 3: supports 40-bit addressing */
		np->desc_ver = DESC_VER_3;
		if (pci_set_dma_mask(pci_dev, 0x0000007fffffffffULL)) {
			printk(KERN_INFO "forcedeth: 64-bit DMA failed, using 32-bit addressing for device %s.\n",
					pci_name(pci_dev));
		}
	} else if (id->driver_data & DEV_HAS_LARGEDESC) {
		/* packet format 2: supports jumbo frames */
		np->desc_ver = DESC_VER_2;
	} else {
		/* original packet format */
		np->desc_ver = DESC_VER_1;
	}

	np->pkt_limit = NV_PKTLIMIT_1;
	if (id->driver_data & DEV_HAS_LARGEDESC)
		np->pkt_limit = NV_PKTLIMIT_2;

	err = -ENOMEM;
	np->base = ioremap(addr, NV_PCI_REGSZ);
	if (!np->base)
		goto out_relreg;
	dev->base_addr = (unsigned long)np->base;

	dev->irq = pci_dev->irq;

	if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2) {
		np->rx_ring.orig = pci_alloc_consistent(pci_dev,
					sizeof(struct ring_desc) * (RX_RING + TX_RING),
					&np->ring_addr);
		if (!np->rx_ring.orig)
			goto out_unmap;
		np->tx_ring.orig = &np->rx_ring.orig[RX_RING];
	} else {
		np->rx_ring.ex = pci_alloc_consistent(pci_dev,
					sizeof(struct ring_desc_ex) * (RX_RING + TX_RING),
					&np->ring_addr);
		if (!np->rx_ring.ex)
			goto out_unmap;
		np->tx_ring.ex = &np->rx_ring.ex[RX_RING];
	}

	dev->open = nv_open;
	dev->stop = nv_close;
	dev->hard_start_xmit = nv_start_xmit;
	dev->get_stats = nv_get_stats;
	dev->change_mtu = nv_change_mtu;
	dev->set_mac_address = nv_set_mac_address;
	dev->set_multicast_list = nv_set_multicast;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = nv_poll_controller;
#endif
	SET_ETHTOOL_OPS(dev, &ops);
	dev->tx_timeout = nv_tx_timeout;
	dev->watchdog_timeo = NV_WATCHDOG_TIMEO;

	pci_set_drvdata(pci_dev, dev);

	/* read the mac address */
	base = get_hwbase(dev);
	np->orig_mac[0] = readl(base + NvRegMacAddrA);
	np->orig_mac[1] = readl(base + NvRegMacAddrB);

	dev->dev_addr[0] = (np->orig_mac[1] >>  8) & 0xff;
	dev->dev_addr[1] = (np->orig_mac[1] >>  0) & 0xff;
	dev->dev_addr[2] = (np->orig_mac[0] >> 24) & 0xff;
	dev->dev_addr[3] = (np->orig_mac[0] >> 16) & 0xff;
	dev->dev_addr[4] = (np->orig_mac[0] >>  8) & 0xff;
	dev->dev_addr[5] = (np->orig_mac[0] >>  0) & 0xff;

	if (!is_valid_ether_addr(dev->dev_addr)) {
		/*
		 * Bad mac address. At least one bios sets the mac address
		 * to 01:23:45:67:89:ab
		 */
		printk(KERN_ERR "%s: Invalid Mac address detected: %02x:%02x:%02x:%02x:%02x:%02x\n",
			pci_name(pci_dev),
			dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
			dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
		printk(KERN_ERR "Please complain to your hardware vendor. Switching to a random MAC.\n");
		dev->dev_addr[0] = 0x00;
		dev->dev_addr[1] = 0x00;
		dev->dev_addr[2] = 0x6c;
		get_random_bytes(&dev->dev_addr[3], 3);
	}

	dprintk(KERN_DEBUG "%s: MAC Address %02x:%02x:%02x:%02x:%02x:%02x\n", pci_name(pci_dev),
			dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
			dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	/* disable WOL */
	writel(0, base + NvRegWakeUpFlags);
	np->wolenabled = 0;

	if (np->desc_ver == DESC_VER_1) {
		np->tx_flags = NV_TX_LASTPACKET|NV_TX_VALID;
	} else {
		np->tx_flags = NV_TX2_LASTPACKET|NV_TX2_VALID;
	}
	np->irqmask = NVREG_IRQMASK_WANTED;
	if (id->driver_data & DEV_NEED_TIMERIRQ)
		np->irqmask |= NVREG_IRQ_TIMER;
	if (id->driver_data & DEV_NEED_LINKTIMER) {
		dprintk(KERN_INFO "%s: link timer on.\n", pci_name(pci_dev));
		np->need_linktimer = 1;
		np->link_timeout = jiffies + LINK_TIMEOUT;
	} else {
		dprintk(KERN_INFO "%s: link timer off.\n", pci_name(pci_dev));
		np->need_linktimer = 0;
	}

	/* find a suitable phy */
	for (i = 1; i < 32; i++) {
		int id1, id2;

		spin_lock_irq(&np->lock);
		id1 = mii_rw(dev, i, MII_PHYSID1, MII_READ);
		spin_unlock_irq(&np->lock);
		if (id1 < 0 || id1 == 0xffff)
			continue;
		spin_lock_irq(&np->lock);
		id2 = mii_rw(dev, i, MII_PHYSID2, MII_READ);
		spin_unlock_irq(&np->lock);
		if (id2 < 0 || id2 == 0xffff)
			continue;

		id1 = (id1 & PHYID1_OUI_MASK) << PHYID1_OUI_SHFT;
		id2 = (id2 & PHYID2_OUI_MASK) >> PHYID2_OUI_SHFT;
		dprintk(KERN_DEBUG "%s: open: Found PHY %04x:%04x at address %d.\n",
				pci_name(pci_dev), id1, id2, i);
		np->phyaddr = i;
		np->phy_oui = id1 | id2;
		break;
	}
	if (i == 32) {
		/* PHY in isolate mode? No phy attached and user wants to
		 * test loopback? Very odd, but can be correct.
		 */
		printk(KERN_INFO "%s: open: Could not find a valid PHY.\n",
				pci_name(pci_dev));
	}

	if (i != 32) {
		/* reset it */
		phy_init(dev);
	}

	/* set default link speed settings */
	np->linkspeed = NVREG_LINKSPEED_FORCE|NVREG_LINKSPEED_10;
	np->duplex = 0;
	np->autoneg = 1;

	err = register_netdev(dev);
	if (err) {
		printk(KERN_INFO "forcedeth: unable to register netdev: %d\n", err);
		goto out_freering;
	}
	printk(KERN_INFO "%s: forcedeth.c: subsystem: %05x:%04x bound to %s\n",
			dev->name, pci_dev->subsystem_vendor, pci_dev->subsystem_device,
			pci_name(pci_dev));

	return 0;

out_freering:
	if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
		pci_free_consistent(np->pci_dev, sizeof(struct ring_desc) * (RX_RING + TX_RING),
				    np->rx_ring.orig, np->ring_addr);
	else
		pci_free_consistent(np->pci_dev, sizeof(struct ring_desc_ex) * (RX_RING + TX_RING),
				    np->rx_ring.ex, np->ring_addr);
	pci_set_drvdata(pci_dev, NULL);
out_unmap:
	iounmap(get_hwbase(dev));
out_relreg:
	pci_release_regions(pci_dev);
out_disable:
	pci_disable_device(pci_dev);
out_free:
	free_netdev(dev);
out:
	return err;
}

static void __devexit nv_remove(struct pci_dev *pci_dev)
{
	struct net_device *dev = pci_get_drvdata(pci_dev);
	struct fe_priv *np = get_nvpriv(dev);

	unregister_netdev(dev);

	/* free all structures */
	if (np->desc_ver == DESC_VER_1 || np->desc_ver == DESC_VER_2)
		pci_free_consistent(np->pci_dev, sizeof(struct ring_desc) * (RX_RING + TX_RING), np->rx_ring.orig, np->ring_addr);
	else
		pci_free_consistent(np->pci_dev, sizeof(struct ring_desc_ex) * (RX_RING + TX_RING), np->rx_ring.ex, np->ring_addr);
	iounmap(get_hwbase(dev));
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
	free_netdev(dev);
	pci_set_drvdata(pci_dev, NULL);
}

static struct pci_device_id pci_tbl[] = {
	{	/* nForce Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_1),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER,
	},
	{	/* nForce2 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_2),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER,
	},
	{	/* nForce3 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_3),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER,
	},
	{	/* nForce3 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_4),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC,
	},
	{	/* nForce3 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_5),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC,
	},
	{	/* nForce3 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_6),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC,
	},
	{	/* nForce3 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_7),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC,
	},
	{	/* CK804 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_8),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC|DEV_HAS_HIGH_DMA,
	},
	{	/* CK804 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_9),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC|DEV_HAS_HIGH_DMA,
	},
	{	/* MCP04 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_10),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC|DEV_HAS_HIGH_DMA,
	},
	{	/* MCP04 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_11),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC|DEV_HAS_HIGH_DMA,
	},
	{	/* MCP51 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_12),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_HIGH_DMA,
	},
	{	/* MCP51 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_13),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_HIGH_DMA,
	},
	{	/* MCP55 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_14),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC|DEV_HAS_HIGH_DMA,
	},
	{	/* MCP55 Ethernet Controller */
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NVENET_15),
		.driver_data = DEV_NEED_TIMERIRQ|DEV_NEED_LINKTIMER|DEV_HAS_LARGEDESC|DEV_HAS_HIGH_DMA,
	},
	{0,},
};

static struct pci_driver driver = {
	.name = "forcedeth",
	.id_table = pci_tbl,
	.probe = nv_probe,
	.remove = __devexit_p(nv_remove),
};


static int __init init_nic(void)
{
	printk(KERN_INFO "forcedeth.c: Reverse Engineered nForce ethernet driver. Version %s.\n", FORCEDETH_VERSION);
	return pci_module_init(&driver);
}

static void __exit exit_nic(void)
{
	pci_unregister_driver(&driver);
}

module_param(max_interrupt_work, int, 0);
MODULE_PARM_DESC(max_interrupt_work, "forcedeth maximum events handled per interrupt");

MODULE_AUTHOR("Manfred Spraul <manfred@colorfullife.com>");
MODULE_DESCRIPTION("Reverse Engineered nForce ethernet driver");
MODULE_LICENSE("GPL");

MODULE_DEVICE_TABLE(pci, pci_tbl);

module_init(init_nic);
module_exit(exit_nic);
