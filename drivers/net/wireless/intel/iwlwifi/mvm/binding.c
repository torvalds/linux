// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2020 Intel Corporation
 * Copyright (C) 2016 Intel Deutschland GmbH
 * Copyright (C) 2022 Intel Corporation
 */
#include <net/mac80211.h>
#include "fw-api.h"
#include "mvm.h"

struct iwl_mvm_iface_iterator_data {
	struct ieee80211_vif *ignore_vif;
	int idx;

	struct iwl_mvm_phy_ctxt *phyctxt;

	u16 ids[MAX_MACS_IN_BINDING];
	u16 colors[MAX_MACS_IN_BINDING];
};

static int iwl_mvm_binding_cmd(struct iwl_mvm *mvm, u32 action,
			       struct iwl_mvm_iface_iterator_data *data)
{
	struct iwl_binding_cmd cmd;
	struct iwl_mvm_phy_ctxt *phyctxt = data->phyctxt;
	int i, ret;
	u32 status;
	int size;

	memset(&cmd, 0, sizeof(cmd));

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT)) {
		size = sizeof(cmd);
		cmd.lmac_id = cpu_to_le32(iwl_mvm_get_lmac_id(mvm->fw,
							      phyctxt->channel->band));
	} else {
		size = IWL_BINDING_CMD_SIZE_V1;
	}

	cmd.id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(phyctxt->id,
							   phyctxt->color));
	cmd.action = cpu_to_le32(action);
	cmd.phy = cpu_to_le32(FW_CMD_ID_AND_COLOR(phyctxt->id,
						  phyctxt->color));

	for (i = 0; i < MAX_MACS_IN_BINDING; i++)
		cmd.macs[i] = cpu_to_le32(FW_CTXT_INVALID);
	for (i = 0; i < data->idx; i++)
		cmd.macs[i] = cpu_to_le32(FW_CMD_ID_AND_COLOR(data->ids[i],
							      data->colors[i]));

	status = 0;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, BINDING_CONTEXT_CMD,
					  size, &cmd, &status);
	if (ret) {
		IWL_ERR(mvm, "Failed to send binding (action:%d): %d\n",
			action, ret);
		return ret;
	}

	if (status) {
		IWL_ERR(mvm, "Binding command failed: %u\n", status);
		ret = -EIO;
	}

	return ret;
}

static void iwl_mvm_iface_iterator(void *_data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct iwl_mvm_iface_iterator_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif == data->ignore_vif)
		return;

	if (mvmvif->deflink.phy_ctxt != data->phyctxt)
		return;

	if (WARN_ON_ONCE(data->idx >= MAX_MACS_IN_BINDING))
		return;

	data->ids[data->idx] = mvmvif->id;
	data->colors[data->idx] = mvmvif->color;
	data->idx++;
}

static int iwl_mvm_binding_update(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  struct iwl_mvm_phy_ctxt *phyctxt,
				  bool add)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_iface_iterator_data data = {
		.ignore_vif = vif,
		.phyctxt = phyctxt,
	};
	u32 action = FW_CTXT_ACTION_MODIFY;

	lockdep_assert_held(&mvm->mutex);

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mvm_iface_iterator,
						   &data);

	/*
	 * If there are no other interfaces yet we
	 * need to create a new binding.
	 */
	if (data.idx == 0) {
		if (add)
			action = FW_CTXT_ACTION_ADD;
		else
			action = FW_CTXT_ACTION_REMOVE;
	}

	if (add) {
		if (WARN_ON_ONCE(data.idx >= MAX_MACS_IN_BINDING))
			return -EINVAL;

		data.ids[data.idx] = mvmvif->id;
		data.colors[data.idx] = mvmvif->color;
		data.idx++;
	}

	return iwl_mvm_binding_cmd(mvm, action, &data);
}

int iwl_mvm_binding_add_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (WARN_ON_ONCE(!mvmvif->deflink.phy_ctxt))
		return -EINVAL;

	/*
	 * Update SF - Disable if needed. if this fails, SF might still be on
	 * while many macs are bound, which is forbidden - so fail the binding.
	 */
	if (iwl_mvm_sf_update(mvm, vif, false))
		return -EINVAL;

	return iwl_mvm_binding_update(mvm, vif, mvmvif->deflink.phy_ctxt,
				      true);
}

int iwl_mvm_binding_remove_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (WARN_ON_ONCE(!mvmvif->deflink.phy_ctxt))
		return -EINVAL;

	ret = iwl_mvm_binding_update(mvm, vif, mvmvif->deflink.phy_ctxt,
				     false);

	if (!ret)
		if (iwl_mvm_sf_update(mvm, vif, true))
			IWL_ERR(mvm, "Failed to update SF state\n");

	return ret;
}
