// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/relay.h>
#include "core.h"
#include "debug.h"

struct ath11k_dbring *ath11k_cfr_get_dbring(struct ath11k *ar)
{
	if (ar->cfr_enabled)
		return &ar->cfr.rx_ring;

	return NULL;
}

static int ath11k_cfr_calculate_tones_from_dma_hdr(struct ath11k_cfr_dma_hdr *hdr)
{
	u8 bw = FIELD_GET(CFIR_DMA_HDR_INFO1_UPLOAD_PKT_BW, hdr->info1);
	u8 preamble = FIELD_GET(CFIR_DMA_HDR_INFO1_PREAMBLE_TYPE, hdr->info1);

	switch (preamble) {
	case ATH11K_CFR_PREAMBLE_TYPE_LEGACY:
		fallthrough;
	case ATH11K_CFR_PREAMBLE_TYPE_VHT:
		switch (bw) {
		case 0:
			return TONES_IN_20MHZ;
		case 1: /* DUP40/VHT40 */
			return TONES_IN_40MHZ;
		case 2: /* DUP80/VHT80 */
			return TONES_IN_80MHZ;
		case 3: /* DUP160/VHT160 */
			return TONES_IN_160MHZ;
		default:
			return TONES_INVALID;
		}
	case ATH11K_CFR_PREAMBLE_TYPE_HT:
		switch (bw) {
		case 0:
			return TONES_IN_20MHZ;
		case 1:
			return TONES_IN_40MHZ;
		default:
			return TONES_INVALID;
		}
	default:
		return TONES_INVALID;
	}
}

void ath11k_cfr_release_lut_entry(struct ath11k_look_up_table *lut)
{
	memset(lut, 0, sizeof(*lut));
}

static void ath11k_cfr_rfs_write(struct ath11k *ar, const void *head,
				 u32 head_len, const void *data, u32 data_len,
				 const void *tail, int tail_data)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	if (!cfr->rfs_cfr_capture)
		return;

	relay_write(cfr->rfs_cfr_capture, head, head_len);
	relay_write(cfr->rfs_cfr_capture, data, data_len);
	relay_write(cfr->rfs_cfr_capture, tail, tail_data);
	relay_flush(cfr->rfs_cfr_capture);
}

static void ath11k_cfr_free_pending_dbr_events(struct ath11k *ar)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct ath11k_look_up_table *lut;
	int i;

	if (!cfr->lut)
		return;

	for (i = 0; i < cfr->lut_num; i++) {
		lut = &cfr->lut[i];
		if (lut->dbr_recv && !lut->tx_recv &&
		    lut->dbr_tstamp < cfr->last_success_tstamp) {
			ath11k_dbring_bufs_replenish(ar, &cfr->rx_ring, lut->buff,
						     WMI_DIRECT_BUF_CFR);
			ath11k_cfr_release_lut_entry(lut);
			cfr->flush_dbr_cnt++;
		}
	}
}

/**
 * ath11k_cfr_correlate_and_relay() - Correlate and relay CFR events
 * @ar: Pointer to ath11k structure
 * @lut: Lookup table for correlation
 * @event_type: Type of event received (TX or DBR)
 *
 * Correlates WMI_PDEV_DMA_RING_BUF_RELEASE_EVENT (DBR) and
 * WMI_PEER_CFR_CAPTURE_EVENT (TX capture) by PPDU ID. If both events
 * are present and the PPDU IDs match, returns CORRELATE_STATUS_RELEASE
 * to relay thecorrelated data to userspace. Otherwise returns
 * CORRELATE_STATUS_HOLD to wait for the other event.
 *
 * Also checks pending DBR events and clears them when no corresponding TX
 * capture event is received for the PPDU.
 *
 * Return: CORRELATE_STATUS_RELEASE or CORRELATE_STATUS_HOLD
 */

static enum ath11k_cfr_correlate_status
ath11k_cfr_correlate_and_relay(struct ath11k *ar,
			       struct ath11k_look_up_table *lut,
			       u8 event_type)
{
	enum ath11k_cfr_correlate_status status;
	struct ath11k_cfr *cfr = &ar->cfr;
	u64 diff;

	if (event_type == ATH11K_CORRELATE_TX_EVENT) {
		if (lut->tx_recv)
			cfr->cfr_dma_aborts++;
		cfr->tx_evt_cnt++;
		lut->tx_recv = true;
	} else if (event_type == ATH11K_CORRELATE_DBR_EVENT) {
		cfr->dbr_evt_cnt++;
		lut->dbr_recv = true;
	}

	if (lut->dbr_recv && lut->tx_recv) {
		if (lut->dbr_ppdu_id == lut->tx_ppdu_id) {
			/*
			 * 64-bit counters make wraparound highly improbable,
			 * wraparound handling is omitted.
			 */
			cfr->last_success_tstamp = lut->dbr_tstamp;
			if (lut->dbr_tstamp > lut->txrx_tstamp) {
				diff = lut->dbr_tstamp - lut->txrx_tstamp;
				ath11k_dbg(ar->ab, ATH11K_DBG_CFR,
					   "txrx event -> dbr event delay = %u ms",
					   jiffies_to_msecs(diff));
			} else if (lut->txrx_tstamp > lut->dbr_tstamp) {
				diff = lut->txrx_tstamp - lut->dbr_tstamp;
				ath11k_dbg(ar->ab, ATH11K_DBG_CFR,
					   "dbr event -> txrx event delay = %u ms",
					   jiffies_to_msecs(diff));
			}

			ath11k_cfr_free_pending_dbr_events(ar);

			cfr->release_cnt++;
			status = ATH11K_CORRELATE_STATUS_RELEASE;
		} else {
			/*
			 * Discard TXRX event on PPDU ID mismatch because multiple PPDUs
			 * may share the same DMA address due to ucode aborts.
			 */

			ath11k_dbg(ar->ab, ATH11K_DBG_CFR,
				   "Received dbr event twice for the same lut entry");
			lut->tx_recv = false;
			lut->tx_ppdu_id = 0;
			cfr->clear_txrx_event++;
			cfr->cfr_dma_aborts++;
			status = ATH11K_CORRELATE_STATUS_HOLD;
		}
	} else {
		status = ATH11K_CORRELATE_STATUS_HOLD;
	}

	return status;
}

static int ath11k_cfr_process_data(struct ath11k *ar,
				   struct ath11k_dbring_data *param)
{
	u32 end_magic = ATH11K_CFR_END_MAGIC;
	struct ath11k_csi_cfr_header *header;
	struct ath11k_cfr_dma_hdr *dma_hdr;
	struct ath11k_cfr *cfr = &ar->cfr;
	struct ath11k_look_up_table *lut;
	struct ath11k_base *ab = ar->ab;
	u32 buf_id, tones, length;
	u8 num_chains;
	int status;
	u8 *data;

	data = param->data;
	buf_id = param->buf_id;

	if (param->data_sz < sizeof(*dma_hdr))
		return -EINVAL;

	dma_hdr = (struct ath11k_cfr_dma_hdr *)data;

	tones = ath11k_cfr_calculate_tones_from_dma_hdr(dma_hdr);
	if (tones == TONES_INVALID) {
		ath11k_warn(ar->ab, "Number of tones received is invalid\n");
		return -EINVAL;
	}

	num_chains = FIELD_GET(CFIR_DMA_HDR_INFO1_NUM_CHAINS,
			       dma_hdr->info1);

	length = sizeof(*dma_hdr);
	length += tones * (num_chains + 1);

	spin_lock_bh(&cfr->lut_lock);

	if (!cfr->lut) {
		spin_unlock_bh(&cfr->lut_lock);
		return -EINVAL;
	}

	lut = &cfr->lut[buf_id];

	ath11k_dbg_dump(ab, ATH11K_DBG_CFR_DUMP, "data_from_buf_rel:", "",
			data, length);

	lut->buff = param->buff;
	lut->data = data;
	lut->data_len = length;
	lut->dbr_ppdu_id = dma_hdr->phy_ppdu_id;
	lut->dbr_tstamp = jiffies;

	memcpy(&lut->hdr, dma_hdr, sizeof(*dma_hdr));

	header = &lut->header;
	header->meta_data.channel_bw = FIELD_GET(CFIR_DMA_HDR_INFO1_UPLOAD_PKT_BW,
						 dma_hdr->info1);
	header->meta_data.length = length;

	status = ath11k_cfr_correlate_and_relay(ar, lut,
						ATH11K_CORRELATE_DBR_EVENT);
	if (status == ATH11K_CORRELATE_STATUS_RELEASE) {
		ath11k_dbg(ab, ATH11K_DBG_CFR,
			   "releasing CFR data to user space");
		ath11k_cfr_rfs_write(ar, &lut->header,
				     sizeof(struct ath11k_csi_cfr_header),
				     lut->data, lut->data_len,
				     &end_magic, sizeof(u32));
		ath11k_cfr_release_lut_entry(lut);
	} else if (status == ATH11K_CORRELATE_STATUS_HOLD) {
		ath11k_dbg(ab, ATH11K_DBG_CFR,
			   "tx event is not yet received holding the buf");
	}

	spin_unlock_bh(&cfr->lut_lock);

	return status;
}

static void ath11k_cfr_fill_hdr_info(struct ath11k *ar,
				     struct ath11k_csi_cfr_header *header,
				     struct ath11k_cfr_peer_tx_param *params)
{
	struct ath11k_cfr *cfr;

	cfr = &ar->cfr;
	header->cfr_metadata_version = ATH11K_CFR_META_VERSION_4;
	header->cfr_data_version = ATH11K_CFR_DATA_VERSION_1;
	header->cfr_metadata_len = sizeof(struct cfr_metadata);
	header->chip_type = ar->ab->hw_rev;
	header->meta_data.status = FIELD_GET(WMI_CFR_PEER_CAPTURE_STATUS,
					     params->status);
	header->meta_data.capture_bw = params->bandwidth;

	/*
	 * FW reports phymode will always be HE mode.
	 * Replace it with cached phy mode during peer assoc
	 */
	header->meta_data.phy_mode = cfr->phymode;

	header->meta_data.prim20_chan = params->primary_20mhz_chan;
	header->meta_data.center_freq1 = params->band_center_freq1;
	header->meta_data.center_freq2 = params->band_center_freq2;

	/*
	 * CFR capture is triggered by the ACK of a QoS Null frame:
	 * - 20 MHz: Legacy ACK
	 * - 40/80/160 MHz: DUP Legacy ACK
	 */
	header->meta_data.capture_mode = params->bandwidth ?
		ATH11K_CFR_CAPTURE_DUP_LEGACY_ACK : ATH11K_CFR_CAPTURE_LEGACY_ACK;
	header->meta_data.capture_type = params->capture_method;
	header->meta_data.num_rx_chain = ar->num_rx_chains;
	header->meta_data.sts_count = params->spatial_streams;
	header->meta_data.timestamp = params->timestamp_us;
	ether_addr_copy(header->meta_data.peer_addr, params->peer_mac_addr);
	memcpy(header->meta_data.chain_rssi, params->chain_rssi,
	       sizeof(params->chain_rssi));
	memcpy(header->meta_data.chain_phase, params->chain_phase,
	       sizeof(params->chain_phase));
	memcpy(header->meta_data.agc_gain, params->agc_gain,
	       sizeof(params->agc_gain));
}

int ath11k_process_cfr_capture_event(struct ath11k_base *ab,
				     struct ath11k_cfr_peer_tx_param *params)
{
	struct ath11k_look_up_table *lut = NULL;
	u32 end_magic = ATH11K_CFR_END_MAGIC;
	struct ath11k_csi_cfr_header *header;
	struct ath11k_dbring_element *buff;
	struct ath11k_cfr *cfr;
	dma_addr_t buf_addr;
	struct ath11k *ar;
	u8 tx_status;
	int status;
	int i;

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_vdev_id(ab, params->vdev_id);
	if (!ar) {
		rcu_read_unlock();
		ath11k_warn(ab, "Failed to get ar for vdev id %d\n",
			    params->vdev_id);
		return -ENOENT;
	}

	cfr = &ar->cfr;
	rcu_read_unlock();

	if (WMI_CFR_CAPTURE_STATUS_PEER_PS & params->status) {
		ath11k_warn(ab, "CFR capture failed as peer %pM is in powersave",
			    params->peer_mac_addr);
		return -EINVAL;
	}

	if (!(WMI_CFR_PEER_CAPTURE_STATUS & params->status)) {
		ath11k_warn(ab, "CFR capture failed for the peer : %pM",
			    params->peer_mac_addr);
		cfr->tx_peer_status_cfr_fail++;
		return -EINVAL;
	}

	tx_status = FIELD_GET(WMI_CFR_FRAME_TX_STATUS, params->status);
	if (tx_status != WMI_FRAME_TX_STATUS_OK) {
		ath11k_warn(ab, "WMI tx status %d for the peer %pM",
			    tx_status, params->peer_mac_addr);
		cfr->tx_evt_status_cfr_fail++;
		return -EINVAL;
	}

	buf_addr = (((u64)FIELD_GET(WMI_CFR_CORRELATION_INFO2_BUF_ADDR_HIGH,
				    params->correlation_info_2)) << 32) |
		   params->correlation_info_1;

	spin_lock_bh(&cfr->lut_lock);

	if (!cfr->lut) {
		spin_unlock_bh(&cfr->lut_lock);
		return -EINVAL;
	}

	for (i = 0; i < cfr->lut_num; i++) {
		struct ath11k_look_up_table *temp = &cfr->lut[i];

		if (temp->dbr_address == buf_addr) {
			lut = &cfr->lut[i];
			break;
		}
	}

	if (!lut) {
		spin_unlock_bh(&cfr->lut_lock);
		ath11k_warn(ab, "lut failure to process tx event\n");
		cfr->tx_dbr_lookup_fail++;
		return -EINVAL;
	}

	lut->tx_ppdu_id = FIELD_GET(WMI_CFR_CORRELATION_INFO2_PPDU_ID,
				    params->correlation_info_2);
	lut->txrx_tstamp = jiffies;

	header = &lut->header;
	header->start_magic_num = ATH11K_CFR_START_MAGIC;
	header->vendorid = VENDOR_QCA;
	header->platform_type = PLATFORM_TYPE_ARM;

	ath11k_cfr_fill_hdr_info(ar, header, params);

	status = ath11k_cfr_correlate_and_relay(ar, lut,
						ATH11K_CORRELATE_TX_EVENT);
	if (status == ATH11K_CORRELATE_STATUS_RELEASE) {
		ath11k_dbg(ab, ATH11K_DBG_CFR,
			   "Releasing CFR data to user space");
		ath11k_cfr_rfs_write(ar, &lut->header,
				     sizeof(struct ath11k_csi_cfr_header),
				     lut->data, lut->data_len,
				     &end_magic, sizeof(u32));
		buff = lut->buff;
		ath11k_cfr_release_lut_entry(lut);

		ath11k_dbring_bufs_replenish(ar, &cfr->rx_ring, buff,
					     WMI_DIRECT_BUF_CFR);
	} else if (status == ATH11K_CORRELATE_STATUS_HOLD) {
		ath11k_dbg(ab, ATH11K_DBG_CFR,
			   "dbr event is not yet received holding buf\n");
	}

	spin_unlock_bh(&cfr->lut_lock);

	return 0;
}

/* Helper function to check whether the given peer mac address
 * is in unassociated peer pool or not.
 */
bool ath11k_cfr_peer_is_in_cfr_unassoc_pool(struct ath11k *ar, const u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	int i;

	if (!ar->cfr_enabled)
		return false;

	spin_lock_bh(&cfr->lock);
	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];
		if (!entry->is_valid)
			continue;

		if (ether_addr_equal(peer_mac, entry->peer_mac)) {
			spin_unlock_bh(&cfr->lock);
			return true;
		}
	}

	spin_unlock_bh(&cfr->lock);

	return false;
}

void ath11k_cfr_update_unassoc_pool_entry(struct ath11k *ar,
					  const u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	int i;

	spin_lock_bh(&cfr->lock);
	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];
		if (!entry->is_valid)
			continue;

		if (ether_addr_equal(peer_mac, entry->peer_mac) &&
		    entry->period == 0) {
			memset(entry->peer_mac, 0, ETH_ALEN);
			entry->is_valid = false;
			cfr->cfr_enabled_peer_cnt--;
			break;
		}
	}

	spin_unlock_bh(&cfr->lock);
}

void ath11k_cfr_decrement_peer_count(struct ath11k *ar,
				     struct ath11k_sta *arsta)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	spin_lock_bh(&cfr->lock);

	if (arsta->cfr_capture.cfr_enable)
		cfr->cfr_enabled_peer_cnt--;

	spin_unlock_bh(&cfr->lock);
}

static enum ath11k_wmi_cfr_capture_bw
ath11k_cfr_bw_to_fw_cfr_bw(enum ath11k_cfr_capture_bw bw)
{
	switch (bw) {
	case ATH11K_CFR_CAPTURE_BW_20:
		return WMI_PEER_CFR_CAPTURE_BW_20;
	case ATH11K_CFR_CAPTURE_BW_40:
		return WMI_PEER_CFR_CAPTURE_BW_40;
	case ATH11K_CFR_CAPTURE_BW_80:
		return WMI_PEER_CFR_CAPTURE_BW_80;
	default:
		return WMI_PEER_CFR_CAPTURE_BW_MAX;
	}
}

static enum ath11k_wmi_cfr_capture_method
ath11k_cfr_method_to_fw_cfr_method(enum ath11k_cfr_capture_method method)
{
	switch (method) {
	case ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME:
		return WMI_CFR_CAPTURE_METHOD_NULL_FRAME;
	case ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME_WITH_PHASE:
		return WMI_CFR_CAPTURE_METHOD_NULL_FRAME_WITH_PHASE;
	case ATH11K_CFR_CAPTURE_METHOD_PROBE_RESP:
		return WMI_CFR_CAPTURE_METHOD_PROBE_RESP;
	default:
		return WMI_CFR_CAPTURE_METHOD_MAX;
	}
}

int ath11k_cfr_send_peer_cfr_capture_cmd(struct ath11k *ar,
					 struct ath11k_sta *arsta,
					 struct ath11k_per_peer_cfr_capture *params,
					 const u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct wmi_peer_cfr_capture_conf_arg arg;
	enum ath11k_wmi_cfr_capture_bw bw;
	enum ath11k_wmi_cfr_capture_method method;
	int ret = 0;

	if (cfr->cfr_enabled_peer_cnt >= ATH11K_MAX_CFR_ENABLED_CLIENTS &&
	    !arsta->cfr_capture.cfr_enable) {
		ath11k_err(ar->ab, "CFR enable peer threshold reached %u\n",
			   cfr->cfr_enabled_peer_cnt);
		return -ENOSPC;
	}

	if (params->cfr_enable == arsta->cfr_capture.cfr_enable &&
	    params->cfr_period == arsta->cfr_capture.cfr_period &&
	    params->cfr_method == arsta->cfr_capture.cfr_method &&
	    params->cfr_bw == arsta->cfr_capture.cfr_bw)
		return ret;

	if (!params->cfr_enable && !arsta->cfr_capture.cfr_enable)
		return ret;

	bw = ath11k_cfr_bw_to_fw_cfr_bw(params->cfr_bw);
	if (bw >= WMI_PEER_CFR_CAPTURE_BW_MAX) {
		ath11k_warn(ar->ab, "FW doesn't support configured bw %d\n",
			    params->cfr_bw);
		return -EINVAL;
	}

	method = ath11k_cfr_method_to_fw_cfr_method(params->cfr_method);
	if (method >= WMI_CFR_CAPTURE_METHOD_MAX) {
		ath11k_warn(ar->ab, "FW doesn't support configured method %d\n",
			    params->cfr_method);
		return -EINVAL;
	}

	arg.request = params->cfr_enable;
	arg.periodicity = params->cfr_period;
	arg.bw = bw;
	arg.method = method;

	ret = ath11k_wmi_peer_set_cfr_capture_conf(ar, arsta->arvif->vdev_id,
						   peer_mac, &arg);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send cfr capture info: vdev_id %u peer %pM: %d\n",
			    arsta->arvif->vdev_id, peer_mac, ret);
		return ret;
	}

	spin_lock_bh(&cfr->lock);

	if (params->cfr_enable &&
	    params->cfr_enable != arsta->cfr_capture.cfr_enable)
		cfr->cfr_enabled_peer_cnt++;
	else if (!params->cfr_enable)
		cfr->cfr_enabled_peer_cnt--;

	spin_unlock_bh(&cfr->lock);

	arsta->cfr_capture.cfr_enable = params->cfr_enable;
	arsta->cfr_capture.cfr_period = params->cfr_period;
	arsta->cfr_capture.cfr_method = params->cfr_method;
	arsta->cfr_capture.cfr_bw = params->cfr_bw;

	return ret;
}

void ath11k_cfr_update_unassoc_pool(struct ath11k *ar,
				    struct ath11k_per_peer_cfr_capture *params,
				    u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	int available_idx = -1;
	int i;

	guard(spinlock_bh)(&cfr->lock);

	if (!params->cfr_enable) {
		for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
			entry = &cfr->unassoc_pool[i];
			if (ether_addr_equal(peer_mac, entry->peer_mac)) {
				memset(entry->peer_mac, 0, ETH_ALEN);
				entry->is_valid = false;
				cfr->cfr_enabled_peer_cnt--;
				break;
			}
		}
		return;
	}

	if (cfr->cfr_enabled_peer_cnt >= ATH11K_MAX_CFR_ENABLED_CLIENTS) {
		ath11k_info(ar->ab, "Max cfr peer threshold reached\n");
		return;
	}

	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];

		if (ether_addr_equal(peer_mac, entry->peer_mac)) {
			ath11k_info(ar->ab,
				    "peer entry already present updating params\n");
			entry->period = params->cfr_period;
			available_idx = -1;
			break;
		}

		if (available_idx < 0 && !entry->is_valid)
			available_idx = i;
	}

	if (available_idx >= 0) {
		entry = &cfr->unassoc_pool[available_idx];
		ether_addr_copy(entry->peer_mac, peer_mac);
		entry->period = params->cfr_period;
		entry->is_valid = true;
		cfr->cfr_enabled_peer_cnt++;
	}
}

static ssize_t ath11k_read_file_enable_cfr(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32] = {};
	size_t len;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf), "%d\n", ar->cfr_enabled);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath11k_write_file_enable_cfr(struct file *file,
					    const char __user *ubuf,
					    size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	u32 enable_cfr;
	int ret;

	if (kstrtouint_from_user(ubuf, count, 0, &enable_cfr))
		return -EINVAL;

	guard(mutex)(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON)
		return -ENETDOWN;

	if (enable_cfr > 1)
		return -EINVAL;

	if (ar->cfr_enabled == enable_cfr)
		return count;

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_PER_PEER_CFR_ENABLE,
					enable_cfr, ar->pdev->pdev_id);
	if (ret) {
		ath11k_warn(ar->ab,
			    "Failed to enable/disable per peer cfr %d\n", ret);
		return ret;
	}

	ar->cfr_enabled = enable_cfr;

	return count;
}

static const struct file_operations fops_enable_cfr = {
	.read = ath11k_read_file_enable_cfr,
	.write = ath11k_write_file_enable_cfr,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_write_file_cfr_unassoc(struct file *file,
					     const char __user *ubuf,
					     size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	char buf[64] = {};
	u8 peer_mac[6];
	u32 cfr_capture_enable;
	u32 cfr_capture_period;
	int available_idx = -1;
	int ret, i;

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);

	guard(mutex)(&ar->conf_mutex);
	guard(spinlock_bh)(&cfr->lock);

	if (ar->state != ATH11K_STATE_ON)
		return -ENETDOWN;

	if (!ar->cfr_enabled) {
		ath11k_err(ar->ab, "CFR is not enabled on this pdev %d\n",
			   ar->pdev_idx);
		return -EINVAL;
	}

	ret = sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx %u %u",
		     &peer_mac[0], &peer_mac[1], &peer_mac[2], &peer_mac[3],
		     &peer_mac[4], &peer_mac[5], &cfr_capture_enable,
		     &cfr_capture_period);

	if (ret < 1)
		return -EINVAL;

	if (cfr_capture_enable && ret != 8)
		return -EINVAL;

	if (!cfr_capture_enable) {
		for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
			entry = &cfr->unassoc_pool[i];
			if (ether_addr_equal(peer_mac, entry->peer_mac)) {
				memset(entry->peer_mac, 0, ETH_ALEN);
				entry->is_valid = false;
				cfr->cfr_enabled_peer_cnt--;
			}
		}

		return count;
	}

	if (cfr->cfr_enabled_peer_cnt >= ATH11K_MAX_CFR_ENABLED_CLIENTS) {
		ath11k_info(ar->ab, "Max cfr peer threshold reached\n");
		return count;
	}

	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];

		if (available_idx < 0 && !entry->is_valid)
			available_idx = i;

		if (ether_addr_equal(peer_mac, entry->peer_mac)) {
			ath11k_info(ar->ab,
				    "peer entry already present updating params\n");
			entry->period = cfr_capture_period;
			return count;
		}
	}

	if (available_idx >= 0) {
		entry = &cfr->unassoc_pool[available_idx];
		ether_addr_copy(entry->peer_mac, peer_mac);
		entry->period = cfr_capture_period;
		entry->is_valid = true;
		cfr->cfr_enabled_peer_cnt++;
	}

	return count;
}

static ssize_t ath11k_read_file_cfr_unassoc(struct file *file,
					    char __user *ubuf,
					    size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	char buf[512] = {};
	int len = 0, i;

	spin_lock_bh(&cfr->lock);

	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];
		if (entry->is_valid)
			len += scnprintf(buf + len, sizeof(buf) - len,
					 "peer: %pM period: %u\n",
					 entry->peer_mac, entry->period);
	}

	spin_unlock_bh(&cfr->lock);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct file_operations fops_configure_cfr_unassoc = {
	.write = ath11k_write_file_cfr_unassoc,
	.read = ath11k_read_file_cfr_unassoc,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static void ath11k_cfr_debug_unregister(struct ath11k *ar)
{
	debugfs_remove(ar->cfr.enable_cfr);
	ar->cfr.enable_cfr = NULL;
	debugfs_remove(ar->cfr.cfr_unassoc);
	ar->cfr.cfr_unassoc = NULL;

	relay_close(ar->cfr.rfs_cfr_capture);
	ar->cfr.rfs_cfr_capture = NULL;
}

static struct dentry *ath11k_cfr_create_buf_file_handler(const char *filename,
							 struct dentry *parent,
							 umode_t mode,
							 struct rchan_buf *buf,
							 int *is_global)
{
	struct dentry *buf_file;

	buf_file = debugfs_create_file(filename, mode, parent, buf,
				       &relay_file_operations);
	*is_global = 1;
	return buf_file;
}

static int ath11k_cfr_remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

static const struct rchan_callbacks rfs_cfr_capture_cb = {
	.create_buf_file = ath11k_cfr_create_buf_file_handler,
	.remove_buf_file = ath11k_cfr_remove_buf_file_handler,
};

static void ath11k_cfr_debug_register(struct ath11k *ar)
{
	ar->cfr.rfs_cfr_capture = relay_open("cfr_capture",
					     ar->debug.debugfs_pdev,
					     ar->ab->hw_params.cfr_stream_buf_size,
					     ar->ab->hw_params.cfr_num_stream_bufs,
					     &rfs_cfr_capture_cb, NULL);

	ar->cfr.enable_cfr = debugfs_create_file("enable_cfr", 0600,
						 ar->debug.debugfs_pdev, ar,
						 &fops_enable_cfr);

	ar->cfr.cfr_unassoc = debugfs_create_file("cfr_unassoc", 0600,
						  ar->debug.debugfs_pdev, ar,
						  &fops_configure_cfr_unassoc);
}

void ath11k_cfr_lut_update_paddr(struct ath11k *ar, dma_addr_t paddr,
				 u32 buf_id)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	if (cfr->lut)
		cfr->lut[buf_id].dbr_address = paddr;
}

void ath11k_cfr_update_phymode(struct ath11k *ar, enum wmi_phy_mode phymode)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	cfr->phymode = phymode;
}

static void ath11k_cfr_ring_free(struct ath11k *ar)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	ath11k_dbring_buf_cleanup(ar, &cfr->rx_ring);
	ath11k_dbring_srng_cleanup(ar, &cfr->rx_ring);
}

static int ath11k_cfr_ring_alloc(struct ath11k *ar,
				 struct ath11k_dbring_cap *db_cap)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	int ret;

	ret = ath11k_dbring_srng_setup(ar, &cfr->rx_ring,
				       ATH11K_CFR_NUM_RING_ENTRIES,
				       db_cap->min_elem);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring: %d\n", ret);
		return ret;
	}

	ath11k_dbring_set_cfg(ar, &cfr->rx_ring,
			      ATH11K_CFR_NUM_RESP_PER_EVENT,
			      ATH11K_CFR_EVENT_TIMEOUT_MS,
			      ath11k_cfr_process_data);

	ret = ath11k_dbring_buf_setup(ar, &cfr->rx_ring, db_cap);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring buffer: %d\n", ret);
		goto srng_cleanup;
	}

	ret = ath11k_dbring_wmi_cfg_setup(ar, &cfr->rx_ring, WMI_DIRECT_BUF_CFR);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring cfg: %d\n", ret);
		goto buffer_cleanup;
	}

	return 0;

buffer_cleanup:
	ath11k_dbring_buf_cleanup(ar, &cfr->rx_ring);
srng_cleanup:
	ath11k_dbring_srng_cleanup(ar, &cfr->rx_ring);
	return ret;
}

void ath11k_cfr_deinit(struct ath11k_base *ab)
{
	struct ath11k_cfr *cfr;
	struct ath11k *ar;
	int i;

	if (!test_bit(WMI_TLV_SERVICE_CFR_CAPTURE_SUPPORT, ab->wmi_ab.svc_map) ||
	    !ab->hw_params.cfr_support)
		return;

	for (i = 0; i <  ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		cfr = &ar->cfr;

		if (!cfr->enabled)
			continue;

		ath11k_cfr_debug_unregister(ar);
		ath11k_cfr_ring_free(ar);

		spin_lock_bh(&cfr->lut_lock);
		kfree(cfr->lut);
		cfr->lut = NULL;
		cfr->enabled = false;
		spin_unlock_bh(&cfr->lut_lock);
	}
}

int ath11k_cfr_init(struct ath11k_base *ab)
{
	struct ath11k_dbring_cap db_cap;
	struct ath11k_cfr *cfr;
	u32 num_lut_entries;
	struct ath11k *ar;
	int i, ret;

	if (!test_bit(WMI_TLV_SERVICE_CFR_CAPTURE_SUPPORT, ab->wmi_ab.svc_map) ||
	    !ab->hw_params.cfr_support)
		return 0;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		cfr = &ar->cfr;

		ret = ath11k_dbring_get_cap(ar->ab, ar->pdev_idx,
					    WMI_DIRECT_BUF_CFR, &db_cap);
		if (ret)
			continue;

		idr_init(&cfr->rx_ring.bufs_idr);
		spin_lock_init(&cfr->rx_ring.idr_lock);
		spin_lock_init(&cfr->lock);
		spin_lock_init(&cfr->lut_lock);

		num_lut_entries = min_t(u32, CFR_MAX_LUT_ENTRIES, db_cap.min_elem);
		cfr->lut = kcalloc(num_lut_entries, sizeof(*cfr->lut),
				   GFP_KERNEL);
		if (!cfr->lut) {
			ret = -ENOMEM;
			goto err;
		}

		ret = ath11k_cfr_ring_alloc(ar, &db_cap);
		if (ret) {
			ath11k_warn(ab, "failed to init cfr ring for pdev %d: %d\n",
				    i, ret);
			spin_lock_bh(&cfr->lut_lock);
			kfree(cfr->lut);
			cfr->lut = NULL;
			cfr->enabled = false;
			spin_unlock_bh(&cfr->lut_lock);
			goto err;
		}

		cfr->lut_num = num_lut_entries;
		cfr->enabled = true;

		ath11k_cfr_debug_register(ar);
	}

	return 0;

err:
	for (i = i - 1; i >= 0; i--) {
		ar = ab->pdevs[i].ar;
		cfr = &ar->cfr;

		if (!cfr->enabled)
			continue;

		ath11k_cfr_debug_unregister(ar);
		ath11k_cfr_ring_free(ar);

		spin_lock_bh(&cfr->lut_lock);
		kfree(cfr->lut);
		cfr->lut = NULL;
		cfr->enabled = false;
		spin_unlock_bh(&cfr->lut_lock);
	}
	return ret;
}
