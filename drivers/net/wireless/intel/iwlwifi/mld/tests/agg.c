// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <kunit/skbuff.h>

#include "utils.h"
#include "mld.h"
#include "sta.h"
#include "agg.h"
#include "rx.h"

#define FC_QOS_DATA (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)
#define BA_WINDOW_SIZE 64
#define QUEUE 0

static const struct reorder_buffer_case {
	const char *desc;
	struct {
		/* ieee80211_hdr fields */
		u16 fc;
		u8 tid;
		bool multicast;
		/* iwl_rx_mpdu_desc fields */
		u16 nssn;
		/* used also for setting hdr::seq_ctrl */
		u16 sn;
		u8 baid;
		bool amsdu;
		bool last_subframe;
		bool old_sn;
		bool dup;
	} rx_pkt;
	struct {
		bool valid;
		u16 head_sn;
		u8 baid;
		u16 num_entries;
		/* The test prepares the reorder buffer with fake skbs based
		 * on the sequence numbers provided in @entries array.
		 */
		struct {
			u16 sn;
			/* Set add_subframes > 0 to simulate an A-MSDU by
			 * queueing additional @add_subframes skbs in the
			 * appropriate reorder buffer entry (based on the @sn)
			 */
			u8 add_subframes;
		} entries[BA_WINDOW_SIZE];
	} reorder_buf_state;
	struct {
		enum iwl_mld_reorder_result reorder_res;
		u16 head_sn;
		u16 num_stored;
		u16 skb_release_order[BA_WINDOW_SIZE];
		u16 skb_release_order_count;
	} expected;
} reorder_buffer_cases[] = {
	{
		.desc = "RX packet with invalid BAID",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.baid = IWL_RX_REORDER_DATA_INVALID_BAID,
		},
		.reorder_buf_state = {
			.valid = true,
		},
		.expected = {
			/* Invalid BAID should not be buffered. The frame is
			 * passed to the network stack immediately.
			 */
			.reorder_res = IWL_MLD_PASS_SKB,
			.num_stored = 0,
		},
	},
	{
		.desc = "RX Multicast packet",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.multicast = true,
		},
		.reorder_buf_state = {
			.valid = true,
		},
		.expected = {
			/* Multicast packets are not buffered. The packet is
			 * passed to the network stack immediately.
			 */
			.reorder_res = IWL_MLD_PASS_SKB,
			.num_stored = 0,
		},
	},
	{
		.desc = "RX non-QoS data",
		.rx_pkt = {
			.fc = IEEE80211_FTYPE_DATA,
		},
		.reorder_buf_state = {
			.valid = true,
		},
		.expected = {
			/* non-QoS data frames do not require reordering.
			 * The packet is passed to the network stack
			 * immediately.
			 */
		.reorder_res = IWL_MLD_PASS_SKB,
		},
	},
	{
		.desc = "RX old SN packet, reorder buffer is not yet valid",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.old_sn = true,
		},
		.reorder_buf_state = {
			.valid = false,
		},
		.expected = {
			/* The buffer is invalid and the RX packet has an old
			 * SN. The packet is passed to the network stack
			 * immediately.
			 */
			.reorder_res = IWL_MLD_PASS_SKB,
		},
	},
	{
		.desc = "RX old SN packet, reorder buffer valid",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.old_sn = true,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
		},
		.expected = {
			/* The buffer is valid and the RX packet has an old SN.
			 * The packet should be dropped.
			 */
			.reorder_res = IWL_MLD_DROP_SKB,
			.num_stored = 0,
			.head_sn = 100,
		},
	},
	{
		.desc = "RX duplicate packet",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.dup = true,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
		},
		.expected = {
			/* Duplicate packets should be dropped */
			.reorder_res = IWL_MLD_DROP_SKB,
			.num_stored = 0,
			.head_sn = 100,
		},
	},
	{
		.desc = "RX In-order packet, sn < nssn",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 100,
			.nssn = 101,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
		},
		.expected = {
			/* 1. Reorder buffer is empty.
			 * 2. RX packet SN is in order and less than NSSN.
			 * Packet is released to the network stack immediately
			 * and buffer->head_sn is updated to NSSN.
			 */
			.reorder_res = IWL_MLD_PASS_SKB,
			.num_stored = 0,
			.head_sn = 101,
		},
	},
	{
		.desc = "RX In-order packet, sn == head_sn",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 101,
			.nssn = 100,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 101,
		},
		.expected = {
			/* 1. Reorder buffer is empty.
			 * 2. RX packet SN is equal to buffer->head_sn.
			 * Packet is released to the network stack immediately
			 * and buffer->head_sn is incremented.
			 */
			.reorder_res = IWL_MLD_PASS_SKB,
			.num_stored = 0,
			.head_sn = 102,
		},
	},
	{
		.desc = "RX In-order packet, IEEE80211_MAX_SN wrap around",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = IEEE80211_MAX_SN,
			.nssn = IEEE80211_MAX_SN - 1,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = IEEE80211_MAX_SN,
		},
		.expected = {
			/* 1. Reorder buffer is empty.
			 * 2. RX SN == buffer->head_sn == IEEE80211_MAX_SN
			 * Packet is released to the network stack immediately
			 * and buffer->head_sn is incremented correctly (wraps
			 * around to 0).
			 */
			.reorder_res = IWL_MLD_PASS_SKB,
			.num_stored = 0,
			.head_sn = 0,
		},
	},
	{
		.desc = "RX Out-of-order packet, pending packet in buffer",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 100,
			.nssn = 102,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
			.num_entries = 1,
			.entries[0].sn = 101,
		},
		.expected = {
			/* 1. Reorder buffer contains one packet with SN=101.
			 * 2. RX packet SN = buffer->head_sn.
			 * Both packets are released (in order) to the network
			 * stack as there are no gaps.
			 */
			.reorder_res = IWL_MLD_BUFFERED_SKB,
			.num_stored = 0,
			.head_sn = 102,
			.skb_release_order = {100, 101},
			.skb_release_order_count = 2,
		},
	},
	{
		.desc = "RX Out-of-order packet, pending packet in buffer (wrap around)",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 0,
			.nssn = 1,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = IEEE80211_MAX_SN - 1,
			.num_entries = 1,
			.entries[0].sn = IEEE80211_MAX_SN,
		},
		.expected = {
			/* 1. Reorder buffer contains one packet with
			 *    SN=IEEE80211_MAX_SN.
			 * 2. RX Packet SN = 0 (after wrap around)
			 * Both packets are released (in order) to the network
			 * stack as there are no gaps.
			 */
			.reorder_res = IWL_MLD_BUFFERED_SKB,
			.num_stored = 0,
			.head_sn = 1,
			.skb_release_order = { 4095, 0 },
			.skb_release_order_count = 2,
		},
	},
	{
		.desc = "RX Out-of-order packet, filling 1/2 holes in buffer, release RX packet",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 100,
			.nssn = 101,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
			.num_entries = 1,
			.entries[0].sn = 102,
		},
		.expected = {
			/* 1. Reorder buffer contains one packet with SN=102.
			 * 2. There are 2 holes at SN={100, 101}.
			 * Only the RX packet (SN=100) is released, there is
			 * still a hole at 101.
			 */
			.reorder_res = IWL_MLD_BUFFERED_SKB,
			.num_stored = 1,
			.head_sn = 101,
			.skb_release_order = {100},
			.skb_release_order_count = 1,
		},
	},
	{
		.desc = "RX Out-of-order packet, filling 1/2 holes, release 2 packets",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 102,
			.nssn = 103,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
			.num_entries = 3,
			.entries[0].sn = 101,
			.entries[1].sn = 104,
			.entries[2].sn = 105,
		},
		.expected = {
			/* 1. Reorder buffer contains three packets.
			 * 2. RX packet fills one of two holes (at SN=102).
			 * Two packets are released (until the next hole at
			 * SN=103).
			 */
			.reorder_res = IWL_MLD_BUFFERED_SKB,
			.num_stored = 2,
			.head_sn = 103,
			.skb_release_order = {101, 102},
			.skb_release_order_count = 2,
		},
	},
		{
		.desc = "RX Out-of-order packet, filling 1/1 holes, no packets released",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 102,
			.nssn = 100,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
			.num_entries = 3,
			.entries[0].sn = 101,
			.entries[1].sn = 103,
			.entries[2].sn = 104,
		},
		.expected = {
			/* 1. Reorder buffer contains three packets:
			 *    SN={101, 103, 104}.
			 * 2. RX packet fills a hole (SN=102), but NSSN is
			 *    smaller than buffered frames.
			 * No packets can be released yet and buffer->head_sn
			 * is not updated.
			 */
			.reorder_res = IWL_MLD_BUFFERED_SKB,
			.num_stored = 4,
			.head_sn = 100,
		},
	},
	{
		.desc = "RX In-order A-MSDU, last subframe",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 100,
			.nssn = 101,
			.amsdu = true,
			.last_subframe = true,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
			.num_entries = 1,
			.entries[0] = {
				.sn = 100,
				.add_subframes = 1,
			},
		},
		.expected = {
			/* 1. Reorder buffer contains a 2-sub frames A-MSDU
			 *    at SN=100.
			 * 2. RX packet is the last SN=100 A-MSDU subframe
			 * All packets are released in order (3 x SN=100).
			 */
			.reorder_res = IWL_MLD_BUFFERED_SKB,
			.num_stored = 0,
			.head_sn = 101,
			.skb_release_order = {100, 100, 100},
			.skb_release_order_count = 3,
		},
	},
	{
		.desc = "RX In-order A-MSDU, not the last subframe",
		.rx_pkt = {
			.fc = FC_QOS_DATA,
			.sn = 100,
			.nssn = 101,
			.amsdu = true,
			.last_subframe = false,
		},
		.reorder_buf_state = {
			.valid = true,
			.head_sn = 100,
			.num_entries = 1,
			.entries[0] = {
				.sn = 100,
				.add_subframes = 1,
			},
		},
		.expected = {
			/* 1. Reorder buffer contains a 2-sub frames A-MSDU
			 *    at SN=100.
			 * 2. RX packet additional SN=100 A-MSDU subframe,
			 *    but not the last one
			 * No packets are released and head_sn is not updated.
			 */
			.reorder_res = IWL_MLD_BUFFERED_SKB,
			.num_stored = 3,
			.head_sn = 100,
		},
	},
};

KUNIT_ARRAY_PARAM_DESC(test_reorder_buffer, reorder_buffer_cases, desc);

static struct sk_buff_head g_released_skbs;
static u16 g_num_released_skbs;

/* Add released skbs from reorder buffer to a global list; This allows us
 * to verify the correct release order of packets after they pass through the
 * simulated reorder logic.
 */
static void
fake_iwl_mld_pass_packet_to_mac80211(struct iwl_mld *mld,
				     struct napi_struct *napi,
				     struct sk_buff *skb, int queue,
				     struct ieee80211_sta *sta)
{
	__skb_queue_tail(&g_released_skbs, skb);
	g_num_released_skbs++;
}

static u32
fake_iwl_mld_fw_sta_id_mask(struct iwl_mld *mld, struct ieee80211_sta *sta)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_link_sta *mld_link_sta;
	u8 link_id;
	u32 sta_mask = 0;

	/* This is the expectation in the real function */
	lockdep_assert_wiphy(mld->wiphy);

	/* We can't use for_each_sta_active_link */
	for_each_mld_link_sta(mld_sta, mld_link_sta, link_id)
		sta_mask |= BIT(mld_link_sta->fw_id);
	return sta_mask;
}

static struct iwl_rx_mpdu_desc *setup_mpdu_desc(void)
{
	struct kunit *test = kunit_get_current_test();
	const struct reorder_buffer_case *param =
		(const void *)(test->param_value);
	struct iwl_rx_mpdu_desc *mpdu_desc;

	KUNIT_ALLOC_AND_ASSERT(test, mpdu_desc);

	mpdu_desc->reorder_data |=
		le32_encode_bits(param->rx_pkt.baid,
				 IWL_RX_MPDU_REORDER_BAID_MASK);
	mpdu_desc->reorder_data |=
		le32_encode_bits(param->rx_pkt.sn,
				 IWL_RX_MPDU_REORDER_SN_MASK);
	mpdu_desc->reorder_data |=
		le32_encode_bits(param->rx_pkt.nssn,
				 IWL_RX_MPDU_REORDER_NSSN_MASK);
	if (param->rx_pkt.old_sn)
		mpdu_desc->reorder_data |=
			cpu_to_le32(IWL_RX_MPDU_REORDER_BA_OLD_SN);

	if (param->rx_pkt.dup)
		mpdu_desc->status |= cpu_to_le32(IWL_RX_MPDU_STATUS_DUPLICATE);

	if (param->rx_pkt.amsdu) {
		mpdu_desc->mac_flags2 |= IWL_RX_MPDU_MFLG2_AMSDU;
		if (param->rx_pkt.last_subframe)
			mpdu_desc->amsdu_info |=
				IWL_RX_MPDU_AMSDU_LAST_SUBFRAME;
	}

	return mpdu_desc;
}

static struct sk_buff *alloc_and_setup_skb(u16 fc, u16 seq_ctrl, u8 tid,
					   bool mcast)
{
	struct kunit *test = kunit_get_current_test();
	struct ieee80211_hdr hdr = {
		.frame_control = cpu_to_le16(fc),
		.seq_ctrl = cpu_to_le16(seq_ctrl),
	};
	struct sk_buff *skb;

	skb = kunit_zalloc_skb(test, 128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);

	if (ieee80211_is_data_qos(hdr.frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(&hdr);

		qc[0] = tid & IEEE80211_QOS_CTL_TID_MASK;
	}

	if (mcast)
		hdr.addr1[0] = 0x1;

	skb_set_mac_header(skb, skb->len);
	skb_put_data(skb, &hdr, ieee80211_hdrlen(hdr.frame_control));

	return skb;
}

static struct iwl_mld_reorder_buffer *
setup_reorder_buffer(struct iwl_mld_baid_data *baid_data)
{
	struct kunit *test = kunit_get_current_test();
	const struct reorder_buffer_case *param =
		(const void *)(test->param_value);
	struct iwl_mld_reorder_buffer *buffer = baid_data->reorder_buf;
	struct iwl_mld_reorder_buf_entry *entries = baid_data->entries;
	struct sk_buff *fake_skb;

	buffer->valid = param->reorder_buf_state.valid;
	buffer->head_sn = param->reorder_buf_state.head_sn;
	buffer->queue = QUEUE;

	for (int i = 0; i < baid_data->buf_size; i++)
		__skb_queue_head_init(&entries[i].frames);

	for (int i = 0; i < param->reorder_buf_state.num_entries; i++) {
		u16 sn = param->reorder_buf_state.entries[i].sn;
		int index = sn % baid_data->buf_size;
		u8 add_subframes =
			param->reorder_buf_state.entries[i].add_subframes;
		/* create 1 skb per entry + additional skbs per num of
		 * requested subframes
		 */
		u8 num_skbs = 1 + add_subframes;

		for (int j = 0; j < num_skbs; j++) {
			fake_skb = alloc_and_setup_skb(FC_QOS_DATA, sn, 0,
						       false);
			__skb_queue_tail(&entries[index].frames, fake_skb);
			buffer->num_stored++;
		}
	}

	return buffer;
}

static struct iwl_mld_reorder_buffer *setup_ba_data(struct ieee80211_sta *sta)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	const struct reorder_buffer_case *param =
		(const void *)(test->param_value);
	struct iwl_mld_baid_data *baid_data = NULL;
	struct iwl_mld_reorder_buffer *buffer;
	u32 reorder_buf_size = BA_WINDOW_SIZE * sizeof(baid_data->entries[0]);
	u8 baid = param->reorder_buf_state.baid;

	/* Assuming only 1 RXQ */
	KUNIT_ALLOC_AND_ASSERT_SIZE(test, baid_data,
				    sizeof(*baid_data) + reorder_buf_size);

	baid_data->baid = baid;
	baid_data->tid = param->rx_pkt.tid;
	baid_data->buf_size = BA_WINDOW_SIZE;

	wiphy_lock(mld->wiphy);
	baid_data->sta_mask = iwl_mld_fw_sta_id_mask(mld, sta);
	wiphy_unlock(mld->wiphy);

	baid_data->entries_per_queue = BA_WINDOW_SIZE;

	buffer = setup_reorder_buffer(baid_data);

	KUNIT_EXPECT_NULL(test, rcu_access_pointer(mld->fw_id_to_ba[baid]));
	rcu_assign_pointer(mld->fw_id_to_ba[baid], baid_data);

	return buffer;
}

static void test_reorder_buffer(struct kunit *test)
{
	struct iwl_mld *mld = test->priv;
	const struct reorder_buffer_case *param =
		(const void *)(test->param_value);
	struct iwl_rx_mpdu_desc *mpdu_desc;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	struct sk_buff *skb;
	struct iwl_mld_reorder_buffer *buffer;
	enum iwl_mld_reorder_result reorder_res;
	u16 skb_release_order_count = param->expected.skb_release_order_count;
	u16 skb_idx = 0;

	/* Init globals and activate stubs */
	__skb_queue_head_init(&g_released_skbs);
	g_num_released_skbs = 0;
	kunit_activate_static_stub(test, iwl_mld_fw_sta_id_mask,
				   fake_iwl_mld_fw_sta_id_mask);
	kunit_activate_static_stub(test, iwl_mld_pass_packet_to_mac80211,
				   fake_iwl_mld_pass_packet_to_mac80211);

	vif = iwlmld_kunit_add_vif(false, NL80211_IFTYPE_STATION);
	sta = iwlmld_kunit_setup_sta(vif, IEEE80211_STA_AUTHORIZED, -1);

	/* Prepare skb, mpdu_desc, BA data and the reorder buffer */
	skb = alloc_and_setup_skb(param->rx_pkt.fc, param->rx_pkt.sn,
				  param->rx_pkt.tid, param->rx_pkt.multicast);
	buffer = setup_ba_data(sta);
	mpdu_desc = setup_mpdu_desc();

	rcu_read_lock();
	reorder_res = iwl_mld_reorder(mld, NULL, QUEUE, sta, skb, mpdu_desc);
	rcu_read_unlock();

	KUNIT_ASSERT_EQ(test, reorder_res, param->expected.reorder_res);
	KUNIT_ASSERT_EQ(test, buffer->num_stored, param->expected.num_stored);
	KUNIT_ASSERT_EQ(test, buffer->head_sn, param->expected.head_sn);

	/* Verify skbs release order */
	KUNIT_ASSERT_EQ(test, skb_release_order_count, g_num_released_skbs);
	while ((skb = __skb_dequeue(&g_released_skbs))) {
		struct ieee80211_hdr *hdr = (void *)skb_mac_header(skb);

		KUNIT_ASSERT_EQ(test, le16_to_cpu(hdr->seq_ctrl),
				param->expected.skb_release_order[skb_idx]);
		skb_idx++;
	}
	KUNIT_ASSERT_EQ(test, skb_idx, skb_release_order_count);
}

static struct kunit_case reorder_buffer_test_cases[] = {
	KUNIT_CASE_PARAM(test_reorder_buffer, test_reorder_buffer_gen_params),
	{},
};

static struct kunit_suite reorder_buffer = {
	.name = "iwlmld-reorder-buffer",
	.test_cases = reorder_buffer_test_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(reorder_buffer);
