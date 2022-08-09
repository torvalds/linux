/*
 * Copyright (c) 2013, 2021 Johannes Berg <johannes@sipsolutions.net>
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <linux/mdio.h>
#include <linux/aer.h>
#include <linux/bitops.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/ip6_checksum.h>
#include <linux/crc32.h>
#include "alx.h"
#include "hw.h"
#include "reg.h"

static const char alx_drv_name[] = "alx";

static void alx_free_txbuf(struct alx_tx_queue *txq, int entry)
{
	struct alx_buffer *txb = &txq->bufs[entry];

	if (dma_unmap_len(txb, size)) {
		dma_unmap_single(txq->dev,
				 dma_unmap_addr(txb, dma),
				 dma_unmap_len(txb, size),
				 DMA_TO_DEVICE);
		dma_unmap_len_set(txb, size, 0);
	}

	if (txb->skb) {
		dev_kfree_skb_any(txb->skb);
		txb->skb = NULL;
	}
}

static int alx_refill_rx_ring(struct alx_priv *alx, gfp_t gfp)
{
	struct alx_rx_queue *rxq = alx->qnapi[0]->rxq;
	struct sk_buff *skb;
	struct alx_buffer *cur_buf;
	dma_addr_t dma;
	u16 cur, next, count = 0;

	next = cur = rxq->write_idx;
	if (++next == alx->rx_ringsz)
		next = 0;
	cur_buf = &rxq->bufs[cur];

	while (!cur_buf->skb && next != rxq->read_idx) {
		struct alx_rfd *rfd = &rxq->rfd[cur];

		/*
		 * When DMA RX address is set to something like
		 * 0x....fc0, it will be very likely to cause DMA
		 * RFD overflow issue.
		 *
		 * To work around it, we apply rx skb with 64 bytes
		 * longer space, and offset the address whenever
		 * 0x....fc0 is detected.
		 */
		skb = __netdev_alloc_skb(alx->dev, alx->rxbuf_size + 64, gfp);
		if (!skb)
			break;

		if (((unsigned long)skb->data & 0xfff) == 0xfc0)
			skb_reserve(skb, 64);

		dma = dma_map_single(&alx->hw.pdev->dev,
				     skb->data, alx->rxbuf_size,
				     DMA_FROM_DEVICE);
		if (dma_mapping_error(&alx->hw.pdev->dev, dma)) {
			dev_kfree_skb(skb);
			break;
		}

		/* Unfortunately, RX descriptor buffers must be 4-byte
		 * aligned, so we can't use IP alignment.
		 */
		if (WARN_ON(dma & 3)) {
			dev_kfree_skb(skb);
			break;
		}

		cur_buf->skb = skb;
		dma_unmap_len_set(cur_buf, size, alx->rxbuf_size);
		dma_unmap_addr_set(cur_buf, dma, dma);
		rfd->addr = cpu_to_le64(dma);

		cur = next;
		if (++next == alx->rx_ringsz)
			next = 0;
		cur_buf = &rxq->bufs[cur];
		count++;
	}

	if (count) {
		/* flush all updates before updating hardware */
		wmb();
		rxq->write_idx = cur;
		alx_write_mem16(&alx->hw, ALX_RFD_PIDX, cur);
	}

	return count;
}

static struct alx_tx_queue *alx_tx_queue_mapping(struct alx_priv *alx,
						 struct sk_buff *skb)
{
	unsigned int r_idx = skb->queue_mapping;

	if (r_idx >= alx->num_txq)
		r_idx = r_idx % alx->num_txq;

	return alx->qnapi[r_idx]->txq;
}

static struct netdev_queue *alx_get_tx_queue(const struct alx_tx_queue *txq)
{
	return netdev_get_tx_queue(txq->netdev, txq->queue_idx);
}

static inline int alx_tpd_avail(struct alx_tx_queue *txq)
{
	if (txq->write_idx >= txq->read_idx)
		return txq->count + txq->read_idx - txq->write_idx - 1;
	return txq->read_idx - txq->write_idx - 1;
}

static bool alx_clean_tx_irq(struct alx_tx_queue *txq)
{
	struct alx_priv *alx;
	struct netdev_queue *tx_queue;
	u16 hw_read_idx, sw_read_idx;
	unsigned int total_bytes = 0, total_packets = 0;
	int budget = ALX_DEFAULT_TX_WORK;

	alx = netdev_priv(txq->netdev);
	tx_queue = alx_get_tx_queue(txq);

	sw_read_idx = txq->read_idx;
	hw_read_idx = alx_read_mem16(&alx->hw, txq->c_reg);

	if (sw_read_idx != hw_read_idx) {
		while (sw_read_idx != hw_read_idx && budget > 0) {
			struct sk_buff *skb;

			skb = txq->bufs[sw_read_idx].skb;
			if (skb) {
				total_bytes += skb->len;
				total_packets++;
				budget--;
			}

			alx_free_txbuf(txq, sw_read_idx);

			if (++sw_read_idx == txq->count)
				sw_read_idx = 0;
		}
		txq->read_idx = sw_read_idx;

		netdev_tx_completed_queue(tx_queue, total_packets, total_bytes);
	}

	if (netif_tx_queue_stopped(tx_queue) && netif_carrier_ok(alx->dev) &&
	    alx_tpd_avail(txq) > txq->count / 4)
		netif_tx_wake_queue(tx_queue);

	return sw_read_idx == hw_read_idx;
}

static void alx_schedule_link_check(struct alx_priv *alx)
{
	schedule_work(&alx->link_check_wk);
}

static void alx_schedule_reset(struct alx_priv *alx)
{
	schedule_work(&alx->reset_wk);
}

static int alx_clean_rx_irq(struct alx_rx_queue *rxq, int budget)
{
	struct alx_priv *alx;
	struct alx_rrd *rrd;
	struct alx_buffer *rxb;
	struct sk_buff *skb;
	u16 length, rfd_cleaned = 0;
	int work = 0;

	alx = netdev_priv(rxq->netdev);

	while (work < budget) {
		rrd = &rxq->rrd[rxq->rrd_read_idx];
		if (!(rrd->word3 & cpu_to_le32(1 << RRD_UPDATED_SHIFT)))
			break;
		rrd->word3 &= ~cpu_to_le32(1 << RRD_UPDATED_SHIFT);

		if (ALX_GET_FIELD(le32_to_cpu(rrd->word0),
				  RRD_SI) != rxq->read_idx ||
		    ALX_GET_FIELD(le32_to_cpu(rrd->word0),
				  RRD_NOR) != 1) {
			alx_schedule_reset(alx);
			return work;
		}

		rxb = &rxq->bufs[rxq->read_idx];
		dma_unmap_single(rxq->dev,
				 dma_unmap_addr(rxb, dma),
				 dma_unmap_len(rxb, size),
				 DMA_FROM_DEVICE);
		dma_unmap_len_set(rxb, size, 0);
		skb = rxb->skb;
		rxb->skb = NULL;

		if (rrd->word3 & cpu_to_le32(1 << RRD_ERR_RES_SHIFT) ||
		    rrd->word3 & cpu_to_le32(1 << RRD_ERR_LEN_SHIFT)) {
			rrd->word3 = 0;
			dev_kfree_skb_any(skb);
			goto next_pkt;
		}

		length = ALX_GET_FIELD(le32_to_cpu(rrd->word3),
				       RRD_PKTLEN) - ETH_FCS_LEN;
		skb_put(skb, length);
		skb->protocol = eth_type_trans(skb, rxq->netdev);

		skb_checksum_none_assert(skb);
		if (alx->dev->features & NETIF_F_RXCSUM &&
		    !(rrd->word3 & (cpu_to_le32(1 << RRD_ERR_L4_SHIFT) |
				    cpu_to_le32(1 << RRD_ERR_IPV4_SHIFT)))) {
			switch (ALX_GET_FIELD(le32_to_cpu(rrd->word2),
					      RRD_PID)) {
			case RRD_PID_IPV6UDP:
			case RRD_PID_IPV4UDP:
			case RRD_PID_IPV4TCP:
			case RRD_PID_IPV6TCP:
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				break;
			}
		}

		napi_gro_receive(&rxq->np->napi, skb);
		work++;

next_pkt:
		if (++rxq->read_idx == rxq->count)
			rxq->read_idx = 0;
		if (++rxq->rrd_read_idx == rxq->count)
			rxq->rrd_read_idx = 0;

		if (++rfd_cleaned > ALX_RX_ALLOC_THRESH)
			rfd_cleaned -= alx_refill_rx_ring(alx, GFP_ATOMIC);
	}

	if (rfd_cleaned)
		alx_refill_rx_ring(alx, GFP_ATOMIC);

	return work;
}

static int alx_poll(struct napi_struct *napi, int budget)
{
	struct alx_napi *np = container_of(napi, struct alx_napi, napi);
	struct alx_priv *alx = np->alx;
	struct alx_hw *hw = &alx->hw;
	unsigned long flags;
	bool tx_complete = true;
	int work = 0;

	if (np->txq)
		tx_complete = alx_clean_tx_irq(np->txq);
	if (np->rxq)
		work = alx_clean_rx_irq(np->rxq, budget);

	if (!tx_complete || work == budget)
		return budget;

	napi_complete_done(&np->napi, work);

	/* enable interrupt */
	if (alx->hw.pdev->msix_enabled) {
		alx_mask_msix(hw, np->vec_idx, false);
	} else {
		spin_lock_irqsave(&alx->irq_lock, flags);
		alx->int_mask |= ALX_ISR_TX_Q0 | ALX_ISR_RX_Q0;
		alx_write_mem32(hw, ALX_IMR, alx->int_mask);
		spin_unlock_irqrestore(&alx->irq_lock, flags);
	}

	alx_post_write(hw);

	return work;
}

static bool alx_intr_handle_misc(struct alx_priv *alx, u32 intr)
{
	struct alx_hw *hw = &alx->hw;

	if (intr & ALX_ISR_FATAL) {
		netif_warn(alx, hw, alx->dev,
			   "fatal interrupt 0x%x, resetting\n", intr);
		alx_schedule_reset(alx);
		return true;
	}

	if (intr & ALX_ISR_ALERT)
		netdev_warn(alx->dev, "alert interrupt: 0x%x\n", intr);

	if (intr & ALX_ISR_PHY) {
		/* suppress PHY interrupt, because the source
		 * is from PHY internal. only the internal status
		 * is cleared, the interrupt status could be cleared.
		 */
		alx->int_mask &= ~ALX_ISR_PHY;
		alx_write_mem32(hw, ALX_IMR, alx->int_mask);
		alx_schedule_link_check(alx);
	}

	return false;
}

static irqreturn_t alx_intr_handle(struct alx_priv *alx, u32 intr)
{
	struct alx_hw *hw = &alx->hw;

	spin_lock(&alx->irq_lock);

	/* ACK interrupt */
	alx_write_mem32(hw, ALX_ISR, intr | ALX_ISR_DIS);
	intr &= alx->int_mask;

	if (alx_intr_handle_misc(alx, intr))
		goto out;

	if (intr & (ALX_ISR_TX_Q0 | ALX_ISR_RX_Q0)) {
		napi_schedule(&alx->qnapi[0]->napi);
		/* mask rx/tx interrupt, enable them when napi complete */
		alx->int_mask &= ~ALX_ISR_ALL_QUEUES;
		alx_write_mem32(hw, ALX_IMR, alx->int_mask);
	}

	alx_write_mem32(hw, ALX_ISR, 0);

 out:
	spin_unlock(&alx->irq_lock);
	return IRQ_HANDLED;
}

static irqreturn_t alx_intr_msix_ring(int irq, void *data)
{
	struct alx_napi *np = data;
	struct alx_hw *hw = &np->alx->hw;

	/* mask interrupt to ACK chip */
	alx_mask_msix(hw, np->vec_idx, true);
	/* clear interrupt status */
	alx_write_mem32(hw, ALX_ISR, np->vec_mask);

	napi_schedule(&np->napi);

	return IRQ_HANDLED;
}

static irqreturn_t alx_intr_msix_misc(int irq, void *data)
{
	struct alx_priv *alx = data;
	struct alx_hw *hw = &alx->hw;
	u32 intr;

	/* mask interrupt to ACK chip */
	alx_mask_msix(hw, 0, true);

	/* read interrupt status */
	intr = alx_read_mem32(hw, ALX_ISR);
	intr &= (alx->int_mask & ~ALX_ISR_ALL_QUEUES);

	if (alx_intr_handle_misc(alx, intr))
		return IRQ_HANDLED;

	/* clear interrupt status */
	alx_write_mem32(hw, ALX_ISR, intr);

	/* enable interrupt again */
	alx_mask_msix(hw, 0, false);

	return IRQ_HANDLED;
}

static irqreturn_t alx_intr_msi(int irq, void *data)
{
	struct alx_priv *alx = data;

	return alx_intr_handle(alx, alx_read_mem32(&alx->hw, ALX_ISR));
}

static irqreturn_t alx_intr_legacy(int irq, void *data)
{
	struct alx_priv *alx = data;
	struct alx_hw *hw = &alx->hw;
	u32 intr;

	intr = alx_read_mem32(hw, ALX_ISR);

	if (intr & ALX_ISR_DIS || !(intr & alx->int_mask))
		return IRQ_NONE;

	return alx_intr_handle(alx, intr);
}

static const u16 txring_header_reg[] = {ALX_TPD_PRI0_ADDR_LO,
					ALX_TPD_PRI1_ADDR_LO,
					ALX_TPD_PRI2_ADDR_LO,
					ALX_TPD_PRI3_ADDR_LO};

static void alx_init_ring_ptrs(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;
	u32 addr_hi = ((u64)alx->descmem.dma) >> 32;
	struct alx_napi *np;
	int i;

	for (i = 0; i < alx->num_napi; i++) {
		np = alx->qnapi[i];
		if (np->txq) {
			np->txq->read_idx = 0;
			np->txq->write_idx = 0;
			alx_write_mem32(hw,
					txring_header_reg[np->txq->queue_idx],
					np->txq->tpd_dma);
		}

		if (np->rxq) {
			np->rxq->read_idx = 0;
			np->rxq->write_idx = 0;
			np->rxq->rrd_read_idx = 0;
			alx_write_mem32(hw, ALX_RRD_ADDR_LO, np->rxq->rrd_dma);
			alx_write_mem32(hw, ALX_RFD_ADDR_LO, np->rxq->rfd_dma);
		}
	}

	alx_write_mem32(hw, ALX_TX_BASE_ADDR_HI, addr_hi);
	alx_write_mem32(hw, ALX_TPD_RING_SZ, alx->tx_ringsz);

	alx_write_mem32(hw, ALX_RX_BASE_ADDR_HI, addr_hi);
	alx_write_mem32(hw, ALX_RRD_RING_SZ, alx->rx_ringsz);
	alx_write_mem32(hw, ALX_RFD_RING_SZ, alx->rx_ringsz);
	alx_write_mem32(hw, ALX_RFD_BUF_SZ, alx->rxbuf_size);

	/* load these pointers into the chip */
	alx_write_mem32(hw, ALX_SRAM9, ALX_SRAM_LOAD_PTR);
}

static void alx_free_txring_buf(struct alx_tx_queue *txq)
{
	int i;

	if (!txq->bufs)
		return;

	for (i = 0; i < txq->count; i++)
		alx_free_txbuf(txq, i);

	memset(txq->bufs, 0, txq->count * sizeof(struct alx_buffer));
	memset(txq->tpd, 0, txq->count * sizeof(struct alx_txd));
	txq->write_idx = 0;
	txq->read_idx = 0;

	netdev_tx_reset_queue(alx_get_tx_queue(txq));
}

static void alx_free_rxring_buf(struct alx_rx_queue *rxq)
{
	struct alx_buffer *cur_buf;
	u16 i;

	if (!rxq->bufs)
		return;

	for (i = 0; i < rxq->count; i++) {
		cur_buf = rxq->bufs + i;
		if (cur_buf->skb) {
			dma_unmap_single(rxq->dev,
					 dma_unmap_addr(cur_buf, dma),
					 dma_unmap_len(cur_buf, size),
					 DMA_FROM_DEVICE);
			dev_kfree_skb(cur_buf->skb);
			cur_buf->skb = NULL;
			dma_unmap_len_set(cur_buf, size, 0);
			dma_unmap_addr_set(cur_buf, dma, 0);
		}
	}

	rxq->write_idx = 0;
	rxq->read_idx = 0;
	rxq->rrd_read_idx = 0;
}

static void alx_free_buffers(struct alx_priv *alx)
{
	int i;

	for (i = 0; i < alx->num_txq; i++)
		if (alx->qnapi[i] && alx->qnapi[i]->txq)
			alx_free_txring_buf(alx->qnapi[i]->txq);

	if (alx->qnapi[0] && alx->qnapi[0]->rxq)
		alx_free_rxring_buf(alx->qnapi[0]->rxq);
}

static int alx_reinit_rings(struct alx_priv *alx)
{
	alx_free_buffers(alx);

	alx_init_ring_ptrs(alx);

	if (!alx_refill_rx_ring(alx, GFP_KERNEL))
		return -ENOMEM;

	return 0;
}

static void alx_add_mc_addr(struct alx_hw *hw, const u8 *addr, u32 *mc_hash)
{
	u32 crc32, bit, reg;

	crc32 = ether_crc(ETH_ALEN, addr);
	reg = (crc32 >> 31) & 0x1;
	bit = (crc32 >> 26) & 0x1F;

	mc_hash[reg] |= BIT(bit);
}

static void __alx_set_rx_mode(struct net_device *netdev)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;
	struct netdev_hw_addr *ha;
	u32 mc_hash[2] = {};

	if (!(netdev->flags & IFF_ALLMULTI)) {
		netdev_for_each_mc_addr(ha, netdev)
			alx_add_mc_addr(hw, ha->addr, mc_hash);

		alx_write_mem32(hw, ALX_HASH_TBL0, mc_hash[0]);
		alx_write_mem32(hw, ALX_HASH_TBL1, mc_hash[1]);
	}

	hw->rx_ctrl &= ~(ALX_MAC_CTRL_MULTIALL_EN | ALX_MAC_CTRL_PROMISC_EN);
	if (netdev->flags & IFF_PROMISC)
		hw->rx_ctrl |= ALX_MAC_CTRL_PROMISC_EN;
	if (netdev->flags & IFF_ALLMULTI)
		hw->rx_ctrl |= ALX_MAC_CTRL_MULTIALL_EN;

	alx_write_mem32(hw, ALX_MAC_CTRL, hw->rx_ctrl);
}

static void alx_set_rx_mode(struct net_device *netdev)
{
	__alx_set_rx_mode(netdev);
}

static int alx_set_mac_address(struct net_device *netdev, void *data)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;
	struct sockaddr *addr = data;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (netdev->addr_assign_type & NET_ADDR_RANDOM)
		netdev->addr_assign_type ^= NET_ADDR_RANDOM;

	eth_hw_addr_set(netdev, addr->sa_data);
	memcpy(hw->mac_addr, addr->sa_data, netdev->addr_len);
	alx_set_macaddr(hw, hw->mac_addr);

	return 0;
}

static int alx_alloc_tx_ring(struct alx_priv *alx, struct alx_tx_queue *txq,
			     int offset)
{
	txq->bufs = kcalloc(txq->count, sizeof(struct alx_buffer), GFP_KERNEL);
	if (!txq->bufs)
		return -ENOMEM;

	txq->tpd = alx->descmem.virt + offset;
	txq->tpd_dma = alx->descmem.dma + offset;
	offset += sizeof(struct alx_txd) * txq->count;

	return offset;
}

static int alx_alloc_rx_ring(struct alx_priv *alx, struct alx_rx_queue *rxq,
			     int offset)
{
	rxq->bufs = kcalloc(rxq->count, sizeof(struct alx_buffer), GFP_KERNEL);
	if (!rxq->bufs)
		return -ENOMEM;

	rxq->rrd = alx->descmem.virt + offset;
	rxq->rrd_dma = alx->descmem.dma + offset;
	offset += sizeof(struct alx_rrd) * rxq->count;

	rxq->rfd = alx->descmem.virt + offset;
	rxq->rfd_dma = alx->descmem.dma + offset;
	offset += sizeof(struct alx_rfd) * rxq->count;

	return offset;
}

static int alx_alloc_rings(struct alx_priv *alx)
{
	int i, offset = 0;

	/* physical tx/rx ring descriptors
	 *
	 * Allocate them as a single chunk because they must not cross a
	 * 4G boundary (hardware has a single register for high 32 bits
	 * of addresses only)
	 */
	alx->descmem.size = sizeof(struct alx_txd) * alx->tx_ringsz *
			    alx->num_txq +
			    sizeof(struct alx_rrd) * alx->rx_ringsz +
			    sizeof(struct alx_rfd) * alx->rx_ringsz;
	alx->descmem.virt = dma_alloc_coherent(&alx->hw.pdev->dev,
					       alx->descmem.size,
					       &alx->descmem.dma, GFP_KERNEL);
	if (!alx->descmem.virt)
		return -ENOMEM;

	/* alignment requirements */
	BUILD_BUG_ON(sizeof(struct alx_txd) % 8);
	BUILD_BUG_ON(sizeof(struct alx_rrd) % 8);

	for (i = 0; i < alx->num_txq; i++) {
		offset = alx_alloc_tx_ring(alx, alx->qnapi[i]->txq, offset);
		if (offset < 0) {
			netdev_err(alx->dev, "Allocation of tx buffer failed!\n");
			return -ENOMEM;
		}
	}

	offset = alx_alloc_rx_ring(alx, alx->qnapi[0]->rxq, offset);
	if (offset < 0) {
		netdev_err(alx->dev, "Allocation of rx buffer failed!\n");
		return -ENOMEM;
	}

	return 0;
}

static void alx_free_rings(struct alx_priv *alx)
{
	int i;

	alx_free_buffers(alx);

	for (i = 0; i < alx->num_txq; i++)
		if (alx->qnapi[i] && alx->qnapi[i]->txq)
			kfree(alx->qnapi[i]->txq->bufs);

	if (alx->qnapi[0] && alx->qnapi[0]->rxq)
		kfree(alx->qnapi[0]->rxq->bufs);

	if (alx->descmem.virt)
		dma_free_coherent(&alx->hw.pdev->dev,
				  alx->descmem.size,
				  alx->descmem.virt,
				  alx->descmem.dma);
}

static void alx_free_napis(struct alx_priv *alx)
{
	struct alx_napi *np;
	int i;

	for (i = 0; i < alx->num_napi; i++) {
		np = alx->qnapi[i];
		if (!np)
			continue;

		netif_napi_del(&np->napi);
		kfree(np->txq);
		kfree(np->rxq);
		kfree(np);
		alx->qnapi[i] = NULL;
	}
}

static const u16 tx_pidx_reg[] = {ALX_TPD_PRI0_PIDX, ALX_TPD_PRI1_PIDX,
				  ALX_TPD_PRI2_PIDX, ALX_TPD_PRI3_PIDX};
static const u16 tx_cidx_reg[] = {ALX_TPD_PRI0_CIDX, ALX_TPD_PRI1_CIDX,
				  ALX_TPD_PRI2_CIDX, ALX_TPD_PRI3_CIDX};
static const u32 tx_vect_mask[] = {ALX_ISR_TX_Q0, ALX_ISR_TX_Q1,
				   ALX_ISR_TX_Q2, ALX_ISR_TX_Q3};
static const u32 rx_vect_mask[] = {ALX_ISR_RX_Q0, ALX_ISR_RX_Q1,
				   ALX_ISR_RX_Q2, ALX_ISR_RX_Q3,
				   ALX_ISR_RX_Q4, ALX_ISR_RX_Q5,
				   ALX_ISR_RX_Q6, ALX_ISR_RX_Q7};

static int alx_alloc_napis(struct alx_priv *alx)
{
	struct alx_napi *np;
	struct alx_rx_queue *rxq;
	struct alx_tx_queue *txq;
	int i;

	alx->int_mask &= ~ALX_ISR_ALL_QUEUES;

	/* allocate alx_napi structures */
	for (i = 0; i < alx->num_napi; i++) {
		np = kzalloc(sizeof(struct alx_napi), GFP_KERNEL);
		if (!np)
			goto err_out;

		np->alx = alx;
		netif_napi_add(alx->dev, &np->napi, alx_poll, 64);
		alx->qnapi[i] = np;
	}

	/* allocate tx queues */
	for (i = 0; i < alx->num_txq; i++) {
		np = alx->qnapi[i];
		txq = kzalloc(sizeof(*txq), GFP_KERNEL);
		if (!txq)
			goto err_out;

		np->txq = txq;
		txq->p_reg = tx_pidx_reg[i];
		txq->c_reg = tx_cidx_reg[i];
		txq->queue_idx = i;
		txq->count = alx->tx_ringsz;
		txq->netdev = alx->dev;
		txq->dev = &alx->hw.pdev->dev;
		np->vec_mask |= tx_vect_mask[i];
		alx->int_mask |= tx_vect_mask[i];
	}

	/* allocate rx queues */
	np = alx->qnapi[0];
	rxq = kzalloc(sizeof(*rxq), GFP_KERNEL);
	if (!rxq)
		goto err_out;

	np->rxq = rxq;
	rxq->np = alx->qnapi[0];
	rxq->queue_idx = 0;
	rxq->count = alx->rx_ringsz;
	rxq->netdev = alx->dev;
	rxq->dev = &alx->hw.pdev->dev;
	np->vec_mask |= rx_vect_mask[0];
	alx->int_mask |= rx_vect_mask[0];

	return 0;

err_out:
	netdev_err(alx->dev, "error allocating internal structures\n");
	alx_free_napis(alx);
	return -ENOMEM;
}

static const int txq_vec_mapping_shift[] = {
	0, ALX_MSI_MAP_TBL1_TXQ0_SHIFT,
	0, ALX_MSI_MAP_TBL1_TXQ1_SHIFT,
	1, ALX_MSI_MAP_TBL2_TXQ2_SHIFT,
	1, ALX_MSI_MAP_TBL2_TXQ3_SHIFT,
};

static void alx_config_vector_mapping(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;
	u32 tbl[2] = {0, 0};
	int i, vector, idx, shift;

	if (alx->hw.pdev->msix_enabled) {
		/* tx mappings */
		for (i = 0, vector = 1; i < alx->num_txq; i++, vector++) {
			idx = txq_vec_mapping_shift[i * 2];
			shift = txq_vec_mapping_shift[i * 2 + 1];
			tbl[idx] |= vector << shift;
		}

		/* rx mapping */
		tbl[0] |= 1 << ALX_MSI_MAP_TBL1_RXQ0_SHIFT;
	}

	alx_write_mem32(hw, ALX_MSI_MAP_TBL1, tbl[0]);
	alx_write_mem32(hw, ALX_MSI_MAP_TBL2, tbl[1]);
	alx_write_mem32(hw, ALX_MSI_ID_MAP, 0);
}

static int alx_enable_msix(struct alx_priv *alx)
{
	int err, num_vec, num_txq, num_rxq;

	num_txq = min_t(int, num_online_cpus(), ALX_MAX_TX_QUEUES);
	num_rxq = 1;
	num_vec = max_t(int, num_txq, num_rxq) + 1;

	err = pci_alloc_irq_vectors(alx->hw.pdev, num_vec, num_vec,
			PCI_IRQ_MSIX);
	if (err < 0) {
		netdev_warn(alx->dev, "Enabling MSI-X interrupts failed!\n");
		return err;
	}

	alx->num_vec = num_vec;
	alx->num_napi = num_vec - 1;
	alx->num_txq = num_txq;
	alx->num_rxq = num_rxq;

	return err;
}

static int alx_request_msix(struct alx_priv *alx)
{
	struct net_device *netdev = alx->dev;
	int i, err, vector = 0, free_vector = 0;

	err = request_irq(pci_irq_vector(alx->hw.pdev, 0), alx_intr_msix_misc,
			  0, netdev->name, alx);
	if (err)
		goto out_err;

	for (i = 0; i < alx->num_napi; i++) {
		struct alx_napi *np = alx->qnapi[i];

		vector++;

		if (np->txq && np->rxq)
			sprintf(np->irq_lbl, "%s-TxRx-%u", netdev->name,
				np->txq->queue_idx);
		else if (np->txq)
			sprintf(np->irq_lbl, "%s-tx-%u", netdev->name,
				np->txq->queue_idx);
		else if (np->rxq)
			sprintf(np->irq_lbl, "%s-rx-%u", netdev->name,
				np->rxq->queue_idx);
		else
			sprintf(np->irq_lbl, "%s-unused", netdev->name);

		np->vec_idx = vector;
		err = request_irq(pci_irq_vector(alx->hw.pdev, vector),
				  alx_intr_msix_ring, 0, np->irq_lbl, np);
		if (err)
			goto out_free;
	}
	return 0;

out_free:
	free_irq(pci_irq_vector(alx->hw.pdev, free_vector++), alx);

	vector--;
	for (i = 0; i < vector; i++)
		free_irq(pci_irq_vector(alx->hw.pdev,free_vector++),
			 alx->qnapi[i]);

out_err:
	return err;
}

static int alx_init_intr(struct alx_priv *alx)
{
	int ret;

	ret = pci_alloc_irq_vectors(alx->hw.pdev, 1, 1,
			PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if (ret < 0)
		return ret;

	alx->num_vec = 1;
	alx->num_napi = 1;
	alx->num_txq = 1;
	alx->num_rxq = 1;
	return 0;
}

static void alx_irq_enable(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;
	int i;

	/* level-1 interrupt switch */
	alx_write_mem32(hw, ALX_ISR, 0);
	alx_write_mem32(hw, ALX_IMR, alx->int_mask);
	alx_post_write(hw);

	if (alx->hw.pdev->msix_enabled) {
		/* enable all msix irqs */
		for (i = 0; i < alx->num_vec; i++)
			alx_mask_msix(hw, i, false);
	}
}

static void alx_irq_disable(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;
	int i;

	alx_write_mem32(hw, ALX_ISR, ALX_ISR_DIS);
	alx_write_mem32(hw, ALX_IMR, 0);
	alx_post_write(hw);

	if (alx->hw.pdev->msix_enabled) {
		for (i = 0; i < alx->num_vec; i++) {
			alx_mask_msix(hw, i, true);
			synchronize_irq(pci_irq_vector(alx->hw.pdev, i));
		}
	} else {
		synchronize_irq(pci_irq_vector(alx->hw.pdev, 0));
	}
}

static int alx_realloc_resources(struct alx_priv *alx)
{
	int err;

	alx_free_rings(alx);
	alx_free_napis(alx);
	pci_free_irq_vectors(alx->hw.pdev);

	err = alx_init_intr(alx);
	if (err)
		return err;

	err = alx_alloc_napis(alx);
	if (err)
		return err;

	err = alx_alloc_rings(alx);
	if (err)
		return err;

	return 0;
}

static int alx_request_irq(struct alx_priv *alx)
{
	struct pci_dev *pdev = alx->hw.pdev;
	struct alx_hw *hw = &alx->hw;
	int err;
	u32 msi_ctrl;

	msi_ctrl = (hw->imt >> 1) << ALX_MSI_RETRANS_TM_SHIFT;

	if (alx->hw.pdev->msix_enabled) {
		alx_write_mem32(hw, ALX_MSI_RETRANS_TIMER, msi_ctrl);
		err = alx_request_msix(alx);
		if (!err)
			goto out;

		/* msix request failed, realloc resources */
		err = alx_realloc_resources(alx);
		if (err)
			goto out;
	}

	if (alx->hw.pdev->msi_enabled) {
		alx_write_mem32(hw, ALX_MSI_RETRANS_TIMER,
				msi_ctrl | ALX_MSI_MASK_SEL_LINE);
		err = request_irq(pci_irq_vector(pdev, 0), alx_intr_msi, 0,
				  alx->dev->name, alx);
		if (!err)
			goto out;

		/* fall back to legacy interrupt */
		pci_free_irq_vectors(alx->hw.pdev);
	}

	alx_write_mem32(hw, ALX_MSI_RETRANS_TIMER, 0);
	err = request_irq(pci_irq_vector(pdev, 0), alx_intr_legacy, IRQF_SHARED,
			  alx->dev->name, alx);
out:
	if (!err)
		alx_config_vector_mapping(alx);
	else
		netdev_err(alx->dev, "IRQ registration failed!\n");
	return err;
}

static void alx_free_irq(struct alx_priv *alx)
{
	struct pci_dev *pdev = alx->hw.pdev;
	int i;

	free_irq(pci_irq_vector(pdev, 0), alx);
	if (alx->hw.pdev->msix_enabled) {
		for (i = 0; i < alx->num_napi; i++)
			free_irq(pci_irq_vector(pdev, i + 1), alx->qnapi[i]);
	}

	pci_free_irq_vectors(pdev);
}

static int alx_identify_hw(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;
	int rev = alx_hw_revision(hw);

	if (rev > ALX_REV_C0)
		return -EINVAL;

	hw->max_dma_chnl = rev >= ALX_REV_B0 ? 4 : 2;

	return 0;
}

static int alx_init_sw(struct alx_priv *alx)
{
	struct pci_dev *pdev = alx->hw.pdev;
	struct alx_hw *hw = &alx->hw;
	int err;

	err = alx_identify_hw(alx);
	if (err) {
		dev_err(&pdev->dev, "unrecognized chip, aborting\n");
		return err;
	}

	alx->hw.lnk_patch =
		pdev->device == ALX_DEV_ID_AR8161 &&
		pdev->subsystem_vendor == PCI_VENDOR_ID_ATTANSIC &&
		pdev->subsystem_device == 0x0091 &&
		pdev->revision == 0;

	hw->smb_timer = 400;
	hw->mtu = alx->dev->mtu;
	alx->rxbuf_size = ALX_MAX_FRAME_LEN(hw->mtu);
	/* MTU range: 34 - 9256 */
	alx->dev->min_mtu = 34;
	alx->dev->max_mtu = ALX_MAX_FRAME_LEN(ALX_MAX_FRAME_SIZE);
	alx->tx_ringsz = 256;
	alx->rx_ringsz = 512;
	hw->imt = 200;
	alx->int_mask = ALX_ISR_MISC;
	hw->dma_chnl = hw->max_dma_chnl;
	hw->ith_tpd = alx->tx_ringsz / 3;
	hw->link_speed = SPEED_UNKNOWN;
	hw->duplex = DUPLEX_UNKNOWN;
	hw->adv_cfg = ADVERTISED_Autoneg |
		      ADVERTISED_10baseT_Half |
		      ADVERTISED_10baseT_Full |
		      ADVERTISED_100baseT_Full |
		      ADVERTISED_100baseT_Half |
		      ADVERTISED_1000baseT_Full;
	hw->flowctrl = ALX_FC_ANEG | ALX_FC_RX | ALX_FC_TX;

	hw->rx_ctrl = ALX_MAC_CTRL_WOLSPED_SWEN |
		      ALX_MAC_CTRL_MHASH_ALG_HI5B |
		      ALX_MAC_CTRL_BRD_EN |
		      ALX_MAC_CTRL_PCRCE |
		      ALX_MAC_CTRL_CRCE |
		      ALX_MAC_CTRL_RXFC_EN |
		      ALX_MAC_CTRL_TXFC_EN |
		      7 << ALX_MAC_CTRL_PRMBLEN_SHIFT;
	mutex_init(&alx->mtx);

	return 0;
}


static netdev_features_t alx_fix_features(struct net_device *netdev,
					  netdev_features_t features)
{
	if (netdev->mtu > ALX_MAX_TSO_PKT_SIZE)
		features &= ~(NETIF_F_TSO | NETIF_F_TSO6);

	return features;
}

static void alx_netif_stop(struct alx_priv *alx)
{
	int i;

	netif_trans_update(alx->dev);
	if (netif_carrier_ok(alx->dev)) {
		netif_carrier_off(alx->dev);
		netif_tx_disable(alx->dev);
		for (i = 0; i < alx->num_napi; i++)
			napi_disable(&alx->qnapi[i]->napi);
	}
}

static void alx_halt(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;

	lockdep_assert_held(&alx->mtx);

	alx_netif_stop(alx);
	hw->link_speed = SPEED_UNKNOWN;
	hw->duplex = DUPLEX_UNKNOWN;

	alx_reset_mac(hw);

	/* disable l0s/l1 */
	alx_enable_aspm(hw, false, false);
	alx_irq_disable(alx);
	alx_free_buffers(alx);
}

static void alx_configure(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;

	alx_configure_basic(hw);
	alx_disable_rss(hw);
	__alx_set_rx_mode(alx->dev);

	alx_write_mem32(hw, ALX_MAC_CTRL, hw->rx_ctrl);
}

static void alx_activate(struct alx_priv *alx)
{
	lockdep_assert_held(&alx->mtx);

	/* hardware setting lost, restore it */
	alx_reinit_rings(alx);
	alx_configure(alx);

	/* clear old interrupts */
	alx_write_mem32(&alx->hw, ALX_ISR, ~(u32)ALX_ISR_DIS);

	alx_irq_enable(alx);

	alx_schedule_link_check(alx);
}

static void alx_reinit(struct alx_priv *alx)
{
	lockdep_assert_held(&alx->mtx);

	alx_halt(alx);
	alx_activate(alx);
}

static int alx_change_mtu(struct net_device *netdev, int mtu)
{
	struct alx_priv *alx = netdev_priv(netdev);
	int max_frame = ALX_MAX_FRAME_LEN(mtu);

	netdev->mtu = mtu;
	alx->hw.mtu = mtu;
	alx->rxbuf_size = max(max_frame, ALX_DEF_RXBUF_SIZE);
	netdev_update_features(netdev);
	if (netif_running(netdev))
		alx_reinit(alx);
	return 0;
}

static void alx_netif_start(struct alx_priv *alx)
{
	int i;

	netif_tx_wake_all_queues(alx->dev);
	for (i = 0; i < alx->num_napi; i++)
		napi_enable(&alx->qnapi[i]->napi);
	netif_carrier_on(alx->dev);
}

static int __alx_open(struct alx_priv *alx, bool resume)
{
	int err;

	err = alx_enable_msix(alx);
	if (err < 0) {
		err = alx_init_intr(alx);
		if (err)
			return err;
	}

	if (!resume)
		netif_carrier_off(alx->dev);

	err = alx_alloc_napis(alx);
	if (err)
		goto out_disable_adv_intr;

	err = alx_alloc_rings(alx);
	if (err)
		goto out_free_rings;

	alx_configure(alx);

	err = alx_request_irq(alx);
	if (err)
		goto out_free_rings;

	/* must be called after alx_request_irq because the chip stops working
	 * if we copy the dma addresses in alx_init_ring_ptrs twice when
	 * requesting msi-x interrupts failed
	 */
	alx_reinit_rings(alx);

	netif_set_real_num_tx_queues(alx->dev, alx->num_txq);
	netif_set_real_num_rx_queues(alx->dev, alx->num_rxq);

	/* clear old interrupts */
	alx_write_mem32(&alx->hw, ALX_ISR, ~(u32)ALX_ISR_DIS);

	alx_irq_enable(alx);

	if (!resume)
		netif_tx_start_all_queues(alx->dev);

	alx_schedule_link_check(alx);
	return 0;

out_free_rings:
	alx_free_rings(alx);
	alx_free_napis(alx);
out_disable_adv_intr:
	pci_free_irq_vectors(alx->hw.pdev);
	return err;
}

static void __alx_stop(struct alx_priv *alx)
{
	lockdep_assert_held(&alx->mtx);

	alx_free_irq(alx);

	cancel_work_sync(&alx->link_check_wk);
	cancel_work_sync(&alx->reset_wk);

	alx_halt(alx);
	alx_free_rings(alx);
	alx_free_napis(alx);
}

static const char *alx_speed_desc(struct alx_hw *hw)
{
	switch (alx_speed_to_ethadv(hw->link_speed, hw->duplex)) {
	case ADVERTISED_1000baseT_Full:
		return "1 Gbps Full";
	case ADVERTISED_100baseT_Full:
		return "100 Mbps Full";
	case ADVERTISED_100baseT_Half:
		return "100 Mbps Half";
	case ADVERTISED_10baseT_Full:
		return "10 Mbps Full";
	case ADVERTISED_10baseT_Half:
		return "10 Mbps Half";
	default:
		return "Unknown speed";
	}
}

static void alx_check_link(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;
	unsigned long flags;
	int old_speed;
	int err;

	lockdep_assert_held(&alx->mtx);

	/* clear PHY internal interrupt status, otherwise the main
	 * interrupt status will be asserted forever
	 */
	alx_clear_phy_intr(hw);

	old_speed = hw->link_speed;
	err = alx_read_phy_link(hw);
	if (err < 0)
		goto reset;

	spin_lock_irqsave(&alx->irq_lock, flags);
	alx->int_mask |= ALX_ISR_PHY;
	alx_write_mem32(hw, ALX_IMR, alx->int_mask);
	spin_unlock_irqrestore(&alx->irq_lock, flags);

	if (old_speed == hw->link_speed)
		return;

	if (hw->link_speed != SPEED_UNKNOWN) {
		netif_info(alx, link, alx->dev,
			   "NIC Up: %s\n", alx_speed_desc(hw));
		alx_post_phy_link(hw);
		alx_enable_aspm(hw, true, true);
		alx_start_mac(hw);

		if (old_speed == SPEED_UNKNOWN)
			alx_netif_start(alx);
	} else {
		/* link is now down */
		alx_netif_stop(alx);
		netif_info(alx, link, alx->dev, "Link Down\n");
		err = alx_reset_mac(hw);
		if (err)
			goto reset;
		alx_irq_disable(alx);

		/* MAC reset causes all HW settings to be lost, restore all */
		err = alx_reinit_rings(alx);
		if (err)
			goto reset;
		alx_configure(alx);
		alx_enable_aspm(hw, false, true);
		alx_post_phy_link(hw);
		alx_irq_enable(alx);
	}

	return;

reset:
	alx_schedule_reset(alx);
}

static int alx_open(struct net_device *netdev)
{
	struct alx_priv *alx = netdev_priv(netdev);
	int ret;

	mutex_lock(&alx->mtx);
	ret = __alx_open(alx, false);
	mutex_unlock(&alx->mtx);

	return ret;
}

static int alx_stop(struct net_device *netdev)
{
	struct alx_priv *alx = netdev_priv(netdev);

	mutex_lock(&alx->mtx);
	__alx_stop(alx);
	mutex_unlock(&alx->mtx);

	return 0;
}

static void alx_link_check(struct work_struct *work)
{
	struct alx_priv *alx;

	alx = container_of(work, struct alx_priv, link_check_wk);

	mutex_lock(&alx->mtx);
	alx_check_link(alx);
	mutex_unlock(&alx->mtx);
}

static void alx_reset(struct work_struct *work)
{
	struct alx_priv *alx = container_of(work, struct alx_priv, reset_wk);

	mutex_lock(&alx->mtx);
	alx_reinit(alx);
	mutex_unlock(&alx->mtx);
}

static int alx_tpd_req(struct sk_buff *skb)
{
	int num;

	num = skb_shinfo(skb)->nr_frags + 1;
	/* we need one extra descriptor for LSOv2 */
	if (skb_is_gso(skb) && skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
		num++;

	return num;
}

static int alx_tx_csum(struct sk_buff *skb, struct alx_txd *first)
{
	u8 cso, css;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	cso = skb_checksum_start_offset(skb);
	if (cso & 1)
		return -EINVAL;

	css = cso + skb->csum_offset;
	first->word1 |= cpu_to_le32((cso >> 1) << TPD_CXSUMSTART_SHIFT);
	first->word1 |= cpu_to_le32((css >> 1) << TPD_CXSUMOFFSET_SHIFT);
	first->word1 |= cpu_to_le32(1 << TPD_CXSUM_EN_SHIFT);

	return 0;
}

static int alx_tso(struct sk_buff *skb, struct alx_txd *first)
{
	int err;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	if (skb->protocol == htons(ETH_P_IP)) {
		struct iphdr *iph = ip_hdr(skb);

		iph->check = 0;
		tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
							 0, IPPROTO_TCP, 0);
		first->word1 |= 1 << TPD_IPV4_SHIFT;
	} else if (skb_is_gso_v6(skb)) {
		tcp_v6_gso_csum_prep(skb);
		/* LSOv2: the first TPD only provides the packet length */
		first->adrl.l.pkt_len = skb->len;
		first->word1 |= 1 << TPD_LSO_V2_SHIFT;
	}

	first->word1 |= 1 << TPD_LSO_EN_SHIFT;
	first->word1 |= (skb_transport_offset(skb) &
			 TPD_L4HDROFFSET_MASK) << TPD_L4HDROFFSET_SHIFT;
	first->word1 |= (skb_shinfo(skb)->gso_size &
			 TPD_MSS_MASK) << TPD_MSS_SHIFT;
	return 1;
}

static int alx_map_tx_skb(struct alx_tx_queue *txq, struct sk_buff *skb)
{
	struct alx_txd *tpd, *first_tpd;
	dma_addr_t dma;
	int maplen, f, first_idx = txq->write_idx;

	first_tpd = &txq->tpd[txq->write_idx];
	tpd = first_tpd;

	if (tpd->word1 & (1 << TPD_LSO_V2_SHIFT)) {
		if (++txq->write_idx == txq->count)
			txq->write_idx = 0;

		tpd = &txq->tpd[txq->write_idx];
		tpd->len = first_tpd->len;
		tpd->vlan_tag = first_tpd->vlan_tag;
		tpd->word1 = first_tpd->word1;
	}

	maplen = skb_headlen(skb);
	dma = dma_map_single(txq->dev, skb->data, maplen,
			     DMA_TO_DEVICE);
	if (dma_mapping_error(txq->dev, dma))
		goto err_dma;

	dma_unmap_len_set(&txq->bufs[txq->write_idx], size, maplen);
	dma_unmap_addr_set(&txq->bufs[txq->write_idx], dma, dma);

	tpd->adrl.addr = cpu_to_le64(dma);
	tpd->len = cpu_to_le16(maplen);

	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[f];

		if (++txq->write_idx == txq->count)
			txq->write_idx = 0;
		tpd = &txq->tpd[txq->write_idx];

		tpd->word1 = first_tpd->word1;

		maplen = skb_frag_size(frag);
		dma = skb_frag_dma_map(txq->dev, frag, 0,
				       maplen, DMA_TO_DEVICE);
		if (dma_mapping_error(txq->dev, dma))
			goto err_dma;
		dma_unmap_len_set(&txq->bufs[txq->write_idx], size, maplen);
		dma_unmap_addr_set(&txq->bufs[txq->write_idx], dma, dma);

		tpd->adrl.addr = cpu_to_le64(dma);
		tpd->len = cpu_to_le16(maplen);
	}

	/* last TPD, set EOP flag and store skb */
	tpd->word1 |= cpu_to_le32(1 << TPD_EOP_SHIFT);
	txq->bufs[txq->write_idx].skb = skb;

	if (++txq->write_idx == txq->count)
		txq->write_idx = 0;

	return 0;

err_dma:
	f = first_idx;
	while (f != txq->write_idx) {
		alx_free_txbuf(txq, f);
		if (++f == txq->count)
			f = 0;
	}
	return -ENOMEM;
}

static netdev_tx_t alx_start_xmit_ring(struct sk_buff *skb,
				       struct alx_tx_queue *txq)
{
	struct alx_priv *alx;
	struct alx_txd *first;
	int tso;

	alx = netdev_priv(txq->netdev);

	if (alx_tpd_avail(txq) < alx_tpd_req(skb)) {
		netif_tx_stop_queue(alx_get_tx_queue(txq));
		goto drop;
	}

	first = &txq->tpd[txq->write_idx];
	memset(first, 0, sizeof(*first));

	tso = alx_tso(skb, first);
	if (tso < 0)
		goto drop;
	else if (!tso && alx_tx_csum(skb, first))
		goto drop;

	if (alx_map_tx_skb(txq, skb) < 0)
		goto drop;

	netdev_tx_sent_queue(alx_get_tx_queue(txq), skb->len);

	/* flush updates before updating hardware */
	wmb();
	alx_write_mem16(&alx->hw, txq->p_reg, txq->write_idx);

	if (alx_tpd_avail(txq) < txq->count / 8)
		netif_tx_stop_queue(alx_get_tx_queue(txq));

	return NETDEV_TX_OK;

drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static netdev_tx_t alx_start_xmit(struct sk_buff *skb,
				  struct net_device *netdev)
{
	struct alx_priv *alx = netdev_priv(netdev);
	return alx_start_xmit_ring(skb, alx_tx_queue_mapping(alx, skb));
}

static void alx_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct alx_priv *alx = netdev_priv(dev);

	alx_schedule_reset(alx);
}

static int alx_mdio_read(struct net_device *netdev,
			 int prtad, int devad, u16 addr)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;
	u16 val;
	int err;

	if (prtad != hw->mdio.prtad)
		return -EINVAL;

	if (devad == MDIO_DEVAD_NONE)
		err = alx_read_phy_reg(hw, addr, &val);
	else
		err = alx_read_phy_ext(hw, devad, addr, &val);

	if (err)
		return err;
	return val;
}

static int alx_mdio_write(struct net_device *netdev,
			  int prtad, int devad, u16 addr, u16 val)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;

	if (prtad != hw->mdio.prtad)
		return -EINVAL;

	if (devad == MDIO_DEVAD_NONE)
		return alx_write_phy_reg(hw, addr, val);

	return alx_write_phy_ext(hw, devad, addr, val);
}

static int alx_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct alx_priv *alx = netdev_priv(netdev);

	if (!netif_running(netdev))
		return -EAGAIN;

	return mdio_mii_ioctl(&alx->hw.mdio, if_mii(ifr), cmd);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void alx_poll_controller(struct net_device *netdev)
{
	struct alx_priv *alx = netdev_priv(netdev);
	int i;

	if (alx->hw.pdev->msix_enabled) {
		alx_intr_msix_misc(0, alx);
		for (i = 0; i < alx->num_txq; i++)
			alx_intr_msix_ring(0, alx->qnapi[i]);
	} else if (alx->hw.pdev->msi_enabled)
		alx_intr_msi(0, alx);
	else
		alx_intr_legacy(0, alx);
}
#endif

static void alx_get_stats64(struct net_device *dev,
			    struct rtnl_link_stats64 *net_stats)
{
	struct alx_priv *alx = netdev_priv(dev);
	struct alx_hw_stats *hw_stats = &alx->hw.stats;

	spin_lock(&alx->stats_lock);

	alx_update_hw_stats(&alx->hw);

	net_stats->tx_bytes   = hw_stats->tx_byte_cnt;
	net_stats->rx_bytes   = hw_stats->rx_byte_cnt;
	net_stats->multicast  = hw_stats->rx_mcast;
	net_stats->collisions = hw_stats->tx_single_col +
				hw_stats->tx_multi_col +
				hw_stats->tx_late_col +
				hw_stats->tx_abort_col;

	net_stats->rx_errors  = hw_stats->rx_frag +
				hw_stats->rx_fcs_err +
				hw_stats->rx_len_err +
				hw_stats->rx_ov_sz +
				hw_stats->rx_ov_rrd +
				hw_stats->rx_align_err +
				hw_stats->rx_ov_rxf;

	net_stats->rx_fifo_errors   = hw_stats->rx_ov_rxf;
	net_stats->rx_length_errors = hw_stats->rx_len_err;
	net_stats->rx_crc_errors    = hw_stats->rx_fcs_err;
	net_stats->rx_frame_errors  = hw_stats->rx_align_err;
	net_stats->rx_dropped       = hw_stats->rx_ov_rrd;

	net_stats->tx_errors = hw_stats->tx_late_col +
			       hw_stats->tx_abort_col +
			       hw_stats->tx_underrun +
			       hw_stats->tx_trunc;

	net_stats->tx_aborted_errors = hw_stats->tx_abort_col;
	net_stats->tx_fifo_errors    = hw_stats->tx_underrun;
	net_stats->tx_window_errors  = hw_stats->tx_late_col;

	net_stats->tx_packets = hw_stats->tx_ok + net_stats->tx_errors;
	net_stats->rx_packets = hw_stats->rx_ok + net_stats->rx_errors;

	spin_unlock(&alx->stats_lock);
}

static const struct net_device_ops alx_netdev_ops = {
	.ndo_open               = alx_open,
	.ndo_stop               = alx_stop,
	.ndo_start_xmit         = alx_start_xmit,
	.ndo_get_stats64        = alx_get_stats64,
	.ndo_set_rx_mode        = alx_set_rx_mode,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = alx_set_mac_address,
	.ndo_change_mtu         = alx_change_mtu,
	.ndo_eth_ioctl           = alx_ioctl,
	.ndo_tx_timeout         = alx_tx_timeout,
	.ndo_fix_features	= alx_fix_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = alx_poll_controller,
#endif
};

static int alx_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct alx_priv *alx;
	struct alx_hw *hw;
	bool phy_configured;
	int err;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	/* The alx chip can DMA to 64-bit addresses, but it uses a single
	 * shared register for the high 32 bits, so only a single, aligned,
	 * 4 GB physical address range can be used for descriptors.
	 */
	if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		dev_dbg(&pdev->dev, "DMA to 64-BIT addresses\n");
	} else {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "No usable DMA config, aborting\n");
			goto out_pci_disable;
		}
	}

	err = pci_request_mem_regions(pdev, alx_drv_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_mem_regions failed\n");
		goto out_pci_disable;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	if (!pdev->pm_cap) {
		dev_err(&pdev->dev,
			"Can't find power management capability, aborting\n");
		err = -EIO;
		goto out_pci_release;
	}

	netdev = alloc_etherdev_mqs(sizeof(*alx),
				    ALX_MAX_TX_QUEUES, 1);
	if (!netdev) {
		err = -ENOMEM;
		goto out_pci_release;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);
	alx = netdev_priv(netdev);
	spin_lock_init(&alx->hw.mdio_lock);
	spin_lock_init(&alx->irq_lock);
	spin_lock_init(&alx->stats_lock);
	alx->dev = netdev;
	alx->hw.pdev = pdev;
	alx->msg_enable = NETIF_MSG_LINK | NETIF_MSG_HW | NETIF_MSG_IFUP |
			  NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR | NETIF_MSG_WOL;
	hw = &alx->hw;
	pci_set_drvdata(pdev, alx);

	hw->hw_addr = pci_ioremap_bar(pdev, 0);
	if (!hw->hw_addr) {
		dev_err(&pdev->dev, "cannot map device registers\n");
		err = -EIO;
		goto out_free_netdev;
	}

	netdev->netdev_ops = &alx_netdev_ops;
	netdev->ethtool_ops = &alx_ethtool_ops;
	netdev->irq = pci_irq_vector(pdev, 0);
	netdev->watchdog_timeo = ALX_WATCHDOG_TIME;

	if (ent->driver_data & ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG)
		pdev->dev_flags |= PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG;

	err = alx_init_sw(alx);
	if (err) {
		dev_err(&pdev->dev, "net device private data init failed\n");
		goto out_unmap;
	}

	mutex_lock(&alx->mtx);

	alx_reset_pcie(hw);

	phy_configured = alx_phy_configured(hw);

	if (!phy_configured)
		alx_reset_phy(hw);

	err = alx_reset_mac(hw);
	if (err) {
		dev_err(&pdev->dev, "MAC Reset failed, error = %d\n", err);
		goto out_unlock;
	}

	/* setup link to put it in a known good starting state */
	if (!phy_configured) {
		err = alx_setup_speed_duplex(hw, hw->adv_cfg, hw->flowctrl);
		if (err) {
			dev_err(&pdev->dev,
				"failed to configure PHY speed/duplex (err=%d)\n",
				err);
			goto out_unlock;
		}
	}

	netdev->hw_features = NETIF_F_SG |
			      NETIF_F_HW_CSUM |
			      NETIF_F_RXCSUM |
			      NETIF_F_TSO |
			      NETIF_F_TSO6;

	if (alx_get_perm_macaddr(hw, hw->perm_addr)) {
		dev_warn(&pdev->dev,
			 "Invalid permanent address programmed, using random one\n");
		eth_hw_addr_random(netdev);
		memcpy(hw->perm_addr, netdev->dev_addr, netdev->addr_len);
	}

	memcpy(hw->mac_addr, hw->perm_addr, ETH_ALEN);
	eth_hw_addr_set(netdev, hw->mac_addr);
	memcpy(netdev->perm_addr, hw->perm_addr, ETH_ALEN);

	hw->mdio.prtad = 0;
	hw->mdio.mmds = 0;
	hw->mdio.dev = netdev;
	hw->mdio.mode_support = MDIO_SUPPORTS_C45 |
				MDIO_SUPPORTS_C22 |
				MDIO_EMULATE_C22;
	hw->mdio.mdio_read = alx_mdio_read;
	hw->mdio.mdio_write = alx_mdio_write;

	if (!alx_get_phy_info(hw)) {
		dev_err(&pdev->dev, "failed to identify PHY\n");
		err = -EIO;
		goto out_unlock;
	}

	mutex_unlock(&alx->mtx);

	INIT_WORK(&alx->link_check_wk, alx_link_check);
	INIT_WORK(&alx->reset_wk, alx_reset);
	netif_carrier_off(netdev);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "register netdevice failed\n");
		goto out_unmap;
	}

	netdev_info(netdev,
		    "Qualcomm Atheros AR816x/AR817x Ethernet [%pM]\n",
		    netdev->dev_addr);

	return 0;

out_unlock:
	mutex_unlock(&alx->mtx);
out_unmap:
	iounmap(hw->hw_addr);
out_free_netdev:
	free_netdev(netdev);
out_pci_release:
	pci_release_mem_regions(pdev);
	pci_disable_pcie_error_reporting(pdev);
out_pci_disable:
	pci_disable_device(pdev);
	return err;
}

static void alx_remove(struct pci_dev *pdev)
{
	struct alx_priv *alx = pci_get_drvdata(pdev);
	struct alx_hw *hw = &alx->hw;

	/* restore permanent mac address */
	alx_set_macaddr(hw, hw->perm_addr);

	unregister_netdev(alx->dev);
	iounmap(hw->hw_addr);
	pci_release_mem_regions(pdev);

	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);

	mutex_destroy(&alx->mtx);

	free_netdev(alx->dev);
}

#ifdef CONFIG_PM_SLEEP
static int alx_suspend(struct device *dev)
{
	struct alx_priv *alx = dev_get_drvdata(dev);

	if (!netif_running(alx->dev))
		return 0;
	netif_device_detach(alx->dev);

	mutex_lock(&alx->mtx);
	__alx_stop(alx);
	mutex_unlock(&alx->mtx);

	return 0;
}

static int alx_resume(struct device *dev)
{
	struct alx_priv *alx = dev_get_drvdata(dev);
	struct alx_hw *hw = &alx->hw;
	int err;

	mutex_lock(&alx->mtx);
	alx_reset_phy(hw);

	if (!netif_running(alx->dev)) {
		err = 0;
		goto unlock;
	}

	err = __alx_open(alx, true);
	if (err)
		goto unlock;

	netif_device_attach(alx->dev);

unlock:
	mutex_unlock(&alx->mtx);
	return err;
}

static SIMPLE_DEV_PM_OPS(alx_pm_ops, alx_suspend, alx_resume);
#define ALX_PM_OPS      (&alx_pm_ops)
#else
#define ALX_PM_OPS      NULL
#endif


static pci_ers_result_t alx_pci_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct alx_priv *alx = pci_get_drvdata(pdev);
	struct net_device *netdev = alx->dev;
	pci_ers_result_t rc = PCI_ERS_RESULT_NEED_RESET;

	dev_info(&pdev->dev, "pci error detected\n");

	mutex_lock(&alx->mtx);

	if (netif_running(netdev)) {
		netif_device_detach(netdev);
		alx_halt(alx);
	}

	if (state == pci_channel_io_perm_failure)
		rc = PCI_ERS_RESULT_DISCONNECT;
	else
		pci_disable_device(pdev);

	mutex_unlock(&alx->mtx);

	return rc;
}

static pci_ers_result_t alx_pci_error_slot_reset(struct pci_dev *pdev)
{
	struct alx_priv *alx = pci_get_drvdata(pdev);
	struct alx_hw *hw = &alx->hw;
	pci_ers_result_t rc = PCI_ERS_RESULT_DISCONNECT;

	dev_info(&pdev->dev, "pci error slot reset\n");

	mutex_lock(&alx->mtx);

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "Failed to re-enable PCI device after reset\n");
		goto out;
	}

	pci_set_master(pdev);

	alx_reset_pcie(hw);
	if (!alx_reset_mac(hw))
		rc = PCI_ERS_RESULT_RECOVERED;
out:
	mutex_unlock(&alx->mtx);

	return rc;
}

static void alx_pci_error_resume(struct pci_dev *pdev)
{
	struct alx_priv *alx = pci_get_drvdata(pdev);
	struct net_device *netdev = alx->dev;

	dev_info(&pdev->dev, "pci error resume\n");

	mutex_lock(&alx->mtx);

	if (netif_running(netdev)) {
		alx_activate(alx);
		netif_device_attach(netdev);
	}

	mutex_unlock(&alx->mtx);
}

static const struct pci_error_handlers alx_err_handlers = {
	.error_detected = alx_pci_error_detected,
	.slot_reset     = alx_pci_error_slot_reset,
	.resume         = alx_pci_error_resume,
};

static const struct pci_device_id alx_pci_tbl[] = {
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_AR8161),
	  .driver_data = ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG },
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_E2200),
	  .driver_data = ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG },
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_E2400),
	  .driver_data = ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG },
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_E2500),
	  .driver_data = ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG },
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_AR8162),
	  .driver_data = ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG },
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_AR8171) },
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_AR8172) },
	{}
};

static struct pci_driver alx_driver = {
	.name        = alx_drv_name,
	.id_table    = alx_pci_tbl,
	.probe       = alx_probe,
	.remove      = alx_remove,
	.err_handler = &alx_err_handlers,
	.driver.pm   = ALX_PM_OPS,
};

module_pci_driver(alx_driver);
MODULE_DEVICE_TABLE(pci, alx_pci_tbl);
MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_AUTHOR("Qualcomm Corporation");
MODULE_DESCRIPTION(
	"Qualcomm Atheros(R) AR816x/AR817x PCI-E Ethernet Network Driver");
MODULE_LICENSE("GPL");
