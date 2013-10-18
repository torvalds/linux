/*
 * Copyright (c) 2013 Johannes Berg <johannes@sipsolutions.net>
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

const char alx_drv_name[] = "alx";


static void alx_free_txbuf(struct alx_priv *alx, int entry)
{
	struct alx_buffer *txb = &alx->txq.bufs[entry];

	if (dma_unmap_len(txb, size)) {
		dma_unmap_single(&alx->hw.pdev->dev,
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
	struct alx_rx_queue *rxq = &alx->rxq;
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

		skb = __netdev_alloc_skb(alx->dev, alx->rxbuf_size, gfp);
		if (!skb)
			break;
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

static inline int alx_tpd_avail(struct alx_priv *alx)
{
	struct alx_tx_queue *txq = &alx->txq;

	if (txq->write_idx >= txq->read_idx)
		return alx->tx_ringsz + txq->read_idx - txq->write_idx - 1;
	return txq->read_idx - txq->write_idx - 1;
}

static bool alx_clean_tx_irq(struct alx_priv *alx)
{
	struct alx_tx_queue *txq = &alx->txq;
	u16 hw_read_idx, sw_read_idx;
	unsigned int total_bytes = 0, total_packets = 0;
	int budget = ALX_DEFAULT_TX_WORK;

	sw_read_idx = txq->read_idx;
	hw_read_idx = alx_read_mem16(&alx->hw, ALX_TPD_PRI0_CIDX);

	if (sw_read_idx != hw_read_idx) {
		while (sw_read_idx != hw_read_idx && budget > 0) {
			struct sk_buff *skb;

			skb = txq->bufs[sw_read_idx].skb;
			if (skb) {
				total_bytes += skb->len;
				total_packets++;
				budget--;
			}

			alx_free_txbuf(alx, sw_read_idx);

			if (++sw_read_idx == alx->tx_ringsz)
				sw_read_idx = 0;
		}
		txq->read_idx = sw_read_idx;

		netdev_completed_queue(alx->dev, total_packets, total_bytes);
	}

	if (netif_queue_stopped(alx->dev) && netif_carrier_ok(alx->dev) &&
	    alx_tpd_avail(alx) > alx->tx_ringsz/4)
		netif_wake_queue(alx->dev);

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

static bool alx_clean_rx_irq(struct alx_priv *alx, int budget)
{
	struct alx_rx_queue *rxq = &alx->rxq;
	struct alx_rrd *rrd;
	struct alx_buffer *rxb;
	struct sk_buff *skb;
	u16 length, rfd_cleaned = 0;

	while (budget > 0) {
		rrd = &rxq->rrd[rxq->rrd_read_idx];
		if (!(rrd->word3 & cpu_to_le32(1 << RRD_UPDATED_SHIFT)))
			break;
		rrd->word3 &= ~cpu_to_le32(1 << RRD_UPDATED_SHIFT);

		if (ALX_GET_FIELD(le32_to_cpu(rrd->word0),
				  RRD_SI) != rxq->read_idx ||
		    ALX_GET_FIELD(le32_to_cpu(rrd->word0),
				  RRD_NOR) != 1) {
			alx_schedule_reset(alx);
			return 0;
		}

		rxb = &rxq->bufs[rxq->read_idx];
		dma_unmap_single(&alx->hw.pdev->dev,
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
		skb->protocol = eth_type_trans(skb, alx->dev);

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

		napi_gro_receive(&alx->napi, skb);
		budget--;

next_pkt:
		if (++rxq->read_idx == alx->rx_ringsz)
			rxq->read_idx = 0;
		if (++rxq->rrd_read_idx == alx->rx_ringsz)
			rxq->rrd_read_idx = 0;

		if (++rfd_cleaned > ALX_RX_ALLOC_THRESH)
			rfd_cleaned -= alx_refill_rx_ring(alx, GFP_ATOMIC);
	}

	if (rfd_cleaned)
		alx_refill_rx_ring(alx, GFP_ATOMIC);

	return budget > 0;
}

static int alx_poll(struct napi_struct *napi, int budget)
{
	struct alx_priv *alx = container_of(napi, struct alx_priv, napi);
	struct alx_hw *hw = &alx->hw;
	bool complete = true;
	unsigned long flags;

	complete = alx_clean_tx_irq(alx) &&
		   alx_clean_rx_irq(alx, budget);

	if (!complete)
		return 1;

	napi_complete(&alx->napi);

	/* enable interrupt */
	spin_lock_irqsave(&alx->irq_lock, flags);
	alx->int_mask |= ALX_ISR_TX_Q0 | ALX_ISR_RX_Q0;
	alx_write_mem32(hw, ALX_IMR, alx->int_mask);
	spin_unlock_irqrestore(&alx->irq_lock, flags);

	alx_post_write(hw);

	return 0;
}

static irqreturn_t alx_intr_handle(struct alx_priv *alx, u32 intr)
{
	struct alx_hw *hw = &alx->hw;
	bool write_int_mask = false;

	spin_lock(&alx->irq_lock);

	/* ACK interrupt */
	alx_write_mem32(hw, ALX_ISR, intr | ALX_ISR_DIS);
	intr &= alx->int_mask;

	if (intr & ALX_ISR_FATAL) {
		netif_warn(alx, hw, alx->dev,
			   "fatal interrupt 0x%x, resetting\n", intr);
		alx_schedule_reset(alx);
		goto out;
	}

	if (intr & ALX_ISR_ALERT)
		netdev_warn(alx->dev, "alert interrupt: 0x%x\n", intr);

	if (intr & ALX_ISR_PHY) {
		/* suppress PHY interrupt, because the source
		 * is from PHY internal. only the internal status
		 * is cleared, the interrupt status could be cleared.
		 */
		alx->int_mask &= ~ALX_ISR_PHY;
		write_int_mask = true;
		alx_schedule_link_check(alx);
	}

	if (intr & (ALX_ISR_TX_Q0 | ALX_ISR_RX_Q0)) {
		napi_schedule(&alx->napi);
		/* mask rx/tx interrupt, enable them when napi complete */
		alx->int_mask &= ~ALX_ISR_ALL_QUEUES;
		write_int_mask = true;
	}

	if (write_int_mask)
		alx_write_mem32(hw, ALX_IMR, alx->int_mask);

	alx_write_mem32(hw, ALX_ISR, 0);

 out:
	spin_unlock(&alx->irq_lock);
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

static void alx_init_ring_ptrs(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;
	u32 addr_hi = ((u64)alx->descmem.dma) >> 32;

	alx->rxq.read_idx = 0;
	alx->rxq.write_idx = 0;
	alx->rxq.rrd_read_idx = 0;
	alx_write_mem32(hw, ALX_RX_BASE_ADDR_HI, addr_hi);
	alx_write_mem32(hw, ALX_RRD_ADDR_LO, alx->rxq.rrd_dma);
	alx_write_mem32(hw, ALX_RRD_RING_SZ, alx->rx_ringsz);
	alx_write_mem32(hw, ALX_RFD_ADDR_LO, alx->rxq.rfd_dma);
	alx_write_mem32(hw, ALX_RFD_RING_SZ, alx->rx_ringsz);
	alx_write_mem32(hw, ALX_RFD_BUF_SZ, alx->rxbuf_size);

	alx->txq.read_idx = 0;
	alx->txq.write_idx = 0;
	alx_write_mem32(hw, ALX_TX_BASE_ADDR_HI, addr_hi);
	alx_write_mem32(hw, ALX_TPD_PRI0_ADDR_LO, alx->txq.tpd_dma);
	alx_write_mem32(hw, ALX_TPD_RING_SZ, alx->tx_ringsz);

	/* load these pointers into the chip */
	alx_write_mem32(hw, ALX_SRAM9, ALX_SRAM_LOAD_PTR);
}

static void alx_free_txring_buf(struct alx_priv *alx)
{
	struct alx_tx_queue *txq = &alx->txq;
	int i;

	if (!txq->bufs)
		return;

	for (i = 0; i < alx->tx_ringsz; i++)
		alx_free_txbuf(alx, i);

	memset(txq->bufs, 0, alx->tx_ringsz * sizeof(struct alx_buffer));
	memset(txq->tpd, 0, alx->tx_ringsz * sizeof(struct alx_txd));
	txq->write_idx = 0;
	txq->read_idx = 0;

	netdev_reset_queue(alx->dev);
}

static void alx_free_rxring_buf(struct alx_priv *alx)
{
	struct alx_rx_queue *rxq = &alx->rxq;
	struct alx_buffer *cur_buf;
	u16 i;

	if (rxq == NULL)
		return;

	for (i = 0; i < alx->rx_ringsz; i++) {
		cur_buf = rxq->bufs + i;
		if (cur_buf->skb) {
			dma_unmap_single(&alx->hw.pdev->dev,
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
	alx_free_txring_buf(alx);
	alx_free_rxring_buf(alx);
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

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(hw->mac_addr, addr->sa_data, netdev->addr_len);
	alx_set_macaddr(hw, hw->mac_addr);

	return 0;
}

static int alx_alloc_descriptors(struct alx_priv *alx)
{
	alx->txq.bufs = kcalloc(alx->tx_ringsz,
				sizeof(struct alx_buffer),
				GFP_KERNEL);
	if (!alx->txq.bufs)
		return -ENOMEM;

	alx->rxq.bufs = kcalloc(alx->rx_ringsz,
				sizeof(struct alx_buffer),
				GFP_KERNEL);
	if (!alx->rxq.bufs)
		goto out_free;

	/* physical tx/rx ring descriptors
	 *
	 * Allocate them as a single chunk because they must not cross a
	 * 4G boundary (hardware has a single register for high 32 bits
	 * of addresses only)
	 */
	alx->descmem.size = sizeof(struct alx_txd) * alx->tx_ringsz +
			    sizeof(struct alx_rrd) * alx->rx_ringsz +
			    sizeof(struct alx_rfd) * alx->rx_ringsz;
	alx->descmem.virt = dma_zalloc_coherent(&alx->hw.pdev->dev,
						alx->descmem.size,
						&alx->descmem.dma,
						GFP_KERNEL);
	if (!alx->descmem.virt)
		goto out_free;

	alx->txq.tpd = (void *)alx->descmem.virt;
	alx->txq.tpd_dma = alx->descmem.dma;

	/* alignment requirement for next block */
	BUILD_BUG_ON(sizeof(struct alx_txd) % 8);

	alx->rxq.rrd =
		(void *)((u8 *)alx->descmem.virt +
			 sizeof(struct alx_txd) * alx->tx_ringsz);
	alx->rxq.rrd_dma = alx->descmem.dma +
			   sizeof(struct alx_txd) * alx->tx_ringsz;

	/* alignment requirement for next block */
	BUILD_BUG_ON(sizeof(struct alx_rrd) % 8);

	alx->rxq.rfd =
		(void *)((u8 *)alx->descmem.virt +
			 sizeof(struct alx_txd) * alx->tx_ringsz +
			 sizeof(struct alx_rrd) * alx->rx_ringsz);
	alx->rxq.rfd_dma = alx->descmem.dma +
			   sizeof(struct alx_txd) * alx->tx_ringsz +
			   sizeof(struct alx_rrd) * alx->rx_ringsz;

	return 0;
out_free:
	kfree(alx->txq.bufs);
	kfree(alx->rxq.bufs);
	return -ENOMEM;
}

static int alx_alloc_rings(struct alx_priv *alx)
{
	int err;

	err = alx_alloc_descriptors(alx);
	if (err)
		return err;

	alx->int_mask &= ~ALX_ISR_ALL_QUEUES;
	alx->int_mask |= ALX_ISR_TX_Q0 | ALX_ISR_RX_Q0;
	alx->tx_ringsz = alx->tx_ringsz;

	netif_napi_add(alx->dev, &alx->napi, alx_poll, 64);

	alx_reinit_rings(alx);
	return 0;
}

static void alx_free_rings(struct alx_priv *alx)
{
	netif_napi_del(&alx->napi);
	alx_free_buffers(alx);

	kfree(alx->txq.bufs);
	kfree(alx->rxq.bufs);

	dma_free_coherent(&alx->hw.pdev->dev,
			  alx->descmem.size,
			  alx->descmem.virt,
			  alx->descmem.dma);
}

static void alx_config_vector_mapping(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;

	alx_write_mem32(hw, ALX_MSI_MAP_TBL1, 0);
	alx_write_mem32(hw, ALX_MSI_MAP_TBL2, 0);
	alx_write_mem32(hw, ALX_MSI_ID_MAP, 0);
}

static void alx_irq_enable(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;

	/* level-1 interrupt switch */
	alx_write_mem32(hw, ALX_ISR, 0);
	alx_write_mem32(hw, ALX_IMR, alx->int_mask);
	alx_post_write(hw);
}

static void alx_irq_disable(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;

	alx_write_mem32(hw, ALX_ISR, ALX_ISR_DIS);
	alx_write_mem32(hw, ALX_IMR, 0);
	alx_post_write(hw);

	synchronize_irq(alx->hw.pdev->irq);
}

static int alx_request_irq(struct alx_priv *alx)
{
	struct pci_dev *pdev = alx->hw.pdev;
	struct alx_hw *hw = &alx->hw;
	int err;
	u32 msi_ctrl;

	msi_ctrl = (hw->imt >> 1) << ALX_MSI_RETRANS_TM_SHIFT;

	if (!pci_enable_msi(alx->hw.pdev)) {
		alx->msi = true;

		alx_write_mem32(hw, ALX_MSI_RETRANS_TIMER,
				msi_ctrl | ALX_MSI_MASK_SEL_LINE);
		err = request_irq(pdev->irq, alx_intr_msi, 0,
				  alx->dev->name, alx);
		if (!err)
			goto out;
		/* fall back to legacy interrupt */
		pci_disable_msi(alx->hw.pdev);
	}

	alx_write_mem32(hw, ALX_MSI_RETRANS_TIMER, 0);
	err = request_irq(pdev->irq, alx_intr_legacy, IRQF_SHARED,
			  alx->dev->name, alx);
out:
	if (!err)
		alx_config_vector_mapping(alx);
	return err;
}

static void alx_free_irq(struct alx_priv *alx)
{
	struct pci_dev *pdev = alx->hw.pdev;

	free_irq(pdev->irq, alx);

	if (alx->msi) {
		pci_disable_msi(alx->hw.pdev);
		alx->msi = false;
	}
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
	alx->rxbuf_size = ALIGN(ALX_RAW_MTU(hw->mtu), 8);
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

	return err;
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
	alx->dev->trans_start = jiffies;
	if (netif_carrier_ok(alx->dev)) {
		netif_carrier_off(alx->dev);
		netif_tx_disable(alx->dev);
		napi_disable(&alx->napi);
	}
}

static void alx_halt(struct alx_priv *alx)
{
	struct alx_hw *hw = &alx->hw;

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
	ASSERT_RTNL();

	alx_halt(alx);
	alx_activate(alx);
}

static int alx_change_mtu(struct net_device *netdev, int mtu)
{
	struct alx_priv *alx = netdev_priv(netdev);
	int max_frame = mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	if ((max_frame < ALX_MIN_FRAME_SIZE) ||
	    (max_frame > ALX_MAX_FRAME_SIZE))
		return -EINVAL;

	if (netdev->mtu == mtu)
		return 0;

	netdev->mtu = mtu;
	alx->hw.mtu = mtu;
	alx->rxbuf_size = mtu > ALX_DEF_RXBUF_SIZE ?
			   ALIGN(max_frame, 8) : ALX_DEF_RXBUF_SIZE;
	netdev_update_features(netdev);
	if (netif_running(netdev))
		alx_reinit(alx);
	return 0;
}

static void alx_netif_start(struct alx_priv *alx)
{
	netif_tx_wake_all_queues(alx->dev);
	napi_enable(&alx->napi);
	netif_carrier_on(alx->dev);
}

static int __alx_open(struct alx_priv *alx, bool resume)
{
	int err;

	if (!resume)
		netif_carrier_off(alx->dev);

	err = alx_alloc_rings(alx);
	if (err)
		return err;

	alx_configure(alx);

	err = alx_request_irq(alx);
	if (err)
		goto out_free_rings;

	/* clear old interrupts */
	alx_write_mem32(&alx->hw, ALX_ISR, ~(u32)ALX_ISR_DIS);

	alx_irq_enable(alx);

	if (!resume)
		netif_tx_start_all_queues(alx->dev);

	alx_schedule_link_check(alx);
	return 0;

out_free_rings:
	alx_free_rings(alx);
	return err;
}

static void __alx_stop(struct alx_priv *alx)
{
	alx_halt(alx);
	alx_free_irq(alx);
	alx_free_rings(alx);
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
	u8 old_duplex;
	int err;

	/* clear PHY internal interrupt status, otherwise the main
	 * interrupt status will be asserted forever
	 */
	alx_clear_phy_intr(hw);

	old_speed = hw->link_speed;
	old_duplex = hw->duplex;
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
	return __alx_open(netdev_priv(netdev), false);
}

static int alx_stop(struct net_device *netdev)
{
	__alx_stop(netdev_priv(netdev));
	return 0;
}

static void alx_link_check(struct work_struct *work)
{
	struct alx_priv *alx;

	alx = container_of(work, struct alx_priv, link_check_wk);

	rtnl_lock();
	alx_check_link(alx);
	rtnl_unlock();
}

static void alx_reset(struct work_struct *work)
{
	struct alx_priv *alx = container_of(work, struct alx_priv, reset_wk);

	rtnl_lock();
	alx_reinit(alx);
	rtnl_unlock();
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

static int alx_map_tx_skb(struct alx_priv *alx, struct sk_buff *skb)
{
	struct alx_tx_queue *txq = &alx->txq;
	struct alx_txd *tpd, *first_tpd;
	dma_addr_t dma;
	int maplen, f, first_idx = txq->write_idx;

	first_tpd = &txq->tpd[txq->write_idx];
	tpd = first_tpd;

	maplen = skb_headlen(skb);
	dma = dma_map_single(&alx->hw.pdev->dev, skb->data, maplen,
			     DMA_TO_DEVICE);
	if (dma_mapping_error(&alx->hw.pdev->dev, dma))
		goto err_dma;

	dma_unmap_len_set(&txq->bufs[txq->write_idx], size, maplen);
	dma_unmap_addr_set(&txq->bufs[txq->write_idx], dma, dma);

	tpd->adrl.addr = cpu_to_le64(dma);
	tpd->len = cpu_to_le16(maplen);

	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];

		if (++txq->write_idx == alx->tx_ringsz)
			txq->write_idx = 0;
		tpd = &txq->tpd[txq->write_idx];

		tpd->word1 = first_tpd->word1;

		maplen = skb_frag_size(frag);
		dma = skb_frag_dma_map(&alx->hw.pdev->dev, frag, 0,
				       maplen, DMA_TO_DEVICE);
		if (dma_mapping_error(&alx->hw.pdev->dev, dma))
			goto err_dma;
		dma_unmap_len_set(&txq->bufs[txq->write_idx], size, maplen);
		dma_unmap_addr_set(&txq->bufs[txq->write_idx], dma, dma);

		tpd->adrl.addr = cpu_to_le64(dma);
		tpd->len = cpu_to_le16(maplen);
	}

	/* last TPD, set EOP flag and store skb */
	tpd->word1 |= cpu_to_le32(1 << TPD_EOP_SHIFT);
	txq->bufs[txq->write_idx].skb = skb;

	if (++txq->write_idx == alx->tx_ringsz)
		txq->write_idx = 0;

	return 0;

err_dma:
	f = first_idx;
	while (f != txq->write_idx) {
		alx_free_txbuf(alx, f);
		if (++f == alx->tx_ringsz)
			f = 0;
	}
	return -ENOMEM;
}

static netdev_tx_t alx_start_xmit(struct sk_buff *skb,
				  struct net_device *netdev)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_tx_queue *txq = &alx->txq;
	struct alx_txd *first;
	int tpdreq = skb_shinfo(skb)->nr_frags + 1;

	if (alx_tpd_avail(alx) < tpdreq) {
		netif_stop_queue(alx->dev);
		goto drop;
	}

	first = &txq->tpd[txq->write_idx];
	memset(first, 0, sizeof(*first));

	if (alx_tx_csum(skb, first))
		goto drop;

	if (alx_map_tx_skb(alx, skb) < 0)
		goto drop;

	netdev_sent_queue(alx->dev, skb->len);

	/* flush updates before updating hardware */
	wmb();
	alx_write_mem16(&alx->hw, ALX_TPD_PRI0_PIDX, txq->write_idx);

	if (alx_tpd_avail(alx) < alx->tx_ringsz/8)
		netif_stop_queue(alx->dev);

	return NETDEV_TX_OK;

drop:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void alx_tx_timeout(struct net_device *dev)
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

	if (alx->msi)
		alx_intr_msi(0, alx);
	else
		alx_intr_legacy(0, alx);
}
#endif

static const struct net_device_ops alx_netdev_ops = {
	.ndo_open               = alx_open,
	.ndo_stop               = alx_stop,
	.ndo_start_xmit         = alx_start_xmit,
	.ndo_set_rx_mode        = alx_set_rx_mode,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = alx_set_mac_address,
	.ndo_change_mtu         = alx_change_mtu,
	.ndo_do_ioctl           = alx_ioctl,
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
	int bars, err;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	/* The alx chip can DMA to 64-bit addresses, but it uses a single
	 * shared register for the high 32 bits, so only a single, aligned,
	 * 4 GB physical address range can be used for descriptors.
	 */
	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)) &&
	    !dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		dev_dbg(&pdev->dev, "DMA to 64-BIT addresses\n");
	} else {
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			err = dma_set_coherent_mask(&pdev->dev,
						    DMA_BIT_MASK(32));
			if (err) {
				dev_err(&pdev->dev,
					"No usable DMA config, aborting\n");
				goto out_pci_disable;
			}
		}
	}

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	err = pci_request_selected_regions(pdev, bars, alx_drv_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed(bars:%d)\n", bars);
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

	netdev = alloc_etherdev(sizeof(*alx));
	if (!netdev) {
		err = -ENOMEM;
		goto out_pci_release;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);
	alx = netdev_priv(netdev);
	spin_lock_init(&alx->hw.mdio_lock);
	spin_lock_init(&alx->irq_lock);
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
	SET_ETHTOOL_OPS(netdev, &alx_ethtool_ops);
	netdev->irq = pdev->irq;
	netdev->watchdog_timeo = ALX_WATCHDOG_TIME;

	if (ent->driver_data & ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG)
		pdev->dev_flags |= PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG;

	err = alx_init_sw(alx);
	if (err) {
		dev_err(&pdev->dev, "net device private data init failed\n");
		goto out_unmap;
	}

	alx_reset_pcie(hw);

	phy_configured = alx_phy_configured(hw);

	if (!phy_configured)
		alx_reset_phy(hw);

	err = alx_reset_mac(hw);
	if (err) {
		dev_err(&pdev->dev, "MAC Reset failed, error = %d\n", err);
		goto out_unmap;
	}

	/* setup link to put it in a known good starting state */
	if (!phy_configured) {
		err = alx_setup_speed_duplex(hw, hw->adv_cfg, hw->flowctrl);
		if (err) {
			dev_err(&pdev->dev,
				"failed to configure PHY speed/duplex (err=%d)\n",
				err);
			goto out_unmap;
		}
	}

	netdev->hw_features = NETIF_F_SG | NETIF_F_HW_CSUM;

	if (alx_get_perm_macaddr(hw, hw->perm_addr)) {
		dev_warn(&pdev->dev,
			 "Invalid permanent address programmed, using random one\n");
		eth_hw_addr_random(netdev);
		memcpy(hw->perm_addr, netdev->dev_addr, netdev->addr_len);
	}

	memcpy(hw->mac_addr, hw->perm_addr, ETH_ALEN);
	memcpy(netdev->dev_addr, hw->mac_addr, ETH_ALEN);
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
		goto out_unmap;
	}

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

out_unmap:
	iounmap(hw->hw_addr);
out_free_netdev:
	free_netdev(netdev);
out_pci_release:
	pci_release_selected_regions(pdev, bars);
out_pci_disable:
	pci_disable_device(pdev);
	return err;
}

static void alx_remove(struct pci_dev *pdev)
{
	struct alx_priv *alx = pci_get_drvdata(pdev);
	struct alx_hw *hw = &alx->hw;

	cancel_work_sync(&alx->link_check_wk);
	cancel_work_sync(&alx->reset_wk);

	/* restore permanent mac address */
	alx_set_macaddr(hw, hw->perm_addr);

	unregister_netdev(alx->dev);
	iounmap(hw->hw_addr);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);

	free_netdev(alx->dev);
}

#ifdef CONFIG_PM_SLEEP
static int alx_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct alx_priv *alx = pci_get_drvdata(pdev);

	if (!netif_running(alx->dev))
		return 0;
	netif_device_detach(alx->dev);
	__alx_stop(alx);
	return 0;
}

static int alx_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct alx_priv *alx = pci_get_drvdata(pdev);

	if (!netif_running(alx->dev))
		return 0;
	netif_device_attach(alx->dev);
	return __alx_open(alx, true);
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

	rtnl_lock();

	if (netif_running(netdev)) {
		netif_device_detach(netdev);
		alx_halt(alx);
	}

	if (state == pci_channel_io_perm_failure)
		rc = PCI_ERS_RESULT_DISCONNECT;
	else
		pci_disable_device(pdev);

	rtnl_unlock();

	return rc;
}

static pci_ers_result_t alx_pci_error_slot_reset(struct pci_dev *pdev)
{
	struct alx_priv *alx = pci_get_drvdata(pdev);
	struct alx_hw *hw = &alx->hw;
	pci_ers_result_t rc = PCI_ERS_RESULT_DISCONNECT;

	dev_info(&pdev->dev, "pci error slot reset\n");

	rtnl_lock();

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "Failed to re-enable PCI device after reset\n");
		goto out;
	}

	pci_set_master(pdev);

	alx_reset_pcie(hw);
	if (!alx_reset_mac(hw))
		rc = PCI_ERS_RESULT_RECOVERED;
out:
	pci_cleanup_aer_uncorrect_error_status(pdev);

	rtnl_unlock();

	return rc;
}

static void alx_pci_error_resume(struct pci_dev *pdev)
{
	struct alx_priv *alx = pci_get_drvdata(pdev);
	struct net_device *netdev = alx->dev;

	dev_info(&pdev->dev, "pci error resume\n");

	rtnl_lock();

	if (netif_running(netdev)) {
		alx_activate(alx);
		netif_device_attach(netdev);
	}

	rtnl_unlock();
}

static const struct pci_error_handlers alx_err_handlers = {
	.error_detected = alx_pci_error_detected,
	.slot_reset     = alx_pci_error_slot_reset,
	.resume         = alx_pci_error_resume,
};

static DEFINE_PCI_DEVICE_TABLE(alx_pci_tbl) = {
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_AR8161),
	  .driver_data = ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG },
	{ PCI_VDEVICE(ATTANSIC, ALX_DEV_ID_E2200),
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
MODULE_AUTHOR("Qualcomm Corporation, <nic-devel@qualcomm.com>");
MODULE_DESCRIPTION(
	"Qualcomm Atheros(R) AR816x/AR817x PCI-E Ethernet Network Driver");
MODULE_LICENSE("GPL");
