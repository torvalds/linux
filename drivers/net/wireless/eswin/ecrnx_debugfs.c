/**
 ******************************************************************************
 *
 * @file ecrnx_debugfs.c
 *
 * @brief Definition of debugfs entries
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */


#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/debugfs.h>
#include <linux/string.h>
#include <linux/sort.h>

#include "ecrnx_debugfs.h"
#include "ecrnx_msg_tx.h"
#include "ecrnx_radar.h"
#include "ecrnx_tx.h"

#define CONFIG_ECRNX_DBGFS_FW_TRACE 0

#ifdef CONFIG_ECRNX_SOFTMAC
static ssize_t ecrnx_dbgfs_stats_read(struct file *file,
                                     char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char *buf;
    int per;
    int ret;
    int i, skipped;
    ssize_t read;
    int bufsz = (10 + NX_TX_PAYLOAD_MAX + NX_TXQ_CNT + IEEE80211_MAX_AMPDU_BUF) * 50;

    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    if (priv->stats.agg_done)
        per = DIV_ROUND_UP((priv->stats.agg_retries + priv->stats.agg_died) *
                           100, priv->stats.agg_done);
    else
        per = 0;

    ret = scnprintf(buf, min_t(size_t, bufsz - 1, count),
                    "agg_done         %10d\n"
                    "agg_retries      %10d\n"
                    "agg_retries_last %10d\n"
                    "agg_died         %10d\n"
                    "ampdu_all_ko     %10d\n"
                    "agg_PER (%%)      %10d\n"
                    "queues_stops     %10d\n\n",
                    priv->stats.agg_done,
                    priv->stats.agg_retries,
                    priv->stats.agg_retries_last,
                    priv->stats.agg_died,
                    priv->stats.ampdu_all_ko,
                    per,
                    priv->stats.queues_stops);

    ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
                     "TXQs CFM balances ");
    for (i = 0; i < NX_TXQ_CNT; i++)
        ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
                         "  [%1d]:%3d", i,
                         priv->stats.cfm_balance[i]);

#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
                     "\n\nAMSDU[len]             done   failed(%%)\n");
    for (i = skipped = 0; i < NX_TX_PAYLOAD_MAX; i++) {
        if (priv->stats.amsdus[i].done) {
            per = DIV_ROUND_UP((priv->stats.amsdus[i].failed) *
                               100, priv->stats.amsdus[i].done);
        } else {
            per = 0;
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
                             "   * * *         %10d  %10d\n", 0, 0);
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
                         "   [%1d]           %10d  %10d\n", i + 1,
                         priv->stats.amsdus[i].done, per);
    }
    if (skipped)
        ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
                         "   * * *         %10d  %10d\n", 0, 0);
#endif

    ret += scnprintf(&buf[ret], min_t(size_t, bufsz - ret - 1, count - ret),
                     "\nIn-AMPDU     TX failures(%%)   RX counts\n");
    for (i = skipped = 0; i < IEEE80211_MAX_AMPDU_BUF; i++) {
        int failed;

        if (priv->stats.in_ampdu[i].done) {
            failed = DIV_ROUND_UP(priv->stats.in_ampdu[i].failed *
                                  100, priv->stats.in_ampdu[i].done);
        } else {
            if (!priv->stats.rx_in_ampdu[i].cnt) {
                skipped = 1;
                continue;
            }
            failed = 0;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret],
                             min_t(size_t, bufsz - ret - 1, count - ret),
                             "   * * *         %10d  %10d\n", 0, 0);
            skipped = 0;
        }
        ret += scnprintf(&buf[ret],
                         min_t(size_t, bufsz - ret - 1, count - ret),
                         "   mpdu#%2d       %10d  %10d\n", i, failed,
                         priv->stats.rx_in_ampdu[i].cnt);

    }
    if (skipped)
        ret += scnprintf(&buf[ret],
                         min_t(size_t, bufsz - ret - 1, count - ret),
                         "   * * *         %10d  %10d\n", 0, 0);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    kfree(buf);

    return read;
}

#else

static ssize_t ecrnx_dbgfs_stats_read(struct file *file,
                                     char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char *buf;
    int ret;
    int i, skipped;
    ssize_t read;
    int bufsz = (NX_TXQ_CNT) * 20 + (ARRAY_SIZE(priv->stats.amsdus_rx) + 1) * 40
        + (ARRAY_SIZE(priv->stats.ampdus_tx) * 30);

    if (*ppos)
        return 0;

    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    ret = scnprintf(buf, bufsz, "TXQs CFM balances ");
    for (i = 0; i < NX_TXQ_CNT; i++)
        ret += scnprintf(&buf[ret], bufsz - ret,
                         "  [%1d]:%3d", i,
                         priv->stats.cfm_balance[i]);

    ret += scnprintf(&buf[ret], bufsz - ret, "\n");

#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    int per = 0;
    ret += scnprintf(&buf[ret], bufsz - ret,
                     "\nAMSDU[len]       done         failed   received\n");
    for (i = skipped = 0; i < NX_TX_PAYLOAD_MAX; i++) {
        if (priv->stats.amsdus[i].done) {
            per = DIV_ROUND_UP((priv->stats.amsdus[i].failed) *
                               100, priv->stats.amsdus[i].done);
        } else if (priv->stats.amsdus_rx[i]) {
            per = 0;
        } else {
            per = 0;
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]    %10d %8d(%3d%%) %10d\n",  i ? i + 1 : i,
                         priv->stats.amsdus[i].done,
                         priv->stats.amsdus[i].failed, per,
                         priv->stats.amsdus_rx[i]);
    }

    for (; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
        if (!priv->stats.amsdus_rx[i]) {
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]                              %10d\n",
                         i + 1, priv->stats.amsdus_rx[i]);
    }
#else
    ret += scnprintf(&buf[ret], bufsz - ret,
                     "\nAMSDU[len]   received\n");
    for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
        if (!priv->stats.amsdus_rx[i]) {
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret,
                             "   ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]    %10d\n",
                         i + 1, priv->stats.amsdus_rx[i]);
    }

#endif /* CONFIG_ECRNX_SPLIT_TX_BUF */

    ret += scnprintf(&buf[ret], bufsz - ret,
                     "\nAMPDU[len]     done  received\n");
    for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.ampdus_tx); i++) {
        if (!priv->stats.ampdus_tx[i] && !priv->stats.ampdus_rx[i]) {
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret,
                             "    ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]   %9d %9d\n", i ? i + 1 : i,
                         priv->stats.ampdus_tx[i], priv->stats.ampdus_rx[i]);
    }

    ret += scnprintf(&buf[ret], bufsz - ret,
                     "#mpdu missed        %9d\n",
                     priv->stats.ampdus_rx_miss);

    ret += scnprintf(&buf[ret], bufsz - ret,
                     "\nmsg_tx:%d,%d; data_tx:%d,%d\n",
                     priv->msg_tx, priv->msg_tx_done, priv->data_tx, priv->data_tx_done);
    ret += scnprintf(&buf[ret], bufsz - ret,
                     "usb_rx:%d, data_rx:%d, msg_rx:%d\n",
                     priv->usb_rx, priv->data_rx, priv->msg_rx);
    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    kfree(buf);

    return read;
}
#endif /* CONFIG_ECRNX_SOFTMAC */

static ssize_t ecrnx_dbgfs_stats_write(struct file *file,
                                      const char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;

    /* Prevent from interrupt preemption as these statistics are updated under
     * interrupt */
    spin_lock_bh(&priv->tx_lock);

    memset(&priv->stats, 0, sizeof(priv->stats));

    spin_unlock_bh(&priv->tx_lock);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(stats);

#define TXQ_STA_PREF "tid|"
#define TXQ_STA_PREF_FMT "%3d|"

#ifdef CONFIG_ECRNX_FULLMAC
#define TXQ_VIF_PREF "type|"
#define TXQ_VIF_PREF_FMT "%4s|"
#else
#define TXQ_VIF_PREF "AC|"
#define TXQ_VIF_PREF_FMT "%2s|"
#endif /* CONFIG_ECRNX_FULLMAC */

#define TXQ_HDR "idx|  status|credit|ready|retry|pushed"
#define TXQ_HDR_FMT "%3d|%s%s%s%s%s%s%s%s|%6d|%5d|%5d|%6d"

#ifdef CONFIG_ECRNX_AMSDUS_TX
#ifdef CONFIG_ECRNX_FULLMAC
#define TXQ_HDR_SUFF "|amsdu"
#define TXQ_HDR_SUFF_FMT "|%5d"
#else
#define TXQ_HDR_SUFF "|amsdu-ht|amdsu-vht"
#define TXQ_HDR_SUFF_FMT "|%8d|%9d"
#endif /* CONFIG_ECRNX_FULLMAC */
#else
#define TXQ_HDR_SUFF ""
#define TXQ_HDR_SUF_FMT ""
#endif /* CONFIG_ECRNX_AMSDUS_TX */

#define TXQ_HDR_MAX_LEN (sizeof(TXQ_STA_PREF) + sizeof(TXQ_HDR) + sizeof(TXQ_HDR_SUFF) + 1)

#ifdef CONFIG_ECRNX_FULLMAC
#define PS_HDR  "Legacy PS: ready=%d, sp=%d / UAPSD: ready=%d, sp=%d"
#define PS_HDR_LEGACY "Legacy PS: ready=%d, sp=%d"
#define PS_HDR_UAPSD  "UAPSD: ready=%d, sp=%d"
#define PS_HDR_MAX_LEN  sizeof("Legacy PS: ready=xxx, sp=xxx / UAPSD: ready=xxx, sp=xxx\n")
#else
#define PS_HDR ""
#define PS_HDR_MAX_LEN 0
#endif /* CONFIG_ECRNX_FULLMAC */

#define STA_HDR "** STA %d (%pM)\n"
#define STA_HDR_MAX_LEN sizeof("- STA xx (xx:xx:xx:xx:xx:xx)\n") + PS_HDR_MAX_LEN

#ifdef CONFIG_ECRNX_FULLMAC
#define VIF_HDR "* VIF [%d] %s\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR) + IFNAMSIZ
#else
#define VIF_HDR "* VIF [%d]\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR)
#endif


#ifdef CONFIG_ECRNX_AMSDUS_TX

#ifdef CONFIG_ECRNX_FULLMAC
#define VIF_SEP "---------------------------------------\n"
#else
#define VIF_SEP "----------------------------------------------------\n"
#endif /* CONFIG_ECRNX_FULLMAC */

#else /* ! CONFIG_ECRNX_AMSDUS_TX */
#define VIF_SEP "---------------------------------\n"
#endif /* CONFIG_ECRNX_AMSDUS_TX*/

#define VIF_SEP_LEN sizeof(VIF_SEP)

#define CAPTION "status: L=in hwq list, F=stop full, P=stop sta PS, V=stop vif PS,\
 C=stop channel, S=stop CSA, M=stop MU, N=Ndev queue stopped"
#define CAPTION_LEN sizeof(CAPTION)

#define STA_TXQ 0
#define VIF_TXQ 1

static int ecrnx_dbgfs_txq(char *buf, size_t size, struct ecrnx_txq *txq, int type, int tid, char *name)
{
    int res, idx = 0;
    int i, pushed = 0;

    if (type == STA_TXQ) {
        res = scnprintf(&buf[idx], size, TXQ_STA_PREF_FMT, tid);
        idx += res;
        size -= res;
    } else {
        res = scnprintf(&buf[idx], size, TXQ_VIF_PREF_FMT, name);
        idx += res;
        size -= res;
    }

    for (i = 0; i < CONFIG_USER_MAX; i++) {
        pushed += txq->pkt_pushed[i];
    }

    res = scnprintf(&buf[idx], size, TXQ_HDR_FMT, txq->idx,
                    (txq->status & ECRNX_TXQ_IN_HWQ_LIST) ? "L" : " ",
                    (txq->status & ECRNX_TXQ_STOP_FULL) ? "F" : " ",
                    (txq->status & ECRNX_TXQ_STOP_STA_PS) ? "P" : " ",
                    (txq->status & ECRNX_TXQ_STOP_VIF_PS) ? "V" : " ",
                    (txq->status & ECRNX_TXQ_STOP_CHAN) ? "C" : " ",
                    (txq->status & ECRNX_TXQ_STOP_CSA) ? "S" : " ",
                    (txq->status & ECRNX_TXQ_STOP_MU_POS) ? "M" : " ",
                    (txq->status & ECRNX_TXQ_NDEV_FLOW_CTRL) ? "N" : " ",
                    txq->credits, skb_queue_len(&txq->sk_list),
                    txq->nb_retry, pushed);
    idx += res;
    size -= res;

#ifdef CONFIG_ECRNX_AMSDUS_TX
    if (type == STA_TXQ) {
        res = scnprintf(&buf[idx], size, TXQ_HDR_SUFF_FMT,
#ifdef CONFIG_ECRNX_FULLMAC
                        txq->amsdu_len
#else
                        txq->amsdu_ht_len_cap, txq->amsdu_vht_len_cap
#endif /* CONFIG_ECRNX_FULLMAC */
                        );
        idx += res;
        size -= res;
    }
#endif

    res = scnprintf(&buf[idx], size, "\n");
    idx += res;
    size -= res;

    return idx;
}

static int ecrnx_dbgfs_txq_sta(char *buf, size_t size, struct ecrnx_sta *ecrnx_sta,
                              struct ecrnx_hw *ecrnx_hw)
{
    int tid, res, idx = 0;
    struct ecrnx_txq *txq;
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ieee80211_sta *sta = ecrnx_to_ieee80211_sta(ecrnx_sta);
#endif /* CONFIG_ECRNX_SOFTMAC */

    res = scnprintf(&buf[idx], size, "\n" STA_HDR,
                    ecrnx_sta->sta_idx,
#ifdef CONFIG_ECRNX_SOFTMAC
                    sta->addr
#else
                    ecrnx_sta->mac_addr
#endif /* CONFIG_ECRNX_SOFTMAC */
                    );
    idx += res;
    size -= res;

#ifdef CONFIG_ECRNX_FULLMAC
    if (ecrnx_sta->ps.active) {
        if (ecrnx_sta->uapsd_tids &&
            (ecrnx_sta->uapsd_tids == ((1 << NX_NB_TXQ_PER_STA) - 1)))
            res = scnprintf(&buf[idx], size, PS_HDR_UAPSD "\n",
                            ecrnx_sta->ps.pkt_ready[UAPSD_ID],
                            ecrnx_sta->ps.sp_cnt[UAPSD_ID]);
        else if (ecrnx_sta->uapsd_tids)
            res = scnprintf(&buf[idx], size, PS_HDR "\n",
                            ecrnx_sta->ps.pkt_ready[LEGACY_PS_ID],
                            ecrnx_sta->ps.sp_cnt[LEGACY_PS_ID],
                            ecrnx_sta->ps.pkt_ready[UAPSD_ID],
                            ecrnx_sta->ps.sp_cnt[UAPSD_ID]);
        else
            res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
                            ecrnx_sta->ps.pkt_ready[LEGACY_PS_ID],
                            ecrnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
        idx += res;
        size -= res;
    } else {
        res = scnprintf(&buf[idx], size, "\n");
        idx += res;
        size -= res;
    }
#endif /* CONFIG_ECRNX_FULLMAC */


    res = scnprintf(&buf[idx], size, TXQ_STA_PREF TXQ_HDR TXQ_HDR_SUFF "\n");
    idx += res;
    size -= res;


    foreach_sta_txq(ecrnx_sta, txq, tid, ecrnx_hw) {
        res = ecrnx_dbgfs_txq(&buf[idx], size, txq, STA_TXQ, tid, NULL);
        idx += res;
        size -= res;
    }

    return idx;
}

static int ecrnx_dbgfs_txq_vif(char *buf, size_t size, struct ecrnx_vif *ecrnx_vif,
                              struct ecrnx_hw *ecrnx_hw)
{
    int res, idx = 0;
    struct ecrnx_txq *txq;
    struct ecrnx_sta *ecrnx_sta;

#ifdef CONFIG_ECRNX_FULLMAC
    res = scnprintf(&buf[idx], size, VIF_HDR, ecrnx_vif->vif_index, ecrnx_vif->ndev->name);
    idx += res;
    size -= res;
    if (!ecrnx_vif->up || ecrnx_vif->ndev == NULL)
        return idx;

#else
    int ac;
    char ac_name[2] = {'0', '\0'};

    res = scnprintf(&buf[idx], size, VIF_HDR, ecrnx_vif->vif_index);
    idx += res;
    size -= res;
#endif /* CONFIG_ECRNX_FULLMAC */

#ifdef CONFIG_ECRNX_FULLMAC
    if (ECRNX_VIF_TYPE(ecrnx_vif) ==  NL80211_IFTYPE_AP ||
        ECRNX_VIF_TYPE(ecrnx_vif) ==  NL80211_IFTYPE_P2P_GO ||
        ECRNX_VIF_TYPE(ecrnx_vif) ==  NL80211_IFTYPE_MESH_POINT) {
        res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
        idx += res;
        size -= res;
        txq = ecrnx_txq_vif_get(ecrnx_vif, NX_UNK_TXQ_TYPE);
        res = ecrnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "UNK");
        idx += res;
        size -= res;
        txq = ecrnx_txq_vif_get(ecrnx_vif, NX_BCMC_TXQ_TYPE);
        res = ecrnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "BCMC");
        idx += res;
        size -= res;
        ecrnx_sta = &ecrnx_hw->sta_table[ecrnx_vif->ap.bcmc_index];
        if (ecrnx_sta->ps.active) {
            res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
                            ecrnx_sta->ps.sp_cnt[LEGACY_PS_ID],
                            ecrnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
            idx += res;
            size -= res;
        } else {
            res = scnprintf(&buf[idx], size, "\n");
            idx += res;
            size -= res;
        }

        list_for_each_entry(ecrnx_sta, &ecrnx_vif->ap.sta_list, list) {
            res = ecrnx_dbgfs_txq_sta(&buf[idx], size, ecrnx_sta, ecrnx_hw);
            idx += res;
            size -= res;
        }
    } else if (ECRNX_VIF_TYPE(ecrnx_vif) ==  NL80211_IFTYPE_STATION ||
               ECRNX_VIF_TYPE(ecrnx_vif) ==  NL80211_IFTYPE_P2P_CLIENT) {
        if (ecrnx_vif->sta.ap) {
            res = ecrnx_dbgfs_txq_sta(&buf[idx], size, ecrnx_vif->sta.ap, ecrnx_hw);
            idx += res;
            size -= res;
        }
    }

#else
    res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
    idx += res;
    size -= res;

    foreach_vif_txq(ecrnx_vif, txq, ac) {
        ac_name[0]++;
        res = ecrnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, ac_name);
        idx += res;
        size -= res;
    }

    list_for_each_entry(ecrnx_sta, &ecrnx_vif->stations, list) {
        res = ecrnx_dbgfs_txq_sta(&buf[idx], size, ecrnx_sta, ecrnx_hw);
        idx += res;
        size -= res;
    }
#endif /* CONFIG_ECRNX_FULLMAC */
    return idx;
}

static ssize_t ecrnx_dbgfs_txq_read(struct file *file ,
                                   char __user *user_buf,
                                   size_t count, loff_t *ppos)
{
    struct ecrnx_hw *ecrnx_hw = file->private_data;
    struct ecrnx_vif *vif;
    char *buf;
    int idx, res;
    ssize_t read;
    size_t bufsz = ((NX_VIRT_DEV_MAX * (VIF_HDR_MAX_LEN + 2 * VIF_SEP_LEN)) +
                    (NX_REMOTE_STA_MAX * STA_HDR_MAX_LEN) +
                    ((NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX + NX_NB_TXQ) *
                     TXQ_HDR_MAX_LEN) + CAPTION_LEN);

    /* everything is read in one go */
    if (*ppos)
        return 0;

    bufsz = min_t(size_t, bufsz, count);
    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    bufsz--;
    idx = 0;

    res = scnprintf(&buf[idx], bufsz, CAPTION);
    idx += res;
    bufsz -= res;

    //spin_lock_bh(&ecrnx_hw->tx_lock);
    list_for_each_entry(vif, &ecrnx_hw->vifs, list) {
        res = scnprintf(&buf[idx], bufsz, "\n"VIF_SEP);
        idx += res;
        bufsz -= res;
        res = ecrnx_dbgfs_txq_vif(&buf[idx], bufsz, vif, ecrnx_hw);
        idx += res;
        bufsz -= res;
        res = scnprintf(&buf[idx], bufsz, VIF_SEP);
        idx += res;
        bufsz -= res;
    }
    //spin_unlock_bh(&ecrnx_hw->tx_lock);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
    kfree(buf);

    return read;
}
DEBUGFS_READ_FILE_OPS(txq);

static ssize_t ecrnx_dbgfs_acsinfo_read(struct file *file,
                                           char __user *user_buf,
                                           size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    #ifdef CONFIG_ECRNX_SOFTMAC
    struct wiphy *wiphy = priv->hw->wiphy;
    #else //CONFIG_ECRNX_SOFTMAC
    struct wiphy *wiphy = priv->wiphy;
    #endif //CONFIG_ECRNX_SOFTMAC
    ssize_t read;
    char *buf = kmalloc((SCAN_CHANNEL_MAX + 1) * 43, GFP_ATOMIC);

    //char buf[(SCAN_CHANNEL_MAX + 1) * 43];
    int survey_cnt = 0;
    int len = 0;
    int band, chan_cnt;

    if(!buf){
        return 0;
    }

    mutex_lock(&priv->dbgdump_elem.mutex);

    len += scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                     "FREQ    TIME(ms)    BUSY(ms)    NOISE(dBm)\n");

#ifdef CONFIG_ECRNX_5G
    for (band = NL80211_BAND_2GHZ; band <= NL80211_BAND_5GHZ; band++) {
#else
	for (band = NL80211_BAND_2GHZ; band <= NL80211_BAND_2GHZ; band++) {
#endif
        for (chan_cnt = 0; chan_cnt < wiphy->bands[band]->n_channels; chan_cnt++) {
            struct ecrnx_survey_info *p_survey_info = &priv->survey[survey_cnt];
            struct ieee80211_channel *p_chan = &wiphy->bands[band]->channels[chan_cnt];

            if (p_survey_info->filled) {
                len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - len - 1, count),
                                 "%d    %03d         %03d         %d\n",
                                 p_chan->center_freq,
                                 p_survey_info->chan_time_ms,
                                 p_survey_info->chan_time_busy_ms,
                                 p_survey_info->noise_dbm);
            } else {
                len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) -len -1, count),
                                 "%d    NOT AVAILABLE\n",
                                 p_chan->center_freq);
            }

            survey_cnt++;
        }
    }

    mutex_unlock(&priv->dbgdump_elem.mutex);
    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
    kfree(buf);

    return read;
}

DEBUGFS_READ_FILE_OPS(acsinfo);

static ssize_t ecrnx_dbgfs_fw_dbg_read(struct file *file,
                                           char __user *user_buf,
                                           size_t count, loff_t *ppos)
{
    char help[]="usage: [MOD:<ALL|KE|DBG|IPC|DMA|MM|TX|RX|PHY>]* "
        "[DBG:<NONE|CRT|ERR|WRN|INF|VRB>]\n";

    return simple_read_from_buffer(user_buf, count, ppos, help, sizeof(help));
}


static ssize_t ecrnx_dbgfs_fw_dbg_write(struct file *file,
                                            const char __user *user_buf,
                                            size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int idx = 0;
    u32 mod = 0;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';

#define ECRNX_MOD_TOKEN(str, val)                                        \
    if (strncmp(&buf[idx], str, sizeof(str) - 1 ) == 0) {               \
        idx += sizeof(str) - 1;                                         \
        mod |= val;                                                     \
        continue;                                                       \
    }

#define ECRNX_DBG_TOKEN(str, val)                                \
    if (strncmp(&buf[idx], str, sizeof(str) - 1) == 0) {        \
        idx += sizeof(str) - 1;                                 \
        dbg = val;                                              \
        goto dbg_done;                                          \
    }

    while ((idx + 4) < len) {
        if (strncmp(&buf[idx], "MOD:", 4) == 0) {
            idx += 4;
            ECRNX_MOD_TOKEN("ALL", 0xffffffff);
            ECRNX_MOD_TOKEN("KE",  BIT(0));
            ECRNX_MOD_TOKEN("DBG", BIT(1));
            ECRNX_MOD_TOKEN("IPC", BIT(2));
            ECRNX_MOD_TOKEN("DMA", BIT(3));
            ECRNX_MOD_TOKEN("MM",  BIT(4));
            ECRNX_MOD_TOKEN("TX",  BIT(5));
            ECRNX_MOD_TOKEN("RX",  BIT(6));
            ECRNX_MOD_TOKEN("PHY", BIT(7));
            idx++;
        } else if (strncmp(&buf[idx], "DBG:", 4) == 0) {
            u32 dbg = 0;
            idx += 4;
            ECRNX_DBG_TOKEN("NONE", 0);
            ECRNX_DBG_TOKEN("CRT",  1);
            ECRNX_DBG_TOKEN("ERR",  2);
            ECRNX_DBG_TOKEN("WRN",  3);
            ECRNX_DBG_TOKEN("INF",  4);
            ECRNX_DBG_TOKEN("VRB",  5);
            idx++;
            continue;
          dbg_done:
            ecrnx_send_dbg_set_sev_filter_req(priv, dbg);
        } else {
            idx++;
        }
    }

    if (mod) {
        ecrnx_send_dbg_set_mod_filter_req(priv, mod);
    }

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg);

static ssize_t ecrnx_dbgfs_sys_stats_read(struct file *file,
                                         char __user *user_buf,
                                         size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[3*64];
    int len = 0;
    ssize_t read;
    int error = 0;
    struct dbg_get_sys_stat_cfm cfm;
    u32 sleep_int, sleep_frac, doze_int, doze_frac;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* Get the information from the FW */
    if ((error = ecrnx_send_dbg_get_sys_stat_req(priv, &cfm)))
        return error;

    if (cfm.stats_time == 0)
        return 0;

    sleep_int = ((cfm.cpu_sleep_time * 100) / cfm.stats_time);
    sleep_frac = (((cfm.cpu_sleep_time * 100) % cfm.stats_time) * 10) / cfm.stats_time;
    doze_int = ((cfm.doze_time * 100) / cfm.stats_time);
    doze_frac = (((cfm.doze_time * 100) % cfm.stats_time) * 10) / cfm.stats_time;

    len += scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                     "\nSystem statistics:\n");
    len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count),
                     "  CPU sleep [%%]: %d.%d\n", sleep_int, sleep_frac);
    len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count),
                     "  Doze      [%%]: %d.%d\n", doze_int, doze_frac);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    return read;
}

DEBUGFS_READ_FILE_OPS(sys_stats);

#ifdef CONFIG_ECRNX_MUMIMO_TX
static ssize_t ecrnx_dbgfs_mu_group_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *ecrnx_hw = file->private_data;
    struct ecrnx_mu_info *mu = &ecrnx_hw->mu;
    struct ecrnx_mu_group *group;
    size_t bufsz = NX_MU_GROUP_MAX * sizeof("xx = (xx - xx - xx - xx)\n") + 50;
    char *buf;
    int j, res, idx = 0;

    if (*ppos)
        return 0;

    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    res = scnprintf(&buf[idx], bufsz, "MU Group list (%d groups, %d users max)\n",
                    NX_MU_GROUP_MAX, CONFIG_USER_MAX);
    idx += res;
    bufsz -= res;

    list_for_each_entry(group, &mu->active_groups, list) {
        if (group->user_cnt) {
            res = scnprintf(&buf[idx], bufsz, "%2d = (", group->group_id);
            idx += res;
            bufsz -= res;
            for (j = 0; j < (CONFIG_USER_MAX - 1) ; j++) {
                if (group->users[j])
                    res = scnprintf(&buf[idx], bufsz, "%2d - ",
                                    group->users[j]->sta_idx);
                else
                    res = scnprintf(&buf[idx], bufsz, ".. - ");

                idx += res;
                bufsz -= res;
            }

            if (group->users[j])
                res = scnprintf(&buf[idx], bufsz, "%2d)\n",
                                group->users[j]->sta_idx);
            else
                res = scnprintf(&buf[idx], bufsz, "..)\n");

            idx += res;
            bufsz -= res;
        }
    }

    res = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
    kfree(buf);

    return res;
}

DEBUGFS_READ_FILE_OPS(mu_group);
#endif

#ifdef CONFIG_ECRNX_P2P_DEBUGFS
static ssize_t ecrnx_dbgfs_oppps_write(struct file *file,
                                      const char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct ecrnx_hw *rw_hw = file->private_data;
    struct ecrnx_vif *rw_vif;
    char buf[32];
    size_t len = min_t(size_t, count, sizeof(buf) - 1);
    int ctw;

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';

    /* Read the written CT Window (provided in ms) value */
    if (sscanf(buf, "ctw=%d", &ctw) > 0) {
        /* Check if at least one VIF is configured as P2P GO */
        list_for_each_entry(rw_vif, &rw_hw->vifs, list) {
#ifdef CONFIG_ECRNX_SOFTMAC
            if ((ECRNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_AP) && rw_vif->vif->p2p) {
#else /* CONFIG_ECRNX_FULLMAC */
            if (ECRNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
#endif /* CONFIG_ECRNX_SOFTMAC */
                struct mm_set_p2p_oppps_cfm cfm;

                /* Forward request to the embedded and wait for confirmation */
                ecrnx_send_p2p_oppps_req(rw_hw, rw_vif, (u8)ctw, &cfm);

                break;
            }
        }
    }

    return count;
}

DEBUGFS_WRITE_FILE_OPS(oppps);

static ssize_t ecrnx_dbgfs_noa_write(struct file *file,
                                    const char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct ecrnx_hw *rw_hw = file->private_data;
    struct ecrnx_vif *rw_vif;
    char buf[64];
    size_t len = min_t(size_t, count, sizeof(buf) - 1);
    int noa_count, interval, duration, dyn_noa;

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';

    /* Read the written NOA information */
    if (sscanf(buf, "count=%d interval=%d duration=%d dyn=%d",
               &noa_count, &interval, &duration, &dyn_noa) > 0) {
        /* Check if at least one VIF is configured as P2P GO */
        list_for_each_entry(rw_vif, &rw_hw->vifs, list) {
#ifdef CONFIG_ECRNX_SOFTMAC
            if ((ECRNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_AP) && rw_vif->vif->p2p) {
#else /* CONFIG_ECRNX_FULLMAC */
            if (ECRNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
#endif /* CONFIG_ECRNX_SOFTMAC */
                struct mm_set_p2p_noa_cfm cfm;

                /* Forward request to the embedded and wait for confirmation */
                ecrnx_send_p2p_noa_req(rw_hw, rw_vif, noa_count, interval,
                                      duration, (dyn_noa > 0),  &cfm);

                break;
            }
        }
    }

    return count;
}

DEBUGFS_WRITE_FILE_OPS(noa);
#endif /* CONFIG_ECRNX_P2P_DEBUGFS */

struct ecrnx_dbgfs_fw_trace {
    struct ecrnx_fw_trace_local_buf lbuf;
    struct ecrnx_fw_trace *trace;
    struct ecrnx_hw *ecrnx_hw;
};

static int ecrnx_dbgfs_fw_trace_open(struct inode *inode, struct file *file)
{
    struct ecrnx_dbgfs_fw_trace *ltrace = kmalloc(sizeof(*ltrace), GFP_KERNEL);
    struct ecrnx_hw *priv = inode->i_private;

    if (!ltrace)
        return -ENOMEM;

    if (ecrnx_fw_trace_alloc_local(&ltrace->lbuf, 5120)) {
        kfree(ltrace);
    }

    ltrace->trace = &priv->debugfs.fw_trace;
    ltrace->ecrnx_hw = priv;
    file->private_data = ltrace;
    return 0;
}

static int ecrnx_dbgfs_fw_trace_release(struct inode *inode, struct file *file)
{
    struct ecrnx_dbgfs_fw_trace *ltrace = file->private_data;

    if (ltrace) {
        ecrnx_fw_trace_free_local(&ltrace->lbuf);
        kfree(ltrace);
    }

    return 0;
}

static ssize_t ecrnx_dbgfs_fw_trace_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_dbgfs_fw_trace *ltrace = file->private_data;
    bool dont_wait = ((file->f_flags & O_NONBLOCK) ||
                      ltrace->ecrnx_hw->debugfs.unregistering);

    return ecrnx_fw_trace_read(ltrace->trace, &ltrace->lbuf,
                              dont_wait, user_buf, count);
}

static ssize_t ecrnx_dbgfs_fw_trace_write(struct file *file,
                                         const char __user *user_buf,
                                         size_t count, loff_t *ppos)
{
    struct ecrnx_dbgfs_fw_trace *ltrace = file->private_data;
    int ret;

    ret = _ecrnx_fw_trace_reset(ltrace->trace, true);
    if (ret)
        return ret;

    return count;
}

DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(fw_trace);

static ssize_t ecrnx_dbgfs_fw_trace_level_read(struct file *file,
                                              char __user *user_buf,
                                              size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    return ecrnx_fw_trace_level_read(&priv->debugfs.fw_trace, user_buf,
                                    count, ppos);
}

static ssize_t ecrnx_dbgfs_fw_trace_level_write(struct file *file,
                                               const char __user *user_buf,
                                               size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    return ecrnx_fw_trace_level_write(&priv->debugfs.fw_trace, user_buf, count);
}
DEBUGFS_READ_WRITE_FILE_OPS(fw_trace_level);


#ifdef CONFIG_ECRNX_RADAR
static ssize_t ecrnx_dbgfs_pulses_read(struct file *file,
                                      char __user *user_buf,
                                      size_t count, loff_t *ppos,
                                      int rd_idx)
{
    struct ecrnx_hw *priv = file->private_data;
    char *buf;
    int len = 0;
    int bufsz;
    int i;
    int index;
    struct ecrnx_radar_pulses *p = &priv->radar.pulses[rd_idx];
    ssize_t read;

    if (*ppos != 0)
        return 0;

    /* Prevent from interrupt preemption */
    spin_lock_bh(&priv->radar.lock);
    bufsz = p->count * 34 + 51;
    bufsz += ecrnx_radar_dump_pattern_detector(NULL, 0, &priv->radar, rd_idx);
    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL) {
        spin_unlock_bh(&priv->radar.lock);
        return 0;
    }

    if (p->count) {
        len += scnprintf(&buf[len], bufsz - len,
                         " PRI     WIDTH     FOM     FREQ\n");
        index = p->index;
        for (i = 0; i < p->count; i++) {
            struct radar_pulse *pulse;

            if (index > 0)
                index--;
            else
                index = ECRNX_RADAR_PULSE_MAX - 1;

            pulse = (struct radar_pulse *) &p->buffer[index];

            len += scnprintf(&buf[len], bufsz - len,
                             "%05dus  %03dus     %2d%%    %+3dMHz\n", pulse->rep,
                             2 * pulse->len, 6 * pulse->fom, 2*pulse->freq);
        }
    }

    len += ecrnx_radar_dump_pattern_detector(&buf[len], bufsz - len,
                                            &priv->radar, rd_idx);

    spin_unlock_bh(&priv->radar.lock);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);

    return read;
}

static ssize_t ecrnx_dbgfs_pulses_prim_read(struct file *file,
                                           char __user *user_buf,
                                           size_t count, loff_t *ppos)
{
    return ecrnx_dbgfs_pulses_read(file, user_buf, count, ppos, 0);
}

DEBUGFS_READ_FILE_OPS(pulses_prim);

static ssize_t ecrnx_dbgfs_pulses_sec_read(struct file *file,
                                          char __user *user_buf,
                                          size_t count, loff_t *ppos)
{
    return ecrnx_dbgfs_pulses_read(file, user_buf, count, ppos, 1);
}

DEBUGFS_READ_FILE_OPS(pulses_sec);

static ssize_t ecrnx_dbgfs_detected_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char *buf;
    int bufsz,len = 0;
    ssize_t read;

    if (*ppos != 0)
        return 0;

    bufsz = 5; // RIU:\n
    bufsz += ecrnx_radar_dump_radar_detected(NULL, 0, &priv->radar,
                                            ECRNX_RADAR_RIU);

    if (priv->phy.cnt > 1) {
        bufsz += 5; // FCU:\n
        bufsz += ecrnx_radar_dump_radar_detected(NULL, 0, &priv->radar,
                                                ECRNX_RADAR_FCU);
    }

    buf = kmalloc(bufsz, GFP_KERNEL);
    if (buf == NULL) {
        return 0;
    }

    len = scnprintf(&buf[len], bufsz, "RIU:\n");
    len += ecrnx_radar_dump_radar_detected(&buf[len], bufsz - len, &priv->radar,
                                            ECRNX_RADAR_RIU);

    if (priv->phy.cnt > 1) {
        len += scnprintf(&buf[len], bufsz - len, "FCU:\n");
        len += ecrnx_radar_dump_radar_detected(&buf[len], bufsz - len,
                                              &priv->radar, ECRNX_RADAR_FCU);
    }

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);

    return read;
}

DEBUGFS_READ_FILE_OPS(detected);

static ssize_t ecrnx_dbgfs_enable_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "RIU=%d FCU=%d\n", priv->radar.dpd[ECRNX_RADAR_RIU]->enabled,
                    priv->radar.dpd[ECRNX_RADAR_FCU]->enabled);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t ecrnx_dbgfs_enable_write(struct file *file,
                                     const char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "RIU=%d", &val) > 0)
        ecrnx_radar_detection_enable(&priv->radar, val, ECRNX_RADAR_RIU);

    if (sscanf(buf, "FCU=%d", &val) > 0)
        ecrnx_radar_detection_enable(&priv->radar, val, ECRNX_RADAR_FCU);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(enable);

static ssize_t ecrnx_dbgfs_band_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "BAND=%d\n", priv->phy.sec_chan.band);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t ecrnx_dbgfs_band_write(struct file *file,
                                     const char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

#ifdef CONFIG_ECRNX_5G
    if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) && (val <= NL80211_BAND_5GHZ))
#else
	if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) && (val <= NL80211_BAND_2GHZ))
#endif
        priv->phy.sec_chan.band = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(band);

static ssize_t ecrnx_dbgfs_type_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "TYPE=%d\n", priv->phy.sec_chan.type);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t ecrnx_dbgfs_type_write(struct file *file,
                                     const char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if ((sscanf(buf, "%d", &val) > 0) && (val >= PHY_CHNL_BW_20) &&
        (val <= PHY_CHNL_BW_80P80))
        priv->phy.sec_chan.type = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(type);

static ssize_t ecrnx_dbgfs_prim20_read(struct file *file,
                                      char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "PRIM20=%dMHz\n", priv->phy.sec_chan.prim20_freq);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t ecrnx_dbgfs_prim20_write(struct file *file,
                                       const char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.prim20_freq = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(prim20);

static ssize_t ecrnx_dbgfs_center1_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "CENTER1=%dMHz\n", priv->phy.sec_chan.center1_freq);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t ecrnx_dbgfs_center1_write(struct file *file,
                                        const char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.center1_freq = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center1);

static ssize_t ecrnx_dbgfs_center2_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "CENTER2=%dMHz\n", priv->phy.sec_chan.center2_freq);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t ecrnx_dbgfs_center2_write(struct file *file,
                                        const char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.center2_freq = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center2);


static ssize_t ecrnx_dbgfs_set_read(struct file *file,
                                   char __user *user_buf,
                                   size_t count, loff_t *ppos)
{
    return 0;
}

static ssize_t ecrnx_dbgfs_set_write(struct file *file,
                                    const char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;

    ecrnx_send_set_channel(priv, 1, NULL);
    ecrnx_radar_detection_enable(&priv->radar, ECRNX_RADAR_DETECT_ENABLE,
                                ECRNX_RADAR_FCU);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(set);
#endif /* CONFIG_ECRNX_RADAR */

#ifdef CONFIG_ECRNX_FULLMAC

#define LINE_MAX_SZ 150

struct st {
    char line[LINE_MAX_SZ + 1];
    unsigned int r_idx;
};

static int compare_idx(const void *st1, const void *st2)
{
    int index1 = ((struct st *)st1)->r_idx;
    int index2 = ((struct st *)st2)->r_idx;

    if (index1 > index2) return 1;
    if (index1 < index2) return -1;

    return 0;
}

static const int ru_size[] =
{
    26,
    52,
    106,
    242,
    484,
    996
};

static int print_rate(char *buf, int size, int format, int nss, int mcs, int bw,
                      int sgi, int pre, int dcm, int *r_idx)
{
    int res = 0;
    int bitrates_cck[4] = { 10, 20, 55, 110 };
    int bitrates_ofdm[8] = { 6, 9, 12, 18, 24, 36, 48, 54};
    char he_gi[3][4] = {"0.8", "1.6", "3.2"};

    if (format < FORMATMOD_HT_MF) {
        if (mcs < 4) {
            if (r_idx) {
                *r_idx = (mcs * 2) + pre;
                res = scnprintf(buf, size - res, "%3d ", *r_idx);
            }
            res += scnprintf(&buf[res], size - res, "L-CCK/%cP          %2u.%1uM    ",
                             pre > 0 ? 'L' : 'S',
                             bitrates_cck[mcs] / 10,
                             bitrates_cck[mcs] % 10);
        } else {
            mcs -= 4;
            if (r_idx) {
                *r_idx = N_CCK + mcs;
                res = scnprintf(buf, size - res, "%3d ", *r_idx);
            }
            res += scnprintf(&buf[res], size - res, "L-OFDM            %2u.0M    ",
                             bitrates_ofdm[mcs]);
        }
    } else if (format < FORMATMOD_VHT) {
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 + bw * 2 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        mcs += nss * 8;
        res += scnprintf(&buf[res], size - res, "HT%d/%cGI           MCS%-2d   ",
                         20 * (1 << bw), sgi ? 'S' : 'L', mcs);
    } else if (format == FORMATMOD_VHT){
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + nss * 80 + mcs * 8 + bw * 2 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        res += scnprintf(&buf[res], size - res, "VHT%d/%cGI%*cMCS%d/%1d  ",
                         20 * (1 << bw), sgi ? 'S' : 'L', bw > 2 ? 9 : 10, ' ',
                         mcs, nss + 1);
    } else if (format == FORMATMOD_HE_SU){
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + N_VHT + nss * 144 + mcs * 12 + bw * 3 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        res += scnprintf(&buf[res], size - res, "HE%d/GI%s%4s%*cMCS%d/%1d%*c",
                         20 * (1 << bw), he_gi[sgi], dcm ? "/DCM" : "",
                         bw > 2 ? 4 : 5, ' ', mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');
    } else {
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + nss * 216 + mcs * 18 + bw * 3 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        res += scnprintf(&buf[res], size - res, "HEMU-%d/GI%s%*cMCS%d/%1d%*c",
                         ru_size[bw], he_gi[sgi], bw > 1 ? 1 : 2, ' ',
                         mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');

    }

    return res;
}

static int print_rate_from_cfg(char *buf, int size, u32 rate_config, int *r_idx, int ru_size)
{
    union ecrnx_rate_ctrl_info *r_cfg = (union ecrnx_rate_ctrl_info *)&rate_config;
    union ecrnx_mcs_index *mcs_index = (union ecrnx_mcs_index *)&rate_config;
    unsigned int ft, pre, gi, bw, nss, mcs, dcm, len;

    ft = r_cfg->formatModTx;
    pre = r_cfg->giAndPreTypeTx >> 1;
    gi = r_cfg->giAndPreTypeTx;
    bw = r_cfg->bwTx;
    dcm = 0;
    if (ft == FORMATMOD_HE_MU) {
        mcs = mcs_index->he.mcs;
        nss = mcs_index->he.nss;
        bw = ru_size;
        dcm = r_cfg->dcmTx;
    } else if (ft == FORMATMOD_HE_SU) {
        mcs = mcs_index->he.mcs;
        nss = mcs_index->he.nss;
        dcm = r_cfg->dcmTx;
    } else if (ft == FORMATMOD_VHT) {
        mcs = mcs_index->vht.mcs;
        nss = mcs_index->vht.nss;
    } else if (ft >= FORMATMOD_HT_MF) {
        mcs = mcs_index->ht.mcs;
        nss = mcs_index->ht.nss;
    } else {
        mcs = mcs_index->legacy;
        nss = 0;
    }

    len = print_rate(buf, size, ft, nss, mcs, bw, gi, pre, dcm, r_idx);
    return len;
}

static void idx_to_rate_cfg(int idx, union ecrnx_rate_ctrl_info *r_cfg, int *ru_size)
{
    r_cfg->value = 0;
    if (idx < N_CCK)
    {
        r_cfg->formatModTx = FORMATMOD_NON_HT;
        r_cfg->giAndPreTypeTx = (idx & 1) << 1;
        r_cfg->mcsIndexTx = idx / 2;
    }
    else if (idx < (N_CCK + N_OFDM))
    {
        r_cfg->formatModTx = FORMATMOD_NON_HT;
        r_cfg->mcsIndexTx =  idx - N_CCK + 4;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT))
    {
        union ecrnx_mcs_index *r = (union ecrnx_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM);
        r_cfg->formatModTx = FORMATMOD_HT_MF;
        r->ht.nss = idx / (8*2*2);
        r->ht.mcs = (idx % (8*2*2)) / (2*2);
        r_cfg->bwTx = ((idx % (8*2*2)) % (2*2)) / 2;
        r_cfg->giAndPreTypeTx = idx & 1;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT))
    {
        union ecrnx_mcs_index *r = (union ecrnx_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM + N_HT);
        r_cfg->formatModTx = FORMATMOD_VHT;
        r->vht.nss = idx / (10*4*2);
        r->vht.mcs = (idx % (10*4*2)) / (4*2);
        r_cfg->bwTx = ((idx % (10*4*2)) % (4*2)) / 2;
        r_cfg->giAndPreTypeTx = idx & 1;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU))
    {
        union ecrnx_mcs_index *r = (union ecrnx_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM + N_HT + N_VHT);
        r_cfg->formatModTx = FORMATMOD_HE_SU;
        r->vht.nss = idx / (12*4*3);
        r->vht.mcs = (idx % (12*4*3)) / (4*3);
        r_cfg->bwTx = ((idx % (12*4*3)) % (4*3)) / 3;
        r_cfg->giAndPreTypeTx = idx % 3;
    }
    else
    {
        union ecrnx_mcs_index *r = (union ecrnx_mcs_index *)r_cfg;

        BUG_ON(ru_size == NULL);

        idx -= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU);
        r_cfg->formatModTx = FORMATMOD_HE_MU;
        r->vht.nss = idx / (12*6*3);
        r->vht.mcs = (idx % (12*6*3)) / (6*3);
        *ru_size = ((idx % (12*6*3)) % (6*3)) / 3;
        r_cfg->giAndPreTypeTx = idx % 3;
        r_cfg->bwTx = 0;
    }
}

static struct ecrnx_sta* ecrnx_dbgfs_get_sta(struct ecrnx_hw *ecrnx_hw,
                                           char* mac_addr)
{
    u8 mac[6];

    if (sscanf(mac_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6)
        return NULL;
    return ecrnx_get_sta(ecrnx_hw, mac);
}

static ssize_t ecrnx_dbgfs_twt_request_read(struct file *file,
                                           char __user *user_buf,
                                           size_t count,
                                           loff_t *ppos)
{
    char buf[750];
    ssize_t read;
    struct ecrnx_hw *priv = file->private_data;
    struct ecrnx_sta *sta = NULL;
    int len;

    /* Get the station index from MAC address */
    sta = ecrnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;
    if (sta->twt_ind.sta_idx != ECRNX_INVALID_STA)
    {
        struct twt_conf_tag *conf = &sta->twt_ind.conf;
        if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ACCEPT)
            len = scnprintf(buf, sizeof(buf) - 1, "Accepted configuration");
        else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ALTERNATE)
            len = scnprintf(buf, sizeof(buf) - 1, "Alternate configuration proposed by AP");
        else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_DICTATE)
            len = scnprintf(buf, sizeof(buf) - 1, "AP dictates the following configuration");
        else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_REJECT)
            len = scnprintf(buf, sizeof(buf) - 1, "AP rejects the following configuration");
        else
        {
            len = scnprintf(buf, sizeof(buf) - 1, "Invalid response from the peer");
            goto end;
        }
        len += scnprintf(&buf[len], sizeof(buf) - 1 - len,":\n"
                         "flow_type = %d\n"
                         "wake interval mantissa = %d\n"
                         "wake interval exponent = %d\n"
                         "wake interval = %d us\n"
                         "nominal minimum wake duration = %d us\n",
                         conf->flow_type, conf->wake_int_mantissa,
                         conf->wake_int_exp,
                         conf->wake_int_mantissa << conf->wake_int_exp,
                         conf->wake_dur_unit ?
                         conf->min_twt_wake_dur * 1024:
                         conf->min_twt_wake_dur * 256);
    }
    else
    {
        len = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                        "setup_command = <0: request, 1: suggest, 2: demand>,"
                        "flow_type = <0: announced, 1: unannounced>,"
                        "wake_interval_mantissa = <0 if setup request and no constraints>,"
                        "wake_interval_exp = <0 if setup request and no constraints>,"
                        "nominal_min_wake_dur = <0 if setup request and no constraints>,"
                        "wake_dur_unit = <0: 256us, 1: tu>");
    }
  end:
    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
    return read;
}

static ssize_t ecrnx_dbgfs_twt_request_write(struct file *file,
                                            const char __user *user_buf,
                                            size_t count,
                                            loff_t *ppos)
{
    char *accepted_params[] = {"setup_command",
                               "flow_type",
                               "wake_interval_mantissa",
                               "wake_interval_exp",
                               "nominal_min_wake_dur",
                               "wake_dur_unit",
                               0
                               };
    struct twt_conf_tag twt_conf;
    struct twt_setup_cfm twt_setup_cfm;
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_hw *priv = file->private_data;
    char param[30];
    char *line;
    int error = 1, i, val, setup_command = -1;
    bool_l found;
    char *buf = kmalloc(1024, GFP_ATOMIC);
    size_t len = 1024 - 1;

    if(!buf){
        return 0;
    }

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    /* Get the station index from MAC address */
    sta = ecrnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL){
        kfree(buf);
        return -EINVAL;
    }

    /* Get the content of the file */
    if (copy_from_user(buf, user_buf, len)){
        kfree(buf);
        return -EFAULT;
    }

    buf[len] = '\0';
    memset(&twt_conf, 0, sizeof(twt_conf));

    line = buf;
    /* Get the content of the file */
    while (line != NULL)
    {
        if (sscanf(line, "%s = %d", param, &val) == 2)
        {
            i = 0;
            found = false;
            // Check if parameter is valid
            while(accepted_params[i])
            {
                if (strcmp(accepted_params[i], param) == 0)
                {
                    found = true;
                    break;
                }
                i++;
            }

            if (!found)
            {
                dev_err(priv->dev, "%s: parameter %s is not valid\n", __func__, param);
                kfree(buf);
                return -EINVAL;
            }

            if (!strcmp(param, "setup_command"))
            {
                setup_command = val;
            }
            else if (!strcmp(param, "flow_type"))
            {
                twt_conf.flow_type = val;
            }
            else if (!strcmp(param, "wake_interval_mantissa"))
            {
                twt_conf.wake_int_mantissa = val;
            }
            else if (!strcmp(param, "wake_interval_exp"))
            {
                twt_conf.wake_int_exp = val;
            }
            else if (!strcmp(param, "nominal_min_wake_dur"))
            {
                twt_conf.min_twt_wake_dur = val;
            }
            else if (!strcmp(param, "wake_dur_unit"))
            {
                twt_conf.wake_dur_unit = val;
            }
        }
        else
        {
            dev_err(priv->dev, "%s: Impossible to read TWT configuration option\n", __func__);
            kfree(buf);
            return -EFAULT;
        }
        line = strchr(line, ',');
        if(line == NULL)
            break;
        line++;
    }

    if (setup_command == -1)
    {
        dev_err(priv->dev, "%s: TWT missing setup command\n", __func__);
        kfree(buf);
        return -EFAULT;
    }

    // Forward the request to the LMAC
    if ((error = ecrnx_send_twt_request(priv, setup_command, sta->vif_idx,
                                       &twt_conf, &twt_setup_cfm)) != 0){
        kfree(buf);
        return error;
        }

    // Check the status
    if (twt_setup_cfm.status != CO_OK){
        kfree(buf);
        return -EIO;
    }

    kfree(buf);
    return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_request);

static ssize_t ecrnx_dbgfs_twt_teardown_read(struct file *file,
                                            char __user *user_buf,
                                            size_t count,
                                            loff_t *ppos)
{
    char buf[512];
    int ret;
    ssize_t read;


    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "TWT teardown format:\n\n"
                    "flow_id = <ID>\n");
    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t ecrnx_dbgfs_twt_teardown_write(struct file *file,
                                             const char __user *user_buf,
                                             size_t count,
                                             loff_t *ppos)
{
    struct twt_teardown_req twt_teardown;
    struct twt_teardown_cfm twt_teardown_cfm;
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_hw *priv = file->private_data;
    char buf[256];
    char *line;
    int error = 1;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    /* Get the station index from MAC address */
    sta = ecrnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Get the content of the file */
    if (copy_from_user(buf, user_buf, len))
        return -EINVAL;

    buf[len] = '\0';
    memset(&twt_teardown, 0, sizeof(twt_teardown));

    /* Get the content of the file */
    line = buf;

    if (sscanf(line, "flow_id = %d", (int *) &twt_teardown.id) != 1)
    {
        dev_err(priv->dev, "%s: Invalid TWT configuration\n", __func__);
        return -EINVAL;
    }

    twt_teardown.neg_type = 0;
    twt_teardown.all_twt = 0;
    twt_teardown.vif_idx = sta->vif_idx;

    // Forward the request to the LMAC
    if ((error = ecrnx_send_twt_teardown(priv, &twt_teardown, &twt_teardown_cfm)) != 0)
        return error;

    // Check the status
    if (twt_teardown_cfm.status != CO_OK)
        return -EIO;

    return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_teardown);

static ssize_t ecrnx_dbgfs_rc_stats_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_hw *priv = file->private_data;
    char *buf;
    int bufsz, len = 0;
    ssize_t read;
    int i = 0;
    int error = 0;
    struct me_rc_stats_cfm me_rc_stats_cfm;
    unsigned int no_samples;
    struct st *st;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* everything should fit in one call */
    if (*ppos)
        return 0;

    /* Get the station index from MAC address */
    sta = ecrnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Forward the information to the LMAC */
    if ((error = ecrnx_send_me_rc_stats(priv, sta->sta_idx, &me_rc_stats_cfm)))
        return error;

    no_samples = me_rc_stats_cfm.no_samples;
    if (no_samples == 0)
        return 0;

    bufsz = no_samples * LINE_MAX_SZ + 500;

    buf = kmalloc(bufsz + 1, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    st = kmalloc(sizeof(struct st) * no_samples, GFP_ATOMIC);
    if (st == NULL)
    {
        kfree(buf);
        return 0;
    }

    for (i = 0; i < no_samples; i++)
    {
        unsigned int tp, eprob;
        len = print_rate_from_cfg(st[i].line, LINE_MAX_SZ,
                                  me_rc_stats_cfm.rate_stats[i].rate_config,
                                  &st[i].r_idx, 0);

        if (me_rc_stats_cfm.sw_retry_step != 0)
        {
            len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len,  "%c",
                    me_rc_stats_cfm.retry_step_idx[me_rc_stats_cfm.sw_retry_step] == i ? '*' : ' ');
        }
        else
        {
            len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, " ");
        }
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
                me_rc_stats_cfm.retry_step_idx[0] == i ? 'T' : ' ');
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
                me_rc_stats_cfm.retry_step_idx[1] == i ? 't' : ' ');
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c ",
                me_rc_stats_cfm.retry_step_idx[2] == i ? 'P' : ' ');

        tp = me_rc_stats_cfm.tp[i] / 10;
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, " %4u.%1u",
                         tp / 10, tp % 10);

        eprob = ((me_rc_stats_cfm.rate_stats[i].probability * 1000) >> 16) + 1;
        len += scnprintf(&st[i].line[len],LINE_MAX_SZ - len,
                         "  %4u.%1u %5u(%6u)  %6u",
                         eprob / 10, eprob % 10,
                         me_rc_stats_cfm.rate_stats[i].success,
                         me_rc_stats_cfm.rate_stats[i].attempts,
                         me_rc_stats_cfm.rate_stats[i].sample_skipped);
    }
    len = scnprintf(buf, bufsz ,
                     "\nTX rate info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
                     sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
                     sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

    len += scnprintf(&buf[len], bufsz - len,
            " #  type               rate             tpt   eprob    ok(   tot)   skipped\n");

    // add sorted statistics to the buffer
    sort(st, no_samples, sizeof(st[0]), compare_idx, NULL);
    for (i = 0; i < no_samples; i++)
    {
        len += scnprintf(&buf[len], bufsz - len, "%s\n", st[i].line);
    }

    // display HE TB statistics if any
    if (me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX].rate_config != 0) {
        unsigned int tp, eprob;
        struct rc_rate_stats *rate_stats = &me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX];
        int ru_index = rate_stats->ru_and_length & 0x07;
        int ul_length = rate_stats->ru_and_length >> 3;

        len += scnprintf(&buf[len], bufsz - len,
                         "\nHE TB rate info:\n");

        len += scnprintf(&buf[len], bufsz - len,
                "    type               rate             tpt   eprob    ok(   tot)   ul_length\n    ");
        len += print_rate_from_cfg(&buf[len], bufsz - len, rate_stats->rate_config,
                                   NULL, ru_index);

        tp = me_rc_stats_cfm.tp[RC_HE_STATS_IDX] / 10;
        len += scnprintf(&buf[len], bufsz - len, "      %4u.%1u",
                         tp / 10, tp % 10);

        eprob = ((rate_stats->probability * 1000) >> 16) + 1;
        len += scnprintf(&buf[len],bufsz - len,
                         "  %4u.%1u %5u(%6u)  %6u\n",
                         eprob / 10, eprob % 10,
                         rate_stats->success,
                         rate_stats->attempts,
                         ul_length);
    }

    len += scnprintf(&buf[len], bufsz - len, "\n MPDUs AMPDUs AvLen trialP");
    len += scnprintf(&buf[len], bufsz - len, "\n%6u %6u %3d.%1d %6u\n",
                     me_rc_stats_cfm.ampdu_len,
                     me_rc_stats_cfm.ampdu_packets,
                     me_rc_stats_cfm.avg_ampdu_len >> 16,
                     ((me_rc_stats_cfm.avg_ampdu_len * 10) >> 16) % 10,
                     me_rc_stats_cfm.sample_wait);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);
    kfree(st);

    return read;
}

DEBUGFS_READ_FILE_OPS(rc_stats);

static ssize_t ecrnx_dbgfs_rc_fixed_rate_idx_write(struct file *file,
                                                  const char __user *user_buf,
                                                  size_t count, loff_t *ppos)
{
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_hw *priv = file->private_data;
    char buf[10];
    int fixed_rate_idx = -1;
    union ecrnx_rate_ctrl_info rate_config;
    int error = 0;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* Get the station index from MAC address */
    sta = ecrnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Get the content of the file */
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';
    sscanf(buf, "%i\n", &fixed_rate_idx);

    /* Convert rate index into rate configuration */
    if ((fixed_rate_idx < 0) || (fixed_rate_idx >= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU)))
    {
        // disable fixed rate
        rate_config.value = (u32)-1;
    }
    else
    {
        idx_to_rate_cfg(fixed_rate_idx, &rate_config, NULL);
    }

    // Forward the request to the LMAC
    if ((error = ecrnx_send_me_rc_set_rate(priv, sta->sta_idx,
                                          (u16)rate_config.value)) != 0)
    {
        return error;
    }

    priv->debugfs.rc_config[sta->sta_idx] = (int)rate_config.value;
    return len;
}

DEBUGFS_WRITE_FILE_OPS(rc_fixed_rate_idx);

static ssize_t ecrnx_dbgfs_last_rx_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_hw *priv = file->private_data;
    struct ecrnx_rx_rate_stats *rate_stats;
    char *buf;
    int bufsz, i, len = 0;
    ssize_t read;
    unsigned int fmt, pre, bw, nss, mcs, gi, dcm = 0;
    struct rx_vector_1 *last_rx;
    char hist[] = "##################################################";
    int hist_len = sizeof(hist) - 1;
    u8 nrx;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* everything should fit in one call */
    if (*ppos)
        return 0;

    /* Get the station index from MAC address */
    sta = ecrnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    rate_stats = &sta->stats.rx_rate;
    bufsz = (rate_stats->rate_cnt * ( 50 + hist_len) + 200);
    buf = kmalloc(bufsz + 1, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    // Get number of RX paths
    nrx = (priv->version_cfm.version_phy_1 & MDM_NRX_MASK) >> MDM_NRX_LSB;

    len += scnprintf(buf, bufsz,
                     "\nRX rate info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
                     sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
                     sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

    // Display Statistics
    for (i = 0 ; i < rate_stats->size ; i++ )
    {
        if (rate_stats->table[i]) {
            union ecrnx_rate_ctrl_info rate_config;
            int percent = ((/*(u64)*/rate_stats->table[i]) * 1000) / rate_stats->cpt;
            int p;
            int ru_size;

            idx_to_rate_cfg(i, &rate_config, &ru_size);
            len += print_rate_from_cfg(&buf[len], bufsz - len,
                                       rate_config.value, NULL, ru_size);
            p = (percent * hist_len) / 1000;
            len += scnprintf(&buf[len], bufsz - len, ": %9d(%2d.%1d%%)%.*s\n",
                             rate_stats->table[i],
                             percent / 10, percent % 10, p, hist);
        }
    }

    // Display detailed info of the last received rate
    last_rx = &sta->stats.last_rx.rx_vect1;

    len += scnprintf(&buf[len], bufsz - len,"\nLast received rate\n"
                     "  type         rate    LDPC STBC BEAMFM DCM DOPPLER %s\n",
                     (nrx > 1) ? "rssi1(dBm) rssi2(dBm)" : "rssi(dBm)");

    fmt = last_rx->format_mod;
    bw = last_rx->ch_bw;
    pre = last_rx->pre_type;
    if (fmt >= FORMATMOD_HE_SU) {
        mcs = last_rx->he.mcs;
        nss = last_rx->he.nss;
        gi = last_rx->he.gi_type;
        if (fmt == FORMATMOD_HE_MU)
            bw = last_rx->he.ru_size;
        dcm = last_rx->he.dcm;
    } else if (fmt == FORMATMOD_VHT) {
        mcs = last_rx->vht.mcs;
        nss = last_rx->vht.nss;
        gi = last_rx->vht.short_gi;
    } else if (fmt >= FORMATMOD_HT_MF) {
        mcs = last_rx->ht.mcs % 8;
        nss = last_rx->ht.mcs / 8;;
        gi = last_rx->ht.short_gi;
    } else {
        BUG_ON((mcs = legrates_lut[last_rx->leg_rate].idx) == -1);
        nss = 0;
        gi = 0;
    }

    len += print_rate(&buf[len], bufsz - len, fmt, nss, mcs, bw, gi, pre, dcm, NULL);

    /* flags for HT/VHT/HE */
    if (fmt >= FORMATMOD_HE_SU) {
        len += scnprintf(&buf[len], bufsz - len, "  %c    %c     %c    %c     %c",
                         last_rx->he.fec ? 'L' : ' ',
                         last_rx->he.stbc ? 'S' : ' ',
                         last_rx->he.beamformed ? 'B' : ' ',
                         last_rx->he.dcm ? 'D' : ' ',
                         last_rx->he.doppler ? 'D' : ' ');
    } else if (fmt == FORMATMOD_VHT) {
        len += scnprintf(&buf[len], bufsz - len, "  %c    %c     %c           ",
                         last_rx->vht.fec ? 'L' : ' ',
                         last_rx->vht.stbc ? 'S' : ' ',
                         last_rx->vht.beamformed ? 'B' : ' ');
    } else if (fmt >= FORMATMOD_HT_MF) {
        len += scnprintf(&buf[len], bufsz - len, "  %c    %c                  ",
                         last_rx->ht.fec ? 'L' : ' ',
                         last_rx->ht.stbc ? 'S' : ' ');
    } else {
        len += scnprintf(&buf[len], bufsz - len, "                         ");
    }
    if (nrx > 1) {
        len += scnprintf(&buf[len], bufsz - len, "       %-4d       %d\n",
                         last_rx->rssi1, last_rx->rssi1);
    } else {
        len += scnprintf(&buf[len], bufsz - len, "      %d\n", last_rx->rssi1);
    }

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);
    return read;
}

static ssize_t ecrnx_dbgfs_last_rx_write(struct file *file,
                                        const char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_hw *priv = file->private_data;

    /* Get the station index from MAC address */
    sta = ecrnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Prevent from interrupt preemption as these statistics are updated under
     * interrupt */
    spin_lock_bh(&priv->tx_lock);
    memset(sta->stats.rx_rate.table, 0,
           sta->stats.rx_rate.size * sizeof(sta->stats.rx_rate.table[0]));
    sta->stats.rx_rate.cpt = 0;
    sta->stats.rx_rate.rate_cnt = 0;
    spin_unlock_bh(&priv->tx_lock);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(last_rx);

#endif /* CONFIG_ECRNX_FULLMAC */

/*
 * trace helper
 */
void ecrnx_fw_trace_dump(struct ecrnx_hw *ecrnx_hw)
{
#ifndef CONFIG_ECRNX_ESWIN
    /* may be called before ecrnx_dbgfs_register */
    if (ecrnx_hw->plat->enabled && !ecrnx_hw->debugfs.fw_trace.buf.data) {
        ecrnx_fw_trace_buf_init(&ecrnx_hw->debugfs.fw_trace.buf,
                               ecrnx_ipc_fw_trace_desc_get(ecrnx_hw));
    }

    if (!ecrnx_hw->debugfs.fw_trace.buf.data)
        return;

    _ecrnx_fw_trace_dump(&ecrnx_hw->debugfs.fw_trace.buf);
#endif
}

void ecrnx_fw_trace_reset(struct ecrnx_hw *ecrnx_hw)
{
    _ecrnx_fw_trace_reset(&ecrnx_hw->debugfs.fw_trace, true);
}

void ecrnx_dbgfs_trigger_fw_dump(struct ecrnx_hw *ecrnx_hw, char *reason)
{
    ecrnx_send_dbg_trigger_req(ecrnx_hw, reason);
}

#ifdef CONFIG_ECRNX_FULLMAC
static void _ecrnx_dbgfs_register_sta(struct ecrnx_debugfs *ecrnx_debugfs, struct ecrnx_sta *sta)
{
    struct ecrnx_hw *ecrnx_hw = container_of(ecrnx_debugfs, struct ecrnx_hw, debugfs);
    struct dentry *dir_sta;
    char sta_name[18];
    struct dentry *dir_rc;
    struct dentry *file;
    struct ecrnx_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
    int nb_rx_rate = N_CCK + N_OFDM;
    struct ecrnx_rc_config_save *rc_cfg, *next;

    if (sta->sta_idx >= NX_REMOTE_STA_MAX) {
        scnprintf(sta_name, sizeof(sta_name), "bc_mc");
    } else {
        scnprintf(sta_name, sizeof(sta_name), "%pM", sta->mac_addr);
    }

    if (!(dir_sta = debugfs_create_dir(sta_name, ecrnx_debugfs->dir_stas)))
        goto error;
    ecrnx_debugfs->dir_sta[sta->sta_idx] = dir_sta;

    if (!(dir_rc = debugfs_create_dir("rc", ecrnx_debugfs->dir_sta[sta->sta_idx])))
        goto error_after_dir;

    ecrnx_debugfs->dir_rc_sta[sta->sta_idx] = dir_rc;

    file = debugfs_create_file("stats", S_IRUSR, dir_rc, ecrnx_hw,
                               &ecrnx_dbgfs_rc_stats_ops);
    if (IS_ERR_OR_NULL(file))
        goto error_after_dir;

    file = debugfs_create_file("fixed_rate_idx", S_IWUSR , dir_rc, ecrnx_hw,
                               &ecrnx_dbgfs_rc_fixed_rate_idx_ops);
    if (IS_ERR_OR_NULL(file))
        goto error_after_dir;

    file = debugfs_create_file("rx_rate", S_IRUSR | S_IWUSR, dir_rc, ecrnx_hw,
                               &ecrnx_dbgfs_last_rx_ops);
    if (IS_ERR_OR_NULL(file))
        goto error_after_dir;

    if (ecrnx_hw->mod_params->ht_on)
        nb_rx_rate += N_HT;

    if (ecrnx_hw->mod_params->vht_on)
        nb_rx_rate += N_VHT;

    if (ecrnx_hw->mod_params->he_on)
        nb_rx_rate += N_HE_SU + N_HE_MU;

    rate_stats->table = kzalloc(nb_rx_rate * sizeof(rate_stats->table[0]),
                                GFP_ATOMIC);
    if (!rate_stats->table)
        goto error_after_dir;

    rate_stats->size = nb_rx_rate;
    rate_stats->cpt = 0;
    rate_stats->rate_cnt = 0;

    /* By default enable rate contoller */
    ecrnx_debugfs->rc_config[sta->sta_idx] = -1;

    /* Unless we already fix the rate for this station */
    list_for_each_entry_safe(rc_cfg, next, &ecrnx_debugfs->rc_config_save, list) {
        if (jiffies_to_msecs(jiffies - rc_cfg->timestamp) > RC_CONFIG_DUR) {
            list_del(&rc_cfg->list);
            kfree(rc_cfg);
        } else if (!memcmp(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN)) {
            ecrnx_debugfs->rc_config[sta->sta_idx] = rc_cfg->rate;
            list_del(&rc_cfg->list);
            kfree(rc_cfg);
            break;
        }
    }

    if ((ecrnx_debugfs->rc_config[sta->sta_idx] >= 0) &&
        ecrnx_send_me_rc_set_rate(ecrnx_hw, sta->sta_idx,
                                 (u16)ecrnx_debugfs->rc_config[sta->sta_idx]))
        ecrnx_debugfs->rc_config[sta->sta_idx] = -1;

    if (ECRNX_VIF_TYPE(ecrnx_hw->vif_table[sta->vif_idx]) == NL80211_IFTYPE_STATION)
    {
        /* register the sta */
        struct dentry *dir_twt;
        struct dentry *file;

        if (!(dir_twt = debugfs_create_dir("twt", ecrnx_debugfs->dir_sta[sta->sta_idx])))
            goto error_after_dir;

        ecrnx_debugfs->dir_twt_sta[sta->sta_idx] = dir_twt;

        file = debugfs_create_file("request", S_IRUSR | S_IWUSR, dir_twt, ecrnx_hw,
                                   &ecrnx_dbgfs_twt_request_ops);
        if (IS_ERR_OR_NULL(file))
            goto error_after_dir;

        file = debugfs_create_file("teardown", S_IRUSR | S_IWUSR, dir_twt, ecrnx_hw,
                                   &ecrnx_dbgfs_twt_teardown_ops);
        if (IS_ERR_OR_NULL(file))
            goto error_after_dir;

        sta->twt_ind.sta_idx = ECRNX_INVALID_STA;
    }
    return;

    error_after_dir:
      debugfs_remove_recursive(ecrnx_debugfs->dir_sta[sta->sta_idx]);
      ecrnx_debugfs->dir_sta[sta->sta_idx] = NULL;
      ecrnx_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
      ecrnx_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
    error:
      dev_err(ecrnx_hw->dev,
              "Error while registering debug entry for sta %d\n", sta->sta_idx);
}

static void _ecrnx_dbgfs_unregister_sta(struct ecrnx_debugfs *ecrnx_debugfs, struct ecrnx_sta *sta)
{
    debugfs_remove_recursive(ecrnx_debugfs->dir_sta[sta->sta_idx]);
    /* unregister the sta */
    if (sta->stats.rx_rate.table) {
        kfree(sta->stats.rx_rate.table);
        sta->stats.rx_rate.table = NULL;
    }
    sta->stats.rx_rate.size = 0;
    sta->stats.rx_rate.cpt  = 0;
    sta->stats.rx_rate.rate_cnt = 0;

    /* If fix rate was set for this station, save the configuration in case
       we reconnect to this station within RC_CONFIG_DUR msec */
    if (ecrnx_debugfs->rc_config[sta->sta_idx] >= 0) {
        struct ecrnx_rc_config_save *rc_cfg;
        rc_cfg = kmalloc(sizeof(*rc_cfg), GFP_ATOMIC);
        if (rc_cfg) {
            rc_cfg->rate = ecrnx_debugfs->rc_config[sta->sta_idx];
            rc_cfg->timestamp = jiffies;
            memcpy(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN);
            list_add_tail(&rc_cfg->list, &ecrnx_debugfs->rc_config_save);
        }
    }

    ecrnx_debugfs->dir_sta[sta->sta_idx] = NULL;
    ecrnx_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
    ecrnx_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
    sta->twt_ind.sta_idx = ECRNX_INVALID_STA;
}

static void ecrnx_sta_work(struct work_struct *ws)
{
    struct ecrnx_debugfs *ecrnx_debugfs = container_of(ws, struct ecrnx_debugfs, sta_work);
    struct ecrnx_hw *ecrnx_hw = container_of(ecrnx_debugfs, struct ecrnx_hw, debugfs);
    struct ecrnx_sta *sta;
    uint8_t sta_idx;

    sta_idx = ecrnx_debugfs->sta_idx;
    if (sta_idx > (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
        WARN(1, "Invalid sta index %d", sta_idx);
        return;
    }

    ecrnx_debugfs->sta_idx = ECRNX_INVALID_STA;
    sta = &ecrnx_hw->sta_table[sta_idx];
    if (!sta) {
        WARN(1, "Invalid sta %d", sta_idx);
        return;
    }

    if (ecrnx_debugfs->dir_sta[sta_idx] == NULL)
        _ecrnx_dbgfs_register_sta(ecrnx_debugfs, sta);
    else
        _ecrnx_dbgfs_unregister_sta(ecrnx_debugfs, sta);

    return;
}

void _ecrnx_dbgfs_sta_write(struct ecrnx_debugfs *ecrnx_debugfs, uint8_t sta_idx)
{
    if (ecrnx_debugfs->unregistering)
        return;

    ecrnx_debugfs->sta_idx = sta_idx;
    schedule_work(&ecrnx_debugfs->sta_work);
}

void ecrnx_dbgfs_unregister_sta(struct ecrnx_hw *ecrnx_hw,
                               struct ecrnx_sta *sta)
{
    _ecrnx_dbgfs_sta_write(&ecrnx_hw->debugfs, sta->sta_idx);
}

void ecrnx_dbgfs_register_sta(struct ecrnx_hw *ecrnx_hw,
                             struct ecrnx_sta *sta)
{
    _ecrnx_dbgfs_sta_write(&ecrnx_hw->debugfs, sta->sta_idx);
}
#endif /* CONFIG_ECRNX_FULLMAC */

int ecrnx_dbgfs_register(struct ecrnx_hw *ecrnx_hw, const char *name)
{
#ifdef CONFIG_ECRNX_SOFTMAC
    struct dentry *phyd = ecrnx_hw->hw->wiphy->debugfsdir;
#else
    struct dentry *phyd = ecrnx_hw->wiphy->debugfsdir;
#endif /* CONFIG_ECRNX_SOFTMAC */
    struct ecrnx_debugfs *ecrnx_debugfs = &ecrnx_hw->debugfs;
    struct dentry *dir_drv, *dir_diags, *dir_stas;

    if (!(dir_drv = debugfs_create_dir(name, phyd)))
        return -ENOMEM;

    ecrnx_debugfs->dir = dir_drv;

    if (!(dir_stas = debugfs_create_dir("stations", dir_drv)))
        return -ENOMEM;

    ecrnx_debugfs->dir_stas = dir_stas;
    ecrnx_debugfs->unregistering = false;

    if (!(dir_diags = debugfs_create_dir("diags", dir_drv)))
        goto err;

#ifdef CONFIG_ECRNX_FULLMAC
    INIT_WORK(&ecrnx_debugfs->sta_work, ecrnx_sta_work);
    INIT_LIST_HEAD(&ecrnx_debugfs->rc_config_save);
    ecrnx_debugfs->sta_idx = ECRNX_INVALID_STA;
#endif

    DEBUGFS_ADD_U32(tcp_pacing_shift, dir_drv, &ecrnx_hw->tcp_pacing_shift,
                    S_IWUSR | S_IRUSR);
    DEBUGFS_ADD_FILE(stats, dir_drv, S_IWUSR | S_IRUSR);
#if 0
    DEBUGFS_ADD_FILE(sys_stats, dir_drv,  S_IRUSR);
#endif
#ifdef CONFIG_ECRNX_SOFTMAC
    DEBUGFS_ADD_X64(rateidx, dir_drv, &ecrnx_hw->debugfs.rateidx);
#endif
    DEBUGFS_ADD_FILE(txq, dir_drv, S_IRUSR);
    DEBUGFS_ADD_FILE(acsinfo, dir_drv, S_IRUSR);
#ifdef CONFIG_ECRNX_MUMIMO_TX
    DEBUGFS_ADD_FILE(mu_group, dir_drv, S_IRUSR);
#endif

#ifdef CONFIG_ECRNX_P2P_DEBUGFS
    {
        /* Create a p2p directory */
        struct dentry *dir_p2p;
        if (!(dir_p2p = debugfs_create_dir("p2p", dir_drv)))
            goto err;

        /* Add file allowing to control Opportunistic PS */
        DEBUGFS_ADD_FILE(oppps, dir_p2p, S_IRUSR);
        /* Add file allowing to control Notice of Absence */
        DEBUGFS_ADD_FILE(noa, dir_p2p, S_IRUSR);
    }
#endif /* CONFIG_ECRNX_P2P_DEBUGFS */

#if CONFIG_ECRNX_DBGFS_FW_TRACE
    if (ecrnx_dbgfs_register_fw_dump(ecrnx_hw, dir_drv, dir_diags))
        goto err;
    DEBUGFS_ADD_FILE(fw_dbg, dir_diags, S_IWUSR | S_IRUSR);

    if (!ecrnx_fw_trace_init(&ecrnx_hw->debugfs.fw_trace,
                            ecrnx_ipc_fw_trace_desc_get(ecrnx_hw))) {
        DEBUGFS_ADD_FILE(fw_trace, dir_diags, S_IWUSR | S_IRUSR);
        if (ecrnx_hw->debugfs.fw_trace.buf.nb_compo)
            DEBUGFS_ADD_FILE(fw_trace_level, dir_diags, S_IWUSR | S_IRUSR);
    } else {
        ecrnx_debugfs->fw_trace.buf.data = NULL;
    }
#endif

#ifdef CONFIG_ECRNX_RADAR
    {
        struct dentry *dir_radar, *dir_sec;
        if (!(dir_radar = debugfs_create_dir("radar", dir_drv)))
            goto err;

        DEBUGFS_ADD_FILE(pulses_prim, dir_radar, S_IRUSR);
        DEBUGFS_ADD_FILE(detected,    dir_radar, S_IRUSR);
        DEBUGFS_ADD_FILE(enable,      dir_radar, S_IRUSR);

        if (ecrnx_hw->phy.cnt == 2) {
            DEBUGFS_ADD_FILE(pulses_sec, dir_radar, S_IRUSR);

            if (!(dir_sec = debugfs_create_dir("sec", dir_radar)))
                goto err;

            DEBUGFS_ADD_FILE(band,    dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(type,    dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(prim20,  dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(center1, dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(center2, dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(set,     dir_sec, S_IWUSR | S_IRUSR);
        }
    }
#endif /* CONFIG_ECRNX_RADAR */
    return 0;

err:
    ecrnx_dbgfs_unregister(ecrnx_hw);
    return -ENOMEM;
}

void ecrnx_dbgfs_unregister(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_debugfs *ecrnx_debugfs = &ecrnx_hw->debugfs;

#ifdef CONFIG_ECRNX_FULLMAC
    struct ecrnx_rc_config_save *cfg, *next;
    list_for_each_entry_safe(cfg, next, &ecrnx_debugfs->rc_config_save, list) {
        list_del(&cfg->list);
        kfree(cfg);
    }
#endif /* CONFIG_ECRNX_FULLMAC */

    if (!ecrnx_hw->debugfs.dir)
        return;

    spin_lock_bh(&ecrnx_debugfs->umh_lock);
    ecrnx_debugfs->unregistering = true;
    spin_unlock_bh(&ecrnx_debugfs->umh_lock);
    ecrnx_wait_um_helper(ecrnx_hw);
#if CONFIG_ECRNX_DBGFS_FW_TRACE
    ecrnx_fw_trace_deinit(&ecrnx_hw->debugfs.fw_trace);
#endif
#ifdef CONFIG_ECRNX_FULLMAC
    flush_work(&ecrnx_debugfs->sta_work);
#endif
    debugfs_remove_recursive(ecrnx_hw->debugfs.dir);
    ecrnx_hw->debugfs.dir = NULL;
}

