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
	}

	return 0;

err:
	for (i = i - 1; i >= 0; i--) {
		ar = ab->pdevs[i].ar;
		cfr = &ar->cfr;

		if (!cfr->enabled)
			continue;

		ath11k_cfr_ring_free(ar);

		spin_lock_bh(&cfr->lut_lock);
		kfree(cfr->lut);
		cfr->lut = NULL;
		cfr->enabled = false;
		spin_unlock_bh(&cfr->lut_lock);
	}
	return ret;
}
