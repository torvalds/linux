/**
 ******************************************************************************
 *
 * @file ecrnx_txq.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include "ecrnx_defs.h"
#include "ecrnx_tx.h"
#include "ipc_host.h"
#include "ecrnx_events.h"

/******************************************************************************
 * Utils functions
 *****************************************************************************/
#ifdef CONFIG_ECRNX_SOFTMAC
const int nx_tid_prio[NX_NB_TID_PER_STA] = {15, 7, 14, 6, 13, 5, 12, 4,
                                            11, 3, 8, 0, 10, 2, 9, 1};

static inline int ecrnx_txq_sta_idx(struct ecrnx_sta *sta, u8 tid)
{
    return sta->sta_idx * NX_NB_TXQ_PER_STA;
}

static inline int ecrnx_txq_vif_idx(struct ecrnx_vif *vif, u8 ac)
{
    return vif->vif_index * NX_NB_TXQ_PER_VIF + ac + NX_FIRST_VIF_TXQ_IDX;
}

#ifdef CONFIG_MAC80211_TXQ
struct ecrnx_txq *ecrnx_txq_sta_get(struct ecrnx_sta *ecrnx_sta, u8 tid)
{
    struct ieee80211_sta *sta = ecrnx_to_ieee80211_sta(ecrnx_sta);
    struct ieee80211_txq *mac_txq = sta->txq[tid];

    return (struct ecrnx_txq *)mac_txq->drv_priv;
}

struct ecrnx_txq *ecrnx_txq_vif_get(struct ecrnx_vif *ecrnx_vif, u8 ac)
{
    struct ieee80211_vif *vif = ecrnx_to_ieee80211_vif(ecrnx_vif);

    /* mac80211 only allocate one txq per vif for Best Effort */
    if (ac == ECRNX_HWQ_BE) {
        struct ieee80211_txq *mac_txq = vif->txq;
        if (!mac_txq)
            return NULL;
        return (struct ecrnx_txq *)mac_txq->drv_priv;
    }

    if (ac > NX_TXQ_CNT)
        ac = ECRNX_HWQ_BK;

    return &ecrnx_vif->txqs[ac];
}

#else /* ! CONFIG_MAC80211_TXQ */
struct ecrnx_txq *ecrnx_txq_sta_get(struct ecrnx_sta *sta, u8 tid)
{
    if (tid >= NX_NB_TXQ_PER_STA)
        tid = 0;

    return &sta->txqs[tid];
}

struct ecrnx_txq *ecrnx_txq_vif_get(struct ecrnx_vif *vif, u8 ac)
{
    if (ac > NX_TXQ_CNT)
        ac = ECRNX_HWQ_BK;

    return &vif->txqs[ac];
}

#endif /* CONFIG_MAC80211_TXQ */

static inline struct ecrnx_sta *ecrnx_txq_2_sta(struct ecrnx_txq *txq)
{
    if (txq->sta)
        return (struct ecrnx_sta *)txq->sta->drv_priv;
    return NULL;
}

#else /* CONFIG_ECRNX_FULLMAC */
const int nx_tid_prio[NX_NB_TID_PER_STA] = {7, 6, 5, 4, 3, 0, 2, 1};

static inline int ecrnx_txq_sta_idx(struct ecrnx_sta *sta, u8 tid)
{
    if (is_multicast_sta(sta->sta_idx))
        return NX_FIRST_VIF_TXQ_IDX + sta->vif_idx;
    else
        return (sta->sta_idx * NX_NB_TXQ_PER_STA) + tid;
}

static inline int ecrnx_txq_vif_idx(struct ecrnx_vif *vif, u8 type)
{
    return NX_FIRST_VIF_TXQ_IDX + master_vif_idx(vif) + (type * NX_VIRT_DEV_MAX);
}

struct ecrnx_txq *ecrnx_txq_sta_get(struct ecrnx_sta *sta, u8 tid,
                                  struct ecrnx_hw * ecrnx_hw)
{
    if (tid >= NX_NB_TXQ_PER_STA)
        tid = 0;

    return &ecrnx_hw->txq[ecrnx_txq_sta_idx(sta, tid)];
}

struct ecrnx_txq *ecrnx_txq_vif_get(struct ecrnx_vif *vif, u8 type)
{
    if (type > NX_UNK_TXQ_TYPE)
        type = NX_BCMC_TXQ_TYPE;

    return &vif->ecrnx_hw->txq[ecrnx_txq_vif_idx(vif, type)];
}

static inline struct ecrnx_sta *ecrnx_txq_2_sta(struct ecrnx_txq *txq)
{
    return txq->sta;
}

#endif /* CONFIG_ECRNX_SOFTMAC */


/******************************************************************************
 * Init/Deinit functions
 *****************************************************************************/
/**
 * ecrnx_txq_init - Initialize a TX queue
 *
 * @txq: TX queue to be initialized
 * @idx: TX queue index
 * @status: TX queue initial status
 * @hwq: Associated HW queue
 * @ndev: Net device this queue belongs to
 *        (may be null for non netdev txq)
 *
 * Each queue is initialized with the credit of @NX_TXQ_INITIAL_CREDITS.
 */
static void ecrnx_txq_init(struct ecrnx_txq *txq, int idx, u8 status,
                          struct ecrnx_hwq *hwq, int tid,
#ifdef CONFIG_ECRNX_SOFTMAC
                          struct ieee80211_sta *sta
#else
                          struct ecrnx_sta *sta, struct net_device *ndev
#endif
                          )
{
    int i;

    txq->idx = idx;
    txq->status = status;
    txq->credits = NX_TXQ_INITIAL_CREDITS;
    txq->pkt_sent = 0;
    skb_queue_head_init(&txq->sk_list);
    txq->last_retry_skb = NULL;
    txq->nb_retry = 0;
    txq->hwq = hwq;
    txq->sta = sta;
    for (i = 0; i < CONFIG_USER_MAX ; i++)
        txq->pkt_pushed[i] = 0;
    txq->push_limit = 0;
    txq->tid = tid;
#ifdef CONFIG_MAC80211_TXQ
    txq->nb_ready_mac80211 = 0;
#endif
#ifdef CONFIG_ECRNX_SOFTMAC
    txq->baw.agg_on = false;
#ifdef CONFIG_ECRNX_AMSDUS_TX
    txq->amsdu_anchor = NULL;
    txq->amsdu_ht_len_cap = 0;
    txq->amsdu_vht_len_cap = 0;
#endif /* CONFIG_ECRNX_AMSDUS_TX */

#else /* ! CONFIG_ECRNX_SOFTMAC */
    txq->ps_id = LEGACY_PS_ID;
    if (idx < NX_FIRST_VIF_TXQ_IDX) {
        int sta_idx = sta->sta_idx;
        int tid = idx - (sta_idx * NX_NB_TXQ_PER_STA);
        if (tid < NX_NB_TID_PER_STA)
            txq->ndev_idx = NX_STA_NDEV_IDX(tid, sta_idx);
        else
            txq->ndev_idx = NDEV_NO_TXQ;
    } else if (idx < NX_FIRST_UNK_TXQ_IDX) {
        txq->ndev_idx = NX_BCMC_TXQ_NDEV_IDX;
    } else {
        txq->ndev_idx = NDEV_NO_TXQ;
    }
    txq->ndev = ndev;
#ifdef CONFIG_ECRNX_AMSDUS_TX
    txq->amsdu = NULL;
    txq->amsdu_len = 0;
#endif /* CONFIG_ECRNX_AMSDUS_TX */
#endif /* CONFIG_ECRNX_SOFTMAC */
}

/**
 * ecrnx_txq_flush - Flush all buffers queued for a TXQ
 *
 * @ecrnx_hw: main driver data
 * @txq: txq to flush
 */
void ecrnx_txq_drop_skb(struct ecrnx_txq *txq, struct sk_buff *skb, struct ecrnx_hw *ecrnx_hw, bool retry_packet)
{
    struct ecrnx_sw_txhdr *sw_txhdr;
    unsigned long queued_time = 0;

    skb_unlink(skb, &txq->sk_list);

    sw_txhdr = ((struct ecrnx_txhdr *)skb->data)->sw_hdr;
    /* hwq->len doesn't count skb to retry */
#ifdef CONFIG_ECRNX_FULLMAC
    queued_time = jiffies - sw_txhdr->jiffies;
#endif /* CONFIG_ECRNX_SOFTMAC*/
    trace_txq_drop_skb(skb, txq, queued_time);

#ifdef CONFIG_ECRNX_AMSDUS_TX
        if (sw_txhdr->desc.host.packet_cnt > 1) {
            struct ecrnx_amsdu_txhdr *amsdu_txhdr;
            list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
#ifndef CONFIG_ECRNX_ESWIN             
                dma_unmap_single(ecrnx_hw->dev, amsdu_txhdr->dma_addr,
                                 amsdu_txhdr->map_len, DMA_TO_DEVICE);
#endif
                dev_kfree_skb_any(amsdu_txhdr->skb);
            }
#ifdef CONFIG_ECRNX_FULLMAC
        if (txq->amsdu == sw_txhdr)
            txq->amsdu = NULL;
#endif
        }
#endif
        kmem_cache_free(ecrnx_hw->sw_txhdr_cache, sw_txhdr);
#ifndef CONFIG_ECRNX_ESWIN
        dma_unmap_single(ecrnx_hw->dev, sw_txhdr->dma_addr, sw_txhdr->map_len,
                         DMA_TO_DEVICE);
#endif
    if (retry_packet) {
        txq->nb_retry--;
        if (txq->nb_retry == 0) {
            WARN(skb != txq->last_retry_skb,
                 "last dropped retry buffer is not the expected one");
            txq->last_retry_skb = NULL;
        }
    }
#ifdef CONFIG_ECRNX_SOFTMAC
    else
        txq->hwq->len --; // hwq->len doesn't count skb to retry
        ieee80211_free_txskb(ecrnx_hw->hw, skb);
#else
        dev_kfree_skb_any(skb);
#endif /* CONFIG_ECRNX_SOFTMAC */
}
/**
 * ecrnx_txq_flush - Flush all buffers queued for a TXQ
 *
 * @ecrnx_hw: main driver data
 * @txq: txq to flush
 */
void ecrnx_txq_flush(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txq *txq)
{
    int i, pushed = 0;

    while(!skb_queue_empty(&txq->sk_list)) {
        ecrnx_txq_drop_skb(txq, skb_peek(&txq->sk_list), ecrnx_hw, txq->nb_retry);
    }

    for (i = 0; i < CONFIG_USER_MAX; i++) {
        pushed += txq->pkt_pushed[i];
    }

    if (pushed)
        dev_warn(ecrnx_hw->dev, "TXQ[%d]: %d skb still pushed to the FW",
                 txq->idx, pushed);
}

/**
 * ecrnx_txq_deinit - De-initialize a TX queue
 *
 * @ecrnx_hw: Driver main data
 * @txq: TX queue to be de-initialized
 * Any buffer stuck in a queue will be freed.
 */
static void ecrnx_txq_deinit(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txq *txq)
{
    if (txq->idx == TXQ_INACTIVE)
        return;

    spin_lock_bh(&ecrnx_hw->tx_lock);
    ecrnx_txq_del_from_hw_list(txq);
    txq->idx = TXQ_INACTIVE;
    spin_unlock_bh(&ecrnx_hw->tx_lock);

    ecrnx_txq_flush(ecrnx_hw, txq);
}

/**
 * ecrnx_txq_vif_init - Initialize all TXQ linked to a vif
 *
 * @ecrnx_hw: main driver data
 * @ecrnx_vif: Pointer on VIF
 * @status: Intial txq status
 *
 * Softmac : 1 VIF TXQ per HWQ
 *
 * Fullmac : 1 VIF TXQ for BC/MC
 *           1 VIF TXQ for MGMT to unknown STA
 */
void ecrnx_txq_vif_init(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                       u8 status)
{
    struct ecrnx_txq *txq;
    int idx;

#ifdef CONFIG_ECRNX_SOFTMAC
    int ac;

    idx = ecrnx_txq_vif_idx(ecrnx_vif, 0);
    foreach_vif_txq(ecrnx_vif, txq, ac) {
        if (txq) {
            ecrnx_txq_init(txq, idx, status, &ecrnx_hw->hwq[ac], 0, NULL);
#ifdef CONFIG_MAC80211_TXQ
            if (ac != ECRNX_HWQ_BE)
                txq->nb_ready_mac80211 = NOT_MAC80211_TXQ;
#endif
        }
        idx++;
    }

#else
    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_BCMC_TXQ_TYPE);
    idx = ecrnx_txq_vif_idx(ecrnx_vif, NX_BCMC_TXQ_TYPE);
    ecrnx_txq_init(txq, idx, status, &ecrnx_hw->hwq[ECRNX_HWQ_BE], 0,
                  &ecrnx_hw->sta_table[ecrnx_vif->ap.bcmc_index], ecrnx_vif->ndev);

    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_UNK_TXQ_TYPE);
    idx = ecrnx_txq_vif_idx(ecrnx_vif, NX_UNK_TXQ_TYPE);
    ecrnx_txq_init(txq, idx, status, &ecrnx_hw->hwq[ECRNX_HWQ_VO], TID_MGT,
                  NULL, ecrnx_vif->ndev);

#endif /* CONFIG_ECRNX_SOFTMAC */
}

/**
 * ecrnx_txq_vif_deinit - Deinitialize all TXQ linked to a vif
 *
 * @ecrnx_hw: main driver data
 * @ecrnx_vif: Pointer on VIF
 */
void ecrnx_txq_vif_deinit(struct ecrnx_hw * ecrnx_hw, struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_txq *txq;

#ifdef CONFIG_ECRNX_SOFTMAC
    int ac;

    foreach_vif_txq(ecrnx_vif, txq, ac) {
        if (txq)
            ecrnx_txq_deinit(ecrnx_hw, txq);
    }

#else
    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_BCMC_TXQ_TYPE);
    ecrnx_txq_deinit(ecrnx_hw, txq);

    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_UNK_TXQ_TYPE);
    ecrnx_txq_deinit(ecrnx_hw, txq);

#endif /* CONFIG_ECRNX_SOFTMAC */
}


/**
 * ecrnx_txq_sta_init - Initialize TX queues for a STA
 *
 * @ecrnx_hw: Main driver data
 * @ecrnx_sta: STA for which tx queues need to be initialized
 * @status: Intial txq status
 *
 * This function initialize all the TXQ associated to a STA.
 * Softmac : 1 TXQ per TID
 *
 * Fullmac : 1 TXQ per TID (limited to 8)
 *           1 TXQ for MGMT
 */
void ecrnx_txq_sta_init(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *ecrnx_sta,
                       u8 status)
{
    struct ecrnx_txq *txq;
    int tid, idx;

#ifdef CONFIG_ECRNX_SOFTMAC
    struct ieee80211_sta *sta = ecrnx_to_ieee80211_sta(ecrnx_sta);
    idx = ecrnx_txq_sta_idx(ecrnx_sta, 0);

    foreach_sta_txq(ecrnx_sta, txq, tid, ecrnx_hw) {
        ecrnx_txq_init(txq, idx, status, &ecrnx_hw->hwq[ecrnx_tid2hwq[tid]], tid, sta);
        idx++;
    }

#else
    struct ecrnx_vif *ecrnx_vif = ecrnx_hw->vif_table[ecrnx_sta->vif_idx];
    idx = ecrnx_txq_sta_idx(ecrnx_sta, 0);

    foreach_sta_txq(ecrnx_sta, txq, tid, ecrnx_hw) {
        ecrnx_txq_init(txq, idx, status, &ecrnx_hw->hwq[ecrnx_tid2hwq[tid]], tid,
                      ecrnx_sta, ecrnx_vif->ndev);
        txq->ps_id = ecrnx_sta->uapsd_tids & (1 << tid) ? UAPSD_ID : LEGACY_PS_ID;
        idx++;
    }

#endif /* CONFIG_ECRNX_SOFTMAC*/
#ifndef CONFIG_ECRNX_ESWIN
    ecrnx_ipc_sta_buffer_init(ecrnx_hw, ecrnx_sta->sta_idx);
#endif
}

/**
 * ecrnx_txq_sta_deinit - Deinitialize TX queues for a STA
 *
 * @ecrnx_hw: Main driver data
 * @ecrnx_sta: STA for which tx queues need to be deinitialized
 */
void ecrnx_txq_sta_deinit(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *ecrnx_sta)
{
    struct ecrnx_txq *txq;
    int tid;

    foreach_sta_txq(ecrnx_sta, txq, tid, ecrnx_hw) {
        ecrnx_txq_deinit(ecrnx_hw, txq);
    }
}

#ifdef CONFIG_ECRNX_FULLMAC
/**
 * ecrnx_txq_unk_vif_init - Initialize TXQ for unknown STA linked to a vif
 *
 * @ecrnx_vif: Pointer on VIF
 */
void ecrnx_txq_unk_vif_init(struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;
    struct ecrnx_txq *txq;
    int idx;

    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_UNK_TXQ_TYPE);
    idx = ecrnx_txq_vif_idx(ecrnx_vif, NX_UNK_TXQ_TYPE);
    ecrnx_txq_init(txq, idx, 0, &ecrnx_hw->hwq[ECRNX_HWQ_VO], TID_MGT, NULL, ecrnx_vif->ndev);
}

/**
 * ecrnx_txq_tdls_vif_deinit - Deinitialize TXQ for unknown STA linked to a vif
 *
 * @ecrnx_vif: Pointer on VIF
 */
void ecrnx_txq_unk_vif_deinit(struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_txq *txq;

    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_UNK_TXQ_TYPE);
    ecrnx_txq_deinit(ecrnx_vif->ecrnx_hw, txq);
}

/**
 * ecrnx_init_unk_txq - Initialize TX queue for the transmission on a offchannel
 *
 * @vif: Interface for which the queue has to be initialized
 *
 * NOTE: Offchannel txq is only active for the duration of the ROC
 */
void ecrnx_txq_offchan_init(struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;
    struct ecrnx_txq *txq;

    txq = &ecrnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
    ecrnx_txq_init(txq, NX_OFF_CHAN_TXQ_IDX, ECRNX_TXQ_STOP_CHAN,
                  &ecrnx_hw->hwq[ECRNX_HWQ_VO], TID_MGT, NULL, ecrnx_vif->ndev);
}

/**
 * ecrnx_deinit_offchan_txq - Deinitialize TX queue for offchannel
 *
 * @vif: Interface that manages the STA
 *
 * This function deintialize txq for one STA.
 * Any buffer stuck in a queue will be freed.
 */
void ecrnx_txq_offchan_deinit(struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_txq *txq;

    txq = &ecrnx_vif->ecrnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
    ecrnx_txq_deinit(ecrnx_vif->ecrnx_hw, txq);
}


/**
 * ecrnx_txq_tdls_vif_init - Initialize TXQ vif for TDLS
 *
 * @ecrnx_vif: Pointer on VIF
 */
void ecrnx_txq_tdls_vif_init(struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;

    if (!(ecrnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
        return;

    ecrnx_txq_unk_vif_init(ecrnx_vif);
}

/**
 * ecrnx_txq_tdls_vif_deinit - Deinitialize TXQ vif for TDLS
 *
 * @ecrnx_vif: Pointer on VIF
 */
void ecrnx_txq_tdls_vif_deinit(struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;

    if (!(ecrnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
        return;

    ecrnx_txq_unk_vif_deinit(ecrnx_vif);
}
/**
 * ecrnx_txq_drop_old_traffic - Drop pkt queued for too long in a TXQ
 *
 * @txq: TXQ to process
 * @ecrnx_hw: Driver main data
 * @skb_timeout: Max queue duration, in jiffies, for this queue
 * @dropped: Updated to inidicate if at least one skb was dropped
 *
 * @return Whether there is still pkt queued in this queue.
 */
static bool ecrnx_txq_drop_old_traffic(struct ecrnx_txq *txq, struct ecrnx_hw *ecrnx_hw,
                                      unsigned long skb_timeout, bool *dropped)
{
    struct sk_buff *skb, *skb_next;
    bool pkt_queued = false;
    int retry_packet = txq->nb_retry;

    if (txq->idx == TXQ_INACTIVE)
        return false;

    spin_lock(&ecrnx_hw->tx_lock);

    skb_queue_walk_safe(&txq->sk_list, skb, skb_next) {

        struct ecrnx_sw_txhdr *sw_txhdr;

        if (retry_packet) {
            // Don't drop retry packets
            retry_packet--;
            continue;
        }

        sw_txhdr = ((struct ecrnx_txhdr *)skb->data)->sw_hdr;

        if (!time_after(jiffies, sw_txhdr->jiffies + skb_timeout)) {
            pkt_queued = true;
            break;
        }

        *dropped = true;
        ECRNX_WARN("%s:skb:0x%08x,txq:0x%p, txq_idx:%d, ps_active:%d, ps_id:%d \n", __func__, skb, txq, txq->idx, txq->sta->ps.active, txq->ps_id);
        ecrnx_txq_drop_skb(txq, skb, ecrnx_hw, false);
        if (txq->sta && txq->sta->ps.active) {
            txq->sta->ps.pkt_ready[txq->ps_id]--;
            if (txq->sta->ps.pkt_ready[txq->ps_id] == 0)
                ecrnx_set_traffic_status(ecrnx_hw, txq->sta, false, txq->ps_id);

            // drop packet during PS service period
            if (txq->sta->ps.sp_cnt[txq->ps_id]) {
                txq->sta->ps.sp_cnt[txq->ps_id] --;
                if (txq->push_limit)
                    txq->push_limit--;
                if (WARN(((txq->ps_id == UAPSD_ID) &&
                          (txq->sta->ps.sp_cnt[txq->ps_id] == 0)),
                         "Drop last packet of UAPSD service period")) {
                    // TODO: inform FW to end SP
                }
            }
            trace_ps_drop(txq->sta);
        }
    }

    if (skb_queue_empty(&txq->sk_list)) {
        ecrnx_txq_del_from_hw_list(txq);
        txq->pkt_sent = 0;
    }

    spin_unlock(&ecrnx_hw->tx_lock);

#ifdef CONFIG_ECRNX_FULLMAC
    /* restart netdev queue if number no more queued buffer */
    if (unlikely(txq->status & ECRNX_TXQ_NDEV_FLOW_CTRL) &&
        skb_queue_empty(&txq->sk_list)) {
        txq->status &= ~ECRNX_TXQ_NDEV_FLOW_CTRL;
        netif_wake_subqueue(txq->ndev, txq->ndev_idx);
        trace_txq_flowctrl_restart(txq);
    }
#endif /* CONFIG_ECRNX_FULLMAC */

    return pkt_queued;
}

/**
 * ecrnx_txq_drop_ap_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to an "AP" vif (AP, MESH, P2P_GO)
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool ecrnx_txq_drop_ap_vif_old_traffic(struct ecrnx_vif *vif)
{
    struct ecrnx_sta *sta;
    unsigned long timeout = (vif->ap.bcn_interval * HZ * 3) >> 10;
    bool pkt_queued = false;
    bool pkt_dropped = false;

    // Should never be needed but still check VIF queues
    ecrnx_txq_drop_old_traffic(ecrnx_txq_vif_get(vif, NX_BCMC_TXQ_TYPE),
                              vif->ecrnx_hw, ECRNX_TXQ_MAX_QUEUE_JIFFIES, &pkt_dropped);
    ecrnx_txq_drop_old_traffic(ecrnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE),
                              vif->ecrnx_hw, ECRNX_TXQ_MAX_QUEUE_JIFFIES, &pkt_dropped);
    ECRNX_WARN("Dropped packet in BCMC/UNK queue. \n");

    list_for_each_entry(sta, &vif->ap.sta_list, list) {
        struct ecrnx_txq *txq;
        int tid;
        foreach_sta_txq(sta, txq, tid, vif->ecrnx_hw) {
            pkt_queued |= ecrnx_txq_drop_old_traffic(txq, vif->ecrnx_hw,
                                                    timeout * sta->listen_interval,
                                                    &pkt_dropped);
        }
    }

    return pkt_queued;
}

/**
 * ecrnx_txq_drop_sta_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to a "STA" vif. In theory this should not be required as there is no
 * case where traffic can accumulate in a STA interface.
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool ecrnx_txq_drop_sta_vif_old_traffic(struct ecrnx_vif *vif)
{
    struct ecrnx_txq *txq;
    bool pkt_queued = false, pkt_dropped = false;
    int tid;

    if (vif->tdls_status == TDLS_LINK_ACTIVE) {
        txq = ecrnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
        pkt_queued |= ecrnx_txq_drop_old_traffic(txq, vif->ecrnx_hw,
                                                ECRNX_TXQ_MAX_QUEUE_JIFFIES,
                                                &pkt_dropped);
        foreach_sta_txq(vif->sta.tdls_sta, txq, tid, vif->ecrnx_hw) {
            pkt_queued |= ecrnx_txq_drop_old_traffic(txq, vif->ecrnx_hw,
                                                    ECRNX_TXQ_MAX_QUEUE_JIFFIES,
                                                    &pkt_dropped);
        }
    }

    if (vif->sta.ap) {
        foreach_sta_txq(vif->sta.ap, txq, tid, vif->ecrnx_hw) {
            pkt_queued |= ecrnx_txq_drop_old_traffic(txq, vif->ecrnx_hw,
                                                    ECRNX_TXQ_MAX_QUEUE_JIFFIES,
                                                    &pkt_dropped);
        }
    }

    if (pkt_dropped) {
        ECRNX_WARN("Dropped packet in STA interface TXQs. \n");
    }
    return pkt_queued;
}

/**
 * ecrnx_txq_cleanup_timer_cb - callack for TXQ cleaup timer
 * Used to prevent pkt to accumulate in TXQ. The main use case is for AP
 * interface with client in Power Save mode but just in case all TXQs are
 * checked.
 *
 * @t: timer structure
 */
static void ecrnx_txq_cleanup_timer_cb(struct timer_list *t)
{
    struct ecrnx_hw *ecrnx_hw = from_timer(ecrnx_hw, t, txq_cleanup);
    struct ecrnx_vif *vif;
    bool pkt_queue = false;

    list_for_each_entry(vif, &ecrnx_hw->vifs, list) {
        switch (ECRNX_VIF_TYPE(vif)) {
            case NL80211_IFTYPE_AP:
            case NL80211_IFTYPE_P2P_GO:
            case NL80211_IFTYPE_MESH_POINT:
                pkt_queue |= ecrnx_txq_drop_ap_vif_old_traffic(vif);
                break;
            case NL80211_IFTYPE_STATION:
            case NL80211_IFTYPE_P2P_CLIENT:
                 pkt_queue |= ecrnx_txq_drop_sta_vif_old_traffic(vif);
                 break;
            case NL80211_IFTYPE_AP_VLAN:
            case NL80211_IFTYPE_MONITOR:
            default:
                continue;
        }
    }

    if (pkt_queue)
        mod_timer(t, jiffies + ECRNX_TXQ_CLEANUP_INTERVAL);
}

/**
 * ecrnx_txq_start_cleanup_timer - Start 'cleanup' timer if not started
 *
 * @ecrnx_hw: Driver main data
 */
void ecrnx_txq_start_cleanup_timer(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta)
{
    if (sta && !is_multicast_sta(sta->sta_idx) &&
        !timer_pending(&ecrnx_hw->txq_cleanup))
        mod_timer(&ecrnx_hw->txq_cleanup, jiffies + ECRNX_TXQ_CLEANUP_INTERVAL);
}

/**
 * ecrnx_txq_prepare - Global initialization of txq
 *
 * @ecrnx_hw: Driver main data
 */
void ecrnx_txq_prepare(struct ecrnx_hw *ecrnx_hw)
{
    int i;

    for (i = 0; i < NX_NB_TXQ; i++) {
        ecrnx_hw->txq[i].idx = TXQ_INACTIVE;
    }

    timer_setup(&ecrnx_hw->txq_cleanup, ecrnx_txq_cleanup_timer_cb, 0);
}
#endif

/******************************************************************************
 * Start/Stop functions
 *****************************************************************************/
/**
 * ecrnx_txq_add_to_hw_list - Add TX queue to a HW queue schedule list.
 *
 * @txq: TX queue to add
 *
 * Add the TX queue if not already present in the HW queue list.
 * To be called with tx_lock hold
 */
void ecrnx_txq_add_to_hw_list(struct ecrnx_txq *txq)
{
    if (!(txq->status & ECRNX_TXQ_IN_HWQ_LIST)) {
        trace_txq_add_to_hw(txq);
        txq->status |= ECRNX_TXQ_IN_HWQ_LIST;
        list_add_tail(&txq->sched_list, &txq->hwq->list);
        txq->hwq->need_processing = true;
    }
}

/**
 * ecrnx_txq_del_from_hw_list - Delete TX queue from a HW queue schedule list.
 *
 * @txq: TX queue to delete
 *
 * Remove the TX queue from the HW queue list if present.
 * To be called with tx_lock hold
 */
void ecrnx_txq_del_from_hw_list(struct ecrnx_txq *txq)
{
    if (txq->status & ECRNX_TXQ_IN_HWQ_LIST) {
        trace_txq_del_from_hw(txq);
        txq->status &= ~ECRNX_TXQ_IN_HWQ_LIST;
        list_del(&txq->sched_list);
    }
}

/**
 * ecrnx_txq_skb_ready - Check if skb are available for the txq
 *
 * @txq: Pointer on txq
 * @return True if there are buffer ready to be pushed on this txq,
 * false otherwise
 */
static inline bool ecrnx_txq_skb_ready(struct ecrnx_txq *txq)
{
#ifdef CONFIG_MAC80211_TXQ
    if (txq->nb_ready_mac80211 != NOT_MAC80211_TXQ)
        return ((txq->nb_ready_mac80211 > 0) || !skb_queue_empty(&txq->sk_list));
    else
#endif
    return !skb_queue_empty(&txq->sk_list);
}

/**
 * ecrnx_txq_start - Try to Start one TX queue
 *
 * @txq: TX queue to start
 * @reason: reason why the TX queue is started (among ECRNX_TXQ_STOP_xxx)
 *
 * Re-start the TX queue for one reason.
 * If after this the txq is no longer stopped and some buffers are ready,
 * the TX queue is also added to HW queue list.
 * To be called with tx_lock hold
 */
void ecrnx_txq_start(struct ecrnx_txq *txq, u16 reason)
{
    BUG_ON(txq==NULL);
    if (txq->idx != TXQ_INACTIVE && (txq->status & reason))
    {
        trace_txq_start(txq, reason);
        txq->status &= ~reason;
        if (!ecrnx_txq_is_stopped(txq) && ecrnx_txq_skb_ready(txq))
            ecrnx_txq_add_to_hw_list(txq);
    }
}

/**
 * ecrnx_txq_stop - Stop one TX queue
 *
 * @txq: TX queue to stop
 * @reason: reason why the TX queue is stopped (among ECRNX_TXQ_STOP_xxx)
 *
 * Stop the TX queue. It will remove the TX queue from HW queue list
 * To be called with tx_lock hold
 */
void ecrnx_txq_stop(struct ecrnx_txq *txq, u16 reason)
{
    BUG_ON(txq==NULL);
    if (txq->idx != TXQ_INACTIVE)
    {
        trace_txq_stop(txq, reason);
        txq->status |= reason;
        ecrnx_txq_del_from_hw_list(txq);
    }
}


/**
 * ecrnx_txq_sta_start - Start all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be re-started
 * @reason: Reason why the TX queue are restarted (among ECRNX_TXQ_STOP_xxx)
 * @ecrnx_hw: Driver main data
 *
 * This function will re-start all the TX queues of the STA for the reason
 * specified. It can be :
 * - ECRNX_TXQ_STOP_STA_PS: the STA is no longer in power save mode
 * - ECRNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - ECRNX_TXQ_STOP_CHAN: the STA's VIF is now on the current active channel
 *
 * Any TX queue with buffer ready and not Stopped for other reasons, will be
 * added to the HW queue list
 * To be called with tx_lock hold
 */
void ecrnx_txq_sta_start(struct ecrnx_sta *ecrnx_sta, u16 reason
#ifdef CONFIG_ECRNX_FULLMAC
                        , struct ecrnx_hw *ecrnx_hw
#endif
                        )
{
    struct ecrnx_txq *txq;
    int tid;

    trace_txq_sta_start(ecrnx_sta->sta_idx);
	ECRNX_DBG("%s-%d:ecrnx_txq_stop,reaosn:0x%x,sta:0x%08x,sta_index:0x%08x \n", __func__, __LINE__, reason, ecrnx_sta, ecrnx_sta->sta_idx);
    foreach_sta_txq(ecrnx_sta, txq, tid, ecrnx_hw) {
        ecrnx_txq_start(txq, reason);

        if (txq->idx != TXQ_INACTIVE && !skb_queue_empty(&txq->sk_list))
        {
            ecrnx_hwq_process(ecrnx_hw, txq->hwq);
        }
    }
}


/**
 * ecrnx_stop_sta_txq - Stop all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be stopped
 * @reason: Reason why the TX queue are stopped (among ECRNX_TX_STOP_xxx)
 * @ecrnx_hw: Driver main data
 *
 * This function will stop all the TX queues of the STA for the reason
 * specified. It can be :
 * - ECRNX_TXQ_STOP_STA_PS: the STA is in power save mode
 * - ECRNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - ECRNX_TXQ_STOP_CHAN: the STA's VIF is not on the current active channel
 *
 * Any TX queue present in a HW queue list will be removed from this list.
 * To be called with tx_lock hold
 */
void ecrnx_txq_sta_stop(struct ecrnx_sta *ecrnx_sta, u16 reason
#ifdef CONFIG_ECRNX_FULLMAC
                       , struct ecrnx_hw *ecrnx_hw
#endif
                       )
{
    struct ecrnx_txq *txq;
    int tid;

    if (!ecrnx_sta)
        return;

    trace_txq_sta_stop(ecrnx_sta->sta_idx);
    foreach_sta_txq(ecrnx_sta, txq, tid, ecrnx_hw) {
 	ECRNX_DBG("%s-%d:stop_reaosn:0x%x,sta:0x%08x,sta_index:0x%08x ,txq:0x%p,tid:%d \n", __func__, __LINE__, reason, ecrnx_sta, ecrnx_sta->sta_idx, txq, tid);
        ecrnx_txq_stop(txq, reason);
    }
}

#ifdef CONFIG_ECRNX_SOFTMAC
void ecrnx_txq_tdls_sta_start(struct ecrnx_sta *ecrnx_sta, u16 reason,
                             struct ecrnx_hw *ecrnx_hw)
#else
void ecrnx_txq_tdls_sta_start(struct ecrnx_vif *ecrnx_vif, u16 reason,
                             struct ecrnx_hw *ecrnx_hw)
#endif
{
#ifdef CONFIG_ECRNX_SOFTMAC
    trace_txq_vif_start(ecrnx_sta->vif_idx);
    spin_lock_bh(&ecrnx_hw->tx_lock);

    if (ecrnx_sta->tdls.active) {
        ecrnx_txq_sta_start(ecrnx_sta, reason);
    }

    spin_unlock_bh(&ecrnx_hw->tx_lock);
#else
    trace_txq_vif_start(ecrnx_vif->vif_index);
    spin_lock_bh(&ecrnx_hw->tx_lock);

    if (ecrnx_vif->sta.tdls_sta)
        ecrnx_txq_sta_start(ecrnx_vif->sta.tdls_sta, reason, ecrnx_hw);

    spin_unlock_bh(&ecrnx_hw->tx_lock);
#endif
}

#ifdef CONFIG_ECRNX_SOFTMAC
void ecrnx_txq_tdls_sta_stop(struct ecrnx_sta *ecrnx_sta, u16 reason,
                            struct ecrnx_hw *ecrnx_hw)
#else
void ecrnx_txq_tdls_sta_stop(struct ecrnx_vif *ecrnx_vif, u16 reason,
                            struct ecrnx_hw *ecrnx_hw)
#endif
{
#ifdef CONFIG_ECRNX_SOFTMAC
    trace_txq_vif_stop(ecrnx_sta->vif_idx);

    spin_lock_bh(&ecrnx_hw->tx_lock);

    if (ecrnx_sta->tdls.active) {
        ecrnx_txq_sta_stop(ecrnx_sta, reason);
    }

    spin_unlock_bh(&ecrnx_hw->tx_lock);
#else
    trace_txq_vif_stop(ecrnx_vif->vif_index);

    spin_lock_bh(&ecrnx_hw->tx_lock);

    if (ecrnx_vif->sta.tdls_sta)
        ecrnx_txq_sta_stop(ecrnx_vif->sta.tdls_sta, reason, ecrnx_hw);

    spin_unlock_bh(&ecrnx_hw->tx_lock);
#endif
}

#ifdef CONFIG_ECRNX_FULLMAC
static inline
void ecrnx_txq_vif_for_each_sta(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                               void (*f)(struct ecrnx_sta *, u16, struct ecrnx_hw *),
                               u16 reason)
{
    switch (ECRNX_VIF_TYPE(ecrnx_vif)) {
    case NL80211_IFTYPE_STATION:
    case NL80211_IFTYPE_P2P_CLIENT:
    {
        if (ecrnx_vif->tdls_status == TDLS_LINK_ACTIVE)
            f(ecrnx_vif->sta.tdls_sta, reason, ecrnx_hw);
#ifdef CONFIG_ECRNX_P2P
        if (!(ecrnx_vif->sta.ap == NULL))
#else
		if (!WARN_ON(ecrnx_vif->sta.ap == NULL))
#endif
            f(ecrnx_vif->sta.ap, reason, ecrnx_hw);
        break;
    }
    case NL80211_IFTYPE_AP_VLAN:
    {
        ecrnx_vif = ecrnx_vif->ap_vlan.master;
		struct ecrnx_sta *sta;
        list_for_each_entry(sta, &ecrnx_vif->ap.sta_list, list) {
            f(sta, reason, ecrnx_hw);
        }
        break;
    }
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_MESH_POINT:
    case NL80211_IFTYPE_P2P_GO:
    {
        struct ecrnx_sta *sta;
        list_for_each_entry(sta, &ecrnx_vif->ap.sta_list, list) {
            f(sta, reason, ecrnx_hw);
        }
        break;
    }
    default:
        BUG();
        break;
    }
}

#endif

/**
 * ecrnx_txq_vif_start - START TX queues of all STA associated to the vif
 *                      and vif's TXQ
 *
 * @vif: Interface to start
 * @reason: Start reason (ECRNX_TXQ_STOP_CHAN or ECRNX_TXQ_STOP_VIF_PS)
 * @ecrnx_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and re-start them for the
 * reason @reason
 * Take tx_lock
 */
void ecrnx_txq_vif_start(struct ecrnx_vif *ecrnx_vif, u16 reason,
                        struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_txq *txq;
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_sta *ecrnx_sta;
    int ac;
#endif

    trace_txq_vif_start(ecrnx_vif->vif_index);

    spin_lock_bh(&ecrnx_hw->tx_lock);

#ifdef CONFIG_ECRNX_SOFTMAC
    list_for_each_entry(ecrnx_sta, &ecrnx_vif->stations, list) {
        if ((!ecrnx_vif->roc_tdls) ||
            (ecrnx_sta->tdls.active && ecrnx_vif->roc_tdls && ecrnx_sta->tdls.chsw_en))
            ecrnx_txq_sta_start(ecrnx_sta, reason);
    }

    foreach_vif_txq(ecrnx_vif, txq, ac) {
        if (txq)
            ecrnx_txq_start(txq, reason);
    }
#else
    //Reject if monitor interface
    if (ecrnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
        goto end;

    if (ecrnx_vif->roc_tdls && ecrnx_vif->sta.tdls_sta && ecrnx_vif->sta.tdls_sta->tdls.chsw_en) {
        ecrnx_txq_sta_start(ecrnx_vif->sta.tdls_sta, reason, ecrnx_hw);
    }
    if (!ecrnx_vif->roc_tdls) {
        ecrnx_txq_vif_for_each_sta(ecrnx_hw, ecrnx_vif, ecrnx_txq_sta_start, reason);
    }

    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_BCMC_TXQ_TYPE);
    ecrnx_txq_start(txq, reason);
    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_UNK_TXQ_TYPE);
    ecrnx_txq_start(txq, reason);

end:
#endif /* CONFIG_ECRNX_SOFTMAC */

    spin_unlock_bh(&ecrnx_hw->tx_lock);
}


/**
 * ecrnx_txq_vif_stop - STOP TX queues of all STA associated to the vif
 *
 * @vif: Interface to stop
 * @arg: Stop reason (ECRNX_TXQ_STOP_CHAN or ECRNX_TXQ_STOP_VIF_PS)
 * @ecrnx_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and stop them for the
 * reason ECRNX_TXQ_STOP_CHAN or ECRNX_TXQ_STOP_VIF_PS
 * Take tx_lock
 */
void ecrnx_txq_vif_stop(struct ecrnx_vif *ecrnx_vif, u16 reason,
                       struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_txq *txq;
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_sta *sta;
    int ac;
#endif

    trace_txq_vif_stop(ecrnx_vif->vif_index);
    spin_lock_bh(&ecrnx_hw->tx_lock);

#ifdef CONFIG_ECRNX_SOFTMAC
    list_for_each_entry(sta, &ecrnx_vif->stations, list) {
        ecrnx_txq_sta_stop(sta, reason);
    }

    foreach_vif_txq(ecrnx_vif, txq, ac) {
        if (txq)
        {
            ecrnx_txq_stop(txq, reason);
        }
    }

#else
    //Reject if monitor interface
    if (ecrnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
        goto end;

    ecrnx_txq_vif_for_each_sta(ecrnx_hw, ecrnx_vif, ecrnx_txq_sta_stop, reason);

    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_BCMC_TXQ_TYPE);
    ecrnx_txq_stop(txq, reason);
    txq = ecrnx_txq_vif_get(ecrnx_vif, NX_UNK_TXQ_TYPE);
    ecrnx_txq_stop(txq, reason);

end:
#endif /* CONFIG_ECRNX_SOFTMAC*/

    spin_unlock_bh(&ecrnx_hw->tx_lock);
}

#ifdef CONFIG_ECRNX_FULLMAC

/**
 * ecrnx_start_offchan_txq - START TX queue for offchannel frame
 *
 * @ecrnx_hw: Driver main data
 */
void ecrnx_txq_offchan_start(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_txq *txq;

    txq = &ecrnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
    spin_lock_bh(&ecrnx_hw->tx_lock);
    ecrnx_txq_start(txq, ECRNX_TXQ_STOP_CHAN);
    spin_unlock_bh(&ecrnx_hw->tx_lock);
    ECRNX_DBG("%s-%d:ecrnx_txq_start,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_CHAN);
}

/**
 * ecrnx_switch_vif_sta_txq - Associate TXQ linked to a STA to a new vif
 *
 * @sta: STA whose txq must be switched
 * @old_vif: Vif currently associated to the STA (may no longer be active)
 * @new_vif: vif which should be associated to the STA for now on
 *
 * This function will switch the vif (i.e. the netdev) associated to all STA's
 * TXQ. This is used when AP_VLAN interface are created.
 * If one STA is associated to an AP_vlan vif, it will be moved from the master
 * AP vif to the AP_vlan vif.
 * If an AP_vlan vif is removed, then STA will be moved back to mastert AP vif.
 *
 */
void ecrnx_txq_sta_switch_vif(struct ecrnx_sta *sta, struct ecrnx_vif *old_vif,
                             struct ecrnx_vif *new_vif)
{
    struct ecrnx_hw *ecrnx_hw = new_vif->ecrnx_hw;
    struct ecrnx_txq *txq;
    int i;

    /* start TXQ on the new interface, and update ndev field in txq */
    if (!netif_carrier_ok(new_vif->ndev))
        netif_carrier_on(new_vif->ndev);
    txq = ecrnx_txq_sta_get(sta, 0, ecrnx_hw);
    for (i = 0; i < NX_NB_TID_PER_STA; i++, txq++) {
        txq->ndev = new_vif->ndev;
        netif_wake_subqueue(txq->ndev, txq->ndev_idx);
    }
}
#endif /* CONFIG_ECRNX_FULLMAC */

/******************************************************************************
 * TXQ queue/schedule functions
 *****************************************************************************/
/**
 * ecrnx_txq_queue_skb - Queue a buffer in a TX queue
 *
 * @skb: Buffer to queue
 * @txq: TX Queue in which the buffer must be added
 * @ecrnx_hw: Driver main data
 * @retry: Should it be queued in the retry list
 *
 * @return: Retrun 1 if txq has been added to hwq list, 0 otherwise
 *
 * Add a buffer in the buffer list of the TX queue
 * and add this TX queue in the HW queue list if the txq is not stopped.
 * If this is a retry packet it is added after the last retry packet or at the
 * beginning if there is no retry packet queued.
 *
 * If the STA is in PS mode and this is the first packet queued for this txq
 * update TIM.
 *
 * To be called with tx_lock hold
 */
int ecrnx_txq_queue_skb(struct sk_buff *skb, struct ecrnx_txq *txq,
                       struct ecrnx_hw *ecrnx_hw,  bool retry,
                       struct sk_buff *skb_prev)
{

#ifdef CONFIG_ECRNX_FULLMAC
    if (unlikely(txq->sta && txq->sta->ps.active)) {
        txq->sta->ps.pkt_ready[txq->ps_id]++;
        trace_ps_queue(txq->sta);

        if (txq->sta->ps.pkt_ready[txq->ps_id] == 1) {
            ecrnx_set_traffic_status(ecrnx_hw, txq->sta, true, txq->ps_id);
        }
    }
#endif

    if (!retry) {
        /* add buffer in the sk_list */
        if (skb_prev)
            skb_append(skb_prev, skb, &txq->sk_list);
        else
        skb_queue_tail(&txq->sk_list, skb);
#ifdef CONFIG_ECRNX_FULLMAC
        // to update for SOFTMAC
        ecrnx_ipc_sta_buffer(ecrnx_hw, txq->sta, txq->tid,
                            ((struct ecrnx_txhdr *)skb->data)->sw_hdr->frame_len);
        ecrnx_txq_start_cleanup_timer(ecrnx_hw, txq->sta);
#endif
    } else {
        if (txq->last_retry_skb)
            skb_append(txq->last_retry_skb, skb, &txq->sk_list);
        else
            skb_queue_head(&txq->sk_list, skb);

        txq->last_retry_skb = skb;
        txq->nb_retry++;
    }

    trace_txq_queue_skb(skb, txq, retry);

    /* Flowctrl corresponding netdev queue if needed */
#ifdef CONFIG_ECRNX_FULLMAC
    /* If too many buffer are queued for this TXQ stop netdev queue */
    if ((txq->ndev_idx != NDEV_NO_TXQ) &&
        (skb_queue_len(&txq->sk_list) > ECRNX_NDEV_FLOW_CTRL_STOP)) {
        txq->status |= ECRNX_TXQ_NDEV_FLOW_CTRL;
        netif_stop_subqueue(txq->ndev, txq->ndev_idx);
        trace_txq_flowctrl_stop(txq);
    }
#else /* ! CONFIG_ECRNX_FULLMAC */

    if (!retry && ++txq->hwq->len == txq->hwq->len_stop) {
         trace_hwq_flowctrl_stop(txq->hwq->id);
         ieee80211_stop_queue(ecrnx_hw->hw, txq->hwq->id);
         ecrnx_hw->stats.queues_stops++;
     }
#endif /* CONFIG_ECRNX_FULLMAC */

    //ECRNX_DBG("txq status: 0x%x \n", txq->status);
    /* add it in the hwq list if not stopped and not yet present */
    if (!ecrnx_txq_is_stopped(txq)) {
        ecrnx_txq_add_to_hw_list(txq);
        return 1;
    }

    return 0;
}

/**
 * ecrnx_txq_confirm_any - Process buffer confirmed by fw
 *
 * @ecrnx_hw: Driver main data
 * @txq: TX Queue
 * @hwq: HW Queue
 * @sw_txhdr: software descriptor of the confirmed packet
 *
 * Process a buffer returned by the fw. It doesn't check buffer status
 * and only does systematic counter update:
 * - hw credit
 * - buffer pushed to fw
 *
 * To be called with tx_lock hold
 */
void ecrnx_txq_confirm_any(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txq *txq,
                          struct ecrnx_hwq *hwq, struct ecrnx_sw_txhdr *sw_txhdr)
{
    int user = 0;

#ifdef CONFIG_ECRNX_MUMIMO_TX
    int group_id;

    user = ECRNX_MUMIMO_INFO_POS_ID(sw_txhdr->desc.host.mumimo_info);
    group_id = ECRNX_MUMIMO_INFO_GROUP_ID(sw_txhdr->desc.host.mumimo_info);

    if ((txq->idx != TXQ_INACTIVE) &&
        (txq->pkt_pushed[user] == 1) &&
        (txq->status & ECRNX_TXQ_STOP_MU_POS)){
            ecrnx_txq_start(txq, ECRNX_TXQ_STOP_MU_POS);
            ECRNX_DBG("%s-%d:ecrnx_txq_start,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_MU_POS);
        }

#endif /* CONFIG_ECRNX_MUMIMO_TX */

    if (txq->pkt_pushed[user])
        txq->pkt_pushed[user]--;

    hwq->credits[user]++;
    hwq->need_processing = true;
    ecrnx_hw->stats.cfm_balance[hwq->id]--;
}

/******************************************************************************
 * HWQ processing
 *****************************************************************************/
static inline
bool ecrnx_txq_take_mu_lock(struct ecrnx_hw *ecrnx_hw)
{
    bool res = false;
#ifdef CONFIG_ECRNX_MUMIMO_TX
    if (ecrnx_hw->mod_params->mutx)
        res = (down_trylock(&ecrnx_hw->mu.lock) == 0);
#endif /* CONFIG_ECRNX_MUMIMO_TX */
    return res;
}

static inline
void ecrnx_txq_release_mu_lock(struct ecrnx_hw *ecrnx_hw)
{
#ifdef CONFIG_ECRNX_MUMIMO_TX
    up(&ecrnx_hw->mu.lock);
#endif /* CONFIG_ECRNX_MUMIMO_TX */
}

static inline
void ecrnx_txq_set_mu_info(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txq *txq,
                          int group_id, int pos)
{
#ifdef CONFIG_ECRNX_MUMIMO_TX
    trace_txq_select_mu_group(txq, group_id, pos);
    if (group_id) {
        txq->mumimo_info = group_id | (pos << 6);
        ecrnx_mu_set_active_group(ecrnx_hw, group_id);
    } else
        txq->mumimo_info = 0;
#endif /* CONFIG_ECRNX_MUMIMO_TX */
}

static inline
s8 ecrnx_txq_get_credits(struct ecrnx_txq *txq)
{
    s8 cred = txq->credits;
    /* if destination is in PS mode, push_limit indicates the maximum
       number of packet that can be pushed on this txq. */
    if (txq->push_limit && (cred > txq->push_limit)) {
        cred = txq->push_limit;
    }
    return cred;
}

/**
 * skb_queue_extract - Extract buffer from skb list
 *
 * @list: List of skb to extract from
 * @head: List of skb to append to
 * @nb_elt: Number of skb to extract
 *
 * extract the first @nb_elt of @list and append them to @head
 * It is assume that:
 * - @list contains more that @nb_elt
 * - There is no need to take @list nor @head lock to modify them
 */
static inline void skb_queue_extract(struct sk_buff_head *list,
                                     struct sk_buff_head *head, int nb_elt)
{
    int i;
    struct sk_buff *first, *last, *ptr;

    first = ptr = list->next;
    for (i = 0; i < nb_elt; i++) {
        ptr = ptr->next;
    }
    last = ptr->prev;

    /* unlink nb_elt in list */
    list->qlen -= nb_elt;
    list->next = ptr;
    ptr->prev = (struct sk_buff *)list;

    /* append nb_elt at end of head */
    head->qlen += nb_elt;
    last->next = (struct sk_buff *)head;
    head->prev->next = first;
    first->prev = head->prev;
    head->prev = last;
}


#ifdef CONFIG_MAC80211_TXQ
/**
 * ecrnx_txq_mac80211_dequeue - Dequeue buffer from mac80211 txq and
 *                             add them to push list
 *
 * @ecrnx_hw: Main driver data
 * @sk_list: List of buffer to push (initialized without lock)
 * @txq: TXQ to dequeue buffers from
 * @max: Max number of buffer to dequeue
 *
 * Dequeue buffer from mac80211 txq, prepare them for transmission and chain them
 * to the list of buffer to push.
 *
 * @return true if no more buffer are queued in mac80211 txq and false otherwise.
 */
static bool ecrnx_txq_mac80211_dequeue(struct ecrnx_hw *ecrnx_hw,
                                      struct sk_buff_head *sk_list,
                                      struct ecrnx_txq *txq, int max)
{
    struct ieee80211_txq *mac_txq;
    struct sk_buff *skb;
    unsigned long mac_txq_len;

    if (txq->nb_ready_mac80211 == NOT_MAC80211_TXQ)
        return true;

    mac_txq = container_of((void *)txq, struct ieee80211_txq, drv_priv);

    for (; max > 0; max--) {
        skb = ecrnx_tx_dequeue_prep(ecrnx_hw, mac_txq);
        if (skb == NULL)
            return true;

        __skb_queue_tail(sk_list, skb);
    }

    /* re-read mac80211 txq current length.
       It is mainly for debug purpose to trace dropped packet. There is no
       problems to have nb_ready_mac80211 != actual mac80211 txq length */
    ieee80211_txq_get_depth(mac_txq, &mac_txq_len, NULL);
    if (txq->nb_ready_mac80211 > mac_txq_len)
        trace_txq_drop(txq, txq->nb_ready_mac80211 - mac_txq_len);
    txq->nb_ready_mac80211 = mac_txq_len;

    return (txq->nb_ready_mac80211 == 0);
}
#endif

/**
 * ecrnx_txq_get_skb_to_push - Get list of buffer to push for one txq
 *
 * @ecrnx_hw: main driver data
 * @hwq: HWQ on wich buffers will be pushed
 * @txq: TXQ to get buffers from
 * @user: user postion to use
 * @sk_list_push: list to update
 *
 *
 * This function will returned a list of buffer to push for one txq.
 * It will take into account the number of credit of the HWQ for this user
 * position and TXQ (and push_limit).
 * This allow to get a list that can be pushed without having to test for
 * hwq/txq status after each push
 *
 * If a MU group has been selected for this txq, it will also update the
 * counter for the group
 *
 * @return true if txq no longer have buffer ready after the ones returned.
 *         false otherwise
 */
static
bool ecrnx_txq_get_skb_to_push(struct ecrnx_hw *ecrnx_hw, struct ecrnx_hwq *hwq,
                              struct ecrnx_txq *txq, int user,
                              struct sk_buff_head *sk_list_push)
{
    int nb_ready = skb_queue_len(&txq->sk_list);
    int credits = min_t(int, ecrnx_txq_get_credits(txq), hwq->credits[user]);
    bool res = false;

    __skb_queue_head_init(sk_list_push);

    if (credits >= nb_ready) {
        skb_queue_splice_init(&txq->sk_list, sk_list_push);
#ifdef CONFIG_MAC80211_TXQ
        res = ecrnx_txq_mac80211_dequeue(ecrnx_hw, sk_list_push, txq, credits - nb_ready);
        credits = skb_queue_len(sk_list_push);
#else
        res = true;
        credits = nb_ready;
#endif
    } else {
        skb_queue_extract(&txq->sk_list, sk_list_push, credits);

        /* When processing PS service period (i.e. push_limit != 0), no longer
           process this txq if the buffers extracted will complete the SP for
           this txq */
        if (txq->push_limit && (credits == txq->push_limit))
            res = true;
    }

    ecrnx_mu_set_active_sta(ecrnx_hw, ecrnx_txq_2_sta(txq), credits);

    return res;
}

/**
 * ecrnx_txq_select_user - Select User queue for a txq
 *
 * @ecrnx_hw: main driver data
 * @mu_lock: true is MU lock is taken
 * @txq: TXQ to select MU group for
 * @hwq: HWQ for the TXQ
 * @user: Updated with user position selected
 *
 * @return false if it is no possible to process this txq.
 *         true otherwise
 *
 * This function selects the MU group to use for a TXQ.
 * The selection is done as follow:
 *
 * - return immediately for STA that don't belongs to any group and select
 *   group 0 / user 0
 *
 * - If MU tx is disabled (by user mutx_on, or because mu group are being
 *   updated !mu_lock), select group 0 / user 0
 *
 * - Use the best group selected by @ecrnx_mu_group_sta_select.
 *
 *   Each time a group is selected (except for the first case where sta
 *   doesn't belongs to a MU group), the function checks that no buffer is
 *   pending for this txq on another user position. If this is the case stop
 *   the txq (ECRNX_TXQ_STOP_MU_POS) and return false.
 *
 */
static
bool ecrnx_txq_select_user(struct ecrnx_hw *ecrnx_hw, bool mu_lock,
                          struct ecrnx_txq *txq, struct ecrnx_hwq *hwq, int *user)
{
    int pos = 0;
#ifdef CONFIG_ECRNX_MUMIMO_TX
    int id, group_id = 0;
    struct ecrnx_sta *sta = ecrnx_txq_2_sta(txq);

    /* for sta that belong to no group return immediately */
    if (!sta || !sta->group_info.cnt)
        goto end;

    /* If MU is disabled, need to check user */
    if (!ecrnx_hw->mod_params->mutx_on || !mu_lock)
        goto check_user;

    /* Use the "best" group selected */
    group_id = sta->group_info.group;

    if (group_id > 0)
        pos = ecrnx_mu_group_sta_get_pos(ecrnx_hw, sta, group_id);

  check_user:
    /* check that we can push on this user position */
#if CONFIG_USER_MAX == 2
    id = (pos + 1) & 0x1;
    if (txq->pkt_pushed[id]) {
        ecrnx_txq_stop(txq, ECRNX_TXQ_STOP_MU_POS);
        ECRNX_DBG("%s-%d:ecrnx_txq_stop,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_MU_POS);
        return false;
    }

#else
    for (id = 0 ; id < CONFIG_USER_MAX ; id++) {
        if (id != pos && txq->pkt_pushed[id]) {
            ecrnx_txq_stop(txq, ECRNX_TXQ_STOP_MU_POS);
            ECRNX_DBG("%s-%d:ecrnx_txq_stop,reaosn:0x%x \n", __func__, __LINE__, ECRNX_TXQ_STOP_MU_POS);
            return false;
        }
    }
#endif

  end:
    ecrnx_txq_set_mu_info(ecrnx_hw, txq, group_id, pos);
#endif /* CONFIG_ECRNX_MUMIMO_TX */

    *user = pos;
    return true;
}


/**
 * ecrnx_hwq_process - Process one HW queue list
 *
 * @ecrnx_hw: Driver main data
 * @hw_queue: HW queue index to process
 *
 * The function will iterate over all the TX queues linked in this HW queue
 * list. For each TX queue, push as many buffers as possible in the HW queue.
 * (NB: TX queue have at least 1 buffer, otherwise it wouldn't be in the list)
 * - If TX queue no longer have buffer, remove it from the list and check next
 *   TX queue
 * - If TX queue no longer have credits or has a push_limit (PS mode) and it
 *   is reached , remove it from the list and check next TX queue
 * - If HW queue is full, update list head to start with the next TX queue on
 *   next call if current TX queue already pushed "too many" pkt in a row, and
 *   return
 *
 * To be called when HW queue list is modified:
 * - when a buffer is pushed on a TX queue
 * - when new credits are received
 * - when a STA returns from Power Save mode or receives traffic request.
 * - when Channel context change
 *
 * To be called with tx_lock hold
 */
#define ALL_HWQ_MASK  ((1 << CONFIG_USER_MAX) - 1)

void ecrnx_hwq_process(struct ecrnx_hw *ecrnx_hw, struct ecrnx_hwq *hwq)
{
    struct ecrnx_txq *txq, *next;
    int user, credit_map = 0;
    bool mu_enable;

    trace_process_hw_queue(hwq);

    hwq->need_processing = false;

    mu_enable = ecrnx_txq_take_mu_lock(ecrnx_hw);
    if (!mu_enable)
        credit_map = ALL_HWQ_MASK - 1;

    list_for_each_entry_safe(txq, next, &hwq->list, sched_list) {
        struct ecrnx_txhdr *txhdr = NULL;
        struct sk_buff_head sk_list_push;
        struct sk_buff *skb;
        bool txq_empty;

        trace_process_txq(txq);

        /* sanity check for debug */
        BUG_ON(!(txq->status & ECRNX_TXQ_IN_HWQ_LIST));
        BUG_ON(txq->idx == TXQ_INACTIVE);
        BUG_ON(txq->credits <= 0);
        BUG_ON(!ecrnx_txq_skb_ready(txq));

        if (!ecrnx_txq_select_user(ecrnx_hw, mu_enable, txq, hwq, &user))
            continue;

        if (!hwq->credits[user]) {
            credit_map |= BIT(user);
            if (credit_map == ALL_HWQ_MASK)
                break;
            continue;
        }

        txq_empty = ecrnx_txq_get_skb_to_push(ecrnx_hw, hwq, txq, user,
                                             &sk_list_push);

        while ((skb = __skb_dequeue(&sk_list_push)) != NULL) {
            txhdr = (struct ecrnx_txhdr *)skb->data;
            ecrnx_tx_push(ecrnx_hw, txhdr, 0);
        }

        if (txq_empty) {
            ecrnx_txq_del_from_hw_list(txq);
            txq->pkt_sent = 0;
#if defined CONFIG_ECRNX_SOFTMAC && defined CONFIG_ECRNX_AMSDUS_TX
            if (txq->amsdu_ht_len_cap)
                ieee80211_amsdu_ctl(ecrnx_hw->hw, txq->sta, txq->tid, NULL,
                                    0, 0, false);
#endif
        } else if ((hwq->credits[user] == 0) &&
                   ecrnx_txq_is_scheduled(txq)) {
            /* txq not empty,
               - To avoid starving need to process other txq in the list
               - For better aggregation, need to send "as many consecutive
               pkt as possible" for he same txq
               ==> Add counter to trigger txq switch
            */
            if (txq->pkt_sent > hwq->size) {
                txq->pkt_sent = 0;
                list_rotate_left(&hwq->list);
            }
        }

#ifdef CONFIG_ECRNX_FULLMAC
        /* Unable to complete PS traffic request because of hwq credit */
        if (txq->push_limit && txq->sta) {
            if (txq->ps_id == LEGACY_PS_ID) {
                /* for legacy PS abort SP and wait next ps-poll */
                txq->sta->ps.sp_cnt[txq->ps_id] -= txq->push_limit;
                txq->push_limit = 0;
            }
            /* for u-apsd need to complete the SP to send EOSP frame */
        }

        /* restart netdev queue if number of queued buffer is below threshold */
        if (unlikely(txq->status & ECRNX_TXQ_NDEV_FLOW_CTRL) &&
            skb_queue_len(&txq->sk_list) < ECRNX_NDEV_FLOW_CTRL_RESTART) {
            txq->status &= ~ECRNX_TXQ_NDEV_FLOW_CTRL;
            netif_wake_subqueue(txq->ndev, txq->ndev_idx);
            trace_txq_flowctrl_restart(txq);
        }
#endif /* CONFIG_ECRNX_FULLMAC */

    }

#ifdef CONFIG_ECRNX_SOFTMAC
    if (hwq->len < hwq->len_start &&
        ieee80211_queue_stopped(ecrnx_hw->hw, hwq->id)) {
        trace_hwq_flowctrl_start(hwq->id);
        ieee80211_wake_queue(ecrnx_hw->hw, hwq->id);
    }
#endif /* CONFIG_ECRNX_SOFTMAC */

    if (mu_enable)
        ecrnx_txq_release_mu_lock(ecrnx_hw);
}

/**
 * ecrnx_hwq_process_all - Process all HW queue list
 *
 * @ecrnx_hw: Driver main data
 *
 * Loop over all HWQ, and process them if needed
 * To be called with tx_lock hold
 */
void ecrnx_hwq_process_all(struct ecrnx_hw *ecrnx_hw)
{
    int id;

    ecrnx_mu_group_sta_select(ecrnx_hw);

    for (id = ARRAY_SIZE(ecrnx_hw->hwq) - 1; id >= 0 ; id--) {
        if (ecrnx_hw->hwq[id].need_processing) {
            ecrnx_hwq_process(ecrnx_hw, &ecrnx_hw->hwq[id]);
        }
    }
}

/**
 * ecrnx_hwq_init - Initialize all hwq structures
 *
 * @ecrnx_hw: Driver main data
 *
 */
void ecrnx_hwq_init(struct ecrnx_hw *ecrnx_hw)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(ecrnx_hw->hwq); i++) {
        struct ecrnx_hwq *hwq = &ecrnx_hw->hwq[i];

        for (j = 0 ; j < CONFIG_USER_MAX; j++)
            hwq->credits[j] = nx_txdesc_cnt[i];
        hwq->id = i;
        hwq->size = nx_txdesc_cnt[i];
        INIT_LIST_HEAD(&hwq->list);

#ifdef CONFIG_ECRNX_SOFTMAC
        hwq->len = 0;
        hwq->len_stop = nx_txdesc_cnt[i] * 2;
        hwq->len_start = hwq->len_stop / 4;
#endif
    }
}
