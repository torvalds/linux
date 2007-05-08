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

#include "pasemi_mac.h"


/* TODO list
 *
 * - Get rid of pci_{read,write}_config(), map registers with ioremap
 *   for performance
 * - PHY support
 * - Multicast support
 * - Large MTU support
 * - Other performance improvements
 */


/* Must be a power of two */
#define RX_RING_SIZE 512
#define TX_RING_SIZE 512

#define TX_DESC(mac, num)	((mac)->tx->desc[(num) & (TX_RING_SIZE-1)])
#define TX_DESC_INFO(mac, num)	((mac)->tx->desc_info[(num) & (TX_RING_SIZE-1)])
#define RX_DESC(mac, num)	((mac)->rx->desc[(num) & (RX_RING_SIZE-1)])
#define RX_DESC_INFO(mac, num)	((mac)->rx->desc_info[(num) & (RX_RING_SIZE-1)])
#define RX_BUFF(mac, num)	((mac)->rx->buffers[(num) & (RX_RING_SIZE-1)])

#define BUF_SIZE 1646 /* 1500 MTU + ETH_HLEN + VLAN_HLEN + 2 64B cachelines */

/* XXXOJN these should come out of the device tree some day */
#define PAS_DMA_CAP_BASE   0xe00d0040
#define PAS_DMA_CAP_SIZE   0x100
#define PAS_DMA_COM_BASE   0xe00d0100
#define PAS_DMA_COM_SIZE   0x100

static struct pasdma_status *dma_status;

static int pasemi_get_mac_addr(struct pasemi_mac *mac)
{
	struct pci_dev *pdev = mac->pdev;
	struct device_node *dn = pci_device_to_OF_node(pdev);
	const u8 *maddr;
	u8 addr[6];

	if (!dn) {
		dev_dbg(&pdev->dev,
			  "No device node for mac, not configuring\n");
		return -ENOENT;
	}

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

	memcpy(mac->mac_addr, addr, sizeof(addr));
	return 0;
}

static int pasemi_mac_setup_rx_resources(struct net_device *dev)
{
	struct pasemi_mac_rxring *ring;
	struct pasemi_mac *mac = netdev_priv(dev);
	int chan_id = mac->dma_rxch;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);

	if (!ring)
		goto out_ring;

	spin_lock_init(&ring->lock);

	ring->desc_info = kzalloc(sizeof(struct pasemi_mac_buffer) *
				  RX_RING_SIZE, GFP_KERNEL);

	if (!ring->desc_info)
		goto out_desc_info;

	/* Allocate descriptors */
	ring->desc = dma_alloc_coherent(&mac->dma_pdev->dev,
					RX_RING_SIZE *
					sizeof(struct pas_dma_xct_descr),
					&ring->dma, GFP_KERNEL);

	if (!ring->desc)
		goto out_desc;

	memset(ring->desc, 0, RX_RING_SIZE * sizeof(struct pas_dma_xct_descr));

	ring->buffers = dma_alloc_coherent(&mac->dma_pdev->dev,
					   RX_RING_SIZE * sizeof(u64),
					   &ring->buf_dma, GFP_KERNEL);
	if (!ring->buffers)
		goto out_buffers;

	memset(ring->buffers, 0, RX_RING_SIZE * sizeof(u64));

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_RXCHAN_BASEL(chan_id),
			       PAS_DMA_RXCHAN_BASEL_BRBL(ring->dma));

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_RXCHAN_BASEU(chan_id),
			       PAS_DMA_RXCHAN_BASEU_BRBH(ring->dma >> 32) |
			       PAS_DMA_RXCHAN_BASEU_SIZ(RX_RING_SIZE >> 2));

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_RXCHAN_CFG(chan_id),
			       PAS_DMA_RXCHAN_CFG_HBU(1));

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_RXINT_BASEL(mac->dma_if),
			       PAS_DMA_RXINT_BASEL_BRBL(__pa(ring->buffers)));

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_RXINT_BASEU(mac->dma_if),
			       PAS_DMA_RXINT_BASEU_BRBH(__pa(ring->buffers) >> 32) |
			       PAS_DMA_RXINT_BASEU_SIZ(RX_RING_SIZE >> 3));

	ring->next_to_fill = 0;
	ring->next_to_clean = 0;

	snprintf(ring->irq_name, sizeof(ring->irq_name),
		 "%s rx", dev->name);
	mac->rx = ring;

	return 0;

out_buffers:
	dma_free_coherent(&mac->dma_pdev->dev,
			  RX_RING_SIZE * sizeof(struct pas_dma_xct_descr),
			  mac->rx->desc, mac->rx->dma);
out_desc:
	kfree(ring->desc_info);
out_desc_info:
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

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		goto out_ring;

	spin_lock_init(&ring->lock);

	ring->desc_info = kzalloc(sizeof(struct pasemi_mac_buffer) *
				  TX_RING_SIZE, GFP_KERNEL);
	if (!ring->desc_info)
		goto out_desc_info;

	/* Allocate descriptors */
	ring->desc = dma_alloc_coherent(&mac->dma_pdev->dev,
					TX_RING_SIZE *
					sizeof(struct pas_dma_xct_descr),
					&ring->dma, GFP_KERNEL);
	if (!ring->desc)
		goto out_desc;

	memset(ring->desc, 0, TX_RING_SIZE * sizeof(struct pas_dma_xct_descr));

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_TXCHAN_BASEL(chan_id),
			       PAS_DMA_TXCHAN_BASEL_BRBL(ring->dma));
	val = PAS_DMA_TXCHAN_BASEU_BRBH(ring->dma >> 32);
	val |= PAS_DMA_TXCHAN_BASEU_SIZ(TX_RING_SIZE >> 2);

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_TXCHAN_BASEU(chan_id), val);

	pci_write_config_dword(mac->dma_pdev, PAS_DMA_TXCHAN_CFG(chan_id),
			       PAS_DMA_TXCHAN_CFG_TY_IFACE |
			       PAS_DMA_TXCHAN_CFG_TATTR(mac->dma_if) |
			       PAS_DMA_TXCHAN_CFG_UP |
			       PAS_DMA_TXCHAN_CFG_WT(2));

	ring->next_to_use = 0;
	ring->next_to_clean = 0;

	snprintf(ring->irq_name, sizeof(ring->irq_name),
		 "%s tx", dev->name);
	mac->tx = ring;

	return 0;

out_desc:
	kfree(ring->desc_info);
out_desc_info:
	kfree(ring);
out_ring:
	return -ENOMEM;
}

static void pasemi_mac_free_tx_resources(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int i;
	struct pasemi_mac_buffer *info;
	struct pas_dma_xct_descr *dp;

	for (i = 0; i < TX_RING_SIZE; i++) {
		info = &TX_DESC_INFO(mac, i);
		dp = &TX_DESC(mac, i);
		if (info->dma) {
			if (info->skb) {
				pci_unmap_single(mac->dma_pdev,
						 info->dma,
						 info->skb->len,
						 PCI_DMA_TODEVICE);
				dev_kfree_skb_any(info->skb);
			}
			info->dma = 0;
			info->skb = NULL;
			dp->mactx = 0;
			dp->ptr = 0;
		}
	}

	dma_free_coherent(&mac->dma_pdev->dev,
			  TX_RING_SIZE * sizeof(struct pas_dma_xct_descr),
			  mac->tx->desc, mac->tx->dma);

	kfree(mac->tx->desc_info);
	kfree(mac->tx);
	mac->tx = NULL;
}

static void pasemi_mac_free_rx_resources(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int i;
	struct pasemi_mac_buffer *info;
	struct pas_dma_xct_descr *dp;

	for (i = 0; i < RX_RING_SIZE; i++) {
		info = &RX_DESC_INFO(mac, i);
		dp = &RX_DESC(mac, i);
		if (info->dma) {
			if (info->skb) {
				pci_unmap_single(mac->dma_pdev,
						 info->dma,
						 info->skb->len,
						 PCI_DMA_FROMDEVICE);
				dev_kfree_skb_any(info->skb);
			}
			info->dma = 0;
			info->skb = NULL;
			dp->macrx = 0;
			dp->ptr = 0;
		}
	}

	dma_free_coherent(&mac->dma_pdev->dev,
			  RX_RING_SIZE * sizeof(struct pas_dma_xct_descr),
			  mac->rx->desc, mac->rx->dma);

	dma_free_coherent(&mac->dma_pdev->dev, RX_RING_SIZE * sizeof(u64),
			  mac->rx->buffers, mac->rx->buf_dma);

	kfree(mac->rx->desc_info);
	kfree(mac->rx);
	mac->rx = NULL;
}

static void pasemi_mac_replenish_rx_ring(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int i;
	int start = mac->rx->next_to_fill;
	unsigned int count;

	count = (mac->rx->next_to_clean + RX_RING_SIZE -
		 mac->rx->next_to_fill) & (RX_RING_SIZE - 1);

	/* Check to see if we're doing first-time setup */
	if (unlikely(mac->rx->next_to_clean == 0 && mac->rx->next_to_fill == 0))
		count = RX_RING_SIZE;

	if (count <= 0)
		return;

	for (i = start; i < start + count; i++) {
		struct pasemi_mac_buffer *info = &RX_DESC_INFO(mac, i);
		u64 *buff = &RX_BUFF(mac, i);
		struct sk_buff *skb;
		dma_addr_t dma;

		skb = dev_alloc_skb(BUF_SIZE);

		if (!skb) {
			count = i - start;
			break;
		}

		dma = pci_map_single(mac->dma_pdev, skb->data, skb->len,
				     PCI_DMA_FROMDEVICE);

		if (dma_mapping_error(dma)) {
			dev_kfree_skb_irq(info->skb);
			count = i - start;
			break;
		}

		info->skb = skb;
		info->dma = dma;
		*buff = XCT_RXB_LEN(BUF_SIZE) | XCT_RXB_ADDR(dma);
	}

	wmb();

	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_RXCHAN_INCR(mac->dma_rxch),
			       count);
	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_RXINT_INCR(mac->dma_if),
			       count);

	mac->rx->next_to_fill += count;
}

static int pasemi_mac_clean_rx(struct pasemi_mac *mac, int limit)
{
	unsigned int i;
	int start, count;

	spin_lock(&mac->rx->lock);

	start = mac->rx->next_to_clean;
	count = 0;

	for (i = start; i < (start + RX_RING_SIZE) && count < limit; i++) {
		struct pas_dma_xct_descr *dp;
		struct pasemi_mac_buffer *info;
		struct sk_buff *skb;
		unsigned int j, len;
		dma_addr_t dma;

		rmb();

		dp = &RX_DESC(mac, i);

		if (!(dp->macrx & XCT_MACRX_O))
			break;

		count++;

		info = NULL;

		/* We have to scan for our skb since there's no way
		 * to back-map them from the descriptor, and if we
		 * have several receive channels then they might not
		 * show up in the same order as they were put on the
		 * interface ring.
		 */

		dma = (dp->ptr & XCT_PTR_ADDR_M);
		for (j = start; j < (start + RX_RING_SIZE); j++) {
			info = &RX_DESC_INFO(mac, j);
			if (info->dma == dma)
				break;
		}

		BUG_ON(!info);
		BUG_ON(info->dma != dma);

		pci_unmap_single(mac->dma_pdev, info->dma, info->skb->len,
				 PCI_DMA_FROMDEVICE);

		skb = info->skb;

		len = (dp->macrx & XCT_MACRX_LLEN_M) >> XCT_MACRX_LLEN_S;

		skb_put(skb, len);

		skb->protocol = eth_type_trans(skb, mac->netdev);

		if ((dp->macrx & XCT_MACRX_HTY_M) == XCT_MACRX_HTY_IPV4_OK) {
			skb->ip_summed = CHECKSUM_COMPLETE;
			skb->csum = (dp->macrx & XCT_MACRX_CSUM_M) >>
					   XCT_MACRX_CSUM_S;
		} else
			skb->ip_summed = CHECKSUM_NONE;

		mac->stats.rx_bytes += len;
		mac->stats.rx_packets++;

		netif_receive_skb(skb);

		info->dma = 0;
		info->skb = NULL;
		dp->ptr = 0;
		dp->macrx = 0;
	}

	mac->rx->next_to_clean += count;
	pasemi_mac_replenish_rx_ring(mac->netdev);

	spin_unlock(&mac->rx->lock);

	return count;
}

static int pasemi_mac_clean_tx(struct pasemi_mac *mac)
{
	int i;
	struct pasemi_mac_buffer *info;
	struct pas_dma_xct_descr *dp;
	int start, count;
	int flags;

	spin_lock_irqsave(&mac->tx->lock, flags);

	start = mac->tx->next_to_clean;
	count = 0;

	for (i = start; i < mac->tx->next_to_use; i++) {
		dp = &TX_DESC(mac, i);
		if (!dp || (dp->mactx & XCT_MACTX_O))
			break;

		count++;

		info = &TX_DESC_INFO(mac, i);

		pci_unmap_single(mac->dma_pdev, info->dma,
				 info->skb->len, PCI_DMA_TODEVICE);
		dev_kfree_skb_irq(info->skb);

		info->skb = NULL;
		info->dma = 0;
		dp->mactx = 0;
		dp->ptr = 0;
	}
	mac->tx->next_to_clean += count;
	spin_unlock_irqrestore(&mac->tx->lock, flags);

	return count;
}


static irqreturn_t pasemi_mac_rx_intr(int irq, void *data)
{
	struct net_device *dev = data;
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int reg;

	if (!(*mac->rx_status & PAS_STATUS_INT))
		return IRQ_NONE;

	netif_rx_schedule(dev);
	pci_write_config_dword(mac->iob_pdev, PAS_IOB_DMA_COM_TIMEOUTCFG,
			       PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT(0));

	reg = PAS_IOB_DMA_RXCH_RESET_PINTC | PAS_IOB_DMA_RXCH_RESET_SINTC |
	      PAS_IOB_DMA_RXCH_RESET_DINTC;
	if (*mac->rx_status & PAS_STATUS_TIMER)
		reg |= PAS_IOB_DMA_RXCH_RESET_TINTC;

	pci_write_config_dword(mac->iob_pdev,
			       PAS_IOB_DMA_RXCH_RESET(mac->dma_rxch), reg);


	return IRQ_HANDLED;
}

static irqreturn_t pasemi_mac_tx_intr(int irq, void *data)
{
	struct net_device *dev = data;
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int reg;
	int was_full;

	was_full = mac->tx->next_to_clean - mac->tx->next_to_use == TX_RING_SIZE;

	if (!(*mac->tx_status & PAS_STATUS_INT))
		return IRQ_NONE;

	pasemi_mac_clean_tx(mac);

	reg = PAS_IOB_DMA_TXCH_RESET_PINTC | PAS_IOB_DMA_TXCH_RESET_SINTC;
	if (*mac->tx_status & PAS_STATUS_TIMER)
		reg |= PAS_IOB_DMA_TXCH_RESET_TINTC;

	pci_write_config_dword(mac->iob_pdev, PAS_IOB_DMA_TXCH_RESET(mac->dma_txch),
			       reg);

	if (was_full)
		netif_wake_queue(dev);

	return IRQ_HANDLED;
}

static int pasemi_mac_open(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int flags;
	int ret;

	/* enable rx section */
	pci_write_config_dword(mac->dma_pdev, PAS_DMA_COM_RXCMD,
			       PAS_DMA_COM_RXCMD_EN);

	/* enable tx section */
	pci_write_config_dword(mac->dma_pdev, PAS_DMA_COM_TXCMD,
			       PAS_DMA_COM_TXCMD_EN);

	flags = PAS_MAC_CFG_TXP_FCE | PAS_MAC_CFG_TXP_FPC(3) |
		PAS_MAC_CFG_TXP_SL(3) | PAS_MAC_CFG_TXP_COB(0xf) |
		PAS_MAC_CFG_TXP_TIFT(8) | PAS_MAC_CFG_TXP_TIFG(12);

	pci_write_config_dword(mac->pdev, PAS_MAC_CFG_TXP, flags);

	flags = PAS_MAC_CFG_PCFG_S1 | PAS_MAC_CFG_PCFG_PE |
		PAS_MAC_CFG_PCFG_PR | PAS_MAC_CFG_PCFG_CE;

	flags |= PAS_MAC_CFG_PCFG_TSR_1G | PAS_MAC_CFG_PCFG_SPD_1G;

	pci_write_config_dword(mac->iob_pdev, PAS_IOB_DMA_RXCH_CFG(mac->dma_rxch),
			       PAS_IOB_DMA_RXCH_CFG_CNTTH(30));

	pci_write_config_dword(mac->iob_pdev, PAS_IOB_DMA_COM_TIMEOUTCFG,
			       PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT(1000000));

	pci_write_config_dword(mac->pdev, PAS_MAC_CFG_PCFG, flags);

	ret = pasemi_mac_setup_rx_resources(dev);
	if (ret)
		goto out_rx_resources;

	ret = pasemi_mac_setup_tx_resources(dev);
	if (ret)
		goto out_tx_resources;

	pci_write_config_dword(mac->pdev, PAS_MAC_IPC_CHNL,
			       PAS_MAC_IPC_CHNL_DCHNO(mac->dma_rxch) |
			       PAS_MAC_IPC_CHNL_BCH(mac->dma_rxch));

	/* enable rx if */
	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_RXINT_RCMDSTA(mac->dma_if),
			       PAS_DMA_RXINT_RCMDSTA_EN);

	/* enable rx channel */
	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch),
			       PAS_DMA_RXCHAN_CCMDSTA_EN |
			       PAS_DMA_RXCHAN_CCMDSTA_DU);

	/* enable tx channel */
	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch),
			       PAS_DMA_TXCHAN_TCMDSTA_EN);

	pasemi_mac_replenish_rx_ring(dev);

	netif_start_queue(dev);
	netif_poll_enable(dev);

	ret = request_irq(mac->dma_pdev->irq + mac->dma_txch,
			  &pasemi_mac_tx_intr, IRQF_DISABLED,
			  mac->tx->irq_name, dev);
	if (ret) {
		dev_err(&mac->pdev->dev, "request_irq of irq %d failed: %d\n",
		       mac->dma_pdev->irq + mac->dma_txch, ret);
		goto out_tx_int;
	}

	ret = request_irq(mac->dma_pdev->irq + 20 + mac->dma_rxch,
			  &pasemi_mac_rx_intr, IRQF_DISABLED,
			  mac->rx->irq_name, dev);
	if (ret) {
		dev_err(&mac->pdev->dev, "request_irq of irq %d failed: %d\n",
		       mac->dma_pdev->irq + 20 + mac->dma_rxch, ret);
		goto out_rx_int;
	}

	return 0;

out_rx_int:
	free_irq(mac->dma_pdev->irq + mac->dma_txch, dev);
out_tx_int:
	netif_poll_disable(dev);
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
	unsigned int stat;
	int retries;

	netif_stop_queue(dev);

	/* Clean out any pending buffers */
	pasemi_mac_clean_tx(mac);
	pasemi_mac_clean_rx(mac, RX_RING_SIZE);

	/* Disable interface */
	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch),
			       PAS_DMA_TXCHAN_TCMDSTA_ST);
	pci_write_config_dword(mac->dma_pdev,
		      PAS_DMA_RXINT_RCMDSTA(mac->dma_if),
		      PAS_DMA_RXINT_RCMDSTA_ST);
	pci_write_config_dword(mac->dma_pdev,
		      PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch),
		      PAS_DMA_RXCHAN_CCMDSTA_ST);

	for (retries = 0; retries < MAX_RETRIES; retries++) {
		pci_read_config_dword(mac->dma_pdev,
				      PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch),
				      &stat);
		if (stat & PAS_DMA_TXCHAN_TCMDSTA_ACT)
			break;
		cond_resched();
	}

	if (!(stat & PAS_DMA_TXCHAN_TCMDSTA_ACT)) {
		dev_err(&mac->dma_pdev->dev, "Failed to stop tx channel\n");
	}

	for (retries = 0; retries < MAX_RETRIES; retries++) {
		pci_read_config_dword(mac->dma_pdev,
				      PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch),
				      &stat);
		if (stat & PAS_DMA_RXCHAN_CCMDSTA_ACT)
			break;
		cond_resched();
	}

	if (!(stat & PAS_DMA_RXCHAN_CCMDSTA_ACT)) {
		dev_err(&mac->dma_pdev->dev, "Failed to stop rx channel\n");
	}

	for (retries = 0; retries < MAX_RETRIES; retries++) {
		pci_read_config_dword(mac->dma_pdev,
				      PAS_DMA_RXINT_RCMDSTA(mac->dma_if),
				      &stat);
		if (stat & PAS_DMA_RXINT_RCMDSTA_ACT)
			break;
		cond_resched();
	}

	if (!(stat & PAS_DMA_RXINT_RCMDSTA_ACT)) {
		dev_err(&mac->dma_pdev->dev, "Failed to stop rx interface\n");
	}

	/* Then, disable the channel. This must be done separately from
	 * stopping, since you can't disable when active.
	 */

	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_TXCHAN_TCMDSTA(mac->dma_txch), 0);
	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_RXCHAN_CCMDSTA(mac->dma_rxch), 0);
	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_RXINT_RCMDSTA(mac->dma_if), 0);

	free_irq(mac->dma_pdev->irq + mac->dma_txch, dev);
	free_irq(mac->dma_pdev->irq + 20 + mac->dma_rxch, dev);

	/* Free resources */
	pasemi_mac_free_rx_resources(dev);
	pasemi_mac_free_tx_resources(dev);

	return 0;
}

static int pasemi_mac_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	struct pasemi_mac_txring *txring;
	struct pasemi_mac_buffer *info;
	struct pas_dma_xct_descr *dp;
	u64 dflags;
	dma_addr_t map;
	int flags;

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

	map = pci_map_single(mac->dma_pdev, skb->data, skb->len, PCI_DMA_TODEVICE);

	if (dma_mapping_error(map))
		return NETDEV_TX_BUSY;

	txring = mac->tx;

	spin_lock_irqsave(&txring->lock, flags);

	if (txring->next_to_clean - txring->next_to_use == TX_RING_SIZE) {
		spin_unlock_irqrestore(&txring->lock, flags);
		pasemi_mac_clean_tx(mac);
		spin_lock_irqsave(&txring->lock, flags);

		if (txring->next_to_clean - txring->next_to_use ==
		    TX_RING_SIZE) {
			/* Still no room -- stop the queue and wait for tx
			 * intr when there's room.
			 */
			netif_stop_queue(dev);
			goto out_err;
		}
	}


	dp = &TX_DESC(mac, txring->next_to_use);
	info = &TX_DESC_INFO(mac, txring->next_to_use);

	dp->mactx = dflags | XCT_MACTX_LLEN(skb->len);
	dp->ptr   = XCT_PTR_LEN(skb->len) | XCT_PTR_ADDR(map);
	info->dma = map;
	info->skb = skb;

	txring->next_to_use++;
	mac->stats.tx_packets++;
	mac->stats.tx_bytes += skb->len;

	spin_unlock_irqrestore(&txring->lock, flags);

	pci_write_config_dword(mac->dma_pdev,
			       PAS_DMA_TXCHAN_INCR(mac->dma_txch), 1);

	return NETDEV_TX_OK;

out_err:
	spin_unlock_irqrestore(&txring->lock, flags);
	pci_unmap_single(mac->dma_pdev, map, skb->len, PCI_DMA_TODEVICE);
	return NETDEV_TX_BUSY;
}

static struct net_device_stats *pasemi_mac_get_stats(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);

	return &mac->stats;
}

static void pasemi_mac_set_rx_mode(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int flags;

	pci_read_config_dword(mac->pdev, PAS_MAC_CFG_PCFG, &flags);

	/* Set promiscuous */
	if (dev->flags & IFF_PROMISC)
		flags |= PAS_MAC_CFG_PCFG_PR;
	else
		flags &= ~PAS_MAC_CFG_PCFG_PR;

	pci_write_config_dword(mac->pdev, PAS_MAC_CFG_PCFG, flags);
}


static int pasemi_mac_poll(struct net_device *dev, int *budget)
{
	int pkts, limit = min(*budget, dev->quota);
	struct pasemi_mac *mac = netdev_priv(dev);

	pkts = pasemi_mac_clean_rx(mac, limit);

	if (pkts < limit) {
		/* all done, no more packets present */
		netif_rx_complete(dev);

		/* re-enable receive interrupts */
		pci_write_config_dword(mac->iob_pdev, PAS_IOB_DMA_COM_TIMEOUTCFG,
				       PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT(1000000));
		return 0;
	} else {
		/* used up our quantum, so reschedule */
		dev->quota -= pkts;
		*budget -= pkts;
		return 1;
	}
}

static int __devinit
pasemi_mac_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int index = 0;
	struct net_device *dev;
	struct pasemi_mac *mac;
	int err;

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

	SET_MODULE_OWNER(dev);
	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	mac = netdev_priv(dev);

	mac->pdev = pdev;
	mac->netdev = dev;
	mac->dma_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa007, NULL);

	if (!mac->dma_pdev) {
		dev_err(&pdev->dev, "Can't find DMA Controller\n");
		err = -ENODEV;
		goto out_free_netdev;
	}

	mac->iob_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa001, NULL);

	if (!mac->iob_pdev) {
		dev_err(&pdev->dev, "Can't find I/O Bridge\n");
		err = -ENODEV;
		goto out_put_dma_pdev;
	}

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
	dev->get_stats = pasemi_mac_get_stats;
	dev->set_multicast_list = pasemi_mac_set_rx_mode;
	dev->weight = 64;
	dev->poll = pasemi_mac_poll;
	dev->features = NETIF_F_HW_CSUM;

	/* The dma status structure is located in the I/O bridge, and
	 * is cache coherent.
	 */
	if (!dma_status)
		/* XXXOJN This should come from the device tree */
		dma_status = __ioremap(0xfd800000, 0x1000, 0);

	mac->rx_status = &dma_status->rx_sta[mac->dma_rxch];
	mac->tx_status = &dma_status->tx_sta[mac->dma_txch];

	err = register_netdev(dev);

	if (err) {
		dev_err(&mac->pdev->dev, "register_netdev failed with error %d\n",
			err);
		goto out;
	} else
		printk(KERN_INFO "%s: PA Semi %s: intf %d, txch %d, rxch %d, "
		       "hw addr %02x:%02x:%02x:%02x:%02x:%02x\n",
		       dev->name, mac->type == MAC_TYPE_GMAC ? "GMAC" : "XAUI",
		       mac->dma_if, mac->dma_txch, mac->dma_rxch,
		       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	return err;

out:
	pci_dev_put(mac->iob_pdev);
out_put_dma_pdev:
	pci_dev_put(mac->dma_pdev);
out_free_netdev:
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

	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
}

static struct pci_device_id pasemi_mac_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PASEMI, 0xa005) },
	{ PCI_DEVICE(PCI_VENDOR_ID_PASEMI, 0xa006) },
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

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi PWRficient Ethernet driver");

module_init(pasemi_mac_init_module);
module_exit(pasemi_mac_cleanup_module);
