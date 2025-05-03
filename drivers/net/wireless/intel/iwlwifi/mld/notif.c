// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#include "mld.h"
#include "notif.h"
#include "scan.h"
#include "iface.h"
#include "mlo.h"
#include "iwl-trans.h"
#include "fw/file.h"
#include "fw/dbg.h"
#include "fw/api/cmdhdr.h"
#include "fw/api/mac-cfg.h"
#include "session-protect.h"
#include "fw/api/time-event.h"
#include "fw/api/tx.h"
#include "fw/api/rs.h"
#include "fw/api/offload.h"
#include "fw/api/stats.h"
#include "fw/api/rfi.h"
#include "fw/api/coex.h"

#include "mcc.h"
#include "link.h"
#include "tx.h"
#include "rx.h"
#include "tlc.h"
#include "agg.h"
#include "mac80211.h"
#include "thermal.h"
#include "roc.h"
#include "stats.h"
#include "coex.h"
#include "time_sync.h"
#include "ftm-initiator.h"

/* Please use this in an increasing order of the versions */
#define CMD_VER_ENTRY(_ver, _struct)			\
	{ .size = sizeof(struct _struct), .ver = _ver },
#define CMD_VERSIONS(name, ...)				\
	static const struct iwl_notif_struct_size	\
	iwl_notif_struct_sizes_##name[] = { __VA_ARGS__ };

#define RX_HANDLER_NO_OBJECT(_grp, _cmd, _name, _context)		\
	{.cmd_id = WIDE_ID(_grp, _cmd),					\
	 .context = _context,						\
	 .fn = iwl_mld_handle_##_name,					\
	 .sizes = iwl_notif_struct_sizes_##_name,			\
	 .n_sizes = ARRAY_SIZE(iwl_notif_struct_sizes_##_name),		\
	},

/* Use this for Rx handlers that do not need notification validation */
#define RX_HANDLER_NO_VAL(_grp, _cmd, _name, _context)			\
	{.cmd_id = WIDE_ID(_grp, _cmd),					\
	 .context = _context,						\
	 .fn = iwl_mld_handle_##_name,					\
	},

#define RX_HANDLER_VAL_FN(_grp, _cmd, _name, _context)			\
	{ .cmd_id = WIDE_ID(_grp, _cmd),				\
	  .context = _context,						\
	  .fn = iwl_mld_handle_##_name,					\
	  .val_fn = iwl_mld_validate_##_name,				\
	},

#define DEFINE_SIMPLE_CANCELLATION(name, notif_struct, id_member)		\
static bool iwl_mld_cancel_##name##_notif(struct iwl_mld *mld,			\
					  struct iwl_rx_packet *pkt,		\
					  u32 obj_id)				\
{										\
	const struct notif_struct *notif = (const void *)pkt->data;		\
										\
	return obj_id == _Generic((notif)->id_member,				\
				  __le32: le32_to_cpu((notif)->id_member),	\
				  __le16: le16_to_cpu((notif)->id_member),	\
				  u8: (notif)->id_member);			\
}

static bool iwl_mld_always_cancel(struct iwl_mld *mld,
				  struct iwl_rx_packet *pkt,
				  u32 obj_id)
{
	return true;
}

/* Currently only defined for the RX_HANDLER_SIZES options. Use this for
 * notifications that belong to a specific object, and that should be
 * canceled when the object is removed
 */
#define RX_HANDLER_OF_OBJ(_grp, _cmd, _name, _obj_type)			\
	{.cmd_id = WIDE_ID(_grp, _cmd),					\
	/* Only async handlers can be canceled */			\
	 .context = RX_HANDLER_ASYNC,					\
	 .fn = iwl_mld_handle_##_name,					\
	 .sizes = iwl_notif_struct_sizes_##_name,			\
	 .n_sizes = ARRAY_SIZE(iwl_notif_struct_sizes_##_name),		\
	 .obj_type = IWL_MLD_OBJECT_TYPE_##_obj_type,			\
	 .cancel = iwl_mld_cancel_##_name,				\
	 },

#define RX_HANDLER_OF_LINK(_grp, _cmd, _name)				\
	RX_HANDLER_OF_OBJ(_grp, _cmd, _name, LINK)			\

#define RX_HANDLER_OF_VIF(_grp, _cmd, _name)				\
	RX_HANDLER_OF_OBJ(_grp, _cmd, _name, VIF)			\

#define RX_HANDLER_OF_STA(_grp, _cmd, _name)				\
	RX_HANDLER_OF_OBJ(_grp, _cmd, _name, STA)			\

#define RX_HANDLER_OF_ROC(_grp, _cmd, _name)				\
	RX_HANDLER_OF_OBJ(_grp, _cmd, _name, ROC)

#define RX_HANDLER_OF_SCAN(_grp, _cmd, _name)				\
	RX_HANDLER_OF_OBJ(_grp, _cmd, _name, SCAN)

#define RX_HANDLER_OF_FTM_REQ(_grp, _cmd, _name)				\
	RX_HANDLER_OF_OBJ(_grp, _cmd, _name, FTM_REQ)

static void iwl_mld_handle_mfuart_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt)
{
	struct iwl_mfuart_load_notif *mfuart_notif = (void *)pkt->data;

	IWL_DEBUG_INFO(mld,
		       "MFUART: installed ver: 0x%08x, external ver: 0x%08x\n",
		       le32_to_cpu(mfuart_notif->installed_ver),
		       le32_to_cpu(mfuart_notif->external_ver));
	IWL_DEBUG_INFO(mld,
		       "MFUART: status: 0x%08x, duration: 0x%08x image size: 0x%08x\n",
		       le32_to_cpu(mfuart_notif->status),
		       le32_to_cpu(mfuart_notif->duration),
		       le32_to_cpu(mfuart_notif->image_size));
}

static void iwl_mld_mu_mimo_iface_iterator(void *_data, u8 *mac,
					   struct ieee80211_vif *vif)
{
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	unsigned int link_id = 0;

	if (WARN(hweight16(vif->active_links) > 1,
		 "no support for this notif while in EMLSR 0x%x\n",
		 vif->active_links))
		return;

	if (ieee80211_vif_is_mld(vif)) {
		link_id = __ffs(vif->active_links);
		bss_conf = link_conf_dereference_check(vif, link_id);
	}

	if (!WARN_ON(!bss_conf) && bss_conf->mu_mimo_owner) {
		const struct iwl_mu_group_mgmt_notif *notif = _data;

		BUILD_BUG_ON(sizeof(notif->membership_status) !=
			     WLAN_MEMBERSHIP_LEN);
		BUILD_BUG_ON(sizeof(notif->user_position) !=
			     WLAN_USER_POSITION_LEN);

		/* MU-MIMO Group Id action frame is little endian. We treat
		 * the data received from firmware as if it came from the
		 * action frame, so no conversion is needed.
		 */
		ieee80211_update_mu_groups(vif, link_id,
					   (u8 *)&notif->membership_status,
					   (u8 *)&notif->user_position);
	}
}

/* This handler is called in SYNC mode because it needs to be serialized with
 * Rx as specified in ieee80211_update_mu_groups()'s documentation.
 */
static void iwl_mld_handle_mu_mimo_grp_notif(struct iwl_mld *mld,
					     struct iwl_rx_packet *pkt)
{
	struct iwl_mu_group_mgmt_notif *notif = (void *)pkt->data;

	ieee80211_iterate_active_interfaces_atomic(mld->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mld_mu_mimo_iface_iterator,
						   notif);
}

static void
iwl_mld_handle_channel_switch_start_notif(struct iwl_mld *mld,
					  struct iwl_rx_packet *pkt)
{
	struct iwl_channel_switch_start_notif *notif = (void *)pkt->data;
	u32 link_id = le32_to_cpu(notif->link_id);
	struct ieee80211_bss_conf *link_conf =
		iwl_mld_fw_id_to_link_conf(mld, link_id);
	struct ieee80211_vif *vif;

	if (WARN_ON(!link_conf))
		return;

	vif = link_conf->vif;

	IWL_DEBUG_INFO(mld,
		       "CSA Start Notification with vif type: %d, link_id: %d\n",
		       vif->type,
		       link_conf->link_id);

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		/* We don't support canceling a CSA as it was advertised
		 * by the AP itself
		 */
		if (!link_conf->csa_active)
			return;

		ieee80211_csa_finish(vif, link_conf->link_id);
		break;
	case NL80211_IFTYPE_STATION:
		if (!link_conf->csa_active) {
			/* Either unexpected cs notif or mac80211 chose to
			 * ignore, for example in channel switch to same channel
			 */
			struct iwl_cancel_channel_switch_cmd cmd = {
				.id = cpu_to_le32(link_id),
			};

			if (iwl_mld_send_cmd_pdu(mld,
						 WIDE_ID(MAC_CONF_GROUP,
							 CANCEL_CHANNEL_SWITCH_CMD),
						 &cmd))
				IWL_ERR(mld,
					"Failed to cancel the channel switch\n");
			return;
		}

		ieee80211_chswitch_done(vif, true, link_conf->link_id);
		break;

	default:
		WARN(1, "CSA on invalid vif type: %d", vif->type);
	}
}

static void
iwl_mld_handle_channel_switch_error_notif(struct iwl_mld *mld,
					  struct iwl_rx_packet *pkt)
{
	struct iwl_channel_switch_error_notif *notif = (void *)pkt->data;
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_vif *vif;
	u32 link_id = le32_to_cpu(notif->link_id);
	u32 csa_err_mask = le32_to_cpu(notif->csa_err_mask);

	link_conf = iwl_mld_fw_id_to_link_conf(mld, link_id);
	if (WARN_ON(!link_conf))
		return;

	vif = link_conf->vif;

	IWL_DEBUG_INFO(mld, "FW reports CSA error: id=%u, csa_err_mask=%u\n",
		       link_id, csa_err_mask);

	if (csa_err_mask & (CS_ERR_COUNT_ERROR |
			    CS_ERR_LONG_DELAY_AFTER_CS |
			    CS_ERR_TX_BLOCK_TIMER_EXPIRED))
		ieee80211_channel_switch_disconnect(vif);
}

static void iwl_mld_handle_beacon_notification(struct iwl_mld *mld,
					       struct iwl_rx_packet *pkt)
{
	struct iwl_extended_beacon_notif *beacon = (void *)pkt->data;

	mld->ibss_manager = !!beacon->ibss_mgr_status;
}

/**
 * DOC: Notification versioning
 *
 * The firmware's notifications change from time to time. In order to
 * differentiate between different versions of the same notification, the
 * firmware advertises the version of each notification.
 * Here are listed all the notifications that are supported. Several versions
 * of the same notification can be allowed at the same time:
 *
 * CMD_VERSION(my_multi_version_notif,
 *	       CMD_VER_ENTRY(1, iwl_my_multi_version_notif_ver1)
 *	       CMD_VER_ENTRY(2, iwl_my_multi_version_notif_ver2)
 *
 * etc...
 *
 * The driver will enforce that the notification coming from the firmware
 * has its version listed here and it'll also enforce that the firmware sent
 * at least enough bytes to cover the structure listed in the CMD_VER_ENTRY.
 */

CMD_VERSIONS(scan_complete_notif,
	     CMD_VER_ENTRY(1, iwl_umac_scan_complete))
CMD_VERSIONS(scan_iter_complete_notif,
	     CMD_VER_ENTRY(2, iwl_umac_scan_iter_complete_notif))
CMD_VERSIONS(mfuart_notif,
	     CMD_VER_ENTRY(2, iwl_mfuart_load_notif))
CMD_VERSIONS(update_mcc,
	     CMD_VER_ENTRY(1, iwl_mcc_chub_notif))
CMD_VERSIONS(session_prot_notif,
	     CMD_VER_ENTRY(3, iwl_session_prot_notif))
CMD_VERSIONS(missed_beacon_notif,
	     CMD_VER_ENTRY(5, iwl_missed_beacons_notif))
CMD_VERSIONS(tx_resp_notif,
	     CMD_VER_ENTRY(8, iwl_tx_resp))
CMD_VERSIONS(compressed_ba_notif,
	     CMD_VER_ENTRY(5, iwl_compressed_ba_notif)
	     CMD_VER_ENTRY(6, iwl_compressed_ba_notif)
	     CMD_VER_ENTRY(7, iwl_compressed_ba_notif))
CMD_VERSIONS(tlc_notif,
	     CMD_VER_ENTRY(3, iwl_tlc_update_notif))
CMD_VERSIONS(mu_mimo_grp_notif,
	     CMD_VER_ENTRY(1, iwl_mu_group_mgmt_notif))
CMD_VERSIONS(channel_switch_start_notif,
	     CMD_VER_ENTRY(3, iwl_channel_switch_start_notif))
CMD_VERSIONS(channel_switch_error_notif,
	     CMD_VER_ENTRY(2, iwl_channel_switch_error_notif))
CMD_VERSIONS(ct_kill_notif,
	     CMD_VER_ENTRY(2, ct_kill_notif))
CMD_VERSIONS(temp_notif,
	     CMD_VER_ENTRY(2, iwl_dts_measurement_notif))
CMD_VERSIONS(roc_notif,
	     CMD_VER_ENTRY(1, iwl_roc_notif))
CMD_VERSIONS(probe_resp_data_notif,
	     CMD_VER_ENTRY(1, iwl_probe_resp_data_notif))
CMD_VERSIONS(datapath_monitor_notif,
	     CMD_VER_ENTRY(1, iwl_datapath_monitor_notif))
CMD_VERSIONS(stats_oper_notif,
	     CMD_VER_ENTRY(3, iwl_system_statistics_notif_oper))
CMD_VERSIONS(stats_oper_part1_notif,
	     CMD_VER_ENTRY(4, iwl_system_statistics_part1_notif_oper))
CMD_VERSIONS(bt_coex_notif,
	     CMD_VER_ENTRY(1, iwl_bt_coex_profile_notif))
CMD_VERSIONS(beacon_notification,
	     CMD_VER_ENTRY(6, iwl_extended_beacon_notif))
CMD_VERSIONS(emlsr_mode_notif,
	     CMD_VER_ENTRY(1, iwl_esr_mode_notif_v1)
	     CMD_VER_ENTRY(2, iwl_esr_mode_notif))
CMD_VERSIONS(emlsr_trans_fail_notif,
	     CMD_VER_ENTRY(1, iwl_esr_trans_fail_notif))
CMD_VERSIONS(uapsd_misbehaving_ap_notif,
	     CMD_VER_ENTRY(1, iwl_uapsd_misbehaving_ap_notif))
CMD_VERSIONS(time_msmt_notif,
	     CMD_VER_ENTRY(1, iwl_time_msmt_notify))
CMD_VERSIONS(time_sync_confirm_notif,
	     CMD_VER_ENTRY(1, iwl_time_msmt_cfm_notify))
CMD_VERSIONS(omi_status_notif,
	     CMD_VER_ENTRY(1, iwl_omi_send_status_notif))
CMD_VERSIONS(ftm_resp_notif, CMD_VER_ENTRY(9, iwl_tof_range_rsp_ntfy))

DEFINE_SIMPLE_CANCELLATION(session_prot, iwl_session_prot_notif, mac_link_id)
DEFINE_SIMPLE_CANCELLATION(tlc, iwl_tlc_update_notif, sta_id)
DEFINE_SIMPLE_CANCELLATION(channel_switch_start,
			   iwl_channel_switch_start_notif, link_id)
DEFINE_SIMPLE_CANCELLATION(channel_switch_error,
			   iwl_channel_switch_error_notif, link_id)
DEFINE_SIMPLE_CANCELLATION(datapath_monitor, iwl_datapath_monitor_notif,
			   link_id)
DEFINE_SIMPLE_CANCELLATION(roc, iwl_roc_notif, activity)
DEFINE_SIMPLE_CANCELLATION(scan_complete, iwl_umac_scan_complete, uid)
DEFINE_SIMPLE_CANCELLATION(probe_resp_data, iwl_probe_resp_data_notif,
			   mac_id)
DEFINE_SIMPLE_CANCELLATION(uapsd_misbehaving_ap, iwl_uapsd_misbehaving_ap_notif,
			   mac_id)
#define iwl_mld_cancel_omi_status_notif iwl_mld_always_cancel
DEFINE_SIMPLE_CANCELLATION(ftm_resp, iwl_tof_range_rsp_ntfy, request_id)

/**
 * DOC: Handlers for fw notifications
 *
 * Here are listed the notifications IDs (including the group ID), the handler
 * of the notification and how it should be called:
 *
 *  - RX_HANDLER_SYNC: will be called as part of the Rx path
 *  - RX_HANDLER_ASYNC: will be handled in a working with the wiphy_lock held
 *
 * This means that if the firmware sends two notifications A and B in that
 * order and notification A is RX_HANDLER_ASYNC and notification is
 * RX_HANDLER_SYNC, the handler of B will likely be called before the handler
 * of A.
 *
 * This list should be in order of frequency for performance purposes.
 * The handler can be one from two contexts, see &iwl_rx_handler_context
 *
 * A handler can declare that it relies on a specific object in which case it
 * can be cancelled in case the object is deleted. In order to use this
 * mechanism, a cancellation function is needed. The cancellation function must
 * receive an object id (the index of that object in the firmware) and a
 * notification payload. It'll return true if that specific notification should
 * be cancelled upon the obliteration of the specific instance of the object.
 *
 * DEFINE_SIMPLE_CANCELLATION allows to easily create a cancellation function
 * that wills simply return true if a given object id matches the object id in
 * the firmware notification.
 */

VISIBLE_IF_IWLWIFI_KUNIT
const struct iwl_rx_handler iwl_mld_rx_handlers[] = {
	RX_HANDLER_NO_OBJECT(LEGACY_GROUP, TX_CMD, tx_resp_notif,
			     RX_HANDLER_SYNC)
	RX_HANDLER_NO_OBJECT(LEGACY_GROUP, BA_NOTIF, compressed_ba_notif,
			     RX_HANDLER_SYNC)
	RX_HANDLER_OF_SCAN(LEGACY_GROUP, SCAN_COMPLETE_UMAC,
			   scan_complete_notif)
	RX_HANDLER_NO_OBJECT(LEGACY_GROUP, SCAN_ITERATION_COMPLETE_UMAC,
			     scan_iter_complete_notif,
			     RX_HANDLER_SYNC)
	RX_HANDLER_NO_VAL(LEGACY_GROUP, MATCH_FOUND_NOTIFICATION,
			  match_found_notif, RX_HANDLER_SYNC)

	RX_HANDLER_NO_OBJECT(STATISTICS_GROUP, STATISTICS_OPER_NOTIF,
			     stats_oper_notif, RX_HANDLER_ASYNC)
	RX_HANDLER_NO_OBJECT(STATISTICS_GROUP, STATISTICS_OPER_PART1_NOTIF,
			     stats_oper_part1_notif, RX_HANDLER_ASYNC)

	RX_HANDLER_NO_OBJECT(LEGACY_GROUP, MFUART_LOAD_NOTIFICATION,
			     mfuart_notif, RX_HANDLER_SYNC)

	RX_HANDLER_NO_OBJECT(PHY_OPS_GROUP, DTS_MEASUREMENT_NOTIF_WIDE,
			     temp_notif, RX_HANDLER_ASYNC)
	RX_HANDLER_OF_LINK(MAC_CONF_GROUP, SESSION_PROTECTION_NOTIF,
			   session_prot_notif)
	RX_HANDLER_OF_LINK(MAC_CONF_GROUP, MISSED_BEACONS_NOTIF,
			   missed_beacon_notif)
	RX_HANDLER_OF_STA(DATA_PATH_GROUP, TLC_MNG_UPDATE_NOTIF, tlc_notif)
	RX_HANDLER_OF_LINK(MAC_CONF_GROUP, CHANNEL_SWITCH_START_NOTIF,
			   channel_switch_start_notif)
	RX_HANDLER_OF_LINK(MAC_CONF_GROUP, CHANNEL_SWITCH_ERROR_NOTIF,
			   channel_switch_error_notif)
	RX_HANDLER_OF_ROC(MAC_CONF_GROUP, ROC_NOTIF, roc_notif)
	RX_HANDLER_NO_OBJECT(DATA_PATH_GROUP, MU_GROUP_MGMT_NOTIF,
			     mu_mimo_grp_notif, RX_HANDLER_SYNC)
	RX_HANDLER_OF_VIF(MAC_CONF_GROUP, PROBE_RESPONSE_DATA_NOTIF,
			  probe_resp_data_notif)
	RX_HANDLER_NO_OBJECT(PHY_OPS_GROUP, CT_KILL_NOTIFICATION,
			     ct_kill_notif, RX_HANDLER_ASYNC)
	RX_HANDLER_OF_LINK(DATA_PATH_GROUP, MONITOR_NOTIF,
			   datapath_monitor_notif)
	RX_HANDLER_NO_OBJECT(LEGACY_GROUP, MCC_CHUB_UPDATE_CMD, update_mcc,
			     RX_HANDLER_ASYNC)
	RX_HANDLER_NO_OBJECT(BT_COEX_GROUP, PROFILE_NOTIF,
			     bt_coex_notif, RX_HANDLER_ASYNC)
	RX_HANDLER_NO_OBJECT(LEGACY_GROUP, BEACON_NOTIFICATION,
			     beacon_notification, RX_HANDLER_ASYNC)
	RX_HANDLER_NO_OBJECT(DATA_PATH_GROUP, ESR_MODE_NOTIF,
			     emlsr_mode_notif, RX_HANDLER_ASYNC)
	RX_HANDLER_NO_OBJECT(MAC_CONF_GROUP, EMLSR_TRANS_FAIL_NOTIF,
			     emlsr_trans_fail_notif, RX_HANDLER_ASYNC)
	RX_HANDLER_OF_VIF(LEGACY_GROUP, PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION,
			  uapsd_misbehaving_ap_notif)
	RX_HANDLER_NO_OBJECT(LEGACY_GROUP,
			     WNM_80211V_TIMING_MEASUREMENT_NOTIFICATION,
			     time_msmt_notif, RX_HANDLER_SYNC)
	RX_HANDLER_NO_OBJECT(LEGACY_GROUP,
			     WNM_80211V_TIMING_MEASUREMENT_CONFIRM_NOTIFICATION,
			     time_sync_confirm_notif, RX_HANDLER_ASYNC)
	RX_HANDLER_OF_LINK(DATA_PATH_GROUP, OMI_SEND_STATUS_NOTIF,
			   omi_status_notif)
	RX_HANDLER_OF_FTM_REQ(LOCATION_GROUP, TOF_RANGE_RESPONSE_NOTIF,
			      ftm_resp_notif)
};
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_rx_handlers);

#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
const unsigned int iwl_mld_rx_handlers_num = ARRAY_SIZE(iwl_mld_rx_handlers);
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_rx_handlers_num);
#endif

static bool
iwl_mld_notif_is_valid(struct iwl_mld *mld, struct iwl_rx_packet *pkt,
		       const struct iwl_rx_handler *handler)
{
	unsigned int size = iwl_rx_packet_payload_len(pkt);
	size_t notif_ver;

	/* If n_sizes == 0, it indicates that a validation function may be used
	 * or that no validation is required.
	 */
	if (!handler->n_sizes) {
		if (handler->val_fn)
			return handler->val_fn(mld, pkt);
		return true;
	}

	notif_ver = iwl_fw_lookup_notif_ver(mld->fw,
					    iwl_cmd_groupid(handler->cmd_id),
					    iwl_cmd_opcode(handler->cmd_id),
					    IWL_FW_CMD_VER_UNKNOWN);

	for (int i = 0; i < handler->n_sizes; i++) {
		if (handler->sizes[i].ver != notif_ver)
			continue;

		if (IWL_FW_CHECK(mld, size < handler->sizes[i].size,
				 "unexpected notification 0x%04x size %d, need %d\n",
				 handler->cmd_id, size, handler->sizes[i].size))
			return false;
		return true;
	}

	IWL_FW_CHECK_FAILED(mld,
			    "notif 0x%04x ver %zu missing expected size, use version %u size\n",
			    handler->cmd_id, notif_ver,
			    handler->sizes[handler->n_sizes - 1].ver);

	return size < handler->sizes[handler->n_sizes - 1].size;
}

struct iwl_async_handler_entry {
	struct list_head list;
	struct iwl_rx_cmd_buffer rxb;
	const struct iwl_rx_handler *rx_h;
};

static void
iwl_mld_log_async_handler_op(struct iwl_mld *mld, const char *op,
			     struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);

	IWL_DEBUG_HC(mld,
		     "%s async handler for notif %s (%.2x.%2x, seq 0x%x)\n",
		     op, iwl_get_cmd_string(mld->trans,
		     WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd)),
		     pkt->hdr.group_id, pkt->hdr.cmd,
		     le16_to_cpu(pkt->hdr.sequence));
}

static void iwl_mld_rx_notif(struct iwl_mld *mld,
			     struct iwl_rx_cmd_buffer *rxb,
			     struct iwl_rx_packet *pkt)
{
	for (int i = 0; i < ARRAY_SIZE(iwl_mld_rx_handlers); i++) {
		const struct iwl_rx_handler *rx_h = &iwl_mld_rx_handlers[i];
		struct iwl_async_handler_entry *entry;

		if (rx_h->cmd_id != WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd))
			continue;

		if (!iwl_mld_notif_is_valid(mld, pkt, rx_h))
			return;

		if (rx_h->context == RX_HANDLER_SYNC) {
			rx_h->fn(mld, pkt);
			break;
		}

		entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
		/* we can't do much... */
		if (!entry)
			return;

		/* Set the async handler entry */
		entry->rxb._page = rxb_steal_page(rxb);
		entry->rxb._offset = rxb->_offset;
		entry->rxb._rx_page_order = rxb->_rx_page_order;

		entry->rx_h = rx_h;

		/* Add it to the list and queue the work */
		spin_lock(&mld->async_handlers_lock);
		list_add_tail(&entry->list, &mld->async_handlers_list);
		spin_unlock(&mld->async_handlers_lock);

		wiphy_work_queue(mld->hw->wiphy,
				 &mld->async_handlers_wk);

		iwl_mld_log_async_handler_op(mld, "Queued", rxb);
		break;
	}

	iwl_notification_wait_notify(&mld->notif_wait, pkt);
}

void iwl_mld_rx(struct iwl_op_mode *op_mode, struct napi_struct *napi,
		struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);
	u16 cmd_id = WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);

	if (likely(cmd_id == WIDE_ID(LEGACY_GROUP, REPLY_RX_MPDU_CMD)))
		iwl_mld_rx_mpdu(mld, napi, rxb, 0);
	else if (cmd_id == WIDE_ID(LEGACY_GROUP, FRAME_RELEASE))
		iwl_mld_handle_frame_release_notif(mld, napi, pkt, 0);
	else if (cmd_id == WIDE_ID(LEGACY_GROUP, BAR_FRAME_RELEASE))
		iwl_mld_handle_bar_frame_release_notif(mld, napi, pkt, 0);
	else if (unlikely(cmd_id == WIDE_ID(DATA_PATH_GROUP,
					    RX_QUEUES_NOTIFICATION)))
		iwl_mld_handle_rx_queues_sync_notif(mld, napi, pkt, 0);
	else if (cmd_id == WIDE_ID(DATA_PATH_GROUP, RX_NO_DATA_NOTIF))
		iwl_mld_rx_monitor_no_data(mld, napi, pkt, 0);
	else
		iwl_mld_rx_notif(mld, rxb, pkt);
}

void iwl_mld_rx_rss(struct iwl_op_mode *op_mode, struct napi_struct *napi,
		    struct iwl_rx_cmd_buffer *rxb, unsigned int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);
	u16 cmd_id = WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);

	if (unlikely(queue >= mld->trans->info.num_rxqs))
		return;

	if (likely(cmd_id == WIDE_ID(LEGACY_GROUP, REPLY_RX_MPDU_CMD)))
		iwl_mld_rx_mpdu(mld, napi, rxb, queue);
	else if (unlikely(cmd_id == WIDE_ID(DATA_PATH_GROUP,
					    RX_QUEUES_NOTIFICATION)))
		iwl_mld_handle_rx_queues_sync_notif(mld, napi, pkt, queue);
	else if (unlikely(cmd_id == WIDE_ID(LEGACY_GROUP, FRAME_RELEASE)))
		iwl_mld_handle_frame_release_notif(mld, napi, pkt, queue);
}

void iwl_mld_delete_handlers(struct iwl_mld *mld, const u16 *cmds, int n_cmds)
{
	struct iwl_async_handler_entry *entry, *tmp;

	spin_lock_bh(&mld->async_handlers_lock);
	list_for_each_entry_safe(entry, tmp, &mld->async_handlers_list, list) {
		bool match = false;

		for (int i = 0; i < n_cmds; i++) {
			if (entry->rx_h->cmd_id == cmds[i]) {
				match = true;
				break;
			}
		}

		if (!match)
			continue;

		iwl_mld_log_async_handler_op(mld, "Delete", &entry->rxb);
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		kfree(entry);
	}
	spin_unlock_bh(&mld->async_handlers_lock);
}

void iwl_mld_async_handlers_wk(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct iwl_mld *mld =
		container_of(wk, struct iwl_mld, async_handlers_wk);
	struct iwl_async_handler_entry *entry, *tmp;
	LIST_HEAD(local_list);

	/* Sync with Rx path with a lock. Remove all the entries from this
	 * list, add them to a local one (lock free), and then handle them.
	 */
	spin_lock_bh(&mld->async_handlers_lock);
	list_splice_init(&mld->async_handlers_list, &local_list);
	spin_unlock_bh(&mld->async_handlers_lock);

	list_for_each_entry_safe(entry, tmp, &local_list, list) {
		iwl_mld_log_async_handler_op(mld, "Handle", &entry->rxb);
		entry->rx_h->fn(mld, rxb_addr(&entry->rxb));
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		kfree(entry);
	}
}

void iwl_mld_cancel_async_notifications(struct iwl_mld *mld)
{
	struct iwl_async_handler_entry *entry, *tmp;

	lockdep_assert_wiphy(mld->wiphy);

	wiphy_work_cancel(mld->wiphy, &mld->async_handlers_wk);

	spin_lock_bh(&mld->async_handlers_lock);
	list_for_each_entry_safe(entry, tmp, &mld->async_handlers_list, list) {
		iwl_mld_log_async_handler_op(mld, "Purged", &entry->rxb);
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		kfree(entry);
	}
	spin_unlock_bh(&mld->async_handlers_lock);
}

void iwl_mld_cancel_notifications_of_object(struct iwl_mld *mld,
					    enum iwl_mld_object_type obj_type,
					    u32 obj_id)
{
	struct iwl_async_handler_entry *entry, *tmp;
	LIST_HEAD(cancel_list);

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(obj_type == IWL_MLD_OBJECT_TYPE_NONE))
		return;

	/* Sync with RX path and remove matching entries from the async list */
	spin_lock_bh(&mld->async_handlers_lock);
	list_for_each_entry_safe(entry, tmp, &mld->async_handlers_list, list) {
		const struct iwl_rx_handler *rx_h = entry->rx_h;

		if (rx_h->obj_type != obj_type || WARN_ON(!rx_h->cancel))
			continue;

		if (rx_h->cancel(mld, rxb_addr(&entry->rxb), obj_id)) {
			iwl_mld_log_async_handler_op(mld, "Cancel", &entry->rxb);
			list_del(&entry->list);
			list_add_tail(&entry->list, &cancel_list);
		}
	}

	spin_unlock_bh(&mld->async_handlers_lock);

	/* Free the matching entries outside of the spinlock */
	list_for_each_entry_safe(entry, tmp, &cancel_list, list) {
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		kfree(entry);
	}
}
