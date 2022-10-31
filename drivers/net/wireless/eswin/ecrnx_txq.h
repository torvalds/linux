/**
 ****************************************************************************************
 *
 * @file ecrnx_txq.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */
#ifndef _ECRNX_TXQ_H_
#define _ECRNX_TXQ_H_

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/ieee80211.h>

#ifdef CONFIG_ECRNX_SOFTMAC
#include <net/mac80211.h>
#include "ecrnx_baws.h"

/**
 * Softmac TXQ configuration
 *  - STA have one TXQ per TID
 *  - VIF have one TXQ per HW queue
 *
 * Txq mapping looks like
 * for NX_REMOTE_STA_MAX=10 and NX_VIRT_DEV_MAX=4
 *
 * | TXQ | VIF |   STA |  TID | HWQ |
 * |-----+-----+-------+------+-----|-
 * |   0 |     |     0 |    0 |   1 | 16 TXQ per STA
 * |   1 |     |     0 |    1 |   0 |
 * |   2 |     |     0 |    2 |   0 |
 * |   3 |     |     0 |    3 |   1 |
 * |   4 |     |     0 |    4 |   2 |
 * |   5 |     |     0 |    5 |   2 |
 * |   6 |     |     0 |    6 |   3 |
 * |   7 |     |     0 |    7 |   3 |
 * |   8 |     |     0 |    8 |   1 |
 * |  ...|     |       |      |     |
 * |  16 |     |     0 |   16 |   1 |
 * |-----+-----+-------+------+-----|-
 * | ... |     |       |      |     | same for all STAs
 * |-----+-----+-------+------+-----|-
 * | 160 |   0 |       |      |   0 | 5 TXQ per VIF
 * | ... |     |       |      |     |
 * | 164 |   0 |       |      |   4 |
 * |-----+-----+-------+------+-----|-
 * | ... |     |       |      |     | same for all VIFs
 * |-----+-----+-------+------+-----|-
 *
 * NOTE: When using CONFIG_MAC80211_TXQ only one TXQ is allocated by mac80211
 * for the VIF (associated to BE ac). To avoid too much differences with case
 * where TXQ are allocated by the driver the "missing" VIF TXQs are allocated
 * by the driver. Actually driver also allocates txq for BE (to avoid having
 * modify ac parameter to access the TXQ) but this one is never used.
 * Driver check if nb_ready_mac80211 field is equal to NOT_MAC80211_TXQ in
 * order to distinguish non mac80211 txq.
 * When the txq interface (.wake_tx_queue) is used only the TXQ
 * allocated by mac80211 will be used and thus BE access category will always
 * be used. When "VIF" frames needs to be pushed on different access category
 * mac80211 will use the tx interface (.tx) and in this case driver will select
 * the txq associated to the requested access category.
 */
#define NX_NB_TID_PER_STA IEEE80211_NUM_TIDS
#define NX_NB_TXQ_PER_STA NX_NB_TID_PER_STA
#define NX_NB_TXQ_PER_VIF NX_TXQ_CNT
#define NX_NB_TXQ ((NX_NB_TXQ_PER_STA * NX_REMOTE_STA_MAX) +    \
                   (NX_NB_TXQ_PER_VIF * NX_VIRT_DEV_MAX))

#define NX_FIRST_VIF_TXQ_IDX (NX_REMOTE_STA_MAX * NX_NB_TXQ_PER_STA)

#define NOT_MAC80211_TXQ ULONG_MAX

#else /* i.e. #ifdef CONFIG_ECRNX_FULLMAC */
/**
 * Fullmac TXQ configuration:
 *  - STA: 1 TXQ per TID (limited to 8)
 *         1 TXQ for bufferable MGT frames
 *  - VIF: 1 TXQ for Multi/Broadcast +
 *         1 TXQ for MGT for unknown STAs or non-bufferable MGT frames
 *  - 1 TXQ for offchannel transmissions
 *
 *
 * Txq mapping looks like
 * for NX_REMOTE_STA_MAX=10 and NX_VIRT_DEV_MAX=4
 *
 * | TXQ | NDEV_ID | VIF |   STA |  TID | HWQ |
 * |-----+---------+-----+-------+------+-----|-
 * |   0 |       0 |     |     0 |    0 |   1 | 9 TXQ per STA
 * |   1 |       1 |     |     0 |    1 |   0 | (8 data + 1 mgmt)
 * |   2 |       2 |     |     0 |    2 |   0 |
 * |   3 |       3 |     |     0 |    3 |   1 |
 * |   4 |       4 |     |     0 |    4 |   2 |
 * |   5 |       5 |     |     0 |    5 |   2 |
 * |   6 |       6 |     |     0 |    6 |   3 |
 * |   7 |       7 |     |     0 |    7 |   3 |
 * |   8 |     N/A |     |     0 | MGMT |   3 |
 * |-----+---------+-----+-------+------+-----|-
 * | ... |         |     |       |      |     | Same for all STAs
 * |-----+---------+-----+-------+------+-----|-
 * |  90 |      80 |   0 | BC/MC |    0 | 1/4 | 1 TXQ for BC/MC per VIF
 * | ... |         |     |       |      |     |
 * |  93 |      80 |   3 | BC/MC |    0 | 1/4 |
 * |-----+---------+-----+-------+------+-----|-
 * |  94 |     N/A |   0 |   N/A | MGMT |   3 | 1 TXQ for unknown STA per VIF
 * | ... |         |     |       |      |     |
 * |  97 |     N/A |   3 |   N/A | MGMT |   3 |
 * |-----+---------+-----+-------+------+-----|-
 * |  98 |     N/A |     |   N/A | MGMT |   3 | 1 TXQ for offchannel frame
 */
#define NX_NB_TID_PER_STA 8
#define NX_NB_TXQ_PER_STA (NX_NB_TID_PER_STA + 1)
#define NX_NB_TXQ_PER_VIF 2
#define NX_NB_TXQ ((NX_NB_TXQ_PER_STA * NX_REMOTE_STA_MAX) +    \
                   (NX_NB_TXQ_PER_VIF * NX_VIRT_DEV_MAX) + 1)

#define NX_FIRST_VIF_TXQ_IDX (NX_REMOTE_STA_MAX * NX_NB_TXQ_PER_STA)
#define NX_FIRST_BCMC_TXQ_IDX  NX_FIRST_VIF_TXQ_IDX
#define NX_FIRST_UNK_TXQ_IDX  (NX_FIRST_BCMC_TXQ_IDX + NX_VIRT_DEV_MAX)

#define NX_OFF_CHAN_TXQ_IDX (NX_FIRST_VIF_TXQ_IDX +                     \
                             (NX_VIRT_DEV_MAX * NX_NB_TXQ_PER_VIF))
#define NX_BCMC_TXQ_TYPE 0
#define NX_UNK_TXQ_TYPE  1

/**
 * Each data TXQ is a netdev queue. TXQ to send MGT are not data TXQ as
 * they did not recieved buffer from netdev interface.
 * Need to allocate the maximum case.
 * AP : all STAs + 1 BC/MC
 */
#define NX_NB_NDEV_TXQ ((NX_NB_TID_PER_STA * NX_REMOTE_STA_MAX) + 1 )
#define NX_BCMC_TXQ_NDEV_IDX (NX_NB_TID_PER_STA * NX_REMOTE_STA_MAX)
#define NX_STA_NDEV_IDX(tid, sta_idx) ((tid) + (sta_idx) * NX_NB_TID_PER_STA)
#define NDEV_NO_TXQ 0xffff
#if (NX_NB_NDEV_TXQ >= NDEV_NO_TXQ)
#error("Need to increase struct ecrnx_txq->ndev_idx size")
#endif

/* stop netdev queue when number of queued buffers if greater than this  */
#define ECRNX_NDEV_FLOW_CTRL_STOP    200
/* restart netdev queue when number of queued buffers is lower than this */
#define ECRNX_NDEV_FLOW_CTRL_RESTART 100

#endif /*  CONFIG_ECRNX_SOFTMAC */

#define TXQ_INACTIVE 0xffff
#if (NX_NB_TXQ >= TXQ_INACTIVE)
#error("Need to increase struct ecrnx_txq->idx size")
#endif

#define NX_TXQ_INITIAL_CREDITS 20 //4

#define ECRNX_TXQ_CLEANUP_INTERVAL (10 * HZ) //10s in jiffies
#define ECRNX_TXQ_MAX_QUEUE_JIFFIES (20 * HZ)
/**
 * TXQ tid sorted by decreasing priority
 */
extern const int nx_tid_prio[NX_NB_TID_PER_STA];

/**
 * struct ecrnx_hwq - Structure used to save information relative to
 *                   an AC TX queue (aka HW queue)
 * @list: List of TXQ, that have buffers ready for this HWQ
 * @credits: available credit for the queue (i.e. nb of buffers that
 *           can be pushed to FW )
 * @id Id of the HWQ among ECRNX_HWQ_....
 * @size size of the queue
 * @need_processing Indicate if hwq should be processed
 * @len number of packet ready to be pushed to fw for this HW queue
 * @len_stop threshold to stop mac80211(i.e. netdev) queues. Stop queue when
 *           driver has more than @len_stop packets ready.
 * @len_start threshold to wake mac8011 queues. Wake queue when driver has
 *            less than @len_start packets ready.
 */
struct ecrnx_hwq {
    struct list_head list;
    u8 credits[CONFIG_USER_MAX];
    u8 size;
    u8 id;
    bool need_processing;
#ifdef CONFIG_ECRNX_SOFTMAC
    u8 len;
    u8 len_stop;
    u8 len_start;
#endif /* CONFIG_ECRNX_SOFTMAC */
};

/**
 * enum ecrnx_push_flags - Flags of pushed buffer
 *
 * @ECRNX_PUSH_RETRY Pushing a buffer for retry
 * @ECRNX_PUSH_IMMEDIATE Pushing a buffer without queuing it first
 */
enum ecrnx_push_flags {
    ECRNX_PUSH_RETRY  = BIT(0),
    ECRNX_PUSH_IMMEDIATE = BIT(1),
};

/**
 * enum ecrnx_txq_flags - TXQ status flag
 *
 * @ECRNX_TXQ_IN_HWQ_LIST: The queue is scheduled for transmission
 * @ECRNX_TXQ_STOP_FULL: No more credits for the queue
 * @ECRNX_TXQ_STOP_CSA: CSA is in progress
 * @ECRNX_TXQ_STOP_STA_PS: Destiniation sta is currently in power save mode
 * @ECRNX_TXQ_STOP_VIF_PS: Vif owning this queue is currently in power save mode
 * @ECRNX_TXQ_STOP_CHAN: Channel of this queue is not the current active channel
 * @ECRNX_TXQ_STOP_MU_POS: TXQ is stopped waiting for all the buffers pushed to
 *                       fw to be confirmed
 * @ECRNX_TXQ_STOP: All possible reason to have a txq stopped
 * @ECRNX_TXQ_NDEV_FLOW_CTRL: associated netdev queue is currently stopped.
 *                          Note: when a TXQ is flowctrl it is NOT stopped
 */
enum ecrnx_txq_flags {
    ECRNX_TXQ_IN_HWQ_LIST  = BIT(0),
    ECRNX_TXQ_STOP_FULL    = BIT(1),
    ECRNX_TXQ_STOP_CSA     = BIT(2),
    ECRNX_TXQ_STOP_STA_PS  = BIT(3),
    ECRNX_TXQ_STOP_VIF_PS  = BIT(4),
    ECRNX_TXQ_STOP_CHAN    = BIT(5),
    ECRNX_TXQ_STOP_MU_POS  = BIT(6),
    ECRNX_TXQ_STOP         = (ECRNX_TXQ_STOP_FULL | ECRNX_TXQ_STOP_CSA |
                             ECRNX_TXQ_STOP_STA_PS | ECRNX_TXQ_STOP_VIF_PS |
                             ECRNX_TXQ_STOP_CHAN) ,
    ECRNX_TXQ_NDEV_FLOW_CTRL = BIT(7),
};


/**
 * struct ecrnx_txq - Structure used to save information relative to
 *                   a RA/TID TX queue
 *
 * @idx: Unique txq idx. Set to TXQ_INACTIVE if txq is not used.
 * @status: bitfield of @ecrnx_txq_flags.
 * @credits: available credit for the queue (i.e. nb of buffers that
 *           can be pushed to FW).
 * @pkt_sent: number of consecutive pkt sent without leaving HW queue list
 * @pkt_pushed: number of pkt currently pending for transmission confirmation
 * @sched_list: list node for HW queue schedule list (ecrnx_hwq.list)
 * @sk_list: list of buffers to push to fw
 * @last_retry_skb: pointer on the last skb in @sk_list that is a retry.
 *                  (retry skb are stored at the beginning of the list)
 *                  NULL if no retry skb is queued in @sk_list
 * @nb_retry: Number of retry packet queued.
 * @hwq: Pointer on the associated HW queue.
 * @push_limit: number of packet to push before removing the txq from hwq list.
 *              (we always have push_limit < skb_queue_len(sk_list))
 * @tid: TID
 *
 * SOFTMAC specific:
 * @baw: Block Ack window information
 * @amsdu_anchor: pointer to ecrnx_sw_txhdr of the first subframe of the A-MSDU.
 *                NULL if no A-MSDU frame is in construction
 * @amsdu_ht_len_cap:
 * @amsdu_vht_len_cap:
 * @nb_ready_mac80211: Number of buffer ready in mac80211 txq
 *
 * FULLMAC specific
 * @ps_id: Index to use for Power save mode (LEGACY or UAPSD)
 * @ndev_idx: txq idx from netdev point of view (0xFF for non netdev queue)
 * @ndev: pointer to ndev of the corresponding vif
 * @amsdu: pointer to ecrnx_sw_txhdr of the first subframe of the A-MSDU.
 *         NULL if no A-MSDU frame is in construction
 * @amsdu_len: Maximum size allowed for an A-MSDU. 0 means A-MSDU not allowed
 */
struct ecrnx_txq {
    u16 idx;
    u8 status;
    s8 credits;
    u8 pkt_sent;
    u8 pkt_pushed[CONFIG_USER_MAX];
    struct list_head sched_list;
    struct sk_buff_head sk_list;
    struct sk_buff *last_retry_skb;
    struct ecrnx_hwq *hwq;
    int nb_retry;
    u8 push_limit;
    u8 tid;
#ifdef CONFIG_MAC80211_TXQ
    unsigned long nb_ready_mac80211;
#endif
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_baw baw;
    struct ieee80211_sta *sta;
#ifdef CONFIG_ECRNX_AMSDUS_TX
    struct ecrnx_sw_txhdr *amsdu_anchor;
    u16 amsdu_ht_len_cap;
    u16 amsdu_vht_len_cap;
#endif /* CONFIG_ECRNX_AMSDUS_TX */
#else /* ! CONFIG_ECRNX_SOFTMAC */
    struct ecrnx_sta *sta;
    u8 ps_id;
    u16 ndev_idx;
    struct net_device *ndev;
#ifdef CONFIG_ECRNX_AMSDUS_TX
    struct ecrnx_sw_txhdr *amsdu;
    u16 amsdu_len;
#endif /* CONFIG_ECRNX_AMSDUS_TX */
#endif /* CONFIG_ECRNX_SOFTMAC */
#ifdef CONFIG_ECRNX_MUMIMO_TX
    u8 mumimo_info;
#endif
};

struct ecrnx_sta;
struct ecrnx_vif;
struct ecrnx_hw;
struct ecrnx_sw_txhdr;

#ifdef CONFIG_ECRNX_MUMIMO_TX
#define ECRNX_TXQ_GROUP_ID(txq) ((txq)->mumimo_info & 0x3f)
#define ECRNX_TXQ_POS_ID(txq)   (((txq)->mumimo_info >> 6) & 0x3)
#else
#define ECRNX_TXQ_GROUP_ID(txq) 0
#define ECRNX_TXQ_POS_ID(txq)   0
#endif /* CONFIG_ECRNX_MUMIMO_TX */

static inline bool ecrnx_txq_is_stopped(struct ecrnx_txq *txq)
{
    return (txq->status & ECRNX_TXQ_STOP);
}

static inline bool ecrnx_txq_is_full(struct ecrnx_txq *txq)
{
    return (txq->status & ECRNX_TXQ_STOP_FULL);
}

static inline bool ecrnx_txq_is_scheduled(struct ecrnx_txq *txq)
{
    return (txq->status & ECRNX_TXQ_IN_HWQ_LIST);
}

/**
 * ecrnx_txq_is_ready_for_push - Check if a TXQ is ready for push
 *
 * @txq: txq pointer
 *
 * if
 * - txq is not stopped
 * - and hwq has credits
 * - and there is no buffer queued
 * then a buffer can be immediately pushed without having to queue it first
 * @return: true if the 3 conditions are met and false otherwise.
 */
static inline bool ecrnx_txq_is_ready_for_push(struct ecrnx_txq *txq)
{
    return (!ecrnx_txq_is_stopped(txq) &&
            txq->hwq->credits[ECRNX_TXQ_POS_ID(txq)] > 0 &&
            skb_queue_empty(&txq->sk_list));
}

/**
 * foreach_sta_txq - Macro to iterate over all TXQ of a STA in increasing
 *                   TID order
 *
 * @sta: pointer to ecrnx_sta
 * @txq: pointer to ecrnx_txq updated with the next TXQ at each iteration
 * @tid: int updated with the TXQ tid at each iteration
 * @ecrnx_hw: main driver data
 */
#ifdef CONFIG_MAC80211_TXQ
#define foreach_sta_txq(sta, txq, tid, ecrnx_hw)                         \
    for (tid = 0, txq = ecrnx_txq_sta_get(sta, 0);                       \
         tid < NX_NB_TXQ_PER_STA;                                       \
         tid++, txq = ecrnx_txq_sta_get(sta, tid))

#elif defined(CONFIG_ECRNX_SOFTMAC)
#define foreach_sta_txq(sta, txq, tid, ecrnx_hw)                         \
    for (tid = 0, txq = &sta->txqs[0];                                  \
         tid < NX_NB_TXQ_PER_STA;                                       \
         tid++, txq++)

#else /* CONFIG_ECRNX_FULLMAC */
#define foreach_sta_txq(sta, txq, tid, ecrnx_hw)                          \
    for (tid = 0, txq = ecrnx_txq_sta_get(sta, 0, ecrnx_hw);               \
         tid < (is_multicast_sta(sta->sta_idx) ? 1 : NX_NB_TXQ_PER_STA); \
         tid++, txq++)

#endif

/**
 * foreach_sta_txq_prio - Macro to iterate over all TXQ of a STA in
 *                        decreasing priority order
 *
 * @sta: pointer to ecrnx_sta
 * @txq: pointer to ecrnx_txq updated with the next TXQ at each iteration
 * @tid: int updated with the TXQ tid at each iteration
 * @i: int updated with ieration count
 * @ecrnx_hw: main driver data
 *
 * Note: For fullmac txq for mgmt frame is skipped
 */
#ifdef CONFIG_ECRNX_SOFTMAC
#define foreach_sta_txq_prio(sta, txq, tid, i, ecrnx_hw)                 \
    for (i = 0, tid = nx_tid_prio[0], txq = ecrnx_txq_sta_get(sta, tid); \
         i < NX_NB_TID_PER_STA;                                         \
         i++, tid = nx_tid_prio[i], txq = ecrnx_txq_sta_get(sta, tid))
#else /* CONFIG_ECRNX_FULLMAC */
#define foreach_sta_txq_prio(sta, txq, tid, i, ecrnx_hw)                          \
    for (i = 0, tid = nx_tid_prio[0], txq = ecrnx_txq_sta_get(sta, tid, ecrnx_hw); \
         i < NX_NB_TID_PER_STA;                                                  \
         i++, tid = nx_tid_prio[i], txq = ecrnx_txq_sta_get(sta, tid, ecrnx_hw))
#endif

/**
 * foreach_vif_txq - Macro to iterate over all TXQ of a VIF (in AC order)
 *
 * @vif: pointer to ecrnx_vif
 * @txq: pointer to ecrnx_txq updated with the next TXQ at each iteration
 * @ac:  int updated with the TXQ ac at each iteration
 */
#ifdef CONFIG_MAC80211_TXQ
#define foreach_vif_txq(vif, txq, ac)                                   \
    for (ac = ECRNX_HWQ_BK, txq = ecrnx_txq_vif_get(vif, ac);             \
         ac < NX_NB_TXQ_PER_VIF;                                        \
         ac++, txq = ecrnx_txq_vif_get(vif, ac))

#else
#define foreach_vif_txq(vif, txq, ac)                                   \
    for (ac = ECRNX_HWQ_BK, txq = &vif->txqs[0];                         \
         ac < NX_NB_TXQ_PER_VIF;                                        \
         ac++, txq++)
#endif

#ifdef CONFIG_ECRNX_SOFTMAC
struct ecrnx_txq *ecrnx_txq_sta_get(struct ecrnx_sta *sta, u8 tid);
struct ecrnx_txq *ecrnx_txq_vif_get(struct ecrnx_vif *vif, u8 ac);
#else
struct ecrnx_txq *ecrnx_txq_sta_get(struct ecrnx_sta *sta, u8 tid,
                                  struct ecrnx_hw * ecrnx_hw);
struct ecrnx_txq *ecrnx_txq_vif_get(struct ecrnx_vif *vif, u8 type);
#endif /* CONFIG_ECRNX_SOFTMAC */

/**
 * ecrnx_txq_vif_get_status - return status bits related to the vif
 *
 * @ecrnx_vif: Pointer to vif structure
 */
static inline u8 ecrnx_txq_vif_get_status(struct ecrnx_vif *ecrnx_vif)
{
    struct ecrnx_txq *txq = ecrnx_txq_vif_get(ecrnx_vif, 0);
    return (txq->status & (ECRNX_TXQ_STOP_CHAN | ECRNX_TXQ_STOP_VIF_PS));
}

void ecrnx_txq_vif_init(struct ecrnx_hw * ecrnx_hw, struct ecrnx_vif *vif,
                       u8 status);
void ecrnx_txq_vif_deinit(struct ecrnx_hw * ecrnx_hw, struct ecrnx_vif *vif);
void ecrnx_txq_sta_init(struct ecrnx_hw * ecrnx_hw, struct ecrnx_sta *ecrnx_sta,
                       u8 status);
void ecrnx_txq_sta_deinit(struct ecrnx_hw * ecrnx_hw, struct ecrnx_sta *ecrnx_sta);
#ifdef CONFIG_ECRNX_FULLMAC
void ecrnx_txq_unk_vif_init(struct ecrnx_vif *ecrnx_vif);
void ecrnx_txq_unk_vif_deinit(struct ecrnx_vif *vif);
void ecrnx_txq_offchan_init(struct ecrnx_vif *ecrnx_vif);
void ecrnx_txq_offchan_deinit(struct ecrnx_vif *ecrnx_vif);
void ecrnx_txq_tdls_vif_init(struct ecrnx_vif *ecrnx_vif);
void ecrnx_txq_tdls_vif_deinit(struct ecrnx_vif *vif);
void ecrnx_txq_tdls_sta_start(struct ecrnx_vif *ecrnx_vif, u16 reason,
                             struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_tdls_sta_stop(struct ecrnx_vif *ecrnx_vif, u16 reason,
                            struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_prepare(struct ecrnx_hw *ecrnx_hw);
#endif


void ecrnx_txq_add_to_hw_list(struct ecrnx_txq *txq);
void ecrnx_txq_del_from_hw_list(struct ecrnx_txq *txq);
void ecrnx_txq_stop(struct ecrnx_txq *txq, u16 reason);
void ecrnx_txq_start(struct ecrnx_txq *txq, u16 reason);
void ecrnx_txq_vif_start(struct ecrnx_vif *vif, u16 reason,
                        struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_vif_stop(struct ecrnx_vif *vif, u16 reason,
                       struct ecrnx_hw *ecrnx_hw);

#ifdef CONFIG_ECRNX_SOFTMAC
void ecrnx_txq_sta_start(struct ecrnx_sta *sta, u16 reason);
void ecrnx_txq_sta_stop(struct ecrnx_sta *sta, u16 reason);
void ecrnx_txq_tdls_sta_start(struct ecrnx_sta *ecrnx_sta, u16 reason,
                             struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_tdls_sta_stop(struct ecrnx_sta *ecrnx_sta, u16 reason,
                            struct ecrnx_hw *ecrnx_hw);
#else
void ecrnx_txq_sta_start(struct ecrnx_sta *sta, u16 reason,
                        struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_sta_stop(struct ecrnx_sta *sta, u16 reason,
                       struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_offchan_start(struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_sta_switch_vif(struct ecrnx_sta *sta, struct ecrnx_vif *old_vif,
                             struct ecrnx_vif *new_vif);

#endif /* CONFIG_ECRNX_SOFTMAC */

int ecrnx_txq_queue_skb(struct sk_buff *skb, struct ecrnx_txq *txq,
                       struct ecrnx_hw *ecrnx_hw,  bool retry,
                       struct sk_buff *skb_prev);
void ecrnx_txq_confirm_any(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txq *txq,
                          struct ecrnx_hwq *hwq, struct ecrnx_sw_txhdr *sw_txhdr);
void ecrnx_txq_drop_skb(struct ecrnx_txq *txq,  struct sk_buff *skb, struct ecrnx_hw *ecrnx_hw, bool retry_packet);

void ecrnx_hwq_init(struct ecrnx_hw *ecrnx_hw);
void ecrnx_hwq_process(struct ecrnx_hw *ecrnx_hw, struct ecrnx_hwq *hwq);
void ecrnx_hwq_process_all(struct ecrnx_hw *ecrnx_hw);

#endif /* _ECRNX_TXQ_H_ */
