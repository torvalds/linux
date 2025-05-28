// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#include "fw/api/coex.h"

#include "coex.h"
#include "mld.h"
#include "hcmd.h"
#include "mlo.h"

int iwl_mld_send_bt_init_conf(struct iwl_mld *mld)
{
	struct iwl_bt_coex_cmd cmd = {
		.mode = cpu_to_le32(BT_COEX_NW),
		.enabled_modules = cpu_to_le32(BT_COEX_MPLUT_ENABLED |
					       BT_COEX_HIGH_BAND_RET),
	};

	return iwl_mld_send_cmd_pdu(mld, BT_CONFIG, &cmd);
}

void iwl_mld_handle_bt_coex_notif(struct iwl_mld *mld,
				  struct iwl_rx_packet *pkt)
{
	const struct iwl_bt_coex_profile_notif *notif = (void *)pkt->data;
	const struct iwl_bt_coex_profile_notif zero_notif = {};
	/* zeroed structure means that BT is OFF */
	bool bt_is_active = memcmp(notif, &zero_notif, sizeof(*notif));

	if (bt_is_active == mld->bt_is_active)
		return;

	IWL_DEBUG_INFO(mld, "BT was turned %s\n", bt_is_active ? "ON" : "OFF");

	mld->bt_is_active = bt_is_active;

	iwl_mld_emlsr_check_bt(mld);
}
