/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2020 Intel Corporation */

#ifndef _IGC_TSN_H_
#define _IGC_TSN_H_

#include <net/pkt_sched.h>

#define IGC_RX_MIN_FRAG_SIZE		60
#define SMD_FRAME_SIZE			60

enum igc_txd_popts_type {
	SMD_V = 0x01,
	SMD_R = 0x02,
};

DECLARE_STATIC_KEY_FALSE(igc_fpe_enabled);

void igc_fpe_init(struct igc_adapter *adapter);
void igc_fpe_clear_preempt_queue(struct igc_adapter *adapter);
void igc_fpe_save_preempt_queue(struct igc_adapter *adapter,
				const struct tc_mqprio_qopt_offload *mqprio);
u32 igc_fpe_get_supported_frag_size(u32 frag_size);
int igc_tsn_offload_apply(struct igc_adapter *adapter);
int igc_tsn_reset(struct igc_adapter *adapter);
void igc_tsn_adjust_txtime_offset(struct igc_adapter *adapter);
bool igc_tsn_is_taprio_activated_by_user(struct igc_adapter *adapter);

static inline bool igc_fpe_is_pmac_enabled(struct igc_adapter *adapter)
{
	return static_branch_unlikely(&igc_fpe_enabled) &&
	       adapter->fpe.mmsv.pmac_enabled;
}

static inline bool igc_fpe_handle_mpacket(struct igc_adapter *adapter,
					  union igc_adv_rx_desc *rx_desc,
					  unsigned int size, void *pktbuf)
{
	u32 status_error = le32_to_cpu(rx_desc->wb.upper.status_error);
	int smd;

	smd = FIELD_GET(IGC_RXDADV_STAT_SMD_TYPE_MASK, status_error);
	if (smd != IGC_RXD_STAT_SMD_TYPE_V && smd != IGC_RXD_STAT_SMD_TYPE_R)
		return false;

	if (size == SMD_FRAME_SIZE && mem_is_zero(pktbuf, SMD_FRAME_SIZE)) {
		struct ethtool_mmsv *mmsv = &adapter->fpe.mmsv;
		enum ethtool_mmsv_event event;

		if (smd == IGC_RXD_STAT_SMD_TYPE_V)
			event = ETHTOOL_MMSV_LP_SENT_VERIFY_MPACKET;
		else
			event = ETHTOOL_MMSV_LP_SENT_RESPONSE_MPACKET;

		ethtool_mmsv_event_handle(mmsv, event);
	}

	return true;
}

static inline bool igc_fpe_transmitted_smd_v(union igc_adv_tx_desc *tx_desc)
{
	u32 olinfo_status = le32_to_cpu(tx_desc->read.olinfo_status);
	u8 smd = FIELD_GET(IGC_TXD_POPTS_SMD_MASK, olinfo_status);

	return smd == SMD_V;
}

#endif /* _IGC_BASE_H */
