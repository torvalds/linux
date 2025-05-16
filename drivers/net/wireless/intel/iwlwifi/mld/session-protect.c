// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024 Intel Corporation
 */
#include "session-protect.h"
#include "fw/api/time-event.h"
#include "fw/api/context.h"
#include "iface.h"
#include <net/mac80211.h>

void iwl_mld_handle_session_prot_notif(struct iwl_mld *mld,
				       struct iwl_rx_packet *pkt)
{
	struct iwl_session_prot_notif *notif = (void *)pkt->data;
	int fw_link_id = le32_to_cpu(notif->mac_link_id);
	struct ieee80211_bss_conf *link_conf =
		iwl_mld_fw_id_to_link_conf(mld, fw_link_id);
	struct ieee80211_vif *vif;
	struct iwl_mld_vif *mld_vif;
	struct iwl_mld_session_protect *session_protect;

	if (WARN_ON(!link_conf))
		return;

	vif = link_conf->vif;
	mld_vif = iwl_mld_vif_from_mac80211(vif);
	session_protect = &mld_vif->session_protect;

	if (!le32_to_cpu(notif->status)) {
		memset(session_protect, 0, sizeof(*session_protect));
	} else if (le32_to_cpu(notif->start)) {
		/* End_jiffies indicates an active session */
		session_protect->session_requested = false;
		session_protect->end_jiffies =
			TU_TO_EXP_TIME(session_protect->duration);
		/* !session_protect->end_jiffies means inactive session */
		if (!session_protect->end_jiffies)
			session_protect->end_jiffies = 1;
	} else {
		memset(session_protect, 0, sizeof(*session_protect));
	}
}

static int _iwl_mld_schedule_session_protection(struct iwl_mld *mld,
						struct ieee80211_vif *vif,
						u32 duration, u32 min_duration,
						int link_id)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *link =
		iwl_mld_link_dereference_check(mld_vif, link_id);
	struct iwl_mld_session_protect *session_protect =
		&mld_vif->session_protect;
	struct iwl_session_prot_cmd cmd = {
		.id_and_color = cpu_to_le32(link->fw_id),
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.conf_id = cpu_to_le32(SESSION_PROTECT_CONF_ASSOC),
		.duration_tu = cpu_to_le32(MSEC_TO_TU(duration)),
	};
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	WARN(hweight16(vif->active_links) > 1,
	     "Session protection isn't allowed with more than one active link");

	if (session_protect->end_jiffies &&
	    time_after(session_protect->end_jiffies,
		       TU_TO_EXP_TIME(min_duration))) {
		IWL_DEBUG_TE(mld, "We have ample in the current session: %u\n",
			     jiffies_to_msecs(session_protect->end_jiffies -
					      jiffies));
		return -EALREADY;
	}

	IWL_DEBUG_TE(mld, "Add a new session protection, duration %d TU\n",
		     le32_to_cpu(cmd.duration_tu));

	ret = iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP,
						SESSION_PROTECTION_CMD), &cmd);

	if (ret)
		return ret;

	/* end_jiffies will be updated when handling session_prot_notif */
	session_protect->end_jiffies = 0;
	session_protect->duration = duration;
	session_protect->session_requested = true;

	return 0;
}

void iwl_mld_schedule_session_protection(struct iwl_mld *mld,
					 struct ieee80211_vif *vif,
					 u32 duration, u32 min_duration,
					 int link_id)
{
	int ret;

	ret = _iwl_mld_schedule_session_protection(mld, vif, duration,
						   min_duration, link_id);
	if (ret && ret != -EALREADY)
		IWL_ERR(mld,
			"Couldn't send the SESSION_PROTECTION_CMD (%d)\n",
			ret);
}

struct iwl_mld_session_start_data {
	struct iwl_mld *mld;
	struct ieee80211_bss_conf *link_conf;
	bool success;
};

static bool iwl_mld_session_start_fn(struct iwl_notif_wait_data *notif_wait,
				     struct iwl_rx_packet *pkt, void *_data)
{
	struct iwl_session_prot_notif *notif = (void *)pkt->data;
	unsigned int pkt_len = iwl_rx_packet_payload_len(pkt);
	struct iwl_mld_session_start_data *data = _data;
	struct ieee80211_bss_conf *link_conf;
	struct iwl_mld *mld = data->mld;
	int fw_link_id;

	if (IWL_FW_CHECK(mld, pkt_len < sizeof(*notif),
			 "short session prot notif (%d)\n",
			 pkt_len))
		return false;

	fw_link_id = le32_to_cpu(notif->mac_link_id);
	link_conf = iwl_mld_fw_id_to_link_conf(mld, fw_link_id);

	if (link_conf != data->link_conf)
		return false;

	if (!le32_to_cpu(notif->status))
		return true;

	if (notif->start) {
		data->success = true;
		return true;
	}

	return false;
}

int iwl_mld_start_session_protection(struct iwl_mld *mld,
				     struct ieee80211_vif *vif,
				     u32 duration, u32 min_duration,
				     int link_id, unsigned long timeout)
{
	static const u16 start_notif[] = { SESSION_PROTECTION_NOTIF };
	struct iwl_notification_wait start_wait;
	struct iwl_mld_session_start_data data = {
		.mld = mld,
		.link_conf = wiphy_dereference(mld->wiphy,
					       vif->link_conf[link_id]),
	};
	int ret;

	if (WARN_ON(!data.link_conf))
		return -EINVAL;

	iwl_init_notification_wait(&mld->notif_wait, &start_wait,
				   start_notif, ARRAY_SIZE(start_notif),
				   iwl_mld_session_start_fn, &data);

	ret = _iwl_mld_schedule_session_protection(mld, vif, duration,
						   min_duration, link_id);

	if (ret) {
		iwl_remove_notification(&mld->notif_wait, &start_wait);
		return ret == -EALREADY ? 0 : ret;
	}

	ret = iwl_wait_notification(&mld->notif_wait, &start_wait, timeout);
	if (ret)
		return ret;
	return data.success ? 0 : -EIO;
}

int iwl_mld_cancel_session_protection(struct iwl_mld *mld,
				      struct ieee80211_vif *vif,
				      int link_id)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *link =
		iwl_mld_link_dereference_check(mld_vif, link_id);
	struct iwl_mld_session_protect *session_protect =
		&mld_vif->session_protect;
	struct iwl_session_prot_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.conf_id = cpu_to_le32(SESSION_PROTECT_CONF_ASSOC),
	};
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	/* If there isn't an active session or a requested one for this
	 * link do nothing
	 */
	if (!session_protect->session_requested &&
	    !session_protect->end_jiffies)
		return 0;

	if (WARN_ON(!link))
		return -EINVAL;

	cmd.id_and_color = cpu_to_le32(link->fw_id);

	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(MAC_CONF_GROUP,
					   SESSION_PROTECTION_CMD), &cmd);
	if (ret) {
		IWL_ERR(mld,
			"Couldn't send the SESSION_PROTECTION_CMD\n");
		return ret;
	}

	memset(session_protect, 0, sizeof(*session_protect));

	return 0;
}
