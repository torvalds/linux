/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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

#ifndef __sta_h__
#define __sta_h__

#include <linux/spinlock.h>
#include <net/mac80211.h>
#include <linux/wait.h>

#include "iwl-trans.h" /* for IWL_MAX_TID_COUNT */
#include "fw-api.h" /* IWL_MVM_STATION_COUNT */
#include "rs.h"

struct iwl_mvm;
struct iwl_mvm_vif;

/**
 * DOC: station table - introduction
 *
 * The station table is a list of data structure that reprensent the stations.
 * In STA/P2P client mode, the driver will hold one station for the AP/ GO.
 * In GO/AP mode, the driver will have as many stations as associated clients.
 * All these stations are reflected in the fw's station table. The driver
 * keeps the fw's station table up to date with the ADD_STA command. Stations
 * can be removed by the REMOVE_STA command.
 *
 * All the data related to a station is held in the structure %iwl_mvm_sta
 * which is embed in the mac80211's %ieee80211_sta (in the drv_priv) area.
 * This data includes the index of the station in the fw, per tid information
 * (sequence numbers, Block-ack state machine, etc...). The stations are
 * created and deleted by the %sta_state callback from %ieee80211_ops.
 *
 * The driver holds a map: %fw_id_to_mac_id that allows to fetch a
 * %ieee80211_sta (and the %iwl_mvm_sta embedded into it) based on a fw
 * station index. That way, the driver is able to get the tid related data in
 * O(1) in time sensitive paths (Tx / Tx response / BA notification). These
 * paths are triggered by the fw, and the driver needs to get a pointer to the
 * %ieee80211 structure. This map helps to get that pointer quickly.
 */

/**
 * DOC: station table - locking
 *
 * As stated before, the station is created / deleted by mac80211's %sta_state
 * callback from %ieee80211_ops which can sleep. The next paragraph explains
 * the locking of a single stations, the next ones relates to the station
 * table.
 *
 * The station holds the sequence number per tid. So this data needs to be
 * accessed in the Tx path (which is softIRQ). It also holds the Block-Ack
 * information (the state machine / and the logic that checks if the queues
 * were drained), so it also needs to be accessible from the Tx response flow.
 * In short, the station needs to be access from sleepable context as well as
 * from tasklets, so the station itself needs a spinlock.
 *
 * The writers of %fw_id_to_mac_id map are serialized by the global mutex of
 * the mvm op_mode. This is possible since %sta_state can sleep.
 * The pointers in this map are RCU protected, hence we won't replace the
 * station while we have Tx / Tx response / BA notification running.
 *
 * If a station is deleted while it still has packets in its A-MPDU queues,
 * then the reclaim flow will notice that there is no station in the map for
 * sta_id and it will dump the responses.
 */

/**
 * DOC: station table - internal stations
 *
 * The FW needs a few internal stations that are not reflected in
 * mac80211, such as broadcast station in AP / GO mode, or AUX sta for
 * scanning and P2P device (during the GO negotiation).
 * For these kind of stations we have %iwl_mvm_int_sta struct which holds the
 * data relevant for them from both %iwl_mvm_sta and %ieee80211_sta.
 * Usually the data for these stations is static, so no locking is required,
 * and no TID data as this is also not needed.
 * One thing to note, is that these stations have an ID in the fw, but not
 * in mac80211. In order to "reserve" them a sta_id in %fw_id_to_mac_id
 * we fill ERR_PTR(EINVAL) in this mapping and all other dereferencing of
 * pointers from this mapping need to check that the value is not error
 * or NULL.
 *
 * Currently there is only one auxiliary station for scanning, initialized
 * on init.
 */

/**
 * DOC: station table - AP Station in STA mode
 *
 * %iwl_mvm_vif includes the index of the AP station in the fw's STA table:
 * %ap_sta_id. To get the point to the corresponding %ieee80211_sta,
 * &fw_id_to_mac_id can be used. Due to the way the fw works, we must not remove
 * the AP station from the fw before setting the MAC context as unassociated.
 * Hence, %fw_id_to_mac_id[%ap_sta_id] will be NULLed when the AP station is
 * removed by mac80211, but the station won't be removed in the fw until the
 * VIF is set as unassociated. Then, %ap_sta_id will be invalidated.
 */

/**
 * DOC: station table - Drain vs. Flush
 *
 * Flush means that all the frames in the SCD queue are dumped regardless the
 * station to which they were sent. We do that when we disassociate and before
 * we remove the STA of the AP. The flush can be done synchronously against the
 * fw.
 * Drain means that the fw will drop all the frames sent to a specific station.
 * This is useful when a client (if we are IBSS / GO or AP) disassociates. In
 * that case, we need to drain all the frames for that client from the AC queues
 * that are shared with the other clients. Only then, we can remove the STA in
 * the fw. In order to do so, we track the non-AMPDU packets for each station.
 * If mac80211 removes a STA and if it still has non-AMPDU packets pending in
 * the queues, we mark this station as %EBUSY in %fw_id_to_mac_id, and drop all
 * the frames for this STA (%iwl_mvm_rm_sta). When the last frame is dropped
 * (we know about it with its Tx response), we remove the station in fw and set
 * it as %NULL in %fw_id_to_mac_id: this is the purpose of
 * %iwl_mvm_sta_drained_wk.
 */

/**
 * DOC: station table - fw restart
 *
 * When the fw asserts, or we have any other issue that requires to reset the
 * driver, we require mac80211 to reconfigure the driver. Since the private
 * data of the stations is embed in mac80211's %ieee80211_sta, that data will
 * not be zeroed and needs to be reinitialized manually.
 * %IWL_MVM_STATUS_IN_HW_RESTART is set during restart and that will hint us
 * that we must not allocate a new sta_id but reuse the previous one. This
 * means that the stations being re-added after the reset will have the same
 * place in the fw as before the reset. We do need to zero the %fw_id_to_mac_id
 * map, since the stations aren't in the fw any more. Internal stations that
 * are not added by mac80211 will be re-added in the init flow that is called
 * after the restart: mac80211 call's %iwl_mvm_mac_start which calls to
 * %iwl_mvm_up.
 */

/**
 * DOC: AP mode - PS
 *
 * When a station is asleep, the fw will set it as "asleep". All frames on
 * shared queues (i.e. non-aggregation queues) to that station will be dropped
 * by the fw (%TX_STATUS_FAIL_DEST_PS failure code).
 *
 * AMPDUs are in a separate queue that is stopped by the fw. We just need to
 * let mac80211 know when there are frames in these queues so that it can
 * properly handle trigger frames.
 *
 * When a trigger frame is received, mac80211 tells the driver to send frames
 * from the AMPDU queues or sends frames to non-aggregation queues itself,
 * depending on which ACs are delivery-enabled and what TID has frames to
 * transmit. Note that mac80211 has all the knowledge since all the non-agg
 * frames are buffered / filtered, and the driver tells mac80211 about agg
 * frames). The driver needs to tell the fw to let frames out even if the
 * station is asleep. This is done by %iwl_mvm_sta_modify_sleep_tx_count.
 *
 * When we receive a frame from that station with PM bit unset, the driver
 * needs to let the fw know that this station isn't asleep any more. This is
 * done by %iwl_mvm_sta_modify_ps_wake in response to mac80211 signaling the
 * station's wakeup.
 *
 * For a GO, the Service Period might be cut short due to an absence period
 * of the GO. In this (and all other cases) the firmware notifies us with the
 * EOSP_NOTIFICATION, and we notify mac80211 of that. Further frames that we
 * already sent to the device will be rejected again.
 *
 * See also "AP support for powersaving clients" in mac80211.h.
 */

/**
 * enum iwl_mvm_agg_state
 *
 * The state machine of the BA agreement establishment / tear down.
 * These states relate to a specific RA / TID.
 *
 * @IWL_AGG_OFF: aggregation is not used
 * @IWL_AGG_STARTING: aggregation are starting (between start and oper)
 * @IWL_AGG_ON: aggregation session is up
 * @IWL_EMPTYING_HW_QUEUE_ADDBA: establishing a BA session - waiting for the
 *	HW queue to be empty from packets for this RA /TID.
 * @IWL_EMPTYING_HW_QUEUE_DELBA: tearing down a BA session - waiting for the
 *	HW queue to be empty from packets for this RA /TID.
 */
enum iwl_mvm_agg_state {
	IWL_AGG_OFF = 0,
	IWL_AGG_STARTING,
	IWL_AGG_ON,
	IWL_EMPTYING_HW_QUEUE_ADDBA,
	IWL_EMPTYING_HW_QUEUE_DELBA,
};

/**
 * struct iwl_mvm_tid_data - holds the states for each RA / TID
 * @seq_number: the next WiFi sequence number to use
 * @next_reclaimed: the WiFi sequence number of the next packet to be acked.
 *	This is basically (last acked packet++).
 * @rate_n_flags: Rate at which Tx was attempted. Holds the data between the
 *	Tx response (TX_CMD), and the block ack notification (COMPRESSED_BA).
 * @reduced_tpc: Reduced tx power. Holds the data between the
 *	Tx response (TX_CMD), and the block ack notification (COMPRESSED_BA).
 * @state: state of the BA agreement establishment / tear down.
 * @txq_id: Tx queue used by the BA session
 * @ssn: the first packet to be sent in AGG HW queue in Tx AGG start flow, or
 *	the first packet to be sent in legacy HW queue in Tx AGG stop flow.
 *	Basically when next_reclaimed reaches ssn, we can tell mac80211 that
 *	we are ready to finish the Tx AGG stop / start flow.
 * @tx_time: medium time consumed by this A-MPDU
 */
struct iwl_mvm_tid_data {
	u16 seq_number;
	u16 next_reclaimed;
	/* The rest is Tx AGG related */
	u32 rate_n_flags;
	u8 reduced_tpc;
	enum iwl_mvm_agg_state state;
	u16 txq_id;
	u16 ssn;
	u16 tx_time;
};

static inline u16 iwl_mvm_tid_queued(struct iwl_mvm_tid_data *tid_data)
{
	return ieee80211_sn_sub(IEEE80211_SEQ_TO_SN(tid_data->seq_number),
				tid_data->next_reclaimed);
}

/**
 * struct iwl_mvm_sta - representation of a station in the driver
 * @sta_id: the index of the station in the fw (will be replaced by id_n_color)
 * @tfd_queue_msk: the tfd queues used by the station
 * @hw_queue: per-AC mapping of the TFD queues used by station
 * @mac_id_n_color: the MAC context this station is linked to
 * @tid_disable_agg: bitmap: if bit(tid) is set, the fw won't send ampdus for
 *	tid.
 * @max_agg_bufsize: the maximal size of the AGG buffer for this station
 * @bt_reduced_txpower: is reduced tx power enabled for this station
 * @next_status_eosp: the next reclaimed packet is a PS-Poll response and
 *	we need to signal the EOSP
 * @lock: lock to protect the whole struct. Since %tid_data is access from Tx
 * and from Tx response flow, it needs a spinlock.
 * @tid_data: per tid data. Look at %iwl_mvm_tid_data.
 * @tx_protection: reference counter for controlling the Tx protection.
 * @tt_tx_protection: is thermal throttling enable Tx protection?
 * @disable_tx: is tx to this STA disabled?
 * @agg_tids: bitmap of tids whose status is operational aggregated (IWL_AGG_ON)
 *
 * When mac80211 creates a station it reserves some space (hw->sta_data_size)
 * in the structure for use by driver. This structure is placed in that
 * space.
 *
 */
struct iwl_mvm_sta {
	u32 sta_id;
	u32 tfd_queue_msk;
	u8 hw_queue[IEEE80211_NUM_ACS];
	u32 mac_id_n_color;
	u16 tid_disable_agg;
	u8 max_agg_bufsize;
	bool bt_reduced_txpower;
	bool next_status_eosp;
	spinlock_t lock;
	struct iwl_mvm_tid_data tid_data[IWL_MAX_TID_COUNT];
	struct iwl_lq_sta lq_sta;
	struct ieee80211_vif *vif;

	/* Temporary, until the new TLC will control the Tx protection */
	s8 tx_protection;
	bool tt_tx_protection;

	bool disable_tx;
	u8 agg_tids;
};

static inline struct iwl_mvm_sta *
iwl_mvm_sta_from_mac80211(struct ieee80211_sta *sta)
{
	return (void *)sta->drv_priv;
}

/**
 * struct iwl_mvm_int_sta - representation of an internal station (auxiliary or
 * broadcast)
 * @sta_id: the index of the station in the fw (will be replaced by id_n_color)
 * @tfd_queue_msk: the tfd queues used by the station
 */
struct iwl_mvm_int_sta {
	u32 sta_id;
	u32 tfd_queue_msk;
};

int iwl_mvm_sta_send_to_fw(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			   bool update);
int iwl_mvm_add_sta(struct iwl_mvm *mvm,
		    struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta);
int iwl_mvm_update_sta(struct iwl_mvm *mvm,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
int iwl_mvm_rm_sta(struct iwl_mvm *mvm,
		   struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
int iwl_mvm_rm_sta_id(struct iwl_mvm *mvm,
		      struct ieee80211_vif *vif,
		      u8 sta_id);
int iwl_mvm_set_sta_key(struct iwl_mvm *mvm,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *key,
			bool have_key_offset);
int iwl_mvm_remove_sta_key(struct iwl_mvm *mvm,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *keyconf);

void iwl_mvm_update_tkip_key(struct iwl_mvm *mvm,
			     struct ieee80211_vif *vif,
			     struct ieee80211_key_conf *keyconf,
			     struct ieee80211_sta *sta, u32 iv32,
			     u16 *phase1key);

int iwl_mvm_rx_eosp_notif(struct iwl_mvm *mvm,
			  struct iwl_rx_cmd_buffer *rxb,
			  struct iwl_device_cmd *cmd);

/* AMPDU */
int iwl_mvm_sta_rx_agg(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
		       int tid, u16 ssn, bool start);
int iwl_mvm_sta_tx_agg_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn);
int iwl_mvm_sta_tx_agg_oper(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u8 buf_size);
int iwl_mvm_sta_tx_agg_stop(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid);
int iwl_mvm_sta_tx_agg_flush(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid);

int iwl_mvm_add_aux_sta(struct iwl_mvm *mvm);
void iwl_mvm_del_aux_sta(struct iwl_mvm *mvm);

int iwl_mvm_alloc_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_send_add_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_add_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_send_rm_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_rm_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_dealloc_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif);

void iwl_mvm_sta_drained_wk(struct work_struct *wk);
void iwl_mvm_sta_modify_ps_wake(struct iwl_mvm *mvm,
				struct ieee80211_sta *sta);
void iwl_mvm_sta_modify_sleep_tx_count(struct iwl_mvm *mvm,
				       struct ieee80211_sta *sta,
				       enum ieee80211_frame_release_type reason,
				       u16 cnt, u16 tids, bool more_data,
				       bool agg);
int iwl_mvm_drain_sta(struct iwl_mvm *mvm, struct iwl_mvm_sta *mvmsta,
		      bool drain);
void iwl_mvm_sta_modify_disable_tx(struct iwl_mvm *mvm,
				   struct iwl_mvm_sta *mvmsta, bool disable);
void iwl_mvm_sta_modify_disable_tx_ap(struct iwl_mvm *mvm,
				      struct ieee80211_sta *sta,
				      bool disable);
void iwl_mvm_modify_all_sta_disable_tx(struct iwl_mvm *mvm,
				       struct iwl_mvm_vif *mvmvif,
				       bool disable);
void iwl_mvm_csa_client_absent(struct iwl_mvm *mvm, struct ieee80211_vif *vif);

#endif /* __sta_h__ */
