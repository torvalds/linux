// SPDX-License-Identifier: GPL-2.0-or-later
/**
 ******************************************************************************
 *
 * @file rwnx_debugfs.c
 *
 * @brief Definition of debugfs entries
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */


#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/debugfs.h>
#include <linux/string.h>
#include <linux/sort.h>

#include "rwnx_debugfs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_radar.h"
#include "rwnx_tx.h"

#ifdef CONFIG_DEBUG_FS
#ifdef CONFIG_RWNX_FULLMAC
static ssize_t rwnx_dbgfs_stats_read(struct file *file,
                                     char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
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

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
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

#endif /* CONFIG_RWNX_SPLIT_TX_BUF */

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
    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    kfree(buf);

    return read;
}
#endif /* CONFIG_RWNX_FULLMAC */

static ssize_t rwnx_dbgfs_stats_write(struct file *file,
                                      const char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;

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

#ifdef CONFIG_RWNX_FULLMAC
#define TXQ_VIF_PREF "type|"
#define TXQ_VIF_PREF_FMT "%4s|"
#else
#define TXQ_VIF_PREF "AC|"
#define TXQ_VIF_PREF_FMT "%2s|"
#endif /* CONFIG_RWNX_FULLMAC */

#define TXQ_HDR "idx| status|credit|ready|retry"
#define TXQ_HDR_FMT "%3d|%s%s%s%s%s%s%s|%6d|%5d|%5d"

#ifdef CONFIG_RWNX_AMSDUS_TX
#ifdef CONFIG_RWNX_FULLMAC
#define TXQ_HDR_SUFF "|amsdu"
#define TXQ_HDR_SUFF_FMT "|%5d"
#else
#define TXQ_HDR_SUFF "|amsdu-ht|amdsu-vht"
#define TXQ_HDR_SUFF_FMT "|%8d|%9d"
#endif /* CONFIG_RWNX_FULLMAC */
#else
#define TXQ_HDR_SUFF ""
#define TXQ_HDR_SUF_FMT ""
#endif /* CONFIG_RWNX_AMSDUS_TX */

#define TXQ_HDR_MAX_LEN (sizeof(TXQ_STA_PREF) + sizeof(TXQ_HDR) + sizeof(TXQ_HDR_SUFF) + 1)

#ifdef CONFIG_RWNX_FULLMAC
#define PS_HDR  "Legacy PS: ready=%d, sp=%d / UAPSD: ready=%d, sp=%d"
#define PS_HDR_LEGACY "Legacy PS: ready=%d, sp=%d"
#define PS_HDR_UAPSD  "UAPSD: ready=%d, sp=%d"
#define PS_HDR_MAX_LEN  sizeof("Legacy PS: ready=xxx, sp=xxx / UAPSD: ready=xxx, sp=xxx\n")
#else
#define PS_HDR ""
#define PS_HDR_MAX_LEN 0
#endif /* CONFIG_RWNX_FULLMAC */

#define STA_HDR "** STA %d (%pM)\n"
#define STA_HDR_MAX_LEN sizeof("- STA xx (xx:xx:xx:xx:xx:xx)\n") + PS_HDR_MAX_LEN

#ifdef CONFIG_RWNX_FULLMAC
#define VIF_HDR "* VIF [%d] %s\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR) + IFNAMSIZ
#else
#define VIF_HDR "* VIF [%d]\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR)
#endif


#ifdef CONFIG_RWNX_AMSDUS_TX

#ifdef CONFIG_RWNX_FULLMAC
#define VIF_SEP "---------------------------------------\n"
#else
#define VIF_SEP "----------------------------------------------------\n"
#endif /* CONFIG_RWNX_FULLMAC */

#else /* ! CONFIG_RWNX_AMSDUS_TX */
#define VIF_SEP "---------------------------------\n"
#endif /* CONFIG_RWNX_AMSDUS_TX*/

#define VIF_SEP_LEN sizeof(VIF_SEP)

#define CAPTION "status: L=in hwq list, F=stop full, P=stop sta PS, V=stop vif PS, C=stop channel, S=stop CSA, M=stop MU"
#define CAPTION_LEN sizeof(CAPTION)

#define STA_TXQ 0
#define VIF_TXQ 1

static int rwnx_dbgfs_txq(char *buf, size_t size, struct rwnx_txq *txq, int type, int tid, char *name)
{
    int res, idx = 0;

    if (type == STA_TXQ) {
        res = scnprintf(&buf[idx], size, TXQ_STA_PREF_FMT, tid);
        idx += res;
        size -= res;
    } else {
        res = scnprintf(&buf[idx], size, TXQ_VIF_PREF_FMT, name);
        idx += res;
        size -= res;
    }

    res = scnprintf(&buf[idx], size, TXQ_HDR_FMT, txq->idx,
                    (txq->status & RWNX_TXQ_IN_HWQ_LIST) ? "L" : " ",
                    (txq->status & RWNX_TXQ_STOP_FULL) ? "F" : " ",
                    (txq->status & RWNX_TXQ_STOP_STA_PS) ? "P" : " ",
                    (txq->status & RWNX_TXQ_STOP_VIF_PS) ? "V" : " ",
                    (txq->status & RWNX_TXQ_STOP_CHAN) ? "C" : " ",
                    (txq->status & RWNX_TXQ_STOP_CSA) ? "S" : " ",
                    (txq->status & RWNX_TXQ_STOP_MU_POS) ? "M" : " ",
                    txq->credits, skb_queue_len(&txq->sk_list),
                    txq->nb_retry);
    idx += res;
    size -= res;

#ifdef CONFIG_RWNX_AMSDUS_TX
    if (type == STA_TXQ) {
        res = scnprintf(&buf[idx], size, TXQ_HDR_SUFF_FMT,
#ifdef CONFIG_RWNX_FULLMAC
                        txq->amsdu_len
#else
                        txq->amsdu_ht_len_cap, txq->amsdu_vht_len_cap
#endif /* CONFIG_RWNX_FULLMAC */
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

static int rwnx_dbgfs_txq_sta(char *buf, size_t size, struct rwnx_sta *rwnx_sta,
                              struct rwnx_hw *rwnx_hw)
{
    int tid, res, idx = 0;
    struct rwnx_txq *txq;

    res = scnprintf(&buf[idx], size, "\n" STA_HDR,
                    rwnx_sta->sta_idx,
#ifdef CONFIG_RWNX_FULLMAC
                    rwnx_sta->mac_addr
#endif /* CONFIG_RWNX_FULLMAC */
                    );
    idx += res;
    size -= res;

#ifdef CONFIG_RWNX_FULLMAC
    if (rwnx_sta->ps.active) {
        if (rwnx_sta->uapsd_tids &&
            (rwnx_sta->uapsd_tids == ((1 << NX_NB_TXQ_PER_STA) - 1)))
            res = scnprintf(&buf[idx], size, PS_HDR_UAPSD "\n",
                            rwnx_sta->ps.pkt_ready[UAPSD_ID],
                            rwnx_sta->ps.sp_cnt[UAPSD_ID]);
        else if (rwnx_sta->uapsd_tids)
            res = scnprintf(&buf[idx], size, PS_HDR "\n",
                            rwnx_sta->ps.pkt_ready[LEGACY_PS_ID],
                            rwnx_sta->ps.sp_cnt[LEGACY_PS_ID],
                            rwnx_sta->ps.pkt_ready[UAPSD_ID],
                            rwnx_sta->ps.sp_cnt[UAPSD_ID]);
        else
            res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
                            rwnx_sta->ps.pkt_ready[LEGACY_PS_ID],
                            rwnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
        idx += res;
        size -= res;
    } else {
        res = scnprintf(&buf[idx], size, "\n");
        idx += res;
        size -= res;
    }
#endif /* CONFIG_RWNX_FULLMAC */


    res = scnprintf(&buf[idx], size, TXQ_STA_PREF TXQ_HDR TXQ_HDR_SUFF "\n");
    idx += res;
    size -= res;


    foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw) {
        res = rwnx_dbgfs_txq(&buf[idx], size, txq, STA_TXQ, tid, NULL);
        idx += res;
        size -= res;
    }

    return idx;
}

static int rwnx_dbgfs_txq_vif(char *buf, size_t size, struct rwnx_vif *rwnx_vif,
                              struct rwnx_hw *rwnx_hw)
{
    int res, idx = 0;
    struct rwnx_txq *txq;
    struct rwnx_sta *rwnx_sta;

#ifdef CONFIG_RWNX_FULLMAC
    res = scnprintf(&buf[idx], size, VIF_HDR, rwnx_vif->vif_index, rwnx_vif->ndev->name);
    idx += res;
    size -= res;
    if (!rwnx_vif->up || rwnx_vif->ndev == NULL)
        return idx;

#else
    int ac;
    char ac_name[2] = {'0', '\0'};

    res = scnprintf(&buf[idx], size, VIF_HDR, rwnx_vif->vif_index);
    idx += res;
    size -= res;
#endif /* CONFIG_RWNX_FULLMAC */

#ifdef CONFIG_RWNX_FULLMAC
    if (RWNX_VIF_TYPE(rwnx_vif) ==  NL80211_IFTYPE_AP ||
        RWNX_VIF_TYPE(rwnx_vif) ==  NL80211_IFTYPE_P2P_GO ||
        RWNX_VIF_TYPE(rwnx_vif) ==  NL80211_IFTYPE_MESH_POINT) {
        res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
        idx += res;
        size -= res;
        txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
        res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "UNK");
        idx += res;
        size -= res;
        txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
        res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "BCMC");
        idx += res;
        size -= res;
        rwnx_sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
        if (rwnx_sta->ps.active) {
            res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
                            rwnx_sta->ps.sp_cnt[LEGACY_PS_ID],
                            rwnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
            idx += res;
            size -= res;
        } else {
            res = scnprintf(&buf[idx], size, "\n");
            idx += res;
            size -= res;
        }

        list_for_each_entry(rwnx_sta, &rwnx_vif->ap.sta_list, list) {
            res = rwnx_dbgfs_txq_sta(&buf[idx], size, rwnx_sta, rwnx_hw);
            idx += res;
            size -= res;
        }
    } else if (RWNX_VIF_TYPE(rwnx_vif) ==  NL80211_IFTYPE_STATION ||
               RWNX_VIF_TYPE(rwnx_vif) ==  NL80211_IFTYPE_P2P_CLIENT) {
        if (rwnx_vif->sta.ap) {
            res = rwnx_dbgfs_txq_sta(&buf[idx], size, rwnx_vif->sta.ap, rwnx_hw);
            idx += res;
            size -= res;
        }
    }

#else
    res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
    idx += res;
    size -= res;

    foreach_vif_txq(rwnx_vif, txq, ac) {
        ac_name[0]++;
        res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, ac_name);
        idx += res;
        size -= res;
    }

    list_for_each_entry(rwnx_sta, &rwnx_vif->stations, list) {
        res = rwnx_dbgfs_txq_sta(&buf[idx], size, rwnx_sta, rwnx_hw);
        idx += res;
        size -= res;
    }
#endif /* CONFIG_RWNX_FULLMAC */
    return idx;
}

static ssize_t rwnx_dbgfs_txq_read(struct file *file ,
                                   char __user *user_buf,
                                   size_t count, loff_t *ppos)
{
    struct rwnx_hw *rwnx_hw = file->private_data;
    struct rwnx_vif *vif;
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

    //spin_lock_bh(&rwnx_hw->tx_lock);
    list_for_each_entry(vif, &rwnx_hw->vifs, list) {
        res = scnprintf(&buf[idx], bufsz, "\n"VIF_SEP);
        idx += res;
        bufsz -= res;
        res = rwnx_dbgfs_txq_vif(&buf[idx], bufsz, vif, rwnx_hw);
        idx += res;
        bufsz -= res;
        res = scnprintf(&buf[idx], bufsz, VIF_SEP);
        idx += res;
        bufsz -= res;
    }
    //spin_unlock_bh(&rwnx_hw->tx_lock);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
    kfree(buf);

    return read;
}
DEBUGFS_READ_FILE_OPS(txq);

static ssize_t rwnx_dbgfs_acsinfo_read(struct file *file,
                                           char __user *user_buf,
                                           size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    #ifdef CONFIG_RWNX_FULLMAC
    struct wiphy *wiphy = priv->wiphy;
    #endif //CONFIG_RWNX_FULLMAC
    char buf[(SCAN_CHANNEL_MAX + 1) * 43];
    int survey_cnt = 0;
    int len = 0;
    int band, chan_cnt;
	int band_max = NL80211_BAND_5GHZ;

	if (priv->band_5g_support){
		band_max = NL80211_BAND_5GHZ + 1;
	}

    mutex_lock(&priv->dbgdump_elem.mutex);

    len += scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                     "FREQ    TIME(ms)    BUSY(ms)    NOISE(dBm)\n");


	//#ifdef USE_5G
    //for (band = NL80211_BAND_2GHZ; band <= NL80211_BAND_5GHZ; band++) {
	//#else
	//for (band = NL80211_BAND_2GHZ; band < NL80211_BAND_5GHZ; band++) {
	//#endif
	for (band = NL80211_BAND_2GHZ; band < band_max; band++) {
        for (chan_cnt = 0; chan_cnt < wiphy->bands[band]->n_channels; chan_cnt++) {
            struct rwnx_survey_info *p_survey_info = &priv->survey[survey_cnt];
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

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

DEBUGFS_READ_FILE_OPS(acsinfo);

static ssize_t rwnx_dbgfs_fw_dbg_read(struct file *file,
                                           char __user *user_buf,
                                           size_t count, loff_t *ppos)
{
    char help[]="usage: [MOD:<ALL|KE|DBG|IPC|DMA|MM|TX|RX|PHY>]* "
        "[DBG:<NONE|CRT|ERR|WRN|INF|VRB>]\n";

    return simple_read_from_buffer(user_buf, count, ppos, help, sizeof(help));
}


static ssize_t rwnx_dbgfs_fw_dbg_write(struct file *file,
                                            const char __user *user_buf,
                                            size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int idx = 0;
    u32 mod = 0;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';

#define RWNX_MOD_TOKEN(str, val)                                        \
    if (strncmp(&buf[idx], str, sizeof(str) - 1 ) == 0) {               \
        idx += sizeof(str) - 1;                                         \
        mod |= val;                                                     \
        continue;                                                       \
    }

#define RWNX_DBG_TOKEN(str, val)                                \
    if (strncmp(&buf[idx], str, sizeof(str) - 1) == 0) {        \
        idx += sizeof(str) - 1;                                 \
        dbg = val;                                              \
        goto dbg_done;                                          \
    }

    while ((idx + 4) < len) {
        if (strncmp(&buf[idx], "MOD:", 4) == 0) {
            idx += 4;
            RWNX_MOD_TOKEN("ALL", 0xffffffff);
            RWNX_MOD_TOKEN("KE",  BIT(0));
            RWNX_MOD_TOKEN("DBG", BIT(1));
            RWNX_MOD_TOKEN("IPC", BIT(2));
            RWNX_MOD_TOKEN("DMA", BIT(3));
            RWNX_MOD_TOKEN("MM",  BIT(4));
            RWNX_MOD_TOKEN("TX",  BIT(5));
            RWNX_MOD_TOKEN("RX",  BIT(6));
            RWNX_MOD_TOKEN("PHY", BIT(7));
            idx++;
        } else if (strncmp(&buf[idx], "DBG:", 4) == 0) {
            u32 dbg = 0;
            idx += 4;
            RWNX_DBG_TOKEN("NONE", 0);
            RWNX_DBG_TOKEN("CRT",  1);
            RWNX_DBG_TOKEN("ERR",  2);
            RWNX_DBG_TOKEN("WRN",  3);
            RWNX_DBG_TOKEN("INF",  4);
            RWNX_DBG_TOKEN("VRB",  5);
            idx++;
            continue;
          dbg_done:
            rwnx_send_dbg_set_sev_filter_req(priv, dbg);
        } else {
            idx++;
        }
    }

    if (mod) {
        rwnx_send_dbg_set_mod_filter_req(priv, mod);
    }

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg);

static ssize_t rwnx_dbgfs_sys_stats_read(struct file *file,
                                         char __user *user_buf,
                                         size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[3*64];
    int len = 0;
    ssize_t read;
    int error = 0;
    struct dbg_get_sys_stat_cfm cfm;
    u32 sleep_int, sleep_frac, doze_int, doze_frac;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Get the information from the FW */
    if ((error = rwnx_send_dbg_get_sys_stat_req(priv, &cfm)))
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

#ifdef CONFIG_RWNX_MUMIMO_TX
static ssize_t rwnx_dbgfs_mu_group_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct rwnx_hw *rwnx_hw = file->private_data;
    struct rwnx_mu_info *mu = &rwnx_hw->mu;
    struct rwnx_mu_group *group;
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

#ifdef CONFIG_RWNX_P2P_DEBUGFS
static ssize_t rwnx_dbgfs_oppps_write(struct file *file,
                                      const char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct rwnx_hw *rw_hw = file->private_data;
    struct rwnx_vif *rw_vif;
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
#ifdef CONFIG_RWNX_FULLMAC
            if (RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
#endif /* CONFIG_RWNX_FULLMAC */
                struct mm_set_p2p_oppps_cfm cfm;

                /* Forward request to the embedded and wait for confirmation */
                rwnx_send_p2p_oppps_req(rw_hw, rw_vif, (u8)ctw, &cfm);

                break;
            }
        }
    }

    return count;
}

DEBUGFS_WRITE_FILE_OPS(oppps);

static ssize_t rwnx_dbgfs_noa_write(struct file *file,
                                    const char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct rwnx_hw *rw_hw = file->private_data;
    struct rwnx_vif *rw_vif;
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
#ifdef CONFIG_RWNX_FULLMAC
            if (RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
#endif /* CONFIG_RWNX_FULLMAC */
                struct mm_set_p2p_noa_cfm cfm;

                /* Forward request to the embedded and wait for confirmation */
                rwnx_send_p2p_noa_req(rw_hw, rw_vif, noa_count, interval,
                                      duration, (dyn_noa > 0),  &cfm);

                break;
            }
        }
    }

    return count;
}

DEBUGFS_WRITE_FILE_OPS(noa);
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

static char fw_log_buffer[FW_LOG_SIZE];

static ssize_t rwnx_dbgfs_fw_log_read(struct file *file,
											  char __user *user_buf,
											  size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	size_t not_cpy;
	size_t nb_cpy;
	char *log = fw_log_buffer;

	printk("%s, %d, %p, %p\n", __func__, priv->debugfs.fw_log.buf.size, priv->debugfs.fw_log.buf.start, priv->debugfs.fw_log.buf.dataend);
	//spin_lock_bh(&priv->debugfs.fw_log.lock);

	if ((priv->debugfs.fw_log.buf.start + priv->debugfs.fw_log.buf.size) >= priv->debugfs.fw_log.buf.dataend) {
		memcpy(log, priv->debugfs.fw_log.buf.start, priv->debugfs.fw_log.buf.dataend - priv->debugfs.fw_log.buf.start);
		not_cpy = copy_to_user(user_buf, log, priv->debugfs.fw_log.buf.dataend - priv->debugfs.fw_log.buf.start);
		nb_cpy = priv->debugfs.fw_log.buf.dataend - priv->debugfs.fw_log.buf.start - not_cpy;
		priv->debugfs.fw_log.buf.start = priv->debugfs.fw_log.buf.data;
	} else {
		memcpy(log, priv->debugfs.fw_log.buf.start, priv->debugfs.fw_log.buf.size);
		not_cpy = copy_to_user(user_buf, log, priv->debugfs.fw_log.buf.size);
		nb_cpy = priv->debugfs.fw_log.buf.size - not_cpy;
		priv->debugfs.fw_log.buf.start = priv->debugfs.fw_log.buf.start + priv->debugfs.fw_log.buf.size - not_cpy;
	}

	priv->debugfs.fw_log.buf.size -= nb_cpy;
	//spin_unlock_bh(&priv->debugfs.fw_log.lock);

	printk("nb_cpy=%lu, not_cpy=%lu, start=%p, end=%p\n", (long unsigned int)nb_cpy, (long unsigned int)not_cpy, priv->debugfs.fw_log.buf.start, priv->debugfs.fw_log.buf.end);
	return nb_cpy;
}

static ssize_t rwnx_dbgfs_fw_log_write(struct file *file,
											   const char __user *user_buf,
											   size_t count, loff_t *ppos)
{
	//struct rwnx_hw *priv = file->private_data;

	printk("%s\n", __func__);
	return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(fw_log);

#ifdef CONFIG_RWNX_RADAR
static ssize_t rwnx_dbgfs_pulses_read(struct file *file,
                                      char __user *user_buf,
                                      size_t count, loff_t *ppos,
                                      int rd_idx)
{
    struct rwnx_hw *priv = file->private_data;
    char *buf;
    int len = 0;
    int bufsz;
    int i;
    int index;
    struct rwnx_radar_pulses *p = &priv->radar.pulses[rd_idx];
    ssize_t read;

    if (*ppos != 0)
        return 0;

    /* Prevent from interrupt preemption */
    spin_lock_bh(&priv->radar.lock);
    bufsz = p->count * 34 + 51;
    bufsz += rwnx_radar_dump_pattern_detector(NULL, 0, &priv->radar, rd_idx);
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
                index = RWNX_RADAR_PULSE_MAX - 1;

            pulse = (struct radar_pulse *) &p->buffer[index];

            len += scnprintf(&buf[len], bufsz - len,
                             "%05dus  %03dus     %2d%%    %+3dMHz\n", pulse->rep,
                             2 * pulse->len, 6 * pulse->fom, 2*pulse->freq);
        }
    }

    len += rwnx_radar_dump_pattern_detector(&buf[len], bufsz - len,
                                            &priv->radar, rd_idx);

    spin_unlock_bh(&priv->radar.lock);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);

    return read;
}

static ssize_t rwnx_dbgfs_pulses_prim_read(struct file *file,
                                           char __user *user_buf,
                                           size_t count, loff_t *ppos)
{
    return rwnx_dbgfs_pulses_read(file, user_buf, count, ppos, 0);
}

DEBUGFS_READ_FILE_OPS(pulses_prim);

static ssize_t rwnx_dbgfs_pulses_sec_read(struct file *file,
                                          char __user *user_buf,
                                          size_t count, loff_t *ppos)
{
    return rwnx_dbgfs_pulses_read(file, user_buf, count, ppos, 1);
}

DEBUGFS_READ_FILE_OPS(pulses_sec);

static ssize_t rwnx_dbgfs_detected_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char *buf;
    int bufsz,len = 0;
    ssize_t read;

    if (*ppos != 0)
        return 0;

    bufsz = 5; // RIU:\n
    bufsz += rwnx_radar_dump_radar_detected(NULL, 0, &priv->radar,
                                            RWNX_RADAR_RIU);

    if (priv->phy.cnt > 1) {
        bufsz += 5; // FCU:\n
        bufsz += rwnx_radar_dump_radar_detected(NULL, 0, &priv->radar,
                                                RWNX_RADAR_FCU);
    }

    buf = kmalloc(bufsz, GFP_KERNEL);
    if (buf == NULL) {
        return 0;
    }

    len = scnprintf(&buf[len], bufsz, "RIU:\n");
    len += rwnx_radar_dump_radar_detected(&buf[len], bufsz - len, &priv->radar,
                                            RWNX_RADAR_RIU);

    if (priv->phy.cnt > 1) {
        len += scnprintf(&buf[len], bufsz - len, "FCU:\n");
        len += rwnx_radar_dump_radar_detected(&buf[len], bufsz - len,
                                              &priv->radar, RWNX_RADAR_FCU);
    }

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);

    return read;
}

DEBUGFS_READ_FILE_OPS(detected);

static ssize_t rwnx_dbgfs_enable_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "RIU=%d FCU=%d\n", priv->radar.dpd[RWNX_RADAR_RIU]->enabled,
                    priv->radar.dpd[RWNX_RADAR_FCU]->enabled);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t rwnx_dbgfs_enable_write(struct file *file,
                                     const char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "RIU=%d", &val) > 0)
        rwnx_radar_detection_enable(&priv->radar, val, RWNX_RADAR_RIU);

    if (sscanf(buf, "FCU=%d", &val) > 0)
        rwnx_radar_detection_enable(&priv->radar, val, RWNX_RADAR_FCU);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(enable);

static ssize_t rwnx_dbgfs_band_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "BAND=%d\n", priv->phy.sec_chan.band);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t rwnx_dbgfs_band_write(struct file *file,
                                     const char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);
	int band_max = NL80211_BAND_5GHZ;

	if (priv->band_5g_support){
		band_max = NL80211_BAND_5GHZ + 1;
	}

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

//	#ifdef USE_5G
//    if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) && (val <= NL80211_BAND_5GHZ))
//	#else
//	if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) && (val < NL80211_BAND_5GHZ))
//	#endif
	if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) && (val < band_max)){
        priv->phy.sec_chan.band = val;
	}

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(band);

static ssize_t rwnx_dbgfs_type_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "TYPE=%d\n", priv->phy.sec_chan.type);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t rwnx_dbgfs_type_write(struct file *file,
                                     const char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
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

static ssize_t rwnx_dbgfs_prim20_read(struct file *file,
                                      char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "PRIM20=%dMHz\n", priv->phy.sec_chan.prim20_freq);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t rwnx_dbgfs_prim20_write(struct file *file,
                                       const char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
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

static ssize_t rwnx_dbgfs_center1_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "CENTER1=%dMHz\n", priv->phy.sec_chan.center_freq1);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t rwnx_dbgfs_center1_write(struct file *file,
                                        const char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.center_freq1 = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center1);

static ssize_t rwnx_dbgfs_center2_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "CENTER2=%dMHz\n", priv->phy.sec_chan.center_freq2);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

static ssize_t rwnx_dbgfs_center2_write(struct file *file,
                                        const char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.center_freq2 = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center2);


static ssize_t rwnx_dbgfs_set_read(struct file *file,
                                   char __user *user_buf,
                                   size_t count, loff_t *ppos)
{
    return 0;
}

static ssize_t rwnx_dbgfs_set_write(struct file *file,
                                    const char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct rwnx_hw *priv = file->private_data;

    rwnx_send_set_channel(priv, 1, NULL);
    rwnx_radar_detection_enable(&priv->radar, RWNX_RADAR_DETECT_ENABLE,
                                RWNX_RADAR_FCU);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(set);
#endif /* CONFIG_RWNX_RADAR */

static ssize_t rwnx_dbgfs_regdbg_write(struct file *file,
			const char __user *user_buf,
			size_t count, loff_t *ppos)
{

	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	u32 addr,val, oper;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
    	struct dbg_mem_read_cfm mem_read_cfm;
    	int ret;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

    	buf[len] = '\0';

	if (sscanf(buf, "%x %x %x" , &oper, &addr, &val ) > 0) 
		printk("addr=%x, val=%x,oper=%d\n", addr, val, oper);

    	if(oper== 0) {
		ret = rwnx_send_dbg_mem_read_req(priv, addr, &mem_read_cfm);
        	printk("[0x%x] = [0x%x]\n", mem_read_cfm.memaddr, mem_read_cfm.memdata);
    	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(regdbg);

static ssize_t rwnx_dbgfs_vendor_hwconfig_write(struct file *file,
			const char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[64];
	int32_t addr[9];
	u32_l hwconfig_id;
	size_t len = min_t(size_t,count,sizeof(buf)-1);
	int ret;
    printk("%s\n",__func__);
	//choose the type of write info by struct
	//struct mm_set_vendor_trx_param_req trx_param;

	if(copy_from_user(buf,user_buf,len)) {
		return -EFAULT;
	}

	buf[len] = '\0';
	ret = sscanf(buf, "%x %x %x %x %x %x %x %x %x %x",
                            &hwconfig_id, &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5], &addr[6], &addr[7], &addr[8]);
	if(ret > 10) {
		printk("param error > 10\n");
	} else {
		switch(hwconfig_id)
		    {
		    case 0:
			if(ret != 5) {
			    printk("param error  != 5\n");
			    break;}
			ret = rwnx_send_vendor_hwconfig_req(priv, hwconfig_id, addr);
			printk("ACS_TXOP_REQ bk:0x%x be:0x%x vi:0x%x vo:0x%x\n",addr[0],  addr[1], addr[2], addr[3]);
			break;
		    case 1:
			if(ret != 10) {
			    printk("param error  != 10\n");
			    break;}
			ret = rwnx_send_vendor_hwconfig_req(priv, hwconfig_id, addr);
			printk("CHANNEL_ACCESS_REQ edca:%x,%x,%x,%x, vif:%x, retry_cnt:%x, rts:%x, long_nav:%x, cfe:%x\n",
                                addr[0],  addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7], addr[8]);
			break;
		    case 2:
			if(ret != 7) {
		            printk("param error  != 7\n");
			    break;}
			ret = rwnx_send_vendor_hwconfig_req(priv, hwconfig_id, addr);
			printk("MAC_TIMESCALE_REQ sifsA:%x,sifsB:%x,slot:%x,ofdm_delay:%x,long_delay:%x,short_delay:%x\n",
                                addr[0],  addr[1], addr[2], addr[3], addr[4], addr[5]);
			break;
		    case 3:
                        if(ret != 6) {
		            printk("param error  != 6\n");
			    break;}
			addr[1] = ~addr[1] + 1;
			addr[2] = ~addr[2] + 1;
			addr[3] = ~addr[3] + 1;
			addr[4] = ~addr[4] + 1;
			ret = rwnx_send_vendor_hwconfig_req(priv, hwconfig_id, addr);
			printk("CCA_THRESHOLD_REQ auto_cca:%d, cca20p_rise:%d cca20s_rise:%d cca20p_fail:%d cca20s_fail:%d\n",
                                addr[0],  addr[1], addr[2], addr[3], addr[4]);
			break;
		    default:
			printk("param error\n");
			break;
		}
		if(ret) {
		    printk("rwnx_send_vendor_hwconfig_req fail: %x\n", ret);
		}
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(vendor_hwconfig)

#ifdef CONFIG_RWNX_FULLMAC

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
                      int sgi, int pre, int *r_idx)
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
            res += scnprintf(&buf[res], size - res, "L-CCK/%cP      %2u.%1uM    ",
                             pre > 0 ? 'L' : 'S',
                             bitrates_cck[mcs] / 10,
                             bitrates_cck[mcs] % 10);
        } else {
            mcs -= 4;
            if (r_idx) {
                *r_idx = N_CCK + mcs;
                res = scnprintf(buf, size - res, "%3d ", *r_idx);
            }
            res += scnprintf(&buf[res], size - res, "L-OFDM        %2u.0M    ",
                             bitrates_ofdm[mcs]);
        }
    } else if (format < FORMATMOD_VHT) {
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 + bw * 2 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        mcs += nss * 8;
        res += scnprintf(&buf[res], size - res, "HT%d/%cGI       MCS%-2d   ",
                         20 * (1 << bw), sgi ? 'S' : 'L', mcs);
    } else if (format == FORMATMOD_VHT){
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + nss * 80 + mcs * 8 + bw * 2 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        res += scnprintf(&buf[res], size - res, "VHT%d/%cGI%*cMCS%d/%1d  ",
                         20 * (1 << bw), sgi ? 'S' : 'L', bw > 2 ? 5 : 6, ' ',
                         mcs, nss + 1);
    } else if (format == FORMATMOD_HE_SU){
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + N_VHT + nss * 144 + mcs * 12 + bw * 3 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        res += scnprintf(&buf[res], size - res, "HE%d/GI%s%*cMCS%d/%1d%*c",
                         20 * (1 << bw), he_gi[sgi], bw > 2 ? 4 : 5, ' ',
                         mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');
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
    union rwnx_rate_ctrl_info *r_cfg = (union rwnx_rate_ctrl_info *)&rate_config;
    union rwnx_mcs_index *mcs_index = (union rwnx_mcs_index *)&rate_config;
    unsigned int ft, pre, gi, bw, nss, mcs, len;

    ft = r_cfg->formatModTx;
    pre = r_cfg->giAndPreTypeTx >> 1;
    gi = r_cfg->giAndPreTypeTx;
    bw = r_cfg->bwTx;
    if (ft == FORMATMOD_HE_MU) {
        mcs = mcs_index->he.mcs;
        nss = mcs_index->he.nss;
        bw = ru_size;
    } else if (ft == FORMATMOD_HE_SU) {
        mcs = mcs_index->he.mcs;
        nss = mcs_index->he.nss;
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

    len = print_rate(buf, size, ft, nss, mcs, bw, gi, pre, r_idx);
    return len;
}

static void idx_to_rate_cfg(int idx, union rwnx_rate_ctrl_info *r_cfg, int *ru_size)
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
        union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM);
        r_cfg->formatModTx = FORMATMOD_HT_MF;
        r->ht.nss = idx / (8*2*2);
        r->ht.mcs = (idx % (8*2*2)) / (2*2);
        r_cfg->bwTx = ((idx % (8*2*2)) % (2*2)) / 2;
        r_cfg->giAndPreTypeTx = idx & 1;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT))
    {
        union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM + N_HT);
        r_cfg->formatModTx = FORMATMOD_VHT;
        r->vht.nss = idx / (10*4*2);
        r->vht.mcs = (idx % (10*4*2)) / (4*2);
        r_cfg->bwTx = ((idx % (10*4*2)) % (4*2)) / 2;
        r_cfg->giAndPreTypeTx = idx & 1;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU))
    {
        union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM + N_HT + N_VHT);
        r_cfg->formatModTx = FORMATMOD_HE_SU;
        r->vht.nss = idx / (12*4*3);
        r->vht.mcs = (idx % (12*4*3)) / (4*3);
        r_cfg->bwTx = ((idx % (12*4*3)) % (4*3)) / 3;
        r_cfg->giAndPreTypeTx = idx % 3;
    }
    else
    {
        union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

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

static void idx_to_rate_cfg1(unsigned int formatmod,
	unsigned int mcs,unsigned int nss,
	unsigned int bwTx,unsigned int gi,
	 union rwnx_rate_ctrl_info *r_cfg, int *ru_size)
{
    r_cfg->value = 0;

    switch(formatmod){
		case FORMATMOD_NON_HT:
		{
			r_cfg->formatModTx = formatmod;
			r_cfg->giAndPreTypeTx = 1;
			r_cfg->mcsIndexTx = mcs;
            break;
		}
		case FORMATMOD_NON_HT_DUP_OFDM:
		{
			r_cfg->formatModTx = formatmod;
			r_cfg->giAndPreTypeTx = gi;
			r_cfg->mcsIndexTx = mcs;
            break;
		}
        case FORMATMOD_HT_MF:
		{
			union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

			r_cfg->formatModTx = formatmod;
            r->ht.nss = nss;
            r->ht.mcs = mcs;
            r_cfg->bwTx = bwTx;
            r_cfg->giAndPreTypeTx = gi;
            break;
        }
        case FORMATMOD_VHT:
        case FORMATMOD_HE_SU:
        {
			union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

			r_cfg->formatModTx = formatmod;
            r->vht.nss = nss;
            r->vht.mcs = mcs;
            r_cfg->bwTx = bwTx;
            r_cfg->giAndPreTypeTx = gi;
            break;
        }
        case FORMATMOD_HE_MU:
        {
			union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

			r_cfg->formatModTx = formatmod;
            r->he.nss = nss;
            r->he.mcs = mcs;
            r_cfg->bwTx = 0;
            r_cfg->giAndPreTypeTx = gi;
            break;
        }
        default:
            printk("Don't have the formatmod");
    }
}

static ssize_t rwnx_dbgfs_rc_stats_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct rwnx_sta *sta = NULL;
    struct rwnx_hw *priv = file->private_data;
    char *buf;
    int bufsz, len = 0;
    ssize_t read;
    int i = 0;
    int error = 0;
    struct me_rc_stats_cfm me_rc_stats_cfm;
    unsigned int no_samples;
    struct st *st;
    u8 mac[6];

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* everything should fit in one call */
    if (*ppos)
        return 0;

    /* Get the station index from MAC address */
    sscanf(file->f_path.dentry->d_parent->d_iname, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    //if (mac == NULL)
    //    return 0;
    sta = rwnx_get_sta(priv, mac);
    if (sta == NULL)
        return 0;

    /* Forward the information to the LMAC */
    if ((error = rwnx_send_me_rc_stats(priv, sta->sta_idx, &me_rc_stats_cfm)))
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
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    len += scnprintf(&buf[len], bufsz - len,
            " #  type           rate             tpt   eprob    ok(   tot)   skipped\n");

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
                "    type           rate             tpt   eprob    ok(   tot)   ul_length\n    ");
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

static ssize_t rwnx_dbgfs_rc_fixed_rate_idx_write(struct file *file,
                                                  const char __user *user_buf,
                                                  size_t count, loff_t *ppos)
{
    struct rwnx_sta *sta = NULL;
    struct rwnx_hw *priv = file->private_data;
    u8 mac[6];
    char buf[10];
    int fixed_rate_idx = 1;
	unsigned int formatmod, mcs, nss, bwTx, gi;
    union rwnx_rate_ctrl_info rate_config;
    int error = 0;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Get the station index from MAC address */
    sscanf(file->f_path.dentry->d_parent->d_iname, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    if (mac == NULL)
        return 0;
    sta = rwnx_get_sta(priv, mac);
    if (sta == NULL)
        return 0;

    /* Get the content of the file */
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';
    //sscanf(buf, "%i\n", &fixed_rate_idx);
	sscanf(buf, "%u %u %u %u %u",&formatmod, &mcs, &nss, &bwTx, &gi);
	//printk("%u %u %u %u %u\n",formatmod, mcs, nss, bwTx, gi);
    /* Convert rate index into rate configuration */
    if ((fixed_rate_idx < 0) || (fixed_rate_idx >= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU)))
    {
        // disable fixed rate
        rate_config.value = (u32)-1;
    }
    else
    {
        //idx_to_rate_cfg(fixed_rate_idx, &rate_config, NULL);
        idx_to_rate_cfg1(formatmod, mcs, nss, bwTx, gi, &rate_config, NULL);
    }
	/*union rwnx_rate_ctrl_info *r_cfg=&rate_config;
	printk("formatModTx=%u mcsIndexTx=%u bwTx=%u giAndPreTypeTx=%u\n",r_cfg->formatModTx,r_cfg->mcsIndexTx,r_cfg->bwTx,r_cfg->giAndPreTypeTx);
	printk("you wen ti");*/
	// Forward the request to the LMAC
    if ((error = rwnx_send_me_rc_set_rate(priv, sta->sta_idx,
                                          (u16)rate_config.value)) != 0)
    {
        return error;
    }

    priv->debugfs.rc_config[sta->sta_idx] = (int)rate_config.value;
    return len;

}


DEBUGFS_WRITE_FILE_OPS(rc_fixed_rate_idx);

static ssize_t rwnx_dbgfs_last_rx_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct rwnx_sta *sta = NULL;
    struct rwnx_hw *priv = file->private_data;
    struct rwnx_rx_rate_stats *rate_stats;
    char *buf;
    int bufsz, i, len = 0;
    ssize_t read;
    unsigned int fmt, pre, bw, nss, mcs, gi;
    u8 mac[6];
    struct rx_vector_1 *last_rx;
    char hist[] = "##################################################";
    int hist_len = sizeof(hist) - 1;
    u8 nrx;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* everything should fit in one call */
    if (*ppos)
        return 0;

    /* Get the station index from MAC address */
    sscanf(file->f_path.dentry->d_parent->d_iname, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    //if (mac == NULL)
    //    return 0;
    sta = rwnx_get_sta(priv, mac);
    if (sta == NULL)
        return 0;

    rate_stats = &sta->stats.rx_rate;
    bufsz = (rate_stats->rate_cnt * ( 50 + hist_len) + 200);
    buf = kmalloc(bufsz + 1, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    // Get number of RX paths
    nrx = (priv->version_cfm.version_phy_1 & MDM_NRX_MASK) >> MDM_NRX_LSB;

    len += scnprintf(buf, bufsz,
                     "\nRX rate info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Display Statistics
    for (i = 0 ; i < rate_stats->size ; i++ )
    {
        if (rate_stats->table[i]) {
            union rwnx_rate_ctrl_info rate_config;
            int percent = (rate_stats->table[i] * 1000) / rate_stats->cpt;
            int p;
            int ru_size;

            idx_to_rate_cfg(i, &rate_config, &ru_size);
            len += print_rate_from_cfg(&buf[len], bufsz - len,
                                       rate_config.value, NULL, ru_size);
            p = (percent * hist_len) / 1000;
            len += scnprintf(&buf[len], bufsz - len, ": %6d(%3d.%1d%%)%.*s\n",
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
    } else if (fmt == FORMATMOD_VHT) {
        mcs = last_rx->vht.mcs;
        nss = last_rx->vht.nss;
        gi = last_rx->vht.short_gi;
    } else if (fmt >= FORMATMOD_HT_MF) {
        mcs = last_rx->ht.mcs % 8;
        nss = last_rx->ht.mcs / 8;;
        gi = last_rx->ht.short_gi;
    } else {
        BUG_ON((mcs = legrates_lut[last_rx->leg_rate]) == -1);
        nss = 0;
        gi = 0;
    }

    len += print_rate(&buf[len], bufsz - len, fmt, nss, mcs, bw, gi, pre, NULL);

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

static ssize_t rwnx_dbgfs_last_rx_write(struct file *file,
                                        const char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct rwnx_sta *sta = NULL;
    struct rwnx_hw *priv = file->private_data;
    u8 mac[6];

    /* Get the station index from MAC address */
    sscanf(file->f_path.dentry->d_parent->d_iname, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    //if (mac == NULL)
    //    return 0;
    sta = rwnx_get_sta(priv, mac);
    if (sta == NULL)
        return 0;

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

#endif /* CONFIG_RWNX_FULLMAC */

#ifdef CONFIG_RWNX_FULLMAC
static void rwnx_rc_stat_work(struct work_struct *ws)
{
    struct rwnx_debugfs *rwnx_debugfs = container_of(ws, struct rwnx_debugfs,
                                                     rc_stat_work);
    struct rwnx_hw *rwnx_hw = container_of(rwnx_debugfs, struct rwnx_hw,
                                           debugfs);
    struct rwnx_sta *sta;
    uint8_t ridx, sta_idx;

    ridx = rwnx_debugfs->rc_read;
    sta_idx = rwnx_debugfs->rc_sta[ridx];
    if (sta_idx > (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
        WARN(1, "Invalid sta index %d", sta_idx);
        return;
    }

    rwnx_debugfs->rc_sta[ridx] = 0xFF;
    ridx = (ridx + 1) % ARRAY_SIZE(rwnx_debugfs->rc_sta);
    rwnx_debugfs->rc_read = ridx;
    sta = &rwnx_hw->sta_table[sta_idx];
    if (!sta) {
        WARN(1, "Invalid sta %d", sta_idx);
        return;
    }

    if (rwnx_debugfs->dir_sta[sta_idx] == NULL) {
        /* register the sta */
        struct dentry *dir_rc = rwnx_debugfs->dir_rc;
        struct dentry *dir_sta;
        struct dentry *file;
        char sta_name[18];
        struct rwnx_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
        int nb_rx_rate = N_CCK + N_OFDM;
        struct rwnx_rc_config_save *rc_cfg, *next;

        if (sta->sta_idx >= NX_REMOTE_STA_MAX) {
            scnprintf(sta_name, sizeof(sta_name), "bc_mc");
        } else {
            scnprintf(sta_name, sizeof(sta_name), "%pM", sta->mac_addr);
        }

        if (!(dir_sta = debugfs_create_dir(sta_name, dir_rc)))
            goto error;

        rwnx_debugfs->dir_sta[sta->sta_idx] = dir_sta;

        file = debugfs_create_file("stats", S_IRUSR, dir_sta, rwnx_hw,
                                   &rwnx_dbgfs_rc_stats_ops);
        if (IS_ERR_OR_NULL(file))
            goto error_after_dir;

        file = debugfs_create_file("fixed_rate_idx", S_IWUSR , dir_sta, rwnx_hw,
                                   &rwnx_dbgfs_rc_fixed_rate_idx_ops);
        if (IS_ERR_OR_NULL(file))
            goto error_after_dir;

        file = debugfs_create_file("rx_rate", S_IRUSR | S_IWUSR, dir_sta, rwnx_hw,
                                   &rwnx_dbgfs_last_rx_ops);
        if (IS_ERR_OR_NULL(file))
            goto error_after_dir;

        if (rwnx_hw->mod_params->ht_on)
            nb_rx_rate += N_HT;

        if (rwnx_hw->mod_params->vht_on)
            nb_rx_rate += N_VHT;

        if (rwnx_hw->mod_params->he_on)
            nb_rx_rate += N_HE_SU + N_HE_MU;

        rate_stats->table = kzalloc(nb_rx_rate * sizeof(rate_stats->table[0]),
                                    GFP_KERNEL);
        if (!rate_stats->table)
            goto error_after_dir;

        rate_stats->size = nb_rx_rate;
        rate_stats->cpt = 0;
        rate_stats->rate_cnt = 0;

        /* By default enable rate contoller */
        rwnx_debugfs->rc_config[sta_idx] = -1;

        /* Unless we already fix the rate for this station */
        list_for_each_entry_safe(rc_cfg, next, &rwnx_debugfs->rc_config_save, list) {
            if (jiffies_to_msecs(jiffies - rc_cfg->timestamp) > RC_CONFIG_DUR) {
                list_del(&rc_cfg->list);
                kfree(rc_cfg);
            } else if (!memcmp(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN)) {
                rwnx_debugfs->rc_config[sta_idx] = rc_cfg->rate;
                list_del(&rc_cfg->list);
                kfree(rc_cfg);
                break;
            }
        }

        if ((rwnx_debugfs->rc_config[sta_idx] >= 0) &&
            rwnx_send_me_rc_set_rate(rwnx_hw, sta_idx,
                                     (u16)rwnx_debugfs->rc_config[sta_idx]))
            rwnx_debugfs->rc_config[sta_idx] = -1;

    } else {
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
        if (rwnx_debugfs->rc_config[sta_idx] >= 0) {
            struct rwnx_rc_config_save *rc_cfg;
            rc_cfg = kmalloc(sizeof(*rc_cfg), GFP_KERNEL);
            if (rc_cfg) {
                rc_cfg->rate = rwnx_debugfs->rc_config[sta_idx];
                rc_cfg->timestamp = jiffies;
                memcpy(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN);
                list_add_tail(&rc_cfg->list, &rwnx_debugfs->rc_config_save);
            }
        }

        debugfs_remove_recursive(rwnx_debugfs->dir_sta[sta_idx]);
        rwnx_debugfs->dir_sta[sta->sta_idx] = NULL;
    }

    return;

  error_after_dir:
    debugfs_remove_recursive(rwnx_debugfs->dir_sta[sta_idx]);
    rwnx_debugfs->dir_sta[sta->sta_idx] = NULL;
  error:
    dev_err(rwnx_hw->dev,
            "Error while (un)registering debug entry for sta %d\n", sta_idx);
}

void _rwnx_dbgfs_rc_stat_write(struct rwnx_debugfs *rwnx_debugfs, uint8_t sta_idx)
{
    uint8_t widx = rwnx_debugfs->rc_write;
    if (rwnx_debugfs->rc_sta[widx] != 0XFF) {
        WARN(1, "Overlap in debugfs rc_sta table\n");
    }

    if (rwnx_debugfs->unregistering)
        return;

    rwnx_debugfs->rc_sta[widx] = sta_idx;
    widx = (widx + 1) % ARRAY_SIZE(rwnx_debugfs->rc_sta);
    rwnx_debugfs->rc_write = widx;

    schedule_work(&rwnx_debugfs->rc_stat_work);
}

void rwnx_dbgfs_register_rc_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
    _rwnx_dbgfs_rc_stat_write(&rwnx_hw->debugfs, sta->sta_idx);
}

void rwnx_dbgfs_unregister_rc_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
    _rwnx_dbgfs_rc_stat_write(&rwnx_hw->debugfs, sta->sta_idx);
}
#endif /* CONFIG_RWNX_FULLMAC */

int rwnx_dbgfs_register(struct rwnx_hw *rwnx_hw, const char *name)
{
#ifdef CONFIG_RWNX_FULLMAC
    struct dentry *phyd = rwnx_hw->wiphy->debugfsdir;
    struct dentry *dir_rc;
#endif /* CONFIG_RWNX_FULLMAC */
    struct rwnx_debugfs *rwnx_debugfs = &rwnx_hw->debugfs;
    struct dentry *dir_drv, *dir_diags;

    if (!(dir_drv = debugfs_create_dir(name, phyd)))
        return -ENOMEM;

    rwnx_debugfs->dir = dir_drv;
    rwnx_debugfs->unregistering = false;

    if (!(dir_diags = debugfs_create_dir("diags", dir_drv)))
        goto err;

#ifdef CONFIG_RWNX_FULLMAC
    if (!(dir_rc = debugfs_create_dir("rc", dir_drv)))
        goto err;
    rwnx_debugfs->dir_rc = dir_rc;
    INIT_WORK(&rwnx_debugfs->rc_stat_work, rwnx_rc_stat_work);
    INIT_LIST_HEAD(&rwnx_debugfs->rc_config_save);
    rwnx_debugfs->rc_write = rwnx_debugfs->rc_read = 0;
    memset(rwnx_debugfs->rc_sta, 0xFF, sizeof(rwnx_debugfs->rc_sta));
#endif

    DEBUGFS_ADD_U32(tcp_pacing_shift, dir_drv, &rwnx_hw->tcp_pacing_shift,
                    S_IWUSR | S_IRUSR);
    DEBUGFS_ADD_FILE(stats, dir_drv, S_IWUSR | S_IRUSR);
    DEBUGFS_ADD_FILE(sys_stats, dir_drv,  S_IRUSR);
    DEBUGFS_ADD_FILE(txq, dir_drv, S_IRUSR);
    DEBUGFS_ADD_FILE(acsinfo, dir_drv, S_IRUSR);
#ifdef CONFIG_RWNX_MUMIMO_TX
    DEBUGFS_ADD_FILE(mu_group, dir_drv, S_IRUSR);
#endif
    DEBUGFS_ADD_FILE(regdbg, dir_drv, S_IWUSR);
	DEBUGFS_ADD_FILE(vendor_hwconfig, dir_drv,S_IWUSR);

#ifdef CONFIG_RWNX_P2P_DEBUGFS
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
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

    if (rwnx_hw->fwlog_en) {
        rwnx_fw_log_init(&rwnx_hw->debugfs.fw_log);
        DEBUGFS_ADD_FILE(fw_log, dir_drv, S_IWUSR | S_IRUSR);
    }
#ifdef CONFIG_RWNX_RADAR
    {
        struct dentry *dir_radar, *dir_sec;
        if (!(dir_radar = debugfs_create_dir("radar", dir_drv)))
            goto err;

        DEBUGFS_ADD_FILE(pulses_prim, dir_radar, S_IRUSR);
        DEBUGFS_ADD_FILE(detected,    dir_radar, S_IRUSR);
        DEBUGFS_ADD_FILE(enable,      dir_radar, S_IRUSR);

        if (rwnx_hw->phy.cnt == 2) {
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
#endif /* CONFIG_RWNX_RADAR */
    return 0;

err:
    rwnx_dbgfs_unregister(rwnx_hw);
    return -ENOMEM;
}

void rwnx_dbgfs_unregister(struct rwnx_hw *rwnx_hw)
{
    struct rwnx_debugfs *rwnx_debugfs = &rwnx_hw->debugfs;
#ifdef CONFIG_RWNX_FULLMAC
        struct rwnx_rc_config_save *cfg, *next;
#endif

#ifdef CONFIG_RWNX_FULLMAC
    list_for_each_entry_safe(cfg, next, &rwnx_debugfs->rc_config_save, list) {
        list_del(&cfg->list);
        kfree(cfg);
    }
#endif /* CONFIG_RWNX_FULLMAC */

    if (rwnx_hw->fwlog_en)
        rwnx_fw_log_deinit(&rwnx_hw->debugfs.fw_log);

    if (!rwnx_hw->debugfs.dir)
        return;

    rwnx_debugfs->unregistering = true;
#ifdef CONFIG_RWNX_FULLMAC
    flush_work(&rwnx_debugfs->rc_stat_work);
#endif
    debugfs_remove_recursive(rwnx_hw->debugfs.dir);
    rwnx_hw->debugfs.dir = NULL;
}

#endif //
