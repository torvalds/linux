// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2024 Intel Corporation
 */
#include <kunit/test.h>
#include "utils.h"
#include "iwl-trans.h"
#include "mld.h"
#include "sta.h"

static const struct is_dup_case {
	const char *desc;
	struct {
		/* ieee80211_hdr fields */
		__le16 fc;
		__le16 seq;
		u8 tid;
		bool multicast;
		/* iwl_rx_mpdu_desc fields */
		bool is_amsdu;
		u8 sub_frame_idx;
	} rx_pkt;
	struct {
		__le16 last_seq;
		u8 last_sub_frame_idx;
		u8 tid;
	} dup_data_state;
	struct {
		bool is_dup;
		u32 rx_status_flag;
	} result;
} is_dup_cases[] = {
	{
		.desc = "Control frame",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_CTL),
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = 0,
		}
	},
	{
		.desc = "Null func frame",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_NULLFUNC),
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = 0,
		}
	},
	{
		.desc = "Multicast data",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA),
			.multicast = true,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = 0,
		}
	},
	{
		.desc = "QoS null func frame",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_NULLFUNC),
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = 0,
		}
	},
	{
		.desc = "QoS data new sequence",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA),
			.seq = __cpu_to_le16(0x101),
		},
		.dup_data_state = {
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = RX_FLAG_DUP_VALIDATED,
		},
	},
	{
		.desc = "QoS data same sequence, no retry",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA),
			.seq = __cpu_to_le16(0x100),
		},
		.dup_data_state = {
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = RX_FLAG_DUP_VALIDATED,
		},
	},
	{
		.desc = "QoS data same sequence, has retry",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA |
					    IEEE80211_FCTL_RETRY),
			.seq = __cpu_to_le16(0x100),
		},
		.dup_data_state = {
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = true,
			.rx_status_flag = 0,
		},
	},
	{
		.desc = "QoS data invalid tid",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA),
			.seq = __cpu_to_le16(0x100),
			.tid = IWL_MAX_TID_COUNT + 1,
		},
		.result = {
			.is_dup = true,
			.rx_status_flag = 0,
		},
	},
	{
		.desc = "non-QoS data, same sequence, same tid, no retry",
		.rx_pkt = {
			/* Driver will use tid = IWL_MAX_TID_COUNT */
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA),
			.seq = __cpu_to_le16(0x100),
		},
		.dup_data_state = {
			.tid = IWL_MAX_TID_COUNT,
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = RX_FLAG_DUP_VALIDATED,
		},
	},
	{
		.desc = "non-QoS data, same sequence, same tid, has retry",
		.rx_pkt = {
			/* Driver will use tid = IWL_MAX_TID_COUNT */
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_FCTL_RETRY),
			.seq = __cpu_to_le16(0x100),
		},
		.dup_data_state = {
			.tid = IWL_MAX_TID_COUNT,
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = true,
			.rx_status_flag = 0,
		},
	},
	{
		.desc = "non-QoS data, same sequence on different tid's",
		.rx_pkt = {
			/* Driver will use tid = IWL_MAX_TID_COUNT */
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA),
			.seq = __cpu_to_le16(0x100),
		},
		.dup_data_state = {
			.tid = 7,
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = RX_FLAG_DUP_VALIDATED,
		},
	},
	{
		.desc = "A-MSDU new subframe, allow same PN",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA),
			.seq = __cpu_to_le16(0x100),
			.is_amsdu = true,
			.sub_frame_idx = 1,
		},
		.dup_data_state = {
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = RX_FLAG_ALLOW_SAME_PN |
					  RX_FLAG_DUP_VALIDATED,
		},
	},
	{
		.desc = "A-MSDU subframe with smaller idx, disallow same PN",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA),
			.seq = __cpu_to_le16(0x100),
			.is_amsdu = true,
			.sub_frame_idx = 1,
		},
		.dup_data_state = {
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 2,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = RX_FLAG_DUP_VALIDATED,
		},
	},
	{
		.desc = "A-MSDU same subframe, no retry, disallow same PN",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA),
			.seq = __cpu_to_le16(0x100),
			.is_amsdu = true,
			.sub_frame_idx = 0,
		},
		.dup_data_state = {
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = false,
			.rx_status_flag = RX_FLAG_DUP_VALIDATED,
		},
	},
	{
		.desc = "A-MSDU same subframe, has retry",
		.rx_pkt = {
			.fc = __cpu_to_le16(IEEE80211_FTYPE_DATA |
					    IEEE80211_STYPE_QOS_DATA |
					    IEEE80211_FCTL_RETRY),
			.seq = __cpu_to_le16(0x100),
			.is_amsdu = true,
			.sub_frame_idx = 0,
		},
		.dup_data_state = {
			.last_seq = __cpu_to_le16(0x100),
			.last_sub_frame_idx = 0,
		},
		.result = {
			.is_dup = true,
			.rx_status_flag = 0,
		},
	},
};

KUNIT_ARRAY_PARAM_DESC(test_is_dup, is_dup_cases, desc);

static void
setup_dup_data_state(struct ieee80211_sta *sta)
{
	struct kunit *test = kunit_get_current_test();
	const struct is_dup_case *param = (const void *)(test->param_value);
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	u8 tid = param->dup_data_state.tid;
	struct iwl_mld_rxq_dup_data *dup_data;

	/* Allocate dup_data only for 1 queue */
	KUNIT_ALLOC_AND_ASSERT(test, dup_data);

	/* Initialize dup data, see iwl_mld_alloc_dup_data */
	memset(dup_data->last_seq, 0xff, sizeof(dup_data->last_seq));

	dup_data->last_seq[tid] = param->dup_data_state.last_seq;
	dup_data->last_sub_frame_idx[tid] =
		param->dup_data_state.last_sub_frame_idx;

	mld_sta->dup_data = dup_data;
}

static void setup_rx_pkt(const struct is_dup_case *param,
			 struct ieee80211_hdr *hdr,
			 struct iwl_rx_mpdu_desc *mpdu_desc)
{
	u8 tid = param->rx_pkt.tid;

	/* Set "new rx packet" header */
	hdr->frame_control = param->rx_pkt.fc;
	hdr->seq_ctrl = param->rx_pkt.seq;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);

		qc[0] = tid & IEEE80211_QOS_CTL_TID_MASK;
	}

	if (param->rx_pkt.multicast)
		hdr->addr1[0] = 0x1;

	/* Set mpdu_desc */
	mpdu_desc->amsdu_info = param->rx_pkt.sub_frame_idx &
				IWL_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;
	if (param->rx_pkt.is_amsdu)
		mpdu_desc->mac_flags2 |= IWL_RX_MPDU_MFLG2_AMSDU;
}

static void test_is_dup(struct kunit *test)
{
	const struct is_dup_case *param = (const void *)(test->param_value);
	struct iwl_mld *mld = test->priv;
	struct iwl_rx_mpdu_desc mpdu_desc = { };
	struct ieee80211_rx_status rx_status = { };
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr hdr;

	vif = iwlmld_kunit_add_vif(false, NL80211_IFTYPE_STATION);
	sta = iwlmld_kunit_setup_sta(vif, IEEE80211_STA_AUTHORIZED, -1);

	/* Prepare test case state */
	setup_dup_data_state(sta);
	setup_rx_pkt(param, &hdr, &mpdu_desc);

	KUNIT_EXPECT_EQ(test,
			iwl_mld_is_dup(mld, sta, &hdr, &mpdu_desc, &rx_status,
				       0), /* assuming only 1 queue */
			param->result.is_dup);
	KUNIT_EXPECT_EQ(test, rx_status.flag, param->result.rx_status_flag);
}

static struct kunit_case is_dup_test_cases[] = {
	KUNIT_CASE_PARAM(test_is_dup, test_is_dup_gen_params),
	{},
};

static struct kunit_suite is_dup = {
	.name = "iwlmld-rx-is-dup",
	.test_cases = is_dup_test_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(is_dup);
