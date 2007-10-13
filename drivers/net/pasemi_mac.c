/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Driver for the PA Semi PWRficient onchip 1G/10G Ethernet MACs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <asm/dma-mapping.h>
#include <linux/in.h>
#include <linux/skbuff.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/checksum.h>

#include <asm/irq.h>
#include <asm/firmware.h>

#include "pasemi_mac.h"

/* We have our own align, since ppc64 in general has it at 0 because
 * of design flaws in some of the server bridge chips. However, for
 * PWRficient doing the unaligned copies is more expensive than doing
 * unaligned DMA, so make sure the data is aligned instead.
 */
#define LOCAL_SKB_ALIGN	2

/* TODO list
 *
 * - Multicast support
 * - Large MTU support
 * - SW LRO
 * - Multiqueue RX/TX
 */


/* Must be a power of two */
#define RX_RING_SIZE 4096
#define TX_RING_SIZE 4096

#define DEFAULT_MSG_ENABLE	  \
	(NETIF_MSG_DRV		| \
	 NETIF_MSG_PROBE	| \
	 NETIF_MSG_LINK		| \
	 NETIF_MSG_TIMER	| \
	 NETIF_MSG_IFDOWN	| \
	 NETIF_MSG_IFUP		| \
	 NETIF_MSG_RX_ERR	| \
	 NETIF_MSG_TX_ERR)

#define TX_RING(mac, num)	((mac)->tx->ring[(num) & (TX_RING_SIZE-1)])
#define TX_RING_INFO(mac, num)	((mac)->tx->ring_info[(num) & (TX_RING_SIZE-1)])
#define RX_RING(mac, num)	((mac)->rx->ring[(num) & (RX_RING_SIZE-1)])
#define RX_RING_INFO(mac, num)	((mac)->rx->ring_info[(num) & (RX_RING_SIZE-1)])
#define RX_BUFF(mac, num)	((mac)->rx->buffers[(num) & (RX_RING_SIZE-1)])

#define RING_USED(ring)		(((ring)->next_to_fill - (ring)->next_to_clean) \
				 & ((ring)->size - 1))
#define RING_AVAIL(ring)	((ring->size) - RING_USED(ring))

#define BUF_SIZE 1646 /* 1500 MTU + ETH_HLEN + VLAN_HLEN + 2 64B cachelines */

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi PWRficient Ethernet driver");

static int debug = -1;	/* -1 == use DEFAULT_MSG_ENABLE as value */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "PA Semi MAC bitmapped debugging message enable value");

static struct pasdma_status *dma_status;

static int translation_enabled(void)
{
#if defined(CONFIG_PPC_PASEMI_IOMMU_DMA_FORCE)
	return 1;
#else
	return firmware_has_feature(FW_FEATURE_LPAR);
#endif
}

static void write_iob_reg(struct pasemi_mac *mac, unsigned int reg,
			  unsigned int val)
{
	out_le32(mac->iob_regs+reg, val);
}

static unsigned int read_mac_reg(struct pasemi_mac *mac, unsigned int reg)
{
	return in_le32(mac->regs+reg);
}

static void write_mac_reg(struct pasemi_mac *mac, unsigned int reg,
			  unsigned int val)
{
	out_le32(mac->regs+reg, val);
}

static unsigned int read_dma_reg(struct pasemi_mac *mac, unsigned int reg)
{
	return in_le32(mac->dma_regs+reg);
}

static void write_dma_reg(struct pasemi_mac *mac, unsigned int reg,
			  unsigned int val)
{
	out_le32(mac->dma_regs+reg, val);
}

static int pasemi_get_mac_addr(struct pasemi_mac *mac)
{
	struct pci_dev *pdev = mac->pdev;
	struct device_node *dn = pci_device_to_OF_node(pdev);
	int len;
	const u8 *maddr;
	u8 addr[6];

	if (!dn) {
		dev_dbg(&pdev->dev,
			  "No device node for mac, not configuring\n");
		return -ENOENT;
	}

	maddr = of_get_property(dn, "local-mac-address", &len);

	if (maddr && len == 6) {
		memcpy(mac->mac_addr, maddr, 6);
		return 0;
	}

	/* Some old versions of firmware mistakenly uses mac-address
	 * (and as a string) instead of a byte array in local-mac-address.
	 */

	if (maddr == NULL)
		maddr = of_get_property(dn, "mac-address", NULL);

	if (maddr == NULL) {
		dev_warn(&pdev->dev,
			 "no mac address in device tree, not configuring\n");
		return -ENOENT;
	}


	if (sscanf(maddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &addr[0],
		   &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) != 6) {
		dev_warn(&pdev->dev,
			 "can't parse mac address, not configuring\n");
		return -EINVAL;
	}

	memcpy(mac->mac_addr, addr, 6);

	return 0;
}

static int pasemi_mac_unmap_tx_skb(struct pasemi_mac *mac,
				    struct sk_buff *skb,
				    dma_addr_t *dmas)
{
	int f;
	int nfrags = skb_shinfo(skb)->nr_frags;

	pci_unmap_single(mac->dma_pdev, dmas[0], skb_headlen(skb),
			 PCI_DMA_TODEVICE);

	for (f = 0; f < nfrags; f++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[f];

		pci_unmap_page(mac->dma_pdev, dmas[f+1], frag->size,
			       PCI_DMA_TODEVICE);
	}
	dev_kfree_skb_irq(skb);

	/* Freed descriptor slot + main SKB ptr + nfrags additional ptrs,
	 * aligned up to a power of 2
	 */
	return (nfrags + 3) & ~1;
}

static int pasemi_mac_setup_rx_resources(struct net_device *dev)
{
	struct pasemi_mac_rxring *ring;
	struct pasemi_mac *mac = netdev_priv(dev);
	int chan_id = mac->dma_rxch;
	unsigned int cfg;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);

	if (!ring)
		goto out_ring;

	spin_lock_init(&ring->lock);

	ring->size = RX_RING_SIZE;
	ring->ring_info = kzalloc(sizeof(struct pasemi_mac_buffer) *
				  RX_RING_SIZE, GFP_KERNEL);

	if (!ring->ring_info)
		goto out_ring_info;

	/* Allocate descriptors */
	ring->ring = dma_alloc_coherent(&mac->dma_pdev->dev,
					RX_RING_SIZE * sizeof(u64),
					&ring->dma, GFP_KERNEL);

	if (!ring->ring)
		goto out_ring_desc;

	memset(ring->ring, 0, RX_RING_SIZE * sizeof(u64));

	ring->buffers = dma_alloc_coherent(&mac->dma_pdev->dev,
					   RX_RING_SIZE * sizeof(u64),
					   &ring->buf_dma, GFP_KERNEL);
	if (!ring->buffers)
		goto out_buffers;

	memset(ring->buffers, 0, RX_RING_SIZE * sizeof(u64));

	write_dma_reg(mac, PAS_DMA_RXCHAN_BASEL(chan_id), PAS_DMA_RXCHAN_BASEL_BRBL(ring->dma));

	write_dma_reg(mac, PAS_DMA_RXCHAN_BASEU(chan_id),
			   PAS_DMA_RXCHAN_BASEU_BRBH(ring->dma >> 32) |
			   PAS_DMA_RXCHAN_BASEU_SIZ(RX_RING_SIZE >> 3));

	cfg = PAS_DMA_RXCHAN_CFG_HBU(2);

	if (translation_enabled())
		cfg |= PAS_DMA_RXCHAN_CFG_CTR;

	write_dma_reg(mac, PAS_DMA_RXCHAN_CFG(chan_id), cfg);

	write_dma_reg(mac, PAS_DMA_RXINT_BASEL(mac->dma_if),
			   PAS_DMA_RXINT_BASEL_BRBL(ring->buf_dma));

	write_dma_reg(mac, PAS_DMA_RXINT_BASEU(mac->dma_if),
			   PAS_DMA_RXINT_BASEU_BRBH(ring->buf_dma >> 32) |
			   PAS_DMA_RXINT_BASEU_SIZ(RX_RING_SIZE >> 3));

	cfg = PAS_DMA_RXINT_CFG_DHL(3) | PAS_DMA_RXINT_CFG_L2 |
	      PAS_DMA_RXINT_CFG_LW | PAS_DMA_RXINT_CFG_RBP |
	      PAS_DMA_RXINT_CFG_HEN;

	if (translation_enabled())
		cfg |= PAS_DMA_RXINT_CFG_ITRR | PAS_DMA_RXINT_CFG_ITR;

	write_dma_reg(mac, PAS_DMA_RXINT_CFG(mac->dma_if), cfg);

	ring->next_to_fill = 0;
	ring->next_to_clean = 0;

	snprintf(ring->irq_name, sizeof(ring->irq_name),
		 "%s rx", dev->name);
	mac->rx = ring;

	return 0;

out_buffers:
	dma_free_coherent(&mac->dma_pdev->dev,
			  RX_RING_SIZE * sizeof(u64),
			  mac->rx->ring, mac->rx->dma);
out_ring_desc:
	kfree(ring->ring_info);
out_ring_info:
	kfree(ring);
out_ring:
	return -ENOMEM;
}


static int pasemi_mac_setup_tx_resources(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	u32 val;
	int chan_id = mac->dma_txch;
	struct pasemi_mac_txring *ring;
	unsigned int cfg;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		goto out_ring;

	spin_lock_init(&ring->lock);

	ring->size = TX_RING_SIZE;
	ring->ring_info = kzalloc(sizeof(struct pasemi_mac_buffer) *
				  TX_RING_SIZE, GFP_KERNEL);
	if (!ring->ring_info)
		goto out_ring_info;

	/* Allocate descriptors */
	ring->ring = dma_alloc_coherent(&mac->dma_pdev->dev,
					TX_RING_SIZE * sizeof(u64),
					&ring->dma, GFP_KERNEL);
	if (!ring->ring)
		goto out_ring_desc;

	memset(ring->ring, 0, TX_RING_SIZE * sizeof(u64));

	write_dma_reg(mac, PAS_DMA_TXCHAN_BASEL(chan_id),
			   PAS_DMA_TXCHAN_BASEL_BRBL(ring->dma));
	val = PAS_DMA_TXCHAN_BASEU_BRBH(ring->dma >> 32);
	val |= PAS_DMA_TXCHAN_BASEU_SIZ(TX_RING_SIZE >> 3);

	write_dma_reg(mac, PAS_DMA_TXCHAN_BASEU(chan_id), val);

	cfg = PAS_DMA_TXCHAN_CFG_TY_IFACE |
	      PAS_DMA_TXCHAN_CFG_TATTR(mac->dma_if) |
	      PAS_DMA_TXCHAN_CFG_UP |
	      PAS_DMA_TXCHAN_CFG_WT(2);

	if (translation_enabled())
		cfg |= PAS_DMA_TXCHAN_CFG_TRD | PAS_DMA_TXCHAN_CFG_TRR;

	write_dma_reg(mac, PAS_DMA_TXCHAN_CFG(chan_id), cfg);

	ring->next_to_fill = 0;
	ring->next_to_clean = 0;

	snprintf(ring->irq_name, sizeof(ring->irq_name),
		 "%s tx", dev->name);
	mac->tx = ring;

	return 0;

out_ring_desc:
	kfree(ring->ring_info);
out_ring_info:
	kfree(ring);
out_ring:
	return -ENOMEM;
}

static void pasemi_mac_free_tx_resources(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int i, j;
	struct pasemi_mac_buffer *info;
	dma_addr_t dmas[MAX_SKB_FRAGS+1];
	int freed;
	int start, limit;

	start = mac->tx->next_to_clean;
	limit = mac->tx->next_to_fill;

	/* Compensate for when fill has wrapped and clean has not */
	if (start > limit)
		limit += TX_RING_SIZE;

	for (i = start; i < limit; i += freed) {
		info = &TX_RING_INFO(mac, i+1);
		if (info->dma && info->skb) {
			for (j = 0; j <= skb_shinfo(info->skb)->nr_frags; j++)
				dmas[j] = TX_RING_INFO(mac, i+1+j).dma;
			freed = pasemi_mac_unmap_tx_skb(mac, info->skb, dmas);
		} else
			freed = 2;
	}

	for (i = 0; i < TX_RING_SIZE; i++)
		TX_RING(mac, i) = 0;

	dma_free_coherent(&mac->dma_pdev->dev,
			  TX_RING_SIZE * sizeof(u64),
			  mac->tx->ring, mac->tx->dma);

	kfree(mac->tx->ring_info);
	kfree(mac->tx);
	mac->tx = NULL;
}

static void pasemi_mac_free_rx_resources(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int i;
	struct pasemi_mac_buffer *info;

	for (i = 0; i < RX_RING_SIZE; i++) {
		info = &RX_RING_INFO(mac, i);
		if (info->skb && info->dma) {
			pci_unmap_single(mac->dma_pdev,
					 info->dma,
					 info->skb->len,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(info->skb);
		}
		info->dma = 0;
		info->skb = NULL;
	}

	for (i = 0; i < RX_RING_SIZE; i++)
		RX_RING(mac, i) = 0;

	dma_free_coherent(&mac->dma_pdev->dev,
			  RX_RING_SIZE * sizeof(u64),
			  mac->rx->ring, mac->rx->dma);

	dma_free_coherent(&mac->dma_pdev->dev, RX_RING_SIZE * sizeof(u64),
			  mac->rx->buffers, mac->rx->buf_dma);

	kfree(mac->rx->ring_info);
	kfree(mac->rx);
	mac->rx = NULL;
}

static void pasemi_mac_replenish_rx_ring(struct net_device *dev, int limit)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	int fill, count;

	if (limit <= 0)
		return;

	fill = mac->rx->next_to_fill;
	for (count = 0; count < limit; count++) {
		struct pasemi_mac_buffer *info = &RX_RING_INFO(mac, fill);
		u64 *buff = &RX_BUFF(mac, fill);
		struct sk_buff *skb;
		dma_addr_t dma;

		/* Entry in use? */
		WARN_ON(*buff);

		/* skb might still be in there for recycle on short receives */
		if (info->skb)
			skb = info->skb;
		else {
			skb = dev_alloc_skb(BUF_SIZE);
			skb_reserve(skb, LOCAL_SKB_ALIGN);
		}

		if (unlikely(!skb))
			break;

		dma = pci_map_single(mac->dma_pdev, skb->data,
				     BUF_SIZE - LOCAL_SKB_ALIGN,
				     PCI_DMA_FROMDEVICE);

		if (unlikely(dma_mapping_error(dma))) {
			dev_kfree_skb_irq(info->skb);
			break;
		}

		info->skb = skb;
		info->dma = dma;
		*buff = XCT_RXB_LEN(BUF_SIZE) | XCT_RXB_ADDR(dma);
		fill++;
	}

	wmb();

	write_dma_reg(mac, PAS_DMA_RXINT_INCR(mac->dma_if), count);

	mac->rx->next_to_fill = (mac->rx->next_to_fill + count) &
				(RX_RING_SIZE - 1);
}

static void pasemi_mac_restart_rx_intr(struct pasemi_mac *mac)
{
	unsigned int reg, pcnt;
	/* Re-enable packet count interrupts: finally
	 * ack the packet count interrupt we got in rx_intr.
	 */

	pcnt = *mac->rx_status & PAS_STATUS_PCNT_M;

	reg = PAS_IOB_DMA_RXCH_RESET_PCNT(pcnt) | PAS_IOB_DMA_RXCH_RESET_PINTC;

	write_iob_reg(mac, PAS_IOB_DMA_RXCH_RESET(mac->dma_rxch), reg);
}

static void pasemi_mac_restart_tx_intr(struct pasemi_mac *mac)
{
	unsigned int reg, pcnt;

	/* Re-enable packet count interrupts */
	pcnt = *mac->tx_status & PAS_STATUS_PCNT_M;

	reg = PAS_IOB_DMA_TXCH_RESET_PCNT(pcnt) | PAS_IOB_DMA_TXCH_RESET_PINTC;

	write_iob_reg(mac, PAS_IOB_DMA_TXCH_RESET(mac->dma_txch), reg);
}


static inline void pasemi_mac_rx_error(struct pasemi_mac *mac, u64 macrx)
{
	unsigned int rcmdsta, ccmdsta;

	if (!netif_msg_rx_err(mac))
		return;

	rcmdsta = read_dma_reg(mac, PAS_DMA_RXINT_RCMDSTA(mac->dma_if));
	ccmdsta = read_dma_reg(mac, PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch));

	printk(KERN_ERR "pasemi_mac: rx error. macrx %016lx, rx status %lx\n",
		macrx, *mac->rx_status);

	printk(KERN_ERR "pasemi_mac: rcmdsta %08x ccmdsta %08x\n",
		rcmdsta, ccmdsta);
}

static inline void pasemi_mac_tx_error(struct pasemi_mac *mac, u64 mactx)
{
	unsigned int cmdsta;

	if (!netif_msg_tx_err(mac))
		return;

	cmdsta = read_dma_reg(mac, PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch));

	printk(KERN_ERR "pasemi_mac: tx error. mactx 0x%016lx, "\
		"tx status 0x%016lx\n", mactx, *mac->tx_status);

	printk(KERN_ERR "pasemi_mac: tcmdsta 0x%08x\n", cmdsta);
}

static int pasemi_mac_clean_rx(struct pasemi_mac *mac, int limit)
{
	unsigned int n;
	int count;
	struct pasemi_mac_buffer *info;
	struct sk_buff *skb;
	unsigned int len;
	u64 macrx;
	dma_addr_t dma;
	int buf_index;
	u64 eval;

	spin_lock(&mac->rx->lock);

	n = mac->rx->next_to_clean;

	prefetch(RX_RING(mac, n));

	for (count = 0; count < limit; count++) {
		macrx = RX_RING(mac, n);

		if ((macrx & XCT_MACRX_E) ||
		    (*mac->rx_status & PAS_STATUS_ERROR))
			pasemi_mac_rx_error(mac, macrx);

		if (!(macrx & XCT_MACRX_O))
			break;

		info = NULL;

		BUG_ON(!(macrx & XCT_MACRX_RR_8BRES));

		eval = (RX_RING(mac, n+1) & XCT_RXRES_8B_EVAL_M) >>
			XCT_RXRES_8B_EVAL_S;
		buf_index = eval-1;

		dma = (RX_RING(mac, n+2) & XCT_PTR_ADDR_M);
		info = &RX_RING_INFO(mac, buf_index);

		skb = info->skb;

		prefetch(skb);
		prefetch(&skb->data_len);

		len = (macrx & XCT_MACRX_LLEN_M) >> XCT_MACRX_LLEN_S;

		if (len < 256) {
			struct sk_buff *new_skb;

			new_skb = netdev_alloc_skb(mac->netdev,
						   len + LOCAL_SKB_ALIGN);
			if (new_skb) {
				skb_reserve(new_skb, LOCAL_SKB_ALIGN);
				memcpy(new_skb->data, skb->data, len);
				/* save the skb in buffer_info as good */
				skb = new_skb;
			}
			/* else just continue with the old one */
		} else
			info->skb = NULL;

		pci_unmap_single(mac->dma_pdev, dma, len, PCI_DMA_FROMDEVICE);

		info->dma = 0;

		skb_put(skb, len);

		if (likely((macrx & XCT_MACRX_HTY_M) == XCT_MACRX_HTY_IPV4_OK)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->csum = (macrx & XCT_MACRX_CSUM_M) >>
					   XCT_MACRX_CSUM_S;
		} else
			skb->ip_summed = CHECKSUM_NONE;

		mac->netdev->stats.rx_bytes += len;
		mac->netdev->stats.rx_packets++;

		skb->protocol = eth_type_trans(skb, mac->netdev);
		netif_receive_skb(skb);

		RX_RING(mac, n) = 0;
		RX_RING(mac, n+1) = 0;

		/* Need to zero it out since hardware doesn't, since the
		 * replenish loop uses it to tell when it's done.
		 */
		RX_BUFF(mac, buf_index) = 0;

		n += 4;
	}

	if (n > RX_RING_SIZE) {
		/* Errata 5971 workaround: L2 target of headers */
		write_iob_reg(mac, PAS_IOB_COM_PKTHDRCNT, 0);
		n &= (RX_RING_SIZE-1);
	}

	mac->rx->next_to_clean = n;

	/* Increase is in number of 16-byte entries, and since each descriptor
	 * with an 8BRES takes up 3x8 bytes (padded to 4x8), increase with
	 * count*2.
	 */
	write_dma_reg(mac, PAS_DMA_RXCHAN_INCR(mac->dma_rxch), count << 1);

	pasemi_mac_replenish_rx_ring(mac->netdev, count);

	spin_unlock(&mac->rx->lock);

	return count;
}

/* Can't make this too large or we blow the kernel stack limits */
#define TX_CLEAN_BATCHSIZE (128/MAX_SKB_FRAGS)

static int pasemi_mac_clean_tx(struct pasemi_mac *mac)
{
	int i, j;
	unsigned int start, descr_count, buf_count, batch_limit;
	unsigned int ring_limit;
	unsigned int total_count;
	unsigned long flags;
	struct sk_buff *skbs[TX_CLEAN_BATCHSIZE];
	dma_addr_t dmas[TX_CLEAN_BATCHSIZE][MAX_SKB_FRAGS+1];

	total_count = 0;
	batch_limit = TX_CLEAN_BATCHSIZE;
restart:
	spin_lock_irqsave(&mac->tx->lock, flags);

	start = mac->tx->next_to_clean;
	ring_limit = mac->tx->next_to_fill;

	/* Compensate for when fill has wrapped but clean has not */
	if (start > ring_limit)
		ring_limit += TX_RING_SIZE;

	buf_count = 0;
	descr_count = 0;

	for (i = start;
	     descr_count < batch_limit && i < ring_limit;
	     i += buf_count) {
		u64 mactx = TX_RING(mac, i);
		struct sk_buff *skb;

		if ((mactx  & XCT_MACTX_E) ||
		    (*mac->tx_status & PAS_STATUS_ERROR))
			pasemi_mac_tx_error(mac, mactx);

		if (unlikely(mactx & XCT_MACTX_O))
			/* Not yet transmitted */
			break;

		skb = TX_RING_INFO(mac, i+1).skb;
		skbs[descr_count] = skb;

		buf_count = 2 + skb_shinfo(skb)->nr_frags;
		for (j = 0; j <= skb_shinfo(skb)->nr_frags; j++)
			dmas[descr_count][j] = TX_RING_INFO(mac, i+1+j).dma;

		TX_RING(mac, i) = 0;
		TX_RING(mac, i+1) = 0;

		/* Since we always fill with an even number of entries, make
		 * sure we skip any unused one at the end as well.
		 */
		if (buf_count & 1)
			buf_count++;
		descr_count++;
	}
	mac->tx->next_to_clean = i & (TX_RING_SIZE-1);

	spin_unlock_irqrestore(&mac->tx->lock, flags);
	netif_wake_queue(mac->netdev);

	for (i = 0; i < descr_count; i++)
		pasemi_mac_unmap_tx_skb(mac, skbs[i], dmas[i]);

	total_count += descr_count;

	/* If the batch was full, try to clean more */
	if (descr_count == batch_limit)
		goto restart;

	return total_count;
}


static irqreturn_t pasemi_mac_rx_intr(int irq, void *data)
{
	struct net_device *dev = data;
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int reg;

	if (!(*mac->rx_status & PAS_STATUS_CAUSE_M))
		return IRQ_NONE;

	/* Don't reset packet count so it won't fire again but clear
	 * all others.
	 */

	reg = 0;
	if (*mac->rx_status & PAS_STATUS_SOFT)
		reg |= PAS_IOB_DMA_RXCH_RESET_SINTC;
	if (*mac->rx_status & PAS_STATUS_ERROR)
		reg |= PAS_IOB_DMA_RXCH_RESET_DINTC;
	if (*mac->rx_status & PAS_STATUS_TIMER)
		reg |= PAS_IOB_DMA_RXCH_RESET_TINTC;

	netif_rx_schedule(dev, &mac->napi);

	write_iob_reg(mac, PAS_IOB_DMA_RXCH_RESET(mac->dma_rxch), reg);

	return IRQ_HANDLED;
}

static irqreturn_t pasemi_mac_tx_intr(int irq, void *data)
{
	struct net_device *dev = data;
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int reg, pcnt;

	if (!(*mac->tx_status & PAS_STATUS_CAUSE_M))
		return IRQ_NONE;

	pasemi_mac_clean_tx(mac);

	pcnt = *mac->tx_status & PAS_STATUS_PCNT_M;

	reg = PAS_IOB_DMA_TXCH_RESET_PCNT(pcnt) | PAS_IOB_DMA_TXCH_RESET_PINTC;

	if (*mac->tx_status & PAS_STATUS_SOFT)
		reg |= PAS_IOB_DMA_TXCH_RESET_SINTC;
	if (*mac->tx_status & PAS_STATUS_ERROR)
		reg |= PAS_IOB_DMA_TXCH_RESET_DINTC;

	write_iob_reg(mac, PAS_IOB_DMA_TXCH_RESET(mac->dma_txch), reg);

	return IRQ_HANDLED;
}

static void pasemi_adjust_link(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	int msg;
	unsigned int flags;
	unsigned int new_flags;

	if (!mac->phydev->link) {
		/* If no link, MAC speed settings don't matter. Just report
		 * link down and return.
		 */
		if (mac->link && netif_msg_link(mac))
			printk(KERN_INFO "%s: Link is down.\n", dev->name);

		netif_carrier_off(dev);
		mac->link = 0;

		return;
	} else
		netif_carrier_on(dev);

	flags = read_mac_reg(mac, PAS_MAC_CFG_PCFG);
	new_flags = flags & ~(PAS_MAC_CFG_PCFG_HD | PAS_MAC_CFG_PCFG_SPD_M |
			      PAS_MAC_CFG_PCFG_TSR_M);

	if (!mac->phydev->duplex)
		new_flags |= PAS_MAC_CFG_PCFG_HD;

	switch (mac->phydev->speed) {
	case 1000:
		new_flags |= PAS_MAC_CFG_PCFG_SPD_1G |
			     PAS_MAC_CFG_PCFG_TSR_1G;
		break;
	case 100:
		new_flags |= PAS_MAC_CFG_PCFG_SPD_100M |
			     PAS_MAC_CFG_PCFG_TSR_100M;
		break;
	case 10:
		new_flags |= PAS_MAC_CFG_PCFG_SPD_10M |
			     PAS_MAC_CFG_PCFG_TSR_10M;
		break;
	default:
		printk("Unsupported speed %d\n", mac->phydev->speed);
	}

	/* Print on link or speed/duplex change */
	msg = mac->link != mac->phydev->link || flags != new_flags;

	mac->duplex = mac->phydev->duplex;
	mac->speed = mac->phydev->speed;
	mac->link = mac->phydev->link;

	if (new_flags != flags)
		write_mac_reg(mac, PAS_MAC_CFG_PCFG, new_flags);

	if (msg && netif_msg_link(mac))
		printk(KERN_INFO "%s: Link is up at %d Mbps, %s duplex.\n",
		       dev->name, mac->speed, mac->duplex ? "full" : "half");
}

static int pasemi_mac_phy_init(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	struct device_node *dn, *phy_dn;
	struct phy_device *phydev;
	unsigned int phy_id;
	const phandle *ph;
	const unsigned int *prop;
	struct resource r;
	int ret;

	dn = pci_device_to_OF_node(mac->pdev);
	ph = of_get_property(dn, "phy-handle", NULL);
	if (!ph)
		return -ENODEV;
	phy_dn = of_find_node_by_phandle(*ph);

	prop = of_get_property(phy_dn, "reg", NULL);
	ret = of_address_to_resource(phy_dn->parent, 0, &r);
	if (ret)
		goto err;

	phy_id = *prop;
	snprintf(mac->phy_id, BUS_ID_SIZE, PHY_ID_FMT, (int)r.start, phy_id);

	of_node_put(phy_dn);

	mac->link = 0;
	mac->speed = 0;
	mac->duplex = -1;

	phydev = phy_connect(dev, mac->phy_id, &pasemi_adjust_link, 0, PHY_INTERFACE_MODE_SGMII);

	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to phy\n", dev->name);
		return PTR_ERR(phydev);
	}

	mac->phydev = phydev;

	return 0;

err:
	of_node_put(phy_dn);
	return -ENODEV;
}


static int pasemi_mac_open(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	int base_irq;
	unsigned int flags;
	int ret;

	/* enable rx section */
	write_dma_reg(mac, PAS_DMA_COM_RXCMD, PAS_DMA_COM_RXCMD_EN);

	/* enable tx section */
	write_dma_reg(mac, PAS_DMA_COM_TXCMD, PAS_DMA_COM_TXCMD_EN);

	flags = PAS_MAC_CFG_TXP_FCE | PAS_MAC_CFG_TXP_FPC(3) |
		PAS_MAC_CFG_TXP_SL(3) | PAS_MAC_CFG_TXP_COB(0xf) |
		PAS_MAC_CFG_TXP_TIFT(8) | PAS_MAC_CFG_TXP_TIFG(12);

	write_mac_reg(mac, PAS_MAC_CFG_TXP, flags);

	write_iob_reg(mac, PAS_IOB_DMA_RXCH_CFG(mac->dma_rxch),
			   PAS_IOB_DMA_RXCH_CFG_CNTTH(0));

	write_iob_reg(mac, PAS_IOB_DMA_TXCH_CFG(mac->dma_txch),
			   PAS_IOB_DMA_TXCH_CFG_CNTTH(128));

	/* Clear out any residual packet count state from firmware */
	pasemi_mac_restart_rx_intr(mac);
	pasemi_mac_restart_tx_intr(mac);

	/* 0xffffff is max value, about 16ms */
	write_iob_reg(mac, PAS_IOB_DMA_COM_TIMEOUTCFG,
			   PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT(0xffffff));

	ret = pasemi_mac_setup_rx_resources(dev);
	if (ret)
		goto out_rx_resources;

	ret = pasemi_mac_setup_tx_resources(dev);
	if (ret)
		goto out_tx_resources;

	write_mac_reg(mac, PAS_MAC_IPC_CHNL,
			   PAS_MAC_IPC_CHNL_DCHNO(mac->dma_rxch) |
			   PAS_MAC_IPC_CHNL_BCH(mac->dma_rxch));

	/* enable rx if */
	write_dma_reg(mac, PAS_DMA_RXINT_RCMDSTA(mac->dma_if),
			   PAS_DMA_RXINT_RCMDSTA_EN |
			   PAS_DMA_RXINT_RCMDSTA_DROPS_M |
			   PAS_DMA_RXINT_RCMDSTA_BP |
			   PAS_DMA_RXINT_RCMDSTA_OO |
			   PAS_DMA_RXINT_RCMDSTA_BT);

	/* enable rx channel */
	write_dma_reg(mac, PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch),
			   PAS_DMA_RXCHAN_CCMDSTA_EN |
			   PAS_DMA_RXCHAN_CCMDSTA_DU |
			   PAS_DMA_RXCHAN_CCMDSTA_OD |
			   PAS_DMA_RXCHAN_CCMDSTA_FD |
			   PAS_DMA_RXCHAN_CCMDSTA_DT);

	/* enable tx channel */
	write_dma_reg(mac, PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch),
			   PAS_DMA_TXCHAN_TCMDSTA_EN |
			   PAS_DMA_TXCHAN_TCMDSTA_SZ |
			   PAS_DMA_TXCHAN_TCMDSTA_DB |
			   PAS_DMA_TXCHAN_TCMDSTA_DE |
			   PAS_DMA_TXCHAN_TCMDSTA_DA);

	pasemi_mac_replenish_rx_ring(dev, RX_RING_SIZE);

	write_dma_reg(mac, PAS_DMA_RXCHAN_INCR(mac->dma_rxch), RX_RING_SIZE>>1);

	flags = PAS_MAC_CFG_PCFG_S1 | PAS_MAC_CFG_PCFG_PE |
		PAS_MAC_CFG_PCFG_PR | PAS_MAC_CFG_PCFG_CE;

	if (mac->type == MAC_TYPE_GMAC)
		flags |= PAS_MAC_CFG_PCFG_TSR_1G | PAS_MAC_CFG_PCFG_SPD_1G;
	else
		flags |= PAS_MAC_CFG_PCFG_TSR_10G | PAS_MAC_CFG_PCFG_SPD_10G;

	/* Enable interface in MAC */
	write_mac_reg(mac, PAS_MAC_CFG_PCFG, flags);

	ret = pasemi_mac_phy_init(dev);
	/* Some configs don't have PHYs (XAUI etc), so don't complain about
	 * failed init due to -ENODEV.
	 */
	if (ret && ret != -ENODEV)
		dev_warn(&mac->pdev->dev, "phy init failed: %d\n", ret);

	netif_start_queue(dev);
	napi_enable(&mac->napi);

	/* Interrupts are a bit different for our DMA controller: While
	 * it's got one a regular PCI device header, the interrupt there
	 * is really the base of the range it's using. Each tx and rx
	 * channel has it's own interrupt source.
	 */

	base_irq = virq_to_hw(mac->dma_pdev->irq);

	mac->tx_irq = irq_create_mapping(NULL, base_irq + mac->dma_txch);
	mac->rx_irq = irq_create_mapping(NULL, base_irq + 20 + mac->dma_txch);

	ret = request_irq(mac->tx_irq, &pasemi_mac_tx_intr, IRQF_DISABLED,
			  mac->tx->irq_name, dev);
	if (ret) {
		dev_err(&mac->pdev->dev, "request_irq of irq %d failed: %d\n",
			base_irq + mac->dma_txch, ret);
		goto out_tx_int;
	}

	ret = request_irq(mac->rx_irq, &pasemi_mac_rx_intr, IRQF_DISABLED,
			  mac->rx->irq_name, dev);
	if (ret) {
		dev_err(&mac->pdev->dev, "request_irq of irq %d failed: %d\n",
			base_irq + 20 + mac->dma_rxch, ret);
		goto out_rx_int;
	}

	if (mac->phydev)
		phy_start(mac->phydev);

	return 0;

out_rx_int:
	free_irq(mac->tx_irq, dev);
out_tx_int:
	napi_disable(&mac->napi);
	netif_stop_queue(dev);
	pasemi_mac_free_tx_resources(dev);
out_tx_resources:
	pasemi_mac_free_rx_resources(dev);
out_rx_resources:

	return ret;
}

#define MAX_RETRIES 5000

static int pasemi_mac_close(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int sta;
	int retries;

	if (mac->phydev) {
		phy_stop(mac->phydev);
		phy_disconnect(mac->phydev);
	}

	netif_stop_queue(dev);
	napi_disable(&mac->napi);

	sta = read_dma_reg(mac, PAS_DMA_RXINT_RCMDSTA(mac->dma_if));
	if (sta & (PAS_DMA_RXINT_RCMDSTA_BP |
		      PAS_DMA_RXINT_RCMDSTA_OO |
		      PAS_DMA_RXINT_RCMDSTA_BT))
		printk(KERN_DEBUG "pasemi_mac: rcmdsta error: 0x%08x\n", sta);

	sta = read_dma_reg(mac, PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch));
	if (sta & (PAS_DMA_RXCHAN_CCMDSTA_DU |
		     PAS_DMA_RXCHAN_CCMDSTA_OD |
		     PAS_DMA_RXCHAN_CCMDSTA_FD |
		     PAS_DMA_RXCHAN_CCMDSTA_DT))
		printk(KERN_DEBUG "pasemi_mac: ccmdsta error: 0x%08x\n", sta);

	sta = read_dma_reg(mac, PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch));
	if (sta & (PAS_DMA_TXCHAN_TCMDSTA_SZ |
		      PAS_DMA_TXCHAN_TCMDSTA_DB |
		      PAS_DMA_TXCHAN_TCMDSTA_DE |
		      PAS_DMA_TXCHAN_TCMDSTA_DA))
		printk(KERN_DEBUG "pasemi_mac: tcmdsta error: 0x%08x\n", sta);

	/* Clean out any pending buffers */
	pasemi_mac_clean_tx(mac);
	pasemi_mac_clean_rx(mac, RX_RING_SIZE);

	/* Disable interface */
	write_dma_reg(mac, PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch), PAS_DMA_TXCHAN_TCMDSTA_ST);
	write_dma_reg(mac, PAS_DMA_RXINT_RCMDSTA(mac->dma_if), PAS_DMA_RXINT_RCMDSTA_ST);
	write_dma_reg(mac, PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch), PAS_DMA_RXCHAN_CCMDSTA_ST);

	for (retries = 0; retries < MAX_RETRIES; retries++) {
		sta = read_dma_reg(mac, PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch));
		if (!(sta & PAS_DMA_TXCHAN_TCMDSTA_ACT))
			break;
		cond_resched();
	}

	if (sta & PAS_DMA_TXCHAN_TCMDSTA_ACT)
		dev_err(&mac->dma_pdev->dev, "Failed to stop tx channel\n");

	for (retries = 0; retries < MAX_RETRIES; retries++) {
		sta = read_dma_reg(mac, PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch));
		if (!(sta & PAS_DMA_RXCHAN_CCMDSTA_ACT))
			break;
		cond_resched();
	}

	if (sta & PAS_DMA_RXCHAN_CCMDSTA_ACT)
		dev_err(&mac->dma_pdev->dev, "Failed to stop rx channel\n");

	for (retries = 0; retries < MAX_RETRIES; retries++) {
		sta = read_dma_reg(mac, PAS_DMA_RXINT_RCMDSTA(mac->dma_if));
		if (!(sta & PAS_DMA_RXINT_RCMDSTA_ACT))
			break;
		cond_resched();
	}

	if (sta & PAS_DMA_RXINT_RCMDSTA_ACT)
		dev_err(&mac->dma_pdev->dev, "Failed to stop rx interface\n");

	/* Then, disable the channel. This must be done separately from
	 * stopping, since you can't disable when active.
	 */

	write_dma_reg(mac, PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch), 0);
	write_dma_reg(mac, PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch), 0);
	write_dma_reg(mac, PAS_DMA_RXINT_RCMDSTA(mac->dma_if), 0);

	free_irq(mac->tx_irq, dev);
	free_irq(mac->rx_irq, dev);

	/* Free resources */
	pasemi_mac_free_rx_resources(dev);
	pasemi_mac_free_tx_resources(dev);

	return 0;
}

static int pasemi_mac_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	struct pasemi_mac_txring *txring;
	u64 dflags, mactx;
	dma_addr_t map[MAX_SKB_FRAGS+1];
	unsigned int map_size[MAX_SKB_FRAGS+1];
	unsigned long flags;
	int i, nfrags;

	dflags = XCT_MACTX_O | XCT_MACTX_ST | XCT_MACTX_SS | XCT_MACTX_CRC_PAD;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const unsigned char *nh = skb_network_header(skb);

		switch (ip_hdr(skb)->protocol) {
		case IPPROTO_TCP:
			dflags |= XCT_MACTX_CSUM_TCP;
			dflags |= XCT_MACTX_IPH(skb_network_header_len(skb) >> 2);
			dflags |= XCT_MACTX_IPO(nh - skb->data);
			break;
		case IPPROTO_UDP:
			dflags |= XCT_MACTX_CSUM_UDP;
			dflags |= XCT_MACTX_IPH(skb_network_header_len(skb) >> 2);
			dflags |= XCT_MACTX_IPO(nh - skb->data);
			break;
		}
	}

	nfrags = skb_shinfo(skb)->nr_frags;

	map[0] = pci_map_single(mac->dma_pdev, skb->data, skb_headlen(skb),
				PCI_DMA_TODEVICE);
	map_size[0] = skb_headlen(skb);
	if (dma_mapping_error(map[0]))
		goto out_err_nolock;

	for (i = 0; i < nfrags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		map[i+1] = pci_map_page(mac->dma_pdev, frag->page,
					frag->page_offset, frag->size,
					PCI_DMA_TODEVICE);
		map_size[i+1] = frag->size;
		if (dma_mapping_error(map[i+1])) {
			nfrags = i;
			goto out_err_nolock;
		}
	}

	mactx = dflags | XCT_MACTX_LLEN(skb->len);

	txring = mac->tx;

	spin_lock_irqsave(&txring->lock, flags);

	/* Avoid stepping on the same cache line that the DMA controller
	 * is currently about to send, so leave at least 8 words available.
	 * Total free space needed is mactx + fragments + 8
	 */
	if (RING_AVAIL(txring) < nfrags + 10) {
		/* no room -- stop the queue and wait for tx intr */
		netif_stop_queue(dev);
		goto out_err;
	}

	TX_RING(mac, txring->next_to_fill) = mactx;
	txring->next_to_fill++;
	TX_RING_INFO(mac, txring->next_to_fill).skb = skb;
	for (i = 0; i <= nfrags; i++) {
		TX_RING(mac, txring->next_to_fill+i) =
		XCT_PTR_LEN(map_size[i]) | XCT_PTR_ADDR(map[i]);
		TX_RING_INFO(mac, txring->next_to_fill+i).dma = map[i];
	}

	/* We have to add an even number of 8-byte entries to the ring
	 * even if the last one is unused. That means always an odd number
	 * of pointers + one mactx descriptor.
	 */
	if (nfrags & 1)
		nfrags++;

	txring->next_to_fill = (txring->next_to_fill + nfrags + 1) &
				(TX_RING_SIZE-1);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	spin_unlock_irqrestore(&txring->lock, flags);

	write_dma_reg(mac, PAS_DMA_TXCHAN_INCR(mac->dma_txch), (nfrags+2) >> 1);

	return NETDEV_TX_OK;

out_err:
	spin_unlock_irqrestore(&txring->lock, flags);
out_err_nolock:
	while (nfrags--)
		pci_unmap_single(mac->dma_pdev, map[nfrags], map_size[nfrags],
				 PCI_DMA_TODEVICE);

	return NETDEV_TX_BUSY;
}

static void pasemi_mac_set_rx_mode(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int flags;

	flags = read_mac_reg(mac, PAS_MAC_CFG_PCFG);

	/* Set promiscuous */
	if (dev->flags & IFF_PROMISC)
		flags |= PAS_MAC_CFG_PCFG_PR;
	else
		flags &= ~PAS_MAC_CFG_PCFG_PR;

	write_mac_reg(mac, PAS_MAC_CFG_PCFG, flags);
}


static int pasemi_mac_poll(struct napi_struct *napi, int budget)
{
	struct pasemi_mac *mac = container_of(napi, struct pasemi_mac, napi);
	struct net_device *dev = mac->netdev;
	int pkts;

	pasemi_mac_clean_tx(mac);
	pkts = pasemi_mac_clean_rx(mac, budget);
	if (pkts < budget) {
		/* all done, no more packets present */
		netif_rx_complete(dev, napi);

		pasemi_mac_restart_rx_intr(mac);
	}
	return pkts;
}

static void __iomem * __devinit map_onedev(struct pci_dev *p, int index)
{
	struct device_node *dn;
	void __iomem *ret;

	dn = pci_device_to_OF_node(p);
	if (!dn)
		goto fallback;

	ret = of_iomap(dn, index);
	if (!ret)
		goto fallback;

	return ret;
fallback:
	/* This is hardcoded and ugly, but we have some firmware versions
	 * that don't provide the register space in the device tree. Luckily
	 * they are at well-known locations so we can just do the math here.
	 */
	return ioremap(0xe0000000 + (p->devfn << 12), 0x2000);
}

static int __devinit pasemi_mac_map_regs(struct pasemi_mac *mac)
{
	struct resource res;
	struct device_node *dn;
	int err;

	mac->dma_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa007, NULL);
	if (!mac->dma_pdev) {
		dev_err(&mac->pdev->dev, "Can't find DMA Controller\n");
		return -ENODEV;
	}

	mac->iob_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa001, NULL);
	if (!mac->iob_pdev) {
		dev_err(&mac->pdev->dev, "Can't find I/O Bridge\n");
		return -ENODEV;
	}

	mac->regs = map_onedev(mac->pdev, 0);
	mac->dma_regs = map_onedev(mac->dma_pdev, 0);
	mac->iob_regs = map_onedev(mac->iob_pdev, 0);

	if (!mac->regs || !mac->dma_regs || !mac->iob_regs) {
		dev_err(&mac->pdev->dev, "Can't map registers\n");
		return -ENODEV;
	}

	/* The dma status structure is located in the I/O bridge, and
	 * is cache coherent.
	 */
	if (!dma_status) {
		dn = pci_device_to_OF_node(mac->iob_pdev);
		if (dn)
			err = of_address_to_resource(dn, 1, &res);
		if (!dn || err) {
			/* Fallback for old firmware */
			res.start = 0xfd800000;
			res.end = res.start + 0x1000;
		}
		dma_status = __ioremap(res.start, res.end-res.start, 0);
	}

	return 0;
}

static int __devinit
pasemi_mac_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int index = 0;
	struct net_device *dev;
	struct pasemi_mac *mac;
	int err;
	DECLARE_MAC_BUF(mac_buf);

	err = pci_enable_device(pdev);
	if (err)
		return err;

	dev = alloc_etherdev(sizeof(struct pasemi_mac));
	if (dev == NULL) {
		dev_err(&pdev->dev,
			"pasemi_mac: Could not allocate ethernet device.\n");
		err = -ENOMEM;
		goto out_disable_device;
	}

	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	mac = netdev_priv(dev);

	mac->pdev = pdev;
	mac->netdev = dev;

	netif_napi_add(dev, &mac->napi, pasemi_mac_poll, 64);

	dev->features = NETIF_F_HW_CSUM | NETIF_F_LLTX | NETIF_F_SG;

	/* These should come out of the device tree eventually */
	mac->dma_txch = index;
	mac->dma_rxch = index;

	/* We probe GMAC before XAUI, but the DMA interfaces are
	 * in XAUI, GMAC order.
	 */
	if (index < 4)
		mac->dma_if = index + 2;
	else
		mac->dma_if = index - 4;
	index++;

	switch (pdev->device) {
	case 0xa005:
		mac->type = MAC_TYPE_GMAC;
		break;
	case 0xa006:
		mac->type = MAC_TYPE_XAUI;
		break;
	default:
		err = -ENODEV;
		goto out;
	}

	/* get mac addr from device tree */
	if (pasemi_get_mac_addr(mac) || !is_valid_ether_addr(mac->mac_addr)) {
		err = -ENODEV;
		goto out;
	}
	memcpy(dev->dev_addr, mac->mac_addr, sizeof(mac->mac_addr));

	dev->open = pasemi_mac_open;
	dev->stop = pasemi_mac_close;
	dev->hard_start_xmit = pasemi_mac_start_tx;
	dev->set_multicast_list = pasemi_mac_set_rx_mode;

	err = pasemi_mac_map_regs(mac);
	if (err)
		goto out;

	mac->rx_status = &dma_status->rx_sta[mac->dma_rxch];
	mac->tx_status = &dma_status->tx_sta[mac->dma_txch];

	mac->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	/* Enable most messages by default */
	mac->msg_enable = (NETIF_MSG_IFUP << 1 ) - 1;

	err = register_netdev(dev);

	if (err) {
		dev_err(&mac->pdev->dev, "register_netdev failed with error %d\n",
			err);
		goto out;
	} else if netif_msg_probe(mac)
		printk(KERN_INFO "%s: PA Semi %s: intf %d, txch %d, rxch %d, "
		       "hw addr %s\n",
		       dev->name, mac->type == MAC_TYPE_GMAC ? "GMAC" : "XAUI",
		       mac->dma_if, mac->dma_txch, mac->dma_rxch,
		       print_mac(mac_buf, dev->dev_addr));

	return err;

out:
	if (mac->iob_pdev)
		pci_dev_put(mac->iob_pdev);
	if (mac->dma_pdev)
		pci_dev_put(mac->dma_pdev);
	if (mac->dma_regs)
		iounmap(mac->dma_regs);
	if (mac->iob_regs)
		iounmap(mac->iob_regs);
	if (mac->regs)
		iounmap(mac->regs);

	free_netdev(dev);
out_disable_device:
	pci_disable_device(pdev);
	return err;

}

static void __devexit pasemi_mac_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pasemi_mac *mac;

	if (!netdev)
		return;

	mac = netdev_priv(netdev);

	unregister_netdev(netdev);

	pci_disable_device(pdev);
	pci_dev_put(mac->dma_pdev);
	pci_dev_put(mac->iob_pdev);

	iounmap(mac->regs);
	iounmap(mac->dma_regs);
	iounmap(mac->iob_regs);

	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
}

static struct pci_device_id pasemi_mac_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PASEMI, 0xa005) },
	{ PCI_DEVICE(PCI_VENDOR_ID_PASEMI, 0xa006) },
	{ },
};

MODULE_DEVICE_TABLE(pci, pasemi_mac_pci_tbl);

static struct pci_driver pasemi_mac_driver = {
	.name		= "pasemi_mac",
	.id_table	= pasemi_mac_pci_tbl,
	.probe		= pasemi_mac_probe,
	.remove		= __devexit_p(pasemi_mac_remove),
};

static void __exit pasemi_mac_cleanup_module(void)
{
	pci_unregister_driver(&pasemi_mac_driver);
	__iounmap(dma_status);
	dma_status = NULL;
}

int pasemi_mac_init_module(void)
{
	return pci_register_driver(&pasemi_mac_driver);
}

module_init(pasemi_mac_init_module);
module_exit(pasemi_mac_cleanup_module);
