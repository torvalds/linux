/*
 *  Driver for the IDT RC32434 (Korina) on-chip ethernet controller.
 *
 *  Copyright 2004 IDT Inc. (rischelp@idt.com)
 *  Copyright 2006 Felix Fietkau <nbd@openwrt.org>
 *  Copyright 2008 Florian Fainelli <florian@openwrt.org>
 *  Copyright 2017 Roman Yeryomin <roman@advem.lv>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Writing to a DMA status register:
 *
 *  When writing to the status register, you should mask the bit you have
 *  been testing the status register with. Both Tx and Rx DMA registers
 *  should stick to this procedure.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/in.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/pgtable.h>
#include <linux/clk.h>

#define DRV_NAME	"korina"
#define DRV_VERSION	"0.20"
#define DRV_RELDATE	"15Sep2017"

struct eth_regs {
	u32 ethintfc;
	u32 ethfifott;
	u32 etharc;
	u32 ethhash0;
	u32 ethhash1;
	u32 ethu0[4];		/* Reserved. */
	u32 ethpfs;
	u32 ethmcp;
	u32 eth_u1[10];		/* Reserved. */
	u32 ethspare;
	u32 eth_u2[42];		/* Reserved. */
	u32 ethsal0;
	u32 ethsah0;
	u32 ethsal1;
	u32 ethsah1;
	u32 ethsal2;
	u32 ethsah2;
	u32 ethsal3;
	u32 ethsah3;
	u32 ethrbc;
	u32 ethrpc;
	u32 ethrupc;
	u32 ethrfc;
	u32 ethtbc;
	u32 ethgpf;
	u32 eth_u9[50];		/* Reserved. */
	u32 ethmac1;
	u32 ethmac2;
	u32 ethipgt;
	u32 ethipgr;
	u32 ethclrt;
	u32 ethmaxf;
	u32 eth_u10;		/* Reserved. */
	u32 ethmtest;
	u32 miimcfg;
	u32 miimcmd;
	u32 miimaddr;
	u32 miimwtd;
	u32 miimrdd;
	u32 miimind;
	u32 eth_u11;		/* Reserved. */
	u32 eth_u12;		/* Reserved. */
	u32 ethcfsa0;
	u32 ethcfsa1;
	u32 ethcfsa2;
};

/* Ethernet interrupt registers */
#define ETH_INT_FC_EN		BIT(0)
#define ETH_INT_FC_ITS		BIT(1)
#define ETH_INT_FC_RIP		BIT(2)
#define ETH_INT_FC_JAM		BIT(3)
#define ETH_INT_FC_OVR		BIT(4)
#define ETH_INT_FC_UND		BIT(5)
#define ETH_INT_FC_IOC		0x000000c0

/* Ethernet FIFO registers */
#define ETH_FIFI_TT_TTH_BIT	0
#define ETH_FIFO_TT_TTH		0x0000007f

/* Ethernet ARC/multicast registers */
#define ETH_ARC_PRO		BIT(0)
#define ETH_ARC_AM		BIT(1)
#define ETH_ARC_AFM		BIT(2)
#define ETH_ARC_AB		BIT(3)

/* Ethernet SAL registers */
#define ETH_SAL_BYTE_5		0x000000ff
#define ETH_SAL_BYTE_4		0x0000ff00
#define ETH_SAL_BYTE_3		0x00ff0000
#define ETH_SAL_BYTE_2		0xff000000

/* Ethernet SAH registers */
#define ETH_SAH_BYTE1		0x000000ff
#define ETH_SAH_BYTE0		0x0000ff00

/* Ethernet GPF register */
#define ETH_GPF_PTV		0x0000ffff

/* Ethernet PFG register */
#define ETH_PFS_PFD		BIT(0)

/* Ethernet CFSA[0-3] registers */
#define ETH_CFSA0_CFSA4		0x000000ff
#define ETH_CFSA0_CFSA5		0x0000ff00
#define ETH_CFSA1_CFSA2		0x000000ff
#define ETH_CFSA1_CFSA3		0x0000ff00
#define ETH_CFSA1_CFSA0		0x000000ff
#define ETH_CFSA1_CFSA1		0x0000ff00

/* Ethernet MAC1 registers */
#define ETH_MAC1_RE		BIT(0)
#define ETH_MAC1_PAF		BIT(1)
#define ETH_MAC1_RFC		BIT(2)
#define ETH_MAC1_TFC		BIT(3)
#define ETH_MAC1_LB		BIT(4)
#define ETH_MAC1_MR		BIT(31)

/* Ethernet MAC2 registers */
#define ETH_MAC2_FD		BIT(0)
#define ETH_MAC2_FLC		BIT(1)
#define ETH_MAC2_HFE		BIT(2)
#define ETH_MAC2_DC		BIT(3)
#define ETH_MAC2_CEN		BIT(4)
#define ETH_MAC2_PE		BIT(5)
#define ETH_MAC2_VPE		BIT(6)
#define ETH_MAC2_APE		BIT(7)
#define ETH_MAC2_PPE		BIT(8)
#define ETH_MAC2_LPE		BIT(9)
#define ETH_MAC2_NB		BIT(12)
#define ETH_MAC2_BP		BIT(13)
#define ETH_MAC2_ED		BIT(14)

/* Ethernet IPGT register */
#define ETH_IPGT		0x0000007f

/* Ethernet IPGR registers */
#define ETH_IPGR_IPGR2		0x0000007f
#define ETH_IPGR_IPGR1		0x00007f00

/* Ethernet CLRT registers */
#define ETH_CLRT_MAX_RET	0x0000000f
#define ETH_CLRT_COL_WIN	0x00003f00

/* Ethernet MAXF register */
#define ETH_MAXF		0x0000ffff

/* Ethernet test registers */
#define ETH_TEST_REG		BIT(2)
#define ETH_MCP_DIV		0x000000ff

/* MII registers */
#define ETH_MII_CFG_RSVD	0x0000000c
#define ETH_MII_CMD_RD		BIT(0)
#define ETH_MII_CMD_SCN		BIT(1)
#define ETH_MII_REG_ADDR	0x0000001f
#define ETH_MII_PHY_ADDR	0x00001f00
#define ETH_MII_WTD_DATA	0x0000ffff
#define ETH_MII_RDD_DATA	0x0000ffff
#define ETH_MII_IND_BSY		BIT(0)
#define ETH_MII_IND_SCN		BIT(1)
#define ETH_MII_IND_NV		BIT(2)

/* Values for the DEVCS field of the Ethernet DMA Rx and Tx descriptors. */
#define ETH_RX_FD		BIT(0)
#define ETH_RX_LD		BIT(1)
#define ETH_RX_ROK		BIT(2)
#define ETH_RX_FM		BIT(3)
#define ETH_RX_MP		BIT(4)
#define ETH_RX_BP		BIT(5)
#define ETH_RX_VLT		BIT(6)
#define ETH_RX_CF		BIT(7)
#define ETH_RX_OVR		BIT(8)
#define ETH_RX_CRC		BIT(9)
#define ETH_RX_CV		BIT(10)
#define ETH_RX_DB		BIT(11)
#define ETH_RX_LE		BIT(12)
#define ETH_RX_LOR		BIT(13)
#define ETH_RX_CES		BIT(14)
#define ETH_RX_LEN_BIT		16
#define ETH_RX_LEN		0xffff0000

#define ETH_TX_FD		BIT(0)
#define ETH_TX_LD		BIT(1)
#define ETH_TX_OEN		BIT(2)
#define ETH_TX_PEN		BIT(3)
#define ETH_TX_CEN		BIT(4)
#define ETH_TX_HEN		BIT(5)
#define ETH_TX_TOK		BIT(6)
#define ETH_TX_MP		BIT(7)
#define ETH_TX_BP		BIT(8)
#define ETH_TX_UND		BIT(9)
#define ETH_TX_OF		BIT(10)
#define ETH_TX_ED		BIT(11)
#define ETH_TX_EC		BIT(12)
#define ETH_TX_LC		BIT(13)
#define ETH_TX_TD		BIT(14)
#define ETH_TX_CRC		BIT(15)
#define ETH_TX_LE		BIT(16)
#define ETH_TX_CC		0x001E0000

/* DMA descriptor (in physical memory). */
struct dma_desc {
	u32 control;			/* Control. use DMAD_* */
	u32 ca;				/* Current Address. */
	u32 devcs;			/* Device control and status. */
	u32 link;			/* Next descriptor in chain. */
};

#define DMA_DESC_COUNT_BIT		0
#define DMA_DESC_COUNT_MSK		0x0003ffff
#define DMA_DESC_DS_BIT			20
#define DMA_DESC_DS_MSK			0x00300000

#define DMA_DESC_DEV_CMD_BIT		22
#define DMA_DESC_DEV_CMD_MSK		0x01c00000

/* DMA descriptors interrupts */
#define DMA_DESC_COF			BIT(25) /* Chain on finished */
#define DMA_DESC_COD			BIT(26) /* Chain on done */
#define DMA_DESC_IOF			BIT(27) /* Interrupt on finished */
#define DMA_DESC_IOD			BIT(28) /* Interrupt on done */
#define DMA_DESC_TERM			BIT(29) /* Terminated */
#define DMA_DESC_DONE			BIT(30) /* Done */
#define DMA_DESC_FINI			BIT(31) /* Finished */

/* DMA register (within Internal Register Map).  */
struct dma_reg {
	u32 dmac;		/* Control. */
	u32 dmas;		/* Status. */
	u32 dmasm;		/* Mask. */
	u32 dmadptr;		/* Descriptor pointer. */
	u32 dmandptr;		/* Next descriptor pointer. */
};

/* DMA channels specific registers */
#define DMA_CHAN_RUN_BIT		BIT(0)
#define DMA_CHAN_DONE_BIT		BIT(1)
#define DMA_CHAN_MODE_BIT		BIT(2)
#define DMA_CHAN_MODE_MSK		0x0000000c
#define	 DMA_CHAN_MODE_AUTO		0
#define	 DMA_CHAN_MODE_BURST		1
#define	 DMA_CHAN_MODE_XFRT		2
#define	 DMA_CHAN_MODE_RSVD		3
#define DMA_CHAN_ACT_BIT		BIT(4)

/* DMA status registers */
#define DMA_STAT_FINI			BIT(0)
#define DMA_STAT_DONE			BIT(1)
#define DMA_STAT_CHAIN			BIT(2)
#define DMA_STAT_ERR			BIT(3)
#define DMA_STAT_HALT			BIT(4)

#define STATION_ADDRESS_HIGH(dev) (((dev)->dev_addr[0] << 8) | \
				   ((dev)->dev_addr[1]))
#define STATION_ADDRESS_LOW(dev)  (((dev)->dev_addr[2] << 24) | \
				   ((dev)->dev_addr[3] << 16) | \
				   ((dev)->dev_addr[4] << 8)  | \
				   ((dev)->dev_addr[5]))

#define MII_CLOCK	1250000 /* no more than 2.5MHz */

/* the following must be powers of two */
#define KORINA_NUM_RDS	64  /* number of receive descriptors */
#define KORINA_NUM_TDS	64  /* number of transmit descriptors */

/* KORINA_RBSIZE is the hardware's default maximum receive
 * frame size in bytes. Having this hardcoded means that there
 * is no support for MTU sizes greater than 1500. */
#define KORINA_RBSIZE	1536 /* size of one resource buffer = Ether MTU */
#define KORINA_RDS_MASK	(KORINA_NUM_RDS - 1)
#define KORINA_TDS_MASK	(KORINA_NUM_TDS - 1)
#define RD_RING_SIZE	(KORINA_NUM_RDS * sizeof(struct dma_desc))
#define TD_RING_SIZE	(KORINA_NUM_TDS * sizeof(struct dma_desc))

#define TX_TIMEOUT	(6000 * HZ / 1000)

enum chain_status {
	desc_filled,
	desc_is_empty
};

#define DMA_COUNT(count)	((count) & DMA_DESC_COUNT_MSK)
#define IS_DMA_FINISHED(X)	(((X) & (DMA_DESC_FINI)) != 0)
#define IS_DMA_DONE(X)		(((X) & (DMA_DESC_DONE)) != 0)
#define RCVPKT_LENGTH(X)	(((X) & ETH_RX_LEN) >> ETH_RX_LEN_BIT)

/* Information that need to be kept for each board. */
struct korina_private {
	struct eth_regs __iomem *eth_regs;
	struct dma_reg __iomem *rx_dma_regs;
	struct dma_reg __iomem *tx_dma_regs;
	struct dma_desc *td_ring; /* transmit descriptor ring */
	struct dma_desc *rd_ring; /* receive descriptor ring  */
	dma_addr_t td_dma;
	dma_addr_t rd_dma;

	struct sk_buff *tx_skb[KORINA_NUM_TDS];
	struct sk_buff *rx_skb[KORINA_NUM_RDS];

	dma_addr_t rx_skb_dma[KORINA_NUM_RDS];
	dma_addr_t tx_skb_dma[KORINA_NUM_TDS];

	int rx_next_done;
	int rx_chain_head;
	int rx_chain_tail;
	enum chain_status rx_chain_status;

	int tx_next_done;
	int tx_chain_head;
	int tx_chain_tail;
	enum chain_status tx_chain_status;
	int tx_count;
	int tx_full;

	int rx_irq;
	int tx_irq;

	spinlock_t lock;	/* NIC xmit lock */

	int dma_halt_cnt;
	int dma_run_cnt;
	struct napi_struct napi;
	struct timer_list media_check_timer;
	struct mii_if_info mii_if;
	struct work_struct restart_task;
	struct net_device *dev;
	struct device *dmadev;
	int mii_clock_freq;
};

static dma_addr_t korina_tx_dma(struct korina_private *lp, int idx)
{
	return lp->td_dma + (idx * sizeof(struct dma_desc));
}

static dma_addr_t korina_rx_dma(struct korina_private *lp, int idx)
{
	return lp->rd_dma + (idx * sizeof(struct dma_desc));
}

static inline void korina_abort_dma(struct net_device *dev,
					struct dma_reg *ch)
{
	if (readl(&ch->dmac) & DMA_CHAN_RUN_BIT) {
		writel(0x10, &ch->dmac);

		while (!(readl(&ch->dmas) & DMA_STAT_HALT))
			netif_trans_update(dev);

		writel(0, &ch->dmas);
	}

	writel(0, &ch->dmadptr);
	writel(0, &ch->dmandptr);
}

static void korina_abort_tx(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);

	korina_abort_dma(dev, lp->tx_dma_regs);
}

static void korina_abort_rx(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);

	korina_abort_dma(dev, lp->rx_dma_regs);
}

/* transmit packet */
static int korina_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);
	u32 chain_prev, chain_next;
	unsigned long flags;
	struct dma_desc *td;
	dma_addr_t ca;
	u32 length;
	int idx;

	spin_lock_irqsave(&lp->lock, flags);

	idx = lp->tx_chain_tail;
	td = &lp->td_ring[idx];

	/* stop queue when full, drop pkts if queue already full */
	if (lp->tx_count >= (KORINA_NUM_TDS - 2)) {
		lp->tx_full = 1;

		if (lp->tx_count == (KORINA_NUM_TDS - 2))
			netif_stop_queue(dev);
		else
			goto drop_packet;
	}

	lp->tx_count++;

	lp->tx_skb[idx] = skb;

	length = skb->len;

	/* Setup the transmit descriptor. */
	ca = dma_map_single(lp->dmadev, skb->data, length, DMA_TO_DEVICE);
	if (dma_mapping_error(lp->dmadev, ca))
		goto drop_packet;

	lp->tx_skb_dma[idx] = ca;
	td->ca = ca;

	chain_prev = (idx - 1) & KORINA_TDS_MASK;
	chain_next = (idx + 1) & KORINA_TDS_MASK;

	if (readl(&(lp->tx_dma_regs->dmandptr)) == 0) {
		if (lp->tx_chain_status == desc_is_empty) {
			/* Update tail */
			td->control = DMA_COUNT(length) |
					DMA_DESC_COF | DMA_DESC_IOF;
			/* Move tail */
			lp->tx_chain_tail = chain_next;
			/* Write to NDPTR */
			writel(korina_tx_dma(lp, lp->tx_chain_head),
			       &lp->tx_dma_regs->dmandptr);
			/* Move head to tail */
			lp->tx_chain_head = lp->tx_chain_tail;
		} else {
			/* Update tail */
			td->control = DMA_COUNT(length) |
					DMA_DESC_COF | DMA_DESC_IOF;
			/* Link to prev */
			lp->td_ring[chain_prev].control &=
					~DMA_DESC_COF;
			/* Link to prev */
			lp->td_ring[chain_prev].link = korina_tx_dma(lp, idx);
			/* Move tail */
			lp->tx_chain_tail = chain_next;
			/* Write to NDPTR */
			writel(korina_tx_dma(lp, lp->tx_chain_head),
			       &lp->tx_dma_regs->dmandptr);
			/* Move head to tail */
			lp->tx_chain_head = lp->tx_chain_tail;
			lp->tx_chain_status = desc_is_empty;
		}
	} else {
		if (lp->tx_chain_status == desc_is_empty) {
			/* Update tail */
			td->control = DMA_COUNT(length) |
					DMA_DESC_COF | DMA_DESC_IOF;
			/* Move tail */
			lp->tx_chain_tail = chain_next;
			lp->tx_chain_status = desc_filled;
		} else {
			/* Update tail */
			td->control = DMA_COUNT(length) |
					DMA_DESC_COF | DMA_DESC_IOF;
			lp->td_ring[chain_prev].control &=
					~DMA_DESC_COF;
			lp->td_ring[chain_prev].link = korina_tx_dma(lp, idx);
			lp->tx_chain_tail = chain_next;
		}
	}

	netif_trans_update(dev);
	spin_unlock_irqrestore(&lp->lock, flags);

	return NETDEV_TX_OK;

drop_packet:
	dev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	spin_unlock_irqrestore(&lp->lock, flags);

	return NETDEV_TX_OK;
}

static int korina_mdio_wait(struct korina_private *lp)
{
	u32 value;

	return readl_poll_timeout_atomic(&lp->eth_regs->miimind,
					 value, value & ETH_MII_IND_BSY,
					 1, 1000);
}

static int korina_mdio_read(struct net_device *dev, int phy, int reg)
{
	struct korina_private *lp = netdev_priv(dev);
	int ret;

	ret = korina_mdio_wait(lp);
	if (ret < 0)
		return ret;

	writel(phy << 8 | reg, &lp->eth_regs->miimaddr);
	writel(1, &lp->eth_regs->miimcmd);

	ret = korina_mdio_wait(lp);
	if (ret < 0)
		return ret;

	if (readl(&lp->eth_regs->miimind) & ETH_MII_IND_NV)
		return -EINVAL;

	ret = readl(&lp->eth_regs->miimrdd);
	writel(0, &lp->eth_regs->miimcmd);
	return ret;
}

static void korina_mdio_write(struct net_device *dev, int phy, int reg, int val)
{
	struct korina_private *lp = netdev_priv(dev);

	if (korina_mdio_wait(lp))
		return;

	writel(0, &lp->eth_regs->miimcmd);
	writel(phy << 8 | reg, &lp->eth_regs->miimaddr);
	writel(val, &lp->eth_regs->miimwtd);
}

/* Ethernet Rx DMA interrupt */
static irqreturn_t korina_rx_dma_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct korina_private *lp = netdev_priv(dev);
	u32 dmas, dmasm;
	irqreturn_t retval;

	dmas = readl(&lp->rx_dma_regs->dmas);
	if (dmas & (DMA_STAT_DONE | DMA_STAT_HALT | DMA_STAT_ERR)) {
		dmasm = readl(&lp->rx_dma_regs->dmasm);
		writel(dmasm | (DMA_STAT_DONE |
				DMA_STAT_HALT | DMA_STAT_ERR),
				&lp->rx_dma_regs->dmasm);

		napi_schedule(&lp->napi);

		if (dmas & DMA_STAT_ERR)
			printk(KERN_ERR "%s: DMA error\n", dev->name);

		retval = IRQ_HANDLED;
	} else
		retval = IRQ_NONE;

	return retval;
}

static int korina_rx(struct net_device *dev, int limit)
{
	struct korina_private *lp = netdev_priv(dev);
	struct dma_desc *rd = &lp->rd_ring[lp->rx_next_done];
	struct sk_buff *skb, *skb_new;
	u32 devcs, pkt_len, dmas;
	dma_addr_t ca;
	int count;

	for (count = 0; count < limit; count++) {
		skb = lp->rx_skb[lp->rx_next_done];
		skb_new = NULL;

		devcs = rd->devcs;

		if ((KORINA_RBSIZE - (u32)DMA_COUNT(rd->control)) == 0)
			break;

		/* check that this is a whole packet
		 * WARNING: DMA_FD bit incorrectly set
		 * in Rc32434 (errata ref #077) */
		if (!(devcs & ETH_RX_LD))
			goto next;

		if (!(devcs & ETH_RX_ROK)) {
			/* Update statistics counters */
			dev->stats.rx_errors++;
			dev->stats.rx_dropped++;
			if (devcs & ETH_RX_CRC)
				dev->stats.rx_crc_errors++;
			if (devcs & ETH_RX_LE)
				dev->stats.rx_length_errors++;
			if (devcs & ETH_RX_OVR)
				dev->stats.rx_fifo_errors++;
			if (devcs & ETH_RX_CV)
				dev->stats.rx_frame_errors++;
			if (devcs & ETH_RX_CES)
				dev->stats.rx_frame_errors++;

			goto next;
		}

		/* Malloc up new buffer. */
		skb_new = netdev_alloc_skb_ip_align(dev, KORINA_RBSIZE);
		if (!skb_new)
			break;

		ca = dma_map_single(lp->dmadev, skb_new->data, KORINA_RBSIZE,
				    DMA_FROM_DEVICE);
		if (dma_mapping_error(lp->dmadev, ca)) {
			dev_kfree_skb_any(skb_new);
			break;
		}

		pkt_len = RCVPKT_LENGTH(devcs);
		dma_unmap_single(lp->dmadev, lp->rx_skb_dma[lp->rx_next_done],
				 pkt_len, DMA_FROM_DEVICE);

		/* Do not count the CRC */
		skb_put(skb, pkt_len - 4);
		skb->protocol = eth_type_trans(skb, dev);

		/* Pass the packet to upper layers */
		napi_gro_receive(&lp->napi, skb);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += pkt_len;

		/* Update the mcast stats */
		if (devcs & ETH_RX_MP)
			dev->stats.multicast++;

		lp->rx_skb[lp->rx_next_done] = skb_new;
		lp->rx_skb_dma[lp->rx_next_done] = ca;

next:
		rd->devcs = 0;

		/* Restore descriptor's curr_addr */
		rd->ca = lp->rx_skb_dma[lp->rx_next_done];

		rd->control = DMA_COUNT(KORINA_RBSIZE) |
			DMA_DESC_COD | DMA_DESC_IOD;
		lp->rd_ring[(lp->rx_next_done - 1) &
			KORINA_RDS_MASK].control &=
			~DMA_DESC_COD;

		lp->rx_next_done = (lp->rx_next_done + 1) & KORINA_RDS_MASK;
		rd = &lp->rd_ring[lp->rx_next_done];
		writel((u32)~DMA_STAT_DONE, &lp->rx_dma_regs->dmas);
	}

	dmas = readl(&lp->rx_dma_regs->dmas);

	if (dmas & DMA_STAT_HALT) {
		writel((u32)~(DMA_STAT_HALT | DMA_STAT_ERR),
		       &lp->rx_dma_regs->dmas);

		lp->dma_halt_cnt++;
		rd->devcs = 0;
		rd->ca = lp->rx_skb_dma[lp->rx_next_done];
		writel(korina_rx_dma(lp, rd - lp->rd_ring),
		       &lp->rx_dma_regs->dmandptr);
	}

	return count;
}

static int korina_poll(struct napi_struct *napi, int budget)
{
	struct korina_private *lp =
		container_of(napi, struct korina_private, napi);
	struct net_device *dev = lp->dev;
	int work_done;

	work_done = korina_rx(dev, budget);
	if (work_done < budget) {
		napi_complete_done(napi, work_done);

		writel(readl(&lp->rx_dma_regs->dmasm) &
			~(DMA_STAT_DONE | DMA_STAT_HALT | DMA_STAT_ERR),
			&lp->rx_dma_regs->dmasm);
	}
	return work_done;
}

/*
 * Set or clear the multicast filter for this adaptor.
 */
static void korina_multicast_list(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);
	unsigned long flags;
	struct netdev_hw_addr *ha;
	u32 recognise = ETH_ARC_AB;	/* always accept broadcasts */

	/* Set promiscuous mode */
	if (dev->flags & IFF_PROMISC)
		recognise |= ETH_ARC_PRO;

	else if ((dev->flags & IFF_ALLMULTI) || (netdev_mc_count(dev) > 4))
		/* All multicast and broadcast */
		recognise |= ETH_ARC_AM;

	/* Build the hash table */
	if (netdev_mc_count(dev) > 4) {
		u16 hash_table[4] = { 0 };
		u32 crc;

		netdev_for_each_mc_addr(ha, dev) {
			crc = ether_crc_le(6, ha->addr);
			crc >>= 26;
			hash_table[crc >> 4] |= 1 << (15 - (crc & 0xf));
		}
		/* Accept filtered multicast */
		recognise |= ETH_ARC_AFM;

		/* Fill the MAC hash tables with their values */
		writel((u32)(hash_table[1] << 16 | hash_table[0]),
					&lp->eth_regs->ethhash0);
		writel((u32)(hash_table[3] << 16 | hash_table[2]),
					&lp->eth_regs->ethhash1);
	}

	spin_lock_irqsave(&lp->lock, flags);
	writel(recognise, &lp->eth_regs->etharc);
	spin_unlock_irqrestore(&lp->lock, flags);
}

static void korina_tx(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);
	struct dma_desc *td = &lp->td_ring[lp->tx_next_done];
	u32 devcs;
	u32 dmas;

	spin_lock(&lp->lock);

	/* Process all desc that are done */
	while (IS_DMA_FINISHED(td->control)) {
		if (lp->tx_full == 1) {
			netif_wake_queue(dev);
			lp->tx_full = 0;
		}

		devcs = lp->td_ring[lp->tx_next_done].devcs;
		if ((devcs & (ETH_TX_FD | ETH_TX_LD)) !=
				(ETH_TX_FD | ETH_TX_LD)) {
			dev->stats.tx_errors++;
			dev->stats.tx_dropped++;

			/* Should never happen */
			printk(KERN_ERR "%s: split tx ignored\n",
							dev->name);
		} else if (devcs & ETH_TX_TOK) {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes +=
					lp->tx_skb[lp->tx_next_done]->len;
		} else {
			dev->stats.tx_errors++;
			dev->stats.tx_dropped++;

			/* Underflow */
			if (devcs & ETH_TX_UND)
				dev->stats.tx_fifo_errors++;

			/* Oversized frame */
			if (devcs & ETH_TX_OF)
				dev->stats.tx_aborted_errors++;

			/* Excessive deferrals */
			if (devcs & ETH_TX_ED)
				dev->stats.tx_carrier_errors++;

			/* Collisions: medium busy */
			if (devcs & ETH_TX_EC)
				dev->stats.collisions++;

			/* Late collision */
			if (devcs & ETH_TX_LC)
				dev->stats.tx_window_errors++;
		}

		/* We must always free the original skb */
		if (lp->tx_skb[lp->tx_next_done]) {
			dma_unmap_single(lp->dmadev,
					 lp->tx_skb_dma[lp->tx_next_done],
					 lp->tx_skb[lp->tx_next_done]->len,
					 DMA_TO_DEVICE);
			dev_kfree_skb_any(lp->tx_skb[lp->tx_next_done]);
			lp->tx_skb[lp->tx_next_done] = NULL;
		}

		lp->td_ring[lp->tx_next_done].control = DMA_DESC_IOF;
		lp->td_ring[lp->tx_next_done].devcs = ETH_TX_FD | ETH_TX_LD;
		lp->td_ring[lp->tx_next_done].link = 0;
		lp->td_ring[lp->tx_next_done].ca = 0;
		lp->tx_count--;

		/* Go on to next transmission */
		lp->tx_next_done = (lp->tx_next_done + 1) & KORINA_TDS_MASK;
		td = &lp->td_ring[lp->tx_next_done];

	}

	/* Clear the DMA status register */
	dmas = readl(&lp->tx_dma_regs->dmas);
	writel(~dmas, &lp->tx_dma_regs->dmas);

	writel(readl(&lp->tx_dma_regs->dmasm) &
			~(DMA_STAT_FINI | DMA_STAT_ERR),
			&lp->tx_dma_regs->dmasm);

	spin_unlock(&lp->lock);
}

static irqreturn_t
korina_tx_dma_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct korina_private *lp = netdev_priv(dev);
	u32 dmas, dmasm;
	irqreturn_t retval;

	dmas = readl(&lp->tx_dma_regs->dmas);

	if (dmas & (DMA_STAT_FINI | DMA_STAT_ERR)) {
		dmasm = readl(&lp->tx_dma_regs->dmasm);
		writel(dmasm | (DMA_STAT_FINI | DMA_STAT_ERR),
				&lp->tx_dma_regs->dmasm);

		korina_tx(dev);

		if (lp->tx_chain_status == desc_filled &&
			(readl(&(lp->tx_dma_regs->dmandptr)) == 0)) {
			writel(korina_tx_dma(lp, lp->tx_chain_head),
			       &lp->tx_dma_regs->dmandptr);
			lp->tx_chain_status = desc_is_empty;
			lp->tx_chain_head = lp->tx_chain_tail;
			netif_trans_update(dev);
		}
		if (dmas & DMA_STAT_ERR)
			printk(KERN_ERR "%s: DMA error\n", dev->name);

		retval = IRQ_HANDLED;
	} else
		retval = IRQ_NONE;

	return retval;
}


static void korina_check_media(struct net_device *dev, unsigned int init_media)
{
	struct korina_private *lp = netdev_priv(dev);

	mii_check_media(&lp->mii_if, 1, init_media);

	if (lp->mii_if.full_duplex)
		writel(readl(&lp->eth_regs->ethmac2) | ETH_MAC2_FD,
						&lp->eth_regs->ethmac2);
	else
		writel(readl(&lp->eth_regs->ethmac2) & ~ETH_MAC2_FD,
						&lp->eth_regs->ethmac2);
}

static void korina_poll_media(struct timer_list *t)
{
	struct korina_private *lp = from_timer(lp, t, media_check_timer);
	struct net_device *dev = lp->dev;

	korina_check_media(dev, 0);
	mod_timer(&lp->media_check_timer, jiffies + HZ);
}

static void korina_set_carrier(struct mii_if_info *mii)
{
	if (mii->force_media) {
		/* autoneg is off: Link is always assumed to be up */
		if (!netif_carrier_ok(mii->dev))
			netif_carrier_on(mii->dev);
	} else  /* Let MMI library update carrier status */
		korina_check_media(mii->dev, 0);
}

static int korina_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct korina_private *lp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(rq);
	int rc;

	if (!netif_running(dev))
		return -EINVAL;
	spin_lock_irq(&lp->lock);
	rc = generic_mii_ioctl(&lp->mii_if, data, cmd, NULL);
	spin_unlock_irq(&lp->lock);
	korina_set_carrier(&lp->mii_if);

	return rc;
}

/* ethtool helpers */
static void netdev_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct korina_private *lp = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, lp->dev->name, sizeof(info->bus_info));
}

static int netdev_get_link_ksettings(struct net_device *dev,
				     struct ethtool_link_ksettings *cmd)
{
	struct korina_private *lp = netdev_priv(dev);

	spin_lock_irq(&lp->lock);
	mii_ethtool_get_link_ksettings(&lp->mii_if, cmd);
	spin_unlock_irq(&lp->lock);

	return 0;
}

static int netdev_set_link_ksettings(struct net_device *dev,
				     const struct ethtool_link_ksettings *cmd)
{
	struct korina_private *lp = netdev_priv(dev);
	int rc;

	spin_lock_irq(&lp->lock);
	rc = mii_ethtool_set_link_ksettings(&lp->mii_if, cmd);
	spin_unlock_irq(&lp->lock);
	korina_set_carrier(&lp->mii_if);

	return rc;
}

static u32 netdev_get_link(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);

	return mii_link_ok(&lp->mii_if);
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_link		= netdev_get_link,
	.get_link_ksettings	= netdev_get_link_ksettings,
	.set_link_ksettings	= netdev_set_link_ksettings,
};

static int korina_alloc_ring(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);
	struct sk_buff *skb;
	dma_addr_t ca;
	int i;

	/* Initialize the transmit descriptors */
	for (i = 0; i < KORINA_NUM_TDS; i++) {
		lp->td_ring[i].control = DMA_DESC_IOF;
		lp->td_ring[i].devcs = ETH_TX_FD | ETH_TX_LD;
		lp->td_ring[i].ca = 0;
		lp->td_ring[i].link = 0;
	}
	lp->tx_next_done = lp->tx_chain_head = lp->tx_chain_tail =
			lp->tx_full = lp->tx_count = 0;
	lp->tx_chain_status = desc_is_empty;

	/* Initialize the receive descriptors */
	for (i = 0; i < KORINA_NUM_RDS; i++) {
		skb = netdev_alloc_skb_ip_align(dev, KORINA_RBSIZE);
		if (!skb)
			return -ENOMEM;
		lp->rx_skb[i] = skb;
		lp->rd_ring[i].control = DMA_DESC_IOD |
				DMA_COUNT(KORINA_RBSIZE);
		lp->rd_ring[i].devcs = 0;
		ca = dma_map_single(lp->dmadev, skb->data, KORINA_RBSIZE,
				    DMA_FROM_DEVICE);
		if (dma_mapping_error(lp->dmadev, ca))
			return -ENOMEM;
		lp->rd_ring[i].ca = ca;
		lp->rx_skb_dma[i] = ca;
		lp->rd_ring[i].link = korina_rx_dma(lp, i + 1);
	}

	/* loop back receive descriptors, so the last
	 * descriptor points to the first one */
	lp->rd_ring[i - 1].link = lp->rd_dma;
	lp->rd_ring[i - 1].control |= DMA_DESC_COD;

	lp->rx_next_done  = 0;
	lp->rx_chain_head = 0;
	lp->rx_chain_tail = 0;
	lp->rx_chain_status = desc_is_empty;

	return 0;
}

static void korina_free_ring(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);
	int i;

	for (i = 0; i < KORINA_NUM_RDS; i++) {
		lp->rd_ring[i].control = 0;
		if (lp->rx_skb[i]) {
			dma_unmap_single(lp->dmadev, lp->rx_skb_dma[i],
					 KORINA_RBSIZE, DMA_FROM_DEVICE);
			dev_kfree_skb_any(lp->rx_skb[i]);
			lp->rx_skb[i] = NULL;
		}
	}

	for (i = 0; i < KORINA_NUM_TDS; i++) {
		lp->td_ring[i].control = 0;
		if (lp->tx_skb[i]) {
			dma_unmap_single(lp->dmadev, lp->tx_skb_dma[i],
					 lp->tx_skb[i]->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(lp->tx_skb[i]);
			lp->tx_skb[i] = NULL;
		}
	}
}

/*
 * Initialize the RC32434 ethernet controller.
 */
static int korina_init(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);

	/* Disable DMA */
	korina_abort_tx(dev);
	korina_abort_rx(dev);

	/* reset ethernet logic */
	writel(0, &lp->eth_regs->ethintfc);
	while ((readl(&lp->eth_regs->ethintfc) & ETH_INT_FC_RIP))
		netif_trans_update(dev);

	/* Enable Ethernet Interface */
	writel(ETH_INT_FC_EN, &lp->eth_regs->ethintfc);

	/* Allocate rings */
	if (korina_alloc_ring(dev)) {
		printk(KERN_ERR "%s: descriptor allocation failed\n", dev->name);
		korina_free_ring(dev);
		return -ENOMEM;
	}

	writel(0, &lp->rx_dma_regs->dmas);
	/* Start Rx DMA */
	writel(0, &lp->rx_dma_regs->dmandptr);
	writel(korina_rx_dma(lp, 0), &lp->rx_dma_regs->dmadptr);

	writel(readl(&lp->tx_dma_regs->dmasm) &
			~(DMA_STAT_FINI | DMA_STAT_ERR),
			&lp->tx_dma_regs->dmasm);
	writel(readl(&lp->rx_dma_regs->dmasm) &
			~(DMA_STAT_DONE | DMA_STAT_HALT | DMA_STAT_ERR),
			&lp->rx_dma_regs->dmasm);

	/* Accept only packets destined for this Ethernet device address */
	writel(ETH_ARC_AB, &lp->eth_regs->etharc);

	/* Set all Ether station address registers to their initial values */
	writel(STATION_ADDRESS_LOW(dev), &lp->eth_regs->ethsal0);
	writel(STATION_ADDRESS_HIGH(dev), &lp->eth_regs->ethsah0);

	writel(STATION_ADDRESS_LOW(dev), &lp->eth_regs->ethsal1);
	writel(STATION_ADDRESS_HIGH(dev), &lp->eth_regs->ethsah1);

	writel(STATION_ADDRESS_LOW(dev), &lp->eth_regs->ethsal2);
	writel(STATION_ADDRESS_HIGH(dev), &lp->eth_regs->ethsah2);

	writel(STATION_ADDRESS_LOW(dev), &lp->eth_regs->ethsal3);
	writel(STATION_ADDRESS_HIGH(dev), &lp->eth_regs->ethsah3);


	/* Frame Length Checking, Pad Enable, CRC Enable, Full Duplex set */
	writel(ETH_MAC2_PE | ETH_MAC2_CEN | ETH_MAC2_FD,
			&lp->eth_regs->ethmac2);

	/* Back to back inter-packet-gap */
	writel(0x15, &lp->eth_regs->ethipgt);
	/* Non - Back to back inter-packet-gap */
	writel(0x12, &lp->eth_regs->ethipgr);

	/* Management Clock Prescaler Divisor
	 * Clock independent setting */
	writel(((lp->mii_clock_freq) / MII_CLOCK + 1) & ~1,
	       &lp->eth_regs->ethmcp);
	writel(0, &lp->eth_regs->miimcfg);

	/* don't transmit until fifo contains 48b */
	writel(48, &lp->eth_regs->ethfifott);

	writel(ETH_MAC1_RE, &lp->eth_regs->ethmac1);

	korina_check_media(dev, 1);

	napi_enable(&lp->napi);
	netif_start_queue(dev);

	return 0;
}

/*
 * Restart the RC32434 ethernet controller.
 */
static void korina_restart_task(struct work_struct *work)
{
	struct korina_private *lp = container_of(work,
			struct korina_private, restart_task);
	struct net_device *dev = lp->dev;

	/*
	 * Disable interrupts
	 */
	disable_irq(lp->rx_irq);
	disable_irq(lp->tx_irq);

	writel(readl(&lp->tx_dma_regs->dmasm) |
				DMA_STAT_FINI | DMA_STAT_ERR,
				&lp->tx_dma_regs->dmasm);
	writel(readl(&lp->rx_dma_regs->dmasm) |
				DMA_STAT_DONE | DMA_STAT_HALT | DMA_STAT_ERR,
				&lp->rx_dma_regs->dmasm);

	napi_disable(&lp->napi);

	korina_free_ring(dev);

	if (korina_init(dev) < 0) {
		printk(KERN_ERR "%s: cannot restart device\n", dev->name);
		return;
	}
	korina_multicast_list(dev);

	enable_irq(lp->tx_irq);
	enable_irq(lp->rx_irq);
}

static void korina_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct korina_private *lp = netdev_priv(dev);

	schedule_work(&lp->restart_task);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void korina_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	korina_tx_dma_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static int korina_open(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);
	int ret;

	/* Initialize */
	ret = korina_init(dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: cannot open device\n", dev->name);
		goto out;
	}

	/* Install the interrupt handler
	 * that handles the Done Finished */
	ret = request_irq(lp->rx_irq, korina_rx_dma_interrupt,
			0, "Korina ethernet Rx", dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: unable to get Rx DMA IRQ %d\n",
			dev->name, lp->rx_irq);
		goto err_release;
	}
	ret = request_irq(lp->tx_irq, korina_tx_dma_interrupt,
			0, "Korina ethernet Tx", dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: unable to get Tx DMA IRQ %d\n",
			dev->name, lp->tx_irq);
		goto err_free_rx_irq;
	}

	mod_timer(&lp->media_check_timer, jiffies + 1);
out:
	return ret;

err_free_rx_irq:
	free_irq(lp->rx_irq, dev);
err_release:
	korina_free_ring(dev);
	goto out;
}

static int korina_close(struct net_device *dev)
{
	struct korina_private *lp = netdev_priv(dev);
	u32 tmp;

	del_timer(&lp->media_check_timer);

	/* Disable interrupts */
	disable_irq(lp->rx_irq);
	disable_irq(lp->tx_irq);

	korina_abort_tx(dev);
	tmp = readl(&lp->tx_dma_regs->dmasm);
	tmp = tmp | DMA_STAT_FINI | DMA_STAT_ERR;
	writel(tmp, &lp->tx_dma_regs->dmasm);

	korina_abort_rx(dev);
	tmp = readl(&lp->rx_dma_regs->dmasm);
	tmp = tmp | DMA_STAT_DONE | DMA_STAT_HALT | DMA_STAT_ERR;
	writel(tmp, &lp->rx_dma_regs->dmasm);

	napi_disable(&lp->napi);

	cancel_work_sync(&lp->restart_task);

	korina_free_ring(dev);

	free_irq(lp->rx_irq, dev);
	free_irq(lp->tx_irq, dev);

	return 0;
}

static const struct net_device_ops korina_netdev_ops = {
	.ndo_open		= korina_open,
	.ndo_stop		= korina_close,
	.ndo_start_xmit		= korina_send_packet,
	.ndo_set_rx_mode	= korina_multicast_list,
	.ndo_tx_timeout		= korina_tx_timeout,
	.ndo_do_ioctl		= korina_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= korina_poll_controller,
#endif
};

static int korina_probe(struct platform_device *pdev)
{
	u8 *mac_addr = dev_get_platdata(&pdev->dev);
	struct korina_private *lp;
	struct net_device *dev;
	struct clk *clk;
	void __iomem *p;
	int rc;

	dev = devm_alloc_etherdev(&pdev->dev, sizeof(struct korina_private));
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &pdev->dev);
	lp = netdev_priv(dev);

	if (mac_addr)
		ether_addr_copy(dev->dev_addr, mac_addr);
	else if (of_get_mac_address(pdev->dev.of_node, dev->dev_addr) < 0)
		eth_hw_addr_random(dev);

	clk = devm_clk_get_optional(&pdev->dev, "mdioclk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	if (clk) {
		clk_prepare_enable(clk);
		lp->mii_clock_freq = clk_get_rate(clk);
	} else {
		lp->mii_clock_freq = 200000000; /* max possible input clk */
	}

	lp->rx_irq = platform_get_irq_byname(pdev, "rx");
	lp->tx_irq = platform_get_irq_byname(pdev, "tx");

	p = devm_platform_ioremap_resource_byname(pdev, "emac");
	if (!p) {
		printk(KERN_ERR DRV_NAME ": cannot remap registers\n");
		return -ENOMEM;
	}
	lp->eth_regs = p;

	p = devm_platform_ioremap_resource_byname(pdev, "dma_rx");
	if (!p) {
		printk(KERN_ERR DRV_NAME ": cannot remap Rx DMA registers\n");
		return -ENOMEM;
	}
	lp->rx_dma_regs = p;

	p = devm_platform_ioremap_resource_byname(pdev, "dma_tx");
	if (!p) {
		printk(KERN_ERR DRV_NAME ": cannot remap Tx DMA registers\n");
		return -ENOMEM;
	}
	lp->tx_dma_regs = p;

	lp->td_ring = dmam_alloc_coherent(&pdev->dev, TD_RING_SIZE,
					  &lp->td_dma, GFP_KERNEL);
	if (!lp->td_ring)
		return -ENOMEM;

	lp->rd_ring = dmam_alloc_coherent(&pdev->dev, RD_RING_SIZE,
					  &lp->rd_dma, GFP_KERNEL);
	if (!lp->rd_ring)
		return -ENOMEM;

	spin_lock_init(&lp->lock);
	/* just use the rx dma irq */
	dev->irq = lp->rx_irq;
	lp->dev = dev;
	lp->dmadev = &pdev->dev;

	dev->netdev_ops = &korina_netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;
	netif_napi_add(dev, &lp->napi, korina_poll, NAPI_POLL_WEIGHT);

	lp->mii_if.dev = dev;
	lp->mii_if.mdio_read = korina_mdio_read;
	lp->mii_if.mdio_write = korina_mdio_write;
	lp->mii_if.phy_id = 1;
	lp->mii_if.phy_id_mask = 0x1f;
	lp->mii_if.reg_num_mask = 0x1f;

	platform_set_drvdata(pdev, dev);

	rc = register_netdev(dev);
	if (rc < 0) {
		printk(KERN_ERR DRV_NAME
			": cannot register net device: %d\n", rc);
		return rc;
	}
	timer_setup(&lp->media_check_timer, korina_poll_media, 0);

	INIT_WORK(&lp->restart_task, korina_restart_task);

	printk(KERN_INFO "%s: " DRV_NAME "-" DRV_VERSION " " DRV_RELDATE "\n",
			dev->name);
	return rc;
}

static int korina_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_netdev(dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id korina_match[] = {
	{
		.compatible = "idt,3243x-emac",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, korina_match);
#endif

static struct platform_driver korina_driver = {
	.driver = {
		.name = "korina",
		.of_match_table = of_match_ptr(korina_match),
	},
	.probe = korina_probe,
	.remove = korina_remove,
};

module_platform_driver(korina_driver);

MODULE_AUTHOR("Philip Rischel <rischelp@idt.com>");
MODULE_AUTHOR("Felix Fietkau <nbd@openwrt.org>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Roman Yeryomin <roman@advem.lv>");
MODULE_DESCRIPTION("IDT RC32434 (Korina) Ethernet driver");
MODULE_LICENSE("GPL");
