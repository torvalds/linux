// SPDX-License-Identifier: GPL-2.0-or-later
/**
 ****************************************************************************************
 *
 * @file rwnx_msg_rx.c
 *
 * @brief RX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ****************************************************************************************
 */

#include <linux/vmalloc.h>
#include "rwnx_defs.h"
#include "rwnx_prof.h"
#include "rwnx_tx.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#ifdef CONFIG_RWNX_FULLMAC
#include "rwnx_debugfs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_tdls.h"
#endif /* CONFIG_RWNX_FULLMAC */
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "aicwf_txrxif.h"
#ifdef CONFIG_USB_WIRELESS_EXT
#include "aicwf_wext_linux.h"
#endif

static int rwnx_freq_to_idx(struct rwnx_hw *rwnx_hw, int freq)
{
    struct ieee80211_supported_band *sband;
    int band, ch, idx = 0;

    for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
#ifdef CONFIG_RWNX_FULLMAC
        sband = rwnx_hw->wiphy->bands[band];
#endif /* CONFIG_RWNX_FULLMAC */
        if (!sband) {
            continue;
        }

        for (ch = 0; ch < sband->n_channels; ch++, idx++) {
            if (sband->channels[ch].center_freq == freq) {
                goto exit;
            }
        }
    }

    BUG_ON(1);

exit:
    // Channel has been found, return the index
    return idx;
}

/***************************************************************************
 * Messages from MM task
 **************************************************************************/
static inline int rwnx_rx_chan_pre_switch_ind(struct rwnx_hw *rwnx_hw,
                                              struct rwnx_cmd *cmd,
                                              struct ipc_e2a_msg *msg)
{
    struct rwnx_vif *rwnx_vif;
    int chan_idx = ((struct mm_channel_pre_switch_ind *)msg->param)->chan_index;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    REG_SW_SET_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_PSWTCH_BIT);

#ifdef CONFIG_RWNX_FULLMAC
    list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
        if (rwnx_vif->up && rwnx_vif->ch_index == chan_idx) {
			AICWFDBG(LOGDEBUG, "rwnx_txq_vif_stop\r\n");
            rwnx_txq_vif_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
        }
    }
#endif /* CONFIG_RWNX_FULLMAC */

    REG_SW_CLEAR_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_PSWTCH_BIT);

    return 0;
}

static inline int rwnx_rx_chan_switch_ind(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct rwnx_vif *rwnx_vif;
    int chan_idx = ((struct mm_channel_switch_ind *)msg->param)->chan_index;
    bool roc     = ((struct mm_channel_switch_ind *)msg->param)->roc;
    bool roc_tdls = ((struct mm_channel_switch_ind *)msg->param)->roc_tdls;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    REG_SW_SET_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_SWTCH_BIT);

#ifdef CONFIG_RWNX_FULLMAC
    if (roc_tdls) {
        u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;
        list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
            if (rwnx_vif->vif_index == vif_index) {
                rwnx_vif->roc_tdls = true;
                rwnx_txq_tdls_sta_start(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
            }
        }
    } else if (!roc) {
        list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
            if (rwnx_vif->up && rwnx_vif->ch_index == chan_idx) {
                rwnx_txq_vif_start(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
            }
        }
    } else {
        /* Retrieve the allocated RoC element */
        struct rwnx_roc_elem *roc_elem = rwnx_hw->roc_elem;

        /* If mgmt_roc is true, remain on channel has been started by ourself */
        if (!roc_elem->mgmt_roc) {
            /* Inform the host that we have switch on the indicated off-channel */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	    cfg80211_ready_on_channel(roc_elem->wdev->netdev, (u64)(rwnx_hw->roc_cookie_cnt),
                                      roc_elem->chan, NL80211_CHAN_HT20, roc_elem->duration, GFP_ATOMIC);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
	    cfg80211_ready_on_channel(roc_elem->wdev, (u64)(rwnx_hw->roc_cookie_cnt),
                                      roc_elem->chan, NL80211_CHAN_HT20, roc_elem->duration, GFP_ATOMIC);
#else
            cfg80211_ready_on_channel(roc_elem->wdev, (u64)(rwnx_hw->roc_cookie_cnt),
                                      roc_elem->chan, roc_elem->duration, GFP_ATOMIC);
#endif
        }

        /* Keep in mind that we have switched on the channel */
        roc_elem->on_chan = true;

        // Enable traffic on OFF channel queue
        rwnx_txq_offchan_start(rwnx_hw);
    }

    tasklet_schedule(&rwnx_hw->task);

    rwnx_hw->cur_chanctx = chan_idx;
    rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);

#endif /* CONFIG_RWNX_FULLMAC */

    REG_SW_CLEAR_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_SWTCH_BIT);

    return 0;
}

static inline int rwnx_rx_tdls_chan_switch_cfm(struct rwnx_hw *rwnx_hw,
                                                struct rwnx_cmd *cmd,
                                                struct ipc_e2a_msg *msg)
{
    return 0;
}

static inline int rwnx_rx_tdls_chan_switch_ind(struct rwnx_hw *rwnx_hw,
                                               struct rwnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_FULLMAC
    // Enable traffic on OFF channel queue
    rwnx_txq_offchan_start(rwnx_hw);

    return 0;
#endif
}

static inline int rwnx_rx_tdls_chan_switch_base_ind(struct rwnx_hw *rwnx_hw,
                                                    struct rwnx_cmd *cmd,
                                                    struct ipc_e2a_msg *msg)
{
    struct rwnx_vif *rwnx_vif;
    u8 vif_index = ((struct tdls_chan_switch_base_ind *)msg->param)->vif_index;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_FULLMAC
    list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
        if (rwnx_vif->vif_index == vif_index) {
            rwnx_vif->roc_tdls = false;
            rwnx_txq_tdls_sta_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
        }
    }
    return 0;
#endif
}

static inline int rwnx_rx_tdls_peer_ps_ind(struct rwnx_hw *rwnx_hw,
                                           struct rwnx_cmd *cmd,
                                           struct ipc_e2a_msg *msg)
{
    struct rwnx_vif *rwnx_vif;
    u8 vif_index = ((struct tdls_peer_ps_ind *)msg->param)->vif_index;
    bool ps_on = ((struct tdls_peer_ps_ind *)msg->param)->ps_on;

#ifdef CONFIG_RWNX_FULLMAC
    list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
        if (rwnx_vif->vif_index == vif_index) {
            rwnx_vif->sta.tdls_sta->tdls.ps_on = ps_on;
            // Update PS status for the TDLS station
            rwnx_ps_bh_enable(rwnx_hw, rwnx_vif->sta.tdls_sta, ps_on);
        }
    }

    return 0;
#endif
}

static inline int rwnx_rx_remain_on_channel_exp_ind(struct rwnx_hw *rwnx_hw,
                                                    struct rwnx_cmd *cmd,
                                                    struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_FULLMAC
    /* Retrieve the allocated RoC element */
    struct rwnx_roc_elem *roc_elem = rwnx_hw->roc_elem;
    /* Get VIF on which RoC has been started */
    struct rwnx_vif *rwnx_vif = NULL;// = container_of(roc_elem->wdev, struct rwnx_vif, wdev);

    RWNX_DBG(RWNX_FN_ENTRY_STR);
    if(roc_elem == NULL){
        AICWFDBG(LOGERROR,"%s roc_elem == NULL \r\n", __func__);
        return 0;
    }

    rwnx_vif = container_of(roc_elem->wdev, struct rwnx_vif, wdev);

	
#ifdef CREATE_TRACE_POINTS
    /* For debug purpose (use ftrace kernel option) */
    trace_roc_exp(rwnx_vif->vif_index);
#endif
    /* If mgmt_roc is true, remain on channel has been started by ourself */
    /* If RoC has been cancelled before we switched on channel, do not call cfg80211 */
    if (!roc_elem->mgmt_roc && roc_elem->on_chan) {
        /* Inform the host that off-channel period has expired */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	cfg80211_remain_on_channel_expired(roc_elem->wdev->netdev, (u64)(rwnx_hw->roc_cookie_cnt),
                                           roc_elem->chan, NL80211_CHAN_HT20, GFP_ATOMIC);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
	cfg80211_remain_on_channel_expired(roc_elem->wdev, (u64)(rwnx_hw->roc_cookie_cnt),
                                           roc_elem->chan, NL80211_CHAN_HT20, GFP_ATOMIC);
#else
        cfg80211_remain_on_channel_expired(roc_elem->wdev, (u64)(rwnx_hw->roc_cookie_cnt),
                                           roc_elem->chan, GFP_ATOMIC);
#endif
    }

    /* De-init offchannel TX queue */
    rwnx_txq_offchan_deinit(rwnx_vif);

    /* Increase the cookie counter cannot be zero */
    rwnx_hw->roc_cookie_cnt++;

    if (rwnx_hw->roc_cookie_cnt == 0) {
        rwnx_hw->roc_cookie_cnt = 1;
    }

    /* Free the allocated RoC element */
    kfree(roc_elem);
    rwnx_hw->roc_elem = NULL;

	AICWFDBG(LOGTRACE, "%s exit\r\n", __func__);
#endif /* CONFIG_RWNX_FULLMAC */
    return 0;
}

static inline int rwnx_rx_p2p_vif_ps_change_ind(struct rwnx_hw *rwnx_hw,
                                                struct rwnx_cmd *cmd,
                                                struct ipc_e2a_msg *msg)
{
    int vif_idx  = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->vif_index;
    int ps_state = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->ps_state;
    struct rwnx_vif *vif_entry;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_FULLMAC
    vif_entry = rwnx_hw->vif_table[vif_idx];

    if (vif_entry) {
        goto found_vif;
    }
#endif /* CONFIG_RWNX_FULLMAC */

    goto exit;

found_vif:

#ifdef CONFIG_RWNX_FULLMAC
    if (ps_state == MM_PS_MODE_OFF) {
        // Start TX queues for provided VIF
        rwnx_txq_vif_start(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
    }
    else {
        // Stop TX queues for provided VIF
        rwnx_txq_vif_stop(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
    }
#endif /* CONFIG_RWNX_FULLMAC */

exit:
    return 0;
}

static inline int rwnx_rx_channel_survey_ind(struct rwnx_hw *rwnx_hw,
                                             struct rwnx_cmd *cmd,
                                             struct ipc_e2a_msg *msg)
{
    struct mm_channel_survey_ind *ind = (struct mm_channel_survey_ind *)msg->param;
    // Get the channel index
    int idx = rwnx_freq_to_idx(rwnx_hw, ind->freq);
    // Get the survey
    struct rwnx_survey_info *rwnx_survey;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (idx >  ARRAY_SIZE(rwnx_hw->survey))
        return 0;

    rwnx_survey = &rwnx_hw->survey[idx];

    // Store the received parameters
    rwnx_survey->chan_time_ms = ind->chan_time_ms;
    rwnx_survey->chan_time_busy_ms = ind->chan_time_busy_ms;
    rwnx_survey->noise_dbm = ind->noise_dbm;
    rwnx_survey->filled = (SURVEY_INFO_TIME |
                           SURVEY_INFO_TIME_BUSY);

    if (ind->noise_dbm != 0) {
        rwnx_survey->filled |= SURVEY_INFO_NOISE_DBM;
    }

    return 0;
}

static inline int rwnx_rx_p2p_noa_upd_ind(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    return 0;
}

static inline int rwnx_rx_rssi_status_ind(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_rssi_status_ind *ind = (struct mm_rssi_status_ind *)msg->param;
    int vif_idx  = ind->vif_index;
    bool rssi_status = ind->rssi_status;

    struct rwnx_vif *vif_entry;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_FULLMAC
    vif_entry = rwnx_hw->vif_table[vif_idx];
    if (vif_entry) {
        cfg80211_cqm_rssi_notify(vif_entry->ndev,
                                 rssi_status ? NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW :
                                               NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
                                 ind->rssi, GFP_ATOMIC);
    }
#endif /* CONFIG_RWNX_FULLMAC */

    return 0;
}

static inline int rwnx_rx_pktloss_notify_ind(struct rwnx_hw *rwnx_hw,
                                             struct rwnx_cmd *cmd,
                                             struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_FULLMAC
    struct mm_pktloss_ind *ind = (struct mm_pktloss_ind *)msg->param;
    struct rwnx_vif *vif_entry;
    int vif_idx  = ind->vif_index;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    vif_entry = rwnx_hw->vif_table[vif_idx];
    if (vif_entry) {
        cfg80211_cqm_pktloss_notify(vif_entry->ndev, (const u8 *)ind->mac_addr.array,
                                    ind->num_packets, GFP_ATOMIC);
    }
#endif /* CONFIG_RWNX_FULLMAC */

    return 0;
}

static inline int rwnx_apm_staloss_ind(struct rwnx_hw *rwnx_hw,
                                                struct rwnx_cmd *cmd,
                                                struct ipc_e2a_msg *msg)
{
    struct mm_apm_staloss_ind *ind = (struct mm_apm_staloss_ind *)msg->param;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    memcpy(rwnx_hw->sta_mac_addr, ind->mac_addr, 6);
    rwnx_hw->apm_vif_idx = ind->vif_idx;

    queue_work(rwnx_hw->apmStaloss_wq, &rwnx_hw->apmStalossWork);

    return 0;
}


static inline int rwnx_rx_csa_counter_ind(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_csa_counter_ind *ind = (struct mm_csa_counter_ind *)msg->param;
    struct rwnx_vif *vif;
    bool found = false;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &rwnx_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
#ifdef CONFIG_RWNX_FULLMAC
        if (vif->ap.csa)
            vif->ap.csa->count = ind->csa_count;
        else
            netdev_err(vif->ndev, "CSA counter update but no active CSA");

#endif
    }

    return 0;
}

#ifdef CONFIG_RWNX_FULLMAC
static inline int rwnx_rx_csa_finish_ind(struct rwnx_hw *rwnx_hw,
                                         struct rwnx_cmd *cmd,
                                         struct ipc_e2a_msg *msg)
{
    struct mm_csa_finish_ind *ind = (struct mm_csa_finish_ind *)msg->param;
    struct rwnx_vif *vif;
    bool found = false;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &rwnx_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
        if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP ||
            RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) {
            if (vif->ap.csa) {
                vif->ap.csa->status = ind->status;
                vif->ap.csa->ch_idx = ind->chan_idx;
                schedule_work(&vif->ap.csa->work);
            } else
                netdev_err(vif->ndev, "CSA finish indication but no active CSA");
        } else {
            if (ind->status == 0) {
                rwnx_chanctx_unlink(vif);
                rwnx_chanctx_link(vif, ind->chan_idx, NULL);
                if (rwnx_hw->cur_chanctx == ind->chan_idx) {
                    rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
                    rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
                } else
                    rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
            }
        }
    }

    return 0;
}

static inline int rwnx_rx_csa_traffic_ind(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_csa_traffic_ind *ind = (struct mm_csa_traffic_ind *)msg->param;
    struct rwnx_vif *vif;
    bool found = false;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &rwnx_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
        if (ind->enable)
            rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CSA, rwnx_hw);
        else
            rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CSA, rwnx_hw);
    }

    return 0;
}

static inline int rwnx_rx_ps_change_ind(struct rwnx_hw *rwnx_hw,
                                        struct rwnx_cmd *cmd,
                                        struct ipc_e2a_msg *msg)
{
    struct mm_ps_change_ind *ind = (struct mm_ps_change_ind *)msg->param;
    struct rwnx_sta *sta = &rwnx_hw->sta_table[ind->sta_idx];

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#if 1//2022-01-15 add for rwnx_hw->vif_table[sta->vif_idx] if null when rwnx_close
	if (!rwnx_hw->vif_table[sta->vif_idx]){
        wiphy_err(rwnx_hw->wiphy, "rwnx_hw->vif_table[sta->vif_idx] is null\n");	
		return 0;
	}
#endif

    if (ind->sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
        wiphy_err(rwnx_hw->wiphy, "Invalid sta index reported by fw %d\n",
                  ind->sta_idx);
        return 1;
    }

    netdev_dbg(rwnx_hw->vif_table[sta->vif_idx]->ndev,
               "Sta %d, change PS mode to %s", sta->sta_idx,
               ind->ps_state ? "ON" : "OFF");

    if (sta->valid) {
        rwnx_ps_bh_enable(rwnx_hw, sta, ind->ps_state);
    } else if (rwnx_hw->adding_sta) {
        sta->ps.active = ind->ps_state ? true : false;
    } else {
        netdev_err(rwnx_hw->vif_table[sta->vif_idx]->ndev,
                   "Ignore PS mode change on invalid sta\n");
    }
	

    return 0;
}


static inline int rwnx_rx_traffic_req_ind(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_traffic_req_ind *ind = (struct mm_traffic_req_ind *)msg->param;
    struct rwnx_sta *sta = &rwnx_hw->sta_table[ind->sta_idx];

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    netdev_dbg(rwnx_hw->vif_table[sta->vif_idx]->ndev,
               "Sta %d, asked for %d pkt", sta->sta_idx, ind->pkt_cnt);

    rwnx_ps_bh_traffic_req(rwnx_hw, sta, ind->pkt_cnt,
                           ind->uapsd ? UAPSD_ID : LEGACY_PS_ID);

    return 0;
}
#endif /* CONFIG_RWNX_FULLMAC */

/***************************************************************************
 * Messages from SCAN task
 **************************************************************************/
#if 0
static inline int rwnx_rx_scan_done_ind(struct rwnx_hw *rwnx_hw,
                                        struct rwnx_cmd *cmd,
                                        struct ipc_e2a_msg *msg)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    struct cfg80211_scan_info info = {
        .aborted = false,
    };
#endif
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    rwnx_ipc_elem_var_deallocs(rwnx_hw, &rwnx_hw->scan_ie);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    ieee80211_scan_completed(rwnx_hw->hw, &info);
#else
    ieee80211_scan_completed(rwnx_hw->hw, false);
#endif

    return 0;
}
#endif

/***************************************************************************
 * Messages from SCANU task
 **************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC
extern uint8_t scanning;
static inline int rwnx_rx_scanu_start_cfm(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
			struct cfg80211_scan_info info = {
				.aborted = false,
			};
#endif

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (rwnx_hw->scan_request 
#ifdef CONFIG_USB_WIRELESS_EXT
		&& !rwnx_hw->wext_scan) {
#else
		){
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
        cfg80211_scan_done(rwnx_hw->scan_request, false);
#endif
    } 


#ifdef CONFIG_USB_WIRELESS_EXT
	else if(rwnx_hw->wext_scan){
    	rwnx_hw->wext_scan = 0;
		AICWFDBG(LOGDEBUG, "%s rwnx_hw->wext_scan done!!\r\n", __func__);
		if(rwnx_hw->scan_request){
			vfree(rwnx_hw->scan_request);
		}
		complete(&rwnx_hw->wext_scan_com);
	}
#endif
	else {
        AICWFDBG(LOGERROR, "%s rwnx_hw->scan_request is NULL!!\r\n", __func__);
    }

    rwnx_hw->scan_request = NULL;
    scanning = 0;

    return 0;
}

static inline int rwnx_rx_scanu_result_ind(struct rwnx_hw *rwnx_hw,
                                           struct rwnx_cmd *cmd,
                                           struct ipc_e2a_msg *msg)
{
    struct cfg80211_bss *bss = NULL;
    struct ieee80211_channel *chan;
    struct scanu_result_ind *ind = (struct scanu_result_ind *)msg->param;
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)ind->payload;
	
	const u8 *ie = mgmt->u.beacon.variable;
	char *ssid = NULL;
	int ssid_len = 0;
	int freq = 0;
	
#ifdef CONFIG_USB_WIRELESS_EXT
	struct scanu_result_wext *scan_re_wext;
#endif


    RWNX_DBG(RWNX_FN_ENTRY_STR);
	
    chan = ieee80211_get_channel(rwnx_hw->wiphy, ind->center_freq);

    if (chan != NULL) {
        #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
        //ktime_t ts;
        struct timespec ts;
		get_monotonic_boottime(&ts);
        //ts = ktime_get_real();
        mgmt->u.probe_resp.timestamp = ((u64)ts.tv_sec*1000000) + ts.tv_nsec/1000;
        #else
        struct timespec64 ts;
        ktime_get_real_ts64(&ts);
        mgmt->u.probe_resp.timestamp = ((u64)ts.tv_sec*1000000) + ts.tv_nsec/1000;
        #endif
        bss = cfg80211_inform_bss_frame(rwnx_hw->wiphy, chan,
                                        (struct ieee80211_mgmt *)ind->payload,
                                        ind->length, ind->rssi * 100, GFP_ATOMIC);

		//print scan result info start
		ssid_len = ie[1];
		ssid = (char *)kmalloc(sizeof(char)* (ssid_len + 1), GFP_ATOMIC);
		memset(ssid, 0, ssid_len + 1);
		memcpy(ssid, &ie[2], ssid_len);
		freq = ind->center_freq;
		AICWFDBG(LOGDEBUG, "%s %02x:%02x:%02x:%02x:%02x:%02x ssid:%s freq:%d timestamp:%ld\r\n", __func__, 
			bss->bssid[0],bss->bssid[1],bss->bssid[2],
			bss->bssid[3],bss->bssid[4],bss->bssid[5],
			ssid, freq, (long)mgmt->u.probe_resp.timestamp);
		kfree(ssid);
		ssid = NULL;
		//print scan result info end
		
#ifdef CONFIG_USB_WIRELESS_EXT
		if(rwnx_hw->wext_scan){
			
			scan_re_wext = (struct scanu_result_wext *)vmalloc(sizeof(struct scanu_result_wext));
			scan_re_wext->ind = (struct scanu_result_ind *)vmalloc(sizeof(struct scanu_result_ind));
			scan_re_wext->payload = (u32_l *)vmalloc(sizeof(u32_l) * ind->length);

			memset(scan_re_wext->ind, 0, sizeof(struct scanu_result_ind));
			memset(scan_re_wext->payload, 0, ind->length);
			
			memcpy(scan_re_wext->ind, ind, sizeof(struct scanu_result_ind));
			memcpy(scan_re_wext->payload, ind->payload, ind->length);
	
			scan_re_wext->bss = bss;

			INIT_LIST_HEAD(&scan_re_wext->scanu_re_list);
			list_add_tail(&scan_re_wext->scanu_re_list, &rwnx_hw->wext_scanre_list);
			return 0;
		}
#endif
    }

    if (bss != NULL)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
	cfg80211_put_bss(bss);
#else
        cfg80211_put_bss(rwnx_hw->wiphy, bss);
#endif

    return 0;
}
#endif /* CONFIG_RWNX_FULLMAC */

/***************************************************************************
 * Messages from ME task
 **************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC
static inline int rwnx_rx_me_tkip_mic_failure_ind(struct rwnx_hw *rwnx_hw,
                                                  struct rwnx_cmd *cmd,
                                                  struct ipc_e2a_msg *msg)
{
    struct me_tkip_mic_failure_ind *ind = (struct me_tkip_mic_failure_ind *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
    struct net_device *dev = rwnx_vif->ndev;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    cfg80211_michael_mic_failure(dev, (u8 *)&ind->addr, (ind->ga?NL80211_KEYTYPE_GROUP:
                                 NL80211_KEYTYPE_PAIRWISE), ind->keyid,
                                 (u8 *)&ind->tsc, GFP_ATOMIC);

    return 0;
}

static inline int rwnx_rx_me_tx_credits_update_ind(struct rwnx_hw *rwnx_hw,
                                                   struct rwnx_cmd *cmd,
                                                   struct ipc_e2a_msg *msg)
{
    struct me_tx_credits_update_ind *ind = (struct me_tx_credits_update_ind *)msg->param;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    rwnx_txq_credit_update(rwnx_hw, ind->sta_idx, ind->tid, ind->credits);

    return 0;
}
#endif /* CONFIG_RWNX_FULLMAC */

/***************************************************************************
 * Messages from SM task
 **************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static inline void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
                             struct ieee80211_channel *chan,
                             enum nl80211_channel_type chan_type)
{
        if (WARN_ON(!chan))
                return;
        chandef->chan = chan;
        chandef->center_freq2 = 0;
        switch (chan_type) {
        case NL80211_CHAN_NO_HT:
                chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
                chandef->center_freq1 = chan->center_freq;
                break;
        case NL80211_CHAN_HT20:
                chandef->width = NL80211_CHAN_WIDTH_20;
                chandef->center_freq1 = chan->center_freq;
                break;
        case NL80211_CHAN_HT40PLUS:
                chandef->width = NL80211_CHAN_WIDTH_40;
                chandef->center_freq1 = chan->center_freq + 10;
                break;
        case NL80211_CHAN_HT40MINUS:
                chandef->width = NL80211_CHAN_WIDTH_40;
                chandef->center_freq1 = chan->center_freq - 10;
                break;
        default:
                WARN_ON(1);
        }
}
#endif
static inline int rwnx_rx_sm_connect_ind(struct rwnx_hw *rwnx_hw,
                                         struct rwnx_cmd *cmd,
                                         struct ipc_e2a_msg *msg)
{
    struct sm_connect_ind *ind = (struct sm_connect_ind *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
    struct net_device *dev = NULL;
    const u8 *req_ie, *rsp_ie;
    const u8 *extcap_ie;
    const struct ieee_types_extcap *extcap;
    struct ieee80211_channel *chan;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

	if(!rwnx_vif){
		AICWFDBG(LOGERROR, "%s rwnx_vif is null \r\n", __func__);
		return 0;
	}
	dev = rwnx_vif->ndev;


    /* Retrieve IE addresses and lengths */
    req_ie = (const u8 *)ind->assoc_ie_buf;
    rsp_ie = req_ie + ind->assoc_req_ie_len;


	if (rwnx_vif->sta.external_auth)
		rwnx_vif->sta.external_auth = false;


    // Fill-in the AP information
	AICWFDBG(LOGINFO, "%s ind->status_code:%d \r\n", __func__, ind->status_code);
    if (ind->status_code == 0)
    {
        struct rwnx_sta *sta = &rwnx_hw->sta_table[ind->ap_idx];
        u8 txq_status;
        struct cfg80211_chan_def chandef;

        sta->valid = true;
        sta->sta_idx = ind->ap_idx;
        sta->ch_idx = ind->ch_idx;
        sta->vif_idx = ind->vif_idx;
        sta->vlan_idx = sta->vif_idx;
        sta->qos = ind->qos;
        sta->acm = ind->acm;
        sta->ps.active = false;
        sta->aid = ind->aid;
        sta->band = ind->band;
        sta->width = ind->width;
        sta->center_freq = ind->center_freq;
        sta->center_freq1 = ind->center_freq1;
        sta->center_freq2 = ind->center_freq2;
        rwnx_vif->sta.ap = sta;

        chan = ieee80211_get_channel(rwnx_hw->wiphy, ind->center_freq);
        cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
        if (!rwnx_hw->mod_params->ht_on)
            chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
        else
            chandef.width = chnl2bw[ind->width];
        chandef.center_freq1 = ind->center_freq1;
        chandef.center_freq2 = ind->center_freq2;
        rwnx_chanctx_link(rwnx_vif, ind->ch_idx, &chandef);
        memcpy(sta->mac_addr, ind->bssid.array, ETH_ALEN);
        if (ind->ch_idx == rwnx_hw->cur_chanctx) {
            txq_status = 0;
        } else {
            txq_status = RWNX_TXQ_STOP_CHAN;
        }
        memcpy(sta->ac_param, ind->ac_param, sizeof(sta->ac_param));
        rwnx_txq_sta_init(rwnx_hw, sta, txq_status);
        rwnx_txq_tdls_vif_init(rwnx_vif);
#ifdef CONFIG_DEBUG_FS
        rwnx_dbgfs_register_rc_stat(rwnx_hw, sta);
#endif
        rwnx_mu_group_sta_init(sta, NULL);
        /* Look for TDLS Channel Switch Prohibited flag in the Extended Capability
         * Information Element*/
        extcap_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, rsp_ie, ind->assoc_rsp_ie_len);
        if (extcap_ie && extcap_ie[1] >= 5) {
            extcap = (void *)(extcap_ie);
            rwnx_vif->tdls_chsw_prohibited = extcap->ext_capab[4] & WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED;
        }

		if(atomic_read(&rwnx_hw->sta_flowctrl[sta->sta_idx].tx_pending_cnt) > 0) {
			AICWFDBG(LOGDEBUG, "sta idx %d fc error %d\n",  sta->sta_idx,  atomic_read(&rwnx_hw->sta_flowctrl[sta->sta_idx].tx_pending_cnt));
		}

        if (rwnx_vif->wep_enabled)
            rwnx_vif->wep_auth_err = false;

#ifdef CONFIG_RWNX_BFMER
        /* If Beamformer feature is activated, check if features can be used
         * with the new peer device
         */
        if (rwnx_hw->mod_params->bfmer) {
            const u8 *vht_capa_ie;
            const struct ieee80211_vht_cap *vht_cap;

            do {
                /* Look for VHT Capability Information Element */
                vht_capa_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, rsp_ie,
                                               ind->assoc_rsp_ie_len);

                /* Stop here if peer device does not support VHT */
                if (!vht_capa_ie) {
                    break;
                }

                vht_cap = (const struct ieee80211_vht_cap *)(vht_capa_ie + 2);

                /* Send MM_BFMER_ENABLE_REQ message if needed */
                rwnx_send_bfmer_enable(rwnx_hw, sta, vht_cap);
            } while (0);
        }
#endif //(CONFIG_RWNX_BFMER)

#ifdef CONFIG_RWNX_MON_DATA
        // If there are 1 sta and 1 monitor interface active at the same time then
        // monitor interface channel context is always the same as the STA interface.
        // This doesn't work with 2 STA interfaces but we don't want to support it.
        if (rwnx_hw->monitor_vif != RWNX_INVALID_VIF) {
            struct rwnx_vif *rwnx_mon_vif = rwnx_hw->vif_table[rwnx_hw->monitor_vif];
            rwnx_chanctx_unlink(rwnx_mon_vif);
            rwnx_chanctx_link(rwnx_mon_vif, ind->ch_idx, NULL);
        }
#endif
		atomic_set(&rwnx_vif->drv_conn_state, (int)RWNX_DRV_STATUS_CONNECTED);

    } else if (ind->status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG) {
        if (rwnx_vif->wep_enabled) {
            rwnx_vif->wep_auth_err = true;
            printk("con ind wep_auth_err %d\n", rwnx_vif->wep_auth_err);
        }
		atomic_set(&rwnx_vif->drv_conn_state, (int)RWNX_DRV_STATUS_DISCONNECTED);
    }else{
		atomic_set(&rwnx_vif->drv_conn_state, (int)RWNX_DRV_STATUS_DISCONNECTED);
	}

    if (!ind->roamed) {
        cfg80211_connect_result(dev, (const u8 *)ind->bssid.array, req_ie,
                                ind->assoc_req_ie_len, rsp_ie,
                                ind->assoc_rsp_ie_len, ind->status_code,
                                GFP_ATOMIC);
    }
    else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
        struct cfg80211_roam_info info;
        memset(&info, 0, sizeof(info));
        if (rwnx_vif->ch_index < NX_CHAN_CTXT_CNT)
            info.channel = rwnx_hw->chanctx_table[rwnx_vif->ch_index].chan_def.chan;
        info.bssid = (const u8 *)ind->bssid.array;
        info.req_ie = req_ie;
        info.req_ie_len = ind->assoc_req_ie_len;
        info.resp_ie = rsp_ie;
        info.resp_ie_len = ind->assoc_rsp_ie_len;
        cfg80211_roamed(dev, &info, GFP_ATOMIC);
#else
        chan = ieee80211_get_channel(rwnx_hw->wiphy, ind->center_freq);
        cfg80211_roamed(dev
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39) || defined(COMPAT_KERNEL_RELEASE)
            , chan
#endif
            , (const u8 *)ind->bssid.array
            , req_ie
            , ind->assoc_req_ie_len
            , rsp_ie
            , ind->assoc_rsp_ie_len
            , GFP_ATOMIC);
#endif /*LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)*/
    }
    netif_tx_start_all_queues(dev);
    netif_carrier_on(dev);

    return 0;
}

void rwnx_cfg80211_unlink_bss(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif){
	struct wiphy *wiphy = rwnx_hw->wiphy;
	struct cfg80211_bss *bss = NULL;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	bss = cfg80211_get_bss(wiphy, NULL/*notify_channel*/,
		rwnx_vif->sta.bssid, rwnx_vif->sta.ssid,
		rwnx_vif->sta.ssid_len,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
		IEEE80211_BSS_TYPE_ESS,
		IEEE80211_PRIVACY(true));//temp set true
#else
		WLAN_CAPABILITY_ESS,
		WLAN_CAPABILITY_ESS);
#endif

	if (bss) {
		cfg80211_unlink_bss(wiphy, bss);
		AICWFDBG(LOGINFO, "%s(): cfg80211_unlink %s!!\n", __func__, rwnx_vif->sta.ssid);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		cfg80211_put_bss(wiphy, bss);
#else
		cfg80211_put_bss(bss);
#endif
	}else{
		AICWFDBG(LOGERROR, "%s(): cfg80211_unlink error %s!!\n", __func__, rwnx_vif->sta.ssid);
	}

	memset(rwnx_vif->sta.ssid, 0, rwnx_vif->sta.ssid_len);
	rwnx_vif->sta.ssid_len = 0;
	memset(rwnx_vif->sta.bssid, 0, ETH_ALEN);
}


extern u8 dhcped;
static inline int rwnx_rx_sm_disconnect_ind(struct rwnx_hw *rwnx_hw,
                                            struct rwnx_cmd *cmd,
                                            struct ipc_e2a_msg *msg)
{
    struct sm_disconnect_ind *ind = (struct sm_disconnect_ind *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
    struct net_device *dev;
#ifdef AICWF_RX_REORDER
    struct reord_ctrl_info *reord_info, *tmp;
    u8 *macaddr;
    struct aicwf_rx_priv *rx_priv;
#endif

    RWNX_DBG(RWNX_FN_ENTRY_STR);
    dhcped = 0;

    if(!rwnx_vif){
		AICWFDBG(LOGERROR, "%s rwnx_vif is null \r\n", __func__);
		//atomic_set(&rwnx_vif->drv_conn_state, RWNX_DRV_STATUS_DISCONNECTED);
        return 0;
    }
    dev = rwnx_vif->ndev;

	rwnx_cfg80211_unlink_bss(rwnx_hw, rwnx_vif);


#ifdef CONFIG_BR_SUPPORT
	struct rwnx_vif *vif = netdev_priv(dev);
        /* clear bridge database */
        nat25_db_cleanup(rwnx_vif);
#endif /* CONFIG_BR_SUPPORT */

    if(rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)
        rwnx_hw->is_p2p_connected = 0;
    /* if vif is not up, rwnx_close has already been called */
    if (rwnx_vif->up) {
        if (!ind->ft_over_ds) {
            cfg80211_disconnected(dev, ind->reason_code, NULL, 0,
                                  (ind->reason_code <= 1), GFP_ATOMIC);
        }
        netif_tx_stop_all_queues(dev);
        netif_carrier_off(dev);
    }

#ifdef CONFIG_RWNX_BFMER
    /* Disable Beamformer if supported */
    rwnx_bfmer_report_del(rwnx_hw, rwnx_vif->sta.ap);
#endif //(CONFIG_RWNX_BFMER)

#ifdef AICWF_RX_REORDER
#ifdef AICWF_SDIO_SUPPORT
    rx_priv = rwnx_hw->sdiodev->rx_priv;
#else
    rx_priv = rwnx_hw->usbdev->rx_priv;
#endif
    if((rwnx_vif->wdev.iftype == NL80211_IFTYPE_STATION) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)) {
        macaddr = rwnx_vif->ndev->dev_addr;
		AICWFDBG(LOGINFO, "deinit:macaddr:%x,%x,%x,%x,%x,%x\r\n", macaddr[0],macaddr[1],macaddr[2], \
                               macaddr[3],macaddr[4],macaddr[5]);
        list_for_each_entry_safe(reord_info, tmp, &rx_priv->stas_reord_list, list) {
            macaddr = rwnx_vif->ndev->dev_addr;
			AICWFDBG(LOGINFO, "reord_mac:%x,%x,%x,%x,%x,%x\r\n", reord_info->mac_addr[0],reord_info->mac_addr[1],reord_info->mac_addr[2], \
                                   reord_info->mac_addr[3],reord_info->mac_addr[4],reord_info->mac_addr[5]);
            if (!memcmp(reord_info->mac_addr, macaddr, 6)) {
                reord_deinit_sta(rx_priv, reord_info);
                break;
            }
        }
    }
    else if((rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO))
        BUG();//should be not here: del_sta function
#endif

    rwnx_txq_sta_deinit(rwnx_hw, rwnx_vif->sta.ap);
    rwnx_txq_tdls_vif_deinit(rwnx_vif);
    #if 0
    rwnx_dbgfs_unregister_rc_stat(rwnx_hw, rwnx_vif->sta.ap);
    #endif
    rwnx_vif->sta.ap->valid = false;
    rwnx_vif->sta.ap = NULL;
    rwnx_external_auth_disable(rwnx_vif);
    rwnx_chanctx_unlink(rwnx_vif);

	//msleep(200);
	atomic_set(&rwnx_vif->drv_conn_state, (int)RWNX_DRV_STATUS_DISCONNECTED);
    return 0;
}

static inline int rwnx_rx_sm_external_auth_required_ind(struct rwnx_hw *rwnx_hw,
                                                        struct rwnx_cmd *cmd,
                                                        struct ipc_e2a_msg *msg)
{
    struct sm_external_auth_required_ind *ind =
        (struct sm_external_auth_required_ind *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(CONFIG_WPA3_FOR_OLD_KERNEL)
    struct net_device *dev = rwnx_vif->ndev;
    struct cfg80211_external_auth_params params;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    params.action = NL80211_EXTERNAL_AUTH_START;
    memcpy(params.bssid, ind->bssid.array, ETH_ALEN);
    params.ssid.ssid_len = ind->ssid.length;
    memcpy(params.ssid.ssid, ind->ssid.array,
           min_t(size_t, ind->ssid.length, sizeof(params.ssid.ssid)));
    params.key_mgmt_suite = ind->akm;

    if ((ind->vif_idx > NX_VIRT_DEV_MAX) || !rwnx_vif->up ||
        (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION) ||
        cfg80211_external_auth_request(dev, &params, GFP_ATOMIC)) {
        wiphy_err(rwnx_hw->wiphy, "Failed to start external auth on vif %d",
                  ind->vif_idx);
        rwnx_send_sm_external_auth_required_rsp(rwnx_hw, rwnx_vif,
                                                WLAN_STATUS_UNSPECIFIED_FAILURE);
        return 0;
    }

    rwnx_external_auth_enable(rwnx_vif);
#else
    rwnx_send_sm_external_auth_required_rsp(rwnx_hw, rwnx_vif,
                                            WLAN_STATUS_UNSPECIFIED_FAILURE);
#endif
    return 0;
}


static inline int rwnx_rx_mesh_path_create_cfm(struct rwnx_hw *rwnx_hw,
                                               struct rwnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_path_create_cfm *cfm = (struct mesh_path_create_cfm *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[cfm->vif_idx];

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Check we well have a Mesh Point Interface */
    if (rwnx_vif && (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT)) {
        rwnx_vif->ap.create_path = false;
    }

    return 0;
}

static inline int rwnx_rx_mesh_peer_update_ind(struct rwnx_hw *rwnx_hw,
                                               struct rwnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_peer_update_ind *ind = (struct mesh_peer_update_ind *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
    struct rwnx_sta *rwnx_sta = &rwnx_hw->sta_table[ind->sta_idx];

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if ((ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX)) ||
        (rwnx_vif && (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)) ||
        (ind->sta_idx >= NX_REMOTE_STA_MAX))
        return 1;

    /* Check we well have a Mesh Point Interface */
    if (!rwnx_vif->user_mpm)
    {
        /* Check if peer link has been established or lost */
        if (ind->estab) {
            if (!rwnx_sta->valid) {
                u8 txq_status;

                rwnx_sta->valid = true;
                rwnx_sta->sta_idx = ind->sta_idx;
                rwnx_sta->ch_idx = rwnx_vif->ch_index;
                rwnx_sta->vif_idx = ind->vif_idx;
                rwnx_sta->vlan_idx = rwnx_sta->vif_idx;
                rwnx_sta->ps.active = false;
                rwnx_sta->qos = true;
                rwnx_sta->aid = ind->sta_idx + 1;
                //rwnx_sta->acm = ind->acm;
                memcpy(rwnx_sta->mac_addr, ind->peer_addr.array, ETH_ALEN);

                rwnx_chanctx_link(rwnx_vif, rwnx_sta->ch_idx, NULL);

                /* Add the station in the list of VIF's stations */
                INIT_LIST_HEAD(&rwnx_sta->list);
                list_add_tail(&rwnx_sta->list, &rwnx_vif->ap.sta_list);

                /* Initialize the TX queues */
                if (rwnx_sta->ch_idx == rwnx_hw->cur_chanctx) {
                    txq_status = 0;
                } else {
                    txq_status = RWNX_TXQ_STOP_CHAN;
                }

                rwnx_txq_sta_init(rwnx_hw, rwnx_sta, txq_status);
#ifdef CONFIG_DEBUG_FS
                rwnx_dbgfs_register_rc_stat(rwnx_hw, rwnx_sta);
#endif
#ifdef CONFIG_RWNX_BFMER
                // TODO: update indication to contains vht capabilties
                if (rwnx_hw->mod_params->bfmer)
                    rwnx_send_bfmer_enable(rwnx_hw, rwnx_sta, NULL);

                rwnx_mu_group_sta_init(rwnx_sta, NULL);
#endif /* CONFIG_RWNX_BFMER */

            } else {
                WARN_ON(0);
            }
        } else {
            if (rwnx_sta->valid) {
                rwnx_sta->ps.active = false;
                rwnx_sta->valid = false;

                /* Remove the station from the list of VIF's station */
                list_del_init(&rwnx_sta->list);

                rwnx_txq_sta_deinit(rwnx_hw, rwnx_sta);
#ifdef CONFIG_DEBUG_FS
                rwnx_dbgfs_unregister_rc_stat(rwnx_hw, rwnx_sta);
#endif
            } else {
                WARN_ON(0);
            }
        }
    } else {
        if (!ind->estab && rwnx_sta->valid) {
            /* There is no way to inform upper layer for lost of peer, still
               clean everything in the driver */
            rwnx_sta->ps.active = false;
            rwnx_sta->valid = false;

            /* Remove the station from the list of VIF's station */
            list_del_init(&rwnx_sta->list);

            rwnx_txq_sta_deinit(rwnx_hw, rwnx_sta);
#ifdef CONFIG_DEBUG_FS
            rwnx_dbgfs_unregister_rc_stat(rwnx_hw, rwnx_sta);
#endif
        } else {
            WARN_ON(0);
        }
    }

    return 0;
}

static inline int rwnx_rx_mesh_path_update_ind(struct rwnx_hw *rwnx_hw,
                                               struct rwnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_path_update_ind *ind = (struct mesh_path_update_ind *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
    struct rwnx_mesh_path *mesh_path;
    bool found = false;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        return 1;

    if (!rwnx_vif || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT))
        return 0;

    /* Look for path with provided target address */
    list_for_each_entry(mesh_path, &rwnx_vif->ap.mpath_list, list) {
        if (mesh_path->path_idx == ind->path_idx) {
            found = true;
            break;
        }
    }

    /* Check if element has been deleted */
    if (ind->delete) {
        if (found) {
#ifdef CREATE_TRACE_POINTS
            trace_mesh_delete_path(mesh_path);
#endif
            /* Remove element from list */
            list_del_init(&mesh_path->list);
            /* Free the element */
            kfree(mesh_path);
        }
    }
    else {
        if (found) {
            // Update the Next Hop STA
            mesh_path->p_nhop_sta = &rwnx_hw->sta_table[ind->nhop_sta_idx];
#ifdef CREATE_TRACE_POINTS
            trace_mesh_update_path(mesh_path);
#endif
        } else {
            // Allocate a Mesh Path structure
            mesh_path = (struct rwnx_mesh_path *)kmalloc(sizeof(struct rwnx_mesh_path), GFP_ATOMIC);

            if (mesh_path) {
                INIT_LIST_HEAD(&mesh_path->list);

                mesh_path->path_idx = ind->path_idx;
                mesh_path->p_nhop_sta = &rwnx_hw->sta_table[ind->nhop_sta_idx];
                memcpy(&mesh_path->tgt_mac_addr, &ind->tgt_mac_addr, MAC_ADDR_LEN);

                // Insert the path in the list of path
                list_add_tail(&mesh_path->list, &rwnx_vif->ap.mpath_list);
#ifdef CREATE_TRACE_POINTS
                trace_mesh_create_path(mesh_path);
#endif
            }
        }
    }

    return 0;
}

static inline int rwnx_rx_mesh_proxy_update_ind(struct rwnx_hw *rwnx_hw,
                                               struct rwnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_proxy_update_ind *ind = (struct mesh_proxy_update_ind *)msg->param;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
    struct rwnx_mesh_proxy *mesh_proxy;
    bool found = false;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        return 1;

    if (!rwnx_vif || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT))
        return 0;

    /* Look for path with provided external STA address */
    list_for_each_entry(mesh_proxy, &rwnx_vif->ap.proxy_list, list) {
        if (!memcmp(&ind->ext_sta_addr, &mesh_proxy->ext_sta_addr, ETH_ALEN)) {
            found = true;
            break;
        }
    }

    if (ind->delete && found) {
        /* Delete mesh path */
        list_del_init(&mesh_proxy->list);
        kfree(mesh_proxy);
    } else if (!ind->delete && !found) {
        /* Allocate a Mesh Path structure */
        mesh_proxy = (struct rwnx_mesh_proxy *)kmalloc(sizeof(*mesh_proxy),
                                                       GFP_ATOMIC);

        if (mesh_proxy) {
            INIT_LIST_HEAD(&mesh_proxy->list);

            memcpy(&mesh_proxy->ext_sta_addr, &ind->ext_sta_addr, MAC_ADDR_LEN);
            mesh_proxy->local = ind->local;

            if (!ind->local) {
                memcpy(&mesh_proxy->proxy_addr, &ind->proxy_mac_addr, MAC_ADDR_LEN);
            }

            /* Insert the path in the list of path */
            list_add_tail(&mesh_proxy->list, &rwnx_vif->ap.proxy_list);
        }
    }

    return 0;
}
#endif /* CONFIG_RWNX_FULLMAC */

/***************************************************************************
 * Messages from APM task
 **************************************************************************/


/***************************************************************************
 * Messages from DEBUG task
 **************************************************************************/
static inline int rwnx_rx_dbg_error_ind(struct rwnx_hw *rwnx_hw,
                                        struct rwnx_cmd *cmd,
                                        struct ipc_e2a_msg *msg)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    return 0;
}

#ifdef CONFIG_RWNX_FULLMAC

static msg_cb_fct mm_hdlrs[MSG_I(MM_MAX)] = {
    [MSG_I(MM_CHANNEL_SWITCH_IND)]     = rwnx_rx_chan_switch_ind,
    [MSG_I(MM_CHANNEL_PRE_SWITCH_IND)] = rwnx_rx_chan_pre_switch_ind,
    [MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] = rwnx_rx_remain_on_channel_exp_ind,
    [MSG_I(MM_PS_CHANGE_IND)]          = rwnx_rx_ps_change_ind,
    [MSG_I(MM_TRAFFIC_REQ_IND)]        = rwnx_rx_traffic_req_ind,
    [MSG_I(MM_P2P_VIF_PS_CHANGE_IND)]  = rwnx_rx_p2p_vif_ps_change_ind,
    [MSG_I(MM_CSA_COUNTER_IND)]        = rwnx_rx_csa_counter_ind,
    [MSG_I(MM_CSA_FINISH_IND)]         = rwnx_rx_csa_finish_ind,
    [MSG_I(MM_CSA_TRAFFIC_IND)]        = rwnx_rx_csa_traffic_ind,
    [MSG_I(MM_CHANNEL_SURVEY_IND)]     = rwnx_rx_channel_survey_ind,
    [MSG_I(MM_P2P_NOA_UPD_IND)]        = rwnx_rx_p2p_noa_upd_ind,
    [MSG_I(MM_RSSI_STATUS_IND)]        = rwnx_rx_rssi_status_ind,
    [MSG_I(MM_PKTLOSS_IND)]            = rwnx_rx_pktloss_notify_ind,
    [MSG_I(MM_APM_STALOSS_IND)]        = rwnx_apm_staloss_ind,
};

static msg_cb_fct scan_hdlrs[MSG_I(SCANU_MAX)] = {
    [MSG_I(SCANU_START_CFM)]           = rwnx_rx_scanu_start_cfm,
    [MSG_I(SCANU_RESULT_IND)]          = rwnx_rx_scanu_result_ind,
};

static msg_cb_fct me_hdlrs[MSG_I(ME_MAX)] = {
    [MSG_I(ME_TKIP_MIC_FAILURE_IND)] = rwnx_rx_me_tkip_mic_failure_ind,
    [MSG_I(ME_TX_CREDITS_UPDATE_IND)] = rwnx_rx_me_tx_credits_update_ind,
};

static msg_cb_fct sm_hdlrs[MSG_I(SM_MAX)] = {
    [MSG_I(SM_CONNECT_IND)]    = rwnx_rx_sm_connect_ind,
    [MSG_I(SM_DISCONNECT_IND)] = rwnx_rx_sm_disconnect_ind,
    [MSG_I(SM_EXTERNAL_AUTH_REQUIRED_IND)] = rwnx_rx_sm_external_auth_required_ind,
};

static msg_cb_fct apm_hdlrs[MSG_I(APM_MAX)] = {
};

static msg_cb_fct mesh_hdlrs[MSG_I(MESH_MAX)] = {
    [MSG_I(MESH_PATH_CREATE_CFM)]  = rwnx_rx_mesh_path_create_cfm,
    [MSG_I(MESH_PEER_UPDATE_IND)]  = rwnx_rx_mesh_peer_update_ind,
    [MSG_I(MESH_PATH_UPDATE_IND)]  = rwnx_rx_mesh_path_update_ind,
    [MSG_I(MESH_PROXY_UPDATE_IND)] = rwnx_rx_mesh_proxy_update_ind,
};

#endif /* CONFIG_RWNX_FULLMAC */

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {
    [MSG_I(DBG_ERROR_IND)]                = rwnx_rx_dbg_error_ind,
};

static msg_cb_fct tdls_hdlrs[MSG_I(TDLS_MAX)] = {
    [MSG_I(TDLS_CHAN_SWITCH_CFM)] = rwnx_rx_tdls_chan_switch_cfm,
    [MSG_I(TDLS_CHAN_SWITCH_IND)] = rwnx_rx_tdls_chan_switch_ind,
    [MSG_I(TDLS_CHAN_SWITCH_BASE_IND)] = rwnx_rx_tdls_chan_switch_base_ind,
    [MSG_I(TDLS_PEER_PS_IND)] = rwnx_rx_tdls_peer_ps_ind,
};

static msg_cb_fct *msg_hdlrs[] = {
    [TASK_MM]    = mm_hdlrs,
    [TASK_DBG]   = dbg_hdlrs,
#ifdef CONFIG_RWNX_FULLMAC
    [TASK_TDLS]  = tdls_hdlrs,
    [TASK_SCANU] = scan_hdlrs,
    [TASK_ME]    = me_hdlrs,
    [TASK_SM]    = sm_hdlrs,
    [TASK_APM]   = apm_hdlrs,
    [TASK_MESH]  = mesh_hdlrs,
#endif /* CONFIG_RWNX_FULLMAC */
};

/**
 *
 */
void rwnx_rx_handle_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	//printk("%s(%d) MSG_T(msg->id):%d MSG_I(msg->id):%d cmd:%s\r\n", __func__, 
	//	msg->id,
	//	MSG_T(msg->id), 
	//	MSG_I(msg->id),
	//	rwnx_id2str[MSG_T(msg->id)][MSG_I(msg->id)]);
	
    rwnx_hw->cmd_mgr->msgind(rwnx_hw->cmd_mgr, msg,
                            msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
}

void rwnx_rx_handle_print(struct rwnx_hw *rwnx_hw, u8 *msg, u32 len)
{
    u8 *data_end = NULL;
    (void)data_end;

    if (!rwnx_hw || !rwnx_hw->fwlog_en) {
        pr_err("FWLOG-OVFL: %s", msg);
        return;
    }

    printk("FWLOG: %s", msg);

#ifdef CONFIG_RWNX_DEBUGFS
    data_end = rwnx_hw->debugfs.fw_log.buf.dataend;

    if (!rwnx_hw->debugfs.fw_log.buf.data)
        return ;

    //printk("end=%lx, len=%d\n", (unsigned long)rwnx_hw->debugfs.fw_log.buf.end, len);

    spin_lock_bh(&rwnx_hw->debugfs.fw_log.lock);

    if (rwnx_hw->debugfs.fw_log.buf.end + len > data_end) {
        int rem = data_end - rwnx_hw->debugfs.fw_log.buf.end;
        memcpy(rwnx_hw->debugfs.fw_log.buf.end, msg, rem);
        memcpy(rwnx_hw->debugfs.fw_log.buf.data, &msg[rem], len - rem);
        rwnx_hw->debugfs.fw_log.buf.end = rwnx_hw->debugfs.fw_log.buf.data + (len - rem);
    } else {
        memcpy(rwnx_hw->debugfs.fw_log.buf.end, msg, len);
        rwnx_hw->debugfs.fw_log.buf.end += len;
    }

    rwnx_hw->debugfs.fw_log.buf.size += len;
    if (rwnx_hw->debugfs.fw_log.buf.size > FW_LOG_SIZE)
        rwnx_hw->debugfs.fw_log.buf.size = FW_LOG_SIZE;

    spin_unlock_bh(&rwnx_hw->debugfs.fw_log.lock);
#endif
}

