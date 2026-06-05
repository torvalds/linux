// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: management element handling - set/delete elements.
 *
 * Copyright 2011-2024 NXP
 */

#include "main.h"
#include "cmdevt.h"

/* Return true if the IE index is used by another interface. */
static bool
nxpwifi_ie_index_used_by_other_intf(struct nxpwifi_private *priv, u16 idx)
{
	int i;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_ie *ie;

	for (i = 0; i < adapter->priv_num; i++) {
		if (adapter->priv[i] != priv) {
			ie = &adapter->priv[i]->mgmt_ie[idx];
			if (ie->mgmt_subtype_mask && ie->ie_length)
				return true;
		}
	}

	return false;
}

/* Pick an unused IE index for a new element. */
static int
nxpwifi_ie_get_autoidx(struct nxpwifi_private *priv, u16 subtype_mask,
		       struct nxpwifi_ie *ie, u16 *index)
{
	u16 mask, len, i;

	for (i = 0; i < priv->adapter->max_mgmt_ie_index; i++) {
		mask = le16_to_cpu(priv->mgmt_ie[i].mgmt_subtype_mask);
		len = le16_to_cpu(ie->ie_length);

		if (mask == NXPWIFI_AUTO_IDX_MASK)
			continue;

		if (mask == subtype_mask) {
			if (len > IEEE_MAX_IE_SIZE)
				continue;

			*index = i;
			return 0;
		}

		if (!priv->mgmt_ie[i].ie_length) {
			if (nxpwifi_ie_index_used_by_other_intf(priv, i))
				continue;

			*index = i;
			return 0;
		}
	}

	return -ENOENT;
}

/* Build IE list and resolve AUTO index before sending to FW. */
static int
nxpwifi_update_autoindex_ies(struct nxpwifi_private *priv,
			     struct nxpwifi_ie_list *ie_list)
{
	u16 travel_len, index, mask;
	s16 input_len, tlv_len;
	struct nxpwifi_ie *ie;
	u8 *tmp;

	input_len = le16_to_cpu(ie_list->len);
	travel_len = sizeof(struct nxpwifi_ie_types_header);

	ie_list->len = 0;

	while (input_len >= sizeof(struct nxpwifi_ie_types_header)) {
		ie = (struct nxpwifi_ie *)(((u8 *)ie_list) + travel_len);
		tlv_len = le16_to_cpu(ie->ie_length);
		travel_len += tlv_len + NXPWIFI_IE_HDR_SIZE;

		if (input_len < tlv_len + NXPWIFI_IE_HDR_SIZE)
			return -EINVAL;
		index = le16_to_cpu(ie->ie_index);
		mask = le16_to_cpu(ie->mgmt_subtype_mask);

		if (index == NXPWIFI_AUTO_IDX_MASK) {
			/* automatic addition */
			if (nxpwifi_ie_get_autoidx(priv, mask, ie, &index))
				return -ENOENT;
			if (index == NXPWIFI_AUTO_IDX_MASK)
				return -EINVAL;

			tmp = (u8 *)&priv->mgmt_ie[index].ie_buffer;
			memcpy(tmp, &ie->ie_buffer, le16_to_cpu(ie->ie_length));
			priv->mgmt_ie[index].ie_length = ie->ie_length;
			priv->mgmt_ie[index].ie_index = cpu_to_le16(index);
			priv->mgmt_ie[index].mgmt_subtype_mask =
							cpu_to_le16(mask);

			ie->ie_index = cpu_to_le16(index);
		} else {
			if (mask != NXPWIFI_DELETE_MASK)
				return -EINVAL;
			/*
			 * Check if this index is being used on any
			 * other interface.
			 */
			if (nxpwifi_ie_index_used_by_other_intf(priv, index))
				return -EPERM;

			ie->ie_length = 0;
			memcpy(&priv->mgmt_ie[index], ie,
			       sizeof(struct nxpwifi_ie));
		}

		le16_unaligned_add_cpu
		(&ie_list->len,
		 le16_to_cpu(priv->mgmt_ie[index].ie_length) +
		 NXPWIFI_IE_HDR_SIZE);
		input_len -= tlv_len + NXPWIFI_IE_HDR_SIZE;
	}

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP)
		return nxpwifi_send_cmd(priv, HOST_CMD_UAP_SYS_CONFIG,
					HOST_ACT_GEN_SET,
					UAP_CUSTOM_IE_I, ie_list, true);

	return 0;
}

/* Pack beacon/probe/assoc IEs into one list and update auto-assigned indices. */
static int
nxpwifi_update_uap_custom_ie(struct nxpwifi_private *priv,
			     struct nxpwifi_ie *beacon_ie, u16 *beacon_idx,
			     struct nxpwifi_ie *pr_ie, u16 *probe_idx,
			     struct nxpwifi_ie *ar_ie, u16 *assoc_idx)
{
	struct nxpwifi_ie_list *ap_custom_ie;
	u8 *pos;
	u16 len;
	int ret;

	ap_custom_ie = kzalloc_obj(*ap_custom_ie, GFP_KERNEL);
	if (!ap_custom_ie)
		return -ENOMEM;

	ap_custom_ie->type = cpu_to_le16(TLV_TYPE_MGMT_IE);
	pos = (u8 *)ap_custom_ie->ie_list;

	if (beacon_ie) {
		len = sizeof(struct nxpwifi_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(beacon_ie->ie_length);
		memcpy(pos, beacon_ie, len);
		pos += len;
		le16_unaligned_add_cpu(&ap_custom_ie->len, len);
	}
	if (pr_ie) {
		len = sizeof(struct nxpwifi_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(pr_ie->ie_length);
		memcpy(pos, pr_ie, len);
		pos += len;
		le16_unaligned_add_cpu(&ap_custom_ie->len, len);
	}
	if (ar_ie) {
		len = sizeof(struct nxpwifi_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(ar_ie->ie_length);
		memcpy(pos, ar_ie, len);
		pos += len;
		le16_unaligned_add_cpu(&ap_custom_ie->len, len);
	}

	ret = nxpwifi_update_autoindex_ies(priv, ap_custom_ie);

	pos = (u8 *)(&ap_custom_ie->ie_list[0].ie_index);
	if (beacon_ie && *beacon_idx == NXPWIFI_AUTO_IDX_MASK) {
		/* save beacon element index after auto-indexing */
		*beacon_idx = le16_to_cpu(ap_custom_ie->ie_list[0].ie_index);
		len = sizeof(*beacon_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(beacon_ie->ie_length);
		pos += len;
	}
	if (pr_ie && le16_to_cpu(pr_ie->ie_index) == NXPWIFI_AUTO_IDX_MASK) {
		/* save probe resp element index after auto-indexing */
		*probe_idx = *((u16 *)pos);
		len = sizeof(*pr_ie) - IEEE_MAX_IE_SIZE +
		      le16_to_cpu(pr_ie->ie_length);
		pos += len;
	}
	if (ar_ie && le16_to_cpu(ar_ie->ie_index) == NXPWIFI_AUTO_IDX_MASK)
		/* save assoc resp element index after auto-indexing */
		*assoc_idx = *((u16 *)pos);

	kfree(ap_custom_ie);
	return ret;
}

/* Append vendor IE (if present) into nxpwifi_ie, allocating as needed. */
static int nxpwifi_update_vs_ie(const u8 *ies, int ies_len,
				struct nxpwifi_ie **ie_ptr, u16 mask,
				unsigned int oui, u8 oui_type)
{
	struct element *vs_ie;
	struct nxpwifi_ie *ie = *ie_ptr;
	const u8 *vendor_ie;

	vendor_ie = cfg80211_find_vendor_ie(oui, oui_type, ies, ies_len);
	if (vendor_ie) {
		if (!*ie_ptr) {
			*ie_ptr = kzalloc_obj(struct nxpwifi_ie, GFP_KERNEL);
			if (!*ie_ptr)
				return -ENOMEM;
			ie = *ie_ptr;
		}

		vs_ie = (struct element *)vendor_ie;
		if (le16_to_cpu(ie->ie_length) + vs_ie->datalen + 2 >
			IEEE_MAX_IE_SIZE)
			return -EINVAL;
		memcpy(ie->ie_buffer + le16_to_cpu(ie->ie_length),
		       vs_ie, vs_ie->datalen + 2);
		le16_unaligned_add_cpu(&ie->ie_length, vs_ie->datalen + 2);
		ie->mgmt_subtype_mask = cpu_to_le16(mask);
		ie->ie_index = cpu_to_le16(NXPWIFI_AUTO_IDX_MASK);
	}

	*ie_ptr = ie;
	return 0;
}

/* Parse beacon/probe/assoc IEs from cfg80211 and push them to FW. */
static int nxpwifi_set_mgmt_beacon_data_ies(struct nxpwifi_private *priv,
					    struct cfg80211_beacon_data *data)
{
	struct nxpwifi_ie *beacon_ie = NULL, *pr_ie = NULL, *ar_ie = NULL;
	u16 beacon_idx = NXPWIFI_AUTO_IDX_MASK, pr_idx = NXPWIFI_AUTO_IDX_MASK;
	u16 ar_idx = NXPWIFI_AUTO_IDX_MASK;
	int ret = 0;

	if (data->beacon_ies && data->beacon_ies_len) {
		nxpwifi_update_vs_ie(data->beacon_ies, data->beacon_ies_len,
				     &beacon_ie, MGMT_MASK_BEACON,
				     WLAN_OUI_MICROSOFT,
				     WLAN_OUI_TYPE_MICROSOFT_WPS);
		nxpwifi_update_vs_ie(data->beacon_ies, data->beacon_ies_len,
				     &beacon_ie, MGMT_MASK_BEACON,
				     WLAN_OUI_WFA, WLAN_OUI_TYPE_WFA_P2P);
	}

	if (data->proberesp_ies && data->proberesp_ies_len) {
		nxpwifi_update_vs_ie(data->proberesp_ies,
				     data->proberesp_ies_len, &pr_ie,
				     MGMT_MASK_PROBE_RESP, WLAN_OUI_MICROSOFT,
				     WLAN_OUI_TYPE_MICROSOFT_WPS);
		nxpwifi_update_vs_ie(data->proberesp_ies,
				     data->proberesp_ies_len, &pr_ie,
				     MGMT_MASK_PROBE_RESP,
				     WLAN_OUI_WFA, WLAN_OUI_TYPE_WFA_P2P);
	}

	if (data->assocresp_ies && data->assocresp_ies_len) {
		nxpwifi_update_vs_ie(data->assocresp_ies,
				     data->assocresp_ies_len, &ar_ie,
				     MGMT_MASK_ASSOC_RESP |
				     MGMT_MASK_REASSOC_RESP,
				     WLAN_OUI_MICROSOFT,
				     WLAN_OUI_TYPE_MICROSOFT_WPS);
		nxpwifi_update_vs_ie(data->assocresp_ies,
				     data->assocresp_ies_len, &ar_ie,
				     MGMT_MASK_ASSOC_RESP |
				     MGMT_MASK_REASSOC_RESP, WLAN_OUI_WFA,
				     WLAN_OUI_TYPE_WFA_P2P);
	}

	if (beacon_ie || pr_ie || ar_ie) {
		ret = nxpwifi_update_uap_custom_ie(priv, beacon_ie,
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

	return ret;
}

/* Parse head/tail IEs from cfg80211_beacon_data and send them to FW. */
static int nxpwifi_uap_parse_tail_ies(struct nxpwifi_private *priv,
				      struct cfg80211_beacon_data *info)
{
	struct nxpwifi_ie *gen_ie;
	struct element *hdr;
	struct ieee80211_vendor_ie *vendorhdr;
	u16 gen_idx = NXPWIFI_AUTO_IDX_MASK, ie_len = 0;
	int left_len, parsed_len = 0;
	unsigned int token_len;
	int ret = 0;

	if (!info->tail || !info->tail_len)
		return 0;

	gen_ie = kzalloc_obj(*gen_ie, GFP_KERNEL);
	if (!gen_ie)
		return -ENOMEM;

	left_len = info->tail_len;

	/* Skip IEs generated by FW from bss configuration to avoid duplicates. */
	while (left_len > sizeof(struct element)) {
		hdr = (void *)(info->tail + parsed_len);
		token_len = hdr->datalen + sizeof(struct element);
		if (token_len > left_len) {
			ret = -EINVAL;
			goto done;
		}

		switch (hdr->id) {
		case WLAN_EID_SSID:
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_COUNTRY:
		case WLAN_EID_PWR_CONSTRAINT:
		case WLAN_EID_ERP_INFO:
		case WLAN_EID_EXT_SUPP_RATES:
		case WLAN_EID_HT_CAPABILITY:
		case WLAN_EID_HT_OPERATION:
		case WLAN_EID_VHT_CAPABILITY:
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			/* Skip only Microsoft WMM element */
			if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						    WLAN_OUI_TYPE_MICROSOFT_WMM,
						    (const u8 *)hdr,
						    token_len))
				break;
			fallthrough;
		default:
			if (ie_len + token_len > IEEE_MAX_IE_SIZE) {
				ret = -EINVAL;
				goto done;
			}
			memcpy(gen_ie->ie_buffer + ie_len, hdr, token_len);
			ie_len += token_len;
			break;
		}
		left_len -= token_len;
		parsed_len += token_len;
	}

	/*
	 * parse only WPA vendor element from tail, WMM element is configured by
	 * bss_config command
	 */
	vendorhdr = (void *)cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						    WLAN_OUI_TYPE_MICROSOFT_WPA,
						    info->tail, info->tail_len);
	if (vendorhdr) {
		token_len = vendorhdr->len + sizeof(struct element);
		if (ie_len + token_len > IEEE_MAX_IE_SIZE) {
			ret = -EINVAL;
			goto done;
		}
		memcpy(gen_ie->ie_buffer + ie_len, vendorhdr, token_len);
		ie_len += token_len;
	}

	if (!ie_len)
		goto done;

	gen_ie->ie_index = cpu_to_le16(gen_idx);
	gen_ie->mgmt_subtype_mask = cpu_to_le16(MGMT_MASK_BEACON |
						MGMT_MASK_PROBE_RESP |
						MGMT_MASK_ASSOC_RESP);
	gen_ie->ie_length = cpu_to_le16(ie_len);

	ret = nxpwifi_update_uap_custom_ie(priv, gen_ie, &gen_idx, NULL,
					   NULL, NULL, NULL);

	if (ret)
		goto done;

	priv->gen_idx = gen_idx;

 done:
	kfree(gen_ie);
	return ret;
}

/* Parse head/tail/beacon/probe/assoc IEs and program the FW. */
int nxpwifi_set_mgmt_ies(struct nxpwifi_private *priv,
			 struct cfg80211_beacon_data *info)
{
	int ret;

	ret = nxpwifi_uap_parse_tail_ies(priv, info);

	if (ret)
		return ret;

	return nxpwifi_set_mgmt_beacon_data_ies(priv, info);
}

/* Remove previously set management IEs. */
int nxpwifi_del_mgmt_ies(struct nxpwifi_private *priv)
{
	struct nxpwifi_ie *beacon_ie = NULL, *pr_ie = NULL;
	struct nxpwifi_ie *ar_ie = NULL, *gen_ie = NULL;
	int ret = 0;

	if (priv->gen_idx != NXPWIFI_AUTO_IDX_MASK) {
		gen_ie = kmalloc_obj(*gen_ie, GFP_KERNEL);
		if (!gen_ie)
			return -ENOMEM;

		gen_ie->ie_index = cpu_to_le16(priv->gen_idx);
		gen_ie->mgmt_subtype_mask = cpu_to_le16(NXPWIFI_DELETE_MASK);
		gen_ie->ie_length = 0;
		ret = nxpwifi_update_uap_custom_ie(priv, gen_ie, &priv->gen_idx,
						   NULL, &priv->proberesp_idx,
						   NULL, &priv->assocresp_idx);
		if (ret)
			goto done;

		priv->gen_idx = NXPWIFI_AUTO_IDX_MASK;
	}

	if (priv->beacon_idx != NXPWIFI_AUTO_IDX_MASK) {
		beacon_ie = kmalloc_obj(*beacon_ie, GFP_KERNEL);
		if (!beacon_ie) {
			ret = -ENOMEM;
			goto done;
		}
		beacon_ie->ie_index = cpu_to_le16(priv->beacon_idx);
		beacon_ie->mgmt_subtype_mask = cpu_to_le16(NXPWIFI_DELETE_MASK);
		beacon_ie->ie_length = 0;
	}
	if (priv->proberesp_idx != NXPWIFI_AUTO_IDX_MASK) {
		pr_ie = kmalloc_obj(*pr_ie, GFP_KERNEL);
		if (!pr_ie) {
			ret = -ENOMEM;
			goto done;
		}
		pr_ie->ie_index = cpu_to_le16(priv->proberesp_idx);
		pr_ie->mgmt_subtype_mask = cpu_to_le16(NXPWIFI_DELETE_MASK);
		pr_ie->ie_length = 0;
	}
	if (priv->assocresp_idx != NXPWIFI_AUTO_IDX_MASK) {
		ar_ie = kmalloc_obj(*ar_ie, GFP_KERNEL);
		if (!ar_ie) {
			ret = -ENOMEM;
			goto done;
		}
		ar_ie->ie_index = cpu_to_le16(priv->assocresp_idx);
		ar_ie->mgmt_subtype_mask = cpu_to_le16(NXPWIFI_DELETE_MASK);
		ar_ie->ie_length = 0;
	}

	if (beacon_ie || pr_ie || ar_ie)
		ret = nxpwifi_update_uap_custom_ie(priv,
						   beacon_ie, &priv->beacon_idx,
						   pr_ie, &priv->proberesp_idx,
						   ar_ie, &priv->assocresp_idx);

done:
	kfree(gen_ie);
	kfree(beacon_ie);
	kfree(pr_ie);
	kfree(ar_ie);

	return ret;
}
