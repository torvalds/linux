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

/* Returns the TSN specific registers to their default values after
 * TSN offloading is disabled.
 */
static int igc_tsn_disable_offload(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 tqavctrl;
	int i;

	if (!(adapter->flags & IGC_FLAG_TSN_QBV_ENABLED))
		return 0;

	adapter->cycle_time = 0;

	wr32(IGC_TXPBS, I225_TXPBSIZE_DEFAULT);
	wr32(IGC_DTXMXPKTSZ, IGC_DTXMXPKTSZ_DEFAULT);

	tqavctrl = rd32(IGC_TQAVCTRL);
	tqavctrl &= ~(IGC_TQAVCTRL_TRANSMIT_MODE_TSN |
		      IGC_TQAVCTRL_ENHANCED_QAV);
	wr32(IGC_TQAVCTRL, tqavctrl);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *ring = adapter->tx_ring[i];

		ring->start_time = 0;
		ring->end_time = 0;
		ring->launchtime_enable = false;

		wr32(IGC_TXQCTL(i), 0);
		wr32(IGC_STQT(i), 0);
		wr32(IGC_ENDQT(i), NSEC_PER_SEC);
	}

	wr32(IGC_QBVCYCLET_S, NSEC_PER_SEC);
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

	if (adapter->flags & IGC_FLAG_TSN_QBV_ENABLED)
		return 0;

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

		wr32(IGC_STQT(i), ring->start_time);
		wr32(IGC_ENDQT(i), ring->end_time);

		txqctl |= IGC_TXQCTL_STRICT_CYCLE |
			IGC_TXQCTL_STRICT_END;

		if (ring->launchtime_enable)
			txqctl |= IGC_TXQCTL_QUEUE_MODE_LAUNCHT;

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

	adapter->flags |= IGC_FLAG_TSN_QBV_ENABLED;

	return 0;
}

int igc_tsn_offload_apply(struct igc_adapter *adapter)
{
	bool is_any_enabled = adapter->base_time || is_any_launchtime(adapter);

	if (!(adapter->flags & IGC_FLAG_TSN_QBV_ENABLED) && !is_any_enabled)
		return 0;

	if (!is_any_enabled) {
		int err = igc_tsn_disable_offload(adapter);

		if (err < 0)
			return err;

		/* The BASET registers aren't cleared when writing
		 * into them, force a reset if the interface is
		 * running.
		 */
		if (netif_running(adapter->netdev))
			schedule_work(&adapter->reset_task);

		return 0;
	}

	return igc_tsn_enable_offload(adapter);
}
