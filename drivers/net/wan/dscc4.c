/*
 * drivers/net/wan/dscc4/dscc4.c: a DSCC4 HDLC driver for Linux
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License.
 *
 * The author may be reached as romieu@cogenit.fr.
 * Specific bug reports/asian food will be welcome.
 *
 * Special thanks to the nice people at CS-Telecom for the hardware and the
 * access to the test/measure tools.
 *
 *
 *                             Theory of Operation
 *
 * I. Board Compatibility
 *
 * This device driver is designed for the Siemens PEB20534 4 ports serial
 * controller as found on Etinc PCISYNC cards. The documentation for the
 * chipset is available at http://www.infineon.com:
 * - Data Sheet "DSCC4, DMA Supported Serial Communication Controller with
 * 4 Channels, PEB 20534 Version 2.1, PEF 20534 Version 2.1";
 * - Application Hint "Management of DSCC4 on-chip FIFO resources".
 * - Errata sheet DS5 (courtesy of Michael Skerritt).
 * Jens David has built an adapter based on the same chipset. Take a look
 * at http://www.afthd.tu-darmstadt.de/~dg1kjd/pciscc4 for a specific
 * driver.
 * Sample code (2 revisions) is available at Infineon.
 *
 * II. Board-specific settings
 *
 * Pcisync can transmit some clock signal to the outside world on the
 * *first two* ports provided you put a quartz and a line driver on it and
 * remove the jumpers. The operation is described on Etinc web site. If you
 * go DCE on these ports, don't forget to use an adequate cable.
 *
 * Sharing of the PCI interrupt line for this board is possible.
 *
 * III. Driver operation
 *
 * The rx/tx operations are based on a linked list of descriptors. The driver
 * doesn't use HOLD mode any more. HOLD mode is definitely buggy and the more
 * I tried to fix it, the more it started to look like (convoluted) software
 * mutation of LxDA method. Errata sheet DS5 suggests to use LxDA: consider
 * this a rfc2119 MUST.
 *
 * Tx direction
 * When the tx ring is full, the xmit routine issues a call to netdev_stop.
 * The device is supposed to be enabled again during an ALLS irq (we could
 * use HI but as it's easy to lose events, it's fscked).
 *
 * Rx direction
 * The received frames aren't supposed to span over multiple receiving areas.
 * I may implement it some day but it isn't the highest ranked item.
 *
 * IV. Notes
 * The current error (XDU, RFO) recovery code is untested.
 * So far, RDO takes his RX channel down and the right sequence to enable it
 * again is still a mystery. If RDO happens, plan a reboot. More details
 * in the code (NB: as this happens, TX still works).
 * Don't mess the cables during operation, especially on DTE ports. I don't
 * suggest it for DCE either but at least one can get some messages instead
 * of a complete instant freeze.
 * Tests are done on Rev. 20 of the silicium. The RDO handling changes with
 * the documentation/chipset releases.
 *
 * TODO:
 * - test X25.
 * - use polling at high irq/s,
 * - performance analysis,
 * - endianness.
 *
 * 2001/12/10	Daniela Squassoni  <daniela@cyclades.com>
 * - Contribution to support the new generic HDLC layer.
 *
 * 2002/01	Ueimor
 * - old style interface removal
 * - dscc4_release_ring fix (related to DMA mapping)
 * - hard_start_xmit fix (hint: TxSizeMax)
 * - misc crapectomy.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/cache.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/string.h>

#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/hdlc.h>
#include <linux/mutex.h>

/* Version */
static const char version[] = "$Id: dscc4.c,v 1.173 2003/09/20 23:55:34 romieu Exp $ for Linux\n";
static int debug;
static int quartz;

#ifdef CONFIG_DSCC4_PCI_RST
static DEFINE_MUTEX(dscc4_mutex);
static u32 dscc4_pci_config_store[16];
#endif

#define	DRV_NAME	"dscc4"

#undef DSCC4_POLLING

/* Module parameters */

MODULE_AUTHOR("Maintainer: Francois Romieu <romieu@cogenit.fr>");
MODULE_DESCRIPTION("Siemens PEB20534 PCI Controller");
MODULE_LICENSE("GPL");
module_param(debug, int, 0);
MODULE_PARM_DESC(debug,"Enable/disable extra messages");
module_param(quartz, int, 0);
MODULE_PARM_DESC(quartz,"If present, on-board quartz frequency (Hz)");

/* Structures */

struct thingie {
	int define;
	u32 bits;
};

struct TxFD {
	__le32 state;
	__le32 next;
	__le32 data;
	__le32 complete;
	u32 jiffies; /* Allows sizeof(TxFD) == sizeof(RxFD) + extra hack */
		     /* FWIW, datasheet calls that "dummy" and says that card
		      * never looks at it; neither does the driver */
};

struct RxFD {
	__le32 state1;
	__le32 next;
	__le32 data;
	__le32 state2;
	__le32 end;
};

#define DUMMY_SKB_SIZE		64
#define TX_LOW			8
#define TX_RING_SIZE		32
#define RX_RING_SIZE		32
#define TX_TOTAL_SIZE		TX_RING_SIZE*sizeof(struct TxFD)
#define RX_TOTAL_SIZE		RX_RING_SIZE*sizeof(struct RxFD)
#define IRQ_RING_SIZE		64		/* Keep it a multiple of 32 */
#define TX_TIMEOUT		(HZ/10)
#define DSCC4_HZ_MAX		33000000
#define BRR_DIVIDER_MAX		64*0x00004000	/* Cf errata DS5 p.10 */
#define dev_per_card		4
#define SCC_REGISTERS_MAX	23		/* Cf errata DS5 p.4 */

#define SOURCE_ID(flags)	(((flags) >> 28) & 0x03)
#define TO_SIZE(state)		(((state) >> 16) & 0x1fff)

/*
 * Given the operating range of Linux HDLC, the 2 defines below could be
 * made simpler. However they are a fine reminder for the limitations of
 * the driver: it's better to stay < TxSizeMax and < RxSizeMax.
 */
#define TO_STATE_TX(len)	cpu_to_le32(((len) & TxSizeMax) << 16)
#define TO_STATE_RX(len)	cpu_to_le32((RX_MAX(len) % RxSizeMax) << 16)
#define RX_MAX(len)		((((len) >> 5) + 1) << 5)	/* Cf RLCR */
#define SCC_REG_START(dpriv)	(SCC_START+(dpriv->dev_id)*SCC_OFFSET)

struct dscc4_pci_priv {
        __le32 *iqcfg;
        int cfg_cur;
        spinlock_t lock;
        struct pci_dev *pdev;

        struct dscc4_dev_priv *root;
        dma_addr_t iqcfg_dma;
	u32 xtal_hz;
};

struct dscc4_dev_priv {
        struct sk_buff *rx_skbuff[RX_RING_SIZE];
        struct sk_buff *tx_skbuff[TX_RING_SIZE];

        struct RxFD *rx_fd;
        struct TxFD *tx_fd;
        __le32 *iqrx;
        __le32 *iqtx;

	/* FIXME: check all the volatile are required */
        volatile u32 tx_current;
        u32 rx_current;
        u32 iqtx_current;
        u32 iqrx_current;

        volatile u32 tx_dirty;
        volatile u32 ltda;
        u32 rx_dirty;
        u32 lrda;

        dma_addr_t tx_fd_dma;
        dma_addr_t rx_fd_dma;
        dma_addr_t iqtx_dma;
        dma_addr_t iqrx_dma;

	u32 scc_regs[SCC_REGISTERS_MAX]; /* Cf errata DS5 p.4 */

	struct timer_list timer;

        struct dscc4_pci_priv *pci_priv;
        spinlock_t lock;

        int dev_id;
	volatile u32 flags;
	u32 timer_help;

	unsigned short encoding;
	unsigned short parity;
	struct net_device *dev;
	sync_serial_settings settings;
	void __iomem *base_addr;
	u32 __pad __attribute__ ((aligned (4)));
};

/* GLOBAL registers definitions */
#define GCMDR   0x00
#define GSTAR   0x04
#define GMODE   0x08
#define IQLENR0 0x0C
#define IQLENR1 0x10
#define IQRX0   0x14
#define IQTX0   0x24
#define IQCFG   0x3c
#define FIFOCR1 0x44
#define FIFOCR2 0x48
#define FIFOCR3 0x4c
#define FIFOCR4 0x34
#define CH0CFG  0x50
#define CH0BRDA 0x54
#define CH0BTDA 0x58
#define CH0FRDA 0x98
#define CH0FTDA 0xb0
#define CH0LRDA 0xc8
#define CH0LTDA 0xe0

/* SCC registers definitions */
#define SCC_START	0x0100
#define SCC_OFFSET      0x80
#define CMDR    0x00
#define STAR    0x04
#define CCR0    0x08
#define CCR1    0x0c
#define CCR2    0x10
#define BRR     0x2C
#define RLCR    0x40
#define IMR     0x54
#define ISR     0x58

#define GPDIR	0x0400
#define GPDATA	0x0404
#define GPIM	0x0408

/* Bit masks */
#define EncodingMask	0x00700000
#define CrcMask		0x00000003

#define IntRxScc0	0x10000000
#define IntTxScc0	0x01000000

#define TxPollCmd	0x00000400
#define RxActivate	0x08000000
#define MTFi		0x04000000
#define Rdr		0x00400000
#define Rdt		0x00200000
#define Idr		0x00100000
#define Idt		0x00080000
#define TxSccRes	0x01000000
#define RxSccRes	0x00010000
#define TxSizeMax	0x1fff		/* Datasheet DS1 - 11.1.1.1 */
#define RxSizeMax	0x1ffc		/* Datasheet DS1 - 11.1.2.1 */

#define Ccr0ClockMask	0x0000003f
#define Ccr1LoopMask	0x00000200
#define IsrMask		0x000fffff
#define BrrExpMask	0x00000f00
#define BrrMultMask	0x0000003f
#define EncodingMask	0x00700000
#define Hold		cpu_to_le32(0x40000000)
#define SccBusy		0x10000000
#define PowerUp		0x80000000
#define Vis		0x00001000
#define FrameOk		(FrameVfr | FrameCrc)
#define FrameVfr	0x80
#define FrameRdo	0x40
#define FrameCrc	0x20
#define FrameRab	0x10
#define FrameAborted	cpu_to_le32(0x00000200)
#define FrameEnd	cpu_to_le32(0x80000000)
#define DataComplete	cpu_to_le32(0x40000000)
#define LengthCheck	0x00008000
#define SccEvt		0x02000000
#define NoAck		0x00000200
#define Action		0x00000001
#define HiDesc		cpu_to_le32(0x20000000)

/* SCC events */
#define RxEvt		0xf0000000
#define TxEvt		0x0f000000
#define Alls		0x00040000
#define Xdu		0x00010000
#define Cts		0x00004000
#define Xmr		0x00002000
#define Xpr		0x00001000
#define Rdo		0x00000080
#define Rfs		0x00000040
#define Cd		0x00000004
#define Rfo		0x00000002
#define Flex		0x00000001

/* DMA core events */
#define Cfg		0x00200000
#define Hi		0x00040000
#define Fi		0x00020000
#define Err		0x00010000
#define Arf		0x00000002
#define ArAck		0x00000001

/* State flags */
#define Ready		0x00000000
#define NeedIDR		0x00000001
#define NeedIDT		0x00000002
#define RdoSet		0x00000004
#define FakeReset	0x00000008

/* Don't mask RDO. Ever. */
#ifdef DSCC4_POLLING
#define EventsMask	0xfffeef7f
#else
#define EventsMask	0xfffa8f7a
#endif

/* Functions prototypes */
static void dscc4_rx_irq(struct dscc4_pci_priv *, struct dscc4_dev_priv *);
static void dscc4_tx_irq(struct dscc4_pci_priv *, struct dscc4_dev_priv *);
static int dscc4_found1(struct pci_dev *, void __iomem *ioaddr);
static int dscc4_init_one(struct pci_dev *, const struct pci_device_id *ent);
static int dscc4_open(struct net_device *);
static netdev_tx_t dscc4_start_xmit(struct sk_buff *,
					  struct net_device *);
static int dscc4_close(struct net_device *);
static int dscc4_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int dscc4_init_ring(struct net_device *);
static void dscc4_release_ring(struct dscc4_dev_priv *);
static void dscc4_timer(unsigned long);
static void dscc4_tx_timeout(struct net_device *);
static irqreturn_t dscc4_irq(int irq, void *dev_id);
static int dscc4_hdlc_attach(struct net_device *, unsigned short, unsigned short);
static int dscc4_set_iface(struct dscc4_dev_priv *, struct net_device *);
#ifdef DSCC4_POLLING
static int dscc4_tx_poll(struct dscc4_dev_priv *, struct net_device *);
#endif

static inline struct dscc4_dev_priv *dscc4_priv(struct net_device *dev)
{
	return dev_to_hdlc(dev)->priv;
}

static inline struct net_device *dscc4_to_dev(struct dscc4_dev_priv *p)
{
	return p->dev;
}

static void scc_patchl(u32 mask, u32 value, struct dscc4_dev_priv *dpriv,
			struct net_device *dev, int offset)
{
	u32 state;

	/* Cf scc_writel for concern regarding thread-safety */
	state = dpriv->scc_regs[offset >> 2];
	state &= ~mask;
	state |= value;
	dpriv->scc_regs[offset >> 2] = state;
	writel(state, dpriv->base_addr + SCC_REG_START(dpriv) + offset);
}

static void scc_writel(u32 bits, struct dscc4_dev_priv *dpriv,
		       struct net_device *dev, int offset)
{
	/*
	 * Thread-UNsafe.
	 * As of 2002/02/16, there are no thread racing for access.
	 */
	dpriv->scc_regs[offset >> 2] = bits;
	writel(bits, dpriv->base_addr + SCC_REG_START(dpriv) + offset);
}

static inline u32 scc_readl(struct dscc4_dev_priv *dpriv, int offset)
{
	return dpriv->scc_regs[offset >> 2];
}

static u32 scc_readl_star(struct dscc4_dev_priv *dpriv, struct net_device *dev)
{
	/* Cf errata DS5 p.4 */
	readl(dpriv->base_addr + SCC_REG_START(dpriv) + STAR);
	return readl(dpriv->base_addr + SCC_REG_START(dpriv) + STAR);
}

static inline void dscc4_do_tx(struct dscc4_dev_priv *dpriv,
			       struct net_device *dev)
{
	dpriv->ltda = dpriv->tx_fd_dma +
                      ((dpriv->tx_current-1)%TX_RING_SIZE)*sizeof(struct TxFD);
	writel(dpriv->ltda, dpriv->base_addr + CH0LTDA + dpriv->dev_id*4);
	/* Flush posted writes *NOW* */
	readl(dpriv->base_addr + CH0LTDA + dpriv->dev_id*4);
}

static inline void dscc4_rx_update(struct dscc4_dev_priv *dpriv,
				   struct net_device *dev)
{
	dpriv->lrda = dpriv->rx_fd_dma +
		      ((dpriv->rx_dirty - 1)%RX_RING_SIZE)*sizeof(struct RxFD);
	writel(dpriv->lrda, dpriv->base_addr + CH0LRDA + dpriv->dev_id*4);
}

static inline unsigned int dscc4_tx_done(struct dscc4_dev_priv *dpriv)
{
	return dpriv->tx_current == dpriv->tx_dirty;
}

static inline unsigned int dscc4_tx_quiescent(struct dscc4_dev_priv *dpriv,
					      struct net_device *dev)
{
	return readl(dpriv->base_addr + CH0FTDA + dpriv->dev_id*4) == dpriv->ltda;
}

static int state_check(u32 state, struct dscc4_dev_priv *dpriv,
		       struct net_device *dev, const char *msg)
{
	int ret = 0;

	if (debug > 1) {
	if (SOURCE_ID(state) != dpriv->dev_id) {
		printk(KERN_DEBUG "%s (%s): Source Id=%d, state=%08x\n",
		       dev->name, msg, SOURCE_ID(state), state );
			ret = -1;
	}
	if (state & 0x0df80c00) {
		printk(KERN_DEBUG "%s (%s): state=%08x (UFO alert)\n",
		       dev->name, msg, state);
			ret = -1;
	}
	}
	return ret;
}

static void dscc4_tx_print(struct net_device *dev,
			   struct dscc4_dev_priv *dpriv,
			   char *msg)
{
	printk(KERN_DEBUG "%s: tx_current=%02d tx_dirty=%02d (%s)\n",
	       dev->name, dpriv->tx_current, dpriv->tx_dirty, msg);
}

static void dscc4_release_ring(struct dscc4_dev_priv *dpriv)
{
	struct pci_dev *pdev = dpriv->pci_priv->pdev;
	struct TxFD *tx_fd = dpriv->tx_fd;
	struct RxFD *rx_fd = dpriv->rx_fd;
	struct sk_buff **skbuff;
	int i;

	pci_free_consistent(pdev, TX_TOTAL_SIZE, tx_fd, dpriv->tx_fd_dma);
	pci_free_consistent(pdev, RX_TOTAL_SIZE, rx_fd, dpriv->rx_fd_dma);

	skbuff = dpriv->tx_skbuff;
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (*skbuff) {
			pci_unmap_single(pdev, le32_to_cpu(tx_fd->data),
				(*skbuff)->len, PCI_DMA_TODEVICE);
			dev_kfree_skb(*skbuff);
		}
		skbuff++;
		tx_fd++;
	}

	skbuff = dpriv->rx_skbuff;
	for (i = 0; i < RX_RING_SIZE; i++) {
		if (*skbuff) {
			pci_unmap_single(pdev, le32_to_cpu(rx_fd->data),
				RX_MAX(HDLC_MAX_MRU), PCI_DMA_FROMDEVICE);
			dev_kfree_skb(*skbuff);
		}
		skbuff++;
		rx_fd++;
	}
}

static inline int try_get_rx_skb(struct dscc4_dev_priv *dpriv,
				 struct net_device *dev)
{
	unsigned int dirty = dpriv->rx_dirty%RX_RING_SIZE;
	struct RxFD *rx_fd = dpriv->rx_fd + dirty;
	const int len = RX_MAX(HDLC_MAX_MRU);
	struct sk_buff *skb;
	int ret = 0;

	skb = dev_alloc_skb(len);
	dpriv->rx_skbuff[dirty] = skb;
	if (skb) {
		skb->protocol = hdlc_type_trans(skb, dev);
		rx_fd->data = cpu_to_le32(pci_map_single(dpriv->pci_priv->pdev,
					  skb->data, len, PCI_DMA_FROMDEVICE));
	} else {
		rx_fd->data = 0;
		ret = -1;
	}
	return ret;
}

/*
 * IRQ/thread/whatever safe
 */
static int dscc4_wait_ack_cec(struct dscc4_dev_priv *dpriv,
			      struct net_device *dev, char *msg)
{
	s8 i = 0;

	do {
		if (!(scc_readl_star(dpriv, dev) & SccBusy)) {
			printk(KERN_DEBUG "%s: %s ack (%d try)\n", dev->name,
			       msg, i);
			goto done;
		}
		schedule_timeout_uninterruptible(10);
		rmb();
	} while (++i > 0);
	netdev_err(dev, "%s timeout\n", msg);
done:
	return (i >= 0) ? i : -EAGAIN;
}

static int dscc4_do_action(struct net_device *dev, char *msg)
{
	void __iomem *ioaddr = dscc4_priv(dev)->base_addr;
	s16 i = 0;

	writel(Action, ioaddr + GCMDR);
	ioaddr += GSTAR;
	do {
		u32 state = readl(ioaddr);

		if (state & ArAck) {
			netdev_dbg(dev, "%s ack\n", msg);
			writel(ArAck, ioaddr);
			goto done;
		} else if (state & Arf) {
			netdev_err(dev, "%s failed\n", msg);
			writel(Arf, ioaddr);
			i = -1;
			goto done;
	}
		rmb();
	} while (++i > 0);
	netdev_err(dev, "%s timeout\n", msg);
done:
	return i;
}

static inline int dscc4_xpr_ack(struct dscc4_dev_priv *dpriv)
{
	int cur = dpriv->iqtx_current%IRQ_RING_SIZE;
	s8 i = 0;

	do {
		if (!(dpriv->flags & (NeedIDR | NeedIDT)) ||
		    (dpriv->iqtx[cur] & cpu_to_le32(Xpr)))
			break;
		smp_rmb();
		schedule_timeout_uninterruptible(10);
	} while (++i > 0);

	return (i >= 0 ) ? i : -EAGAIN;
}

#if 0 /* dscc4_{rx/tx}_reset are both unreliable - more tweak needed */
static void dscc4_rx_reset(struct dscc4_dev_priv *dpriv, struct net_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dpriv->pci_priv->lock, flags);
	/* Cf errata DS5 p.6 */
	writel(0x00000000, dpriv->base_addr + CH0LRDA + dpriv->dev_id*4);
	scc_patchl(PowerUp, 0, dpriv, dev, CCR0);
	readl(dpriv->base_addr + CH0LRDA + dpriv->dev_id*4);
	writel(MTFi|Rdr, dpriv->base_addr + dpriv->dev_id*0x0c + CH0CFG);
	writel(Action, dpriv->base_addr + GCMDR);
	spin_unlock_irqrestore(&dpriv->pci_priv->lock, flags);
}

#endif

#if 0
static void dscc4_tx_reset(struct dscc4_dev_priv *dpriv, struct net_device *dev)
{
	u16 i = 0;

	/* Cf errata DS5 p.7 */
	scc_patchl(PowerUp, 0, dpriv, dev, CCR0);
	scc_writel(0x00050000, dpriv, dev, CCR2);
	/*
	 * Must be longer than the time required to fill the fifo.
	 */
	while (!dscc4_tx_quiescent(dpriv, dev) && ++i) {
		udelay(1);
		wmb();
	}

	writel(MTFi|Rdt, dpriv->base_addr + dpriv->dev_id*0x0c + CH0CFG);
	if (dscc4_do_action(dev, "Rdt") < 0)
		netdev_err(dev, "Tx reset failed\n");
}
#endif

/* TODO: (ab)use this function to refill a completely depleted RX ring. */
static inline void dscc4_rx_skb(struct dscc4_dev_priv *dpriv,
				struct net_device *dev)
{
	struct RxFD *rx_fd = dpriv->rx_fd + dpriv->rx_current%RX_RING_SIZE;
	struct pci_dev *pdev = dpriv->pci_priv->pdev;
	struct sk_buff *skb;
	int pkt_len;

	skb = dpriv->rx_skbuff[dpriv->rx_current++%RX_RING_SIZE];
	if (!skb) {
		printk(KERN_DEBUG "%s: skb=0 (%s)\n", dev->name, __func__);
		goto refill;
	}
	pkt_len = TO_SIZE(le32_to_cpu(rx_fd->state2));
	pci_unmap_single(pdev, le32_to_cpu(rx_fd->data),
			 RX_MAX(HDLC_MAX_MRU), PCI_DMA_FROMDEVICE);
	if ((skb->data[--pkt_len] & FrameOk) == FrameOk) {
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += pkt_len;
		skb_put(skb, pkt_len);
		if (netif_running(dev))
			skb->protocol = hdlc_type_trans(skb, dev);
		netif_rx(skb);
	} else {
		if (skb->data[pkt_len] & FrameRdo)
			dev->stats.rx_fifo_errors++;
		else if (!(skb->data[pkt_len] & FrameCrc))
			dev->stats.rx_crc_errors++;
		else if ((skb->data[pkt_len] & (FrameVfr | FrameRab)) !=
			 (FrameVfr | FrameRab))
			dev->stats.rx_length_errors++;
		dev->stats.rx_errors++;
		dev_kfree_skb_irq(skb);
	}
refill:
	while ((dpriv->rx_dirty - dpriv->rx_current) % RX_RING_SIZE) {
		if (try_get_rx_skb(dpriv, dev) < 0)
			break;
		dpriv->rx_dirty++;
	}
	dscc4_rx_update(dpriv, dev);
	rx_fd->state2 = 0x00000000;
	rx_fd->end = cpu_to_le32(0xbabeface);
}

static void dscc4_free1(struct pci_dev *pdev)
{
	struct dscc4_pci_priv *ppriv;
	struct dscc4_dev_priv *root;
	int i;

	ppriv = pci_get_drvdata(pdev);
	root = ppriv->root;

	for (i = 0; i < dev_per_card; i++)
		unregister_hdlc_device(dscc4_to_dev(root + i));

	pci_set_drvdata(pdev, NULL);

	for (i = 0; i < dev_per_card; i++)
		free_netdev(root[i].dev);
	kfree(root);
	kfree(ppriv);
}

static int __devinit dscc4_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct dscc4_pci_priv *priv;
	struct dscc4_dev_priv *dpriv;
	void __iomem *ioaddr;
	int i, rc;

	printk(KERN_DEBUG "%s", version);

	rc = pci_enable_device(pdev);
	if (rc < 0)
		goto out;

	rc = pci_request_region(pdev, 0, "registers");
	if (rc < 0) {
		pr_err("can't reserve MMIO region (regs)\n");
	        goto err_disable_0;
	}
	rc = pci_request_region(pdev, 1, "LBI interface");
	if (rc < 0) {
		pr_err("can't reserve MMIO region (lbi)\n");
	        goto err_free_mmio_region_1;
	}

	ioaddr = pci_ioremap_bar(pdev, 0);
	if (!ioaddr) {
		pr_err("cannot remap MMIO region %llx @ %llx\n",
		       (unsigned long long)pci_resource_len(pdev, 0),
		       (unsigned long long)pci_resource_start(pdev, 0));
		rc = -EIO;
		goto err_free_mmio_regions_2;
	}
	printk(KERN_DEBUG "Siemens DSCC4, MMIO at %#llx (regs), %#llx (lbi), IRQ %d\n",
	        (unsigned long long)pci_resource_start(pdev, 0),
	        (unsigned long long)pci_resource_start(pdev, 1), pdev->irq);

	/* Cf errata DS5 p.2 */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xf8);
	pci_set_master(pdev);

	rc = dscc4_found1(pdev, ioaddr);
	if (rc < 0)
	        goto err_iounmap_3;

	priv = pci_get_drvdata(pdev);

	rc = request_irq(pdev->irq, dscc4_irq, IRQF_SHARED, DRV_NAME, priv->root);
	if (rc < 0) {
		pr_warn("IRQ %d busy\n", pdev->irq);
		goto err_release_4;
	}

	/* power up/little endian/dma core controlled via lrda/ltda */
	writel(0x00000001, ioaddr + GMODE);
	/* Shared interrupt queue */
	{
		u32 bits;

		bits = (IRQ_RING_SIZE >> 5) - 1;
		bits |= bits << 4;
		bits |= bits << 8;
		bits |= bits << 16;
		writel(bits, ioaddr + IQLENR0);
	}
	/* Global interrupt queue */
	writel((u32)(((IRQ_RING_SIZE >> 5) - 1) << 20), ioaddr + IQLENR1);
	priv->iqcfg = (__le32 *) pci_alloc_consistent(pdev,
		IRQ_RING_SIZE*sizeof(__le32), &priv->iqcfg_dma);
	if (!priv->iqcfg)
		goto err_free_irq_5;
	writel(priv->iqcfg_dma, ioaddr + IQCFG);

	rc = -ENOMEM;

	/*
	 * SCC 0-3 private rx/tx irq structures
	 * IQRX/TXi needs to be set soon. Learned it the hard way...
	 */
	for (i = 0; i < dev_per_card; i++) {
		dpriv = priv->root + i;
		dpriv->iqtx = (__le32 *) pci_alloc_consistent(pdev,
			IRQ_RING_SIZE*sizeof(u32), &dpriv->iqtx_dma);
		if (!dpriv->iqtx)
			goto err_free_iqtx_6;
		writel(dpriv->iqtx_dma, ioaddr + IQTX0 + i*4);
	}
	for (i = 0; i < dev_per_card; i++) {
		dpriv = priv->root + i;
		dpriv->iqrx = (__le32 *) pci_alloc_consistent(pdev,
			IRQ_RING_SIZE*sizeof(u32), &dpriv->iqrx_dma);
		if (!dpriv->iqrx)
			goto err_free_iqrx_7;
		writel(dpriv->iqrx_dma, ioaddr + IQRX0 + i*4);
	}

	/* Cf application hint. Beware of hard-lock condition on threshold. */
	writel(0x42104000, ioaddr + FIFOCR1);
	//writel(0x9ce69800, ioaddr + FIFOCR2);
	writel(0xdef6d800, ioaddr + FIFOCR2);
	//writel(0x11111111, ioaddr + FIFOCR4);
	writel(0x18181818, ioaddr + FIFOCR4);
	// FIXME: should depend on the chipset revision
	writel(0x0000000e, ioaddr + FIFOCR3);

	writel(0xff200001, ioaddr + GCMDR);

	rc = 0;
out:
	return rc;

err_free_iqrx_7:
	while (--i >= 0) {
		dpriv = priv->root + i;
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32),
				    dpriv->iqrx, dpriv->iqrx_dma);
	}
	i = dev_per_card;
err_free_iqtx_6:
	while (--i >= 0) {
		dpriv = priv->root + i;
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32),
				    dpriv->iqtx, dpriv->iqtx_dma);
	}
	pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), priv->iqcfg,
			    priv->iqcfg_dma);
err_free_irq_5:
	free_irq(pdev->irq, priv->root);
err_release_4:
	dscc4_free1(pdev);
err_iounmap_3:
	iounmap (ioaddr);
err_free_mmio_regions_2:
	pci_release_region(pdev, 1);
err_free_mmio_region_1:
	pci_release_region(pdev, 0);
err_disable_0:
	pci_disable_device(pdev);
	goto out;
};

/*
 * Let's hope the default values are decent enough to protect my
 * feet from the user's gun - Ueimor
 */
static void dscc4_init_registers(struct dscc4_dev_priv *dpriv,
				 struct net_device *dev)
{
	/* No interrupts, SCC core disabled. Let's relax */
	scc_writel(0x00000000, dpriv, dev, CCR0);

	scc_writel(LengthCheck | (HDLC_MAX_MRU >> 5), dpriv, dev, RLCR);

	/*
	 * No address recognition/crc-CCITT/cts enabled
	 * Shared flags transmission disabled - cf errata DS5 p.11
	 * Carrier detect disabled - cf errata p.14
	 * FIXME: carrier detection/polarity may be handled more gracefully.
	 */
	scc_writel(0x02408000, dpriv, dev, CCR1);

	/* crc not forwarded - Cf errata DS5 p.11 */
	scc_writel(0x00050008 & ~RxActivate, dpriv, dev, CCR2);
	// crc forwarded
	//scc_writel(0x00250008 & ~RxActivate, dpriv, dev, CCR2);
}

static inline int dscc4_set_quartz(struct dscc4_dev_priv *dpriv, int hz)
{
	int ret = 0;

	if ((hz < 0) || (hz > DSCC4_HZ_MAX))
		ret = -EOPNOTSUPP;
	else
		dpriv->pci_priv->xtal_hz = hz;

	return ret;
}

static const struct net_device_ops dscc4_ops = {
	.ndo_open       = dscc4_open,
	.ndo_stop       = dscc4_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = dscc4_ioctl,
	.ndo_tx_timeout = dscc4_tx_timeout,
};

static int dscc4_found1(struct pci_dev *pdev, void __iomem *ioaddr)
{
	struct dscc4_pci_priv *ppriv;
	struct dscc4_dev_priv *root;
	int i, ret = -ENOMEM;

	root = kcalloc(dev_per_card, sizeof(*root), GFP_KERNEL);
	if (!root)
		goto err_out;

	for (i = 0; i < dev_per_card; i++) {
		root[i].dev = alloc_hdlcdev(root + i);
		if (!root[i].dev)
			goto err_free_dev;
	}

	ppriv = kzalloc(sizeof(*ppriv), GFP_KERNEL);
	if (!ppriv)
		goto err_free_dev;

	ppriv->root = root;
	spin_lock_init(&ppriv->lock);

	for (i = 0; i < dev_per_card; i++) {
		struct dscc4_dev_priv *dpriv = root + i;
		struct net_device *d = dscc4_to_dev(dpriv);
		hdlc_device *hdlc = dev_to_hdlc(d);

	        d->base_addr = (unsigned long)ioaddr;
	        d->irq = pdev->irq;
		d->netdev_ops = &dscc4_ops;
		d->watchdog_timeo = TX_TIMEOUT;
		SET_NETDEV_DEV(d, &pdev->dev);

		dpriv->dev_id = i;
		dpriv->pci_priv = ppriv;
		dpriv->base_addr = ioaddr;
		spin_lock_init(&dpriv->lock);

		hdlc->xmit = dscc4_start_xmit;
		hdlc->attach = dscc4_hdlc_attach;

		dscc4_init_registers(dpriv, d);
		dpriv->parity = PARITY_CRC16_PR0_CCITT;
		dpriv->encoding = ENCODING_NRZ;
	
		ret = dscc4_init_ring(d);
		if (ret < 0)
			goto err_unregister;

		ret = register_hdlc_device(d);
		if (ret < 0) {
			pr_err("unable to register\n");
			dscc4_release_ring(dpriv);
			goto err_unregister;
	        }
	}

	ret = dscc4_set_quartz(root, quartz);
	if (ret < 0)
		goto err_unregister;

	pci_set_drvdata(pdev, ppriv);
	return ret;

err_unregister:
	while (i-- > 0) {
		dscc4_release_ring(root + i);
		unregister_hdlc_device(dscc4_to_dev(root + i));
	}
	kfree(ppriv);
	i = dev_per_card;
err_free_dev:
	while (i-- > 0)
		free_netdev(root[i].dev);
	kfree(root);
err_out:
	return ret;
};

/* FIXME: get rid of the unneeded code */
static void dscc4_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);
//	struct dscc4_pci_priv *ppriv;

	goto done;
done:
        dpriv->timer.expires = jiffies + TX_TIMEOUT;
        add_timer(&dpriv->timer);
}

static void dscc4_tx_timeout(struct net_device *dev)
{
	/* FIXME: something is missing there */
}

static int dscc4_loopback_check(struct dscc4_dev_priv *dpriv)
{
	sync_serial_settings *settings = &dpriv->settings;

	if (settings->loopback && (settings->clock_type != CLOCK_INT)) {
		struct net_device *dev = dscc4_to_dev(dpriv);

		netdev_info(dev, "loopback requires clock\n");
		return -1;
	}
	return 0;
}

#ifdef CONFIG_DSCC4_PCI_RST
/*
 * Some DSCC4-based cards wires the GPIO port and the PCI #RST pin together
 * so as to provide a safe way to reset the asic while not the whole machine
 * rebooting.
 *
 * This code doesn't need to be efficient. Keep It Simple
 */
static void dscc4_pci_reset(struct pci_dev *pdev, void __iomem *ioaddr)
{
	int i;

	mutex_lock(&dscc4_mutex);
	for (i = 0; i < 16; i++)
		pci_read_config_dword(pdev, i << 2, dscc4_pci_config_store + i);

	/* Maximal LBI clock divider (who cares ?) and whole GPIO range. */
	writel(0x001c0000, ioaddr + GMODE);
	/* Configure GPIO port as output */
	writel(0x0000ffff, ioaddr + GPDIR);
	/* Disable interruption */
	writel(0x0000ffff, ioaddr + GPIM);

	writel(0x0000ffff, ioaddr + GPDATA);
	writel(0x00000000, ioaddr + GPDATA);

	/* Flush posted writes */
	readl(ioaddr + GSTAR);

	schedule_timeout_uninterruptible(10);

	for (i = 0; i < 16; i++)
		pci_write_config_dword(pdev, i << 2, dscc4_pci_config_store[i]);
	mutex_unlock(&dscc4_mutex);
}
#else
#define dscc4_pci_reset(pdev,ioaddr)	do {} while (0)
#endif /* CONFIG_DSCC4_PCI_RST */

static int dscc4_open(struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);
	struct dscc4_pci_priv *ppriv;
	int ret = -EAGAIN;

	if ((dscc4_loopback_check(dpriv) < 0))
		goto err;

	if ((ret = hdlc_open(dev)))
		goto err;

	ppriv = dpriv->pci_priv;

	/*
	 * Due to various bugs, there is no way to reliably reset a
	 * specific port (manufacturer's dependent special PCI #RST wiring
	 * apart: it affects all ports). Thus the device goes in the best
	 * silent mode possible at dscc4_close() time and simply claims to
	 * be up if it's opened again. It still isn't possible to change
	 * the HDLC configuration without rebooting but at least the ports
	 * can be up/down ifconfig'ed without killing the host.
	 */
	if (dpriv->flags & FakeReset) {
		dpriv->flags &= ~FakeReset;
		scc_patchl(0, PowerUp, dpriv, dev, CCR0);
		scc_patchl(0, 0x00050000, dpriv, dev, CCR2);
		scc_writel(EventsMask, dpriv, dev, IMR);
		netdev_info(dev, "up again\n");
		goto done;
	}

	/* IDT+IDR during XPR */
	dpriv->flags = NeedIDR | NeedIDT;

	scc_patchl(0, PowerUp | Vis, dpriv, dev, CCR0);

	/*
	 * The following is a bit paranoid...
	 *
	 * NB: the datasheet "...CEC will stay active if the SCC is in
	 * power-down mode or..." and CCR2.RAC = 1 are two different
	 * situations.
	 */
	if (scc_readl_star(dpriv, dev) & SccBusy) {
		netdev_err(dev, "busy - try later\n");
		ret = -EAGAIN;
		goto err_out;
	} else
		netdev_info(dev, "available - good\n");

	scc_writel(EventsMask, dpriv, dev, IMR);

	/* Posted write is flushed in the wait_ack loop */
	scc_writel(TxSccRes | RxSccRes, dpriv, dev, CMDR);

	if ((ret = dscc4_wait_ack_cec(dpriv, dev, "Cec")) < 0)
		goto err_disable_scc_events;

	/*
	 * I would expect XPR near CE completion (before ? after ?).
	 * At worst, this code won't see a late XPR and people
	 * will have to re-issue an ifconfig (this is harmless).
	 * WARNING, a really missing XPR usually means a hardware
	 * reset is needed. Suggestions anyone ?
	 */
	if ((ret = dscc4_xpr_ack(dpriv)) < 0) {
		pr_err("XPR timeout\n");
		goto err_disable_scc_events;
	}
	
	if (debug > 2)
		dscc4_tx_print(dev, dpriv, "Open");

done:
	netif_start_queue(dev);

        init_timer(&dpriv->timer);
        dpriv->timer.expires = jiffies + 10*HZ;
        dpriv->timer.data = (unsigned long)dev;
	dpriv->timer.function = dscc4_timer;
        add_timer(&dpriv->timer);
	netif_carrier_on(dev);

	return 0;

err_disable_scc_events:
	scc_writel(0xffffffff, dpriv, dev, IMR);
	scc_patchl(PowerUp | Vis, 0, dpriv, dev, CCR0);
err_out:
	hdlc_close(dev);
err:
	return ret;
}

#ifdef DSCC4_POLLING
static int dscc4_tx_poll(struct dscc4_dev_priv *dpriv, struct net_device *dev)
{
	/* FIXME: it's gonna be easy (TM), for sure */
}
#endif /* DSCC4_POLLING */

static netdev_tx_t dscc4_start_xmit(struct sk_buff *skb,
					  struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);
	struct dscc4_pci_priv *ppriv = dpriv->pci_priv;
	struct TxFD *tx_fd;
	int next;

	next = dpriv->tx_current%TX_RING_SIZE;
	dpriv->tx_skbuff[next] = skb;
	tx_fd = dpriv->tx_fd + next;
	tx_fd->state = FrameEnd | TO_STATE_TX(skb->len);
	tx_fd->data = cpu_to_le32(pci_map_single(ppriv->pdev, skb->data, skb->len,
				     PCI_DMA_TODEVICE));
	tx_fd->complete = 0x00000000;
	tx_fd->jiffies = jiffies;
	mb();

#ifdef DSCC4_POLLING
	spin_lock(&dpriv->lock);
	while (dscc4_tx_poll(dpriv, dev));
	spin_unlock(&dpriv->lock);
#endif

	if (debug > 2)
		dscc4_tx_print(dev, dpriv, "Xmit");
	/* To be cleaned(unsigned int)/optimized. Later, ok ? */
	if (!((++dpriv->tx_current - dpriv->tx_dirty)%TX_RING_SIZE))
		netif_stop_queue(dev);

	if (dscc4_tx_quiescent(dpriv, dev))
		dscc4_do_tx(dpriv, dev);

	return NETDEV_TX_OK;
}

static int dscc4_close(struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);

	del_timer_sync(&dpriv->timer);
	netif_stop_queue(dev);

	scc_patchl(PowerUp | Vis, 0, dpriv, dev, CCR0);
	scc_patchl(0x00050000, 0, dpriv, dev, CCR2);
	scc_writel(0xffffffff, dpriv, dev, IMR);

	dpriv->flags |= FakeReset;

	hdlc_close(dev);

	return 0;
}

static inline int dscc4_check_clock_ability(int port)
{
	int ret = 0;

#ifdef CONFIG_DSCC4_PCISYNC
	if (port >= 2)
		ret = -1;
#endif
	return ret;
}

/*
 * DS1 p.137: "There are a total of 13 different clocking modes..."
 *                                  ^^
 * Design choices:
 * - by default, assume a clock is provided on pin RxClk/TxClk (clock mode 0a).
 *   Clock mode 3b _should_ work but the testing seems to make this point
 *   dubious (DIY testing requires setting CCR0 at 0x00000033).
 *   This is supposed to provide least surprise "DTE like" behavior.
 * - if line rate is specified, clocks are assumed to be locally generated.
 *   A quartz must be available (on pin XTAL1). Modes 6b/7b are used. Choosing
 *   between these it automagically done according on the required frequency
 *   scaling. Of course some rounding may take place.
 * - no high speed mode (40Mb/s). May be trivial to do but I don't have an
 *   appropriate external clocking device for testing.
 * - no time-slot/clock mode 5: shameless laziness.
 *
 * The clock signals wiring can be (is ?) manufacturer dependent. Good luck.
 *
 * BIG FAT WARNING: if the device isn't provided enough clocking signal, it
 * won't pass the init sequence. For example, straight back-to-back DTE without
 * external clock will fail when dscc4_open() (<- 'ifconfig hdlcx xxx') is
 * called.
 *
 * Typos lurk in datasheet (missing divier in clock mode 7a figure 51 p.153
 * DS0 for example)
 *
 * Clock mode related bits of CCR0:
 *     +------------ TOE: output TxClk (0b/2b/3a/3b/6b/7a/7b only)
 *     | +---------- SSEL: sub-mode select 0 -> a, 1 -> b
 *     | | +-------- High Speed: say 0
 *     | | | +-+-+-- Clock Mode: 0..7
 *     | | | | | |
 * -+-+-+-+-+-+-+-+
 * x|x|5|4|3|2|1|0| lower bits
 *
 * Division factor of BRR: k = (N+1)x2^M (total divider = 16xk in mode 6b)
 *            +-+-+-+------------------ M (0..15)
 *            | | | |     +-+-+-+-+-+-- N (0..63)
 *    0 0 0 0 | | | | 0 0 | | | | | |
 * ...-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    f|e|d|c|b|a|9|8|7|6|5|4|3|2|1|0| lower bits
 *
 */
static int dscc4_set_clock(struct net_device *dev, u32 *bps, u32 *state)
{
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);
	int ret = -1;
	u32 brr;

	*state &= ~Ccr0ClockMask;
	if (*bps) { /* Clock generated - required for DCE */
		u32 n = 0, m = 0, divider;
		int xtal;

		xtal = dpriv->pci_priv->xtal_hz;
		if (!xtal)
			goto done;
		if (dscc4_check_clock_ability(dpriv->dev_id) < 0)
			goto done;
		divider = xtal / *bps;
		if (divider > BRR_DIVIDER_MAX) {
			divider >>= 4;
			*state |= 0x00000036; /* Clock mode 6b (BRG/16) */
		} else
			*state |= 0x00000037; /* Clock mode 7b (BRG) */
		if (divider >> 22) {
			n = 63;
			m = 15;
		} else if (divider) {
			/* Extraction of the 6 highest weighted bits */
			m = 0;
			while (0xffffffc0 & divider) {
				m++;
				divider >>= 1;
			}
			n = divider;
		}
		brr = (m << 8) | n;
		divider = n << m;
		if (!(*state & 0x00000001)) /* ?b mode mask => clock mode 6b */
			divider <<= 4;
		*bps = xtal / divider;
	} else {
		/*
		 * External clock - DTE
		 * "state" already reflects Clock mode 0a (CCR0 = 0xzzzzzz00).
		 * Nothing more to be done
		 */
		brr = 0;
	}
	scc_writel(brr, dpriv, dev, BRR);
	ret = 0;
done:
	return ret;
}

static int dscc4_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	sync_serial_settings __user *line = ifr->ifr_settings.ifs_ifsu.sync;
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);
	const size_t size = sizeof(dpriv->settings);
	int ret = 0;

        if (dev->flags & IFF_UP)
                return -EBUSY;

	if (cmd != SIOCWANDEV)
		return -EOPNOTSUPP;

	switch(ifr->ifr_settings.type) {
	case IF_GET_IFACE:
		ifr->ifr_settings.type = IF_IFACE_SYNC_SERIAL;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(line, &dpriv->settings, size))
			return -EFAULT;
		break;

	case IF_IFACE_SYNC_SERIAL:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (dpriv->flags & FakeReset) {
			netdev_info(dev, "please reset the device before this command\n");
			return -EPERM;
		}
		if (copy_from_user(&dpriv->settings, line, size))
			return -EFAULT;
		ret = dscc4_set_iface(dpriv, dev);
		break;

	default:
		ret = hdlc_ioctl(dev, ifr, cmd);
		break;
	}

	return ret;
}

static int dscc4_match(const struct thingie *p, int value)
{
	int i;

	for (i = 0; p[i].define != -1; i++) {
		if (value == p[i].define)
			break;
	}
	if (p[i].define == -1)
		return -1;
	else
		return i;
}

static int dscc4_clock_setting(struct dscc4_dev_priv *dpriv,
			       struct net_device *dev)
{
	sync_serial_settings *settings = &dpriv->settings;
	int ret = -EOPNOTSUPP;
	u32 bps, state;

	bps = settings->clock_rate;
	state = scc_readl(dpriv, CCR0);
	if (dscc4_set_clock(dev, &bps, &state) < 0)
		goto done;
	if (bps) { /* DCE */
		printk(KERN_DEBUG "%s: generated RxClk (DCE)\n", dev->name);
		if (settings->clock_rate != bps) {
			printk(KERN_DEBUG "%s: clock adjusted (%08d -> %08d)\n",
				dev->name, settings->clock_rate, bps);
			settings->clock_rate = bps;
		}
	} else { /* DTE */
		state |= PowerUp | Vis;
		printk(KERN_DEBUG "%s: external RxClk (DTE)\n", dev->name);
	}
	scc_writel(state, dpriv, dev, CCR0);
	ret = 0;
done:
	return ret;
}

static int dscc4_encoding_setting(struct dscc4_dev_priv *dpriv,
				  struct net_device *dev)
{
	static const struct thingie encoding[] = {
		{ ENCODING_NRZ,		0x00000000 },
		{ ENCODING_NRZI,	0x00200000 },
		{ ENCODING_FM_MARK,	0x00400000 },
		{ ENCODING_FM_SPACE,	0x00500000 },
		{ ENCODING_MANCHESTER,	0x00600000 },
		{ -1,			0}
	};
	int i, ret = 0;

	i = dscc4_match(encoding, dpriv->encoding);
	if (i >= 0)
		scc_patchl(EncodingMask, encoding[i].bits, dpriv, dev, CCR0);
	else
		ret = -EOPNOTSUPP;
	return ret;
}

static int dscc4_loopback_setting(struct dscc4_dev_priv *dpriv,
				  struct net_device *dev)
{
	sync_serial_settings *settings = &dpriv->settings;
	u32 state;

	state = scc_readl(dpriv, CCR1);
	if (settings->loopback) {
		printk(KERN_DEBUG "%s: loopback\n", dev->name);
		state |= 0x00000100;
	} else {
		printk(KERN_DEBUG "%s: normal\n", dev->name);
		state &= ~0x00000100;
	}
	scc_writel(state, dpriv, dev, CCR1);
	return 0;
}

static int dscc4_crc_setting(struct dscc4_dev_priv *dpriv,
			     struct net_device *dev)
{
	static const struct thingie crc[] = {
		{ PARITY_CRC16_PR0_CCITT,	0x00000010 },
		{ PARITY_CRC16_PR1_CCITT,	0x00000000 },
		{ PARITY_CRC32_PR0_CCITT,	0x00000011 },
		{ PARITY_CRC32_PR1_CCITT,	0x00000001 }
	};
	int i, ret = 0;

	i = dscc4_match(crc, dpriv->parity);
	if (i >= 0)
		scc_patchl(CrcMask, crc[i].bits, dpriv, dev, CCR1);
	else
		ret = -EOPNOTSUPP;
	return ret;
}

static int dscc4_set_iface(struct dscc4_dev_priv *dpriv, struct net_device *dev)
{
	struct {
		int (*action)(struct dscc4_dev_priv *, struct net_device *);
	} *p, do_setting[] = {
		{ dscc4_encoding_setting },
		{ dscc4_clock_setting },
		{ dscc4_loopback_setting },
		{ dscc4_crc_setting },
		{ NULL }
	};
	int ret = 0;

	for (p = do_setting; p->action; p++) {
		if ((ret = p->action(dpriv, dev)) < 0)
			break;
	}
	return ret;
}

static irqreturn_t dscc4_irq(int irq, void *token)
{
	struct dscc4_dev_priv *root = token;
	struct dscc4_pci_priv *priv;
	struct net_device *dev;
	void __iomem *ioaddr;
	u32 state;
	unsigned long flags;
	int i, handled = 1;

	priv = root->pci_priv;
	dev = dscc4_to_dev(root);

	spin_lock_irqsave(&priv->lock, flags);

	ioaddr = root->base_addr;

	state = readl(ioaddr + GSTAR);
	if (!state) {
		handled = 0;
		goto out;
	}
	if (debug > 3)
		printk(KERN_DEBUG "%s: GSTAR = 0x%08x\n", DRV_NAME, state);
	writel(state, ioaddr + GSTAR);

	if (state & Arf) {
		netdev_err(dev, "failure (Arf). Harass the maintainer\n");
		goto out;
	}
	state &= ~ArAck;
	if (state & Cfg) {
		if (debug > 0)
			printk(KERN_DEBUG "%s: CfgIV\n", DRV_NAME);
		if (priv->iqcfg[priv->cfg_cur++%IRQ_RING_SIZE] & cpu_to_le32(Arf))
			netdev_err(dev, "CFG failed\n");
		if (!(state &= ~Cfg))
			goto out;
	}
	if (state & RxEvt) {
		i = dev_per_card - 1;
		do {
			dscc4_rx_irq(priv, root + i);
		} while (--i >= 0);
		state &= ~RxEvt;
	}
	if (state & TxEvt) {
		i = dev_per_card - 1;
		do {
			dscc4_tx_irq(priv, root + i);
		} while (--i >= 0);
		state &= ~TxEvt;
	}
out:
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_RETVAL(handled);
}

static void dscc4_tx_irq(struct dscc4_pci_priv *ppriv,
				struct dscc4_dev_priv *dpriv)
{
	struct net_device *dev = dscc4_to_dev(dpriv);
	u32 state;
	int cur, loop = 0;

try:
	cur = dpriv->iqtx_current%IRQ_RING_SIZE;
	state = le32_to_cpu(dpriv->iqtx[cur]);
	if (!state) {
		if (debug > 4)
			printk(KERN_DEBUG "%s: Tx ISR = 0x%08x\n", dev->name,
			       state);
		if ((debug > 1) && (loop > 1))
			printk(KERN_DEBUG "%s: Tx irq loop=%d\n", dev->name, loop);
		if (loop && netif_queue_stopped(dev))
			if ((dpriv->tx_current - dpriv->tx_dirty)%TX_RING_SIZE)
				netif_wake_queue(dev);

		if (netif_running(dev) && dscc4_tx_quiescent(dpriv, dev) &&
		    !dscc4_tx_done(dpriv))
				dscc4_do_tx(dpriv, dev);
		return;
	}
	loop++;
	dpriv->iqtx[cur] = 0;
	dpriv->iqtx_current++;

	if (state_check(state, dpriv, dev, "Tx") < 0)
		return;

	if (state & SccEvt) {
		if (state & Alls) {
			struct sk_buff *skb;
			struct TxFD *tx_fd;

			if (debug > 2)
				dscc4_tx_print(dev, dpriv, "Alls");
			/*
			 * DataComplete can't be trusted for Tx completion.
			 * Cf errata DS5 p.8
			 */
			cur = dpriv->tx_dirty%TX_RING_SIZE;
			tx_fd = dpriv->tx_fd + cur;
			skb = dpriv->tx_skbuff[cur];
			if (skb) {
				pci_unmap_single(ppriv->pdev, le32_to_cpu(tx_fd->data),
						 skb->len, PCI_DMA_TODEVICE);
				if (tx_fd->state & FrameEnd) {
					dev->stats.tx_packets++;
					dev->stats.tx_bytes += skb->len;
				}
				dev_kfree_skb_irq(skb);
				dpriv->tx_skbuff[cur] = NULL;
				++dpriv->tx_dirty;
			} else {
				if (debug > 1)
					netdev_err(dev, "Tx: NULL skb %d\n",
						   cur);
			}
			/*
			 * If the driver ends sending crap on the wire, it
			 * will be way easier to diagnose than the (not so)
			 * random freeze induced by null sized tx frames.
			 */
			tx_fd->data = tx_fd->next;
			tx_fd->state = FrameEnd | TO_STATE_TX(2*DUMMY_SKB_SIZE);
			tx_fd->complete = 0x00000000;
			tx_fd->jiffies = 0;

			if (!(state &= ~Alls))
				goto try;
		}
		/*
		 * Transmit Data Underrun
		 */
		if (state & Xdu) {
			netdev_err(dev, "Tx Data Underrun. Ask maintainer\n");
			dpriv->flags = NeedIDT;
			/* Tx reset */
			writel(MTFi | Rdt,
			       dpriv->base_addr + 0x0c*dpriv->dev_id + CH0CFG);
			writel(Action, dpriv->base_addr + GCMDR);
			return;
		}
		if (state & Cts) {
			netdev_info(dev, "CTS transition\n");
			if (!(state &= ~Cts)) /* DEBUG */
				goto try;
		}
		if (state & Xmr) {
			/* Frame needs to be sent again - FIXME */
			netdev_err(dev, "Tx ReTx. Ask maintainer\n");
			if (!(state &= ~Xmr)) /* DEBUG */
				goto try;
		}
		if (state & Xpr) {
			void __iomem *scc_addr;
			unsigned long ring;
			int i;

			/*
			 * - the busy condition happens (sometimes);
			 * - it doesn't seem to make the handler unreliable.
			 */
			for (i = 1; i; i <<= 1) {
				if (!(scc_readl_star(dpriv, dev) & SccBusy))
					break;
			}
			if (!i)
				netdev_info(dev, "busy in irq\n");

			scc_addr = dpriv->base_addr + 0x0c*dpriv->dev_id;
			/* Keep this order: IDT before IDR */
			if (dpriv->flags & NeedIDT) {
				if (debug > 2)
					dscc4_tx_print(dev, dpriv, "Xpr");
				ring = dpriv->tx_fd_dma +
				       (dpriv->tx_dirty%TX_RING_SIZE)*
				       sizeof(struct TxFD);
				writel(ring, scc_addr + CH0BTDA);
				dscc4_do_tx(dpriv, dev);
				writel(MTFi | Idt, scc_addr + CH0CFG);
				if (dscc4_do_action(dev, "IDT") < 0)
					goto err_xpr;
				dpriv->flags &= ~NeedIDT;
			}
			if (dpriv->flags & NeedIDR) {
				ring = dpriv->rx_fd_dma +
				       (dpriv->rx_current%RX_RING_SIZE)*
				       sizeof(struct RxFD);
				writel(ring, scc_addr + CH0BRDA);
				dscc4_rx_update(dpriv, dev);
				writel(MTFi | Idr, scc_addr + CH0CFG);
				if (dscc4_do_action(dev, "IDR") < 0)
					goto err_xpr;
				dpriv->flags &= ~NeedIDR;
				smp_wmb();
				/* Activate receiver and misc */
				scc_writel(0x08050008, dpriv, dev, CCR2);
			}
		err_xpr:
			if (!(state &= ~Xpr))
				goto try;
		}
		if (state & Cd) {
			if (debug > 0)
				netdev_info(dev, "CD transition\n");
			if (!(state &= ~Cd)) /* DEBUG */
				goto try;
		}
	} else { /* ! SccEvt */
		if (state & Hi) {
#ifdef DSCC4_POLLING
			while (!dscc4_tx_poll(dpriv, dev));
#endif
			netdev_info(dev, "Tx Hi\n");
			state &= ~Hi;
		}
		if (state & Err) {
			netdev_info(dev, "Tx ERR\n");
			dev->stats.tx_errors++;
			state &= ~Err;
		}
	}
	goto try;
}

static void dscc4_rx_irq(struct dscc4_pci_priv *priv,
				    struct dscc4_dev_priv *dpriv)
{
	struct net_device *dev = dscc4_to_dev(dpriv);
	u32 state;
	int cur;

try:
	cur = dpriv->iqrx_current%IRQ_RING_SIZE;
	state = le32_to_cpu(dpriv->iqrx[cur]);
	if (!state)
		return;
	dpriv->iqrx[cur] = 0;
	dpriv->iqrx_current++;

	if (state_check(state, dpriv, dev, "Rx") < 0)
		return;

	if (!(state & SccEvt)){
		struct RxFD *rx_fd;

		if (debug > 4)
			printk(KERN_DEBUG "%s: Rx ISR = 0x%08x\n", dev->name,
			       state);
		state &= 0x00ffffff;
		if (state & Err) { /* Hold or reset */
			printk(KERN_DEBUG "%s: Rx ERR\n", dev->name);
			cur = dpriv->rx_current%RX_RING_SIZE;
			rx_fd = dpriv->rx_fd + cur;
			/*
			 * Presume we're not facing a DMAC receiver reset.
			 * As We use the rx size-filtering feature of the
			 * DSCC4, the beginning of a new frame is waiting in
			 * the rx fifo. I bet a Receive Data Overflow will
			 * happen most of time but let's try and avoid it.
			 * Btw (as for RDO) if one experiences ERR whereas
			 * the system looks rather idle, there may be a
			 * problem with latency. In this case, increasing
			 * RX_RING_SIZE may help.
			 */
			//while (dpriv->rx_needs_refill) {
				while (!(rx_fd->state1 & Hold)) {
					rx_fd++;
					cur++;
					if (!(cur = cur%RX_RING_SIZE))
						rx_fd = dpriv->rx_fd;
				}
				//dpriv->rx_needs_refill--;
				try_get_rx_skb(dpriv, dev);
				if (!rx_fd->data)
					goto try;
				rx_fd->state1 &= ~Hold;
				rx_fd->state2 = 0x00000000;
				rx_fd->end = cpu_to_le32(0xbabeface);
			//}
			goto try;
		}
		if (state & Fi) {
			dscc4_rx_skb(dpriv, dev);
			goto try;
		}
		if (state & Hi ) { /* HI bit */
			netdev_info(dev, "Rx Hi\n");
			state &= ~Hi;
			goto try;
		}
	} else { /* SccEvt */
		if (debug > 1) {
			//FIXME: verifier la presence de tous les evenements
		static struct {
			u32 mask;
			const char *irq_name;
		} evts[] = {
			{ 0x00008000, "TIN"},
			{ 0x00000020, "RSC"},
			{ 0x00000010, "PCE"},
			{ 0x00000008, "PLLA"},
			{ 0, NULL}
		}, *evt;

		for (evt = evts; evt->irq_name; evt++) {
			if (state & evt->mask) {
					printk(KERN_DEBUG "%s: %s\n",
						dev->name, evt->irq_name);
				if (!(state &= ~evt->mask))
					goto try;
			}
		}
		} else {
			if (!(state &= ~0x0000c03c))
				goto try;
		}
		if (state & Cts) {
			netdev_info(dev, "CTS transition\n");
			if (!(state &= ~Cts)) /* DEBUG */
				goto try;
		}
		/*
		 * Receive Data Overflow (FIXME: fscked)
		 */
		if (state & Rdo) {
			struct RxFD *rx_fd;
			void __iomem *scc_addr;
			int cur;

			//if (debug)
			//	dscc4_rx_dump(dpriv);
			scc_addr = dpriv->base_addr + 0x0c*dpriv->dev_id;

			scc_patchl(RxActivate, 0, dpriv, dev, CCR2);
			/*
			 * This has no effect. Why ?
			 * ORed with TxSccRes, one sees the CFG ack (for
			 * the TX part only).
			 */
			scc_writel(RxSccRes, dpriv, dev, CMDR);
			dpriv->flags |= RdoSet;

			/*
			 * Let's try and save something in the received data.
			 * rx_current must be incremented at least once to
			 * avoid HOLD in the BRDA-to-be-pointed desc.
			 */
			do {
				cur = dpriv->rx_current++%RX_RING_SIZE;
				rx_fd = dpriv->rx_fd + cur;
				if (!(rx_fd->state2 & DataComplete))
					break;
				if (rx_fd->state2 & FrameAborted) {
					dev->stats.rx_over_errors++;
					rx_fd->state1 |= Hold;
					rx_fd->state2 = 0x00000000;
					rx_fd->end = cpu_to_le32(0xbabeface);
				} else
					dscc4_rx_skb(dpriv, dev);
			} while (1);

			if (debug > 0) {
				if (dpriv->flags & RdoSet)
					printk(KERN_DEBUG
					       "%s: no RDO in Rx data\n", DRV_NAME);
			}
#ifdef DSCC4_RDO_EXPERIMENTAL_RECOVERY
			/*
			 * FIXME: must the reset be this violent ?
			 */
#warning "FIXME: CH0BRDA"
			writel(dpriv->rx_fd_dma +
			       (dpriv->rx_current%RX_RING_SIZE)*
			       sizeof(struct RxFD), scc_addr + CH0BRDA);
			writel(MTFi|Rdr|Idr, scc_addr + CH0CFG);
			if (dscc4_do_action(dev, "RDR") < 0) {
				netdev_err(dev, "RDO recovery failed(RDR)\n");
				goto rdo_end;
			}
			writel(MTFi|Idr, scc_addr + CH0CFG);
			if (dscc4_do_action(dev, "IDR") < 0) {
				netdev_err(dev, "RDO recovery failed(IDR)\n");
				goto rdo_end;
			}
		rdo_end:
#endif
			scc_patchl(0, RxActivate, dpriv, dev, CCR2);
			goto try;
		}
		if (state & Cd) {
			netdev_info(dev, "CD transition\n");
			if (!(state &= ~Cd)) /* DEBUG */
				goto try;
		}
		if (state & Flex) {
			printk(KERN_DEBUG "%s: Flex. Ttttt...\n", DRV_NAME);
			if (!(state &= ~Flex))
				goto try;
		}
	}
}

/*
 * I had expected the following to work for the first descriptor
 * (tx_fd->state = 0xc0000000)
 * - Hold=1 (don't try and branch to the next descripto);
 * - No=0 (I want an empty data section, i.e. size=0);
 * - Fe=1 (required by No=0 or we got an Err irq and must reset).
 * It failed and locked solid. Thus the introduction of a dummy skb.
 * Problem is acknowledged in errata sheet DS5. Joy :o/
 */
static struct sk_buff *dscc4_init_dummy_skb(struct dscc4_dev_priv *dpriv)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(DUMMY_SKB_SIZE);
	if (skb) {
		int last = dpriv->tx_dirty%TX_RING_SIZE;
		struct TxFD *tx_fd = dpriv->tx_fd + last;

		skb->len = DUMMY_SKB_SIZE;
		skb_copy_to_linear_data(skb, version,
					strlen(version) % DUMMY_SKB_SIZE);
		tx_fd->state = FrameEnd | TO_STATE_TX(DUMMY_SKB_SIZE);
		tx_fd->data = cpu_to_le32(pci_map_single(dpriv->pci_priv->pdev,
					     skb->data, DUMMY_SKB_SIZE,
					     PCI_DMA_TODEVICE));
		dpriv->tx_skbuff[last] = skb;
	}
	return skb;
}

static int dscc4_init_ring(struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);
	struct pci_dev *pdev = dpriv->pci_priv->pdev;
	struct TxFD *tx_fd;
	struct RxFD *rx_fd;
	void *ring;
	int i;

	ring = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &dpriv->rx_fd_dma);
	if (!ring)
		goto err_out;
	dpriv->rx_fd = rx_fd = (struct RxFD *) ring;

	ring = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &dpriv->tx_fd_dma);
	if (!ring)
		goto err_free_dma_rx;
	dpriv->tx_fd = tx_fd = (struct TxFD *) ring;

	memset(dpriv->tx_skbuff, 0, sizeof(struct sk_buff *)*TX_RING_SIZE);
	dpriv->tx_dirty = 0xffffffff;
	i = dpriv->tx_current = 0;
	do {
		tx_fd->state = FrameEnd | TO_STATE_TX(2*DUMMY_SKB_SIZE);
		tx_fd->complete = 0x00000000;
	        /* FIXME: NULL should be ok - to be tried */
	        tx_fd->data = cpu_to_le32(dpriv->tx_fd_dma);
		(tx_fd++)->next = cpu_to_le32(dpriv->tx_fd_dma +
					(++i%TX_RING_SIZE)*sizeof(*tx_fd));
	} while (i < TX_RING_SIZE);

	if (!dscc4_init_dummy_skb(dpriv))
		goto err_free_dma_tx;

	memset(dpriv->rx_skbuff, 0, sizeof(struct sk_buff *)*RX_RING_SIZE);
	i = dpriv->rx_dirty = dpriv->rx_current = 0;
	do {
		/* size set by the host. Multiple of 4 bytes please */
	        rx_fd->state1 = HiDesc;
	        rx_fd->state2 = 0x00000000;
	        rx_fd->end = cpu_to_le32(0xbabeface);
	        rx_fd->state1 |= TO_STATE_RX(HDLC_MAX_MRU);
		// FIXME: return value verifiee mais traitement suspect
		if (try_get_rx_skb(dpriv, dev) >= 0)
			dpriv->rx_dirty++;
		(rx_fd++)->next = cpu_to_le32(dpriv->rx_fd_dma +
					(++i%RX_RING_SIZE)*sizeof(*rx_fd));
	} while (i < RX_RING_SIZE);

	return 0;

err_free_dma_tx:
	pci_free_consistent(pdev, TX_TOTAL_SIZE, ring, dpriv->tx_fd_dma);
err_free_dma_rx:
	pci_free_consistent(pdev, RX_TOTAL_SIZE, rx_fd, dpriv->rx_fd_dma);
err_out:
	return -ENOMEM;
}

static void __devexit dscc4_remove_one(struct pci_dev *pdev)
{
	struct dscc4_pci_priv *ppriv;
	struct dscc4_dev_priv *root;
	void __iomem *ioaddr;
	int i;

	ppriv = pci_get_drvdata(pdev);
	root = ppriv->root;

	ioaddr = root->base_addr;

	dscc4_pci_reset(pdev, ioaddr);

	free_irq(pdev->irq, root);
	pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), ppriv->iqcfg,
			    ppriv->iqcfg_dma);
	for (i = 0; i < dev_per_card; i++) {
		struct dscc4_dev_priv *dpriv = root + i;

		dscc4_release_ring(dpriv);
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32),
				    dpriv->iqrx, dpriv->iqrx_dma);
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32),
				    dpriv->iqtx, dpriv->iqtx_dma);
	}

	dscc4_free1(pdev);

	iounmap(ioaddr);

	pci_release_region(pdev, 1);
	pci_release_region(pdev, 0);

	pci_disable_device(pdev);
}

static int dscc4_hdlc_attach(struct net_device *dev, unsigned short encoding,
	unsigned short parity)
{
	struct dscc4_dev_priv *dpriv = dscc4_priv(dev);

	if (encoding != ENCODING_NRZ &&
	    encoding != ENCODING_NRZI &&
	    encoding != ENCODING_FM_MARK &&
	    encoding != ENCODING_FM_SPACE &&
	    encoding != ENCODING_MANCHESTER)
		return -EINVAL;

	if (parity != PARITY_NONE &&
	    parity != PARITY_CRC16_PR0_CCITT &&
	    parity != PARITY_CRC16_PR1_CCITT &&
	    parity != PARITY_CRC32_PR0_CCITT &&
	    parity != PARITY_CRC32_PR1_CCITT)
		return -EINVAL;

        dpriv->encoding = encoding;
        dpriv->parity = parity;
	return 0;
}

#ifndef MODULE
static int __init dscc4_setup(char *str)
{
	int *args[] = { &debug, &quartz, NULL }, **p = args;

	while (*p && (get_option(&str, *p) == 2))
		p++;
	return 1;
}

__setup("dscc4.setup=", dscc4_setup);
#endif

static DEFINE_PCI_DEVICE_TABLE(dscc4_pci_tbl) = {
	{ PCI_VENDOR_ID_SIEMENS, PCI_DEVICE_ID_SIEMENS_DSCC4,
	        PCI_ANY_ID, PCI_ANY_ID, },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, dscc4_pci_tbl);

static struct pci_driver dscc4_driver = {
	.name		= DRV_NAME,
	.id_table	= dscc4_pci_tbl,
	.probe		= dscc4_init_one,
	.remove		= __devexit_p(dscc4_remove_one),
};

module_pci_driver(dscc4_driver);
