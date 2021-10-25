// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/completion.h>
#include <linux/if_ether.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/uuid.h>
#include <linux/time.h>
#include <linux/of.h>
#include "core.h"
#include "debug.h"
#include "mac.h"
#include "hw.h"
#include "peer.h"

struct wmi_tlv_policy {
	size_t min_len;
};

struct wmi_tlv_svc_ready_parse {
	bool wmi_svc_bitmap_done;
};

struct wmi_tlv_dma_ring_caps_parse {
	struct wmi_dma_ring_capabilities *dma_ring_caps;
	u32 n_dma_ring_caps;
};

struct wmi_tlv_svc_rdy_ext_parse {
	struct ath11k_service_ext_param param;
	struct wmi_soc_mac_phy_hw_mode_caps *hw_caps;
	struct wmi_hw_mode_capabilities *hw_mode_caps;
	u32 n_hw_mode_caps;
	u32 tot_phy_id;
	struct wmi_hw_mode_capabilities pref_hw_mode_caps;
	struct wmi_mac_phy_capabilities *mac_phy_caps;
	u32 n_mac_phy_caps;
	struct wmi_soc_hal_reg_capabilities *soc_hal_reg_caps;
	struct wmi_hal_reg_capabilities_ext *ext_hal_reg_caps;
	u32 n_ext_hal_reg_caps;
	struct wmi_tlv_dma_ring_caps_parse dma_caps_parse;
	bool hw_mode_done;
	bool mac_phy_done;
	bool ext_hal_reg_done;
	bool mac_phy_chainmask_combo_done;
	bool mac_phy_chainmask_cap_done;
	bool oem_dma_ring_cap_done;
	bool dma_ring_cap_done;
};

struct wmi_tlv_svc_rdy_ext2_parse {
	struct wmi_tlv_dma_ring_caps_parse dma_caps_parse;
	bool dma_ring_cap_done;
};

struct wmi_tlv_rdy_parse {
	u32 num_extra_mac_addr;
};

struct wmi_tlv_dma_buf_release_parse {
	struct ath11k_wmi_dma_buf_release_fixed_param fixed;
	struct wmi_dma_buf_release_entry *buf_entry;
	struct wmi_dma_buf_release_meta_data *meta_data;
	u32 num_buf_entry;
	u32 num_meta;
	bool buf_entry_done;
	bool meta_data_done;
};

static const struct wmi_tlv_policy wmi_tlv_policies[] = {
	[WMI_TAG_ARRAY_BYTE]
		= { .min_len = 0 },
	[WMI_TAG_ARRAY_UINT32]
		= { .min_len = 0 },
	[WMI_TAG_SERVICE_READY_EVENT]
		= { .min_len = sizeof(struct wmi_service_ready_event) },
	[WMI_TAG_SERVICE_READY_EXT_EVENT]
		= { .min_len =  sizeof(struct wmi_service_ready_ext_event) },
	[WMI_TAG_SOC_MAC_PHY_HW_MODE_CAPS]
		= { .min_len = sizeof(struct wmi_soc_mac_phy_hw_mode_caps) },
	[WMI_TAG_SOC_HAL_REG_CAPABILITIES]
		= { .min_len = sizeof(struct wmi_soc_hal_reg_capabilities) },
	[WMI_TAG_VDEV_START_RESPONSE_EVENT]
		= { .min_len = sizeof(struct wmi_vdev_start_resp_event) },
	[WMI_TAG_PEER_DELETE_RESP_EVENT]
		= { .min_len = sizeof(struct wmi_peer_delete_resp_event) },
	[WMI_TAG_OFFLOAD_BCN_TX_STATUS_EVENT]
		= { .min_len = sizeof(struct wmi_bcn_tx_status_event) },
	[WMI_TAG_VDEV_STOPPED_EVENT]
		= { .min_len = sizeof(struct wmi_vdev_stopped_event) },
	[WMI_TAG_REG_CHAN_LIST_CC_EVENT]
		= { .min_len = sizeof(struct wmi_reg_chan_list_cc_event) },
	[WMI_TAG_MGMT_RX_HDR]
		= { .min_len = sizeof(struct wmi_mgmt_rx_hdr) },
	[WMI_TAG_MGMT_TX_COMPL_EVENT]
		= { .min_len = sizeof(struct wmi_mgmt_tx_compl_event) },
	[WMI_TAG_SCAN_EVENT]
		= { .min_len = sizeof(struct wmi_scan_event) },
	[WMI_TAG_PEER_STA_KICKOUT_EVENT]
		= { .min_len = sizeof(struct wmi_peer_sta_kickout_event) },
	[WMI_TAG_ROAM_EVENT]
		= { .min_len = sizeof(struct wmi_roam_event) },
	[WMI_TAG_CHAN_INFO_EVENT]
		= { .min_len = sizeof(struct wmi_chan_info_event) },
	[WMI_TAG_PDEV_BSS_CHAN_INFO_EVENT]
		= { .min_len = sizeof(struct wmi_pdev_bss_chan_info_event) },
	[WMI_TAG_VDEV_INSTALL_KEY_COMPLETE_EVENT]
		= { .min_len = sizeof(struct wmi_vdev_install_key_compl_event) },
	[WMI_TAG_READY_EVENT] = {
		.min_len = sizeof(struct wmi_ready_event_min) },
	[WMI_TAG_SERVICE_AVAILABLE_EVENT]
		= {.min_len = sizeof(struct wmi_service_available_event) },
	[WMI_TAG_PEER_ASSOC_CONF_EVENT]
		= { .min_len = sizeof(struct wmi_peer_assoc_conf_event) },
	[WMI_TAG_STATS_EVENT]
		= { .min_len = sizeof(struct wmi_stats_event) },
	[WMI_TAG_PDEV_CTL_FAILSAFE_CHECK_EVENT]
		= { .min_len = sizeof(struct wmi_pdev_ctl_failsafe_chk_event) },
	[WMI_TAG_HOST_SWFDA_EVENT] = {
		.min_len = sizeof(struct wmi_fils_discovery_event) },
	[WMI_TAG_OFFLOAD_PRB_RSP_TX_STATUS_EVENT] = {
		.min_len = sizeof(struct wmi_probe_resp_tx_status_event) },
	[WMI_TAG_VDEV_DELETE_RESP_EVENT] = {
		.min_len = sizeof(struct wmi_vdev_delete_resp_event) },
};

#define PRIMAP(_hw_mode_) \
	[_hw_mode_] = _hw_mode_##_PRI

static const int ath11k_hw_mode_pri_map[] = {
	PRIMAP(WMI_HOST_HW_MODE_SINGLE),
	PRIMAP(WMI_HOST_HW_MODE_DBS),
	PRIMAP(WMI_HOST_HW_MODE_SBS_PASSIVE),
	PRIMAP(WMI_HOST_HW_MODE_SBS),
	PRIMAP(WMI_HOST_HW_MODE_DBS_SBS),
	PRIMAP(WMI_HOST_HW_MODE_DBS_OR_SBS),
	/* keep last */
	PRIMAP(WMI_HOST_HW_MODE_MAX),
};

static int
ath11k_wmi_tlv_iter(struct ath11k_base *ab, const void *ptr, size_t len,
		    int (*iter)(struct ath11k_base *ab, u16 tag, u16 len,
				const void *ptr, void *data),
		    void *data)
{
	const void *begin = ptr;
	const struct wmi_tlv *tlv;
	u16 tlv_tag, tlv_len;
	int ret;

	while (len > 0) {
		if (len < sizeof(*tlv)) {
			ath11k_err(ab, "wmi tlv parse failure at byte %zd (%zu bytes left, %zu expected)\n",
				   ptr - begin, len, sizeof(*tlv));
			return -EINVAL;
		}

		tlv = ptr;
		tlv_tag = FIELD_GET(WMI_TLV_TAG, tlv->header);
		tlv_len = FIELD_GET(WMI_TLV_LEN, tlv->header);
		ptr += sizeof(*tlv);
		len -= sizeof(*tlv);

		if (tlv_len > len) {
			ath11k_err(ab, "wmi tlv parse failure of tag %u at byte %zd (%zu bytes left, %u expected)\n",
				   tlv_tag, ptr - begin, len, tlv_len);
			return -EINVAL;
		}

		if (tlv_tag < ARRAY_SIZE(wmi_tlv_policies) &&
		    wmi_tlv_policies[tlv_tag].min_len &&
		    wmi_tlv_policies[tlv_tag].min_len > tlv_len) {
			ath11k_err(ab, "wmi tlv parse failure of tag %u at byte %zd (%u bytes is less than min length %zu)\n",
				   tlv_tag, ptr - begin, tlv_len,
				   wmi_tlv_policies[tlv_tag].min_len);
			return -EINVAL;
		}

		ret = iter(ab, tlv_tag, tlv_len, ptr, data);
		if (ret)
			return ret;

		ptr += tlv_len;
		len -= tlv_len;
	}

	return 0;
}

static int ath11k_wmi_tlv_iter_parse(struct ath11k_base *ab, u16 tag, u16 len,
				     const void *ptr, void *data)
{
	const void **tb = data;

	if (tag < WMI_TAG_MAX)
		tb[tag] = ptr;

	return 0;
}

static int ath11k_wmi_tlv_parse(struct ath11k_base *ar, const void **tb,
				const void *ptr, size_t len)
{
	return ath11k_wmi_tlv_iter(ar, ptr, len, ath11k_wmi_tlv_iter_parse,
				   (void *)tb);
}

static const void **
ath11k_wmi_tlv_parse_alloc(struct ath11k_base *ab, const void *ptr,
			   size_t len, gfp_t gfp)
{
	const void **tb;
	int ret;

	tb = kcalloc(WMI_TAG_MAX, sizeof(*tb), gfp);
	if (!tb)
		return ERR_PTR(-ENOMEM);

	ret = ath11k_wmi_tlv_parse(ab, tb, ptr, len);
	if (ret) {
		kfree(tb);
		return ERR_PTR(ret);
	}

	return tb;
}

static int ath11k_wmi_cmd_send_nowait(struct ath11k_pdev_wmi *wmi, struct sk_buff *skb,
				      u32 cmd_id)
{
	struct ath11k_skb_cb *skb_cb = ATH11K_SKB_CB(skb);
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_cmd_hdr *cmd_hdr;
	int ret;
	u32 cmd = 0;

	if (skb_push(skb, sizeof(struct wmi_cmd_hdr)) == NULL)
		return -ENOMEM;

	cmd |= FIELD_PREP(WMI_CMD_HDR_CMD_ID, cmd_id);

	cmd_hdr = (struct wmi_cmd_hdr *)skb->data;
	cmd_hdr->cmd_id = cmd;

	memset(skb_cb, 0, sizeof(*skb_cb));
	ret = ath11k_htc_send(&ab->htc, wmi->eid, skb);

	if (ret)
		goto err_pull;

	return 0;

err_pull:
	skb_pull(skb, sizeof(struct wmi_cmd_hdr));
	return ret;
}

int ath11k_wmi_cmd_send(struct ath11k_pdev_wmi *wmi, struct sk_buff *skb,
			u32 cmd_id)
{
	struct ath11k_wmi_base *wmi_sc = wmi->wmi_ab;
	int ret = -EOPNOTSUPP;

	might_sleep();

	wait_event_timeout(wmi_sc->tx_credits_wq, ({
		ret = ath11k_wmi_cmd_send_nowait(wmi, skb, cmd_id);

		if (ret && test_bit(ATH11K_FLAG_CRASH_FLUSH, &wmi_sc->ab->dev_flags))
			ret = -ESHUTDOWN;

		(ret != -EAGAIN);
	}), WMI_SEND_TIMEOUT_HZ);

	if (ret == -EAGAIN)
		ath11k_warn(wmi_sc->ab, "wmi command %d timeout\n", cmd_id);

	return ret;
}

static int ath11k_pull_svc_ready_ext(struct ath11k_pdev_wmi *wmi_handle,
				     const void *ptr,
				     struct ath11k_service_ext_param *param)
{
	const struct wmi_service_ready_ext_event *ev = ptr;

	if (!ev)
		return -EINVAL;

	/* Move this to host based bitmap */
	param->default_conc_scan_config_bits = ev->default_conc_scan_config_bits;
	param->default_fw_config_bits =	ev->default_fw_config_bits;
	param->he_cap_info = ev->he_cap_info;
	param->mpdu_density = ev->mpdu_density;
	param->max_bssid_rx_filters = ev->max_bssid_rx_filters;
	memcpy(&param->ppet, &ev->ppet, sizeof(param->ppet));

	return 0;
}

static int
ath11k_pull_mac_phy_cap_svc_ready_ext(struct ath11k_pdev_wmi *wmi_handle,
				      struct wmi_soc_mac_phy_hw_mode_caps *hw_caps,
				      struct wmi_hw_mode_capabilities *wmi_hw_mode_caps,
				      struct wmi_soc_hal_reg_capabilities *hal_reg_caps,
				      struct wmi_mac_phy_capabilities *wmi_mac_phy_caps,
				      u8 hw_mode_id, u8 phy_id,
				      struct ath11k_pdev *pdev)
{
	struct wmi_mac_phy_capabilities *mac_phy_caps;
	struct ath11k_band_cap *cap_band;
	struct ath11k_pdev_cap *pdev_cap = &pdev->cap;
	u32 phy_map;
	u32 hw_idx, phy_idx = 0;

	if (!hw_caps || !wmi_hw_mode_caps || !hal_reg_caps)
		return -EINVAL;

	for (hw_idx = 0; hw_idx < hw_caps->num_hw_modes; hw_idx++) {
		if (hw_mode_id == wmi_hw_mode_caps[hw_idx].hw_mode_id)
			break;

		phy_map = wmi_hw_mode_caps[hw_idx].phy_id_map;
		while (phy_map) {
			phy_map >>= 1;
			phy_idx++;
		}
	}

	if (hw_idx == hw_caps->num_hw_modes)
		return -EINVAL;

	phy_idx += phy_id;
	if (phy_id >= hal_reg_caps->num_phy)
		return -EINVAL;

	mac_phy_caps = wmi_mac_phy_caps + phy_idx;

	pdev->pdev_id = mac_phy_caps->pdev_id;
	pdev_cap->supported_bands |= mac_phy_caps->supported_bands;
	pdev_cap->ampdu_density = mac_phy_caps->ampdu_density;

	/* Take non-zero tx/rx chainmask. If tx/rx chainmask differs from
	 * band to band for a single radio, need to see how this should be
	 * handled.
	 */
	if (mac_phy_caps->supported_bands & WMI_HOST_WLAN_2G_CAP) {
		pdev_cap->tx_chain_mask = mac_phy_caps->tx_chain_mask_2g;
		pdev_cap->rx_chain_mask = mac_phy_caps->rx_chain_mask_2g;
	} else if (mac_phy_caps->supported_bands & WMI_HOST_WLAN_5G_CAP) {
		pdev_cap->vht_cap = mac_phy_caps->vht_cap_info_5g;
		pdev_cap->vht_mcs = mac_phy_caps->vht_supp_mcs_5g;
		pdev_cap->he_mcs = mac_phy_caps->he_supp_mcs_5g;
		pdev_cap->tx_chain_mask = mac_phy_caps->tx_chain_mask_5g;
		pdev_cap->rx_chain_mask = mac_phy_caps->rx_chain_mask_5g;
		pdev_cap->nss_ratio_enabled =
			WMI_NSS_RATIO_ENABLE_DISABLE_GET(mac_phy_caps->nss_ratio);
		pdev_cap->nss_ratio_info =
			WMI_NSS_RATIO_INFO_GET(mac_phy_caps->nss_ratio);
	} else {
		return -EINVAL;
	}

	/* tx/rx chainmask reported from fw depends on the actual hw chains used,
	 * For example, for 4x4 capable macphys, first 4 chains can be used for first
	 * mac and the remaing 4 chains can be used for the second mac or vice-versa.
	 * In this case, tx/rx chainmask 0xf will be advertised for first mac and 0xf0
	 * will be advertised for second mac or vice-versa. Compute the shift value
	 * for tx/rx chainmask which will be used to advertise supported ht/vht rates to
	 * mac80211.
	 */
	pdev_cap->tx_chain_mask_shift =
			find_first_bit((unsigned long *)&pdev_cap->tx_chain_mask, 32);
	pdev_cap->rx_chain_mask_shift =
			find_first_bit((unsigned long *)&pdev_cap->rx_chain_mask, 32);

	if (mac_phy_caps->supported_bands & WMI_HOST_WLAN_2G_CAP) {
		cap_band = &pdev_cap->band[NL80211_BAND_2GHZ];
		cap_band->phy_id = mac_phy_caps->phy_id;
		cap_band->max_bw_supported = mac_phy_caps->max_bw_supported_2g;
		cap_band->ht_cap_info = mac_phy_caps->ht_cap_info_2g;
		cap_band->he_cap_info[0] = mac_phy_caps->he_cap_info_2g;
		cap_band->he_cap_info[1] = mac_phy_caps->he_cap_info_2g_ext;
		cap_band->he_mcs = mac_phy_caps->he_supp_mcs_2g;
		memcpy(cap_band->he_cap_phy_info, &mac_phy_caps->he_cap_phy_info_2g,
		       sizeof(u32) * PSOC_HOST_MAX_PHY_SIZE);
		memcpy(&cap_band->he_ppet, &mac_phy_caps->he_ppet2g,
		       sizeof(struct ath11k_ppe_threshold));
	}

	if (mac_phy_caps->supported_bands & WMI_HOST_WLAN_5G_CAP) {
		cap_band = &pdev_cap->band[NL80211_BAND_5GHZ];
		cap_band->phy_id = mac_phy_caps->phy_id;
		cap_band->max_bw_supported = mac_phy_caps->max_bw_supported_5g;
		cap_band->ht_cap_info = mac_phy_caps->ht_cap_info_5g;
		cap_band->he_cap_info[0] = mac_phy_caps->he_cap_info_5g;
		cap_band->he_cap_info[1] = mac_phy_caps->he_cap_info_5g_ext;
		cap_band->he_mcs = mac_phy_caps->he_supp_mcs_5g;
		memcpy(cap_band->he_cap_phy_info, &mac_phy_caps->he_cap_phy_info_5g,
		       sizeof(u32) * PSOC_HOST_MAX_PHY_SIZE);
		memcpy(&cap_band->he_ppet, &mac_phy_caps->he_ppet5g,
		       sizeof(struct ath11k_ppe_threshold));

		cap_band = &pdev_cap->band[NL80211_BAND_6GHZ];
		cap_band->max_bw_supported = mac_phy_caps->max_bw_supported_5g;
		cap_band->ht_cap_info = mac_phy_caps->ht_cap_info_5g;
		cap_band->he_cap_info[0] = mac_phy_caps->he_cap_info_5g;
		cap_band->he_cap_info[1] = mac_phy_caps->he_cap_info_5g_ext;
		cap_band->he_mcs = mac_phy_caps->he_supp_mcs_5g;
		memcpy(cap_band->he_cap_phy_info, &mac_phy_caps->he_cap_phy_info_5g,
		       sizeof(u32) * PSOC_HOST_MAX_PHY_SIZE);
		memcpy(&cap_band->he_ppet, &mac_phy_caps->he_ppet5g,
		       sizeof(struct ath11k_ppe_threshold));
	}

	return 0;
}

static int
ath11k_pull_reg_cap_svc_rdy_ext(struct ath11k_pdev_wmi *wmi_handle,
				struct wmi_soc_hal_reg_capabilities *reg_caps,
				struct wmi_hal_reg_capabilities_ext *wmi_ext_reg_cap,
				u8 phy_idx,
				struct ath11k_hal_reg_capabilities_ext *param)
{
	struct wmi_hal_reg_capabilities_ext *ext_reg_cap;

	if (!reg_caps || !wmi_ext_reg_cap)
		return -EINVAL;

	if (phy_idx >= reg_caps->num_phy)
		return -EINVAL;

	ext_reg_cap = &wmi_ext_reg_cap[phy_idx];

	param->phy_id = ext_reg_cap->phy_id;
	param->eeprom_reg_domain = ext_reg_cap->eeprom_reg_domain;
	param->eeprom_reg_domain_ext =
			      ext_reg_cap->eeprom_reg_domain_ext;
	param->regcap1 = ext_reg_cap->regcap1;
	param->regcap2 = ext_reg_cap->regcap2;
	/* check if param->wireless_mode is needed */
	param->low_2ghz_chan = ext_reg_cap->low_2ghz_chan;
	param->high_2ghz_chan = ext_reg_cap->high_2ghz_chan;
	param->low_5ghz_chan = ext_reg_cap->low_5ghz_chan;
	param->high_5ghz_chan = ext_reg_cap->high_5ghz_chan;

	return 0;
}

static int ath11k_pull_service_ready_tlv(struct ath11k_base *ab,
					 const void *evt_buf,
					 struct ath11k_targ_cap *cap)
{
	const struct wmi_service_ready_event *ev = evt_buf;

	if (!ev) {
		ath11k_err(ab, "%s: failed by NULL param\n",
			   __func__);
		return -EINVAL;
	}

	cap->phy_capability = ev->phy_capability;
	cap->max_frag_entry = ev->max_frag_entry;
	cap->num_rf_chains = ev->num_rf_chains;
	cap->ht_cap_info = ev->ht_cap_info;
	cap->vht_cap_info = ev->vht_cap_info;
	cap->vht_supp_mcs = ev->vht_supp_mcs;
	cap->hw_min_tx_power = ev->hw_min_tx_power;
	cap->hw_max_tx_power = ev->hw_max_tx_power;
	cap->sys_cap_info = ev->sys_cap_info;
	cap->min_pkt_size_enable = ev->min_pkt_size_enable;
	cap->max_bcn_ie_size = ev->max_bcn_ie_size;
	cap->max_num_scan_channels = ev->max_num_scan_channels;
	cap->max_supported_macs = ev->max_supported_macs;
	cap->wmi_fw_sub_feat_caps = ev->wmi_fw_sub_feat_caps;
	cap->txrx_chainmask = ev->txrx_chainmask;
	cap->default_dbs_hw_mode_index = ev->default_dbs_hw_mode_index;
	cap->num_msdu_desc = ev->num_msdu_desc;

	return 0;
}

/* Save the wmi_service_bitmap into a linear bitmap. The wmi_services in
 * wmi_service ready event are advertised in b0-b3 (LSB 4-bits) of each
 * 4-byte word.
 */
static void ath11k_wmi_service_bitmap_copy(struct ath11k_pdev_wmi *wmi,
					   const u32 *wmi_svc_bm)
{
	int i, j;

	for (i = 0, j = 0; i < WMI_SERVICE_BM_SIZE && j < WMI_MAX_SERVICE; i++) {
		do {
			if (wmi_svc_bm[i] & BIT(j % WMI_SERVICE_BITS_IN_SIZE32))
				set_bit(j, wmi->wmi_ab->svc_map);
		} while (++j % WMI_SERVICE_BITS_IN_SIZE32);
	}
}

static int ath11k_wmi_tlv_svc_rdy_parse(struct ath11k_base *ab, u16 tag, u16 len,
					const void *ptr, void *data)
{
	struct wmi_tlv_svc_ready_parse *svc_ready = data;
	struct ath11k_pdev_wmi *wmi_handle = &ab->wmi_ab.wmi[0];
	u16 expect_len;

	switch (tag) {
	case WMI_TAG_SERVICE_READY_EVENT:
		if (ath11k_pull_service_ready_tlv(ab, ptr, &ab->target_caps))
			return -EINVAL;
		break;

	case WMI_TAG_ARRAY_UINT32:
		if (!svc_ready->wmi_svc_bitmap_done) {
			expect_len = WMI_SERVICE_BM_SIZE * sizeof(u32);
			if (len < expect_len) {
				ath11k_warn(ab, "invalid len %d for the tag 0x%x\n",
					    len, tag);
				return -EINVAL;
			}

			ath11k_wmi_service_bitmap_copy(wmi_handle, ptr);

			svc_ready->wmi_svc_bitmap_done = true;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int ath11k_service_ready_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_tlv_svc_ready_parse svc_ready = { };
	int ret;

	ret = ath11k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath11k_wmi_tlv_svc_rdy_parse,
				  &svc_ready);
	if (ret) {
		ath11k_warn(ab, "failed to parse tlv %d\n", ret);
		return ret;
	}

	return 0;
}

struct sk_buff *ath11k_wmi_alloc_skb(struct ath11k_wmi_base *wmi_sc, u32 len)
{
	struct sk_buff *skb;
	struct ath11k_base *ab = wmi_sc->ab;
	u32 round_len = roundup(len, 4);

	skb = ath11k_htc_alloc_skb(ab, WMI_SKB_HEADROOM + round_len);
	if (!skb)
		return NULL;

	skb_reserve(skb, WMI_SKB_HEADROOM);
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath11k_warn(ab, "unaligned WMI skb data\n");

	skb_put(skb, round_len);
	memset(skb->data, 0, round_len);

	return skb;
}

int ath11k_wmi_mgmt_send(struct ath11k *ar, u32 vdev_id, u32 buf_id,
			 struct sk_buff *frame)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_mgmt_send_cmd *cmd;
	struct wmi_tlv *frame_tlv;
	struct sk_buff *skb;
	u32 buf_len;
	int ret, len;

	buf_len = frame->len < WMI_MGMT_SEND_DOWNLD_LEN ?
		  frame->len : WMI_MGMT_SEND_DOWNLD_LEN;

	len = sizeof(*cmd) + sizeof(*frame_tlv) + roundup(buf_len, 4);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_mgmt_send_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_MGMT_TX_SEND_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->desc_id = buf_id;
	cmd->chanfreq = 0;
	cmd->paddr_lo = lower_32_bits(ATH11K_SKB_CB(frame)->paddr);
	cmd->paddr_hi = upper_32_bits(ATH11K_SKB_CB(frame)->paddr);
	cmd->frame_len = frame->len;
	cmd->buf_len = buf_len;
	cmd->tx_params_valid = 0;

	frame_tlv = (struct wmi_tlv *)(skb->data + sizeof(*cmd));
	frame_tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
			    FIELD_PREP(WMI_TLV_LEN, buf_len);

	memcpy(frame_tlv->value, frame->data, buf_len);

	ath11k_ce_byte_swap(frame_tlv->value, buf_len);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_MGMT_TX_SEND_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to submit WMI_MGMT_TX_SEND_CMDID cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_vdev_create(struct ath11k *ar, u8 *macaddr,
			   struct vdev_create_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_create_cmd *cmd;
	struct sk_buff *skb;
	struct wmi_vdev_txrx_streams *txrx_streams;
	struct wmi_tlv *tlv;
	int ret, len;
	void *ptr;

	/* It can be optimized my sending tx/rx chain configuration
	 * only for supported bands instead of always sending it for
	 * both the bands.
	 */
	len = sizeof(*cmd) + TLV_HDR_SIZE +
		(WMI_NUM_SUPPORTED_BAND_MAX * sizeof(*txrx_streams));

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_create_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_CREATE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = param->if_id;
	cmd->vdev_type = param->type;
	cmd->vdev_subtype = param->subtype;
	cmd->num_cfg_txrx_streams = WMI_NUM_SUPPORTED_BAND_MAX;
	cmd->pdev_id = param->pdev_id;
	ether_addr_copy(cmd->vdev_macaddr.addr, macaddr);

	ptr = skb->data + sizeof(*cmd);
	len = WMI_NUM_SUPPORTED_BAND_MAX * sizeof(*txrx_streams);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, len);

	ptr += TLV_HDR_SIZE;
	txrx_streams = ptr;
	len = sizeof(*txrx_streams);
	txrx_streams->tlv_header =
		FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_TXRX_STREAMS) |
		FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	txrx_streams->band = WMI_TPC_CHAINMASK_CONFIG_BAND_2G;
	txrx_streams->supported_tx_streams =
				 param->chains[NL80211_BAND_2GHZ].tx;
	txrx_streams->supported_rx_streams =
				 param->chains[NL80211_BAND_2GHZ].rx;

	txrx_streams++;
	txrx_streams->tlv_header =
		FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_TXRX_STREAMS) |
		FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	txrx_streams->band = WMI_TPC_CHAINMASK_CONFIG_BAND_5G;
	txrx_streams->supported_tx_streams =
				 param->chains[NL80211_BAND_5GHZ].tx;
	txrx_streams->supported_rx_streams =
				 param->chains[NL80211_BAND_5GHZ].rx;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_VDEV_CREATE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to submit WMI_VDEV_CREATE_CMDID\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev create: id %d type %d subtype %d macaddr %pM pdevid %d\n",
		   param->if_id, param->type, param->subtype,
		   macaddr, param->pdev_id);

	return ret;
}

int ath11k_wmi_vdev_delete(struct ath11k *ar, u8 vdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_delete_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_delete_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_DELETE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_VDEV_DELETE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to submit WMI_VDEV_DELETE_CMDID\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI, "WMI vdev delete id %d\n", vdev_id);

	return ret;
}

int ath11k_wmi_vdev_stop(struct ath11k *ar, u8 vdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_stop_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_stop_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_STOP_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_VDEV_STOP_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to submit WMI_VDEV_STOP cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI, "WMI vdev stop id 0x%x\n", vdev_id);

	return ret;
}

int ath11k_wmi_vdev_down(struct ath11k *ar, u8 vdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_down_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_down_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_DOWN_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_VDEV_DOWN_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to submit WMI_VDEV_DOWN cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI, "WMI vdev down id 0x%x\n", vdev_id);

	return ret;
}

static void ath11k_wmi_put_wmi_channel(struct wmi_channel *chan,
				       struct wmi_vdev_start_req_arg *arg)
{
	u32 center_freq1 = arg->channel.band_center_freq1;

	memset(chan, 0, sizeof(*chan));

	chan->mhz = arg->channel.freq;
	chan->band_center_freq1 = arg->channel.band_center_freq1;

	if (arg->channel.mode == MODE_11AX_HE160) {
		if (arg->channel.freq > arg->channel.band_center_freq1)
			chan->band_center_freq1 = center_freq1 + 40;
		else
			chan->band_center_freq1 = center_freq1 - 40;

		chan->band_center_freq2 = arg->channel.band_center_freq1;

	} else if (arg->channel.mode == MODE_11AC_VHT80_80) {
		chan->band_center_freq2 = arg->channel.band_center_freq2;
	} else {
		chan->band_center_freq2 = 0;
	}

	chan->info |= FIELD_PREP(WMI_CHAN_INFO_MODE, arg->channel.mode);
	if (arg->channel.passive)
		chan->info |= WMI_CHAN_INFO_PASSIVE;
	if (arg->channel.allow_ibss)
		chan->info |= WMI_CHAN_INFO_ADHOC_ALLOWED;
	if (arg->channel.allow_ht)
		chan->info |= WMI_CHAN_INFO_ALLOW_HT;
	if (arg->channel.allow_vht)
		chan->info |= WMI_CHAN_INFO_ALLOW_VHT;
	if (arg->channel.allow_he)
		chan->info |= WMI_CHAN_INFO_ALLOW_HE;
	if (arg->channel.ht40plus)
		chan->info |= WMI_CHAN_INFO_HT40_PLUS;
	if (arg->channel.chan_radar)
		chan->info |= WMI_CHAN_INFO_DFS;
	if (arg->channel.freq2_radar)
		chan->info |= WMI_CHAN_INFO_DFS_FREQ2;

	chan->reg_info_1 = FIELD_PREP(WMI_CHAN_REG_INFO1_MAX_PWR,
				      arg->channel.max_power) |
		FIELD_PREP(WMI_CHAN_REG_INFO1_MAX_REG_PWR,
			   arg->channel.max_reg_power);

	chan->reg_info_2 = FIELD_PREP(WMI_CHAN_REG_INFO2_ANT_MAX,
				      arg->channel.max_antenna_gain) |
		FIELD_PREP(WMI_CHAN_REG_INFO2_MAX_TX_PWR,
			   arg->channel.max_power);
}

int ath11k_wmi_vdev_start(struct ath11k *ar, struct wmi_vdev_start_req_arg *arg,
			  bool restart)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_start_request_cmd *cmd;
	struct sk_buff *skb;
	struct wmi_channel *chan;
	struct wmi_tlv *tlv;
	void *ptr;
	int ret, len;

	if (WARN_ON(arg->ssid_len > sizeof(cmd->ssid.ssid)))
		return -EINVAL;

	len = sizeof(*cmd) + sizeof(*chan) + TLV_HDR_SIZE;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_start_request_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_VDEV_START_REQUEST_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = arg->vdev_id;
	cmd->beacon_interval = arg->bcn_intval;
	cmd->bcn_tx_rate = arg->bcn_tx_rate;
	cmd->dtim_period = arg->dtim_period;
	cmd->num_noa_descriptors = arg->num_noa_descriptors;
	cmd->preferred_rx_streams = arg->pref_rx_streams;
	cmd->preferred_tx_streams = arg->pref_tx_streams;
	cmd->cac_duration_ms = arg->cac_duration_ms;
	cmd->regdomain = arg->regdomain;
	cmd->he_ops = arg->he_ops;

	if (!restart) {
		if (arg->ssid) {
			cmd->ssid.ssid_len = arg->ssid_len;
			memcpy(cmd->ssid.ssid, arg->ssid, arg->ssid_len);
		}
		if (arg->hidden_ssid)
			cmd->flags |= WMI_VDEV_START_HIDDEN_SSID;
		if (arg->pmf_enabled)
			cmd->flags |= WMI_VDEV_START_PMF_ENABLED;
	}

	cmd->flags |= WMI_VDEV_START_LDPC_RX_ENABLED;
	if (test_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED, &ar->ab->dev_flags))
		cmd->flags |= WMI_VDEV_START_HW_ENCRYPTION_DISABLED;

	ptr = skb->data + sizeof(*cmd);
	chan = ptr;

	ath11k_wmi_put_wmi_channel(chan, arg);

	chan->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_CHANNEL) |
			   FIELD_PREP(WMI_TLV_LEN,
				      sizeof(*chan) - TLV_HDR_SIZE);
	ptr += sizeof(*chan);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, 0);

	/* Note: This is a nested TLV containing:
	 * [wmi_tlv][wmi_p2p_noa_descriptor][wmi_tlv]..
	 */

	ptr += sizeof(*tlv);

	if (restart)
		ret = ath11k_wmi_cmd_send(wmi, skb,
					  WMI_VDEV_RESTART_REQUEST_CMDID);
	else
		ret = ath11k_wmi_cmd_send(wmi, skb,
					  WMI_VDEV_START_REQUEST_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to submit vdev_%s cmd\n",
			    restart ? "restart" : "start");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI, "vdev %s id 0x%x freq 0x%x mode 0x%x\n",
		   restart ? "restart" : "start", arg->vdev_id,
		   arg->channel.freq, arg->channel.mode);

	return ret;
}

int ath11k_wmi_vdev_up(struct ath11k *ar, u32 vdev_id, u32 aid, const u8 *bssid)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_up_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_up_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_UP_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->vdev_assoc_id = aid;

	ether_addr_copy(cmd->vdev_bssid.addr, bssid);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_VDEV_UP_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to submit WMI_VDEV_UP cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI mgmt vdev up id 0x%x assoc id %d bssid %pM\n",
		   vdev_id, aid, bssid);

	return ret;
}

int ath11k_wmi_send_peer_create_cmd(struct ath11k *ar,
				    struct peer_create_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_peer_create_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_create_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PEER_CREATE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	ether_addr_copy(cmd->peer_macaddr.addr, param->peer_addr);
	cmd->peer_type = param->peer_type;
	cmd->vdev_id = param->vdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PEER_CREATE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to submit WMI_PEER_CREATE cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI peer create vdev_id %d peer_addr %pM\n",
		   param->vdev_id, param->peer_addr);

	return ret;
}

int ath11k_wmi_send_peer_delete_cmd(struct ath11k *ar,
				    const u8 *peer_addr, u8 vdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_peer_delete_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_delete_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PEER_DELETE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);
	cmd->vdev_id = vdev_id;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI peer delete vdev_id %d peer_addr %pM\n",
		   vdev_id,  peer_addr);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PEER_DELETE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PEER_DELETE cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_send_pdev_set_regdomain(struct ath11k *ar,
				       struct pdev_set_regdomain_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pdev_set_regdomain_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_regdomain_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_PDEV_SET_REGDOMAIN_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->reg_domain = param->current_rd_in_use;
	cmd->reg_domain_2g = param->current_rd_2g;
	cmd->reg_domain_5g = param->current_rd_5g;
	cmd->conformance_test_limit_2g = param->ctl_2g;
	cmd->conformance_test_limit_5g = param->ctl_5g;
	cmd->dfs_domain = param->dfs_domain;
	cmd->pdev_id = param->pdev_id;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI pdev regd rd %d rd2g %d rd5g %d domain %d pdev id %d\n",
		   param->current_rd_in_use, param->current_rd_2g,
		   param->current_rd_5g, param->dfs_domain, param->pdev_id);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PDEV_SET_REGDOMAIN_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_PDEV_SET_REGDOMAIN cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_set_peer_param(struct ath11k *ar, const u8 *peer_addr,
			      u32 vdev_id, u32 param_id, u32 param_val)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_peer_set_param_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_set_param_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PEER_SET_PARAM_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);
	cmd->vdev_id = vdev_id;
	cmd->param_id = param_id;
	cmd->param_value = param_val;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PEER_SET_PARAM_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PEER_SET_PARAM cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev %d peer 0x%pM set param %d value %d\n",
		   vdev_id, peer_addr, param_id, param_val);

	return ret;
}

int ath11k_wmi_send_peer_flush_tids_cmd(struct ath11k *ar,
					u8 peer_addr[ETH_ALEN],
					struct peer_flush_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_peer_flush_tids_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_flush_tids_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PEER_FLUSH_TIDS_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);
	cmd->peer_tid_bitmap = param->peer_tid_bitmap;
	cmd->vdev_id = param->vdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PEER_FLUSH_TIDS_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_PEER_FLUSH_TIDS cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI peer flush vdev_id %d peer_addr %pM tids %08x\n",
		   param->vdev_id, peer_addr, param->peer_tid_bitmap);

	return ret;
}

int ath11k_wmi_peer_rx_reorder_queue_setup(struct ath11k *ar,
					   int vdev_id, const u8 *addr,
					   dma_addr_t paddr, u8 tid,
					   u8 ba_window_size_valid,
					   u32 ba_window_size)
{
	struct wmi_peer_reorder_queue_setup_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_reorder_queue_setup_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_REORDER_QUEUE_SETUP_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	ether_addr_copy(cmd->peer_macaddr.addr, addr);
	cmd->vdev_id = vdev_id;
	cmd->tid = tid;
	cmd->queue_ptr_lo = lower_32_bits(paddr);
	cmd->queue_ptr_hi = upper_32_bits(paddr);
	cmd->queue_no = tid;
	cmd->ba_window_size_valid = ba_window_size_valid;
	cmd->ba_window_size = ba_window_size;

	ret = ath11k_wmi_cmd_send(ar->wmi, skb,
				  WMI_PEER_REORDER_QUEUE_SETUP_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_PEER_REORDER_QUEUE_SETUP\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi rx reorder queue setup addr %pM vdev_id %d tid %d\n",
		   addr, vdev_id, tid);

	return ret;
}

int
ath11k_wmi_rx_reord_queue_remove(struct ath11k *ar,
				 struct rx_reorder_queue_remove_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_peer_reorder_queue_remove_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_reorder_queue_remove_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_REORDER_QUEUE_REMOVE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	ether_addr_copy(cmd->peer_macaddr.addr, param->peer_macaddr);
	cmd->vdev_id = param->vdev_id;
	cmd->tid_mask = param->peer_tid_bitmap;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "%s: peer_macaddr %pM vdev_id %d, tid_map %d", __func__,
		   param->peer_macaddr, param->vdev_id, param->peer_tid_bitmap);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PEER_REORDER_QUEUE_REMOVE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_PEER_REORDER_QUEUE_REMOVE_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_pdev_set_param(struct ath11k *ar, u32 param_id,
			      u32 param_value, u8 pdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pdev_set_param_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_param_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_SET_PARAM_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->pdev_id = pdev_id;
	cmd->param_id = param_id;
	cmd->param_value = param_value;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PDEV_SET_PARAM_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_SET_PARAM cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI pdev set param %d pdev id %d value %d\n",
		   param_id, pdev_id, param_value);

	return ret;
}

int ath11k_wmi_pdev_set_ps_mode(struct ath11k *ar, int vdev_id,
				enum wmi_sta_ps_mode psmode)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pdev_set_ps_mode_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_ps_mode_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_STA_POWERSAVE_MODE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->sta_ps_mode = psmode;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_STA_POWERSAVE_MODE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_SET_PARAM cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev set psmode %d vdev id %d\n",
		   psmode, vdev_id);

	return ret;
}

int ath11k_wmi_pdev_suspend(struct ath11k *ar, u32 suspend_opt,
			    u32 pdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pdev_suspend_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_suspend_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_SUSPEND_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->suspend_opt = suspend_opt;
	cmd->pdev_id = pdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PDEV_SUSPEND_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_SUSPEND cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI pdev suspend pdev_id %d\n", pdev_id);

	return ret;
}

int ath11k_wmi_pdev_resume(struct ath11k *ar, u32 pdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pdev_resume_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_resume_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_RESUME_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->pdev_id = pdev_id;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI pdev resume pdev id %d\n", pdev_id);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PDEV_RESUME_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_RESUME cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

/* TODO FW Support for the cmd is not available yet.
 * Can be tested once the command and corresponding
 * event is implemented in FW
 */
int ath11k_wmi_pdev_bss_chan_info_request(struct ath11k *ar,
					  enum wmi_bss_chan_info_req_type type)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pdev_bss_chan_info_req_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_bss_chan_info_req_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_PDEV_BSS_CHAN_INFO_REQUEST) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->req_type = type;
	cmd->pdev_id = ar->pdev->pdev_id;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI bss chan info req type %d\n", type);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_BSS_CHAN_INFO_REQUEST_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_PDEV_BSS_CHAN_INFO_REQUEST cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_send_set_ap_ps_param_cmd(struct ath11k *ar, u8 *peer_addr,
					struct ap_ps_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_ap_ps_peer_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_ap_ps_peer_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_AP_PS_PEER_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = param->vdev_id;
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);
	cmd->param = param->param;
	cmd->value = param->value;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_AP_PS_PEER_PARAM_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_AP_PS_PEER_PARAM_CMDID\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI set ap ps vdev id %d peer %pM param %d value %d\n",
		   param->vdev_id, peer_addr, param->param, param->value);

	return ret;
}

int ath11k_wmi_set_sta_ps_param(struct ath11k *ar, u32 vdev_id,
				u32 param, u32 param_value)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_sta_powersave_param_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_sta_powersave_param_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_STA_POWERSAVE_PARAM_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = vdev_id;
	cmd->param = param;
	cmd->value = param_value;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI set sta ps vdev_id %d param %d value %d\n",
		   vdev_id, param, param_value);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_STA_POWERSAVE_PARAM_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_STA_POWERSAVE_PARAM_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_force_fw_hang_cmd(struct ath11k *ar, u32 type, u32 delay_time_ms)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_force_fw_hang_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_force_fw_hang_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_FORCE_FW_HANG_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);

	cmd->type = type;
	cmd->delay_time_ms = delay_time_ms;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_FORCE_FW_HANG_CMDID);

	if (ret) {
		ath11k_warn(ar->ab, "Failed to send WMI_FORCE_FW_HANG_CMDID");
		dev_kfree_skb(skb);
	}
	return ret;
}

int ath11k_wmi_vdev_set_param_cmd(struct ath11k *ar, u32 vdev_id,
				  u32 param_id, u32 param_value)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_set_param_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_set_param_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_SET_PARAM_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = vdev_id;
	cmd->param_id = param_id;
	cmd->param_value = param_value;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_VDEV_SET_PARAM_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_VDEV_SET_PARAM_CMDID\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev id 0x%x set param %d value %d\n",
		   vdev_id, param_id, param_value);

	return ret;
}

int ath11k_wmi_send_stats_request_cmd(struct ath11k *ar,
				      struct stats_request_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_request_stats_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_request_stats_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_REQUEST_STATS_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->stats_id = param->stats_id;
	cmd->vdev_id = param->vdev_id;
	cmd->pdev_id = param->pdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_REQUEST_STATS_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_REQUEST_STATS cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI request stats 0x%x vdev id %d pdev id %d\n",
		   param->stats_id, param->vdev_id, param->pdev_id);

	return ret;
}

int ath11k_wmi_send_pdev_temperature_cmd(struct ath11k *ar)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_get_pdev_temperature_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_get_pdev_temperature_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_GET_TEMPERATURE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PDEV_GET_TEMPERATURE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_GET_TEMPERATURE cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI pdev get temperature for pdev_id %d\n", ar->pdev->pdev_id);

	return ret;
}

int ath11k_wmi_send_bcn_offload_control_cmd(struct ath11k *ar,
					    u32 vdev_id, u32 bcn_ctrl_op)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_bcn_offload_ctrl_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_bcn_offload_ctrl_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_BCN_OFFLOAD_CTRL_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = vdev_id;
	cmd->bcn_ctrl_op = bcn_ctrl_op;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI bcn ctrl offload vdev id %d ctrl_op %d\n",
		   vdev_id, bcn_ctrl_op);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_BCN_OFFLOAD_CTRL_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_BCN_OFFLOAD_CTRL_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_bcn_tmpl(struct ath11k *ar, u32 vdev_id,
			struct ieee80211_mutable_offsets *offs,
			struct sk_buff *bcn)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_bcn_tmpl_cmd *cmd;
	struct wmi_bcn_prb_info *bcn_prb_info;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	int ret, len;
	size_t aligned_len = roundup(bcn->len, 4);

	len = sizeof(*cmd) + sizeof(*bcn_prb_info) + TLV_HDR_SIZE + aligned_len;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_bcn_tmpl_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_BCN_TMPL_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->tim_ie_offset = offs->tim_offset;
	cmd->csa_switch_count_offset = offs->cntdwn_counter_offs[0];
	cmd->ext_csa_switch_count_offset = offs->cntdwn_counter_offs[1];
	cmd->buf_len = bcn->len;

	ptr = skb->data + sizeof(*cmd);

	bcn_prb_info = ptr;
	len = sizeof(*bcn_prb_info);
	bcn_prb_info->tlv_header = FIELD_PREP(WMI_TLV_TAG,
					      WMI_TAG_BCN_PRB_INFO) |
				   FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	bcn_prb_info->caps = 0;
	bcn_prb_info->erp = 0;

	ptr += sizeof(*bcn_prb_info);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
		      FIELD_PREP(WMI_TLV_LEN, aligned_len);
	memcpy(tlv->value, bcn->data, bcn->len);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_BCN_TMPL_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_BCN_TMPL_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_vdev_install_key(struct ath11k *ar,
				struct wmi_vdev_install_key_arg *arg)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_install_key_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	int ret, len;
	int key_len_aligned = roundup(arg->key_len, sizeof(uint32_t));

	len = sizeof(*cmd) + TLV_HDR_SIZE + key_len_aligned;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_install_key_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VDEV_INSTALL_KEY_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = arg->vdev_id;
	ether_addr_copy(cmd->peer_macaddr.addr, arg->macaddr);
	cmd->key_idx = arg->key_idx;
	cmd->key_flags = arg->key_flags;
	cmd->key_cipher = arg->key_cipher;
	cmd->key_len = arg->key_len;
	cmd->key_txmic_len = arg->key_txmic_len;
	cmd->key_rxmic_len = arg->key_rxmic_len;

	if (arg->key_rsc_counter)
		memcpy(&cmd->key_rsc_counter, &arg->key_rsc_counter,
		       sizeof(struct wmi_key_seq_counter));

	tlv = (struct wmi_tlv *)(skb->data + sizeof(*cmd));
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
		      FIELD_PREP(WMI_TLV_LEN, key_len_aligned);
	memcpy(tlv->value, (u8 *)arg->key_data, key_len_aligned);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_VDEV_INSTALL_KEY_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_VDEV_INSTALL_KEY cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev install key idx %d cipher %d len %d\n",
		   arg->key_idx, arg->key_cipher, arg->key_len);

	return ret;
}

static inline void
ath11k_wmi_copy_peer_flags(struct wmi_peer_assoc_complete_cmd *cmd,
			   struct peer_assoc_params *param,
			   bool hw_crypto_disabled)
{
	cmd->peer_flags = 0;

	if (param->is_wme_set) {
		if (param->qos_flag)
			cmd->peer_flags |= WMI_PEER_QOS;
		if (param->apsd_flag)
			cmd->peer_flags |= WMI_PEER_APSD;
		if (param->ht_flag)
			cmd->peer_flags |= WMI_PEER_HT;
		if (param->bw_40)
			cmd->peer_flags |= WMI_PEER_40MHZ;
		if (param->bw_80)
			cmd->peer_flags |= WMI_PEER_80MHZ;
		if (param->bw_160)
			cmd->peer_flags |= WMI_PEER_160MHZ;

		/* Typically if STBC is enabled for VHT it should be enabled
		 * for HT as well
		 **/
		if (param->stbc_flag)
			cmd->peer_flags |= WMI_PEER_STBC;

		/* Typically if LDPC is enabled for VHT it should be enabled
		 * for HT as well
		 **/
		if (param->ldpc_flag)
			cmd->peer_flags |= WMI_PEER_LDPC;

		if (param->static_mimops_flag)
			cmd->peer_flags |= WMI_PEER_STATIC_MIMOPS;
		if (param->dynamic_mimops_flag)
			cmd->peer_flags |= WMI_PEER_DYN_MIMOPS;
		if (param->spatial_mux_flag)
			cmd->peer_flags |= WMI_PEER_SPATIAL_MUX;
		if (param->vht_flag)
			cmd->peer_flags |= WMI_PEER_VHT;
		if (param->he_flag)
			cmd->peer_flags |= WMI_PEER_HE;
		if (param->twt_requester)
			cmd->peer_flags |= WMI_PEER_TWT_REQ;
		if (param->twt_responder)
			cmd->peer_flags |= WMI_PEER_TWT_RESP;
	}

	/* Suppress authorization for all AUTH modes that need 4-way handshake
	 * (during re-association).
	 * Authorization will be done for these modes on key installation.
	 */
	if (param->auth_flag)
		cmd->peer_flags |= WMI_PEER_AUTH;
	if (param->need_ptk_4_way) {
		cmd->peer_flags |= WMI_PEER_NEED_PTK_4_WAY;
		if (!hw_crypto_disabled && param->is_assoc)
			cmd->peer_flags &= ~WMI_PEER_AUTH;
	}
	if (param->need_gtk_2_way)
		cmd->peer_flags |= WMI_PEER_NEED_GTK_2_WAY;
	/* safe mode bypass the 4-way handshake */
	if (param->safe_mode_enabled)
		cmd->peer_flags &= ~(WMI_PEER_NEED_PTK_4_WAY |
				     WMI_PEER_NEED_GTK_2_WAY);

	if (param->is_pmf_enabled)
		cmd->peer_flags |= WMI_PEER_PMF;

	/* Disable AMSDU for station transmit, if user configures it */
	/* Disable AMSDU for AP transmit to 11n Stations, if user configures
	 * it
	 * if (param->amsdu_disable) Add after FW support
	 **/

	/* Target asserts if node is marked HT and all MCS is set to 0.
	 * Mark the node as non-HT if all the mcs rates are disabled through
	 * iwpriv
	 **/
	if (param->peer_ht_rates.num_rates == 0)
		cmd->peer_flags &= ~WMI_PEER_HT;
}

int ath11k_wmi_send_peer_assoc_cmd(struct ath11k *ar,
				   struct peer_assoc_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_peer_assoc_complete_cmd *cmd;
	struct wmi_vht_rate_set *mcs;
	struct wmi_he_rate_set *he_mcs;
	struct sk_buff *skb;
	struct wmi_tlv *tlv;
	void *ptr;
	u32 peer_legacy_rates_align;
	u32 peer_ht_rates_align;
	int i, ret, len;

	peer_legacy_rates_align = roundup(param->peer_legacy_rates.num_rates,
					  sizeof(u32));
	peer_ht_rates_align = roundup(param->peer_ht_rates.num_rates,
				      sizeof(u32));

	len = sizeof(*cmd) +
	      TLV_HDR_SIZE + (peer_legacy_rates_align * sizeof(u8)) +
	      TLV_HDR_SIZE + (peer_ht_rates_align * sizeof(u8)) +
	      sizeof(*mcs) + TLV_HDR_SIZE +
	      (sizeof(*he_mcs) * param->peer_he_mcs_count);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	ptr = skb->data;

	cmd = ptr;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_PEER_ASSOC_COMPLETE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = param->vdev_id;

	cmd->peer_new_assoc = param->peer_new_assoc;
	cmd->peer_associd = param->peer_associd;

	ath11k_wmi_copy_peer_flags(cmd, param,
				   test_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED,
					    &ar->ab->dev_flags));

	ether_addr_copy(cmd->peer_macaddr.addr, param->peer_mac);

	cmd->peer_rate_caps = param->peer_rate_caps;
	cmd->peer_caps = param->peer_caps;
	cmd->peer_listen_intval = param->peer_listen_intval;
	cmd->peer_ht_caps = param->peer_ht_caps;
	cmd->peer_max_mpdu = param->peer_max_mpdu;
	cmd->peer_mpdu_density = param->peer_mpdu_density;
	cmd->peer_vht_caps = param->peer_vht_caps;
	cmd->peer_phymode = param->peer_phymode;

	/* Update 11ax capabilities */
	cmd->peer_he_cap_info = param->peer_he_cap_macinfo[0];
	cmd->peer_he_cap_info_ext = param->peer_he_cap_macinfo[1];
	cmd->peer_he_cap_info_internal = param->peer_he_cap_macinfo_internal;
	cmd->peer_he_caps_6ghz = param->peer_he_caps_6ghz;
	cmd->peer_he_ops = param->peer_he_ops;
	memcpy(&cmd->peer_he_cap_phy, &param->peer_he_cap_phyinfo,
	       sizeof(param->peer_he_cap_phyinfo));
	memcpy(&cmd->peer_ppet, &param->peer_ppet,
	       sizeof(param->peer_ppet));

	/* Update peer legacy rate information */
	ptr += sizeof(*cmd);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
		      FIELD_PREP(WMI_TLV_LEN, peer_legacy_rates_align);

	ptr += TLV_HDR_SIZE;

	cmd->num_peer_legacy_rates = param->peer_legacy_rates.num_rates;
	memcpy(ptr, param->peer_legacy_rates.rates,
	       param->peer_legacy_rates.num_rates);

	/* Update peer HT rate information */
	ptr += peer_legacy_rates_align;

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
		      FIELD_PREP(WMI_TLV_LEN, peer_ht_rates_align);
	ptr += TLV_HDR_SIZE;
	cmd->num_peer_ht_rates = param->peer_ht_rates.num_rates;
	memcpy(ptr, param->peer_ht_rates.rates,
	       param->peer_ht_rates.num_rates);

	/* VHT Rates */
	ptr += peer_ht_rates_align;

	mcs = ptr;

	mcs->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_VHT_RATE_SET) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*mcs) - TLV_HDR_SIZE);

	cmd->peer_nss = param->peer_nss;

	/* Update bandwidth-NSS mapping */
	cmd->peer_bw_rxnss_override = 0;
	cmd->peer_bw_rxnss_override |= param->peer_bw_rxnss_override;

	if (param->vht_capable) {
		mcs->rx_max_rate = param->rx_max_rate;
		mcs->rx_mcs_set = param->rx_mcs_set;
		mcs->tx_max_rate = param->tx_max_rate;
		mcs->tx_mcs_set = param->tx_mcs_set;
	}

	/* HE Rates */
	cmd->peer_he_mcs = param->peer_he_mcs_count;
	cmd->min_data_rate = param->min_data_rate;

	ptr += sizeof(*mcs);

	len = param->peer_he_mcs_count * sizeof(*he_mcs);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, len);
	ptr += TLV_HDR_SIZE;

	/* Loop through the HE rate set */
	for (i = 0; i < param->peer_he_mcs_count; i++) {
		he_mcs = ptr;
		he_mcs->tlv_header = FIELD_PREP(WMI_TLV_TAG,
						WMI_TAG_HE_RATE_SET) |
				     FIELD_PREP(WMI_TLV_LEN,
						sizeof(*he_mcs) - TLV_HDR_SIZE);

		he_mcs->rx_mcs_set = param->peer_he_tx_mcs_set[i];
		he_mcs->tx_mcs_set = param->peer_he_rx_mcs_set[i];
		ptr += sizeof(*he_mcs);
	}

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_PEER_ASSOC_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_PEER_ASSOC_CMDID\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi peer assoc vdev id %d assoc id %d peer mac %pM peer_flags %x rate_caps %x peer_caps %x listen_intval %d ht_caps %x max_mpdu %d nss %d phymode %d peer_mpdu_density %d vht_caps %x he cap_info %x he ops %x he cap_info_ext %x he phy %x %x %x peer_bw_rxnss_override %x\n",
		   cmd->vdev_id, cmd->peer_associd, param->peer_mac,
		   cmd->peer_flags, cmd->peer_rate_caps, cmd->peer_caps,
		   cmd->peer_listen_intval, cmd->peer_ht_caps,
		   cmd->peer_max_mpdu, cmd->peer_nss, cmd->peer_phymode,
		   cmd->peer_mpdu_density,
		   cmd->peer_vht_caps, cmd->peer_he_cap_info,
		   cmd->peer_he_ops, cmd->peer_he_cap_info_ext,
		   cmd->peer_he_cap_phy[0], cmd->peer_he_cap_phy[1],
		   cmd->peer_he_cap_phy[2],
		   cmd->peer_bw_rxnss_override);

	return ret;
}

void ath11k_wmi_start_scan_init(struct ath11k *ar,
				struct scan_req_params *arg)
{
	/* setup commonly used values */
	arg->scan_req_id = 1;
	arg->scan_priority = WMI_SCAN_PRIORITY_LOW;
	arg->dwell_time_active = 50;
	arg->dwell_time_active_2g = 0;
	arg->dwell_time_passive = 150;
	arg->dwell_time_active_6g = 40;
	arg->dwell_time_passive_6g = 30;
	arg->min_rest_time = 50;
	arg->max_rest_time = 500;
	arg->repeat_probe_time = 0;
	arg->probe_spacing_time = 0;
	arg->idle_time = 0;
	arg->max_scan_time = 20000;
	arg->probe_delay = 5;
	arg->notify_scan_events = WMI_SCAN_EVENT_STARTED |
				  WMI_SCAN_EVENT_COMPLETED |
				  WMI_SCAN_EVENT_BSS_CHANNEL |
				  WMI_SCAN_EVENT_FOREIGN_CHAN |
				  WMI_SCAN_EVENT_DEQUEUED;
	arg->scan_flags |= WMI_SCAN_CHAN_STAT_EVENT;
	arg->num_bssid = 1;

	/* fill bssid_list[0] with 0xff, otherwise bssid and RA will be
	 * ZEROs in probe request
	 */
	eth_broadcast_addr(arg->bssid_list[0].addr);
}

static inline void
ath11k_wmi_copy_scan_event_cntrl_flags(struct wmi_start_scan_cmd *cmd,
				       struct scan_req_params *param)
{
	/* Scan events subscription */
	if (param->scan_ev_started)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_STARTED;
	if (param->scan_ev_completed)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_COMPLETED;
	if (param->scan_ev_bss_chan)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_BSS_CHANNEL;
	if (param->scan_ev_foreign_chan)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_FOREIGN_CHAN;
	if (param->scan_ev_dequeued)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_DEQUEUED;
	if (param->scan_ev_preempted)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_PREEMPTED;
	if (param->scan_ev_start_failed)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_START_FAILED;
	if (param->scan_ev_restarted)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_RESTARTED;
	if (param->scan_ev_foreign_chn_exit)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_FOREIGN_CHAN_EXIT;
	if (param->scan_ev_suspended)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_SUSPENDED;
	if (param->scan_ev_resumed)
		cmd->notify_scan_events |=  WMI_SCAN_EVENT_RESUMED;

	/** Set scan control flags */
	cmd->scan_ctrl_flags = 0;
	if (param->scan_f_passive)
		cmd->scan_ctrl_flags |=  WMI_SCAN_FLAG_PASSIVE;
	if (param->scan_f_strict_passive_pch)
		cmd->scan_ctrl_flags |=  WMI_SCAN_FLAG_STRICT_PASSIVE_ON_PCHN;
	if (param->scan_f_promisc_mode)
		cmd->scan_ctrl_flags |=  WMI_SCAN_FILTER_PROMISCUOS;
	if (param->scan_f_capture_phy_err)
		cmd->scan_ctrl_flags |=  WMI_SCAN_CAPTURE_PHY_ERROR;
	if (param->scan_f_half_rate)
		cmd->scan_ctrl_flags |=  WMI_SCAN_FLAG_HALF_RATE_SUPPORT;
	if (param->scan_f_quarter_rate)
		cmd->scan_ctrl_flags |=  WMI_SCAN_FLAG_QUARTER_RATE_SUPPORT;
	if (param->scan_f_cck_rates)
		cmd->scan_ctrl_flags |=  WMI_SCAN_ADD_CCK_RATES;
	if (param->scan_f_ofdm_rates)
		cmd->scan_ctrl_flags |=  WMI_SCAN_ADD_OFDM_RATES;
	if (param->scan_f_chan_stat_evnt)
		cmd->scan_ctrl_flags |=  WMI_SCAN_CHAN_STAT_EVENT;
	if (param->scan_f_filter_prb_req)
		cmd->scan_ctrl_flags |=  WMI_SCAN_FILTER_PROBE_REQ;
	if (param->scan_f_bcast_probe)
		cmd->scan_ctrl_flags |=  WMI_SCAN_ADD_BCAST_PROBE_REQ;
	if (param->scan_f_offchan_mgmt_tx)
		cmd->scan_ctrl_flags |=  WMI_SCAN_OFFCHAN_MGMT_TX;
	if (param->scan_f_offchan_data_tx)
		cmd->scan_ctrl_flags |=  WMI_SCAN_OFFCHAN_DATA_TX;
	if (param->scan_f_force_active_dfs_chn)
		cmd->scan_ctrl_flags |=  WMI_SCAN_FLAG_FORCE_ACTIVE_ON_DFS;
	if (param->scan_f_add_tpc_ie_in_probe)
		cmd->scan_ctrl_flags |=  WMI_SCAN_ADD_TPC_IE_IN_PROBE_REQ;
	if (param->scan_f_add_ds_ie_in_probe)
		cmd->scan_ctrl_flags |=  WMI_SCAN_ADD_DS_IE_IN_PROBE_REQ;
	if (param->scan_f_add_spoofed_mac_in_probe)
		cmd->scan_ctrl_flags |=  WMI_SCAN_ADD_SPOOF_MAC_IN_PROBE_REQ;
	if (param->scan_f_add_rand_seq_in_probe)
		cmd->scan_ctrl_flags |=  WMI_SCAN_RANDOM_SEQ_NO_IN_PROBE_REQ;
	if (param->scan_f_en_ie_whitelist_in_probe)
		cmd->scan_ctrl_flags |=
			 WMI_SCAN_ENABLE_IE_WHTELIST_IN_PROBE_REQ;

	/* for adaptive scan mode using 3 bits (21 - 23 bits) */
	WMI_SCAN_SET_DWELL_MODE(cmd->scan_ctrl_flags,
				param->adaptive_dwell_time_mode);
}

int ath11k_wmi_send_scan_start_cmd(struct ath11k *ar,
				   struct scan_req_params *params)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_start_scan_cmd *cmd;
	struct wmi_ssid *ssid = NULL;
	struct wmi_mac_addr *bssid;
	struct sk_buff *skb;
	struct wmi_tlv *tlv;
	void *ptr;
	int i, ret, len;
	u32 *tmp_ptr;
	u8 extraie_len_with_pad = 0;
	struct hint_short_ssid *s_ssid = NULL;
	struct hint_bssid *hint_bssid = NULL;

	len = sizeof(*cmd);

	len += TLV_HDR_SIZE;
	if (params->num_chan)
		len += params->num_chan * sizeof(u32);

	len += TLV_HDR_SIZE;
	if (params->num_ssids)
		len += params->num_ssids * sizeof(*ssid);

	len += TLV_HDR_SIZE;
	if (params->num_bssid)
		len += sizeof(*bssid) * params->num_bssid;

	len += TLV_HDR_SIZE;
	if (params->extraie.len)
		extraie_len_with_pad =
			roundup(params->extraie.len, sizeof(u32));
	len += extraie_len_with_pad;

	if (params->num_hint_bssid)
		len += TLV_HDR_SIZE +
		       params->num_hint_bssid * sizeof(struct hint_bssid);

	if (params->num_hint_s_ssid)
		len += TLV_HDR_SIZE +
		       params->num_hint_s_ssid * sizeof(struct hint_short_ssid);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	ptr = skb->data;

	cmd = ptr;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_START_SCAN_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->scan_id = params->scan_id;
	cmd->scan_req_id = params->scan_req_id;
	cmd->vdev_id = params->vdev_id;
	cmd->scan_priority = params->scan_priority;
	cmd->notify_scan_events = params->notify_scan_events;

	ath11k_wmi_copy_scan_event_cntrl_flags(cmd, params);

	cmd->dwell_time_active = params->dwell_time_active;
	cmd->dwell_time_active_2g = params->dwell_time_active_2g;
	cmd->dwell_time_passive = params->dwell_time_passive;
	cmd->dwell_time_active_6g = params->dwell_time_active_6g;
	cmd->dwell_time_passive_6g = params->dwell_time_passive_6g;
	cmd->min_rest_time = params->min_rest_time;
	cmd->max_rest_time = params->max_rest_time;
	cmd->repeat_probe_time = params->repeat_probe_time;
	cmd->probe_spacing_time = params->probe_spacing_time;
	cmd->idle_time = params->idle_time;
	cmd->max_scan_time = params->max_scan_time;
	cmd->probe_delay = params->probe_delay;
	cmd->burst_duration = params->burst_duration;
	cmd->num_chan = params->num_chan;
	cmd->num_bssid = params->num_bssid;
	cmd->num_ssids = params->num_ssids;
	cmd->ie_len = params->extraie.len;
	cmd->n_probes = params->n_probes;

	ptr += sizeof(*cmd);

	len = params->num_chan * sizeof(u32);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_UINT32) |
		      FIELD_PREP(WMI_TLV_LEN, len);
	ptr += TLV_HDR_SIZE;
	tmp_ptr = (u32 *)ptr;

	for (i = 0; i < params->num_chan; ++i)
		tmp_ptr[i] = params->chan_list[i];

	ptr += len;

	len = params->num_ssids * sizeof(*ssid);
	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_FIXED_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, len);

	ptr += TLV_HDR_SIZE;

	if (params->num_ssids) {
		ssid = ptr;
		for (i = 0; i < params->num_ssids; ++i) {
			ssid->ssid_len = params->ssid[i].length;
			memcpy(ssid->ssid, params->ssid[i].ssid,
			       params->ssid[i].length);
			ssid++;
		}
	}

	ptr += (params->num_ssids * sizeof(*ssid));
	len = params->num_bssid * sizeof(*bssid);
	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_FIXED_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, len);

	ptr += TLV_HDR_SIZE;
	bssid = ptr;

	if (params->num_bssid) {
		for (i = 0; i < params->num_bssid; ++i) {
			ether_addr_copy(bssid->addr,
					params->bssid_list[i].addr);
			bssid++;
		}
	}

	ptr += params->num_bssid * sizeof(*bssid);

	len = extraie_len_with_pad;
	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
		      FIELD_PREP(WMI_TLV_LEN, len);
	ptr += TLV_HDR_SIZE;

	if (params->extraie.len)
		memcpy(ptr, params->extraie.ptr,
		       params->extraie.len);

	ptr += extraie_len_with_pad;

	if (params->num_hint_s_ssid) {
		len = params->num_hint_s_ssid * sizeof(struct hint_short_ssid);
		tlv = ptr;
		tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_FIXED_STRUCT) |
			      FIELD_PREP(WMI_TLV_LEN, len);
		ptr += TLV_HDR_SIZE;
		s_ssid = ptr;
		for (i = 0; i < params->num_hint_s_ssid; ++i) {
			s_ssid->freq_flags = params->hint_s_ssid[i].freq_flags;
			s_ssid->short_ssid = params->hint_s_ssid[i].short_ssid;
			s_ssid++;
		}
		ptr += len;
	}

	if (params->num_hint_bssid) {
		len = params->num_hint_bssid * sizeof(struct hint_bssid);
		tlv = ptr;
		tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_FIXED_STRUCT) |
			      FIELD_PREP(WMI_TLV_LEN, len);
		ptr += TLV_HDR_SIZE;
		hint_bssid = ptr;
		for (i = 0; i < params->num_hint_bssid; ++i) {
			hint_bssid->freq_flags =
				params->hint_bssid[i].freq_flags;
			ether_addr_copy(&params->hint_bssid[i].bssid.addr[0],
					&hint_bssid->bssid.addr[0]);
			hint_bssid++;
		}
	}

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_START_SCAN_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_START_SCAN_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_send_scan_stop_cmd(struct ath11k *ar,
				  struct scan_cancel_param *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_stop_scan_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_stop_scan_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_STOP_SCAN_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = param->vdev_id;
	cmd->requestor = param->requester;
	cmd->scan_id = param->scan_id;
	cmd->pdev_id = param->pdev_id;
	/* stop the scan with the corresponding scan_id */
	if (param->req_type == WLAN_SCAN_CANCEL_PDEV_ALL) {
		/* Cancelling all scans */
		cmd->req_type =  WMI_SCAN_STOP_ALL;
	} else if (param->req_type == WLAN_SCAN_CANCEL_VDEV_ALL) {
		/* Cancelling VAP scans */
		cmd->req_type =  WMI_SCN_STOP_VAP_ALL;
	} else if (param->req_type == WLAN_SCAN_CANCEL_SINGLE) {
		/* Cancelling specific scan */
		cmd->req_type =  WMI_SCAN_STOP_ONE;
	} else {
		ath11k_warn(ar->ab, "invalid scan cancel param %d",
			    param->req_type);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_STOP_SCAN_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_STOP_SCAN_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_send_scan_chan_list_cmd(struct ath11k *ar,
				       struct scan_chan_list_params *chan_list)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_scan_chan_list_cmd *cmd;
	struct sk_buff *skb;
	struct wmi_channel *chan_info;
	struct channel_param *tchan_info;
	struct wmi_tlv *tlv;
	void *ptr;
	int i, ret, len;
	u16 num_send_chans, num_sends = 0, max_chan_limit = 0;
	u32 *reg1, *reg2;

	tchan_info = chan_list->ch_param;
	while (chan_list->nallchans) {
		len = sizeof(*cmd) + TLV_HDR_SIZE;
		max_chan_limit = (wmi->wmi_ab->max_msg_len[ar->pdev_idx] - len) /
			sizeof(*chan_info);

		if (chan_list->nallchans > max_chan_limit)
			num_send_chans = max_chan_limit;
		else
			num_send_chans = chan_list->nallchans;

		chan_list->nallchans -= num_send_chans;
		len += sizeof(*chan_info) * num_send_chans;

		skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
		if (!skb)
			return -ENOMEM;

		cmd = (struct wmi_scan_chan_list_cmd *)skb->data;
		cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_SCAN_CHAN_LIST_CMD) |
			FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
		cmd->pdev_id = chan_list->pdev_id;
		cmd->num_scan_chans = num_send_chans;
		if (num_sends)
			cmd->flags |= WMI_APPEND_TO_EXISTING_CHAN_LIST_FLAG;

		ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
			   "WMI no.of chan = %d len = %d pdev_id = %d num_sends = %d\n",
			   num_send_chans, len, cmd->pdev_id, num_sends);

		ptr = skb->data + sizeof(*cmd);

		len = sizeof(*chan_info) * num_send_chans;
		tlv = ptr;
		tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
			      FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
		ptr += TLV_HDR_SIZE;

		for (i = 0; i < num_send_chans; ++i) {
			chan_info = ptr;
			memset(chan_info, 0, sizeof(*chan_info));
			len = sizeof(*chan_info);
			chan_info->tlv_header = FIELD_PREP(WMI_TLV_TAG,
							   WMI_TAG_CHANNEL) |
						FIELD_PREP(WMI_TLV_LEN,
							   len - TLV_HDR_SIZE);

			reg1 = &chan_info->reg_info_1;
			reg2 = &chan_info->reg_info_2;
			chan_info->mhz = tchan_info->mhz;
			chan_info->band_center_freq1 = tchan_info->cfreq1;
			chan_info->band_center_freq2 = tchan_info->cfreq2;

			if (tchan_info->is_chan_passive)
				chan_info->info |= WMI_CHAN_INFO_PASSIVE;
			if (tchan_info->allow_he)
				chan_info->info |= WMI_CHAN_INFO_ALLOW_HE;
			else if (tchan_info->allow_vht)
				chan_info->info |= WMI_CHAN_INFO_ALLOW_VHT;
			else if (tchan_info->allow_ht)
				chan_info->info |= WMI_CHAN_INFO_ALLOW_HT;
			if (tchan_info->half_rate)
				chan_info->info |= WMI_CHAN_INFO_HALF_RATE;
			if (tchan_info->quarter_rate)
				chan_info->info |= WMI_CHAN_INFO_QUARTER_RATE;
			if (tchan_info->psc_channel)
				chan_info->info |= WMI_CHAN_INFO_PSC;
			if (tchan_info->dfs_set)
				chan_info->info |= WMI_CHAN_INFO_DFS;

			chan_info->info |= FIELD_PREP(WMI_CHAN_INFO_MODE,
						      tchan_info->phy_mode);
			*reg1 |= FIELD_PREP(WMI_CHAN_REG_INFO1_MIN_PWR,
					    tchan_info->minpower);
			*reg1 |= FIELD_PREP(WMI_CHAN_REG_INFO1_MAX_PWR,
					    tchan_info->maxpower);
			*reg1 |= FIELD_PREP(WMI_CHAN_REG_INFO1_MAX_REG_PWR,
					    tchan_info->maxregpower);
			*reg1 |= FIELD_PREP(WMI_CHAN_REG_INFO1_REG_CLS,
					    tchan_info->reg_class_id);
			*reg2 |= FIELD_PREP(WMI_CHAN_REG_INFO2_ANT_MAX,
					    tchan_info->antennamax);

			ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
				   "WMI chan scan list chan[%d] = %u, chan_info->info %8x\n",
				   i, chan_info->mhz, chan_info->info);

			ptr += sizeof(*chan_info);

			tchan_info++;
		}

		ret = ath11k_wmi_cmd_send(wmi, skb, WMI_SCAN_CHAN_LIST_CMDID);
		if (ret) {
			ath11k_warn(ar->ab, "failed to send WMI_SCAN_CHAN_LIST cmd\n");
			dev_kfree_skb(skb);
			return ret;
		}

		num_sends++;
	}

	return 0;
}

int ath11k_wmi_send_wmm_update_cmd_tlv(struct ath11k *ar, u32 vdev_id,
				       struct wmi_wmm_params_all_arg *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_vdev_set_wmm_params_cmd *cmd;
	struct wmi_wmm_params *wmm_param;
	struct wmi_wmm_params_arg *wmi_wmm_arg;
	struct sk_buff *skb;
	int ret, ac;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_set_wmm_params_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_VDEV_SET_WMM_PARAMS_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = vdev_id;
	cmd->wmm_param_type = 0;

	for (ac = 0; ac < WME_NUM_AC; ac++) {
		switch (ac) {
		case WME_AC_BE:
			wmi_wmm_arg = &param->ac_be;
			break;
		case WME_AC_BK:
			wmi_wmm_arg = &param->ac_bk;
			break;
		case WME_AC_VI:
			wmi_wmm_arg = &param->ac_vi;
			break;
		case WME_AC_VO:
			wmi_wmm_arg = &param->ac_vo;
			break;
		}

		wmm_param = (struct wmi_wmm_params *)&cmd->wmm_params[ac];
		wmm_param->tlv_header =
				FIELD_PREP(WMI_TLV_TAG,
					   WMI_TAG_VDEV_SET_WMM_PARAMS_CMD) |
				FIELD_PREP(WMI_TLV_LEN,
					   sizeof(*wmm_param) - TLV_HDR_SIZE);

		wmm_param->aifs = wmi_wmm_arg->aifs;
		wmm_param->cwmin = wmi_wmm_arg->cwmin;
		wmm_param->cwmax = wmi_wmm_arg->cwmax;
		wmm_param->txoplimit = wmi_wmm_arg->txop;
		wmm_param->acm = wmi_wmm_arg->acm;
		wmm_param->no_ack = wmi_wmm_arg->no_ack;

		ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
			   "wmi wmm set ac %d aifs %d cwmin %d cwmax %d txop %d acm %d no_ack %d\n",
			   ac, wmm_param->aifs, wmm_param->cwmin,
			   wmm_param->cwmax, wmm_param->txoplimit,
			   wmm_param->acm, wmm_param->no_ack);
	}
	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_VDEV_SET_WMM_PARAMS_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_VDEV_SET_WMM_PARAMS_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_send_dfs_phyerr_offload_enable_cmd(struct ath11k *ar,
						  u32 pdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_dfs_phyerr_offload_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_dfs_phyerr_offload_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PDEV_DFS_PHYERR_OFFLOAD_ENABLE_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id = pdev_id;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI dfs phy err offload enable pdev id %d\n", pdev_id);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_DFS_PHYERR_OFFLOAD_ENABLE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_PDEV_DFS_PHYERR_OFFLOAD_ENABLE cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_delba_send(struct ath11k *ar, u32 vdev_id, const u8 *mac,
			  u32 tid, u32 initiator, u32 reason)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_delba_send_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_delba_send_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_DELBA_SEND_CMD) |
			FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	ether_addr_copy(cmd->peer_macaddr.addr, mac);
	cmd->tid = tid;
	cmd->initiator = initiator;
	cmd->reasoncode = reason;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi delba send vdev_id 0x%X mac_addr %pM tid %u initiator %u reason %u\n",
		   vdev_id, mac, tid, initiator, reason);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_DELBA_SEND_CMDID);

	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_DELBA_SEND_CMDID cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_addba_set_resp(struct ath11k *ar, u32 vdev_id, const u8 *mac,
			      u32 tid, u32 status)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_addba_setresponse_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_addba_setresponse_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ADDBA_SETRESPONSE_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	ether_addr_copy(cmd->peer_macaddr.addr, mac);
	cmd->tid = tid;
	cmd->statuscode = status;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi addba set resp vdev_id 0x%X mac_addr %pM tid %u status %u\n",
		   vdev_id, mac, tid, status);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_ADDBA_SET_RESP_CMDID);

	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_ADDBA_SET_RESP_CMDID cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_addba_send(struct ath11k *ar, u32 vdev_id, const u8 *mac,
			  u32 tid, u32 buf_size)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_addba_send_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_addba_send_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ADDBA_SEND_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	ether_addr_copy(cmd->peer_macaddr.addr, mac);
	cmd->tid = tid;
	cmd->buffersize = buf_size;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi addba send vdev_id 0x%X mac_addr %pM tid %u bufsize %u\n",
		   vdev_id, mac, tid, buf_size);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_ADDBA_SEND_CMDID);

	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_ADDBA_SEND_CMDID cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_addba_clear_resp(struct ath11k *ar, u32 vdev_id, const u8 *mac)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_addba_clear_resp_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_addba_clear_resp_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ADDBA_CLEAR_RESP_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	ether_addr_copy(cmd->peer_macaddr.addr, mac);

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi addba clear resp vdev_id 0x%X mac_addr %pM\n",
		   vdev_id, mac);

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_ADDBA_CLEAR_RESP_CMDID);

	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_ADDBA_CLEAR_RESP_CMDID cmd\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_pdev_peer_pktlog_filter(struct ath11k *ar, u8 *addr, u8 enable)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pdev_pktlog_filter_cmd *cmd;
	struct wmi_pdev_pktlog_filter_info *info;
	struct sk_buff *skb;
	struct wmi_tlv *tlv;
	void *ptr;
	int ret, len;

	len = sizeof(*cmd) + sizeof(*info) + TLV_HDR_SIZE;
	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_pktlog_filter_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_PEER_PKTLOG_FILTER_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id = DP_HW2SW_MACID(ar->pdev->pdev_id);
	cmd->num_mac = 1;
	cmd->enable = enable;

	ptr = skb->data + sizeof(*cmd);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, sizeof(*info));

	ptr += TLV_HDR_SIZE;
	info = ptr;

	ether_addr_copy(info->peer_macaddr.addr, addr);
	info->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_PEER_PKTLOG_FILTER_INFO) |
			   FIELD_PREP(WMI_TLV_LEN,
				      sizeof(*info) - TLV_HDR_SIZE);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_PKTLOG_FILTER_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_PKTLOG_ENABLE_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_send_init_country_cmd(struct ath11k *ar,
				 struct wmi_init_country_params init_cc_params)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_init_country_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_init_country_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_SET_INIT_COUNTRY_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id = ar->pdev->pdev_id;

	switch (init_cc_params.flags) {
	case ALPHA_IS_SET:
		cmd->init_cc_type = WMI_COUNTRY_INFO_TYPE_ALPHA;
		memcpy((u8 *)&cmd->cc_info.alpha2,
		       init_cc_params.cc_info.alpha2, 3);
		break;
	case CC_IS_SET:
		cmd->init_cc_type = WMI_COUNTRY_INFO_TYPE_COUNTRY_CODE;
		cmd->cc_info.country_code = init_cc_params.cc_info.country_code;
		break;
	case REGDMN_IS_SET:
		cmd->init_cc_type = WMI_COUNTRY_INFO_TYPE_REGDOMAIN;
		cmd->cc_info.regdom_id = init_cc_params.cc_info.regdom_id;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_SET_INIT_COUNTRY_CMDID);

out:
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send WMI_SET_INIT_COUNTRY CMD :%d\n",
			    ret);
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_send_thermal_mitigation_param_cmd(struct ath11k *ar,
					     struct thermal_mitigation_params *param)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_therm_throt_config_request_cmd *cmd;
	struct wmi_therm_throt_level_config_info *lvl_conf;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	int i, ret, len;

	len = sizeof(*cmd) + TLV_HDR_SIZE +
	      THERMAL_LEVELS * sizeof(struct wmi_therm_throt_level_config_info);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_therm_throt_config_request_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_THERM_THROT_CONFIG_REQUEST) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id = ar->pdev->pdev_id;
	cmd->enable = param->enable;
	cmd->dc = param->dc;
	cmd->dc_per_event = param->dc_per_event;
	cmd->therm_throt_levels = THERMAL_LEVELS;

	tlv = (struct wmi_tlv *)(skb->data + sizeof(*cmd));
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN,
				 (THERMAL_LEVELS *
				  sizeof(struct wmi_therm_throt_level_config_info)));

	lvl_conf = (struct wmi_therm_throt_level_config_info *)(skb->data +
								sizeof(*cmd) +
								TLV_HDR_SIZE);
	for (i = 0; i < THERMAL_LEVELS; i++) {
		lvl_conf->tlv_header =
			FIELD_PREP(WMI_TLV_TAG, WMI_TAG_THERM_THROT_LEVEL_CONFIG_INFO) |
			FIELD_PREP(WMI_TLV_LEN, sizeof(*lvl_conf) - TLV_HDR_SIZE);

		lvl_conf->temp_lwm = param->levelconf[i].tmplwm;
		lvl_conf->temp_hwm = param->levelconf[i].tmphwm;
		lvl_conf->dc_off_percent = param->levelconf[i].dcoffpercent;
		lvl_conf->prio = param->levelconf[i].priority;
		lvl_conf++;
	}

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_THERM_THROT_SET_CONF_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send THERM_THROT_SET_CONF cmd\n");
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev set thermal throt pdev_id %d enable %d dc %d dc_per_event %x levels %d\n",
		   ar->pdev->pdev_id, param->enable, param->dc,
		   param->dc_per_event, THERMAL_LEVELS);

	return ret;
}

int ath11k_wmi_pdev_pktlog_enable(struct ath11k *ar, u32 pktlog_filter)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pktlog_enable_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pktlog_enable_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_PKTLOG_ENABLE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id = DP_HW2SW_MACID(ar->pdev->pdev_id);
	cmd->evlist = pktlog_filter;
	cmd->enable = ATH11K_WMI_PKTLOG_ENABLE_FORCE;

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_PKTLOG_ENABLE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_PKTLOG_ENABLE_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_pdev_pktlog_disable(struct ath11k *ar)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_pktlog_disable_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pktlog_disable_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_PKTLOG_DISABLE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id = DP_HW2SW_MACID(ar->pdev->pdev_id);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_PKTLOG_DISABLE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_PDEV_PKTLOG_ENABLE_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_send_twt_enable_cmd(struct ath11k *ar, u32 pdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_twt_enable_params_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_twt_enable_params_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_TWT_ENABLE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = pdev_id;
	cmd->sta_cong_timer_ms = ATH11K_TWT_DEF_STA_CONG_TIMER_MS;
	cmd->default_slot_size = ATH11K_TWT_DEF_DEFAULT_SLOT_SIZE;
	cmd->congestion_thresh_setup = ATH11K_TWT_DEF_CONGESTION_THRESH_SETUP;
	cmd->congestion_thresh_teardown =
		ATH11K_TWT_DEF_CONGESTION_THRESH_TEARDOWN;
	cmd->congestion_thresh_critical =
		ATH11K_TWT_DEF_CONGESTION_THRESH_CRITICAL;
	cmd->interference_thresh_teardown =
		ATH11K_TWT_DEF_INTERFERENCE_THRESH_TEARDOWN;
	cmd->interference_thresh_setup =
		ATH11K_TWT_DEF_INTERFERENCE_THRESH_SETUP;
	cmd->min_no_sta_setup = ATH11K_TWT_DEF_MIN_NO_STA_SETUP;
	cmd->min_no_sta_teardown = ATH11K_TWT_DEF_MIN_NO_STA_TEARDOWN;
	cmd->no_of_bcast_mcast_slots = ATH11K_TWT_DEF_NO_OF_BCAST_MCAST_SLOTS;
	cmd->min_no_twt_slots = ATH11K_TWT_DEF_MIN_NO_TWT_SLOTS;
	cmd->max_no_sta_twt = ATH11K_TWT_DEF_MAX_NO_STA_TWT;
	cmd->mode_check_interval = ATH11K_TWT_DEF_MODE_CHECK_INTERVAL;
	cmd->add_sta_slot_interval = ATH11K_TWT_DEF_ADD_STA_SLOT_INTERVAL;
	cmd->remove_sta_slot_interval =
		ATH11K_TWT_DEF_REMOVE_STA_SLOT_INTERVAL;
	/* TODO add MBSSID support */
	cmd->mbss_support = 0;

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_TWT_ENABLE_CMDID);
	if (ret) {
		ath11k_warn(ab, "Failed to send WMI_TWT_ENABLE_CMDID");
		dev_kfree_skb(skb);
	}
	return ret;
}

int
ath11k_wmi_send_twt_disable_cmd(struct ath11k *ar, u32 pdev_id)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_twt_disable_params_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_twt_disable_params_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_TWT_DISABLE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = pdev_id;

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_TWT_DISABLE_CMDID);
	if (ret) {
		ath11k_warn(ab, "Failed to send WMI_TWT_DISABLE_CMDID");
		dev_kfree_skb(skb);
	}
	return ret;
}

int
ath11k_wmi_send_obss_spr_cmd(struct ath11k *ar, u32 vdev_id,
			     struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_obss_spatial_reuse_params_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_obss_spatial_reuse_params_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_OBSS_SPATIAL_REUSE_SET_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->enable = he_obss_pd->enable;
	cmd->obss_min = he_obss_pd->min_offset;
	cmd->obss_max = he_obss_pd->max_offset;

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_OBSS_PD_SPATIAL_REUSE_CMDID);
	if (ret) {
		ath11k_warn(ab,
			    "Failed to send WMI_PDEV_OBSS_PD_SPATIAL_REUSE_CMDID");
		dev_kfree_skb(skb);
	}
	return ret;
}

int
ath11k_wmi_pdev_set_srg_bss_color_bitmap(struct ath11k *ar, u32 *bitmap)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_pdev_obss_pd_bitmap_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_obss_pd_bitmap_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_PDEV_SRG_BSS_COLOR_BITMAP_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	memcpy(cmd->bitmap, bitmap, sizeof(cmd->bitmap));

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "obss pd pdev_id %d bss color bitmap %08x %08x\n",
		   cmd->pdev_id, cmd->bitmap[0], cmd->bitmap[1]);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_SET_SRG_BSS_COLOR_BITMAP_CMDID);
	if (ret) {
		ath11k_warn(ab,
			    "failed to send WMI_PDEV_SET_SRG_BSS_COLOR_BITMAP_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_pdev_set_srg_patial_bssid_bitmap(struct ath11k *ar, u32 *bitmap)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_pdev_obss_pd_bitmap_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_obss_pd_bitmap_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PDEV_SRG_PARTIAL_BSSID_BITMAP_CMD) |
		FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	memcpy(cmd->bitmap, bitmap, sizeof(cmd->bitmap));

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "obss pd pdev_id %d partial bssid bitmap %08x %08x\n",
		   cmd->pdev_id, cmd->bitmap[0], cmd->bitmap[1]);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_SET_SRG_PARTIAL_BSSID_BITMAP_CMDID);
	if (ret) {
		ath11k_warn(ab,
			    "failed to send WMI_PDEV_SET_SRG_PARTIAL_BSSID_BITMAP_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_pdev_srg_obss_color_enable_bitmap(struct ath11k *ar, u32 *bitmap)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_pdev_obss_pd_bitmap_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_obss_pd_bitmap_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PDEV_SRG_OBSS_COLOR_ENABLE_BITMAP_CMD) |
		FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	memcpy(cmd->bitmap, bitmap, sizeof(cmd->bitmap));

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "obss pd srg pdev_id %d bss color enable bitmap %08x %08x\n",
		   cmd->pdev_id, cmd->bitmap[0], cmd->bitmap[1]);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_SET_SRG_OBSS_COLOR_ENABLE_BITMAP_CMDID);
	if (ret) {
		ath11k_warn(ab,
			    "failed to send WMI_PDEV_SET_SRG_OBSS_COLOR_ENABLE_BITMAP_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_pdev_srg_obss_bssid_enable_bitmap(struct ath11k *ar, u32 *bitmap)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_pdev_obss_pd_bitmap_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_obss_pd_bitmap_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PDEV_SRG_OBSS_BSSID_ENABLE_BITMAP_CMD) |
		FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	memcpy(cmd->bitmap, bitmap, sizeof(cmd->bitmap));

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "obss pd srg pdev_id %d bssid enable bitmap %08x %08x\n",
		   cmd->pdev_id, cmd->bitmap[0], cmd->bitmap[1]);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_SET_SRG_OBSS_BSSID_ENABLE_BITMAP_CMDID);
	if (ret) {
		ath11k_warn(ab,
			    "failed to send WMI_PDEV_SET_SRG_OBSS_BSSID_ENABLE_BITMAP_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_pdev_non_srg_obss_color_enable_bitmap(struct ath11k *ar, u32 *bitmap)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_pdev_obss_pd_bitmap_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_obss_pd_bitmap_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PDEV_NON_SRG_OBSS_COLOR_ENABLE_BITMAP_CMD) |
		FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	memcpy(cmd->bitmap, bitmap, sizeof(cmd->bitmap));

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "obss pd non_srg pdev_id %d bss color enable bitmap %08x %08x\n",
		   cmd->pdev_id, cmd->bitmap[0], cmd->bitmap[1]);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_SET_NON_SRG_OBSS_COLOR_ENABLE_BITMAP_CMDID);
	if (ret) {
		ath11k_warn(ab,
			    "failed to send WMI_PDEV_SET_NON_SRG_OBSS_COLOR_ENABLE_BITMAP_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_pdev_non_srg_obss_bssid_enable_bitmap(struct ath11k *ar, u32 *bitmap)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_pdev_obss_pd_bitmap_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_obss_pd_bitmap_cmd *)skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PDEV_NON_SRG_OBSS_BSSID_ENABLE_BITMAP_CMD) |
		FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	memcpy(cmd->bitmap, bitmap, sizeof(cmd->bitmap));

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "obss pd non_srg pdev_id %d bssid enable bitmap %08x %08x\n",
		   cmd->pdev_id, cmd->bitmap[0], cmd->bitmap[1]);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_SET_NON_SRG_OBSS_BSSID_ENABLE_BITMAP_CMDID);
	if (ret) {
		ath11k_warn(ab,
			    "failed to send WMI_PDEV_SET_NON_SRG_OBSS_BSSID_ENABLE_BITMAP_CMDID");
		dev_kfree_skb(skb);
	}

	return ret;
}

int
ath11k_wmi_send_obss_color_collision_cfg_cmd(struct ath11k *ar, u32 vdev_id,
					     u8 bss_color, u32 period,
					     bool enable)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_obss_color_collision_cfg_params_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_obss_color_collision_cfg_params_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_OBSS_COLOR_COLLISION_DET_CONFIG) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->evt_type = enable ? ATH11K_OBSS_COLOR_COLLISION_DETECTION :
				 ATH11K_OBSS_COLOR_COLLISION_DETECTION_DISABLE;
	cmd->current_bss_color = bss_color;
	cmd->detection_period_ms = period;
	cmd->scan_period_ms = ATH11K_BSS_COLOR_COLLISION_SCAN_PERIOD_MS;
	cmd->free_slot_expiry_time_ms = 0;
	cmd->flags = 0;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi_send_obss_color_collision_cfg id %d type %d bss_color %d detect_period %d scan_period %d\n",
		   cmd->vdev_id, cmd->evt_type, cmd->current_bss_color,
		   cmd->detection_period_ms, cmd->scan_period_ms);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_OBSS_COLOR_COLLISION_DET_CONFIG_CMDID);
	if (ret) {
		ath11k_warn(ab, "Failed to send WMI_OBSS_COLOR_COLLISION_DET_CONFIG_CMDID");
		dev_kfree_skb(skb);
	}
	return ret;
}

int ath11k_wmi_send_bss_color_change_enable_cmd(struct ath11k *ar, u32 vdev_id,
						bool enable)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct wmi_bss_color_change_enable_params_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_bss_color_change_enable_params_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_BSS_COLOR_CHANGE_ENABLE) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->enable = enable ? 1 : 0;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "wmi_send_bss_color_change_enable id %d enable %d\n",
		   cmd->vdev_id, cmd->enable);

	ret = ath11k_wmi_cmd_send(wmi, skb,
				  WMI_BSS_COLOR_CHANGE_ENABLE_CMDID);
	if (ret) {
		ath11k_warn(ab, "Failed to send WMI_BSS_COLOR_CHANGE_ENABLE_CMDID");
		dev_kfree_skb(skb);
	}
	return ret;
}

int ath11k_wmi_fils_discovery_tmpl(struct ath11k *ar, u32 vdev_id,
				   struct sk_buff *tmpl)
{
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	int ret, len;
	size_t aligned_len;
	struct wmi_fils_discovery_tmpl_cmd *cmd;

	aligned_len = roundup(tmpl->len, 4);
	len = sizeof(*cmd) + TLV_HDR_SIZE + aligned_len;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev %i set FILS discovery template\n", vdev_id);

	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_fils_discovery_tmpl_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_FILS_DISCOVERY_TMPL_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->buf_len = tmpl->len;
	ptr = skb->data + sizeof(*cmd);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
		      FIELD_PREP(WMI_TLV_LEN, aligned_len);
	memcpy(tlv->value, tmpl->data, tmpl->len);

	ret = ath11k_wmi_cmd_send(ar->wmi, skb, WMI_FILS_DISCOVERY_TMPL_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "WMI vdev %i failed to send FILS discovery template command\n",
			    vdev_id);
		dev_kfree_skb(skb);
	}
	return ret;
}

int ath11k_wmi_probe_resp_tmpl(struct ath11k *ar, u32 vdev_id,
			       struct sk_buff *tmpl)
{
	struct wmi_probe_tmpl_cmd *cmd;
	struct wmi_bcn_prb_info *probe_info;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	int ret, len;
	size_t aligned_len = roundup(tmpl->len, 4);

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev %i set probe response template\n", vdev_id);

	len = sizeof(*cmd) + sizeof(*probe_info) + TLV_HDR_SIZE + aligned_len;

	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_probe_tmpl_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PRB_TMPL_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->buf_len = tmpl->len;

	ptr = skb->data + sizeof(*cmd);

	probe_info = ptr;
	len = sizeof(*probe_info);
	probe_info->tlv_header = FIELD_PREP(WMI_TLV_TAG,
					    WMI_TAG_BCN_PRB_INFO) |
				 FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	probe_info->caps = 0;
	probe_info->erp = 0;

	ptr += sizeof(*probe_info);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
		      FIELD_PREP(WMI_TLV_LEN, aligned_len);
	memcpy(tlv->value, tmpl->data, tmpl->len);

	ret = ath11k_wmi_cmd_send(ar->wmi, skb, WMI_PRB_TMPL_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "WMI vdev %i failed to send probe response template command\n",
			    vdev_id);
		dev_kfree_skb(skb);
	}
	return ret;
}

int ath11k_wmi_fils_discovery(struct ath11k *ar, u32 vdev_id, u32 interval,
			      bool unsol_bcast_probe_resp_enabled)
{
	struct sk_buff *skb;
	int ret, len;
	struct wmi_fils_discovery_cmd *cmd;

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI vdev %i set %s interval to %u TU\n",
		   vdev_id, unsol_bcast_probe_resp_enabled ?
		   "unsolicited broadcast probe response" : "FILS discovery",
		   interval);

	len = sizeof(*cmd);
	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_fils_discovery_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ENABLE_FILS_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, len - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	cmd->interval = interval;
	cmd->config = unsol_bcast_probe_resp_enabled;

	ret = ath11k_wmi_cmd_send(ar->wmi, skb, WMI_ENABLE_FILS_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "WMI vdev %i failed to send FILS discovery enable/disable command\n",
			    vdev_id);
		dev_kfree_skb(skb);
	}
	return ret;
}

static void
ath11k_fill_band_to_mac_param(struct ath11k_base  *soc,
			      struct wmi_host_pdev_band_to_mac *band_to_mac)
{
	u8 i;
	struct ath11k_hal_reg_capabilities_ext *hal_reg_cap;
	struct ath11k_pdev *pdev;

	for (i = 0; i < soc->num_radios; i++) {
		pdev = &soc->pdevs[i];
		hal_reg_cap = &soc->hal_reg_cap[i];
		band_to_mac[i].pdev_id = pdev->pdev_id;

		switch (pdev->cap.supported_bands) {
		case WMI_HOST_WLAN_2G_5G_CAP:
			band_to_mac[i].start_freq = hal_reg_cap->low_2ghz_chan;
			band_to_mac[i].end_freq = hal_reg_cap->high_5ghz_chan;
			break;
		case WMI_HOST_WLAN_2G_CAP:
			band_to_mac[i].start_freq = hal_reg_cap->low_2ghz_chan;
			band_to_mac[i].end_freq = hal_reg_cap->high_2ghz_chan;
			break;
		case WMI_HOST_WLAN_5G_CAP:
			band_to_mac[i].start_freq = hal_reg_cap->low_5ghz_chan;
			band_to_mac[i].end_freq = hal_reg_cap->high_5ghz_chan;
			break;
		default:
			break;
		}
	}
}

static void
ath11k_wmi_copy_resource_config(struct wmi_resource_config *wmi_cfg,
				struct target_resource_config *tg_cfg)
{
	wmi_cfg->num_vdevs = tg_cfg->num_vdevs;
	wmi_cfg->num_peers = tg_cfg->num_peers;
	wmi_cfg->num_offload_peers = tg_cfg->num_offload_peers;
	wmi_cfg->num_offload_reorder_buffs = tg_cfg->num_offload_reorder_buffs;
	wmi_cfg->num_peer_keys = tg_cfg->num_peer_keys;
	wmi_cfg->num_tids = tg_cfg->num_tids;
	wmi_cfg->ast_skid_limit = tg_cfg->ast_skid_limit;
	wmi_cfg->tx_chain_mask = tg_cfg->tx_chain_mask;
	wmi_cfg->rx_chain_mask = tg_cfg->rx_chain_mask;
	wmi_cfg->rx_timeout_pri[0] = tg_cfg->rx_timeout_pri[0];
	wmi_cfg->rx_timeout_pri[1] = tg_cfg->rx_timeout_pri[1];
	wmi_cfg->rx_timeout_pri[2] = tg_cfg->rx_timeout_pri[2];
	wmi_cfg->rx_timeout_pri[3] = tg_cfg->rx_timeout_pri[3];
	wmi_cfg->rx_decap_mode = tg_cfg->rx_decap_mode;
	wmi_cfg->scan_max_pending_req = tg_cfg->scan_max_pending_req;
	wmi_cfg->bmiss_offload_max_vdev = tg_cfg->bmiss_offload_max_vdev;
	wmi_cfg->roam_offload_max_vdev = tg_cfg->roam_offload_max_vdev;
	wmi_cfg->roam_offload_max_ap_profiles =
		tg_cfg->roam_offload_max_ap_profiles;
	wmi_cfg->num_mcast_groups = tg_cfg->num_mcast_groups;
	wmi_cfg->num_mcast_table_elems = tg_cfg->num_mcast_table_elems;
	wmi_cfg->mcast2ucast_mode = tg_cfg->mcast2ucast_mode;
	wmi_cfg->tx_dbg_log_size = tg_cfg->tx_dbg_log_size;
	wmi_cfg->num_wds_entries = tg_cfg->num_wds_entries;
	wmi_cfg->dma_burst_size = tg_cfg->dma_burst_size;
	wmi_cfg->mac_aggr_delim = tg_cfg->mac_aggr_delim;
	wmi_cfg->rx_skip_defrag_timeout_dup_detection_check =
		tg_cfg->rx_skip_defrag_timeout_dup_detection_check;
	wmi_cfg->vow_config = tg_cfg->vow_config;
	wmi_cfg->gtk_offload_max_vdev = tg_cfg->gtk_offload_max_vdev;
	wmi_cfg->num_msdu_desc = tg_cfg->num_msdu_desc;
	wmi_cfg->max_frag_entries = tg_cfg->max_frag_entries;
	wmi_cfg->num_tdls_vdevs = tg_cfg->num_tdls_vdevs;
	wmi_cfg->num_tdls_conn_table_entries =
		tg_cfg->num_tdls_conn_table_entries;
	wmi_cfg->beacon_tx_offload_max_vdev =
		tg_cfg->beacon_tx_offload_max_vdev;
	wmi_cfg->num_multicast_filter_entries =
		tg_cfg->num_multicast_filter_entries;
	wmi_cfg->num_wow_filters = tg_cfg->num_wow_filters;
	wmi_cfg->num_keep_alive_pattern = tg_cfg->num_keep_alive_pattern;
	wmi_cfg->keep_alive_pattern_size = tg_cfg->keep_alive_pattern_size;
	wmi_cfg->max_tdls_concurrent_sleep_sta =
		tg_cfg->max_tdls_concurrent_sleep_sta;
	wmi_cfg->max_tdls_concurrent_buffer_sta =
		tg_cfg->max_tdls_concurrent_buffer_sta;
	wmi_cfg->wmi_send_separate = tg_cfg->wmi_send_separate;
	wmi_cfg->num_ocb_vdevs = tg_cfg->num_ocb_vdevs;
	wmi_cfg->num_ocb_channels = tg_cfg->num_ocb_channels;
	wmi_cfg->num_ocb_schedules = tg_cfg->num_ocb_schedules;
	wmi_cfg->bpf_instruction_size = tg_cfg->bpf_instruction_size;
	wmi_cfg->max_bssid_rx_filters = tg_cfg->max_bssid_rx_filters;
	wmi_cfg->use_pdev_id = tg_cfg->use_pdev_id;
	wmi_cfg->flag1 = tg_cfg->flag1;
	wmi_cfg->peer_map_unmap_v2_support = tg_cfg->peer_map_unmap_v2_support;
	wmi_cfg->sched_params = tg_cfg->sched_params;
	wmi_cfg->twt_ap_pdev_count = tg_cfg->twt_ap_pdev_count;
	wmi_cfg->twt_ap_sta_count = tg_cfg->twt_ap_sta_count;
}

static int ath11k_init_cmd_send(struct ath11k_pdev_wmi *wmi,
				struct wmi_init_cmd_param *param)
{
	struct ath11k_base *ab = wmi->wmi_ab->ab;
	struct sk_buff *skb;
	struct wmi_init_cmd *cmd;
	struct wmi_resource_config *cfg;
	struct wmi_pdev_set_hw_mode_cmd_param *hw_mode;
	struct wmi_pdev_band_to_mac *band_to_mac;
	struct wlan_host_mem_chunk *host_mem_chunks;
	struct wmi_tlv *tlv;
	size_t ret, len;
	void *ptr;
	u32 hw_mode_len = 0;
	u16 idx;

	if (param->hw_mode_id != WMI_HOST_HW_MODE_MAX)
		hw_mode_len = sizeof(*hw_mode) + TLV_HDR_SIZE +
			      (param->num_band_to_mac * sizeof(*band_to_mac));

	len = sizeof(*cmd) + TLV_HDR_SIZE + sizeof(*cfg) + hw_mode_len +
	      (param->num_mem_chunks ? (sizeof(*host_mem_chunks) * WMI_MAX_MEM_REQS) : 0);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_init_cmd *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_INIT_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	ptr = skb->data + sizeof(*cmd);
	cfg = ptr;

	ath11k_wmi_copy_resource_config(cfg, param->res_cfg);

	cfg->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_RESOURCE_CONFIG) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cfg) - TLV_HDR_SIZE);

	ptr += sizeof(*cfg);
	host_mem_chunks = ptr + TLV_HDR_SIZE;
	len = sizeof(struct wlan_host_mem_chunk);

	for (idx = 0; idx < param->num_mem_chunks; ++idx) {
		host_mem_chunks[idx].tlv_header =
				FIELD_PREP(WMI_TLV_TAG,
					   WMI_TAG_WLAN_HOST_MEMORY_CHUNK) |
				FIELD_PREP(WMI_TLV_LEN, len);

		host_mem_chunks[idx].ptr = param->mem_chunks[idx].paddr;
		host_mem_chunks[idx].size = param->mem_chunks[idx].len;
		host_mem_chunks[idx].req_id = param->mem_chunks[idx].req_id;

		ath11k_dbg(ab, ATH11K_DBG_WMI,
			   "WMI host mem chunk req_id %d paddr 0x%llx len %d\n",
			   param->mem_chunks[idx].req_id,
			   (u64)param->mem_chunks[idx].paddr,
			   param->mem_chunks[idx].len);
	}
	cmd->num_host_mem_chunks = param->num_mem_chunks;
	len = sizeof(struct wlan_host_mem_chunk) * param->num_mem_chunks;

	/* num_mem_chunks is zero */
	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, len);
	ptr += TLV_HDR_SIZE + len;

	if (param->hw_mode_id != WMI_HOST_HW_MODE_MAX) {
		hw_mode = (struct wmi_pdev_set_hw_mode_cmd_param *)ptr;
		hw_mode->tlv_header = FIELD_PREP(WMI_TLV_TAG,
						 WMI_TAG_PDEV_SET_HW_MODE_CMD) |
				      FIELD_PREP(WMI_TLV_LEN,
						 sizeof(*hw_mode) - TLV_HDR_SIZE);

		hw_mode->hw_mode_index = param->hw_mode_id;
		hw_mode->num_band_to_mac = param->num_band_to_mac;

		ptr += sizeof(*hw_mode);

		len = param->num_band_to_mac * sizeof(*band_to_mac);
		tlv = ptr;
		tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
			      FIELD_PREP(WMI_TLV_LEN, len);

		ptr += TLV_HDR_SIZE;
		len = sizeof(*band_to_mac);

		for (idx = 0; idx < param->num_band_to_mac; idx++) {
			band_to_mac = (void *)ptr;

			band_to_mac->tlv_header = FIELD_PREP(WMI_TLV_TAG,
							     WMI_TAG_PDEV_BAND_TO_MAC) |
						  FIELD_PREP(WMI_TLV_LEN,
							     len - TLV_HDR_SIZE);
			band_to_mac->pdev_id = param->band_to_mac[idx].pdev_id;
			band_to_mac->start_freq =
				param->band_to_mac[idx].start_freq;
			band_to_mac->end_freq =
				param->band_to_mac[idx].end_freq;
			ptr += sizeof(*band_to_mac);
		}
	}

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_INIT_CMDID);
	if (ret) {
		ath11k_warn(ab, "failed to send WMI_INIT_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_pdev_lro_cfg(struct ath11k *ar,
			    int pdev_id)
{
	struct ath11k_wmi_pdev_lro_config_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct ath11k_wmi_pdev_lro_config_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_LRO_INFO_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	get_random_bytes(cmd->th_4, sizeof(uint32_t) * ATH11K_IPV4_TH_SEED_SIZE);
	get_random_bytes(cmd->th_6, sizeof(uint32_t) * ATH11K_IPV6_TH_SEED_SIZE);

	cmd->pdev_id = pdev_id;

	ret = ath11k_wmi_cmd_send(ar->wmi, skb, WMI_LRO_CONFIG_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send lro cfg req wmi cmd\n");
		goto err;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI lro cfg cmd pdev_id 0x%x\n", pdev_id);
	return 0;
err:
	dev_kfree_skb(skb);
	return ret;
}

int ath11k_wmi_wait_for_service_ready(struct ath11k_base *ab)
{
	unsigned long time_left;

	time_left = wait_for_completion_timeout(&ab->wmi_ab.service_ready,
						WMI_SERVICE_READY_TIMEOUT_HZ);
	if (!time_left)
		return -ETIMEDOUT;

	return 0;
}

int ath11k_wmi_wait_for_unified_ready(struct ath11k_base *ab)
{
	unsigned long time_left;

	time_left = wait_for_completion_timeout(&ab->wmi_ab.unified_ready,
						WMI_SERVICE_READY_TIMEOUT_HZ);
	if (!time_left)
		return -ETIMEDOUT;

	return 0;
}

int ath11k_wmi_set_hw_mode(struct ath11k_base *ab,
			   enum wmi_host_hw_mode_config_type mode)
{
	struct wmi_pdev_set_hw_mode_cmd_param *cmd;
	struct sk_buff *skb;
	struct ath11k_wmi_base *wmi_ab = &ab->wmi_ab;
	int len;
	int ret;

	len = sizeof(*cmd);

	skb = ath11k_wmi_alloc_skb(wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_hw_mode_cmd_param *)skb->data;

	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_SET_HW_MODE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id = WMI_PDEV_ID_SOC;
	cmd->hw_mode_index = mode;

	ret = ath11k_wmi_cmd_send(&wmi_ab->wmi[0], skb, WMI_PDEV_SET_HW_MODE_CMDID);
	if (ret) {
		ath11k_warn(ab, "failed to send WMI_PDEV_SET_HW_MODE_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

int ath11k_wmi_cmd_init(struct ath11k_base *ab)
{
	struct ath11k_wmi_base *wmi_sc = &ab->wmi_ab;
	struct wmi_init_cmd_param init_param;
	struct target_resource_config  config;

	memset(&init_param, 0, sizeof(init_param));
	memset(&config, 0, sizeof(config));

	ab->hw_params.hw_ops->wmi_init_config(ab, &config);

	memcpy(&wmi_sc->wlan_resource_config, &config, sizeof(config));

	init_param.res_cfg = &wmi_sc->wlan_resource_config;
	init_param.num_mem_chunks = wmi_sc->num_mem_chunks;
	init_param.hw_mode_id = wmi_sc->preferred_hw_mode;
	init_param.mem_chunks = wmi_sc->mem_chunks;

	if (ab->hw_params.single_pdev_only)
		init_param.hw_mode_id = WMI_HOST_HW_MODE_MAX;

	init_param.num_band_to_mac = ab->num_radios;
	ath11k_fill_band_to_mac_param(ab, init_param.band_to_mac);

	return ath11k_init_cmd_send(&wmi_sc->wmi[0], &init_param);
}

int ath11k_wmi_vdev_spectral_conf(struct ath11k *ar,
				  struct ath11k_wmi_vdev_spectral_conf_param *param)
{
	struct ath11k_wmi_vdev_spectral_conf_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct ath11k_wmi_vdev_spectral_conf_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_VDEV_SPECTRAL_CONFIGURE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	memcpy(&cmd->param, param, sizeof(*param));

	ret = ath11k_wmi_cmd_send(ar->wmi, skb,
				  WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send spectral scan config wmi cmd\n");
		goto err;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI spectral scan config cmd vdev_id 0x%x\n",
		   param->vdev_id);

	return 0;
err:
	dev_kfree_skb(skb);
	return ret;
}

int ath11k_wmi_vdev_spectral_enable(struct ath11k *ar, u32 vdev_id,
				    u32 trigger, u32 enable)
{
	struct ath11k_wmi_vdev_spectral_enable_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct ath11k_wmi_vdev_spectral_enable_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_VDEV_SPECTRAL_ENABLE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = vdev_id;
	cmd->trigger_cmd = trigger;
	cmd->enable_cmd = enable;

	ret = ath11k_wmi_cmd_send(ar->wmi, skb,
				  WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send spectral enable wmi cmd\n");
		goto err;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI spectral enable cmd vdev id 0x%x\n",
		   vdev_id);

	return 0;
err:
	dev_kfree_skb(skb);
	return ret;
}

int ath11k_wmi_pdev_dma_ring_cfg(struct ath11k *ar,
				 struct ath11k_wmi_pdev_dma_ring_cfg_req_cmd *param)
{
	struct ath11k_wmi_pdev_dma_ring_cfg_req_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct ath11k_wmi_pdev_dma_ring_cfg_req_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_DMA_RING_CFG_REQ) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->pdev_id		= param->pdev_id;
	cmd->module_id		= param->module_id;
	cmd->base_paddr_lo	= param->base_paddr_lo;
	cmd->base_paddr_hi	= param->base_paddr_hi;
	cmd->head_idx_paddr_lo	= param->head_idx_paddr_lo;
	cmd->head_idx_paddr_hi	= param->head_idx_paddr_hi;
	cmd->tail_idx_paddr_lo	= param->tail_idx_paddr_lo;
	cmd->tail_idx_paddr_hi	= param->tail_idx_paddr_hi;
	cmd->num_elems		= param->num_elems;
	cmd->buf_size		= param->buf_size;
	cmd->num_resp_per_event	= param->num_resp_per_event;
	cmd->event_timeout_ms	= param->event_timeout_ms;

	ret = ath11k_wmi_cmd_send(ar->wmi, skb,
				  WMI_PDEV_DMA_RING_CFG_REQ_CMDID);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send dma ring cfg req wmi cmd\n");
		goto err;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI DMA ring cfg req cmd pdev_id 0x%x\n",
		   param->pdev_id);

	return 0;
err:
	dev_kfree_skb(skb);
	return ret;
}

static int ath11k_wmi_tlv_dma_buf_entry_parse(struct ath11k_base *soc,
					      u16 tag, u16 len,
					      const void *ptr, void *data)
{
	struct wmi_tlv_dma_buf_release_parse *parse = data;

	if (tag != WMI_TAG_DMA_BUF_RELEASE_ENTRY)
		return -EPROTO;

	if (parse->num_buf_entry >= parse->fixed.num_buf_release_entry)
		return -ENOBUFS;

	parse->num_buf_entry++;
	return 0;
}

static int ath11k_wmi_tlv_dma_buf_meta_parse(struct ath11k_base *soc,
					     u16 tag, u16 len,
					     const void *ptr, void *data)
{
	struct wmi_tlv_dma_buf_release_parse *parse = data;

	if (tag != WMI_TAG_DMA_BUF_RELEASE_SPECTRAL_META_DATA)
		return -EPROTO;

	if (parse->num_meta >= parse->fixed.num_meta_data_entry)
		return -ENOBUFS;

	parse->num_meta++;
	return 0;
}

static int ath11k_wmi_tlv_dma_buf_parse(struct ath11k_base *ab,
					u16 tag, u16 len,
					const void *ptr, void *data)
{
	struct wmi_tlv_dma_buf_release_parse *parse = data;
	int ret;

	switch (tag) {
	case WMI_TAG_DMA_BUF_RELEASE:
		memcpy(&parse->fixed, ptr,
		       sizeof(struct ath11k_wmi_dma_buf_release_fixed_param));
		parse->fixed.pdev_id = DP_HW2SW_MACID(parse->fixed.pdev_id);
		break;
	case WMI_TAG_ARRAY_STRUCT:
		if (!parse->buf_entry_done) {
			parse->num_buf_entry = 0;
			parse->buf_entry = (struct wmi_dma_buf_release_entry *)ptr;

			ret = ath11k_wmi_tlv_iter(ab, ptr, len,
						  ath11k_wmi_tlv_dma_buf_entry_parse,
						  parse);
			if (ret) {
				ath11k_warn(ab, "failed to parse dma buf entry tlv %d\n",
					    ret);
				return ret;
			}

			parse->buf_entry_done = true;
		} else if (!parse->meta_data_done) {
			parse->num_meta = 0;
			parse->meta_data = (struct wmi_dma_buf_release_meta_data *)ptr;

			ret = ath11k_wmi_tlv_iter(ab, ptr, len,
						  ath11k_wmi_tlv_dma_buf_meta_parse,
						  parse);
			if (ret) {
				ath11k_warn(ab, "failed to parse dma buf meta tlv %d\n",
					    ret);
				return ret;
			}

			parse->meta_data_done = true;
		}
		break;
	default:
		break;
	}
	return 0;
}

static void ath11k_wmi_pdev_dma_ring_buf_release_event(struct ath11k_base *ab,
						       struct sk_buff *skb)
{
	struct wmi_tlv_dma_buf_release_parse parse = { };
	struct ath11k_dbring_buf_release_event param;
	int ret;

	ret = ath11k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath11k_wmi_tlv_dma_buf_parse,
				  &parse);
	if (ret) {
		ath11k_warn(ab, "failed to parse dma buf release tlv %d\n", ret);
		return;
	}

	param.fixed		= parse.fixed;
	param.buf_entry		= parse.buf_entry;
	param.num_buf_entry	= parse.num_buf_entry;
	param.meta_data		= parse.meta_data;
	param.num_meta		= parse.num_meta;

	ret = ath11k_dbring_buffer_release_event(ab, &param);
	if (ret) {
		ath11k_warn(ab, "failed to handle dma buf release event %d\n", ret);
		return;
	}
}

static int ath11k_wmi_tlv_hw_mode_caps_parse(struct ath11k_base *soc,
					     u16 tag, u16 len,
					     const void *ptr, void *data)
{
	struct wmi_tlv_svc_rdy_ext_parse *svc_rdy_ext = data;
	struct wmi_hw_mode_capabilities *hw_mode_cap;
	u32 phy_map = 0;

	if (tag != WMI_TAG_HW_MODE_CAPABILITIES)
		return -EPROTO;

	if (svc_rdy_ext->n_hw_mode_caps >= svc_rdy_ext->param.num_hw_modes)
		return -ENOBUFS;

	hw_mode_cap = container_of(ptr, struct wmi_hw_mode_capabilities,
				   hw_mode_id);
	svc_rdy_ext->n_hw_mode_caps++;

	phy_map = hw_mode_cap->phy_id_map;
	while (phy_map) {
		svc_rdy_ext->tot_phy_id++;
		phy_map = phy_map >> 1;
	}

	return 0;
}

static int ath11k_wmi_tlv_hw_mode_caps(struct ath11k_base *soc,
				       u16 len, const void *ptr, void *data)
{
	struct wmi_tlv_svc_rdy_ext_parse *svc_rdy_ext = data;
	struct wmi_hw_mode_capabilities *hw_mode_caps;
	enum wmi_host_hw_mode_config_type mode, pref;
	u32 i;
	int ret;

	svc_rdy_ext->n_hw_mode_caps = 0;
	svc_rdy_ext->hw_mode_caps = (struct wmi_hw_mode_capabilities *)ptr;

	ret = ath11k_wmi_tlv_iter(soc, ptr, len,
				  ath11k_wmi_tlv_hw_mode_caps_parse,
				  svc_rdy_ext);
	if (ret) {
		ath11k_warn(soc, "failed to parse tlv %d\n", ret);
		return ret;
	}

	i = 0;
	while (i < svc_rdy_ext->n_hw_mode_caps) {
		hw_mode_caps = &svc_rdy_ext->hw_mode_caps[i];
		mode = hw_mode_caps->hw_mode_id;
		pref = soc->wmi_ab.preferred_hw_mode;

		if (ath11k_hw_mode_pri_map[mode] < ath11k_hw_mode_pri_map[pref]) {
			svc_rdy_ext->pref_hw_mode_caps = *hw_mode_caps;
			soc->wmi_ab.preferred_hw_mode = mode;
		}
		i++;
	}

	ath11k_dbg(soc, ATH11K_DBG_WMI, "preferred_hw_mode:%d\n",
		   soc->wmi_ab.preferred_hw_mode);
	if (soc->wmi_ab.preferred_hw_mode == WMI_HOST_HW_MODE_MAX)
		return -EINVAL;

	return 0;
}

static int ath11k_wmi_tlv_mac_phy_caps_parse(struct ath11k_base *soc,
					     u16 tag, u16 len,
					     const void *ptr, void *data)
{
	struct wmi_tlv_svc_rdy_ext_parse *svc_rdy_ext = data;

	if (tag != WMI_TAG_MAC_PHY_CAPABILITIES)
		return -EPROTO;

	if (svc_rdy_ext->n_mac_phy_caps >= svc_rdy_ext->tot_phy_id)
		return -ENOBUFS;

	len = min_t(u16, len, sizeof(struct wmi_mac_phy_capabilities));
	if (!svc_rdy_ext->n_mac_phy_caps) {
		svc_rdy_ext->mac_phy_caps = kcalloc(svc_rdy_ext->tot_phy_id,
						    len, GFP_ATOMIC);
		if (!svc_rdy_ext->mac_phy_caps)
			return -ENOMEM;
	}

	memcpy(svc_rdy_ext->mac_phy_caps + svc_rdy_ext->n_mac_phy_caps, ptr, len);
	svc_rdy_ext->n_mac_phy_caps++;
	return 0;
}

static int ath11k_wmi_tlv_ext_hal_reg_caps_parse(struct ath11k_base *soc,
						 u16 tag, u16 len,
						 const void *ptr, void *data)
{
	struct wmi_tlv_svc_rdy_ext_parse *svc_rdy_ext = data;

	if (tag != WMI_TAG_HAL_REG_CAPABILITIES_EXT)
		return -EPROTO;

	if (svc_rdy_ext->n_ext_hal_reg_caps >= svc_rdy_ext->param.num_phy)
		return -ENOBUFS;

	svc_rdy_ext->n_ext_hal_reg_caps++;
	return 0;
}

static int ath11k_wmi_tlv_ext_hal_reg_caps(struct ath11k_base *soc,
					   u16 len, const void *ptr, void *data)
{
	struct ath11k_pdev_wmi *wmi_handle = &soc->wmi_ab.wmi[0];
	struct wmi_tlv_svc_rdy_ext_parse *svc_rdy_ext = data;
	struct ath11k_hal_reg_capabilities_ext reg_cap;
	int ret;
	u32 i;

	svc_rdy_ext->n_ext_hal_reg_caps = 0;
	svc_rdy_ext->ext_hal_reg_caps = (struct wmi_hal_reg_capabilities_ext *)ptr;
	ret = ath11k_wmi_tlv_iter(soc, ptr, len,
				  ath11k_wmi_tlv_ext_hal_reg_caps_parse,
				  svc_rdy_ext);
	if (ret) {
		ath11k_warn(soc, "failed to parse tlv %d\n", ret);
		return ret;
	}

	for (i = 0; i < svc_rdy_ext->param.num_phy; i++) {
		ret = ath11k_pull_reg_cap_svc_rdy_ext(wmi_handle,
						      svc_rdy_ext->soc_hal_reg_caps,
						      svc_rdy_ext->ext_hal_reg_caps, i,
						      &reg_cap);
		if (ret) {
			ath11k_warn(soc, "failed to extract reg cap %d\n", i);
			return ret;
		}

		memcpy(&soc->hal_reg_cap[reg_cap.phy_id],
		       &reg_cap, sizeof(reg_cap));
	}
	return 0;
}

static int ath11k_wmi_tlv_ext_soc_hal_reg_caps_parse(struct ath11k_base *soc,
						     u16 len, const void *ptr,
						     void *data)
{
	struct ath11k_pdev_wmi *wmi_handle = &soc->wmi_ab.wmi[0];
	struct wmi_tlv_svc_rdy_ext_parse *svc_rdy_ext = data;
	u8 hw_mode_id = svc_rdy_ext->pref_hw_mode_caps.hw_mode_id;
	u32 phy_id_map;
	int pdev_index = 0;
	int ret;

	svc_rdy_ext->soc_hal_reg_caps = (struct wmi_soc_hal_reg_capabilities *)ptr;
	svc_rdy_ext->param.num_phy = svc_rdy_ext->soc_hal_reg_caps->num_phy;

	soc->num_radios = 0;
	phy_id_map = svc_rdy_ext->pref_hw_mode_caps.phy_id_map;

	while (phy_id_map && soc->num_radios < MAX_RADIOS) {
		ret = ath11k_pull_mac_phy_cap_svc_ready_ext(wmi_handle,
							    svc_rdy_ext->hw_caps,
							    svc_rdy_ext->hw_mode_caps,
							    svc_rdy_ext->soc_hal_reg_caps,
							    svc_rdy_ext->mac_phy_caps,
							    hw_mode_id, soc->num_radios,
							    &soc->pdevs[pdev_index]);
		if (ret) {
			ath11k_warn(soc, "failed to extract mac caps, idx :%d\n",
				    soc->num_radios);
			return ret;
		}

		soc->num_radios++;

		/* For QCA6390, save mac_phy capability in the same pdev */
		if (soc->hw_params.single_pdev_only)
			pdev_index = 0;
		else
			pdev_index = soc->num_radios;

		/* TODO: mac_phy_cap prints */
		phy_id_map >>= 1;
	}

	/* For QCA6390, set num_radios to 1 because host manages
	 * both 2G and 5G radio in one pdev.
	 * Set pdev_id = 0 and 0 means soc level.
	 */
	if (soc->hw_params.single_pdev_only) {
		soc->num_radios = 1;
		soc->pdevs[0].pdev_id = 0;
	}

	return 0;
}

static int ath11k_wmi_tlv_dma_ring_caps_parse(struct ath11k_base *soc,
					      u16 tag, u16 len,
					      const void *ptr, void *data)
{
	struct wmi_tlv_dma_ring_caps_parse *parse = data;

	if (tag != WMI_TAG_DMA_RING_CAPABILITIES)
		return -EPROTO;

	parse->n_dma_ring_caps++;
	return 0;
}

static int ath11k_wmi_alloc_dbring_caps(struct ath11k_base *ab,
					u32 num_cap)
{
	size_t sz;
	void *ptr;

	sz = num_cap * sizeof(struct ath11k_dbring_cap);
	ptr = kzalloc(sz, GFP_ATOMIC);
	if (!ptr)
		return -ENOMEM;

	ab->db_caps = ptr;
	ab->num_db_cap = num_cap;

	return 0;
}

static void ath11k_wmi_free_dbring_caps(struct ath11k_base *ab)
{
	kfree(ab->db_caps);
	ab->db_caps = NULL;
}

static int ath11k_wmi_tlv_dma_ring_caps(struct ath11k_base *ab,
					u16 len, const void *ptr, void *data)
{
	struct wmi_tlv_dma_ring_caps_parse *dma_caps_parse = data;
	struct wmi_dma_ring_capabilities *dma_caps;
	struct ath11k_dbring_cap *dir_buff_caps;
	int ret;
	u32 i;

	dma_caps_parse->n_dma_ring_caps = 0;
	dma_caps = (struct wmi_dma_ring_capabilities *)ptr;
	ret = ath11k_wmi_tlv_iter(ab, ptr, len,
				  ath11k_wmi_tlv_dma_ring_caps_parse,
				  dma_caps_parse);
	if (ret) {
		ath11k_warn(ab, "failed to parse dma ring caps tlv %d\n", ret);
		return ret;
	}

	if (!dma_caps_parse->n_dma_ring_caps)
		return 0;

	if (ab->num_db_cap) {
		ath11k_warn(ab, "Already processed, so ignoring dma ring caps\n");
		return 0;
	}

	ret = ath11k_wmi_alloc_dbring_caps(ab, dma_caps_parse->n_dma_ring_caps);
	if (ret)
		return ret;

	dir_buff_caps = ab->db_caps;
	for (i = 0; i < dma_caps_parse->n_dma_ring_caps; i++) {
		if (dma_caps[i].module_id >= WMI_DIRECT_BUF_MAX) {
			ath11k_warn(ab, "Invalid module id %d\n", dma_caps[i].module_id);
			ret = -EINVAL;
			goto free_dir_buff;
		}

		dir_buff_caps[i].id = dma_caps[i].module_id;
		dir_buff_caps[i].pdev_id = DP_HW2SW_MACID(dma_caps[i].pdev_id);
		dir_buff_caps[i].min_elem = dma_caps[i].min_elem;
		dir_buff_caps[i].min_buf_sz = dma_caps[i].min_buf_sz;
		dir_buff_caps[i].min_buf_align = dma_caps[i].min_buf_align;
	}

	return 0;

free_dir_buff:
	ath11k_wmi_free_dbring_caps(ab);
	return ret;
}

static int ath11k_wmi_tlv_svc_rdy_ext_parse(struct ath11k_base *ab,
					    u16 tag, u16 len,
					    const void *ptr, void *data)
{
	struct ath11k_pdev_wmi *wmi_handle = &ab->wmi_ab.wmi[0];
	struct wmi_tlv_svc_rdy_ext_parse *svc_rdy_ext = data;
	int ret;

	switch (tag) {
	case WMI_TAG_SERVICE_READY_EXT_EVENT:
		ret = ath11k_pull_svc_ready_ext(wmi_handle, ptr,
						&svc_rdy_ext->param);
		if (ret) {
			ath11k_warn(ab, "unable to extract ext params\n");
			return ret;
		}
		break;

	case WMI_TAG_SOC_MAC_PHY_HW_MODE_CAPS:
		svc_rdy_ext->hw_caps = (struct wmi_soc_mac_phy_hw_mode_caps *)ptr;
		svc_rdy_ext->param.num_hw_modes = svc_rdy_ext->hw_caps->num_hw_modes;
		break;

	case WMI_TAG_SOC_HAL_REG_CAPABILITIES:
		ret = ath11k_wmi_tlv_ext_soc_hal_reg_caps_parse(ab, len, ptr,
								svc_rdy_ext);
		if (ret)
			return ret;
		break;

	case WMI_TAG_ARRAY_STRUCT:
		if (!svc_rdy_ext->hw_mode_done) {
			ret = ath11k_wmi_tlv_hw_mode_caps(ab, len, ptr,
							  svc_rdy_ext);
			if (ret)
				return ret;

			svc_rdy_ext->hw_mode_done = true;
		} else if (!svc_rdy_ext->mac_phy_done) {
			svc_rdy_ext->n_mac_phy_caps = 0;
			ret = ath11k_wmi_tlv_iter(ab, ptr, len,
						  ath11k_wmi_tlv_mac_phy_caps_parse,
						  svc_rdy_ext);
			if (ret) {
				ath11k_warn(ab, "failed to parse tlv %d\n", ret);
				return ret;
			}

			svc_rdy_ext->mac_phy_done = true;
		} else if (!svc_rdy_ext->ext_hal_reg_done) {
			ret = ath11k_wmi_tlv_ext_hal_reg_caps(ab, len, ptr,
							      svc_rdy_ext);
			if (ret)
				return ret;

			svc_rdy_ext->ext_hal_reg_done = true;
		} else if (!svc_rdy_ext->mac_phy_chainmask_combo_done) {
			svc_rdy_ext->mac_phy_chainmask_combo_done = true;
		} else if (!svc_rdy_ext->mac_phy_chainmask_cap_done) {
			svc_rdy_ext->mac_phy_chainmask_cap_done = true;
		} else if (!svc_rdy_ext->oem_dma_ring_cap_done) {
			svc_rdy_ext->oem_dma_ring_cap_done = true;
		} else if (!svc_rdy_ext->dma_ring_cap_done) {
			ret = ath11k_wmi_tlv_dma_ring_caps(ab, len, ptr,
							   &svc_rdy_ext->dma_caps_parse);
			if (ret)
				return ret;

			svc_rdy_ext->dma_ring_cap_done = true;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int ath11k_service_ready_ext_event(struct ath11k_base *ab,
					  struct sk_buff *skb)
{
	struct wmi_tlv_svc_rdy_ext_parse svc_rdy_ext = { };
	int ret;

	ret = ath11k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath11k_wmi_tlv_svc_rdy_ext_parse,
				  &svc_rdy_ext);
	if (ret) {
		ath11k_warn(ab, "failed to parse tlv %d\n", ret);
		goto err;
	}

	if (!test_bit(WMI_TLV_SERVICE_EXT2_MSG, ab->wmi_ab.svc_map))
		complete(&ab->wmi_ab.service_ready);

	kfree(svc_rdy_ext.mac_phy_caps);
	return 0;

err:
	ath11k_wmi_free_dbring_caps(ab);
	return ret;
}

static int ath11k_wmi_tlv_svc_rdy_ext2_parse(struct ath11k_base *ab,
					     u16 tag, u16 len,
					     const void *ptr, void *data)
{
	struct wmi_tlv_svc_rdy_ext2_parse *parse = data;
	int ret;

	switch (tag) {
	case WMI_TAG_ARRAY_STRUCT:
		if (!parse->dma_ring_cap_done) {
			ret = ath11k_wmi_tlv_dma_ring_caps(ab, len, ptr,
							   &parse->dma_caps_parse);
			if (ret)
				return ret;

			parse->dma_ring_cap_done = true;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int ath11k_service_ready_ext2_event(struct ath11k_base *ab,
					   struct sk_buff *skb)
{
	struct wmi_tlv_svc_rdy_ext2_parse svc_rdy_ext2 = { };
	int ret;

	ret = ath11k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath11k_wmi_tlv_svc_rdy_ext2_parse,
				  &svc_rdy_ext2);
	if (ret) {
		ath11k_warn(ab, "failed to parse ext2 event tlv %d\n", ret);
		goto err;
	}

	complete(&ab->wmi_ab.service_ready);

	return 0;

err:
	ath11k_wmi_free_dbring_caps(ab);
	return ret;
}

static int ath11k_pull_vdev_start_resp_tlv(struct ath11k_base *ab, struct sk_buff *skb,
					   struct wmi_vdev_start_resp_event *vdev_rsp)
{
	const void **tb;
	const struct wmi_vdev_start_resp_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_VDEV_START_RESPONSE_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch vdev start resp ev");
		kfree(tb);
		return -EPROTO;
	}

	memset(vdev_rsp, 0, sizeof(*vdev_rsp));

	vdev_rsp->vdev_id = ev->vdev_id;
	vdev_rsp->requestor_id = ev->requestor_id;
	vdev_rsp->resp_type = ev->resp_type;
	vdev_rsp->status = ev->status;
	vdev_rsp->chain_mask = ev->chain_mask;
	vdev_rsp->smps_mode = ev->smps_mode;
	vdev_rsp->mac_id = ev->mac_id;
	vdev_rsp->cfgd_tx_streams = ev->cfgd_tx_streams;
	vdev_rsp->cfgd_rx_streams = ev->cfgd_rx_streams;

	kfree(tb);
	return 0;
}

static struct cur_reg_rule
*create_reg_rules_from_wmi(u32 num_reg_rules,
			   struct wmi_regulatory_rule_struct *wmi_reg_rule)
{
	struct cur_reg_rule *reg_rule_ptr;
	u32 count;

	reg_rule_ptr = kcalloc(num_reg_rules, sizeof(*reg_rule_ptr),
			       GFP_ATOMIC);

	if (!reg_rule_ptr)
		return NULL;

	for (count = 0; count < num_reg_rules; count++) {
		reg_rule_ptr[count].start_freq =
			FIELD_GET(REG_RULE_START_FREQ,
				  wmi_reg_rule[count].freq_info);
		reg_rule_ptr[count].end_freq =
			FIELD_GET(REG_RULE_END_FREQ,
				  wmi_reg_rule[count].freq_info);
		reg_rule_ptr[count].max_bw =
			FIELD_GET(REG_RULE_MAX_BW,
				  wmi_reg_rule[count].bw_pwr_info);
		reg_rule_ptr[count].reg_power =
			FIELD_GET(REG_RULE_REG_PWR,
				  wmi_reg_rule[count].bw_pwr_info);
		reg_rule_ptr[count].ant_gain =
			FIELD_GET(REG_RULE_ANT_GAIN,
				  wmi_reg_rule[count].bw_pwr_info);
		reg_rule_ptr[count].flags =
			FIELD_GET(REG_RULE_FLAGS,
				  wmi_reg_rule[count].flag_info);
	}

	return reg_rule_ptr;
}

static int ath11k_pull_reg_chan_list_update_ev(struct ath11k_base *ab,
					       struct sk_buff *skb,
					       struct cur_regulatory_info *reg_info)
{
	const void **tb;
	const struct wmi_reg_chan_list_cc_event *chan_list_event_hdr;
	struct wmi_regulatory_rule_struct *wmi_reg_rule;
	u32 num_2g_reg_rules, num_5g_reg_rules;
	int ret;

	ath11k_dbg(ab, ATH11K_DBG_WMI, "processing regulatory channel list\n");

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	chan_list_event_hdr = tb[WMI_TAG_REG_CHAN_LIST_CC_EVENT];
	if (!chan_list_event_hdr) {
		ath11k_warn(ab, "failed to fetch reg chan list update ev\n");
		kfree(tb);
		return -EPROTO;
	}

	reg_info->num_2g_reg_rules = chan_list_event_hdr->num_2g_reg_rules;
	reg_info->num_5g_reg_rules = chan_list_event_hdr->num_5g_reg_rules;

	if (!(reg_info->num_2g_reg_rules + reg_info->num_5g_reg_rules)) {
		ath11k_warn(ab, "No regulatory rules available in the event info\n");
		kfree(tb);
		return -EINVAL;
	}

	memcpy(reg_info->alpha2, &chan_list_event_hdr->alpha2,
	       REG_ALPHA2_LEN);
	reg_info->dfs_region = chan_list_event_hdr->dfs_region;
	reg_info->phybitmap = chan_list_event_hdr->phybitmap;
	reg_info->num_phy = chan_list_event_hdr->num_phy;
	reg_info->phy_id = chan_list_event_hdr->phy_id;
	reg_info->ctry_code = chan_list_event_hdr->country_id;
	reg_info->reg_dmn_pair = chan_list_event_hdr->domain_code;
	if (chan_list_event_hdr->status_code == WMI_REG_SET_CC_STATUS_PASS)
		reg_info->status_code = REG_SET_CC_STATUS_PASS;
	else if (chan_list_event_hdr->status_code == WMI_REG_CURRENT_ALPHA2_NOT_FOUND)
		reg_info->status_code = REG_CURRENT_ALPHA2_NOT_FOUND;
	else if (chan_list_event_hdr->status_code == WMI_REG_INIT_ALPHA2_NOT_FOUND)
		reg_info->status_code = REG_INIT_ALPHA2_NOT_FOUND;
	else if (chan_list_event_hdr->status_code == WMI_REG_SET_CC_CHANGE_NOT_ALLOWED)
		reg_info->status_code = REG_SET_CC_CHANGE_NOT_ALLOWED;
	else if (chan_list_event_hdr->status_code == WMI_REG_SET_CC_STATUS_NO_MEMORY)
		reg_info->status_code = REG_SET_CC_STATUS_NO_MEMORY;
	else if (chan_list_event_hdr->status_code == WMI_REG_SET_CC_STATUS_FAIL)
		reg_info->status_code = REG_SET_CC_STATUS_FAIL;

	reg_info->min_bw_2g = chan_list_event_hdr->min_bw_2g;
	reg_info->max_bw_2g = chan_list_event_hdr->max_bw_2g;
	reg_info->min_bw_5g = chan_list_event_hdr->min_bw_5g;
	reg_info->max_bw_5g = chan_list_event_hdr->max_bw_5g;

	num_2g_reg_rules = reg_info->num_2g_reg_rules;
	num_5g_reg_rules = reg_info->num_5g_reg_rules;

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "%s:cc %s dsf %d BW: min_2g %d max_2g %d min_5g %d max_5g %d",
		   __func__, reg_info->alpha2, reg_info->dfs_region,
		   reg_info->min_bw_2g, reg_info->max_bw_2g,
		   reg_info->min_bw_5g, reg_info->max_bw_5g);

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "%s: num_2g_reg_rules %d num_5g_reg_rules %d", __func__,
		   num_2g_reg_rules, num_5g_reg_rules);

	wmi_reg_rule =
		(struct wmi_regulatory_rule_struct *)((u8 *)chan_list_event_hdr
						+ sizeof(*chan_list_event_hdr)
						+ sizeof(struct wmi_tlv));

	if (num_2g_reg_rules) {
		reg_info->reg_rules_2g_ptr = create_reg_rules_from_wmi(num_2g_reg_rules,
								       wmi_reg_rule);
		if (!reg_info->reg_rules_2g_ptr) {
			kfree(tb);
			ath11k_warn(ab, "Unable to Allocate memory for 2g rules\n");
			return -ENOMEM;
		}
	}

	if (num_5g_reg_rules) {
		wmi_reg_rule += num_2g_reg_rules;
		reg_info->reg_rules_5g_ptr = create_reg_rules_from_wmi(num_5g_reg_rules,
								       wmi_reg_rule);
		if (!reg_info->reg_rules_5g_ptr) {
			kfree(tb);
			ath11k_warn(ab, "Unable to Allocate memory for 5g rules\n");
			return -ENOMEM;
		}
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI, "processed regulatory channel list\n");

	kfree(tb);
	return 0;
}

static int ath11k_pull_peer_del_resp_ev(struct ath11k_base *ab, struct sk_buff *skb,
					struct wmi_peer_delete_resp_event *peer_del_resp)
{
	const void **tb;
	const struct wmi_peer_delete_resp_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_PEER_DELETE_RESP_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch peer delete resp ev");
		kfree(tb);
		return -EPROTO;
	}

	memset(peer_del_resp, 0, sizeof(*peer_del_resp));

	peer_del_resp->vdev_id = ev->vdev_id;
	ether_addr_copy(peer_del_resp->peer_macaddr.addr,
			ev->peer_macaddr.addr);

	kfree(tb);
	return 0;
}

static int ath11k_pull_vdev_del_resp_ev(struct ath11k_base *ab,
					struct sk_buff *skb,
					u32 *vdev_id)
{
	const void **tb;
	const struct wmi_vdev_delete_resp_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_VDEV_DELETE_RESP_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch vdev delete resp ev");
		kfree(tb);
		return -EPROTO;
	}

	*vdev_id = ev->vdev_id;

	kfree(tb);
	return 0;
}

static int ath11k_pull_bcn_tx_status_ev(struct ath11k_base *ab, void *evt_buf,
					u32 len, u32 *vdev_id,
					u32 *tx_status)
{
	const void **tb;
	const struct wmi_bcn_tx_status_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, evt_buf, len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_OFFLOAD_BCN_TX_STATUS_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch bcn tx status ev");
		kfree(tb);
		return -EPROTO;
	}

	*vdev_id   = ev->vdev_id;
	*tx_status = ev->tx_status;

	kfree(tb);
	return 0;
}

static int ath11k_pull_vdev_stopped_param_tlv(struct ath11k_base *ab, struct sk_buff *skb,
					      u32 *vdev_id)
{
	const void **tb;
	const struct wmi_vdev_stopped_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_VDEV_STOPPED_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch vdev stop ev");
		kfree(tb);
		return -EPROTO;
	}

	*vdev_id =  ev->vdev_id;

	kfree(tb);
	return 0;
}

static int ath11k_pull_mgmt_rx_params_tlv(struct ath11k_base *ab,
					  struct sk_buff *skb,
					  struct mgmt_rx_event_params *hdr)
{
	const void **tb;
	const struct wmi_mgmt_rx_hdr *ev;
	const u8 *frame;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_MGMT_RX_HDR];
	frame = tb[WMI_TAG_ARRAY_BYTE];

	if (!ev || !frame) {
		ath11k_warn(ab, "failed to fetch mgmt rx hdr");
		kfree(tb);
		return -EPROTO;
	}

	hdr->pdev_id =  ev->pdev_id;
	hdr->chan_freq = ev->chan_freq;
	hdr->channel =  ev->channel;
	hdr->snr =  ev->snr;
	hdr->rate =  ev->rate;
	hdr->phy_mode =  ev->phy_mode;
	hdr->buf_len =  ev->buf_len;
	hdr->status =  ev->status;
	hdr->flags =  ev->flags;
	hdr->rssi =  ev->rssi;
	hdr->tsf_delta =  ev->tsf_delta;
	memcpy(hdr->rssi_ctl, ev->rssi_ctl, sizeof(hdr->rssi_ctl));

	if (skb->len < (frame - skb->data) + hdr->buf_len) {
		ath11k_warn(ab, "invalid length in mgmt rx hdr ev");
		kfree(tb);
		return -EPROTO;
	}

	/* shift the sk_buff to point to `frame` */
	skb_trim(skb, 0);
	skb_put(skb, frame - skb->data);
	skb_pull(skb, frame - skb->data);
	skb_put(skb, hdr->buf_len);

	ath11k_ce_byte_swap(skb->data, hdr->buf_len);

	kfree(tb);
	return 0;
}

static int wmi_process_mgmt_tx_comp(struct ath11k *ar, u32 desc_id,
				    u32 status)
{
	struct sk_buff *msdu;
	struct ieee80211_tx_info *info;
	struct ath11k_skb_cb *skb_cb;

	spin_lock_bh(&ar->txmgmt_idr_lock);
	msdu = idr_find(&ar->txmgmt_idr, desc_id);

	if (!msdu) {
		ath11k_warn(ar->ab, "received mgmt tx compl for invalid msdu_id: %d\n",
			    desc_id);
		spin_unlock_bh(&ar->txmgmt_idr_lock);
		return -ENOENT;
	}

	idr_remove(&ar->txmgmt_idr, desc_id);
	spin_unlock_bh(&ar->txmgmt_idr_lock);

	skb_cb = ATH11K_SKB_CB(msdu);
	dma_unmap_single(ar->ab->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);

	info = IEEE80211_SKB_CB(msdu);
	if ((!(info->flags & IEEE80211_TX_CTL_NO_ACK)) && !status)
		info->flags |= IEEE80211_TX_STAT_ACK;

	ieee80211_tx_status_irqsafe(ar->hw, msdu);

	/* WARN when we received this event without doing any mgmt tx */
	if (atomic_dec_if_positive(&ar->num_pending_mgmt_tx) < 0)
		WARN_ON_ONCE(1);

	return 0;
}

static int ath11k_pull_mgmt_tx_compl_param_tlv(struct ath11k_base *ab,
					       struct sk_buff *skb,
					       struct wmi_mgmt_tx_compl_event *param)
{
	const void **tb;
	const struct wmi_mgmt_tx_compl_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_MGMT_TX_COMPL_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch mgmt tx compl ev");
		kfree(tb);
		return -EPROTO;
	}

	param->pdev_id = ev->pdev_id;
	param->desc_id = ev->desc_id;
	param->status = ev->status;

	kfree(tb);
	return 0;
}

static void ath11k_wmi_event_scan_started(struct ath11k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		ath11k_warn(ar->ab, "received scan started event in an invalid scan state: %s (%d)\n",
			    ath11k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH11K_SCAN_STARTING:
		ar->scan.state = ATH11K_SCAN_RUNNING;
		complete(&ar->scan.started);
		break;
	}
}

static void ath11k_wmi_event_scan_start_failed(struct ath11k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		ath11k_warn(ar->ab, "received scan start failed event in an invalid scan state: %s (%d)\n",
			    ath11k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH11K_SCAN_STARTING:
		complete(&ar->scan.started);
		__ath11k_mac_scan_finish(ar);
		break;
	}
}

static void ath11k_wmi_event_scan_completed(struct ath11k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
	case ATH11K_SCAN_STARTING:
		/* One suspected reason scan can be completed while starting is
		 * if firmware fails to deliver all scan events to the host,
		 * e.g. when transport pipe is full. This has been observed
		 * with spectral scan phyerr events starving wmi transport
		 * pipe. In such case the "scan completed" event should be (and
		 * is) ignored by the host as it may be just firmware's scan
		 * state machine recovering.
		 */
		ath11k_warn(ar->ab, "received scan completed event in an invalid scan state: %s (%d)\n",
			    ath11k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		__ath11k_mac_scan_finish(ar);
		break;
	}
}

static void ath11k_wmi_event_scan_bss_chan(struct ath11k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
	case ATH11K_SCAN_STARTING:
		ath11k_warn(ar->ab, "received scan bss chan event in an invalid scan state: %s (%d)\n",
			    ath11k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		ar->scan_channel = NULL;
		break;
	}
}

static void ath11k_wmi_event_scan_foreign_chan(struct ath11k *ar, u32 freq)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
	case ATH11K_SCAN_STARTING:
		ath11k_warn(ar->ab, "received scan foreign chan event in an invalid scan state: %s (%d)\n",
			    ath11k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		ar->scan_channel = ieee80211_get_channel(ar->hw->wiphy, freq);
		break;
	}
}

static const char *
ath11k_wmi_event_scan_type_str(enum wmi_scan_event_type type,
			       enum wmi_scan_completion_reason reason)
{
	switch (type) {
	case WMI_SCAN_EVENT_STARTED:
		return "started";
	case WMI_SCAN_EVENT_COMPLETED:
		switch (reason) {
		case WMI_SCAN_REASON_COMPLETED:
			return "completed";
		case WMI_SCAN_REASON_CANCELLED:
			return "completed [cancelled]";
		case WMI_SCAN_REASON_PREEMPTED:
			return "completed [preempted]";
		case WMI_SCAN_REASON_TIMEDOUT:
			return "completed [timedout]";
		case WMI_SCAN_REASON_INTERNAL_FAILURE:
			return "completed [internal err]";
		case WMI_SCAN_REASON_MAX:
			break;
		}
		return "completed [unknown]";
	case WMI_SCAN_EVENT_BSS_CHANNEL:
		return "bss channel";
	case WMI_SCAN_EVENT_FOREIGN_CHAN:
		return "foreign channel";
	case WMI_SCAN_EVENT_DEQUEUED:
		return "dequeued";
	case WMI_SCAN_EVENT_PREEMPTED:
		return "preempted";
	case WMI_SCAN_EVENT_START_FAILED:
		return "start failed";
	case WMI_SCAN_EVENT_RESTARTED:
		return "restarted";
	case WMI_SCAN_EVENT_FOREIGN_CHAN_EXIT:
		return "foreign channel exit";
	default:
		return "unknown";
	}
}

static int ath11k_pull_scan_ev(struct ath11k_base *ab, struct sk_buff *skb,
			       struct wmi_scan_event *scan_evt_param)
{
	const void **tb;
	const struct wmi_scan_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_SCAN_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch scan ev");
		kfree(tb);
		return -EPROTO;
	}

	scan_evt_param->event_type = ev->event_type;
	scan_evt_param->reason = ev->reason;
	scan_evt_param->channel_freq = ev->channel_freq;
	scan_evt_param->scan_req_id = ev->scan_req_id;
	scan_evt_param->scan_id = ev->scan_id;
	scan_evt_param->vdev_id = ev->vdev_id;
	scan_evt_param->tsf_timestamp = ev->tsf_timestamp;

	kfree(tb);
	return 0;
}

static int ath11k_pull_peer_sta_kickout_ev(struct ath11k_base *ab, struct sk_buff *skb,
					   struct wmi_peer_sta_kickout_arg *arg)
{
	const void **tb;
	const struct wmi_peer_sta_kickout_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_PEER_STA_KICKOUT_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch peer sta kickout ev");
		kfree(tb);
		return -EPROTO;
	}

	arg->mac_addr = ev->peer_macaddr.addr;

	kfree(tb);
	return 0;
}

static int ath11k_pull_roam_ev(struct ath11k_base *ab, struct sk_buff *skb,
			       struct wmi_roam_event *roam_ev)
{
	const void **tb;
	const struct wmi_roam_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_ROAM_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch roam ev");
		kfree(tb);
		return -EPROTO;
	}

	roam_ev->vdev_id = ev->vdev_id;
	roam_ev->reason = ev->reason;
	roam_ev->rssi = ev->rssi;

	kfree(tb);
	return 0;
}

static int freq_to_idx(struct ath11k *ar, int freq)
{
	struct ieee80211_supported_band *sband;
	int band, ch, idx = 0;

	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
		sband = ar->hw->wiphy->bands[band];
		if (!sband)
			continue;

		for (ch = 0; ch < sband->n_channels; ch++, idx++)
			if (sband->channels[ch].center_freq == freq)
				goto exit;
	}

exit:
	return idx;
}

static int ath11k_pull_chan_info_ev(struct ath11k_base *ab, u8 *evt_buf,
				    u32 len, struct wmi_chan_info_event *ch_info_ev)
{
	const void **tb;
	const struct wmi_chan_info_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, evt_buf, len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_CHAN_INFO_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch chan info ev");
		kfree(tb);
		return -EPROTO;
	}

	ch_info_ev->err_code = ev->err_code;
	ch_info_ev->freq = ev->freq;
	ch_info_ev->cmd_flags = ev->cmd_flags;
	ch_info_ev->noise_floor = ev->noise_floor;
	ch_info_ev->rx_clear_count = ev->rx_clear_count;
	ch_info_ev->cycle_count = ev->cycle_count;
	ch_info_ev->chan_tx_pwr_range = ev->chan_tx_pwr_range;
	ch_info_ev->chan_tx_pwr_tp = ev->chan_tx_pwr_tp;
	ch_info_ev->rx_frame_count = ev->rx_frame_count;
	ch_info_ev->tx_frame_cnt = ev->tx_frame_cnt;
	ch_info_ev->mac_clk_mhz = ev->mac_clk_mhz;
	ch_info_ev->vdev_id = ev->vdev_id;

	kfree(tb);
	return 0;
}

static int
ath11k_pull_pdev_bss_chan_info_ev(struct ath11k_base *ab, struct sk_buff *skb,
				  struct wmi_pdev_bss_chan_info_event *bss_ch_info_ev)
{
	const void **tb;
	const struct wmi_pdev_bss_chan_info_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_PDEV_BSS_CHAN_INFO_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch pdev bss chan info ev");
		kfree(tb);
		return -EPROTO;
	}

	bss_ch_info_ev->pdev_id = ev->pdev_id;
	bss_ch_info_ev->freq = ev->freq;
	bss_ch_info_ev->noise_floor = ev->noise_floor;
	bss_ch_info_ev->rx_clear_count_low = ev->rx_clear_count_low;
	bss_ch_info_ev->rx_clear_count_high = ev->rx_clear_count_high;
	bss_ch_info_ev->cycle_count_low = ev->cycle_count_low;
	bss_ch_info_ev->cycle_count_high = ev->cycle_count_high;
	bss_ch_info_ev->tx_cycle_count_low = ev->tx_cycle_count_low;
	bss_ch_info_ev->tx_cycle_count_high = ev->tx_cycle_count_high;
	bss_ch_info_ev->rx_cycle_count_low = ev->rx_cycle_count_low;
	bss_ch_info_ev->rx_cycle_count_high = ev->rx_cycle_count_high;
	bss_ch_info_ev->rx_bss_cycle_count_low = ev->rx_bss_cycle_count_low;
	bss_ch_info_ev->rx_bss_cycle_count_high = ev->rx_bss_cycle_count_high;

	kfree(tb);
	return 0;
}

static int
ath11k_pull_vdev_install_key_compl_ev(struct ath11k_base *ab, struct sk_buff *skb,
				      struct wmi_vdev_install_key_complete_arg *arg)
{
	const void **tb;
	const struct wmi_vdev_install_key_compl_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_VDEV_INSTALL_KEY_COMPLETE_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch vdev install key compl ev");
		kfree(tb);
		return -EPROTO;
	}

	arg->vdev_id = ev->vdev_id;
	arg->macaddr = ev->peer_macaddr.addr;
	arg->key_idx = ev->key_idx;
	arg->key_flags = ev->key_flags;
	arg->status = ev->status;

	kfree(tb);
	return 0;
}

static int ath11k_pull_peer_assoc_conf_ev(struct ath11k_base *ab, struct sk_buff *skb,
					  struct wmi_peer_assoc_conf_arg *peer_assoc_conf)
{
	const void **tb;
	const struct wmi_peer_assoc_conf_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_PEER_ASSOC_CONF_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch peer assoc conf ev");
		kfree(tb);
		return -EPROTO;
	}

	peer_assoc_conf->vdev_id = ev->vdev_id;
	peer_assoc_conf->macaddr = ev->peer_macaddr.addr;

	kfree(tb);
	return 0;
}

static void ath11k_wmi_pull_pdev_stats_base(const struct wmi_pdev_stats_base *src,
					    struct ath11k_fw_stats_pdev *dst)
{
	dst->ch_noise_floor = src->chan_nf;
	dst->tx_frame_count = src->tx_frame_count;
	dst->rx_frame_count = src->rx_frame_count;
	dst->rx_clear_count = src->rx_clear_count;
	dst->cycle_count = src->cycle_count;
	dst->phy_err_count = src->phy_err_count;
	dst->chan_tx_power = src->chan_tx_pwr;
}

static void
ath11k_wmi_pull_pdev_stats_tx(const struct wmi_pdev_stats_tx *src,
			      struct ath11k_fw_stats_pdev *dst)
{
	dst->comp_queued = src->comp_queued;
	dst->comp_delivered = src->comp_delivered;
	dst->msdu_enqued = src->msdu_enqued;
	dst->mpdu_enqued = src->mpdu_enqued;
	dst->wmm_drop = src->wmm_drop;
	dst->local_enqued = src->local_enqued;
	dst->local_freed = src->local_freed;
	dst->hw_queued = src->hw_queued;
	dst->hw_reaped = src->hw_reaped;
	dst->underrun = src->underrun;
	dst->hw_paused = src->hw_paused;
	dst->tx_abort = src->tx_abort;
	dst->mpdus_requeued = src->mpdus_requeued;
	dst->tx_ko = src->tx_ko;
	dst->tx_xretry = src->tx_xretry;
	dst->data_rc = src->data_rc;
	dst->self_triggers = src->self_triggers;
	dst->sw_retry_failure = src->sw_retry_failure;
	dst->illgl_rate_phy_err = src->illgl_rate_phy_err;
	dst->pdev_cont_xretry = src->pdev_cont_xretry;
	dst->pdev_tx_timeout = src->pdev_tx_timeout;
	dst->pdev_resets = src->pdev_resets;
	dst->stateless_tid_alloc_failure = src->stateless_tid_alloc_failure;
	dst->phy_underrun = src->phy_underrun;
	dst->txop_ovf = src->txop_ovf;
	dst->seq_posted = src->seq_posted;
	dst->seq_failed_queueing = src->seq_failed_queueing;
	dst->seq_completed = src->seq_completed;
	dst->seq_restarted = src->seq_restarted;
	dst->mu_seq_posted = src->mu_seq_posted;
	dst->mpdus_sw_flush = src->mpdus_sw_flush;
	dst->mpdus_hw_filter = src->mpdus_hw_filter;
	dst->mpdus_truncated = src->mpdus_truncated;
	dst->mpdus_ack_failed = src->mpdus_ack_failed;
	dst->mpdus_expired = src->mpdus_expired;
}

static void ath11k_wmi_pull_pdev_stats_rx(const struct wmi_pdev_stats_rx *src,
					  struct ath11k_fw_stats_pdev *dst)
{
	dst->mid_ppdu_route_change = src->mid_ppdu_route_change;
	dst->status_rcvd = src->status_rcvd;
	dst->r0_frags = src->r0_frags;
	dst->r1_frags = src->r1_frags;
	dst->r2_frags = src->r2_frags;
	dst->r3_frags = src->r3_frags;
	dst->htt_msdus = src->htt_msdus;
	dst->htt_mpdus = src->htt_mpdus;
	dst->loc_msdus = src->loc_msdus;
	dst->loc_mpdus = src->loc_mpdus;
	dst->oversize_amsdu = src->oversize_amsdu;
	dst->phy_errs = src->phy_errs;
	dst->phy_err_drop = src->phy_err_drop;
	dst->mpdu_errs = src->mpdu_errs;
	dst->rx_ovfl_errs = src->rx_ovfl_errs;
}

static void
ath11k_wmi_pull_vdev_stats(const struct wmi_vdev_stats *src,
			   struct ath11k_fw_stats_vdev *dst)
{
	int i;

	dst->vdev_id = src->vdev_id;
	dst->beacon_snr = src->beacon_snr;
	dst->data_snr = src->data_snr;
	dst->num_rx_frames = src->num_rx_frames;
	dst->num_rts_fail = src->num_rts_fail;
	dst->num_rts_success = src->num_rts_success;
	dst->num_rx_err = src->num_rx_err;
	dst->num_rx_discard = src->num_rx_discard;
	dst->num_tx_not_acked = src->num_tx_not_acked;

	for (i = 0; i < ARRAY_SIZE(src->num_tx_frames); i++)
		dst->num_tx_frames[i] = src->num_tx_frames[i];

	for (i = 0; i < ARRAY_SIZE(src->num_tx_frames_retries); i++)
		dst->num_tx_frames_retries[i] = src->num_tx_frames_retries[i];

	for (i = 0; i < ARRAY_SIZE(src->num_tx_frames_failures); i++)
		dst->num_tx_frames_failures[i] = src->num_tx_frames_failures[i];

	for (i = 0; i < ARRAY_SIZE(src->tx_rate_history); i++)
		dst->tx_rate_history[i] = src->tx_rate_history[i];

	for (i = 0; i < ARRAY_SIZE(src->beacon_rssi_history); i++)
		dst->beacon_rssi_history[i] = src->beacon_rssi_history[i];
}

static void
ath11k_wmi_pull_bcn_stats(const struct wmi_bcn_stats *src,
			  struct ath11k_fw_stats_bcn *dst)
{
	dst->vdev_id = src->vdev_id;
	dst->tx_bcn_succ_cnt = src->tx_bcn_succ_cnt;
	dst->tx_bcn_outage_cnt = src->tx_bcn_outage_cnt;
}

int ath11k_wmi_pull_fw_stats(struct ath11k_base *ab, struct sk_buff *skb,
			     struct ath11k_fw_stats *stats)
{
	const void **tb;
	const struct wmi_stats_event *ev;
	const void *data;
	int i, ret;
	u32 len = skb->len;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return ret;
	}

	ev = tb[WMI_TAG_STATS_EVENT];
	data = tb[WMI_TAG_ARRAY_BYTE];
	if (!ev || !data) {
		ath11k_warn(ab, "failed to fetch update stats ev");
		kfree(tb);
		return -EPROTO;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "wmi stats update ev pdev_id %d pdev %i vdev %i bcn %i\n",
		   ev->pdev_id,
		   ev->num_pdev_stats, ev->num_vdev_stats,
		   ev->num_bcn_stats);

	stats->pdev_id = ev->pdev_id;
	stats->stats_id = 0;

	for (i = 0; i < ev->num_pdev_stats; i++) {
		const struct wmi_pdev_stats *src;
		struct ath11k_fw_stats_pdev *dst;

		src = data;
		if (len < sizeof(*src)) {
			kfree(tb);
			return -EPROTO;
		}

		stats->stats_id = WMI_REQUEST_PDEV_STAT;

		data += sizeof(*src);
		len -= sizeof(*src);

		dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
		if (!dst)
			continue;

		ath11k_wmi_pull_pdev_stats_base(&src->base, dst);
		ath11k_wmi_pull_pdev_stats_tx(&src->tx, dst);
		ath11k_wmi_pull_pdev_stats_rx(&src->rx, dst);
		list_add_tail(&dst->list, &stats->pdevs);
	}

	for (i = 0; i < ev->num_vdev_stats; i++) {
		const struct wmi_vdev_stats *src;
		struct ath11k_fw_stats_vdev *dst;

		src = data;
		if (len < sizeof(*src)) {
			kfree(tb);
			return -EPROTO;
		}

		stats->stats_id = WMI_REQUEST_VDEV_STAT;

		data += sizeof(*src);
		len -= sizeof(*src);

		dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
		if (!dst)
			continue;

		ath11k_wmi_pull_vdev_stats(src, dst);
		list_add_tail(&dst->list, &stats->vdevs);
	}

	for (i = 0; i < ev->num_bcn_stats; i++) {
		const struct wmi_bcn_stats *src;
		struct ath11k_fw_stats_bcn *dst;

		src = data;
		if (len < sizeof(*src)) {
			kfree(tb);
			return -EPROTO;
		}

		stats->stats_id = WMI_REQUEST_BCN_STAT;

		data += sizeof(*src);
		len -= sizeof(*src);

		dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
		if (!dst)
			continue;

		ath11k_wmi_pull_bcn_stats(src, dst);
		list_add_tail(&dst->list, &stats->bcn);
	}

	kfree(tb);
	return 0;
}

size_t ath11k_wmi_fw_stats_num_vdevs(struct list_head *head)
{
	struct ath11k_fw_stats_vdev *i;
	size_t num = 0;

	list_for_each_entry(i, head, list)
		++num;

	return num;
}

static size_t ath11k_wmi_fw_stats_num_bcn(struct list_head *head)
{
	struct ath11k_fw_stats_bcn *i;
	size_t num = 0;

	list_for_each_entry(i, head, list)
		++num;

	return num;
}

static void
ath11k_wmi_fw_pdev_base_stats_fill(const struct ath11k_fw_stats_pdev *pdev,
				   char *buf, u32 *length)
{
	u32 len = *length;
	u32 buf_len = ATH11K_FW_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			"ath11k PDEV stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
			"=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			"Channel noise floor", pdev->ch_noise_floor);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			"Channel TX power", pdev->chan_tx_power);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			"TX frame count", pdev->tx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			"RX frame count", pdev->rx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			"RX clear count", pdev->rx_clear_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			"Cycle count", pdev->cycle_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			"PHY error count", pdev->phy_err_count);

	*length = len;
}

static void
ath11k_wmi_fw_pdev_tx_stats_fill(const struct ath11k_fw_stats_pdev *pdev,
				 char *buf, u32 *length)
{
	u32 len = *length;
	u32 buf_len = ATH11K_FW_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "\n%30s\n",
			 "ath11k PDEV TX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
			 "====================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies queued", pdev->comp_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies disp.", pdev->comp_delivered);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDU queued", pdev->msdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU queued", pdev->mpdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs dropped", pdev->wmm_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local enqued", pdev->local_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local freed", pdev->local_freed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HW queued", pdev->hw_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs reaped", pdev->hw_reaped);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Num underruns", pdev->underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Num HW Paused", pdev->hw_paused);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs cleaned", pdev->tx_abort);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs requeued", pdev->mpdus_requeued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "PPDU OK", pdev->tx_ko);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Excessive retries", pdev->tx_xretry);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "HW rate", pdev->data_rc);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Sched self triggers", pdev->self_triggers);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Dropped due to SW retries",
			 pdev->sw_retry_failure);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Illegal rate phy errors",
			 pdev->illgl_rate_phy_err);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "PDEV continuous xretry", pdev->pdev_cont_xretry);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "TX timeout", pdev->pdev_tx_timeout);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "PDEV resets", pdev->pdev_resets);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Stateless TIDs alloc failures",
			 pdev->stateless_tid_alloc_failure);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "PHY underrun", pdev->phy_underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "MPDU is more than txop limit", pdev->txop_ovf);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num sequences posted", pdev->seq_posted);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num seq failed queueing ", pdev->seq_failed_queueing);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num sequences completed ", pdev->seq_completed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num sequences restarted ", pdev->seq_restarted);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num of MU sequences posted ", pdev->mu_seq_posted);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num of MPDUS SW flushed ", pdev->mpdus_sw_flush);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num of MPDUS HW filtered ", pdev->mpdus_hw_filter);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num of MPDUS truncated ", pdev->mpdus_truncated);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num of MPDUS ACK failed ", pdev->mpdus_ack_failed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Num of MPDUS expired ", pdev->mpdus_expired);
	*length = len;
}

static void
ath11k_wmi_fw_pdev_rx_stats_fill(const struct ath11k_fw_stats_pdev *pdev,
				 char *buf, u32 *length)
{
	u32 len = *length;
	u32 buf_len = ATH11K_FW_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "\n%30s\n",
			 "ath11k PDEV RX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
			 "====================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Mid PPDU route change",
			 pdev->mid_ppdu_route_change);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Tot. number of statuses", pdev->status_rcvd);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 0", pdev->r0_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 1", pdev->r1_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 2", pdev->r2_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 3", pdev->r3_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to HTT", pdev->htt_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to HTT", pdev->htt_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to stack", pdev->loc_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to stack", pdev->loc_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Oversized AMSUs", pdev->oversize_amsdu);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors", pdev->phy_errs);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors drops", pdev->phy_err_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU errors (FCS, MIC, ENC)", pdev->mpdu_errs);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Overflow errors", pdev->rx_ovfl_errs);
	*length = len;
}

static void
ath11k_wmi_fw_vdev_stats_fill(struct ath11k *ar,
			      const struct ath11k_fw_stats_vdev *vdev,
			      char *buf, u32 *length)
{
	u32 len = *length;
	u32 buf_len = ATH11K_FW_STATS_BUF_SIZE;
	struct ath11k_vif *arvif = ath11k_mac_get_arvif(ar, vdev->vdev_id);
	u8 *vif_macaddr;
	int i;

	/* VDEV stats has all the active VDEVs of other PDEVs as well,
	 * ignoring those not part of requested PDEV
	 */
	if (!arvif)
		return;

	vif_macaddr = arvif->vif->addr;

	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "VDEV ID", vdev->vdev_id);
	len += scnprintf(buf + len, buf_len - len, "%30s %pM\n",
			 "VDEV MAC address", vif_macaddr);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "beacon snr", vdev->beacon_snr);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "data snr", vdev->data_snr);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "num rx frames", vdev->num_rx_frames);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "num rts fail", vdev->num_rts_fail);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "num rts success", vdev->num_rts_success);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "num rx err", vdev->num_rx_err);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "num rx discard", vdev->num_rx_discard);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "num tx not acked", vdev->num_tx_not_acked);

	for (i = 0 ; i < ARRAY_SIZE(vdev->num_tx_frames); i++)
		len += scnprintf(buf + len, buf_len - len,
				"%25s [%02d] %u\n",
				"num tx frames", i,
				vdev->num_tx_frames[i]);

	for (i = 0 ; i < ARRAY_SIZE(vdev->num_tx_frames_retries); i++)
		len += scnprintf(buf + len, buf_len - len,
				"%25s [%02d] %u\n",
				"num tx frames retries", i,
				vdev->num_tx_frames_retries[i]);

	for (i = 0 ; i < ARRAY_SIZE(vdev->num_tx_frames_failures); i++)
		len += scnprintf(buf + len, buf_len - len,
				"%25s [%02d] %u\n",
				"num tx frames failures", i,
				vdev->num_tx_frames_failures[i]);

	for (i = 0 ; i < ARRAY_SIZE(vdev->tx_rate_history); i++)
		len += scnprintf(buf + len, buf_len - len,
				"%25s [%02d] 0x%08x\n",
				"tx rate history", i,
				vdev->tx_rate_history[i]);

	for (i = 0 ; i < ARRAY_SIZE(vdev->beacon_rssi_history); i++)
		len += scnprintf(buf + len, buf_len - len,
				"%25s [%02d] %u\n",
				"beacon rssi history", i,
				vdev->beacon_rssi_history[i]);

	len += scnprintf(buf + len, buf_len - len, "\n");
	*length = len;
}

static void
ath11k_wmi_fw_bcn_stats_fill(struct ath11k *ar,
			     const struct ath11k_fw_stats_bcn *bcn,
			     char *buf, u32 *length)
{
	u32 len = *length;
	u32 buf_len = ATH11K_FW_STATS_BUF_SIZE;
	struct ath11k_vif *arvif = ath11k_mac_get_arvif(ar, bcn->vdev_id);
	u8 *vdev_macaddr;

	if (!arvif) {
		ath11k_warn(ar->ab, "invalid vdev id %d in bcn stats",
			    bcn->vdev_id);
		return;
	}

	vdev_macaddr = arvif->vif->addr;

	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "VDEV ID", bcn->vdev_id);
	len += scnprintf(buf + len, buf_len - len, "%30s %pM\n",
			 "VDEV MAC address", vdev_macaddr);
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
			 "================");
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "Num of beacon tx success", bcn->tx_bcn_succ_cnt);
	len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
			 "Num of beacon tx failures", bcn->tx_bcn_outage_cnt);

	len += scnprintf(buf + len, buf_len - len, "\n");
	*length = len;
}

void ath11k_wmi_fw_stats_fill(struct ath11k *ar,
			      struct ath11k_fw_stats *fw_stats,
			      u32 stats_id, char *buf)
{
	u32 len = 0;
	u32 buf_len = ATH11K_FW_STATS_BUF_SIZE;
	const struct ath11k_fw_stats_pdev *pdev;
	const struct ath11k_fw_stats_vdev *vdev;
	const struct ath11k_fw_stats_bcn *bcn;
	size_t num_bcn;

	spin_lock_bh(&ar->data_lock);

	if (stats_id == WMI_REQUEST_PDEV_STAT) {
		pdev = list_first_entry_or_null(&fw_stats->pdevs,
						struct ath11k_fw_stats_pdev, list);
		if (!pdev) {
			ath11k_warn(ar->ab, "failed to get pdev stats\n");
			goto unlock;
		}

		ath11k_wmi_fw_pdev_base_stats_fill(pdev, buf, &len);
		ath11k_wmi_fw_pdev_tx_stats_fill(pdev, buf, &len);
		ath11k_wmi_fw_pdev_rx_stats_fill(pdev, buf, &len);
	}

	if (stats_id == WMI_REQUEST_VDEV_STAT) {
		len += scnprintf(buf + len, buf_len - len, "\n");
		len += scnprintf(buf + len, buf_len - len, "%30s\n",
				 "ath11k VDEV stats");
		len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

		list_for_each_entry(vdev, &fw_stats->vdevs, list)
			ath11k_wmi_fw_vdev_stats_fill(ar, vdev, buf, &len);
	}

	if (stats_id == WMI_REQUEST_BCN_STAT) {
		num_bcn = ath11k_wmi_fw_stats_num_bcn(&fw_stats->bcn);

		len += scnprintf(buf + len, buf_len - len, "\n");
		len += scnprintf(buf + len, buf_len - len, "%30s (%zu)\n",
				 "ath11k Beacon stats", num_bcn);
		len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "===================");

		list_for_each_entry(bcn, &fw_stats->bcn, list)
			ath11k_wmi_fw_bcn_stats_fill(ar, bcn, buf, &len);
	}

unlock:
	spin_unlock_bh(&ar->data_lock);

	if (len >= buf_len)
		buf[len - 1] = 0;
	else
		buf[len] = 0;
}

static void ath11k_wmi_op_ep_tx_credits(struct ath11k_base *ab)
{
	/* try to send pending beacons first. they take priority */
	wake_up(&ab->wmi_ab.tx_credits_wq);
}

static void ath11k_wmi_htc_tx_complete(struct ath11k_base *ab,
				       struct sk_buff *skb)
{
	dev_kfree_skb(skb);
}

static bool ath11k_reg_is_world_alpha(char *alpha)
{
	return alpha[0] == '0' && alpha[1] == '0';
}

static int ath11k_reg_chan_list_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct cur_regulatory_info *reg_info = NULL;
	struct ieee80211_regdomain *regd = NULL;
	bool intersect = false;
	int ret = 0, pdev_idx;
	struct ath11k *ar;

	reg_info = kzalloc(sizeof(*reg_info), GFP_ATOMIC);
	if (!reg_info) {
		ret = -ENOMEM;
		goto fallback;
	}

	ret = ath11k_pull_reg_chan_list_update_ev(ab, skb, reg_info);
	if (ret) {
		ath11k_warn(ab, "failed to extract regulatory info from received event\n");
		goto fallback;
	}

	if (reg_info->status_code != REG_SET_CC_STATUS_PASS) {
		/* In case of failure to set the requested ctry,
		 * fw retains the current regd. We print a failure info
		 * and return from here.
		 */
		ath11k_warn(ab, "Failed to set the requested Country regulatory setting\n");
		goto mem_free;
	}

	pdev_idx = reg_info->phy_id;

	/* Avoid default reg rule updates sent during FW recovery if
	 * it is already available
	 */
	spin_lock(&ab->base_lock);
	if (test_bit(ATH11K_FLAG_RECOVERY, &ab->dev_flags) &&
	    ab->default_regd[pdev_idx]) {
		spin_unlock(&ab->base_lock);
		goto mem_free;
	}
	spin_unlock(&ab->base_lock);

	if (pdev_idx >= ab->num_radios) {
		/* Process the event for phy0 only if single_pdev_only
		 * is true. If pdev_idx is valid but not 0, discard the
		 * event. Otherwise, it goes to fallback.
		 */
		if (ab->hw_params.single_pdev_only &&
		    pdev_idx < ab->hw_params.num_rxmda_per_pdev)
			goto mem_free;
		else
			goto fallback;
	}

	/* Avoid multiple overwrites to default regd, during core
	 * stop-start after mac registration.
	 */
	if (ab->default_regd[pdev_idx] && !ab->new_regd[pdev_idx] &&
	    !memcmp((char *)ab->default_regd[pdev_idx]->alpha2,
		    (char *)reg_info->alpha2, 2))
		goto mem_free;

	/* Intersect new rules with default regd if a new country setting was
	 * requested, i.e a default regd was already set during initialization
	 * and the regd coming from this event has a valid country info.
	 */
	if (ab->default_regd[pdev_idx] &&
	    !ath11k_reg_is_world_alpha((char *)
		ab->default_regd[pdev_idx]->alpha2) &&
	    !ath11k_reg_is_world_alpha((char *)reg_info->alpha2))
		intersect = true;

	regd = ath11k_reg_build_regd(ab, reg_info, intersect);
	if (!regd) {
		ath11k_warn(ab, "failed to build regd from reg_info\n");
		goto fallback;
	}

	spin_lock(&ab->base_lock);
	if (ab->default_regd[pdev_idx]) {
		/* The initial rules from FW after WMI Init is to build
		 * the default regd. From then on, any rules updated for
		 * the pdev could be due to user reg changes.
		 * Free previously built regd before assigning the newly
		 * generated regd to ar. NULL pointer handling will be
		 * taken care by kfree itself.
		 */
		ar = ab->pdevs[pdev_idx].ar;
		kfree(ab->new_regd[pdev_idx]);
		ab->new_regd[pdev_idx] = regd;
		ieee80211_queue_work(ar->hw, &ar->regd_update_work);
	} else {
		/* This regd would be applied during mac registration and is
		 * held constant throughout for regd intersection purpose
		 */
		ab->default_regd[pdev_idx] = regd;
	}
	ab->dfs_region = reg_info->dfs_region;
	spin_unlock(&ab->base_lock);

	goto mem_free;

fallback:
	/* Fallback to older reg (by sending previous country setting
	 * again if fw has succeded and we failed to process here.
	 * The Regdomain should be uniform across driver and fw. Since the
	 * FW has processed the command and sent a success status, we expect
	 * this function to succeed as well. If it doesn't, CTRY needs to be
	 * reverted at the fw and the old SCAN_CHAN_LIST cmd needs to be sent.
	 */
	/* TODO: This is rare, but still should also be handled */
	WARN_ON(1);
mem_free:
	if (reg_info) {
		kfree(reg_info->reg_rules_2g_ptr);
		kfree(reg_info->reg_rules_5g_ptr);
		kfree(reg_info);
	}
	return ret;
}

static int ath11k_wmi_tlv_rdy_parse(struct ath11k_base *ab, u16 tag, u16 len,
				    const void *ptr, void *data)
{
	struct wmi_tlv_rdy_parse *rdy_parse = data;
	struct wmi_ready_event fixed_param;
	struct wmi_mac_addr *addr_list;
	struct ath11k_pdev *pdev;
	u32 num_mac_addr;
	int i;

	switch (tag) {
	case WMI_TAG_READY_EVENT:
		memset(&fixed_param, 0, sizeof(fixed_param));
		memcpy(&fixed_param, (struct wmi_ready_event *)ptr,
		       min_t(u16, sizeof(fixed_param), len));
		ab->wlan_init_status = fixed_param.ready_event_min.status;
		rdy_parse->num_extra_mac_addr =
			fixed_param.ready_event_min.num_extra_mac_addr;

		ether_addr_copy(ab->mac_addr,
				fixed_param.ready_event_min.mac_addr.addr);
		ab->pktlog_defs_checksum = fixed_param.pktlog_defs_checksum;
		ab->wmi_ready = true;
		break;
	case WMI_TAG_ARRAY_FIXED_STRUCT:
		addr_list = (struct wmi_mac_addr *)ptr;
		num_mac_addr = rdy_parse->num_extra_mac_addr;

		if (!(ab->num_radios > 1 && num_mac_addr >= ab->num_radios))
			break;

		for (i = 0; i < ab->num_radios; i++) {
			pdev = &ab->pdevs[i];
			ether_addr_copy(pdev->mac_addr, addr_list[i].addr);
		}
		ab->pdevs_macaddr_valid = true;
		break;
	default:
		break;
	}

	return 0;
}

static int ath11k_ready_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_tlv_rdy_parse rdy_parse = { };
	int ret;

	ret = ath11k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath11k_wmi_tlv_rdy_parse, &rdy_parse);
	if (ret) {
		ath11k_warn(ab, "failed to parse tlv %d\n", ret);
		return ret;
	}

	complete(&ab->wmi_ab.unified_ready);
	return 0;
}

static void ath11k_peer_delete_resp_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_peer_delete_resp_event peer_del_resp;
	struct ath11k *ar;

	if (ath11k_pull_peer_del_resp_ev(ab, skb, &peer_del_resp) != 0) {
		ath11k_warn(ab, "failed to extract peer delete resp");
		return;
	}

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, peer_del_resp.vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in peer delete resp ev %d",
			    peer_del_resp.vdev_id);
		rcu_read_unlock();
		return;
	}

	complete(&ar->peer_delete_done);
	rcu_read_unlock();
	ath11k_dbg(ab, ATH11K_DBG_WMI, "peer delete resp for vdev id %d addr %pM\n",
		   peer_del_resp.vdev_id, peer_del_resp.peer_macaddr.addr);
}

static void ath11k_vdev_delete_resp_event(struct ath11k_base *ab,
					  struct sk_buff *skb)
{
	struct ath11k *ar;
	u32 vdev_id = 0;

	if (ath11k_pull_vdev_del_resp_ev(ab, skb, &vdev_id) != 0) {
		ath11k_warn(ab, "failed to extract vdev delete resp");
		return;
	}

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in vdev delete resp ev %d",
			    vdev_id);
		rcu_read_unlock();
		return;
	}

	complete(&ar->vdev_delete_done);

	rcu_read_unlock();

	ath11k_dbg(ab, ATH11K_DBG_WMI, "vdev delete resp for vdev id %d\n",
		   vdev_id);
}

static inline const char *ath11k_wmi_vdev_resp_print(u32 vdev_resp_status)
{
	switch (vdev_resp_status) {
	case WMI_VDEV_START_RESPONSE_INVALID_VDEVID:
		return "invalid vdev id";
	case WMI_VDEV_START_RESPONSE_NOT_SUPPORTED:
		return "not supported";
	case WMI_VDEV_START_RESPONSE_DFS_VIOLATION:
		return "dfs violation";
	case WMI_VDEV_START_RESPONSE_INVALID_REGDOMAIN:
		return "invalid regdomain";
	default:
		return "unknown";
	}
}

static void ath11k_vdev_start_resp_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_vdev_start_resp_event vdev_start_resp;
	struct ath11k *ar;
	u32 status;

	if (ath11k_pull_vdev_start_resp_tlv(ab, skb, &vdev_start_resp) != 0) {
		ath11k_warn(ab, "failed to extract vdev start resp");
		return;
	}

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, vdev_start_resp.vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in vdev start resp ev %d",
			    vdev_start_resp.vdev_id);
		rcu_read_unlock();
		return;
	}

	ar->last_wmi_vdev_start_status = 0;

	status = vdev_start_resp.status;

	if (WARN_ON_ONCE(status)) {
		ath11k_warn(ab, "vdev start resp error status %d (%s)\n",
			    status, ath11k_wmi_vdev_resp_print(status));
		ar->last_wmi_vdev_start_status = status;
	}

	complete(&ar->vdev_setup_done);

	rcu_read_unlock();

	ath11k_dbg(ab, ATH11K_DBG_WMI, "vdev start resp for vdev id %d",
		   vdev_start_resp.vdev_id);
}

static void ath11k_bcn_tx_status_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	u32 vdev_id, tx_status;

	if (ath11k_pull_bcn_tx_status_ev(ab, skb->data, skb->len,
					 &vdev_id, &tx_status) != 0) {
		ath11k_warn(ab, "failed to extract bcn tx status");
		return;
	}
}

static void ath11k_vdev_stopped_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct ath11k *ar;
	u32 vdev_id = 0;

	if (ath11k_pull_vdev_stopped_param_tlv(ab, skb, &vdev_id) != 0) {
		ath11k_warn(ab, "failed to extract vdev stopped event");
		return;
	}

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in vdev stopped ev %d",
			    vdev_id);
		rcu_read_unlock();
		return;
	}

	complete(&ar->vdev_setup_done);

	rcu_read_unlock();

	ath11k_dbg(ab, ATH11K_DBG_WMI, "vdev stopped for vdev id %d", vdev_id);
}

static void ath11k_mgmt_rx_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct mgmt_rx_event_params rx_ev = {0};
	struct ath11k *ar;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *hdr;
	u16 fc;
	struct ieee80211_supported_band *sband;

	if (ath11k_pull_mgmt_rx_params_tlv(ab, skb, &rx_ev) != 0) {
		ath11k_warn(ab, "failed to extract mgmt rx event");
		dev_kfree_skb(skb);
		return;
	}

	memset(status, 0, sizeof(*status));

	ath11k_dbg(ab, ATH11K_DBG_MGMT, "mgmt rx event status %08x\n",
		   rx_ev.status);

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_pdev_id(ab, rx_ev.pdev_id);

	if (!ar) {
		ath11k_warn(ab, "invalid pdev_id %d in mgmt_rx_event\n",
			    rx_ev.pdev_id);
		dev_kfree_skb(skb);
		goto exit;
	}

	if ((test_bit(ATH11K_CAC_RUNNING, &ar->dev_flags)) ||
	    (rx_ev.status & (WMI_RX_STATUS_ERR_DECRYPT |
	    WMI_RX_STATUS_ERR_KEY_CACHE_MISS | WMI_RX_STATUS_ERR_CRC))) {
		dev_kfree_skb(skb);
		goto exit;
	}

	if (rx_ev.status & WMI_RX_STATUS_ERR_MIC)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (rx_ev.chan_freq >= ATH11K_MIN_6G_FREQ &&
	    rx_ev.chan_freq <= ATH11K_MAX_6G_FREQ) {
		status->band = NL80211_BAND_6GHZ;
		status->freq = rx_ev.chan_freq;
	} else if (rx_ev.channel >= 1 && rx_ev.channel <= 14) {
		status->band = NL80211_BAND_2GHZ;
	} else if (rx_ev.channel >= 36 && rx_ev.channel <= ATH11K_MAX_5G_CHAN) {
		status->band = NL80211_BAND_5GHZ;
	} else {
		/* Shouldn't happen unless list of advertised channels to
		 * mac80211 has been changed.
		 */
		WARN_ON_ONCE(1);
		dev_kfree_skb(skb);
		goto exit;
	}

	if (rx_ev.phy_mode == MODE_11B &&
	    (status->band == NL80211_BAND_5GHZ || status->band == NL80211_BAND_6GHZ))
		ath11k_dbg(ab, ATH11K_DBG_WMI,
			   "wmi mgmt rx 11b (CCK) on 5/6GHz, band = %d\n", status->band);

	sband = &ar->mac.sbands[status->band];

	if (status->band != NL80211_BAND_6GHZ)
		status->freq = ieee80211_channel_to_frequency(rx_ev.channel,
							      status->band);

	status->signal = rx_ev.snr + ATH11K_DEFAULT_NOISE_FLOOR;
	status->rate_idx = ath11k_mac_bitrate_to_idx(sband, rx_ev.rate / 100);

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	/* Firmware is guaranteed to report all essential management frames via
	 * WMI while it can deliver some extra via HTT. Since there can be
	 * duplicates split the reporting wrt monitor/sniffing.
	 */
	status->flag |= RX_FLAG_SKIP_MONITOR;

	/* In case of PMF, FW delivers decrypted frames with Protected Bit set.
	 * Don't clear that. Also, FW delivers broadcast management frames
	 * (ex: group privacy action frames in mesh) as encrypted payload.
	 */
	if (ieee80211_has_protected(hdr->frame_control) &&
	    !is_multicast_ether_addr(ieee80211_get_DA(hdr))) {
		status->flag |= RX_FLAG_DECRYPTED;

		if (!ieee80211_is_robust_mgmt_frame(skb)) {
			status->flag |= RX_FLAG_IV_STRIPPED |
					RX_FLAG_MMIC_STRIPPED;
			hdr->frame_control = __cpu_to_le16(fc &
					     ~IEEE80211_FCTL_PROTECTED);
		}
	}

	if (ieee80211_is_beacon(hdr->frame_control))
		ath11k_mac_handle_beacon(ar, skb);

	ath11k_dbg(ab, ATH11K_DBG_MGMT,
		   "event mgmt rx skb %pK len %d ftype %02x stype %02x\n",
		   skb, skb->len,
		   fc & IEEE80211_FCTL_FTYPE, fc & IEEE80211_FCTL_STYPE);

	ath11k_dbg(ab, ATH11K_DBG_MGMT,
		   "event mgmt rx freq %d band %d snr %d, rate_idx %d\n",
		   status->freq, status->band, status->signal,
		   status->rate_idx);

	ieee80211_rx_ni(ar->hw, skb);

exit:
	rcu_read_unlock();
}

static void ath11k_mgmt_tx_compl_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_mgmt_tx_compl_event tx_compl_param = {0};
	struct ath11k *ar;

	if (ath11k_pull_mgmt_tx_compl_param_tlv(ab, skb, &tx_compl_param) != 0) {
		ath11k_warn(ab, "failed to extract mgmt tx compl event");
		return;
	}

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_pdev_id(ab, tx_compl_param.pdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid pdev id %d in mgmt_tx_compl_event\n",
			    tx_compl_param.pdev_id);
		goto exit;
	}

	wmi_process_mgmt_tx_comp(ar, tx_compl_param.desc_id,
				 tx_compl_param.status);

	ath11k_dbg(ab, ATH11K_DBG_MGMT,
		   "mgmt tx compl ev pdev_id %d, desc_id %d, status %d",
		   tx_compl_param.pdev_id, tx_compl_param.desc_id,
		   tx_compl_param.status);

exit:
	rcu_read_unlock();
}

static struct ath11k *ath11k_get_ar_on_scan_state(struct ath11k_base *ab,
						  u32 vdev_id,
						  enum ath11k_scan_state state)
{
	int i;
	struct ath11k_pdev *pdev;
	struct ath11k *ar;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = rcu_dereference(ab->pdevs_active[i]);
		if (pdev && pdev->ar) {
			ar = pdev->ar;

			spin_lock_bh(&ar->data_lock);
			if (ar->scan.state == state &&
			    ar->scan.vdev_id == vdev_id) {
				spin_unlock_bh(&ar->data_lock);
				return ar;
			}
			spin_unlock_bh(&ar->data_lock);
		}
	}
	return NULL;
}

static void ath11k_scan_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct ath11k *ar;
	struct wmi_scan_event scan_ev = {0};

	if (ath11k_pull_scan_ev(ab, skb, &scan_ev) != 0) {
		ath11k_warn(ab, "failed to extract scan event");
		return;
	}

	rcu_read_lock();

	/* In case the scan was cancelled, ex. during interface teardown,
	 * the interface will not be found in active interfaces.
	 * Rather, in such scenarios, iterate over the active pdev's to
	 * search 'ar' if the corresponding 'ar' scan is ABORTING and the
	 * aborting scan's vdev id matches this event info.
	 */
	if (scan_ev.event_type == WMI_SCAN_EVENT_COMPLETED &&
	    scan_ev.reason == WMI_SCAN_REASON_CANCELLED) {
		ar = ath11k_get_ar_on_scan_state(ab, scan_ev.vdev_id,
						 ATH11K_SCAN_ABORTING);
		if (!ar)
			ar = ath11k_get_ar_on_scan_state(ab, scan_ev.vdev_id,
							 ATH11K_SCAN_RUNNING);
	} else {
		ar = ath11k_mac_get_ar_by_vdev_id(ab, scan_ev.vdev_id);
	}

	if (!ar) {
		ath11k_warn(ab, "Received scan event for unknown vdev");
		rcu_read_unlock();
		return;
	}

	spin_lock_bh(&ar->data_lock);

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "scan event %s type %d reason %d freq %d req_id %d scan_id %d vdev_id %d state %s (%d)\n",
		   ath11k_wmi_event_scan_type_str(scan_ev.event_type, scan_ev.reason),
		   scan_ev.event_type, scan_ev.reason, scan_ev.channel_freq,
		   scan_ev.scan_req_id, scan_ev.scan_id, scan_ev.vdev_id,
		   ath11k_scan_state_str(ar->scan.state), ar->scan.state);

	switch (scan_ev.event_type) {
	case WMI_SCAN_EVENT_STARTED:
		ath11k_wmi_event_scan_started(ar);
		break;
	case WMI_SCAN_EVENT_COMPLETED:
		ath11k_wmi_event_scan_completed(ar);
		break;
	case WMI_SCAN_EVENT_BSS_CHANNEL:
		ath11k_wmi_event_scan_bss_chan(ar);
		break;
	case WMI_SCAN_EVENT_FOREIGN_CHAN:
		ath11k_wmi_event_scan_foreign_chan(ar, scan_ev.channel_freq);
		break;
	case WMI_SCAN_EVENT_START_FAILED:
		ath11k_warn(ab, "received scan start failure event\n");
		ath11k_wmi_event_scan_start_failed(ar);
		break;
	case WMI_SCAN_EVENT_DEQUEUED:
		__ath11k_mac_scan_finish(ar);
		break;
	case WMI_SCAN_EVENT_PREEMPTED:
	case WMI_SCAN_EVENT_RESTARTED:
	case WMI_SCAN_EVENT_FOREIGN_CHAN_EXIT:
	default:
		break;
	}

	spin_unlock_bh(&ar->data_lock);

	rcu_read_unlock();
}

static void ath11k_peer_sta_kickout_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_peer_sta_kickout_arg arg = {};
	struct ieee80211_sta *sta;
	struct ath11k_peer *peer;
	struct ath11k *ar;

	if (ath11k_pull_peer_sta_kickout_ev(ab, skb, &arg) != 0) {
		ath11k_warn(ab, "failed to extract peer sta kickout event");
		return;
	}

	rcu_read_lock();

	spin_lock_bh(&ab->base_lock);

	peer = ath11k_peer_find_by_addr(ab, arg.mac_addr);

	if (!peer) {
		ath11k_warn(ab, "peer not found %pM\n",
			    arg.mac_addr);
		goto exit;
	}

	ar = ath11k_mac_get_ar_by_vdev_id(ab, peer->vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in peer sta kickout ev %d",
			    peer->vdev_id);
		goto exit;
	}

	sta = ieee80211_find_sta_by_ifaddr(ar->hw,
					   arg.mac_addr, NULL);
	if (!sta) {
		ath11k_warn(ab, "Spurious quick kickout for STA %pM\n",
			    arg.mac_addr);
		goto exit;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI, "peer sta kickout event %pM",
		   arg.mac_addr);

	ieee80211_report_low_ack(sta, 10);

exit:
	spin_unlock_bh(&ab->base_lock);
	rcu_read_unlock();
}

static void ath11k_roam_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_roam_event roam_ev = {};
	struct ath11k *ar;

	if (ath11k_pull_roam_ev(ab, skb, &roam_ev) != 0) {
		ath11k_warn(ab, "failed to extract roam event");
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "wmi roam event vdev %u reason 0x%08x rssi %d\n",
		   roam_ev.vdev_id, roam_ev.reason, roam_ev.rssi);

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, roam_ev.vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in roam ev %d",
			    roam_ev.vdev_id);
		rcu_read_unlock();
		return;
	}

	if (roam_ev.reason >= WMI_ROAM_REASON_MAX)
		ath11k_warn(ab, "ignoring unknown roam event reason %d on vdev %i\n",
			    roam_ev.reason, roam_ev.vdev_id);

	switch (roam_ev.reason) {
	case WMI_ROAM_REASON_BEACON_MISS:
		ath11k_mac_handle_beacon_miss(ar, roam_ev.vdev_id);
		break;
	case WMI_ROAM_REASON_BETTER_AP:
	case WMI_ROAM_REASON_LOW_RSSI:
	case WMI_ROAM_REASON_SUITABLE_AP_FOUND:
	case WMI_ROAM_REASON_HO_FAILED:
		ath11k_warn(ab, "ignoring not implemented roam event reason %d on vdev %i\n",
			    roam_ev.reason, roam_ev.vdev_id);
		break;
	}

	rcu_read_unlock();
}

static void ath11k_chan_info_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_chan_info_event ch_info_ev = {0};
	struct ath11k *ar;
	struct survey_info *survey;
	int idx;
	/* HW channel counters frequency value in hertz */
	u32 cc_freq_hz = ab->cc_freq_hz;

	if (ath11k_pull_chan_info_ev(ab, skb->data, skb->len, &ch_info_ev) != 0) {
		ath11k_warn(ab, "failed to extract chan info event");
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "chan info vdev_id %d err_code %d freq %d cmd_flags %d noise_floor %d rx_clear_count %d cycle_count %d mac_clk_mhz %d\n",
		   ch_info_ev.vdev_id, ch_info_ev.err_code, ch_info_ev.freq,
		   ch_info_ev.cmd_flags, ch_info_ev.noise_floor,
		   ch_info_ev.rx_clear_count, ch_info_ev.cycle_count,
		   ch_info_ev.mac_clk_mhz);

	if (ch_info_ev.cmd_flags == WMI_CHAN_INFO_END_RESP) {
		ath11k_dbg(ab, ATH11K_DBG_WMI, "chan info report completed\n");
		return;
	}

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, ch_info_ev.vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in chan info ev %d",
			    ch_info_ev.vdev_id);
		rcu_read_unlock();
		return;
	}
	spin_lock_bh(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
	case ATH11K_SCAN_STARTING:
		ath11k_warn(ab, "received chan info event without a scan request, ignoring\n");
		goto exit;
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		break;
	}

	idx = freq_to_idx(ar, ch_info_ev.freq);
	if (idx >= ARRAY_SIZE(ar->survey)) {
		ath11k_warn(ab, "chan info: invalid frequency %d (idx %d out of bounds)\n",
			    ch_info_ev.freq, idx);
		goto exit;
	}

	/* If FW provides MAC clock frequency in Mhz, overriding the initialized
	 * HW channel counters frequency value
	 */
	if (ch_info_ev.mac_clk_mhz)
		cc_freq_hz = (ch_info_ev.mac_clk_mhz * 1000);

	if (ch_info_ev.cmd_flags == WMI_CHAN_INFO_START_RESP) {
		survey = &ar->survey[idx];
		memset(survey, 0, sizeof(*survey));
		survey->noise = ch_info_ev.noise_floor;
		survey->filled = SURVEY_INFO_NOISE_DBM | SURVEY_INFO_TIME |
				 SURVEY_INFO_TIME_BUSY;
		survey->time = div_u64(ch_info_ev.cycle_count, cc_freq_hz);
		survey->time_busy = div_u64(ch_info_ev.rx_clear_count, cc_freq_hz);
	}
exit:
	spin_unlock_bh(&ar->data_lock);
	rcu_read_unlock();
}

static void
ath11k_pdev_bss_chan_info_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_pdev_bss_chan_info_event bss_ch_info_ev = {};
	struct survey_info *survey;
	struct ath11k *ar;
	u32 cc_freq_hz = ab->cc_freq_hz;
	u64 busy, total, tx, rx, rx_bss;
	int idx;

	if (ath11k_pull_pdev_bss_chan_info_ev(ab, skb, &bss_ch_info_ev) != 0) {
		ath11k_warn(ab, "failed to extract pdev bss chan info event");
		return;
	}

	busy = (u64)(bss_ch_info_ev.rx_clear_count_high) << 32 |
			bss_ch_info_ev.rx_clear_count_low;

	total = (u64)(bss_ch_info_ev.cycle_count_high) << 32 |
			bss_ch_info_ev.cycle_count_low;

	tx = (u64)(bss_ch_info_ev.tx_cycle_count_high) << 32 |
			bss_ch_info_ev.tx_cycle_count_low;

	rx = (u64)(bss_ch_info_ev.rx_cycle_count_high) << 32 |
			bss_ch_info_ev.rx_cycle_count_low;

	rx_bss = (u64)(bss_ch_info_ev.rx_bss_cycle_count_high) << 32 |
			bss_ch_info_ev.rx_bss_cycle_count_low;

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "pdev bss chan info:\n pdev_id: %d freq: %d noise: %d cycle: busy %llu total %llu tx %llu rx %llu rx_bss %llu\n",
		   bss_ch_info_ev.pdev_id, bss_ch_info_ev.freq,
		   bss_ch_info_ev.noise_floor, busy, total,
		   tx, rx, rx_bss);

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_pdev_id(ab, bss_ch_info_ev.pdev_id);

	if (!ar) {
		ath11k_warn(ab, "invalid pdev id %d in bss_chan_info event\n",
			    bss_ch_info_ev.pdev_id);
		rcu_read_unlock();
		return;
	}

	spin_lock_bh(&ar->data_lock);
	idx = freq_to_idx(ar, bss_ch_info_ev.freq);
	if (idx >= ARRAY_SIZE(ar->survey)) {
		ath11k_warn(ab, "bss chan info: invalid frequency %d (idx %d out of bounds)\n",
			    bss_ch_info_ev.freq, idx);
		goto exit;
	}

	survey = &ar->survey[idx];

	survey->noise     = bss_ch_info_ev.noise_floor;
	survey->time      = div_u64(total, cc_freq_hz);
	survey->time_busy = div_u64(busy, cc_freq_hz);
	survey->time_rx   = div_u64(rx_bss, cc_freq_hz);
	survey->time_tx   = div_u64(tx, cc_freq_hz);
	survey->filled   |= (SURVEY_INFO_NOISE_DBM |
			     SURVEY_INFO_TIME |
			     SURVEY_INFO_TIME_BUSY |
			     SURVEY_INFO_TIME_RX |
			     SURVEY_INFO_TIME_TX);
exit:
	spin_unlock_bh(&ar->data_lock);
	complete(&ar->bss_survey_done);

	rcu_read_unlock();
}

static void ath11k_vdev_install_key_compl_event(struct ath11k_base *ab,
						struct sk_buff *skb)
{
	struct wmi_vdev_install_key_complete_arg install_key_compl = {0};
	struct ath11k *ar;

	if (ath11k_pull_vdev_install_key_compl_ev(ab, skb, &install_key_compl) != 0) {
		ath11k_warn(ab, "failed to extract install key compl event");
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "vdev install key ev idx %d flags %08x macaddr %pM status %d\n",
		   install_key_compl.key_idx, install_key_compl.key_flags,
		   install_key_compl.macaddr, install_key_compl.status);

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, install_key_compl.vdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in install key compl ev %d",
			    install_key_compl.vdev_id);
		rcu_read_unlock();
		return;
	}

	ar->install_key_status = 0;

	if (install_key_compl.status != WMI_VDEV_INSTALL_KEY_COMPL_STATUS_SUCCESS) {
		ath11k_warn(ab, "install key failed for %pM status %d\n",
			    install_key_compl.macaddr, install_key_compl.status);
		ar->install_key_status = install_key_compl.status;
	}

	complete(&ar->install_key_done);
	rcu_read_unlock();
}

static void ath11k_service_available_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	const void **tb;
	const struct wmi_service_available_event *ev;
	int ret;
	int i, j;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return;
	}

	ev = tb[WMI_TAG_SERVICE_AVAILABLE_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch svc available ev");
		kfree(tb);
		return;
	}

	/* TODO: Use wmi_service_segment_offset information to get the service
	 * especially when more services are advertised in multiple sevice
	 * available events.
	 */
	for (i = 0, j = WMI_MAX_SERVICE;
	     i < WMI_SERVICE_SEGMENT_BM_SIZE32 && j < WMI_MAX_EXT_SERVICE;
	     i++) {
		do {
			if (ev->wmi_service_segment_bitmap[i] &
			    BIT(j % WMI_AVAIL_SERVICE_BITS_IN_SIZE32))
				set_bit(j, ab->wmi_ab.svc_map);
		} while (++j % WMI_AVAIL_SERVICE_BITS_IN_SIZE32);
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "wmi_ext_service_bitmap 0:0x%x, 1:0x%x, 2:0x%x, 3:0x%x",
		   ev->wmi_service_segment_bitmap[0], ev->wmi_service_segment_bitmap[1],
		   ev->wmi_service_segment_bitmap[2], ev->wmi_service_segment_bitmap[3]);

	kfree(tb);
}

static void ath11k_peer_assoc_conf_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_peer_assoc_conf_arg peer_assoc_conf = {0};
	struct ath11k *ar;

	if (ath11k_pull_peer_assoc_conf_ev(ab, skb, &peer_assoc_conf) != 0) {
		ath11k_warn(ab, "failed to extract peer assoc conf event");
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "peer assoc conf ev vdev id %d macaddr %pM\n",
		   peer_assoc_conf.vdev_id, peer_assoc_conf.macaddr);

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, peer_assoc_conf.vdev_id);

	if (!ar) {
		ath11k_warn(ab, "invalid vdev id in peer assoc conf ev %d",
			    peer_assoc_conf.vdev_id);
		rcu_read_unlock();
		return;
	}

	complete(&ar->peer_assoc_done);
	rcu_read_unlock();
}

static void ath11k_update_stats_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	ath11k_debugfs_fw_stats_process(ab, skb);
}

/* PDEV_CTL_FAILSAFE_CHECK_EVENT is received from FW when the frequency scanned
 * is not part of BDF CTL(Conformance test limits) table entries.
 */
static void ath11k_pdev_ctl_failsafe_check_event(struct ath11k_base *ab,
						 struct sk_buff *skb)
{
	const void **tb;
	const struct wmi_pdev_ctl_failsafe_chk_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return;
	}

	ev = tb[WMI_TAG_PDEV_CTL_FAILSAFE_CHECK_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch pdev ctl failsafe check ev");
		kfree(tb);
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "pdev ctl failsafe check ev status %d\n",
		   ev->ctl_failsafe_status);

	/* If ctl_failsafe_status is set to 1 FW will max out the Transmit power
	 * to 10 dBm else the CTL power entry in the BDF would be picked up.
	 */
	if (ev->ctl_failsafe_status != 0)
		ath11k_warn(ab, "pdev ctl failsafe failure status %d",
			    ev->ctl_failsafe_status);

	kfree(tb);
}

static void
ath11k_wmi_process_csa_switch_count_event(struct ath11k_base *ab,
					  const struct wmi_pdev_csa_switch_ev *ev,
					  const u32 *vdev_ids)
{
	int i;
	struct ath11k_vif *arvif;

	/* Finish CSA once the switch count becomes NULL */
	if (ev->current_switch_count)
		return;

	rcu_read_lock();
	for (i = 0; i < ev->num_vdevs; i++) {
		arvif = ath11k_mac_get_arvif_by_vdev_id(ab, vdev_ids[i]);

		if (!arvif) {
			ath11k_warn(ab, "Recvd csa status for unknown vdev %d",
				    vdev_ids[i]);
			continue;
		}

		if (arvif->is_up && arvif->vif->csa_active)
			ieee80211_csa_finish(arvif->vif);
	}
	rcu_read_unlock();
}

static void
ath11k_wmi_pdev_csa_switch_count_status_event(struct ath11k_base *ab,
					      struct sk_buff *skb)
{
	const void **tb;
	const struct wmi_pdev_csa_switch_ev *ev;
	const u32 *vdev_ids;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return;
	}

	ev = tb[WMI_TAG_PDEV_CSA_SWITCH_COUNT_STATUS_EVENT];
	vdev_ids = tb[WMI_TAG_ARRAY_UINT32];

	if (!ev || !vdev_ids) {
		ath11k_warn(ab, "failed to fetch pdev csa switch count ev");
		kfree(tb);
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "pdev csa switch count %d for pdev %d, num_vdevs %d",
		   ev->current_switch_count, ev->pdev_id,
		   ev->num_vdevs);

	ath11k_wmi_process_csa_switch_count_event(ab, ev, vdev_ids);

	kfree(tb);
}

static void
ath11k_wmi_pdev_dfs_radar_detected_event(struct ath11k_base *ab, struct sk_buff *skb)
{
	const void **tb;
	const struct wmi_pdev_radar_ev *ev;
	struct ath11k *ar;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return;
	}

	ev = tb[WMI_TAG_PDEV_DFS_RADAR_DETECTION_EVENT];

	if (!ev) {
		ath11k_warn(ab, "failed to fetch pdev dfs radar detected ev");
		kfree(tb);
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "pdev dfs radar detected on pdev %d, detection mode %d, chan freq %d, chan_width %d, detector id %d, seg id %d, timestamp %d, chirp %d, freq offset %d, sidx %d",
		   ev->pdev_id, ev->detection_mode, ev->chan_freq, ev->chan_width,
		   ev->detector_id, ev->segment_id, ev->timestamp, ev->is_chirp,
		   ev->freq_offset, ev->sidx);

	ar = ath11k_mac_get_ar_by_pdev_id(ab, ev->pdev_id);

	if (!ar) {
		ath11k_warn(ab, "radar detected in invalid pdev %d\n",
			    ev->pdev_id);
		goto exit;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_REG, "DFS Radar Detected in pdev %d\n",
		   ev->pdev_id);

	if (ar->dfs_block_radar_events)
		ath11k_info(ab, "DFS Radar detected, but ignored as requested\n");
	else
		ieee80211_radar_detected(ar->hw);

exit:
	kfree(tb);
}

static void
ath11k_wmi_pdev_temperature_event(struct ath11k_base *ab,
				  struct sk_buff *skb)
{
	struct ath11k *ar;
	const void **tb;
	const struct wmi_pdev_temperature_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse tlv: %d\n", ret);
		return;
	}

	ev = tb[WMI_TAG_PDEV_TEMPERATURE_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch pdev temp ev");
		kfree(tb);
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "pdev temperature ev temp %d pdev_id %d\n", ev->temp, ev->pdev_id);

	ar = ath11k_mac_get_ar_by_pdev_id(ab, ev->pdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid pdev id in pdev temperature ev %d", ev->pdev_id);
		kfree(tb);
		return;
	}

	ath11k_thermal_event_temperature(ar, ev->temp);

	kfree(tb);
}

static void ath11k_fils_discovery_event(struct ath11k_base *ab,
					struct sk_buff *skb)
{
	const void **tb;
	const struct wmi_fils_discovery_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab,
			    "failed to parse FILS discovery event tlv %d\n",
			    ret);
		return;
	}

	ev = tb[WMI_TAG_HOST_SWFDA_EVENT];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch FILS discovery event\n");
		kfree(tb);
		return;
	}

	ath11k_warn(ab,
		    "FILS discovery frame expected from host for vdev_id: %u, transmission scheduled at %u, next TBTT: %u\n",
		    ev->vdev_id, ev->fils_tt, ev->tbtt);

	kfree(tb);
}

static void ath11k_probe_resp_tx_status_event(struct ath11k_base *ab,
					      struct sk_buff *skb)
{
	const void **tb;
	const struct wmi_probe_resp_tx_status_event *ev;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab,
			    "failed to parse probe response transmission status event tlv: %d\n",
			    ret);
		return;
	}

	ev = tb[WMI_TAG_OFFLOAD_PRB_RSP_TX_STATUS_EVENT];
	if (!ev) {
		ath11k_warn(ab,
			    "failed to fetch probe response transmission status event");
		kfree(tb);
		return;
	}

	if (ev->tx_status)
		ath11k_warn(ab,
			    "Probe response transmission failed for vdev_id %u, status %u\n",
			    ev->vdev_id, ev->tx_status);

	kfree(tb);
}

static int ath11k_wmi_tlv_wow_wakeup_host_parse(struct ath11k_base *ab,
						u16 tag, u16 len,
						const void *ptr, void *data)
{
	struct wmi_wow_ev_arg *ev = data;
	const char *wow_pg_fault;
	int wow_pg_len;

	switch (tag) {
	case WMI_TAG_WOW_EVENT_INFO:
		memcpy(ev, ptr, sizeof(*ev));
		ath11k_dbg(ab, ATH11K_DBG_WMI, "wow wakeup host reason %d %s\n",
			   ev->wake_reason, wow_reason(ev->wake_reason));
		break;

	case WMI_TAG_ARRAY_BYTE:
		if (ev && ev->wake_reason == WOW_REASON_PAGE_FAULT) {
			wow_pg_fault = ptr;
			/* the first 4 bytes are length */
			wow_pg_len = *(int *)wow_pg_fault;
			wow_pg_fault += sizeof(int);
			ath11k_dbg(ab, ATH11K_DBG_WMI, "wow data_len = %d\n",
				   wow_pg_len);
			ath11k_dbg_dump(ab, ATH11K_DBG_WMI,
					"wow_event_info_type packet present",
					"wow_pg_fault ",
					wow_pg_fault,
					wow_pg_len);
		}
		break;
	default:
		break;
	}

	return 0;
}

static void ath11k_wmi_event_wow_wakeup_host(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_wow_ev_arg ev = { };
	int ret;

	ret = ath11k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath11k_wmi_tlv_wow_wakeup_host_parse,
				  &ev);
	if (ret) {
		ath11k_warn(ab, "failed to parse wmi wow tlv: %d\n", ret);
		return;
	}

	complete(&ab->wow.wakeup_completed);
}

static void ath11k_wmi_tlv_op_rx(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct wmi_cmd_hdr *cmd_hdr;
	enum wmi_tlv_event_id id;

	cmd_hdr = (struct wmi_cmd_hdr *)skb->data;
	id = FIELD_GET(WMI_CMD_HDR_CMD_ID, (cmd_hdr->cmd_id));

	if (skb_pull(skb, sizeof(struct wmi_cmd_hdr)) == NULL)
		goto out;

	switch (id) {
		/* Process all the WMI events here */
	case WMI_SERVICE_READY_EVENTID:
		ath11k_service_ready_event(ab, skb);
		break;
	case WMI_SERVICE_READY_EXT_EVENTID:
		ath11k_service_ready_ext_event(ab, skb);
		break;
	case WMI_SERVICE_READY_EXT2_EVENTID:
		ath11k_service_ready_ext2_event(ab, skb);
		break;
	case WMI_REG_CHAN_LIST_CC_EVENTID:
		ath11k_reg_chan_list_event(ab, skb);
		break;
	case WMI_READY_EVENTID:
		ath11k_ready_event(ab, skb);
		break;
	case WMI_PEER_DELETE_RESP_EVENTID:
		ath11k_peer_delete_resp_event(ab, skb);
		break;
	case WMI_VDEV_START_RESP_EVENTID:
		ath11k_vdev_start_resp_event(ab, skb);
		break;
	case WMI_OFFLOAD_BCN_TX_STATUS_EVENTID:
		ath11k_bcn_tx_status_event(ab, skb);
		break;
	case WMI_VDEV_STOPPED_EVENTID:
		ath11k_vdev_stopped_event(ab, skb);
		break;
	case WMI_MGMT_RX_EVENTID:
		ath11k_mgmt_rx_event(ab, skb);
		/* mgmt_rx_event() owns the skb now! */
		return;
	case WMI_MGMT_TX_COMPLETION_EVENTID:
		ath11k_mgmt_tx_compl_event(ab, skb);
		break;
	case WMI_SCAN_EVENTID:
		ath11k_scan_event(ab, skb);
		break;
	case WMI_PEER_STA_KICKOUT_EVENTID:
		ath11k_peer_sta_kickout_event(ab, skb);
		break;
	case WMI_ROAM_EVENTID:
		ath11k_roam_event(ab, skb);
		break;
	case WMI_CHAN_INFO_EVENTID:
		ath11k_chan_info_event(ab, skb);
		break;
	case WMI_PDEV_BSS_CHAN_INFO_EVENTID:
		ath11k_pdev_bss_chan_info_event(ab, skb);
		break;
	case WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID:
		ath11k_vdev_install_key_compl_event(ab, skb);
		break;
	case WMI_SERVICE_AVAILABLE_EVENTID:
		ath11k_service_available_event(ab, skb);
		break;
	case WMI_PEER_ASSOC_CONF_EVENTID:
		ath11k_peer_assoc_conf_event(ab, skb);
		break;
	case WMI_UPDATE_STATS_EVENTID:
		ath11k_update_stats_event(ab, skb);
		break;
	case WMI_PDEV_CTL_FAILSAFE_CHECK_EVENTID:
		ath11k_pdev_ctl_failsafe_check_event(ab, skb);
		break;
	case WMI_PDEV_CSA_SWITCH_COUNT_STATUS_EVENTID:
		ath11k_wmi_pdev_csa_switch_count_status_event(ab, skb);
		break;
	case WMI_PDEV_TEMPERATURE_EVENTID:
		ath11k_wmi_pdev_temperature_event(ab, skb);
		break;
	case WMI_PDEV_DMA_RING_BUF_RELEASE_EVENTID:
		ath11k_wmi_pdev_dma_ring_buf_release_event(ab, skb);
		break;
	case WMI_HOST_FILS_DISCOVERY_EVENTID:
		ath11k_fils_discovery_event(ab, skb);
		break;
	case WMI_OFFLOAD_PROB_RESP_TX_STATUS_EVENTID:
		ath11k_probe_resp_tx_status_event(ab, skb);
		break;
	/* add Unsupported events here */
	case WMI_TBTTOFFSET_EXT_UPDATE_EVENTID:
	case WMI_PEER_OPER_MODE_CHANGE_EVENTID:
	case WMI_TWT_ENABLE_EVENTID:
	case WMI_TWT_DISABLE_EVENTID:
	case WMI_PDEV_DMA_RING_CFG_RSP_EVENTID:
	case WMI_PEER_CREATE_CONF_EVENTID:
		ath11k_dbg(ab, ATH11K_DBG_WMI,
			   "ignoring unsupported event 0x%x\n", id);
		break;
	case WMI_PDEV_DFS_RADAR_DETECTION_EVENTID:
		ath11k_wmi_pdev_dfs_radar_detected_event(ab, skb);
		break;
	case WMI_VDEV_DELETE_RESP_EVENTID:
		ath11k_vdev_delete_resp_event(ab, skb);
		break;
	case WMI_WOW_WAKEUP_HOST_EVENTID:
		ath11k_wmi_event_wow_wakeup_host(ab, skb);
		break;
	/* TODO: Add remaining events */
	default:
		ath11k_dbg(ab, ATH11K_DBG_WMI, "Unknown eventid: 0x%x\n", id);
		break;
	}

out:
	dev_kfree_skb(skb);
}

static int ath11k_connect_pdev_htc_service(struct ath11k_base *ab,
					   u32 pdev_idx)
{
	int status;
	u32 svc_id[] = { ATH11K_HTC_SVC_ID_WMI_CONTROL,
			 ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1,
			 ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2 };

	struct ath11k_htc_svc_conn_req conn_req;
	struct ath11k_htc_svc_conn_resp conn_resp;

	memset(&conn_req, 0, sizeof(conn_req));
	memset(&conn_resp, 0, sizeof(conn_resp));

	/* these fields are the same for all service endpoints */
	conn_req.ep_ops.ep_tx_complete = ath11k_wmi_htc_tx_complete;
	conn_req.ep_ops.ep_rx_complete = ath11k_wmi_tlv_op_rx;
	conn_req.ep_ops.ep_tx_credits = ath11k_wmi_op_ep_tx_credits;

	/* connect to control service */
	conn_req.service_id = svc_id[pdev_idx];

	status = ath11k_htc_connect_service(&ab->htc, &conn_req, &conn_resp);
	if (status) {
		ath11k_warn(ab, "failed to connect to WMI CONTROL service status: %d\n",
			    status);
		return status;
	}

	ab->wmi_ab.wmi_endpoint_id[pdev_idx] = conn_resp.eid;
	ab->wmi_ab.wmi[pdev_idx].eid = conn_resp.eid;
	ab->wmi_ab.max_msg_len[pdev_idx] = conn_resp.max_msg_len;

	return 0;
}

static int
ath11k_wmi_send_unit_test_cmd(struct ath11k *ar,
			      struct wmi_unit_test_cmd ut_cmd,
			      u32 *test_args)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct wmi_unit_test_cmd *cmd;
	struct sk_buff *skb;
	struct wmi_tlv *tlv;
	void *ptr;
	u32 *ut_cmd_args;
	int buf_len, arg_len;
	int ret;
	int i;

	arg_len = sizeof(u32) * ut_cmd.num_args;
	buf_len = sizeof(ut_cmd) + arg_len + TLV_HDR_SIZE;

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, buf_len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_unit_test_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_UNIT_TEST_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(ut_cmd) - TLV_HDR_SIZE);

	cmd->vdev_id = ut_cmd.vdev_id;
	cmd->module_id = ut_cmd.module_id;
	cmd->num_args = ut_cmd.num_args;
	cmd->diag_token = ut_cmd.diag_token;

	ptr = skb->data + sizeof(ut_cmd);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_UINT32) |
		      FIELD_PREP(WMI_TLV_LEN, arg_len);

	ptr += TLV_HDR_SIZE;

	ut_cmd_args = ptr;
	for (i = 0; i < ut_cmd.num_args; i++)
		ut_cmd_args[i] = test_args[i];

	ret = ath11k_wmi_cmd_send(wmi, skb, WMI_UNIT_TEST_CMDID);

	if (ret) {
		ath11k_warn(ar->ab, "failed to send WMI_UNIT_TEST CMD :%d\n",
			    ret);
		dev_kfree_skb(skb);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
		   "WMI unit test : module %d vdev %d n_args %d token %d\n",
		   cmd->module_id, cmd->vdev_id, cmd->num_args,
		   cmd->diag_token);

	return ret;
}

int ath11k_wmi_simulate_radar(struct ath11k *ar)
{
	struct ath11k_vif *arvif;
	u32 dfs_args[DFS_MAX_TEST_ARGS];
	struct wmi_unit_test_cmd wmi_ut;
	bool arvif_found = false;

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->is_started && arvif->vdev_type == WMI_VDEV_TYPE_AP) {
			arvif_found = true;
			break;
		}
	}

	if (!arvif_found)
		return -EINVAL;

	dfs_args[DFS_TEST_CMDID] = 0;
	dfs_args[DFS_TEST_PDEV_ID] = ar->pdev->pdev_id;
	/* Currently we could pass segment_id(b0 - b1), chirp(b2)
	 * freq offset (b3 - b10) to unit test. For simulation
	 * purpose this can be set to 0 which is valid.
	 */
	dfs_args[DFS_TEST_RADAR_PARAM] = 0;

	wmi_ut.vdev_id = arvif->vdev_id;
	wmi_ut.module_id = DFS_UNIT_TEST_MODULE;
	wmi_ut.num_args = DFS_MAX_TEST_ARGS;
	wmi_ut.diag_token = DFS_UNIT_TEST_TOKEN;

	ath11k_dbg(ar->ab, ATH11K_DBG_REG, "Triggering Radar Simulation\n");

	return ath11k_wmi_send_unit_test_cmd(ar, wmi_ut, dfs_args);
}

int ath11k_wmi_connect(struct ath11k_base *ab)
{
	u32 i;
	u8 wmi_ep_count;

	wmi_ep_count = ab->htc.wmi_ep_count;
	if (wmi_ep_count > ab->hw_params.max_radios)
		return -1;

	for (i = 0; i < wmi_ep_count; i++)
		ath11k_connect_pdev_htc_service(ab, i);

	return 0;
}

static void ath11k_wmi_pdev_detach(struct ath11k_base *ab, u8 pdev_id)
{
	if (WARN_ON(pdev_id >= MAX_RADIOS))
		return;

	/* TODO: Deinit any pdev specific wmi resource */
}

int ath11k_wmi_pdev_attach(struct ath11k_base *ab,
			   u8 pdev_id)
{
	struct ath11k_pdev_wmi *wmi_handle;

	if (pdev_id >= ab->hw_params.max_radios)
		return -EINVAL;

	wmi_handle = &ab->wmi_ab.wmi[pdev_id];

	wmi_handle->wmi_ab = &ab->wmi_ab;

	ab->wmi_ab.ab = ab;
	/* TODO: Init remaining resource specific to pdev */

	return 0;
}

int ath11k_wmi_attach(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_wmi_pdev_attach(ab, 0);
	if (ret)
		return ret;

	ab->wmi_ab.ab = ab;
	ab->wmi_ab.preferred_hw_mode = WMI_HOST_HW_MODE_MAX;

	/* It's overwritten when service_ext_ready is handled */
	if (ab->hw_params.single_pdev_only)
		ab->wmi_ab.preferred_hw_mode = WMI_HOST_HW_MODE_SINGLE;

	/* TODO: Init remaining wmi soc resources required */
	init_completion(&ab->wmi_ab.service_ready);
	init_completion(&ab->wmi_ab.unified_ready);

	return 0;
}

void ath11k_wmi_detach(struct ath11k_base *ab)
{
	int i;

	/* TODO: Deinit wmi resource specific to SOC as required */

	for (i = 0; i < ab->htc.wmi_ep_count; i++)
		ath11k_wmi_pdev_detach(ab, i);

	ath11k_wmi_free_dbring_caps(ab);
}

int ath11k_wmi_wow_host_wakeup_ind(struct ath11k *ar)
{
	struct wmi_wow_host_wakeup_ind *cmd;
	struct sk_buff *skb;
	size_t len;

	len = sizeof(*cmd);
	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_wow_host_wakeup_ind *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_WOW_HOSTWAKEUP_FROM_SLEEP_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	ath11k_dbg(ar->ab, ATH11K_DBG_WMI, "wmi tlv wow host wakeup ind\n");

	return ath11k_wmi_cmd_send(ar->wmi, skb, WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID);
}

int ath11k_wmi_wow_enable(struct ath11k *ar)
{
	struct wmi_wow_enable_cmd *cmd;
	struct sk_buff *skb;
	int len;

	len = sizeof(*cmd);
	skb = ath11k_wmi_alloc_skb(ar->wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_wow_enable_cmd *)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_WOW_ENABLE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);

	cmd->enable = 1;
	cmd->pause_iface_config = WOW_IFACE_PAUSE_ENABLED;
	ath11k_dbg(ar->ab, ATH11K_DBG_WMI, "wmi tlv wow enable\n");

	return ath11k_wmi_cmd_send(ar->wmi, skb, WMI_WOW_ENABLE_CMDID);
}
