// SPDX-License-Identifier: GPL-2.0-only
/* Aquantia Corporation Network Driver
 * Copyright (C) 2014-2019 Aquantia Corporation. All rights reserved
 */

/* File aq_ptp.c:
 * Definition of functions for Linux PTP support.
 */

#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_classify.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>

#include "aq_nic.h"
#include "aq_ptp.h"
#include "aq_ring.h"
#include "aq_filters.h"

#define AQ_PTP_TX_TIMEOUT        (HZ *  10)

enum ptp_speed_offsets {
	ptp_offset_idx_10 = 0,
	ptp_offset_idx_100,
	ptp_offset_idx_1000,
	ptp_offset_idx_2500,
	ptp_offset_idx_5000,
	ptp_offset_idx_10000,
};

struct ptp_skb_ring {
	struct sk_buff **buff;
	spinlock_t lock;
	unsigned int size;
	unsigned int head;
	unsigned int tail;
};

struct ptp_tx_timeout {
	spinlock_t lock;
	bool active;
	unsigned long tx_start;
};

struct aq_ptp_s {
	struct aq_nic_s *aq_nic;
	struct hwtstamp_config hwtstamp_config;
	spinlock_t ptp_lock;
	spinlock_t ptp_ring_lock;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_info;

	atomic_t offset_egress;
	atomic_t offset_ingress;

	struct aq_ring_param_s ptp_ring_param;

	struct ptp_tx_timeout ptp_tx_timeout;

	unsigned int idx_vector;
	struct napi_struct napi;

	struct aq_ring_s ptp_tx;
	struct aq_ring_s ptp_rx;
	struct aq_ring_s hwts_rx;

	struct ptp_skb_ring skb_ring;

	struct aq_rx_filter_l3l4 udp_filter;
	struct aq_rx_filter_l2 eth_type_filter;
};

struct ptp_tm_offset {
	unsigned int mbps;
	int egress;
	int ingress;
};

static struct ptp_tm_offset ptp_offset[6];

void aq_ptp_tm_offset_set(struct aq_nic_s *aq_nic, unsigned int mbps)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	int i, egress, ingress;

	if (!aq_ptp)
		return;

	egress = 0;
	ingress = 0;

	for (i = 0; i < ARRAY_SIZE(ptp_offset); i++) {
		if (mbps == ptp_offset[i].mbps) {
			egress = ptp_offset[i].egress;
			ingress = ptp_offset[i].ingress;
			break;
		}
	}

	atomic_set(&aq_ptp->offset_egress, egress);
	atomic_set(&aq_ptp->offset_ingress, ingress);
}

static int __aq_ptp_skb_put(struct ptp_skb_ring *ring, struct sk_buff *skb)
{
	unsigned int next_head = (ring->head + 1) % ring->size;

	if (next_head == ring->tail)
		return -ENOMEM;

	ring->buff[ring->head] = skb_get(skb);
	ring->head = next_head;

	return 0;
}

static int aq_ptp_skb_put(struct ptp_skb_ring *ring, struct sk_buff *skb)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ring->lock, flags);
	ret = __aq_ptp_skb_put(ring, skb);
	spin_unlock_irqrestore(&ring->lock, flags);

	return ret;
}

static struct sk_buff *__aq_ptp_skb_get(struct ptp_skb_ring *ring)
{
	struct sk_buff *skb;

	if (ring->tail == ring->head)
		return NULL;

	skb = ring->buff[ring->tail];
	ring->tail = (ring->tail + 1) % ring->size;

	return skb;
}

static struct sk_buff *aq_ptp_skb_get(struct ptp_skb_ring *ring)
{
	unsigned long flags;
	struct sk_buff *skb;

	spin_lock_irqsave(&ring->lock, flags);
	skb = __aq_ptp_skb_get(ring);
	spin_unlock_irqrestore(&ring->lock, flags);

	return skb;
}

static unsigned int aq_ptp_skb_buf_len(struct ptp_skb_ring *ring)
{
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&ring->lock, flags);
	len = (ring->head >= ring->tail) ?
	ring->head - ring->tail :
	ring->size - ring->tail + ring->head;
	spin_unlock_irqrestore(&ring->lock, flags);

	return len;
}

static int aq_ptp_skb_ring_init(struct ptp_skb_ring *ring, unsigned int size)
{
	struct sk_buff **buff = kmalloc(sizeof(*buff) * size, GFP_KERNEL);

	if (!buff)
		return -ENOMEM;

	spin_lock_init(&ring->lock);

	ring->buff = buff;
	ring->size = size;
	ring->head = 0;
	ring->tail = 0;

	return 0;
}

static void aq_ptp_skb_ring_clean(struct ptp_skb_ring *ring)
{
	struct sk_buff *skb;

	while ((skb = aq_ptp_skb_get(ring)) != NULL)
		dev_kfree_skb_any(skb);
}

static void aq_ptp_skb_ring_release(struct ptp_skb_ring *ring)
{
	if (ring->buff) {
		aq_ptp_skb_ring_clean(ring);
		kfree(ring->buff);
		ring->buff = NULL;
	}
}

static void aq_ptp_tx_timeout_init(struct ptp_tx_timeout *timeout)
{
	spin_lock_init(&timeout->lock);
	timeout->active = false;
}

static void aq_ptp_tx_timeout_start(struct aq_ptp_s *aq_ptp)
{
	struct ptp_tx_timeout *timeout = &aq_ptp->ptp_tx_timeout;
	unsigned long flags;

	spin_lock_irqsave(&timeout->lock, flags);
	timeout->active = true;
	timeout->tx_start = jiffies;
	spin_unlock_irqrestore(&timeout->lock, flags);
}

static void aq_ptp_tx_timeout_update(struct aq_ptp_s *aq_ptp)
{
	if (!aq_ptp_skb_buf_len(&aq_ptp->skb_ring)) {
		struct ptp_tx_timeout *timeout = &aq_ptp->ptp_tx_timeout;
		unsigned long flags;

		spin_lock_irqsave(&timeout->lock, flags);
		timeout->active = false;
		spin_unlock_irqrestore(&timeout->lock, flags);
	}
}

static void aq_ptp_tx_timeout_check(struct aq_ptp_s *aq_ptp)
{
	struct ptp_tx_timeout *timeout = &aq_ptp->ptp_tx_timeout;
	unsigned long flags;
	bool timeout_flag;

	timeout_flag = false;

	spin_lock_irqsave(&timeout->lock, flags);
	if (timeout->active) {
		timeout_flag = time_is_before_jiffies(timeout->tx_start +
						      AQ_PTP_TX_TIMEOUT);
		/* reset active flag if timeout detected */
		if (timeout_flag)
			timeout->active = false;
	}
	spin_unlock_irqrestore(&timeout->lock, flags);

	if (timeout_flag) {
		aq_ptp_skb_ring_clean(&aq_ptp->skb_ring);
		netdev_err(aq_ptp->aq_nic->ndev,
			   "PTP Timeout. Clearing Tx Timestamp SKBs\n");
	}
}

/* aq_ptp_adjfine
 * @ptp: the ptp clock structure
 * @ppb: parts per billion adjustment from base
 *
 * adjust the frequency of the ptp cycle counter by the
 * indicated ppb from the base frequency.
 */
static int aq_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;

	mutex_lock(&aq_nic->fwreq_mutex);
	aq_nic->aq_hw_ops->hw_adj_clock_freq(aq_nic->aq_hw,
					     scaled_ppm_to_ppb(scaled_ppm));
	mutex_unlock(&aq_nic->fwreq_mutex);

	return 0;
}

/* aq_ptp_adjtime
 * @ptp: the ptp clock structure
 * @delta: offset to adjust the cycle counter by
 *
 * adjust the timer by resetting the timecounter structure.
 */
static int aq_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	unsigned long flags;

	spin_lock_irqsave(&aq_ptp->ptp_lock, flags);
	aq_nic->aq_hw_ops->hw_adj_sys_clock(aq_nic->aq_hw, delta);
	spin_unlock_irqrestore(&aq_ptp->ptp_lock, flags);

	return 0;
}

/* aq_ptp_gettime
 * @ptp: the ptp clock structure
 * @ts: timespec structure to hold the current time value
 *
 * read the timecounter and return the correct value on ns,
 * after converting it into a struct timespec.
 */
static int aq_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&aq_ptp->ptp_lock, flags);
	aq_nic->aq_hw_ops->hw_get_ptp_ts(aq_nic->aq_hw, &ns);
	spin_unlock_irqrestore(&aq_ptp->ptp_lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

/* aq_ptp_settime
 * @ptp: the ptp clock structure
 * @ts: the timespec containing the new time for the cycle counter
 *
 * reset the timecounter to use a new base value instead of the kernel
 * wall timer value.
 */
static int aq_ptp_settime(struct ptp_clock_info *ptp,
			  const struct timespec64 *ts)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	unsigned long flags;
	u64 ns = timespec64_to_ns(ts);
	u64 now;

	spin_lock_irqsave(&aq_ptp->ptp_lock, flags);
	aq_nic->aq_hw_ops->hw_get_ptp_ts(aq_nic->aq_hw, &now);
	aq_nic->aq_hw_ops->hw_adj_sys_clock(aq_nic->aq_hw, (s64)ns - (s64)now);

	spin_unlock_irqrestore(&aq_ptp->ptp_lock, flags);

	return 0;
}

static void aq_ptp_convert_to_hwtstamp(struct aq_ptp_s *aq_ptp,
				       struct skb_shared_hwtstamps *hwtstamp,
				       u64 timestamp)
{
	memset(hwtstamp, 0, sizeof(*hwtstamp));
	hwtstamp->hwtstamp = ns_to_ktime(timestamp);
}

/* aq_ptp_tx_hwtstamp - utility function which checks for TX time stamp
 * @adapter: the private adapter struct
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the hwtstamps structure which
 * is passed up the network stack
 */
void aq_ptp_tx_hwtstamp(struct aq_nic_s *aq_nic, u64 timestamp)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	struct sk_buff *skb = aq_ptp_skb_get(&aq_ptp->skb_ring);
	struct skb_shared_hwtstamps hwtstamp;

	if (!skb) {
		netdev_err(aq_nic->ndev, "have timestamp but tx_queus empty\n");
		return;
	}

	timestamp += atomic_read(&aq_ptp->offset_egress);
	aq_ptp_convert_to_hwtstamp(aq_ptp, &hwtstamp, timestamp);
	skb_tstamp_tx(skb, &hwtstamp);
	dev_kfree_skb_any(skb);

	aq_ptp_tx_timeout_update(aq_ptp);
}

/* aq_ptp_rx_hwtstamp - utility function which checks for RX time stamp
 * @adapter: pointer to adapter struct
 * @skb: particular skb to send timestamp with
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the hwtstamps structure which
 * is passed up the network stack
 */
static void aq_ptp_rx_hwtstamp(struct aq_ptp_s *aq_ptp, struct sk_buff *skb,
			       u64 timestamp)
{
	timestamp -= atomic_read(&aq_ptp->offset_ingress);
	aq_ptp_convert_to_hwtstamp(aq_ptp, skb_hwtstamps(skb), timestamp);
}

void aq_ptp_hwtstamp_config_get(struct aq_ptp_s *aq_ptp,
				struct hwtstamp_config *config)
{
	*config = aq_ptp->hwtstamp_config;
}

static void aq_ptp_prepare_filters(struct aq_ptp_s *aq_ptp)
{
	aq_ptp->udp_filter.cmd = HW_ATL_RX_ENABLE_FLTR_L3L4 |
			       HW_ATL_RX_ENABLE_CMP_PROT_L4 |
			       HW_ATL_RX_UDP |
			       HW_ATL_RX_ENABLE_CMP_DEST_PORT_L4 |
			       HW_ATL_RX_HOST << HW_ATL_RX_ACTION_FL3F4_SHIFT |
			       HW_ATL_RX_ENABLE_QUEUE_L3L4 |
			       aq_ptp->ptp_rx.idx << HW_ATL_RX_QUEUE_FL3L4_SHIFT;
	aq_ptp->udp_filter.p_dst = PTP_EV_PORT;

	aq_ptp->eth_type_filter.ethertype = ETH_P_1588;
	aq_ptp->eth_type_filter.queue = aq_ptp->ptp_rx.idx;
}

int aq_ptp_hwtstamp_config_set(struct aq_ptp_s *aq_ptp,
			       struct hwtstamp_config *config)
{
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	const struct aq_hw_ops *hw_ops;
	int err = 0;

	hw_ops = aq_nic->aq_hw_ops;
	if (config->tx_type == HWTSTAMP_TX_ON ||
	    config->rx_filter == HWTSTAMP_FILTER_PTP_V2_EVENT) {
		aq_ptp_prepare_filters(aq_ptp);
		if (hw_ops->hw_filter_l3l4_set) {
			err = hw_ops->hw_filter_l3l4_set(aq_nic->aq_hw,
							 &aq_ptp->udp_filter);
		}
		if (!err && hw_ops->hw_filter_l2_set) {
			err = hw_ops->hw_filter_l2_set(aq_nic->aq_hw,
						       &aq_ptp->eth_type_filter);
		}
		aq_utils_obj_set(&aq_nic->flags, AQ_NIC_PTP_DPATH_UP);
	} else {
		aq_ptp->udp_filter.cmd &= ~HW_ATL_RX_ENABLE_FLTR_L3L4;
		if (hw_ops->hw_filter_l3l4_set) {
			err = hw_ops->hw_filter_l3l4_set(aq_nic->aq_hw,
							 &aq_ptp->udp_filter);
		}
		if (!err && hw_ops->hw_filter_l2_clear) {
			err = hw_ops->hw_filter_l2_clear(aq_nic->aq_hw,
							&aq_ptp->eth_type_filter);
		}
		aq_utils_obj_clear(&aq_nic->flags, AQ_NIC_PTP_DPATH_UP);
	}

	if (err)
		return -EREMOTEIO;

	aq_ptp->hwtstamp_config = *config;

	return 0;
}

bool aq_ptp_ring(struct aq_nic_s *aq_nic, struct aq_ring_s *ring)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return false;

	return &aq_ptp->ptp_tx == ring ||
	       &aq_ptp->ptp_rx == ring || &aq_ptp->hwts_rx == ring;
}

u16 aq_ptp_extract_ts(struct aq_nic_s *aq_nic, struct sk_buff *skb, u8 *p,
		      unsigned int len)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	u64 timestamp = 0;
	u16 ret = aq_nic->aq_hw_ops->rx_extract_ts(aq_nic->aq_hw,
						   p, len, &timestamp);

	if (ret > 0)
		aq_ptp_rx_hwtstamp(aq_ptp, skb, timestamp);

	return ret;
}

static int aq_ptp_poll(struct napi_struct *napi, int budget)
{
	struct aq_ptp_s *aq_ptp = container_of(napi, struct aq_ptp_s, napi);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	bool was_cleaned = false;
	int work_done = 0;
	int err;

	/* Processing PTP TX traffic */
	err = aq_nic->aq_hw_ops->hw_ring_tx_head_update(aq_nic->aq_hw,
							&aq_ptp->ptp_tx);
	if (err < 0)
		goto err_exit;

	if (aq_ptp->ptp_tx.sw_head != aq_ptp->ptp_tx.hw_head) {
		aq_ring_tx_clean(&aq_ptp->ptp_tx);

		was_cleaned = true;
	}

	/* Processing HW_TIMESTAMP RX traffic */
	err = aq_nic->aq_hw_ops->hw_ring_hwts_rx_receive(aq_nic->aq_hw,
							 &aq_ptp->hwts_rx);
	if (err < 0)
		goto err_exit;

	if (aq_ptp->hwts_rx.sw_head != aq_ptp->hwts_rx.hw_head) {
		aq_ring_hwts_rx_clean(&aq_ptp->hwts_rx, aq_nic);

		err = aq_nic->aq_hw_ops->hw_ring_hwts_rx_fill(aq_nic->aq_hw,
							      &aq_ptp->hwts_rx);

		was_cleaned = true;
	}

	/* Processing PTP RX traffic */
	err = aq_nic->aq_hw_ops->hw_ring_rx_receive(aq_nic->aq_hw,
						    &aq_ptp->ptp_rx);
	if (err < 0)
		goto err_exit;

	if (aq_ptp->ptp_rx.sw_head != aq_ptp->ptp_rx.hw_head) {
		unsigned int sw_tail_old;

		err = aq_ring_rx_clean(&aq_ptp->ptp_rx, napi, &work_done, budget);
		if (err < 0)
			goto err_exit;

		sw_tail_old = aq_ptp->ptp_rx.sw_tail;
		err = aq_ring_rx_fill(&aq_ptp->ptp_rx);
		if (err < 0)
			goto err_exit;

		err = aq_nic->aq_hw_ops->hw_ring_rx_fill(aq_nic->aq_hw,
							 &aq_ptp->ptp_rx,
							 sw_tail_old);
		if (err < 0)
			goto err_exit;
	}

	if (was_cleaned)
		work_done = budget;

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		aq_nic->aq_hw_ops->hw_irq_enable(aq_nic->aq_hw,
					1 << aq_ptp->ptp_ring_param.vec_idx);
	}

err_exit:
	return work_done;
}

static irqreturn_t aq_ptp_isr(int irq, void *private)
{
	struct aq_ptp_s *aq_ptp = private;
	int err = 0;

	if (!aq_ptp) {
		err = -EINVAL;
		goto err_exit;
	}
	napi_schedule(&aq_ptp->napi);

err_exit:
	return err >= 0 ? IRQ_HANDLED : IRQ_NONE;
}

int aq_ptp_xmit(struct aq_nic_s *aq_nic, struct sk_buff *skb)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	struct aq_ring_s *ring = &aq_ptp->ptp_tx;
	unsigned long irq_flags;
	int err = NETDEV_TX_OK;
	unsigned int frags;

	if (skb->len <= 0) {
		dev_kfree_skb_any(skb);
		goto err_exit;
	}

	frags = skb_shinfo(skb)->nr_frags + 1;
	/* Frags cannot be bigger 16KB
	 * because PTP usually works
	 * without Jumbo even in a background
	 */
	if (frags > AQ_CFG_SKB_FRAGS_MAX || frags > aq_ring_avail_dx(ring)) {
		/* Drop packet because it doesn't make sence to delay it */
		dev_kfree_skb_any(skb);
		goto err_exit;
	}

	err = aq_ptp_skb_put(&aq_ptp->skb_ring, skb);
	if (err) {
		netdev_err(aq_nic->ndev, "SKB Ring is overflow (%u)!\n",
			   ring->size);
		return NETDEV_TX_BUSY;
	}
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	aq_ptp_tx_timeout_start(aq_ptp);
	skb_tx_timestamp(skb);

	spin_lock_irqsave(&aq_nic->aq_ptp->ptp_ring_lock, irq_flags);
	frags = aq_nic_map_skb(aq_nic, skb, ring);

	if (likely(frags)) {
		err = aq_nic->aq_hw_ops->hw_ring_tx_xmit(aq_nic->aq_hw,
						       ring, frags);
		if (err >= 0) {
			++ring->stats.tx.packets;
			ring->stats.tx.bytes += skb->len;
		}
	} else {
		err = NETDEV_TX_BUSY;
	}
	spin_unlock_irqrestore(&aq_nic->aq_ptp->ptp_ring_lock, irq_flags);

err_exit:
	return err;
}

void aq_ptp_service_task(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	aq_ptp_tx_timeout_check(aq_ptp);
}

int aq_ptp_irq_alloc(struct aq_nic_s *aq_nic)
{
	struct pci_dev *pdev = aq_nic->pdev;
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	int err = 0;

	if (!aq_ptp)
		return 0;

	if (pdev->msix_enabled || pdev->msi_enabled) {
		err = request_irq(pci_irq_vector(pdev, aq_ptp->idx_vector),
				  aq_ptp_isr, 0, aq_nic->ndev->name, aq_ptp);
	} else {
		err = -EINVAL;
		goto err_exit;
	}

err_exit:
	return err;
}

void aq_ptp_irq_free(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	struct pci_dev *pdev = aq_nic->pdev;

	if (!aq_ptp)
		return;

	free_irq(pci_irq_vector(pdev, aq_ptp->idx_vector), aq_ptp);
}

int aq_ptp_ring_init(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	int err = 0;

	if (!aq_ptp)
		return 0;

	err = aq_ring_init(&aq_ptp->ptp_tx);
	if (err < 0)
		goto err_exit;
	err = aq_nic->aq_hw_ops->hw_ring_tx_init(aq_nic->aq_hw,
						 &aq_ptp->ptp_tx,
						 &aq_ptp->ptp_ring_param);
	if (err < 0)
		goto err_exit;

	err = aq_ring_init(&aq_ptp->ptp_rx);
	if (err < 0)
		goto err_exit;
	err = aq_nic->aq_hw_ops->hw_ring_rx_init(aq_nic->aq_hw,
						 &aq_ptp->ptp_rx,
						 &aq_ptp->ptp_ring_param);
	if (err < 0)
		goto err_exit;

	err = aq_ring_rx_fill(&aq_ptp->ptp_rx);
	if (err < 0)
		goto err_rx_free;
	err = aq_nic->aq_hw_ops->hw_ring_rx_fill(aq_nic->aq_hw,
						 &aq_ptp->ptp_rx,
						 0U);
	if (err < 0)
		goto err_rx_free;

	err = aq_ring_init(&aq_ptp->hwts_rx);
	if (err < 0)
		goto err_rx_free;
	err = aq_nic->aq_hw_ops->hw_ring_rx_init(aq_nic->aq_hw,
						 &aq_ptp->hwts_rx,
						 &aq_ptp->ptp_ring_param);
	if (err < 0)
		goto err_exit;
	err = aq_nic->aq_hw_ops->hw_ring_hwts_rx_fill(aq_nic->aq_hw,
						      &aq_ptp->hwts_rx);
	if (err < 0)
		goto err_exit;

	return err;

err_rx_free:
	aq_ring_rx_deinit(&aq_ptp->ptp_rx);
err_exit:
	return err;
}

int aq_ptp_ring_start(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	int err = 0;

	if (!aq_ptp)
		return 0;

	err = aq_nic->aq_hw_ops->hw_ring_tx_start(aq_nic->aq_hw, &aq_ptp->ptp_tx);
	if (err < 0)
		goto err_exit;

	err = aq_nic->aq_hw_ops->hw_ring_rx_start(aq_nic->aq_hw, &aq_ptp->ptp_rx);
	if (err < 0)
		goto err_exit;

	err = aq_nic->aq_hw_ops->hw_ring_rx_start(aq_nic->aq_hw,
						  &aq_ptp->hwts_rx);
	if (err < 0)
		goto err_exit;

	napi_enable(&aq_ptp->napi);

err_exit:
	return err;
}

void aq_ptp_ring_stop(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	aq_nic->aq_hw_ops->hw_ring_tx_stop(aq_nic->aq_hw, &aq_ptp->ptp_tx);
	aq_nic->aq_hw_ops->hw_ring_rx_stop(aq_nic->aq_hw, &aq_ptp->ptp_rx);

	aq_nic->aq_hw_ops->hw_ring_rx_stop(aq_nic->aq_hw, &aq_ptp->hwts_rx);

	napi_disable(&aq_ptp->napi);
}

void aq_ptp_ring_deinit(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp || !aq_ptp->ptp_tx.aq_nic || !aq_ptp->ptp_rx.aq_nic)
		return;

	aq_ring_tx_clean(&aq_ptp->ptp_tx);
	aq_ring_rx_deinit(&aq_ptp->ptp_rx);
}

#define PTP_8TC_RING_IDX             8
#define PTP_4TC_RING_IDX            16
#define PTP_HWST_RING_IDX           31

int aq_ptp_ring_alloc(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	unsigned int tx_ring_idx, rx_ring_idx;
	struct aq_ring_s *hwts = 0;
	u32 tx_tc_mode, rx_tc_mode;
	struct aq_ring_s *ring;
	int err;

	if (!aq_ptp)
		return 0;

	/* Index must to be 8 (8 TCs) or 16 (4 TCs).
	 * It depends from Traffic Class mode.
	 */
	aq_nic->aq_hw_ops->hw_tx_tc_mode_get(aq_nic->aq_hw, &tx_tc_mode);
	if (tx_tc_mode == 0)
		tx_ring_idx = PTP_8TC_RING_IDX;
	else
		tx_ring_idx = PTP_4TC_RING_IDX;

	ring = aq_ring_tx_alloc(&aq_ptp->ptp_tx, aq_nic,
				tx_ring_idx, &aq_nic->aq_nic_cfg);
	if (!ring) {
		err = -ENOMEM;
		goto err_exit;
	}

	aq_nic->aq_hw_ops->hw_rx_tc_mode_get(aq_nic->aq_hw, &rx_tc_mode);
	if (rx_tc_mode == 0)
		rx_ring_idx = PTP_8TC_RING_IDX;
	else
		rx_ring_idx = PTP_4TC_RING_IDX;

	ring = aq_ring_rx_alloc(&aq_ptp->ptp_rx, aq_nic,
				rx_ring_idx, &aq_nic->aq_nic_cfg);
	if (!ring) {
		err = -ENOMEM;
		goto err_exit_ptp_tx;
	}

	hwts = aq_ring_hwts_rx_alloc(&aq_ptp->hwts_rx, aq_nic, PTP_HWST_RING_IDX,
				     aq_nic->aq_nic_cfg.rxds,
				     aq_nic->aq_nic_cfg.aq_hw_caps->rxd_size);
	if (!hwts) {
		err = -ENOMEM;
		goto err_exit_ptp_rx;
	}

	err = aq_ptp_skb_ring_init(&aq_ptp->skb_ring, aq_nic->aq_nic_cfg.rxds);
	if (err != 0) {
		err = -ENOMEM;
		goto err_exit_hwts_rx;
	}

	aq_ptp->ptp_ring_param.vec_idx = aq_ptp->idx_vector;
	aq_ptp->ptp_ring_param.cpu = aq_ptp->ptp_ring_param.vec_idx +
			aq_nic_get_cfg(aq_nic)->aq_rss.base_cpu_number;
	cpumask_set_cpu(aq_ptp->ptp_ring_param.cpu,
			&aq_ptp->ptp_ring_param.affinity_mask);

	return 0;

err_exit_hwts_rx:
	aq_ring_free(&aq_ptp->hwts_rx);
err_exit_ptp_rx:
	aq_ring_free(&aq_ptp->ptp_rx);
err_exit_ptp_tx:
	aq_ring_free(&aq_ptp->ptp_tx);
err_exit:
	return err;
}

void aq_ptp_ring_free(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	aq_ring_free(&aq_ptp->ptp_tx);
	aq_ring_free(&aq_ptp->ptp_rx);
	aq_ring_free(&aq_ptp->hwts_rx);

	aq_ptp_skb_ring_release(&aq_ptp->skb_ring);
}

static struct ptp_clock_info aq_ptp_clock = {
	.owner		= THIS_MODULE,
	.name		= "atlantic ptp",
	.max_adj	= 999999999,
	.n_ext_ts	= 0,
	.pps		= 0,
	.adjfine	= aq_ptp_adjfine,
	.adjtime	= aq_ptp_adjtime,
	.gettime64	= aq_ptp_gettime,
	.settime64	= aq_ptp_settime,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pin_config	= NULL,
};

#define ptp_offset_init(__idx, __mbps, __egress, __ingress)   do { \
		ptp_offset[__idx].mbps = (__mbps); \
		ptp_offset[__idx].egress = (__egress); \
		ptp_offset[__idx].ingress = (__ingress); } \
		while (0)

static void aq_ptp_offset_init_from_fw(const struct hw_aq_ptp_offset *offsets)
{
	int i;

	/* Load offsets for PTP */
	for (i = 0; i < ARRAY_SIZE(ptp_offset); i++) {
		switch (i) {
		/* 100M */
		case ptp_offset_idx_100:
			ptp_offset_init(i, 100,
					offsets->egress_100,
					offsets->ingress_100);
			break;
		/* 1G */
		case ptp_offset_idx_1000:
			ptp_offset_init(i, 1000,
					offsets->egress_1000,
					offsets->ingress_1000);
			break;
		/* 2.5G */
		case ptp_offset_idx_2500:
			ptp_offset_init(i, 2500,
					offsets->egress_2500,
					offsets->ingress_2500);
			break;
		/* 5G */
		case ptp_offset_idx_5000:
			ptp_offset_init(i, 5000,
					offsets->egress_5000,
					offsets->ingress_5000);
			break;
		/* 10G */
		case ptp_offset_idx_10000:
			ptp_offset_init(i, 10000,
					offsets->egress_10000,
					offsets->ingress_10000);
			break;
		}
	}
}

static void aq_ptp_offset_init(const struct hw_aq_ptp_offset *offsets)
{
	memset(ptp_offset, 0, sizeof(ptp_offset));

	aq_ptp_offset_init_from_fw(offsets);
}

void aq_ptp_clock_init(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	aq_ptp_settime(&aq_ptp->ptp_info, &ts);
}

int aq_ptp_init(struct aq_nic_s *aq_nic, unsigned int idx_vec)
{
	struct hw_atl_utils_mbox mbox;
	struct ptp_clock *clock;
	struct aq_ptp_s *aq_ptp;
	int err = 0;

	if (!aq_nic->aq_hw_ops->hw_get_ptp_ts) {
		aq_nic->aq_ptp = NULL;
		return 0;
	}

	if (!aq_nic->aq_fw_ops->enable_ptp) {
		aq_nic->aq_ptp = NULL;
		return 0;
	}

	hw_atl_utils_mpi_read_stats(aq_nic->aq_hw, &mbox);

	if (!(mbox.info.caps_ex & BIT(CAPS_EX_PHY_PTP_EN))) {
		aq_nic->aq_ptp = NULL;
		return 0;
	}

	aq_ptp_offset_init(&mbox.info.ptp_offset);

	aq_ptp = kzalloc(sizeof(*aq_ptp), GFP_KERNEL);
	if (!aq_ptp) {
		err = -ENOMEM;
		goto err_exit;
	}

	aq_ptp->aq_nic = aq_nic;

	spin_lock_init(&aq_ptp->ptp_lock);
	spin_lock_init(&aq_ptp->ptp_ring_lock);

	aq_ptp->ptp_info = aq_ptp_clock;
	clock = ptp_clock_register(&aq_ptp->ptp_info, &aq_nic->ndev->dev);
	if (!clock || IS_ERR(clock)) {
		netdev_err(aq_nic->ndev, "ptp_clock_register failed\n");
		err = PTR_ERR(clock);
		goto err_exit;
	}
	aq_ptp->ptp_clock = clock;
	aq_ptp_tx_timeout_init(&aq_ptp->ptp_tx_timeout);

	atomic_set(&aq_ptp->offset_egress, 0);
	atomic_set(&aq_ptp->offset_ingress, 0);

	netif_napi_add(aq_nic_get_ndev(aq_nic), &aq_ptp->napi,
		       aq_ptp_poll, AQ_CFG_NAPI_WEIGHT);

	aq_ptp->idx_vector = idx_vec;

	aq_nic->aq_ptp = aq_ptp;

	/* enable ptp counter */
	aq_utils_obj_set(&aq_nic->aq_hw->flags, AQ_HW_PTP_AVAILABLE);
	mutex_lock(&aq_nic->fwreq_mutex);
	aq_nic->aq_fw_ops->enable_ptp(aq_nic->aq_hw, 1);
	aq_ptp_clock_init(aq_nic);
	mutex_unlock(&aq_nic->fwreq_mutex);

	aq_ptp->eth_type_filter.location =
			aq_nic_reserve_filter(aq_nic, aq_rx_filter_ethertype);
	aq_ptp->udp_filter.location =
			aq_nic_reserve_filter(aq_nic, aq_rx_filter_l3l4);

	return 0;

err_exit:
	kfree(aq_ptp);
	aq_nic->aq_ptp = NULL;
	return err;
}

void aq_ptp_unregister(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	ptp_clock_unregister(aq_ptp->ptp_clock);
}

void aq_ptp_free(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	aq_nic_release_filter(aq_nic, aq_rx_filter_ethertype,
			      aq_ptp->eth_type_filter.location);
	aq_nic_release_filter(aq_nic, aq_rx_filter_l3l4,
			      aq_ptp->udp_filter.location);
	/* disable ptp */
	mutex_lock(&aq_nic->fwreq_mutex);
	aq_nic->aq_fw_ops->enable_ptp(aq_nic->aq_hw, 0);
	mutex_unlock(&aq_nic->fwreq_mutex);

	netif_napi_del(&aq_ptp->napi);
	kfree(aq_ptp);
	aq_nic->aq_ptp = NULL;
}

struct ptp_clock *aq_ptp_get_ptp_clock(struct aq_ptp_s *aq_ptp)
{
	return aq_ptp->ptp_clock;
}
