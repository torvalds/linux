// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <net/mac80211.h>
#include "core.h"
#include "mac.h"
#include "p2p.h"

static void ath12k_p2p_noa_ie_fill(u8 *data, size_t len,
				   const struct ath12k_wmi_p2p_noa_info *noa)
{
	struct ieee80211_p2p_noa_attr *noa_attr;
	u8 ctwindow = le32_get_bits(noa->noa_attr, WMI_P2P_NOA_INFO_CTWIN_TU);
	bool oppps = le32_get_bits(noa->noa_attr, WMI_P2P_NOA_INFO_OPP_PS);
	__le16 *noa_attr_len;
	u16 attr_len;
	u8 noa_descriptors = le32_get_bits(noa->noa_attr,
					   WMI_P2P_NOA_INFO_DESC_NUM);
	int i;

	/* P2P IE */
	data[0] = WLAN_EID_VENDOR_SPECIFIC;
	data[1] = len - 2;
	data[2] = (WLAN_OUI_WFA >> 16) & 0xff;
	data[3] = (WLAN_OUI_WFA >> 8) & 0xff;
	data[4] = (WLAN_OUI_WFA >> 0) & 0xff;
	data[5] = WLAN_OUI_TYPE_WFA_P2P;

	/* NOA ATTR */
	data[6] = IEEE80211_P2P_ATTR_ABSENCE_NOTICE;
	noa_attr_len = (__le16 *)&data[7]; /* 2 bytes */
	noa_attr = (struct ieee80211_p2p_noa_attr *)&data[9];

	noa_attr->index = le32_get_bits(noa->noa_attr,
					WMI_P2P_NOA_INFO_INDEX);
	noa_attr->oppps_ctwindow = ctwindow;
	if (oppps)
		noa_attr->oppps_ctwindow |= IEEE80211_P2P_OPPPS_ENABLE_BIT;

	for (i = 0; i < noa_descriptors; i++) {
		noa_attr->desc[i].count =
			__le32_to_cpu(noa->descriptors[i].type_count);
		noa_attr->desc[i].duration = noa->descriptors[i].duration;
		noa_attr->desc[i].interval = noa->descriptors[i].interval;
		noa_attr->desc[i].start_time = noa->descriptors[i].start_time;
	}

	attr_len = 2; /* index + oppps_ctwindow */
	attr_len += noa_descriptors * sizeof(struct ieee80211_p2p_noa_desc);
	*noa_attr_len = __cpu_to_le16(attr_len);
}

static size_t ath12k_p2p_noa_ie_len_compute(const struct ath12k_wmi_p2p_noa_info *noa)
{
	size_t len = 0;

	if (!(le32_get_bits(noa->noa_attr, WMI_P2P_NOA_INFO_DESC_NUM)) &&
	    !(le32_get_bits(noa->noa_attr, WMI_P2P_NOA_INFO_OPP_PS)))
		return 0;

	len += 1 + 1 + 4; /* EID + len + OUI */
	len += 1 + 2; /* noa attr + attr len */
	len += 1 + 1; /* index + oppps_ctwindow */
	len += le32_get_bits(noa->noa_attr, WMI_P2P_NOA_INFO_DESC_NUM) *
			sizeof(struct ieee80211_p2p_noa_desc);

	return len;
}

static void ath12k_p2p_noa_ie_assign(struct ath12k_vif *arvif, void *ie,
				     size_t len)
{
	struct ath12k *ar = arvif->ar;

	lockdep_assert_held(&ar->data_lock);

	kfree(arvif->u.ap.noa_data);

	arvif->u.ap.noa_data = ie;
	arvif->u.ap.noa_len = len;
}

static void __ath12k_p2p_noa_update(struct ath12k_vif *arvif,
				    const struct ath12k_wmi_p2p_noa_info *noa)
{
	struct ath12k *ar = arvif->ar;
	void *ie;
	size_t len;

	lockdep_assert_held(&ar->data_lock);

	ath12k_p2p_noa_ie_assign(arvif, NULL, 0);

	len = ath12k_p2p_noa_ie_len_compute(noa);
	if (!len)
		return;

	ie = kmalloc(len, GFP_ATOMIC);
	if (!ie)
		return;

	ath12k_p2p_noa_ie_fill(ie, len, noa);
	ath12k_p2p_noa_ie_assign(arvif, ie, len);
}

void ath12k_p2p_noa_update(struct ath12k_vif *arvif,
			   const struct ath12k_wmi_p2p_noa_info *noa)
{
	struct ath12k *ar = arvif->ar;

	spin_lock_bh(&ar->data_lock);
	__ath12k_p2p_noa_update(arvif, noa);
	spin_unlock_bh(&ar->data_lock);
}

static void ath12k_p2p_noa_update_vdev_iter(void *data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct ath12k_vif *arvif = ath12k_vif_to_arvif(vif);
	struct ath12k_p2p_noa_arg *arg = data;

	if (arvif->ar != arg->ar || arvif->vdev_id != arg->vdev_id)
		return;

	ath12k_p2p_noa_update(arvif, arg->noa);
}

void ath12k_p2p_noa_update_by_vdev_id(struct ath12k *ar, u32 vdev_id,
				      const struct ath12k_wmi_p2p_noa_info *noa)
{
	struct ath12k_p2p_noa_arg arg = {
		.vdev_id = vdev_id,
		.ar = ar,
		.noa = noa,
	};

	ieee80211_iterate_active_interfaces_atomic(ath12k_ar_to_hw(ar),
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath12k_p2p_noa_update_vdev_iter,
						   &arg);
}
