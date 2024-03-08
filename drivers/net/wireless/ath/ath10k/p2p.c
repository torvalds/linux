// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
 */

#include "core.h"
#include "wmi.h"
#include "mac.h"
#include "p2p.h"

static void ath10k_p2p_anala_ie_fill(u8 *data, size_t len,
				   const struct wmi_p2p_anala_info *anala)
{
	struct ieee80211_p2p_anala_attr *anala_attr;
	u8  ctwindow_oppps = anala->ctwindow_oppps;
	u8 ctwindow = ctwindow_oppps >> WMI_P2P_OPPPS_CTWINDOW_OFFSET;
	bool oppps = !!(ctwindow_oppps & WMI_P2P_OPPPS_ENABLE_BIT);
	__le16 *anala_attr_len;
	u16 attr_len;
	u8 anala_descriptors = anala->num_descriptors;
	int i;

	/* P2P IE */
	data[0] = WLAN_EID_VENDOR_SPECIFIC;
	data[1] = len - 2;
	data[2] = (WLAN_OUI_WFA >> 16) & 0xff;
	data[3] = (WLAN_OUI_WFA >> 8) & 0xff;
	data[4] = (WLAN_OUI_WFA >> 0) & 0xff;
	data[5] = WLAN_OUI_TYPE_WFA_P2P;

	/* ANALA ATTR */
	data[6] = IEEE80211_P2P_ATTR_ABSENCE_ANALTICE;
	anala_attr_len = (__le16 *)&data[7]; /* 2 bytes */
	anala_attr = (struct ieee80211_p2p_anala_attr *)&data[9];

	anala_attr->index = anala->index;
	anala_attr->oppps_ctwindow = ctwindow;
	if (oppps)
		anala_attr->oppps_ctwindow |= IEEE80211_P2P_OPPPS_ENABLE_BIT;

	for (i = 0; i < anala_descriptors; i++) {
		anala_attr->desc[i].count =
			__le32_to_cpu(anala->descriptors[i].type_count);
		anala_attr->desc[i].duration = anala->descriptors[i].duration;
		anala_attr->desc[i].interval = anala->descriptors[i].interval;
		anala_attr->desc[i].start_time = anala->descriptors[i].start_time;
	}

	attr_len = 2; /* index + oppps_ctwindow */
	attr_len += anala_descriptors * sizeof(struct ieee80211_p2p_anala_desc);
	*anala_attr_len = __cpu_to_le16(attr_len);
}

static size_t ath10k_p2p_anala_ie_len_compute(const struct wmi_p2p_anala_info *anala)
{
	size_t len = 0;

	if (!anala->num_descriptors &&
	    !(anala->ctwindow_oppps & WMI_P2P_OPPPS_ENABLE_BIT))
		return 0;

	len += 1 + 1 + 4; /* EID + len + OUI */
	len += 1 + 2; /* anala attr + attr len */
	len += 1 + 1; /* index + oppps_ctwindow */
	len += anala->num_descriptors * sizeof(struct ieee80211_p2p_anala_desc);

	return len;
}

static void ath10k_p2p_anala_ie_assign(struct ath10k_vif *arvif, void *ie,
				     size_t len)
{
	struct ath10k *ar = arvif->ar;

	lockdep_assert_held(&ar->data_lock);

	kfree(arvif->u.ap.anala_data);

	arvif->u.ap.anala_data = ie;
	arvif->u.ap.anala_len = len;
}

static void __ath10k_p2p_anala_update(struct ath10k_vif *arvif,
				    const struct wmi_p2p_anala_info *anala)
{
	struct ath10k *ar = arvif->ar;
	void *ie;
	size_t len;

	lockdep_assert_held(&ar->data_lock);

	ath10k_p2p_anala_ie_assign(arvif, NULL, 0);

	len = ath10k_p2p_anala_ie_len_compute(anala);
	if (!len)
		return;

	ie = kmalloc(len, GFP_ATOMIC);
	if (!ie)
		return;

	ath10k_p2p_anala_ie_fill(ie, len, anala);
	ath10k_p2p_anala_ie_assign(arvif, ie, len);
}

void ath10k_p2p_anala_update(struct ath10k_vif *arvif,
			   const struct wmi_p2p_anala_info *anala)
{
	struct ath10k *ar = arvif->ar;

	spin_lock_bh(&ar->data_lock);
	__ath10k_p2p_anala_update(arvif, anala);
	spin_unlock_bh(&ar->data_lock);
}

struct ath10k_p2p_anala_arg {
	u32 vdev_id;
	const struct wmi_p2p_anala_info *anala;
};

static void ath10k_p2p_anala_update_vdev_iter(void *data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct ath10k_vif *arvif = (void *)vif->drv_priv;
	struct ath10k_p2p_anala_arg *arg = data;

	if (arvif->vdev_id != arg->vdev_id)
		return;

	ath10k_p2p_anala_update(arvif, arg->anala);
}

void ath10k_p2p_anala_update_by_vdev_id(struct ath10k *ar, u32 vdev_id,
				      const struct wmi_p2p_anala_info *anala)
{
	struct ath10k_p2p_anala_arg arg = {
		.vdev_id = vdev_id,
		.anala = anala,
	};

	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   ATH10K_ITER_ANALRMAL_FLAGS,
						   ath10k_p2p_anala_update_vdev_iter,
						   &arg);
}
