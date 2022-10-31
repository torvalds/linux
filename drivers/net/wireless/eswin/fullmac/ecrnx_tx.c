/**
 ******************************************************************************
 *
 * @file ecrnx_tx.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <net/sock.h>

#include "ecrnx_defs.h"
#include "ecrnx_tx.h"
#include "ecrnx_msg_tx.h"
#include "ecrnx_mesh.h"
#include "ecrnx_events.h"
#include "ecrnx_compat.h"

#ifdef CONFIG_ECRNX_ESWIN
#include "eswin_utils.h"
#endif
/******************************************************************************
 * Power Save functions
 *****************************************************************************/
/**
 * ecrnx_set_traffic_status - Inform FW if traffic is available for STA in PS
 *
 * @ecrnx_hw: Driver main data
 * @sta: Sta in PS mode
 * @available: whether traffic is buffered for the STA
 * @ps_id: type of PS data requested (@LEGACY_PS_ID or @UAPSD_ID)
  */
void ecrnx_set_traffic_status(struct ecrnx_hw *ecrnx_hw,
                             struct ecrnx_sta *sta,
                             bool available,
                             u8 ps_id)
{
    if (sta->tdls.active) {
        ecrnx_send_tdls_peer_traffic_ind_req(ecrnx_hw,
                                            ecrnx_hw->vif_table[sta->vif_idx]);
    } else {
        bool uapsd = (ps_id != LEGACY_PS_ID);
        ecrnx_send_me_traffic_ind(ecrnx_hw, sta->sta_idx, uapsd, available);
        trace_ps_traffic_update(sta->sta_idx, available, uapsd);
    }
}

/**
 * ecrnx_ps_bh_enable - Enable/disable PS mode for one STA
 *
 * @ecrnx_hw: Driver main data
 * @sta: Sta which enters/leaves PS mode
 * @enable: PS mode status
 *
 * This function will enable/disable PS mode for one STA.
 * When enabling PS mode:
 *  - Stop all STA's txq for ECRNX_TXQ_STOP_STA_PS reason
 *  - Count how many buffers are already ready for this STA
 *  - For BC/MC sta, update all queued SKB to use hw_queue BCMC
 *  - Update TIM if some packet are ready
 *
 * When disabling PS mode:
 *  - Start all STA's txq for ECRNX_TXQ_STOP_STA_PS reason
 *  - For BC/MC sta, update all queued SKB to use hw_queue AC_BE
 *  - Update TIM if some packet are ready (otherwise fw will not update TIM
 *    in beacon for this STA)
 *
 * All counter/skb updates are protected from TX path by taking tx_lock
 *
 * NOTE: _bh_ in function name indicates that this function is called
 * from a bottom_half tasklet.
 */
void ecrnx_ps_bh_enable(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta,
                       bool enable)
{
    struct ecrnx_txq *txq;

    if (enable) {
        trace_ps_enable(sta);

        spin_lock(&ecrnx_hw->tx_lock);
        sta->ps.active = true;
        sta->ps.sp_cnt[LEGACY_PS_ID] = 0;
        sta->ps.sp_cnt[UAPSD_ID] = 0;
        ecrnx_txq_sta_stop(sta, ECRNX_TXQ_STOP_STA_PS, ecrnx_hw);

        if (is_multicast_sta(sta->sta_idx)) {
            txq = ecrnx_txq_sta_get(sta, 0, ecrnx_hw);
            sta->ps.pkt_ready[LEGACY_PS_ID] = skb_queue_len(&txq->sk_list);
            sta->ps.pkt_ready[UAPSD_ID] = 0;
            txq->hwq = &ecrnx_hw->hwq[ECRNX_HWQ_BCMC];
        } else {
            int i;
            sta->ps.pkt_ready[LEGACY_PS_ID] = 0;
            sta->ps.pkt_ready[UAPSD_ID] = 0;
            foreach_sta_txq(sta, txq, i, ecrnx_hw) {
                sta->ps.pkt_ready[txq->ps_id] += skb_queue_len(&txq->sk_list);
            }
        }

        spin_unlock(&ecrnx_hw->tx_lock);

        if (sta->ps.pkt_ready[LEGACY_PS_ID])
            ecrnx_set_traffic_status(ecrnx_hw, sta, true, LEGACY_PS_ID);

        if (sta->ps.pkt_ready[UAPSD_ID])
            ecrnx_set_traffic_status(ecrnx_hw, sta, true, UAPSD_ID);
    } else {
        trace_ps_disable(sta->sta_idx);

        spin_lock(&ecrnx_hw->tx_lock);
        sta->ps.active = false;

        if (is_multicast_sta(sta->sta_idx)) {
            txq = ecrnx_txq_sta_get(sta, 0, ecrnx_hw);
            txq->hwq = &ecrnx_hw->hwq[ECRNX_HWQ_BE];
            txq->push_limit = 0;
        } else {
            int i;
            foreach_sta_txq(sta, txq, i, ecrnx_hw) {
                txq->push_limit = 0;
            }
        }

        ecrnx_txq_sta_start(sta, ECRNX_TXQ_STOP_STA_PS, ecrnx_hw);
        spin_unlock(&ecrnx_hw->tx_lock);

        if (sta->ps.pkt_ready[LEGACY_PS_ID])
            ecrnx_set_traffic_status(ecrnx_hw, sta, false, LEGACY_PS_ID);

        if (sta->ps.pkt_ready[UAPSD_ID])
            ecrnx_set_traffic_status(ecrnx_hw, sta, false, UAPSD_ID);
    }
}

/**
 * ecrnx_ps_bh_traffic_req - Handle traffic request for STA in PS mode
 *
 * @ecrnx_hw: Driver main data
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
void ecrnx_ps_bh_traffic_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta,
                            u16 pkt_req, u8 ps_id)
{
    int pkt_ready_all;
	u16 txq_len;
    struct ecrnx_txq *txq;

#ifndef CONFIG_ECRNX_ESWIN
    if (WARN(!sta->ps.active, "sta %pM is not in Power Save mode",
             sta->mac_addr))
        return;
#else
    if (!sta->ps.active)
    {
        ECRNX_DBG(" sta is not in Power Save mode %02x:%02x:%02x:%02x:%02x:%02x %d %d \n", sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2], \
                sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5],pkt_req, ps_id);
        return;
    }
#endif

    trace_ps_traffic_req(sta, pkt_req, ps_id);

    spin_lock(&ecrnx_hw->tx_lock);

    /* Fw may ask to stop a service period with PS_SP_INTERRUPTED. This only
       happens for p2p-go interface if NOA starts during a service period */
    if ((pkt_req == PS_SP_INTERRUPTED) && (ps_id == UAPSD_ID)) {
        int tid;
        sta->ps.sp_cnt[ps_id] = 0;
        foreach_sta_txq(sta, txq, tid, ecrnx_hw) {
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

    /* "dispatch" the request between txq */
    if (is_multicast_sta(sta->sta_idx)) {
        txq = ecrnx_txq_sta_get(sta, 0, ecrnx_hw);
        if (txq->credits <= 0)
            goto done;
        if (pkt_req > txq->credits)
            pkt_req = txq->credits;
        txq->push_limit = pkt_req;
        sta->ps.sp_cnt[ps_id] = pkt_req;
        ECRNX_DBG("%s-%d:sta:0x%p, sta_idx:%d, txq:0x%p, txq status:%d \n", __func__, __LINE__, sta, sta->sta_idx, txq, txq->status);
        ecrnx_txq_add_to_hw_list(txq);
        ecrnx_txq_sta_start(sta, ECRNX_TXQ_STOP_STA_PS, ecrnx_hw);
    } else {
        int i, tid;

        for (i = 0; i < NX_NB_TID_PER_STA; i++) {
			tid = nx_tid_prio[i];

#ifdef CONFIG_ECRNX_SOFTMAC
			txq = ecrnx_txq_sta_get(sta, tid);
#else
			txq = ecrnx_txq_sta_get(sta, tid, ecrnx_hw);
#endif
			
            txq_len = skb_queue_len(&txq->sk_list);

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
                ecrnx_txq_add_to_hw_list(txq);
            } else {
                /* Enough pkt in this txq to comlete the request
                   add this txq to hwq list and stop processing txq */
                txq->push_limit = pkt_req;
                sta->ps.sp_cnt[ps_id] += pkt_req;
                ecrnx_txq_add_to_hw_list(txq);
                break;
            }
        }
    }

  done:
    spin_unlock(&ecrnx_hw->tx_lock);
}

/******************************************************************************
 * TX functions
 *****************************************************************************/
#define PRIO_STA_NULL 0xAA

static const int ecrnx_down_hwq2tid[3] = {
    [ECRNX_HWQ_BK] = 2,
    [ECRNX_HWQ_BE] = 3,
    [ECRNX_HWQ_VI] = 5,
};

static void ecrnx_downgrade_ac(struct ecrnx_sta *sta, struct sk_buff *skb)
{
    int8_t ac = ecrnx_tid2hwq[skb->priority];

    if (WARN((ac > ECRNX_HWQ_VO),
             "Unexepcted ac %d for skb before downgrade", ac))
        ac = ECRNX_HWQ_VO;

    while (sta->acm & BIT(ac)) {
        if (ac == ECRNX_HWQ_BK) {
            skb->priority = 1;
            return;
        }
        ac--;
        skb->priority = ecrnx_down_hwq2tid[ac];
    }
}

static void ecrnx_tx_statistic(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txq *txq,
                              union ecrnx_hw_txstatus ecrnx_txst, unsigned int data_len)
{
    struct ecrnx_sta *sta = txq->sta;
    if (!sta || !ecrnx_txst.acknowledged)
        return;
    sta->stats.tx_pkts ++;
    sta->stats.tx_bytes += data_len;
    sta->stats.last_act = ecrnx_hw->stats.last_tx;
}
u16 ecrnx_select_txq(struct ecrnx_vif *ecrnx_vif, struct sk_buff *skb)
{
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;
    struct wireless_dev *wdev = &ecrnx_vif->wdev;
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_txq *txq;
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
        if ((ecrnx_vif->tdls_status == TDLS_LINK_ACTIVE) &&
            (ecrnx_vif->sta.tdls_sta != NULL) &&
            (memcmp(eth->h_dest, ecrnx_vif->sta.tdls_sta->mac_addr, ETH_ALEN) == 0))
            sta = ecrnx_vif->sta.tdls_sta;
        else
            sta = ecrnx_vif->sta.ap;
        break;
    }
    case NL80211_IFTYPE_AP_VLAN:
    {
        struct ecrnx_sta *cur;
        struct ethhdr *eth = (struct ethhdr *)skb->data;

        if (ecrnx_vif->ap_vlan.sta_4a) {
            sta = ecrnx_vif->ap_vlan.sta_4a;
            break;
        }

        /* AP_VLAN interface is not used for a 4A STA,
           fallback searching sta amongs all AP's clients */
        ecrnx_vif = ecrnx_vif->ap_vlan.master;
        
        if (is_multicast_ether_addr(eth->h_dest)) {
            sta = &ecrnx_hw->sta_table[ecrnx_vif->ap.bcmc_index];
        } else {
            list_for_each_entry(cur, &ecrnx_vif->ap.sta_list, list) {
                if (!memcmp(cur->mac_addr, eth->h_dest, ETH_ALEN)) {
                    sta = cur;
                    break;
                }
            }
        }

        break;
    }
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_P2P_GO:
    {
        struct ecrnx_sta *cur;
        struct ethhdr *eth = (struct ethhdr *)skb->data;

        if (is_multicast_ether_addr(eth->h_dest)) {
            sta = &ecrnx_hw->sta_table[ecrnx_vif->ap.bcmc_index];
        } else {
            list_for_each_entry(cur, &ecrnx_vif->ap.sta_list, list) {
                if (!memcmp(cur->mac_addr, eth->h_dest, ETH_ALEN)) {
                    sta = cur;
                    break;
                }
            }
        }

        break;
    }
    case NL80211_IFTYPE_MESH_POINT:
    {
        struct ethhdr *eth = (struct ethhdr *)skb->data;

        if (!ecrnx_vif->is_resending) {
            /*
             * If ethernet source address is not the address of a mesh wireless interface, we are proxy for
             * this address and have to inform the HW
             */
            if (memcmp(&eth->h_source[0], &ecrnx_vif->ndev->perm_addr[0], ETH_ALEN)) {
                /* Check if LMAC is already informed */
                if (!ecrnx_get_mesh_proxy_info(ecrnx_vif, (u8 *)&eth->h_source, true)) {
                    ecrnx_send_mesh_proxy_add_req(ecrnx_hw, ecrnx_vif, (u8 *)&eth->h_source);
                }
            }
        }

        if (is_multicast_ether_addr(eth->h_dest)) {
            sta = &ecrnx_hw->sta_table[ecrnx_vif->ap.bcmc_index];
        } else {
            /* Path to be used */
            struct ecrnx_mesh_path *p_mesh_path = NULL;
            struct ecrnx_mesh_path *p_cur_path;
            /* Check if destination is proxied by a peer Mesh STA */
            struct ecrnx_mesh_proxy *p_mesh_proxy = ecrnx_get_mesh_proxy_info(ecrnx_vif, (u8 *)&eth->h_dest, false);
            /* Mesh Target address */
            struct mac_addr *p_tgt_mac_addr;

            if (p_mesh_proxy) {
                p_tgt_mac_addr = &p_mesh_proxy->proxy_addr;
            } else {
                p_tgt_mac_addr = (struct mac_addr *)&eth->h_dest;
            }

            /* Look for path with provided target address */
            list_for_each_entry(p_cur_path, &ecrnx_vif->ap.mpath_list, list) {
                if (!memcmp(&p_cur_path->tgt_mac_addr, p_tgt_mac_addr, ETH_ALEN)) {
                    p_mesh_path = p_cur_path;
                    break;
                }
            }

            if (p_mesh_path) {
                sta = p_mesh_path->nhop_sta;
            } else {
                ecrnx_send_mesh_path_create_req(ecrnx_hw, ecrnx_vif, (u8 *)p_tgt_mac_addr);
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
            ecrnx_downgrade_ac(sta, skb);

        txq = ecrnx_txq_sta_get(sta, skb->priority, ecrnx_hw);
        netdev_queue = txq->ndev_idx;
    }
    else if (sta)
    {
        skb->priority = 0xFF;
        txq = ecrnx_txq_sta_get(sta, 0, ecrnx_hw);
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

    BUG_ON(netdev_queue >= NX_NB_NDEV_TXQ);

    return netdev_queue;
}

/**
 * ecrnx_set_more_data_flag - Update MORE_DATA flag in tx sw desc
 *
 * @ecrnx_hw: Driver main data
 * @sw_txhdr: Header for pkt to be pushed
 *
 * If STA is in PS mode
 *  - Set EOSP in case the packet is the last of the UAPSD service period
 *  - Set MORE_DATA flag if more pkt are ready for this sta
 *  - Update TIM if this is the last pkt buffered for this sta
 *
 * note: tx_lock already taken.
 */
static inline void ecrnx_set_more_data_flag(struct ecrnx_hw *ecrnx_hw,
                                           struct ecrnx_sw_txhdr *sw_txhdr)
{
    struct ecrnx_sta *sta = sw_txhdr->ecrnx_sta;
    struct ecrnx_vif *vif = sw_txhdr->ecrnx_vif;
    struct ecrnx_txq *txq = sw_txhdr->txq;

    if (unlikely(sta->ps.active)) {
        sta->ps.pkt_ready[txq->ps_id]--;
        sta->ps.sp_cnt[txq->ps_id]--;

        trace_ps_push(sta);

        if (((txq->ps_id == UAPSD_ID) || (vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) || (sta->tdls.active))
                && !sta->ps.sp_cnt[txq->ps_id]) {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_EOSP;
        }

        if (sta->ps.pkt_ready[txq->ps_id]) {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_MORE_DATA;
        } else {
            ecrnx_set_traffic_status(ecrnx_hw, sta, false, txq->ps_id);
        }
    }
}

/**
 * ecrnx_get_tx_info - Get STA and tid for one skb
 *
 * @ecrnx_vif: vif ptr
 * @skb: skb
 * @tid: pointer updated with the tid to use for this skb
 *
 * @return: pointer on the destination STA (may be NULL)
 *
 * skb has already been parsed in ecrnx_select_queue function
 * simply re-read information form skb.
 */
static struct ecrnx_sta *ecrnx_get_tx_info(struct ecrnx_vif *ecrnx_vif,
                                         struct sk_buff *skb,
                                         u8 *tid)
{
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;
    struct ecrnx_sta *sta;
    int sta_idx;

    *tid = skb->priority;
    if (unlikely(skb->priority == PRIO_STA_NULL)) {
        return NULL;
    } else {
        int ndev_idx = skb_get_queue_mapping(skb);

        if (ndev_idx == NX_BCMC_TXQ_NDEV_IDX)
            sta_idx = NX_REMOTE_STA_MAX + master_vif_idx(ecrnx_vif);
        else
            sta_idx = ndev_idx / NX_NB_TID_PER_STA;

        sta = &ecrnx_hw->sta_table[sta_idx];
    }

    return sta;
}

#ifndef CONFIG_ECRNX_ESWIN
/**
 * ecrnx_prep_tx - Prepare buffer for DMA transmission
 *
 * @ecrnx_hw: Driver main data
 * @txhdr: Tx descriptor
 *
 * Maps hw_txhdr and buffer data for transmission via DMA.
 * - Data buffer with be downloaded by embebded side.
 * - hw_txhdr will be uploaded by embedded side when buffer has been
 *   transmitted over the air.
 */
static int ecrnx_prep_dma_tx(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txhdr *txhdr, bool eth_hdr)
{
    struct ecrnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct ecrnx_hw_txhdr *hw_txhdr = &txhdr->hw_hdr;
    struct txdesc_api *desc = &sw_txhdr->desc;
    dma_addr_t dma_addr;

    txhdr->hw_hdr.cfm.status.value = 0;
    /* MAP (and sync) memory for DMA */
    dma_addr = dma_map_single(ecrnx_hw->dev, hw_txhdr,
                              sw_txhdr->map_len, DMA_BIDIRECTIONAL);
    if (WARN_ON(dma_mapping_error(ecrnx_hw->dev, dma_addr)))
        return -1;

    sw_txhdr->dma_addr = dma_addr;

    desc->host.status_desc_addr = dma_addr;
    dma_addr += ECRNX_TX_DATA_OFT(sw_txhdr);
    if (eth_hdr)
        dma_addr += sizeof(struct ethhdr);
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    desc->host.packet_len[0] = sw_txhdr->frame_len;
    desc->host.packet_addr[0] = dma_addr;
    desc->host.packet_cnt = 1;
#else
    desc->host.packet_len = sw_txhdr->frame_len;
    desc->host.packet_addr = dma_addr;
#endif
    return 0;
}
#endif

/**
 *  ecrnx_tx_push - Push one packet to fw
 *
 * @ecrnx_hw: Driver main data
 * @txhdr: tx desc of the buffer to push
 * @flags: push flags (see @ecrnx_push_flags)
 *
 * Push one packet to fw. Sw desc of the packet has already been updated.
 * Only MORE_DATA flag will be set if needed.
 */
void ecrnx_tx_push(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txhdr *txhdr, int flags)
{
    struct ecrnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct sk_buff *skb = sw_txhdr->skb;
    struct ecrnx_txq *txq = sw_txhdr->txq;
    u16 hw_queue = txq->hwq->id;
    int user = 0;

    lockdep_assert_held(&ecrnx_hw->tx_lock);

    /* RETRY flag is not always set so retest here */
    if (txq->nb_retry) {
        flags |= ECRNX_PUSH_RETRY;
        txq->nb_retry--;
        if (txq->nb_retry == 0) {
            WARN(skb != txq->last_retry_skb,
                 "last retry buffer is not the expected one");
            txq->last_retry_skb = NULL;
        }
    } else if (!(flags & ECRNX_PUSH_RETRY)) {
        txq->pkt_sent++;
    }

#ifdef CONFIG_ECRNX_AMSDUS_TX
    if (txq->amsdu == sw_txhdr) {
        WARN((flags & ECRNX_PUSH_RETRY), "End A-MSDU on a retry");
        ecrnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
        txq->amsdu = NULL;
    } else if (!(flags & ECRNX_PUSH_RETRY) &&
               !(sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU)) {
        ecrnx_hw->stats.amsdus[0].done++;
    }
#endif /* CONFIG_ECRNX_AMSDUS_TX */

    /* Wait here to update hw_queue, as for multicast STA hwq may change
       between queue and push (because of PS) */
    sw_txhdr->hw_queue = hw_queue;

#ifdef CONFIG_ECRNX_MUMIMO_TX
    /* MU group is only selected during hwq processing */
    sw_txhdr->desc.host.mumimo_info = txq->mumimo_info;
    user = ECRNX_TXQ_POS_ID(txq);
#endif /* CONFIG_ECRNX_MUMIMO_TX */

    if (sw_txhdr->ecrnx_sta) {
        /* only for AP mode */
        ecrnx_set_more_data_flag(ecrnx_hw, sw_txhdr);
    }

    trace_push_desc(skb, sw_txhdr, flags);
    txq->credits--;
    txq->pkt_pushed[user]++;
    if (txq->credits <= 0){
        ECRNX_DBG("%s-%d:ecrnx_txq_stop,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_FULL);
        ecrnx_txq_stop(txq, ECRNX_TXQ_STOP_FULL);
    }

    if (txq->push_limit)
        txq->push_limit--;

    ecrnx_ipc_txdesc_push(ecrnx_hw, &sw_txhdr->desc, skb, hw_queue, user);
    txq->hwq->credits[user]--;
    ecrnx_hw->stats.cfm_balance[hw_queue]++;
}



/**
 * ecrnx_tx_retry - Push an AMPDU pkt that need to be retried
 *
 * @ecrnx_hw: Driver main data
 * @skb: pkt to re-push
 * @txhdr: tx desc of the pkt to re-push
 * @sw_retry: Indicates if fw decide to retry this buffer
 *            (i.e. it has never been transmitted over the air)
 *
 * Called when a packet needs to be repushed to the firmware.
 * First update sw descriptor and then queue it in the retry list.
 */
static void ecrnx_tx_retry(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb,
                           struct ecrnx_txhdr *txhdr, bool sw_retry)
{
    struct ecrnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct tx_cfm_tag *cfm = &txhdr->hw_hdr.cfm;
    struct ecrnx_txq *txq = sw_txhdr->txq;
#ifndef CONFIG_ECRNX_ESWIN
    dma_addr_t cfm_dma_addr;
#endif

    if (!sw_retry) {
        /* update sw desc */
        sw_txhdr->desc.host.sn = cfm->sn;
        sw_txhdr->desc.host.pn[0] = cfm->pn[0];
        sw_txhdr->desc.host.pn[1] = cfm->pn[1];
        sw_txhdr->desc.host.pn[2] = cfm->pn[2];
        sw_txhdr->desc.host.pn[3] = cfm->pn[3];
        sw_txhdr->desc.host.timestamp = cfm->timestamp;
        sw_txhdr->desc.host.flags |= TXU_CNTRL_RETRY;

        #ifdef CONFIG_ECRNX_AMSDUS_TX
        if (sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU)
            ecrnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].failed++;
        #endif
    }

    /* MORE_DATA will be re-set if needed when pkt will be repushed */
    sw_txhdr->desc.host.flags &= ~TXU_CNTRL_MORE_DATA;

    cfm->status.value = 0;
//TODO:need to check here. 
#ifndef CONFIG_ECRNX_ESWIN
	cfm_dma_addr = (ptr_addr)sw_txhdr->desc.host.status_desc_addr;
    dma_sync_single_for_device(ecrnx_hw->dev, cfm_dma_addr, sizeof(cfm), DMA_BIDIRECTIONAL);
#endif
    txq->credits++;
    if (txq->credits > 0){
        ecrnx_txq_start(txq, ECRNX_TXQ_STOP_FULL);
        ECRNX_DBG("%s-%d:ecrnx_txq_start,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_FULL);
    }
    /* Queue the buffer */
    if (ecrnx_txq_queue_skb(skb, txq, ecrnx_hw, true, NULL))
    {
        /* baoyong:we need to send this AMPDU retry pkt asap, so process it now */
        ecrnx_hwq_process(ecrnx_hw, txq->hwq);
    }

    return;
}


#ifdef CONFIG_ECRNX_AMSDUS_TX
/* return size of subframe (including header) */
static inline int ecrnx_amsdu_subframe_length(struct ethhdr *eth, int eth_len)
{
    /* ethernet header is replaced with amdsu header that have the same size
       Only need to check if LLC/SNAP header will be added */
    int len = eth_len;

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        len += sizeof(rfc1042_header) + 2;
    }

    return len;
}

static inline bool ecrnx_amsdu_is_aggregable(struct sk_buff *skb)
{
    /* need to add some check on buffer to see if it can be aggregated ? */
    return true;
}


/**
 * ecrnx_amsdu_del_subframe_header - remove AMSDU header
 *
 * amsdu_txhdr: amsdu tx descriptor
 *
 * Move back the ethernet header at the "beginning" of the data buffer.
 * (which has been moved in @ecrnx_amsdu_add_subframe_header)
 */
static void ecrnx_amsdu_del_subframe_header(struct ecrnx_amsdu_txhdr *amsdu_txhdr)
{
    struct sk_buff *skb = amsdu_txhdr->skb;
    struct ethhdr *eth;
    u8 *pos;

    pos = skb->data;
    pos += sizeof(struct ecrnx_amsdu_txhdr);
    eth = (struct ethhdr*)pos;
    pos += amsdu_txhdr->pad + sizeof(struct ethhdr);

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        pos += sizeof(rfc1042_header) + 2;
    }

    memmove(pos, eth, sizeof(*eth));
    skb_pull(skb, (pos - skb->data));
}

/**
 * ecrnx_amsdu_add_subframe_header - Add AMSDU header and link subframe
 *
 * @ecrnx_hw Driver main data
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
static int ecrnx_amsdu_add_subframe_header(struct ecrnx_hw *ecrnx_hw,
                                          struct sk_buff *skb,
                                          struct ecrnx_sw_txhdr *sw_txhdr)
{
    struct ecrnx_amsdu *amsdu = &sw_txhdr->amsdu;
    struct ecrnx_amsdu_txhdr *amsdu_txhdr;
    struct ethhdr *amsdu_hdr, *eth = (struct ethhdr *)skb->data;
    int headroom_need, map_len, msdu_len, amsdu_len, map_oft = 0;
#ifndef CONFIG_ECRNX_ESWIN
    dma_addr_t dma_addr;
#endif
    u8 *pos, *map_start;

    map_len = ECRNX_TX_DMA_MAP_LEN(skb);
    msdu_len = skb->len - sizeof(*eth);
    headroom_need = sizeof(*amsdu_txhdr) + amsdu->pad +
        sizeof(*amsdu_hdr);
    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        headroom_need += sizeof(rfc1042_header) + 2;
        msdu_len += sizeof(rfc1042_header) + 2;
    }
    amsdu_len = msdu_len + sizeof(*amsdu_hdr) + amsdu->pad;

    /* we should have enough headroom (checked in xmit) */
    if (WARN_ON(skb_headroom(skb) < headroom_need)) {
        return -1;
    }

    /* allocate headroom */
    pos = skb_push(skb, headroom_need);
    amsdu_txhdr = (struct ecrnx_amsdu_txhdr *)pos;
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
        pos += sizeof(rfc1042_header) + 2;
    }

    if (amsdu_len < map_len) {
        map_oft = map_len - amsdu_len;
        map_start -= map_oft;
    }
    /* MAP (and sync) memory for DMA */
#ifndef CONFIG_ECRNX_ESWIN  
    dma_addr = dma_map_single(ecrnx_hw->dev, map_start, map_len, DMA_BIDIRECTIONAL);
    if (WARN_ON(dma_mapping_error(ecrnx_hw->dev, dma_addr))) {
        pos -= sizeof(*eth);
        memmove(pos, eth, sizeof(*eth));
        skb_pull(skb, headroom_need);
        return -1;
    }
#endif

    /* update amdsu_txhdr */
    amsdu_txhdr->map_len = map_len;
#ifdef CONFIG_ECRNX_ESWIN
    amsdu_txhdr->send_pos = map_start;
#else
    amsdu_txhdr->dma_addr = dma_addr;
#endif
    amsdu_txhdr->skb = skb;
    amsdu_txhdr->pad = amsdu->pad;
    amsdu_txhdr->msdu_len = msdu_len;

    /* update ecrnx_sw_txhdr (of the first subframe) */
    BUG_ON(amsdu->nb != sw_txhdr->desc.host.packet_cnt);
#ifdef CONFIG_ECRNX_ESWIN
    sw_txhdr->desc.host.packet_addr[amsdu->nb] = skb;
#else
    sw_txhdr->desc.host.packet_addr[amsdu->nb] = dma_addr + map_oft;
#endif
    sw_txhdr->desc.host.packet_len[amsdu->nb] = amsdu_len;
    sw_txhdr->desc.host.packet_cnt++;
    amsdu->nb++;

    amsdu->pad = AMSDU_PADDING(amsdu_len - amsdu->pad);
    list_add_tail(&amsdu_txhdr->list, &amsdu->hdrs);
    amsdu->len += amsdu_len;

    ecrnx_ipc_sta_buffer(ecrnx_hw, sw_txhdr->txq->sta,
                        sw_txhdr->txq->tid, msdu_len);

    trace_amsdu_subframe(sw_txhdr);
    return 0;
}

/**
 * ecrnx_amsdu_add_subframe - Add this buffer as an A-MSDU subframe if possible
 *
 * @ecrnx_hw Driver main data
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
static bool ecrnx_amsdu_add_subframe(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb,
                                    struct ecrnx_sta *sta, struct ecrnx_txq *txq)
{
    bool res = false;
    struct ethhdr *eth;
    ecrnx_adjust_amsdu_maxnb(ecrnx_hw);

    /* immediately return if amsdu are not allowed for this sta */
    if (!txq->amsdu_len || ecrnx_hw->mod_params->amsdu_maxnb < 2 ||
        !ecrnx_amsdu_is_aggregable(skb)
       )
        return false;

    spin_lock_bh(&ecrnx_hw->tx_lock);
    if (txq->amsdu) {
        /* aggreagation already in progress, add this buffer if enough space
           available, otherwise end the current amsdu */
        struct ecrnx_sw_txhdr *sw_txhdr = txq->amsdu;
        eth = (struct ethhdr *)(skb->data);

        if (((sw_txhdr->amsdu.len + sw_txhdr->amsdu.pad +
              ecrnx_amsdu_subframe_length(eth, skb->len)) > txq->amsdu_len) ||
            ecrnx_amsdu_add_subframe_header(ecrnx_hw, skb, sw_txhdr)) {
            txq->amsdu = NULL;
            goto end;
        }

        if (sw_txhdr->amsdu.nb >= ecrnx_hw->mod_params->amsdu_maxnb) {
            ecrnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
            /* max number of subframes reached */
            txq->amsdu = NULL;
        }
    } else {
        /* Check if a new amsdu can be started with the previous buffer
           (if any) and this one */
        struct sk_buff *skb_prev = skb_peek_tail(&txq->sk_list);
        struct ecrnx_txhdr *txhdr;
        struct ecrnx_sw_txhdr *sw_txhdr;
        int len1, len2;

        if (!skb_prev || !ecrnx_amsdu_is_aggregable(skb_prev))
            goto end;

        txhdr = (struct ecrnx_txhdr *)skb_prev->data;
        sw_txhdr = txhdr->sw_hdr;
        if ((sw_txhdr->amsdu.len) ||
            (sw_txhdr->desc.host.flags & TXU_CNTRL_RETRY))
            /* previous buffer is already a complete amsdu or a retry */
            goto end;

        eth = (struct ethhdr *)(skb_prev->data + sw_txhdr->headroom);
        len1 = ecrnx_amsdu_subframe_length(eth, (sw_txhdr->frame_len +
                                                sizeof(struct ethhdr)));

        eth = (struct ethhdr *)(skb->data);
        len2 = ecrnx_amsdu_subframe_length(eth, skb->len);

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
        if (ecrnx_amsdu_add_subframe_header(ecrnx_hw, skb, sw_txhdr))
            goto end;

        sw_txhdr->desc.host.flags |= TXU_CNTRL_AMSDU;

        if (sw_txhdr->amsdu.nb < ecrnx_hw->mod_params->amsdu_maxnb)
            txq->amsdu = sw_txhdr;
        else
            ecrnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
    }

    res = true;

  end:
    spin_unlock_bh(&ecrnx_hw->tx_lock);
    return res;
}
/**
 * ecrnx_amsdu_dismantle - Dismantle an already formatted A-MSDU
 *
 * @ecrnx_hw Driver main data
 * @sw_txhdr_main Software descriptor of the A-MSDU to dismantle.
 *
 * The a-mdsu is always fully dismantled (i.e don't try to reduce it's size to
 * fit the new limit).
 * The DMA mapping can be re-used as ecrnx_amsdu_add_subframe_header ensure that
 * enough data in the skb bufer are 'DMA mapped'.
 * It would have been slightly simple to unmap/re-map but it is a little faster like this
 * and not that much more complicated to read.
 */
static void ecrnx_amsdu_dismantle(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sw_txhdr *sw_txhdr_main)
{
    struct ecrnx_amsdu_txhdr *amsdu_txhdr, *next;
    struct sk_buff *skb_prev = sw_txhdr_main->skb;
    struct ecrnx_txq *txq =  sw_txhdr_main->txq;
    trace_amsdu_dismantle(sw_txhdr_main);
    ecrnx_hw->stats.amsdus[sw_txhdr_main->amsdu.nb - 1].done--;
    sw_txhdr_main->amsdu.len = 0;
    sw_txhdr_main->amsdu.nb = 0;
    sw_txhdr_main->desc.host.flags &= ~TXU_CNTRL_AMSDU;
    sw_txhdr_main->desc.host.packet_cnt = 1;
    list_for_each_entry_safe(amsdu_txhdr, next, &sw_txhdr_main->amsdu.hdrs, list) {
        struct ecrnx_txhdr *txhdr;
        struct ecrnx_sw_txhdr *sw_txhdr;
        dma_addr_t dma_addr = amsdu_txhdr->dma_addr;
        size_t map_len = amsdu_txhdr->map_len;
        size_t tx_map_len;
        size_t data_oft, cfm_oft = 0;
        struct sk_buff *skb = amsdu_txhdr->skb;
        int headroom;
        list_del(&amsdu_txhdr->list);
        ecrnx_ipc_sta_buffer(ecrnx_hw, txq->sta, txq->tid, -amsdu_txhdr->msdu_len);
        ecrnx_amsdu_del_subframe_header(amsdu_txhdr);
        headroom = ECRNX_TX_HEADROOM(skb);
        tx_map_len = ECRNX_TX_DMA_MAP_LEN(skb);
        sw_txhdr = kmem_cache_alloc(ecrnx_hw->sw_txhdr_cache, GFP_ATOMIC);
        if (unlikely((skb_headroom(skb) < headroom) ||
                     (sw_txhdr == NULL) || (tx_map_len > map_len))) {
            if (sw_txhdr)
                kmem_cache_free(ecrnx_hw->sw_txhdr_cache, sw_txhdr);
            dma_unmap_single(ecrnx_hw->dev, dma_addr, map_len, DMA_TO_DEVICE);
            dev_kfree_skb_any(skb);
            continue;
        }
        sw_txhdr->headroom = headroom;
        cfm_oft = map_len - tx_map_len;
        data_oft = sizeof(struct ethhdr) + ECRNX_TX_DATA_OFT(sw_txhdr) + cfm_oft;
        txhdr = skb_push(skb, headroom);
        txhdr->sw_hdr = sw_txhdr;
        memcpy(sw_txhdr, sw_txhdr_main, sizeof(*sw_txhdr));
        sw_txhdr->frame_len = map_len - data_oft;
        sw_txhdr->skb = skb;
        sw_txhdr->headroom = headroom;
        txhdr->hw_hdr.cfm.status.value = 0;
        sw_txhdr->map_len = map_len;
        sw_txhdr->dma_addr = dma_addr;
        sw_txhdr->desc.host.packet_addr[0] = dma_addr + data_oft;
        sw_txhdr->desc.host.status_desc_addr = dma_addr + cfm_oft;
        sw_txhdr->desc.host.packet_len[0] = sw_txhdr->frame_len;
        sw_txhdr->desc.host.packet_cnt = 1;
        ecrnx_txq_queue_skb(skb, sw_txhdr->txq, ecrnx_hw, false, skb_prev);
        skb_prev = skb;
    }
}
/**
 * ecrnx_amsdu_update_len - Update length allowed for A-MSDU on a TXQ
 *
 * @ecrnx_hw Driver main data.
 * @txq The TXQ.
 * @amsdu_len New length allowed ofr A-MSDU.
 *
 * If this is a TXQ linked to a STA and the allowed A-MSDU size is reduced it is
 * then necessary to disassemble all A-MSDU currently queued on all STA' txq that
 * are larger than this new limit.
 * Does nothing if the A-MSDU limit increase or stay the same.
 */
static void ecrnx_amsdu_update_len(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txq *txq,
                                  u16 amsdu_len)
{
    struct ecrnx_sta *sta = txq->sta;
    int tid;

    if (amsdu_len != txq->amsdu_len)
        trace_amsdu_len_update(txq->sta, amsdu_len);

    if (amsdu_len >= txq->amsdu_len) {
        txq->amsdu_len = amsdu_len;
        return;
    }

    if (!sta) {
        netdev_err(txq->ndev, "Non STA txq(%d) with a-amsdu len %d\n",
                   txq->idx, amsdu_len);
        txq->amsdu_len = 0;
        return;
    }

    /* A-MSDU size has been reduced by the firmware, need to dismantle all
       queued a-msdu that are too large. Need to do this for all txq of the STA. */
    foreach_sta_txq(sta, txq, tid, ecrnx_hw) {
        struct sk_buff *skb, *skb_next;

        if (txq->amsdu_len <= amsdu_len)
            continue;

        if (txq->last_retry_skb)
            skb = txq->last_retry_skb->next;
        else
            skb = txq->sk_list.next;

        skb_queue_walk_from_safe(&txq->sk_list, skb, skb_next) {
            struct ecrnx_txhdr *txhdr = (struct ecrnx_txhdr *)skb->data;
            struct ecrnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
            if ((sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU) &&
                (sw_txhdr->amsdu.len > amsdu_len))
                ecrnx_amsdu_dismantle(ecrnx_hw, sw_txhdr);

            if (txq->amsdu == sw_txhdr)
                txq->amsdu = NULL;
        }

        txq->amsdu_len = amsdu_len;
    }
}
#endif /* CONFIG_ECRNX_AMSDUS_TX */

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
netdev_tx_t ecrnx_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;
    struct ecrnx_txhdr *txhdr;
    struct ecrnx_sw_txhdr *sw_txhdr = NULL;
    struct ethhdr *eth;
    struct txdesc_api *desc;
    struct ecrnx_sta *sta;
    struct ecrnx_txq *txq;
    int headroom = 0, hdr_pads = 0;
	u16 frame_len;
    u64_l skb_addr;

    u8 tid;

    sk_pacing_shift_update(skb->sk, ecrnx_hw->tcp_pacing_shift);

    /* check whether the current skb can be used */
    if (skb_shared(skb) || (skb_headroom(skb) < ECRNX_TX_MAX_HEADROOM) ||
        (skb_cloned(skb) && (dev->priv_flags & IFF_BRIDGE_PORT))) {
        struct sk_buff *newskb = skb_copy_expand(skb, ECRNX_TX_MAX_HEADROOM, 0, GFP_ATOMIC);
        if (unlikely(newskb == NULL))
            goto free;

        dev_kfree_skb_any(skb);

        skb = newskb;
    }

    /* Get the STA id and TID information */
    sta = ecrnx_get_tx_info(ecrnx_vif, skb, &tid);
    if (!sta)
        goto free;

    txq = ecrnx_txq_sta_get(sta, tid, ecrnx_hw);
    ECRNX_DBG("%s-%d:sta:0x%p,sta_idx:%d, sta_mac:%pM, tid:%d, ecrnx_hw:0x%p, txq:0x%p \n", __func__, __LINE__, sta, sta->sta_idx, sta->mac_addr, tid, ecrnx_hw, txq);
    if (txq->idx == TXQ_INACTIVE)
        goto free;

#ifdef CONFIG_ECRNX_AMSDUS_TX
    if (ecrnx_amsdu_add_subframe(ecrnx_hw, skb, sta, txq))
        return NETDEV_TX_OK;
#endif

    sw_txhdr = kmem_cache_alloc(ecrnx_hw->sw_txhdr_cache, GFP_ATOMIC);
    if (unlikely(sw_txhdr == NULL))
        goto free;

    /* Retrieve the pointer to the Ethernet data */
    eth = (struct ethhdr *)skb->data;

	
#if 0
    if ((skb->data[0] & 0x1) && (skb->data[0] != 0xff)) {
        printk("drop mc pkt 0x%x\n", skb->data[0]);
        goto free;
    }
#endif

#if 1
    if (0xDD86 == eth->h_proto) { //ipv6
		//printk("%s-%d: eapol\n", __func__, __LINE__);
		goto free;
		//dump_xxx_buf(skb->data, skb->len);
	}
#endif
#if 0
    if (0x8e88 == eth->h_proto) { //icmp
		printk("%s-%d: eapol\n", __func__, __LINE__);
		//dump_xxx_buf(skb->data, skb->len);
	}
#endif

#if 0

	if (8 == eth->h_proto && 0x1 == skb->data[23]) { //icmp
		memset(skb->data + 14, 0xff, skb->len - 14);
	}
	if (8 == eth->h_proto && 0x11 == skb->data[23]) {
		printk("---drop udp pkt\n");
		goto free;
	}
#endif	
    //no_encrypt = check_eapol_dont_encrypt(skb);

    hdr_pads  = ECRNX_SWTXHDR_ALIGN_PADS((long)eth);
    /* Use headroom to store struct ecrnx_txhdr */
    headroom = ECRNX_TX_HEADROOM(skb);

    txhdr = (struct ecrnx_txhdr *)skb_push(skb, headroom);
    txhdr->sw_hdr = sw_txhdr;
    frame_len = (u16)skb->len - headroom - sizeof(*eth);

    sw_txhdr->txq       = txq;
    sw_txhdr->frame_len = frame_len;
    sw_txhdr->ecrnx_sta  = sta;
    sw_txhdr->ecrnx_vif  = ecrnx_vif;
    sw_txhdr->skb       = skb;
    sw_txhdr->headroom  = headroom;
#ifdef CONFIG_ECRNX_ESWIN
    sw_txhdr->offset = headroom + sizeof(*eth);
#else
    sw_txhdr->map_len   = skb->len - offsetof(struct ecrnx_txhdr, hw_hdr);
#endif
    sw_txhdr->jiffies   = jiffies;
#ifdef CONFIG_ECRNX_AMSDUS_TX
    sw_txhdr->amsdu.len = 0;
    sw_txhdr->amsdu.nb = 0;
#endif
    // Fill-in the descriptor
    desc = &sw_txhdr->desc;
    memcpy(&desc->host.eth_dest_addr, eth->h_dest, ETH_ALEN);
    memcpy(&desc->host.eth_src_addr, eth->h_source, ETH_ALEN);
    desc->host.ethertype = eth->h_proto;
    desc->host.staid = sta->sta_idx;
    desc->host.tid = tid;
    if (unlikely(ecrnx_vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN))
        desc->host.vif_idx = ecrnx_vif->ap_vlan.master->vif_index;
    else
        desc->host.vif_idx = ecrnx_vif->vif_index;
    desc->host.flags = 0;

    if (ecrnx_vif->use_4addr && (sta->sta_idx < NX_REMOTE_STA_MAX))
        desc->host.flags |= TXU_CNTRL_USE_4ADDR;

    if ((ecrnx_vif->tdls_status == TDLS_LINK_ACTIVE) &&
        ecrnx_vif->sta.tdls_sta &&
        (memcmp(desc->host.eth_dest_addr.array, ecrnx_vif->sta.tdls_sta->mac_addr, ETH_ALEN) == 0)) {
        desc->host.flags |= TXU_CNTRL_TDLS;
        ecrnx_vif->sta.tdls_sta->tdls.last_tid = desc->host.tid;
        ecrnx_vif->sta.tdls_sta->tdls.last_sn = desc->host.sn;
    }

    if ((ecrnx_vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) &&
        (ecrnx_vif->is_resending))
            desc->host.flags |= TXU_CNTRL_MESH_FWD;

#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    desc->host.packet_len[0] = frame_len;
#else
    desc->host.packet_len = frame_len;
#endif

    txhdr->hw_hdr.cfm.status.value = 0;

#ifdef CONFIG_ECRNX_ESWIN
    skb_addr = (ptr_addr)skb;
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    desc->host.packet_addr[0] = (u64_l)skb;
    desc->host.packet_cnt = 1;
#else
    //desc->host.packet_addr = (u64_l)skb;
    desc->host.packet_addr[0] = (u32_l)skb_addr;
    desc->host.packet_addr[1] = (u32_l)(skb_addr >> 32);
#endif
    //desc->host.status_desc_addr = (u64_l)skb;
    desc->host.status_desc_addr[0] = (u32_l)skb_addr;
    desc->host.status_desc_addr[1] = (u32_l)(skb_addr >> 32);

    /*if (no_encrypt) {
        desc->host.flags |= TXU_CNTRL_NO_ENCRYPT;
    }*/
#else //CONFIG_ECRNX_ESWIN_SDIO
    if (unlikely(ecrnx_prep_dma_tx(ecrnx_hw, txhdr, true)))
        goto free;
#endif //CONFIG_ECRNX_ESWIN_SDIO
    //ECRNX_DBG("%s:desc:0x%08x, vif_idx:%d, skb:0x%08x, headroom:%d !!! \n", __func__, desc, desc->host.vif_idx, skb, headroom);
    spin_lock_bh(&ecrnx_hw->tx_lock);
    if (ecrnx_txq_queue_skb(skb, txq, ecrnx_hw, false, NULL))
    {
        ECRNX_DBG("%s-%d:txdesc:0x%x, skb:0x%08x, skb->len:%d \n", __func__, __LINE__, desc, skb, skb->len);
        ecrnx_hwq_process(ecrnx_hw, txq->hwq);
    }
    else
    {
        ECRNX_DBG("%s-%d: delay send(put txq), txq:0x%p, queue status 0x%x, skb:0x%08x, skb->len:%d !!! \n", __func__, __LINE__, txq, txq->status, skb, skb->len);
    }
    spin_unlock_bh(&ecrnx_hw->tx_lock);

    return NETDEV_TX_OK;

free:
    if (sw_txhdr)
        kmem_cache_free(ecrnx_hw->sw_txhdr_cache, sw_txhdr);
    if (headroom)
        skb_pull(skb, headroom);
    dev_kfree_skb_any(skb);

    return NETDEV_TX_OK;
}

/**
 * ecrnx_start_mgmt_xmit - Transmit a management frame
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
int ecrnx_start_mgmt_xmit(struct ecrnx_vif *vif, struct ecrnx_sta *sta,
                         struct cfg80211_mgmt_tx_params *params, bool offchan,
                         u64 *cookie)
#else
int ecrnx_start_mgmt_xmit(struct ecrnx_vif *vif, struct ecrnx_sta *sta,
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
    struct ecrnx_hw *ecrnx_hw = vif->ecrnx_hw;
    struct ecrnx_txhdr *txhdr;
    struct ecrnx_sw_txhdr *sw_txhdr;
    struct txdesc_api *desc;
    struct sk_buff *skb;
    u16 frame_len, headroom;
    u8 *data;
    struct ecrnx_txq *txq;
    bool robust;
    u64_l skb_addr;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    const u8 *buf = params->buf;
    size_t len = params->len;
    bool no_cck = params->no_cck;
#endif


#ifdef CONFIG_ECRNX_ESWIN
    headroom = sizeof(struct ecrnx_txhdr) + ECRNX_TX_TXDESC_API_ALIGN;
#else
    headroom = sizeof(struct ecrnx_txhdr);
#endif
    frame_len = len;

    //----------------------------------------------------------------------

    /* Set TID and Queues indexes */
    if (sta) {
        txq = ecrnx_txq_sta_get(sta, 8, ecrnx_hw);
    } else {
        if (offchan)
            txq = &ecrnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
        else
            txq = ecrnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
    }

    /* Ensure that TXQ is active */
    if (txq->idx == TXQ_INACTIVE) {
        if(sta){
            netdev_dbg(vif->ndev, "TXQ inactive\n");
            return -EBUSY;
        }else{
            return 0;
        }
    }

    /*
     * Create a SK Buff object that will contain the provided data
     */
    skb = dev_alloc_skb(headroom + frame_len);

    if (!skb) {
        return -ENOMEM;
    }

    *cookie = (unsigned long)skb;

    sw_txhdr = kmem_cache_alloc(ecrnx_hw->sw_txhdr_cache, GFP_ATOMIC);
    if (unlikely(sw_txhdr == NULL)) {
        dev_kfree_skb(skb);
        return -ENOMEM;
    }
    /*
     * Move skb->data pointer in order to reserve room for ecrnx_txhdr
     * headroom value will be equal to sizeof(struct ecrnx_txhdr)
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
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */

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
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0) */

    /*
     * Go back to the beginning of the allocated data area
     * skb->data pointer will move backward
     */
    txhdr = (struct ecrnx_txhdr *)skb_push(skb, headroom);

    //----------------------------------------------------------------------

    /* Fill the TX Header */


    //----------------------------------------------------------------------

    /* Fill the SW TX Header */
    txhdr->sw_hdr = sw_txhdr;

    sw_txhdr->txq = txq;
    sw_txhdr->frame_len = frame_len;
    sw_txhdr->ecrnx_sta = sta;
    sw_txhdr->ecrnx_vif = vif;
    sw_txhdr->skb = skb;
    sw_txhdr->headroom = headroom;
#ifdef CONFIG_ECRNX_ESWIN
    sw_txhdr->offset = headroom; //sizeof(struct ecrnx_txhdr) + sizeof(struct txdesc_api);
#else
    sw_txhdr->map_len = skb->len - offsetof(struct ecrnx_txhdr, hw_hdr);
#endif
	sw_txhdr->jiffies = jiffies;
#ifdef CONFIG_ECRNX_AMSDUS_TX
    sw_txhdr->amsdu.len = 0;
    sw_txhdr->amsdu.nb = 0;
#endif
    //----------------------------------------------------------------------

    /* Fill the Descriptor to be provided to the MAC SW */
    desc = &sw_txhdr->desc;

    desc->host.staid = (sta) ? sta->sta_idx : 0xFF;
    desc->host.vif_idx = vif->vif_index;
    desc->host.tid = 0xFF;
    desc->host.flags = TXU_CNTRL_MGMT;
    if (robust)
        desc->host.flags |= TXU_CNTRL_MGMT_ROBUST;


    if (no_cck) {
        desc->host.flags |= TXU_CNTRL_MGMT_NO_CCK;
    }
    /* baoyong */
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
    desc->host.packet_len[0] = frame_len;
#else
    desc->host.packet_len = frame_len;
#endif

    txhdr->hw_hdr.cfm.status.value = 0;
#ifdef CONFIG_ECRNX_ESWIN
    skb_addr = (ptr_addr)skb;
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    desc->host.packet_addr[0] = skb;
    desc->host.packet_cnt = 1;
#else
    //desc->host.packet_addr = (u64_l)skb;
    desc->host.packet_addr[0] = (u32_l)skb_addr;
    desc->host.packet_addr[1] = (u32_l)(skb_addr >> 32);
#endif
    //desc->host.status_desc_addr = (u64_l)skb;
    desc->host.packet_addr[0] = (u32_l)skb_addr;
    desc->host.packet_addr[1] = (u32_l)(skb_addr >> 32);
#else //CONFIG_ECRNX_ESWIN_SDIO

    /* Get DMA Address */
    if (unlikely(ecrnx_prep_dma_tx(ecrnx_hw, txhdr, false))) {
        kmem_cache_free(ecrnx_hw->sw_txhdr_cache, sw_txhdr);
        dev_kfree_skb(skb);
        return -EBUSY;
    }
#endif
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
#endif

    //----------------------------------------------------------------------

    spin_lock_bh(&ecrnx_hw->tx_lock);
    if (ecrnx_txq_queue_skb(skb, txq, ecrnx_hw, false, NULL)) {
        ECRNX_DBG("%s-%d:txdesc:0x%x, skb:0x%08x, skb->len:%d \n", __func__, __LINE__, desc, skb, skb->len);
        ecrnx_hwq_process(ecrnx_hw, txq->hwq);
    } else {
        ECRNX_DBG("%s-%d: delay send(put txq), queue status 0x%x, skb:0x%08x, skb->len:%d !!! \n", __func__, __LINE__, txq->status, skb, skb->len);
    }
    spin_unlock_bh(&ecrnx_hw->tx_lock);

    return 0;
}

int ecrnx_handle_tx_datacfm(void *priv, void *host_id)
{
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw *)priv;
    struct sk_buff *skb = host_id;
    struct ecrnx_hwq *hwq;
    struct ecrnx_txq *txq;
    struct ecrnx_sta *sta;
    struct ecrnx_txhdr *txhdr;

#if defined(CONFIG_ECRNX_ESWIN_USB)
        txhdr = (struct ecrnx_txhdr *)(*((ptr_addr*)skb->data - 1));
#else
        txhdr = (struct ecrnx_txhdr *)skb->data;
#endif

    struct ecrnx_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
 
    /* Check status in the header. If status is null, it means that the buffer
    * was not transmitted and we have to return immediately */
    ECRNX_DBG("%s:hostid(tx_skb):0x%08x\n", __func__, skb);

    txq = sw_txhdr->txq;
    /* don't use txq->hwq as it may have changed between push and confirm */
    hwq = &ecrnx_hw->hwq[sw_txhdr->hw_queue];
    ecrnx_txq_confirm_any(ecrnx_hw, txq, hwq, sw_txhdr);

    if (txq->idx != TXQ_INACTIVE) {

        txq->credits += 1;
        //printk("finish_cfm: txq->credits %d 0x%08x\n", txq->credits,skb);
        if (txq->credits <= 0){
            ecrnx_txq_stop(txq, ECRNX_TXQ_STOP_FULL);
        }
        else if (txq->credits > 0)
        {
            ecrnx_txq_start(txq, ECRNX_TXQ_STOP_FULL);
            /* baoyong:handle the pkts in sk_list right now */
            if (txq->idx != TXQ_INACTIVE && !skb_queue_empty(&txq->sk_list))
            {
                ecrnx_hwq_process(ecrnx_hw, txq->hwq);
            }
        }

        /* continue service period */
        if (unlikely(txq->push_limit && !ecrnx_txq_is_full(txq))) {
            ecrnx_txq_add_to_hw_list(txq);
        }
    }

    /* Update statistics */
    sw_txhdr->ecrnx_vif->net_stats.tx_packets++;
    sw_txhdr->ecrnx_vif->net_stats.tx_bytes += sw_txhdr->frame_len;

    sta = txq->sta;
    if(sta)
    {
        sta = txq->sta;
        sta->stats.tx_pkts ++;
        sta->stats.tx_bytes += sw_txhdr->frame_len;
        sta->stats.last_act = ecrnx_hw->stats.last_tx;
    }
    //printk("sta->stats.tx_pkts=%d sta->stats.tx_bytes =%d\n", sta->stats.tx_pkts, sta->stats.tx_bytes);

    kmem_cache_free(ecrnx_hw->sw_txhdr_cache, sw_txhdr);
    skb_pull(skb, sw_txhdr->headroom);
    consume_skb(skb);
    return 0;
}

/**
 * ecrnx_txdatacfm - FW callback for TX confirmation
 *
 * called with tx_lock hold
 */
int ecrnx_txdatacfm(void *pthis, void *host_id)
{
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw *)pthis;
    struct sk_buff *skb = host_id;
    struct ecrnx_txhdr *txhdr;
    union ecrnx_hw_txstatus ecrnx_txst;
    struct ecrnx_sw_txhdr *sw_txhdr;
    struct ecrnx_hwq *hwq;
    struct ecrnx_txq *txq;
#ifndef CONFIG_ECRNX_ESWIN
    dma_addr_t cfm_dma_addr;
#endif
    size_t cfm_len;

#if defined(CONFIG_ECRNX_ESWIN_USB)
    txhdr = (struct ecrnx_txhdr *)host_id;
    skb = txhdr->sw_hdr->skb;
    skb_push(skb, sizeof(struct ecrnx_txhdr) - sizeof(u32_l));
#else
    txhdr = (struct ecrnx_txhdr *)skb->data;
#endif
    sw_txhdr = txhdr->sw_hdr;
    cfm_len = sizeof(txhdr->hw_hdr.cfm);

	//ECRNX_DBG("%s-%d: skb:0x%08x, skb->len:%d \n", __func__, __LINE__, skb, skb->len);
#ifndef CONFIG_ECRNX_ESWIN
	cfm_dma_addr = (ptr_addr)sw_txhdr->desc.host.status_desc_addr;
    dma_sync_single_for_cpu(ecrnx_hw->dev, cfm_dma_addr, cfm_len, DMA_FROM_DEVICE);
#endif
    /* Read status in the TX control header */
    ecrnx_txst = txhdr->hw_hdr.cfm.status;

    /* Check status in the header. If status is null, it means that the buffer
     * was not transmitted and we have to return immediately */
    if (ecrnx_txst.value == 0) {
#ifndef CONFIG_ECRNX_ESWIN
        dma_sync_single_for_device(ecrnx_hw->dev, cfm_dma_addr, cfm_len, DMA_FROM_DEVICE);
#endif
        return -1;
    }

    txq = sw_txhdr->txq;
    /* don't use txq->hwq as it may have changed between push and confirm */
    hwq = &ecrnx_hw->hwq[sw_txhdr->hw_queue];
    ecrnx_txq_confirm_any(ecrnx_hw, txq, hwq, sw_txhdr);

    /* Update txq and HW queue credits */
    if (sw_txhdr->desc.host.flags & TXU_CNTRL_MGMT) {
        trace_mgmt_cfm(sw_txhdr->ecrnx_vif->vif_index,
                       (sw_txhdr->ecrnx_sta) ? sw_txhdr->ecrnx_sta->sta_idx : 0xFF,
                       ecrnx_txst.acknowledged);

        /* Confirm transmission to CFG80211 */
        cfg80211_mgmt_tx_status(&sw_txhdr->ecrnx_vif->wdev,
                                (unsigned long)skb,
                                (skb->data + sw_txhdr->headroom),
                                sw_txhdr->frame_len,
                                ecrnx_txst.acknowledged,
                                GFP_ATOMIC);
    } else if ((txq->idx != TXQ_INACTIVE) &&
               (ecrnx_txst.retry_required || ecrnx_txst.sw_retry_required)) {
        bool sw_retry = (ecrnx_txst.sw_retry_required) ? true : false;

        /* Reset the status */
        txhdr->hw_hdr.cfm.status.value = 0;

        /* The confirmed packet was part of an AMPDU and not acked
         * correctly, so reinject it in the TX path to be retried */
        ecrnx_tx_retry(ecrnx_hw, skb, txhdr, sw_retry);
        return 0;
    }

    trace_skb_confirm(skb, txq, hwq, &txhdr->hw_hdr.cfm);

    /* STA may have disconnect (and txq stopped) when buffers were stored
       in fw. In this case do nothing when they're returned */
    if (txq->idx != TXQ_INACTIVE) {
        if (txhdr->hw_hdr.cfm.credits) {
            txq->credits += txhdr->hw_hdr.cfm.credits;
            if (txq->credits <= 0)
                ecrnx_txq_stop(txq, ECRNX_TXQ_STOP_FULL);
            else if (txq->credits > 0)
            {
                ecrnx_txq_start(txq, ECRNX_TXQ_STOP_FULL);
                /* baoyong:handle the pkts in sk_list right now */
                if (txq->idx != TXQ_INACTIVE && !skb_queue_empty(&txq->sk_list))
                {
                    ecrnx_hwq_process(ecrnx_hw, txq->hwq);
                }
            
            }
        }

        /* continue service period */
        if (unlikely(txq->push_limit && !ecrnx_txq_is_full(txq))) {
            ecrnx_txq_add_to_hw_list(txq);
        }
    }

    if (txhdr->hw_hdr.cfm.ampdu_size &&
        txhdr->hw_hdr.cfm.ampdu_size < IEEE80211_MAX_AMPDU_BUF)
        ecrnx_hw->stats.ampdus_tx[txhdr->hw_hdr.cfm.ampdu_size - 1]++;

#ifdef CONFIG_ECRNX_AMSDUS_TX
    ecrnx_amsdu_update_len(ecrnx_hw, txq, txhdr->hw_hdr.cfm.amsdu_size);
#endif

    /* Update statistics */
    sw_txhdr->ecrnx_vif->net_stats.tx_packets++;
    sw_txhdr->ecrnx_vif->net_stats.tx_bytes += sw_txhdr->frame_len;

    /* Release SKBs */
#ifdef CONFIG_ECRNX_AMSDUS_TX
    if (sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU) {
        struct ecrnx_amsdu_txhdr *amsdu_txhdr;
        list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
            ecrnx_amsdu_del_subframe_header(amsdu_txhdr);
#ifndef CONFIG_ECRNX_ESWIN
            dma_unmap_single(ecrnx_hw->dev, amsdu_txhdr->dma_addr,
                             amsdu_txhdr->map_len, DMA_TO_DEVICE);
#endif
             ecrnx_ipc_sta_buffer(ecrnx_hw, txq->sta, txq->tid,
                                 -amsdu_txhdr->msdu_len);
            ecrnx_tx_statistic(ecrnx_hw, txq, ecrnx_txst, amsdu_txhdr->msdu_len);
            consume_skb(amsdu_txhdr->skb);
        }
    }
#endif /* CONFIG_ECRNX_AMSDUS_TX */

#ifndef CONFIG_ECRNX_ESWIN
    /* unmap with the least costly DMA_TO_DEVICE since we don't need to inval */
    dma_unmap_single(ecrnx_hw->dev, sw_txhdr->dma_addr, sw_txhdr->map_len,
                     DMA_TO_DEVICE);
#endif
    ecrnx_ipc_sta_buffer(ecrnx_hw, txq->sta, txq->tid, -sw_txhdr->frame_len);
    ecrnx_tx_statistic(ecrnx_hw, txq, ecrnx_txst, sw_txhdr->frame_len);

    kmem_cache_free(ecrnx_hw->sw_txhdr_cache, sw_txhdr);
    skb_pull(skb, sw_txhdr->headroom);
    consume_skb(skb);

    return 0;
}

/**
 * ecrnx_txq_credit_update - Update credit for one txq
 *
 * @ecrnx_hw: Driver main data
 * @sta_idx: STA idx
 * @tid: TID
 * @update: offset to apply in txq credits
 *
 * Called when fw send ME_TX_CREDITS_UPDATE_IND message.
 * Apply @update to txq credits, and stop/start the txq if needed
 */
void ecrnx_txq_credit_update(struct ecrnx_hw *ecrnx_hw, int sta_idx, u8 tid, s8 update)
{
#ifndef CONFIG_ECRNX_ESWIN
    struct ecrnx_sta *sta = &ecrnx_hw->sta_table[sta_idx];
    struct ecrnx_txq *txq;

    txq = ecrnx_txq_sta_get(sta, tid, ecrnx_hw);

    spin_lock_bh(&ecrnx_hw->tx_lock);

    if (txq->idx != TXQ_INACTIVE) {
        txq->credits += update;
        trace_credit_update(txq, update);
        if (txq->credits <= 0){
            ECRNX_DBG("%s-%d:ecrnx_txq_stop,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_FULL);
            ecrnx_txq_stop(txq, ECRNX_TXQ_STOP_FULL);
        }
        else{
            ecrnx_txq_start(txq, ECRNX_TXQ_STOP_FULL);
            ECRNX_DBG("%s-%d:ecrnx_txq_start,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_FULL);
        }
    }

// Drop all the retry packets of a BA that was deleted
    if (update < NX_TXQ_INITIAL_CREDITS) {
        int packet;

        for (packet = 0; packet < txq->nb_retry; packet++) {
            ecrnx_txq_drop_skb(txq, skb_peek(&txq->sk_list), ecrnx_hw, true);
        }
    }

    spin_unlock_bh(&ecrnx_hw->tx_lock);
#endif
}


#ifdef CONFIG_ECRNX_ESWIN_SDIO
void ecrnx_tx_retry_sdio(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb,
                           struct ecrnx_txhdr *txhdr, bool sw_retry)
{
    ecrnx_tx_retry(ecrnx_hw, skb, txhdr, sw_retry);
}


#ifdef CONFIG_ECRNX_AMSDUS_TX
void ecrnx_amsdu_del_subframe_header_sdio(struct ecrnx_amsdu_txhdr *amsdu_txhdr)
{
    ecrnx_amsdu_del_subframe_header(amsdu_txhdr);
}
#endif
#endif

#ifdef CONFIG_ECRNX_ESWIN_USB
void ecrnx_tx_retry_usb(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb,
                           struct ecrnx_txhdr *txhdr, bool sw_retry)
{
    ecrnx_tx_retry(ecrnx_hw, skb, txhdr, sw_retry);
}


#ifdef CONFIG_ECRNX_AMSDUS_TX
void ecrnx_amsdu_del_subframe_header_sdio(struct ecrnx_amsdu_txhdr *amsdu_txhdr)
{
    ecrnx_amsdu_del_subframe_header(amsdu_txhdr);
}
#endif
#endif

