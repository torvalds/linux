/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2020 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2020 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef __iwl_fw_api_time_event_h__
#define __iwl_fw_api_time_event_h__

#include "fw/api/phy-ctxt.h"

/* Time Event types, according to MAC type */
enum iwl_time_event_type {
	/* BSS Station Events */
	TE_BSS_STA_AGGRESSIVE_ASSOC,
	TE_BSS_STA_ASSOC,
	TE_BSS_EAP_DHCP_PROT,
	TE_BSS_QUIET_PERIOD,

	/* P2P Device Events */
	TE_P2P_DEVICE_DISCOVERABLE,
	TE_P2P_DEVICE_LISTEN,
	TE_P2P_DEVICE_ACTION_SCAN,
	TE_P2P_DEVICE_FULL_SCAN,

	/* P2P Client Events */
	TE_P2P_CLIENT_AGGRESSIVE_ASSOC,
	TE_P2P_CLIENT_ASSOC,
	TE_P2P_CLIENT_QUIET_PERIOD,

	/* P2P GO Events */
	TE_P2P_GO_ASSOC_PROT,
	TE_P2P_GO_REPETITIVET_NOA,
	TE_P2P_GO_CT_WINDOW,

	/* WiDi Sync Events */
	TE_WIDI_TX_SYNC,

	/* Channel Switch NoA */
	TE_CHANNEL_SWITCH_PERIOD,

	TE_MAX
}; /* MAC_EVENT_TYPE_API_E_VER_1 */

/* Time event - defines for command API v1 */

/*
 * @TE_V1_FRAG_NONE: fragmentation of the time event is NOT allowed.
 * @TE_V1_FRAG_SINGLE: fragmentation of the time event is allowed, but only
 *	the first fragment is scheduled.
 * @TE_V1_FRAG_DUAL: fragmentation of the time event is allowed, but only
 *	the first 2 fragments are scheduled.
 * @TE_V1_FRAG_ENDLESS: fragmentation of the time event is allowed, and any
 *	number of fragments are valid.
 *
 * Other than the constant defined above, specifying a fragmentation value 'x'
 * means that the event can be fragmented but only the first 'x' will be
 * scheduled.
 */
enum {
	TE_V1_FRAG_NONE = 0,
	TE_V1_FRAG_SINGLE = 1,
	TE_V1_FRAG_DUAL = 2,
	TE_V1_FRAG_ENDLESS = 0xffffffff
};

/* If a Time Event can be fragmented, this is the max number of fragments */
#define TE_V1_FRAG_MAX_MSK	0x0fffffff
/* Repeat the time event endlessly (until removed) */
#define TE_V1_REPEAT_ENDLESS	0xffffffff
/* If a Time Event has bounded repetitions, this is the maximal value */
#define TE_V1_REPEAT_MAX_MSK_V1	0x0fffffff

/* Time Event dependencies: none, on another TE, or in a specific time */
enum {
	TE_V1_INDEPENDENT		= 0,
	TE_V1_DEP_OTHER			= BIT(0),
	TE_V1_DEP_TSF			= BIT(1),
	TE_V1_EVENT_SOCIOPATHIC		= BIT(2),
}; /* MAC_EVENT_DEPENDENCY_POLICY_API_E_VER_2 */

/*
 * @TE_V1_NOTIF_NONE: no notifications
 * @TE_V1_NOTIF_HOST_EVENT_START: request/receive notification on event start
 * @TE_V1_NOTIF_HOST_EVENT_END:request/receive notification on event end
 * @TE_V1_NOTIF_INTERNAL_EVENT_START: internal FW use
 * @TE_V1_NOTIF_INTERNAL_EVENT_END: internal FW use.
 * @TE_V1_NOTIF_HOST_FRAG_START: request/receive notification on frag start
 * @TE_V1_NOTIF_HOST_FRAG_END:request/receive notification on frag end
 * @TE_V1_NOTIF_INTERNAL_FRAG_START: internal FW use.
 * @TE_V1_NOTIF_INTERNAL_FRAG_END: internal FW use.
 *
 * Supported Time event notifications configuration.
 * A notification (both event and fragment) includes a status indicating weather
 * the FW was able to schedule the event or not. For fragment start/end
 * notification the status is always success. There is no start/end fragment
 * notification for monolithic events.
 */
enum {
	TE_V1_NOTIF_NONE = 0,
	TE_V1_NOTIF_HOST_EVENT_START = BIT(0),
	TE_V1_NOTIF_HOST_EVENT_END = BIT(1),
	TE_V1_NOTIF_INTERNAL_EVENT_START = BIT(2),
	TE_V1_NOTIF_INTERNAL_EVENT_END = BIT(3),
	TE_V1_NOTIF_HOST_FRAG_START = BIT(4),
	TE_V1_NOTIF_HOST_FRAG_END = BIT(5),
	TE_V1_NOTIF_INTERNAL_FRAG_START = BIT(6),
	TE_V1_NOTIF_INTERNAL_FRAG_END = BIT(7),
}; /* MAC_EVENT_ACTION_API_E_VER_2 */

/* Time event - defines for command API */

/*
 * @TE_V2_FRAG_NONE: fragmentation of the time event is NOT allowed.
 * @TE_V2_FRAG_SINGLE: fragmentation of the time event is allowed, but only
 *  the first fragment is scheduled.
 * @TE_V2_FRAG_DUAL: fragmentation of the time event is allowed, but only
 *  the first 2 fragments are scheduled.
 * @TE_V2_FRAG_ENDLESS: fragmentation of the time event is allowed, and any
 *  number of fragments are valid.
 *
 * Other than the constant defined above, specifying a fragmentation value 'x'
 * means that the event can be fragmented but only the first 'x' will be
 * scheduled.
 */
enum {
	TE_V2_FRAG_NONE = 0,
	TE_V2_FRAG_SINGLE = 1,
	TE_V2_FRAG_DUAL = 2,
	TE_V2_FRAG_MAX = 0xfe,
	TE_V2_FRAG_ENDLESS = 0xff
};

/* Repeat the time event endlessly (until removed) */
#define TE_V2_REPEAT_ENDLESS	0xff
/* If a Time Event has bounded repetitions, this is the maximal value */
#define TE_V2_REPEAT_MAX	0xfe

#define TE_V2_PLACEMENT_POS	12
#define TE_V2_ABSENCE_POS	15

/**
 * enum iwl_time_event_policy - Time event policy values
 * A notification (both event and fragment) includes a status indicating weather
 * the FW was able to schedule the event or not. For fragment start/end
 * notification the status is always success. There is no start/end fragment
 * notification for monolithic events.
 *
 * @TE_V2_DEFAULT_POLICY: independent, social, present, unoticable
 * @TE_V2_NOTIF_HOST_EVENT_START: request/receive notification on event start
 * @TE_V2_NOTIF_HOST_EVENT_END:request/receive notification on event end
 * @TE_V2_NOTIF_INTERNAL_EVENT_START: internal FW use
 * @TE_V2_NOTIF_INTERNAL_EVENT_END: internal FW use.
 * @TE_V2_NOTIF_HOST_FRAG_START: request/receive notification on frag start
 * @TE_V2_NOTIF_HOST_FRAG_END:request/receive notification on frag end
 * @TE_V2_NOTIF_INTERNAL_FRAG_START: internal FW use.
 * @TE_V2_NOTIF_INTERNAL_FRAG_END: internal FW use.
 * @TE_V2_START_IMMEDIATELY: start time event immediately
 * @TE_V2_DEP_OTHER: depends on another time event
 * @TE_V2_DEP_TSF: depends on a specific time
 * @TE_V2_EVENT_SOCIOPATHIC: can't co-exist with other events of tha same MAC
 * @TE_V2_ABSENCE: are we present or absent during the Time Event.
 */
enum iwl_time_event_policy {
	TE_V2_DEFAULT_POLICY = 0x0,

	/* notifications (event start/stop, fragment start/stop) */
	TE_V2_NOTIF_HOST_EVENT_START = BIT(0),
	TE_V2_NOTIF_HOST_EVENT_END = BIT(1),
	TE_V2_NOTIF_INTERNAL_EVENT_START = BIT(2),
	TE_V2_NOTIF_INTERNAL_EVENT_END = BIT(3),

	TE_V2_NOTIF_HOST_FRAG_START = BIT(4),
	TE_V2_NOTIF_HOST_FRAG_END = BIT(5),
	TE_V2_NOTIF_INTERNAL_FRAG_START = BIT(6),
	TE_V2_NOTIF_INTERNAL_FRAG_END = BIT(7),
	TE_V2_START_IMMEDIATELY = BIT(11),

	/* placement characteristics */
	TE_V2_DEP_OTHER = BIT(TE_V2_PLACEMENT_POS),
	TE_V2_DEP_TSF = BIT(TE_V2_PLACEMENT_POS + 1),
	TE_V2_EVENT_SOCIOPATHIC = BIT(TE_V2_PLACEMENT_POS + 2),

	/* are we present or absent during the Time Event. */
	TE_V2_ABSENCE = BIT(TE_V2_ABSENCE_POS),
};

/**
 * struct iwl_time_event_cmd - configuring Time Events
 * with struct MAC_TIME_EVENT_DATA_API_S_VER_2 (see also
 * with version 1. determined by IWL_UCODE_TLV_FLAGS)
 * ( TIME_EVENT_CMD = 0x29 )
 * @id_and_color: ID and color of the relevant MAC,
 *	&enum iwl_ctxt_id_and_color
 * @action: action to perform, one of &enum iwl_ctxt_action
 * @id: this field has two meanings, depending on the action:
 *	If the action is ADD, then it means the type of event to add.
 *	For all other actions it is the unique event ID assigned when the
 *	event was added by the FW.
 * @apply_time: When to start the Time Event (in GP2)
 * @max_delay: maximum delay to event's start (apply time), in TU
 * @depends_on: the unique ID of the event we depend on (if any)
 * @interval: interval between repetitions, in TU
 * @duration: duration of event in TU
 * @repeat: how many repetitions to do, can be TE_REPEAT_ENDLESS
 * @max_frags: maximal number of fragments the Time Event can be divided to
 * @policy: defines whether uCode shall notify the host or other uCode modules
 *	on event and/or fragment start and/or end
 *	using one of TE_INDEPENDENT, TE_DEP_OTHER, TE_DEP_TSF
 *	TE_EVENT_SOCIOPATHIC
 *	using TE_ABSENCE and using TE_NOTIF_*,
 *	&enum iwl_time_event_policy
 */
struct iwl_time_event_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	__le32 id_and_color;
	__le32 action;
	__le32 id;
	/* MAC_TIME_EVENT_DATA_API_S_VER_2 */
	__le32 apply_time;
	__le32 max_delay;
	__le32 depends_on;
	__le32 interval;
	__le32 duration;
	u8 repeat;
	u8 max_frags;
	__le16 policy;
} __packed; /* MAC_TIME_EVENT_CMD_API_S_VER_2 */

/**
 * struct iwl_time_event_resp - response structure to iwl_time_event_cmd
 * @status: bit 0 indicates success, all others specify errors
 * @id: the Time Event type
 * @unique_id: the unique ID assigned (in ADD) or given (others) to the TE
 * @id_and_color: ID and color of the relevant MAC,
 *	&enum iwl_ctxt_id_and_color
 */
struct iwl_time_event_resp {
	__le32 status;
	__le32 id;
	__le32 unique_id;
	__le32 id_and_color;
} __packed; /* MAC_TIME_EVENT_RSP_API_S_VER_1 */

/**
 * struct iwl_time_event_notif - notifications of time event start/stop
 * ( TIME_EVENT_NOTIFICATION = 0x2a )
 * @timestamp: action timestamp in GP2
 * @session_id: session's unique id
 * @unique_id: unique id of the Time Event itself
 * @id_and_color: ID and color of the relevant MAC
 * @action: &enum iwl_time_event_policy
 * @status: true if scheduled, false otherwise (not executed)
 */
struct iwl_time_event_notif {
	__le32 timestamp;
	__le32 session_id;
	__le32 unique_id;
	__le32 id_and_color;
	__le32 action;
	__le32 status;
} __packed; /* MAC_TIME_EVENT_NTFY_API_S_VER_1 */

/*
 * struct iwl_hs20_roc_req_tail - tail of iwl_hs20_roc_req
 *
 * @node_addr: Our MAC Address
 * @reserved: reserved for alignment
 * @apply_time: GP2 value to start (should always be the current GP2 value)
 * @apply_time_max_delay: Maximum apply time delay value in TU. Defines max
 *	time by which start of the event is allowed to be postponed.
 * @duration: event duration in TU To calculate event duration:
 *	timeEventDuration = min(duration, remainingQuota)
 */
struct iwl_hs20_roc_req_tail {
	u8 node_addr[ETH_ALEN];
	__le16 reserved;
	__le32 apply_time;
	__le32 apply_time_max_delay;
	__le32 duration;
} __packed;

/*
 * Aux ROC command
 *
 * Command requests the firmware to create a time event for a certain duration
 * and remain on the given channel. This is done by using the Aux framework in
 * the FW.
 * The command was first used for Hot Spot issues - but can be used regardless
 * to Hot Spot.
 *
 * ( HOT_SPOT_CMD 0x53 )
 *
 * @id_and_color: ID and color of the MAC
 * @action: action to perform, one of FW_CTXT_ACTION_*
 * @event_unique_id: If the action FW_CTXT_ACTION_REMOVE then the
 *	event_unique_id should be the id of the time event assigned by ucode.
 *	Otherwise ignore the event_unique_id.
 * @sta_id_and_color: station id and color, resumed during "Remain On Channel"
 *	activity.
 * @channel_info: channel info
 */
struct iwl_hs20_roc_req {
	/* COMMON_INDEX_HDR_API_S_VER_1 hdr */
	__le32 id_and_color;
	__le32 action;
	__le32 event_unique_id;
	__le32 sta_id_and_color;
	struct iwl_fw_channel_info channel_info;
	struct iwl_hs20_roc_req_tail tail;
} __packed; /* HOT_SPOT_CMD_API_S_VER_1 */

/*
 * values for AUX ROC result values
 */
enum iwl_mvm_hot_spot {
	HOT_SPOT_RSP_STATUS_OK,
	HOT_SPOT_RSP_STATUS_TOO_MANY_EVENTS,
	HOT_SPOT_MAX_NUM_OF_SESSIONS,
};

/*
 * Aux ROC command response
 *
 * In response to iwl_hs20_roc_req the FW sends this command to notify the
 * driver the uid of the timevent.
 *
 * ( HOT_SPOT_CMD 0x53 )
 *
 * @event_unique_id: Unique ID of time event assigned by ucode
 * @status: Return status 0 is success, all the rest used for specific errors
 */
struct iwl_hs20_roc_res {
	__le32 event_unique_id;
	__le32 status;
} __packed; /* HOT_SPOT_RSP_API_S_VER_1 */

/**
 * enum iwl_mvm_session_prot_conf_id - session protection's configurations
 * @SESSION_PROTECT_CONF_ASSOC: Start a session protection for association.
 *	The firmware will allocate two events.
 *	Valid for BSS_STA and P2P_STA.
 *	* A rather short event that can't be fragmented and with a very
 *	high priority. If every goes well (99% of the cases) the
 *	association should complete within this first event. During
 *	that event, no other activity will happen in the firmware,
 *	which is why it can't be too long.
 *	The length of this event is hard-coded in the firmware: 300TUs.
 *	* Another event which can be much longer (it's duration is
 *	configurable by the driver) which has a slightly lower
 *	priority and that can be fragmented allowing other activities
 *	to run while this event is running.
 *	The firmware will automatically remove both events once the driver sets
 *	the BSS MAC as associated. Neither of the events will be removed
 *	for the P2P_STA MAC.
 *	Only the duration is configurable for this protection.
 * @SESSION_PROTECT_CONF_GO_CLIENT_ASSOC: not used
 * @SESSION_PROTECT_CONF_P2P_DEVICE_DISCOV: Schedule the P2P Device to be in
 *	listen mode. Will be fragmented. Valid only on the P2P Device MAC.
 *	Valid only on the P2P Device MAC. The firmware will take into account
 *	the duration, the interval and the repetition count.
 * @SESSION_PROTECT_CONF_P2P_GO_NEGOTIATION: Schedule the P2P Device to be be
 *	able to run the GO Negotiation. Will not be fragmented and not
 *	repetitive. Valid only on the P2P Device MAC. Only the duration will
 *	be taken into account.
 * @SESSION_PROTECT_CONF_MAX_ID: not used
 */
enum iwl_mvm_session_prot_conf_id {
	SESSION_PROTECT_CONF_ASSOC,
	SESSION_PROTECT_CONF_GO_CLIENT_ASSOC,
	SESSION_PROTECT_CONF_P2P_DEVICE_DISCOV,
	SESSION_PROTECT_CONF_P2P_GO_NEGOTIATION,
	SESSION_PROTECT_CONF_MAX_ID,
}; /* SESSION_PROTECTION_CONF_ID_E_VER_1 */

/**
 * struct iwl_mvm_session_prot_cmd - configure a session protection
 * @id_and_color: the id and color of the mac for which this session protection
 *	is sent
 * @action: can be either FW_CTXT_ACTION_ADD or FW_CTXT_ACTION_REMOVE
 * @conf_id: see &enum iwl_mvm_session_prot_conf_id
 * @duration_tu: the duration of the whole protection in TUs.
 * @repetition_count: not used
 * @interval: not used
 *
 * Note: the session protection will always be scheduled to start as
 * early as possible, but the maximum delay is configuration dependent.
 * The firmware supports only one concurrent session protection per vif.
 * Adding a new session protection will remove any currently running session.
 */
struct iwl_mvm_session_prot_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 hdr */
	__le32 id_and_color;
	__le32 action;
	__le32 conf_id;
	__le32 duration_tu;
	__le32 repetition_count;
	__le32 interval;
} __packed; /* SESSION_PROTECTION_CMD_API_S_VER_1 */

/**
 * struct iwl_mvm_session_prot_notif - session protection started / ended
 * @mac_id: the mac id for which the session protection started / ended
 * @status: 1 means success, 0 means failure
 * @start: 1 means the session protection started, 0 means it ended
 * @conf_id: see &enum iwl_mvm_session_prot_conf_id
 *
 * Note that any session protection will always get two notifications: start
 * and end even the firmware could not schedule it.
 */
struct iwl_mvm_session_prot_notif {
	__le32 mac_id;
	__le32 status;
	__le32 start;
	__le32 conf_id;
} __packed; /* SESSION_PROTECTION_NOTIFICATION_API_S_VER_2 */

#endif /* __iwl_fw_api_time_event_h__ */
