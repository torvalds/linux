/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2014 Qualcomm Atheros, Inc.
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
#include "core.h"
#include "debug.h"
#include "hw.h"
#include "wmi.h"
#include "wmi-ops.h"
#include "wmi-tlv.h"

/***************/
/* TLV helpers */
/**************/

struct wmi_tlv_policy {
	size_t min_len;
};

static const struct wmi_tlv_policy wmi_tlv_policies[] = {
	[WMI_TLV_TAG_ARRAY_BYTE]
		= { .min_len = sizeof(u8) },
	[WMI_TLV_TAG_ARRAY_UINT32]
		= { .min_len = sizeof(u32) },
	[WMI_TLV_TAG_STRUCT_SCAN_EVENT]
		= { .min_len = sizeof(struct wmi_scan_event) },
	[WMI_TLV_TAG_STRUCT_MGMT_RX_HDR]
		= { .min_len = sizeof(struct wmi_tlv_mgmt_rx_ev) },
	[WMI_TLV_TAG_STRUCT_CHAN_INFO_EVENT]
		= { .min_len = sizeof(struct wmi_chan_info_event) },
	[WMI_TLV_TAG_STRUCT_VDEV_START_RESPONSE_EVENT]
		= { .min_len = sizeof(struct wmi_vdev_start_response_event) },
	[WMI_TLV_TAG_STRUCT_PEER_STA_KICKOUT_EVENT]
		= { .min_len = sizeof(struct wmi_peer_sta_kickout_event) },
	[WMI_TLV_TAG_STRUCT_HOST_SWBA_EVENT]
		= { .min_len = sizeof(struct wmi_host_swba_event) },
	[WMI_TLV_TAG_STRUCT_TIM_INFO]
		= { .min_len = sizeof(struct wmi_tim_info) },
	[WMI_TLV_TAG_STRUCT_P2P_NOA_INFO]
		= { .min_len = sizeof(struct wmi_p2p_noa_info) },
	[WMI_TLV_TAG_STRUCT_SERVICE_READY_EVENT]
		= { .min_len = sizeof(struct wmi_tlv_svc_rdy_ev) },
	[WMI_TLV_TAG_STRUCT_HAL_REG_CAPABILITIES]
		= { .min_len = sizeof(struct hal_reg_capabilities) },
	[WMI_TLV_TAG_STRUCT_WLAN_HOST_MEM_REQ]
		= { .min_len = sizeof(struct wlan_host_mem_req) },
	[WMI_TLV_TAG_STRUCT_READY_EVENT]
		= { .min_len = sizeof(struct wmi_tlv_rdy_ev) },
};

static int
ath10k_wmi_tlv_iter(struct ath10k *ar, const void *ptr, size_t len,
		    int (*iter)(struct ath10k *ar, u16 tag, u16 len,
				const void *ptr, void *data),
		    void *data)
{
	const void *begin = ptr;
	const struct wmi_tlv *tlv;
	u16 tlv_tag, tlv_len;
	int ret;

	while (len > 0) {
		if (len < sizeof(*tlv)) {
			ath10k_dbg(ar, ATH10K_DBG_WMI,
				   "wmi tlv parse failure at byte %zd (%zu bytes left, %zu expected)\n",
				   ptr - begin, len, sizeof(*tlv));
			return -EINVAL;
		}

		tlv = ptr;
		tlv_tag = __le16_to_cpu(tlv->tag);
		tlv_len = __le16_to_cpu(tlv->len);
		ptr += sizeof(*tlv);
		len -= sizeof(*tlv);

		if (tlv_len > len) {
			ath10k_dbg(ar, ATH10K_DBG_WMI,
				   "wmi tlv parse failure of tag %hhu at byte %zd (%zu bytes left, %hhu expected)\n",
				   tlv_tag, ptr - begin, len, tlv_len);
			return -EINVAL;
		}

		if (tlv_tag < ARRAY_SIZE(wmi_tlv_policies) &&
		    wmi_tlv_policies[tlv_tag].min_len &&
		    wmi_tlv_policies[tlv_tag].min_len > tlv_len) {
			ath10k_dbg(ar, ATH10K_DBG_WMI,
				   "wmi tlv parse failure of tag %hhu at byte %zd (%hhu bytes is less than min length %zu)\n",
				   tlv_tag, ptr - begin, tlv_len,
				   wmi_tlv_policies[tlv_tag].min_len);
			return -EINVAL;
		}

		ret = iter(ar, tlv_tag, tlv_len, ptr, data);
		if (ret)
			return ret;

		ptr += tlv_len;
		len -= tlv_len;
	}

	return 0;
}

static int ath10k_wmi_tlv_iter_parse(struct ath10k *ar, u16 tag, u16 len,
				     const void *ptr, void *data)
{
	const void **tb = data;

	if (tag < WMI_TLV_TAG_MAX)
		tb[tag] = ptr;

	return 0;
}

static int ath10k_wmi_tlv_parse(struct ath10k *ar, const void **tb,
				const void *ptr, size_t len)
{
	return ath10k_wmi_tlv_iter(ar, ptr, len, ath10k_wmi_tlv_iter_parse,
				   (void *)tb);
}

static const void **
ath10k_wmi_tlv_parse_alloc(struct ath10k *ar, const void *ptr,
			   size_t len, gfp_t gfp)
{
	const void **tb;
	int ret;

	tb = kzalloc(sizeof(*tb) * WMI_TLV_TAG_MAX, gfp);
	if (!tb)
		return ERR_PTR(-ENOMEM);

	ret = ath10k_wmi_tlv_parse(ar, tb, ptr, len);
	if (ret) {
		kfree(tb);
		return ERR_PTR(ret);
	}

	return tb;
}

static u16 ath10k_wmi_tlv_len(const void *ptr)
{
	return __le16_to_cpu((((const struct wmi_tlv *)ptr) - 1)->len);
}

/***********/
/* TLV ops */
/***********/

static void ath10k_wmi_tlv_op_rx(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_cmd_hdr *cmd_hdr;
	enum wmi_tlv_event_id id;

	cmd_hdr = (struct wmi_cmd_hdr *)skb->data;
	id = MS(__le32_to_cpu(cmd_hdr->cmd_id), WMI_CMD_HDR_CMD_ID);

	if (skb_pull(skb, sizeof(struct wmi_cmd_hdr)) == NULL)
		return;

	trace_ath10k_wmi_event(ar, id, skb->data, skb->len);

	switch (id) {
	case WMI_TLV_MGMT_RX_EVENTID:
		ath10k_wmi_event_mgmt_rx(ar, skb);
		/* mgmt_rx() owns the skb now! */
		return;
	case WMI_TLV_SCAN_EVENTID:
		ath10k_wmi_event_scan(ar, skb);
		break;
	case WMI_TLV_CHAN_INFO_EVENTID:
		ath10k_wmi_event_chan_info(ar, skb);
		break;
	case WMI_TLV_ECHO_EVENTID:
		ath10k_wmi_event_echo(ar, skb);
		break;
	case WMI_TLV_DEBUG_MESG_EVENTID:
		ath10k_wmi_event_debug_mesg(ar, skb);
		break;
	case WMI_TLV_UPDATE_STATS_EVENTID:
		ath10k_wmi_event_update_stats(ar, skb);
		break;
	case WMI_TLV_VDEV_START_RESP_EVENTID:
		ath10k_wmi_event_vdev_start_resp(ar, skb);
		break;
	case WMI_TLV_VDEV_STOPPED_EVENTID:
		ath10k_wmi_event_vdev_stopped(ar, skb);
		break;
	case WMI_TLV_PEER_STA_KICKOUT_EVENTID:
		ath10k_wmi_event_peer_sta_kickout(ar, skb);
		break;
	case WMI_TLV_HOST_SWBA_EVENTID:
		ath10k_wmi_event_host_swba(ar, skb);
		break;
	case WMI_TLV_TBTTOFFSET_UPDATE_EVENTID:
		ath10k_wmi_event_tbttoffset_update(ar, skb);
		break;
	case WMI_TLV_PHYERR_EVENTID:
		ath10k_wmi_event_phyerr(ar, skb);
		break;
	case WMI_TLV_ROAM_EVENTID:
		ath10k_wmi_event_roam(ar, skb);
		break;
	case WMI_TLV_PROFILE_MATCH:
		ath10k_wmi_event_profile_match(ar, skb);
		break;
	case WMI_TLV_DEBUG_PRINT_EVENTID:
		ath10k_wmi_event_debug_print(ar, skb);
		break;
	case WMI_TLV_PDEV_QVIT_EVENTID:
		ath10k_wmi_event_pdev_qvit(ar, skb);
		break;
	case WMI_TLV_WLAN_PROFILE_DATA_EVENTID:
		ath10k_wmi_event_wlan_profile_data(ar, skb);
		break;
	case WMI_TLV_RTT_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_rtt_measurement_report(ar, skb);
		break;
	case WMI_TLV_TSF_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_tsf_measurement_report(ar, skb);
		break;
	case WMI_TLV_RTT_ERROR_REPORT_EVENTID:
		ath10k_wmi_event_rtt_error_report(ar, skb);
		break;
	case WMI_TLV_WOW_WAKEUP_HOST_EVENTID:
		ath10k_wmi_event_wow_wakeup_host(ar, skb);
		break;
	case WMI_TLV_DCS_INTERFERENCE_EVENTID:
		ath10k_wmi_event_dcs_interference(ar, skb);
		break;
	case WMI_TLV_PDEV_TPC_CONFIG_EVENTID:
		ath10k_wmi_event_pdev_tpc_config(ar, skb);
		break;
	case WMI_TLV_PDEV_FTM_INTG_EVENTID:
		ath10k_wmi_event_pdev_ftm_intg(ar, skb);
		break;
	case WMI_TLV_GTK_OFFLOAD_STATUS_EVENTID:
		ath10k_wmi_event_gtk_offload_status(ar, skb);
		break;
	case WMI_TLV_GTK_REKEY_FAIL_EVENTID:
		ath10k_wmi_event_gtk_rekey_fail(ar, skb);
		break;
	case WMI_TLV_TX_DELBA_COMPLETE_EVENTID:
		ath10k_wmi_event_delba_complete(ar, skb);
		break;
	case WMI_TLV_TX_ADDBA_COMPLETE_EVENTID:
		ath10k_wmi_event_addba_complete(ar, skb);
		break;
	case WMI_TLV_VDEV_INSTALL_KEY_COMPLETE_EVENTID:
		ath10k_wmi_event_vdev_install_key_complete(ar, skb);
		break;
	case WMI_TLV_SERVICE_READY_EVENTID:
		ath10k_wmi_event_service_ready(ar, skb);
		break;
	case WMI_TLV_READY_EVENTID:
		ath10k_wmi_event_ready(ar, skb);
		break;
	default:
		ath10k_warn(ar, "Unknown eventid: %d\n", id);
		break;
	}

	dev_kfree_skb(skb);
}

static int ath10k_wmi_tlv_op_pull_scan_ev(struct ath10k *ar,
					  struct sk_buff *skb,
					  struct wmi_scan_ev_arg *arg)
{
	const void **tb;
	const struct wmi_scan_event *ev;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_SCAN_EVENT];
	if (!ev) {
		kfree(tb);
		return -EPROTO;
	}

	arg->event_type = ev->event_type;
	arg->reason = ev->reason;
	arg->channel_freq = ev->channel_freq;
	arg->scan_req_id = ev->scan_req_id;
	arg->scan_id = ev->scan_id;
	arg->vdev_id = ev->vdev_id;

	kfree(tb);
	return 0;
}

static int ath10k_wmi_tlv_op_pull_mgmt_rx_ev(struct ath10k *ar,
					     struct sk_buff *skb,
					     struct wmi_mgmt_rx_ev_arg *arg)
{
	const void **tb;
	const struct wmi_tlv_mgmt_rx_ev *ev;
	const u8 *frame;
	u32 msdu_len;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_MGMT_RX_HDR];
	frame = tb[WMI_TLV_TAG_ARRAY_BYTE];

	if (!ev || !frame) {
		kfree(tb);
		return -EPROTO;
	}

	arg->channel = ev->channel;
	arg->buf_len = ev->buf_len;
	arg->status = ev->status;
	arg->snr = ev->snr;
	arg->phy_mode = ev->phy_mode;
	arg->rate = ev->rate;

	msdu_len = __le32_to_cpu(arg->buf_len);

	if (skb->len < (frame - skb->data) + msdu_len) {
		kfree(tb);
		return -EPROTO;
	}

	/* shift the sk_buff to point to `frame` */
	skb_trim(skb, 0);
	skb_put(skb, frame - skb->data);
	skb_pull(skb, frame - skb->data);
	skb_put(skb, msdu_len);

	kfree(tb);
	return 0;
}

static int ath10k_wmi_tlv_op_pull_ch_info_ev(struct ath10k *ar,
					     struct sk_buff *skb,
					     struct wmi_ch_info_ev_arg *arg)
{
	const void **tb;
	const struct wmi_chan_info_event *ev;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_CHAN_INFO_EVENT];
	if (!ev) {
		kfree(tb);
		return -EPROTO;
	}

	arg->err_code = ev->err_code;
	arg->freq = ev->freq;
	arg->cmd_flags = ev->cmd_flags;
	arg->noise_floor = ev->noise_floor;
	arg->rx_clear_count = ev->rx_clear_count;
	arg->cycle_count = ev->cycle_count;

	kfree(tb);
	return 0;
}

static int
ath10k_wmi_tlv_op_pull_vdev_start_ev(struct ath10k *ar, struct sk_buff *skb,
				     struct wmi_vdev_start_ev_arg *arg)
{
	const void **tb;
	const struct wmi_vdev_start_response_event *ev;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_VDEV_START_RESPONSE_EVENT];
	if (!ev) {
		kfree(tb);
		return -EPROTO;
	}

	skb_pull(skb, sizeof(*ev));
	arg->vdev_id = ev->vdev_id;
	arg->req_id = ev->req_id;
	arg->resp_type = ev->resp_type;
	arg->status = ev->status;

	kfree(tb);
	return 0;
}

static int ath10k_wmi_tlv_op_pull_peer_kick_ev(struct ath10k *ar,
					       struct sk_buff *skb,
					       struct wmi_peer_kick_ev_arg *arg)
{
	const void **tb;
	const struct wmi_peer_sta_kickout_event *ev;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_PEER_STA_KICKOUT_EVENT];
	if (!ev) {
		kfree(tb);
		return -EPROTO;
	}

	arg->mac_addr = ev->peer_macaddr.addr;

	kfree(tb);
	return 0;
}

struct wmi_tlv_swba_parse {
	const struct wmi_host_swba_event *ev;
	bool tim_done;
	bool noa_done;
	size_t n_tim;
	size_t n_noa;
	struct wmi_swba_ev_arg *arg;
};

static int ath10k_wmi_tlv_swba_tim_parse(struct ath10k *ar, u16 tag, u16 len,
					 const void *ptr, void *data)
{
	struct wmi_tlv_swba_parse *swba = data;

	if (tag != WMI_TLV_TAG_STRUCT_TIM_INFO)
		return -EPROTO;

	if (swba->n_tim >= ARRAY_SIZE(swba->arg->tim_info))
		return -ENOBUFS;

	swba->arg->tim_info[swba->n_tim++] = ptr;
	return 0;
}

static int ath10k_wmi_tlv_swba_noa_parse(struct ath10k *ar, u16 tag, u16 len,
					 const void *ptr, void *data)
{
	struct wmi_tlv_swba_parse *swba = data;

	if (tag != WMI_TLV_TAG_STRUCT_P2P_NOA_INFO)
		return -EPROTO;

	if (swba->n_noa >= ARRAY_SIZE(swba->arg->noa_info))
		return -ENOBUFS;

	swba->arg->noa_info[swba->n_noa++] = ptr;
	return 0;
}

static int ath10k_wmi_tlv_swba_parse(struct ath10k *ar, u16 tag, u16 len,
				     const void *ptr, void *data)
{
	struct wmi_tlv_swba_parse *swba = data;
	int ret;

	switch (tag) {
	case WMI_TLV_TAG_STRUCT_HOST_SWBA_EVENT:
		swba->ev = ptr;
		break;
	case WMI_TLV_TAG_ARRAY_STRUCT:
		if (!swba->tim_done) {
			swba->tim_done = true;
			ret = ath10k_wmi_tlv_iter(ar, ptr, len,
						  ath10k_wmi_tlv_swba_tim_parse,
						  swba);
			if (ret)
				return ret;
		} else if (!swba->noa_done) {
			swba->noa_done = true;
			ret = ath10k_wmi_tlv_iter(ar, ptr, len,
						  ath10k_wmi_tlv_swba_noa_parse,
						  swba);
			if (ret)
				return ret;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int ath10k_wmi_tlv_op_pull_swba_ev(struct ath10k *ar,
					  struct sk_buff *skb,
					  struct wmi_swba_ev_arg *arg)
{
	struct wmi_tlv_swba_parse swba = { .arg = arg };
	u32 map;
	size_t n_vdevs;
	int ret;

	ret = ath10k_wmi_tlv_iter(ar, skb->data, skb->len,
				  ath10k_wmi_tlv_swba_parse, &swba);
	if (ret) {
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	if (!swba.ev)
		return -EPROTO;

	arg->vdev_map = swba.ev->vdev_map;

	for (map = __le32_to_cpu(arg->vdev_map), n_vdevs = 0; map; map >>= 1)
		if (map & BIT(0))
			n_vdevs++;

	if (n_vdevs != swba.n_tim ||
	    n_vdevs != swba.n_noa)
		return -EPROTO;

	return 0;
}

static int ath10k_wmi_tlv_op_pull_phyerr_ev(struct ath10k *ar,
					    struct sk_buff *skb,
					    struct wmi_phyerr_ev_arg *arg)
{
	const void **tb;
	const struct wmi_tlv_phyerr_ev *ev;
	const void *phyerrs;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_COMB_PHYERR_RX_HDR];
	phyerrs = tb[WMI_TLV_TAG_ARRAY_BYTE];

	if (!ev || !phyerrs) {
		kfree(tb);
		return -EPROTO;
	}

	arg->num_phyerrs  = ev->num_phyerrs;
	arg->tsf_l32 = ev->tsf_l32;
	arg->tsf_u32 = ev->tsf_u32;
	arg->buf_len = ev->buf_len;
	arg->phyerrs = phyerrs;

	kfree(tb);
	return 0;
}

#define WMI_TLV_ABI_VER_NS0 0x5F414351
#define WMI_TLV_ABI_VER_NS1 0x00004C4D
#define WMI_TLV_ABI_VER_NS2 0x00000000
#define WMI_TLV_ABI_VER_NS3 0x00000000

#define WMI_TLV_ABI_VER0_MAJOR 1
#define WMI_TLV_ABI_VER0_MINOR 0
#define WMI_TLV_ABI_VER0 ((((WMI_TLV_ABI_VER0_MAJOR) << 24) & 0xFF000000) | \
			  (((WMI_TLV_ABI_VER0_MINOR) <<  0) & 0x00FFFFFF))
#define WMI_TLV_ABI_VER1 53

static int
ath10k_wmi_tlv_parse_mem_reqs(struct ath10k *ar, u16 tag, u16 len,
			      const void *ptr, void *data)
{
	struct wmi_svc_rdy_ev_arg *arg = data;
	int i;

	if (tag != WMI_TLV_TAG_STRUCT_WLAN_HOST_MEM_REQ)
		return -EPROTO;

	for (i = 0; i < ARRAY_SIZE(arg->mem_reqs); i++) {
		if (!arg->mem_reqs[i]) {
			arg->mem_reqs[i] = ptr;
			return 0;
		}
	}

	return -ENOMEM;
}

static int ath10k_wmi_tlv_op_pull_svc_rdy_ev(struct ath10k *ar,
					     struct sk_buff *skb,
					     struct wmi_svc_rdy_ev_arg *arg)
{
	const void **tb;
	const struct hal_reg_capabilities *reg;
	const struct wmi_tlv_svc_rdy_ev *ev;
	const __le32 *svc_bmap;
	const struct wlan_host_mem_req *mem_reqs;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_SERVICE_READY_EVENT];
	reg = tb[WMI_TLV_TAG_STRUCT_HAL_REG_CAPABILITIES];
	svc_bmap = tb[WMI_TLV_TAG_ARRAY_UINT32];
	mem_reqs = tb[WMI_TLV_TAG_ARRAY_STRUCT];

	if (!ev || !reg || !svc_bmap || !mem_reqs) {
		kfree(tb);
		return -EPROTO;
	}

	/* This is an internal ABI compatibility check for WMI TLV so check it
	 * here instead of the generic WMI code.
	 */
	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi tlv abi 0x%08x ?= 0x%08x, 0x%08x ?= 0x%08x, 0x%08x ?= 0x%08x, 0x%08x ?= 0x%08x, 0x%08x ?= 0x%08x\n",
		   __le32_to_cpu(ev->abi.abi_ver0), WMI_TLV_ABI_VER0,
		   __le32_to_cpu(ev->abi.abi_ver_ns0), WMI_TLV_ABI_VER_NS0,
		   __le32_to_cpu(ev->abi.abi_ver_ns1), WMI_TLV_ABI_VER_NS1,
		   __le32_to_cpu(ev->abi.abi_ver_ns2), WMI_TLV_ABI_VER_NS2,
		   __le32_to_cpu(ev->abi.abi_ver_ns3), WMI_TLV_ABI_VER_NS3);

	if (__le32_to_cpu(ev->abi.abi_ver0) != WMI_TLV_ABI_VER0 ||
	    __le32_to_cpu(ev->abi.abi_ver_ns0) != WMI_TLV_ABI_VER_NS0 ||
	    __le32_to_cpu(ev->abi.abi_ver_ns1) != WMI_TLV_ABI_VER_NS1 ||
	    __le32_to_cpu(ev->abi.abi_ver_ns2) != WMI_TLV_ABI_VER_NS2 ||
	    __le32_to_cpu(ev->abi.abi_ver_ns3) != WMI_TLV_ABI_VER_NS3) {
		kfree(tb);
		return -ENOTSUPP;
	}

	arg->min_tx_power = ev->hw_min_tx_power;
	arg->max_tx_power = ev->hw_max_tx_power;
	arg->ht_cap = ev->ht_cap_info;
	arg->vht_cap = ev->vht_cap_info;
	arg->sw_ver0 = ev->abi.abi_ver0;
	arg->sw_ver1 = ev->abi.abi_ver1;
	arg->fw_build = ev->fw_build_vers;
	arg->phy_capab = ev->phy_capability;
	arg->num_rf_chains = ev->num_rf_chains;
	arg->eeprom_rd = reg->eeprom_rd;
	arg->num_mem_reqs = ev->num_mem_reqs;
	arg->service_map = svc_bmap;
	arg->service_map_len = ath10k_wmi_tlv_len(svc_bmap);

	ret = ath10k_wmi_tlv_iter(ar, mem_reqs, ath10k_wmi_tlv_len(mem_reqs),
				  ath10k_wmi_tlv_parse_mem_reqs, arg);
	if (ret) {
		kfree(tb);
		ath10k_warn(ar, "failed to parse mem_reqs tlv: %d\n", ret);
		return ret;
	}

	kfree(tb);
	return 0;
}

static int ath10k_wmi_tlv_op_pull_rdy_ev(struct ath10k *ar,
					 struct sk_buff *skb,
					 struct wmi_rdy_ev_arg *arg)
{
	const void **tb;
	const struct wmi_tlv_rdy_ev *ev;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_READY_EVENT];
	if (!ev) {
		kfree(tb);
		return -EPROTO;
	}

	arg->sw_version = ev->abi.abi_ver0;
	arg->abi_version = ev->abi.abi_ver1;
	arg->status = ev->status;
	arg->mac_addr = ev->mac_addr.addr;

	kfree(tb);
	return 0;
}

static int ath10k_wmi_tlv_op_pull_fw_stats(struct ath10k *ar,
					   struct sk_buff *skb,
					   struct ath10k_fw_stats *stats)
{
	const void **tb;
	const struct wmi_stats_event *ev;
	const void *data;
	u32 num_pdev_stats, num_vdev_stats, num_peer_stats;
	size_t data_len;
	int ret;

	tb = ath10k_wmi_tlv_parse_alloc(ar, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath10k_warn(ar, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TLV_TAG_STRUCT_STATS_EVENT];
	data = tb[WMI_TLV_TAG_ARRAY_BYTE];

	if (!ev || !data) {
		kfree(tb);
		return -EPROTO;
	}

	data_len = ath10k_wmi_tlv_len(data);
	num_pdev_stats = __le32_to_cpu(ev->num_pdev_stats);
	num_vdev_stats = __le32_to_cpu(ev->num_vdev_stats);
	num_peer_stats = __le32_to_cpu(ev->num_peer_stats);

	WARN_ON(1); /* FIXME: not implemented yet */

	kfree(tb);
	return 0;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_pdev_suspend(struct ath10k *ar, u32 opt)
{
	struct wmi_tlv_pdev_suspend *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PDEV_SUSPEND_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->opt = __cpu_to_le32(opt);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv pdev suspend\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_pdev_resume(struct ath10k *ar)
{
	struct wmi_tlv_resume_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PDEV_RESUME_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->reserved = __cpu_to_le32(0);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv pdev resume\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_pdev_set_rd(struct ath10k *ar,
				  u16 rd, u16 rd2g, u16 rd5g,
				  u16 ctl2g, u16 ctl5g,
				  enum wmi_dfs_region dfs_reg)
{
	struct wmi_tlv_pdev_set_rd_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PDEV_SET_REGDOMAIN_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->regd = __cpu_to_le32(rd);
	cmd->regd_2ghz = __cpu_to_le32(rd2g);
	cmd->regd_5ghz = __cpu_to_le32(rd5g);
	cmd->conform_limit_2ghz = __cpu_to_le32(rd2g);
	cmd->conform_limit_5ghz = __cpu_to_le32(rd5g);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv pdev set rd\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_pdev_set_param(struct ath10k *ar, u32 param_id,
				     u32 param_value)
{
	struct wmi_tlv_pdev_set_param_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PDEV_SET_PARAM_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->param_id = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(param_value);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv pdev set param\n");
	return skb;
}

static struct sk_buff *ath10k_wmi_tlv_op_gen_init(struct ath10k *ar)
{
	struct sk_buff *skb;
	struct wmi_tlv *tlv;
	struct wmi_tlv_init_cmd *cmd;
	struct wmi_tlv_resource_config *cfg;
	struct wmi_host_mem_chunks *chunks;
	size_t len, chunks_len;
	void *ptr;

	chunks_len = ar->wmi.num_mem_chunks * sizeof(struct host_memory_chunk);
	len = (sizeof(*tlv) + sizeof(*cmd)) +
	      (sizeof(*tlv) + sizeof(*cfg)) +
	      (sizeof(*tlv) + chunks_len);

	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = skb->data;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_INIT_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_RESOURCE_CONFIG);
	tlv->len = __cpu_to_le16(sizeof(*cfg));
	cfg = (void *)tlv->value;
	ptr += sizeof(*tlv);
	ptr += sizeof(*cfg);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_STRUCT);
	tlv->len = __cpu_to_le16(chunks_len);
	chunks = (void *)tlv->value;

	ptr += sizeof(*tlv);
	ptr += chunks_len;

	cmd->abi.abi_ver0 = __cpu_to_le32(WMI_TLV_ABI_VER0);
	cmd->abi.abi_ver1 = __cpu_to_le32(WMI_TLV_ABI_VER1);
	cmd->abi.abi_ver_ns0 = __cpu_to_le32(WMI_TLV_ABI_VER_NS0);
	cmd->abi.abi_ver_ns1 = __cpu_to_le32(WMI_TLV_ABI_VER_NS1);
	cmd->abi.abi_ver_ns2 = __cpu_to_le32(WMI_TLV_ABI_VER_NS2);
	cmd->abi.abi_ver_ns3 = __cpu_to_le32(WMI_TLV_ABI_VER_NS3);
	cmd->num_host_mem_chunks = __cpu_to_le32(ar->wmi.num_mem_chunks);

	cfg->num_vdevs = __cpu_to_le32(TARGET_TLV_NUM_VDEVS);
	cfg->num_peers = __cpu_to_le32(TARGET_TLV_NUM_PEERS);
	cfg->num_offload_peers = __cpu_to_le32(0);
	cfg->num_offload_reorder_bufs = __cpu_to_le32(0);
	cfg->num_peer_keys = __cpu_to_le32(2);
	cfg->num_tids = __cpu_to_le32(TARGET_TLV_NUM_TIDS);
	cfg->ast_skid_limit = __cpu_to_le32(0x10);
	cfg->tx_chain_mask = __cpu_to_le32(0x7);
	cfg->rx_chain_mask = __cpu_to_le32(0x7);
	cfg->rx_timeout_pri[0] = __cpu_to_le32(0x64);
	cfg->rx_timeout_pri[1] = __cpu_to_le32(0x64);
	cfg->rx_timeout_pri[2] = __cpu_to_le32(0x64);
	cfg->rx_timeout_pri[3] = __cpu_to_le32(0x28);
	cfg->rx_decap_mode = __cpu_to_le32(1);
	cfg->scan_max_pending_reqs = __cpu_to_le32(4);
	cfg->bmiss_offload_max_vdev = __cpu_to_le32(3);
	cfg->roam_offload_max_vdev = __cpu_to_le32(3);
	cfg->roam_offload_max_ap_profiles = __cpu_to_le32(8);
	cfg->num_mcast_groups = __cpu_to_le32(0);
	cfg->num_mcast_table_elems = __cpu_to_le32(0);
	cfg->mcast2ucast_mode = __cpu_to_le32(0);
	cfg->tx_dbg_log_size = __cpu_to_le32(0x400);
	cfg->num_wds_entries = __cpu_to_le32(0x20);
	cfg->dma_burst_size = __cpu_to_le32(0);
	cfg->mac_aggr_delim = __cpu_to_le32(0);
	cfg->rx_skip_defrag_timeout_dup_detection_check = __cpu_to_le32(0);
	cfg->vow_config = __cpu_to_le32(0);
	cfg->gtk_offload_max_vdev = __cpu_to_le32(2);
	cfg->num_msdu_desc = __cpu_to_le32(TARGET_TLV_NUM_MSDU_DESC);
	cfg->max_frag_entries = __cpu_to_le32(2);
	cfg->num_tdls_vdevs = __cpu_to_le32(1);
	cfg->num_tdls_conn_table_entries = __cpu_to_le32(0x20);
	cfg->beacon_tx_offload_max_vdev = __cpu_to_le32(2);
	cfg->num_multicast_filter_entries = __cpu_to_le32(5);
	cfg->num_wow_filters = __cpu_to_le32(0x16);
	cfg->num_keep_alive_pattern = __cpu_to_le32(6);
	cfg->keep_alive_pattern_size = __cpu_to_le32(0);
	cfg->max_tdls_concurrent_sleep_sta = __cpu_to_le32(1);
	cfg->max_tdls_concurrent_buffer_sta = __cpu_to_le32(1);

	ath10k_wmi_put_host_mem_chunks(ar, chunks);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv init\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_start_scan(struct ath10k *ar,
				 const struct wmi_start_scan_arg *arg)
{
	struct wmi_tlv_start_scan_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t len, chan_len, ssid_len, bssid_len, ie_len;
	__le32 *chans;
	struct wmi_ssid *ssids;
	struct wmi_mac_addr *addrs;
	void *ptr;
	int i, ret;

	ret = ath10k_wmi_start_scan_verify(arg);
	if (ret)
		return ERR_PTR(ret);

	chan_len = arg->n_channels * sizeof(__le32);
	ssid_len = arg->n_ssids * sizeof(struct wmi_ssid);
	bssid_len = arg->n_bssids * sizeof(struct wmi_mac_addr);
	ie_len = roundup(arg->ie_len, 4);
	len = (sizeof(*tlv) + sizeof(*cmd)) +
	      (arg->n_channels ? sizeof(*tlv) + chan_len : 0) +
	      (arg->n_ssids ? sizeof(*tlv) + ssid_len : 0) +
	      (arg->n_bssids ? sizeof(*tlv) + bssid_len : 0) +
	      (arg->ie_len ? sizeof(*tlv) + ie_len : 0);

	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;
	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_START_SCAN_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;

	ath10k_wmi_put_start_scan_common(&cmd->common, arg);
	cmd->burst_duration_ms = __cpu_to_le32(0);
	cmd->num_channels = __cpu_to_le32(arg->n_channels);
	cmd->num_ssids = __cpu_to_le32(arg->n_ssids);
	cmd->num_bssids = __cpu_to_le32(arg->n_bssids);
	cmd->ie_len = __cpu_to_le32(arg->ie_len);
	cmd->num_probes = __cpu_to_le32(3);

	/* FIXME: There are some scan flag inconsistencies across firmwares,
	 * e.g. WMI-TLV inverts the logic behind the following flag.
	 */
	cmd->common.scan_ctrl_flags ^= __cpu_to_le32(WMI_SCAN_FILTER_PROBE_REQ);

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_UINT32);
	tlv->len = __cpu_to_le16(chan_len);
	chans = (void *)tlv->value;
	for (i = 0; i < arg->n_channels; i++)
		chans[i] = __cpu_to_le32(arg->channels[i]);

	ptr += sizeof(*tlv);
	ptr += chan_len;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_FIXED_STRUCT);
	tlv->len = __cpu_to_le16(ssid_len);
	ssids = (void *)tlv->value;
	for (i = 0; i < arg->n_ssids; i++) {
		ssids[i].ssid_len = __cpu_to_le32(arg->ssids[i].len);
		memcpy(ssids[i].ssid, arg->ssids[i].ssid, arg->ssids[i].len);
	}

	ptr += sizeof(*tlv);
	ptr += ssid_len;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_FIXED_STRUCT);
	tlv->len = __cpu_to_le16(bssid_len);
	addrs = (void *)tlv->value;
	for (i = 0; i < arg->n_bssids; i++)
		ether_addr_copy(addrs[i].addr, arg->bssids[i].bssid);

	ptr += sizeof(*tlv);
	ptr += bssid_len;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_BYTE);
	tlv->len = __cpu_to_le16(ie_len);
	memcpy(tlv->value, arg->ie, arg->ie_len);

	ptr += sizeof(*tlv);
	ptr += ie_len;

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv start scan\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_stop_scan(struct ath10k *ar,
				const struct wmi_stop_scan_arg *arg)
{
	struct wmi_stop_scan_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	u32 scan_id;
	u32 req_id;

	if (arg->req_id > 0xFFF)
		return ERR_PTR(-EINVAL);
	if (arg->req_type == WMI_SCAN_STOP_ONE && arg->u.scan_id > 0xFFF)
		return ERR_PTR(-EINVAL);

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	scan_id = arg->u.scan_id;
	scan_id |= WMI_HOST_SCAN_REQ_ID_PREFIX;

	req_id = arg->req_id;
	req_id |= WMI_HOST_SCAN_REQUESTOR_ID_PREFIX;

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_STOP_SCAN_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->req_type = __cpu_to_le32(arg->req_type);
	cmd->vdev_id = __cpu_to_le32(arg->u.vdev_id);
	cmd->scan_id = __cpu_to_le32(scan_id);
	cmd->scan_req_id = __cpu_to_le32(req_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv stop scan\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_create(struct ath10k *ar,
				  u32 vdev_id,
				  enum wmi_vdev_type vdev_type,
				  enum wmi_vdev_subtype vdev_subtype,
				  const u8 mac_addr[ETH_ALEN])
{
	struct wmi_vdev_create_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_CREATE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->vdev_type = __cpu_to_le32(vdev_type);
	cmd->vdev_subtype = __cpu_to_le32(vdev_subtype);
	ether_addr_copy(cmd->vdev_macaddr.addr, mac_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev create\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_delete(struct ath10k *ar, u32 vdev_id)
{
	struct wmi_vdev_delete_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_DELETE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev delete\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_start(struct ath10k *ar,
				 const struct wmi_vdev_start_request_arg *arg,
				 bool restart)
{
	struct wmi_tlv_vdev_start_cmd *cmd;
	struct wmi_channel *ch;
	struct wmi_p2p_noa_descriptor *noa;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t len;
	void *ptr;
	u32 flags = 0;

	if (WARN_ON(arg->ssid && arg->ssid_len == 0))
		return ERR_PTR(-EINVAL);
	if (WARN_ON(arg->hidden_ssid && !arg->ssid))
		return ERR_PTR(-EINVAL);
	if (WARN_ON(arg->ssid_len > sizeof(cmd->ssid.ssid)))
		return ERR_PTR(-EINVAL);

	len = (sizeof(*tlv) + sizeof(*cmd)) +
	      (sizeof(*tlv) + sizeof(*ch)) +
	      (sizeof(*tlv) + 0);
	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	if (arg->hidden_ssid)
		flags |= WMI_VDEV_START_HIDDEN_SSID;
	if (arg->pmf_enabled)
		flags |= WMI_VDEV_START_PMF_ENABLED;

	ptr = (void *)skb->data;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_START_REQUEST_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(arg->vdev_id);
	cmd->bcn_intval = __cpu_to_le32(arg->bcn_intval);
	cmd->dtim_period = __cpu_to_le32(arg->dtim_period);
	cmd->flags = __cpu_to_le32(flags);
	cmd->bcn_tx_rate = __cpu_to_le32(arg->bcn_tx_rate);
	cmd->bcn_tx_power = __cpu_to_le32(arg->bcn_tx_power);
	cmd->disable_hw_ack = __cpu_to_le32(arg->disable_hw_ack);

	if (arg->ssid) {
		cmd->ssid.ssid_len = __cpu_to_le32(arg->ssid_len);
		memcpy(cmd->ssid.ssid, arg->ssid, arg->ssid_len);
	}

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_CHANNEL);
	tlv->len = __cpu_to_le16(sizeof(*ch));
	ch = (void *)tlv->value;
	ath10k_wmi_put_wmi_channel(ch, &arg->channel);

	ptr += sizeof(*tlv);
	ptr += sizeof(*ch);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_STRUCT);
	tlv->len = 0;
	noa = (void *)tlv->value;

	/* Note: This is a nested TLV containing:
	 * [wmi_tlv][wmi_p2p_noa_descriptor][wmi_tlv]..
	 */

	ptr += sizeof(*tlv);
	ptr += 0;

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev start\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_stop(struct ath10k *ar, u32 vdev_id)
{
	struct wmi_vdev_stop_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_STOP_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev stop\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_up(struct ath10k *ar, u32 vdev_id, u32 aid,
			      const u8 *bssid)

{
	struct wmi_vdev_up_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_UP_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->vdev_assoc_id = __cpu_to_le32(aid);
	ether_addr_copy(cmd->vdev_bssid.addr, bssid);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev up\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_down(struct ath10k *ar, u32 vdev_id)
{
	struct wmi_vdev_down_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_DOWN_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev down\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_set_param(struct ath10k *ar, u32 vdev_id,
				     u32 param_id, u32 param_value)
{
	struct wmi_vdev_set_param_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_SET_PARAM_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->param_id = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(param_value);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev set param\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_vdev_install_key(struct ath10k *ar,
				       const struct wmi_vdev_install_key_arg *arg)
{
	struct wmi_vdev_install_key_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t len;
	void *ptr;

	if (arg->key_cipher == WMI_CIPHER_NONE && arg->key_data != NULL)
		return ERR_PTR(-EINVAL);
	if (arg->key_cipher != WMI_CIPHER_NONE && arg->key_data == NULL)
		return ERR_PTR(-EINVAL);

	len = sizeof(*tlv) + sizeof(*cmd) +
	      sizeof(*tlv) + roundup(arg->key_len, sizeof(__le32));
	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;
	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VDEV_INSTALL_KEY_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(arg->vdev_id);
	cmd->key_idx = __cpu_to_le32(arg->key_idx);
	cmd->key_flags = __cpu_to_le32(arg->key_flags);
	cmd->key_cipher = __cpu_to_le32(arg->key_cipher);
	cmd->key_len = __cpu_to_le32(arg->key_len);
	cmd->key_txmic_len = __cpu_to_le32(arg->key_txmic_len);
	cmd->key_rxmic_len = __cpu_to_le32(arg->key_rxmic_len);

	if (arg->macaddr)
		ether_addr_copy(cmd->peer_macaddr.addr, arg->macaddr);

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_BYTE);
	tlv->len = __cpu_to_le16(roundup(arg->key_len, sizeof(__le32)));
	if (arg->key_data)
		memcpy(tlv->value, arg->key_data, arg->key_len);

	ptr += sizeof(*tlv);
	ptr += roundup(arg->key_len, sizeof(__le32));

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv vdev install key\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_peer_create(struct ath10k *ar, u32 vdev_id,
				  const u8 peer_addr[ETH_ALEN])
{
	struct wmi_tlv_peer_create_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PEER_CREATE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->peer_type = __cpu_to_le32(WMI_TLV_PEER_TYPE_DEFAULT); /* FIXME */
	ether_addr_copy(cmd->peer_addr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv peer create\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_peer_delete(struct ath10k *ar, u32 vdev_id,
				  const u8 peer_addr[ETH_ALEN])
{
	struct wmi_peer_delete_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PEER_DELETE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv peer delete\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_peer_flush(struct ath10k *ar, u32 vdev_id,
				 const u8 peer_addr[ETH_ALEN], u32 tid_bitmap)
{
	struct wmi_peer_flush_tids_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PEER_FLUSH_TIDS_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->peer_tid_bitmap = __cpu_to_le32(tid_bitmap);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv peer flush\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_peer_set_param(struct ath10k *ar, u32 vdev_id,
				     const u8 *peer_addr,
				     enum wmi_peer_param param_id,
				     u32 param_value)
{
	struct wmi_peer_set_param_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PEER_SET_PARAM_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->param_id = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(param_value);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv peer set param\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_peer_assoc(struct ath10k *ar,
				 const struct wmi_peer_assoc_complete_arg *arg)
{
	struct wmi_tlv_peer_assoc_cmd *cmd;
	struct wmi_vht_rate_set *vht_rate;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t len, legacy_rate_len, ht_rate_len;
	void *ptr;

	if (arg->peer_mpdu_density > 16)
		return ERR_PTR(-EINVAL);
	if (arg->peer_legacy_rates.num_rates > MAX_SUPPORTED_RATES)
		return ERR_PTR(-EINVAL);
	if (arg->peer_ht_rates.num_rates > MAX_SUPPORTED_RATES)
		return ERR_PTR(-EINVAL);

	legacy_rate_len = roundup(arg->peer_legacy_rates.num_rates,
				  sizeof(__le32));
	ht_rate_len = roundup(arg->peer_ht_rates.num_rates, sizeof(__le32));
	len = (sizeof(*tlv) + sizeof(*cmd)) +
	      (sizeof(*tlv) + legacy_rate_len) +
	      (sizeof(*tlv) + ht_rate_len) +
	      (sizeof(*tlv) + sizeof(*vht_rate));
	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;
	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PEER_ASSOC_COMPLETE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;

	cmd->vdev_id = __cpu_to_le32(arg->vdev_id);
	cmd->new_assoc = __cpu_to_le32(arg->peer_reassoc ? 0 : 1);
	cmd->assoc_id = __cpu_to_le32(arg->peer_aid);
	cmd->flags = __cpu_to_le32(arg->peer_flags);
	cmd->caps = __cpu_to_le32(arg->peer_caps);
	cmd->listen_intval = __cpu_to_le32(arg->peer_listen_intval);
	cmd->ht_caps = __cpu_to_le32(arg->peer_ht_caps);
	cmd->max_mpdu = __cpu_to_le32(arg->peer_max_mpdu);
	cmd->mpdu_density = __cpu_to_le32(arg->peer_mpdu_density);
	cmd->rate_caps = __cpu_to_le32(arg->peer_rate_caps);
	cmd->nss = __cpu_to_le32(arg->peer_num_spatial_streams);
	cmd->vht_caps = __cpu_to_le32(arg->peer_vht_caps);
	cmd->phy_mode = __cpu_to_le32(arg->peer_phymode);
	cmd->num_legacy_rates = __cpu_to_le32(arg->peer_legacy_rates.num_rates);
	cmd->num_ht_rates = __cpu_to_le32(arg->peer_ht_rates.num_rates);
	ether_addr_copy(cmd->mac_addr.addr, arg->addr);

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_BYTE);
	tlv->len = __cpu_to_le16(legacy_rate_len);
	memcpy(tlv->value, arg->peer_legacy_rates.rates,
	       arg->peer_legacy_rates.num_rates);

	ptr += sizeof(*tlv);
	ptr += legacy_rate_len;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_BYTE);
	tlv->len = __cpu_to_le16(ht_rate_len);
	memcpy(tlv->value, arg->peer_ht_rates.rates,
	       arg->peer_ht_rates.num_rates);

	ptr += sizeof(*tlv);
	ptr += ht_rate_len;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_VHT_RATE_SET);
	tlv->len = __cpu_to_le16(sizeof(*vht_rate));
	vht_rate = (void *)tlv->value;

	vht_rate->rx_max_rate = __cpu_to_le32(arg->peer_vht_rates.rx_max_rate);
	vht_rate->rx_mcs_set = __cpu_to_le32(arg->peer_vht_rates.rx_mcs_set);
	vht_rate->tx_max_rate = __cpu_to_le32(arg->peer_vht_rates.tx_max_rate);
	vht_rate->tx_mcs_set = __cpu_to_le32(arg->peer_vht_rates.tx_mcs_set);

	ptr += sizeof(*tlv);
	ptr += sizeof(*vht_rate);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv peer assoc\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_set_psmode(struct ath10k *ar, u32 vdev_id,
				 enum wmi_sta_ps_mode psmode)
{
	struct wmi_sta_powersave_mode_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_STA_POWERSAVE_MODE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->sta_ps_mode = __cpu_to_le32(psmode);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv set psmode\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_set_sta_ps(struct ath10k *ar, u32 vdev_id,
				 enum wmi_sta_powersave_param param_id,
				 u32 param_value)
{
	struct wmi_sta_powersave_param_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_STA_POWERSAVE_PARAM_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->param_id = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(param_value);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv set sta ps\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_set_ap_ps(struct ath10k *ar, u32 vdev_id, const u8 *mac,
				enum wmi_ap_ps_peer_param param_id, u32 value)
{
	struct wmi_ap_ps_peer_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	if (!mac)
		return ERR_PTR(-EINVAL);

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_AP_PS_PEER_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->param_id = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(value);
	ether_addr_copy(cmd->peer_macaddr.addr, mac);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv ap ps param\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_scan_chan_list(struct ath10k *ar,
				     const struct wmi_scan_chan_list_arg *arg)
{
	struct wmi_tlv_scan_chan_list_cmd *cmd;
	struct wmi_channel *ci;
	struct wmi_channel_arg *ch;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t chans_len, len;
	int i;
	void *ptr, *chans;

	chans_len = arg->n_channels * (sizeof(*tlv) + sizeof(*ci));
	len = (sizeof(*tlv) + sizeof(*cmd)) +
	      (sizeof(*tlv) + chans_len);

	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;
	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_SCAN_CHAN_LIST_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->num_scan_chans = __cpu_to_le32(arg->n_channels);

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_STRUCT);
	tlv->len = __cpu_to_le16(chans_len);
	chans = (void *)tlv->value;

	for (i = 0; i < arg->n_channels; i++) {
		ch = &arg->channels[i];

		tlv = chans;
		tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_CHANNEL);
		tlv->len = __cpu_to_le16(sizeof(*ci));
		ci = (void *)tlv->value;

		ath10k_wmi_put_wmi_channel(ci, ch);

		chans += sizeof(*tlv);
		chans += sizeof(*ci);
	}

	ptr += sizeof(*tlv);
	ptr += chans_len;

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv scan chan list\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_beacon_dma(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;
	struct wmi_bcn_tx_ref_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	struct sk_buff *beacon = arvif->beacon;
	struct ieee80211_hdr *hdr;
	u16 fc;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	hdr = (struct ieee80211_hdr *)beacon->data;
	fc = le16_to_cpu(hdr->frame_control);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_BCN_SEND_FROM_HOST_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->vdev_id = __cpu_to_le32(arvif->vdev_id);
	cmd->data_len = __cpu_to_le32(beacon->len);
	cmd->data_ptr = __cpu_to_le32(ATH10K_SKB_CB(beacon)->paddr);
	cmd->msdu_id = 0;
	cmd->frame_control = __cpu_to_le32(fc);
	cmd->flags = 0;

	if (ATH10K_SKB_CB(beacon)->bcn.dtim_zero)
		cmd->flags |= __cpu_to_le32(WMI_BCN_TX_REF_FLAG_DTIM_ZERO);

	if (ATH10K_SKB_CB(beacon)->bcn.deliver_cab)
		cmd->flags |= __cpu_to_le32(WMI_BCN_TX_REF_FLAG_DELIVER_CAB);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv beacon dma\n");
	return skb;
}

static void *ath10k_wmi_tlv_put_wmm(void *ptr,
				    const struct wmi_wmm_params_arg *arg)
{
	struct wmi_wmm_params *wmm;
	struct wmi_tlv *tlv;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_WMM_PARAMS);
	tlv->len = __cpu_to_le16(sizeof(*wmm));
	wmm = (void *)tlv->value;
	ath10k_wmi_pdev_set_wmm_param(wmm, arg);

	return ptr + sizeof(*tlv) + sizeof(*wmm);
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_pdev_set_wmm(struct ath10k *ar,
				   const struct wmi_pdev_set_wmm_params_arg *arg)
{
	struct wmi_tlv_pdev_set_wmm_cmd *cmd;
	struct wmi_wmm_params *wmm;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t len;
	void *ptr;

	len = (sizeof(*tlv) + sizeof(*cmd)) +
	      (4 * (sizeof(*tlv) + sizeof(*wmm)));
	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PDEV_SET_WMM_PARAMS_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;

	/* nothing to set here */

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	ptr = ath10k_wmi_tlv_put_wmm(ptr, &arg->ac_be);
	ptr = ath10k_wmi_tlv_put_wmm(ptr, &arg->ac_bk);
	ptr = ath10k_wmi_tlv_put_wmm(ptr, &arg->ac_vi);
	ptr = ath10k_wmi_tlv_put_wmm(ptr, &arg->ac_vo);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv pdev set wmm\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_request_stats(struct ath10k *ar,
				    enum wmi_stats_id stats_id)
{
	struct wmi_request_stats_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_REQUEST_STATS_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->stats_id = __cpu_to_le32(stats_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv request stats\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_force_fw_hang(struct ath10k *ar,
				    enum wmi_force_fw_hang_type type,
				    u32 delay_ms)
{
	struct wmi_force_fw_hang_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*tlv) + sizeof(*cmd));
	if (!skb)
		return ERR_PTR(-ENOMEM);

	tlv = (void *)skb->data;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_FORCE_FW_HANG_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->type = __cpu_to_le32(type);
	cmd->delay_ms = __cpu_to_le32(delay_ms);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv force fw hang\n");
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_dbglog_cfg(struct ath10k *ar, u32 module_enable)
{
	struct wmi_tlv_dbglog_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t len, bmap_len;
	u32 value;
	void *ptr;

	if (module_enable) {
		value = WMI_TLV_DBGLOG_LOG_LEVEL_VALUE(
				module_enable,
				WMI_TLV_DBGLOG_LOG_LEVEL_VERBOSE);
	} else {
		value = WMI_TLV_DBGLOG_LOG_LEVEL_VALUE(
				WMI_TLV_DBGLOG_ALL_MODULES,
				WMI_TLV_DBGLOG_LOG_LEVEL_WARN);
	}

	bmap_len = 0;
	len = sizeof(*tlv) + sizeof(*cmd) + sizeof(*tlv) + bmap_len;
	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_DEBUG_LOG_CONFIG_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->param = __cpu_to_le32(WMI_TLV_DBGLOG_PARAM_LOG_LEVEL);
	cmd->value = __cpu_to_le32(value);

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_ARRAY_UINT32);
	tlv->len = __cpu_to_le16(bmap_len);

	/* nothing to do here */

	ptr += sizeof(*tlv);
	ptr += sizeof(bmap_len);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv dbglog value 0x%08x\n", value);
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_pktlog_enable(struct ath10k *ar, u32 filter)
{
	struct wmi_tlv_pktlog_enable *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	size_t len;

	len = sizeof(*tlv) + sizeof(*cmd);
	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;
	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PDEV_PKTLOG_ENABLE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;
	cmd->filter = __cpu_to_le32(filter);

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv pktlog enable filter 0x%08x\n",
		   filter);
	return skb;
}

static struct sk_buff *
ath10k_wmi_tlv_op_gen_pktlog_disable(struct ath10k *ar)
{
	struct wmi_tlv_pktlog_disable *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	size_t len;

	len = sizeof(*tlv) + sizeof(*cmd);
	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ptr = (void *)skb->data;
	tlv = ptr;
	tlv->tag = __cpu_to_le16(WMI_TLV_TAG_STRUCT_PDEV_PKTLOG_DISABLE_CMD);
	tlv->len = __cpu_to_le16(sizeof(*cmd));
	cmd = (void *)tlv->value;

	ptr += sizeof(*tlv);
	ptr += sizeof(*cmd);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi tlv pktlog disable\n");
	return skb;
}

/****************/
/* TLV mappings */
/****************/

static struct wmi_cmd_map wmi_tlv_cmd_map = {
	.init_cmdid = WMI_TLV_INIT_CMDID,
	.start_scan_cmdid = WMI_TLV_START_SCAN_CMDID,
	.stop_scan_cmdid = WMI_TLV_STOP_SCAN_CMDID,
	.scan_chan_list_cmdid = WMI_TLV_SCAN_CHAN_LIST_CMDID,
	.scan_sch_prio_tbl_cmdid = WMI_TLV_SCAN_SCH_PRIO_TBL_CMDID,
	.pdev_set_regdomain_cmdid = WMI_TLV_PDEV_SET_REGDOMAIN_CMDID,
	.pdev_set_channel_cmdid = WMI_TLV_PDEV_SET_CHANNEL_CMDID,
	.pdev_set_param_cmdid = WMI_TLV_PDEV_SET_PARAM_CMDID,
	.pdev_pktlog_enable_cmdid = WMI_TLV_PDEV_PKTLOG_ENABLE_CMDID,
	.pdev_pktlog_disable_cmdid = WMI_TLV_PDEV_PKTLOG_DISABLE_CMDID,
	.pdev_set_wmm_params_cmdid = WMI_TLV_PDEV_SET_WMM_PARAMS_CMDID,
	.pdev_set_ht_cap_ie_cmdid = WMI_TLV_PDEV_SET_HT_CAP_IE_CMDID,
	.pdev_set_vht_cap_ie_cmdid = WMI_TLV_PDEV_SET_VHT_CAP_IE_CMDID,
	.pdev_set_dscp_tid_map_cmdid = WMI_TLV_PDEV_SET_DSCP_TID_MAP_CMDID,
	.pdev_set_quiet_mode_cmdid = WMI_TLV_PDEV_SET_QUIET_MODE_CMDID,
	.pdev_green_ap_ps_enable_cmdid = WMI_TLV_PDEV_GREEN_AP_PS_ENABLE_CMDID,
	.pdev_get_tpc_config_cmdid = WMI_TLV_PDEV_GET_TPC_CONFIG_CMDID,
	.pdev_set_base_macaddr_cmdid = WMI_TLV_PDEV_SET_BASE_MACADDR_CMDID,
	.vdev_create_cmdid = WMI_TLV_VDEV_CREATE_CMDID,
	.vdev_delete_cmdid = WMI_TLV_VDEV_DELETE_CMDID,
	.vdev_start_request_cmdid = WMI_TLV_VDEV_START_REQUEST_CMDID,
	.vdev_restart_request_cmdid = WMI_TLV_VDEV_RESTART_REQUEST_CMDID,
	.vdev_up_cmdid = WMI_TLV_VDEV_UP_CMDID,
	.vdev_stop_cmdid = WMI_TLV_VDEV_STOP_CMDID,
	.vdev_down_cmdid = WMI_TLV_VDEV_DOWN_CMDID,
	.vdev_set_param_cmdid = WMI_TLV_VDEV_SET_PARAM_CMDID,
	.vdev_install_key_cmdid = WMI_TLV_VDEV_INSTALL_KEY_CMDID,
	.peer_create_cmdid = WMI_TLV_PEER_CREATE_CMDID,
	.peer_delete_cmdid = WMI_TLV_PEER_DELETE_CMDID,
	.peer_flush_tids_cmdid = WMI_TLV_PEER_FLUSH_TIDS_CMDID,
	.peer_set_param_cmdid = WMI_TLV_PEER_SET_PARAM_CMDID,
	.peer_assoc_cmdid = WMI_TLV_PEER_ASSOC_CMDID,
	.peer_add_wds_entry_cmdid = WMI_TLV_PEER_ADD_WDS_ENTRY_CMDID,
	.peer_remove_wds_entry_cmdid = WMI_TLV_PEER_REMOVE_WDS_ENTRY_CMDID,
	.peer_mcast_group_cmdid = WMI_TLV_PEER_MCAST_GROUP_CMDID,
	.bcn_tx_cmdid = WMI_TLV_BCN_TX_CMDID,
	.pdev_send_bcn_cmdid = WMI_TLV_PDEV_SEND_BCN_CMDID,
	.bcn_tmpl_cmdid = WMI_TLV_BCN_TMPL_CMDID,
	.bcn_filter_rx_cmdid = WMI_TLV_BCN_FILTER_RX_CMDID,
	.prb_req_filter_rx_cmdid = WMI_TLV_PRB_REQ_FILTER_RX_CMDID,
	.mgmt_tx_cmdid = WMI_TLV_MGMT_TX_CMDID,
	.prb_tmpl_cmdid = WMI_TLV_PRB_TMPL_CMDID,
	.addba_clear_resp_cmdid = WMI_TLV_ADDBA_CLEAR_RESP_CMDID,
	.addba_send_cmdid = WMI_TLV_ADDBA_SEND_CMDID,
	.addba_status_cmdid = WMI_TLV_ADDBA_STATUS_CMDID,
	.delba_send_cmdid = WMI_TLV_DELBA_SEND_CMDID,
	.addba_set_resp_cmdid = WMI_TLV_ADDBA_SET_RESP_CMDID,
	.send_singleamsdu_cmdid = WMI_TLV_SEND_SINGLEAMSDU_CMDID,
	.sta_powersave_mode_cmdid = WMI_TLV_STA_POWERSAVE_MODE_CMDID,
	.sta_powersave_param_cmdid = WMI_TLV_STA_POWERSAVE_PARAM_CMDID,
	.sta_mimo_ps_mode_cmdid = WMI_TLV_STA_MIMO_PS_MODE_CMDID,
	.pdev_dfs_enable_cmdid = WMI_TLV_PDEV_DFS_ENABLE_CMDID,
	.pdev_dfs_disable_cmdid = WMI_TLV_PDEV_DFS_DISABLE_CMDID,
	.roam_scan_mode = WMI_TLV_ROAM_SCAN_MODE,
	.roam_scan_rssi_threshold = WMI_TLV_ROAM_SCAN_RSSI_THRESHOLD,
	.roam_scan_period = WMI_TLV_ROAM_SCAN_PERIOD,
	.roam_scan_rssi_change_threshold =
				WMI_TLV_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
	.roam_ap_profile = WMI_TLV_ROAM_AP_PROFILE,
	.ofl_scan_add_ap_profile = WMI_TLV_ROAM_AP_PROFILE,
	.ofl_scan_remove_ap_profile = WMI_TLV_OFL_SCAN_REMOVE_AP_PROFILE,
	.ofl_scan_period = WMI_TLV_OFL_SCAN_PERIOD,
	.p2p_dev_set_device_info = WMI_TLV_P2P_DEV_SET_DEVICE_INFO,
	.p2p_dev_set_discoverability = WMI_TLV_P2P_DEV_SET_DISCOVERABILITY,
	.p2p_go_set_beacon_ie = WMI_TLV_P2P_GO_SET_BEACON_IE,
	.p2p_go_set_probe_resp_ie = WMI_TLV_P2P_GO_SET_PROBE_RESP_IE,
	.p2p_set_vendor_ie_data_cmdid = WMI_TLV_P2P_SET_VENDOR_IE_DATA_CMDID,
	.ap_ps_peer_param_cmdid = WMI_TLV_AP_PS_PEER_PARAM_CMDID,
	.ap_ps_peer_uapsd_coex_cmdid = WMI_TLV_AP_PS_PEER_UAPSD_COEX_CMDID,
	.peer_rate_retry_sched_cmdid = WMI_TLV_PEER_RATE_RETRY_SCHED_CMDID,
	.wlan_profile_trigger_cmdid = WMI_TLV_WLAN_PROFILE_TRIGGER_CMDID,
	.wlan_profile_set_hist_intvl_cmdid =
				WMI_TLV_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
	.wlan_profile_get_profile_data_cmdid =
				WMI_TLV_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
	.wlan_profile_enable_profile_id_cmdid =
				WMI_TLV_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
	.wlan_profile_list_profile_id_cmdid =
				WMI_TLV_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
	.pdev_suspend_cmdid = WMI_TLV_PDEV_SUSPEND_CMDID,
	.pdev_resume_cmdid = WMI_TLV_PDEV_RESUME_CMDID,
	.add_bcn_filter_cmdid = WMI_TLV_ADD_BCN_FILTER_CMDID,
	.rmv_bcn_filter_cmdid = WMI_TLV_RMV_BCN_FILTER_CMDID,
	.wow_add_wake_pattern_cmdid = WMI_TLV_WOW_ADD_WAKE_PATTERN_CMDID,
	.wow_del_wake_pattern_cmdid = WMI_TLV_WOW_DEL_WAKE_PATTERN_CMDID,
	.wow_enable_disable_wake_event_cmdid =
				WMI_TLV_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
	.wow_enable_cmdid = WMI_TLV_WOW_ENABLE_CMDID,
	.wow_hostwakeup_from_sleep_cmdid =
				WMI_TLV_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
	.rtt_measreq_cmdid = WMI_TLV_RTT_MEASREQ_CMDID,
	.rtt_tsf_cmdid = WMI_TLV_RTT_TSF_CMDID,
	.vdev_spectral_scan_configure_cmdid = WMI_TLV_SPECTRAL_SCAN_CONF_CMDID,
	.vdev_spectral_scan_enable_cmdid = WMI_TLV_SPECTRAL_SCAN_ENABLE_CMDID,
	.request_stats_cmdid = WMI_TLV_REQUEST_STATS_CMDID,
	.set_arp_ns_offload_cmdid = WMI_TLV_SET_ARP_NS_OFFLOAD_CMDID,
	.network_list_offload_config_cmdid =
				WMI_TLV_NETWORK_LIST_OFFLOAD_CONFIG_CMDID,
	.gtk_offload_cmdid = WMI_TLV_GTK_OFFLOAD_CMDID,
	.csa_offload_enable_cmdid = WMI_TLV_CSA_OFFLOAD_ENABLE_CMDID,
	.csa_offload_chanswitch_cmdid = WMI_TLV_CSA_OFFLOAD_CHANSWITCH_CMDID,
	.chatter_set_mode_cmdid = WMI_TLV_CHATTER_SET_MODE_CMDID,
	.peer_tid_addba_cmdid = WMI_TLV_PEER_TID_ADDBA_CMDID,
	.peer_tid_delba_cmdid = WMI_TLV_PEER_TID_DELBA_CMDID,
	.sta_dtim_ps_method_cmdid = WMI_TLV_STA_DTIM_PS_METHOD_CMDID,
	.sta_uapsd_auto_trig_cmdid = WMI_TLV_STA_UAPSD_AUTO_TRIG_CMDID,
	.sta_keepalive_cmd = WMI_TLV_STA_KEEPALIVE_CMDID,
	.echo_cmdid = WMI_TLV_ECHO_CMDID,
	.pdev_utf_cmdid = WMI_TLV_PDEV_UTF_CMDID,
	.dbglog_cfg_cmdid = WMI_TLV_DBGLOG_CFG_CMDID,
	.pdev_qvit_cmdid = WMI_TLV_PDEV_QVIT_CMDID,
	.pdev_ftm_intg_cmdid = WMI_TLV_PDEV_FTM_INTG_CMDID,
	.vdev_set_keepalive_cmdid = WMI_TLV_VDEV_SET_KEEPALIVE_CMDID,
	.vdev_get_keepalive_cmdid = WMI_TLV_VDEV_GET_KEEPALIVE_CMDID,
	.force_fw_hang_cmdid = WMI_TLV_FORCE_FW_HANG_CMDID,
	.gpio_config_cmdid = WMI_TLV_GPIO_CONFIG_CMDID,
	.gpio_output_cmdid = WMI_TLV_GPIO_OUTPUT_CMDID,
};

static struct wmi_pdev_param_map wmi_tlv_pdev_param_map = {
	.tx_chain_mask = WMI_TLV_PDEV_PARAM_TX_CHAIN_MASK,
	.rx_chain_mask = WMI_TLV_PDEV_PARAM_RX_CHAIN_MASK,
	.txpower_limit2g = WMI_TLV_PDEV_PARAM_TXPOWER_LIMIT2G,
	.txpower_limit5g = WMI_TLV_PDEV_PARAM_TXPOWER_LIMIT5G,
	.txpower_scale = WMI_TLV_PDEV_PARAM_TXPOWER_SCALE,
	.beacon_gen_mode = WMI_TLV_PDEV_PARAM_BEACON_GEN_MODE,
	.beacon_tx_mode = WMI_TLV_PDEV_PARAM_BEACON_TX_MODE,
	.resmgr_offchan_mode = WMI_TLV_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
	.protection_mode = WMI_TLV_PDEV_PARAM_PROTECTION_MODE,
	.dynamic_bw = WMI_TLV_PDEV_PARAM_DYNAMIC_BW,
	.non_agg_sw_retry_th = WMI_TLV_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
	.agg_sw_retry_th = WMI_TLV_PDEV_PARAM_AGG_SW_RETRY_TH,
	.sta_kickout_th = WMI_TLV_PDEV_PARAM_STA_KICKOUT_TH,
	.ac_aggrsize_scaling = WMI_TLV_PDEV_PARAM_AC_AGGRSIZE_SCALING,
	.ltr_enable = WMI_TLV_PDEV_PARAM_LTR_ENABLE,
	.ltr_ac_latency_be = WMI_TLV_PDEV_PARAM_LTR_AC_LATENCY_BE,
	.ltr_ac_latency_bk = WMI_TLV_PDEV_PARAM_LTR_AC_LATENCY_BK,
	.ltr_ac_latency_vi = WMI_TLV_PDEV_PARAM_LTR_AC_LATENCY_VI,
	.ltr_ac_latency_vo = WMI_TLV_PDEV_PARAM_LTR_AC_LATENCY_VO,
	.ltr_ac_latency_timeout = WMI_TLV_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
	.ltr_sleep_override = WMI_TLV_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
	.ltr_rx_override = WMI_TLV_PDEV_PARAM_LTR_RX_OVERRIDE,
	.ltr_tx_activity_timeout = WMI_TLV_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
	.l1ss_enable = WMI_TLV_PDEV_PARAM_L1SS_ENABLE,
	.dsleep_enable = WMI_TLV_PDEV_PARAM_DSLEEP_ENABLE,
	.pcielp_txbuf_flush = WMI_TLV_PDEV_PARAM_PCIELP_TXBUF_FLUSH,
	.pcielp_txbuf_watermark = WMI_TLV_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
	.pcielp_txbuf_tmo_en = WMI_TLV_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
	.pcielp_txbuf_tmo_value = WMI_TLV_PDEV_PARAM_PCIELP_TXBUF_TMO_VALUE,
	.pdev_stats_update_period = WMI_TLV_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
	.vdev_stats_update_period = WMI_TLV_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
	.peer_stats_update_period = WMI_TLV_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
	.bcnflt_stats_update_period =
				WMI_TLV_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
	.pmf_qos = WMI_TLV_PDEV_PARAM_PMF_QOS,
	.arp_ac_override = WMI_TLV_PDEV_PARAM_ARP_AC_OVERRIDE,
	.dcs = WMI_TLV_PDEV_PARAM_DCS,
	.ani_enable = WMI_TLV_PDEV_PARAM_ANI_ENABLE,
	.ani_poll_period = WMI_TLV_PDEV_PARAM_ANI_POLL_PERIOD,
	.ani_listen_period = WMI_TLV_PDEV_PARAM_ANI_LISTEN_PERIOD,
	.ani_ofdm_level = WMI_TLV_PDEV_PARAM_ANI_OFDM_LEVEL,
	.ani_cck_level = WMI_TLV_PDEV_PARAM_ANI_CCK_LEVEL,
	.dyntxchain = WMI_TLV_PDEV_PARAM_DYNTXCHAIN,
	.proxy_sta = WMI_TLV_PDEV_PARAM_PROXY_STA,
	.idle_ps_config = WMI_TLV_PDEV_PARAM_IDLE_PS_CONFIG,
	.power_gating_sleep = WMI_TLV_PDEV_PARAM_POWER_GATING_SLEEP,
	.fast_channel_reset = WMI_TLV_PDEV_PARAM_UNSUPPORTED,
	.burst_dur = WMI_TLV_PDEV_PARAM_BURST_DUR,
	.burst_enable = WMI_TLV_PDEV_PARAM_BURST_ENABLE,
};

static struct wmi_vdev_param_map wmi_tlv_vdev_param_map = {
	.rts_threshold = WMI_TLV_VDEV_PARAM_RTS_THRESHOLD,
	.fragmentation_threshold = WMI_TLV_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
	.beacon_interval = WMI_TLV_VDEV_PARAM_BEACON_INTERVAL,
	.listen_interval = WMI_TLV_VDEV_PARAM_LISTEN_INTERVAL,
	.multicast_rate = WMI_TLV_VDEV_PARAM_MULTICAST_RATE,
	.mgmt_tx_rate = WMI_TLV_VDEV_PARAM_MGMT_TX_RATE,
	.slot_time = WMI_TLV_VDEV_PARAM_SLOT_TIME,
	.preamble = WMI_TLV_VDEV_PARAM_PREAMBLE,
	.swba_time = WMI_TLV_VDEV_PARAM_SWBA_TIME,
	.wmi_vdev_stats_update_period = WMI_TLV_VDEV_STATS_UPDATE_PERIOD,
	.wmi_vdev_pwrsave_ageout_time = WMI_TLV_VDEV_PWRSAVE_AGEOUT_TIME,
	.wmi_vdev_host_swba_interval = WMI_TLV_VDEV_HOST_SWBA_INTERVAL,
	.dtim_period = WMI_TLV_VDEV_PARAM_DTIM_PERIOD,
	.wmi_vdev_oc_scheduler_air_time_limit =
				WMI_TLV_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
	.wds = WMI_TLV_VDEV_PARAM_WDS,
	.atim_window = WMI_TLV_VDEV_PARAM_ATIM_WINDOW,
	.bmiss_count_max = WMI_TLV_VDEV_PARAM_BMISS_COUNT_MAX,
	.bmiss_first_bcnt = WMI_TLV_VDEV_PARAM_BMISS_FIRST_BCNT,
	.bmiss_final_bcnt = WMI_TLV_VDEV_PARAM_BMISS_FINAL_BCNT,
	.feature_wmm = WMI_TLV_VDEV_PARAM_FEATURE_WMM,
	.chwidth = WMI_TLV_VDEV_PARAM_CHWIDTH,
	.chextoffset = WMI_TLV_VDEV_PARAM_CHEXTOFFSET,
	.disable_htprotection =	WMI_TLV_VDEV_PARAM_DISABLE_HTPROTECTION,
	.sta_quickkickout = WMI_TLV_VDEV_PARAM_STA_QUICKKICKOUT,
	.mgmt_rate = WMI_TLV_VDEV_PARAM_MGMT_RATE,
	.protection_mode = WMI_TLV_VDEV_PARAM_PROTECTION_MODE,
	.fixed_rate = WMI_TLV_VDEV_PARAM_FIXED_RATE,
	.sgi = WMI_TLV_VDEV_PARAM_SGI,
	.ldpc = WMI_TLV_VDEV_PARAM_LDPC,
	.tx_stbc = WMI_TLV_VDEV_PARAM_TX_STBC,
	.rx_stbc = WMI_TLV_VDEV_PARAM_RX_STBC,
	.intra_bss_fwd = WMI_TLV_VDEV_PARAM_INTRA_BSS_FWD,
	.def_keyid = WMI_TLV_VDEV_PARAM_DEF_KEYID,
	.nss = WMI_TLV_VDEV_PARAM_NSS,
	.bcast_data_rate = WMI_TLV_VDEV_PARAM_BCAST_DATA_RATE,
	.mcast_data_rate = WMI_TLV_VDEV_PARAM_MCAST_DATA_RATE,
	.mcast_indicate = WMI_TLV_VDEV_PARAM_MCAST_INDICATE,
	.dhcp_indicate = WMI_TLV_VDEV_PARAM_DHCP_INDICATE,
	.unknown_dest_indicate = WMI_TLV_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
	.ap_keepalive_min_idle_inactive_time_secs =
		WMI_TLV_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
	.ap_keepalive_max_idle_inactive_time_secs =
		WMI_TLV_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
	.ap_keepalive_max_unresponsive_time_secs =
		WMI_TLV_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
	.ap_enable_nawds = WMI_TLV_VDEV_PARAM_AP_ENABLE_NAWDS,
	.mcast2ucast_set = WMI_TLV_VDEV_PARAM_UNSUPPORTED,
	.enable_rtscts = WMI_TLV_VDEV_PARAM_ENABLE_RTSCTS,
	.txbf = WMI_TLV_VDEV_PARAM_TXBF,
	.packet_powersave = WMI_TLV_VDEV_PARAM_PACKET_POWERSAVE,
	.drop_unencry = WMI_TLV_VDEV_PARAM_DROP_UNENCRY,
	.tx_encap_type = WMI_TLV_VDEV_PARAM_TX_ENCAP_TYPE,
	.ap_detect_out_of_sync_sleeping_sta_time_secs =
					WMI_TLV_VDEV_PARAM_UNSUPPORTED,
};

static const struct wmi_ops wmi_tlv_ops = {
	.rx = ath10k_wmi_tlv_op_rx,
	.map_svc = wmi_tlv_svc_map,

	.pull_scan = ath10k_wmi_tlv_op_pull_scan_ev,
	.pull_mgmt_rx = ath10k_wmi_tlv_op_pull_mgmt_rx_ev,
	.pull_ch_info = ath10k_wmi_tlv_op_pull_ch_info_ev,
	.pull_vdev_start = ath10k_wmi_tlv_op_pull_vdev_start_ev,
	.pull_peer_kick = ath10k_wmi_tlv_op_pull_peer_kick_ev,
	.pull_swba = ath10k_wmi_tlv_op_pull_swba_ev,
	.pull_phyerr = ath10k_wmi_tlv_op_pull_phyerr_ev,
	.pull_svc_rdy = ath10k_wmi_tlv_op_pull_svc_rdy_ev,
	.pull_rdy = ath10k_wmi_tlv_op_pull_rdy_ev,
	.pull_fw_stats = ath10k_wmi_tlv_op_pull_fw_stats,

	.gen_pdev_suspend = ath10k_wmi_tlv_op_gen_pdev_suspend,
	.gen_pdev_resume = ath10k_wmi_tlv_op_gen_pdev_resume,
	.gen_pdev_set_rd = ath10k_wmi_tlv_op_gen_pdev_set_rd,
	.gen_pdev_set_param = ath10k_wmi_tlv_op_gen_pdev_set_param,
	.gen_init = ath10k_wmi_tlv_op_gen_init,
	.gen_start_scan = ath10k_wmi_tlv_op_gen_start_scan,
	.gen_stop_scan = ath10k_wmi_tlv_op_gen_stop_scan,
	.gen_vdev_create = ath10k_wmi_tlv_op_gen_vdev_create,
	.gen_vdev_delete = ath10k_wmi_tlv_op_gen_vdev_delete,
	.gen_vdev_start = ath10k_wmi_tlv_op_gen_vdev_start,
	.gen_vdev_stop = ath10k_wmi_tlv_op_gen_vdev_stop,
	.gen_vdev_up = ath10k_wmi_tlv_op_gen_vdev_up,
	.gen_vdev_down = ath10k_wmi_tlv_op_gen_vdev_down,
	.gen_vdev_set_param = ath10k_wmi_tlv_op_gen_vdev_set_param,
	.gen_vdev_install_key = ath10k_wmi_tlv_op_gen_vdev_install_key,
	.gen_peer_create = ath10k_wmi_tlv_op_gen_peer_create,
	.gen_peer_delete = ath10k_wmi_tlv_op_gen_peer_delete,
	.gen_peer_flush = ath10k_wmi_tlv_op_gen_peer_flush,
	.gen_peer_set_param = ath10k_wmi_tlv_op_gen_peer_set_param,
	.gen_peer_assoc = ath10k_wmi_tlv_op_gen_peer_assoc,
	.gen_set_psmode = ath10k_wmi_tlv_op_gen_set_psmode,
	.gen_set_sta_ps = ath10k_wmi_tlv_op_gen_set_sta_ps,
	.gen_set_ap_ps = ath10k_wmi_tlv_op_gen_set_ap_ps,
	.gen_scan_chan_list = ath10k_wmi_tlv_op_gen_scan_chan_list,
	.gen_beacon_dma = ath10k_wmi_tlv_op_gen_beacon_dma,
	.gen_pdev_set_wmm = ath10k_wmi_tlv_op_gen_pdev_set_wmm,
	.gen_request_stats = ath10k_wmi_tlv_op_gen_request_stats,
	.gen_force_fw_hang = ath10k_wmi_tlv_op_gen_force_fw_hang,
	/* .gen_mgmt_tx = not implemented; HTT is used */
	.gen_dbglog_cfg = ath10k_wmi_tlv_op_gen_dbglog_cfg,
	.gen_pktlog_enable = ath10k_wmi_tlv_op_gen_pktlog_enable,
	.gen_pktlog_disable = ath10k_wmi_tlv_op_gen_pktlog_disable,
};

/************/
/* TLV init */
/************/

void ath10k_wmi_tlv_attach(struct ath10k *ar)
{
	ar->wmi.cmd = &wmi_tlv_cmd_map;
	ar->wmi.vdev_param = &wmi_tlv_vdev_param_map;
	ar->wmi.pdev_param = &wmi_tlv_pdev_param_map;
	ar->wmi.ops = &wmi_tlv_ops;
}
