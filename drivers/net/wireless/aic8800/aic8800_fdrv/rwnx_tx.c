// SPDX-License-Identifier: GPL-2.0-or-later
/**
 ******************************************************************************
 *
 * @file rwnx_tx.c
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <net/sock.h>

#include "rwnx_defs.h"
#include "rwnx_tx.h"
#include "rwnx_msg_tx.h"
#include "rwnx_mesh.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "aicwf_txrxif.h"
#ifdef CONFIG_RWNX_MON_XMIT
#include <net/ieee80211_radiotap.h>
#endif

/******************************************************************************
 * Power Save functions
 *****************************************************************************/
/**
 * rwnx_set_traffic_status - Inform FW if traffic is available for STA in PS
 *
 * @rwnx_hw: Driver main data
 * @sta: Sta in PS mode
 * @available: whether traffic is buffered for the STA
 * @ps_id: type of PS data requested (@LEGACY_PS_ID or @UAPSD_ID)
  */
void rwnx_set_traffic_status(struct rwnx_hw *rwnx_hw,
                             struct rwnx_sta *sta,
                             bool available,
                             u8 ps_id)
{
    if (sta->tdls.active) {
        rwnx_send_tdls_peer_traffic_ind_req(rwnx_hw,
                                            rwnx_hw->vif_table[sta->vif_idx]);
    } else {
        bool uapsd = (ps_id != LEGACY_PS_ID);
        rwnx_send_me_traffic_ind(rwnx_hw, sta->sta_idx, uapsd, available);
#ifdef CREATE_TRACE_POINTS
        trace_ps_traffic_update(sta->sta_idx, available, uapsd);
#endif
    }
}

/**
 * rwnx_ps_bh_enable - Enable/disable PS mode for one STA
 *
 * @rwnx_hw: Driver main data
 * @sta: Sta which enters/leaves PS mode
 * @enable: PS mode status
 *
 * This function will enable/disable PS mode for one STA.
 * When enabling PS mode:
 *  - Stop all STA's txq for RWNX_TXQ_STOP_STA_PS reason
 *  - Count how many buffers are already ready for this STA
 *  - For BC/MC sta, update all queued SKB to use hw_queue BCMC
 *  - Update TIM if some packet are ready
 *
 * When disabling PS mode:
 *  - Start all STA's txq for RWNX_TXQ_STOP_STA_PS reason
 *  - For BC/MC sta, update all queued SKB to use hw_queue AC_BE
 *  - Update TIM if some packet are ready (otherwise fw will not update TIM
 *    in beacon for this STA)
 *
 * All counter/skb updates are protected from TX path by taking tx_lock
 *
 * NOTE: _bh_ in function name indicates that this function is called
 * from a bottom_half tasklet.
 */
void rwnx_ps_bh_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
                       bool enable)
{
    struct rwnx_txq *txq;

    if (enable) {
#ifdef CREATE_TRACE_POINTS
        trace_ps_enable(sta);
#endif
        spin_lock_bh(&rwnx_hw->tx_lock);
        sta->ps.active = true;
        sta->ps.sp_cnt[LEGACY_PS_ID] = 0;
        sta->ps.sp_cnt[UAPSD_ID] = 0;
        rwnx_txq_sta_stop(sta, RWNX_TXQ_STOP_STA_PS, rwnx_hw);

        if (is_multicast_sta(sta->sta_idx)) {
            txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
            sta->ps.pkt_ready[LEGACY_PS_ID] = skb_queue_len(&txq->sk_list);
            sta->ps.pkt_ready[UAPSD_ID] = 0;
			//txq->hwq = &rwnx_hw->hwq[RWNX_HWQ_BCMC];
        } else {
            int i;
            sta->ps.pkt_ready[LEGACY_PS_ID] = 0;
            sta->ps.pkt_ready[UAPSD_ID] = 0;
            foreach_sta_txq(sta, txq, i, rwnx_hw) {
                sta->ps.pkt_ready[txq->ps_id] += skb_queue_len(&txq->sk_list);
            }
        }

        spin_unlock_bh(&rwnx_hw->tx_lock);

        //if (sta->ps.pkt_ready[LEGACY_PS_ID])
        //    rwnx_set_traffic_status(rwnx_hw, sta, true, LEGACY_PS_ID);

        //if (sta->ps.pkt_ready[UAPSD_ID])
        //    rwnx_set_traffic_status(rwnx_hw, sta, true, UAPSD_ID);
    } else {
#ifdef CREATE_TRACE_POINTS
		trace_ps_disable(sta->sta_idx);
#endif
        spin_lock_bh(&rwnx_hw->tx_lock);
        sta->ps.active = false;

        if (is_multicast_sta(sta->sta_idx)) {
            txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
            txq->hwq = &rwnx_hw->hwq[RWNX_HWQ_BE];
            txq->push_limit = 0;
        } else {
            int i;
            foreach_sta_txq(sta, txq, i, rwnx_hw) {
                txq->push_limit = 0;
            }
        }

        rwnx_txq_sta_start(sta, RWNX_TXQ_STOP_STA_PS, rwnx_hw);
        spin_unlock_bh(&rwnx_hw->tx_lock);

        //if (sta->ps.pkt_ready[LEGACY_PS_ID])
        //    rwnx_set_traffic_status(rwnx_hw, sta, false, LEGACY_PS_ID);

        //if (sta->ps.pkt_ready[UAPSD_ID])
        //    rwnx_set_traffic_status(rwnx_hw, sta, false, UAPSD_ID);

        tasklet_schedule(&rwnx_hw->task);
    }
}

/**
 * rwnx_ps_bh_traffic_req - Handle traffic request for STA in PS mode
 *
 * @rwnx_hw: Driver main data
 * @sta: Sta which enters/leaves PS mode
 * @pkt_req: number of pkt to push
 * @ps_id: type of PS data requested (@LEGACY_PS_ID or @UAPSD_ID)
 *
 * This function will make sure that @pkt_req are pushed to fw
 * whereas the STA is in PS mode.
 * If request is 0, send all traffic
 * If request is greater than available pkt, reduce request
 * Note: request will also be reduce if txq credits are not available
 *
 * All counter updates are protected from TX path by taking tx_lock
 *
 * NOTE: _bh_ in function name indicates that this function is called
 * from the bottom_half tasklet.
 */
void rwnx_ps_bh_traffic_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
                            u16 pkt_req, u8 ps_id)
{
    int pkt_ready_all;
    struct rwnx_txq *txq;
    int schedule = 0;

    //if (WARN(!sta->ps.active, "sta %pM is not in Power Save mode",
    //         sta->mac_addr))
    //    return;
    if(!sta->ps.active) {
		//AICWFDBG(LOGERROR, "sta(%d) %pM is not in Power Save mode", sta->sta_idx, sta->mac_addr);
    	return;
    }
#ifdef CREATE_TRACE_POINTS
    trace_ps_traffic_req(sta, pkt_req, ps_id);
#endif
    spin_lock_bh(&rwnx_hw->tx_lock);

    /* Fw may ask to stop a service period with PS_SP_INTERRUPTED. This only
       happens for p2p-go interface if NOA starts during a service period */
    if ((pkt_req == PS_SP_INTERRUPTED) && (ps_id == UAPSD_ID)) {
        int tid;
        sta->ps.sp_cnt[ps_id] = 0;
        foreach_sta_txq(sta, txq, tid, rwnx_hw) {
            txq->push_limit = 0;
        }
        goto done;
    }

    pkt_ready_all = (sta->ps.pkt_ready[ps_id] - sta->ps.sp_cnt[ps_id]);

    /* Don't start SP until previous one is finished or we don't have
       packet ready (which must not happen for U-APSD) */
    if (sta->ps.sp_cnt[ps_id] || pkt_ready_all <= 0) {
        goto done;
    }

    /* Adapt request to what is available. */
    if (pkt_req == 0 || pkt_req > pkt_ready_all) {
        pkt_req = pkt_ready_all;
    }

    /* Reset the SP counter */
    sta->ps.sp_cnt[ps_id] = 0;
    schedule = 1;

    /* "dispatch" the request between txq */
    if (is_multicast_sta(sta->sta_idx)) {
        txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
        //if (txq->credits <= 0)
        //    goto done;
        if (pkt_req > txq->credits)
            pkt_req = txq->credits;
        txq->push_limit = pkt_req;
        sta->ps.sp_cnt[ps_id] = pkt_req;
        rwnx_txq_add_to_hw_list(txq);
    } else {
        int i, tid;

        foreach_sta_txq_prio(sta, txq, tid, i, rwnx_hw) {
            u16 txq_len = skb_queue_len(&txq->sk_list);

            if (txq->ps_id != ps_id)
                continue;

            if (txq_len > txq->credits)
                txq_len = txq->credits;

            if (txq_len == 0)
                continue;

            if (txq_len < pkt_req) {
                /* Not enough pkt queued in this txq, add this
                   txq to hwq list and process next txq */
                pkt_req -= txq_len;
                txq->push_limit = txq_len;
                sta->ps.sp_cnt[ps_id] += txq_len;
                rwnx_txq_add_to_hw_list(txq);
            } else {
                /* Enough pkt in this txq to comlete the request
                   add this txq to hwq list and stop processing txq */
                txq->push_limit = pkt_req;
                sta->ps.sp_cnt[ps_id] += pkt_req;
                rwnx_txq_add_to_hw_list(txq);
                break;
            }
        }
    }

  done:
    spin_unlock_bh(&rwnx_hw->tx_lock);
    if(schedule)
	tasklet_schedule(&rwnx_hw->task);
}

/******************************************************************************
 * TX functions
 *****************************************************************************/
#define PRIO_STA_NULL 0xAA

static const int rwnx_down_hwq2tid[3] = {
    [RWNX_HWQ_BK] = 2,
    [RWNX_HWQ_BE] = 3,
    [RWNX_HWQ_VI] = 5,
};

static void rwnx_downgrade_ac(struct rwnx_sta *sta, struct sk_buff *skb)
{
    int8_t ac = rwnx_tid2hwq[skb->priority];

    if (WARN((ac > RWNX_HWQ_VO),
             "Unexepcted ac %d for skb before downgrade", ac))
        ac = RWNX_HWQ_VO;

    while (sta->acm & BIT(ac)) {
        if (ac == RWNX_HWQ_BK) {
            skb->priority = 1;
            return;
        }
        ac--;
        skb->priority = rwnx_down_hwq2tid[ac];
    }
}

u16 rwnx_select_txq(struct rwnx_vif *rwnx_vif, struct sk_buff *skb)
{
    struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
    struct wireless_dev *wdev = &rwnx_vif->wdev;
    struct rwnx_sta *sta = NULL;
    struct rwnx_txq *txq;
    u16 netdev_queue;
    bool tdls_mgmgt_frame = false;

    switch (wdev->iftype) {
    case NL80211_IFTYPE_STATION:
    case NL80211_IFTYPE_P2P_CLIENT:
    {
        struct ethhdr *eth;
        eth = (struct ethhdr *)skb->data;
        if (eth->h_proto == cpu_to_be16(ETH_P_TDLS)) {
            tdls_mgmgt_frame = true;
        }
        if ((rwnx_vif->tdls_status == TDLS_LINK_ACTIVE) &&
            (rwnx_vif->sta.tdls_sta != NULL) &&
            (memcmp(eth->h_dest, rwnx_vif->sta.tdls_sta->mac_addr, ETH_ALEN) == 0))
            sta = rwnx_vif->sta.tdls_sta;
        else
            sta = rwnx_vif->sta.ap;
        break;
    }
    case NL80211_IFTYPE_AP_VLAN:
        if (rwnx_vif->ap_vlan.sta_4a) {
            sta = rwnx_vif->ap_vlan.sta_4a;
            break;
        }

        /* AP_VLAN interface is not used for a 4A STA,
           fallback searching sta amongs all AP's clients */
        rwnx_vif = rwnx_vif->ap_vlan.master;
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_P2P_GO:
    {
        struct rwnx_sta *cur;
        struct ethhdr *eth = (struct ethhdr *)skb->data;

        if (is_multicast_ether_addr(eth->h_dest)) {
            sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
        } else {
        	spin_lock_bh(&rwnx_vif->rwnx_hw->cb_lock);
            list_for_each_entry(cur, &rwnx_vif->ap.sta_list, list) {
                if (!memcmp(cur->mac_addr, eth->h_dest, ETH_ALEN)) {
                    sta = cur;
                    break;
                }
            }
			spin_unlock_bh(&rwnx_vif->rwnx_hw->cb_lock);
        }

        break;
    }
    case NL80211_IFTYPE_MESH_POINT:
    {
        struct ethhdr *eth = (struct ethhdr *)skb->data;

        if (!rwnx_vif->is_resending) {
            /*
             * If ethernet source address is not the address of a mesh wireless interface, we are proxy for
             * this address and have to inform the HW
             */
            if (memcmp(&eth->h_source[0], &rwnx_vif->ndev->perm_addr[0], ETH_ALEN)) {
                /* Check if LMAC is already informed */
                if (!rwnx_get_mesh_proxy_info(rwnx_vif, (u8 *)&eth->h_source, true)) {
                    rwnx_send_mesh_proxy_add_req(rwnx_hw, rwnx_vif, (u8 *)&eth->h_source);
                }
            }
        }

        if (is_multicast_ether_addr(eth->h_dest)) {
            sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
        } else {
            /* Path to be used */
            struct rwnx_mesh_path *p_mesh_path = NULL;
            struct rwnx_mesh_path *p_cur_path;
            /* Check if destination is proxied by a peer Mesh STA */
            struct rwnx_mesh_proxy *p_mesh_proxy = rwnx_get_mesh_proxy_info(rwnx_vif, (u8 *)&eth->h_dest, false);
            /* Mesh Target address */
            struct mac_addr *p_tgt_mac_addr;

            if (p_mesh_proxy) {
                p_tgt_mac_addr = &p_mesh_proxy->proxy_addr;
            } else {
                p_tgt_mac_addr = (struct mac_addr *)&eth->h_dest;
            }

            /* Look for path with provided target address */
            list_for_each_entry(p_cur_path, &rwnx_vif->ap.mpath_list, list) {
                if (!memcmp(&p_cur_path->tgt_mac_addr, p_tgt_mac_addr, ETH_ALEN)) {
                    p_mesh_path = p_cur_path;
                    break;
                }
            }

            if (p_mesh_path) {
                sta = p_mesh_path->p_nhop_sta;
            } else {
                rwnx_send_mesh_path_create_req(rwnx_hw, rwnx_vif, (u8 *)p_tgt_mac_addr);
            }
        }

        break;
    }
    default:
        break;
    }

    if (sta && sta->qos)
    {
        if (tdls_mgmgt_frame) {
            skb_set_queue_mapping(skb, NX_STA_NDEV_IDX(skb->priority, sta->sta_idx));
        } else {
            /* use the data classifier to determine what 802.1d tag the
             * data frame has */
            #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
            skb->priority = cfg80211_classify8021d(skb) & IEEE80211_QOS_CTL_TAG1D_MASK;
            #else
            skb->priority = cfg80211_classify8021d(skb, NULL) & IEEE80211_QOS_CTL_TAG1D_MASK;
            #endif
        }
        if (sta->acm)
            rwnx_downgrade_ac(sta, skb);

        txq = rwnx_txq_sta_get(sta, skb->priority, rwnx_hw);
        netdev_queue = txq->ndev_idx;
    }
    else if (sta)
    {
        skb->priority = 0xFF;
        txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
        netdev_queue = txq->ndev_idx;
    }
    else
    {
        /* This packet will be dropped in xmit function, still need to select
           an active queue for xmit to be called. As it most likely to happen
           for AP interface, select BCMC queue
           (TODO: select another queue if BCMC queue is stopped) */
        skb->priority = PRIO_STA_NULL;
        netdev_queue = NX_BCMC_TXQ_NDEV_IDX;
    }
    
#ifndef CONFIG_ONE_TXQ
    BUG_ON(netdev_queue >= NX_NB_NDEV_TXQ);
#endif

    return netdev_queue;
}

/**
 * rwnx_set_more_data_flag - Update MORE_DATA flag in tx sw desc
 *
 * @rwnx_hw: Driver main data
 * @sw_txhdr: Header for pkt to be pushed
 *
 * If STA is in PS mode
 *  - Set EOSP in case the packet is the last of the UAPSD service period
 *  - Set MORE_DATA flag if more pkt are ready for this sta
 *  - Update TIM if this is the last pkt buffered for this sta
 *
 * note: tx_lock already taken.
 */
static inline void rwnx_set_more_data_flag(struct rwnx_hw *rwnx_hw,
                                           struct rwnx_sw_txhdr *sw_txhdr)
{
    struct rwnx_sta *sta = sw_txhdr->rwnx_sta;
    struct rwnx_vif *vif = sw_txhdr->rwnx_vif;
    struct rwnx_txq *txq = sw_txhdr->txq;

    if (unlikely(sta->ps.active)) {
        sta->ps.pkt_ready[txq->ps_id]--;
        sta->ps.sp_cnt[txq->ps_id]--;
#ifdef CREATE_TRACE_POINTS
        trace_ps_push(sta);
#endif
        if (((txq->ps_id == UAPSD_ID) || (vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) || (sta->tdls.active))
                && !sta->ps.sp_cnt[txq->ps_id]) {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_EOSP;
        }

        if (sta->ps.pkt_ready[txq->ps_id]) {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_MORE_DATA;
        } else {
            rwnx_set_traffic_status(rwnx_hw, sta, false, txq->ps_id);
        }
    }
}

/**
 * rwnx_get_tx_priv - Get STA and tid for one skb
 *
 * @rwnx_vif: vif ptr
 * @skb: skb
 * @tid: pointer updated with the tid to use for this skb
 *
 * @return: pointer on the destination STA (may be NULL)
 *
 * skb has already been parsed in rwnx_select_queue function
 * simply re-read information form skb.
 */
static struct rwnx_sta *rwnx_get_tx_priv(struct rwnx_vif *rwnx_vif,
                                         struct sk_buff *skb,
                                         u8 *tid)
{
    struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
    struct rwnx_sta *sta;
    int sta_idx;
    int nx_remote_sta_max = NX_REMOTE_STA_MAX;
    int nx_bcmc_txq_ndev_idx = NX_BCMC_TXQ_NDEV_IDX;

    if((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8801) || 
        ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
        g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW) && chip_id < 3)){
            nx_remote_sta_max = NX_REMOTE_STA_MAX_FOR_OLD_IC;
            nx_bcmc_txq_ndev_idx = NX_BCMC_TXQ_NDEV_IDX_FOR_OLD_IC;
    }


    *tid = skb->priority;
    if (unlikely(skb->priority == PRIO_STA_NULL)) {
        return NULL;
    } else {
        int ndev_idx = skb_get_queue_mapping(skb);

        if (ndev_idx == nx_bcmc_txq_ndev_idx)
            sta_idx = nx_remote_sta_max + master_vif_idx(rwnx_vif);
        else
            sta_idx = ndev_idx / NX_NB_TID_PER_STA;

        sta = &rwnx_hw->sta_table[sta_idx];
    }
	
    return sta;
}

/**
 * rwnx_prep_tx - Prepare buffer for DMA transmission
 *
 * @rwnx_hw: Driver main data
 * @txhdr: Tx descriptor
 *
 * Maps hw_txhdr and buffer data for transmission via DMA.
 * - Data buffer with be downloaded by embebded side.
 * - hw_txhdr will be uploaded by embedded side when buffer has been
 *   transmitted over the air.
 */
static int rwnx_prep_tx(struct rwnx_hw *rwnx_hw, struct rwnx_txhdr *txhdr)
{
#if 0
    struct rwnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct rwnx_hw_txhdr *hw_txhdr = &txhdr->hw_hdr;
    dma_addr_t dma_addr;

    /* MAP (and sync) memory for DMA */
    dma_addr = dma_map_single(rwnx_hw->dev, hw_txhdr,
                              sw_txhdr->map_len, DMA_BIDIRECTIONAL);
    if (WARN_ON(dma_mapping_error(rwnx_hw->dev, dma_addr)))
        return -1;

    sw_txhdr->dma_addr = dma_addr;
#endif
    return 0;
}

/**
 *  rwnx_tx_push - Push one packet to fw
 *
 * @rwnx_hw: Driver main data
 * @txhdr: tx desc of the buffer to push
 * @flags: push flags (see @rwnx_push_flags)
 *
 * Push one packet to fw. Sw desc of the packet has already been updated.
 * Only MORE_DATA flag will be set if needed.
 */
void rwnx_tx_push(struct rwnx_hw *rwnx_hw, struct rwnx_txhdr *txhdr, int flags)
{
    struct rwnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct sk_buff *skb = sw_txhdr->skb;
    struct rwnx_txq *txq = sw_txhdr->txq;
    u16 hw_queue = txq->hwq->id;
    int user = 0;

    lockdep_assert_held(&rwnx_hw->tx_lock);

    //printk("rwnx_tx_push\n");
    /* RETRY flag is not always set so retest here */
    if (txq->nb_retry) {
        flags |= RWNX_PUSH_RETRY;
        txq->nb_retry--;
        if (txq->nb_retry == 0) {
            WARN(skb != txq->last_retry_skb,
                 "last retry buffer is not the expected one");
            txq->last_retry_skb = NULL;
        }
    } else if (!(flags & RWNX_PUSH_RETRY)) {
        txq->pkt_sent++;
    }

#ifdef CONFIG_RWNX_AMSDUS_TX
    if (txq->amsdu == sw_txhdr) {
        WARN((flags & RWNX_PUSH_RETRY), "End A-MSDU on a retry");
        rwnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
        txq->amsdu = NULL;
    } else if (!(flags & RWNX_PUSH_RETRY) &&
               !(sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU)) {
        rwnx_hw->stats.amsdus[0].done++;
    }
#endif /* CONFIG_RWNX_AMSDUS_TX */

    /* Wait here to update hw_queue, as for multicast STA hwq may change
       between queue and push (because of PS) */
    sw_txhdr->hw_queue = hw_queue;

    //sw_txhdr->desc.host.packet_addr = hw_queue; //use packet_addr field for hw_txq
    sw_txhdr->desc.host.ac = hw_queue; //use ac field for hw_txq
#ifdef CONFIG_RWNX_MUMIMO_TX
    /* MU group is only selected during hwq processing */
    sw_txhdr->desc.host.mumimo_info = txq->mumimo_info;
    user = RWNX_TXQ_POS_ID(txq);
#endif /* CONFIG_RWNX_MUMIMO_TX */

    if (sw_txhdr->rwnx_sta) {
        /* only for AP mode */
        rwnx_set_more_data_flag(rwnx_hw, sw_txhdr);
    }
#ifdef CREATE_TRACE_POINTS
    trace_push_desc(skb, sw_txhdr, flags);
#endif
    #if 0
    txq->credits--;
    #endif
    txq->pkt_pushed[user]++;
    //printk("txq->credits=%d\n",txq->credits);
    #if 0
    if (txq->credits <= 0)
        rwnx_txq_stop(txq, RWNX_TXQ_STOP_FULL);
    #endif

    if (txq->push_limit)
        txq->push_limit--;
#if 0
    rwnx_ipc_txdesc_push(rwnx_hw, &sw_txhdr->desc, skb, hw_queue, user);
#else
#ifdef  AICWF_SDIO_SUPPORT
    if( ((sw_txhdr->desc.host.flags & TXU_CNTRL_MGMT) && \
	((*(skb->data+sw_txhdr->headroom)==0xd0) || (*(skb->data+sw_txhdr->headroom)==0x10) || (*(skb->data+sw_txhdr->headroom)==0x30))) || \
        (sw_txhdr->desc.host.ethertype == 0x8e88) ) { // 0xd0:Action, 0x10:AssocRsp, 0x8e88:EAPOL
        sw_txhdr->need_cfm = 1;
        sw_txhdr->desc.host.status_desc_addr = ((1<<31) | rwnx_hw->sdio_env.txdesc_free_idx[0]);
        aicwf_sdio_host_txdesc_push(&(rwnx_hw->sdio_env), 0, (long)skb);
		AICWFDBG(LOGINFO, "need cfm ethertype:%8x,user_idx=%d, skb=%p sta_idx:%d\n", 
			sw_txhdr->desc.host.ethertype, 
			rwnx_hw->sdio_env.txdesc_free_idx[0], 
			skb,
			sw_txhdr->desc.host.staid);
    } else {
        sw_txhdr->need_cfm = 0;
        if (sw_txhdr->raw_frame) {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_MGMT;
        }
        if (sw_txhdr->fixed_rate) {
            sw_txhdr->desc.host.status_desc_addr = (0x01UL << 30) | sw_txhdr->rate_config;
        } else {
            sw_txhdr->desc.host.status_desc_addr = 0;
        }

        sw_txhdr->rwnx_vif->net_stats.tx_packets++;
        sw_txhdr->rwnx_vif->net_stats.tx_bytes += sw_txhdr->frame_len;
        rwnx_hw->stats.last_tx = jiffies;
    }
    aicwf_frame_tx((void *)(rwnx_hw->sdiodev), skb);
#endif
#ifdef AICWF_USB_SUPPORT
    if( ((sw_txhdr->desc.host.flags & TXU_CNTRL_MGMT) && \
	((*(skb->data+sw_txhdr->headroom)==0xd0) || (*(skb->data+sw_txhdr->headroom)==0x10) || (*(skb->data+sw_txhdr->headroom)==0x30))) || \
        (sw_txhdr->desc.host.ethertype == 0x8e88) ) { // 0xd0:Action, 0x10:AssocRsp, 0x8e88:EAPOL
        sw_txhdr->need_cfm = 1;
        sw_txhdr->desc.host.status_desc_addr = ((1<<31) | rwnx_hw->usb_env.txdesc_free_idx[0]);
        aicwf_usb_host_txdesc_push(&(rwnx_hw->usb_env), 0, (long)(skb));
        AICWFDBG(LOGINFO, "need cfm ethertype:%8x,user_idx=%d, skb=%p sta_idx:%d\n", 
			sw_txhdr->desc.host.ethertype, 
			rwnx_hw->usb_env.txdesc_free_idx[0], 
			skb,
			sw_txhdr->desc.host.staid);
    } else {
        sw_txhdr->need_cfm = 0;
        if (sw_txhdr->raw_frame) {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_MGMT;
        }
        if (sw_txhdr->fixed_rate) {
            sw_txhdr->desc.host.status_desc_addr = (0x01UL << 30) | sw_txhdr->rate_config;
        } else {
            sw_txhdr->desc.host.status_desc_addr = 0;
        }

        sw_txhdr->rwnx_vif->net_stats.tx_packets++;
        sw_txhdr->rwnx_vif->net_stats.tx_bytes += sw_txhdr->frame_len;
        rwnx_hw->stats.last_tx = jiffies;
    }
    aicwf_frame_tx((void *)(rwnx_hw->usbdev), skb);
#endif
#endif
    #if 0
    txq->hwq->credits[user]--;
    #endif
    rwnx_hw->stats.cfm_balance[hw_queue]++;
}



/**
 * rwnx_tx_retry - Push an AMPDU pkt that need to be retried
 *
 * @rwnx_hw: Driver main data
 * @skb: pkt to re-push
 * @txhdr: tx desc of the pkt to re-push
 * @sw_retry: Indicates if fw decide to retry this buffer
 *            (i.e. it has never been transmitted over the air)
 *
 * Called when a packet needs to be repushed to the firmware.
 * First update sw descriptor and then queue it in the retry list.
 */
static void rwnx_tx_retry(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
                           struct rwnx_txhdr *txhdr, bool sw_retry)
{
    struct rwnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct tx_cfm_tag *cfm = &txhdr->hw_hdr.cfm;
    struct rwnx_txq *txq = sw_txhdr->txq;
    int peek_off = offsetof(struct rwnx_hw_txhdr, cfm);
    int peek_len = sizeof(((struct rwnx_hw_txhdr *)0)->cfm);

    if (!sw_retry) {
        /* update sw desc */
		#if 0
        sw_txhdr->desc.host.sn = cfm->sn;
        sw_txhdr->desc.host.pn[0] = cfm->pn[0];
        sw_txhdr->desc.host.pn[1] = cfm->pn[1];
        sw_txhdr->desc.host.pn[2] = cfm->pn[2];
        sw_txhdr->desc.host.pn[3] = cfm->pn[3];
        sw_txhdr->desc.host.timestamp = cfm->timestamp;
		#endif
		sw_txhdr->desc.host.flags |= TXU_CNTRL_RETRY;

        #ifdef CONFIG_RWNX_AMSDUS_TX
        if (sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU)
            rwnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].failed++;
        #endif
    }

    /* MORE_DATA will be re-set if needed when pkt will be repushed */
    sw_txhdr->desc.host.flags &= ~TXU_CNTRL_MORE_DATA;

    cfm->status.value = 0;
    dma_sync_single_for_device(rwnx_hw->dev, sw_txhdr->dma_addr + peek_off,
                               peek_len, DMA_BIDIRECTIONAL);

    txq->credits++;
    if (txq->credits > 0)
        rwnx_txq_start(txq, RWNX_TXQ_STOP_FULL);

    /* Queue the buffer */
    rwnx_txq_queue_skb(skb, txq, rwnx_hw, true);
}


#ifdef CONFIG_RWNX_AMSDUS_TX
/* return size of subframe (including header) */
static inline int rwnx_amsdu_subframe_length(struct ethhdr *eth, int eth_len)
{
    /* ethernet header is replaced with amdsu header that have the same size
       Only need to check if LLC/SNAP header will be added */
    int len = eth_len;

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        len += sizeof(rfc1042_header) + 2;
    }

    return len;
}

static inline bool rwnx_amsdu_is_aggregable(struct sk_buff *skb)
{
    /* need to add some check on buffer to see if it can be aggregated ? */
    return true;
}


/**
 * rwnx_amsdu_del_subframe_header - remove AMSDU header
 *
 * amsdu_txhdr: amsdu tx descriptor
 *
 * Move back the ethernet header at the "beginning" of the data buffer.
 * (which has been moved in @rwnx_amsdu_add_subframe_header)
 */
static void rwnx_amsdu_del_subframe_header(struct rwnx_amsdu_txhdr *amsdu_txhdr)
{
    struct sk_buff *skb = amsdu_txhdr->skb;
    struct ethhdr *eth;
    u8 *pos;

    pos = skb->data;
    pos += sizeof(struct rwnx_amsdu_txhdr);
    eth = (struct ethhdr*)pos;
    pos += amsdu_txhdr->pad + sizeof(struct ethhdr);

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        pos += sizeof(rfc1042_header) + 2;
    }

    memmove(pos, eth, sizeof(*eth));
    skb_pull(skb, (pos - skb->data));
}

/**
 * rwnx_amsdu_add_subframe_header - Add AMSDU header and link subframe
 *
 * @rwnx_hw Driver main data
 * @skb Buffer to aggregate
 * @sw_txhdr Tx descriptor for the first A-MSDU subframe
 *
 * return 0 on sucess, -1 otherwise
 *
 * This functions Add A-MSDU header and LLC/SNAP header in the buffer
 * and update sw_txhdr of the first subframe to link this buffer.
 * If an error happens, the buffer will be queued as a normal buffer.
 *
 *
 *            Before           After
 *         +-------------+  +-------------+
 *         | HEADROOM    |  | HEADROOM    |
 *         |             |  +-------------+ <- data
 *         |             |  | amsdu_txhdr |
 *         |             |  | * pad size  |
 *         |             |  +-------------+
 *         |             |  | ETH hdr     | keep original eth hdr
 *         |             |  |             | to restore it once transmitted
 *         |             |  +-------------+ <- packet_addr[x]
 *         |             |  | Pad         |
 *         |             |  +-------------+
 * data -> +-------------+  | AMSDU HDR   |
 *         | ETH hdr     |  +-------------+
 *         |             |  | LLC/SNAP    |
 *         +-------------+  +-------------+
 *         | DATA        |  | DATA        |
 *         |             |  |             |
 *         +-------------+  +-------------+
 *
 * Called with tx_lock hold
 */
static int rwnx_amsdu_add_subframe_header(struct rwnx_hw *rwnx_hw,
                                          struct sk_buff *skb,
                                          struct rwnx_sw_txhdr *sw_txhdr)
{
    struct rwnx_amsdu *amsdu = &sw_txhdr->amsdu;
    struct rwnx_amsdu_txhdr *amsdu_txhdr;
    struct ethhdr *amsdu_hdr, *eth = (struct ethhdr *)skb->data;
    int headroom_need, map_len, msdu_len;
    dma_addr_t dma_addr;
    u8 *pos, *map_start;

    msdu_len = skb->len - sizeof(*eth);
    headroom_need = sizeof(*amsdu_txhdr) + amsdu->pad +
        sizeof(*amsdu_hdr);
    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        headroom_need += sizeof(rfc1042_header) + 2;
        msdu_len += sizeof(rfc1042_header) + 2;
    }

    /* we should have enough headroom (checked in xmit) */
    if (WARN_ON(skb_headroom(skb) < headroom_need)) {
        return -1;
    }

    /* allocate headroom */
    pos = skb_push(skb, headroom_need);
    amsdu_txhdr = (struct rwnx_amsdu_txhdr *)pos;
    pos += sizeof(*amsdu_txhdr);

    /* move eth header */
    memmove(pos, eth, sizeof(*eth));
    eth = (struct ethhdr *)pos;
    pos += sizeof(*eth);

    /* Add padding from previous subframe */
    map_start = pos;
    memset(pos, 0, amsdu->pad);
    pos += amsdu->pad;

    /* Add AMSDU hdr */
    amsdu_hdr = (struct ethhdr *)pos;
    memcpy(amsdu_hdr->h_dest, eth->h_dest, ETH_ALEN);
    memcpy(amsdu_hdr->h_source, eth->h_source, ETH_ALEN);
    amsdu_hdr->h_proto = htons(msdu_len);
    pos += sizeof(*amsdu_hdr);

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        memcpy(pos, rfc1042_header, sizeof(rfc1042_header));
        pos += sizeof(rfc1042_header);
    }

    /* MAP (and sync) memory for DMA */
    map_len = msdu_len + amsdu->pad + sizeof(*amsdu_hdr);
    dma_addr = dma_map_single(rwnx_hw->dev, map_start, map_len,
                              DMA_BIDIRECTIONAL);
    if (WARN_ON(dma_mapping_error(rwnx_hw->dev, dma_addr))) {
        pos -= sizeof(*eth);
        memmove(pos, eth, sizeof(*eth));
        skb_pull(skb, headroom_need);
        return -1;
    }

    /* update amdsu_txhdr */
    amsdu_txhdr->map_len = map_len;
    amsdu_txhdr->dma_addr = dma_addr;
    amsdu_txhdr->skb = skb;
    amsdu_txhdr->pad = amsdu->pad;
    amsdu_txhdr->msdu_len = msdu_len;

    /* update rwnx_sw_txhdr (of the first subframe) */
    BUG_ON(amsdu->nb != sw_txhdr->desc.host.packet_cnt);
    sw_txhdr->desc.host.packet_addr[amsdu->nb] = dma_addr;
    sw_txhdr->desc.host.packet_len[amsdu->nb] = map_len;
    sw_txhdr->desc.host.packet_cnt++;
    amsdu->nb++;

    amsdu->pad = AMSDU_PADDING(map_len - amsdu->pad);
    list_add_tail(&amsdu_txhdr->list, &amsdu->hdrs);
    amsdu->len += map_len;

    rwnx_ipc_sta_buffer(rwnx_hw, sw_txhdr->txq->sta,
                        sw_txhdr->txq->tid, msdu_len);

    trace_amsdu_subframe(sw_txhdr);
    return 0;
}

/**
 * rwnx_amsdu_add_subframe - Add this buffer as an A-MSDU subframe if possible
 *
 * @rwnx_hw Driver main data
 * @skb Buffer to aggregate if possible
 * @sta Destination STA
 * @txq sta's txq used for this buffer
 *
 * Tyr to aggregate the buffer in an A-MSDU. If it succeed then the
 * buffer is added as a new A-MSDU subframe with AMSDU and LLC/SNAP
 * headers added (so FW won't have to modify this subframe).
 *
 * To be added as subframe :
 * - sta must allow amsdu
 * - buffer must be aggregable (to be defined)
 * - at least one other aggregable buffer is pending in the queue
 *  or an a-msdu (with enough free space) is currently in progress
 *
 * returns true if buffer has been added as A-MDSP subframe, false otherwise
 *
 */
static bool rwnx_amsdu_add_subframe(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
                                    struct rwnx_sta *sta, struct rwnx_txq *txq)
{
    bool res = false;
    struct ethhdr *eth;

    /* immediately return if amsdu are not allowed for this sta */
    if (!txq->amsdu_len || rwnx_hw->mod_params->amsdu_maxnb < 2 ||
        !rwnx_amsdu_is_aggregable(skb)
       )
        return false;

    spin_lock_bh(&rwnx_hw->tx_lock);
    if (txq->amsdu) {
        /* aggreagation already in progress, add this buffer if enough space
           available, otherwise end the current amsdu */
        struct rwnx_sw_txhdr *sw_txhdr = txq->amsdu;
        eth = (struct ethhdr *)(skb->data);

        if (((sw_txhdr->amsdu.len + sw_txhdr->amsdu.pad +
              rwnx_amsdu_subframe_length(eth, skb->len)) > txq->amsdu_len) ||
            rwnx_amsdu_add_subframe_header(rwnx_hw, skb, sw_txhdr)) {
            txq->amsdu = NULL;
            goto end;
        }

        if (sw_txhdr->amsdu.nb >= rwnx_hw->mod_params->amsdu_maxnb) {
            rwnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
            /* max number of subframes reached */
            txq->amsdu = NULL;
        }
    } else {
        /* Check if a new amsdu can be started with the previous buffer
           (if any) and this one */
        struct sk_buff *skb_prev = skb_peek_tail(&txq->sk_list);
        struct rwnx_txhdr *txhdr;
        struct rwnx_sw_txhdr *sw_txhdr;
        int len1, len2;

        if (!skb_prev || !rwnx_amsdu_is_aggregable(skb_prev))
            goto end;

        txhdr = (struct rwnx_txhdr *)skb_prev->data;
        sw_txhdr = txhdr->sw_hdr;
        if ((sw_txhdr->amsdu.len) ||
            (sw_txhdr->desc.host.flags & TXU_CNTRL_RETRY))
            /* previous buffer is already a complete amsdu or a retry */
            goto end;

        eth = (struct ethhdr *)(skb_prev->data + sw_txhdr->headroom);
        len1 = rwnx_amsdu_subframe_length(eth, (sw_txhdr->frame_len +
                                                sizeof(struct ethhdr)));

        eth = (struct ethhdr *)(skb->data);
        len2 = rwnx_amsdu_subframe_length(eth, skb->len);

        if (len1 + AMSDU_PADDING(len1) + len2 > txq->amsdu_len)
            /* not enough space to aggregate those two buffers */
            goto end;

        /* Add subframe header.
           Note: Fw will take care of adding AMDSU header for the first
           subframe while generating 802.11 MAC header */
        INIT_LIST_HEAD(&sw_txhdr->amsdu.hdrs);
        sw_txhdr->amsdu.len = len1;
        sw_txhdr->amsdu.nb = 1;
        sw_txhdr->amsdu.pad = AMSDU_PADDING(len1);
        if (rwnx_amsdu_add_subframe_header(rwnx_hw, skb, sw_txhdr))
            goto end;

        sw_txhdr->desc.host.flags |= TXU_CNTRL_AMSDU;

        if (sw_txhdr->amsdu.nb < rwnx_hw->mod_params->amsdu_maxnb)
            txq->amsdu = sw_txhdr;
        else
            rwnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
    }

    res = true;

  end:
    spin_unlock_bh(&rwnx_hw->tx_lock);
    return res;
}
#endif /* CONFIG_RWNX_AMSDUS_TX */

#ifdef CONFIG_BR_SUPPORT
int aic_br_client_tx(struct rwnx_vif *vif, struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;

	/* if(check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) */
	{
		void dhcp_flag_bcast(struct rwnx_vif *vif, struct sk_buff *skb);
		int res, is_vlan_tag = 0, i, do_nat25 = 1;
		unsigned short vlan_hdr = 0;
		void *br_port = NULL;

		/* mac_clone_handle_frame(priv, skb); */

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		br_port = vif->ndev->br_port;
#else   /* (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)) */
		rcu_read_lock();
		br_port = rcu_dereference(vif->ndev->rx_handler_data);
		rcu_read_unlock();
#endif /* (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)) */
#ifdef BR_SUPPORT_DEBUG
		printk("SA=%pM, br_mac=%pM, type=0x%x, da[0]=%x, scdb=%pM, vif_type=%d\n", skb->data + MACADDRLEN,  vif->br_mac, *((unsigned short *)(skb->data + MACADDRLEN * 2)),
			skb->data[0], vif->scdb_mac,RWNX_VIF_TYPE(vif));
#endif
        spin_lock_bh(&vif->br_ext_lock);
		if (!(skb->data[0] & 1) &&
		    br_port &&
		    memcmp(skb->data + MACADDRLEN, vif->br_mac, MACADDRLEN) &&
		    *((unsigned short *)(skb->data + MACADDRLEN * 2)) != __constant_htons(ETH_P_8021Q) &&
		    *((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_IP) &&
		    !memcmp(vif->scdb_mac, skb->data + MACADDRLEN, MACADDRLEN) && vif->scdb_entry) {
			memcpy(skb->data + MACADDRLEN, vif->ndev->dev_addr, MACADDRLEN);
			vif->scdb_entry->ageing_timer = jiffies;
            spin_unlock_bh(&vif->br_ext_lock);
		} else
			/* if (!priv->pmib->ethBrExtInfo.nat25_disable)		 */
		{
			/*			if (priv->dev->br_port &&
			 *				 !memcmp(skb->data+MACADDRLEN, priv->br_mac, MACADDRLEN)) { */
#if 1
			if (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_8021Q)) {
				is_vlan_tag = 1;
				vlan_hdr = *((unsigned short *)(skb->data + MACADDRLEN * 2 + 2));
				for (i = 0; i < 6; i++)
					*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2 - i * 2)) = *((unsigned short *)(skb->data + MACADDRLEN * 2 - 2 - i * 2));
				skb_pull(skb, 4);
			}
			/* if SA == br_mac && skb== IP  => copy SIP to br_ip ?? why */
			if (!memcmp(skb->data + MACADDRLEN, vif->br_mac, MACADDRLEN) &&
			    (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_IP)))
				memcpy(vif->br_ip, skb->data + WLAN_ETHHDR_LEN + 12, 4);

			if (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_IP)) {
				if (memcmp(vif->scdb_mac, skb->data + MACADDRLEN, MACADDRLEN)) {
	#if 1
					void *scdb_findEntry(struct rwnx_vif *vif, unsigned char *macAddr, unsigned char *ipAddr);

					vif->scdb_entry = (struct nat25_network_db_entry *)scdb_findEntry(vif,
						skb->data + MACADDRLEN, skb->data + WLAN_ETHHDR_LEN + 12);
					if (vif->scdb_entry != NULL) {
						memcpy(vif->scdb_mac, skb->data + MACADDRLEN, MACADDRLEN);
						memcpy(vif->scdb_ip, skb->data + WLAN_ETHHDR_LEN + 12, 4);
						vif->scdb_entry->ageing_timer = jiffies;
						do_nat25 = 0;
					}
	#endif
				} else {
					if (vif->scdb_entry) {
						vif->scdb_entry->ageing_timer = jiffies;
						do_nat25 = 0;
					} else {
						memset(vif->scdb_mac, 0, MACADDRLEN);
						memset(vif->scdb_ip, 0, 4);
					}
				}
			}
			spin_unlock_bh(&vif->br_ext_lock);
#endif /* 1 */
			if (do_nat25) {
				#if 1
				int nat25_db_handle(struct rwnx_vif *vif, struct sk_buff *skb, int method);
				if (nat25_db_handle(vif, skb, NAT25_CHECK) == 0) {
					struct sk_buff *newskb;

					if (is_vlan_tag) {
						skb_push(skb, 4);
						for (i = 0; i < 6; i++)
							*((unsigned short *)(skb->data + i * 2)) = *((unsigned short *)(skb->data + 4 + i * 2));
						*((unsigned short *)(skb->data + MACADDRLEN * 2)) = __constant_htons(ETH_P_8021Q);
						*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2)) = vlan_hdr;
					}

					newskb = skb_copy(skb, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
					if (newskb == NULL) {
						/* priv->ext_stats.tx_drops++; */
						printk("TX DROP: skb_copy fail!\n");
						/* goto stop_proc; */
						return -1;
					}
					dev_kfree_skb_any(skb);

					*pskb = skb = newskb;
					if (is_vlan_tag) {
						vlan_hdr = *((unsigned short *)(skb->data + MACADDRLEN * 2 + 2));
						for (i = 0; i < 6; i++)
							*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2 - i * 2)) = *((unsigned short *)(skb->data + MACADDRLEN * 2 - 2 - i * 2));
						skb_pull(skb, 4);
					}
				}

				if (skb_is_nonlinear(skb))
					printk("%s(): skb_is_nonlinear!!\n", __FUNCTION__);


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
				res = skb_linearize(skb, GFP_ATOMIC);
#else	/* (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)) */
				res = skb_linearize(skb);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)) */
				if (res < 0) {
					printk("TX DROP: skb_linearize fail!\n");
					/* goto free_and_stop; */
					return -1;
				}

				res = nat25_db_handle(vif, skb, NAT25_INSERT);
				if (res < 0) {
					if (res == -2) {
						/* priv->ext_stats.tx_drops++; */
						printk("TX DROP: nat25_db_handle fail!\n");
						/* goto free_and_stop; */
						return -1;

					}
					/* we just print warning message and let it go */
					/* DEBUG_WARN("%s()-%d: nat25_db_handle INSERT Warning!\n", __FUNCTION__, __LINE__); */
					/* return -1; */ /* return -1 will cause system crash on 2011/08/30! */
					return 0;
				}
				#endif
			}

			memcpy(skb->data + MACADDRLEN, vif->ndev->dev_addr, MACADDRLEN);

			dhcp_flag_bcast(vif, skb);

			if (is_vlan_tag) {
				skb_push(skb, 4);
				for (i = 0; i < 6; i++)
					*((unsigned short *)(skb->data + i * 2)) = *((unsigned short *)(skb->data + 4 + i * 2));
				*((unsigned short *)(skb->data + MACADDRLEN * 2)) = __constant_htons(ETH_P_8021Q);
				*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2)) = vlan_hdr;
			}
		}
#if 0
		else {
			if (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_8021Q))
				is_vlan_tag = 1;

			if (is_vlan_tag) {
				if (ICMPV6_MCAST_MAC(skb->data) && ICMPV6_PROTO1A_VALN(skb->data))
					memcpy(skb->data + MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
			} else {
				if (ICMPV6_MCAST_MAC(skb->data) && ICMPV6_PROTO1A(skb->data))
					memcpy(skb->data + MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
			}
		}
#endif /* 0 */

		/* check if SA is equal to our MAC */
		if (memcmp(skb->data + MACADDRLEN, vif->ndev->dev_addr, MACADDRLEN)) {
			/* priv->ext_stats.tx_drops++; */
			printk("TX DROP: untransformed frame SA:%02X%02X%02X%02X%02X%02X!\n",
				skb->data[6], skb->data[7], skb->data[8], skb->data[9], skb->data[10], skb->data[11]);
			/* goto free_and_stop; */
			return -1;
		}
	}
	printk("%s:exit\n",__func__);
	return 0;
}
#endif /* CONFIG_BR_SUPPORT */


/**
 * netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb,
 *                               struct net_device *dev);
 *	Called when a packet needs to be transmitted.
 *	Must return NETDEV_TX_OK , NETDEV_TX_BUSY.
 *        (can also return NETDEV_TX_LOCKED if NETIF_F_LLTX)
 *
 *  - Initialize the desciptor for this pkt (stored in skb before data)
 *  - Push the pkt in the corresponding Txq
 *  - If possible (i.e. credit available and not in PS) the pkt is pushed
 *    to fw
 */
netdev_tx_t rwnx_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
    struct rwnx_txhdr *txhdr;
    struct rwnx_sw_txhdr *sw_txhdr;
    struct txdesc_api *desc;
    struct rwnx_sta *sta;
    struct rwnx_txq *txq;
    int headroom;
    int max_headroom;
    int hdr_pads;

    u16 frame_len;
    u16 frame_oft;
    u8 tid;
    
    struct ethhdr eth_t;

#ifdef CONFIG_ONE_TXQ
    skb->queue_mapping = rwnx_select_txq(rwnx_vif, skb);
#endif

    memcpy(&eth_t, skb->data, sizeof(struct ethhdr));

    sk_pacing_shift_update(skb->sk, rwnx_hw->tcp_pacing_shift);
    max_headroom = sizeof(struct rwnx_txhdr);

    /* check whether the current skb can be used */
    if (skb_shared(skb) || (skb_headroom(skb) < max_headroom) ||
        (skb_cloned(skb) && (dev->priv_flags & IFF_BRIDGE_PORT))) {
        struct sk_buff *newskb = skb_copy_expand(skb, max_headroom, 0,
                                                 GFP_ATOMIC);
        if (unlikely(newskb == NULL))
            goto free;

        dev_kfree_skb_any(skb);

        skb = newskb;
    }

    /* Get the STA id and TID information */
    sta = rwnx_get_tx_priv(rwnx_vif, skb, &tid);
    if (!sta)
        goto free;

    txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);
    if (txq->idx == TXQ_INACTIVE)
        goto free;

#ifdef CONFIG_RWNX_AMSDUS_TX
    if (rwnx_amsdu_add_subframe(rwnx_hw, skb, sta, txq))
        return NETDEV_TX_OK;
#endif

#ifdef CONFIG_BR_SUPPORT
    if (1) {//(check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) == _TRUE) {
        void *br_port = NULL;

	#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
        br_port = rwnx_vif->ndev->br_port;
	#else
        rcu_read_lock();
        br_port = rcu_dereference(rwnx_vif->ndev->rx_handler_data);
        rcu_read_unlock();
	#endif

        if (br_port) {
            s32 res = aic_br_client_tx(rwnx_vif, &skb);
            if (res == -1) {
                goto free;
            }
        }
    }
#endif /* CONFIG_BR_SUPPORT */

	/* Retrieve the pointer to the Ethernet data */
	// eth = (struct ethhdr *)skb->data;

    skb_pull(skb, 14);
    //hdr_pads  = RWNX_SWTXHDR_ALIGN_PADS((long)eth);
    hdr_pads  = RWNX_SWTXHDR_ALIGN_PADS((long)skb->data);
    headroom  = sizeof(struct rwnx_txhdr) + hdr_pads;

    skb_push(skb, headroom);

    txhdr = (struct rwnx_txhdr *)skb->data;
    sw_txhdr = kmem_cache_alloc(rwnx_hw->sw_txhdr_cache, GFP_ATOMIC);

    if (unlikely(sw_txhdr == NULL))
        goto free;
    txhdr->sw_hdr = sw_txhdr;
    desc = &sw_txhdr->desc;

    frame_len = (u16)skb->len - headroom;// - sizeof(*eth);

    sw_txhdr->txq       = txq;
    sw_txhdr->frame_len = frame_len;
    sw_txhdr->rwnx_sta  = sta;
    sw_txhdr->rwnx_vif  = rwnx_vif;
    sw_txhdr->skb       = skb;
    sw_txhdr->headroom  = headroom;
    sw_txhdr->map_len   = skb->len - offsetof(struct rwnx_txhdr, hw_hdr);

#ifdef CONFIG_RWNX_AMSDUS_TX
    sw_txhdr->amsdu.len = 0;
    sw_txhdr->amsdu.nb = 0;
#endif
    sw_txhdr->raw_frame = 0;
    sw_txhdr->fixed_rate = 0;
    // Fill-in the descriptor
    memcpy(&desc->host.eth_dest_addr, eth_t.h_dest, ETH_ALEN);
    memcpy(&desc->host.eth_src_addr, eth_t.h_source, ETH_ALEN);
    desc->host.ethertype = eth_t.h_proto;
    desc->host.staid = sta->sta_idx;
    desc->host.tid = tid;
    if (unlikely(rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN))
        desc->host.vif_idx = rwnx_vif->ap_vlan.master->vif_index;
    else
        desc->host.vif_idx = rwnx_vif->vif_index;

    if (rwnx_vif->use_4addr && (sta->sta_idx < NX_REMOTE_STA_MAX))
        desc->host.flags = TXU_CNTRL_USE_4ADDR;
    else
        desc->host.flags = 0;

    if ((rwnx_vif->tdls_status == TDLS_LINK_ACTIVE) &&
        rwnx_vif->sta.tdls_sta &&
        (memcmp(desc->host.eth_dest_addr.array, rwnx_vif->sta.tdls_sta->mac_addr, ETH_ALEN) == 0)) {
        desc->host.flags |= TXU_CNTRL_TDLS;
        rwnx_vif->sta.tdls_sta->tdls.last_tid = desc->host.tid;
        //rwnx_vif->sta.tdls_sta->tdls.last_sn = desc->host.sn;
    }

    if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) {
        if (rwnx_vif->is_resending) {
            desc->host.flags |= TXU_CNTRL_MESH_FWD;
        }
    }

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
    desc->host.packet_len[0] = frame_len;
#else
    desc->host.packet_len = frame_len;
#endif

    txhdr->hw_hdr.cfm.status.value = 0;

    if (unlikely(rwnx_prep_tx(rwnx_hw, txhdr))) {
        kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);
        skb_pull(skb, headroom);
        dev_kfree_skb_any(skb);
        return NETDEV_TX_BUSY;
    }

    /* Fill-in TX descriptor */
    frame_oft = sizeof(struct rwnx_txhdr) - offsetof(struct rwnx_txhdr, hw_hdr)
                + hdr_pads;// + sizeof(*eth);
 #if 0
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
    desc->host.packet_addr[0] = sw_txhdr->dma_addr + frame_oft;
    desc->host.packet_cnt = 1;
#else
    desc->host.packet_addr = sw_txhdr->dma_addr + frame_oft;
#endif
#endif
    desc->host.status_desc_addr = sw_txhdr->dma_addr;

    spin_lock_bh(&rwnx_hw->tx_lock);
    if (rwnx_txq_queue_skb(skb, txq, rwnx_hw, false))
        rwnx_hwq_process(rwnx_hw, txq->hwq);
    spin_unlock_bh(&rwnx_hw->tx_lock);

    return NETDEV_TX_OK;

free:
    dev_kfree_skb_any(skb);

    return NETDEV_TX_OK;
}

/**
 * rwnx_start_mgmt_xmit - Transmit a management frame
 *
 * @vif: Vif that send the frame
 * @sta: Destination of the frame. May be NULL if the destiantion is unknown
 *       to the AP.
 * @params: Mgmt frame parameters
 * @offchan: Indicate whether the frame must be send via the offchan TXQ.
 *           (is is redundant with params->offchan ?)
 * @cookie: updated with a unique value to identify the frame with upper layer
 *
 */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
int rwnx_start_mgmt_xmit(struct rwnx_vif *vif, struct rwnx_sta *sta,
                         struct cfg80211_mgmt_tx_params *params, bool offchan,
                         u64 *cookie)
#else
int rwnx_start_mgmt_xmit(struct rwnx_vif *vif, struct rwnx_sta *sta,
                         struct ieee80211_channel *channel, bool offchan,
                         unsigned int wait, const u8* buf, size_t len,
                    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
                         bool no_cck,
                    #endif
                    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
                         bool dont_wait_for_ack,
                    #endif
                         u64 *cookie)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */
{
    struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
    struct rwnx_txhdr *txhdr;
    struct rwnx_sw_txhdr *sw_txhdr;
    struct txdesc_api *desc;
    struct sk_buff *skb;
    u16 frame_len, headroom, frame_oft;
    u8 *data;
    int nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX;
    struct rwnx_txq *txq;
    bool robust;
    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    const u8 *buf = params->buf;
    size_t len = params->len;
    bool no_cck = params->no_cck;
    #endif
    headroom = sizeof(struct rwnx_txhdr);
    frame_len = len;

    //----------------------------------------------------------------------

	if((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8801) || 
		((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
		g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW) && chip_id < 3)){
		nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX_FOR_OLD_IC;
	}

    /* Set TID and Queues indexes */
    if (sta) {
        txq = rwnx_txq_sta_get(sta, 8, rwnx_hw);
    } else {
        if (offchan)
            txq = &rwnx_hw->txq[nx_off_chan_txq_idx];
        else
            txq = rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
    }

    /* Ensure that TXQ is active */
    if (txq->idx == TXQ_INACTIVE) {
        netdev_dbg(vif->ndev, "TXQ inactive\n");
        return -EBUSY;
    }

    /*
     * Create a SK Buff object that will contain the provided data
     */
    skb = dev_alloc_skb(headroom + frame_len);

    if (!skb) {
        return -ENOMEM;
    }

    *cookie = (unsigned long)skb;

    /*
     * Move skb->data pointer in order to reserve room for rwnx_txhdr
     * headroom value will be equal to sizeof(struct rwnx_txhdr)
     */
    skb_reserve(skb, headroom);

    /*
     * Extend the buffer data area in order to contain the provided packet
     * len value (for skb) will be equal to param->len
     */
    data = skb_put(skb, frame_len);
    /* Copy the provided data */
    memcpy(data, buf, frame_len);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
    robust = ieee80211_is_robust_mgmt_frame(skb);
#else
	if (skb->len < 25){
		robust = false;
	}
	robust = ieee80211_is_robust_mgmt_frame((void *)skb->data);
#endif

    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
    /* Update CSA counter if present */
    if (unlikely(params->n_csa_offsets) &&
        vif->wdev.iftype == NL80211_IFTYPE_AP &&
        vif->ap.csa) {
        int i;

        data = skb->data;
        for (i = 0; i < params->n_csa_offsets ; i++) {
            data[params->csa_offsets[i]] = vif->ap.csa->count;
        }
    }
    #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */

    /*
     * Go back to the beginning of the allocated data area
     * skb->data pointer will move backward
     */
    skb_push(skb, headroom);

    //----------------------------------------------------------------------

    /* Fill the TX Header */
    txhdr = (struct rwnx_txhdr *)skb->data;

    txhdr->hw_hdr.cfm.status.value = 0;

    //----------------------------------------------------------------------

    /* Fill the SW TX Header */
    sw_txhdr = kmem_cache_alloc(rwnx_hw->sw_txhdr_cache, GFP_ATOMIC);
	
    if (unlikely(sw_txhdr == NULL)) {
        dev_kfree_skb(skb);
        return -ENOMEM;
    }


    txhdr->sw_hdr = sw_txhdr;

    sw_txhdr->txq = txq;
    sw_txhdr->frame_len = frame_len;
    sw_txhdr->rwnx_sta = sta;
    sw_txhdr->rwnx_vif = vif;
    sw_txhdr->skb = skb;
    sw_txhdr->headroom = headroom;
    sw_txhdr->map_len = skb->len - offsetof(struct rwnx_txhdr, hw_hdr);
#ifdef CONFIG_RWNX_AMSDUS_TX
    sw_txhdr->amsdu.len = 0;
    sw_txhdr->amsdu.nb = 0;
#endif
    sw_txhdr->raw_frame = 0;
    sw_txhdr->fixed_rate = 0;
    //----------------------------------------------------------------------

    /* Fill the Descriptor to be provided to the MAC SW */
    desc = &sw_txhdr->desc;
	
    desc->host.ethertype = 0;
    desc->host.staid = (sta) ? sta->sta_idx : 0xFF;
    desc->host.vif_idx = vif->vif_index;
    desc->host.tid = 0xFF;
    desc->host.flags = TXU_CNTRL_MGMT;
    if (robust)
        desc->host.flags |= TXU_CNTRL_MGMT_ROBUST;

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
    desc->host.packet_len[0] = frame_len;
#else
    desc->host.packet_len = frame_len;
#endif

    if (no_cck) {
        desc->host.flags |= TXU_CNTRL_MGMT_NO_CCK;
    }

    /* Get DMA Address */
    if (unlikely(rwnx_prep_tx(rwnx_hw, txhdr))) {
        kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);
        dev_kfree_skb(skb);
        return -EBUSY;
    }

    frame_oft = sizeof(struct rwnx_txhdr) - offsetof(struct rwnx_txhdr, hw_hdr);
	#if 0
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
    desc->host.packet_addr[0] = sw_txhdr->dma_addr + frame_oft;
    desc->host.packet_cnt = 1;
#else
    desc->host.packet_addr = sw_txhdr->dma_addr + frame_oft;
#endif
	#endif
    desc->host.status_desc_addr = sw_txhdr->dma_addr;

    //----------------------------------------------------------------------

    spin_lock_bh(&rwnx_hw->tx_lock);
	AICWFDBG(LOGDEBUG, "%s sta:%p skb:%p desc->host.staid:%d \r\n", __func__, sta, skb, desc->host.staid);
    if (rwnx_txq_queue_skb(skb, txq, rwnx_hw, false))
        rwnx_hwq_process(rwnx_hw, txq->hwq);
    spin_unlock_bh(&rwnx_hw->tx_lock);

    return 0;
}

#ifdef CONFIG_RWNX_MON_XMIT
/**
 * netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb,
 *                               struct net_device *dev);
 *	Called when a packet needs to be transmitted.
 *	Must return NETDEV_TX_OK , NETDEV_TX_BUSY.
 *        (can also return NETDEV_TX_LOCKED if NETIF_F_LLTX)
 *
 *  - Initialize the desciptor for this pkt (stored in skb before data)
 *  - Push the pkt in the corresponding Txq
 *  - If possible (i.e. credit available and not in PS) the pkt is pushed
 *    to fw
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
#define IEEE80211_RADIOTAP_MCS_HAVE_STBC	0x20
#define IEEE80211_RADIOTAP_MCS_STBC_MASK	0x60
#define IEEE80211_RADIOTAP_MCS_STBC_SHIFT	5
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
#define IEEE80211_RADIOTAP_CODING_LDPC_USER0			0x01
#endif

netdev_tx_t rwnx_start_monitor_if_xmit(struct sk_buff *skb, struct net_device *dev)
{
    int rtap_len, ret, idx, tmp_len;
    struct ieee80211_radiotap_header *rtap_hdr; // net/ieee80211_radiotap.h
    struct ieee80211_radiotap_iterator iterator; // net/cfg80211.h
    u8_l *rtap_buf = (u8_l *)skb->data;
    u8_l rate;

    struct rwnx_vif *vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
    struct rwnx_txhdr *txhdr;
    struct rwnx_sw_txhdr *sw_txhdr;
    struct txdesc_api *desc;
    struct rwnx_sta *sta;
    struct rwnx_txq *txq;
    u16_l frame_len, headroom, frame_oft;
    u8_l tid, rate_fmt = FORMATMOD_NON_HT, rate_idx = 0, txsig_bw = PHY_CHNL_BW_20;
    u8_l *pframe, *data;
    bool robust;
    struct sk_buff *skb_mgmt;
    bool offchan = false;
    int nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX;

    rtap_hdr = (struct ieee80211_radiotap_header*)(rtap_buf);
    rtap_len = ieee80211_get_radiotap_len(rtap_buf);
    frame_len = skb->len;

    printk("rwnx_start_monitor_if_xmit, skb_len=%d, rtap_len=%d\n", skb->len, rtap_len);

    if((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8801) || 
        ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
        g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW) && chip_id < 3)){
            nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX_FOR_OLD_IC;
    }


    if (unlikely(rtap_hdr->it_version))
        goto free_tag;

    if (unlikely(skb->len < rtap_len))
        goto free_tag;

    if (unlikely(rtap_len < sizeof(struct ieee80211_radiotap_header)))
        goto free_tag;

    frame_len -= rtap_len;
    pframe = rtap_buf + rtap_len;

    // Parse radiotap for injection items and overwrite attribs as needed
    ret = ieee80211_radiotap_iterator_init(&iterator, rtap_hdr, rtap_len, NULL);
    while (!ret) {
        ret = ieee80211_radiotap_iterator_next(&iterator);
        if (ret) {
            continue;
        }
        switch (iterator.this_arg_index) {
            case IEEE80211_RADIOTAP_RATE:
                // This is basic 802.11b/g rate; use MCS/VHT for higher rates
                rate = *iterator.this_arg;
                printk("rate=0x%x\n", rate);
                for (idx = 0; idx < HW_RATE_MAX; idx++) {
                    if ((rate * 5) == tx_legrates_lut_rate[idx]) {
                        break;
                    }
                }
                if (idx < HW_RATE_MAX) {
                    rate_idx = idx;
                } else {
                    printk("invalid radiotap rate: %d\n", rate);
                }
                break;

            case IEEE80211_RADIOTAP_TX_FLAGS: {
                u16_l txflags = get_unaligned_le16(iterator.this_arg);
                printk("txflags=0x%x\n", txflags);
                if ((txflags & IEEE80211_RADIOTAP_F_TX_NOACK) == 0) {
                    printk("  TX_NOACK\n");
                }
                if (txflags & 0x0010) { // Use preconfigured seq num
                    // NOTE: this is currently ignored due to qos_en=_FALSE and HW seq num override
                    printk("  GetSequence\n");
                }
            }
            break;

            case IEEE80211_RADIOTAP_MCS: {
                u8_l mcs_have = iterator.this_arg[0];
                printk("mcs_have=0x%x\n", mcs_have);
                rate_fmt = FORMATMOD_HT_MF;
                if (mcs_have & IEEE80211_RADIOTAP_MCS_HAVE_BW) {
                    u8_l bw = (iterator.this_arg[1] & IEEE80211_RADIOTAP_MCS_BW_MASK);
                    u8_l ch_offset = 0;
                    if (bw == IEEE80211_RADIOTAP_MCS_BW_40) {
                        txsig_bw = PHY_CHNL_BW_40;
                    } else if (bw == IEEE80211_RADIOTAP_MCS_BW_20L) {
                        bw = IEEE80211_RADIOTAP_MCS_BW_20;
                        ch_offset = 1; // CHNL_OFFSET_LOWER;
                    } else if (bw == IEEE80211_RADIOTAP_MCS_BW_20U) {
                        bw = IEEE80211_RADIOTAP_MCS_BW_20;
                        ch_offset = 2; // CHNL_OFFSET_UPPER;
                    }
                    printk("  bw=%d, ch_offset=%d\n", bw, ch_offset);
                }
                if (mcs_have & IEEE80211_RADIOTAP_MCS_HAVE_MCS) {
                    u8_l fixed_rate = iterator.this_arg[2] & 0x7f;
                    if (fixed_rate > 31) {
                        fixed_rate = 0;
                    }
                    rate_idx = fixed_rate;
                    printk("  fixed_rate=0x%x\n", fixed_rate);
                }
                if ((mcs_have & IEEE80211_RADIOTAP_MCS_HAVE_GI) && (iterator.this_arg[1] & IEEE80211_RADIOTAP_MCS_SGI)) {
                    printk("  sgi\n");
                }
                if ((mcs_have & IEEE80211_RADIOTAP_MCS_HAVE_FEC) && (iterator.this_arg[1] & IEEE80211_RADIOTAP_MCS_FEC_LDPC)) {
                    printk("  ldpc\n");
                }
                if (mcs_have & IEEE80211_RADIOTAP_MCS_HAVE_STBC) {
                    u8 stbc = (iterator.this_arg[1] & IEEE80211_RADIOTAP_MCS_STBC_MASK) >> IEEE80211_RADIOTAP_MCS_STBC_SHIFT;
                    printk("  stbc=0x%x\n", stbc);
                }
            }
            break;

            case IEEE80211_RADIOTAP_VHT: {
                unsigned int mcs, nss;
                u8 known = iterator.this_arg[0];
                u8 flags = iterator.this_arg[2];
                rate_fmt = FORMATMOD_VHT;
                printk("known=0x%x, flags=0x%x\n", known, flags);
                // NOTE: this code currently only supports 1SS for radiotap defined rates
                if ((known & IEEE80211_RADIOTAP_VHT_KNOWN_STBC) && (flags & IEEE80211_RADIOTAP_VHT_FLAG_STBC)) {
                    printk("  stbc\n");
                }
                if ((known & IEEE80211_RADIOTAP_VHT_KNOWN_GI) && (flags & IEEE80211_RADIOTAP_VHT_FLAG_SGI)) {
                    printk("  sgi\n");
                }
                if (known & IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH) {
                    u8_l bw = iterator.this_arg[3] & 0x1F;
                    printk("  bw=0x%x\n",bw);
                    // NOTE: there are various L and U, but we just use straight 20/40/80
                    // since it's not clear how to set CHNL_OFFSET_LOWER/_UPPER with different
                    // sideband sizes/configurations.  TODO.
                    // Also, any 160 is treated as 80 due to lack of WIDTH_160.
                    txsig_bw = PHY_CHNL_BW_40;
                    if (bw == 0) {
                        txsig_bw = PHY_CHNL_BW_20;
                        printk("  20M\n");
                    } else if (bw >=1 && bw <= 3) {
                        printk("  40M\n");
                    } else if (bw >=4 && bw <= 10) {
                        printk("  80M\n");
                    } else if (bw >= 11 && bw <= 25) {
                        printk("  160M\n");
                    }
                }
                // User 0
                nss = iterator.this_arg[4] & 0x0F; // Number of spatial streams
                printk("  nss=0x%x\n", nss);
                if (nss > 0) {
                    if (nss > 4) nss = 4;
                    mcs = (iterator.this_arg[4]>>4) & 0x0F; // MCS rate index
                    if (mcs > 8) mcs = 9;
                    rate_idx = mcs;
                    printk("    mcs=0x%x\n", mcs);
                    if (iterator.this_arg[8] & IEEE80211_RADIOTAP_CODING_LDPC_USER0) {
                        printk("    ldpc\n");
                    }
                }
            }
            break;

            case IEEE80211_RADIOTAP_HE: {
                u16 data1 = ((u16)iterator.this_arg[1] << 8) | iterator.this_arg[0];
                u16 data2 = ((u16)iterator.this_arg[3] << 8) | iterator.this_arg[2];
                u16 data3 = ((u16)iterator.this_arg[5] << 8) | iterator.this_arg[4];
                u16 data5 = ((u16)iterator.this_arg[9] << 8) | iterator.this_arg[8];
                u8 fmt_he = data1 & IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MASK;
                if (fmt_he == IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MU) {
                    rate_fmt = FORMATMOD_HE_MU;
                } else if (fmt_he == IEEE80211_RADIOTAP_HE_DATA1_FORMAT_EXT_SU) {
                    rate_fmt = FORMATMOD_HE_ER;
                } else {
                    rate_fmt = FORMATMOD_HE_SU;
                }
                if (data1 & IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN) {
                    u8 mcs = (data3 & IEEE80211_RADIOTAP_HE_DATA3_DATA_MCS) >> 8;
                    if (mcs > 11) mcs = 11;
                    rate_idx = mcs;
                }
                if (data1 & IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN) {
                    u8 bw = data5 & IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC;
                    txsig_bw = (bw == IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_20MHZ) ? PHY_CHNL_BW_20 : PHY_CHNL_BW_40;
                }
                if (data2 & IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN) {
                    u8 gi = (data5 & IEEE80211_RADIOTAP_HE_DATA5_GI) >> 4;
                    printk("  gi: %d\n", gi);
                }
            }
            break;

            default:
                printk("unparsed arg: 0x%x\n",iterator.this_arg_index);
                break;
        }
    }

    #if 0
    // dump buffer
    tmp_len = 128;
    if (skb->len < 128) {
        tmp_len = skb->len;
    }
    for (idx = 0; idx < tmp_len; idx+=16) {
        printk("[%04X] %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X\n", idx,
            rtap_buf[idx+0],rtap_buf[idx+1],rtap_buf[idx+2],rtap_buf[idx+3],
            rtap_buf[idx+4],rtap_buf[idx+5],rtap_buf[idx+6],rtap_buf[idx+7],
            rtap_buf[idx+8],rtap_buf[idx+9],rtap_buf[idx+10],rtap_buf[idx+11],
            rtap_buf[idx+12],rtap_buf[idx+13],rtap_buf[idx+14],rtap_buf[idx+15]);
    }
    #endif

    /* Get the STA id and TID information */
    sta = rwnx_get_tx_priv(vif, skb, &tid);
    //if (!sta) {
    //    printk("sta=null, tid=0x%x\n", tid);
    //}
    /* Set TID and Queues indexes */
    if (sta) {
        txq = rwnx_txq_sta_get(sta, 8, rwnx_hw);
    } else {
        if (offchan)
            txq = &rwnx_hw->txq[nx_off_chan_txq_idx];
        else
            txq = rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
    }
    if (txq->idx == TXQ_INACTIVE) {
        printk("TXQ_INACTIVE\n");
        goto free_tag;
    }
    // prepare to xmit
    headroom = sizeof(struct rwnx_txhdr);
    skb_mgmt = dev_alloc_skb(headroom + frame_len);
    if (!skb_mgmt) {
        printk("skb_mgmt alloc fail\n");
        goto free_tag;
    }
    skb_reserve(skb_mgmt, headroom);
    data = skb_put(skb_mgmt, frame_len);
    /* Copy the provided data */
    memcpy(data, pframe, frame_len);
    robust = ieee80211_is_robust_mgmt_frame(
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
		(void*)skb_mgmt
#else
		skb_mgmt
#endif
		);
    skb_push(skb_mgmt, headroom);
    /* Fill the TX Header */
    txhdr = (struct rwnx_txhdr *)skb_mgmt->data;
    txhdr->hw_hdr.cfm.status.value = 0;
    /* Fill the SW TX Header */
    sw_txhdr = kmem_cache_alloc(rwnx_hw->sw_txhdr_cache, GFP_ATOMIC);
    if (unlikely(sw_txhdr == NULL)) {
        dev_kfree_skb(skb_mgmt);
        printk("sw_txhdr alloc fail\n");
        goto free_tag;
    }
    txhdr->sw_hdr = sw_txhdr;
    sw_txhdr->txq = txq;
    sw_txhdr->frame_len = frame_len;
    sw_txhdr->rwnx_sta = sta;
    sw_txhdr->rwnx_vif = vif;
    sw_txhdr->skb = skb_mgmt;
    sw_txhdr->headroom = headroom;
    sw_txhdr->map_len = skb_mgmt->len - offsetof(struct rwnx_txhdr, hw_hdr);
    sw_txhdr->raw_frame = 1;
    sw_txhdr->fixed_rate = 1;
    sw_txhdr->rate_config = ((rate_fmt << FORMAT_MOD_TX_RCX_OFT) & FORMAT_MOD_TX_RCX_MASK) |
                            ((txsig_bw << BW_TX_RCX_OFT) & BW_TX_RCX_MASK) |
                            ((rate_idx << MCS_INDEX_TX_RCX_OFT) & MCS_INDEX_TX_RCX_MASK); // from radiotap
    /* Fill the Descriptor to be provided to the MAC SW */
    desc = &sw_txhdr->desc;
    desc->host.staid = (sta) ? sta->sta_idx : 0xFF;
    desc->host.vif_idx = vif->vif_index;
    desc->host.tid = 0xFF;
    desc->host.flags = TXU_CNTRL_MGMT;
    if (robust) {
        desc->host.flags |= TXU_CNTRL_MGMT_ROBUST;
    }
    frame_oft = sizeof(struct rwnx_txhdr) - offsetof(struct rwnx_txhdr, hw_hdr);
	#if 0
    #ifdef CONFIG_RWNX_SPLIT_TX_BUF
    desc->host.packet_addr[0] = sw_txhdr->dma_addr + frame_oft;
    desc->host.packet_len[0] = frame_len;
    desc->host.packet_cnt = 1;
    #else
    desc->host.packet_addr = sw_txhdr->dma_addr + frame_oft;
    desc->host.packet_len = frame_len;
    #endif
	#else
	desc->host.packet_len = frame_len;
	#endif

    desc->host.status_desc_addr = sw_txhdr->dma_addr;

    spin_lock_bh(&rwnx_hw->tx_lock);
    if (rwnx_txq_queue_skb(skb_mgmt, txq, rwnx_hw, false))
        rwnx_hwq_process(rwnx_hw, txq->hwq);
    spin_unlock_bh(&rwnx_hw->tx_lock);

free_tag:
    dev_kfree_skb_any(skb);
    return NETDEV_TX_OK;
}
#endif

/**
 * rwnx_txdatacfm - FW callback for TX confirmation
 *
 * called with tx_lock hold
 */
int rwnx_txdatacfm(void *pthis, void *host_id)
{
    struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)pthis;
    struct sk_buff *skb = host_id;
    struct rwnx_txhdr *txhdr;
    union rwnx_hw_txstatus rwnx_txst;
    struct rwnx_sw_txhdr *sw_txhdr;
    struct rwnx_hwq *hwq;
    struct rwnx_txq *txq;
    u16 headroom;
    //int peek_off = offsetof(struct rwnx_hw_txhdr, cfm);
    //int peek_len = sizeof(((struct rwnx_hw_txhdr *)0)->cfm);

    txhdr = (struct rwnx_txhdr *)skb->data;
    sw_txhdr = txhdr->sw_hdr;

    /* Read status in the TX control header */
    rwnx_txst = txhdr->hw_hdr.cfm.status;

    /* Check status in the header. If status is null, it means that the buffer
     * was not transmitted and we have to return immediately */
    if (rwnx_txst.value == 0) {
        return -1;
    }

#ifdef AICWF_USB_SUPPORT
    if (rwnx_hw->usbdev->state == USB_DOWN_ST) {
        headroom = sw_txhdr->headroom;
        kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);
        skb_pull(skb, headroom);
        consume_skb(skb);
        return 0;
    }
#endif
#ifdef AICWF_SDIO_SUPPORT
    if(rwnx_hw->sdiodev->bus_if->state == BUS_DOWN_ST) {
        headroom = sw_txhdr->headroom;
        kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);
        skb_pull(skb, headroom);
        consume_skb(skb);
        return 0;
    }
#endif

    txq = sw_txhdr->txq;
    /* don't use txq->hwq as it may have changed between push and confirm */
    hwq = &rwnx_hw->hwq[sw_txhdr->hw_queue];
    rwnx_txq_confirm_any(rwnx_hw, txq, hwq, sw_txhdr);

    /* Update txq and HW queue credits */
    if (sw_txhdr->desc.host.flags & TXU_CNTRL_MGMT) {
        trace_printk("done=%d retry_required=%d sw_retry_required=%d acknowledged=%d\n",
                     rwnx_txst.tx_done, rwnx_txst.retry_required,
                     rwnx_txst.sw_retry_required, rwnx_txst.acknowledged);
#ifdef CREATE_TRACE_POINTS
        trace_mgmt_cfm(sw_txhdr->rwnx_vif->vif_index,
                       (sw_txhdr->rwnx_sta) ? sw_txhdr->rwnx_sta->sta_idx : 0xFF,
                       rwnx_txst.acknowledged);
#endif
        /* Confirm transmission to CFG80211 */
        cfg80211_mgmt_tx_status(&sw_txhdr->rwnx_vif->wdev,
                                (unsigned long)skb,
                                (skb->data + sw_txhdr->headroom),
                                sw_txhdr->frame_len,
                                rwnx_txst.acknowledged,
                                GFP_ATOMIC);
    } else if ((txq->idx != TXQ_INACTIVE) &&
               (rwnx_txst.retry_required || rwnx_txst.sw_retry_required)) {
        bool sw_retry = (rwnx_txst.sw_retry_required) ? true : false;

        /* Reset the status */
        txhdr->hw_hdr.cfm.status.value = 0;

        /* The confirmed packet was part of an AMPDU and not acked
         * correctly, so reinject it in the TX path to be retried */
        rwnx_tx_retry(rwnx_hw, skb, txhdr, sw_retry);
        return 0;
    }
#ifdef CREATE_TRACE_POINTS
    trace_skb_confirm(skb, txq, hwq, &txhdr->hw_hdr.cfm);
#endif
    /* STA may have disconnect (and txq stopped) when buffers were stored
       in fw. In this case do nothing when they're returned */
    if (txq->idx != TXQ_INACTIVE) {
        #if 0
        if (txhdr->hw_hdr.cfm.credits) {
            txq->credits += txhdr->hw_hdr.cfm.credits;
            if (txq->credits <= 0)
                rwnx_txq_stop(txq, RWNX_TXQ_STOP_FULL);
            else if (txq->credits > 0)
                rwnx_txq_start(txq, RWNX_TXQ_STOP_FULL);
        }
        #endif

        /* continue service period */
        if (unlikely(txq->push_limit && !rwnx_txq_is_full(txq))) {
            rwnx_txq_add_to_hw_list(txq);
        }
    }

    if (txhdr->hw_hdr.cfm.ampdu_size &&
        txhdr->hw_hdr.cfm.ampdu_size < IEEE80211_MAX_AMPDU_BUF)
        rwnx_hw->stats.ampdus_tx[txhdr->hw_hdr.cfm.ampdu_size - 1]++;

#ifdef CONFIG_RWNX_AMSDUS_TX
    txq->amsdu_len = txhdr->hw_hdr.cfm.amsdu_size;
#endif

    /* Update statistics */
    sw_txhdr->rwnx_vif->net_stats.tx_packets++;
    sw_txhdr->rwnx_vif->net_stats.tx_bytes += sw_txhdr->frame_len;

    /* Release SKBs */
#ifdef CONFIG_RWNX_AMSDUS_TX
    if (sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU) {
        struct rwnx_amsdu_txhdr *amsdu_txhdr;
        list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
            rwnx_amsdu_del_subframe_header(amsdu_txhdr);
            consume_skb(amsdu_txhdr->skb);
        }
    }
#endif /* CONFIG_RWNX_AMSDUS_TX */

    kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);
    skb_pull(skb, sw_txhdr->headroom);
    consume_skb(skb);

    return 0;
}

/**
 * rwnx_txq_credit_update - Update credit for one txq
 *
 * @rwnx_hw: Driver main data
 * @sta_idx: STA idx
 * @tid: TID
 * @update: offset to apply in txq credits
 *
 * Called when fw send ME_TX_CREDITS_UPDATE_IND message.
 * Apply @update to txq credits, and stop/start the txq if needed
 */
void rwnx_txq_credit_update(struct rwnx_hw *rwnx_hw, int sta_idx, u8 tid,
                            s8 update)
{
    struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
    struct rwnx_txq *txq;

    txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);

    spin_lock_bh(&rwnx_hw->tx_lock);

    if (txq->idx != TXQ_INACTIVE) {
        //txq->credits += update;
#ifdef CREATE_TRACE_POINTS
        trace_credit_update(txq, update);
#endif
        if (txq->credits <= 0)
            rwnx_txq_stop(txq, RWNX_TXQ_STOP_FULL);
        else
            rwnx_txq_start(txq, RWNX_TXQ_STOP_FULL);
    }

    spin_unlock_bh(&rwnx_hw->tx_lock);
}
