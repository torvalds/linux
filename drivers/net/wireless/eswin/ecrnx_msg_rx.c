/**
 ****************************************************************************************
 *
 * @file ecrnx_msg_rx.c
 *
 * @brief RX function definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */
#include "ecrnx_defs.h"
#include "ecrnx_prof.h"
#include "ecrnx_tx.h"
#ifdef CONFIG_ECRNX_BFMER
#include "ecrnx_bfmer.h"
#endif //(CONFIG_ECRNX_BFMER)
#ifdef CONFIG_ECRNX_FULLMAC
#include "ecrnx_debugfs.h"
#include "ecrnx_msg_tx.h"
#include "ecrnx_tdls.h"
#endif /* CONFIG_ECRNX_FULLMAC */
#include "ecrnx_events.h"
#include "ecrnx_compat.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
#include <linux/time.h>
#endif

#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
#include "ecrnx_debugfs_func.h"
#endif
static int ecrnx_freq_to_idx(struct ecrnx_hw *ecrnx_hw, int freq)
{
    struct ieee80211_supported_band *sband;
    int band, ch, idx = 0;

#ifdef CONFIG_ECRNX_5G
    for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
#else
	for (band = NL80211_BAND_2GHZ; band <= NL80211_BAND_2GHZ; band++) {
#endif
#ifdef CONFIG_ECRNX_SOFTMAC
        sband = ecrnx_hw->hw->wiphy->bands[band];
#else
        sband = ecrnx_hw->wiphy->bands[band];
#endif /* CONFIG_ECRNX_SOFTMAC */
        if (!sband) {
            continue;
        }

        for (ch = 0; ch < sband->n_channels; ch++, idx++) {
            if (sband->channels[ch].center_freq == freq) {
                goto exit;
            }
        }
    }

	ECRNX_ERR("--!!!!!!!!error freq-----%d\n", freq);
    //BUG_ON(1);

exit:
    // Channel has been found, return the index
    return idx;
}

/***************************************************************************
 * Messages from MM task
 **************************************************************************/
static inline int ecrnx_rx_chan_pre_switch_ind(struct ecrnx_hw *ecrnx_hw,
                                              struct ecrnx_cmd *cmd,
                                              struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_chanctx *chan_ctxt;
#endif
    struct ecrnx_vif *ecrnx_vif;
    int chan_idx = ((struct mm_channel_pre_switch_ind *)msg->param)->chan_index;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    REG_SW_SET_PROFILING_CHAN(ecrnx_hw, SW_PROF_CHAN_CTXT_PSWTCH_BIT);

#ifdef CONFIG_ECRNX_SOFTMAC
    list_for_each_entry(chan_ctxt, &ecrnx_hw->chan_ctxts, list) {
        if (chan_ctxt->index == chan_idx) {
            chan_ctxt->active = false;
            break;
        }
    }

    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->chanctx && (ecrnx_vif->chanctx->index == chan_idx)) {
            ecrnx_txq_vif_stop(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
        }
    }
#else
    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->up && ecrnx_vif->ch_index == chan_idx) {
            ecrnx_txq_vif_stop(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
        }
    }
#endif /* CONFIG_ECRNX_SOFTMAC */

    REG_SW_CLEAR_PROFILING_CHAN(ecrnx_hw, SW_PROF_CHAN_CTXT_PSWTCH_BIT);

    return 0;
}

static inline int ecrnx_rx_chan_switch_ind(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_chanctx *chan_ctxt;
    struct ecrnx_sta *ecrnx_sta;
#endif
    struct ecrnx_vif *ecrnx_vif;
    int chan_idx = ((struct mm_channel_switch_ind *)msg->param)->chan_index;
    bool roc_req = ((struct mm_channel_switch_ind *)msg->param)->roc;
    bool roc_tdls = ((struct mm_channel_switch_ind *)msg->param)->roc_tdls;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    REG_SW_SET_PROFILING_CHAN(ecrnx_hw, SW_PROF_CHAN_CTXT_SWTCH_BIT);

#ifdef CONFIG_ECRNX_SOFTMAC
    if (roc_tdls) {
        u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;
        // Enable traffic only for TDLS station
        list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
            if (ecrnx_vif->vif_index == vif_index) {
                list_for_each_entry(ecrnx_sta, &ecrnx_vif->stations, list) {
                    if (ecrnx_sta->tdls.active) {
                        ecrnx_vif->roc_tdls = true;
                        ecrnx_txq_tdls_sta_start(ecrnx_sta, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
                        break;
                    }
                }
                break;
            }
        }
    } else if (!roc_req) {
        list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
            if (ecrnx_vif->chanctx && (ecrnx_vif->chanctx->index == chan_idx)) {
                ecrnx_txq_vif_start(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
            }
        }
    } else {
        u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;

        // Inform the host that the offchannel period has been started
        ieee80211_ready_on_channel(ecrnx_hw->hw);

        // Enable traffic for associated VIF (roc may happen without chanctx)
        list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
            if (ecrnx_vif->vif_index == vif_index) {
                ecrnx_txq_vif_start(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
            }
        }
    }

    /* keep cur_chan up to date */
    list_for_each_entry(chan_ctxt, &ecrnx_hw->chan_ctxts, list) {
        if (chan_ctxt->index == chan_idx) {
            chan_ctxt->active = true;
            ecrnx_hw->cur_freq = chan_ctxt->ctx->def.center_freq1;
            ecrnx_hw->cur_band = chan_ctxt->ctx->def.chan->band;
            if (chan_ctxt->ctx->def.chan->flags & IEEE80211_CHAN_RADAR) {
                ecrnx_radar_detection_enable(&ecrnx_hw->radar,
                                            ECRNX_RADAR_DETECT_REPORT,
                                            ECRNX_RADAR_RIU);
            } else {
                ecrnx_radar_detection_enable(&ecrnx_hw->radar,
                                            ECRNX_RADAR_DETECT_DISABLE,
                                            ECRNX_RADAR_RIU);
            }
            break;
        }
    }

#else
    if (roc_tdls) {
        u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;
        list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
            if (ecrnx_vif->vif_index == vif_index) {
                ecrnx_vif->roc_tdls = true;
                ecrnx_txq_tdls_sta_start(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
            }
        }
    } else if (!roc_req) {
        list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
            if (ecrnx_vif->up && ecrnx_vif->ch_index == chan_idx) {
                ecrnx_txq_vif_start(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
            }
        }
    } else {
        /* Retrieve the allocated RoC element */
        struct ecrnx_roc *roc = ecrnx_hw->roc;

        if (roc && roc->vif) {
            /* Get VIF on which RoC has been started */
            ecrnx_vif = roc->vif;
            /* For debug purpose (use ftrace kernel option) */
            trace_switch_roc(ecrnx_vif->vif_index);

            if (!roc->internal) {
            /* If mgmt_roc is true, remain on channel has been started by ourself */
                /* Inform the host that we have switch on the indicated off-channel */
                cfg80211_ready_on_channel(&ecrnx_vif->wdev, (u64)(ecrnx_hw->roc_cookie),
                                          roc->chan, roc->duration, GFP_ATOMIC);
            }

            /* Keep in mind that we have switched on the channel */
            roc->on_chan = true;
        }

        // Enable traffic on OFF channel queue
        ecrnx_txq_offchan_start(ecrnx_hw);
#if defined(CONFIG_ECRNX_P2P)
    if (roc && roc->internal) {
        ecrnx_hw->p2p_listen.rxdatas = 1;
        wake_up(&ecrnx_hw->p2p_listen.rxdataq);
    }
#endif
    }

    ecrnx_hw->cur_chanctx = chan_idx;
    ecrnx_radar_detection_enable_on_cur_channel(ecrnx_hw);

#endif /* CONFIG_ECRNX_SOFTMAC */

    REG_SW_CLEAR_PROFILING_CHAN(ecrnx_hw, SW_PROF_CHAN_CTXT_SWTCH_BIT);

    return 0;
}

static inline int ecrnx_rx_tdls_chan_switch_cfm(struct ecrnx_hw *ecrnx_hw,
                                                struct ecrnx_cmd *cmd,
                                                struct ipc_e2a_msg *msg)
{
    return 0;
}

static inline int ecrnx_rx_tdls_chan_switch_ind(struct ecrnx_hw *ecrnx_hw,
                                               struct ecrnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_chanctx *chan_ctxt;
    u8 chan_idx = ((struct tdls_chan_switch_ind *)msg->param)->chan_ctxt_index;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    // Enable channel context
    list_for_each_entry(chan_ctxt, &ecrnx_hw->chan_ctxts, list) {
        if (chan_ctxt->index == chan_idx) {
            chan_ctxt->active = true;
            ecrnx_hw->cur_freq = chan_ctxt->ctx->def.center_freq1;
            ecrnx_hw->cur_band = chan_ctxt->ctx->def.chan->band;
        }
    }

    return 0;
#else
    // Enable traffic on OFF channel queue
    ecrnx_txq_offchan_start(ecrnx_hw);

    return 0;
#endif
}

static inline int ecrnx_rx_tdls_chan_switch_base_ind(struct ecrnx_hw *ecrnx_hw,
                                                    struct ecrnx_cmd *cmd,
                                                    struct ipc_e2a_msg *msg)
{
    struct ecrnx_vif *ecrnx_vif;
    u8 vif_index = ((struct tdls_chan_switch_base_ind *)msg->param)->vif_index;
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_sta *ecrnx_sta;
#endif

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

#ifdef CONFIG_ECRNX_SOFTMAC
    // Disable traffic for associated VIF
    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->vif_index == vif_index) {
            if (ecrnx_vif->chanctx)
                ecrnx_vif->chanctx->active = false;
            list_for_each_entry(ecrnx_sta, &ecrnx_vif->stations, list) {
                if (ecrnx_sta->tdls.active) {
                    ecrnx_vif->roc_tdls = false;
                    ecrnx_txq_tdls_sta_stop(ecrnx_sta, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
                    break;
                }
            }
            break;
        }
    }
    return 0;
#else
    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->vif_index == vif_index) {
            ecrnx_vif->roc_tdls = false;
            ecrnx_txq_tdls_sta_stop(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
        }
    }
    return 0;
#endif
}

static inline int ecrnx_rx_tdls_peer_ps_ind(struct ecrnx_hw *ecrnx_hw,
                                           struct ecrnx_cmd *cmd,
                                           struct ipc_e2a_msg *msg)
{
    struct ecrnx_vif *ecrnx_vif;
    u8 vif_index = ((struct tdls_peer_ps_ind *)msg->param)->vif_index;
    bool ps_on = ((struct tdls_peer_ps_ind *)msg->param)->ps_on;

#ifdef CONFIG_ECRNX_SOFTMAC
    u8 sta_idx = ((struct tdls_peer_ps_ind *)msg->param)->sta_idx;
    struct ecrnx_sta *ecrnx_sta;
    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->vif_index == vif_index) {
            list_for_each_entry(ecrnx_sta, &ecrnx_vif->stations, list) {
                if (ecrnx_sta->sta_idx == sta_idx) {
                    ecrnx_sta->tdls.ps_on = ps_on;
                    if (ps_on) {
                        // disable TXQ for TDLS peer
                        ecrnx_txq_tdls_sta_stop(ecrnx_sta, ECRNX_TXQ_STOP_STA_PS, ecrnx_hw);
                    } else {
                        // Enable TXQ for TDLS peer
                        ecrnx_txq_tdls_sta_start(ecrnx_sta, ECRNX_TXQ_STOP_STA_PS, ecrnx_hw);
                    }
                    break;
                }
            }
            break;
        }
    }
    return 0;
#else
    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->vif_index == vif_index) {
            ecrnx_vif->sta.tdls_sta->tdls.ps_on = ps_on;
            // Update PS status for the TDLS station
            ecrnx_ps_bh_enable(ecrnx_hw, ecrnx_vif->sta.tdls_sta, ps_on);
        }
    }

    return 0;
#endif
}

static inline int ecrnx_rx_remain_on_channel_exp_ind(struct ecrnx_hw *ecrnx_hw,
                                                    struct ecrnx_cmd *cmd,
                                                    struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_vif *ecrnx_vif;
    u8 vif_index = ((struct mm_remain_on_channel_exp_ind *)msg->param)->vif_index;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    ieee80211_remain_on_channel_expired(ecrnx_hw->hw);

    // Disable traffic for associated VIF
    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->vif_index == vif_index) {
            if (ecrnx_vif->chanctx)
                ecrnx_vif->chanctx->active = false;

            ecrnx_txq_vif_stop(ecrnx_vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
            break;
        }
    }

    return 0;

#else
    if(!ecrnx_hw->roc || !ecrnx_hw->roc->chan){
        ECRNX_ERR("error!!!:ecrnx_hw->roc or !ecrnx_hw->roc->chan is null \n");
        return 0;
    }
    /* Retrieve the allocated RoC element */
    struct ecrnx_roc *roc = ecrnx_hw->roc;
    /* Get VIF on which RoC has been started */
    struct ecrnx_vif *ecrnx_vif = roc->vif;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* For debug purpose (use ftrace kernel option) */
    trace_roc_exp(ecrnx_vif->vif_index);

    /* If mgmt_roc is true, remain on channel has been started by ourself */
    /* If RoC has been cancelled before we switched on channel, do not call cfg80211 */
    if (!roc->internal && roc->on_chan) {
        /* Inform the host that off-channel period has expired */
        cfg80211_remain_on_channel_expired(&ecrnx_vif->wdev, (u64)(ecrnx_hw->roc_cookie),
                                           roc->chan, GFP_ATOMIC);
    }

    /* De-init offchannel TX queue */
    ecrnx_txq_offchan_deinit(ecrnx_vif);

    /* Increase the cookie counter cannot be zero */
    ecrnx_hw->roc_cookie++;

    if (ecrnx_hw->roc_cookie == 0)
        ecrnx_hw->roc_cookie = 1;

#if CONFIG_ECRNX_P2P
	ecrnx_hw->p2p_listen.listen_started = 0;
#endif

    /* Free the allocated RoC element */
    kfree(roc);
    ecrnx_hw->roc = NULL;
    
#if defined(CONFIG_ECRNX_P2P)
	wake_up(&ecrnx_hw->p2p_listen.rxdataq);
#endif

#endif /* CONFIG_ECRNX_SOFTMAC */
    return 0;
}

static inline int ecrnx_rx_p2p_vif_ps_change_ind(struct ecrnx_hw *ecrnx_hw,
                                                struct ecrnx_cmd *cmd,
                                                struct ipc_e2a_msg *msg)
{
    int vif_idx  = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->vif_index;
    int ps_state = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->ps_state;
    struct ecrnx_vif *vif_entry;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

#ifdef CONFIG_ECRNX_SOFTMAC
    // Look for VIF entry
    list_for_each_entry(vif_entry, &ecrnx_hw->vifs, list) {
        if (vif_entry->vif_index == vif_idx) {
            goto found_vif;
        }
    }
#else
    vif_entry = ecrnx_hw->vif_table[vif_idx];

    if (vif_entry) {
        goto found_vif;
    }
#endif /* CONFIG_ECRNX_SOFTMAC */

    goto exit;

found_vif:

#ifdef CONFIG_ECRNX_SOFTMAC
    if (ps_state == MM_PS_MODE_OFF) {
        ecrnx_txq_vif_start(vif_entry, ECRNX_TXQ_STOP_VIF_PS, ecrnx_hw);
    }
    else {
        ecrnx_txq_vif_stop(vif_entry, ECRNX_TXQ_STOP_VIF_PS, ecrnx_hw);
    }
#else
    if (ps_state == MM_PS_MODE_OFF) {
        // Start TX queues for provided VIF
        ecrnx_txq_vif_start(vif_entry, ECRNX_TXQ_STOP_VIF_PS, ecrnx_hw);
    }
    else {
        // Stop TX queues for provided VIF
        ecrnx_txq_vif_stop(vif_entry, ECRNX_TXQ_STOP_VIF_PS, ecrnx_hw);
    }
#endif /* CONFIG_ECRNX_SOFTMAC */

exit:
    return 0;
}

static inline int ecrnx_rx_channel_survey_ind(struct ecrnx_hw *ecrnx_hw,
                                             struct ecrnx_cmd *cmd,
                                             struct ipc_e2a_msg *msg)
{
    struct mm_channel_survey_ind *ind = (struct mm_channel_survey_ind *)msg->param;
    // Get the channel index
    int idx = ecrnx_freq_to_idx(ecrnx_hw, ind->freq);
    // Get the survey
    struct ecrnx_survey_info *ecrnx_survey;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (idx >  ARRAY_SIZE(ecrnx_hw->survey))
        return 0;

    ecrnx_survey = &ecrnx_hw->survey[idx];

    // Store the received parameters
    ecrnx_survey->chan_time_ms = ind->chan_time_ms;
    ecrnx_survey->chan_time_busy_ms = ind->chan_time_busy_ms;
    ecrnx_survey->noise_dbm = ind->noise_dbm;
    ecrnx_survey->filled = (SURVEY_INFO_TIME |
                           SURVEY_INFO_TIME_BUSY);

    if (ind->noise_dbm != 0) {
        ecrnx_survey->filled |= SURVEY_INFO_NOISE_DBM;
    }

#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
	ecrnx_debugfs_noise_of_survey_info_update(ecrnx_hw, ecrnx_survey, idx);
#endif

    return 0;
}

static inline int ecrnx_rx_p2p_noa_upd_ind(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    return 0;
}

static inline int ecrnx_rx_rssi_status_ind(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_rssi_status_ind *ind = (struct mm_rssi_status_ind *)msg->param;
    int vif_idx  = ind->vif_index;
    bool rssi_status = ind->rssi_status;

    struct ecrnx_vif *vif_entry;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

#ifdef CONFIG_ECRNX_SOFTMAC
    list_for_each_entry(vif_entry, &ecrnx_hw->vifs, list) {
        if (vif_entry->vif_index == vif_idx) {
            ecrnx_ieee80211_cqm_rssi_notify(vif_entry->vif,
                                      rssi_status ? NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW :
                                                    NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
                                      ind->rssi, GFP_ATOMIC);
        }
    }
#else
    vif_entry = ecrnx_hw->vif_table[vif_idx];
    if (vif_entry) {
        ecrnx_cfg80211_cqm_rssi_notify(vif_entry->ndev,
                                 rssi_status ? NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW :
                                               NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
                                 ind->rssi, GFP_ATOMIC);
    }
#endif /* CONFIG_ECRNX_SOFTMAC */

    return 0;
}

static inline int ecrnx_rx_pktloss_notify_ind(struct ecrnx_hw *ecrnx_hw,
                                             struct ecrnx_cmd *cmd,
                                             struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_ECRNX_FULLMAC
    struct mm_pktloss_ind *ind = (struct mm_pktloss_ind *)msg->param;
    struct ecrnx_vif *vif_entry;
    int vif_idx  = ind->vif_index;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    vif_entry = ecrnx_hw->vif_table[vif_idx];
    if (vif_entry) {
        cfg80211_cqm_pktloss_notify(vif_entry->ndev, (const u8 *)ind->mac_addr.array,
                                    ind->num_packets, GFP_ATOMIC);
    }
#endif /* CONFIG_ECRNX_FULLMAC */

    return 0;
}

static inline int ecrnx_rx_csa_counter_ind(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_csa_counter_ind *ind = (struct mm_csa_counter_ind *)msg->param;
    struct ecrnx_vif *vif;
    bool found = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &ecrnx_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
#ifdef CONFIG_ECRNX_SOFTMAC
        if (ind->csa_count == 1)
            ieee80211_csa_finish(vif->vif);
        else
            ieee80211_csa_update_counter(vif->vif);
#else
        if (vif->ap.csa)
            vif->ap.csa->count = ind->csa_count;
        else
            netdev_err(vif->ndev, "CSA counter update but no active CSA");

#endif
    }

    return 0;
}

#ifdef CONFIG_ECRNX_SOFTMAC
static inline int ecrnx_rx_connection_loss_ind(struct ecrnx_hw *ecrnx_hw,
                                              struct ecrnx_cmd *cmd,
                                              struct ipc_e2a_msg *msg)
{
    struct ecrnx_vif *ecrnx_vif;
    u8 inst_nbr;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    inst_nbr = ((struct mm_connection_loss_ind *)msg->param)->inst_nbr;

    /* Search the VIF entry corresponding to the instance number */
    list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
        if (ecrnx_vif->vif_index == inst_nbr) {
            ieee80211_connection_loss(ecrnx_vif->vif);
            break;
        }
    }

    return 0;
}


#ifdef CONFIG_ECRNX_BCN
static inline int ecrnx_rx_prm_tbtt_ind(struct ecrnx_hw *ecrnx_hw,
                                       struct ecrnx_cmd *cmd,
                                       struct ipc_e2a_msg *msg)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    ecrnx_tx_bcns(ecrnx_hw);

    return 0;
}
#endif

#else /* !CONFIG_ECRNX_SOFTMAC */
static inline int ecrnx_rx_csa_finish_ind(struct ecrnx_hw *ecrnx_hw,
                                         struct ecrnx_cmd *cmd,
                                         struct ipc_e2a_msg *msg)
{
    struct mm_csa_finish_ind *ind = (struct mm_csa_finish_ind *)msg->param;
    struct ecrnx_vif *vif;
    bool found = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &ecrnx_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
        if (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP ||
            ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) {
            if (vif->ap.csa) {
                vif->ap.csa->status = ind->status;
                vif->ap.csa->ch_idx = ind->chan_idx;
                schedule_work(&vif->ap.csa->work);
            } else
                netdev_err(vif->ndev, "CSA finish indication but no active CSA");
        } else {
            if (ind->status == 0) {
                ecrnx_chanctx_unlink(vif);
                ecrnx_chanctx_link(vif, ind->chan_idx, NULL);
                if (ecrnx_hw->cur_chanctx == ind->chan_idx) {
                    ecrnx_radar_detection_enable_on_cur_channel(ecrnx_hw);
                    ecrnx_txq_vif_start(vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
                } else
                    ecrnx_txq_vif_stop(vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
            }
        }
    }

    return 0;
}

static inline int ecrnx_rx_csa_traffic_ind(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_csa_traffic_ind *ind = (struct mm_csa_traffic_ind *)msg->param;
    struct ecrnx_vif *vif;
    bool found = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &ecrnx_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
        if (ind->enable)
            ecrnx_txq_vif_start(vif, ECRNX_TXQ_STOP_CSA, ecrnx_hw);
        else
            ecrnx_txq_vif_stop(vif, ECRNX_TXQ_STOP_CSA, ecrnx_hw);
    }

    return 0;
}

static inline int ecrnx_rx_ps_change_ind(struct ecrnx_hw *ecrnx_hw,
                                        struct ecrnx_cmd *cmd,
                                        struct ipc_e2a_msg *msg)
{
    struct mm_ps_change_ind *ind = (struct mm_ps_change_ind *)msg->param;
    struct ecrnx_sta *sta = &ecrnx_hw->sta_table[ind->sta_idx];

    if (ind->sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
        wiphy_err(ecrnx_hw->wiphy, "Invalid sta index reported by fw %d\n",
                  ind->sta_idx);
        return 1;
    }

    netdev_dbg(ecrnx_hw->vif_table[sta->vif_idx]->ndev,
               "Sta %d, change PS mode to %s", sta->sta_idx,
               ind->ps_state ? "ON" : "OFF");

    ECRNX_DBG("Sta:0x%p, sta_idx:%d, sta->valid:%d, sta_mac:%pM, change PS mode to: %s \n",sta, sta->sta_idx, sta->valid, sta->mac_addr, ind->ps_state ? "ON" : "OFF");

    if (sta->valid) {
        ecrnx_ps_bh_enable(ecrnx_hw, sta, ind->ps_state);
    } else if (test_bit(ECRNX_DEV_ADDING_STA, &ecrnx_hw->flags)) {
        sta->ps.active = ind->ps_state ? true : false;
    } else {
        netdev_err(ecrnx_hw->vif_table[sta->vif_idx]->ndev,
                   "Ignore PS mode change on invalid sta\n");
    }

    return 0;
}


static inline int ecrnx_rx_traffic_req_ind(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct mm_traffic_req_ind *ind = (struct mm_traffic_req_ind *)msg->param;
    struct ecrnx_sta *sta = &ecrnx_hw->sta_table[ind->sta_idx];

    ECRNX_DBG("%s-%d:Sta:0x%p, sta_idx:%d, sta->valid:%d, sta_mac:%pM \n",__func__, __LINE__, sta, ind->sta_idx, sta->valid, sta->mac_addr);

    netdev_dbg(ecrnx_hw->vif_table[sta->vif_idx]->ndev,
               "Sta %d, asked for %d pkt", sta->sta_idx, ind->pkt_cnt);

    ecrnx_ps_bh_traffic_req(ecrnx_hw, sta, ind->pkt_cnt,
                           ind->uapsd ? UAPSD_ID : LEGACY_PS_ID);

    return 0;
}
#endif /* CONFIG_ECRNX_SOFTMAC */

/***************************************************************************
 * Messages from SCAN task
 **************************************************************************/
#ifdef CONFIG_ECRNX_SOFTMAC
static inline int ecrnx_rx_scan_done_ind(struct ecrnx_hw *ecrnx_hw,
                                        struct ecrnx_cmd *cmd,
                                        struct ipc_e2a_msg *msg)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    struct cfg80211_scan_info info = {
        .aborted = false,
    };
#endif
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &ecrnx_hw->scan_ie);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    ieee80211_scan_completed(ecrnx_hw->hw, &info);
#else
    ieee80211_scan_completed(ecrnx_hw->hw, false);
#endif

    return 0;
}
#endif /* CONFIG_ECRNX_SOFTMAC */

/***************************************************************************
 * Messages from SCANU task
 **************************************************************************/
#ifdef CONFIG_ECRNX_FULLMAC
static inline int ecrnx_scanu_cancel_cfm(struct ecrnx_hw *ecrnx_hw,
                                           struct ecrnx_cmd *cmd,
                                           struct ipc_e2a_msg *msg)
{
    struct scanu_cancel_cfm *cfm = (struct scanu_cancel_cfm *)msg;
    bool abort = false;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    struct cfg80211_scan_info info = {
        .aborted = abort,
    };
#endif

    ECRNX_PRINT("%s: cfm status:%d, scan_request:0x%p \n", __func__, cfm->status, ecrnx_hw->scan_request);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    cfg80211_scan_done(ecrnx_hw->scan_request, &info);
#else
    cfg80211_scan_done(ecrnx_hw->scan_request, false);
#endif
    ecrnx_hw->scan_request = NULL;

    return 0;
}

static inline int ecrnx_rx_scanu_start_cfm(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_cmd *cmd,
                                          struct ipc_e2a_msg *msg)
{
    struct scanu_start_cfm* cfm = (struct scanu_start_cfm*)msg->param;
    u8_l abort_status;
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    ECRNX_DBG("receive scanu cfm, status:%d \n", cfm->status);
    abort_status = cfm->status?true:false;

#ifndef CONFIG_ECRNX_ESWIN
    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &ecrnx_hw->scan_ie);
#endif

    spin_lock_bh(&ecrnx_hw->scan_req_lock);
    if (ecrnx_hw->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        struct cfg80211_scan_info info = {
            .aborted = abort_status,
        };

        ECRNX_PRINT("%s: cfm status:%d, scan_request:0x%p \n", __func__, cfm->status, ecrnx_hw->scan_request);
        cfg80211_scan_done(ecrnx_hw->scan_request, &info);
#else
        cfg80211_scan_done(ecrnx_hw->scan_request, abort_status);
#endif
    }

    ecrnx_hw->scan_request = NULL;
    spin_unlock_bh(&ecrnx_hw->scan_req_lock);

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
u64_l getBootTime(void)
{
	struct timespec64 ts;
	u64_l bootTime = 0;

	ts = ktime_to_timespec64(ktime_get_boottime());
	bootTime = ts.tv_sec;
	bootTime *= 1000000;
	bootTime += ts.tv_nsec / 1000;
	return bootTime;
}
#endif
static inline int ecrnx_rx_scanu_result_ind(struct ecrnx_hw *ecrnx_hw,
                                           struct ecrnx_cmd *cmd,
                                           struct ipc_e2a_msg *msg)
{
    struct cfg80211_bss *bss = NULL;
    struct ieee80211_channel *chan;
    struct scanu_result_ind *ind = (struct scanu_result_ind *)msg->param;
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)ind->payload;
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    chan = ieee80211_get_channel(ecrnx_hw->wiphy, ind->center_freq);

    if (chan != NULL)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
        if(ieee80211_is_beacon(mgmt->frame_control))
        {
             mgmt->u.beacon.timestamp = getBootTime();
        }
        if(ieee80211_is_probe_resp(mgmt->frame_control))
        {
             mgmt->u.probe_resp.timestamp = getBootTime();
        }
#endif
        bss = cfg80211_inform_bss_frame(ecrnx_hw->wiphy, chan,
                                        mgmt,
                                        ind->length, ind->rssi * 100, GFP_ATOMIC);
    }

    if (bss != NULL)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
        cfg80211_put_bss(ecrnx_hw->wiphy, bss);
#else
        cfg80211_put_bss(bss);
#endif

#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
	ecrnx_debugfs_survey_info_update(ecrnx_hw, bss);
#endif

    return 0;
}
#endif /* CONFIG_ECRNX_FULLMAC */

/***************************************************************************
 * Messages from ME task
 **************************************************************************/
#ifdef CONFIG_ECRNX_FULLMAC
static inline int ecrnx_rx_me_tkip_mic_failure_ind(struct ecrnx_hw *ecrnx_hw,
                                                  struct ecrnx_cmd *cmd,
                                                  struct ipc_e2a_msg *msg)
{
    struct me_tkip_mic_failure_ind *ind = (struct me_tkip_mic_failure_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct net_device *dev = ecrnx_vif->ndev;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    cfg80211_michael_mic_failure(dev, (u8 *)&ind->addr, (ind->ga?NL80211_KEYTYPE_GROUP:
                                 NL80211_KEYTYPE_PAIRWISE), ind->keyid,
                                 (u8 *)&ind->tsc, GFP_ATOMIC);

    return 0;
}

static inline int ecrnx_rx_me_tx_credits_update_ind(struct ecrnx_hw *ecrnx_hw,
                                                   struct ecrnx_cmd *cmd,
                                                   struct ipc_e2a_msg *msg)
{
    struct me_tx_credits_update_ind *ind = (struct me_tx_credits_update_ind *)msg->param;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    ecrnx_txq_credit_update(ecrnx_hw, ind->sta_idx, ind->tid, ind->credits);

    return 0;
}
#endif /* CONFIG_ECRNX_FULLMAC */

/***************************************************************************
 * Messages from SM task
 **************************************************************************/
#ifdef CONFIG_ECRNX_FULLMAC
static inline int ecrnx_rx_sm_connect_ind(struct ecrnx_hw *ecrnx_hw,
                                         struct ecrnx_cmd *cmd,
                                         struct ipc_e2a_msg *msg)
{
    struct sm_connect_ind *ind = (struct sm_connect_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct net_device *dev = ecrnx_vif->ndev;
    const u8 *req_ie, *rsp_ie;
    const u8 *extcap_ie;
    const struct ieee_types_extcap *extcap;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    spin_lock_bh(&ecrnx_hw->connect_req_lock);
    /* Retrieve IE addresses and lengths */
    req_ie = (const u8 *)ind->assoc_ie_buf;
    rsp_ie = req_ie + ind->assoc_req_ie_len;

    // Fill-in the AP information
    if (ind->status_code == 0)
    {
        struct ecrnx_sta *sta = &ecrnx_hw->sta_table[ind->ap_idx];
        u8 txq_status;
        struct ieee80211_channel *chan;
        struct cfg80211_chan_def chandef;

        sta->valid = true;
        memset(&sta->rx_pn, 0, TID_MAX * sizeof(uint64_t));
        memset(&ecrnx_vif->rx_pn, 0, TID_MAX * sizeof(uint64_t));
        sta->sta_idx = ind->ap_idx;
        sta->ch_idx = ind->ch_idx;
        sta->vif_idx = ind->vif_idx;
        sta->vlan_idx = sta->vif_idx;
        sta->qos = ind->qos;
        sta->acm = ind->acm;
        sta->ps.active = false;
        sta->aid = ind->aid;
        sta->band = ind->chan.band;
        sta->width = ind->chan.type;
        sta->center_freq = ind->chan.prim20_freq;
        sta->center_freq1 = ind->chan.center1_freq;
        sta->center_freq2 = ind->chan.center2_freq;
        ecrnx_vif->sta.ap = sta;
        ecrnx_vif->generation++;
        chan = ieee80211_get_channel(ecrnx_hw->wiphy, ind->chan.prim20_freq);
        cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
        if (!ecrnx_hw->mod_params->ht_on)
            chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
        else
            chandef.width = chnl2bw[ind->chan.type];
        chandef.center_freq1 = ind->chan.center1_freq;
        chandef.center_freq2 = ind->chan.center2_freq;
        ecrnx_chanctx_link(ecrnx_vif, ind->ch_idx, &chandef);
        memcpy(sta->mac_addr, ind->bssid.array, ETH_ALEN);
        if (ind->ch_idx == ecrnx_hw->cur_chanctx) {
            txq_status = 0;
        } else {
            txq_status = ECRNX_TXQ_STOP_CHAN;
        }
        memcpy(sta->ac_param, ind->ac_param, sizeof(sta->ac_param));
        ecrnx_txq_sta_init(ecrnx_hw, sta, txq_status);
        ecrnx_rx_reord_sta_init(ecrnx_hw, ecrnx_hw->vif_table[sta->vif_idx], sta->sta_idx);
        ecrnx_dbgfs_register_sta(ecrnx_hw, sta);
        ECRNX_PRINT("ecrnx_rx_sm_connect_ind, mac[%02x:%02x:%02x:%02x:%02x:%02x], status_code:%d \n", \
                sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2], sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5], ind->status_code);
        ecrnx_txq_tdls_vif_init(ecrnx_vif);
        ecrnx_mu_group_sta_init(sta, NULL);
        /* Look for TDLS Channel Switch Prohibited flag in the Extended Capability
         * Information Element*/
        extcap_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, rsp_ie, ind->assoc_rsp_ie_len);
        if (extcap_ie && extcap_ie[1] >= 5) {
            extcap = (void *)(extcap_ie);
            ecrnx_vif->tdls_chsw_prohibited = extcap->ext_capab[4] & WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED;
        }

#ifdef CONFIG_ECRNX_BFMER
        /* If Beamformer feature is activated, check if features can be used
         * with the new peer device
         */
        if (ecrnx_hw->mod_params->bfmer) {
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
                ecrnx_send_bfmer_enable(ecrnx_hw, sta, vht_cap);
            } while (0);
        }
#endif //(CONFIG_ECRNX_BFMER)

#ifdef CONFIG_ECRNX_MON_DATA
        // If there are 1 sta and 1 monitor interface active at the same time then
        // monitor interface channel context is always the same as the STA interface.
        // This doesn't work with 2 STA interfaces but we don't want to support it.
        if (ecrnx_hw->monitor_vif != ECRNX_INVALID_VIF) {
            struct ecrnx_vif *ecrnx_mon_vif = ecrnx_hw->vif_table[ecrnx_hw->monitor_vif];
            ecrnx_chanctx_unlink(ecrnx_mon_vif);
            ecrnx_chanctx_link(ecrnx_mon_vif, ind->ch_idx, NULL);
        }
#endif
    }

    if (ind->roamed) {
        struct cfg80211_roam_info info;
        memset(&info, 0, sizeof(info));
        if (ecrnx_vif->ch_index < NX_CHAN_CTXT_CNT)
            info.channel = ecrnx_hw->chanctx_table[ecrnx_vif->ch_index].chan_def.chan;
        info.bssid = (const u8 *)ind->bssid.array;
        info.req_ie = req_ie;
        info.req_ie_len = ind->assoc_req_ie_len;
        info.resp_ie = rsp_ie;
        info.resp_ie_len = ind->assoc_rsp_ie_len;
        cfg80211_roamed(dev, &info, GFP_ATOMIC);
    } else {
        cfg80211_connect_result(dev, (const u8 *)ind->bssid.array, req_ie,
                                ind->assoc_req_ie_len, rsp_ie,
                                ind->assoc_rsp_ie_len, ind->status_code,
                                GFP_ATOMIC);
    }

    netif_tx_start_all_queues(dev);
    netif_carrier_on(dev);
    spin_unlock_bh(&ecrnx_hw->connect_req_lock);

    return 0;
}

static inline int ecrnx_rx_sm_disconnect_ind(struct ecrnx_hw *ecrnx_hw,
                                            struct ecrnx_cmd *cmd,
                                            struct ipc_e2a_msg *msg)
{
    struct sm_disconnect_ind *ind = (struct sm_disconnect_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct net_device *dev = ecrnx_vif->ndev;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    ECRNX_PRINT("%s:dev:%p, vif_up:%d, reason_code:%d \n", __func__, dev, ecrnx_vif->up, ind->reason_code);
    /* if vif is not up, ecrnx_close has already been called */
    if (ecrnx_vif->up) {
        if (!ind->reassoc) {
            cfg80211_disconnected(dev, ind->reason_code, NULL, 0,
                                  (ind->reason_code <= 1), GFP_ATOMIC);
            if (ecrnx_vif->sta.ft_assoc_ies) {
                kfree(ecrnx_vif->sta.ft_assoc_ies);
                ecrnx_vif->sta.ft_assoc_ies = NULL;
                ecrnx_vif->sta.ft_assoc_ies_len = 0;
            }
        }
        netif_tx_stop_all_queues(dev);
        netif_carrier_off(dev);
    }

    if (ecrnx_vif->sta.ap && ecrnx_vif->sta.ap->valid)
    {
        ecrnx_dbgfs_unregister_sta(ecrnx_hw, ecrnx_vif->sta.ap);
#ifdef CONFIG_ECRNX_BFMER
        /* Disable Beamformer if supported */
        ecrnx_bfmer_report_del(ecrnx_hw, ecrnx_vif->sta.ap);
#endif //(CONFIG_ECRNX_BFMER)

        ecrnx_txq_sta_deinit(ecrnx_hw, ecrnx_vif->sta.ap);
        ecrnx_rx_reord_sta_deinit(ecrnx_hw, ecrnx_vif->sta.ap->sta_idx, true);
        ecrnx_vif->sta.ap->valid = false;
        ecrnx_vif->sta.ap = NULL;
    }
    ecrnx_txq_tdls_vif_deinit(ecrnx_vif);
    //ecrnx_dbgfs_unregister_sta(ecrnx_hw, ecrnx_vif->sta.ap);
    ecrnx_vif->generation++;
    ecrnx_external_auth_disable(ecrnx_vif);
    ecrnx_chanctx_unlink(ecrnx_vif);

    return 0;
}

static inline int ecrnx_rx_sm_external_auth_required_ind(struct ecrnx_hw *ecrnx_hw,
                                                        struct ecrnx_cmd *cmd,
                                                        struct ipc_e2a_msg *msg)
{
    struct sm_external_auth_required_ind *ind =
        (struct sm_external_auth_required_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
    struct net_device *dev = ecrnx_vif->ndev;
    struct cfg80211_external_auth_params params;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    params.action = NL80211_EXTERNAL_AUTH_START;
    memcpy(params.bssid, ind->bssid.array, ETH_ALEN);
    params.ssid.ssid_len = ind->ssid.length;
    memcpy(params.ssid.ssid, ind->ssid.array,
           min_t(size_t, ind->ssid.length, sizeof(params.ssid.ssid)));
    params.key_mgmt_suite = ind->akm;

    if ((ind->vif_idx > NX_VIRT_DEV_MAX) || !ecrnx_vif->up ||
        (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_STATION) ||
        cfg80211_external_auth_request(dev, &params, GFP_ATOMIC)) {
        wiphy_err(ecrnx_hw->wiphy, "Failed to start external auth on vif %d",
                  ind->vif_idx);
        ecrnx_send_sm_external_auth_required_rsp(ecrnx_hw, ecrnx_vif,
                                                WLAN_STATUS_UNSPECIFIED_FAILURE);
        return 0;
    }

    ecrnx_external_auth_enable(ecrnx_vif);
#else
    ecrnx_send_sm_external_auth_required_rsp(ecrnx_hw, ecrnx_vif,
                                            WLAN_STATUS_UNSPECIFIED_FAILURE);
#endif
    return 0;
}

static inline int ecrnx_rx_sm_ft_auth_ind(struct ecrnx_hw *ecrnx_hw,
                                         struct ecrnx_cmd *cmd,
                                         struct ipc_e2a_msg *msg)
{
    struct sm_ft_auth_ind *ind = (struct sm_ft_auth_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct sk_buff *skb;
    size_t data_len = (offsetof(struct ieee80211_mgmt, u.auth.variable) +
                       ind->ft_ie_len);
    skb = dev_alloc_skb(data_len);
    if (skb) {
        struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb_put(skb, data_len);
        mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
        memcpy(mgmt->u.auth.variable, ind->ft_ie_buf, ind->ft_ie_len);
        ecrnx_rx_defer_skb(ecrnx_hw, ecrnx_vif, skb);
        dev_kfree_skb(skb);
    } else {
        netdev_warn(ecrnx_vif->ndev, "Allocation failed for FT auth ind\n");
    }
    return 0;
}
static inline int ecrnx_rx_twt_setup_ind(struct ecrnx_hw *ecrnx_hw,
                                        struct ecrnx_cmd *cmd,
                                        struct ipc_e2a_msg *msg)
{
    struct twt_setup_ind *ind = (struct twt_setup_ind *)msg->param;
    struct ecrnx_sta *ecrnx_sta = &ecrnx_hw->sta_table[ind->sta_idx];
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    memcpy(&ecrnx_sta->twt_ind, ind, sizeof(struct twt_setup_ind));
    return 0;
}

static inline int ecrnx_rx_mesh_path_create_cfm(struct ecrnx_hw *ecrnx_hw,
                                               struct ecrnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_path_create_cfm *cfm = (struct mesh_path_create_cfm *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[cfm->vif_idx];

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* Check we well have a Mesh Point Interface */
    if (ecrnx_vif && (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_MESH_POINT))
        ecrnx_vif->ap.flags &= ~ECRNX_AP_CREATE_MESH_PATH;

    return 0;
}

static inline int ecrnx_rx_mesh_peer_update_ind(struct ecrnx_hw *ecrnx_hw,
                                               struct ecrnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_peer_update_ind *ind = (struct mesh_peer_update_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct ecrnx_sta *ecrnx_sta = &ecrnx_hw->sta_table[ind->sta_idx];

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if ((ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX)) ||
        (ecrnx_vif && (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)) ||
        (ind->sta_idx >= NX_REMOTE_STA_MAX))
        return 1;

    if (ecrnx_vif->ap.flags & ECRNX_AP_USER_MESH_PM)
    {
        if (!ind->estab && ecrnx_sta->valid) {
            ecrnx_sta->ps.active = false;
            ecrnx_sta->valid = false;
            list_del_init(&ecrnx_sta->list);
            ecrnx_txq_sta_deinit(ecrnx_hw, ecrnx_sta);
            ecrnx_dbgfs_unregister_sta(ecrnx_hw, ecrnx_sta);
        } else {
            WARN_ON(0);
        }
    } else {
        /* Check if peer link has been established or lost */
        if (ind->estab) {
            if (!ecrnx_sta->valid) {
                u8 txq_status;

                ecrnx_sta->valid = true;
                ecrnx_sta->sta_idx = ind->sta_idx;
                ecrnx_sta->ch_idx = ecrnx_vif->ch_index;
                ecrnx_sta->vif_idx = ind->vif_idx;
                ecrnx_sta->vlan_idx = ecrnx_sta->vif_idx;
                ecrnx_sta->ps.active = false;
                ecrnx_sta->qos = true;
                ecrnx_sta->aid = ind->sta_idx + 1;
                //ecrnx_sta->acm = ind->acm;
                memcpy(ecrnx_sta->mac_addr, ind->peer_addr.array, ETH_ALEN);

                ecrnx_chanctx_link(ecrnx_vif, ecrnx_sta->ch_idx, NULL);

                /* Add the station in the list of VIF's stations */
                INIT_LIST_HEAD(&ecrnx_sta->list);
                list_add_tail(&ecrnx_sta->list, &ecrnx_vif->ap.sta_list);

                /* Initialize the TX queues */
                if (ecrnx_sta->ch_idx == ecrnx_hw->cur_chanctx) {
                    txq_status = 0;
                } else {
                    txq_status = ECRNX_TXQ_STOP_CHAN;
                }

                ecrnx_txq_sta_init(ecrnx_hw, ecrnx_sta, txq_status);
                ecrnx_dbgfs_register_sta(ecrnx_hw, ecrnx_sta);

#ifdef CONFIG_ECRNX_BFMER
                // TODO: update indication to contains vht capabilties
                if (ecrnx_hw->mod_params->bfmer)
                    ecrnx_send_bfmer_enable(ecrnx_hw, ecrnx_sta, NULL);

                ecrnx_mu_group_sta_init(ecrnx_sta, NULL);
#endif /* CONFIG_ECRNX_BFMER */

            } else {
                WARN_ON(0);
            }
        } else {
            if (ecrnx_sta->valid) {
                ecrnx_sta->ps.active = false;
                ecrnx_sta->valid = false;

                /* Remove the station from the list of VIF's station */
                list_del_init(&ecrnx_sta->list);

                ecrnx_txq_sta_deinit(ecrnx_hw, ecrnx_sta);
                ecrnx_dbgfs_unregister_sta(ecrnx_hw, ecrnx_sta);
            } else {
                WARN_ON(0);
            }
            /* There is no way to inform upper layer for lost of peer, still
               clean everything in the driver */

            /* Remove the station from the list of VIF's station */

        }
    }

    return 0;
}

static inline int ecrnx_rx_mesh_path_update_ind(struct ecrnx_hw *ecrnx_hw,
                                               struct ecrnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_path_update_ind *ind = (struct mesh_path_update_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct ecrnx_mesh_path *mesh_path;
    bool found = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        return 1;

    if (!ecrnx_vif || (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT))
        return 0;

    /* Look for path with provided target address */
    list_for_each_entry(mesh_path, &ecrnx_vif->ap.mpath_list, list) {
        if (mesh_path->path_idx == ind->path_idx) {
            found = true;
            break;
        }
    }

    /* Check if element has been deleted */
    if (ind->delete) {
        if (found) {
            trace_mesh_delete_path(mesh_path);
            /* Remove element from list */
            list_del_init(&mesh_path->list);
            /* Free the element */
            kfree(mesh_path);
        }
    }
    else {
        if (found) {
            // Update the Next Hop STA
            mesh_path->nhop_sta = &ecrnx_hw->sta_table[ind->nhop_sta_idx];
            trace_mesh_update_path(mesh_path);
        } else {
            // Allocate a Mesh Path structure
            mesh_path = kmalloc(sizeof(struct ecrnx_mesh_path), GFP_ATOMIC);

            if (mesh_path) {
                INIT_LIST_HEAD(&mesh_path->list);

                mesh_path->path_idx = ind->path_idx;
                mesh_path->nhop_sta = &ecrnx_hw->sta_table[ind->nhop_sta_idx];
                memcpy(&mesh_path->tgt_mac_addr, &ind->tgt_mac_addr, MAC_ADDR_LEN);

                // Insert the path in the list of path
                list_add_tail(&mesh_path->list, &ecrnx_vif->ap.mpath_list);

                trace_mesh_create_path(mesh_path);
            }
        }
    }

    return 0;
}

static inline int ecrnx_rx_mesh_proxy_update_ind(struct ecrnx_hw *ecrnx_hw,
                                               struct ecrnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct mesh_proxy_update_ind *ind = (struct mesh_proxy_update_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct ecrnx_mesh_proxy *mesh_proxy;
    bool found = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        return 1;

    if (!ecrnx_vif || (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT))
        return 0;

    /* Look for path with provided external STA address */
    list_for_each_entry(mesh_proxy, &ecrnx_vif->ap.proxy_list, list) {
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
        mesh_proxy = (struct ecrnx_mesh_proxy *)kmalloc(sizeof(*mesh_proxy),
                                                       GFP_ATOMIC);

        if (mesh_proxy) {
            INIT_LIST_HEAD(&mesh_proxy->list);

            memcpy(&mesh_proxy->ext_sta_addr, &ind->ext_sta_addr, MAC_ADDR_LEN);
            mesh_proxy->local = ind->local;

            if (!ind->local) {
                memcpy(&mesh_proxy->proxy_addr, &ind->proxy_mac_addr, MAC_ADDR_LEN);
            }

            /* Insert the path in the list of path */
            list_add_tail(&mesh_proxy->list, &ecrnx_vif->ap.proxy_list);
        }
    }

    return 0;
}

/***************************************************************************
 * Messages from APM task
 **************************************************************************/
static inline int ecrnx_rx_apm_probe_client_ind(struct ecrnx_hw *ecrnx_hw,
                                               struct ecrnx_cmd *cmd,
                                               struct ipc_e2a_msg *msg)
{
    struct apm_probe_client_ind *ind = (struct apm_probe_client_ind *)msg->param;
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ind->vif_idx];
    struct ecrnx_sta *ecrnx_sta = &ecrnx_hw->sta_table[ind->sta_idx];

    ecrnx_sta->stats.last_act = jiffies;
    cfg80211_probe_status(ecrnx_vif->ndev, ecrnx_sta->mac_addr, (u64)ind->probe_id,
                          ind->client_present, 0, false, GFP_ATOMIC);
    return 0;
}
#endif /* CONFIG_ECRNX_FULLMAC */

/***************************************************************************
 * Messages from DEBUG task
 **************************************************************************/
static inline int ecrnx_rx_dbg_error_ind(struct ecrnx_hw *ecrnx_hw,
                                        struct ecrnx_cmd *cmd,
                                        struct ipc_e2a_msg *msg)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    ecrnx_error_ind(ecrnx_hw);

    return 0;
}




#ifdef CONFIG_ECRNX_SOFTMAC

static msg_cb_fct mm_hdlrs[MSG_I(MM_MAX)] = {
    [MSG_I(MM_CONNECTION_LOSS_IND)]       = ecrnx_rx_connection_loss_ind,
    [MSG_I(MM_CHANNEL_SWITCH_IND)]        = ecrnx_rx_chan_switch_ind,
    [MSG_I(MM_CHANNEL_PRE_SWITCH_IND)]    = ecrnx_rx_chan_pre_switch_ind,
    [MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] = ecrnx_rx_remain_on_channel_exp_ind,
#ifdef CONFIG_ECRNX_BCN
    [MSG_I(MM_PRIMARY_TBTT_IND)]          = ecrnx_rx_prm_tbtt_ind,
#endif
    [MSG_I(MM_P2P_VIF_PS_CHANGE_IND)]     = ecrnx_rx_p2p_vif_ps_change_ind,
    [MSG_I(MM_CSA_COUNTER_IND)]           = ecrnx_rx_csa_counter_ind,
    [MSG_I(MM_CHANNEL_SURVEY_IND)]        = ecrnx_rx_channel_survey_ind,
    [MSG_I(MM_RSSI_STATUS_IND)]           = ecrnx_rx_rssi_status_ind,
};

static msg_cb_fct scan_hdlrs[MSG_I(SCAN_MAX)] = {
    [MSG_I(SCAN_DONE_IND)]                = ecrnx_rx_scan_done_ind,
};

#else  /* CONFIG_ECRNX_FULLMAC */

static msg_cb_fct mm_hdlrs[MSG_I(MM_MAX)] = {
    [MSG_I(MM_CHANNEL_SWITCH_IND)]     = ecrnx_rx_chan_switch_ind,
    [MSG_I(MM_CHANNEL_PRE_SWITCH_IND)] = ecrnx_rx_chan_pre_switch_ind,
    [MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] = ecrnx_rx_remain_on_channel_exp_ind,
    [MSG_I(MM_PS_CHANGE_IND)]          = ecrnx_rx_ps_change_ind,
    [MSG_I(MM_TRAFFIC_REQ_IND)]        = ecrnx_rx_traffic_req_ind,
    [MSG_I(MM_P2P_VIF_PS_CHANGE_IND)]  = ecrnx_rx_p2p_vif_ps_change_ind,
    [MSG_I(MM_CSA_COUNTER_IND)]        = ecrnx_rx_csa_counter_ind,
    [MSG_I(MM_CSA_FINISH_IND)]         = ecrnx_rx_csa_finish_ind,
    [MSG_I(MM_CSA_TRAFFIC_IND)]        = ecrnx_rx_csa_traffic_ind,
    [MSG_I(MM_CHANNEL_SURVEY_IND)]     = ecrnx_rx_channel_survey_ind,
    [MSG_I(MM_P2P_NOA_UPD_IND)]        = ecrnx_rx_p2p_noa_upd_ind,
    [MSG_I(MM_RSSI_STATUS_IND)]        = ecrnx_rx_rssi_status_ind,
    [MSG_I(MM_PKTLOSS_IND)]            = ecrnx_rx_pktloss_notify_ind,
};

static msg_cb_fct scan_hdlrs[MSG_I(SCANU_MAX)] = {
    [MSG_I(SCANU_START_CFM)]           = ecrnx_rx_scanu_start_cfm,
    [MSG_I(SCANU_RESULT_IND)]          = ecrnx_rx_scanu_result_ind,
    [MSG_I(SCANU_CANCEL_CFM)]          = ecrnx_scanu_cancel_cfm,
};

static msg_cb_fct me_hdlrs[MSG_I(ME_MAX)] = {
    [MSG_I(ME_TKIP_MIC_FAILURE_IND)] = ecrnx_rx_me_tkip_mic_failure_ind,
    [MSG_I(ME_TX_CREDITS_UPDATE_IND)] = ecrnx_rx_me_tx_credits_update_ind,
};

static msg_cb_fct sm_hdlrs[MSG_I(SM_MAX)] = {
    [MSG_I(SM_CONNECT_IND)]    = ecrnx_rx_sm_connect_ind,
    [MSG_I(SM_DISCONNECT_IND)] = ecrnx_rx_sm_disconnect_ind,
    [MSG_I(SM_EXTERNAL_AUTH_REQUIRED_IND)] = ecrnx_rx_sm_external_auth_required_ind,
    [MSG_I(SM_FT_AUTH_IND)] = ecrnx_rx_sm_ft_auth_ind,
};

static msg_cb_fct apm_hdlrs[MSG_I(APM_MAX)] = {
    [MSG_I(APM_PROBE_CLIENT_IND)] = ecrnx_rx_apm_probe_client_ind,
};
static msg_cb_fct twt_hdlrs[MSG_I(TWT_MAX)] = {
    [MSG_I(TWT_SETUP_IND)]    = ecrnx_rx_twt_setup_ind,
};

static msg_cb_fct mesh_hdlrs[MSG_I(MESH_MAX)] = {
    [MSG_I(MESH_PATH_CREATE_CFM)]  = ecrnx_rx_mesh_path_create_cfm,
    [MSG_I(MESH_PEER_UPDATE_IND)]  = ecrnx_rx_mesh_peer_update_ind,
    [MSG_I(MESH_PATH_UPDATE_IND)]  = ecrnx_rx_mesh_path_update_ind,
    [MSG_I(MESH_PROXY_UPDATE_IND)] = ecrnx_rx_mesh_proxy_update_ind,
};


#endif /* CONFIG_ECRNX_SOFTMAC */

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {
    [MSG_I(DBG_ERROR_IND)]                = ecrnx_rx_dbg_error_ind,
};

static msg_cb_fct tdls_hdlrs[MSG_I(TDLS_MAX)] = {
    [MSG_I(TDLS_CHAN_SWITCH_CFM)] = ecrnx_rx_tdls_chan_switch_cfm,
    [MSG_I(TDLS_CHAN_SWITCH_IND)] = ecrnx_rx_tdls_chan_switch_ind,
    [MSG_I(TDLS_CHAN_SWITCH_BASE_IND)] = ecrnx_rx_tdls_chan_switch_base_ind,
    [MSG_I(TDLS_PEER_PS_IND)] = ecrnx_rx_tdls_peer_ps_ind,
};

static msg_cb_fct *msg_hdlrs[] = {
    [TASK_MM]    = mm_hdlrs,
    [TASK_DBG]   = dbg_hdlrs,
#ifdef CONFIG_ECRNX_SOFTMAC
    [TASK_SCAN]  = scan_hdlrs,
    [TASK_TDLS]  = tdls_hdlrs,
#else
    [TASK_TDLS]  = tdls_hdlrs,
    [TASK_SCANU] = scan_hdlrs,
    [TASK_ME]    = me_hdlrs,
    [TASK_SM]    = sm_hdlrs,
    [TASK_APM]   = apm_hdlrs,
    [TASK_MESH]  = mesh_hdlrs,
    [TASK_TWT]   = twt_hdlrs,
#if 0//(CONFIG_ECRNX_P2P)
    [TASK_P2P_LISTEN]	= p2p_listen_hdlrs,
#endif
#endif /* CONFIG_ECRNX_SOFTMAC */
};

/**
 *
 */
void ecrnx_rx_handle_msg(struct ecrnx_hw *ecrnx_hw, struct ipc_e2a_msg *msg)
{

    if(!ecrnx_hw || !msg || !(&ecrnx_hw->cmd_mgr))
    {
        ECRNX_ERR("ecrnx_rx_handle_msg:receive msg info error \n");
        return;
    }

#if defined(CONFIG_ECRNX_P2P)
    if (MSG_T(msg->id) != TASK_P2P_LISTEN && (MSG_T(msg->id) < TASK_MM || MSG_T(msg->id) > TASK_MESH || MSG_I(msg->id) >= MSG_I(MM_MAX)))
#else
    if (MSG_T(msg->id) < TASK_MM || MSG_T(msg->id) > TASK_MESH || MSG_I(msg->id) >= MSG_I(MM_MAX))
#endif
    {
        ECRNX_ERR("msg id 0x%x,%d,%d, max %d, dst:0x%x, src:0x%x\n", msg->id, MSG_T(msg->id), MSG_I(msg->id), MSG_I(MM_MAX), msg->dummy_dest_id, msg->dummy_src_id);
    	ECRNX_ERR("skip msg %p\n", msg);
    	return;
    }

    if(ecrnx_hw->wiphy != NULL)
    {
        ecrnx_hw->cmd_mgr.msgind(&ecrnx_hw->cmd_mgr, msg,
                                msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
    }
}
