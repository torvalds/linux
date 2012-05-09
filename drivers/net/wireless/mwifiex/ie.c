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
