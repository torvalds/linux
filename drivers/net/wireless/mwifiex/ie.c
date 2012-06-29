/*
 * Marvell Wireless LAN device driver: management IE handling- setting and
 * deleting IE.
 *
 * Copyright (C) 2012, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "main.h"

/* This function checks if current IE index is used by any on other interface.
 * Return: -1: yes, current IE index is used by someone else.
 *          0: no, current IE index is NOT used by other interface.
 */
static int
mwifiex_ie_index_used_by_other_intf(struct mwifiex_private *priv, u16 idx)
{
	int i;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_ie *ie;

	for (i = 0; i < adapter->priv_num; i++) {
		if (adapter->priv[i] != priv) {
			ie = &adapter->priv[i]->mgmt_ie[idx];
			if (ie->mgmt_subtype_mask && ie->ie_length)
				return -1;
		}
	}

	return 0;
}

/* Get unused IE index. This index will be used for setting new IE */
static int
mwifiex_ie_get_autoidx(struct mwifiex_private *priv, u16 subtype_mask,
		       struct mwifiex_ie *ie, u16 *index)
{
	u16 mask, len, i;

	for (i = 0; i < priv->adapter->max_mgmt_ie_index; i++) {
		mask = le16_to_cpu(priv->mgmt_ie[i].mgmt_subtype_mask);
		len = le16_to_cpu(priv->mgmt_ie[i].ie_length) +
		      le16_to_cpu(ie->ie_length);

		if (mask == MWIFIEX_AUTO_IDX_MASK)
			continue;

		if (mask == subtype_mask) {
			if (len > IEEE_MAX_IE_SIZE)
				continue;

			*index = i;
			return 0;
		}

		if (!priv->mgmt_ie[i].ie_length) {
			if (mwifiex_ie_index_used_by_other_intf(priv, i))
				continue;

			*index = i;
			return 0;
		}
	}

	return -1;
}

/* This function prepares IE data buffer for command to be sent to FW */
static int
mwifiex_update_autoindex_ies(struct mwifiex_private *priv,
			     struct mwifiex_ie_list *ie_list)
{
	u16 travel_len, index, mask;
	s16 input_len;
	struct mwifiex_ie *ie;
	u8 *tmp;

	input_len = le16_to_cpu(ie_list->len);
	travel_len = sizeof(struct host_cmd_tlv);

	ie_list->len = 0;

	while (input_len > 0) {
		ie = (struct mwifiex_ie *)(((u8 *)ie_list) + travel_len);
		input_len -= le16_to_cpu(ie->ie_length) + MWIFIEX_IE_HDR_SIZE;
		travel_len += le16_to_cpu(ie->ie_length) + MWIFIEX_IE_HDR_SIZE;

		index = le16_to_cpu(ie->ie_index);
		mask = le16_to_cpu(ie->mgmt_subtype_mask);

		if (index == MWIFIEX_AUTO_IDX_MASK) {
			/* automatic addition */
			if (mwifiex_ie_get_autoidx(priv, mask, ie, &index))
				return -1;
			if (index == MWIFIEX_AUTO_IDX_MASK)
				return -1;

			tmp = (u8 *)&priv->mgmt_ie[index].ie_buffer;
			tmp += le16_to_cpu(priv->mgmt_ie[index].ie_length);
			memcpy(tmp, &ie->ie_buffer, le16_to_cpu(ie->ie_length));
			le16_add_cpu(&priv->mgmt_ie[index].ie_length,
				     le16_to_cpu(ie->ie_length));
			priv->mgmt_ie[index].ie_index = cpu_to_le16(index);
			priv->mgmt_ie[index].mgmt_subtype_mask =
							cpu_to_le16(mask);

			ie->ie_index = cpu_to_le16(index);
			ie->ie_length = priv->mgmt_ie[index].ie_length;
			memcpy(&ie->ie_buffer, &priv->mgmt_ie[index].ie_buffer,
			       le16_to_cpu(priv->mgmt_ie[index].ie_length));
		} else {
			if (mask != MWIFIEX_DELETE_MASK)
				return -1;
			/*
			 * Check if this index is being used on any
			 * other interface.
			 */
			if (mwifiex_ie_index_used_by_other_intf(priv, index))
				return -1;

			ie->ie_length = 0;
			memcpy(&priv->mgmt_ie[index], ie,
			       sizeof(struct mwifiex_ie));
		}

		le16_add_cpu(&ie_list->len,
			     le16_to_cpu(priv->mgmt_ie[index].ie_length) +
			     MWIFIEX_IE_HDR_SIZE);
	}

	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP)
		return mwifiex_send_cmd_async(priv, HostCmd_CMD_UAP_SYS_CONFIG,
					      HostCmd_ACT_GEN_SET,
					      UAP_CUSTOM_IE_I, ie_list);

	return 0;
}

/* Copy individual custom IEs for beacon, probe response and assoc response
 * and prepare single structure for IE setting.
 * This function also updates allocated IE indices from driver.
 */
static int
mwifiex_update_uap_custom_ie(struct mwifiex_private *priv,
			     struct mwifiex_ie *beacon_ie, u16 *beacon_idx,
			     struct mwifiex_ie *pr_ie, u16 *probe_idx,
			     struct mwifiex_ie *ar_ie, u16 *assoc_idx)
{
	struct mwifiex_ie_list *ap_custom_ie;
	u8 *pos;
	u16 len;
	int ret;

	ap_custom_ie = kzalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
	if (!ap_custom_ie)
		return -ENOMEM;

	ap_custom_ie->type = cpu_to_le16(TLV_TYPE_MGMT_IE);
	pos = (u8 *)ap_custom_ie->ie_list;

	if (beacon_ie) {
		len = sizeof(struct mwifiex_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(beacon_ie->ie_length);
		memcpy(pos, beacon_ie, len);
		pos += len;
		le16_add_cpu(&ap_custom_ie->len, len);
	}
	if (pr_ie) {
		len = sizeof(struct mwifiex_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(pr_ie->ie_length);
		memcpy(pos, pr_ie, len);
		pos += len;
		le16_add_cpu(&ap_custom_ie->len, len);
	}
	if (ar_ie) {
		len = sizeof(struct mwifiex_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(ar_ie->ie_length);
		memcpy(pos, ar_ie, len);
		pos += len;
		le16_add_cpu(&ap_custom_ie->len, len);
	}

	ret = mwifiex_update_autoindex_ies(priv, ap_custom_ie);

	pos = (u8 *)(&ap_custom_ie->ie_list[0].ie_index);
	if (beacon_ie && *beacon_idx == MWIFIEX_AUTO_IDX_MASK) {
		/* save beacon ie index after auto-indexing */
		*beacon_idx = le16_to_cpu(ap_custom_ie->ie_list[0].ie_index);
		len = sizeof(*beacon_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(beacon_ie->ie_length);
		pos += len;
	}
	if (pr_ie && le16_to_cpu(pr_ie->ie_index) == MWIFIEX_AUTO_IDX_MASK) {
		/* save probe resp ie index after auto-indexing */
		*probe_idx = *((u16 *)pos);
		len = sizeof(*pr_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(pr_ie->ie_length);
		pos += len;
	}
	if (ar_ie && le16_to_cpu(ar_ie->ie_index) == MWIFIEX_AUTO_IDX_MASK)
		/* save assoc resp ie index after auto-indexing */
		*assoc_idx = *((u16 *)pos);

	kfree(ap_custom_ie);
	return ret;
}

/* This function parses different IEs- Tail IEs, beacon IEs, probe response IEs,
 * association response IEs from cfg80211_ap_settings function and sets these IE
 * to FW.
 */
int mwifiex_set_mgmt_ies(struct mwifiex_private *priv,
			 struct cfg80211_ap_settings *params)
{
	struct mwifiex_ie *beacon_ie = NULL, *pr_ie = NULL;
	struct mwifiex_ie *ar_ie = NULL, *gen_ie = NULL;
	struct ieee_types_header *rsn_ie = NULL, *wpa_ie = NULL;
	u16 beacon_idx = MWIFIEX_AUTO_IDX_MASK, pr_idx = MWIFIEX_AUTO_IDX_MASK;
	u16 ar_idx = MWIFIEX_AUTO_IDX_MASK, rsn_idx = MWIFIEX_AUTO_IDX_MASK;
	u16 mask, ie_len = 0;
	const u8 *vendor_ie;
	int ret = 0;

	if (params->beacon.tail && params->beacon.tail_len) {
		gen_ie = kzalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!gen_ie)
			return -ENOMEM;
		gen_ie->ie_index = cpu_to_le16(rsn_idx);
		mask = MGMT_MASK_BEACON | MGMT_MASK_PROBE_RESP |
		       MGMT_MASK_ASSOC_RESP;
		gen_ie->mgmt_subtype_mask = cpu_to_le16(mask);

		rsn_ie = (void *)cfg80211_find_ie(WLAN_EID_RSN,
						  params->beacon.tail,
						  params->beacon.tail_len);
		if (rsn_ie) {
			memcpy(gen_ie->ie_buffer, rsn_ie, rsn_ie->len + 2);
			ie_len = rsn_ie->len + 2;
			gen_ie->ie_length = cpu_to_le16(ie_len);
		}

		vendor_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						    WLAN_OUI_TYPE_MICROSOFT_WPA,
						    params->beacon.tail,
						    params->beacon.tail_len);
		if (vendor_ie) {
			wpa_ie = (struct ieee_types_header *)vendor_ie;
			memcpy(gen_ie->ie_buffer + ie_len,
			       wpa_ie, wpa_ie->len + 2);
			ie_len += wpa_ie->len + 2;
			gen_ie->ie_length = cpu_to_le16(ie_len);
		}

		if (rsn_ie || wpa_ie) {
			if (mwifiex_update_uap_custom_ie(priv, gen_ie, &rsn_idx,
							 NULL, NULL,
							 NULL, NULL)) {
				ret = -1;
				goto done;
			}

			priv->rsn_idx = rsn_idx;
		}
	}

	if (params->beacon.beacon_ies && params->beacon.beacon_ies_len) {
		beacon_ie = kmalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!beacon_ie) {
			ret = -ENOMEM;
			goto done;
		}

		beacon_ie->ie_index = cpu_to_le16(beacon_idx);
		beacon_ie->mgmt_subtype_mask = cpu_to_le16(MGMT_MASK_BEACON);
		beacon_ie->ie_length =
				cpu_to_le16(params->beacon.beacon_ies_len);
		memcpy(beacon_ie->ie_buffer, params->beacon.beacon_ies,
		       params->beacon.beacon_ies_len);
	}

	if (params->beacon.proberesp_ies && params->beacon.proberesp_ies_len) {
		pr_ie = kmalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!pr_ie) {
			ret = -ENOMEM;
			goto done;
		}

		pr_ie->ie_index = cpu_to_le16(pr_idx);
		pr_ie->mgmt_subtype_mask = cpu_to_le16(MGMT_MASK_PROBE_RESP);
		pr_ie->ie_length =
				cpu_to_le16(params->beacon.proberesp_ies_len);
		memcpy(pr_ie->ie_buffer, params->beacon.proberesp_ies,
		       params->beacon.proberesp_ies_len);
	}

	if (params->beacon.assocresp_ies && params->beacon.assocresp_ies_len) {
		ar_ie = kmalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!ar_ie) {
			ret = -ENOMEM;
			goto done;
		}

		ar_ie->ie_index = cpu_to_le16(ar_idx);
		mask = MGMT_MASK_ASSOC_RESP | MGMT_MASK_REASSOC_RESP;
		ar_ie->mgmt_subtype_mask = cpu_to_le16(mask);
		ar_ie->ie_length =
				cpu_to_le16(params->beacon.assocresp_ies_len);
		memcpy(ar_ie->ie_buffer, params->beacon.assocresp_ies,
		       params->beacon.assocresp_ies_len);
	}

	if (beacon_ie || pr_ie || ar_ie) {
		ret = mwifiex_update_uap_custom_ie(priv, beacon_ie,
						   &beacon_idx, pr_ie,
						   &pr_idx, ar_ie, &ar_idx);
		if (ret)
			goto done;
	}

	priv->beacon_idx = beacon_idx;
	priv->proberesp_idx = pr_idx;
	priv->assocresp_idx = ar_idx;

done:
	kfree(beacon_ie);
	kfree(pr_ie);
	kfree(ar_ie);
	kfree(gen_ie);

	return ret;
}

/* This function removes management IE set */
int mwifiex_del_mgmt_ies(struct mwifiex_private *priv)
{
	struct mwifiex_ie *beacon_ie = NULL, *pr_ie = NULL;
	struct mwifiex_ie *ar_ie = NULL, *rsn_ie = NULL;
	int ret = 0;

	if (priv->rsn_idx != MWIFIEX_AUTO_IDX_MASK) {
		rsn_ie = kmalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!rsn_ie)
			return -ENOMEM;

		rsn_ie->ie_index = cpu_to_le16(priv->rsn_idx);
		rsn_ie->mgmt_subtype_mask = cpu_to_le16(MWIFIEX_DELETE_MASK);
		rsn_ie->ie_length = 0;
		if (mwifiex_update_uap_custom_ie(priv, rsn_ie, &priv->rsn_idx,
						 NULL, &priv->proberesp_idx,
						 NULL, &priv->assocresp_idx)) {
			ret = -1;
			goto done;
		}

		priv->rsn_idx = MWIFIEX_AUTO_IDX_MASK;
	}

	if (priv->beacon_idx != MWIFIEX_AUTO_IDX_MASK) {
		beacon_ie = kmalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!beacon_ie) {
			ret = -ENOMEM;
			goto done;
		}
		beacon_ie->ie_index = cpu_to_le16(priv->beacon_idx);
		beacon_ie->mgmt_subtype_mask = cpu_to_le16(MWIFIEX_DELETE_MASK);
		beacon_ie->ie_length = 0;
	}
	if (priv->proberesp_idx != MWIFIEX_AUTO_IDX_MASK) {
		pr_ie = kmalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!pr_ie) {
			ret = -ENOMEM;
			goto done;
		}
		pr_ie->ie_index = cpu_to_le16(priv->proberesp_idx);
		pr_ie->mgmt_subtype_mask = cpu_to_le16(MWIFIEX_DELETE_MASK);
		pr_ie->ie_length = 0;
	}
	if (priv->assocresp_idx != MWIFIEX_AUTO_IDX_MASK) {
		ar_ie = kmalloc(sizeof(struct mwifiex_ie), GFP_KERNEL);
		if (!ar_ie) {
			ret = -ENOMEM;
			goto done;
		}
		ar_ie->ie_index = cpu_to_le16(priv->assocresp_idx);
		ar_ie->mgmt_subtype_mask = cpu_to_le16(MWIFIEX_DELETE_MASK);
		ar_ie->ie_length = 0;
	}

	if (beacon_ie || pr_ie || ar_ie)
		ret = mwifiex_update_uap_custom_ie(priv,
						   beacon_ie, &priv->beacon_idx,
						   pr_ie, &priv->proberesp_idx,
						   ar_ie, &priv->assocresp_idx);

done:
	kfree(beacon_ie);
	kfree(pr_ie);
	kfree(ar_ie);
	kfree(rsn_ie);

	return ret;
}
