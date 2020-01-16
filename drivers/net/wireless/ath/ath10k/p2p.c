// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
 */

#include "core.h"
#include "wmi.h"
#include "mac.h"
#include "p2p.h"

static void ath10k_p2p_yesa_ie_fill(u8 *data, size_t len,
				   const struct wmi_p2p_yesa_info *yesa)
{
	struct ieee80211_p2p_yesa_attr *yesa_attr;
	u8  ctwindow_oppps = yesa->ctwindow_oppps;
	u8 ctwindow = ctwindow_oppps >> WMI_P2P_OPPPS_CTWINDOW_OFFSET;
	bool oppps = !!(ctwindow_oppps & WMI_P2P_OPPPS_ENABLE_BIT);
	__le16 *yesa_attr_len;
	u16 attr_len;
	u8 yesa_descriptors = yesa->num_descriptors;
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
	yesa_attr_len = (__le16 *)&data[7]; /* 2 bytes */
	yesa_attr = (struct ieee80211_p2p_yesa_attr *)&data[9];

	yesa_attr->index = yesa->index;
	yesa_attr->oppps_ctwindow = ctwindow;
	if (oppps)
		yesa_attr->oppps_ctwindow |= IEEE80211_P2P_OPPPS_ENABLE_BIT;

	for (i = 0; i < yesa_descriptors; i++) {
		yesa_attr->desc[i].count =
			__le32_to_cpu(yesa->descriptors[i].type_count);
		yesa_attr->desc[i].duration = yesa->descriptors[i].duration;
		yesa_attr->desc[i].interval = yesa->descriptors[i].interval;
		yesa_attr->desc[i].start_time = yesa->descriptors[i].start_time;
	}

	attr_len = 2; /* index + oppps_ctwindow */
	attr_len += yesa_descriptors * sizeof(struct ieee80211_p2p_yesa_desc);
	*yesa_attr_len = __cpu_to_le16(attr_len);
}

static size_t ath10k_p2p_yesa_ie_len_compute(const struct wmi_p2p_yesa_info *yesa)
{
	size_t len = 0;

	if (!yesa->num_descriptors &&
	    !(yesa->ctwindow_oppps & WMI_P2P_OPPPS_ENABLE_BIT))
		return 0;

	len += 1 + 1 + 4; /* EID + len + OUI */
	len += 1 + 2; /* yesa attr + attr len */
	len += 1 + 1; /* index + oppps_ctwindow */
	len += yesa->num_descriptors * sizeof(struct ieee80211_p2p_yesa_desc);

	return len;
}

static void ath10k_p2p_yesa_ie_assign(struct ath10k_vif *arvif, void *ie,
				     size_t len)
{
	struct ath10k *ar = arvif->ar;

	lockdep_assert_held(&ar->data_lock);

	kfree(arvif->u.ap.yesa_data);

	arvif->u.ap.yesa_data = ie;
	arvif->u.ap.yesa_len = len;
}

static void __ath10k_p2p_yesa_update(struct ath10k_vif *arvif,
				    const struct wmi_p2p_yesa_info *yesa)
{
	struct ath10k *ar = arvif->ar;
	void *ie;
	size_t len;

	lockdep_assert_held(&ar->data_lock);

	ath10k_p2p_yesa_ie_assign(arvif, NULL, 0);

	len = ath10k_p2p_yesa_ie_len_compute(yesa);
	if (!len)
		return;

	ie = kmalloc(len, GFP_ATOMIC);
	if (!ie)
		return;

	ath10k_p2p_yesa_ie_fill(ie, len, yesa);
	ath10k_p2p_yesa_ie_assign(arvif, ie, len);
}

void ath10k_p2p_yesa_update(struct ath10k_vif *arvif,
			   const struct wmi_p2p_yesa_info *yesa)
{
	struct ath10k *ar = arvif->ar;

	spin_lock_bh(&ar->data_lock);
	__ath10k_p2p_yesa_update(arvif, yesa);
	spin_unlock_bh(&ar->data_lock);
}

struct ath10k_p2p_yesa_arg {
	u32 vdev_id;
	const struct wmi_p2p_yesa_info *yesa;
};

static void ath10k_p2p_yesa_update_vdev_iter(void *data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct ath10k_vif *arvif = (void *)vif->drv_priv;
	struct ath10k_p2p_yesa_arg *arg = data;

	if (arvif->vdev_id != arg->vdev_id)
		return;

	ath10k_p2p_yesa_update(arvif, arg->yesa);
}

void ath10k_p2p_yesa_update_by_vdev_id(struct ath10k *ar, u32 vdev_id,
				      const struct wmi_p2p_yesa_info *yesa)
{
	struct ath10k_p2p_yesa_arg arg = {
		.vdev_id = vdev_id,
		.yesa = yesa,
	};

	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath10k_p2p_yesa_update_vdev_iter,
						   &arg);
}
