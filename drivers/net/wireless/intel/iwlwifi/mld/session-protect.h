/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#ifndef __session_protect_h__
#define __session_protect_h__

#include "mld.h"
#include "hcmd.h"
#include <net/mac80211.h>
#include "fw/api/mac-cfg.h"

/**
 * DOC: session protection
 *
 * Session protection is an API from the firmware that allows the driver to
 * request time on medium. This is needed before the association when we need
 * to be on medium for the association frame exchange. Once we configure the
 * firmware as 'associated', the firmware will allocate time on medium without
 * needed a session protection.
 *
 * TDLS discover uses this API as well even after association to ensure that
 * other activities internal to the firmware will not interrupt our presence
 * on medium.
 */

/**
 * struct iwl_mld_session_protect - session protection parameters
 * @end_jiffies: expected end_jiffies of current session protection.
 *	0 if not active
 * @duration: the duration in tu of current session
 * @session_requested: A session protection command was sent and wasn't yet
 *	answered
 */
struct iwl_mld_session_protect {
	unsigned long end_jiffies;
	u32 duration;
	bool session_requested;
};

#define IWL_MLD_SESSION_PROTECTION_ASSOC_TIME_MS 900
#define IWL_MLD_SESSION_PROTECTION_MIN_TIME_MS 400

/**
 * iwl_mld_handle_session_prot_notif - handles %SESSION_PROTECTION_NOTIF
 * @mld: the mld component
 * @pkt: the RX packet containing the notification
 */
void iwl_mld_handle_session_prot_notif(struct iwl_mld *mld,
				       struct iwl_rx_packet *pkt);

/**
 * iwl_mld_schedule_session_protection - schedule a session protection
 * @mld: the mld component
 * @vif: the virtual interface for which the protection issued
 * @duration: the requested duration of the protection
 * @min_duration: the minimum duration of the protection
 * @link_id: The link to schedule a session protection for
 */
void iwl_mld_schedule_session_protection(struct iwl_mld *mld,
					 struct ieee80211_vif *vif,
					 u32 duration, u32 min_duration,
					 int link_id);

/**
 * iwl_mld_start_session_protection - start a session protection
 * @mld: the mld component
 * @vif: the virtual interface for which the protection issued
 * @duration: the requested duration of the protection
 * @min_duration: the minimum duration of the protection
 * @link_id: The link to schedule a session protection for
 * @timeout: timeout for waiting
 *
 * This schedules the session protection, and waits for it to start
 * (with timeout)
 *
 * Returns: 0 if successful, error code otherwise
 */
int iwl_mld_start_session_protection(struct iwl_mld *mld,
				     struct ieee80211_vif *vif,
				     u32 duration, u32 min_duration,
				     int link_id, unsigned long timeout);

/**
 * iwl_mld_cancel_session_protection - cancel the session protection.
 * @mld: the mld component
 * @vif: the virtual interface for which the session is issued
 * @link_id: cancel the session protection for given link
 *
 * This functions cancels the session protection which is an act of good
 * citizenship. If it is not needed any more it should be canceled because
 * the other mac contexts wait for the medium during that time.
 *
 * Returns: 0 if successful, error code otherwise
 *
 */
int iwl_mld_cancel_session_protection(struct iwl_mld *mld,
				      struct ieee80211_vif *vif,
				      int link_id);

#endif /* __session_protect_h__ */
