// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/relay.h>
#include "core.h"
#include "debug.h"

static int ath11k_cfr_process_data(struct ath11k *ar,
				   struct ath11k_dbring_data *param)
{
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
}

static void ath11k_cfr_debug_register(struct ath11k *ar)
{
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
