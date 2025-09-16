// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2019 Intel Corporation */

#include "igc.h"
#include "igc_base.h"
#include "igc_hw.h"
#include "igc_tsn.h"

#define MIN_MULTPLIER_TX_MIN_FRAG	0
#define MAX_MULTPLIER_TX_MIN_FRAG	3
/* Frag size is based on the Section 8.12.2 of the SW User Manual */
#define TX_MIN_FRAG_SIZE		64
#define TX_MAX_FRAG_SIZE	(TX_MIN_FRAG_SIZE * \
				 (MAX_MULTPLIER_TX_MIN_FRAG + 1))

enum tx_queue {
	TX_QUEUE_0 = 0,
	TX_QUEUE_1,
	TX_QUEUE_2,
	TX_QUEUE_3,
};

DEFINE_STATIC_KEY_FALSE(igc_fpe_enabled);

static int igc_fpe_init_smd_frame(struct igc_ring *ring,
				  struct igc_tx_buffer *buffer,
				  struct sk_buff *skb)
{
	dma_addr_t dma = dma_map_single(ring->dev, skb->data, skb->len,
					DMA_TO_DEVICE);

	if (dma_mapping_error(ring->dev, dma)) {
		netdev_err_once(ring->netdev, "Failed to map DMA for TX\n");
		return -ENOMEM;
	}

	buffer->skb = skb;
	buffer->protocol = 0;
	buffer->bytecount = skb->len;
	buffer->gso_segs = 1;
	buffer->time_stamp = jiffies;
	dma_unmap_len_set(buffer, len, skb->len);
	dma_unmap_addr_set(buffer, dma, dma);

	return 0;
}

static int igc_fpe_init_tx_descriptor(struct igc_ring *ring,
				      struct sk_buff *skb,
				      enum igc_txd_popts_type type)
{
	u32 cmd_type, olinfo_status = 0;
	struct igc_tx_buffer *buffer;
	union igc_adv_tx_desc *desc;
	int err;

	if (!igc_desc_unused(ring))
		return -EBUSY;

	buffer = &ring->tx_buffer_info[ring->next_to_use];
	err = igc_fpe_init_smd_frame(ring, buffer, skb);
	if (err)
		return err;

	cmd_type = IGC_ADVTXD_DTYP_DATA | IGC_ADVTXD_DCMD_DEXT |
		   IGC_ADVTXD_DCMD_IFCS | IGC_TXD_DCMD |
		   buffer->bytecount;

	olinfo_status |= FIELD_PREP(IGC_ADVTXD_PAYLEN_MASK, buffer->bytecount);

	switch (type) {
	case SMD_V:
	case SMD_R:
		olinfo_status |= FIELD_PREP(IGC_TXD_POPTS_SMD_MASK, type);
		break;
	}

	desc = IGC_TX_DESC(ring, ring->next_to_use);
	desc->read.cmd_type_len = cpu_to_le32(cmd_type);
	desc->read.olinfo_status = cpu_to_le32(olinfo_status);
	desc->read.buffer_addr = cpu_to_le64(dma_unmap_addr(buffer, dma));

	netdev_tx_sent_queue(txring_txq(ring), skb->len);

	buffer->next_to_watch = desc;
	ring->next_to_use = (ring->next_to_use + 1) % ring->count;

	return 0;
}

static int igc_fpe_xmit_smd_frame(struct igc_adapter *adapter,
				  enum igc_txd_popts_type type)
{
	int cpu = smp_processor_id();
	struct netdev_queue *nq;
	struct igc_ring *ring;
	struct sk_buff *skb;
	int err;

	ring = igc_get_tx_ring(adapter, cpu);
	nq = txring_txq(ring);

	skb = alloc_skb(SMD_FRAME_SIZE, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_zero(skb, SMD_FRAME_SIZE);

	__netif_tx_lock(nq, cpu);

	err = igc_fpe_init_tx_descriptor(ring, skb, type);
	igc_flush_tx_descriptors(ring);

	__netif_tx_unlock(nq);

	return err;
}

static void igc_fpe_configure_tx(struct ethtool_mmsv *mmsv, bool tx_enable)
{
	struct igc_fpe_t *fpe = container_of(mmsv, struct igc_fpe_t, mmsv);
	struct igc_adapter *adapter;

	adapter = container_of(fpe, struct igc_adapter, fpe);
	adapter->fpe.tx_enabled = tx_enable;

	/* Update config since tx_enabled affects preemptible queue configuration */
	igc_tsn_offload_apply(adapter);
}

static void igc_fpe_send_mpacket(struct ethtool_mmsv *mmsv,
				 enum ethtool_mpacket type)
{
	struct igc_fpe_t *fpe = container_of(mmsv, struct igc_fpe_t, mmsv);
	struct igc_adapter *adapter;
	int err;

	adapter = container_of(fpe, struct igc_adapter, fpe);

	if (type == ETHTOOL_MPACKET_VERIFY) {
		err = igc_fpe_xmit_smd_frame(adapter, SMD_V);
		if (err && net_ratelimit())
			netdev_err(adapter->netdev, "Error sending SMD-V\n");
	} else if (type == ETHTOOL_MPACKET_RESPONSE) {
		err = igc_fpe_xmit_smd_frame(adapter, SMD_R);
		if (err && net_ratelimit())
			netdev_err(adapter->netdev, "Error sending SMD-R frame\n");
	}
}

static const struct ethtool_mmsv_ops igc_mmsv_ops = {
	.configure_tx = igc_fpe_configure_tx,
	.send_mpacket = igc_fpe_send_mpacket,
};

void igc_fpe_init(struct igc_adapter *adapter)
{
	adapter->fpe.tx_min_frag_size = TX_MIN_FRAG_SIZE;
	adapter->fpe.tx_enabled = false;
	ethtool_mmsv_init(&adapter->fpe.mmsv, adapter->netdev, &igc_mmsv_ops);
}

void igc_fpe_clear_preempt_queue(struct igc_adapter *adapter)
{
	for (int i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *tx_ring = adapter->tx_ring[i];

		tx_ring->preemptible = false;
	}
}

static u32 igc_fpe_map_preempt_tc_to_queue(const struct igc_adapter *adapter,
					   unsigned long preemptible_tcs)
{
	struct net_device *dev = adapter->netdev;
	u32 i, queue = 0;

	for (i = 0; i < dev->num_tc; i++) {
		u32 offset, count;

		if (!(preemptible_tcs & BIT(i)))
			continue;

		offset = dev->tc_to_txq[i].offset;
		count = dev->tc_to_txq[i].count;
		queue |= GENMASK(offset + count - 1, offset);
	}

	return queue;
}

void igc_fpe_save_preempt_queue(struct igc_adapter *adapter,
				const struct tc_mqprio_qopt_offload *mqprio)
{
	u32 preemptible_queue = igc_fpe_map_preempt_tc_to_queue(adapter,
								mqprio->preemptible_tcs);

	for (int i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *tx_ring = adapter->tx_ring[i];

		tx_ring->preemptible = !!(preemptible_queue & BIT(i));
	}
}

static bool is_any_launchtime(struct igc_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *ring = adapter->tx_ring[i];

		if (ring->launchtime_enable)
			return true;
	}

	return false;
}

static bool is_cbs_enabled(struct igc_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *ring = adapter->tx_ring[i];

		if (ring->cbs_enable)
			return true;
	}

	return false;
}

static unsigned int igc_tsn_new_flags(struct igc_adapter *adapter)
{
	unsigned int new_flags = adapter->flags & ~IGC_FLAG_TSN_ANY_ENABLED;


	if (adapter->taprio_offload_enable || is_any_launchtime(adapter) ||
	    adapter->strict_priority_enable)
		new_flags |= IGC_FLAG_TSN_QBV_ENABLED;

	if (is_cbs_enabled(adapter))
		new_flags |= IGC_FLAG_TSN_QAV_ENABLED;

	if (adapter->fpe.mmsv.pmac_enabled)
		new_flags |= IGC_FLAG_TSN_PREEMPT_ENABLED;

	return new_flags;
}

static bool igc_tsn_is_tx_mode_in_tsn(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	return !!(rd32(IGC_TQAVCTRL) & IGC_TQAVCTRL_TRANSMIT_MODE_TSN);
}

void igc_tsn_adjust_txtime_offset(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u16 txoffset;

	if (!igc_tsn_is_tx_mode_in_tsn(adapter))
		return;

	switch (adapter->link_speed) {
	case SPEED_10:
		txoffset = IGC_TXOFFSET_SPEED_10;
		break;
	case SPEED_100:
		txoffset = IGC_TXOFFSET_SPEED_100;
		break;
	case SPEED_1000:
		txoffset = IGC_TXOFFSET_SPEED_1000;
		break;
	case SPEED_2500:
		txoffset = IGC_TXOFFSET_SPEED_2500;
		break;
	default:
		txoffset = 0;
		break;
	}

	wr32(IGC_GTXOFFSET, txoffset);
}

static void igc_tsn_restore_retx_default(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 retxctl;

	retxctl = rd32(IGC_RETX_CTL) & IGC_RETX_CTL_WATERMARK_MASK;
	wr32(IGC_RETX_CTL, retxctl);
}

bool igc_tsn_is_taprio_activated_by_user(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	return (rd32(IGC_BASET_H) || rd32(IGC_BASET_L)) &&
		adapter->taprio_offload_enable;
}

static void igc_tsn_tx_arb(struct igc_adapter *adapter, bool reverse_prio)
{
	struct igc_hw *hw = &adapter->hw;
	u32 txarb;

	txarb = rd32(IGC_TXARB);

	txarb &= ~(IGC_TXARB_TXQ_PRIO_0_MASK |
		   IGC_TXARB_TXQ_PRIO_1_MASK |
		   IGC_TXARB_TXQ_PRIO_2_MASK |
		   IGC_TXARB_TXQ_PRIO_3_MASK);

	if (reverse_prio) {
		txarb |= IGC_TXARB_TXQ_PRIO_0(TX_QUEUE_3);
		txarb |= IGC_TXARB_TXQ_PRIO_1(TX_QUEUE_2);
		txarb |= IGC_TXARB_TXQ_PRIO_2(TX_QUEUE_1);
		txarb |= IGC_TXARB_TXQ_PRIO_3(TX_QUEUE_0);
	} else {
		txarb |= IGC_TXARB_TXQ_PRIO_0(TX_QUEUE_0);
		txarb |= IGC_TXARB_TXQ_PRIO_1(TX_QUEUE_1);
		txarb |= IGC_TXARB_TXQ_PRIO_2(TX_QUEUE_2);
		txarb |= IGC_TXARB_TXQ_PRIO_3(TX_QUEUE_3);
	}

	wr32(IGC_TXARB, txarb);
}

/**
 * igc_tsn_set_rxpbsize - Set the receive packet buffer size
 * @adapter: Pointer to the igc_adapter structure
 * @rxpbs_exp_bmc_be: Value to set the receive packet buffer size, including
 *                    express buffer, BMC buffer, and Best Effort buffer
 *
 * The IGC_RXPBS register value may include allocations for the Express buffer,
 * BMC buffer, Best Effort buffer, and the timestamp descriptor buffer
 * (IGC_RXPBS_CFG_TS_EN).
 */
static void igc_tsn_set_rxpbsize(struct igc_adapter *adapter,
				 u32 rxpbs_exp_bmc_be)
{
	struct igc_hw *hw = &adapter->hw;
	u32 rxpbs = rd32(IGC_RXPBS);

	rxpbs &= ~(IGC_RXPBSIZE_EXP_MASK | IGC_BMC2OSPBSIZE_MASK |
		   IGC_RXPBSIZE_BE_MASK);
	rxpbs |= rxpbs_exp_bmc_be;

	wr32(IGC_RXPBS, rxpbs);
}

/* Returns the TSN specific registers to their default values after
 * the adapter is reset.
 */
static int igc_tsn_disable_offload(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 tqavctrl;
	int i;

	wr32(IGC_GTXOFFSET, 0);
	wr32(IGC_TXPBS, IGC_TXPBSIZE_DEFAULT);
	wr32(IGC_DTXMXPKTSZ, IGC_DTXMXPKTSZ_DEFAULT);

	igc_tsn_set_rxpbsize(adapter, IGC_RXPBSIZE_EXP_BMC_DEFAULT);

	if (igc_is_device_id_i226(hw))
		igc_tsn_restore_retx_default(adapter);

	tqavctrl = rd32(IGC_TQAVCTRL);
	tqavctrl &= ~(IGC_TQAVCTRL_TRANSMIT_MODE_TSN |
		      IGC_TQAVCTRL_ENHANCED_QAV | IGC_TQAVCTRL_FUTSCDDIS |
		      IGC_TQAVCTRL_PREEMPT_ENA | IGC_TQAVCTRL_MIN_FRAG_MASK);

	wr32(IGC_TQAVCTRL, tqavctrl);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		int reg_idx = adapter->tx_ring[i]->reg_idx;
		u32 txdctl;

		wr32(IGC_TXQCTL(i), 0);
		wr32(IGC_STQT(i), 0);
		wr32(IGC_ENDQT(i), NSEC_PER_SEC);

		txdctl = rd32(IGC_TXDCTL(reg_idx));
		txdctl &= ~IGC_TXDCTL_PRIORITY_HIGH;
		wr32(IGC_TXDCTL(reg_idx), txdctl);
	}

	wr32(IGC_QBVCYCLET_S, 0);
	wr32(IGC_QBVCYCLET, NSEC_PER_SEC);

	/* Restore the default Tx arbitration: Priority 0 has the highest
	 * priority and is assigned to queue 0 and so on and so forth.
	 */
	igc_tsn_tx_arb(adapter, false);

	adapter->flags &= ~IGC_FLAG_TSN_QBV_ENABLED;

	return 0;
}

/* To partially fix i226 HW errata, reduce MAC internal buffering from 192 Bytes
 * to 88 Bytes by setting RETX_CTL register using the recommendation from:
 * a) Ethernet Controller I225/I226 Specification Update Rev 2.1
 *    Item 9: TSN: Packet Transmission Might Cross the Qbv Window
 * b) I225/6 SW User Manual Rev 1.2.4: Section 8.11.5 Retry Buffer Control
 */
static void igc_tsn_set_retx_qbvfullthreshold(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 retxctl, watermark;

	retxctl = rd32(IGC_RETX_CTL);
	watermark = retxctl & IGC_RETX_CTL_WATERMARK_MASK;
	/* Set QBVFULLTH value using watermark and set QBVFULLEN */
	retxctl |= (watermark << IGC_RETX_CTL_QBVFULLTH_SHIFT) |
		   IGC_RETX_CTL_QBVFULLEN;
	wr32(IGC_RETX_CTL, retxctl);
}

static u8 igc_fpe_get_frag_size_mult(const struct igc_fpe_t *fpe)
{
	u8 mult = (fpe->tx_min_frag_size / TX_MIN_FRAG_SIZE) - 1;

	return clamp_t(u8, mult, MIN_MULTPLIER_TX_MIN_FRAG,
		       MAX_MULTPLIER_TX_MIN_FRAG);
}

u32 igc_fpe_get_supported_frag_size(u32 frag_size)
{
	static const u32 supported_sizes[] = { 64, 128, 192, 256 };

	/* Find the smallest supported size that is >= frag_size */
	for (int i = 0; i < ARRAY_SIZE(supported_sizes); i++) {
		if (frag_size <= supported_sizes[i])
			return supported_sizes[i];
	}

	/* Should not happen */
	return TX_MAX_FRAG_SIZE;
}

static int igc_tsn_enable_offload(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 tqavctrl, baset_l, baset_h;
	u32 sec, nsec, cycle;
	ktime_t base_time, systim;
	u32 frag_size_mult;
	int i;

	wr32(IGC_TSAUXC, 0);
	wr32(IGC_DTXMXPKTSZ, IGC_DTXMXPKTSZ_TSN);
	wr32(IGC_TXPBS, IGC_TXPBSIZE_TSN);

	igc_tsn_set_rxpbsize(adapter, IGC_RXPBSIZE_EXP_BMC_BE_TSN);

	if (igc_is_device_id_i226(hw))
		igc_tsn_set_retx_qbvfullthreshold(adapter);

	if (adapter->strict_priority_enable ||
	    adapter->flags & IGC_FLAG_TSN_REVERSE_TXQ_PRIO)
		igc_tsn_tx_arb(adapter, true);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *ring = adapter->tx_ring[i];
		u32 txdctl = rd32(IGC_TXDCTL(ring->reg_idx));
		u32 txqctl = 0;
		u16 cbs_value;
		u32 tqavcc;

		wr32(IGC_STQT(i), ring->start_time);
		wr32(IGC_ENDQT(i), ring->end_time);

		if (adapter->taprio_offload_enable) {
			/* If taprio_offload_enable is set we are in "taprio"
			 * mode and we need to be strict about the
			 * cycles: only transmit a packet if it can be
			 * completed during that cycle.
			 *
			 * If taprio_offload_enable is NOT true when
			 * enabling TSN offload, the cycle should have
			 * no external effects, but is only used internally
			 * to adapt the base time register after a second
			 * has passed.
			 *
			 * Enabling strict mode in this case would
			 * unnecessarily prevent the transmission of
			 * certain packets (i.e. at the boundary of a
			 * second) and thus interfere with the launchtime
			 * feature that promises transmission at a
			 * certain point in time.
			 */
			txqctl |= IGC_TXQCTL_STRICT_CYCLE |
				IGC_TXQCTL_STRICT_END;
		}

		if (ring->launchtime_enable)
			txqctl |= IGC_TXQCTL_QUEUE_MODE_LAUNCHT;

		if (!adapter->fpe.tx_enabled) {
			/* fpe inactive: clear both flags */
			txqctl &= ~IGC_TXQCTL_PREEMPTIBLE;
			txdctl &= ~IGC_TXDCTL_PRIORITY_HIGH;
		} else if (ring->preemptible) {
			/* fpe active + preemptible: enable preemptible queue + set low priority */
			txqctl |= IGC_TXQCTL_PREEMPTIBLE;
			txdctl &= ~IGC_TXDCTL_PRIORITY_HIGH;
		} else {
			/* fpe active + express: enable express queue + set high priority */
			txqctl &= ~IGC_TXQCTL_PREEMPTIBLE;
			txdctl |= IGC_TXDCTL_PRIORITY_HIGH;
		}

		wr32(IGC_TXDCTL(ring->reg_idx), txdctl);

		/* Skip configuring CBS for Q2 and Q3 */
		if (i > 1)
			goto skip_cbs;

		if (ring->cbs_enable) {
			if (i == 0)
				txqctl |= IGC_TXQCTL_QAV_SEL_CBS0;
			else
				txqctl |= IGC_TXQCTL_QAV_SEL_CBS1;

			/* According to i225 datasheet section 7.5.2.7, we
			 * should set the 'idleSlope' field from TQAVCC
			 * register following the equation:
			 *
			 * value = link-speed   0x7736 * BW * 0.2
			 *         ---------- *  -----------------         (E1)
			 *          100Mbps            2.5
			 *
			 * Note that 'link-speed' is in Mbps.
			 *
			 * 'BW' is the percentage bandwidth out of full
			 * link speed which can be found with the
			 * following equation. Note that idleSlope here
			 * is the parameter from this function
			 * which is in kbps.
			 *
			 *     BW =     idleSlope
			 *          -----------------                      (E2)
			 *          link-speed * 1000
			 *
			 * That said, we can come up with a generic
			 * equation to calculate the value we should set
			 * it TQAVCC register by replacing 'BW' in E1 by E2.
			 * The resulting equation is:
			 *
			 * value = link-speed * 0x7736 * idleSlope * 0.2
			 *         -------------------------------------   (E3)
			 *             100 * 2.5 * link-speed * 1000
			 *
			 * 'link-speed' is present in both sides of the
			 * fraction so it is canceled out. The final
			 * equation is the following:
			 *
			 *     value = idleSlope * 61036
			 *             -----------------                   (E4)
			 *                  2500000
			 *
			 * NOTE: For i225, given the above, we can see
			 *       that idleslope is represented in
			 *       40.959433 kbps units by the value at
			 *       the TQAVCC register (2.5Gbps / 61036),
			 *       which reduces the granularity for
			 *       idleslope increments.
			 *
			 * In i225 controller, the sendSlope and loCredit
			 * parameters from CBS are not configurable
			 * by software so we don't do any
			 * 'controller configuration' in respect to
			 * these parameters.
			 */
			cbs_value = DIV_ROUND_UP_ULL(ring->idleslope
						     * 61036ULL, 2500000);

			tqavcc = rd32(IGC_TQAVCC(i));
			tqavcc &= ~IGC_TQAVCC_IDLESLOPE_MASK;
			tqavcc |= cbs_value | IGC_TQAVCC_KEEP_CREDITS;
			wr32(IGC_TQAVCC(i), tqavcc);

			wr32(IGC_TQAVHC(i),
			     0x80000000 + ring->hicredit * 0x7736);
		} else {
			/* Disable any CBS for the queue */
			txqctl &= ~(IGC_TXQCTL_QAV_SEL_MASK);

			/* Set idleSlope to zero. */
			tqavcc = rd32(IGC_TQAVCC(i));
			tqavcc &= ~(IGC_TQAVCC_IDLESLOPE_MASK |
				    IGC_TQAVCC_KEEP_CREDITS);
			wr32(IGC_TQAVCC(i), tqavcc);

			/* Set hiCredit to zero. */
			wr32(IGC_TQAVHC(i), 0);
		}
skip_cbs:
		wr32(IGC_TXQCTL(i), txqctl);
	}

	tqavctrl = rd32(IGC_TQAVCTRL) & ~(IGC_TQAVCTRL_FUTSCDDIS |
		   IGC_TQAVCTRL_PREEMPT_ENA | IGC_TQAVCTRL_MIN_FRAG_MASK);
	tqavctrl |= IGC_TQAVCTRL_TRANSMIT_MODE_TSN | IGC_TQAVCTRL_ENHANCED_QAV;

	if (adapter->fpe.mmsv.pmac_enabled)
		tqavctrl |= IGC_TQAVCTRL_PREEMPT_ENA;

	frag_size_mult = igc_fpe_get_frag_size_mult(&adapter->fpe);
	tqavctrl |= FIELD_PREP(IGC_TQAVCTRL_MIN_FRAG_MASK, frag_size_mult);

	adapter->qbv_count++;

	cycle = adapter->cycle_time;
	base_time = adapter->base_time;

	nsec = rd32(IGC_SYSTIML);
	sec = rd32(IGC_SYSTIMH);

	systim = ktime_set(sec, nsec);
	if (ktime_compare(systim, base_time) > 0) {
		s64 n = div64_s64(ktime_sub_ns(systim, base_time), cycle);

		base_time = ktime_add_ns(base_time, (n + 1) * cycle);
	} else {
		if (igc_is_device_id_i226(hw)) {
			ktime_t adjust_time, expires_time;

		       /* According to datasheet section 7.5.2.9.3.3, FutScdDis bit
			* has to be configured before the cycle time and base time.
			* Tx won't hang if a GCL is already running,
			* so in this case we don't need to set FutScdDis.
			*/
			if (!(rd32(IGC_BASET_H) || rd32(IGC_BASET_L)))
				tqavctrl |= IGC_TQAVCTRL_FUTSCDDIS;

			nsec = rd32(IGC_SYSTIML);
			sec = rd32(IGC_SYSTIMH);
			systim = ktime_set(sec, nsec);

			adjust_time = adapter->base_time;
			expires_time = ktime_sub_ns(adjust_time, systim);
			hrtimer_start(&adapter->hrtimer, expires_time, HRTIMER_MODE_REL);
		}
	}

	wr32(IGC_TQAVCTRL, tqavctrl);

	wr32(IGC_QBVCYCLET_S, cycle);
	wr32(IGC_QBVCYCLET, cycle);

	baset_h = div_s64_rem(base_time, NSEC_PER_SEC, &baset_l);
	wr32(IGC_BASET_H, baset_h);

	/* In i226, Future base time is only supported when FutScdDis bit
	 * is enabled and only active for re-configuration.
	 * In this case, initialize the base time with zero to create
	 * "re-configuration" scenario then only set the desired base time.
	 */
	if (tqavctrl & IGC_TQAVCTRL_FUTSCDDIS)
		wr32(IGC_BASET_L, 0);
	wr32(IGC_BASET_L, baset_l);

	return 0;
}

int igc_tsn_reset(struct igc_adapter *adapter)
{
	unsigned int new_flags;
	int err = 0;

	if (adapter->fpe.mmsv.pmac_enabled) {
		err = igc_enable_empty_addr_recv(adapter);
		if (err && net_ratelimit())
			netdev_err(adapter->netdev, "Error adding empty address to MAC filter\n");
	} else {
		igc_disable_empty_addr_recv(adapter);
	}

	new_flags = igc_tsn_new_flags(adapter);

	if (!(new_flags & IGC_FLAG_TSN_ANY_ENABLED))
		return igc_tsn_disable_offload(adapter);

	err = igc_tsn_enable_offload(adapter);
	if (err < 0)
		return err;

	adapter->flags = new_flags;

	return err;
}

static bool igc_tsn_will_tx_mode_change(struct igc_adapter *adapter)
{
	bool any_tsn_enabled = !!(igc_tsn_new_flags(adapter) &
				  IGC_FLAG_TSN_ANY_ENABLED);

	return (any_tsn_enabled && !igc_tsn_is_tx_mode_in_tsn(adapter)) ||
	       (!any_tsn_enabled && igc_tsn_is_tx_mode_in_tsn(adapter));
}

int igc_tsn_offload_apply(struct igc_adapter *adapter)
{
	/* Per I225/6 HW Design Section 7.5.2.1 guideline, if tx mode change
	 * from legacy->tsn or tsn->legacy, then reset adapter is needed.
	 */
	if (netif_running(adapter->netdev) &&
	    igc_tsn_will_tx_mode_change(adapter)) {
		schedule_work(&adapter->reset_task);
		return 0;
	}

	igc_tsn_reset(adapter);

	return 0;
}
