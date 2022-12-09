// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2019 Intel Corporation */

#include "igc.h"
#include "igc_tsn.h"

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

	if (adapter->base_time)
		new_flags |= IGC_FLAG_TSN_QBV_ENABLED;

	if (is_any_launchtime(adapter))
		new_flags |= IGC_FLAG_TSN_QBV_ENABLED;

	if (is_cbs_enabled(adapter))
		new_flags |= IGC_FLAG_TSN_QAV_ENABLED;

	return new_flags;
}

/* Returns the TSN specific registers to their default values after
 * the adapter is reset.
 */
static int igc_tsn_disable_offload(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 tqavctrl;
	int i;

	wr32(IGC_TXPBS, I225_TXPBSIZE_DEFAULT);
	wr32(IGC_DTXMXPKTSZ, IGC_DTXMXPKTSZ_DEFAULT);

	tqavctrl = rd32(IGC_TQAVCTRL);
	tqavctrl &= ~(IGC_TQAVCTRL_TRANSMIT_MODE_TSN |
		      IGC_TQAVCTRL_ENHANCED_QAV);
	wr32(IGC_TQAVCTRL, tqavctrl);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		wr32(IGC_TXQCTL(i), 0);
		wr32(IGC_STQT(i), 0);
		wr32(IGC_ENDQT(i), NSEC_PER_SEC);
	}

	wr32(IGC_QBVCYCLET_S, 0);
	wr32(IGC_QBVCYCLET, NSEC_PER_SEC);

	adapter->flags &= ~IGC_FLAG_TSN_QBV_ENABLED;

	return 0;
}

static int igc_tsn_enable_offload(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 tqavctrl, baset_l, baset_h;
	u32 sec, nsec, cycle;
	ktime_t base_time, systim;
	int i;

	cycle = adapter->cycle_time;
	base_time = adapter->base_time;

	wr32(IGC_TSAUXC, 0);
	wr32(IGC_DTXMXPKTSZ, IGC_DTXMXPKTSZ_TSN);
	wr32(IGC_TXPBS, IGC_TXPBSIZE_TSN);

	tqavctrl = rd32(IGC_TQAVCTRL);
	tqavctrl |= IGC_TQAVCTRL_TRANSMIT_MODE_TSN | IGC_TQAVCTRL_ENHANCED_QAV;
	wr32(IGC_TQAVCTRL, tqavctrl);

	wr32(IGC_QBVCYCLET_S, cycle);
	wr32(IGC_QBVCYCLET, cycle);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *ring = adapter->tx_ring[i];
		u32 txqctl = 0;
		u16 cbs_value;
		u32 tqavcc;

		wr32(IGC_STQT(i), ring->start_time);
		wr32(IGC_ENDQT(i), ring->end_time);

		txqctl |= IGC_TXQCTL_STRICT_CYCLE |
			IGC_TXQCTL_STRICT_END;

		if (ring->launchtime_enable)
			txqctl |= IGC_TXQCTL_QUEUE_MODE_LAUNCHT;

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
			     0x80000000 + ring->hicredit * 0x7735);
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

	nsec = rd32(IGC_SYSTIML);
	sec = rd32(IGC_SYSTIMH);

	systim = ktime_set(sec, nsec);

	if (ktime_compare(systim, base_time) > 0) {
		s64 n;

		n = div64_s64(ktime_sub_ns(systim, base_time), cycle);
		base_time = ktime_add_ns(base_time, (n + 1) * cycle);
	}

	baset_h = div_s64_rem(base_time, NSEC_PER_SEC, &baset_l);

	wr32(IGC_BASET_H, baset_h);
	wr32(IGC_BASET_L, baset_l);

	return 0;
}

int igc_tsn_reset(struct igc_adapter *adapter)
{
	unsigned int new_flags;
	int err = 0;

	new_flags = igc_tsn_new_flags(adapter);

	if (!(new_flags & IGC_FLAG_TSN_ANY_ENABLED))
		return igc_tsn_disable_offload(adapter);

	err = igc_tsn_enable_offload(adapter);
	if (err < 0)
		return err;

	adapter->flags = new_flags;

	return err;
}

int igc_tsn_offload_apply(struct igc_adapter *adapter)
{
	int err;

	if (netif_running(adapter->netdev)) {
		schedule_work(&adapter->reset_task);
		return 0;
	}

	err = igc_tsn_enable_offload(adapter);
	if (err < 0)
		return err;

	adapter->flags = igc_tsn_new_flags(adapter);
	return 0;
}
