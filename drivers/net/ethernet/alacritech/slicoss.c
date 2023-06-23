// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Gigabit Ethernet adapters based on the Session Layer
 * Interface (SLIC) technology by Alacritech. The driver does not
 * support the hardware acceleration features provided by these cards.
 *
 * Copyright (C) 2016 Lino Sanfilippo <LinoSanfilippo@gmx.de>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/crc32.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/u64_stats_sync.h>

#include "slic.h"

#define DRV_NAME			"slicoss"

static const struct pci_device_id slic_id_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALACRITECH,
		     PCI_DEVICE_ID_ALACRITECH_MOJAVE) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ALACRITECH,
		     PCI_DEVICE_ID_ALACRITECH_OASIS) },
	{ 0 }
};

static const char slic_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"rx_bytes",
	"rx_multicasts",
	"rx_errors",
	"rx_buff_miss",
	"rx_tp_csum",
	"rx_tp_oflow",
	"rx_tp_hlen",
	"rx_ip_csum",
	"rx_ip_len",
	"rx_ip_hdr_len",
	"rx_early",
	"rx_buff_oflow",
	"rx_lcode",
	"rx_drbl",
	"rx_crc",
	"rx_oflow_802",
	"rx_uflow_802",
	"tx_packets",
	"tx_bytes",
	"tx_carrier",
	"tx_dropped",
	"irq_errs",
};

static inline int slic_next_queue_idx(unsigned int idx, unsigned int qlen)
{
	return (idx + 1) & (qlen - 1);
}

static inline int slic_get_free_queue_descs(unsigned int put_idx,
					    unsigned int done_idx,
					    unsigned int qlen)
{
	if (put_idx >= done_idx)
		return (qlen - (put_idx - done_idx) - 1);
	return (done_idx - put_idx - 1);
}

static unsigned int slic_next_compl_idx(struct slic_device *sdev)
{
	struct slic_stat_queue *stq = &sdev->stq;
	unsigned int active = stq->active_array;
	struct slic_stat_desc *descs;
	struct slic_stat_desc *stat;
	unsigned int idx;

	descs = stq->descs[active];
	stat = &descs[stq->done_idx];

	if (!stat->status)
		return SLIC_INVALID_STAT_DESC_IDX;

	idx = (le32_to_cpu(stat->hnd) & 0xffff) - 1;
	/* reset desc */
	stat->hnd = 0;
	stat->status = 0;

	stq->done_idx = slic_next_queue_idx(stq->done_idx, stq->len);
	/* check for wraparound */
	if (!stq->done_idx) {
		dma_addr_t paddr = stq->paddr[active];

		slic_write(sdev, SLIC_REG_RBAR, lower_32_bits(paddr) |
						stq->len);
		/* make sure new status descriptors are immediately available */
		slic_flush_write(sdev);
		active++;
		active &= (SLIC_NUM_STAT_DESC_ARRAYS - 1);
		stq->active_array = active;
	}
	return idx;
}

static unsigned int slic_get_free_tx_descs(struct slic_tx_queue *txq)
{
	/* ensure tail idx is updated */
	smp_mb();
	return slic_get_free_queue_descs(txq->put_idx, txq->done_idx, txq->len);
}

static unsigned int slic_get_free_rx_descs(struct slic_rx_queue *rxq)
{
	return slic_get_free_queue_descs(rxq->put_idx, rxq->done_idx, rxq->len);
}

static void slic_clear_upr_list(struct slic_upr_list *upr_list)
{
	struct slic_upr *upr;
	struct slic_upr *tmp;

	spin_lock_bh(&upr_list->lock);
	list_for_each_entry_safe(upr, tmp, &upr_list->list, list) {
		list_del(&upr->list);
		kfree(upr);
	}
	upr_list->pending = false;
	spin_unlock_bh(&upr_list->lock);
}

static void slic_start_upr(struct slic_device *sdev, struct slic_upr *upr)
{
	u32 reg;

	reg = (upr->type == SLIC_UPR_CONFIG) ? SLIC_REG_RCONFIG :
					       SLIC_REG_LSTAT;
	slic_write(sdev, reg, lower_32_bits(upr->paddr));
	slic_flush_write(sdev);
}

static void slic_queue_upr(struct slic_device *sdev, struct slic_upr *upr)
{
	struct slic_upr_list *upr_list = &sdev->upr_list;
	bool pending;

	spin_lock_bh(&upr_list->lock);
	pending = upr_list->pending;
	INIT_LIST_HEAD(&upr->list);
	list_add_tail(&upr->list, &upr_list->list);
	upr_list->pending = true;
	spin_unlock_bh(&upr_list->lock);

	if (!pending)
		slic_start_upr(sdev, upr);
}

static struct slic_upr *slic_dequeue_upr(struct slic_device *sdev)
{
	struct slic_upr_list *upr_list = &sdev->upr_list;
	struct slic_upr *next_upr = NULL;
	struct slic_upr *upr = NULL;

	spin_lock_bh(&upr_list->lock);
	if (!list_empty(&upr_list->list)) {
		upr = list_first_entry(&upr_list->list, struct slic_upr, list);
		list_del(&upr->list);

		if (list_empty(&upr_list->list))
			upr_list->pending = false;
		else
			next_upr = list_first_entry(&upr_list->list,
						    struct slic_upr, list);
	}
	spin_unlock_bh(&upr_list->lock);
	/* trigger processing of the next upr in list */
	if (next_upr)
		slic_start_upr(sdev, next_upr);

	return upr;
}

static int slic_new_upr(struct slic_device *sdev, unsigned int type,
			dma_addr_t paddr)
{
	struct slic_upr *upr;

	upr = kmalloc(sizeof(*upr), GFP_ATOMIC);
	if (!upr)
		return -ENOMEM;
	upr->type = type;
	upr->paddr = paddr;

	slic_queue_upr(sdev, upr);

	return 0;
}

static void slic_set_mcast_bit(u64 *mcmask, unsigned char const *addr)
{
	u64 mask = *mcmask;
	u8 crc;
	/* Get the CRC polynomial for the mac address: we use bits 1-8 (lsb),
	 * bitwise reversed, msb (= lsb bit 0 before bitrev) is automatically
	 * discarded.
	 */
	crc = ether_crc(ETH_ALEN, addr) >> 23;
	 /* we only have space on the SLIC for 64 entries */
	crc &= 0x3F;
	mask |= (u64)1 << crc;
	*mcmask = mask;
}

/* must be called with link_lock held */
static void slic_configure_rcv(struct slic_device *sdev)
{
	u32 val;

	val = SLIC_GRCR_RESET | SLIC_GRCR_ADDRAEN | SLIC_GRCR_RCVEN |
	      SLIC_GRCR_HASHSIZE << SLIC_GRCR_HASHSIZE_SHIFT | SLIC_GRCR_RCVBAD;

	if (sdev->duplex == DUPLEX_FULL)
		val |= SLIC_GRCR_CTLEN;

	if (sdev->promisc)
		val |= SLIC_GRCR_RCVALL;

	slic_write(sdev, SLIC_REG_WRCFG, val);
}

/* must be called with link_lock held */
static void slic_configure_xmt(struct slic_device *sdev)
{
	u32 val;

	val = SLIC_GXCR_RESET | SLIC_GXCR_XMTEN;

	if (sdev->duplex == DUPLEX_FULL)
		val |= SLIC_GXCR_PAUSEEN;

	slic_write(sdev, SLIC_REG_WXCFG, val);
}

/* must be called with link_lock held */
static void slic_configure_mac(struct slic_device *sdev)
{
	u32 val;

	if (sdev->speed == SPEED_1000) {
		val = SLIC_GMCR_GAPBB_1000 << SLIC_GMCR_GAPBB_SHIFT |
		      SLIC_GMCR_GAPR1_1000 << SLIC_GMCR_GAPR1_SHIFT |
		      SLIC_GMCR_GAPR2_1000 << SLIC_GMCR_GAPR2_SHIFT |
		      SLIC_GMCR_GBIT; /* enable GMII */
	} else {
		val = SLIC_GMCR_GAPBB_100 << SLIC_GMCR_GAPBB_SHIFT |
		      SLIC_GMCR_GAPR1_100 << SLIC_GMCR_GAPR1_SHIFT |
		      SLIC_GMCR_GAPR2_100 << SLIC_GMCR_GAPR2_SHIFT;
	}

	if (sdev->duplex == DUPLEX_FULL)
		val |= SLIC_GMCR_FULLD;

	slic_write(sdev, SLIC_REG_WMCFG, val);
}

static void slic_configure_link_locked(struct slic_device *sdev, int speed,
				       unsigned int duplex)
{
	struct net_device *dev = sdev->netdev;

	if (sdev->speed == speed && sdev->duplex == duplex)
		return;

	sdev->speed = speed;
	sdev->duplex = duplex;

	if (sdev->speed == SPEED_UNKNOWN) {
		if (netif_carrier_ok(dev))
			netif_carrier_off(dev);
	} else {
		/* (re)configure link settings */
		slic_configure_mac(sdev);
		slic_configure_xmt(sdev);
		slic_configure_rcv(sdev);
		slic_flush_write(sdev);

		if (!netif_carrier_ok(dev))
			netif_carrier_on(dev);
	}
}

static void slic_configure_link(struct slic_device *sdev, int speed,
				unsigned int duplex)
{
	spin_lock_bh(&sdev->link_lock);
	slic_configure_link_locked(sdev, speed, duplex);
	spin_unlock_bh(&sdev->link_lock);
}

static void slic_set_rx_mode(struct net_device *dev)
{
	struct slic_device *sdev = netdev_priv(dev);
	struct netdev_hw_addr *hwaddr;
	bool set_promisc;
	u64 mcmask;

	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		/* Turn on all multicast addresses. We have to do this for
		 * promiscuous mode as well as ALLMCAST mode (it saves the
		 * microcode from having to keep state about the MAC
		 * configuration).
		 */
		mcmask = ~(u64)0;
	} else  {
		mcmask = 0;

		netdev_for_each_mc_addr(hwaddr, dev) {
			slic_set_mcast_bit(&mcmask, hwaddr->addr);
		}
	}

	slic_write(sdev, SLIC_REG_MCASTLOW, lower_32_bits(mcmask));
	slic_write(sdev, SLIC_REG_MCASTHIGH, upper_32_bits(mcmask));

	set_promisc = !!(dev->flags & IFF_PROMISC);

	spin_lock_bh(&sdev->link_lock);
	if (sdev->promisc != set_promisc) {
		sdev->promisc = set_promisc;
		slic_configure_rcv(sdev);
	}
	spin_unlock_bh(&sdev->link_lock);
}

static void slic_xmit_complete(struct slic_device *sdev)
{
	struct slic_tx_queue *txq = &sdev->txq;
	struct net_device *dev = sdev->netdev;
	struct slic_tx_buffer *buff;
	unsigned int frames = 0;
	unsigned int bytes = 0;
	unsigned int idx;

	/* Limit processing to SLIC_MAX_TX_COMPLETIONS frames to avoid that new
	 * completions during processing keeps the loop running endlessly.
	 */
	do {
		idx = slic_next_compl_idx(sdev);
		if (idx == SLIC_INVALID_STAT_DESC_IDX)
			break;

		txq->done_idx = idx;
		buff = &txq->txbuffs[idx];

		if (unlikely(!buff->skb)) {
			netdev_warn(dev,
				    "no skb found for desc idx %i\n", idx);
			continue;
		}
		dma_unmap_single(&sdev->pdev->dev,
				 dma_unmap_addr(buff, map_addr),
				 dma_unmap_len(buff, map_len), DMA_TO_DEVICE);

		bytes += buff->skb->len;
		frames++;

		dev_kfree_skb_any(buff->skb);
		buff->skb = NULL;
	} while (frames < SLIC_MAX_TX_COMPLETIONS);
	/* make sure xmit sees the new value for done_idx */
	smp_wmb();

	u64_stats_update_begin(&sdev->stats.syncp);
	sdev->stats.tx_bytes += bytes;
	sdev->stats.tx_packets += frames;
	u64_stats_update_end(&sdev->stats.syncp);

	netif_tx_lock(dev);
	if (netif_queue_stopped(dev) &&
	    (slic_get_free_tx_descs(txq) >= SLIC_MIN_TX_WAKEUP_DESCS))
		netif_wake_queue(dev);
	netif_tx_unlock(dev);
}

static void slic_refill_rx_queue(struct slic_device *sdev, gfp_t gfp)
{
	const unsigned int ALIGN_MASK = SLIC_RX_BUFF_ALIGN - 1;
	unsigned int maplen = SLIC_RX_BUFF_SIZE;
	struct slic_rx_queue *rxq = &sdev->rxq;
	struct net_device *dev = sdev->netdev;
	struct slic_rx_buffer *buff;
	struct slic_rx_desc *desc;
	unsigned int misalign;
	unsigned int offset;
	struct sk_buff *skb;
	dma_addr_t paddr;

	while (slic_get_free_rx_descs(rxq) > SLIC_MAX_REQ_RX_DESCS) {
		skb = alloc_skb(maplen + ALIGN_MASK, gfp);
		if (!skb)
			break;

		paddr = dma_map_single(&sdev->pdev->dev, skb->data, maplen,
				       DMA_FROM_DEVICE);
		if (dma_mapping_error(&sdev->pdev->dev, paddr)) {
			netdev_err(dev, "mapping rx packet failed\n");
			/* drop skb */
			dev_kfree_skb_any(skb);
			break;
		}
		/* ensure head buffer descriptors are 256 byte aligned */
		offset = 0;
		misalign = paddr & ALIGN_MASK;
		if (misalign) {
			offset = SLIC_RX_BUFF_ALIGN - misalign;
			skb_reserve(skb, offset);
		}
		/* the HW expects dma chunks for descriptor + frame data */
		desc = (struct slic_rx_desc *)skb->data;
		/* temporarily sync descriptor for CPU to clear status */
		dma_sync_single_for_cpu(&sdev->pdev->dev, paddr,
					offset + sizeof(*desc),
					DMA_FROM_DEVICE);
		desc->status = 0;
		/* return it to HW again */
		dma_sync_single_for_device(&sdev->pdev->dev, paddr,
					   offset + sizeof(*desc),
					   DMA_FROM_DEVICE);

		buff = &rxq->rxbuffs[rxq->put_idx];
		buff->skb = skb;
		dma_unmap_addr_set(buff, map_addr, paddr);
		dma_unmap_len_set(buff, map_len, maplen);
		buff->addr_offset = offset;
		/* complete write to descriptor before it is handed to HW */
		wmb();
		/* head buffer descriptors are placed immediately before skb */
		slic_write(sdev, SLIC_REG_HBAR, lower_32_bits(paddr) + offset);
		rxq->put_idx = slic_next_queue_idx(rxq->put_idx, rxq->len);
	}
}

static void slic_handle_frame_error(struct slic_device *sdev,
				    struct sk_buff *skb)
{
	struct slic_stats *stats = &sdev->stats;

	if (sdev->model == SLIC_MODEL_OASIS) {
		struct slic_rx_info_oasis *info;
		u32 status_b;
		u32 status;

		info = (struct slic_rx_info_oasis *)skb->data;
		status = le32_to_cpu(info->frame_status);
		status_b = le32_to_cpu(info->frame_status_b);
		/* transport layer */
		if (status_b & SLIC_VRHSTATB_TPCSUM)
			SLIC_INC_STATS_COUNTER(stats, rx_tpcsum);
		if (status & SLIC_VRHSTAT_TPOFLO)
			SLIC_INC_STATS_COUNTER(stats, rx_tpoflow);
		if (status_b & SLIC_VRHSTATB_TPHLEN)
			SLIC_INC_STATS_COUNTER(stats, rx_tphlen);
		/* ip layer */
		if (status_b & SLIC_VRHSTATB_IPCSUM)
			SLIC_INC_STATS_COUNTER(stats, rx_ipcsum);
		if (status_b & SLIC_VRHSTATB_IPLERR)
			SLIC_INC_STATS_COUNTER(stats, rx_iplen);
		if (status_b & SLIC_VRHSTATB_IPHERR)
			SLIC_INC_STATS_COUNTER(stats, rx_iphlen);
		/* link layer */
		if (status_b & SLIC_VRHSTATB_RCVE)
			SLIC_INC_STATS_COUNTER(stats, rx_early);
		if (status_b & SLIC_VRHSTATB_BUFF)
			SLIC_INC_STATS_COUNTER(stats, rx_buffoflow);
		if (status_b & SLIC_VRHSTATB_CODE)
			SLIC_INC_STATS_COUNTER(stats, rx_lcode);
		if (status_b & SLIC_VRHSTATB_DRBL)
			SLIC_INC_STATS_COUNTER(stats, rx_drbl);
		if (status_b & SLIC_VRHSTATB_CRC)
			SLIC_INC_STATS_COUNTER(stats, rx_crc);
		if (status & SLIC_VRHSTAT_802OE)
			SLIC_INC_STATS_COUNTER(stats, rx_oflow802);
		if (status_b & SLIC_VRHSTATB_802UE)
			SLIC_INC_STATS_COUNTER(stats, rx_uflow802);
		if (status_b & SLIC_VRHSTATB_CARRE)
			SLIC_INC_STATS_COUNTER(stats, tx_carrier);
	} else { /* mojave */
		struct slic_rx_info_mojave *info;
		u32 status;

		info = (struct slic_rx_info_mojave *)skb->data;
		status = le32_to_cpu(info->frame_status);
		/* transport layer */
		if (status & SLIC_VGBSTAT_XPERR) {
			u32 xerr = status >> SLIC_VGBSTAT_XERRSHFT;

			if (xerr == SLIC_VGBSTAT_XCSERR)
				SLIC_INC_STATS_COUNTER(stats, rx_tpcsum);
			if (xerr == SLIC_VGBSTAT_XUFLOW)
				SLIC_INC_STATS_COUNTER(stats, rx_tpoflow);
			if (xerr == SLIC_VGBSTAT_XHLEN)
				SLIC_INC_STATS_COUNTER(stats, rx_tphlen);
		}
		/* ip layer */
		if (status & SLIC_VGBSTAT_NETERR) {
			u32 nerr = status >> SLIC_VGBSTAT_NERRSHFT &
				   SLIC_VGBSTAT_NERRMSK;

			if (nerr == SLIC_VGBSTAT_NCSERR)
				SLIC_INC_STATS_COUNTER(stats, rx_ipcsum);
			if (nerr == SLIC_VGBSTAT_NUFLOW)
				SLIC_INC_STATS_COUNTER(stats, rx_iplen);
			if (nerr == SLIC_VGBSTAT_NHLEN)
				SLIC_INC_STATS_COUNTER(stats, rx_iphlen);
		}
		/* link layer */
		if (status & SLIC_VGBSTAT_LNKERR) {
			u32 lerr = status & SLIC_VGBSTAT_LERRMSK;

			if (lerr == SLIC_VGBSTAT_LDEARLY)
				SLIC_INC_STATS_COUNTER(stats, rx_early);
			if (lerr == SLIC_VGBSTAT_LBOFLO)
				SLIC_INC_STATS_COUNTER(stats, rx_buffoflow);
			if (lerr == SLIC_VGBSTAT_LCODERR)
				SLIC_INC_STATS_COUNTER(stats, rx_lcode);
			if (lerr == SLIC_VGBSTAT_LDBLNBL)
				SLIC_INC_STATS_COUNTER(stats, rx_drbl);
			if (lerr == SLIC_VGBSTAT_LCRCERR)
				SLIC_INC_STATS_COUNTER(stats, rx_crc);
			if (lerr == SLIC_VGBSTAT_LOFLO)
				SLIC_INC_STATS_COUNTER(stats, rx_oflow802);
			if (lerr == SLIC_VGBSTAT_LUFLO)
				SLIC_INC_STATS_COUNTER(stats, rx_uflow802);
		}
	}
	SLIC_INC_STATS_COUNTER(stats, rx_errors);
}

static void slic_handle_receive(struct slic_device *sdev, unsigned int todo,
				unsigned int *done)
{
	struct slic_rx_queue *rxq = &sdev->rxq;
	struct net_device *dev = sdev->netdev;
	struct slic_rx_buffer *buff;
	struct slic_rx_desc *desc;
	unsigned int frames = 0;
	unsigned int bytes = 0;
	struct sk_buff *skb;
	u32 status;
	u32 len;

	while (todo && (rxq->done_idx != rxq->put_idx)) {
		buff = &rxq->rxbuffs[rxq->done_idx];

		skb = buff->skb;
		if (!skb)
			break;

		desc = (struct slic_rx_desc *)skb->data;

		dma_sync_single_for_cpu(&sdev->pdev->dev,
					dma_unmap_addr(buff, map_addr),
					buff->addr_offset + sizeof(*desc),
					DMA_FROM_DEVICE);

		status = le32_to_cpu(desc->status);
		if (!(status & SLIC_IRHDDR_SVALID)) {
			dma_sync_single_for_device(&sdev->pdev->dev,
						   dma_unmap_addr(buff,
								  map_addr),
						   buff->addr_offset +
						   sizeof(*desc),
						   DMA_FROM_DEVICE);
			break;
		}

		buff->skb = NULL;

		dma_unmap_single(&sdev->pdev->dev,
				 dma_unmap_addr(buff, map_addr),
				 dma_unmap_len(buff, map_len),
				 DMA_FROM_DEVICE);

		/* skip rx descriptor that is placed before the frame data */
		skb_reserve(skb, SLIC_RX_BUFF_HDR_SIZE);

		if (unlikely(status & SLIC_IRHDDR_ERR)) {
			slic_handle_frame_error(sdev, skb);
			dev_kfree_skb_any(skb);
		} else {
			struct ethhdr *eh = (struct ethhdr *)skb->data;

			if (is_multicast_ether_addr(eh->h_dest))
				SLIC_INC_STATS_COUNTER(&sdev->stats, rx_mcasts);

			len = le32_to_cpu(desc->length) & SLIC_IRHDDR_FLEN_MSK;
			skb_put(skb, len);
			skb->protocol = eth_type_trans(skb, dev);
			skb->ip_summed = CHECKSUM_UNNECESSARY;

			napi_gro_receive(&sdev->napi, skb);

			bytes += len;
			frames++;
		}
		rxq->done_idx = slic_next_queue_idx(rxq->done_idx, rxq->len);
		todo--;
	}

	u64_stats_update_begin(&sdev->stats.syncp);
	sdev->stats.rx_bytes += bytes;
	sdev->stats.rx_packets += frames;
	u64_stats_update_end(&sdev->stats.syncp);

	slic_refill_rx_queue(sdev, GFP_ATOMIC);
}

static void slic_handle_link_irq(struct slic_device *sdev)
{
	struct slic_shmem *sm = &sdev->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;
	unsigned int duplex;
	int speed;
	u32 link;

	link = le32_to_cpu(sm_data->link);

	if (link & SLIC_GIG_LINKUP) {
		if (link & SLIC_GIG_SPEED_1000)
			speed = SPEED_1000;
		else if (link & SLIC_GIG_SPEED_100)
			speed = SPEED_100;
		else
			speed = SPEED_10;

		duplex = (link & SLIC_GIG_FULLDUPLEX) ? DUPLEX_FULL :
							DUPLEX_HALF;
	} else {
		duplex = DUPLEX_UNKNOWN;
		speed = SPEED_UNKNOWN;
	}
	slic_configure_link(sdev, speed, duplex);
}

static void slic_handle_upr_irq(struct slic_device *sdev, u32 irqs)
{
	struct slic_upr *upr;

	/* remove upr that caused this irq (always the first entry in list) */
	upr = slic_dequeue_upr(sdev);
	if (!upr) {
		netdev_warn(sdev->netdev, "no upr found on list\n");
		return;
	}

	if (upr->type == SLIC_UPR_LSTAT) {
		if (unlikely(irqs & SLIC_ISR_UPCERR_MASK)) {
			/* try again */
			slic_queue_upr(sdev, upr);
			return;
		}
		slic_handle_link_irq(sdev);
	}
	kfree(upr);
}

static int slic_handle_link_change(struct slic_device *sdev)
{
	return slic_new_upr(sdev, SLIC_UPR_LSTAT, sdev->shmem.link_paddr);
}

static void slic_handle_err_irq(struct slic_device *sdev, u32 isr)
{
	struct slic_stats *stats = &sdev->stats;

	if (isr & SLIC_ISR_RMISS)
		SLIC_INC_STATS_COUNTER(stats, rx_buff_miss);
	if (isr & SLIC_ISR_XDROP)
		SLIC_INC_STATS_COUNTER(stats, tx_dropped);
	if (!(isr & (SLIC_ISR_RMISS | SLIC_ISR_XDROP)))
		SLIC_INC_STATS_COUNTER(stats, irq_errs);
}

static void slic_handle_irq(struct slic_device *sdev, u32 isr,
			    unsigned int todo, unsigned int *done)
{
	if (isr & SLIC_ISR_ERR)
		slic_handle_err_irq(sdev, isr);

	if (isr & SLIC_ISR_LEVENT)
		slic_handle_link_change(sdev);

	if (isr & SLIC_ISR_UPC_MASK)
		slic_handle_upr_irq(sdev, isr);

	if (isr & SLIC_ISR_RCV)
		slic_handle_receive(sdev, todo, done);

	if (isr & SLIC_ISR_CMD)
		slic_xmit_complete(sdev);
}

static int slic_poll(struct napi_struct *napi, int todo)
{
	struct slic_device *sdev = container_of(napi, struct slic_device, napi);
	struct slic_shmem *sm = &sdev->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;
	u32 isr = le32_to_cpu(sm_data->isr);
	int done = 0;

	slic_handle_irq(sdev, isr, todo, &done);

	if (done < todo) {
		napi_complete_done(napi, done);
		/* reenable irqs */
		sm_data->isr = 0;
		/* make sure sm_data->isr is cleard before irqs are reenabled */
		wmb();
		slic_write(sdev, SLIC_REG_ISR, 0);
		slic_flush_write(sdev);
	}

	return done;
}

static irqreturn_t slic_irq(int irq, void *dev_id)
{
	struct slic_device *sdev = dev_id;
	struct slic_shmem *sm = &sdev->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;

	slic_write(sdev, SLIC_REG_ICR, SLIC_ICR_INT_MASK);
	slic_flush_write(sdev);
	/* make sure sm_data->isr is read after ICR_INT_MASK is set */
	wmb();

	if (!sm_data->isr) {
		dma_rmb();
		/* spurious interrupt */
		slic_write(sdev, SLIC_REG_ISR, 0);
		slic_flush_write(sdev);
		return IRQ_NONE;
	}

	napi_schedule_irqoff(&sdev->napi);

	return IRQ_HANDLED;
}

static void slic_card_reset(struct slic_device *sdev)
{
	u16 cmd;

	slic_write(sdev, SLIC_REG_RESET, SLIC_RESET_MAGIC);
	/* flush write by means of config space */
	pci_read_config_word(sdev->pdev, PCI_COMMAND, &cmd);
	mdelay(1);
}

static int slic_init_stat_queue(struct slic_device *sdev)
{
	const unsigned int DESC_ALIGN_MASK = SLIC_STATS_DESC_ALIGN - 1;
	struct slic_stat_queue *stq = &sdev->stq;
	struct slic_stat_desc *descs;
	unsigned int misalign;
	unsigned int offset;
	dma_addr_t paddr;
	size_t size;
	int err;
	int i;

	stq->len = SLIC_NUM_STAT_DESCS;
	stq->active_array = 0;
	stq->done_idx = 0;

	size = stq->len * sizeof(*descs) + DESC_ALIGN_MASK;

	for (i = 0; i < SLIC_NUM_STAT_DESC_ARRAYS; i++) {
		descs = dma_alloc_coherent(&sdev->pdev->dev, size, &paddr,
					   GFP_KERNEL);
		if (!descs) {
			netdev_err(sdev->netdev,
				   "failed to allocate status descriptors\n");
			err = -ENOMEM;
			goto free_descs;
		}
		/* ensure correct alignment */
		offset = 0;
		misalign = paddr & DESC_ALIGN_MASK;
		if (misalign) {
			offset = SLIC_STATS_DESC_ALIGN - misalign;
			descs += offset;
			paddr += offset;
		}

		slic_write(sdev, SLIC_REG_RBAR, lower_32_bits(paddr) |
						stq->len);
		stq->descs[i] = descs;
		stq->paddr[i] = paddr;
		stq->addr_offset[i] = offset;
	}

	stq->mem_size = size;

	return 0;

free_descs:
	while (i--) {
		dma_free_coherent(&sdev->pdev->dev, stq->mem_size,
				  stq->descs[i] - stq->addr_offset[i],
				  stq->paddr[i] - stq->addr_offset[i]);
	}

	return err;
}

static void slic_free_stat_queue(struct slic_device *sdev)
{
	struct slic_stat_queue *stq = &sdev->stq;
	int i;

	for (i = 0; i < SLIC_NUM_STAT_DESC_ARRAYS; i++) {
		dma_free_coherent(&sdev->pdev->dev, stq->mem_size,
				  stq->descs[i] - stq->addr_offset[i],
				  stq->paddr[i] - stq->addr_offset[i]);
	}
}

static int slic_init_tx_queue(struct slic_device *sdev)
{
	struct slic_tx_queue *txq = &sdev->txq;
	struct slic_tx_buffer *buff;
	struct slic_tx_desc *desc;
	unsigned int i;
	int err;

	txq->len = SLIC_NUM_TX_DESCS;
	txq->put_idx = 0;
	txq->done_idx = 0;

	txq->txbuffs = kcalloc(txq->len, sizeof(*buff), GFP_KERNEL);
	if (!txq->txbuffs)
		return -ENOMEM;

	txq->dma_pool = dma_pool_create("slic_pool", &sdev->pdev->dev,
					sizeof(*desc), SLIC_TX_DESC_ALIGN,
					4096);
	if (!txq->dma_pool) {
		err = -ENOMEM;
		netdev_err(sdev->netdev, "failed to create dma pool\n");
		goto free_buffs;
	}

	for (i = 0; i < txq->len; i++) {
		buff = &txq->txbuffs[i];
		desc = dma_pool_zalloc(txq->dma_pool, GFP_KERNEL,
				       &buff->desc_paddr);
		if (!desc) {
			netdev_err(sdev->netdev,
				   "failed to alloc pool chunk (%i)\n", i);
			err = -ENOMEM;
			goto free_descs;
		}

		desc->hnd = cpu_to_le32((u32)(i + 1));
		desc->cmd = SLIC_CMD_XMT_REQ;
		desc->flags = 0;
		desc->type = cpu_to_le32(SLIC_CMD_TYPE_DUMB);
		buff->desc = desc;
	}

	return 0;

free_descs:
	while (i--) {
		buff = &txq->txbuffs[i];
		dma_pool_free(txq->dma_pool, buff->desc, buff->desc_paddr);
	}
	dma_pool_destroy(txq->dma_pool);

free_buffs:
	kfree(txq->txbuffs);

	return err;
}

static void slic_free_tx_queue(struct slic_device *sdev)
{
	struct slic_tx_queue *txq = &sdev->txq;
	struct slic_tx_buffer *buff;
	unsigned int i;

	for (i = 0; i < txq->len; i++) {
		buff = &txq->txbuffs[i];
		dma_pool_free(txq->dma_pool, buff->desc, buff->desc_paddr);
		if (!buff->skb)
			continue;

		dma_unmap_single(&sdev->pdev->dev,
				 dma_unmap_addr(buff, map_addr),
				 dma_unmap_len(buff, map_len), DMA_TO_DEVICE);
		consume_skb(buff->skb);
	}
	dma_pool_destroy(txq->dma_pool);

	kfree(txq->txbuffs);
}

static int slic_init_rx_queue(struct slic_device *sdev)
{
	struct slic_rx_queue *rxq = &sdev->rxq;
	struct slic_rx_buffer *buff;

	rxq->len = SLIC_NUM_RX_LES;
	rxq->done_idx = 0;
	rxq->put_idx = 0;

	buff = kcalloc(rxq->len, sizeof(*buff), GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	rxq->rxbuffs = buff;
	slic_refill_rx_queue(sdev, GFP_KERNEL);

	return 0;
}

static void slic_free_rx_queue(struct slic_device *sdev)
{
	struct slic_rx_queue *rxq = &sdev->rxq;
	struct slic_rx_buffer *buff;
	unsigned int i;

	/* free rx buffers */
	for (i = 0; i < rxq->len; i++) {
		buff = &rxq->rxbuffs[i];

		if (!buff->skb)
			continue;

		dma_unmap_single(&sdev->pdev->dev,
				 dma_unmap_addr(buff, map_addr),
				 dma_unmap_len(buff, map_len),
				 DMA_FROM_DEVICE);
		consume_skb(buff->skb);
	}
	kfree(rxq->rxbuffs);
}

static void slic_set_link_autoneg(struct slic_device *sdev)
{
	unsigned int subid = sdev->pdev->subsystem_device;
	u32 val;

	if (sdev->is_fiber) {
		/* We've got a fiber gigabit interface, and register 4 is
		 * different in fiber mode than in copper mode.
		 */
		/* advertise FD only @1000 Mb */
		val = MII_ADVERTISE << 16 | ADVERTISE_1000XFULL |
		      ADVERTISE_1000XPAUSE | ADVERTISE_1000XPSE_ASYM;
		/* enable PAUSE frames */
		slic_write(sdev, SLIC_REG_WPHY, val);
		/* reset phy, enable auto-neg  */
		val = MII_BMCR << 16 | BMCR_RESET | BMCR_ANENABLE |
		      BMCR_ANRESTART;
		slic_write(sdev, SLIC_REG_WPHY, val);
	} else {	/* copper gigabit */
		/* We've got a copper gigabit interface, and register 4 is
		 * different in copper mode than in fiber mode.
		 */
		/* advertise 10/100 Mb modes   */
		val = MII_ADVERTISE << 16 | ADVERTISE_100FULL |
		      ADVERTISE_100HALF | ADVERTISE_10FULL | ADVERTISE_10HALF;
		/* enable PAUSE frames  */
		val |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
		/* required by the Cicada PHY  */
		val |= ADVERTISE_CSMA;
		slic_write(sdev, SLIC_REG_WPHY, val);

		/* advertise FD only @1000 Mb  */
		val = MII_CTRL1000 << 16 | ADVERTISE_1000FULL;
		slic_write(sdev, SLIC_REG_WPHY, val);

		if (subid != PCI_SUBDEVICE_ID_ALACRITECH_CICADA) {
			 /* if a Marvell PHY enable auto crossover */
			val = SLIC_MIICR_REG_16 | SLIC_MRV_REG16_XOVERON;
			slic_write(sdev, SLIC_REG_WPHY, val);

			/* reset phy, enable auto-neg  */
			val = MII_BMCR << 16 | BMCR_RESET | BMCR_ANENABLE |
			      BMCR_ANRESTART;
			slic_write(sdev, SLIC_REG_WPHY, val);
		} else {
			/* enable and restart auto-neg (don't reset)  */
			val = MII_BMCR << 16 | BMCR_ANENABLE | BMCR_ANRESTART;
			slic_write(sdev, SLIC_REG_WPHY, val);
		}
	}
}

static void slic_set_mac_address(struct slic_device *sdev)
{
	const u8 *addr = sdev->netdev->dev_addr;
	u32 val;

	val = addr[5] | addr[4] << 8 | addr[3] << 16 | addr[2] << 24;

	slic_write(sdev, SLIC_REG_WRADDRAL, val);
	slic_write(sdev, SLIC_REG_WRADDRBL, val);

	val = addr[0] << 8 | addr[1];

	slic_write(sdev, SLIC_REG_WRADDRAH, val);
	slic_write(sdev, SLIC_REG_WRADDRBH, val);
	slic_flush_write(sdev);
}

static u32 slic_read_dword_from_firmware(const struct firmware *fw, int *offset)
{
	int idx = *offset;
	__le32 val;

	memcpy(&val, fw->data + *offset, sizeof(val));
	idx += 4;
	*offset = idx;

	return le32_to_cpu(val);
}

MODULE_FIRMWARE(SLIC_RCV_FIRMWARE_MOJAVE);
MODULE_FIRMWARE(SLIC_RCV_FIRMWARE_OASIS);

static int slic_load_rcvseq_firmware(struct slic_device *sdev)
{
	const struct firmware *fw;
	const char *file;
	u32 codelen;
	int idx = 0;
	u32 instr;
	u32 addr;
	int err;

	file = (sdev->model == SLIC_MODEL_OASIS) ?  SLIC_RCV_FIRMWARE_OASIS :
						    SLIC_RCV_FIRMWARE_MOJAVE;
	err = request_firmware(&fw, file, &sdev->pdev->dev);
	if (err) {
		dev_err(&sdev->pdev->dev,
			"failed to load receive sequencer firmware %s\n", file);
		return err;
	}
	/* Do an initial sanity check concerning firmware size now. A further
	 * check follows below.
	 */
	if (fw->size < SLIC_FIRMWARE_MIN_SIZE) {
		dev_err(&sdev->pdev->dev,
			"invalid firmware size %zu (min %u expected)\n",
			fw->size, SLIC_FIRMWARE_MIN_SIZE);
		err = -EINVAL;
		goto release;
	}

	codelen = slic_read_dword_from_firmware(fw, &idx);

	/* do another sanity check against firmware size */
	if ((codelen + 4) > fw->size) {
		dev_err(&sdev->pdev->dev,
			"invalid rcv-sequencer firmware size %zu\n", fw->size);
		err = -EINVAL;
		goto release;
	}

	/* download sequencer code to card */
	slic_write(sdev, SLIC_REG_RCV_WCS, SLIC_RCVWCS_BEGIN);
	for (addr = 0; addr < codelen; addr++) {
		__le32 val;
		/* write out instruction address */
		slic_write(sdev, SLIC_REG_RCV_WCS, addr);

		instr = slic_read_dword_from_firmware(fw, &idx);
		/* write out the instruction data low addr */
		slic_write(sdev, SLIC_REG_RCV_WCS, instr);

		val = (__le32)fw->data[idx];
		instr = le32_to_cpu(val);
		idx++;
		/* write out the instruction data high addr */
		slic_write(sdev, SLIC_REG_RCV_WCS, instr);
	}
	/* finish download */
	slic_write(sdev, SLIC_REG_RCV_WCS, SLIC_RCVWCS_FINISH);
	slic_flush_write(sdev);
release:
	release_firmware(fw);

	return err;
}

MODULE_FIRMWARE(SLIC_FIRMWARE_MOJAVE);
MODULE_FIRMWARE(SLIC_FIRMWARE_OASIS);

static int slic_load_firmware(struct slic_device *sdev)
{
	u32 sectstart[SLIC_FIRMWARE_MAX_SECTIONS];
	u32 sectsize[SLIC_FIRMWARE_MAX_SECTIONS];
	const struct firmware *fw;
	unsigned int datalen;
	const char *file;
	int code_start;
	unsigned int i;
	u32 numsects;
	int idx = 0;
	u32 sect;
	u32 instr;
	u32 addr;
	u32 base;
	int err;

	file = (sdev->model == SLIC_MODEL_OASIS) ?  SLIC_FIRMWARE_OASIS :
						    SLIC_FIRMWARE_MOJAVE;
	err = request_firmware(&fw, file, &sdev->pdev->dev);
	if (err) {
		dev_err(&sdev->pdev->dev, "failed to load firmware %s\n", file);
		return err;
	}
	/* Do an initial sanity check concerning firmware size now. A further
	 * check follows below.
	 */
	if (fw->size < SLIC_FIRMWARE_MIN_SIZE) {
		dev_err(&sdev->pdev->dev,
			"invalid firmware size %zu (min is %u)\n", fw->size,
			SLIC_FIRMWARE_MIN_SIZE);
		err = -EINVAL;
		goto release;
	}

	numsects = slic_read_dword_from_firmware(fw, &idx);
	if (numsects == 0 || numsects > SLIC_FIRMWARE_MAX_SECTIONS) {
		dev_err(&sdev->pdev->dev,
			"invalid number of sections in firmware: %u", numsects);
		err = -EINVAL;
		goto release;
	}

	datalen = numsects * 8 + 4;
	for (i = 0; i < numsects; i++) {
		sectsize[i] = slic_read_dword_from_firmware(fw, &idx);
		datalen += sectsize[i];
	}

	/* do another sanity check against firmware size */
	if (datalen > fw->size) {
		dev_err(&sdev->pdev->dev,
			"invalid firmware size %zu (expected >= %u)\n",
			fw->size, datalen);
		err = -EINVAL;
		goto release;
	}
	/* get sections */
	for (i = 0; i < numsects; i++)
		sectstart[i] = slic_read_dword_from_firmware(fw, &idx);

	code_start = idx;
	instr = slic_read_dword_from_firmware(fw, &idx);

	for (sect = 0; sect < numsects; sect++) {
		unsigned int ssize = sectsize[sect] >> 3;

		base = sectstart[sect];

		for (addr = 0; addr < ssize; addr++) {
			/* write out instruction address */
			slic_write(sdev, SLIC_REG_WCS, base + addr);
			/* write out instruction to low addr */
			slic_write(sdev, SLIC_REG_WCS, instr);
			instr = slic_read_dword_from_firmware(fw, &idx);
			/* write out instruction to high addr */
			slic_write(sdev, SLIC_REG_WCS, instr);
			instr = slic_read_dword_from_firmware(fw, &idx);
		}
	}

	idx = code_start;

	for (sect = 0; sect < numsects; sect++) {
		unsigned int ssize = sectsize[sect] >> 3;

		instr = slic_read_dword_from_firmware(fw, &idx);
		base = sectstart[sect];
		if (base < 0x8000)
			continue;

		for (addr = 0; addr < ssize; addr++) {
			/* write out instruction address */
			slic_write(sdev, SLIC_REG_WCS,
				   SLIC_WCS_COMPARE | (base + addr));
			/* write out instruction to low addr */
			slic_write(sdev, SLIC_REG_WCS, instr);
			instr = slic_read_dword_from_firmware(fw, &idx);
			/* write out instruction to high addr */
			slic_write(sdev, SLIC_REG_WCS, instr);
			instr = slic_read_dword_from_firmware(fw, &idx);
		}
	}
	slic_flush_write(sdev);
	mdelay(10);
	/* everything OK, kick off the card */
	slic_write(sdev, SLIC_REG_WCS, SLIC_WCS_START);
	slic_flush_write(sdev);
	/* wait long enough for ucode to init card and reach the mainloop */
	mdelay(20);
release:
	release_firmware(fw);

	return err;
}

static int slic_init_shmem(struct slic_device *sdev)
{
	struct slic_shmem *sm = &sdev->shmem;
	struct slic_shmem_data *sm_data;
	dma_addr_t paddr;

	sm_data = dma_alloc_coherent(&sdev->pdev->dev, sizeof(*sm_data),
				     &paddr, GFP_KERNEL);
	if (!sm_data) {
		dev_err(&sdev->pdev->dev, "failed to allocate shared memory\n");
		return -ENOMEM;
	}

	sm->shmem_data = sm_data;
	sm->isr_paddr = paddr;
	sm->link_paddr = paddr + offsetof(struct slic_shmem_data, link);

	return 0;
}

static void slic_free_shmem(struct slic_device *sdev)
{
	struct slic_shmem *sm = &sdev->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;

	dma_free_coherent(&sdev->pdev->dev, sizeof(*sm_data), sm_data,
			  sm->isr_paddr);
}

static int slic_init_iface(struct slic_device *sdev)
{
	struct slic_shmem *sm = &sdev->shmem;
	int err;

	sdev->upr_list.pending = false;

	err = slic_init_shmem(sdev);
	if (err) {
		netdev_err(sdev->netdev, "failed to init shared memory\n");
		return err;
	}

	err = slic_load_firmware(sdev);
	if (err) {
		netdev_err(sdev->netdev, "failed to load firmware\n");
		goto free_sm;
	}

	err = slic_load_rcvseq_firmware(sdev);
	if (err) {
		netdev_err(sdev->netdev,
			   "failed to load firmware for receive sequencer\n");
		goto free_sm;
	}

	slic_write(sdev, SLIC_REG_ICR, SLIC_ICR_INT_OFF);
	slic_flush_write(sdev);
	mdelay(1);

	err = slic_init_rx_queue(sdev);
	if (err) {
		netdev_err(sdev->netdev, "failed to init rx queue: %u\n", err);
		goto free_sm;
	}

	err = slic_init_tx_queue(sdev);
	if (err) {
		netdev_err(sdev->netdev, "failed to init tx queue: %u\n", err);
		goto free_rxq;
	}

	err = slic_init_stat_queue(sdev);
	if (err) {
		netdev_err(sdev->netdev, "failed to init status queue: %u\n",
			   err);
		goto free_txq;
	}

	slic_write(sdev, SLIC_REG_ISP, lower_32_bits(sm->isr_paddr));
	napi_enable(&sdev->napi);
	/* disable irq mitigation */
	slic_write(sdev, SLIC_REG_INTAGG, 0);
	slic_write(sdev, SLIC_REG_ISR, 0);
	slic_flush_write(sdev);

	slic_set_mac_address(sdev);

	spin_lock_bh(&sdev->link_lock);
	sdev->duplex = DUPLEX_UNKNOWN;
	sdev->speed = SPEED_UNKNOWN;
	spin_unlock_bh(&sdev->link_lock);

	slic_set_link_autoneg(sdev);

	err = request_irq(sdev->pdev->irq, slic_irq, IRQF_SHARED, DRV_NAME,
			  sdev);
	if (err) {
		netdev_err(sdev->netdev, "failed to request irq: %u\n", err);
		goto disable_napi;
	}

	slic_write(sdev, SLIC_REG_ICR, SLIC_ICR_INT_ON);
	slic_flush_write(sdev);
	/* request initial link status */
	err = slic_handle_link_change(sdev);
	if (err)
		netdev_warn(sdev->netdev,
			    "failed to set initial link state: %u\n", err);
	return 0;

disable_napi:
	napi_disable(&sdev->napi);
	slic_free_stat_queue(sdev);
free_txq:
	slic_free_tx_queue(sdev);
free_rxq:
	slic_free_rx_queue(sdev);
free_sm:
	slic_free_shmem(sdev);
	slic_card_reset(sdev);

	return err;
}

static int slic_open(struct net_device *dev)
{
	struct slic_device *sdev = netdev_priv(dev);
	int err;

	netif_carrier_off(dev);

	err = slic_init_iface(sdev);
	if (err) {
		netdev_err(dev, "failed to initialize interface: %i\n", err);
		return err;
	}

	netif_start_queue(dev);

	return 0;
}

static int slic_close(struct net_device *dev)
{
	struct slic_device *sdev = netdev_priv(dev);
	u32 val;

	netif_stop_queue(dev);

	/* stop irq handling */
	napi_disable(&sdev->napi);
	slic_write(sdev, SLIC_REG_ICR, SLIC_ICR_INT_OFF);
	slic_write(sdev, SLIC_REG_ISR, 0);
	slic_flush_write(sdev);

	free_irq(sdev->pdev->irq, sdev);
	/* turn off RCV and XMT and power down PHY */
	val = SLIC_GXCR_RESET | SLIC_GXCR_PAUSEEN;
	slic_write(sdev, SLIC_REG_WXCFG, val);

	val = SLIC_GRCR_RESET | SLIC_GRCR_CTLEN | SLIC_GRCR_ADDRAEN |
	      SLIC_GRCR_HASHSIZE << SLIC_GRCR_HASHSIZE_SHIFT;
	slic_write(sdev, SLIC_REG_WRCFG, val);

	val = MII_BMCR << 16 | BMCR_PDOWN;
	slic_write(sdev, SLIC_REG_WPHY, val);
	slic_flush_write(sdev);

	slic_clear_upr_list(&sdev->upr_list);
	slic_write(sdev, SLIC_REG_QUIESCE, 0);

	slic_free_stat_queue(sdev);
	slic_free_tx_queue(sdev);
	slic_free_rx_queue(sdev);
	slic_free_shmem(sdev);

	slic_card_reset(sdev);
	netif_carrier_off(dev);

	return 0;
}

static netdev_tx_t slic_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct slic_device *sdev = netdev_priv(dev);
	struct slic_tx_queue *txq = &sdev->txq;
	struct slic_tx_buffer *buff;
	struct slic_tx_desc *desc;
	dma_addr_t paddr;
	u32 cbar_val;
	u32 maplen;

	if (unlikely(slic_get_free_tx_descs(txq) < SLIC_MAX_REQ_TX_DESCS)) {
		netdev_err(dev, "BUG! not enough tx LEs left: %u\n",
			   slic_get_free_tx_descs(txq));
		return NETDEV_TX_BUSY;
	}

	maplen = skb_headlen(skb);
	paddr = dma_map_single(&sdev->pdev->dev, skb->data, maplen,
			       DMA_TO_DEVICE);
	if (dma_mapping_error(&sdev->pdev->dev, paddr)) {
		netdev_err(dev, "failed to map tx buffer\n");
		goto drop_skb;
	}

	buff = &txq->txbuffs[txq->put_idx];
	buff->skb = skb;
	dma_unmap_addr_set(buff, map_addr, paddr);
	dma_unmap_len_set(buff, map_len, maplen);

	desc = buff->desc;
	desc->totlen = cpu_to_le32(maplen);
	desc->paddrl = cpu_to_le32(lower_32_bits(paddr));
	desc->paddrh = cpu_to_le32(upper_32_bits(paddr));
	desc->len = cpu_to_le32(maplen);

	txq->put_idx = slic_next_queue_idx(txq->put_idx, txq->len);

	cbar_val = lower_32_bits(buff->desc_paddr) | 1;
	/* complete writes to RAM and DMA before hardware is informed */
	wmb();

	slic_write(sdev, SLIC_REG_CBAR, cbar_val);

	if (slic_get_free_tx_descs(txq) < SLIC_MAX_REQ_TX_DESCS)
		netif_stop_queue(dev);

	return NETDEV_TX_OK;
drop_skb:
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static void slic_get_stats(struct net_device *dev,
			   struct rtnl_link_stats64 *lst)
{
	struct slic_device *sdev = netdev_priv(dev);
	struct slic_stats *stats = &sdev->stats;

	SLIC_GET_STATS_COUNTER(lst->rx_packets, stats, rx_packets);
	SLIC_GET_STATS_COUNTER(lst->tx_packets, stats, tx_packets);
	SLIC_GET_STATS_COUNTER(lst->rx_bytes, stats, rx_bytes);
	SLIC_GET_STATS_COUNTER(lst->tx_bytes, stats, tx_bytes);
	SLIC_GET_STATS_COUNTER(lst->rx_errors, stats, rx_errors);
	SLIC_GET_STATS_COUNTER(lst->rx_dropped, stats, rx_buff_miss);
	SLIC_GET_STATS_COUNTER(lst->tx_dropped, stats, tx_dropped);
	SLIC_GET_STATS_COUNTER(lst->multicast, stats, rx_mcasts);
	SLIC_GET_STATS_COUNTER(lst->rx_over_errors, stats, rx_buffoflow);
	SLIC_GET_STATS_COUNTER(lst->rx_crc_errors, stats, rx_crc);
	SLIC_GET_STATS_COUNTER(lst->rx_fifo_errors, stats, rx_oflow802);
	SLIC_GET_STATS_COUNTER(lst->tx_carrier_errors, stats, tx_carrier);
}

static int slic_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(slic_stats_strings);
	default:
		return -EOPNOTSUPP;
	}
}

static void slic_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *eth_stats, u64 *data)
{
	struct slic_device *sdev = netdev_priv(dev);
	struct slic_stats *stats = &sdev->stats;

	SLIC_GET_STATS_COUNTER(data[0], stats, rx_packets);
	SLIC_GET_STATS_COUNTER(data[1], stats, rx_bytes);
	SLIC_GET_STATS_COUNTER(data[2], stats, rx_mcasts);
	SLIC_GET_STATS_COUNTER(data[3], stats, rx_errors);
	SLIC_GET_STATS_COUNTER(data[4], stats, rx_buff_miss);
	SLIC_GET_STATS_COUNTER(data[5], stats, rx_tpcsum);
	SLIC_GET_STATS_COUNTER(data[6], stats, rx_tpoflow);
	SLIC_GET_STATS_COUNTER(data[7], stats, rx_tphlen);
	SLIC_GET_STATS_COUNTER(data[8], stats, rx_ipcsum);
	SLIC_GET_STATS_COUNTER(data[9], stats, rx_iplen);
	SLIC_GET_STATS_COUNTER(data[10], stats, rx_iphlen);
	SLIC_GET_STATS_COUNTER(data[11], stats, rx_early);
	SLIC_GET_STATS_COUNTER(data[12], stats, rx_buffoflow);
	SLIC_GET_STATS_COUNTER(data[13], stats, rx_lcode);
	SLIC_GET_STATS_COUNTER(data[14], stats, rx_drbl);
	SLIC_GET_STATS_COUNTER(data[15], stats, rx_crc);
	SLIC_GET_STATS_COUNTER(data[16], stats, rx_oflow802);
	SLIC_GET_STATS_COUNTER(data[17], stats, rx_uflow802);
	SLIC_GET_STATS_COUNTER(data[18], stats, tx_packets);
	SLIC_GET_STATS_COUNTER(data[19], stats, tx_bytes);
	SLIC_GET_STATS_COUNTER(data[20], stats, tx_carrier);
	SLIC_GET_STATS_COUNTER(data[21], stats, tx_dropped);
	SLIC_GET_STATS_COUNTER(data[22], stats, irq_errs);
}

static void slic_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS) {
		memcpy(data, slic_stats_strings, sizeof(slic_stats_strings));
		data += sizeof(slic_stats_strings);
	}
}

static void slic_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct slic_device *sdev = netdev_priv(dev);

	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(sdev->pdev), sizeof(info->bus_info));
}

static const struct ethtool_ops slic_ethtool_ops = {
	.get_drvinfo		= slic_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_strings		= slic_get_strings,
	.get_ethtool_stats	= slic_get_ethtool_stats,
	.get_sset_count		= slic_get_sset_count,
};

static const struct net_device_ops slic_netdev_ops = {
	.ndo_open		= slic_open,
	.ndo_stop		= slic_close,
	.ndo_start_xmit		= slic_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_get_stats64	= slic_get_stats,
	.ndo_set_rx_mode	= slic_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
};

static u16 slic_eeprom_csum(unsigned char *eeprom, unsigned int len)
{
	unsigned char *ptr = eeprom;
	u32 csum = 0;
	__le16 data;

	while (len > 1) {
		memcpy(&data, ptr, sizeof(data));
		csum += le16_to_cpu(data);
		ptr += 2;
		len -= 2;
	}
	if (len > 0)
		csum += *(u8 *)ptr;
	while (csum >> 16)
		csum = (csum & 0xFFFF) + ((csum >> 16) & 0xFFFF);
	return ~csum;
}

/* check eeprom size, magic and checksum */
static bool slic_eeprom_valid(unsigned char *eeprom, unsigned int size)
{
	const unsigned int MAX_SIZE = 128;
	const unsigned int MIN_SIZE = 98;
	__le16 magic;
	__le16 csum;

	if (size < MIN_SIZE || size > MAX_SIZE)
		return false;
	memcpy(&magic, eeprom, sizeof(magic));
	if (le16_to_cpu(magic) != SLIC_EEPROM_MAGIC)
		return false;
	/* cut checksum bytes */
	size -= 2;
	memcpy(&csum, eeprom + size, sizeof(csum));

	return (le16_to_cpu(csum) == slic_eeprom_csum(eeprom, size));
}

static int slic_read_eeprom(struct slic_device *sdev)
{
	unsigned int devfn = PCI_FUNC(sdev->pdev->devfn);
	struct slic_shmem *sm = &sdev->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;
	const unsigned int MAX_LOOPS = 5000;
	unsigned int codesize;
	unsigned char *eeprom;
	struct slic_upr *upr;
	unsigned int i = 0;
	dma_addr_t paddr;
	int err = 0;
	u8 *mac[2];

	eeprom = dma_alloc_coherent(&sdev->pdev->dev, SLIC_EEPROM_SIZE,
				    &paddr, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	slic_write(sdev, SLIC_REG_ICR, SLIC_ICR_INT_OFF);
	/* setup ISP temporarily */
	slic_write(sdev, SLIC_REG_ISP, lower_32_bits(sm->isr_paddr));

	err = slic_new_upr(sdev, SLIC_UPR_CONFIG, paddr);
	if (!err) {
		for (i = 0; i < MAX_LOOPS; i++) {
			if (le32_to_cpu(sm_data->isr) & SLIC_ISR_UPC)
				break;
			mdelay(1);
		}
		if (i == MAX_LOOPS) {
			dev_err(&sdev->pdev->dev,
				"timed out while waiting for eeprom data\n");
			err = -ETIMEDOUT;
		}
		upr = slic_dequeue_upr(sdev);
		kfree(upr);
	}

	slic_write(sdev, SLIC_REG_ISP, 0);
	slic_write(sdev, SLIC_REG_ISR, 0);
	slic_flush_write(sdev);

	if (err)
		goto free_eeprom;

	if (sdev->model == SLIC_MODEL_OASIS) {
		struct slic_oasis_eeprom *oee;

		oee = (struct slic_oasis_eeprom *)eeprom;
		mac[0] = oee->mac;
		mac[1] = oee->mac2;
		codesize = le16_to_cpu(oee->eeprom_code_size);
	} else {
		struct slic_mojave_eeprom *mee;

		mee = (struct slic_mojave_eeprom *)eeprom;
		mac[0] = mee->mac;
		mac[1] = mee->mac2;
		codesize = le16_to_cpu(mee->eeprom_code_size);
	}

	if (!slic_eeprom_valid(eeprom, codesize)) {
		dev_err(&sdev->pdev->dev, "invalid checksum in eeprom\n");
		err = -EINVAL;
		goto free_eeprom;
	}
	/* set mac address */
	eth_hw_addr_set(sdev->netdev, mac[devfn]);
free_eeprom:
	dma_free_coherent(&sdev->pdev->dev, SLIC_EEPROM_SIZE, eeprom, paddr);

	return err;
}

static int slic_init(struct slic_device *sdev)
{
	int err;

	spin_lock_init(&sdev->upper_lock);
	spin_lock_init(&sdev->link_lock);
	INIT_LIST_HEAD(&sdev->upr_list.list);
	spin_lock_init(&sdev->upr_list.lock);
	u64_stats_init(&sdev->stats.syncp);

	slic_card_reset(sdev);

	err = slic_load_firmware(sdev);
	if (err) {
		dev_err(&sdev->pdev->dev, "failed to load firmware\n");
		return err;
	}

	/* we need the shared memory to read EEPROM so set it up temporarily */
	err = slic_init_shmem(sdev);
	if (err) {
		dev_err(&sdev->pdev->dev, "failed to init shared memory\n");
		return err;
	}

	err = slic_read_eeprom(sdev);
	if (err) {
		dev_err(&sdev->pdev->dev, "failed to read eeprom\n");
		goto free_sm;
	}

	slic_card_reset(sdev);
	slic_free_shmem(sdev);

	return 0;
free_sm:
	slic_free_shmem(sdev);

	return err;
}

static bool slic_is_fiber(unsigned short subdev)
{
	switch (subdev) {
	/* Mojave */
	case PCI_SUBDEVICE_ID_ALACRITECH_1000X1F:
	case PCI_SUBDEVICE_ID_ALACRITECH_SES1001F: fallthrough;
	/* Oasis */
	case PCI_SUBDEVICE_ID_ALACRITECH_SEN2002XF:
	case PCI_SUBDEVICE_ID_ALACRITECH_SEN2001XF:
	case PCI_SUBDEVICE_ID_ALACRITECH_SEN2104EF:
	case PCI_SUBDEVICE_ID_ALACRITECH_SEN2102EF:
		return true;
	}
	return false;
}

static void slic_configure_pci(struct pci_dev *pdev)
{
	u16 old;
	u16 cmd;

	pci_read_config_word(pdev, PCI_COMMAND, &old);

	cmd = old | PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	if (old != cmd)
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
}

static int slic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct slic_device *sdev;
	struct net_device *dev;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable PCI device\n");
		return err;
	}

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	slic_configure_pci(pdev);

	err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "failed to setup DMA\n");
		goto disable;
	}

	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "failed to obtain PCI regions\n");
		goto disable;
	}

	dev = alloc_etherdev(sizeof(*sdev));
	if (!dev) {
		dev_err(&pdev->dev, "failed to alloc ethernet device\n");
		err = -ENOMEM;
		goto free_regions;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);
	dev->irq = pdev->irq;
	dev->netdev_ops = &slic_netdev_ops;
	dev->hw_features = NETIF_F_RXCSUM;
	dev->features |= dev->hw_features;

	dev->ethtool_ops = &slic_ethtool_ops;

	sdev = netdev_priv(dev);
	sdev->model = (pdev->device == PCI_DEVICE_ID_ALACRITECH_OASIS) ?
		      SLIC_MODEL_OASIS : SLIC_MODEL_MOJAVE;
	sdev->is_fiber = slic_is_fiber(pdev->subsystem_device);
	sdev->pdev = pdev;
	sdev->netdev = dev;
	sdev->regs = ioremap(pci_resource_start(pdev, 0),
				     pci_resource_len(pdev, 0));
	if (!sdev->regs) {
		dev_err(&pdev->dev, "failed to map registers\n");
		err = -ENOMEM;
		goto free_netdev;
	}

	err = slic_init(sdev);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize driver\n");
		goto unmap;
	}

	netif_napi_add(dev, &sdev->napi, slic_poll);
	netif_carrier_off(dev);

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "failed to register net device: %i\n", err);
		goto unmap;
	}

	return 0;

unmap:
	iounmap(sdev->regs);
free_netdev:
	free_netdev(dev);
free_regions:
	pci_release_regions(pdev);
disable:
	pci_disable_device(pdev);

	return err;
}

static void slic_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct slic_device *sdev = netdev_priv(dev);

	unregister_netdev(dev);
	iounmap(sdev->regs);
	free_netdev(dev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver slic_driver = {
	.name = DRV_NAME,
	.id_table = slic_id_tbl,
	.probe = slic_probe,
	.remove = slic_remove,
};

module_pci_driver(slic_driver);

MODULE_DESCRIPTION("Alacritech non-accelerated SLIC driver");
MODULE_AUTHOR("Lino Sanfilippo <LinoSanfilippo@gmx.de>");
MODULE_LICENSE("GPL");
