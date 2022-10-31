/**
 ******************************************************************************
 *
 * @file ecrnx_main.c
 *
 * @brief Entry point of the ECRNX driver
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/inetdevice.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/etherdevice.h>

#include "ecrnx_defs.h"
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#include <linux/if_arp.h>
#include <linux/ieee80211.h>
#endif
#include "ecrnx_msg_tx.h"
#include "ecrnx_tx.h"
#include "reg_access.h"
#include "hal_desc.h"
#include "ecrnx_debugfs.h"
#include "ecrnx_cfgfile.h"
#include "ecrnx_radar.h"
#include "ecrnx_version.h"
#ifdef CONFIG_ECRNX_BFMER
#include "ecrnx_bfmer.h"
#endif //(CONFIG_ECRNX_BFMER)
#include "ecrnx_tdls.h"
#include "ecrnx_events.h"
#include "ecrnx_compat.h"
#include "ecrnx_rx.h"

#include "ecrnx_p2p.h"
#include "ecrnx_debugfs_custom.h"
#include "ecrnx_calibration_data.h"
#include "eswin_utils.h"
#include "ecrnx_debugfs_func.h"


static struct ieee80211_rate ecrnx_ratetable[] = {
    RATE(10,  0x00, 0),
    RATE(20,  0x01, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(55,  0x02, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(110, 0x03, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(60,  0x04, 0),
    RATE(90,  0x05, 0),
    RATE(120, 0x06, 0),
    RATE(180, 0x07, 0),
    RATE(240, 0x08, 0),
    RATE(360, 0x09, 0),
    RATE(480, 0x0A, 0),
    RATE(540, 0x0B, 0),
};

/* The channels indexes here are not used anymore */
static struct ieee80211_channel ecrnx_2ghz_channels[] = {
    CHAN(2412),
    CHAN(2417),
    CHAN(2422),
    CHAN(2427),
    CHAN(2432),
    CHAN(2437),
    CHAN(2442),
    CHAN(2447),
    CHAN(2452),
    CHAN(2457),
    CHAN(2462),
    CHAN(2467),
    CHAN(2472),
    CHAN(2484),
    // Extra channels defined only to be used for PHY measures.
    // Enabled only if custregd and custchan parameters are set
    CHAN(2390),
    CHAN(2400),
    CHAN(2410),
    CHAN(2420),
    CHAN(2430),
    CHAN(2440),
    CHAN(2450),
    CHAN(2460),
    CHAN(2470),
    CHAN(2480),
    CHAN(2490),
    CHAN(2500),
    CHAN(2510),
};

#ifdef CONFIG_ECRNX_5G
static struct ieee80211_channel ecrnx_5ghz_channels[] = {
    CHAN(5180),             // 36 -   20MHz
    CHAN(5200),             // 40 -   20MHz
    CHAN(5220),             // 44 -   20MHz
    CHAN(5240),             // 48 -   20MHz
    CHAN(5260),             // 52 -   20MHz
    CHAN(5280),             // 56 -   20MHz
    CHAN(5300),             // 60 -   20MHz
    CHAN(5320),             // 64 -   20MHz
    CHAN(5500),             // 100 -  20MHz
    CHAN(5520),             // 104 -  20MHz
    CHAN(5540),             // 108 -  20MHz
    CHAN(5560),             // 112 -  20MHz
    CHAN(5580),             // 116 -  20MHz
    CHAN(5600),             // 120 -  20MHz
    CHAN(5620),             // 124 -  20MHz
    CHAN(5640),             // 128 -  20MHz
    CHAN(5660),             // 132 -  20MHz
    CHAN(5680),             // 136 -  20MHz
    CHAN(5700),             // 140 -  20MHz
    CHAN(5720),             // 144 -  20MHz
    CHAN(5745),             // 149 -  20MHz
    CHAN(5765),             // 153 -  20MHz
    CHAN(5785),             // 157 -  20MHz
    CHAN(5805),             // 161 -  20MHz
    CHAN(5825),             // 165 -  20MHz
    // Extra channels defined only to be used for PHY measures.
    // Enabled only if custregd and custchan parameters are set
    CHAN(5190),
    CHAN(5210),
    CHAN(5230),
    CHAN(5250),
    CHAN(5270),
    CHAN(5290),
    CHAN(5310),
    CHAN(5330),
    CHAN(5340),
    CHAN(5350),
    CHAN(5360),
    CHAN(5370),
    CHAN(5380),
    CHAN(5390),
    CHAN(5400),
    CHAN(5410),
    CHAN(5420),
    CHAN(5430),
    CHAN(5440),
    CHAN(5450),
    CHAN(5460),
    CHAN(5470),
    CHAN(5480),
    CHAN(5490),
    CHAN(5510),
    CHAN(5530),
    CHAN(5550),
    CHAN(5570),
    CHAN(5590),
    CHAN(5610),
    CHAN(5630),
    CHAN(5650),
    CHAN(5670),
    CHAN(5690),
    CHAN(5710),
    CHAN(5730),
    CHAN(5750),
    CHAN(5760),
    CHAN(5770),
    CHAN(5780),
    CHAN(5790),
    CHAN(5800),
    CHAN(5810),
    CHAN(5820),
    CHAN(5830),
    CHAN(5840),
    CHAN(5850),
    CHAN(5860),
    CHAN(5870),
    CHAN(5880),
    CHAN(5890),
    CHAN(5900),
    CHAN(5910),
    CHAN(5920),
    CHAN(5930),
    CHAN(5940),
    CHAN(5950),
    CHAN(5960),
    CHAN(5970),
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
#if CONFIG_ECRNX_HE
static struct ieee80211_sband_iftype_data ecrnx_he_capa = {
    .types_mask = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP),
    .he_cap = ECRNX_HE_CAPABILITIES,
};
#endif
#endif

static struct ieee80211_supported_band ecrnx_band_2GHz = {
    .channels   = ecrnx_2ghz_channels,
    .n_channels = ARRAY_SIZE(ecrnx_2ghz_channels) - 13, // -13 to exclude extra channels
    .bitrates   = ecrnx_ratetable,
    .n_bitrates = ARRAY_SIZE(ecrnx_ratetable),
    .ht_cap     = ECRNX_HT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
#if CONFIG_ECRNX_HE
    .iftype_data = &ecrnx_he_capa,
    .n_iftype_data = 1,
#endif
#endif
};

#ifdef CONFIG_ECRNX_5G
static struct ieee80211_supported_band ecrnx_band_5GHz = {
    .channels   = ecrnx_5ghz_channels,
    .n_channels = ARRAY_SIZE(ecrnx_5ghz_channels) - 59, // -59 to exclude extra channels
    .bitrates   = &ecrnx_ratetable[4],
    .n_bitrates = ARRAY_SIZE(ecrnx_ratetable) - 4,
    .ht_cap     = ECRNX_HT_CAPABILITIES,
    .vht_cap    = ECRNX_VHT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
#if CONFIG_ECRNX_HE
    .iftype_data = &ecrnx_he_capa,
    .n_iftype_data = 1,
#endif
#endif
};
#endif

static struct ieee80211_iface_limit ecrnx_limits[] = {
    { .max = NX_VIRT_DEV_MAX, .types = BIT(NL80211_IFTYPE_AP) |
                                       BIT(NL80211_IFTYPE_STATION)}
};

static struct ieee80211_iface_limit ecrnx_limits_dfs[] = {
    { .max = NX_VIRT_DEV_MAX, .types = BIT(NL80211_IFTYPE_AP)}
};

static const struct ieee80211_iface_combination ecrnx_combinations[] = {
    {
        .limits                 = ecrnx_limits,
        .n_limits               = ARRAY_SIZE(ecrnx_limits),
        .num_different_channels = NX_CHAN_CTXT_CNT,
        .max_interfaces         = NX_VIRT_DEV_MAX,
    },
    /* Keep this combination as the last one */
    {
        .limits                 = ecrnx_limits_dfs,
        .n_limits               = ARRAY_SIZE(ecrnx_limits_dfs),
        .num_different_channels = 1,
        .max_interfaces         = NX_VIRT_DEV_MAX,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
        .radar_detect_widths = (BIT(NL80211_CHAN_WIDTH_20_NOHT) |
                                BIT(NL80211_CHAN_WIDTH_20) |
                                BIT(NL80211_CHAN_WIDTH_40) |
                                BIT(NL80211_CHAN_WIDTH_80)),
#endif
    }
};

/* There isn't a lot of sense in it, but you can transmit anything you like */
static struct ieee80211_txrx_stypes
ecrnx_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
    [NL80211_IFTYPE_STATION] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4)),
    },
    [NL80211_IFTYPE_AP] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_AP_VLAN] = {
        /* copy AP */
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_P2P_CLIENT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
    [NL80211_IFTYPE_P2P_GO] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_P2P_DEVICE] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
    [NL80211_IFTYPE_MESH_POINT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4)),
    },
};


static u32 cipher_suites[] = {
    WLAN_CIPHER_SUITE_WEP40,
    WLAN_CIPHER_SUITE_WEP104,
    WLAN_CIPHER_SUITE_TKIP,
    WLAN_CIPHER_SUITE_CCMP,
    0, // reserved entries to enable AES-CMAC and/or SMS4
    0,
    0,
    0,
    0,
};
#define NB_RESERVED_CIPHER 5;

static const int ecrnx_ac2hwq[1][NL80211_NUM_ACS] = {
    {
        [NL80211_TXQ_Q_VO] = ECRNX_HWQ_VO,
        [NL80211_TXQ_Q_VI] = ECRNX_HWQ_VI,
        [NL80211_TXQ_Q_BE] = ECRNX_HWQ_BE,
        [NL80211_TXQ_Q_BK] = ECRNX_HWQ_BK
    }
};

const int ecrnx_tid2hwq[IEEE80211_NUM_TIDS] = {
    ECRNX_HWQ_BE,
    ECRNX_HWQ_BK,
    ECRNX_HWQ_BK,
    ECRNX_HWQ_BE,
    ECRNX_HWQ_VI,
    ECRNX_HWQ_VI,
    ECRNX_HWQ_VO,
    ECRNX_HWQ_VO,
    /* TID_8 is used for management frames */
    ECRNX_HWQ_VO,
    /* At the moment, all others TID are mapped to BE */
    ECRNX_HWQ_BE,
    ECRNX_HWQ_BE,
    ECRNX_HWQ_BE,
    ECRNX_HWQ_BE,
    ECRNX_HWQ_BE,
    ECRNX_HWQ_BE,
    ECRNX_HWQ_BE,
};

static const int ecrnx_hwq2uapsd[NL80211_NUM_ACS] = {
    [ECRNX_HWQ_VO] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VO,
    [ECRNX_HWQ_VI] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VI,
    [ECRNX_HWQ_BE] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BE,
    [ECRNX_HWQ_BK] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BK,
};

/*********************************************************************
 * helper
 *********************************************************************/
struct ecrnx_sta *ecrnx_get_sta(struct ecrnx_hw *ecrnx_hw, const u8 *mac_addr)
{
    int i;

    for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
        struct ecrnx_sta *sta = &ecrnx_hw->sta_table[i];
        if (sta->valid && (memcmp(mac_addr, &sta->mac_addr, 6) == 0))
            return sta;
    }

    return NULL;
}

void ecrnx_enable_wapi(struct ecrnx_hw *ecrnx_hw)
{
    cipher_suites[ecrnx_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_SMS4;
    ecrnx_hw->wiphy->n_cipher_suites ++;
    ecrnx_hw->wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;
}

void ecrnx_enable_mfp(struct ecrnx_hw *ecrnx_hw)
{
    cipher_suites[ecrnx_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_AES_CMAC;
    ecrnx_hw->wiphy->n_cipher_suites ++;
}

void ecrnx_enable_gcmp(struct ecrnx_hw *ecrnx_hw)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
    cipher_suites[ecrnx_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_CCMP_256;
    cipher_suites[ecrnx_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_GCMP;
    cipher_suites[ecrnx_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_GCMP_256;
#endif
}
u8 *ecrnx_build_bcn(struct ecrnx_bcn *bcn, struct cfg80211_beacon_data *new)
{
    u8 *buf, *pos;

    if (new->head) {
        u8 *head = kmalloc(new->head_len, GFP_KERNEL);

        if (!head)
            return NULL;

        if (bcn->head)
            kfree(bcn->head);

        bcn->head = head;
        bcn->head_len = new->head_len;
        memcpy(bcn->head, new->head, new->head_len);
    }
    if (new->tail) {
        u8 *tail = kmalloc(new->tail_len, GFP_KERNEL);

        if (!tail)
            return NULL;

        if (bcn->tail)
            kfree(bcn->tail);

        bcn->tail = tail;
        bcn->tail_len = new->tail_len;
        memcpy(bcn->tail, new->tail, new->tail_len);
    }

    if (!bcn->head)
        return NULL;

    bcn->tim_len = 6;
    bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;

    buf = kmalloc(bcn->len, GFP_KERNEL);
    if (!buf)
        return NULL;

    // Build the beacon buffer
    pos = buf;
    memcpy(pos, bcn->head, bcn->head_len);
    pos += bcn->head_len;
    *pos++ = WLAN_EID_TIM;
    *pos++ = 4;
    *pos++ = 0;
    *pos++ = bcn->dtim;
    *pos++ = 0;
    *pos++ = 0;
    if (bcn->tail) {
        memcpy(pos, bcn->tail, bcn->tail_len);
        pos += bcn->tail_len;
    }
    if (bcn->ies) {
        memcpy(pos, bcn->ies, bcn->ies_len);
    }

    return buf;
}


static void ecrnx_del_bcn(struct ecrnx_bcn *bcn)
{
    if (bcn->head) {
        kfree(bcn->head);
        bcn->head = NULL;
    }
    bcn->head_len = 0;

    if (bcn->tail) {
        kfree(bcn->tail);
        bcn->tail = NULL;
    }
    bcn->tail_len = 0;

    if (bcn->ies) {
        kfree(bcn->ies);
        bcn->ies = NULL;
    }
    bcn->ies_len = 0;
    bcn->tim_len = 0;
    bcn->dtim = 0;
    bcn->len = 0;
}

/**
 * Link channel ctxt to a vif and thus increments count for this context.
 */
void ecrnx_chanctx_link(struct ecrnx_vif *vif, u8 ch_idx,
                       struct cfg80211_chan_def *chandef)
{
    struct ecrnx_chanctx *ctxt;

    if (ch_idx >= NX_CHAN_CTXT_CNT) {
        WARN(1, "Invalid channel ctxt id %d", ch_idx);
        return;
    }

    vif->ch_index = ch_idx;
    ctxt = &vif->ecrnx_hw->chanctx_table[ch_idx];
    ctxt->count++;

    // For now chandef is NULL for STATION interface
    if (chandef) {
        if (!ctxt->chan_def.chan)
            ctxt->chan_def = *chandef;
        else {
            // TODO. check that chandef is the same as the one already
            // set for this ctxt
        }
    }
}

/**
 * Unlink channel ctxt from a vif and thus decrements count for this context
 */
void ecrnx_chanctx_unlink(struct ecrnx_vif *vif)
{
    struct ecrnx_chanctx *ctxt;

    if (vif->ch_index == ECRNX_CH_NOT_SET)
        return;

    ctxt = &vif->ecrnx_hw->chanctx_table[vif->ch_index];

    if (ctxt->count == 0) {
        WARN(1, "Chan ctxt ref count is already 0");
    } else {
        ctxt->count--;
    }

    if (ctxt->count == 0) {
        if (vif->ch_index == vif->ecrnx_hw->cur_chanctx) {
            /* If current chan ctxt is no longer linked to a vif
               disable radar detection (no need to check if it was activated) */
            ecrnx_radar_detection_enable(&vif->ecrnx_hw->radar,
                                        ECRNX_RADAR_DETECT_DISABLE,
                                        ECRNX_RADAR_RIU);
        }
        /* set chan to null, so that if this ctxt is relinked to a vif that
           don't have channel information, don't use wrong information */
        ctxt->chan_def.chan = NULL;
    }
    vif->ch_index = ECRNX_CH_NOT_SET;
}

int ecrnx_chanctx_valid(struct ecrnx_hw *ecrnx_hw, u8 ch_idx)
{
    if (ch_idx >= NX_CHAN_CTXT_CNT ||
        ecrnx_hw->chanctx_table[ch_idx].chan_def.chan == NULL) {
        return 0;
    }

    return 1;
}

static void ecrnx_del_csa(struct ecrnx_vif *vif)
{
    struct ecrnx_hw *ecrnx_hw = vif->ecrnx_hw;
    struct ecrnx_csa *csa = vif->ap.csa;

    if (!csa)
        return;

    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &csa->elem);
    ecrnx_del_bcn(&csa->bcn);
    kfree(csa);
    vif->ap.csa = NULL;
}

static void ecrnx_csa_finish(struct work_struct *ws)
{
    struct ecrnx_csa *csa = container_of(ws, struct ecrnx_csa, work);
    struct ecrnx_vif *vif = csa->vif;
    struct ecrnx_hw *ecrnx_hw = vif->ecrnx_hw;
    int error = csa->status;

    if (!error)
        error = ecrnx_send_bcn_change(ecrnx_hw, vif->vif_index, csa->elem.dma_addr,
                                     csa->bcn.len, csa->bcn.head_len,
                                     csa->bcn.tim_len, NULL);

    if (error){
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
        cfg80211_stop_iface(ecrnx_hw->wiphy, &vif->wdev, GFP_KERNEL);
#else
        cfg80211_disconnected(vif->ndev, 0, NULL, 0, 0, GFP_KERNEL);
#endif
    }
    else {
        mutex_lock(&vif->wdev.mtx);
        __acquire(&vif->wdev.mtx);
        spin_lock_bh(&ecrnx_hw->cb_lock);
        ecrnx_chanctx_unlink(vif);
        ecrnx_chanctx_link(vif, csa->ch_idx, &csa->chandef);
        if (ecrnx_hw->cur_chanctx == csa->ch_idx) {
            ecrnx_radar_detection_enable_on_cur_channel(ecrnx_hw);
            ecrnx_txq_vif_start(vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
        } else
            ecrnx_txq_vif_stop(vif, ECRNX_TXQ_STOP_CHAN, ecrnx_hw);
        spin_unlock_bh(&ecrnx_hw->cb_lock);
        cfg80211_ch_switch_notify(vif->ndev, &csa->chandef);
        mutex_unlock(&vif->wdev.mtx);
        __release(&vif->wdev.mtx);
    }
    ecrnx_del_csa(vif);
}

/**
 * ecrnx_external_auth_enable - Enable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be enabled
 *
 * External authentication requires to start TXQ for unknown STA in
 * order to send auth frame pusehd by user space.
 * Note: It is assumed that fw is on the correct channel.
 */
void ecrnx_external_auth_enable(struct ecrnx_vif *vif)
{
    vif->sta.flags |= ECRNX_STA_EXT_AUTH;
    ecrnx_txq_unk_vif_init(vif);
    ecrnx_txq_start(ecrnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE), 0);
}

/**
 * ecrnx_external_auth_disable - Disable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be disabled
 */
void ecrnx_external_auth_disable(struct ecrnx_vif *vif)
{
    if (!(vif->sta.flags & ECRNX_STA_EXT_AUTH))
        return;

    vif->sta.flags &= ~ECRNX_STA_EXT_AUTH;
    ecrnx_txq_unk_vif_deinit(vif);
}

/**
 * ecrnx_update_mesh_power_mode -
 *
 * @vif: mesh VIF  for which power mode is updated
 *
 * Does nothing if vif is not a mesh point interface.
 * Since firmware doesn't support one power save mode per link select the
 * most "active" power mode among all mesh links.
 * Indeed as soon as we have to be active on one link we might as well be
 * active on all links.
 *
 * If there is no link then the power mode for next peer is used;
 */
void ecrnx_update_mesh_power_mode(struct ecrnx_vif *vif)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    enum nl80211_mesh_power_mode mesh_pm;
    struct ecrnx_sta *sta;
    struct mesh_config mesh_conf;
    struct mesh_update_cfm cfm;
    u32 mask;

    if (ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT)
        return;

    if (list_empty(&vif->ap.sta_list)) {
        mesh_pm = vif->ap.next_mesh_pm;
    } else {
        mesh_pm = NL80211_MESH_POWER_DEEP_SLEEP;
        list_for_each_entry(sta, &vif->ap.sta_list, list) {
            if (sta->valid && (sta->mesh_pm < mesh_pm)) {
                mesh_pm = sta->mesh_pm;
            }
        }
    }

    if (mesh_pm == vif->ap.mesh_pm)
        return;

    mask = BIT(NL80211_MESHCONF_POWER_MODE - 1);
    mesh_conf.power_mode = mesh_pm;
    if (ecrnx_send_mesh_update_req(vif->ecrnx_hw, vif, mask, &mesh_conf, &cfm) ||
        cfm.status)
        return;

    vif->ap.mesh_pm = mesh_pm;
#endif
}

void ecrnx_save_assoc_info_for_ft(struct ecrnx_vif *vif,
                                 struct cfg80211_connect_params *sme)
{
    int ies_len = sme->ie_len + sme->ssid_len + 2;
    u8 *pos;
    if (!vif->sta.ft_assoc_ies) {
        if (!cfg80211_find_ie(WLAN_EID_MOBILITY_DOMAIN, sme->ie, sme->ie_len))
            return;
        vif->sta.ft_assoc_ies_len = ies_len;
        vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
    } else if (vif->sta.ft_assoc_ies_len < ies_len) {
        kfree(vif->sta.ft_assoc_ies);
        vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
    }
    if (!vif->sta.ft_assoc_ies)
        return;
    pos = vif->sta.ft_assoc_ies;
    *pos++ = WLAN_EID_SSID;
    *pos++ = sme->ssid_len;
    memcpy(pos, sme->ssid, sme->ssid_len);
    pos += sme->ssid_len;
    memcpy(pos, sme->ie, sme->ie_len);
    vif->sta.ft_assoc_ies_len = ies_len;
}
/**
 * ecrnx_rsne_to_connect_params - Initialise cfg80211_connect_params from
 * RSN element.
 *
 * @rsne: RSN element
 * @sme: Structure cfg80211_connect_params to initialize
 *
 * The goal is only to initialize enough for ecrnx_send_sm_connect_req
 */
int ecrnx_rsne_to_connect_params(const struct ecrnx_element *rsne,
                                struct cfg80211_connect_params *sme)
{
    int len = rsne->datalen;
    int clen;
    const u8 *pos = rsne->data ;
    if (len < 8)
        return 1;

    sme->crypto.control_port_no_encrypt = false;
    sme->crypto.control_port = true;
    sme->crypto.control_port_ethertype = cpu_to_be16(ETH_P_PAE);

    pos += 2;
    sme->crypto.cipher_group = ntohl(*((u32 *)pos));
    pos += 4;
    clen = le16_to_cpu(*((u16 *)pos)) * 4;
    pos += 2;
    len -= 8;
    if (len < clen + 2)
        return 1;
    // only need one cipher suite
    sme->crypto.n_ciphers_pairwise = 1;
    sme->crypto.ciphers_pairwise[0] = ntohl(*((u32 *)pos));
    pos += clen;
    len -= clen;

    // no need for AKM
    clen = le16_to_cpu(*((u16 *)pos)) * 4;
    pos += 2;
    len -= 2;
    if (len < clen)
        return 1;
    pos += clen;
    len -= clen;

    if (len < 4)
        return 0;

    pos += 2;
    clen = le16_to_cpu(*((u16 *)pos)) * 16;
    len -= 4;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    if (len > clen)
        sme->mfp = NL80211_MFP_REQUIRED;
#endif

    return 0;
}

/*********************************************************************
 * netdev callbacks
 ********************************************************************/
/**
 * int (*ndo_open)(struct net_device *dev);
 *     This function is called when network device transistions to the up
 *     state.
 *
 * - Start FW if this is the first interface opened
 * - Add interface at fw level
 */
static int ecrnx_open(struct net_device *dev)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;
    struct mm_add_if_cfm add_if_cfm;
    int error = 0;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    // Check if it is the first opened VIF
    if (ecrnx_hw->vif_started == 0)
    {
        // Start the FW
       if ((error = ecrnx_send_start(ecrnx_hw)))
           return error;

       /* Device is now started */
       set_bit(ECRNX_DEV_STARTED, &ecrnx_hw->flags);
    }

    if (ecrnx_vif->up) {
        netdev_info(dev, "Started repeatedly");
        return error;
    }

    if (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP_VLAN) {
        /* For AP_vlan use same fw and drv indexes. We ensure that this index
           will not be used by fw for another vif by taking index >= NX_VIRT_DEV_MAX */
        add_if_cfm.inst_nbr = ecrnx_vif->drv_vif_index;
        netif_tx_stop_all_queues(dev);
    } else {
        /* Forward the information to the LMAC,
         *     p2p value not used in FMAC configuration, iftype is sufficient */
        if ((error = ecrnx_send_add_if(ecrnx_hw, dev->dev_addr,
                                      ECRNX_VIF_TYPE(ecrnx_vif), false, &add_if_cfm)))
            return error;

        if (add_if_cfm.status != 0) {
            ECRNX_PRINT_CFM_ERR(add_if);
            return -EIO;
        }
    }

    /* Save the index retrieved from LMAC */
    spin_lock_bh(&ecrnx_hw->cb_lock);
    ecrnx_vif->vif_index = add_if_cfm.inst_nbr;
    ecrnx_vif->up = true;
    ecrnx_hw->vif_started++;
    ecrnx_hw->vif_table[add_if_cfm.inst_nbr] = ecrnx_vif;
    memset(ecrnx_hw->vif_table[add_if_cfm.inst_nbr]->rx_pn, 0, TID_MAX * sizeof(uint64_t));
    spin_unlock_bh(&ecrnx_hw->cb_lock);

    if (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_MONITOR) {
        ecrnx_hw->monitor_vif = ecrnx_vif->vif_index;
        if (ecrnx_vif->ch_index != ECRNX_CH_NOT_SET) {
            //Configure the monitor channel
            error = ecrnx_send_config_monitor_req(ecrnx_hw,
                                                 &ecrnx_hw->chanctx_table[ecrnx_vif->ch_index].chan_def,
                                                 NULL);
        }
    }

    netif_carrier_off(dev);

    return error;
}

/**
 * int (*ndo_stop)(struct net_device *dev);
 *     This function is called when network device transistions to the down
 *     state.
 *
 * - Remove interface at fw level
 * - Reset FW if this is the last interface opened
 */
static int ecrnx_close(struct net_device *dev)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = ecrnx_vif->ecrnx_hw;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    netdev_info(dev, "CLOSE");

    ecrnx_radar_cancel_cac(&ecrnx_hw->radar);

    spin_lock_bh(&ecrnx_hw->scan_req_lock);
    /* Abort scan request on the vif */
    if (ecrnx_hw->scan_request &&
        ecrnx_hw->scan_request->wdev == &ecrnx_vif->wdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        struct cfg80211_scan_info info = {
            .aborted = true,
        };

        cfg80211_scan_done(ecrnx_hw->scan_request, &info);
#else
        cfg80211_scan_done(ecrnx_hw->scan_request, true);
#endif
        ecrnx_hw->scan_request = NULL;
    }

    spin_unlock_bh(&ecrnx_hw->scan_req_lock);
    ecrnx_send_remove_if(ecrnx_hw, ecrnx_vif->vif_index);

    if (ecrnx_hw->roc && (ecrnx_hw->roc->vif == ecrnx_vif)) {
        kfree(ecrnx_hw->roc);
        /* Initialize RoC element pointer to NULL, indicate that RoC can be started */
        ecrnx_hw->roc = NULL;
    }

    /* Ensure that we won't process disconnect ind */
    spin_lock_bh(&ecrnx_hw->cb_lock);

    ecrnx_vif->up = false;
    if (netif_carrier_ok(dev)) {
        if (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_STATION ||
            ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
            if (ecrnx_vif->sta.ft_assoc_ies) {
                kfree(ecrnx_vif->sta.ft_assoc_ies);
                ecrnx_vif->sta.ft_assoc_ies = NULL;
                ecrnx_vif->sta.ft_assoc_ies_len = 0;
            }
            cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING,
                                  NULL, 0, true, GFP_ATOMIC);
            if (ecrnx_vif->sta.ap) {
                ecrnx_txq_sta_deinit(ecrnx_hw, ecrnx_vif->sta.ap);
                ecrnx_txq_tdls_vif_deinit(ecrnx_vif);
            }
            netif_tx_stop_all_queues(dev);
            netif_carrier_off(dev);
        } else if (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP_VLAN) {
            netif_carrier_off(dev);
        } else {
            netdev_warn(dev, "AP not stopped when disabling interface");
        }
    }

    ecrnx_hw->vif_table[ecrnx_vif->vif_index] = NULL;
    spin_unlock_bh(&ecrnx_hw->cb_lock);

    ecrnx_chanctx_unlink(ecrnx_vif);

    if (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_MONITOR)
        ecrnx_hw->monitor_vif = ECRNX_INVALID_VIF;

    ecrnx_hw->vif_started--;
    if (ecrnx_hw->vif_started == 0) {
#ifndef CONFIG_ECRNX_ESWIN
        /* This also lets both ipc sides remain in sync before resetting */
        ecrnx_ipc_tx_drain(ecrnx_hw);
#endif
        ecrnx_send_reset(ecrnx_hw);

        // Set parameters to firmware
        ecrnx_send_me_config_req(ecrnx_hw);

        // Set channel parameters to firmware
        ecrnx_send_me_chan_config_req(ecrnx_hw);

        clear_bit(ECRNX_DEV_STARTED, &ecrnx_hw->flags);
    }

    return 0;
}

/**
 * struct net_device_stats* (*ndo_get_stats)(struct net_device *dev);
 *	Called when a user wants to get the network device usage
 *	statistics. Drivers must do one of the following:
 *	1. Define @ndo_get_stats64 to fill in a zero-initialised
 *	   rtnl_link_stats64 structure passed by the caller.
 *	2. Define @ndo_get_stats to update a net_device_stats structure
 *	   (which should normally be dev->stats) and return a pointer to
 *	   it. The structure may be changed asynchronously only if each
 *	   field is written atomically.
 *	3. Update dev->stats asynchronously and atomically, and define
 *	   neither operation.
 */
static struct net_device_stats *ecrnx_get_stats(struct net_device *dev)
{
    struct ecrnx_vif *vif = netdev_priv(dev);

    return &vif->net_stats;
}

/**
 * u16 (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb,
 *                         struct net_device *sb_dev);
 *	Called to decide which queue to when device supports multiple
 *	transmit queues.
 */
u16 ecrnx_select_queue(struct net_device *dev, struct sk_buff *skb,
                      struct net_device *sb_dev)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    return ecrnx_select_txq(ecrnx_vif, skb);
}

/**
 * int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
 *	This function  is called when the Media Access Control address
 *	needs to be changed. If this interface is not defined, the
 *	mac address can not be changed.
 */
static int ecrnx_set_mac_address(struct net_device *dev, void *addr)
{
    struct sockaddr *sa = addr;
    int ret;

    ret = eth_mac_addr(dev, sa);

    return ret;
}

int ecrnx_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    //struct iwreq *wrq = (struct iwreq *)rq;
    int ret = 0;

    switch (cmd) {
     case (SIOCDEVPRIVATE+1):
      //ret = ecrnx_android_priv_cmd(dev, rq, cmd);
        break;
     default:
        ret = -EOPNOTSUPP;
        break;
    }

 return ret;
}

static const struct net_device_ops ecrnx_netdev_ops = {
    .ndo_open               = ecrnx_open,
    .ndo_stop               = ecrnx_close,
    .ndo_start_xmit         = ecrnx_start_xmit,
    .ndo_get_stats          = ecrnx_get_stats,
    .ndo_select_queue       = ecrnx_select_queue,
    .ndo_set_mac_address    = ecrnx_set_mac_address,
    .ndo_do_ioctl           = ecrnx_ioctl,
//    .ndo_set_features       = ecrnx_set_features,
//    .ndo_set_rx_mode        = ecrnx_set_multicast_list,
};

static const struct net_device_ops ecrnx_netdev_monitor_ops = {
    .ndo_open               = ecrnx_open,
    .ndo_stop               = ecrnx_close,
    .ndo_get_stats          = ecrnx_get_stats,
    .ndo_set_mac_address    = ecrnx_set_mac_address,
};

#ifdef CONFIG_WIRELESS_EXT
extern const struct iw_handler_def  ecrnx_wext_handler_def;
#endif

static void ecrnx_netdev_setup(struct net_device *dev)
{
    ether_setup(dev);
    dev->priv_flags &= ~IFF_TX_SKB_SHARING;
    dev->netdev_ops = &ecrnx_netdev_ops;
#ifdef CONFIG_WIRELESS_EXT
    dev->wireless_handlers = &ecrnx_wext_handler_def;
#endif
#if LINUX_VERSION_CODE <  KERNEL_VERSION(4, 12, 0)
    dev->destructor = free_netdev;
#else
    dev->needs_free_netdev = true;
#endif
    dev->watchdog_timeo = ECRNX_TX_LIFETIME_MS;
    dev->needed_headroom = ECRNX_TX_MAX_HEADROOM;

#ifdef CONFIG_ECRNX_AMSDUS_TX
    dev->needed_headroom = max(dev->needed_headroom,
                               (unsigned short)(sizeof(struct ecrnx_amsdu_txhdr)
                                                + sizeof(struct ethhdr) + 4
                                                + sizeof(rfc1042_header) + 2));
#endif /* CONFIG_ECRNX_AMSDUS_TX */

    dev->hw_features = 0;
}

/*********************************************************************
 * Cfg80211 callbacks (and helper)
 *********************************************************************/
static struct wireless_dev *ecrnx_interface_add(struct ecrnx_hw *ecrnx_hw,
                                               const char *name,
                                               unsigned char name_assign_type,
                                               enum nl80211_iftype type,
                                               struct vif_params *params)
{
    struct net_device *ndev;
    struct ecrnx_vif *vif;
    int min_idx, max_idx;
    int vif_idx = -1;
    int i;

    // Look for an available VIF
    if (type == NL80211_IFTYPE_AP_VLAN) {
        min_idx = NX_VIRT_DEV_MAX;
        max_idx = NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX;
    } else {
        min_idx = 0;
        max_idx = NX_VIRT_DEV_MAX;
    }

    for (i = min_idx; i < max_idx; i++) {
        if ((ecrnx_hw->avail_idx_map) & BIT(i)) {
            vif_idx = i;
            break;
        }
    }
    if (vif_idx < 0)
        return NULL;

    #ifndef CONFIG_ECRNX_MON_DATA
    list_for_each_entry(vif, &ecrnx_hw->vifs, list) {
        // Check if monitor interface already exists or type is monitor
        if ((ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR) ||
           (type == NL80211_IFTYPE_MONITOR)) {
            wiphy_err(ecrnx_hw->wiphy,
                    "Monitor+Data interface support (MON_DATA) disabled\n");
            return NULL;
        }
    }
    #endif

    ndev = alloc_netdev_mqs(sizeof(*vif), name, name_assign_type,
                            ecrnx_netdev_setup, NX_NB_NDEV_TXQ, 1);
    if (!ndev)
        return NULL;

    vif = netdev_priv(ndev);
    ndev->ieee80211_ptr = &vif->wdev;
    vif->wdev.wiphy = ecrnx_hw->wiphy;
    vif->ecrnx_hw = ecrnx_hw;
    vif->ndev = ndev;
    vif->drv_vif_index = vif_idx;
    SET_NETDEV_DEV(ndev, wiphy_dev(vif->wdev.wiphy));
    vif->wdev.netdev = ndev;
    vif->wdev.iftype = type;
    vif->up = false;
    vif->ch_index = ECRNX_CH_NOT_SET;
    vif->generation = 0;
    memset(&vif->net_stats, 0, sizeof(vif->net_stats));
    memset(vif->rx_pn, 0, TID_MAX * sizeof(uint64_t));

    switch (type) {
    case NL80211_IFTYPE_STATION:
    case NL80211_IFTYPE_P2P_CLIENT:
        vif->sta.flags = 0;
        vif->sta.ap = NULL;
        vif->sta.tdls_sta = NULL;
        vif->sta.ft_assoc_ies = NULL;
        vif->sta.ft_assoc_ies_len = 0;
        break;
    case NL80211_IFTYPE_MESH_POINT:
        INIT_LIST_HEAD(&vif->ap.mpath_list);
        INIT_LIST_HEAD(&vif->ap.proxy_list);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
        vif->ap.mesh_pm = NL80211_MESH_POWER_ACTIVE;
        vif->ap.next_mesh_pm = NL80211_MESH_POWER_ACTIVE;
#endif
        break;
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_P2P_GO:
        INIT_LIST_HEAD(&vif->ap.sta_list);
        memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
        vif->ap.flags = 0;
        break;
    case NL80211_IFTYPE_AP_VLAN:
    {
        struct ecrnx_vif *master_vif;
        bool found = false;
        list_for_each_entry(master_vif, &ecrnx_hw->vifs, list) {
            if ((ECRNX_VIF_TYPE(master_vif) == NL80211_IFTYPE_AP)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
                && !(!memcmp(master_vif->ndev->dev_addr, params->macaddr,
                           ETH_ALEN))
#endif
                 ) {
                 found=true;
                 break;
            }
        }

        if (!found)
            goto err;

         vif->ap_vlan.master = master_vif;
         vif->ap_vlan.sta_4a = NULL;
         break;
    }
    case NL80211_IFTYPE_MONITOR:
        ndev->type = ARPHRD_IEEE80211_RADIOTAP;
        ndev->netdev_ops = &ecrnx_netdev_monitor_ops;
        break;
    default:
        break;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    if (type == NL80211_IFTYPE_AP_VLAN)
        memcpy(ndev->dev_addr, params->macaddr, ETH_ALEN);
    else 
#endif
    {
        memcpy(ndev->dev_addr, ecrnx_hw->wiphy->perm_addr, ETH_ALEN);
        ndev->dev_addr[5] ^= vif_idx;
    }

    if (params) {
        vif->use_4addr = params->use_4addr;
        ndev->ieee80211_ptr->use_4addr = params->use_4addr;
    } else
        vif->use_4addr = false;


    if (register_netdevice(ndev))
        goto err;

    spin_lock_bh(&ecrnx_hw->cb_lock);
    list_add_tail(&vif->list, &ecrnx_hw->vifs);
    spin_unlock_bh(&ecrnx_hw->cb_lock);
    ecrnx_hw->avail_idx_map &= ~BIT(vif_idx);

//#if defined(CONFIG_ECRNX_ESWIN_SDIO) || defined(CONFIG_ECRNX_ESWIN_USB)
    init_waitqueue_head(&vif->rxdataq);
//#endif

    return &vif->wdev;

err:
    free_netdev(ndev);
    return NULL;
}


/*
 * @brief Retrieve the ecrnx_sta object allocated for a given MAC address
 * and a given role.
 */
static struct ecrnx_sta *ecrnx_retrieve_sta(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_vif *ecrnx_vif, u8 *addr,
                                          __le16 fc, bool ap)
{
    if (ap) {
        /* only deauth, disassoc and action are bufferable MMPDUs */
        bool bufferable = ieee80211_is_deauth(fc) ||
                          ieee80211_is_disassoc(fc) ||
                          ieee80211_is_action(fc);

        /* Check if the packet is bufferable or not */
        if (bufferable)
        {
            /* Check if address is a broadcast or a multicast address */
            if (is_broadcast_ether_addr(addr) || is_multicast_ether_addr(addr)) {
                /* Returned STA pointer */
                struct ecrnx_sta *ecrnx_sta = &ecrnx_hw->sta_table[ecrnx_vif->ap.bcmc_index];

                if (ecrnx_sta->valid)
                    return ecrnx_sta;
            } else {
                /* Returned STA pointer */
                struct ecrnx_sta *ecrnx_sta;

                /* Go through list of STAs linked with the provided VIF */
                list_for_each_entry(ecrnx_sta, &ecrnx_vif->ap.sta_list, list) {
                    if (ecrnx_sta->valid &&
                        ether_addr_equal(ecrnx_sta->mac_addr, addr)) {
                        /* Return the found STA */
                        return ecrnx_sta;
                    }
                }
            }
        }
    } else {
        return ecrnx_vif->sta.ap;
    }

    return NULL;
}

/**
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype. Beware: You must create
 *	the new netdev in the wiphy's network namespace! Returns the struct
 *	wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *	also set the address member in the wdev.
 */
static struct wireless_dev *ecrnx_cfg80211_add_iface(struct wiphy *wiphy,
                                                    const char *name,
                                                    unsigned char name_assign_type,
                                                    enum nl80211_iftype type,
                                                    struct vif_params *params)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct wireless_dev *wdev;

    wdev = ecrnx_interface_add(ecrnx_hw, name, name_assign_type, type, params);

    if (!wdev)
        return ERR_PTR(-EINVAL);

    return wdev;
}

/**
 * @del_virtual_intf: remove the virtual interface
 */
static int ecrnx_cfg80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
    struct net_device *dev = wdev->netdev;
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);

    netdev_info(dev, "Remove Interface");

    if (dev->reg_state == NETREG_REGISTERED) {
      ECRNX_DBG("%s-%d:unregister_netdevice \n", __func__, __LINE__);
        /* Will call ecrnx_close if interface is UP */
        unregister_netdevice(dev);
    }

    spin_lock_bh(&ecrnx_hw->cb_lock);
    list_del(&ecrnx_vif->list);
    spin_unlock_bh(&ecrnx_hw->cb_lock);
    ecrnx_hw->avail_idx_map |= BIT(ecrnx_vif->drv_vif_index);
    ecrnx_vif->ndev = NULL;

    /* Clear the priv in adapter */
    dev->ieee80211_ptr = NULL;

    return 0;
}

static int ecrnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev);
static int ecrnx_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
                                    u16 reason_code);

/**
 * @change_virtual_intf: change type/configuration of virtual interface,
 *	keep the struct wireless_dev's iftype updated.
 */
static int ecrnx_cfg80211_change_iface(struct wiphy *wiphy,
                                      struct net_device *dev,
                                      enum nl80211_iftype type,
                                      struct vif_params *params)
{
#ifndef CONFIG_ECRNX_MON_DATA
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
#endif
    struct ecrnx_vif *vif = netdev_priv(dev);


    ECRNX_PRINT("%s:dev:0x%p, type:%d, vif->up:%d \n", __func__, dev, type, vif->up);

    if (vif->up)
    {
        if((ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP) || (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO))
        {
            ecrnx_cfg80211_stop_ap(wiphy, dev);
        }
        else if((ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) || (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT))
        {
            ecrnx_cfg80211_disconnect(wiphy, dev, WLAN_REASON_DEAUTH_LEAVING);
        }
        ECRNX_ERR("ecrnx_cfg80211_change_iface: -EBUSY \n");
        ecrnx_close(dev);
        //return (-EBUSY);
    }

#ifndef CONFIG_ECRNX_MON_DATA
    if ((type == NL80211_IFTYPE_MONITOR) &&
       (ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
        struct ecrnx_vif *vif_el;
        list_for_each_entry(vif_el, &ecrnx_hw->vifs, list) {
            // Check if data interface already exists
            if ((vif_el != vif) &&
               (ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
                wiphy_err(ecrnx_hw->wiphy,
                        "Monitor+Data interface support (MON_DATA) disabled\n");
                return -EIO;
            }
        }
    }
#endif

    // Reset to default case (i.e. not monitor)
    dev->type = ARPHRD_ETHER;
    dev->netdev_ops = &ecrnx_netdev_ops;

    switch (type) {
    case NL80211_IFTYPE_STATION:
    case NL80211_IFTYPE_P2P_CLIENT:
        vif->sta.flags = 0;
        vif->sta.ap = NULL;
        vif->sta.tdls_sta = NULL;
        vif->sta.ft_assoc_ies = NULL;
        vif->sta.ft_assoc_ies_len = 0;
        break;
    case NL80211_IFTYPE_MESH_POINT:
        INIT_LIST_HEAD(&vif->ap.mpath_list);
        INIT_LIST_HEAD(&vif->ap.proxy_list);
        break;
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_P2P_GO:
        INIT_LIST_HEAD(&vif->ap.sta_list);
        memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
        vif->ap.flags = 0;
        break;
    case NL80211_IFTYPE_AP_VLAN:
        return -EPERM;
    case NL80211_IFTYPE_MONITOR:
        dev->type = ARPHRD_IEEE80211_RADIOTAP;
        dev->netdev_ops = &ecrnx_netdev_monitor_ops;
        break;
    default:
        break;
    }

    vif->generation = 0;
    vif->wdev.iftype = type;
    if (params->use_4addr != -1)
        vif->use_4addr = params->use_4addr;

    if (!vif->up)
    {
        ecrnx_open(dev);
    }

    return 0;
}

/*
 * GavinGao
 * Used as P2P_DEVICE mode
 */
static int ecrnx_cfg80211_start_p2p_device(struct wiphy *wiphy,
				      struct wireless_dev *wdev)
{
	ECRNX_PRINT("rwnx_cfg80211_start_p2p_device\n");
	return 0;
}

static void ecrnx_cfg80211_stop_p2p_device(struct wiphy *wiphy,
				      struct wireless_dev *wdev)
{
	//TODO
}
/* Used as P2P_DEVICE mode*/
					  

/**
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *	the driver, and will be valid until passed to cfg80211_scan_done().
 *	For scan results, call cfg80211_inform_bss(); you can call this outside
 *	the scan/scan_done bracket too.
 */
static int ecrnx_cfg80211_scan(struct wiphy *wiphy,
                              struct cfg80211_scan_request *request)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = container_of(request->wdev, struct ecrnx_vif,
                                             wdev);
    int error;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if(!ecrnx_hw->scan_request){
        ecrnx_hw->scan_request = request;
        ECRNX_PRINT("%s:scan_request:0x%p \n", __func__, request);
        if ((error = ecrnx_send_scanu_req(ecrnx_hw, ecrnx_vif, request))){
            ECRNX_PRINT("scan message send error!!\n");
            ecrnx_hw->scan_request = NULL;
            return error;	
    	}
    }else{
        ECRNX_PRINT("scan is already running!!\n");
    }
    ECRNX_DBG("send finish:ecrnx_cfg80211_scan \n");

    return 0;
}

static void ecrnx_cfg80211_abort_scan(struct wiphy *wiphy,
                              struct wireless_dev *wdev)
{
    u8_l ret = 0;
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = container_of(wdev, struct ecrnx_vif,
                                             wdev);

    //mutex_lock(&ecrnx_hw->mutex);
    ECRNX_PRINT("%s:ecrnx_hw->scan_request:0x%p \n", __func__, ecrnx_hw->scan_request);

    if(!ecrnx_hw->scan_request){
        ECRNX_ERR("no scan is running, don't need abort! \n");
        goto out;
    }

    if(wdev->iftype != NL80211_IFTYPE_STATION){
        ECRNX_ERR("abort scan ignored, iftype(%d)\n", wdev->iftype);
        goto out;
    }

    if(wdev != ecrnx_hw->scan_request->wdev){
        ECRNX_ERR("abort scan was called on the wrong iface\n");
        goto out;
    }

    ret = ecrnx_send_scanu_cancel_req(ecrnx_hw, ecrnx_vif);

out:
    //mutex_unlock(&ecrnx_hw->mutex);
    return;
}

/**
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *	when adding a group key.
 */
static int ecrnx_cfg80211_add_key(struct wiphy *wiphy, struct net_device *netdev,
                                 u8 key_index, bool pairwise, const u8 *mac_addr,
                                 struct key_params *params)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif = netdev_priv(netdev);
    int i, error = 0;
    struct mm_key_add_cfm key_add_cfm;
    u8_l cipher = 0;
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_key *ecrnx_key;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (mac_addr) {
        sta = ecrnx_get_sta(ecrnx_hw, mac_addr);
        if (!sta)
            return -EINVAL;
        ecrnx_key = &sta->key;
    }
    else
        ecrnx_key = &vif->key[key_index];

    /* Retrieve the cipher suite selector */
    switch (params->cipher) {
    case WLAN_CIPHER_SUITE_WEP40:
        cipher = MAC_CIPHER_WEP40;
        break;
    case WLAN_CIPHER_SUITE_WEP104:
        cipher = MAC_CIPHER_WEP104;
        break;
    case WLAN_CIPHER_SUITE_TKIP:
        cipher = MAC_CIPHER_TKIP;
        break;
    case WLAN_CIPHER_SUITE_CCMP:
        cipher = MAC_CIPHER_CCMP;
        break;
    case WLAN_CIPHER_SUITE_AES_CMAC:
        cipher = MAC_CIPHER_BIP_CMAC_128;
        break;
    case WLAN_CIPHER_SUITE_SMS4:
    {
        // Need to reverse key order
        u8 tmp, *key = (u8 *)params->key;
        cipher = MAC_CIPHER_WPI_SMS4;
        for (i = 0; i < WPI_SUBKEY_LEN/2; i++) {
            tmp = key[i];
            key[i] = key[WPI_SUBKEY_LEN - 1 - i];
            key[WPI_SUBKEY_LEN - 1 - i] = tmp;
        }
        for (i = 0; i < WPI_SUBKEY_LEN/2; i++) {
            tmp = key[i + WPI_SUBKEY_LEN];
            key[i + WPI_SUBKEY_LEN] = key[WPI_KEY_LEN - 1 - i];
            key[WPI_KEY_LEN - 1 - i] = tmp;
        }
        break;
    }
    case WLAN_CIPHER_SUITE_GCMP:
        cipher = MAC_CIPHER_GCMP_128;
        break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    case WLAN_CIPHER_SUITE_GCMP_256:
        cipher = MAC_CIPHER_GCMP_256;
        break;
    case WLAN_CIPHER_SUITE_CCMP_256:
        cipher = MAC_CIPHER_CCMP_256;
        break;
#endif
    default:
        return -EINVAL;
    }

    if ((error = ecrnx_send_key_add(ecrnx_hw, vif->vif_index,
                                   (sta ? sta->sta_idx : 0xFF), pairwise,
                                   (u8 *)params->key, params->key_len,
                                   key_index, cipher, &key_add_cfm)))
        return error;

    if (key_add_cfm.status != 0) {
        ECRNX_PRINT_CFM_ERR(key_add);
        return -EIO;
    }

    /* Save the index retrieved from LMAC */
    ecrnx_key->hw_idx = key_add_cfm.hw_key_idx;

    return 0;
}

/**
 * @get_key: get information about the key with the given parameters.
 *	@mac_addr will be %NULL when requesting information for a group
 *	key. All pointers given to the @callback function need not be valid
 *	after it returns. This function should return an error if it is
 *	not possible to retrieve the key, -ENOENT if it doesn't exist.
 *
 */
static int ecrnx_cfg80211_get_key(struct wiphy *wiphy, struct net_device *netdev,
                                 u8 key_index, bool pairwise, const u8 *mac_addr,
                                 void *cookie,
                                 void (*callback)(void *cookie, struct key_params*))
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    return -1;
}


/**
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *	and @key_index, return -ENOENT if the key doesn't exist.
 */
static int ecrnx_cfg80211_del_key(struct wiphy *wiphy, struct net_device *netdev,
                                 u8 key_index, bool pairwise, const u8 *mac_addr)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif = netdev_priv(netdev);
    int error;
    struct ecrnx_sta *sta = NULL;
    struct ecrnx_key *ecrnx_key;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    if (mac_addr) {
        sta = ecrnx_get_sta(ecrnx_hw, mac_addr);
        if (!sta)
            return -EINVAL;
        ecrnx_key = &sta->key;
    }
    else
        ecrnx_key = &vif->key[key_index];

    error = ecrnx_send_key_del(ecrnx_hw, ecrnx_key->hw_idx);

    return error;
}

/**
 * @set_default_key: set the default key on an interface
 */
static int ecrnx_cfg80211_set_default_key(struct wiphy *wiphy,
                                         struct net_device *netdev,
                                         u8 key_index, bool unicast, bool multicast)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    return 0;
}

/**
 * @set_default_mgmt_key: set the default management frame key on an interface
 */
static int ecrnx_cfg80211_set_default_mgmt_key(struct wiphy *wiphy,
                                              struct net_device *netdev,
                                              u8 key_index)
{
    return 0;
}

/**
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *	call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
 *	If the connection fails for some reason, call cfg80211_connect_result()
 *	with the status from the AP.
 *	(invoked with the wireless_dev mutex held)
 */
static int ecrnx_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
                                 struct cfg80211_connect_params *sme)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct sm_connect_cfm sm_connect_cfm;
    int error = 0;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* For SHARED-KEY authentication, must install key first */
    if (sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY && sme->key)
    {
        struct key_params key_params;
        key_params.key = sme->key;
        key_params.seq = NULL;
        key_params.key_len = sme->key_len;
        key_params.seq_len = 0;
        key_params.cipher = sme->crypto.cipher_group;
        ecrnx_cfg80211_add_key(wiphy, dev, sme->key_idx, false, NULL, &key_params);
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
    else if ((sme->auth_type == NL80211_AUTHTYPE_SAE) &&
             !(sme->flags & CONNECT_REQ_EXTERNAL_AUTH_SUPPORT)) {
        netdev_err(dev, "Doesn't support SAE without external authentication\n");
        return -EINVAL;
    }
#endif

    /* Forward the information to the LMAC */
    if ((error = ecrnx_send_sm_connect_req(ecrnx_hw, ecrnx_vif, sme, &sm_connect_cfm)))
        return error;
    ECRNX_PRINT("%s:bssid:%pM, send status:%d\n", __func__, sme->bssid, sm_connect_cfm.status);

    // Check the status
    switch (sm_connect_cfm.status)
    {
        case CO_OK:
            ecrnx_save_assoc_info_for_ft(ecrnx_vif, sme);
            error = 0;
            break;
        case CO_BUSY:
            error = -EINPROGRESS;
            break;
        case CO_BAD_PARAM:
            error = -EINVAL;
            break;
        case CO_OP_IN_PROGRESS:
            error = -EALREADY;
            break;
        default:
            error = -EIO;
            break;
    }

    return error;
}

/**
 * @disconnect: Disconnect from the BSS/ESS.
 *	(invoked with the wireless_dev mutex held)
 */
static int ecrnx_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
                                    u16 reason_code)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);

    ECRNX_PRINT("%s:dev:0x%p, vif_index:%d, reason_code:%d \n", __func__, dev, ecrnx_vif->vif_index, reason_code);

    return(ecrnx_send_sm_disconnect_req(ecrnx_hw, ecrnx_vif, reason_code));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
/**
 * @external_auth: indicates result of offloaded authentication processing from
 *     user space
 */
static int ecrnx_cfg80211_external_auth(struct wiphy *wiphy, struct net_device *dev,
                                       struct cfg80211_external_auth_params *params)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);

    if (!(ecrnx_vif->sta.flags & ECRNX_STA_EXT_AUTH))
        return -EINVAL;

    ecrnx_external_auth_disable(ecrnx_vif);
    return ecrnx_send_sm_external_auth_required_rsp(ecrnx_hw, ecrnx_vif,
                                                   params->status);
}
#endif

/**
 * @add_station: Add a new station.
 */
static int ecrnx_cfg80211_add_station(struct wiphy *wiphy, struct net_device *dev,
                                     const u8 *mac, struct station_parameters *params)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct me_sta_add_cfm me_sta_add_cfm;
    int error = 0;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    WARN_ON(ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP_VLAN);

    /* Do not add TDLS station */
    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
        return 0;

    /* Indicate we are in a STA addition process - This will allow handling
     * potential PS mode change indications correctly
     */
    set_bit(ECRNX_DEV_ADDING_STA, &ecrnx_hw->flags);

    /* Forward the information to the LMAC */
    if ((error = ecrnx_send_me_sta_add(ecrnx_hw, params, mac, ecrnx_vif->vif_index,
                                      &me_sta_add_cfm)))
        return error;

    // Check the status
    switch (me_sta_add_cfm.status)
    {
        case CO_OK:
        {
            struct ecrnx_sta *sta = &ecrnx_hw->sta_table[me_sta_add_cfm.sta_idx];

            ECRNX_PRINT("%s-%d:sta:0x%p, sta_idx:%d \n", __func__, __LINE__, sta, me_sta_add_cfm.sta_idx);
            int tid;
            sta->aid = params->aid;
            memset(sta->rx_pn, 0, TID_MAX * sizeof(uint64_t));
            sta->sta_idx = me_sta_add_cfm.sta_idx;
            sta->ch_idx = ecrnx_vif->ch_index;
            sta->vif_idx = ecrnx_vif->vif_index;
            sta->vlan_idx = sta->vif_idx;
            sta->qos = (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
            sta->ht = params->ht_capa ? 1 : 0;
            sta->vht = params->vht_capa ? 1 : 0;
            sta->acm = 0;
            sta->listen_interval = params->listen_interval;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            if (params->local_pm != NL80211_MESH_POWER_UNKNOWN)
                sta->mesh_pm = params->local_pm;
            else
                sta->mesh_pm = ecrnx_vif->ap.next_mesh_pm;
#endif
            ecrnx_update_mesh_power_mode(ecrnx_vif);

            for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
                int uapsd_bit = ecrnx_hwq2uapsd[ecrnx_tid2hwq[tid]];
                if (params->uapsd_queues & uapsd_bit)
                    sta->uapsd_tids |= 1 << tid;
                else
                    sta->uapsd_tids &= ~(1 << tid);
            }
            memcpy(sta->mac_addr, mac, ETH_ALEN);
            ecrnx_dbgfs_register_sta(ecrnx_hw, sta);

            /* Ensure that we won't process PS change or channel switch ind*/
            spin_lock_bh(&ecrnx_hw->cb_lock);
            ecrnx_txq_sta_init(ecrnx_hw, sta, ecrnx_txq_vif_get_status(ecrnx_vif));
            ecrnx_rx_reord_sta_init(ecrnx_hw, ecrnx_vif, sta->sta_idx);
            list_add_tail(&sta->list, &ecrnx_vif->ap.sta_list);
            ecrnx_vif->generation++;
            sta->valid = true;
            ecrnx_ps_bh_enable(ecrnx_hw, sta, sta->ps.active || me_sta_add_cfm.pm_state);
            spin_unlock_bh(&ecrnx_hw->cb_lock);

#ifdef CONFIG_ECRNX_ANDRIOD
            if((ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP) || (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_P2P_GO)) {
                struct station_info sinfo;
                u8 ie_offset;

                if((!is_multicast_sta(sta->sta_idx)) && (sta->mac_addr)) {
                    memset(&sinfo, 0, sizeof(sinfo));

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
                    sinfo.filled = STATION_INFO_ASSOC_REQ_IES;
#endif
                    sinfo.assoc_req_ies = NULL;
                    sinfo.assoc_req_ies_len = 0;
                    ECRNX_PRINT("%s-%d:sta:0x%x,sta->mac_addr:%pM \n", __func__, __LINE__, sta, sta->mac_addr);
                    cfg80211_new_sta(ecrnx_vif->ndev, sta->mac_addr, &sinfo, GFP_ATOMIC);
                }
            }
#endif

            error = 0;

#ifdef CONFIG_ECRNX_BFMER
            if (ecrnx_hw->mod_params->bfmer)
                ecrnx_send_bfmer_enable(ecrnx_hw, sta, params->vht_capa);

            ecrnx_mu_group_sta_init(sta, params->vht_capa);
#endif /* CONFIG_ECRNX_BFMER */

            #define PRINT_STA_FLAG(f)                               \
                (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "["#f"]" : "")

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            netdev_info(dev, "Add sta %d (%pM) flags=%s%s%s%s%s%s%s",
                        sta->sta_idx, mac,
                        PRINT_STA_FLAG(AUTHORIZED),
                        PRINT_STA_FLAG(SHORT_PREAMBLE),
                        PRINT_STA_FLAG(WME),
                        PRINT_STA_FLAG(MFP),
                        PRINT_STA_FLAG(AUTHENTICATED),
                        PRINT_STA_FLAG(TDLS_PEER),
                        PRINT_STA_FLAG(ASSOCIATED));
#else
            netdev_info(dev, "Add sta %d (%pM) flags=%s%s%s%s%s%s",
                        sta->sta_idx, mac,
                        PRINT_STA_FLAG(AUTHORIZED),
                        PRINT_STA_FLAG(SHORT_PREAMBLE),
                        PRINT_STA_FLAG(WME),
                        PRINT_STA_FLAG(MFP),
                        PRINT_STA_FLAG(AUTHENTICATED),
                        PRINT_STA_FLAG(TDLS_PEER));
#endif

            #undef PRINT_STA_FLAG
#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
            ecrnx_debugfs_add_station_in_ap_mode(ecrnx_hw, sta, params);
#endif
            break;
        }
        default:
            error = -EBUSY;
            break;
    }

    clear_bit(ECRNX_DEV_ADDING_STA, &ecrnx_hw->flags);

    return error;
}

/**
 * @del_station: Remove a station
 */
static int ecrnx_cfg80211_del_station(struct wiphy *wiphy,
                                        struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
                                        u8 *mac
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0))
                                        const u8 *mac
#else
                                        struct station_del_parameters *params
#endif
)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_sta *cur, *tmp;
    int error = 0, found = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    const u8 *mac = NULL;
    if (params)
        mac = params->mac;
#endif

    if(list_empty(&ecrnx_vif->ap.sta_list)) {
        goto end;
    }

    list_for_each_entry_safe(cur, tmp, &ecrnx_vif->ap.sta_list, list) {
        if ((!mac) || (!memcmp(cur->mac_addr, mac, ETH_ALEN))) {
            netdev_info(dev, "Del sta %d (%pM)", cur->sta_idx, cur->mac_addr);
            ECRNX_PRINT("%s-%d:cur_list:0x%p, vif_list:0x%p, mac:%pM\n", __func__, __LINE__, &cur->list, &ecrnx_vif->ap.sta_list, mac);
            /* Ensure that we won't process PS change ind */
            spin_lock_bh(&ecrnx_hw->cb_lock);
            cur->ps.active = false;
            cur->valid = false;
            spin_unlock_bh(&ecrnx_hw->cb_lock);

            if (cur->vif_idx != cur->vlan_idx) {
                struct ecrnx_vif *vlan_vif;
                vlan_vif = ecrnx_hw->vif_table[cur->vlan_idx];
                if (vlan_vif->up) {
                    if ((ECRNX_VIF_TYPE(vlan_vif) == NL80211_IFTYPE_AP_VLAN) &&
                        (vlan_vif->use_4addr)) {
                        vlan_vif->ap_vlan.sta_4a = NULL;
                    } else {
                        WARN(1, "Deleting sta belonging to VLAN other than AP_VLAN 4A");
                    }
                }
            }

            ecrnx_txq_sta_deinit(ecrnx_hw, cur);
            ecrnx_rx_reord_sta_deinit(ecrnx_hw, cur->sta_idx, true);

            error = ecrnx_send_me_sta_del(ecrnx_hw, cur->sta_idx, false);
            if ((error != 0) && (error != -EPIPE)){
                ECRNX_WARN("del sta msg send fail, error code:%d \n", error);
            }

#ifdef CONFIG_ECRNX_ANDRIOD
            if((ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP) || (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_P2P_GO)) {
                if((!is_multicast_sta(cur->sta_idx)) && params) {
                    if(params->mac){
                        ECRNX_PRINT("%s-%d:vif:%d, mac:%pM \n", __func__, __LINE__, ECRNX_VIF_TYPE(ecrnx_vif), params->mac);
                        cfg80211_del_sta(ecrnx_vif->ndev, params->mac, GFP_ATOMIC);
                    }
                }
            }
#endif


#ifdef CONFIG_ECRNX_BFMER
            // Disable Beamformer if supported
            ecrnx_bfmer_report_del(ecrnx_hw, cur);
            ecrnx_mu_group_sta_del(ecrnx_hw, cur);
#endif /* CONFIG_ECRNX_BFMER */

            list_del(&cur->list);
            ecrnx_vif->generation++;
            ecrnx_dbgfs_unregister_sta(ecrnx_hw, cur);
#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
            ecrnx_debugfs_sta_in_ap_del(cur->sta_idx);
#endif
            found ++;
            break;
        }
    }

end:
    if ((!found) && (mac))
        return -ENOENT;

    ecrnx_update_mesh_power_mode(ecrnx_vif);

    return 0;
}

/**
 * @change_station: Modify a given station. Note that flags changes are not much
 *	validated in cfg80211, in particular the auth/assoc/authorized flags
 *	might come to the driver in invalid combinations -- make sure to check
 *	them, also against the existing state! Drivers must call
 *	cfg80211_check_station_change() to validate the information.
 */
static int ecrnx_cfg80211_change_station(struct wiphy *wiphy, struct net_device *dev,
                                        const u8 *mac, struct station_parameters *params)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif = netdev_priv(dev);
    struct ecrnx_sta *sta;

    sta = ecrnx_get_sta(ecrnx_hw, mac);
    if (!sta)
    {
        /* Add the TDLS station */
        if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
        {
            struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
            struct me_sta_add_cfm me_sta_add_cfm;
            int error = 0;

            /* Indicate we are in a STA addition process - This will allow handling
             * potential PS mode change indications correctly
             */
            set_bit(ECRNX_DEV_ADDING_STA, &ecrnx_hw->flags);

            /* Forward the information to the LMAC */
            if ((error = ecrnx_send_me_sta_add(ecrnx_hw, params, mac, ecrnx_vif->vif_index,
                                              &me_sta_add_cfm)))
                return error;

            // Check the status
            switch (me_sta_add_cfm.status)
            {
                case CO_OK:
                {
                    int tid;
                    sta = &ecrnx_hw->sta_table[me_sta_add_cfm.sta_idx];
                    memset(&sta->rx_pn, 0, TID_MAX * sizeof(uint64_t));
                    sta->aid = params->aid;
                    sta->sta_idx = me_sta_add_cfm.sta_idx;
                    sta->ch_idx = ecrnx_vif->ch_index;
                    sta->vif_idx = ecrnx_vif->vif_index;
                    sta->vlan_idx = sta->vif_idx;
                    sta->qos = (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
                    sta->ht = params->ht_capa ? 1 : 0;
                    sta->vht = params->vht_capa ? 1 : 0;
                    sta->acm = 0;
                    for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
                        int uapsd_bit = ecrnx_hwq2uapsd[ecrnx_tid2hwq[tid]];
                        if (params->uapsd_queues & uapsd_bit)
                            sta->uapsd_tids |= 1 << tid;
                        else
                            sta->uapsd_tids &= ~(1 << tid);
                    }
                    memcpy(sta->mac_addr, mac, ETH_ALEN);
                    ecrnx_dbgfs_register_sta(ecrnx_hw, sta);

                    /* Ensure that we won't process PS change or channel switch ind*/
                    spin_lock_bh(&ecrnx_hw->cb_lock);
                    ecrnx_txq_sta_init(ecrnx_hw, sta, ecrnx_txq_vif_get_status(ecrnx_vif));
                    ecrnx_rx_reord_sta_init(ecrnx_hw, ecrnx_vif, sta->sta_idx);
                    if (ecrnx_vif->tdls_status == TDLS_SETUP_RSP_TX) {
                        ecrnx_vif->tdls_status = TDLS_LINK_ACTIVE;
                        sta->tdls.initiator = true;
                        sta->tdls.active = true;
                    }
                    /* Set TDLS channel switch capability */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
                    if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
                        !ecrnx_vif->tdls_chsw_prohibited)
#else
                    if (!ecrnx_vif->tdls_chsw_prohibited)
#endif
                        sta->tdls.chsw_allowed = true;
                    ecrnx_vif->sta.tdls_sta = sta;
                    sta->valid = true;
                    spin_unlock_bh(&ecrnx_hw->cb_lock);
#ifdef CONFIG_ECRNX_BFMER
                    if (ecrnx_hw->mod_params->bfmer)
                        ecrnx_send_bfmer_enable(ecrnx_hw, sta, params->vht_capa);

                    ecrnx_mu_group_sta_init(sta, NULL);
#endif /* CONFIG_ECRNX_BFMER */

                    #define PRINT_STA_FLAG(f)                               \
                        (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "["#f"]" : "")

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
                    netdev_info(dev, "Add %s TDLS sta %d (%pM) flags=%s%s%s%s%s%s%s",
                                sta->tdls.initiator ? "initiator" : "responder",
                                sta->sta_idx, mac,
                                PRINT_STA_FLAG(AUTHORIZED),
                                PRINT_STA_FLAG(SHORT_PREAMBLE),
                                PRINT_STA_FLAG(WME),
                                PRINT_STA_FLAG(MFP),
                                PRINT_STA_FLAG(AUTHENTICATED),
                                PRINT_STA_FLAG(TDLS_PEER),
                                PRINT_STA_FLAG(ASSOCIATED));
#else
                    netdev_info(dev, "Add %s TDLS sta %d (%pM) flags=%s%s%s%s%s%s",
                            sta->tdls.initiator ? "initiator" : "responder",
                            sta->sta_idx, mac,
                            PRINT_STA_FLAG(AUTHORIZED),
                            PRINT_STA_FLAG(SHORT_PREAMBLE),
                            PRINT_STA_FLAG(WME),
                            PRINT_STA_FLAG(MFP),
                            PRINT_STA_FLAG(AUTHENTICATED),
                            PRINT_STA_FLAG(TDLS_PEER));
#endif
                    #undef PRINT_STA_FLAG

                    break;
                }
                default:
                    error = -EBUSY;
                    break;
            }

            clear_bit(ECRNX_DEV_ADDING_STA, &ecrnx_hw->flags);
        } else  {
            return -EINVAL;
        }
    }

    if (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))
        ecrnx_send_me_set_control_port_req(ecrnx_hw,
                (params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) != 0,
                sta->sta_idx);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    if (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT) {
        if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE) {
            if (params->plink_state < NUM_NL80211_PLINK_STATES) {
                ecrnx_send_mesh_peer_update_ntf(ecrnx_hw, vif, sta->sta_idx, params->plink_state);
            }
        }

        if (params->local_pm != NL80211_MESH_POWER_UNKNOWN) {
            sta->mesh_pm = params->local_pm;
            ecrnx_update_mesh_power_mode(vif);
        }
    }
#endif

    if (params->vlan) {
        uint8_t vlan_idx;

        vif = netdev_priv(params->vlan);
        vlan_idx = vif->vif_index;

        if (sta->vlan_idx != vlan_idx) {
            struct ecrnx_vif *old_vif;
            old_vif = ecrnx_hw->vif_table[sta->vlan_idx];
            ecrnx_txq_sta_switch_vif(sta, old_vif, vif);
            sta->vlan_idx = vlan_idx;

            if ((ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP_VLAN) &&
                (vif->use_4addr)) {
                WARN((vif->ap_vlan.sta_4a),
                     "4A AP_VLAN interface with more than one sta");
                vif->ap_vlan.sta_4a = sta;
            }

            if ((ECRNX_VIF_TYPE(old_vif) == NL80211_IFTYPE_AP_VLAN) &&
                (old_vif->use_4addr)) {
                old_vif->ap_vlan.sta_4a = NULL;
            }
        }
    }

    return 0;
}

/**
 * @start_ap: Start acting in AP mode defined by the parameters.
 */
static int ecrnx_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *dev,
                                  struct cfg80211_ap_settings *settings)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct apm_start_cfm apm_start_cfm;
    struct ecrnx_ipc_elem_var elem;
    struct ecrnx_sta *sta;
    int error = 0;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* Forward the information to the LMAC */
    if ((error = ecrnx_send_apm_start_req(ecrnx_hw, ecrnx_vif, settings,
                                         &apm_start_cfm, &elem)))
        goto end;

    // Check the status
    switch (apm_start_cfm.status)
    {
        case CO_OK:
        {
            u8 txq_status = 0;
            ecrnx_vif->ap.bcmc_index = apm_start_cfm.bcmc_idx;
            ecrnx_vif->ap.flags = 0;
            ecrnx_vif->ap.bcn_interval = settings->beacon_interval;
            sta = &ecrnx_hw->sta_table[apm_start_cfm.bcmc_idx];
            sta->valid = true;
            sta->aid = 0;
            sta->sta_idx = apm_start_cfm.bcmc_idx;
            sta->ch_idx = apm_start_cfm.ch_idx;
            sta->vif_idx = ecrnx_vif->vif_index;
            sta->qos = false;
            sta->acm = 0;
            sta->ps.active = false;
            sta->listen_interval = 5;
            ecrnx_mu_group_sta_init(sta, NULL);
            spin_lock_bh(&ecrnx_hw->cb_lock);
            ecrnx_chanctx_link(ecrnx_vif, apm_start_cfm.ch_idx,
                              &settings->chandef);
            if (ecrnx_hw->cur_chanctx != apm_start_cfm.ch_idx) {
                txq_status = ECRNX_TXQ_STOP_CHAN;
            }
            ecrnx_txq_vif_init(ecrnx_hw, ecrnx_vif, txq_status);
            spin_unlock_bh(&ecrnx_hw->cb_lock);

            netif_tx_start_all_queues(dev);
            netif_carrier_on(dev);
            error = 0;
            /* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
            if (txq_status == 0) {
                ecrnx_radar_detection_enable_on_cur_channel(ecrnx_hw);
            }
            break;
        }
        case CO_BUSY:
            error = -EINPROGRESS;
            break;
        case CO_OP_IN_PROGRESS:
            error = -EALREADY;
            break;
        default:
            error = -EIO;
            break;
    }

    if (error) {
        netdev_info(dev, "Failed to start AP (%d)", error);
    } else {
        netdev_info(dev, "AP started: ch=%d, bcmc_idx=%d",
                    ecrnx_vif->ch_index, ecrnx_vif->ap.bcmc_index);
    }

  end:
    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &elem);

    return error;
}


/**
 * @change_beacon: Change the beacon parameters for an access point mode
 *	interface. This should reject the call when AP mode wasn't started.
 */
static int ecrnx_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *dev,
                                       struct cfg80211_beacon_data *info)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif = netdev_priv(dev);
    struct ecrnx_bcn *bcn = &vif->ap.bcn;
    struct ecrnx_ipc_elem_var elem;
    u8 *buf;
    int error = 0;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    // Build the beacon
    buf = ecrnx_build_bcn(bcn, info);
    if (!buf)
        return -ENOMEM;

    // Sync buffer for FW
    if ((error = ecrnx_ipc_elem_var_allocs(ecrnx_hw, &elem, bcn->len, DMA_TO_DEVICE,
                                          buf, NULL, NULL)))
        return error;

    // Forward the information to the LMAC
    error = ecrnx_send_bcn_change(ecrnx_hw, vif->vif_index, elem.dma_addr,
                                 bcn->len, bcn->head_len, bcn->tim_len, NULL);

    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &elem);

    return error;
}

/**
 * * @stop_ap: Stop being an AP, including stopping beaconing.
 */
static int ecrnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_sta *sta;

    ecrnx_radar_cancel_cac(&ecrnx_hw->radar);
    ecrnx_send_apm_stop_req(ecrnx_hw, ecrnx_vif);
    spin_lock_bh(&ecrnx_hw->cb_lock);
    ecrnx_chanctx_unlink(ecrnx_vif);
    spin_unlock_bh(&ecrnx_hw->cb_lock);

    /* delete any remaining STA*/
    while (!list_empty(&ecrnx_vif->ap.sta_list)) {
        ecrnx_cfg80211_del_station(wiphy, dev, NULL);
    }

    /* delete BC/MC STA */
    sta = &ecrnx_hw->sta_table[ecrnx_vif->ap.bcmc_index];
    ecrnx_txq_vif_deinit(ecrnx_hw, ecrnx_vif);
    ecrnx_del_bcn(&ecrnx_vif->ap.bcn);
    ecrnx_del_csa(ecrnx_vif);

    netif_tx_stop_all_queues(dev);
    netif_carrier_off(dev);

    netdev_info(dev, "AP Stopped");

    return 0;
}

/**
 * @set_monitor_channel: Set the monitor mode channel for the device. If other
 *	interfaces are active this callback should reject the configuration.
 *	If no interfaces are active or the device is down, the channel should
 *	be stored for when a monitor interface becomes active.
 *
 * Also called internaly with chandef set to NULL simply to retrieve the channel
 * configured at firmware level.
 */
static int ecrnx_cfg80211_set_monitor_channel(struct wiphy *wiphy,
                                             struct cfg80211_chan_def *chandef)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif;
    struct me_config_monitor_cfm cfm;
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (ecrnx_hw->monitor_vif == ECRNX_INVALID_VIF)
        return -EINVAL;

    ecrnx_vif = ecrnx_hw->vif_table[ecrnx_hw->monitor_vif];

    // Do nothing if monitor interface is already configured with the requested channel
    if (ecrnx_chanctx_valid(ecrnx_hw, ecrnx_vif->ch_index)) {
        struct ecrnx_chanctx *ctxt;
        ctxt = &ecrnx_vif->ecrnx_hw->chanctx_table[ecrnx_vif->ch_index];
        if (chandef && cfg80211_chandef_identical(&ctxt->chan_def, chandef))
            return 0;
    }

    // Always send command to firmware. It allows to retrieve channel context index
    // and its configuration.
    if (ecrnx_send_config_monitor_req(ecrnx_hw, chandef, &cfm))
        return -EIO;

    // Always re-set channel context info
    ecrnx_chanctx_unlink(ecrnx_vif);



    // If there is also a STA interface not yet connected then monitor interface
    // will only have a channel context after the connection of the STA interface.
    if (cfm.chan_index != ECRNX_CH_NOT_SET)
    {
        struct cfg80211_chan_def mon_chandef;

        if (ecrnx_hw->vif_started > 1) {
            // In this case we just want to update the channel context index not
            // the channel configuration
            ecrnx_chanctx_link(ecrnx_vif, cfm.chan_index, NULL);
            return -EBUSY;
        }

        mon_chandef.chan = ieee80211_get_channel(wiphy, cfm.chan.prim20_freq);
        mon_chandef.center_freq1 = cfm.chan.center1_freq;
        mon_chandef.center_freq2 = cfm.chan.center2_freq;
        mon_chandef.width =  chnl2bw[cfm.chan.type];
        ecrnx_chanctx_link(ecrnx_vif, cfm.chan_index, &mon_chandef);
    }

    return 0;
}

/**
 * @probe_client: probe an associated client, must return a cookie that it
 *	later passes to cfg80211_probe_status().
 */
int ecrnx_cfg80211_probe_client(struct wiphy *wiphy, struct net_device *dev,
            const u8 *peer, u64 *cookie)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif = netdev_priv(dev);
    struct ecrnx_sta *sta = NULL;
    struct apm_probe_client_cfm cfm;
    if ((ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP) &&
        (ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP_VLAN) &&
        (ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_P2P_GO) &&
        (ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT))
        return -EINVAL;
    list_for_each_entry(sta, &vif->ap.sta_list, list) {
        if (sta->valid && ether_addr_equal(sta->mac_addr, peer))
            break;
}

    if (!sta)
        return -EINVAL;

    ecrnx_send_apm_probe_req(ecrnx_hw, vif, sta, &cfm);

    if (cfm.status != CO_OK)
        return -EINVAL;

    *cookie = (u64)cfm.probe_id;
    return 0;
}

/**
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *	@changed bitfield (see &enum wiphy_params_flags) describes which values
 *	have changed. The actual parameter values are available in
 *	struct wiphy. If returning an error, no value should be changed.
 */
static int ecrnx_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
    return 0;
}


/**
 * @set_tx_power: set the transmit power according to the parameters,
 *	the power passed is in mBm, to get dBm use MBM_TO_DBM(). The
 *	wdev may be %NULL if power was set for the wiphy, and will
 *	always be %NULL unless the driver supports per-vif TX power
 *	(as advertised by the nl80211 feature flag.)
 */
static int ecrnx_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
                                      enum nl80211_tx_power_setting type, int mbm)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif;
    s8 pwr;
    int res = 0;

    if (type == NL80211_TX_POWER_AUTOMATIC) {
        pwr = 0x7f;
    } else {
        pwr = MBM_TO_DBM(mbm);
    }

    if (wdev) {
        vif = container_of(wdev, struct ecrnx_vif, wdev);
        res = ecrnx_send_set_power(ecrnx_hw, vif->vif_index, pwr, NULL);
    } else {
        list_for_each_entry(vif, &ecrnx_hw->vifs, list) {
            res = ecrnx_send_set_power(ecrnx_hw, vif->vif_index, pwr, NULL);
            if (res)
                break;
        }
    }

    return res;
}

/**
 * @set_power_mgmt: set the power save to one of those two modes:
 *  Power-save off
 *  Power-save on - Dynamic mode
 */
static int ecrnx_cfg80211_set_power_mgmt(struct wiphy *wiphy,
                                        struct net_device *dev,
                                        bool enabled, int timeout)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    u8 ps_mode;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    if (timeout >= 0)
        netdev_info(dev, "Ignore timeout value %d", timeout);

    if (!(ecrnx_hw->version_cfm.features & BIT(MM_FEAT_PS_BIT)))
        enabled = false;

    if (enabled) {
        /* Switch to Dynamic Power Save */
        ps_mode = MM_PS_MODE_ON_DYN;
    } else {
        /* Exit Power Save */
        ps_mode = MM_PS_MODE_OFF;
    }

    return ecrnx_send_me_set_ps_mode(ecrnx_hw, ps_mode);
}

static int ecrnx_cfg80211_set_txq_params(struct wiphy *wiphy, struct net_device *dev,
                                        struct ieee80211_txq_params *params)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    u8 hw_queue, aifs, cwmin, cwmax;
    u32 param;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    hw_queue = ecrnx_ac2hwq[0][params->ac];

    aifs  = params->aifs;
    cwmin = fls(params->cwmin);
    cwmax = fls(params->cwmax);

    /* Store queue information in general structure */
    param  = (u32) (aifs << 0);
    param |= (u32) (cwmin << 4);
    param |= (u32) (cwmax << 8);
    param |= (u32) (params->txop) << 12;

    /* Send the MM_SET_EDCA_REQ message to the FW */
    return ecrnx_send_set_edca(ecrnx_hw, hw_queue, param, false, ecrnx_vif->vif_index);
}


/**
 * @remain_on_channel: Request the driver to remain awake on the specified
 *	channel for the specified duration to complete an off-channel
 *	operation (e.g., public action frame exchange). When the driver is
 *	ready on the requested channel, it must indicate this with an event
 *	notification by calling cfg80211_ready_on_channel().
 */
static int
ecrnx_cfg80211_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
                                struct ieee80211_channel *chan,
                                unsigned int duration, u64 *cookie)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(wdev->netdev);
    struct ecrnx_roc *roc;
    int error;
    unsigned long timer = 0;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* For debug purpose (use ftrace kernel option) */
    trace_roc(ecrnx_vif->vif_index, chan->center_freq, duration);

    /* Check that no other RoC procedure has been launched */
    if (ecrnx_hw->roc)
    {
        ECRNX_ERR("%s-%d,statu error!!!, duration:%d \n", __func__, __LINE__, duration);
        //exp_report(ecrnx_hw);
            //wait for slave confirm
        //ecrnx_hw->p2p_listen.rxdatas = 0;
#ifdef CONFIG_ECRNX_P2P
		timer = wait_event_interruptible_timeout(ecrnx_hw->p2p_listen.rxdataq, (ecrnx_hw->roc == NULL), HZ/2);
		if (timer)
			ECRNX_PRINT("wait_event: wake up!!! timer:%ld \n", timer);
		else
			ECRNX_PRINT("wait_event: timout!!!\n");
#endif
        //return -EBUSY;
    }

    /* Allocate a temporary RoC element */
    roc = kmalloc(sizeof(struct ecrnx_roc), GFP_KERNEL);

    /* Verify that element has well been allocated */
    if (!roc)
        return -ENOMEM;

    /* Initialize the RoC information element */
    roc->vif = ecrnx_vif;
    roc->chan = chan;
    roc->duration = duration;
    roc->internal = false;
    roc->on_chan = false;

    /* Initialize the OFFCHAN TX queue to allow off-channel transmissions */
    ecrnx_txq_offchan_init(ecrnx_vif);

    /* Forward the information to the FMAC */
    ecrnx_hw->roc = roc;
    error = ecrnx_send_roc(ecrnx_hw, ecrnx_vif, chan, duration);

    /* If no error, keep all the information for handling of end of procedure */
    if (error == 0) {

        /* Set the cookie value */
        *cookie = (u64)(ecrnx_hw->roc_cookie);

#ifdef CONFIG_ECRNX_P2P
		if(ecrnx_vif->mgmt_reg_stypes & BIT(IEEE80211_STYPE_PROBE_REQ >> 4))
		{
			if(ecrnx_send_p2p_start_listen_req(ecrnx_hw, ecrnx_vif, duration))
				ECRNX_ERR("P2P: start_listen failed\n");
		}
#endif
    } else {
        kfree(roc);
        ecrnx_hw->roc = NULL;
        ecrnx_txq_offchan_deinit(ecrnx_vif);
    }

    return error;
}

/**
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *	This allows the operation to be terminated prior to timeout based on
 *	the duration value.
 */
static int ecrnx_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
                                                  struct wireless_dev *wdev,
                                                  u64 cookie)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(wdev->netdev);
	int error;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* For debug purpose (use ftrace kernel option) */
    trace_cancel_roc(ecrnx_vif->vif_index);

    /* Check if a RoC procedure is pending */
    if (!ecrnx_hw->roc)
        return 0;
#ifdef CONFIG_ECRNX_P2P
	//if(ecrnx_vif->mgmt_reg_stypes & BIT(IEEE80211_STYPE_PROBE_REQ >> 4))
	{
		error = ecrnx_send_p2p_cancel_listen_req(ecrnx_hw, ecrnx_vif);
		if(error == 0)
			ECRNX_PRINT("P2P: cancel_listen OK!!!\n");
		else
			ECRNX_ERR("P2P: cancel_listen failed, error=%d\n", error);
	}
#endif
    /* Forward the information to the FMAC */
    return ecrnx_send_cancel_roc(ecrnx_hw);
}

/**
 * @dump_survey: get site survey information.
 */
static int ecrnx_cfg80211_dump_survey(struct wiphy *wiphy, struct net_device *netdev,
                                     int idx, struct survey_info *info)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ieee80211_supported_band *sband;
    struct ecrnx_survey_info *ecrnx_survey;

    //ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (idx >= ARRAY_SIZE(ecrnx_hw->survey))
        return -ENOENT;

    ecrnx_survey = &ecrnx_hw->survey[idx];

    // Check if provided index matches with a supported 2.4GHz channel
    sband = wiphy->bands[NL80211_BAND_2GHZ];
    if (sband && idx >= sband->n_channels) {
        idx -= sband->n_channels;
        sband = NULL;
    }

    if (!sband) {
#ifdef CONFIG_ECRNX_5G
        // Check if provided index matches with a supported 5GHz channel
        sband = wiphy->bands[NL80211_BAND_5GHZ];
#endif
        if (!sband || idx >= sband->n_channels)
            return -ENOENT;
    }

    // Fill the survey
    info->channel = &sband->channels[idx];
    info->filled = ecrnx_survey->filled;

    if (ecrnx_survey->filled != 0) {
        SURVEY_TIME(info) = (u64)ecrnx_survey->chan_time_ms;
        SURVEY_TIME(info) = (u64)ecrnx_survey->chan_time_busy_ms;
        info->noise = ecrnx_survey->noise_dbm;

        // Set the survey report as not used
        ecrnx_survey->filled = 0;
    }

    return 0;
}

/**
 * @get_channel: Get the current operating channel for the virtual interface.
 *	For monitor interfaces, it should return %NULL unless there's a single
 *	current monitoring channel.
 */
int ecrnx_cfg80211_get_channel(struct wiphy *wiphy,
                                     struct wireless_dev *wdev,
                                     struct cfg80211_chan_def *chandef) {
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = container_of(wdev, struct ecrnx_vif, wdev);
    struct ecrnx_chanctx *ctxt;

    if (!ecrnx_vif->up) {
        return -ENODATA;
    }

    if (ecrnx_vif->vif_index == ecrnx_hw->monitor_vif)
    {
        //retrieve channel from firmware
        ecrnx_cfg80211_set_monitor_channel(wiphy, NULL);
    }

    //Check if channel context is valid
    if(!ecrnx_chanctx_valid(ecrnx_hw, ecrnx_vif->ch_index)){
        return -ENODATA;
    }

    ctxt = &ecrnx_hw->chanctx_table[ecrnx_vif->ch_index];
    *chandef = ctxt->chan_def;

    return 0;
}

/**
 * @mgmt_tx: Transmit a management frame.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
static int ecrnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
                                 struct cfg80211_mgmt_tx_params *params,
                                 u64 *cookie)
#else
static int ecrnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
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
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(wdev->netdev);
    struct ecrnx_sta *ecrnx_sta;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    struct ieee80211_channel *channel = params->chan;
    const u8 *buf = params->buf;
    bool offchan = false;
#endif
    struct ieee80211_mgmt *mgmt = (void *)buf;
    bool ap = false;

    /* Check if provided VIF is an AP or a STA one */
    switch (ECRNX_VIF_TYPE(ecrnx_vif)) {
        case NL80211_IFTYPE_AP_VLAN:
            ecrnx_vif = ecrnx_vif->ap_vlan.master;
            break;
        case NL80211_IFTYPE_AP:
        case NL80211_IFTYPE_P2P_GO:
        case NL80211_IFTYPE_MESH_POINT:
            ap = true;
            break;
        case NL80211_IFTYPE_STATION:
        case NL80211_IFTYPE_P2P_CLIENT:
        default:
            break;
    }

    /* Get STA on which management frame has to be sent */
    ecrnx_sta = ecrnx_retrieve_sta(ecrnx_hw, ecrnx_vif, mgmt->da,
                                 mgmt->frame_control, ap);

    trace_mgmt_tx((channel) ? channel->center_freq : 0,
                  ecrnx_vif->vif_index, (ecrnx_sta) ? ecrnx_sta->sta_idx : 0xFF,
                  mgmt);

    if (ap || ecrnx_sta)
        goto send_frame;

    /* Not an AP interface sending frame to unknown STA:
     * This is allowed for external authetication */
    if ((ecrnx_vif->sta.flags & ECRNX_STA_EXT_AUTH) && ieee80211_is_auth(mgmt->frame_control))
        goto send_frame;

	if(ieee80211_is_probe_resp(mgmt->frame_control))
		goto p2p_send_frame;

    /* Otherwise ROC is needed */
    if (!channel)
        return -EINVAL;

    /* Check that a RoC is already pending */
    if (ecrnx_hw->roc) {
        /* Get VIF used for current ROC */

        /* Check if RoC channel is the same than the required one */
        if ((ecrnx_hw->roc->vif != ecrnx_vif) ||
            (ecrnx_hw->roc->chan->center_freq != channel->center_freq))
            return -EINVAL;

    } else {
        u64 cookie;
        int error;

        /* Start a ROC procedure for 30ms */
        error = ecrnx_cfg80211_remain_on_channel(wiphy, wdev, channel,
                                                30, &cookie);
        if (error)
            return error;

        /* Need to keep in mind that RoC has been launched internally in order to
         * avoid to call the cfg80211 callback once expired */
        ecrnx_hw->roc->internal = true;
#ifdef CONFIG_ECRNX_P2P
		ecrnx_hw->p2p_listen.rxdatas = 0;
		wait_event_interruptible_timeout(ecrnx_hw->p2p_listen.rxdataq, ecrnx_hw->p2p_listen.rxdatas, HZ);
#endif
    }

p2p_send_frame:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    offchan = true;
#endif

send_frame:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    return ecrnx_start_mgmt_xmit(ecrnx_vif, ecrnx_sta, params, offchan, cookie);
#else
    return ecrnx_start_mgmt_xmit(ecrnx_vif, ecrnx_sta, channel, offchan, wait, buf, len, no_cck, dont_wait_for_ack, cookie);
#endif
}

/**
 * @start_radar_detection: Start radar detection in the driver.
 */
static int ecrnx_cfg80211_start_radar_detection(struct wiphy *wiphy,
                                        struct net_device *dev,
                                        struct cfg80211_chan_def *chandef,
                                        u32 cac_time_ms)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct apm_start_cac_cfm cfm;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
    ecrnx_radar_start_cac(&ecrnx_hw->radar, cac_time_ms, ecrnx_vif);
#endif
    ecrnx_send_apm_start_cac_req(ecrnx_hw, ecrnx_vif, chandef, &cfm);

    if (cfm.status == CO_OK) {
        spin_lock_bh(&ecrnx_hw->cb_lock);
        ecrnx_chanctx_link(ecrnx_vif, cfm.ch_idx, chandef);
        if (ecrnx_hw->cur_chanctx == ecrnx_vif->ch_index)
            ecrnx_radar_detection_enable(&ecrnx_hw->radar,
                                        ECRNX_RADAR_DETECT_REPORT,
                                        ECRNX_RADAR_RIU);
        spin_unlock_bh(&ecrnx_hw->cb_lock);
    } else {
        return -EIO;
    }

    return 0;
}

/**
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *	driver. If the SME is in the driver/firmware, this information can be
 *	used in building Authentication and Reassociation Request frames.
 */
static int ecrnx_cfg80211_update_ft_ies(struct wiphy *wiphy,
                            struct net_device *dev,
                            struct cfg80211_update_ft_ies_params *ftie)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif = netdev_priv(dev);
    const struct ecrnx_element *rsne = NULL, *mde = NULL, *fte = NULL, *elem;
    bool ft_in_non_rsn = false;
    int fties_len = 0;
    u8 *ft_assoc_ies, *pos;
    if ((ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_STATION) ||
        (vif->sta.ft_assoc_ies == NULL))
        return 0;
    for_each_ecrnx_element(elem, ftie->ie, ftie->ie_len) {
        if (elem->id == WLAN_EID_RSN)
            rsne = elem;
        else if (elem->id == WLAN_EID_MOBILITY_DOMAIN)
            mde = elem;
        else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION)
            fte = elem;
        else
            netdev_warn(dev, "Unexpected FT element %d\n", elem->id);
    }
    if (!mde) {
        netdev_warn(dev, "Didn't find Mobility_Domain Element\n");
        return 0;
    } else if (!rsne && !fte) {
        ft_in_non_rsn = true;
    } else if (!rsne || !fte) {
        netdev_warn(dev, "Didn't find RSN or Fast Transition Element\n");
        return 0;
    }
    for_each_ecrnx_element(elem, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len) {
        if ((elem->id == WLAN_EID_RSN) ||
            (elem->id == WLAN_EID_MOBILITY_DOMAIN) ||
            (elem->id == WLAN_EID_FAST_BSS_TRANSITION))
            fties_len += elem->datalen + sizeof(struct ecrnx_element);
    }
    ft_assoc_ies = kmalloc(vif->sta.ft_assoc_ies_len - fties_len + ftie->ie_len,
                        GFP_KERNEL);
    if (!ft_assoc_ies) {
        netdev_warn(dev, "Fail to allocate buffer for association elements");
    }
    pos = ft_assoc_ies;
    for_each_ecrnx_element(elem, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len) {
        if (elem->id == WLAN_EID_RSN) {
            if (ft_in_non_rsn) {
                netdev_warn(dev, "Found RSN element in non RSN FT");
                goto abort;
            } else if (!rsne) {
                netdev_warn(dev, "Found several RSN element");
                goto abort;
            } else {
                memcpy(pos, rsne, sizeof(*rsne) + rsne->datalen);
                pos += sizeof(*rsne) + rsne->datalen;
                rsne = NULL;
            }
        } else if (elem->id == WLAN_EID_MOBILITY_DOMAIN) {
            if (!mde) {
                netdev_warn(dev, "Found several Mobility Domain element");
                goto abort;
            } else {
                memcpy(pos, mde, sizeof(*mde) + mde->datalen);
                pos += sizeof(*mde) + mde->datalen;
                mde = NULL;
            }
        }
        else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION) {
            if (ft_in_non_rsn) {
                netdev_warn(dev, "Found Fast Transition element in non RSN FT");
                goto abort;
            } else if (!fte) {
                netdev_warn(dev, "found several Fast Transition element");
                goto abort;
            } else {
                memcpy(pos, fte, sizeof(*fte) + fte->datalen);
                pos += sizeof(*fte) + fte->datalen;
                fte = NULL;
            }
        }
        else {
            if (fte && !mde) {
                memcpy(pos, fte, sizeof(*fte) + fte->datalen);
                pos += sizeof(*fte) + fte->datalen;
                fte = NULL;
            }
            memcpy(pos, elem, sizeof(*elem) + elem->datalen);
            pos += sizeof(*elem) + elem->datalen;
        }
    }
    if (fte) {
        memcpy(pos, fte, sizeof(*fte) + fte->datalen);
        pos += sizeof(*fte) + fte->datalen;
        fte = NULL;
    }
    kfree(vif->sta.ft_assoc_ies);
    vif->sta.ft_assoc_ies = ft_assoc_ies;
    vif->sta.ft_assoc_ies_len = pos - ft_assoc_ies;
    if (vif->sta.flags & ECRNX_STA_FT_OVER_DS) {
        struct sm_connect_cfm sm_connect_cfm;
        struct cfg80211_connect_params sme;
        memset(&sme, 0, sizeof(sme));
        rsne = cfg80211_find_ecrnx_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies,
                                  vif->sta.ft_assoc_ies_len);
        if (rsne && ecrnx_rsne_to_connect_params(rsne, &sme)) {
            netdev_warn(dev, "FT RSN parsing failed\n");
            return 0;
        }
        sme.ssid_len = vif->sta.ft_assoc_ies[1];
        sme.ssid = &vif->sta.ft_assoc_ies[2];
        sme.bssid = vif->sta.ft_target_ap;
        sme.ie = &vif->sta.ft_assoc_ies[2 + sme.ssid_len];
        sme.ie_len = vif->sta.ft_assoc_ies_len - (2 + sme.ssid_len);
        sme.auth_type = NL80211_AUTHTYPE_FT;
        ecrnx_send_sm_connect_req(ecrnx_hw, vif, &sme, &sm_connect_cfm);
        vif->sta.flags &= ~ECRNX_STA_FT_OVER_DS;
    } else if (vif->sta.flags & ECRNX_STA_FT_OVER_AIR) {
        uint8_t ssid_len;
        vif->sta.flags &= ~ECRNX_STA_FT_OVER_AIR;
        ssid_len = vif->sta.ft_assoc_ies[1] + 2;
        if (ecrnx_send_sm_ft_auth_rsp(ecrnx_hw, vif, &vif->sta.ft_assoc_ies[ssid_len],
                                     vif->sta.ft_assoc_ies_len - ssid_len))
            netdev_err(dev, "FT Over Air: Failed to send updated assoc elem\n");
    }
    return 0;
abort:
    kfree(ft_assoc_ies);
    return 0;
}

/**
 * @set_cqm_rssi_config: Configure connection quality monitor RSSI threshold.
 */
static int ecrnx_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
                                  struct net_device *dev,
                                  int32_t rssi_thold, uint32_t rssi_hyst)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);

    return ecrnx_send_cfg_rssi_req(ecrnx_hw, ecrnx_vif->vif_index, rssi_thold, rssi_hyst);
}

/**
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *	responsible for veryfing if the switch is possible. Since this is
 *	inherently tricky driver may decide to disconnect an interface later
 *	with cfg80211_stop_iface(). This doesn't mean driver can accept
 *	everything. It should do it's best to verify requests and reject them
 *	as soon as possible.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int ecrnx_cfg80211_channel_switch(struct wiphy *wiphy,
                                 struct net_device *dev,
                                 struct cfg80211_csa_settings *params)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *vif = netdev_priv(dev);
    struct ecrnx_ipc_elem_var elem;
    struct ecrnx_bcn *bcn, *bcn_after;
    struct ecrnx_csa *csa;
    u16 csa_oft[BCN_MAX_CSA_CPT];
    u8 *buf;
    int i, error = 0;


    if (vif->ap.csa)
        return -EBUSY;

    if (params->n_counter_offsets_beacon > BCN_MAX_CSA_CPT)
        return -EINVAL;

    /* Build the new beacon with CSA IE */
    bcn = &vif->ap.bcn;
    buf = ecrnx_build_bcn(bcn, &params->beacon_csa);
    if (!buf)
        return -ENOMEM;

    memset(csa_oft, 0, sizeof(csa_oft));
    for (i = 0; i < params->n_counter_offsets_beacon; i++)
    {
        csa_oft[i] = params->counter_offsets_beacon[i] + bcn->head_len +
            bcn->tim_len;
    }

    /* If count is set to 0 (i.e anytime after this beacon) force it to 2 */
    if (params->count == 0) {
        params->count = 2;
        for (i = 0; i < params->n_counter_offsets_beacon; i++)
        {
            buf[csa_oft[i]] = 2;
        }
    }

    if ((error = ecrnx_ipc_elem_var_allocs(ecrnx_hw, &elem, bcn->len,
                                          DMA_TO_DEVICE, buf, NULL, NULL))) {
        goto end;
    }

    /* Build the beacon to use after CSA. It will only be sent to fw once
       CSA is over, but do it before sending the beacon as it must be ready
       when CSA is finished. */
    csa = kzalloc(sizeof(struct ecrnx_csa), GFP_KERNEL);
    if (!csa) {
        error = -ENOMEM;
        goto end;
    }

    bcn_after = &csa->bcn;
    buf = ecrnx_build_bcn(bcn_after, &params->beacon_after);
    if (!buf) {
        error = -ENOMEM;
        ecrnx_del_csa(vif);
        goto end;
    }

    if ((error = ecrnx_ipc_elem_var_allocs(ecrnx_hw, &csa->elem, bcn_after->len,
                                          DMA_TO_DEVICE, buf, NULL, NULL))) {
        goto end;
    }

    vif->ap.csa = csa;
    csa->vif = vif;
    csa->chandef = params->chandef;

    /* Send new Beacon. FW will extract channel and count from the beacon */
    error = ecrnx_send_bcn_change(ecrnx_hw, vif->vif_index, elem.dma_addr,
                                 bcn->len, bcn->head_len, bcn->tim_len, csa_oft);

    if (error) {
        ecrnx_del_csa(vif);
        goto end;
    } else {
        INIT_WORK(&csa->work, ecrnx_csa_finish);
        cfg80211_ch_switch_started_notify(dev, &csa->chandef, params->count);
    }

  end:
    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &elem);
    return error;
}
#endif

/*
 * @tdls_mgmt: prepare TDLS action frame packets and forward them to FW
 */
static int ecrnx_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
                        const u8 *peer, u8 action_code,  u8 dialog_token,
                        u16 status_code, u32 peer_capability,
                        bool initiator, const u8 *buf, size_t len)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    int ret = 0;

    /* make sure we support TDLS */
    if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
        return -ENOTSUPP;

    /* make sure we are in station mode (and connected) */
    if ((ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_STATION) ||
        (!ecrnx_vif->up) || (!ecrnx_vif->sta.ap))
        return -ENOTSUPP;

    /* only one TDLS link is supported */
    if ((action_code == WLAN_TDLS_SETUP_REQUEST) &&
        (ecrnx_vif->sta.tdls_sta) &&
        (ecrnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
        ECRNX_ERR("%s: only one TDLS link is supported!\n", __func__);
        return -ENOTSUPP;
    }

    if ((action_code == WLAN_TDLS_DISCOVERY_REQUEST) &&
        (ecrnx_hw->mod_params->ps_on)) {
        ECRNX_ERR("%s: discovery request is not supported when "
                "power-save is enabled!\n", __func__);
        return -ENOTSUPP;
    }

    switch (action_code) {
    case WLAN_TDLS_SETUP_RESPONSE:
        /* only one TDLS link is supported */
        if ((status_code == 0) &&
            (ecrnx_vif->sta.tdls_sta) &&
            (ecrnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
            ECRNX_ERR("%s: only one TDLS link is supported!\n", __func__);
            status_code = WLAN_STATUS_REQUEST_DECLINED;
        }
        /* fall-through */
    case WLAN_TDLS_SETUP_REQUEST:
    case WLAN_TDLS_TEARDOWN:
    case WLAN_TDLS_DISCOVERY_REQUEST:
    case WLAN_TDLS_SETUP_CONFIRM:
    case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
        ret = ecrnx_tdls_send_mgmt_packet_data(ecrnx_hw, ecrnx_vif, peer, action_code,
                dialog_token, status_code, peer_capability, initiator, buf, len, 0, NULL);
        break;

    default:
        ECRNX_ERR("%s: Unknown TDLS mgmt/action frame %pM\n",
                __func__, peer);
        ret = -EOPNOTSUPP;
        break;
    }

    if (action_code == WLAN_TDLS_SETUP_REQUEST) {
        ecrnx_vif->tdls_status = TDLS_SETUP_REQ_TX;
    } else if (action_code == WLAN_TDLS_SETUP_RESPONSE) {
        ecrnx_vif->tdls_status = TDLS_SETUP_RSP_TX;
    } else if ((action_code == WLAN_TDLS_SETUP_CONFIRM) && (ret == CO_OK)) {
        ecrnx_vif->tdls_status = TDLS_LINK_ACTIVE;
        /* Set TDLS active */
        ecrnx_vif->sta.tdls_sta->tdls.active = true;
    }

    return ret;
}

/*
 * @tdls_oper: execute TDLS operation
 */
static int ecrnx_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
        const u8 *peer, enum nl80211_tdls_operation oper)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    int error;

    if (oper != NL80211_TDLS_DISABLE_LINK)
        return 0;

    if (!ecrnx_vif->sta.tdls_sta) {
        ECRNX_ERR("%s: TDLS station %pM does not exist\n", __func__, peer);
        return -ENOLINK;
    }

    if (memcmp(ecrnx_vif->sta.tdls_sta->mac_addr, peer, ETH_ALEN) == 0) {
        /* Disable Channel Switch */
        if (!ecrnx_send_tdls_cancel_chan_switch_req(ecrnx_hw, ecrnx_vif,
                                                   ecrnx_vif->sta.tdls_sta,
                                                   NULL))
            ecrnx_vif->sta.tdls_sta->tdls.chsw_en = false;

        netdev_info(dev, "Del TDLS sta %d (%pM)",
                ecrnx_vif->sta.tdls_sta->sta_idx,
                ecrnx_vif->sta.tdls_sta->mac_addr);
        /* Ensure that we won't process PS change ind */
        spin_lock_bh(&ecrnx_hw->cb_lock);
        ecrnx_vif->sta.tdls_sta->ps.active = false;
        ecrnx_vif->sta.tdls_sta->valid = false;
        spin_unlock_bh(&ecrnx_hw->cb_lock);
        ecrnx_txq_sta_deinit(ecrnx_hw, ecrnx_vif->sta.tdls_sta);
        error = ecrnx_send_me_sta_del(ecrnx_hw, ecrnx_vif->sta.tdls_sta->sta_idx, true);
        if ((error != 0) && (error != -EPIPE))
            return error;

#ifdef CONFIG_ECRNX_BFMER
            // Disable Beamformer if supported
            ecrnx_bfmer_report_del(ecrnx_hw, ecrnx_vif->sta.tdls_sta);
            ecrnx_mu_group_sta_del(ecrnx_hw, ecrnx_vif->sta.tdls_sta);
#endif /* CONFIG_ECRNX_BFMER */

        /* Set TDLS not active */
        ecrnx_vif->sta.tdls_sta->tdls.active = false;
        ecrnx_dbgfs_unregister_sta(ecrnx_hw, ecrnx_vif->sta.tdls_sta);
        // Remove TDLS station
        ecrnx_vif->tdls_status = TDLS_LINK_IDLE;
        ecrnx_vif->sta.tdls_sta = NULL;
    }

    return 0;
}

/*
 * @tdls_channel_switch: enable TDLS channel switch
 */
static int ecrnx_cfg80211_tdls_channel_switch(struct wiphy *wiphy,
                                      struct net_device *dev,
                                      const u8 *addr, u8 oper_class,
                                      struct cfg80211_chan_def *chandef)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_sta *ecrnx_sta = ecrnx_vif->sta.tdls_sta;
    struct tdls_chan_switch_cfm cfm;
    int error;

    if ((!ecrnx_sta) || (memcmp(addr, ecrnx_sta->mac_addr, ETH_ALEN))) {
        ECRNX_ERR("%s: TDLS station %pM doesn't exist\n", __func__, addr);
        return -ENOLINK;
    }

    if (!ecrnx_sta->tdls.chsw_allowed) {
        ECRNX_ERR("%s: TDLS station %pM does not support TDLS channel switch\n", __func__, addr);
        return -ENOTSUPP;
    }

    error = ecrnx_send_tdls_chan_switch_req(ecrnx_hw, ecrnx_vif, ecrnx_sta,
                                           ecrnx_sta->tdls.initiator,
                                           oper_class, chandef, &cfm);
    if (error)
        return error;

    if (!cfm.status) {
        ecrnx_sta->tdls.chsw_en = true;
        return 0;
    } else {
        ECRNX_ERR("%s: TDLS channel switch already enabled and only one is supported\n", __func__);
        return -EALREADY;
    }
}

/*
 * @tdls_cancel_channel_switch: disable TDLS channel switch
 */
static void ecrnx_cfg80211_tdls_cancel_channel_switch(struct wiphy *wiphy,
                                              struct net_device *dev,
                                              const u8 *addr)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_sta *ecrnx_sta = ecrnx_vif->sta.tdls_sta;
    struct tdls_cancel_chan_switch_cfm cfm;

    if (!ecrnx_sta)
        return;

    if (!ecrnx_send_tdls_cancel_chan_switch_req(ecrnx_hw, ecrnx_vif,
                                               ecrnx_sta, &cfm))
        ecrnx_sta->tdls.chsw_en = false;
}

/**
 * @change_bss: Modify parameters for a given BSS (mainly for AP mode).
 */
static int ecrnx_cfg80211_change_bss(struct wiphy *wiphy, struct net_device *dev,
                             struct bss_parameters *params)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    int res =  -EOPNOTSUPP;

    if (((ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP) ||
         (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
        (params->ap_isolate > -1)) {

        if (params->ap_isolate)
            ecrnx_vif->ap.flags |= ECRNX_AP_ISOLATE;
        else
            ecrnx_vif->ap.flags &= ~ECRNX_AP_ISOLATE;

        res = 0;
    }

    return res;
}


/**
 * @get_station: get station information for the station identified by @mac
 */
static int ecrnx_fill_station_info(struct ecrnx_sta *sta, struct ecrnx_vif *vif,
                                  struct station_info *sinfo)
{
    struct ecrnx_sta_stats *stats = &sta->stats;
    struct rx_vector_1 *rx_vect1 = &stats->last_rx.rx_vect1;

    // Generic info
    sinfo->generation = vif->generation;

    //sinfo->inactive_time = jiffies_to_msecs(jiffies - stats->last_act);
    sinfo->rx_bytes = stats->rx_bytes;
    sinfo->tx_bytes = stats->tx_bytes;
    sinfo->tx_packets = stats->tx_pkts;
    sinfo->rx_packets = stats->rx_pkts;
    sinfo->signal = rx_vect1->rssi1;
    sinfo->tx_failed = 1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    switch (rx_vect1->ch_bw) {
        case PHY_CHNL_BW_20:
            sinfo->rxrate.bw = RATE_INFO_BW_20;
            break;
        case PHY_CHNL_BW_40:
            sinfo->rxrate.bw = RATE_INFO_BW_40;
            break;
        case PHY_CHNL_BW_80:
            sinfo->rxrate.bw = RATE_INFO_BW_80;
            break;
        case PHY_CHNL_BW_160:
            sinfo->rxrate.bw = RATE_INFO_BW_160;
            break;
        default:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
            sinfo->rxrate.bw = RATE_INFO_BW_HE_RU;
#else
            sinfo->rxrate.bw = RATE_INFO_BW_160;
#endif
            break;
    }
#endif

    switch (rx_vect1->format_mod) {
        case FORMATMOD_NON_HT:
        case FORMATMOD_NON_HT_DUP_OFDM:
            sinfo->rxrate.flags = 0;
            sinfo->rxrate.legacy = legrates_lut[rx_vect1->leg_rate].rate;
            break;
        case FORMATMOD_HT_MF:
        case FORMATMOD_HT_GF:
            sinfo->rxrate.flags = RATE_INFO_FLAGS_MCS;
            if (rx_vect1->ht.short_gi)
                sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
            sinfo->rxrate.mcs = rx_vect1->ht.mcs;
            break;
        case FORMATMOD_VHT:
            sinfo->rxrate.flags = RATE_INFO_FLAGS_VHT_MCS;
            if (rx_vect1->vht.short_gi)
                sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
            sinfo->rxrate.mcs = rx_vect1->vht.mcs;
            sinfo->rxrate.nss = rx_vect1->vht.nss;
            break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#if CONFIG_ECRNX_HE
        case FORMATMOD_HE_MU:
            sinfo->rxrate.he_ru_alloc = rx_vect1->he.ru_size;
            break;
        case FORMATMOD_HE_SU:
        case FORMATMOD_HE_ER:
        case FORMATMOD_HE_TB:
            sinfo->rxrate.flags = RATE_INFO_FLAGS_HE_MCS;
            sinfo->rxrate.mcs = rx_vect1->he.mcs;
            sinfo->rxrate.nss = rx_vect1->he.nss;
            sinfo->rxrate.he_gi = rx_vect1->he.gi_type;
            sinfo->rxrate.he_dcm = rx_vect1->he.dcm;
            break;
#endif
#endif
        default :
            return -EINVAL;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
    sinfo->filled |= (STATION_INFO_INACTIVE_TIME |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
                     STATION_INFO_RX_BYTES64 |
                     STATION_INFO_TX_BYTES64 |
#endif
                     STATION_INFO_RX_PACKETS |
                     STATION_INFO_TX_PACKETS |
                     STATION_INFO_SIGNAL |
                     STATION_INFO_RX_BITRATE);
#else
    sinfo->filled = (BIT(NL80211_STA_INFO_RX_BYTES64)    |
                     BIT(NL80211_STA_INFO_TX_BYTES64)    |
                     BIT(NL80211_STA_INFO_RX_PACKETS)    |
                     BIT(NL80211_STA_INFO_TX_PACKETS)    |
                     BIT(NL80211_STA_INFO_SIGNAL)        |
                     BIT(NL80211_STA_INFO_TX_BITRATE)    |
                     BIT(NL80211_STA_INFO_TX_FAILED)     |
                     BIT(NL80211_STA_INFO_RX_BITRATE));
#endif

    // Mesh specific info
    if (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT)
    {
        struct mesh_peer_info_cfm peer_info_cfm;
        if (ecrnx_send_mesh_peer_info_req(vif->ecrnx_hw, vif, sta->sta_idx,
                                         &peer_info_cfm))
            return -ENOMEM;

        peer_info_cfm.last_bcn_age = peer_info_cfm.last_bcn_age / 1000;
        if (peer_info_cfm.last_bcn_age < sinfo->inactive_time)
            sinfo->inactive_time = peer_info_cfm.last_bcn_age;

        sinfo->llid = peer_info_cfm.local_link_id;
        sinfo->plid = peer_info_cfm.peer_link_id;
        sinfo->plink_state = peer_info_cfm.link_state;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
        sinfo->local_pm = peer_info_cfm.local_ps_mode;
        sinfo->peer_pm = peer_info_cfm.peer_ps_mode;
        sinfo->nonpeer_pm = peer_info_cfm.non_peer_ps_mode;
#endif
        sinfo->filled |= (BIT(NL80211_STA_INFO_LLID) |
                          BIT(NL80211_STA_INFO_PLID) |
                          BIT(NL80211_STA_INFO_PLINK_STATE) |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
                          BIT(NL80211_STA_INFO_LOCAL_PM) |
                          BIT(NL80211_STA_INFO_PEER_PM) |
                          BIT(NL80211_STA_INFO_NONPEER_PM)|
#endif
                          0);
    }

    sinfo->txrate.legacy = 0x6818;

    return 0;
}

static int ecrnx_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
                    u8 *mac,
#else
                    const u8 *mac,
#endif
                     struct station_info *sinfo)
{
    struct ecrnx_vif *vif = netdev_priv(dev);
    struct ecrnx_sta *sta = NULL;

    if (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
        return -EINVAL;
    else if ((ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) ||
             (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
        if (vif->sta.ap && ether_addr_equal(vif->sta.ap->mac_addr, mac))
            sta = vif->sta.ap;
    }
    else
    {
        struct ecrnx_sta *sta_iter;
        list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
            if (sta_iter->valid && ether_addr_equal(sta_iter->mac_addr, mac)) {
                sta = sta_iter;
                break;
            }
        }
    }

    if (sta)
        return ecrnx_fill_station_info(sta, vif, sinfo);

    return -EINVAL;
}

int ecrnx_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
                                      int idx, u8 *mac, struct station_info *sinfo)
{
    struct ecrnx_vif *vif = netdev_priv(dev);
    struct ecrnx_sta *sta = NULL;

    if (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
        return -EINVAL;
    else if ((ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) ||
             (ECRNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
        if ((idx == 0) && vif->sta.ap && vif->sta.ap->valid)
            sta = vif->sta.ap;
    } else {
        struct ecrnx_sta *sta_iter;
        int i = 0;
        list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
            if (i == idx) {
                sta = sta_iter;
                break;
            }
            i++;
        }
    }

    if (sta == NULL)
        return -ENOENT;


    memcpy(mac, &sta->mac_addr, ETH_ALEN);

    return ecrnx_fill_station_info(sta, vif, sinfo);
}

/**
 * @add_mpath: add a fixed mesh path
 */
static int ecrnx_cfg80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
                                   const u8 *dst, const u8 *next_hop)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return ecrnx_send_mesh_path_update_req(ecrnx_hw, ecrnx_vif, dst, next_hop, &cfm);
}

/**
 * @del_mpath: delete a given mesh path
 */
static int ecrnx_cfg80211_del_mpath(struct wiphy *wiphy, struct net_device *dev,
                                   const u8 *dst)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return ecrnx_send_mesh_path_update_req(ecrnx_hw, ecrnx_vif, dst, NULL, &cfm);
}

/**
 * @change_mpath: change a given mesh path
 */
static int ecrnx_cfg80211_change_mpath(struct wiphy *wiphy, struct net_device *dev,
                                      const u8 *dst, const u8 *next_hop)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return ecrnx_send_mesh_path_update_req(ecrnx_hw, ecrnx_vif, dst, next_hop, &cfm);
}

/**
 * @get_mpath: get a mesh path for the given parameters
 */
static int ecrnx_cfg80211_get_mpath(struct wiphy *wiphy, struct net_device *dev,
                                   u8 *dst, u8 *next_hop, struct mpath_info *pinfo)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_mesh_path *mesh_path = NULL;
    struct ecrnx_mesh_path *cur;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &ecrnx_vif->ap.mpath_list, list) {
        /* Compare the path target address and the provided destination address */
        if (memcmp(dst, &cur->tgt_mac_addr, ETH_ALEN)) {
            continue;
        }

        mesh_path = cur;
        break;
    }

    if (mesh_path == NULL)
        return -ENOENT;

    /* Copy next HOP MAC address */
    if (mesh_path->nhop_sta)
        memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = ecrnx_vif->generation;

    return 0;
}

/**
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 */
static int ecrnx_cfg80211_dump_mpath(struct wiphy *wiphy, struct net_device *dev,
                                    int idx, u8 *dst, u8 *next_hop,
                                    struct mpath_info *pinfo)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_mesh_path *mesh_path = NULL;
    struct ecrnx_mesh_path *cur;
    int i = 0;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &ecrnx_vif->ap.mpath_list, list) {
        if (i < idx) {
            i++;
            continue;
        }

        mesh_path = cur;
        break;
    }

    if (mesh_path == NULL)
        return -ENOENT;

    /* Copy target and next hop MAC address */
    memcpy(dst, &mesh_path->tgt_mac_addr, ETH_ALEN);
    if (mesh_path->nhop_sta)
        memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = ecrnx_vif->generation;

    return 0;
}

/**
 * @get_mpp: get a mesh proxy path for the given parameters
 */
static int ecrnx_cfg80211_get_mpp(struct wiphy *wiphy, struct net_device *dev,
                                 u8 *dst, u8 *mpp, struct mpath_info *pinfo)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_mesh_proxy *mesh_proxy = NULL;
    struct ecrnx_mesh_proxy *cur;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &ecrnx_vif->ap.proxy_list, list) {
        if (cur->local) {
            continue;
        }

        /* Compare the path target address and the provided destination address */
        if (memcmp(dst, &cur->ext_sta_addr, ETH_ALEN)) {
            continue;
        }

        mesh_proxy = cur;
        break;
    }

    if (mesh_proxy == NULL)
        return -ENOENT;

    memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = ecrnx_vif->generation;

    return 0;
}

/**
 * @dump_mpp: dump mesh proxy path callback -- resume dump at index @idx
 */
static int ecrnx_cfg80211_dump_mpp(struct wiphy *wiphy, struct net_device *dev,
                                  int idx, u8 *dst, u8 *mpp, struct mpath_info *pinfo)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_mesh_proxy *mesh_proxy = NULL;
    struct ecrnx_mesh_proxy *cur;
    int i = 0;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &ecrnx_vif->ap.proxy_list, list) {
        if (cur->local) {
            continue;
        }

        if (i < idx) {
            i++;
            continue;
        }

        mesh_proxy = cur;
        break;
    }

    if (mesh_proxy == NULL)
        return -ENOENT;

    /* Copy target MAC address */
    memcpy(dst, &mesh_proxy->ext_sta_addr, ETH_ALEN);
    memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = ecrnx_vif->generation;

    return 0;
}

/**
 * @get_mesh_config: Get the current mesh configuration
 */
static int ecrnx_cfg80211_get_mesh_config(struct wiphy *wiphy, struct net_device *dev,
                                         struct mesh_config *conf)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return 0;
}

/**
 * @update_mesh_config: Update mesh parameters on a running mesh.
 */
static int ecrnx_cfg80211_update_mesh_config(struct wiphy *wiphy, struct net_device *dev,
                                            u32 mask, const struct mesh_config *nconf)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct mesh_update_cfm cfm;
    int status;

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    if (mask & CO_BIT(NL80211_MESHCONF_POWER_MODE - 1)) {
        ecrnx_vif->ap.next_mesh_pm = nconf->power_mode;

        if (!list_empty(&ecrnx_vif->ap.sta_list)) {
            // If there are mesh links we don't want to update the power mode
            // It will be updated with ecrnx_update_mesh_power_mode() when the
            // ps mode of a link is updated or when a new link is added/removed
            mask &= ~BIT(NL80211_MESHCONF_POWER_MODE - 1);

            if (!mask)
                return 0;
        }
    }
#endif

    status = ecrnx_send_mesh_update_req(ecrnx_hw, ecrnx_vif, mask, nconf, &cfm);

    if (!status && (cfm.status != 0))
        status = -EINVAL;

    return status;
}

/**
 * @join_mesh: join the mesh network with the specified parameters
 * (invoked with the wireless_dev mutex held)
 */
static int ecrnx_cfg80211_join_mesh(struct wiphy *wiphy, struct net_device *dev,
                                   const struct mesh_config *conf, const struct mesh_setup *setup)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct mesh_start_cfm mesh_start_cfm;
    int error = 0;
    u8 txq_status = 0;
    /* STA for BC/MC traffic */
    struct ecrnx_sta *sta;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (ECRNX_VIF_TYPE(ecrnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    /* Forward the information to the UMAC */
    if ((error = ecrnx_send_mesh_start_req(ecrnx_hw, ecrnx_vif, conf, setup, &mesh_start_cfm))) {
        return error;
    }

    /* Check the status */
    switch (mesh_start_cfm.status) {
        case CO_OK:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            ecrnx_vif->ap.bcmc_index = mesh_start_cfm.bcmc_idx;
            ecrnx_vif->ap.bcn_interval = setup->beacon_interval;
#endif
            ecrnx_vif->ap.flags = 0;
            ecrnx_vif->use_4addr = true;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            if (setup->user_mpm)
                ecrnx_vif->ap.flags |= ECRNX_AP_USER_MESH_PM;
#endif

            sta = &ecrnx_hw->sta_table[mesh_start_cfm.bcmc_idx];
            sta->valid = true;
            sta->aid = 0;
            sta->sta_idx = mesh_start_cfm.bcmc_idx;
            sta->ch_idx = mesh_start_cfm.ch_idx;
            sta->vif_idx = ecrnx_vif->vif_index;
            sta->qos = true;
            sta->acm = 0;
            sta->ps.active = false;
            sta->listen_interval = 5;
            ecrnx_mu_group_sta_init(sta, NULL);
            spin_lock_bh(&ecrnx_hw->cb_lock);
            ecrnx_chanctx_link(ecrnx_vif, mesh_start_cfm.ch_idx,
                              (struct cfg80211_chan_def *)(&setup->chandef));
            if (ecrnx_hw->cur_chanctx != mesh_start_cfm.ch_idx) {
                txq_status = ECRNX_TXQ_STOP_CHAN;
            }
            ecrnx_txq_vif_init(ecrnx_hw, ecrnx_vif, txq_status);
            spin_unlock_bh(&ecrnx_hw->cb_lock);

            netif_tx_start_all_queues(dev);
            netif_carrier_on(dev);

            /* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
            if (ecrnx_hw->cur_chanctx == mesh_start_cfm.ch_idx) {
                ecrnx_radar_detection_enable_on_cur_channel(ecrnx_hw);
            }
            break;

        case CO_BUSY:
            error = -EINPROGRESS;
            break;

        default:
            error = -EIO;
            break;
    }

    /* Print information about the operation */
    if (error) {
        netdev_info(dev, "Failed to start MP (%d)", error);
    } else {
        netdev_info(dev, "MP started: ch=%d, bcmc_idx=%d",
                    ecrnx_vif->ch_index, ecrnx_vif->ap.bcmc_index);
    }

    return error;
}

/**
 * @leave_mesh: leave the current mesh network
 * (invoked with the wireless_dev mutex held)
 */
static int ecrnx_cfg80211_leave_mesh(struct wiphy *wiphy, struct net_device *dev)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);
    struct ecrnx_vif *ecrnx_vif = netdev_priv(dev);
    struct mesh_stop_cfm mesh_stop_cfm;
    int error = 0;

    error = ecrnx_send_mesh_stop_req(ecrnx_hw, ecrnx_vif, &mesh_stop_cfm);

    if (error == 0) {
        /* Check the status */
        switch (mesh_stop_cfm.status) {
            case CO_OK:
                spin_lock_bh(&ecrnx_hw->cb_lock);
                ecrnx_chanctx_unlink(ecrnx_vif);
                ecrnx_radar_cancel_cac(&ecrnx_hw->radar);
                spin_unlock_bh(&ecrnx_hw->cb_lock);
                /* delete BC/MC STA */
                ecrnx_txq_vif_deinit(ecrnx_hw, ecrnx_vif);
                ecrnx_del_bcn(&ecrnx_vif->ap.bcn);

                netif_tx_stop_all_queues(dev);
                netif_carrier_off(dev);

                break;

            default:
                error = -EIO;
                break;
        }
    }

    if (error) {
        netdev_info(dev, "Failed to stop MP");
    } else {
        netdev_info(dev, "MP Stopped");
    }

    return 0;
}

#ifdef CONFIG_ECRNX_P2P
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 8, 0)
static void ecrnx_cfg80211_update_mgmt_frame_registrations(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  struct mgmt_frame_regs *upd)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(wdev->netdev);

	ECRNX_DBG(ECRNX_FN_ENTRY_STR);
	
	if (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_STATION)
	{		
		ecrnx_vif->mgmt_reg_stypes = upd->interface_stypes & BIT(IEEE80211_STYPE_PROBE_REQ >> 4);
    }
}
#else
static void ecrnx_cfg80211_mgmt_frame_register(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   u16 frame_type, bool reg)
{
    struct ecrnx_vif *ecrnx_vif = netdev_priv(wdev->netdev);

	u16 mgmt_type;

	mgmt_type = (frame_type & IEEE80211_FCTL_STYPE) >> 4;

	if (reg)
		ecrnx_vif->mgmt_reg_stypes |= BIT(mgmt_type);
	else
		ecrnx_vif->mgmt_reg_stypes &= ~BIT(mgmt_type);
}			   
#endif
#endif

static struct cfg80211_ops ecrnx_cfg80211_ops = {
    .add_virtual_intf = ecrnx_cfg80211_add_iface,
    .del_virtual_intf = ecrnx_cfg80211_del_iface,
    .change_virtual_intf = ecrnx_cfg80211_change_iface,
    .start_p2p_device = ecrnx_cfg80211_start_p2p_device,
    .stop_p2p_device = ecrnx_cfg80211_stop_p2p_device,
    .scan = ecrnx_cfg80211_scan,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
    .abort_scan = ecrnx_cfg80211_abort_scan,
#endif
    .connect = ecrnx_cfg80211_connect,
    .disconnect = ecrnx_cfg80211_disconnect,
    .add_key = ecrnx_cfg80211_add_key,
    .get_key = ecrnx_cfg80211_get_key,
    .del_key = ecrnx_cfg80211_del_key,
    .set_default_key = ecrnx_cfg80211_set_default_key,
    .set_default_mgmt_key = ecrnx_cfg80211_set_default_mgmt_key,
    .add_station = ecrnx_cfg80211_add_station,
    .del_station = ecrnx_cfg80211_del_station,
    .change_station = ecrnx_cfg80211_change_station,
    .mgmt_tx = ecrnx_cfg80211_mgmt_tx,
    .start_ap = ecrnx_cfg80211_start_ap,
    .change_beacon = ecrnx_cfg80211_change_beacon,
    .stop_ap = ecrnx_cfg80211_stop_ap,
    .set_monitor_channel = ecrnx_cfg80211_set_monitor_channel,
    .probe_client = ecrnx_cfg80211_probe_client,
    .set_wiphy_params = ecrnx_cfg80211_set_wiphy_params,
    .set_txq_params = ecrnx_cfg80211_set_txq_params,
    .set_tx_power = ecrnx_cfg80211_set_tx_power,
//    .get_tx_power = ecrnx_cfg80211_get_tx_power,
    .set_power_mgmt = ecrnx_cfg80211_set_power_mgmt,
    .get_station = ecrnx_cfg80211_get_station,
    .remain_on_channel = ecrnx_cfg80211_remain_on_channel,
    .cancel_remain_on_channel = ecrnx_cfg80211_cancel_remain_on_channel,
    .dump_survey = ecrnx_cfg80211_dump_survey,
    .get_channel = ecrnx_cfg80211_get_channel,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    .start_radar_detection = ecrnx_cfg80211_start_radar_detection,
    .update_ft_ies = ecrnx_cfg80211_update_ft_ies,
#endif
    .set_cqm_rssi_config = ecrnx_cfg80211_set_cqm_rssi_config,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    .channel_switch = ecrnx_cfg80211_channel_switch,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    .tdls_channel_switch = ecrnx_cfg80211_tdls_channel_switch,
    .tdls_cancel_channel_switch = ecrnx_cfg80211_tdls_cancel_channel_switch,
#endif
    .tdls_mgmt = ecrnx_cfg80211_tdls_mgmt,
    .tdls_oper = ecrnx_cfg80211_tdls_oper,
    .change_bss = ecrnx_cfg80211_change_bss,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
    .external_auth = ecrnx_cfg80211_external_auth,
#endif

#ifdef CONFIG_ECRNX_P2P
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	.update_mgmt_frame_registrations =
		ecrnx_cfg80211_update_mgmt_frame_registrations,
#else
    .mgmt_frame_register = ecrnx_cfg80211_mgmt_frame_register,
#endif
#endif

};


/*********************************************************************
 * Init/Exit functions
 *********************************************************************/
static void ecrnx_wdev_unregister(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_vif *ecrnx_vif, *tmp;

    rtnl_lock();
    list_for_each_entry_safe(ecrnx_vif, tmp, &ecrnx_hw->vifs, list) {
        ecrnx_cfg80211_del_iface(ecrnx_hw->wiphy, &ecrnx_vif->wdev);
    }
    rtnl_unlock();
}

static void ecrnx_set_vers(struct ecrnx_hw *ecrnx_hw)
{
    u32 vers = ecrnx_hw->version_cfm.version_lmac;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    snprintf(ecrnx_hw->wiphy->fw_version,
             sizeof(ecrnx_hw->wiphy->fw_version), "%d.%d.%d.%d",
             (vers & (0xff << 24)) >> 24, (vers & (0xff << 16)) >> 16,
             (vers & (0xff <<  8)) >>  8, (vers & (0xff <<  0)) >>  0);
    ecrnx_hw->machw_type = ecrnx_machw_type(ecrnx_hw->version_cfm.version_machw_2);
}

static void ecrnx_reg_notifier(struct wiphy *wiphy,
                              struct regulatory_request *request)
{
    struct ecrnx_hw *ecrnx_hw = wiphy_priv(wiphy);

    // For now trust all initiator
    ecrnx_radar_set_domain(&ecrnx_hw->radar, request->dfs_region);
    ecrnx_send_me_chan_config_req(ecrnx_hw);
}

static void ecrnx_enable_mesh(struct ecrnx_hw *ecrnx_hw)
{
    struct wiphy *wiphy = ecrnx_hw->wiphy;

    if (!ecrnx_mod_params.mesh)
        return;

    ecrnx_cfg80211_ops.add_mpath = ecrnx_cfg80211_add_mpath;
    ecrnx_cfg80211_ops.del_mpath = ecrnx_cfg80211_del_mpath;
    ecrnx_cfg80211_ops.change_mpath = ecrnx_cfg80211_change_mpath;
    ecrnx_cfg80211_ops.get_mpath = ecrnx_cfg80211_get_mpath;
    ecrnx_cfg80211_ops.dump_mpath = ecrnx_cfg80211_dump_mpath;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    ecrnx_cfg80211_ops.get_mpp = ecrnx_cfg80211_get_mpp;
    ecrnx_cfg80211_ops.dump_mpp = ecrnx_cfg80211_dump_mpp;
#endif
    ecrnx_cfg80211_ops.get_mesh_config = ecrnx_cfg80211_get_mesh_config;
    ecrnx_cfg80211_ops.update_mesh_config = ecrnx_cfg80211_update_mesh_config;
    ecrnx_cfg80211_ops.join_mesh = ecrnx_cfg80211_join_mesh;
    ecrnx_cfg80211_ops.leave_mesh = ecrnx_cfg80211_leave_mesh;

    wiphy->flags |= (WIPHY_FLAG_MESH_AUTH | WIPHY_FLAG_IBSS_RSN);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    wiphy->features |= NL80211_FEATURE_USERSPACE_MPM;
#endif
    wiphy->interface_modes |= BIT(NL80211_IFTYPE_MESH_POINT);

    ecrnx_limits[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
    ecrnx_limits_dfs[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
}

int ecrnx_get_cal_result(struct ecrnx_hw *ecrnx_hw)
{
	int ret;
	wifi_cal_data_t *result = &cal_result;

	ret = ecrnx_send_cal_result_get_req(ecrnx_hw, result);

	return ret;
}

void ecrnx_he_init(void)
{
    ecrnx_he_cap.has_he = true;
    memset(&ecrnx_he_cap.he_cap_elem, 0, sizeof(struct ieee80211_he_cap_elem));

    ecrnx_he_cap.he_mcs_nss_supp.rx_mcs_80 = cpu_to_le16(0xfffa);
    ecrnx_he_cap.he_mcs_nss_supp.tx_mcs_80 = cpu_to_le16(0xfffa);
    ecrnx_he_cap.he_mcs_nss_supp.rx_mcs_160 = cpu_to_le16(0xffff);
    ecrnx_he_cap.he_mcs_nss_supp.tx_mcs_160 = cpu_to_le16(0xffff);
    ecrnx_he_cap.he_mcs_nss_supp.rx_mcs_80p80 = cpu_to_le16(0xffff);
    ecrnx_he_cap.he_mcs_nss_supp.tx_mcs_80p80 = cpu_to_le16(0xffff);
    memset(ecrnx_he_cap.ppe_thres, 0, sizeof(u8)*IEEE80211_HE_PPE_THRES_MAX_LEN);
}

/**
 *
 */
bool register_drv_done = false;
int ecrnx_cfg80211_init(void *ecrnx_plat, void **platform_data)
{
    struct ecrnx_hw *ecrnx_hw;
    int ret = 0;
    struct wiphy *wiphy;
    struct wireless_dev *wdev;
    int i;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    /* create a new wiphy for use with cfg80211 */
    wiphy = wiphy_new(&ecrnx_cfg80211_ops, sizeof(struct ecrnx_hw));

    if (!wiphy) {
        dev_err(ecrnx_platform_get_dev(ecrnx_plat), "Failed to create new wiphy\n");
        ret = -ENOMEM;
        goto err_out;
    }

    ecrnx_hw = wiphy_priv(wiphy);
    ecrnx_hw->wiphy = wiphy;
    ecrnx_hw->plat = ecrnx_plat;
    ecrnx_hw->dev = ecrnx_platform_get_dev(ecrnx_plat);
    ecrnx_hw->mod_params = &ecrnx_mod_params;
    ecrnx_hw->tcp_pacing_shift = 7;
    *platform_data = ecrnx_hw;

    /* set device pointer for wiphy */
    set_wiphy_dev(wiphy, ecrnx_hw->dev);
    /* Create cache to allocate sw_txhdr */
    ecrnx_hw->sw_txhdr_cache = KMEM_CACHE(ecrnx_sw_txhdr, 0);
    if (!ecrnx_hw->sw_txhdr_cache) {
        wiphy_err(wiphy, "Cannot allocate cache for sw TX header\n");
        ret = -ENOMEM;
        goto err_cache;
    }

    if ((ret = ecrnx_parse_configfile(ecrnx_hw, ECRNX_CONFIG_FW_NAME))) {
        wiphy_err(wiphy, "ecrnx_parse_configfile failed\n");
        goto err_config;
    }

    ecrnx_hw->vif_started = 0;
    ecrnx_hw->monitor_vif = ECRNX_INVALID_VIF;

    ecrnx_hw->scan_ie.addr = NULL;

    for (i = 0; i < NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX; i++)
        ecrnx_hw->avail_idx_map |= BIT(i);

    ecrnx_hwq_init(ecrnx_hw);
    ecrnx_txq_prepare(ecrnx_hw);

    ecrnx_mu_group_init(ecrnx_hw);

    /* Initialize RoC element pointer to NULL, indicate that RoC can be started */
    ecrnx_hw->roc = NULL;
    /* Cookie can not be 0 */
    ecrnx_hw->roc_cookie = 1;

    wiphy->mgmt_stypes = ecrnx_default_mgmt_stypes;

    wiphy->bands[NL80211_BAND_2GHZ] = &ecrnx_band_2GHz;
#ifdef CONFIG_ECRNX_5G
    wiphy->bands[NL80211_BAND_5GHZ] = &ecrnx_band_5GHz;
#endif
    wiphy->interface_modes =
        BIT(NL80211_IFTYPE_STATION)     |
        BIT(NL80211_IFTYPE_AP)          |
        BIT(NL80211_IFTYPE_AP_VLAN)     |
        BIT(NL80211_IFTYPE_P2P_CLIENT)  |
        BIT(NL80211_IFTYPE_P2P_GO)      |
        BIT(NL80211_IFTYPE_MONITOR);
    wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
        WIPHY_FLAG_HAS_CHANNEL_SWITCH |
#endif
        WIPHY_FLAG_4ADDR_STATION |
        WIPHY_FLAG_4ADDR_AP;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    wiphy->max_num_csa_counters = BCN_MAX_CSA_CPT;
#endif

    wiphy->max_remain_on_channel_duration = ecrnx_hw->mod_params->roc_dur_max;

#if 0 /* eswin:rm the feature of OBSS_SCAN which can cause the uplink stream shutdown */
    wiphy->features |= NL80211_FEATURE_NEED_OBSS_SCAN |
        NL80211_FEATURE_SK_TX_STATUS |
#else
    wiphy->features |= NL80211_FEATURE_SK_TX_STATUS |
#endif
        NL80211_FEATURE_VIF_TXPOWER |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
        NL80211_FEATURE_ACTIVE_MONITOR |
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
        NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
#endif
        0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
    wiphy->features |= NL80211_FEATURE_SAE;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
    ecrnx_he_init();
#endif

    if (ecrnx_mod_params.tdls)
        /* TDLS support */
        wiphy->features |= NL80211_FEATURE_TDLS_CHANNEL_SWITCH;

    wiphy->iface_combinations   = ecrnx_combinations;
    /* -1 not to include combination with radar detection, will be re-added in
       ecrnx_handle_dynparams if supported */
    wiphy->n_iface_combinations = ARRAY_SIZE(ecrnx_combinations) - 1;
    wiphy->reg_notifier = ecrnx_reg_notifier;

    wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

    wiphy->cipher_suites = cipher_suites;
    wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites) - NB_RESERVED_CIPHER;

    ecrnx_hw->ext_capa[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;
    ecrnx_hw->ext_capa[2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT;
    ecrnx_hw->ext_capa[4] = WLAN_EXT_CAPA5_QOS_MAP_SUPPORT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    ecrnx_hw->ext_capa[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF;
    wiphy->extended_capabilities = ecrnx_hw->ext_capa;
    wiphy->extended_capabilities_mask = ecrnx_hw->ext_capa;
    wiphy->extended_capabilities_len = ARRAY_SIZE(ecrnx_hw->ext_capa);
#endif

#ifndef CONFIG_ECRNX_ESWIN
    tasklet_init(&ecrnx_hw->task, ecrnx_task, (unsigned long)ecrnx_hw);
#endif

    INIT_LIST_HEAD(&ecrnx_hw->vifs);
#ifdef CONFIG_ECRNX_ESWIN
    INIT_LIST_HEAD(&ecrnx_hw->agg_rx_list);
    INIT_LIST_HEAD(&ecrnx_hw->defrag_rx_list);
#endif

    mutex_init(&ecrnx_hw->dbgdump_elem.mutex);
    spin_lock_init(&ecrnx_hw->tx_lock);
    spin_lock_init(&ecrnx_hw->cb_lock);
    spin_lock_init(&ecrnx_hw->rx_lock);
    spin_lock_init(&ecrnx_hw->scan_req_lock);
    spin_lock_init(&ecrnx_hw->connect_req_lock);

    if ((ret = ecrnx_platform_on(ecrnx_hw, NULL)))
        goto err_platon;

	if ((ret = ecrnx_get_cal_result(ecrnx_hw))) {
        wiphy_err(wiphy, "get cal result failed\n");
        goto err_lmac_reqs;
	} else {
		if ((0 == (cal_result.mac_addr[0] & 0x1)) && (cal_result.mac_addr[0] || cal_result.mac_addr[1] 
			|| cal_result.mac_addr[2] || cal_result.mac_addr[3] || cal_result.mac_addr[4] 
			|| cal_result.mac_addr[5])) {
			memcpy(ecrnx_hw->conf_param.mac_addr, cal_result.mac_addr, ETH_ALEN);
		}
	}
	memcpy(wiphy->perm_addr, ecrnx_hw->conf_param.mac_addr, ETH_ALEN);

    /* Reset FW */
    if ((ret = ecrnx_send_reset(ecrnx_hw)))
        goto err_lmac_reqs;
    if ((ret = ecrnx_send_version_req(ecrnx_hw, &ecrnx_hw->version_cfm)))
        goto err_lmac_reqs;
    ecrnx_set_vers(ecrnx_hw);

    if ((ret = ecrnx_handle_dynparams(ecrnx_hw, ecrnx_hw->wiphy)))
        goto err_lmac_reqs;

    ecrnx_enable_mesh(ecrnx_hw);
    ecrnx_radar_detection_init(&ecrnx_hw->radar);

#ifdef CONFIG_ECRNX_P2P
	ecrnx_p2p_listen_init(&ecrnx_hw->p2p_listen);
#endif

    /* Set parameters to firmware */
    ecrnx_send_me_config_req(ecrnx_hw);

    /* Only monitor mode supported when custom channels are enabled */
    if (ecrnx_mod_params.custchan) {
        ecrnx_limits[0].types = BIT(NL80211_IFTYPE_MONITOR);
        ecrnx_limits_dfs[0].types = BIT(NL80211_IFTYPE_MONITOR);
    }

    if ((ret = wiphy_register(wiphy))) {
        wiphy_err(wiphy, "Could not register wiphy device\n");
        goto err_register_wiphy;
    }

    INIT_WORK(&ecrnx_hw->defer_rx.work, ecrnx_rx_deferred);
    skb_queue_head_init(&ecrnx_hw->defer_rx.sk_list);
    /* Update regulatory (if needed) and set channel parameters to firmware
       (must be done after WiPHY registration) */
    ecrnx_fw_log_level_set((u32)ecrnx_hw->conf_param.fw_log_level, (u32)ecrnx_hw->conf_param.fw_log_type);
    ecrnx_custregd(ecrnx_hw, wiphy);
    ecrnx_send_me_chan_config_req(ecrnx_hw);

    /* config gain delta */
    ecrnx_send_set_gain_delta_req(ecrnx_hw);

#ifdef CONFIG_ECRNX_DEBUGFS
    if ((ret = ecrnx_dbgfs_register(ecrnx_hw, "ecrnx"))) {
		ECRNX_DBG(" ecrnx_dbgfs_register error \n");
        wiphy_err(wiphy, "Failed to register debugfs entries");
        goto err_debugfs;
    }
#endif

    rtnl_lock();

    /* Add an initial interface */
    wdev = ecrnx_interface_add(ecrnx_hw, "wlan%d", NET_NAME_UNKNOWN,
               ecrnx_mod_params.custchan ? NL80211_IFTYPE_MONITOR : NL80211_IFTYPE_STATION,
               NULL);
#if defined(CONFIG_ECRNX_P2P)
    wdev = ecrnx_interface_add(ecrnx_hw, "p2p%d", NET_NAME_UNKNOWN, NL80211_IFTYPE_STATION,
               NULL);
#endif
    rtnl_unlock();

    if (!wdev) {
        wiphy_err(wiphy, "Failed to instantiate a network device\n");
        ret = -ENOMEM;
        goto err_add_interface;
    }

    wiphy_info(wiphy, "New interface create %s", wdev->netdev->name);

#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
    ecrnx_debugfs_init(ecrnx_hw);
#endif
    register_drv_done = true;
    return 0;

err_add_interface:
#ifdef CONFIG_ECRNX_DEBUGFS
err_debugfs:
#endif
    wiphy_unregister(ecrnx_hw->wiphy);
err_register_wiphy:
err_lmac_reqs:
    ecrnx_fw_trace_dump(ecrnx_hw);
    ecrnx_platform_off(ecrnx_hw, NULL);
err_platon:
err_config:
    kmem_cache_destroy(ecrnx_hw->sw_txhdr_cache);
err_cache:
    wiphy_free(wiphy);
err_out:
    ECRNX_DBG(" %s cfg80211 init failed %d!!", __func__, ret);
    return ret;
}

/**
 *
 */
void ecrnx_cfg80211_deinit(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    if(!register_drv_done)
    {
        return;
    }
#if 0
    ecrnx_dbgfs_unregister(ecrnx_hw);
#endif
    register_drv_done = false;

    del_timer_sync(&ecrnx_hw->txq_cleanup);
    ecrnx_wdev_unregister(ecrnx_hw);
    if(ecrnx_hw->wiphy)
    {
        ECRNX_DBG("%s wiphy_unregister \n", __func__);
        wiphy_unregister(ecrnx_hw->wiphy);
        wiphy_free(ecrnx_hw->wiphy);
        ecrnx_hw->wiphy = NULL;
    }
    ecrnx_radar_detection_deinit(&ecrnx_hw->radar);
    ecrnx_platform_off(ecrnx_hw, NULL);
    kmem_cache_destroy(ecrnx_hw->sw_txhdr_cache);
}

/**
 *
 */
static int __init ecrnx_mod_init(void)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    ecrnx_print_version();
    return ecrnx_platform_register_drv();
}

/**
 *
 */
static void __exit ecrnx_mod_exit(void)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
    ecrnx_debugfs_exit();
#endif

    ecrnx_platform_unregister_drv();
}

module_init(ecrnx_mod_init);
module_exit(ecrnx_mod_exit);

MODULE_FIRMWARE(ECRNX_CONFIG_FW_NAME);

MODULE_DESCRIPTION(RW_DRV_DESCRIPTION);
MODULE_VERSION(ECRNX_VERS_MOD);
MODULE_AUTHOR(RW_DRV_COPYRIGHT " " RW_DRV_AUTHOR);
MODULE_LICENSE("GPL");
