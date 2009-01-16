/*  Silan SC92031 PCI Fast Ethernet Adapter driver
 *
 *  Based on vendor drivers:
 *  Silan Fast Ethernet Netcard Driver:
 *    MODULE_AUTHOR ("gaoyonghong");
 *    MODULE_DESCRIPTION ("SILAN Fast Ethernet driver");
 *    MODULE_LICENSE("GPL");
 *  8139D Fast Ethernet driver:
 *    (C) 2002 by gaoyonghong
 *    MODULE_AUTHOR ("gaoyonghong");
 *    MODULE_DESCRIPTION ("Rsltek 8139D PCI Fast Ethernet Adapter driver");
 *    MODULE_LICENSE("GPL");
 *  Both are almost identical and seem to be based on pci-skeleton.c
 *
 *  Rewritten for 2.6 by Cesar Eduardo Barros
 */

/* Note about set_mac_address: I don't know how to change the hardware
 * matching, so you need to enable IFF_PROMISC when using it.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>

#include <asm/irq.h>

#define PCI_VENDOR_ID_SILAN		0x1904
#define PCI_DEVICE_ID_SILAN_SC92031	0x2031
#define PCI_DEVICE_ID_SILAN_8139D	0x8139

#define SC92031_NAME "sc92031"
#define SC92031_DESCRIPTION "Silan SC92031 PCI Fast Ethernet Adapter driver"
#define SC92031_VERSION "2.0c"

/* BAR 0 is MMIO, BAR 1 is PIO */
#ifndef SC92031_USE_BAR
#define SC92031_USE_BAR 0
#endif

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast). */
static int multicast_filter_limit = 64;
module_param(multicast_filter_limit, int, 0);
MODULE_PARM_DESC(multicast_filter_limit,
	"Maximum number of filtered multicast addresses");

static int media;
module_param(media, int, 0);
MODULE_PARM_DESC(media, "Media type (0x00 = autodetect,"
	" 0x01 = 10M half, 0x02 = 10M full,"
	" 0x04 = 100M half, 0x08 = 100M full)");

/* Size of the in-memory receive ring. */
#define  RX_BUF_LEN_IDX  3 /* 0==8K, 1==16K, 2==32K, 3==64K ,4==128K*/
#define  RX_BUF_LEN	(8192 << RX_BUF_LEN_IDX)

/* Number of Tx descriptor registers. */
#define  NUM_TX_DESC	   4

/* max supported ethernet frame size -- must be at least (dev->mtu+14+4).*/
#define  MAX_ETH_FRAME_SIZE	  1536

/* Size of the Tx bounce buffers -- must be at least (dev->mtu+14+4). */
#define  TX_BUF_SIZE       MAX_ETH_FRAME_SIZE
#define  TX_BUF_TOT_LEN    (TX_BUF_SIZE * NUM_TX_DESC)

/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024, 7==end of packet. */
#define  RX_FIFO_THRESH    7     /* Rx buffer level before first PCI xfer.  */

/* Time in jiffies before concluding the transmitter is hung. */
#define  TX_TIMEOUT     (4*HZ)

#define  SILAN_STATS_NUM    2    /* number of ETHTOOL_GSTATS */

/* media options */
#define  AUTOSELECT    0x00
#define  M10_HALF      0x01
#define  M10_FULL      0x02
#define  M100_HALF     0x04
#define  M100_FULL     0x08

 /* Symbolic offsets to registers. */
enum  silan_registers {
   Config0    = 0x00,         // Config0
   Config1    = 0x04,         // Config1
   RxBufWPtr  = 0x08,         // Rx buffer writer poiter
   IntrStatus = 0x0C,         // Interrupt status
   IntrMask   = 0x10,         // Interrupt mask
   RxbufAddr  = 0x14,         // Rx buffer start address
   RxBufRPtr  = 0x18,         // Rx buffer read pointer
   Txstatusall = 0x1C,        // Transmit status of all descriptors
   TxStatus0  = 0x20,	      // Transmit status (Four 32bit registers).
   TxAddr0    = 0x30,         // Tx descriptors (also four 32bit).
   RxConfig   = 0x40,         // Rx configuration
   MAC0	      = 0x44,	      // Ethernet hardware address.
   MAR0	      = 0x4C,	      // Multicast filter.
   RxStatus0  = 0x54,         // Rx status
   TxConfig   = 0x5C,         // Tx configuration
   PhyCtrl    = 0x60,         // physical control
   FlowCtrlConfig = 0x64,     // flow control
   Miicmd0    = 0x68,         // Mii command0 register
   Miicmd1    = 0x6C,         // Mii command1 register
   Miistatus  = 0x70,         // Mii status register
   Timercnt   = 0x74,         // Timer counter register
   TimerIntr  = 0x78,         // Timer interrupt register
   PMConfig   = 0x7C,         // Power Manager configuration
   CRC0       = 0x80,         // Power Manager CRC ( Two 32bit regisers)
   Wakeup0    = 0x88,         // power Manager wakeup( Eight 64bit regiser)
   LSBCRC0    = 0xC8,         // power Manager LSBCRC(Two 32bit regiser)
   TestD0     = 0xD0,
   TestD4     = 0xD4,
   TestD8     = 0xD8,
};

#define MII_BMCR            0        // Basic mode control register
#define MII_BMSR            1        // Basic mode status register
#define MII_JAB             16
#define MII_OutputStatus    24

#define BMCR_FULLDPLX       0x0100    // Full duplex
#define BMCR_ANRESTART      0x0200    // Auto negotiation restart
#define BMCR_ANENABLE       0x1000    // Enable auto negotiation
#define BMCR_SPEED100       0x2000    // Select 100Mbps
#define BMSR_LSTATUS        0x0004    // Link status
#define PHY_16_JAB_ENB      0x1000
#define PHY_16_PORT_ENB     0x1

enum IntrStatusBits {
   LinkFail       = 0x80000000,
   LinkOK         = 0x40000000,
   TimeOut        = 0x20000000,
   RxOverflow     = 0x0040,
   RxOK           = 0x0020,
   TxOK           = 0x0001,
   IntrBits = LinkFail|LinkOK|TimeOut|RxOverflow|RxOK|TxOK,
};

enum TxStatusBits {
   TxCarrierLost = 0x20000000,
   TxAborted     = 0x10000000,
   TxOutOfWindow = 0x08000000,
   TxNccShift    = 22,
   EarlyTxThresShift = 16,
   TxStatOK      = 0x8000,
   TxUnderrun    = 0x4000,
   TxOwn         = 0x2000,
};

enum RxStatusBits {
   RxStatesOK   = 0x80000,
   RxBadAlign   = 0x40000,
   RxHugeFrame  = 0x20000,
   RxSmallFrame = 0x10000,
   RxCRCOK      = 0x8000,
   RxCrlFrame   = 0x4000,
   Rx_Broadcast = 0x2000,
   Rx_Multicast = 0x1000,
   RxAddrMatch  = 0x0800,
   MiiErr       = 0x0400,
};

enum RxConfigBits {
   RxFullDx    = 0x80000000,
   RxEnb       = 0x40000000,
   RxSmall     = 0x20000000,
   RxHuge      = 0x10000000,
   RxErr       = 0x08000000,
   RxAllphys   = 0x04000000,
   RxMulticast = 0x02000000,
   RxBroadcast = 0x01000000,
   RxLoopBack  = (1 << 23) | (1 << 22),
   LowThresholdShift  = 12,
   HighThresholdShift = 2,
};

enum TxConfigBits {
   TxFullDx       = 0x80000000,
   TxEnb          = 0x40000000,
   TxEnbPad       = 0x20000000,
   TxEnbHuge      = 0x10000000,
   TxEnbFCS       = 0x08000000,
   TxNoBackOff    = 0x04000000,
   TxEnbPrem      = 0x02000000,
   TxCareLostCrs  = 0x1000000,
   TxExdCollNum   = 0xf00000,
   TxDataRate     = 0x80000,
};

enum PhyCtrlconfigbits {
   PhyCtrlAne         = 0x80000000,
   PhyCtrlSpd100      = 0x40000000,
   PhyCtrlSpd10       = 0x20000000,
   PhyCtrlPhyBaseAddr = 0x1f000000,
   PhyCtrlDux         = 0x800000,
   PhyCtrlReset       = 0x400000,
};

enum FlowCtrlConfigBits {
   FlowCtrlFullDX = 0x80000000,
   FlowCtrlEnb    = 0x40000000,
};

enum Config0Bits {
   Cfg0_Reset  = 0x80000000,
   Cfg0_Anaoff = 0x40000000,
   Cfg0_LDPS   = 0x20000000,
};

enum Config1Bits {
   Cfg1_EarlyRx = 1 << 31,
   Cfg1_EarlyTx = 1 << 30,

   //rx buffer size
   Cfg1_Rcv8K   = 0x0,
   Cfg1_Rcv16K  = 0x1,
   Cfg1_Rcv32K  = 0x3,
   Cfg1_Rcv64K  = 0x7,
   Cfg1_Rcv128K = 0xf,
};

enum MiiCmd0Bits {
   Mii_Divider = 0x20000000,
   Mii_WRITE   = 0x400000,
   Mii_READ    = 0x200000,
   Mii_SCAN    = 0x100000,
   Mii_Tamod   = 0x80000,
   Mii_Drvmod  = 0x40000,
   Mii_mdc     = 0x20000,
   Mii_mdoen   = 0x10000,
   Mii_mdo     = 0x8000,
   Mii_mdi     = 0x4000,
};

enum MiiStatusBits {
    Mii_StatusBusy = 0x80000000,
};

enum PMConfigBits {
   PM_Enable  = 1 << 31,
   PM_LongWF  = 1 << 30,
   PM_Magic   = 1 << 29,
   PM_LANWake = 1 << 28,
   PM_LWPTN   = (1 << 27 | 1<< 26),
   PM_LinkUp  = 1 << 25,
   PM_WakeUp  = 1 << 24,
};

/* Locking rules:
 * priv->lock protects most of the fields of priv and most of the
 * hardware registers. It does not have to protect against softirqs
 * between sc92031_disable_interrupts and sc92031_enable_interrupts;
 * it also does not need to be used in ->open and ->stop while the
 * device interrupts are off.
 * Not having to protect against softirqs is very useful due to heavy
 * use of mdelay() at _sc92031_reset.
 * Functions prefixed with _sc92031_ must be called with the lock held;
 * functions prefixed with sc92031_ must be called without the lock held.
 * Use mmiowb() before unlocking if the hardware was written to.
 */

/* Locking rules for the interrupt:
 * - the interrupt and the tasklet never run at the same time
 * - neither run between sc92031_disable_interrupts and
 *   sc92031_enable_interrupt
 */

struct sc92031_priv {
	spinlock_t		lock;
	/* iomap.h cookie */
	void __iomem		*port_base;
	/* pci device structure */
	struct pci_dev		*pdev;
	/* tasklet */
	struct tasklet_struct	tasklet;

	/* CPU address of rx ring */
	void			*rx_ring;
	/* PCI address of rx ring */
	dma_addr_t		rx_ring_dma_addr;
	/* PCI address of rx ring read pointer */
	dma_addr_t		rx_ring_tail;

	/* tx ring write index */
	unsigned		tx_head;
	/* tx ring read index */
	unsigned		tx_tail;
	/* CPU address of tx bounce buffer */
	void			*tx_bufs;
	/* PCI address of tx bounce buffer */
	dma_addr_t		tx_bufs_dma_addr;

	/* copies of some hardware registers */
	u32			intr_status;
	atomic_t		intr_mask;
	u32			rx_config;
	u32			tx_config;
	u32			pm_config;

	/* copy of some flags from dev->flags */
	unsigned int		mc_flags;

	/* for ETHTOOL_GSTATS */
	u64			tx_timeouts;
	u64			rx_loss;

	/* for dev->get_stats */
	long			rx_value;
};

/* I don't know which registers can be safely read; however, I can guess
 * MAC0 is one of them. */
static inline void _sc92031_dummy_read(void __iomem *port_base)
{
	ioread32(port_base + MAC0);
}

static u32 _sc92031_mii_wait(void __iomem *port_base)
{
	u32 mii_status;

	do {
		udelay(10);
		mii_status = ioread32(port_base + Miistatus);
	} while (mii_status & Mii_StatusBusy);

	return mii_status;
}

static u32 _sc92031_mii_cmd(void __iomem *port_base, u32 cmd0, u32 cmd1)
{
	iowrite32(Mii_Divider, port_base + Miicmd0);

	_sc92031_mii_wait(port_base);

	iowrite32(cmd1, port_base + Miicmd1);
	iowrite32(Mii_Divider | cmd0, port_base + Miicmd0);

	return _sc92031_mii_wait(port_base);
}

static void _sc92031_mii_scan(void __iomem *port_base)
{
	_sc92031_mii_cmd(port_base, Mii_SCAN, 0x1 << 6);
}

static u16 _sc92031_mii_read(void __iomem *port_base, unsigned reg)
{
	return _sc92031_mii_cmd(port_base, Mii_READ, reg << 6) >> 13;
}

static void _sc92031_mii_write(void __iomem *port_base, unsigned reg, u16 val)
{
	_sc92031_mii_cmd(port_base, Mii_WRITE, (reg << 6) | ((u32)val << 11));
}

static void sc92031_disable_interrupts(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	/* tell the tasklet/interrupt not to enable interrupts */
	atomic_set(&priv->intr_mask, 0);
	wmb();

	/* stop interrupts */
	iowrite32(0, port_base + IntrMask);
	_sc92031_dummy_read(port_base);
	mmiowb();

	/* wait for any concurrent interrupt/tasklet to finish */
	synchronize_irq(dev->irq);
	tasklet_disable(&priv->tasklet);
}

static void sc92031_enable_interrupts(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	tasklet_enable(&priv->tasklet);

	atomic_set(&priv->intr_mask, IntrBits);
	wmb();

	iowrite32(IntrBits, port_base + IntrMask);
	mmiowb();
}

static void _sc92031_disable_tx_rx(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	priv->rx_config &= ~RxEnb;
	priv->tx_config &= ~TxEnb;
	iowrite32(priv->rx_config, port_base + RxConfig);
	iowrite32(priv->tx_config, port_base + TxConfig);
}

static void _sc92031_enable_tx_rx(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	priv->rx_config |= RxEnb;
	priv->tx_config |= TxEnb;
	iowrite32(priv->rx_config, port_base + RxConfig);
	iowrite32(priv->tx_config, port_base + TxConfig);
}

static void _sc92031_tx_clear(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);

	while (priv->tx_head - priv->tx_tail > 0) {
		priv->tx_tail++;
		dev->stats.tx_dropped++;
	}
	priv->tx_head = priv->tx_tail = 0;
}

static void _sc92031_set_mar(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u32 mar0 = 0, mar1 = 0;

	if ((dev->flags & IFF_PROMISC)
			|| dev->mc_count > multicast_filter_limit
			|| (dev->flags & IFF_ALLMULTI))
		mar0 = mar1 = 0xffffffff;
	else if (dev->flags & IFF_MULTICAST) {
		struct dev_mc_list *mc_list;

		for (mc_list = dev->mc_list; mc_list; mc_list = mc_list->next) {
			u32 crc;
			unsigned bit = 0;

			crc = ~ether_crc(ETH_ALEN, mc_list->dmi_addr);
			crc >>= 24;

			if (crc & 0x01)	bit |= 0x02;
			if (crc & 0x02)	bit |= 0x01;
			if (crc & 0x10)	bit |= 0x20;
			if (crc & 0x20)	bit |= 0x10;
			if (crc & 0x40)	bit |= 0x08;
			if (crc & 0x80)	bit |= 0x04;

			if (bit > 31)
				mar0 |= 0x1 << (bit - 32);
			else
				mar1 |= 0x1 << bit;
		}
	}

	iowrite32(mar0, port_base + MAR0);
	iowrite32(mar1, port_base + MAR0 + 4);
}

static void _sc92031_set_rx_config(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	unsigned int old_mc_flags;
	u32 rx_config_bits = 0;

	old_mc_flags = priv->mc_flags;

	if (dev->flags & IFF_PROMISC)
		rx_config_bits |= RxSmall | RxHuge | RxErr | RxBroadcast
				| RxMulticast | RxAllphys;

	if (dev->flags & (IFF_ALLMULTI | IFF_MULTICAST))
		rx_config_bits |= RxMulticast;

	if (dev->flags & IFF_BROADCAST)
		rx_config_bits |= RxBroadcast;

	priv->rx_config &= ~(RxSmall | RxHuge | RxErr | RxBroadcast
			| RxMulticast | RxAllphys);
	priv->rx_config |= rx_config_bits;

	priv->mc_flags = dev->flags & (IFF_PROMISC | IFF_ALLMULTI
			| IFF_MULTICAST | IFF_BROADCAST);

	if (netif_carrier_ok(dev) && priv->mc_flags != old_mc_flags)
		iowrite32(priv->rx_config, port_base + RxConfig);
}

static bool _sc92031_check_media(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u16 bmsr;

	bmsr = _sc92031_mii_read(port_base, MII_BMSR);
	rmb();
	if (bmsr & BMSR_LSTATUS) {
		bool speed_100, duplex_full;
		u32 flow_ctrl_config = 0;
		u16 output_status = _sc92031_mii_read(port_base,
				MII_OutputStatus);
		_sc92031_mii_scan(port_base);

		speed_100 = output_status & 0x2;
		duplex_full = output_status & 0x4;

		/* Initial Tx/Rx configuration */
		priv->rx_config = (0x40 << LowThresholdShift) | (0x1c0 << HighThresholdShift);
		priv->tx_config = 0x48800000;

		/* NOTE: vendor driver had dead code here to enable tx padding */

		if (!speed_100)
			priv->tx_config |= 0x80000;

		// configure rx mode
		_sc92031_set_rx_config(dev);

		if (duplex_full) {
			priv->rx_config |= RxFullDx;
			priv->tx_config |= TxFullDx;
			flow_ctrl_config = FlowCtrlFullDX | FlowCtrlEnb;
		} else {
			priv->rx_config &= ~RxFullDx;
			priv->tx_config &= ~TxFullDx;
		}

		_sc92031_set_mar(dev);
		_sc92031_set_rx_config(dev);
		_sc92031_enable_tx_rx(dev);
		iowrite32(flow_ctrl_config, port_base + FlowCtrlConfig);

		netif_carrier_on(dev);

		if (printk_ratelimit())
			printk(KERN_INFO "%s: link up, %sMbps, %s-duplex\n",
				dev->name,
				speed_100 ? "100" : "10",
				duplex_full ? "full" : "half");
		return true;
	} else {
		_sc92031_mii_scan(port_base);

		netif_carrier_off(dev);

		_sc92031_disable_tx_rx(dev);

		if (printk_ratelimit())
			printk(KERN_INFO "%s: link down\n", dev->name);
		return false;
	}
}

static void _sc92031_phy_reset(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u32 phy_ctrl;

	phy_ctrl = ioread32(port_base + PhyCtrl);
	phy_ctrl &= ~(PhyCtrlDux | PhyCtrlSpd100 | PhyCtrlSpd10);
	phy_ctrl |= PhyCtrlAne | PhyCtrlReset;

	switch (media) {
	default:
	case AUTOSELECT:
		phy_ctrl |= PhyCtrlDux | PhyCtrlSpd100 | PhyCtrlSpd10;
		break;
	case M10_HALF:
		phy_ctrl |= PhyCtrlSpd10;
		break;
	case M10_FULL:
		phy_ctrl |= PhyCtrlDux | PhyCtrlSpd10;
		break;
	case M100_HALF:
		phy_ctrl |= PhyCtrlSpd100;
		break;
	case M100_FULL:
		phy_ctrl |= PhyCtrlDux | PhyCtrlSpd100;
		break;
	}

	iowrite32(phy_ctrl, port_base + PhyCtrl);
	mdelay(10);

	phy_ctrl &= ~PhyCtrlReset;
	iowrite32(phy_ctrl, port_base + PhyCtrl);
	mdelay(1);

	_sc92031_mii_write(port_base, MII_JAB,
			PHY_16_JAB_ENB | PHY_16_PORT_ENB);
	_sc92031_mii_scan(port_base);

	netif_carrier_off(dev);
	netif_stop_queue(dev);
}

static void _sc92031_reset(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	/* disable PM */
	iowrite32(0, port_base + PMConfig);

	/* soft reset the chip */
	iowrite32(Cfg0_Reset, port_base + Config0);
	mdelay(200);

	iowrite32(0, port_base + Config0);
	mdelay(10);

	/* disable interrupts */
	iowrite32(0, port_base + IntrMask);

	/* clear multicast address */
	iowrite32(0, port_base + MAR0);
	iowrite32(0, port_base + MAR0 + 4);

	/* init rx ring */
	iowrite32(priv->rx_ring_dma_addr, port_base + RxbufAddr);
	priv->rx_ring_tail = priv->rx_ring_dma_addr;

	/* init tx ring */
	_sc92031_tx_clear(dev);

	/* clear old register values */
	priv->intr_status = 0;
	atomic_set(&priv->intr_mask, 0);
	priv->rx_config = 0;
	priv->tx_config = 0;
	priv->mc_flags = 0;

	/* configure rx buffer size */
	/* NOTE: vendor driver had dead code here to enable early tx/rx */
	iowrite32(Cfg1_Rcv64K, port_base + Config1);

	_sc92031_phy_reset(dev);
	_sc92031_check_media(dev);

	/* calculate rx fifo overflow */
	priv->rx_value = 0;

	/* enable PM */
	iowrite32(priv->pm_config, port_base + PMConfig);

	/* clear intr register */
	ioread32(port_base + IntrStatus);
}

static void _sc92031_tx_tasklet(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	unsigned old_tx_tail;
	unsigned entry;
	u32 tx_status;

	old_tx_tail = priv->tx_tail;
	while (priv->tx_head - priv->tx_tail > 0) {
		entry = priv->tx_tail % NUM_TX_DESC;
		tx_status = ioread32(port_base + TxStatus0 + entry * 4);

		if (!(tx_status & (TxStatOK | TxUnderrun | TxAborted)))
			break;

		priv->tx_tail++;

		if (tx_status & TxStatOK) {
			dev->stats.tx_bytes += tx_status & 0x1fff;
			dev->stats.tx_packets++;
			/* Note: TxCarrierLost is always asserted at 100mbps. */
			dev->stats.collisions += (tx_status >> 22) & 0xf;
		}

		if (tx_status & (TxOutOfWindow | TxAborted)) {
			dev->stats.tx_errors++;

			if (tx_status & TxAborted)
				dev->stats.tx_aborted_errors++;

			if (tx_status & TxCarrierLost)
				dev->stats.tx_carrier_errors++;

			if (tx_status & TxOutOfWindow)
				dev->stats.tx_window_errors++;
		}

		if (tx_status & TxUnderrun)
			dev->stats.tx_fifo_errors++;
	}

	if (priv->tx_tail != old_tx_tail)
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
}

static void _sc92031_rx_tasklet_error(struct net_device *dev,
				      u32 rx_status, unsigned rx_size)
{
	if(rx_size > (MAX_ETH_FRAME_SIZE + 4) || rx_size < 16) {
		dev->stats.rx_errors++;
		dev->stats.rx_length_errors++;
	}

	if (!(rx_status & RxStatesOK)) {
		dev->stats.rx_errors++;

		if (rx_status & (RxHugeFrame | RxSmallFrame))
			dev->stats.rx_length_errors++;

		if (rx_status & RxBadAlign)
			dev->stats.rx_frame_errors++;

		if (!(rx_status & RxCRCOK))
			dev->stats.rx_crc_errors++;
	} else {
		struct sc92031_priv *priv = netdev_priv(dev);
		priv->rx_loss++;
	}
}

static void _sc92031_rx_tasklet(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	dma_addr_t rx_ring_head;
	unsigned rx_len;
	unsigned rx_ring_offset;
	void *rx_ring = priv->rx_ring;

	rx_ring_head = ioread32(port_base + RxBufWPtr);
	rmb();

	/* rx_ring_head is only 17 bits in the RxBufWPtr register.
	 * we need to change it to 32 bits physical address
	 */
	rx_ring_head &= (dma_addr_t)(RX_BUF_LEN - 1);
	rx_ring_head |= priv->rx_ring_dma_addr & ~(dma_addr_t)(RX_BUF_LEN - 1);
	if (rx_ring_head < priv->rx_ring_dma_addr)
		rx_ring_head += RX_BUF_LEN;

	if (rx_ring_head >= priv->rx_ring_tail)
		rx_len = rx_ring_head - priv->rx_ring_tail;
	else
		rx_len = RX_BUF_LEN - (priv->rx_ring_tail - rx_ring_head);

	if (!rx_len)
		return;

	if (unlikely(rx_len > RX_BUF_LEN)) {
		if (printk_ratelimit())
			printk(KERN_ERR "%s: rx packets length > rx buffer\n",
					dev->name);
		return;
	}

	rx_ring_offset = (priv->rx_ring_tail - priv->rx_ring_dma_addr) % RX_BUF_LEN;

	while (rx_len) {
		u32 rx_status;
		unsigned rx_size, rx_size_align, pkt_size;
		struct sk_buff *skb;

		rx_status = le32_to_cpup((__le32 *)(rx_ring + rx_ring_offset));
		rmb();

		rx_size = rx_status >> 20;
		rx_size_align = (rx_size + 3) & ~3;	// for 4 bytes aligned
		pkt_size = rx_size - 4;	// Omit the four octet CRC from the length.

		rx_ring_offset = (rx_ring_offset + 4) % RX_BUF_LEN;

		if (unlikely(rx_status == 0
				|| rx_size > (MAX_ETH_FRAME_SIZE + 4)
				|| rx_size < 16
				|| !(rx_status & RxStatesOK))) {
			_sc92031_rx_tasklet_error(dev, rx_status, rx_size);
			break;
		}

		if (unlikely(rx_size_align + 4 > rx_len)) {
			if (printk_ratelimit())
				printk(KERN_ERR "%s: rx_len is too small\n", dev->name);
			break;
		}

		rx_len -= rx_size_align + 4;

		skb = netdev_alloc_skb(dev, pkt_size + NET_IP_ALIGN);
		if (unlikely(!skb)) {
			if (printk_ratelimit())
				printk(KERN_ERR "%s: Couldn't allocate a skb_buff for a packet of size %u\n",
						dev->name, pkt_size);
			goto next;
		}

		skb_reserve(skb, NET_IP_ALIGN);

		if ((rx_ring_offset + pkt_size) > RX_BUF_LEN) {
			memcpy(skb_put(skb, RX_BUF_LEN - rx_ring_offset),
				rx_ring + rx_ring_offset, RX_BUF_LEN - rx_ring_offset);
			memcpy(skb_put(skb, pkt_size - (RX_BUF_LEN - rx_ring_offset)),
				rx_ring, pkt_size - (RX_BUF_LEN - rx_ring_offset));
		} else {
			memcpy(skb_put(skb, pkt_size), rx_ring + rx_ring_offset, pkt_size);
		}

		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);

		dev->stats.rx_bytes += pkt_size;
		dev->stats.rx_packets++;

		if (rx_status & Rx_Multicast)
			dev->stats.multicast++;

	next:
		rx_ring_offset = (rx_ring_offset + rx_size_align) % RX_BUF_LEN;
	}
	mb();

	priv->rx_ring_tail = rx_ring_head;
	iowrite32(priv->rx_ring_tail, port_base + RxBufRPtr);
}

static void _sc92031_link_tasklet(struct net_device *dev)
{
	if (_sc92031_check_media(dev))
		netif_wake_queue(dev);
	else {
		netif_stop_queue(dev);
		dev->stats.tx_carrier_errors++;
	}
}

static void sc92031_tasklet(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u32 intr_status, intr_mask;

	intr_status = priv->intr_status;

	spin_lock(&priv->lock);

	if (unlikely(!netif_running(dev)))
		goto out;

	if (intr_status & TxOK)
		_sc92031_tx_tasklet(dev);

	if (intr_status & RxOK)
		_sc92031_rx_tasklet(dev);

	if (intr_status & RxOverflow)
		dev->stats.rx_errors++;

	if (intr_status & TimeOut) {
		dev->stats.rx_errors++;
		dev->stats.rx_length_errors++;
	}

	if (intr_status & (LinkFail | LinkOK))
		_sc92031_link_tasklet(dev);

out:
	intr_mask = atomic_read(&priv->intr_mask);
	rmb();

	iowrite32(intr_mask, port_base + IntrMask);
	mmiowb();

	spin_unlock(&priv->lock);
}

static irqreturn_t sc92031_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u32 intr_status, intr_mask;

	/* mask interrupts before clearing IntrStatus */
	iowrite32(0, port_base + IntrMask);
	_sc92031_dummy_read(port_base);

	intr_status = ioread32(port_base + IntrStatus);
	if (unlikely(intr_status == 0xffffffff))
		return IRQ_NONE;	// hardware has gone missing

	intr_status &= IntrBits;
	if (!intr_status)
		goto out_none;

	priv->intr_status = intr_status;
	tasklet_schedule(&priv->tasklet);

	return IRQ_HANDLED;

out_none:
	intr_mask = atomic_read(&priv->intr_mask);
	rmb();

	iowrite32(intr_mask, port_base + IntrMask);
	mmiowb();

	return IRQ_NONE;
}

static struct net_device_stats *sc92031_get_stats(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;

	// FIXME I do not understand what is this trying to do.
	if (netif_running(dev)) {
		int temp;

		spin_lock_bh(&priv->lock);

		/* Update the error count. */
		temp = (ioread32(port_base + RxStatus0) >> 16) & 0xffff;

		if (temp == 0xffff) {
			priv->rx_value += temp;
			dev->stats.rx_fifo_errors = priv->rx_value;
		} else
			dev->stats.rx_fifo_errors = temp + priv->rx_value;

		spin_unlock_bh(&priv->lock);
	}

	return &dev->stats;
}

static int sc92031_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	unsigned len;
	unsigned entry;
	u32 tx_status;

	if (unlikely(skb->len > TX_BUF_SIZE)) {
		dev->stats.tx_dropped++;
		goto out;
	}

	spin_lock(&priv->lock);

	if (unlikely(!netif_carrier_ok(dev))) {
		dev->stats.tx_dropped++;
		goto out_unlock;
	}

	BUG_ON(priv->tx_head - priv->tx_tail >= NUM_TX_DESC);

	entry = priv->tx_head++ % NUM_TX_DESC;

	skb_copy_and_csum_dev(skb, priv->tx_bufs + entry * TX_BUF_SIZE);

	len = skb->len;
	if (len < ETH_ZLEN) {
		memset(priv->tx_bufs + entry * TX_BUF_SIZE + len,
				0, ETH_ZLEN - len);
		len = ETH_ZLEN;
	}

	wmb();

	if (len < 100)
		tx_status = len;
	else if (len < 300)
		tx_status = 0x30000 | len;
	else
		tx_status = 0x50000 | len;

	iowrite32(priv->tx_bufs_dma_addr + entry * TX_BUF_SIZE,
			port_base + TxAddr0 + entry * 4);
	iowrite32(tx_status, port_base + TxStatus0 + entry * 4);
	mmiowb();

	dev->trans_start = jiffies;

	if (priv->tx_head - priv->tx_tail >= NUM_TX_DESC)
		netif_stop_queue(dev);

out_unlock:
	spin_unlock(&priv->lock);

out:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static int sc92031_open(struct net_device *dev)
{
	int err;
	struct sc92031_priv *priv = netdev_priv(dev);
	struct pci_dev *pdev = priv->pdev;

	priv->rx_ring = pci_alloc_consistent(pdev, RX_BUF_LEN,
			&priv->rx_ring_dma_addr);
	if (unlikely(!priv->rx_ring)) {
		err = -ENOMEM;
		goto out_alloc_rx_ring;
	}

	priv->tx_bufs = pci_alloc_consistent(pdev, TX_BUF_TOT_LEN,
			&priv->tx_bufs_dma_addr);
	if (unlikely(!priv->tx_bufs)) {
		err = -ENOMEM;
		goto out_alloc_tx_bufs;
	}
	priv->tx_head = priv->tx_tail = 0;

	err = request_irq(pdev->irq, sc92031_interrupt,
			IRQF_SHARED, dev->name, dev);
	if (unlikely(err < 0))
		goto out_request_irq;

	priv->pm_config = 0;

	/* Interrupts already disabled by sc92031_stop or sc92031_probe */
	spin_lock_bh(&priv->lock);

	_sc92031_reset(dev);
	mmiowb();

	spin_unlock_bh(&priv->lock);
	sc92031_enable_interrupts(dev);

	if (netif_carrier_ok(dev))
		netif_start_queue(dev);
	else
		netif_tx_disable(dev);

	return 0;

out_request_irq:
	pci_free_consistent(pdev, TX_BUF_TOT_LEN, priv->tx_bufs,
			priv->tx_bufs_dma_addr);
out_alloc_tx_bufs:
	pci_free_consistent(pdev, RX_BUF_LEN, priv->rx_ring,
			priv->rx_ring_dma_addr);
out_alloc_rx_ring:
	return err;
}

static int sc92031_stop(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	struct pci_dev *pdev = priv->pdev;

	netif_tx_disable(dev);

	/* Disable interrupts, stop Tx and Rx. */
	sc92031_disable_interrupts(dev);

	spin_lock_bh(&priv->lock);

	_sc92031_disable_tx_rx(dev);
	_sc92031_tx_clear(dev);
	mmiowb();

	spin_unlock_bh(&priv->lock);

	free_irq(pdev->irq, dev);
	pci_free_consistent(pdev, TX_BUF_TOT_LEN, priv->tx_bufs,
			priv->tx_bufs_dma_addr);
	pci_free_consistent(pdev, RX_BUF_LEN, priv->rx_ring,
			priv->rx_ring_dma_addr);

	return 0;
}

static void sc92031_set_multicast_list(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);

	spin_lock_bh(&priv->lock);

	_sc92031_set_mar(dev);
	_sc92031_set_rx_config(dev);
	mmiowb();

	spin_unlock_bh(&priv->lock);
}

static void sc92031_tx_timeout(struct net_device *dev)
{
	struct sc92031_priv *priv = netdev_priv(dev);

	/* Disable interrupts by clearing the interrupt mask.*/
	sc92031_disable_interrupts(dev);

	spin_lock(&priv->lock);

	priv->tx_timeouts++;

	_sc92031_reset(dev);
	mmiowb();

	spin_unlock(&priv->lock);

	/* enable interrupts */
	sc92031_enable_interrupts(dev);

	if (netif_carrier_ok(dev))
		netif_wake_queue(dev);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void sc92031_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	if (sc92031_interrupt(dev->irq, dev) != IRQ_NONE)
		sc92031_tasklet((unsigned long)dev);
	enable_irq(dev->irq);
}
#endif

static int sc92031_ethtool_get_settings(struct net_device *dev,
		struct ethtool_cmd *cmd)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u8 phy_address;
	u32 phy_ctrl;
	u16 output_status;

	spin_lock_bh(&priv->lock);

	phy_address = ioread32(port_base + Miicmd1) >> 27;
	phy_ctrl = ioread32(port_base + PhyCtrl);

	output_status = _sc92031_mii_read(port_base, MII_OutputStatus);
	_sc92031_mii_scan(port_base);
	mmiowb();

	spin_unlock_bh(&priv->lock);

	cmd->supported = SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full
			| SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full
			| SUPPORTED_Autoneg | SUPPORTED_TP | SUPPORTED_MII;

	cmd->advertising = ADVERTISED_TP | ADVERTISED_MII;

	if ((phy_ctrl & (PhyCtrlDux | PhyCtrlSpd100 | PhyCtrlSpd10))
			== (PhyCtrlDux | PhyCtrlSpd100 | PhyCtrlSpd10))
		cmd->advertising |= ADVERTISED_Autoneg;

	if ((phy_ctrl & PhyCtrlSpd10) == PhyCtrlSpd10)
		cmd->advertising |= ADVERTISED_10baseT_Half;

	if ((phy_ctrl & (PhyCtrlSpd10 | PhyCtrlDux))
			== (PhyCtrlSpd10 | PhyCtrlDux))
		cmd->advertising |= ADVERTISED_10baseT_Full;

	if ((phy_ctrl & PhyCtrlSpd100) == PhyCtrlSpd100)
		cmd->advertising |= ADVERTISED_100baseT_Half;

	if ((phy_ctrl & (PhyCtrlSpd100 | PhyCtrlDux))
			== (PhyCtrlSpd100 | PhyCtrlDux))
		cmd->advertising |= ADVERTISED_100baseT_Full;

	if (phy_ctrl & PhyCtrlAne)
		cmd->advertising |= ADVERTISED_Autoneg;

	cmd->speed = (output_status & 0x2) ? SPEED_100 : SPEED_10;
	cmd->duplex = (output_status & 0x4) ? DUPLEX_FULL : DUPLEX_HALF;
	cmd->port = PORT_MII;
	cmd->phy_address = phy_address;
	cmd->transceiver = XCVR_INTERNAL;
	cmd->autoneg = (phy_ctrl & PhyCtrlAne) ? AUTONEG_ENABLE : AUTONEG_DISABLE;

	return 0;
}

static int sc92031_ethtool_set_settings(struct net_device *dev,
		struct ethtool_cmd *cmd)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u32 phy_ctrl;
	u32 old_phy_ctrl;

	if (!(cmd->speed == SPEED_10 || cmd->speed == SPEED_100))
		return -EINVAL;
	if (!(cmd->duplex == DUPLEX_HALF || cmd->duplex == DUPLEX_FULL))
		return -EINVAL;
	if (!(cmd->port == PORT_MII))
		return -EINVAL;
	if (!(cmd->phy_address == 0x1f))
		return -EINVAL;
	if (!(cmd->transceiver == XCVR_INTERNAL))
		return -EINVAL;
	if (!(cmd->autoneg == AUTONEG_DISABLE || cmd->autoneg == AUTONEG_ENABLE))
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_ENABLE) {
		if (!(cmd->advertising & (ADVERTISED_Autoneg
				| ADVERTISED_100baseT_Full
				| ADVERTISED_100baseT_Half
				| ADVERTISED_10baseT_Full
				| ADVERTISED_10baseT_Half)))
			return -EINVAL;

		phy_ctrl = PhyCtrlAne;

		// FIXME: I'm not sure what the original code was trying to do
		if (cmd->advertising & ADVERTISED_Autoneg)
			phy_ctrl |= PhyCtrlDux | PhyCtrlSpd100 | PhyCtrlSpd10;
		if (cmd->advertising & ADVERTISED_100baseT_Full)
			phy_ctrl |= PhyCtrlDux | PhyCtrlSpd100;
		if (cmd->advertising & ADVERTISED_100baseT_Half)
			phy_ctrl |= PhyCtrlSpd100;
		if (cmd->advertising & ADVERTISED_10baseT_Full)
			phy_ctrl |= PhyCtrlSpd10 | PhyCtrlDux;
		if (cmd->advertising & ADVERTISED_10baseT_Half)
			phy_ctrl |= PhyCtrlSpd10;
	} else {
		// FIXME: Whole branch guessed
		phy_ctrl = 0;

		if (cmd->speed == SPEED_10)
			phy_ctrl |= PhyCtrlSpd10;
		else /* cmd->speed == SPEED_100 */
			phy_ctrl |= PhyCtrlSpd100;

		if (cmd->duplex == DUPLEX_FULL)
			phy_ctrl |= PhyCtrlDux;
	}

	spin_lock_bh(&priv->lock);

	old_phy_ctrl = ioread32(port_base + PhyCtrl);
	phy_ctrl |= old_phy_ctrl & ~(PhyCtrlAne | PhyCtrlDux
			| PhyCtrlSpd100 | PhyCtrlSpd10);
	if (phy_ctrl != old_phy_ctrl)
		iowrite32(phy_ctrl, port_base + PhyCtrl);

	spin_unlock_bh(&priv->lock);

	return 0;
}

static void sc92031_ethtool_get_drvinfo(struct net_device *dev,
		struct ethtool_drvinfo *drvinfo)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	struct pci_dev *pdev = priv->pdev;

	strcpy(drvinfo->driver, SC92031_NAME);
	strcpy(drvinfo->version, SC92031_VERSION);
	strcpy(drvinfo->bus_info, pci_name(pdev));
}

static void sc92031_ethtool_get_wol(struct net_device *dev,
		struct ethtool_wolinfo *wolinfo)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u32 pm_config;

	spin_lock_bh(&priv->lock);
	pm_config = ioread32(port_base + PMConfig);
	spin_unlock_bh(&priv->lock);

	// FIXME: Guessed
	wolinfo->supported = WAKE_PHY | WAKE_MAGIC
			| WAKE_UCAST | WAKE_MCAST | WAKE_BCAST;
	wolinfo->wolopts = 0;

	if (pm_config & PM_LinkUp)
		wolinfo->wolopts |= WAKE_PHY;

	if (pm_config & PM_Magic)
		wolinfo->wolopts |= WAKE_MAGIC;

	if (pm_config & PM_WakeUp)
		// FIXME: Guessed
		wolinfo->wolopts |= WAKE_UCAST | WAKE_MCAST | WAKE_BCAST;
}

static int sc92031_ethtool_set_wol(struct net_device *dev,
		struct ethtool_wolinfo *wolinfo)
{
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u32 pm_config;

	spin_lock_bh(&priv->lock);

	pm_config = ioread32(port_base + PMConfig)
			& ~(PM_LinkUp | PM_Magic | PM_WakeUp);

	if (wolinfo->wolopts & WAKE_PHY)
		pm_config |= PM_LinkUp;

	if (wolinfo->wolopts & WAKE_MAGIC)
		pm_config |= PM_Magic;

	// FIXME: Guessed
	if (wolinfo->wolopts & (WAKE_UCAST | WAKE_MCAST | WAKE_BCAST))
		pm_config |= PM_WakeUp;

	priv->pm_config = pm_config;
	iowrite32(pm_config, port_base + PMConfig);
	mmiowb();

	spin_unlock_bh(&priv->lock);

	return 0;
}

static int sc92031_ethtool_nway_reset(struct net_device *dev)
{
	int err = 0;
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem *port_base = priv->port_base;
	u16 bmcr;

	spin_lock_bh(&priv->lock);

	bmcr = _sc92031_mii_read(port_base, MII_BMCR);
	if (!(bmcr & BMCR_ANENABLE)) {
		err = -EINVAL;
		goto out;
	}

	_sc92031_mii_write(port_base, MII_BMCR, bmcr | BMCR_ANRESTART);

out:
	_sc92031_mii_scan(port_base);
	mmiowb();

	spin_unlock_bh(&priv->lock);

	return err;
}

static const char sc92031_ethtool_stats_strings[SILAN_STATS_NUM][ETH_GSTRING_LEN] = {
	"tx_timeout",
	"rx_loss",
};

static void sc92031_ethtool_get_strings(struct net_device *dev,
		u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, sc92031_ethtool_stats_strings,
				SILAN_STATS_NUM * ETH_GSTRING_LEN);
}

static int sc92031_ethtool_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return SILAN_STATS_NUM;
	default:
		return -EOPNOTSUPP;
	}
}

static void sc92031_ethtool_get_ethtool_stats(struct net_device *dev,
		struct ethtool_stats *stats, u64 *data)
{
	struct sc92031_priv *priv = netdev_priv(dev);

	spin_lock_bh(&priv->lock);
	data[0] = priv->tx_timeouts;
	data[1] = priv->rx_loss;
	spin_unlock_bh(&priv->lock);
}

static const struct ethtool_ops sc92031_ethtool_ops = {
	.get_settings		= sc92031_ethtool_get_settings,
	.set_settings		= sc92031_ethtool_set_settings,
	.get_drvinfo		= sc92031_ethtool_get_drvinfo,
	.get_wol		= sc92031_ethtool_get_wol,
	.set_wol		= sc92031_ethtool_set_wol,
	.nway_reset		= sc92031_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_strings		= sc92031_ethtool_get_strings,
	.get_sset_count		= sc92031_ethtool_get_sset_count,
	.get_ethtool_stats	= sc92031_ethtool_get_ethtool_stats,
};


static const struct net_device_ops sc92031_netdev_ops = {
	.ndo_get_stats		= sc92031_get_stats,
	.ndo_start_xmit		= sc92031_start_xmit,
	.ndo_open		= sc92031_open,
	.ndo_stop		= sc92031_stop,
	.ndo_set_multicast_list	= sc92031_set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_tx_timeout		= sc92031_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= sc92031_poll_controller,
#endif
};

static int __devinit sc92031_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	int err;
	void __iomem* port_base;
	struct net_device *dev;
	struct sc92031_priv *priv;
	u32 mac0, mac1;

	err = pci_enable_device(pdev);
	if (unlikely(err < 0))
		goto out_enable_device;

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (unlikely(err < 0))
		goto out_set_dma_mask;

	err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (unlikely(err < 0))
		goto out_set_dma_mask;

	err = pci_request_regions(pdev, SC92031_NAME);
	if (unlikely(err < 0))
		goto out_request_regions;

	port_base = pci_iomap(pdev, SC92031_USE_BAR, 0);
	if (unlikely(!port_base)) {
		err = -EIO;
		goto out_iomap;
	}

	dev = alloc_etherdev(sizeof(struct sc92031_priv));
	if (unlikely(!dev)) {
		err = -ENOMEM;
		goto out_alloc_etherdev;
	}

	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

#if SC92031_USE_BAR == 0
	dev->mem_start = pci_resource_start(pdev, SC92031_USE_BAR);
	dev->mem_end = pci_resource_end(pdev, SC92031_USE_BAR);
#elif SC92031_USE_BAR == 1
	dev->base_addr = pci_resource_start(pdev, SC92031_USE_BAR);
#endif
	dev->irq = pdev->irq;

	/* faked with skb_copy_and_csum_dev */
	dev->features = NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_HIGHDMA;

	dev->netdev_ops		= &sc92031_netdev_ops;
	dev->watchdog_timeo	= TX_TIMEOUT;
	dev->ethtool_ops	= &sc92031_ethtool_ops;

	priv = netdev_priv(dev);
	spin_lock_init(&priv->lock);
	priv->port_base = port_base;
	priv->pdev = pdev;
	tasklet_init(&priv->tasklet, sc92031_tasklet, (unsigned long)dev);
	/* Fudge tasklet count so the call to sc92031_enable_interrupts at
	 * sc92031_open will work correctly */
	tasklet_disable_nosync(&priv->tasklet);

	/* PCI PM Wakeup */
	iowrite32((~PM_LongWF & ~PM_LWPTN) | PM_Enable, port_base + PMConfig);

	mac0 = ioread32(port_base + MAC0);
	mac1 = ioread32(port_base + MAC0 + 4);
	dev->dev_addr[0] = dev->perm_addr[0] = mac0 >> 24;
	dev->dev_addr[1] = dev->perm_addr[1] = mac0 >> 16;
	dev->dev_addr[2] = dev->perm_addr[2] = mac0 >> 8;
	dev->dev_addr[3] = dev->perm_addr[3] = mac0;
	dev->dev_addr[4] = dev->perm_addr[4] = mac1 >> 8;
	dev->dev_addr[5] = dev->perm_addr[5] = mac1;

	err = register_netdev(dev);
	if (err < 0)
		goto out_register_netdev;

	return 0;

out_register_netdev:
	free_netdev(dev);
out_alloc_etherdev:
	pci_iounmap(pdev, port_base);
out_iomap:
	pci_release_regions(pdev);
out_request_regions:
out_set_dma_mask:
	pci_disable_device(pdev);
out_enable_device:
	return err;
}

static void __devexit sc92031_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct sc92031_priv *priv = netdev_priv(dev);
	void __iomem* port_base = priv->port_base;

	unregister_netdev(dev);
	free_netdev(dev);
	pci_iounmap(pdev, port_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static int sc92031_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct sc92031_priv *priv = netdev_priv(dev);

	pci_save_state(pdev);

	if (!netif_running(dev))
		goto out;

	netif_device_detach(dev);

	/* Disable interrupts, stop Tx and Rx. */
	sc92031_disable_interrupts(dev);

	spin_lock_bh(&priv->lock);

	_sc92031_disable_tx_rx(dev);
	_sc92031_tx_clear(dev);
	mmiowb();

	spin_unlock_bh(&priv->lock);

out:
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int sc92031_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct sc92031_priv *priv = netdev_priv(dev);

	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);

	if (!netif_running(dev))
		goto out;

	/* Interrupts already disabled by sc92031_suspend */
	spin_lock_bh(&priv->lock);

	_sc92031_reset(dev);
	mmiowb();

	spin_unlock_bh(&priv->lock);
	sc92031_enable_interrupts(dev);

	netif_device_attach(dev);

	if (netif_carrier_ok(dev))
		netif_wake_queue(dev);
	else
		netif_tx_disable(dev);

out:
	return 0;
}

static struct pci_device_id sc92031_pci_device_id_table[] __devinitdata = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SILAN, PCI_DEVICE_ID_SILAN_SC92031) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SILAN, PCI_DEVICE_ID_SILAN_8139D) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sc92031_pci_device_id_table);

static struct pci_driver sc92031_pci_driver = {
	.name		= SC92031_NAME,
	.id_table	= sc92031_pci_device_id_table,
	.probe		= sc92031_probe,
	.remove		= __devexit_p(sc92031_remove),
	.suspend	= sc92031_suspend,
	.resume		= sc92031_resume,
};

static int __init sc92031_init(void)
{
	printk(KERN_INFO SC92031_DESCRIPTION " " SC92031_VERSION "\n");
	return pci_register_driver(&sc92031_pci_driver);
}

static void __exit sc92031_exit(void)
{
	pci_unregister_driver(&sc92031_pci_driver);
}

module_init(sc92031_init);
module_exit(sc92031_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cesar Eduardo Barros <cesarb@cesarb.net>");
MODULE_DESCRIPTION(SC92031_DESCRIPTION);
MODULE_VERSION(SC92031_VERSION);
