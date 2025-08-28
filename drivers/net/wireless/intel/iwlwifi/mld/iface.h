/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_iface_h__
#define __iwl_mld_iface_h__

#include <net/mac80211.h>

#include "link.h"
#include "session-protect.h"
#include "d3.h"
#include "fw/api/time-event.h"

enum iwl_mld_cca_40mhz_wa_status {
	CCA_40_MHZ_WA_NONE,
	CCA_40_MHZ_WA_RESET,
	CCA_40_MHZ_WA_RECONNECT,
};

/**
 * enum iwl_mld_emlsr_blocked - defines reasons for which EMLSR is blocked
 *
 * These blocks are applied/stored per-VIF.
 *
 * @IWL_MLD_EMLSR_BLOCKED_PREVENTION: Prevent repeated EMLSR enter/exit
 * @IWL_MLD_EMLSR_BLOCKED_WOWLAN: WOWLAN is preventing EMLSR
 * @IWL_MLD_EMLSR_BLOCKED_ROC: remain-on-channel is preventing EMLSR
 * @IWL_MLD_EMLSR_BLOCKED_NON_BSS: An active non-BSS interface's link is
 *      preventing EMLSR
 * @IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS: An expected active non-BSS interface's
 *      link is preventing EMLSR. This is a temporary blocking that is set when
 *      there is an indication that a non-BSS interface is to be added.
 * @IWL_MLD_EMLSR_BLOCKED_TPT: throughput is too low to make EMLSR worthwhile
 */
enum iwl_mld_emlsr_blocked {
	IWL_MLD_EMLSR_BLOCKED_PREVENTION	= 0x1,
	IWL_MLD_EMLSR_BLOCKED_WOWLAN		= 0x2,
	IWL_MLD_EMLSR_BLOCKED_ROC		= 0x4,
	IWL_MLD_EMLSR_BLOCKED_NON_BSS		= 0x8,
	IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS	= 0x10,
	IWL_MLD_EMLSR_BLOCKED_TPT		= 0x20,
};

/**
 * enum iwl_mld_emlsr_exit - defines reasons for exiting EMLSR
 *
 * Reasons to exit EMLSR may be either link specific or even specific to a
 * combination of links.
 *
 * @IWL_MLD_EMLSR_EXIT_BLOCK: Exit due to a block reason being set
 * @IWL_MLD_EMLSR_EXIT_MISSED_BEACON: Exit due to missed beacons
 * @IWL_MLD_EMLSR_EXIT_FAIL_ENTRY: FW failed to enter EMLSR
 * @IWL_MLD_EMLSR_EXIT_CSA: EMLSR prevented due to channel switch on link
 * @IWL_MLD_EMLSR_EXIT_EQUAL_BAND: EMLSR prevented as both links share the band
 * @IWL_MLD_EMLSR_EXIT_LOW_RSSI: Link RSSI is unsuitable for EMLSR
 * @IWL_MLD_EMLSR_EXIT_LINK_USAGE: Exit EMLSR due to low TPT on secondary link
 * @IWL_MLD_EMLSR_EXIT_BT_COEX: Exit EMLSR due to BT coexistence
 * @IWL_MLD_EMLSR_EXIT_CHAN_LOAD: Exit EMLSR because the primary channel is not
 *	loaded enough to justify EMLSR.
 * @IWL_MLD_EMLSR_EXIT_RFI: Exit EMLSR due to RFI
 * @IWL_MLD_EMLSR_EXIT_FW_REQUEST: Exit EMLSR because the FW requested it
 * @IWL_MLD_EMLSR_EXIT_INVALID: internal exit reason due to invalid data
 */
enum iwl_mld_emlsr_exit {
	IWL_MLD_EMLSR_EXIT_BLOCK		= 0x1,
	IWL_MLD_EMLSR_EXIT_MISSED_BEACON	= 0x2,
	IWL_MLD_EMLSR_EXIT_FAIL_ENTRY		= 0x4,
	IWL_MLD_EMLSR_EXIT_CSA			= 0x8,
	IWL_MLD_EMLSR_EXIT_EQUAL_BAND		= 0x10,
	IWL_MLD_EMLSR_EXIT_LOW_RSSI		= 0x20,
	IWL_MLD_EMLSR_EXIT_LINK_USAGE		= 0x40,
	IWL_MLD_EMLSR_EXIT_BT_COEX		= 0x80,
	IWL_MLD_EMLSR_EXIT_CHAN_LOAD		= 0x100,
	IWL_MLD_EMLSR_EXIT_RFI			= 0x200,
	IWL_MLD_EMLSR_EXIT_FW_REQUEST		= 0x400,
	IWL_MLD_EMLSR_EXIT_INVALID		= 0x800,
};

/**
 * struct iwl_mld_emlsr - per-VIF data about EMLSR operation
 *
 * @primary: The current primary link
 * @selected_primary: Primary link as selected during the last link selection
 * @selected_links: Links as selected during the last link selection
 * @blocked_reasons: Reasons preventing EMLSR from being enabled
 * @last_exit_reason: Reason for the last EMLSR exit
 * @last_exit_ts: Time of the last EMLSR exit (if @last_exit_reason is non-zero)
 * @exit_repeat_count: Number of times EMLSR was exited for the same reason
 * @last_entry_ts: the time of the last EMLSR entry (if iwl_mld_emlsr_active()
 *	is true)
 * @unblock_tpt_wk: Unblock EMLSR because the throughput limit was reached
 * @check_tpt_wk: a worker to check if IWL_MLD_EMLSR_BLOCKED_TPT should be
 *	added, for example if there is no longer enough traffic.
 * @prevent_done_wk: Worker to remove %IWL_MLD_EMLSR_BLOCKED_PREVENTION
 * @tmp_non_bss_done_wk: Worker to remove %IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS
 */
struct iwl_mld_emlsr {
	struct_group(zeroed_on_not_authorized,
		u8 primary;

		u8 selected_primary;
		u16 selected_links;

		enum iwl_mld_emlsr_blocked blocked_reasons;

		enum iwl_mld_emlsr_exit last_exit_reason;
		unsigned long last_exit_ts;
		u8 exit_repeat_count;
		unsigned long last_entry_ts;
	);

	struct wiphy_work unblock_tpt_wk;
	struct wiphy_delayed_work check_tpt_wk;

	struct wiphy_delayed_work prevent_done_wk;
	struct wiphy_delayed_work tmp_non_bss_done_wk;
};

/**
 * struct iwl_mld_vif - virtual interface (MAC context) configuration parameters
 *
 * @fw_id: fw id of the mac context.
 * @session_protect: session protection parameters
 * @ap_sta: pointer to AP sta, for easier access to it.
 *	Relevant only for STA vifs.
 * @authorized: indicates the AP station was set to authorized
 * @num_associated_stas: number of associated STAs. Relevant only for AP mode.
 * @ap_ibss_active: whether the AP/IBSS was started
 * @cca_40mhz_workaround: When we are connected in 2.4 GHz and 40 MHz, and the
 *	environment is too loaded, we work around this by reconnecting to the
 *	same AP with 20 MHz. This manages the status of the workaround.
 * @beacon_inject_active: indicates an active debugfs beacon ie injection
 * @low_latency_causes: bit flags, indicating the causes for low-latency,
 *	see @iwl_mld_low_latency_cause.
 * @ps_disabled: indicates that PS is disabled for this interface
 * @last_link_activation_time: last time a link was activated, for
 *	deferring MLO scans (to make them more reliable)
 * @mld: pointer to the mld structure.
 * @deflink: default link data, for use in non-MLO,
 * @link: reference to link data for each valid link, for use in MLO.
 * @emlsr: information related to EMLSR
 * @wowlan_data: data used by the wowlan suspend flow
 * @use_ps_poll: use ps_poll frames
 * @disable_bf: disable beacon filter
 * @dbgfs_slink: debugfs symlink for this interface
 * @roc_activity: the id of the roc_activity running. Relevant for STA and
 *	p2p device only. Set to %ROC_NUM_ACTIVITIES when not in use.
 * @aux_sta: station used for remain on channel. Used in P2P device.
 * @mlo_scan_start_wk: worker to start a deferred MLO scan
 */
struct iwl_mld_vif {
	/* Add here fields that need clean up on restart */
	struct_group(zeroed_on_hw_restart,
		u8 fw_id;
		struct iwl_mld_session_protect session_protect;
		struct ieee80211_sta *ap_sta;
		bool authorized;
		u8 num_associated_stas;
		bool ap_ibss_active;
		enum iwl_mld_cca_40mhz_wa_status cca_40mhz_workaround;
#ifdef CONFIG_IWLWIFI_DEBUGFS
		bool beacon_inject_active;
#endif
		u8 low_latency_causes;
		bool ps_disabled;
		time64_t last_link_activation_time;
	);
	/* And here fields that survive a fw restart */
	struct iwl_mld *mld;
	struct iwl_mld_link deflink;
	struct iwl_mld_link __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];

	struct iwl_mld_emlsr emlsr;

#ifdef CONFIG_PM_SLEEP
	struct iwl_mld_wowlan_data wowlan_data;
#endif
#ifdef CONFIG_IWLWIFI_DEBUGFS
	bool use_ps_poll;
	bool disable_bf;
	struct dentry *dbgfs_slink;
#endif
	enum iwl_roc_activity roc_activity;
	struct iwl_mld_int_sta aux_sta;

	struct wiphy_delayed_work mlo_scan_start_wk;
};

static inline struct iwl_mld_vif *
iwl_mld_vif_from_mac80211(struct ieee80211_vif *vif)
{
	return (void *)vif->drv_priv;
}

static inline struct ieee80211_vif *
iwl_mld_vif_to_mac80211(struct iwl_mld_vif *mld_vif)
{
	return container_of((void *)mld_vif, struct ieee80211_vif, drv_priv);
}

#define iwl_mld_link_dereference_check(mld_vif, link_id)		\
	rcu_dereference_check((mld_vif)->link[link_id],			\
			      lockdep_is_held(&mld_vif->mld->wiphy->mtx))

#define for_each_mld_vif_valid_link(mld_vif, mld_link)			\
	for (int link_id = 0; link_id < ARRAY_SIZE((mld_vif)->link);	\
	     link_id++)							\
		if ((mld_link = iwl_mld_link_dereference_check(mld_vif, link_id)))

/* Retrieve pointer to mld link from mac80211 structures */
static inline struct iwl_mld_link *
iwl_mld_link_from_mac80211(struct ieee80211_bss_conf *bss_conf)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(bss_conf->vif);

	return iwl_mld_link_dereference_check(mld_vif, bss_conf->link_id);
}

int iwl_mld_mac80211_iftype_to_fw(const struct ieee80211_vif *vif);

/* Cleanup function for struct iwl_mld_vif, will be called in restart */
void iwl_mld_cleanup_vif(void *data, u8 *mac, struct ieee80211_vif *vif);
int iwl_mld_mac_fw_action(struct iwl_mld *mld, struct ieee80211_vif *vif,
			  u32 action);
int iwl_mld_add_vif(struct iwl_mld *mld, struct ieee80211_vif *vif);
void iwl_mld_rm_vif(struct iwl_mld *mld, struct ieee80211_vif *vif);
void iwl_mld_set_vif_associated(struct iwl_mld *mld,
				struct ieee80211_vif *vif);
u8 iwl_mld_get_fw_bss_vifs_ids(struct iwl_mld *mld);
void iwl_mld_handle_probe_resp_data_notif(struct iwl_mld *mld,
					  struct iwl_rx_packet *pkt);

void iwl_mld_handle_datapath_monitor_notif(struct iwl_mld *mld,
					   struct iwl_rx_packet *pkt);

void iwl_mld_handle_uapsd_misbehaving_ap_notif(struct iwl_mld *mld,
					       struct iwl_rx_packet *pkt);

void iwl_mld_reset_cca_40mhz_workaround(struct iwl_mld *mld,
					struct ieee80211_vif *vif);

static inline bool iwl_mld_vif_low_latency(const struct iwl_mld_vif *mld_vif)
{
	return !!mld_vif->low_latency_causes;
}

struct ieee80211_vif *iwl_mld_get_bss_vif(struct iwl_mld *mld);

#endif /* __iwl_mld_iface_h__ */
