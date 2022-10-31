/**
 ******************************************************************************
 *
 * @file ecrnx_rx.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>

#include "ecrnx_defs.h"
#include "ecrnx_rx.h"
#include "ecrnx_tx.h"
#include "ecrnx_prof.h"
#include "ecrnx_events.h"
#include "ecrnx_compat.h"
#include "ipc_host.h"
#if defined(CONFIG_ECRNX_ESWIN_SDIO)
#include "ecrnx_sdio.h"
#include "sdio.h"
#elif defined(CONFIG_ECRNX_ESWIN_USB)
#include "ecrnx_usb.h"
#include "usb.h"
#endif
#ifdef CONFIG_ECRNX_WIFO_CAIL
#include "core.h"
#endif

#ifndef IEEE80211_MAX_CHAINS
#define IEEE80211_MAX_CHAINS 4
#endif

struct vendor_radiotap_hdr {
    u8 oui[3];
    u8 subns;
    u16 len;
    u8 data[];
};

/**
 * ecrnx_rx_get_vif - Return pointer to the destination vif
 *
 * @ecrnx_hw: main driver data
 * @vif_idx: vif index present in rx descriptor
 *
 * Select the vif that should receive this frame. Returns NULL if the destination
 * vif is not active or vif is not specified in the descriptor.
 */
static inline
struct ecrnx_vif *ecrnx_rx_get_vif(struct ecrnx_hw *ecrnx_hw, int vif_idx)
{
    struct ecrnx_vif *ecrnx_vif = NULL;

    if (vif_idx < NX_VIRT_DEV_MAX) {
        ecrnx_vif = ecrnx_hw->vif_table[vif_idx];
        //ECRNX_DBG("%s, index : %d, up: %d \n", __func__, vif_idx, ecrnx_vif->up);
        if (!ecrnx_vif || !ecrnx_vif->up)
            return NULL;
    }

    return ecrnx_vif;
}

/**
 * ecrnx_rx_statistic - save some statistics about received frames
 *
 * @ecrnx_hw: main driver data.
 * @hw_rxhdr: Rx Hardware descriptor of the received frame.
 * @sta: STA that sent the frame.
 */
static void ecrnx_rx_statistic(struct ecrnx_hw *ecrnx_hw, struct hw_rxhdr *hw_rxhdr,
                              struct ecrnx_sta *sta)
{
    struct ecrnx_stats *stats = &ecrnx_hw->stats;
#ifdef CONFIG_ECRNX_DEBUGFS
    struct ecrnx_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
    struct rx_vector_1 *rxvect = &hw_rxhdr->hwvect.rx_vect1;
    int mpdu, ampdu, mpdu_prev, rate_idx;

    /* save complete hwvect */

    /* update ampdu rx stats */
    mpdu = hw_rxhdr->hwvect.mpdu_cnt;
    ampdu = hw_rxhdr->hwvect.ampdu_cnt;
    mpdu_prev = stats->ampdus_rx_map[ampdu];

    if (mpdu == 63) {
        if (ampdu == stats->ampdus_rx_last)
            mpdu = mpdu_prev + 1;
        else
            mpdu = 0;
    }
    if (ampdu != stats->ampdus_rx_last) {
        stats->ampdus_rx[mpdu_prev]++;
        stats->ampdus_rx_miss += mpdu;
    } else {
        if (mpdu <= mpdu_prev) {
            stats->ampdus_rx_miss += mpdu;
    } else {
            stats->ampdus_rx_miss += mpdu - mpdu_prev - 1;
        }
    }
    stats->ampdus_rx_map[ampdu] = mpdu;
    stats->ampdus_rx_last = ampdu;

    /* update rx rate statistic */
    if (!rate_stats->size)
        return;

    if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) {
        int mcs;
        int bw = rxvect->ch_bw;
        int sgi;
        int nss;
        switch (rxvect->format_mod) {
            case FORMATMOD_HT_MF:
            case FORMATMOD_HT_GF:
                mcs = rxvect->ht.mcs % 8;
                nss = rxvect->ht.mcs / 8;
                sgi = rxvect->ht.short_gi;
                rate_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 +  bw * 2 + sgi;
                break;
            case FORMATMOD_VHT:
                mcs = rxvect->vht.mcs;
                nss = rxvect->vht.nss;
                sgi = rxvect->vht.short_gi;
                rate_idx = N_CCK + N_OFDM + N_HT + nss * 80 + mcs * 8 + bw * 2 + sgi;
                break;
            case FORMATMOD_HE_SU:
                mcs = rxvect->he.mcs;
                nss = rxvect->he.nss;
                sgi = rxvect->he.gi_type;
                rate_idx = N_CCK + N_OFDM + N_HT + N_VHT + nss * 144 + mcs * 12 + bw * 3 + sgi;
                break;
            default:
                mcs = rxvect->he.mcs;
                nss = rxvect->he.nss;
                sgi = rxvect->he.gi_type;
                rate_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU
                                                + nss * 216 + mcs * 18 + rxvect->he.ru_size * 3 + sgi;
                break;
        }
    } else {
        int idx = legrates_lut[rxvect->leg_rate].idx;
        if (idx < 4) {
            rate_idx = idx * 2 + rxvect->pre_type;
        } else {
            rate_idx = N_CCK + idx - 4;
        }
    }
    if (rate_idx < rate_stats->size) {
        if (!rate_stats->table[rate_idx])
            rate_stats->rate_cnt++;
        rate_stats->table[rate_idx]++;
        rate_stats->cpt++;
    } else {
        wiphy_err(ecrnx_hw->wiphy, "RX: Invalid index conversion => %d/%d\n",
                  rate_idx, rate_stats->size);
    }
#endif
    sta->stats.last_rx = hw_rxhdr->hwvect;
    sta->stats.rx_pkts ++;
    sta->stats.rx_bytes += hw_rxhdr->hwvect.len;
    sta->stats.last_act = stats->last_rx;
}
void ecrnx_rx_defer_skb(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                       struct sk_buff *skb)
{
    struct ecrnx_defer_rx_cb *rx_cb = (struct ecrnx_defer_rx_cb *)skb->cb;
    if (skb_shared(skb))
        return;
    skb_get(skb);
    rx_cb->vif = ecrnx_vif;
    skb_queue_tail(&ecrnx_hw->defer_rx.sk_list, skb);
    schedule_work(&ecrnx_hw->defer_rx.work);
}

/**
 * ecrnx_rx_data_skb - Process one data frame
 *
 * @ecrnx_hw: main driver data
 * @ecrnx_vif: vif that received the buffer
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 * @return: true if buffer has been forwarded to upper layer
 *
 * If buffer is amsdu , it is first split into a list of skb.
 * Then each skb may be:
 * - forwarded to upper layer
 * - resent on wireless interface
 *
 * When vif is a STA interface, every skb is only forwarded to upper layer.
 * When vif is an AP interface, multicast skb are forwarded and resent, whereas
 * skb for other BSS's STA are only resent.
 */
static bool ecrnx_rx_data_skb(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                             struct sk_buff *skb,  struct hw_rxhdr *rxhdr)
{
    struct sk_buff_head list;
    struct sk_buff *rx_skb;
    bool amsdu = rxhdr->flags_is_amsdu;
    bool resend = false, forward = true;
    int skip_after_eth_hdr = 0;

    skb->dev = ecrnx_vif->ndev;

    __skb_queue_head_init(&list);

    if (amsdu) {
        int count;
        ieee80211_amsdu_to_8023s(skb, &list, ecrnx_vif->ndev->dev_addr,
                                 ECRNX_VIF_TYPE(ecrnx_vif), 0, NULL, NULL);

        count = skb_queue_len(&list);
        if (count == 0)
        {
            ECRNX_PRINT("amsdu decode fail!!\n");
            //return false;
            print_hex_dump(KERN_INFO, "amsdu:", DUMP_PREFIX_ADDRESS, 16, 1,
                        skb->data, skb->len>64 ? 64 : skb->len, false);
        }

        if (count > ARRAY_SIZE(ecrnx_hw->stats.amsdus_rx))
            count = ARRAY_SIZE(ecrnx_hw->stats.amsdus_rx);
        if(count > 0)
            ecrnx_hw->stats.amsdus_rx[count - 1]++;
    } else {
        ecrnx_hw->stats.amsdus_rx[0]++;
        __skb_queue_head(&list, skb);
    }

    if (((ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP) ||
         (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_AP_VLAN) ||
         (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
        !(ecrnx_vif->ap.flags & ECRNX_AP_ISOLATE)) {
        const struct ethhdr *eth;
        rx_skb = skb_peek(&list);
        skb_reset_mac_header(rx_skb);
        eth = eth_hdr(rx_skb);

        if (unlikely(is_multicast_ether_addr(eth->h_dest))) {
            /* broadcast pkt need to be forwared to upper layer and resent
               on wireless interface */
            resend = true;
        } else {
            /* unicast pkt for STA inside the BSS, no need to forward to upper
               layer simply resend on wireless interface */
            if (rxhdr->flags_dst_idx != ECRNX_INVALID_STA)
            {
                struct ecrnx_sta *sta = &ecrnx_hw->sta_table[rxhdr->flags_dst_idx];
                if (sta->valid && (sta->vlan_idx == ecrnx_vif->vif_index))
                {
                    forward = false;
                    resend = true;
                }
            }
        }
    } else if (ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_MESH_POINT) {
        const struct ethhdr *eth;
        rx_skb = skb_peek(&list);
        skb_reset_mac_header(rx_skb);
        eth = eth_hdr(rx_skb);

            /* unicast pkt for STA inside the BSS, no need to forward to upper
               layer simply resend on wireless interface */
            if (rxhdr->flags_dst_idx != ECRNX_INVALID_STA)
            {
            resend = true;
            if (is_multicast_ether_addr(eth->h_dest)) {
                uint8_t *mesh_ctrl = (uint8_t *)(eth + 1);
                skip_after_eth_hdr = 8 + 6;
                if ((*mesh_ctrl & MESH_FLAGS_AE) == MESH_FLAGS_AE_A4)
                    skip_after_eth_hdr += ETH_ALEN;
                else if ((*mesh_ctrl & MESH_FLAGS_AE) == MESH_FLAGS_AE_A5_A6)
                    skip_after_eth_hdr += 2 * ETH_ALEN;
            } else {
                forward = false;
            }
        }
    }

    while (!skb_queue_empty(&list)) {
        rx_skb = __skb_dequeue(&list);

        /* resend pkt on wireless interface */
        if (resend) {
            struct sk_buff *skb_copy;
            /* always need to copy buffer when forward=0 to get enough headrom for tsdesc */
            skb_copy = skb_copy_expand(rx_skb, ECRNX_TX_MAX_HEADROOM, 0, GFP_ATOMIC);

            if (skb_copy) {
                int res;
                skb_copy->protocol = htons(ETH_P_802_3);
                skb_reset_network_header(skb_copy);
                skb_reset_mac_header(skb_copy);

                ecrnx_vif->is_resending = true;
                res = dev_queue_xmit(skb_copy);
                ecrnx_vif->is_resending = false;
                /* note: buffer is always consummed by dev_queue_xmit */
                if (res == NET_XMIT_DROP) {
                    ecrnx_vif->net_stats.rx_dropped++;
                    ecrnx_vif->net_stats.tx_dropped++;
                } else if (res != NET_XMIT_SUCCESS) {
                    netdev_err(ecrnx_vif->ndev,
                               "Failed to re-send buffer to driver (res=%d)",
                               res);
                    ecrnx_vif->net_stats.tx_errors++;
                }
            } else {
                netdev_err(ecrnx_vif->ndev, "Failed to copy skb");
            }
        }

        /* forward pkt to upper layer */
        if (forward) {
            rx_skb->protocol = eth_type_trans(rx_skb, ecrnx_vif->ndev);
            memset(rx_skb->cb, 0, sizeof(rx_skb->cb));

            if (unlikely(skip_after_eth_hdr)) {
                memmove(skb_mac_header(rx_skb) + skip_after_eth_hdr,
                        skb_mac_header(rx_skb), sizeof(struct ethhdr));
                __skb_pull(rx_skb, skip_after_eth_hdr);
                skb_reset_mac_header(rx_skb);
                skip_after_eth_hdr = 0;
            }
            REG_SW_SET_PROFILING(ecrnx_hw, SW_PROF_IEEE80211RX);
            ECRNX_DBG("netif_receive_skb enter!! \n");
            /* netif_receive_skb only be called from softirq context, replace it with netif_rx */
            netif_rx(rx_skb);
            REG_SW_CLEAR_PROFILING(ecrnx_hw, SW_PROF_IEEE80211RX);

            /* Update statistics */
            ecrnx_vif->net_stats.rx_packets++;
            ecrnx_vif->net_stats.rx_bytes += rx_skb->len;
        }
    }

    return forward;
}

/**
 * ecrnx_rx_mgmt - Process one 802.11 management frame
 *
 * @ecrnx_hw: main driver data
 * @ecrnx_vif: vif to upload the buffer to
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Forward the management frame to a given interface.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
extern u64_l getBootTime(void);
#endif
static void ecrnx_rx_mgmt(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                         struct sk_buff *skb,  struct hw_rxhdr *hw_rxhdr)
{
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
    struct rx_vector_1 *rxvect = &hw_rxhdr->hwvect.rx_vect1;

    if (ieee80211_is_beacon(mgmt->frame_control)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
        mgmt->u.beacon.timestamp = getBootTime();
#endif
        if ((ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_MESH_POINT) &&
            hw_rxhdr->flags_new_peer) {
            cfg80211_notify_new_peer_candidate(ecrnx_vif->ndev, mgmt->sa,
                                               mgmt->u.beacon.variable,
                                               skb->len - offsetof(struct ieee80211_mgmt,
                                                                   u.beacon.variable),
                                               rxvect->rssi1, GFP_ATOMIC);
        } else {
			//ECRNX_DBG("%s:%d beacon\n", __func__, __LINE__);
            cfg80211_report_obss_beacon(ecrnx_hw->wiphy, skb->data, skb->len,
                                        hw_rxhdr->phy_info.phy_prim20_freq,
                                        rxvect->rssi1);
        }
    } else if ((ieee80211_is_deauth(mgmt->frame_control) ||
                ieee80211_is_disassoc(mgmt->frame_control)) &&
               (mgmt->u.deauth.reason_code == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
                mgmt->u.deauth.reason_code == WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)  // TODO: process unprot mgmt
        cfg80211_rx_unprot_mlme_mgmt(ecrnx_vif->ndev, skb->data, skb->len);
#endif
    } else if ((ECRNX_VIF_TYPE(ecrnx_vif) == NL80211_IFTYPE_STATION) &&
               (ieee80211_is_action(mgmt->frame_control) &&
                (mgmt->u.action.category == 6))) {
        // Wpa_supplicant will ignore the FT action frame if reported via cfg80211_rx_mgmt
        // and cannot call cfg80211_ft_event from atomic context so defer message processing
        ecrnx_rx_defer_skb(ecrnx_hw, ecrnx_vif, skb);
    } else {
        cfg80211_rx_mgmt(&ecrnx_vif->wdev, hw_rxhdr->phy_info.phy_prim20_freq,
                 rxvect->rssi1, skb->data, skb->len, 0);
    }
}

#if 0
static void dump_mgmt_rx(struct hw_rxhdr *hw_rxhdr)
{
	ECRNX_DBG("%s, amsdu:%d, 80211_mpdu:%d, 4addr:%d, vif_idx:%d, sta_idx:%d, dst_idx:%d \n", \
		__func__, \
		hw_rxhdr->flags_is_amsdu, \
		hw_rxhdr->flags_is_80211_mpdu, \
		hw_rxhdr->flags_is_4addr, \
		hw_rxhdr->flags_vif_idx, \
		hw_rxhdr->flags_sta_idx, \
		hw_rxhdr->flags_dst_idx);
	
	ECRNX_DBG("phy_prim20_freq: 0x%x \n", hw_rxhdr->phy_info.phy_prim20_freq);
}
#endif

/**
 * ecrnx_rx_mgmt_any - Process one 802.11 management frame
 *
 * @ecrnx_hw: main driver data
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Process the management frame and free the corresponding skb.
 * If vif is not specified in the rx descriptor, the the frame is uploaded
 * on all active vifs.
 */
static void ecrnx_rx_mgmt_any(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb,
                             struct hw_rxhdr *hw_rxhdr)
{
    struct ecrnx_vif *ecrnx_vif;
    int vif_idx = hw_rxhdr->flags_vif_idx;

	//ECRNX_DBG("%s:%d \n", __func__, __LINE__);
    trace_mgmt_rx(hw_rxhdr->phy_info.phy_prim20_freq, vif_idx,
                  hw_rxhdr->flags_sta_idx, (struct ieee80211_mgmt *)skb->data);

    if (vif_idx == ECRNX_INVALID_VIF) {
		//ECRNX_DBG("search the list \n");
        list_for_each_entry(ecrnx_vif, &ecrnx_hw->vifs, list) {
			//ECRNX_DBG("find a item, vif_up:%d \n", ecrnx_vif->up);
            if (! ecrnx_vif->up)
                continue;
			//ECRNX_DBG("wpa up \n");
            ecrnx_rx_mgmt(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr);
        }
    } else {
        ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, vif_idx);
        if (ecrnx_vif)
            ecrnx_rx_mgmt(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr);
    }

    dev_kfree_skb(skb);
}

/**
 * ecrnx_rx_rtap_hdrlen - Return radiotap header length
 *
 * @rxvect: Rx vector used to fill the radiotap header
 * @has_vend_rtap: boolean indicating if vendor specific data is present
 *
 * Compute the length of the radiotap header based on @rxvect and vendor
 * specific data (if any).
 */
static u8 ecrnx_rx_rtap_hdrlen(struct rx_vector_1 *rxvect,
                              bool has_vend_rtap)
{
    u8 rtap_len;

    /* Compute radiotap header length */
    rtap_len = sizeof(struct ieee80211_radiotap_header) + 8;

    // Check for multiple antennas
    if (hweight32(rxvect->antenna_set) > 1)
        // antenna and antenna signal fields
        rtap_len += 4 * hweight8(rxvect->antenna_set);

    // TSFT
    if (!has_vend_rtap) {
        rtap_len = ALIGN(rtap_len, 8);
        rtap_len += 8;
    }

    // IEEE80211_HW_SIGNAL_DBM
    rtap_len++;

    // Check if single antenna
    if (hweight32(rxvect->antenna_set) == 1)
        rtap_len++; //Single antenna

    // padding for RX FLAGS
    rtap_len = ALIGN(rtap_len, 2);

    // Check for HT frames
    if ((rxvect->format_mod == FORMATMOD_HT_MF) ||
        (rxvect->format_mod == FORMATMOD_HT_GF))
        rtap_len += 3;

    // Check for AMPDU
    if (!(has_vend_rtap) && ((rxvect->format_mod >= FORMATMOD_VHT) ||
                             ((rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) &&
                                                     (rxvect->ht.aggregation)))) {
        rtap_len = ALIGN(rtap_len, 4);
        rtap_len += 8;
    }

    // Check for VHT frames
    if (rxvect->format_mod == FORMATMOD_VHT) {
        rtap_len = ALIGN(rtap_len, 2);
        rtap_len += 12;
    }

    // Check for HE frames
    if (rxvect->format_mod == FORMATMOD_HE_SU) {
        rtap_len = ALIGN(rtap_len, 2);
        rtap_len += sizeof(struct ieee80211_radiotap_he);
    }

    // Check for multiple antennas
    if (hweight32(rxvect->antenna_set) > 1) {
        // antenna and antenna signal fields
        rtap_len += 2 * hweight8(rxvect->antenna_set);
    }

    // Check for vendor specific data
    if (has_vend_rtap) {
        /* vendor presence bitmap */
        rtap_len += 4;
        /* alignment for fixed 6-byte vendor data header */
        rtap_len = ALIGN(rtap_len, 2);
    }

    return rtap_len;
}

/**
 * ecrnx_rx_add_rtap_hdr - Add radiotap header to sk_buff
 *
 * @ecrnx_hw: main driver data
 * @skb: skb received (will include the radiotap header)
 * @rxvect: Rx vector
 * @phy_info: Information regarding the phy
 * @hwvect: HW Info (NULL if vendor specific data is available)
 * @rtap_len: Length of the radiotap header
 * @vend_rtap_len: radiotap vendor length (0 if not present)
 * @vend_it_present: radiotap vendor present
 *
 * Builds a radiotap header and add it to @skb.
 */
static void ecrnx_rx_add_rtap_hdr(struct ecrnx_hw* ecrnx_hw,
                                 struct sk_buff *skb,
                                 struct rx_vector_1 *rxvect,
                                 struct phy_channel_info_desc *phy_info,
                                 struct hw_vect *hwvect,
                                 int rtap_len,
                                 u8 vend_rtap_len,
                                 u32 vend_it_present)
{
    struct ieee80211_radiotap_header *rtap;
    u8 *pos, rate_idx;
    __le32 *it_present;
    u32 it_present_val = 0;
    bool fec_coding = false;
    bool short_gi = false;
    bool stbc = false;
    bool aggregation = false;

    rtap = (struct ieee80211_radiotap_header *)skb_push(skb, rtap_len);
    memset((u8*) rtap, 0, rtap_len);

    rtap->it_version = 0;
    rtap->it_pad = 0;
    rtap->it_len = cpu_to_le16(rtap_len + vend_rtap_len);

    it_present = (__le32*)&rtap->it_present;

    // Check for multiple antennas
    if (hweight32(rxvect->antenna_set) > 1) {
        int chain;
        unsigned long chains = rxvect->antenna_set;

        for_each_set_bit(chain, &chains, IEEE80211_MAX_CHAINS) {
            it_present_val |=
                BIT(IEEE80211_RADIOTAP_EXT) |
                BIT(IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE);
            put_unaligned_le32(it_present_val, it_present);
            it_present++;
            it_present_val = BIT(IEEE80211_RADIOTAP_ANTENNA) |
                             BIT(IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
        }
    }

    // Check if vendor specific data is present
    if (vend_rtap_len) {
        it_present_val |= BIT(IEEE80211_RADIOTAP_VENDOR_NAMESPACE) |
                          BIT(IEEE80211_RADIOTAP_EXT);
        put_unaligned_le32(it_present_val, it_present);
        it_present++;
        it_present_val = vend_it_present;
    }

    put_unaligned_le32(it_present_val, it_present);
    pos = (void *)(it_present + 1);

    // IEEE80211_RADIOTAP_TSFT
    if (hwvect) {
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_TSFT);
        // padding
        while ((pos - (u8 *)rtap) & 7)
            *pos++ = 0;
        put_unaligned_le64((((u64)le32_to_cpu(hwvect->tsf_hi) << 32) +
                            (u64)le32_to_cpu(hwvect->tsf_lo)), pos);
        pos += 8;
    }

    // IEEE80211_RADIOTAP_FLAGS
    rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_FLAGS);
    if (hwvect && (!hwvect->status.frm_successful_rx))
        *pos |= IEEE80211_RADIOTAP_F_BADFCS;
    if (!rxvect->pre_type
            && (rxvect->format_mod <= FORMATMOD_NON_HT_DUP_OFDM))
        *pos |= IEEE80211_RADIOTAP_F_SHORTPRE;
    pos++;

    // IEEE80211_RADIOTAP_RATE
    // check for HT, VHT or HE frames
    if (rxvect->format_mod >= FORMATMOD_HE_SU) {
        rate_idx = rxvect->he.mcs;
        fec_coding = rxvect->he.fec;
        stbc = rxvect->he.stbc;
        aggregation = true;
        *pos = 0;
    } else if (rxvect->format_mod == FORMATMOD_VHT) {
        rate_idx = rxvect->vht.mcs;
        fec_coding = rxvect->vht.fec;
        short_gi = rxvect->vht.short_gi;
        stbc = rxvect->vht.stbc;
        aggregation = true;
        *pos = 0;
    } else if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) {
        rate_idx = rxvect->ht.mcs;
        fec_coding = rxvect->ht.fec;
        short_gi = rxvect->ht.short_gi;
        stbc = rxvect->ht.stbc;
        aggregation = rxvect->ht.aggregation;
        *pos = 0;
    } else {
        struct ieee80211_supported_band* band =
                ecrnx_hw->wiphy->bands[phy_info->phy_band];
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RATE);
        BUG_ON((rate_idx = legrates_lut[rxvect->leg_rate].idx) == -1);
#ifdef CONFIG_ECRNX_5G
        if (phy_info->phy_band == NL80211_BAND_5GHZ)
            rate_idx -= 4;  /* ecrnx_ratetable_5ghz[0].hw_value == 4 */
#endif
        *pos = DIV_ROUND_UP(band->bitrates[rate_idx].bitrate, 5);
    }
    pos++;

    // IEEE80211_RADIOTAP_CHANNEL
    rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_CHANNEL);
    put_unaligned_le16(phy_info->phy_prim20_freq, pos);
    pos += 2;

#ifdef CONFIG_ECRNX_5G
    if (phy_info->phy_band == NL80211_BAND_5GHZ)
        put_unaligned_le16(IEEE80211_CHAN_OFDM | IEEE80211_CHAN_5GHZ, pos);
    else if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM)
#else
	if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM)
#endif
        put_unaligned_le16(IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ, pos);
    else
        put_unaligned_le16(IEEE80211_CHAN_CCK | IEEE80211_CHAN_2GHZ, pos);
    pos += 2;

    if (hweight32(rxvect->antenna_set) == 1) {
        // IEEE80211_RADIOTAP_DBM_ANTSIGNAL
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
        *pos++ = rxvect->rssi1;

        // IEEE80211_RADIOTAP_ANTENNA
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_ANTENNA);
        *pos++ = rxvect->antenna_set;
    }

    // IEEE80211_RADIOTAP_LOCK_QUALITY is missing
    // IEEE80211_RADIOTAP_DB_ANTNOISE is missing

    // IEEE80211_RADIOTAP_RX_FLAGS
    rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RX_FLAGS);
    // 2 byte alignment
    if ((pos - (u8 *)rtap) & 1)
        *pos++ = 0;
    put_unaligned_le16(0, pos);
    //Right now, we only support fcs error (no RX_FLAG_FAILED_PLCP_CRC)
    pos += 2;

    // Check if HT
    if ((rxvect->format_mod == FORMATMOD_HT_MF) ||
        (rxvect->format_mod == FORMATMOD_HT_GF)) {
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_MCS);
        *pos++ = (IEEE80211_RADIOTAP_MCS_HAVE_MCS |
                 IEEE80211_RADIOTAP_MCS_HAVE_GI |
                  IEEE80211_RADIOTAP_MCS_HAVE_BW |
                  IEEE80211_RADIOTAP_MCS_HAVE_FMT |
                  IEEE80211_RADIOTAP_MCS_HAVE_FEC |
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 17, 0)
                  IEEE80211_RADIOTAP_MCS_HAVE_STBC |
#endif
                  0);
        pos++;
        *pos = 0;
        if (short_gi)
            *pos |= IEEE80211_RADIOTAP_MCS_SGI;
        if (rxvect->ch_bw  == PHY_CHNL_BW_40)
            *pos |= IEEE80211_RADIOTAP_MCS_BW_40;
        if (rxvect->format_mod == FORMATMOD_HT_GF)
            *pos |= IEEE80211_RADIOTAP_MCS_FMT_GF;
        if (fec_coding)
            *pos |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
        *pos++ |= stbc << 5;
#else
        *pos++ |= stbc << IEEE80211_RADIOTAP_MCS_STBC_SHIFT;
#endif
        *pos++ = rate_idx;
    }

    // check for HT or VHT frames
    if (aggregation && hwvect) {
        // 4 byte alignment
        while ((pos - (u8 *)rtap) & 3)
            pos++;
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_AMPDU_STATUS);
        put_unaligned_le32(hwvect->ampdu_cnt, pos);
        pos += 4;
        put_unaligned_le32(0, pos);
        pos += 4;
    }

    // Check for VHT frames
    if (rxvect->format_mod == FORMATMOD_VHT) {
        u16 vht_details = IEEE80211_RADIOTAP_VHT_KNOWN_GI |
                          IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
        u8 vht_nss = rxvect->vht.nss + 1;

        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_VHT);

        if ((rxvect->ch_bw == PHY_CHNL_BW_160)
                && phy_info->phy_center2_freq)
            vht_details &= ~IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
        put_unaligned_le16(vht_details, pos);
        pos += 2;

        // flags
        if (short_gi)
            *pos |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
        if (stbc)
            *pos |= IEEE80211_RADIOTAP_VHT_FLAG_STBC;
        pos++;

        // bandwidth
        if (rxvect->ch_bw == PHY_CHNL_BW_40)
            *pos++ = 1;
        if (rxvect->ch_bw == PHY_CHNL_BW_80)
            *pos++ = 4;
        else if ((rxvect->ch_bw == PHY_CHNL_BW_160)
                && phy_info->phy_center2_freq)
            *pos++ = 0; //80P80
        else if  (rxvect->ch_bw == PHY_CHNL_BW_160)
            *pos++ = 11;
        else // 20 MHz
            *pos++ = 0;

        // MCS/NSS
        *pos++ = (rate_idx << 4) | vht_nss;
        *pos++ = 0;
        *pos++ = 0;
        *pos++ = 0;
        if (fec_coding){
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
            *pos |= 0x01;
#else
            *pos |= IEEE80211_RADIOTAP_CODING_LDPC_USER0;
#endif
        }
        pos++;
        // group ID
        pos++;
        // partial_aid
        pos += 2;
    }

    // Check for HE frames
    if (rxvect->format_mod >= FORMATMOD_HE_SU) {
        struct ieee80211_radiotap_he he;
        memset(&he, 0, sizeof(struct ieee80211_radiotap_he));
        #define HE_PREP(f, val) cpu_to_le16(FIELD_PREP(IEEE80211_RADIOTAP_HE_##f, val))
        #define D1_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_##f##_KNOWN)
        #define D2_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_##f##_KNOWN)

        he.data1 = D1_KNOWN(BSS_COLOR) | D1_KNOWN(BEAM_CHANGE) |
                   D1_KNOWN(UL_DL) | D1_KNOWN(STBC) |
                   D1_KNOWN(DOPPLER) | D1_KNOWN(DATA_DCM);
        he.data2 = D2_KNOWN(GI) | D2_KNOWN(TXBF) | D2_KNOWN(TXOP);

        he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
        he.data3 |= HE_PREP(DATA3_BEAM_CHANGE, rxvect->he.beam_change);
        he.data3 |= HE_PREP(DATA3_UL_DL, rxvect->he.uplink_flag);
        he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
        he.data3 |= HE_PREP(DATA3_DATA_DCM, rxvect->he.dcm);
        he.data5 |= HE_PREP(DATA5_GI, rxvect->he.gi_type);
        he.data5 |= HE_PREP(DATA5_TXBF, rxvect->he.beamformed);
        he.data5 |= HE_PREP(DATA5_LTF_SIZE, rxvect->he.he_ltf_type + 1);
        he.data6 |= HE_PREP(DATA6_DOPPLER, rxvect->he.doppler);
        he.data6 |= HE_PREP(DATA6_TXOP, rxvect->he.txop_duration);
        if (rxvect->format_mod != FORMATMOD_HE_TB) {
            he.data1 |= (D1_KNOWN(DATA_MCS) | D1_KNOWN(CODING) |
                         D1_KNOWN(SPTL_REUSE) | D1_KNOWN(BW_RU_ALLOC));
        if (stbc) {
            he.data6 |= HE_PREP(DATA6_NSTS, 2);
            he.data3 |= HE_PREP(DATA3_STBC, 1);
        } else {
            he.data6 |= HE_PREP(DATA6_NSTS, rxvect->he.nss);
        }

        he.data3 |= HE_PREP(DATA3_DATA_MCS, rxvect->he.mcs);
        he.data3 |= HE_PREP(DATA3_CODING, rxvect->he.fec);

            he.data4 = HE_PREP(DATA4_SU_MU_SPTL_REUSE, rxvect->he.spatial_reuse);

            if (rxvect->format_mod == FORMATMOD_HE_MU) {
                he.data1 |= IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MU;
                he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
                                    rxvect->he.ru_size +
                                    IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_26T);
            } else {
                if (rxvect->format_mod == FORMATMOD_HE_SU)
                    he.data1 |= IEEE80211_RADIOTAP_HE_DATA1_FORMAT_SU;
                else
                    he.data1 |= IEEE80211_RADIOTAP_HE_DATA1_FORMAT_EXT_SU;

        switch (rxvect->ch_bw) {
        case PHY_CHNL_BW_20:
            he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
                        IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_20MHZ);
            break;
        case PHY_CHNL_BW_40:
            he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
                        IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_40MHZ);
            break;
        case PHY_CHNL_BW_80:
            he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
                        IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_80MHZ);
            break;
        case PHY_CHNL_BW_160:
            he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
                        IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_160MHZ);
            break;
        default:
            WARN_ONCE(1, "Invalid SU BW %d\n", rxvect->ch_bw);
        }
            }
        } else {
            he.data1 |= IEEE80211_RADIOTAP_HE_DATA1_FORMAT_TRIG;
        }

        /* ensure 2 byte alignment */
        while ((pos - (u8 *)rtap) & 1)
            pos++;
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_HE);
        memcpy(pos, &he, sizeof(he));
        pos += sizeof(he);
    }

    // Rx Chains
    if (hweight32(rxvect->antenna_set) > 1) {
        int chain;
        unsigned long chains = rxvect->antenna_set;
        u8 rssis[4] = {rxvect->rssi1, rxvect->rssi1, rxvect->rssi1, rxvect->rssi1};

        for_each_set_bit(chain, &chains, IEEE80211_MAX_CHAINS) {
            *pos++ = rssis[chain];
            *pos++ = chain;
        }
    }
}

/**
 * ecrnx_rx_monitor - Build radiotap header for skb an send it to netdev
 *
 * @ecrnx_hw: main driver data
 * @ecrnx_vif: vif that received the buffer
 * @skb: sk_buff received
 * @hw_rxhdr_ptr: Pointer to HW RX header
 * @rtap_len: Radiotap Header length
 *
 * Add radiotap header to the receved skb and send it to netdev
 */
static int ecrnx_rx_monitor(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                           struct sk_buff *skb,  struct hw_rxhdr *hw_rxhdr_ptr,
                           u8 rtap_len)
{
    skb->dev = ecrnx_vif->ndev;

    ECRNX_DBG("%s enter!!", __func__);
    if (ecrnx_vif->wdev.iftype != NL80211_IFTYPE_MONITOR) {
        netdev_err(ecrnx_vif->ndev, "not a monitor vif\n");
        return -1;
    }

    /* Add RadioTap Header */
    ecrnx_rx_add_rtap_hdr(ecrnx_hw, skb, &hw_rxhdr_ptr->hwvect.rx_vect1,
                         &hw_rxhdr_ptr->phy_info, &hw_rxhdr_ptr->hwvect,
                         rtap_len, 0, 0);

    skb_reset_mac_header(skb);
    skb->ip_summed = CHECKSUM_UNNECESSARY;
    skb->pkt_type = PACKET_OTHERHOST;
    skb->protocol = htons(ETH_P_802_2);

    netif_receive_skb(skb);
    ECRNX_DBG("%s exit!!", __func__);
    return 0;
}

/**
* ecrnx_rx_deferred - Work function to defer processing of buffer that cannot be
* done in ecrnx_rxdataind (that is called in atomic context)
*
* @ws: work field within struct ecrnx_defer_rx
*/
void ecrnx_rx_deferred(struct work_struct *ws)
{
   struct ecrnx_defer_rx *rx = container_of(ws, struct ecrnx_defer_rx, work);
   struct sk_buff *skb;

   while ((skb = skb_dequeue(&rx->sk_list)) != NULL) {
       // Currently only management frame can be deferred
       struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
       struct ecrnx_defer_rx_cb *rx_cb = (struct ecrnx_defer_rx_cb *)skb->cb;

       if (ieee80211_is_action(mgmt->frame_control) &&
           (mgmt->u.action.category == 6)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
           struct cfg80211_ft_event_params ft_event;
           struct ecrnx_vif *vif = rx_cb->vif;
           u8 *action_frame = (u8 *)&mgmt->u.action;
           u8 action_code = action_frame[1];
           u16 status_code = *((u16 *)&action_frame[2 + 2 * ETH_ALEN]);

           if ((action_code == 2) && (status_code == 0)) {
               ft_event.target_ap = action_frame + 2 + ETH_ALEN;
               ft_event.ies = action_frame + 2 + 2 * ETH_ALEN + 2;
               ft_event.ies_len = skb->len - (ft_event.ies - (u8 *)mgmt);
               ft_event.ric_ies = NULL;
               ft_event.ric_ies_len = 0;
               cfg80211_ft_event(rx_cb->vif->ndev, &ft_event);
               vif->sta.flags |= ECRNX_STA_FT_OVER_DS;
               memcpy(vif->sta.ft_target_ap, ft_event.target_ap, ETH_ALEN);

           }
#endif
       } else if (ieee80211_is_auth(mgmt->frame_control)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
           struct cfg80211_ft_event_params ft_event;
           struct ecrnx_vif *vif = rx_cb->vif;
           ft_event.target_ap = vif->sta.ft_target_ap;
           ft_event.ies = mgmt->u.auth.variable;
           ft_event.ies_len = (skb->len -
                               offsetof(struct ieee80211_mgmt, u.auth.variable));
           ft_event.ric_ies = NULL;
           ft_event.ric_ies_len = 0;
           cfg80211_ft_event(rx_cb->vif->ndev, &ft_event);
           vif->sta.flags |= ECRNX_STA_FT_OVER_AIR;
#endif
       } else {
           netdev_warn(rx_cb->vif->ndev, "Unexpected deferred frame fctl=0x%04x",
                       mgmt->frame_control);
       }

       dev_kfree_skb(skb);
   }
}

/**
 * ecrnx_unsup_rx_vec_ind() - IRQ handler callback for %IPC_IRQ_E2A_UNSUP_RX_VEC
 *
 * LMAC has triggered an IT saying that a rx vector of an unsupported frame has been
 * captured and sent to upper layer. Then we need to fill the rx status, create a vendor
 * specific header and fill it with the HT packet length. Finally, we need to specify at
 * least 2 bytes of data and send the sk_buff to mac80211.
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to IPC elem from e2aradars_pool
 */
u8 ecrnx_unsup_rx_vec_ind(void *pthis, void *hostid) {
    struct ecrnx_hw *ecrnx_hw = pthis;
    struct ecrnx_ipc_skb_elem *elem = hostid;
    struct rx_vector_desc *rx_desc;
    struct sk_buff *skb;
    struct rx_vector_1 *rx_vect1;
    struct phy_channel_info_desc *phy_info;
    struct vendor_radiotap_hdr *rtap;
    u16 ht_length;
    struct ecrnx_vif *ecrnx_vif;
    struct rx_vector_desc rx_vect_desc;
    u8 rtap_len, vend_rtap_len = sizeof(*rtap);

    dma_sync_single_for_cpu(ecrnx_hw->dev, elem->dma_addr,
                            sizeof(struct rx_vector_desc), DMA_FROM_DEVICE);

    skb = elem->skb;
    if (((struct rx_vector_desc *) (skb->data))->pattern == 0) {
        /*sync is needed even if the driver did not modify the memory*/
        dma_sync_single_for_device(ecrnx_hw->dev, elem->dma_addr,
                                     sizeof(struct rx_vector_desc), DMA_FROM_DEVICE);
        return -1;
    }

    if (ecrnx_hw->monitor_vif == ECRNX_INVALID_VIF) {
        /* Unmap will synchronize buffer for CPU */
#ifndef CONFIG_ECRNX_ESWIN
       dma_unmap_single(ecrnx_hw->dev, elem->dma_addr, ecrnx_hw->ipc_env->unsuprxvec_bufsz,
                         DMA_FROM_DEVICE);
#endif
        elem->skb = NULL;

        /* Free skb */
        dev_kfree_skb(skb);

#ifndef CONFIG_ECRNX_ESWIN
        /* Allocate and push a new buffer to fw to replace this one */
        if (ecrnx_ipc_unsup_rx_vec_elem_allocs(ecrnx_hw, elem))
            dev_err(ecrnx_hw->dev, "Failed to alloc new unsupported rx vector buf\n");
        return -1;
#endif
    }

    ecrnx_vif = ecrnx_hw->vif_table[ecrnx_hw->monitor_vif];
    skb->dev = ecrnx_vif->ndev;
    memcpy(&rx_vect_desc, skb->data, sizeof(rx_vect_desc));
    rx_desc = &rx_vect_desc;

    rx_vect1 = (struct rx_vector_1 *) (rx_desc->rx_vect1);
    ecrnx_rx_vector_convert(ecrnx_hw->machw_type, rx_vect1, NULL);
    phy_info = (struct phy_channel_info_desc *) (&rx_desc->phy_info);
    if (rx_vect1->format_mod >= FORMATMOD_VHT)
        ht_length = 0;
    else
        ht_length = (u16) le32_to_cpu(rx_vect1->ht.length);

    // Reserve space for radiotap
    skb_reserve(skb, RADIOTAP_HDR_MAX_LEN);

    /* Fill vendor specific header with fake values */
    rtap = (struct vendor_radiotap_hdr *) skb->data;
    rtap->oui[0] = 0x00;
    rtap->oui[1] = 0x25;
    rtap->oui[2] = 0x3A;
    rtap->subns  = 0;
    rtap->len = sizeof(ht_length);
    put_unaligned_le16(ht_length, rtap->data);
    vend_rtap_len += rtap->len;
    skb_put(skb, vend_rtap_len);

    /* Copy fake data */
    put_unaligned_le16(0, skb->data + vend_rtap_len);
    skb_put(skb, UNSUP_RX_VEC_DATA_LEN);

    /* Get RadioTap Header length */
    rtap_len = ecrnx_rx_rtap_hdrlen(rx_vect1, true);

    /* Check headroom space */
    if (skb_headroom(skb) < rtap_len) {
        netdev_err(ecrnx_vif->ndev, "not enough headroom %d need %d\n", skb_headroom(skb), rtap_len);
        return -1;
    }

    /* Add RadioTap Header */
    ecrnx_rx_add_rtap_hdr(ecrnx_hw, skb, rx_vect1, phy_info, NULL,
                         rtap_len, vend_rtap_len, BIT(0));

    skb_reset_mac_header(skb);
    skb->ip_summed = CHECKSUM_UNNECESSARY;
    skb->pkt_type = PACKET_OTHERHOST;
    skb->protocol = htons(ETH_P_802_2);

    /* Unmap will synchronize buffer for CPU */
#ifndef CONFIG_ECRNX_ESWIN
    dma_unmap_single(ecrnx_hw->dev, elem->dma_addr, ecrnx_hw->ipc_env->unsuprxvec_bufsz,
                     DMA_FROM_DEVICE);
#endif
    elem->skb = NULL;

    netif_receive_skb(skb);

#ifndef CONFIG_ECRNX_ESWIN
    /* Allocate and push a new buffer to fw to replace this one */
    if (ecrnx_ipc_unsup_rx_vec_elem_allocs(ecrnx_hw, elem))
        netdev_err(ecrnx_vif->ndev, "Failed to alloc new unsupported rx vector buf\n");
#endif
    return 0;
}

/**
 * ecrnx_rxdataind - Process rx buffer
 *
 * @pthis: Pointer to the object attached to the IPC structure
 *         (points to struct ecrnx_hw is this case)
 * @hostid: Address of the RX descriptor
 *
 * This function is called for each buffer received by the fw
 *
 */
#ifndef CONFIG_ECRNX_ESWIN
u8 ecrnx_rxdataind(void *pthis, void *hostid)
{
    struct ecrnx_hw *ecrnx_hw = pthis;
    struct ecrnx_ipc_elem *elem = hostid;
    struct hw_rxhdr *hw_rxhdr;
    struct rxdesc_tag *rxdesc;
    struct ecrnx_vif *ecrnx_vif;
    struct sk_buff *skb = NULL;
    int rx_buff_idx;
    int msdu_offset = sizeof(struct hw_rxhdr) + 2;

    int peek_len    = msdu_offset + sizeof(struct ethhdr);
    u16_l status;

    REG_SW_SET_PROFILING(ecrnx_hw, SW_PROF_ECRNXDATAIND);

    if(!pthis || !hostid)
    {
        return -1;
    }

    /* Get the ownership of the descriptor */
    dma_sync_single_for_cpu(ecrnx_hw->dev, elem->dma_addr,
                            sizeof(struct rxdesc_tag), DMA_FROM_DEVICE);

    rxdesc = elem->addr;
    status = rxdesc->status;

    /* check that frame is completely uploaded */
    if (!status){
        /* Get the ownership of the descriptor */
        dma_sync_single_for_device(ecrnx_hw->dev, elem->dma_addr,
                                   sizeof(struct rxdesc_tag), DMA_FROM_DEVICE);
        return -1;
    }

    /* Get the buffer linked with the received descriptor */
    rx_buff_idx = ECRNX_RXBUFF_HOSTID_TO_IDX(rxdesc->host_id);
    if (ECRNX_RXBUFF_VALID_IDX(rx_buff_idx))
        skb = ecrnx_hw->rxbuf_elems.skb[rx_buff_idx];

    if (!skb){
        dev_err(ecrnx_hw->dev, "RX Buff invalid idx [%d]\n", rx_buff_idx);
        return -1;
    }

    /* Check the pattern */
    if (ECRNX_RXBUFF_PATTERN_GET(skb) != ecrnx_rxbuff_pattern) {
        dev_err(ecrnx_hw->dev, "RX Buff Pattern not correct\n");
        BUG();
    }

    /* Check if we need to delete the buffer */
    if (status & RX_STAT_DELETE) {
        /* Remove the SK buffer from the rxbuf_elems table */
        ecrnx_ipc_rxbuf_elem_pull(ecrnx_hw, skb);
        /* Free the buffer */
        dev_kfree_skb(skb);
        goto end;
    }

    /* Check if we need to forward the buffer coming from a monitor interface */
    if (status & RX_STAT_MONITOR) {
        struct sk_buff *skb_monitor;
        struct hw_rxhdr hw_rxhdr_copy;
        u8 rtap_len;
        u16 frm_len;

        //Check if monitor interface exists and is open
        ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, ecrnx_hw->monitor_vif);
        if (!ecrnx_vif) {
            dev_err(ecrnx_hw->dev, "Received monitor frame but there is no monitor interface open\n");
            goto check_len_update;
        }

        hw_rxhdr = (struct hw_rxhdr *)skb->data;
        ecrnx_rx_vector_convert(ecrnx_hw->machw_type,
                               &hw_rxhdr->hwvect.rx_vect1,
                               &hw_rxhdr->hwvect.rx_vect2);
        rtap_len = ecrnx_rx_rtap_hdrlen(&hw_rxhdr->hwvect.rx_vect1, false);

        // Move skb->data pointer to MAC Header or Ethernet header
        skb->data += msdu_offset;

        //Save frame length
        frm_len = le32_to_cpu(hw_rxhdr->hwvect.len);

        // Reserve space for frame
        skb->len = frm_len;

        if (status == RX_STAT_MONITOR) {
            /* Remove the SK buffer from the rxbuf_elems table. It will also
               unmap the buffer and then sync the buffer for the cpu */
            ecrnx_ipc_rxbuf_elem_pull(ecrnx_hw, skb);

            //Check if there is enough space to add the radiotap header
            if (skb_headroom(skb) > rtap_len) {

                skb_monitor = skb;

                //Duplicate the HW Rx Header to override with the radiotap header
                memcpy(&hw_rxhdr_copy, hw_rxhdr, sizeof(hw_rxhdr_copy));

                hw_rxhdr = &hw_rxhdr_copy;
            } else {
                //Duplicate the skb and extend the headroom
                skb_monitor = skb_copy_expand(skb, rtap_len, 0, GFP_ATOMIC);

                //Reset original skb->data pointer
                skb->data = (void*) hw_rxhdr;
            }
        }
        else
        {
        #ifdef CONFIG_ECRNX_MON_DATA
            // Check if MSDU
            if (!hw_rxhdr->flags_is_80211_mpdu) {
                // MSDU
                //Extract MAC header
                u16 machdr_len = hw_rxhdr->mac_hdr_backup.buf_len;
                u8* machdr_ptr = hw_rxhdr->mac_hdr_backup.buffer;

                //Pull Ethernet header from skb
                skb_pull(skb, sizeof(struct ethhdr));

                // Copy skb and extend for adding the radiotap header and the MAC header
                skb_monitor = skb_copy_expand(skb,
                                              rtap_len + machdr_len,
                                              0, GFP_ATOMIC);

                //Reserve space for the MAC Header
                skb_push(skb_monitor, machdr_len);

                //Copy MAC Header
                memcpy(skb_monitor->data, machdr_ptr, machdr_len);

                //Update frame length
                frm_len += machdr_len - sizeof(struct ethhdr);
            } else {
                // MPDU
                skb_monitor = skb_copy_expand(skb, rtap_len, 0, GFP_ATOMIC);
            }

            //Reset original skb->data pointer
            skb->data = (void*) hw_rxhdr;
        #else
            //Reset original skb->data pointer
            skb->data = (void*) hw_rxhdr;

            wiphy_err(ecrnx_hw->wiphy, "RX status %d is invalid when MON_DATA is disabled\n", status);
            goto check_len_update;
        #endif
        }

        skb_reset_tail_pointer(skb);
        skb->len = 0;
        skb_reset_tail_pointer(skb_monitor);
        skb_monitor->len = 0;

        skb_put(skb_monitor, frm_len);
        if (ecrnx_rx_monitor(ecrnx_hw, ecrnx_vif, skb_monitor, hw_rxhdr, rtap_len))
            dev_kfree_skb(skb_monitor);

        if (status == RX_STAT_MONITOR) {
            status |= RX_STAT_ALLOC;
            if (skb_monitor != skb) {
                dev_kfree_skb(skb);
            }
        }
    }

check_len_update:
    /* Check if we need to update the length */
    if (status & RX_STAT_LEN_UPDATE) {
        dma_addr_t dma_addr = ECRNX_RXBUFF_DMA_ADDR_GET(skb);
        dma_sync_single_for_cpu(ecrnx_hw->dev, dma_addr,
                                peek_len, DMA_FROM_DEVICE);

        hw_rxhdr = (struct hw_rxhdr *)skb->data;

        hw_rxhdr->hwvect.len = rxdesc->frame_len;

        if (status & RX_STAT_ETH_LEN_UPDATE) {
            /* Update Length Field inside the Ethernet Header */
            struct ethhdr *hdr = (struct ethhdr *)((u8 *)hw_rxhdr + msdu_offset);

            hdr->h_proto = htons(rxdesc->frame_len - sizeof(struct ethhdr));
        }

        dma_sync_single_for_device(ecrnx_hw->dev, dma_addr,
                                   peek_len, DMA_BIDIRECTIONAL);
        goto end;
    }

    /* Check if it must be discarded after informing upper layer */
    if (status & RX_STAT_SPURIOUS) {
        struct ieee80211_hdr *hdr;

        /* Read mac header to obtain Transmitter Address */
        ecrnx_ipc_rxbuf_elem_sync(ecrnx_hw, skb, msdu_offset + sizeof(*hdr));

        hw_rxhdr = (struct hw_rxhdr *)skb->data;
        hdr = (struct ieee80211_hdr *)(skb->data + msdu_offset);
        ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, hw_rxhdr->flags_vif_idx);
        if (ecrnx_vif) {
            cfg80211_rx_spurious_frame(ecrnx_vif->ndev, hdr->addr2, GFP_ATOMIC);
        }
        ecrnx_ipc_rxbuf_elem_repush(ecrnx_hw, skb);
        goto end;
    }

    /* Check if we need to forward the buffer */
    if (status & RX_STAT_FORWARD) {
        struct ecrnx_sta *sta = NULL;

        /* Remove the SK buffer from the rxbuf_elems table. It will also
           unmap the buffer and then sync the buffer for the cpu */
        ecrnx_ipc_rxbuf_elem_pull(ecrnx_hw, skb);

        hw_rxhdr = (struct hw_rxhdr *)skb->data;
        ecrnx_rx_vector_convert(ecrnx_hw->machw_type,
                               &hw_rxhdr->hwvect.rx_vect1,
                               &hw_rxhdr->hwvect.rx_vect2);
        skb_reserve(skb, msdu_offset);
        skb_put(skb, le32_to_cpu(hw_rxhdr->hwvect.len));
        if (hw_rxhdr->flags_sta_idx != ECRNX_INVALID_STA) {
            sta = &ecrnx_hw->sta_table[hw_rxhdr->flags_sta_idx];
            ecrnx_rx_statistic(ecrnx_hw, hw_rxhdr, sta);
        }

        if (hw_rxhdr->flags_is_80211_mpdu) {
            ecrnx_rx_mgmt_any(ecrnx_hw, skb, hw_rxhdr);
        } else {
            ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, hw_rxhdr->flags_vif_idx);

            if (!ecrnx_vif) {
                dev_err(ecrnx_hw->dev, "Frame received but no active vif (%d)",
                        hw_rxhdr->flags_vif_idx);
                dev_kfree_skb(skb);
                goto check_alloc;
            }

            if (sta) {

                if (sta->vlan_idx != ecrnx_vif->vif_index) {
                    ecrnx_vif = ecrnx_hw->vif_table[sta->vlan_idx];
                    if (!ecrnx_vif) {
                        dev_kfree_skb(skb);
                        goto check_alloc;
                    }
                }

                if (hw_rxhdr->flags_is_4addr && !ecrnx_vif->use_4addr) {
                    cfg80211_rx_unexpected_4addr_frame(ecrnx_vif->ndev,
                                                       sta->mac_addr, GFP_ATOMIC);
                }
            }

            skb->priority = 256 + hw_rxhdr->flags_user_prio;
            if (!ecrnx_rx_data_skb(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr))
                dev_kfree_skb(skb);
        }
    }

check_alloc:
    /* Check if we need to allocate a new buffer */
    if ((status & RX_STAT_ALLOC) &&
        ecrnx_ipc_rxbuf_elem_allocs(ecrnx_hw)) {
        dev_err(ecrnx_hw->dev, "Failed to alloc new RX buf\n");
    }

end:
    REG_SW_CLEAR_PROFILING(ecrnx_hw, SW_PROF_ECRNXDATAIND);

    /* Reset and repush descriptor to FW */
    ecrnx_ipc_rxdesc_elem_repush(ecrnx_hw, elem);

    return 0;
}

#else 

#ifdef CONFIG_ESWIN_RX_REORDER

static bool ecrnx_rx_data_pn_check(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb, u8_l vif_idx, u8_l sta_idx, u8_l tid, u8_l is_ga)
{
    struct ecrnx_sta *sta;
    struct ecrnx_vif *vif;
    u64_l *last_pn;
    u64_l pn;

    memcpy(&pn, skb->data, sizeof(u64_l));

    if (is_ga) {
        last_pn = &ecrnx_hw->vif_table[vif_idx]->rx_pn[tid];
    } else if(ECRNX_INVALID_STA != sta_idx) {
        last_pn = &ecrnx_hw->sta_table[sta_idx].rx_pn[tid];
    } else
    {
        return true;
    }

    //printk("sta_idx:%d tid:%d pn:%llu last:%llu\n ", sta_idx, tid, pn, *last_pn);
    if (pn > (*last_pn)){
        *last_pn = pn;
        return true;
    }

    return false;
}

void ecrnx_rx_reord_msdu_free(spinlock_t *lock, struct list_head *q, struct list_head *list)
{
    spin_lock_bh(lock);
    list_add(list, q);
    spin_unlock_bh(lock);
}

struct reord_msdu_info *ecrnx_rx_reord_msdu_alloc(spinlock_t *lock, struct list_head *q)
{
    struct reord_msdu_info *pmsdu;

    spin_lock_bh(lock);
    if (list_empty(q)) {
        spin_unlock_bh(lock);
        return NULL;
    }

    pmsdu = list_entry(q->next, struct reord_msdu_info, rx_msdu_list);
    list_del_init(q->next);
    spin_unlock_bh(lock);

    return pmsdu;
}

int ecrnx_rx_reord_single_msdu(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif, struct reord_msdu_info *pmsdu)
{
    struct list_head *rx_msdu_free_list = NULL;
    struct sk_buff *skb = NULL;
    struct hw_rxhdr* hw_rxhdr;
    //static u16_l last_sn[TID_MAX] = {0};

    rx_msdu_free_list = &ecrnx_hw->rx_msdu_free_list;
    skb = pmsdu->skb;
    if (skb == NULL) {
        ECRNX_ERR("skb is NULL\n");
        return -1;
    }

    if (pmsdu->need_pn_check) {
        if (ecrnx_rx_data_pn_check(ecrnx_hw, skb,  pmsdu->hw_rxhdr->flags_vif_idx, pmsdu->hw_rxhdr->flags_sta_idx, pmsdu->tid, pmsdu->is_ga)){
            skb_pull(skb, 8);
        } else { 
            ECRNX_DBG("rx_data_check_pn error\n");
            dev_kfree_skb(skb);
            goto end;
        }
    }
#if 0
    if ((last_sn[pmsdu->tid] != pmsdu->sn) && ((last_sn[pmsdu->tid] + 1) % 4096 != pmsdu->sn))
    {
        ECRNX_PRINT("miss[%d] last:%d sn=%d\n",pmsdu->tid,last_sn[pmsdu->tid], pmsdu->sn);
    }

    last_sn[pmsdu->tid] = pmsdu->sn;
#endif
    if (!ecrnx_rx_data_skb(ecrnx_hw, ecrnx_vif, skb, pmsdu->hw_rxhdr))
        dev_kfree_skb(skb);

end:
    pmsdu->skb = NULL;
    ecrnx_rx_reord_msdu_free(&ecrnx_hw->rx_msdu_free_lock, rx_msdu_free_list, &pmsdu->rx_msdu_list);

    return 0;
}


void ecrnx_rx_reord_timer_update(struct ecrnx_hw *ecrnx_hw, struct reord_cntrl *reord_cntrl, int force)
{
    struct list_head *phead, *plist;
    struct reord_msdu_info *pmsdu;
    bool update = false;

    if (force == true) {
        phead = &reord_cntrl->reord_list;
        if (list_empty(phead)) {
            goto end;
        }

        plist = phead->next;
        pmsdu = list_entry(plist, struct reord_msdu_info, reord_pending_list);
        reord_cntrl->win_start = pmsdu->sn;
    }

    phead = &reord_cntrl->reord_list;
    if (list_empty(phead)) {
        goto end;
    }

    list_for_each_entry(pmsdu, phead, reord_pending_list) {
        if (!SN_LESS(reord_cntrl->win_start, pmsdu->sn)) {
            if (SN_EQUAL(reord_cntrl->win_start, pmsdu->sn)) {
                reord_cntrl->win_start = (reord_cntrl->win_start + 1) & 0xFFF;
            }
        } else {
            update = true;
            break;
        }
    }

end:
    if (update == true) {
        if (!timer_pending(&reord_cntrl->reord_timer)) {
            mod_timer(&reord_cntrl->reord_timer, jiffies + msecs_to_jiffies(ECRNX_REORD_TIMEOUT));
        }
    } else {
        if(timer_pending(&reord_cntrl->reord_timer)) {
            del_timer(&reord_cntrl->reord_timer);
        }
    }

}

void ecrnx_rx_reord_list_flush(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif, struct reord_cntrl *reord_cntrl)
{
    struct list_head *phead, *plist;
    struct reord_msdu_info *pmsdu;

    phead = &reord_cntrl->reord_list;
    while (1) {
        if (list_empty(phead)) {
            break;
        }

        plist = phead->next;
        pmsdu = list_entry(plist, struct reord_msdu_info, reord_pending_list);

        if (!SN_LESS(reord_cntrl->win_start, pmsdu->sn)) {
            list_del_init(&(pmsdu->reord_pending_list));
            ecrnx_rx_reord_single_msdu(ecrnx_hw, ecrnx_vif, pmsdu);
        } else {
            break;
        }
    }
}


void ecrnx_rx_reord_timeout_handler(struct timer_list *t)
{
    struct reord_cntrl *reord_cntrl = from_timer(reord_cntrl, t, reord_timer);

    if(!reord_cntrl->valid || !reord_cntrl->active)
        return ;

    if(!work_pending(&reord_cntrl->reord_timer_work))
        schedule_work(&reord_cntrl->reord_timer_work);

}

void ecrnx_rx_reord_timeout_worker(struct work_struct *work)
{
    struct reord_cntrl *reord_cntrl = container_of(work, struct reord_cntrl, reord_timer_work);
    struct ecrnx_hw *ecrnx_hw = reord_cntrl->ecrnx_hw;
    struct ecrnx_vif *ecrnx_vif = reord_cntrl->ecrnx_vif;

    if(!reord_cntrl->valid || !reord_cntrl->active)
        return ;

    spin_lock_bh(&reord_cntrl->reord_list_lock);

    ecrnx_rx_reord_timer_update(ecrnx_hw, reord_cntrl, true);

    ecrnx_rx_reord_list_flush(ecrnx_hw, ecrnx_vif, reord_cntrl);
    spin_unlock_bh(&reord_cntrl->reord_list_lock);

    return ;
}

void ecrnx_rx_reord_sta_init(struct ecrnx_hw* ecrnx_hw, struct ecrnx_vif *ecrnx_vif, u8 sta_idx)
{
    struct reord_cntrl *reord_cntrl = NULL;
    u32_l i = 0;

    ECRNX_DBG("%s sta_idx:%d\n", __func__, sta_idx);
    for (i = 0; i < TID_MAX; i++) {
        reord_cntrl = &ecrnx_hw->sta_table[sta_idx].reord_cntrl[i];
        if (!reord_cntrl->valid) {
            reord_cntrl->active = true;
            reord_cntrl->win_start = 0xffff;
            reord_cntrl->win_size = ECRNX_REORD_WINSIZE;
            reord_cntrl->ecrnx_hw = ecrnx_hw;
            reord_cntrl->ecrnx_vif = ecrnx_vif;
            INIT_LIST_HEAD(&reord_cntrl->reord_list);
            spin_lock_init(&reord_cntrl->reord_list_lock);
            timer_setup(&reord_cntrl->reord_timer, ecrnx_rx_reord_timeout_handler, 0);
            INIT_WORK(&reord_cntrl->reord_timer_work, ecrnx_rx_reord_timeout_worker);
            reord_cntrl->valid = true;
        }
    }

}

int ecrnx_rx_reord_tid_flush(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif, struct sk_buff *skb, u8 sta_idx, u8 tid)
{
    struct reord_cntrl *reord_cntrl;
    struct list_head *phead, *plist;
    struct reord_msdu_info *pmsdu;

    if (sta_idx == ECRNX_INVALID_STA)
        return -1;

    reord_cntrl = &ecrnx_hw->sta_table[sta_idx].reord_cntrl[tid];

    if(!reord_cntrl->valid || !reord_cntrl->active)
        return -1;

    spin_lock(&reord_cntrl->reord_list_lock);
    phead = &reord_cntrl->reord_list;
    while (1) {
        if (list_empty(phead)) {
            break;
        }
        plist = phead->next;
        pmsdu = list_entry(plist, struct reord_msdu_info, reord_pending_list);
        ecrnx_rx_reord_single_msdu(ecrnx_hw, ecrnx_vif, pmsdu);
        list_del_init(&(pmsdu->reord_pending_list));
    }

    //printk("flush:sta_idx:%d tid=%d \n", sta_idx,tid);
    reord_cntrl->active = false;
    spin_unlock(&reord_cntrl->reord_list_lock);
    if (timer_pending(&reord_cntrl->reord_timer))
        del_timer_sync(&reord_cntrl->reord_timer);
    //cancel_work_sync(&reord_cntrl->reord_timer_work);

    return 0;
}

void ecrnx_rx_reord_sta_deinit(struct ecrnx_hw* ecrnx_hw, u8 sta_idx, bool is_del)
{
    struct reord_cntrl *reord_cntrl = NULL;
    u32_l i = 0;

    if (ecrnx_hw == NULL || sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
        return;
    }
    ECRNX_DBG("%s sta_idx:%d\n", __func__, sta_idx);

    for (i=0; i < TID_MAX; i++) {
        struct reord_msdu_info *req, *next;
        reord_cntrl = &ecrnx_hw->sta_table[sta_idx].reord_cntrl[i];
        if (reord_cntrl->valid) {
            reord_cntrl->valid = false;
            if(reord_cntrl->active){
                reord_cntrl->active = false;
                if (timer_pending(&reord_cntrl->reord_timer)) {
                    del_timer_sync(&reord_cntrl->reord_timer);
                }

                if (!is_del) {
                    cancel_work_sync(&reord_cntrl->reord_timer_work);
                }
            }

            spin_lock(&reord_cntrl->reord_list_lock);
            list_for_each_entry_safe(req, next, &reord_cntrl->reord_list, reord_pending_list) {
                list_del_init(&req->reord_pending_list);
                if(req->skb != NULL)
                    dev_kfree_skb(req->skb);
                req->skb = NULL;
                ecrnx_rx_reord_msdu_free(&ecrnx_hw->rx_msdu_free_lock, &ecrnx_hw->rx_msdu_free_list, &req->rx_msdu_list);
            }
            spin_unlock(&reord_cntrl->reord_list_lock);
        }
    }

}

static struct reord_msdu_info *ecrnx_rx_reord_queue_init(struct list_head *q, int qsize)
{
    int i;
    struct reord_msdu_info *req, *reqs;

    reqs = vmalloc(qsize*sizeof(struct reord_msdu_info));
    if (reqs == NULL)
        return NULL;

    req = reqs;
    for (i = 0; i < qsize; i++)
    {
        INIT_LIST_HEAD(&req->rx_msdu_list);
        list_add(&req->rx_msdu_list, q);
        req++;
    }

    return reqs;
}

void ecrnx_rx_reord_deinit(struct ecrnx_hw *ecrnx_hw)
{
    struct reord_msdu_info *req, *next;
    u32_l sta_idx;

    ECRNX_DBG("%s\n", __func__);
    for (sta_idx = 0; sta_idx < NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX; sta_idx++)
    {
        if (ecrnx_hw->sta_table[sta_idx].valid)
        {
             ecrnx_rx_reord_sta_deinit(ecrnx_hw, sta_idx, false);
        }
    }
    list_for_each_entry_safe(req, next, &ecrnx_hw->rx_msdu_free_list, rx_msdu_list) {
        list_del_init(&req->rx_msdu_list);
    }

    if (ecrnx_hw->rx_reord_buf)
        vfree(ecrnx_hw->rx_reord_buf);
}

void ecrnx_rx_reord_init(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG("%s\n", __func__);
    INIT_LIST_HEAD(&ecrnx_hw->rx_msdu_free_list);
    spin_lock_init(&ecrnx_hw->rx_msdu_free_lock);
    ecrnx_hw->rx_reord_buf = ecrnx_rx_reord_queue_init(&ecrnx_hw->rx_msdu_free_list, ECRNX_REORD_RX_MSDU_CNT);
    spin_lock_init(&ecrnx_hw->rx_reord_lock);
    INIT_LIST_HEAD(&ecrnx_hw->rx_reord_list);

}

int ecrnx_rx_reord_sn_check(struct reord_cntrl *reord_cntrl, u16 sn)
{
    u16 win_size = reord_cntrl->win_size;
    u16 win_end = (reord_cntrl->win_start + win_size -1) & 0xFFF;

    if (reord_cntrl->win_start == 0xFFFF) {
        reord_cntrl->win_start = sn;
    }

    if (SN_LESS(sn, reord_cntrl->win_start)) {
        return -1;
    }

    if (SN_EQUAL(sn, reord_cntrl->win_start)){
        reord_cntrl->win_start = (reord_cntrl->win_start + 1) & 0xFFF;
    } else if (SN_LESS(win_end, sn)) {
        if (sn >= (win_size-1))
            reord_cntrl->win_start = sn-(win_size-1);
        else
            reord_cntrl->win_start = 0xFFF - (win_size - (sn + 1)) + 1;
    }

    return 0;
}

int ecrnx_rx_reord_msdu_insert(struct reord_cntrl *reord_cntrl, struct reord_msdu_info *pmsdu)
{
    struct list_head *preord_list = &reord_cntrl->reord_list;
    struct list_head *phead, *plist;
    struct reord_msdu_info *nextmsdu;

//first time:not any prframe in preord_list, so phead = phead->next
    phead = preord_list;
    plist = phead->next;

    while(phead != plist) {
        nextmsdu = list_entry(plist, struct reord_msdu_info, reord_pending_list);
        if (SN_LESS(nextmsdu->sn, pmsdu->sn)) {
            plist = plist->next;
        } else if (SN_EQUAL(nextmsdu->sn, pmsdu->sn)){
            return -1;
        } else {
            break;
        }
    }

    list_add_tail(&(pmsdu->reord_pending_list), plist);

    return 0;
}

void ecrnx_rx_reord_check(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif, struct hw_rxhdr *hw_rxhdr, struct sk_buff *skb, struct rxu_stat_mm* orignal_rxu_state)
{
    int ret=0;
    struct reord_msdu_info *pmsdu;
    struct reord_cntrl *reord_cntrl;

    if (ecrnx_vif == NULL || skb->len <= 14) {
        dev_kfree_skb(skb);
        return ;
    }

    pmsdu = ecrnx_rx_reord_msdu_alloc(&ecrnx_hw->rx_msdu_free_lock, &ecrnx_hw->rx_msdu_free_list);
    if (!pmsdu) {
        dev_err(ecrnx_hw->dev, "ecrnx_rx_reord_msdu_alloc fail\n");
        dev_kfree_skb(skb);
        return ;
    }

    INIT_LIST_HEAD(&pmsdu->reord_pending_list);
    pmsdu->hw_rxhdr = hw_rxhdr;
    pmsdu->sn = orignal_rxu_state->sn;
    pmsdu->tid = orignal_rxu_state->tid;
    pmsdu->is_ga = orignal_rxu_state->is_ga;
    pmsdu->skb = skb;
    pmsdu->need_pn_check = orignal_rxu_state->need_pn_check;

    if (hw_rxhdr->flags_sta_idx != ECRNX_INVALID_STA) {
        reord_cntrl = &ecrnx_hw->sta_table[hw_rxhdr->flags_sta_idx].reord_cntrl[orignal_rxu_state->tid];
        pmsdu->preorder_ctrl = reord_cntrl;
        if(reord_cntrl->valid) {
            if (!reord_cntrl->active) {
                reord_cntrl->active = true;
                reord_cntrl->win_start = 0xffff;
                //reord_cntrl->win_size = ECRNX_REORD_WINSIZE;
                //reord_cntrl->ecrnx_hw = ecrnx_hw;
                //reord_cntrl->ecrnx_vif = ecrnx_vif;
            }
        } else {
            ECRNX_PRINT("reord_cntrl invalid sta:%d sn:%d start:%d \n", hw_rxhdr->flags_sta_idx , pmsdu->sn, reord_cntrl->win_start);
            ecrnx_rx_reord_single_msdu(ecrnx_hw, ecrnx_vif, pmsdu);
            return ;
        }
    } else {
        ECRNX_PRINT("sta_idx invalid sta:%d sn:%d start:%d  \n", hw_rxhdr->flags_sta_idx , pmsdu->sn, reord_cntrl->win_start);
        ecrnx_rx_reord_single_msdu(ecrnx_hw, ecrnx_vif, pmsdu);
        return ;
    }

    spin_lock(&reord_cntrl->reord_list_lock);
    if (ecrnx_rx_reord_sn_check(reord_cntrl, pmsdu->sn)) {
        //printk("%s discard sn:%d s:%d \n", __func__, pmsdu->sn, reord_cntrl->win_start);
        //ecrnx_rx_reord_single_msdu(ecrnx_hw, ecrnx_vif, pmsdu);
        spin_unlock(&reord_cntrl->reord_list_lock);
        goto discard;
    }
    //printk("start:%d %d\n",reord_cntrl->win_start, pmsdu->sn);
    if (ecrnx_rx_reord_msdu_insert(reord_cntrl, pmsdu)) {
        spin_unlock(&reord_cntrl->reord_list_lock);
        goto discard;
    }

    ecrnx_rx_reord_timer_update(ecrnx_hw, reord_cntrl, false);

    ecrnx_rx_reord_list_flush(ecrnx_hw, ecrnx_vif, reord_cntrl);

    spin_unlock(&reord_cntrl->reord_list_lock);

    return ;

discard:
    if (pmsdu->skb) {
        dev_kfree_skb(pmsdu->skb);
        pmsdu->skb = NULL;
    }

    ecrnx_rx_reord_msdu_free(&ecrnx_hw->rx_msdu_free_lock, &ecrnx_hw->rx_msdu_free_list, &pmsdu->rx_msdu_list);
}
#endif


u8 ecrnx_rx_agg_data_ind(struct ecrnx_hw *ecrnx_hw, u16_l status, struct sk_buff* skb, int msdu_offset)
{
    struct hw_rxhdr *hw_rxhdr = NULL;
    struct ecrnx_vif *ecrnx_vif  = NULL;
    hw_rxhdr = (struct hw_rxhdr*)skb->data;

#if 0
	ECRNX_DBG("[eswin_agg] hw_vect_len: %d , rxu_stat_mm: %d \n", sizeof(struct hw_rxhdr), sizeof(struct rxu_stat_mm));
    ECRNX_DBG("[eswin_agg] rcv_frm_len: %d \n", hw_rxhdr->hwvect.len);
    ECRNX_DBG("%s, parttern:0x%2x, status:%d !!", __func__, hw_rxhdr->pattern, status);
#endif
    /* Check the pattern */
    if (hw_rxhdr->pattern != ecrnx_rxbuff_pattern) {
        dev_err(ecrnx_hw->dev, "RX Buff Pattern not correct, pattern (%x), status %d\n", hw_rxhdr->pattern, status);
            print_hex_dump(KERN_DEBUG, DBG_PREFIX_PAT, DUMP_PREFIX_NONE, 16, 1, hw_rxhdr, skb->len, false);
        BUG();
    }

    /* Check if we need to delete the buffer */
    if (status & RX_STAT_DELETE) {
        dev_kfree_skb(skb);
        goto end;
    }
    /* Check if we need to forward the buffer */
    else if (status & RX_STAT_FORWARD) {

        /* Remove the SK buffer from the rxbuf_elems table. It will also
           unmap the buffer and then sync the buffer for the cpu */
        //ecrnx_ipc_rxbuf_elem_pull(ecrnx_hw, skb);

        hw_rxhdr = (struct hw_rxhdr *)skb->data;
        ecrnx_rx_vector_convert(ecrnx_hw->machw_type,
                               &hw_rxhdr->hwvect.rx_vect1,
                               &hw_rxhdr->hwvect.rx_vect2);

        //skb_reserve(skb, msdu_offset);
        //ECRNX_DBG("[eswin_agg]before pull skb len: %d \n", skb->len);
        skb_pull(skb, msdu_offset);
        //ECRNX_DBG("[eswin_agg]after pull skb len: %d \n", skb->len);

        if (hw_rxhdr->flags_is_80211_mpdu) {
			//ECRNX_DBG("[eswin_agg]recv mgmt\n");
            ecrnx_rx_mgmt_any(ecrnx_hw, skb, hw_rxhdr);
        } else {
            ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, hw_rxhdr->flags_vif_idx);
            //ECRNX_DBG("[eswin_agg] flags vif idx1:%d, vif: 0x%x \n", hw_rxhdr->flags_vif_idx, ecrnx_vif);
            if (!ecrnx_vif) {
                dev_err(ecrnx_hw->dev, "Frame received but no active vif (%d)",
                        hw_rxhdr->flags_vif_idx);
                dev_kfree_skb(skb);
				ECRNX_ERR("[agg] check_alloc, %d !!", __LINE__);
                goto exit_no_free;
            }

            if (hw_rxhdr->flags_sta_idx != ECRNX_INVALID_STA) {
                struct ecrnx_sta *sta;

                sta = &ecrnx_hw->sta_table[hw_rxhdr->flags_sta_idx];
                ecrnx_rx_statistic(ecrnx_hw, hw_rxhdr, sta);
                //ECRNX_DBG("[eswin_agg] sta idx:%d, vif idx: %d \n", sta->vlan_idx, ecrnx_vif->vif_index);
                if (sta->vlan_idx != ecrnx_vif->vif_index) {
                    ecrnx_vif = ecrnx_hw->vif_table[sta->vlan_idx];
                    if (!ecrnx_vif) {
                        dev_kfree_skb(skb);
						ECRNX_ERR("[agg] check_alloc, %d !!", __LINE__);
                        goto exit_no_free;
                    }
                }

                if (hw_rxhdr->flags_is_4addr && !ecrnx_vif->use_4addr) {
                    cfg80211_rx_unexpected_4addr_frame(ecrnx_vif->ndev,
                                                       sta->mac_addr, GFP_ATOMIC);
                }
            }

            skb->priority = 256 + hw_rxhdr->flags_user_prio;
            if (!ecrnx_rx_data_skb(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr)){
                dev_kfree_skb(skb);
                dev_err(ecrnx_hw->dev, "ecrnx_rx_data_skb_sdio error \n");
            }
        }
    }

exit_no_free:
    /* Check if we need to allocate a new buffer */
    //dev_err(ecrnx_hw->dev, "Failed to alloc new RX buf\n");

end:
    /* Reset and repush descriptor to FW */
    //sdio_rx_buf_repush(&orignal_rxu_state, &orignal_rx_hd, skb);

    return 0;
}

static int ecrnx_set_station_info(struct ecrnx_vif *vif, const u8 *mac, struct rx_vector_1 *rx_vect1)
{
    struct ecrnx_sta *sta = NULL;
    static u32 rx_pkts = 0x1ff;
    static u32 tx_pkts = 0x2ff;
    static u64 rx_bytes = 0x3ff;
    static u64 tx_bytes = 0x4ff;

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
    {
        struct ecrnx_sta_stats *stats = &sta->stats;

        stats->rx_bytes = rx_bytes++;
        stats->tx_bytes = tx_bytes++;
        stats->rx_pkts = rx_pkts++;
        stats->tx_pkts = tx_pkts++;
        memcpy(&stats->last_rx.rx_vect1, rx_vect1, sizeof(struct rx_vector_1));
    }

    return 0;
}

u8 ecrnx_rxdataind(void *pthis, void *hostid)
{
    struct ecrnx_hw *ecrnx_hw = pthis;
    struct hw_rxhdr *hw_rxhdr = NULL;
    struct ecrnx_vif *ecrnx_vif  = NULL;
    int msdu_offset = sizeof(struct hw_rxhdr);
    u16_l status;

    struct rxu_stat_mm* orignal_rxu_state = NULL;
    struct sk_buff *skb = NULL, *frg_ctrl_skb = NULL;

    if(!pthis || !hostid){
        ECRNX_ERR("ecrnx_rxdataind error! \n");
        return 0;
    }

    spin_lock_bh(&ecrnx_hw->rx_lock);
    ecrnx_hw->data_rx++;
    skb = (struct sk_buff *)hostid;
    orignal_rxu_state = (struct rxu_stat_mm*)skb->data;
    skb_pull(skb, sizeof(struct rxu_stat_mm));

#ifdef CONFIG_ECRNX_ESWIN
    if (orignal_rxu_state->real_offset)
    {
        msdu_offset += orignal_rxu_state->real_offset;
        //ECRNX_DBG("orignal_rxu_state->real_offset = %hu, msdu_offset:%d \n", orignal_rxu_state->real_offset, msdu_offset);
    }

    if(orignal_rxu_state->fragment_flag && ((orignal_rxu_state->status & RX_STAT_FORWARD) || (orignal_rxu_state->status & RX_STAT_DELETE))) { //defrag frame
        struct defrag_elem *defrag_elem_rx = NULL, *next = NULL;

        if(list_empty(&ecrnx_hw->defrag_rx_list)){
            ECRNX_ERR("error defrag_rx_list is epmty!!! \n");
            dev_kfree_skb(skb);
            goto end;
        }

        ECRNX_DBG("[eswin_agg]%s enter, tid:0x%x, fragment_flag:0x%llx, status:0x%x \n", __func__, orignal_rxu_state->tid, orignal_rxu_state->fragment_flag, orignal_rxu_state->status);
        if(orignal_rxu_state->status & RX_STAT_DELETE){
            /*delete all the same tid frame*/
            list_for_each_entry_safe(defrag_elem_rx, next, &ecrnx_hw->defrag_rx_list, head) {
                ECRNX_DBG("[eswin_agg]delete: sn1:%d, sn2:%d, tid1:%d, skb:0x%08x \n",defrag_elem_rx->sn, orignal_rxu_state->sn, defrag_elem_rx->tid, defrag_elem_rx->skb);
                if((defrag_elem_rx->tid == orignal_rxu_state->tid) && (defrag_elem_rx->sn == orignal_rxu_state->sn)){ ///==sn
                    dev_kfree_skb(defrag_elem_rx->skb);
                    list_del(&defrag_elem_rx->head);
                    kfree(defrag_elem_rx);
                }
            }
            dev_kfree_skb(skb);
            goto end;
        }
        else if(orignal_rxu_state->status & RX_STAT_FORWARD){
            struct defrag_elem *defrag_elem_rx = NULL, *next = NULL;
            u32_l recv_len = 0, offset_len = 0, hw_rxhdr_len = sizeof(*hw_rxhdr);
            unsigned char* skb_ptr = NULL;
            int fg_first = 1;
            int fg_total_len = 0;

            /*caculate the defrag frames total lens*/
            list_for_each_entry_safe(defrag_elem_rx, next, &ecrnx_hw->defrag_rx_list, head) {
                    if((defrag_elem_rx->tid == orignal_rxu_state->tid) && (defrag_elem_rx->sn == orignal_rxu_state->sn)){
                        recv_len += defrag_elem_rx->skb->len;
                    }
                }

            frg_ctrl_skb = skb;
            /*alloc a new skb, and put the same tid frame to the new skb*/
            skb = dev_alloc_skb(recv_len);
            
            list_for_each_entry_safe(defrag_elem_rx, next, &ecrnx_hw->defrag_rx_list, head) {
                ECRNX_DBG("[eswin_agg]forward: sn1:%d, sn2:%d, tid1:%d, skb:0x%p \n",defrag_elem_rx->sn, orignal_rxu_state->sn, defrag_elem_rx->tid, defrag_elem_rx->skb);
                if((defrag_elem_rx->tid == orignal_rxu_state->tid) && (defrag_elem_rx->sn == orignal_rxu_state->sn)){
                    if (fg_first) {
					    offset_len = 0;
                    } else {
                        /*first skb should include the hw_rxhdr, other's not need*/
                        offset_len = hw_rxhdr_len;
                        offset_len += sizeof(struct ethhdr);
                    }
                    offset_len += defrag_elem_rx->real_offset;
                    skb_ptr = skb_put(skb, defrag_elem_rx->skb->len - offset_len);
                    if (fg_first) {
                        memcpy(skb_ptr, defrag_elem_rx->skb->data, hw_rxhdr_len);
                        memcpy(skb_ptr + hw_rxhdr_len, defrag_elem_rx->skb->data + hw_rxhdr_len + offset_len, defrag_elem_rx->skb->len - hw_rxhdr_len - offset_len);
                        fg_total_len = defrag_elem_rx->skb->len - hw_rxhdr_len - offset_len;
                        hw_rxhdr = (struct hw_rxhdr*)skb_ptr;
                    } else {
                        memcpy(skb_ptr, defrag_elem_rx->skb->data + offset_len, defrag_elem_rx->skb->len - offset_len);
                        fg_total_len += defrag_elem_rx->skb->len - offset_len;
                    }
                    dev_kfree_skb(defrag_elem_rx->skb);
                    list_del(&defrag_elem_rx->head);
                    kfree(defrag_elem_rx);
                    fg_first = 0;
                }
            }

            if (!fg_first) {
                hw_rxhdr->hwvect.len = fg_total_len;//update len
                msdu_offset = hw_rxhdr_len;
                //printk("[llm] sn %d: total %d, skb %x (%d-%x-%x)\n", orignal_rxu_state->sn, fg_total_len, skb, skb->len, skb->data, skb->tail);
            }
        }
    }
#endif

    hw_rxhdr = (struct hw_rxhdr*)skb->data;

#if 0
	ECRNX_DBG(" hw_vect_len: %d , rxu_stat_mm: %d \n", sizeof(struct hw_rxhdr), sizeof(struct rxu_stat_mm));
    ECRNX_DBG(" rcv_frm_len: %d \n", hw_rxhdr->hwvect.len);
#endif

    status = orignal_rxu_state->status;
    //ECRNX_DBG("%s, parttern:0x%2x, status:%d !!", __func__, hw_rxhdr->pattern, status);
    /* Check the pattern */
    if (hw_rxhdr->pattern != ecrnx_rxbuff_pattern) {
        dev_err(ecrnx_hw->dev, "RX Buff Pattern not correct, pattern (%x), status %d\n", hw_rxhdr->pattern, status);
        ECRNX_ERR("RX Buff Pattern not correct, pattern (%x), status %d, skb (%p), skb_len %d\n", hw_rxhdr->pattern, status, skb->data, skb->len);
        print_hex_dump(KERN_DEBUG, DBG_PREFIX_PAT, DUMP_PREFIX_NONE, 16, 1, hw_rxhdr, skb->len, false);
        BUG();
    }

    /* Check if we need to delete the buffer */
    if (status & RX_STAT_DELETE) {
        dev_kfree_skb(skb);
        goto end;
    }

    /* Check if we need to forward the buffer coming from a monitor interface */
    if (status & RX_STAT_MONITOR) {
        struct sk_buff *skb_monitor;
        struct hw_rxhdr hw_rxhdr_copy;
        u8 rtap_len;
        u16 frm_len;

        //Check if monitor interface exists and is open
        ECRNX_DBG("monitor_vif: %d \n", ecrnx_hw->monitor_vif);
        ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, ecrnx_hw->monitor_vif);
        if (!ecrnx_vif) {
            dev_err(ecrnx_hw->dev, "Received monitor frame but there is no monitor interface open\n");
            dev_kfree_skb(skb);
            goto check_len_update;
        }

        ecrnx_rx_vector_convert(ecrnx_hw->machw_type,
                               &hw_rxhdr->hwvect.rx_vect1,
                               &hw_rxhdr->hwvect.rx_vect2);
        rtap_len = ecrnx_rx_rtap_hdrlen(&hw_rxhdr->hwvect.rx_vect1, false);

        // Move skb->data pointer to MAC Header or Ethernet header
        skb->data += msdu_offset;

        //Save frame length
        frm_len = le32_to_cpu(hw_rxhdr->hwvect.len) - msdu_offset;

        // Reserve space for frame
        skb->len = frm_len;

        if (status == RX_STAT_MONITOR) {
            //Check if there is enough space to add the radiotap header
            if (skb_headroom(skb) > rtap_len) {

                skb_monitor = skb;

                //Duplicate the HW Rx Header to override with the radiotap header
                memcpy(&hw_rxhdr_copy, hw_rxhdr, sizeof(hw_rxhdr_copy));

            } else {
                //Duplicate the skb and extend the headroom
                skb_monitor = skb_copy_expand(skb, rtap_len, 0, GFP_ATOMIC);

                //Reset original skb->data pointer
                skb->data = (void*) hw_rxhdr;
            }
        }
        else
        {
#ifdef CONFIG_ECRNX_MON_DATA
            // Check if MSDU
            if (!hw_rxhdr->flags_is_80211_mpdu) {
                // MSDU
                //Extract MAC header
                u16 machdr_len = hw_rxhdr->mac_hdr_backup.buf_len;
                u8* machdr_ptr = hw_rxhdr->mac_hdr_backup.buffer;
        
                //Pull Ethernet header from skb
                skb_pull(skb, sizeof(struct ethhdr));
        
                // Copy skb and extend for adding the radiotap header and the MAC header
                skb_monitor = skb_copy_expand(skb,
                                              rtap_len + machdr_len,
                                              0, GFP_ATOMIC);
        
                //Reserve space for the MAC Header
                skb_push(skb_monitor, machdr_len);
        
                //Copy MAC Header
                memcpy(skb_monitor->data, machdr_ptr, machdr_len);
        
                //Update frame length
                frm_len += machdr_len - sizeof(struct ethhdr);
            } else {
                // MPDU
                skb_monitor = skb_copy_expand(skb, rtap_len, 0, GFP_ATOMIC);
            }
        
            //Reset original skb->data pointer
            skb->data = (void*) hw_rxhdr;
#else
            //Reset original skb->data pointer
            skb->data = (void*) hw_rxhdr;
        
            wiphy_err(ecrnx_hw->wiphy, "RX status %d is invalid when MON_DATA is disabled\n", status);
            dev_kfree_skb(skb);
            goto check_len_update;
#endif
        }

        skb_reset_tail_pointer(skb);
        skb->len = 0;
        skb_reset_tail_pointer(skb_monitor);
        skb_monitor->len = 0;

        skb_put(skb_monitor, frm_len);
        if (ecrnx_rx_monitor(ecrnx_hw, ecrnx_vif, skb_monitor, hw_rxhdr, rtap_len)){
                dev_kfree_skb(skb);
                dev_err(ecrnx_hw->dev, "skb monitor handle error \n");
            }

        if (status == RX_STAT_MONITOR) {
            status |= RX_STAT_ALLOC;
            if (skb_monitor != skb) {
                dev_kfree_skb(skb);
                dev_err(ecrnx_hw->dev, "skb  handle status error \n");
            }
        }
    }

check_len_update:
    /* Check if we need to update the length */
    if (status & RX_STAT_LEN_UPDATE) {

        hw_rxhdr = (struct hw_rxhdr *)skb->data;

        hw_rxhdr->hwvect.len = orignal_rxu_state->frame_len - msdu_offset;

        if (status & RX_STAT_ETH_LEN_UPDATE) {
            /* Update Length Field inside the Ethernet Header */
            struct ethhdr *hdr = (struct ethhdr *)((u8 *)hw_rxhdr + msdu_offset);

            hdr->h_proto = htons(orignal_rxu_state->frame_len - sizeof(struct ethhdr));
        }
        dev_kfree_skb(skb);
        goto end;
    }

    /* Check if it must be discarded after informing upper layer */
    if (status & RX_STAT_SPURIOUS) {
        struct ieee80211_hdr *hdr;

        /* Read mac header to obtain Transmitter Address */
        //ecrnx_ipc_rxbuf_elem_sync(ecrnx_hw, skb, msdu_offset + sizeof(*hdr));

        hw_rxhdr = (struct hw_rxhdr *)skb->data;
        hdr = (struct ieee80211_hdr *)(skb->data + msdu_offset);
        ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, hw_rxhdr->flags_vif_idx);
        ECRNX_DBG(" flags vif idx:%d, vif: 0x%x \n", hw_rxhdr->flags_vif_idx, ecrnx_vif);
        if (ecrnx_vif) {
            cfg80211_rx_spurious_frame(ecrnx_vif->ndev, hdr->addr2, GFP_ATOMIC);
        }else{
            dev_kfree_skb(skb);
        }

        goto end;
    }

    /* Check if we need to forward the buffer */
    if (status & RX_STAT_FORWARD) {

        /* Remove the SK buffer from the rxbuf_elems table. It will also
           unmap the buffer and then sync the buffer for the cpu */
        //ecrnx_ipc_rxbuf_elem_pull(ecrnx_hw, skb);

        hw_rxhdr = (struct hw_rxhdr *)skb->data;
        ecrnx_rx_vector_convert(ecrnx_hw->machw_type,
                               &hw_rxhdr->hwvect.rx_vect1,
                               &hw_rxhdr->hwvect.rx_vect2);

        //skb_reserve(skb, msdu_offset);
        //ECRNX_DBG("before pull skb len: %d, msdu_offset:%d \n", skb->len, msdu_offset);
        skb_pull(skb, msdu_offset);
        //ECRNX_DBG("after pull skb len: %d \n", skb->len);

        if (hw_rxhdr->flags_is_80211_mpdu) {
			//ECRNX_DBG("recv mgmt\n");
            ecrnx_rx_mgmt_any(ecrnx_hw, skb, hw_rxhdr);
        } else {
            ecrnx_vif = ecrnx_rx_get_vif(ecrnx_hw, hw_rxhdr->flags_vif_idx);
            //ECRNX_DBG(" flags vif idx1:%d, vif: 0x%x \n", hw_rxhdr->flags_vif_idx, ecrnx_vif);
            if (!ecrnx_vif) {
                dev_err(ecrnx_hw->dev, "Frame received but no active vif (%d)",
                        hw_rxhdr->flags_vif_idx);
                dev_kfree_skb(skb);
				ECRNX_ERR(" check_alloc, %d !!", __LINE__);
                goto end;
            }

            if (hw_rxhdr->flags_sta_idx != ECRNX_INVALID_STA) {
                struct ecrnx_sta *sta;

                sta = &ecrnx_hw->sta_table[hw_rxhdr->flags_sta_idx];
                ecrnx_rx_statistic(ecrnx_hw, hw_rxhdr, sta);
                //ECRNX_DBG(" sta idx:%d, vif idx: %d \n", sta->vlan_idx, ecrnx_vif->vif_index);
                if (sta->vlan_idx != ecrnx_vif->vif_index) {
                    ecrnx_vif = ecrnx_hw->vif_table[sta->vlan_idx];
                    if (!ecrnx_vif) {
                        dev_kfree_skb(skb);
						ECRNX_ERR(" check_alloc, %d !!", __LINE__);
                        goto end;
                    }
                }

                ecrnx_set_station_info(ecrnx_vif, (const u8*)sta->mac_addr, &hw_rxhdr->hwvect.rx_vect1);
                if (hw_rxhdr->flags_is_4addr && !ecrnx_vif->use_4addr) {
                    cfg80211_rx_unexpected_4addr_frame(ecrnx_vif->ndev,
                                                       sta->mac_addr, GFP_ATOMIC);
                }
            }

            skb->priority = 256 + hw_rxhdr->flags_user_prio;
#ifdef CONFIG_ESWIN_RX_REORDER
                //printk("sn:%d %d %d %d l:%d\n",orignal_rxu_state->is_qos, orignal_rxu_state->need_reord, orignal_rxu_state->need_pn_check,orignal_rxu_state->sn, skb->len -8);
                if(orignal_rxu_state->is_qos && orignal_rxu_state->need_reord) {
                    ecrnx_rx_reord_check(ecrnx_hw, ecrnx_vif, hw_rxhdr, skb, orignal_rxu_state);
                }else if(orignal_rxu_state->is_qos  && !orignal_rxu_state->need_reord) {
                    ecrnx_rx_reord_tid_flush(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr->flags_sta_idx,orignal_rxu_state->tid);
                    if (orignal_rxu_state->need_pn_check)
                    {
                        if (!ecrnx_rx_data_pn_check(ecrnx_hw, skb, hw_rxhdr->flags_vif_idx, hw_rxhdr->flags_sta_idx, orignal_rxu_state->tid, orignal_rxu_state->is_ga))
                        {
                            ECRNX_DBG("rx_data_check_pn error\n");
                            dev_kfree_skb(skb);
                            goto end;
                        }
                        skb_pull(skb, 8);
                    }
                    if (!ecrnx_rx_data_skb(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr))
                        dev_kfree_skb(skb);
                }else {
                    if (orignal_rxu_state->need_pn_check)
                    {
                        if (!ecrnx_rx_data_pn_check(ecrnx_hw, skb, hw_rxhdr->flags_vif_idx, hw_rxhdr->flags_sta_idx, orignal_rxu_state->tid, orignal_rxu_state->is_ga))
                        {
                            ECRNX_DBG("rx_data_check_pn error\n");
                            dev_kfree_skb(skb);
                            goto end;
                        }
                        skb_pull(skb, 8);
                    }
                    if (!ecrnx_rx_data_skb(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr))
                        dev_kfree_skb(skb);
                }
#else
            if (!ecrnx_rx_data_skb(ecrnx_hw, ecrnx_vif, skb, hw_rxhdr)){
                dev_kfree_skb(skb);
                dev_err(ecrnx_hw->dev, "ecrnx_rx_data_skb_sdio error \n");
            }
#endif
        }
        goto end;
    }

    else if (status & RX_STAT_ALLOC) {
        /*agg frame*/
            if (hw_rxhdr->flags_sta_idx != ECRNX_INVALID_STA) {
                if (orignal_rxu_state->need_pn_check)
                {
                    if (!ecrnx_rx_data_pn_check(ecrnx_hw, skb, hw_rxhdr->flags_vif_idx, hw_rxhdr->flags_sta_idx, orignal_rxu_state->tid, orignal_rxu_state->is_ga))
                    {
                        dev_err(ecrnx_hw->dev, "rx_data_check_pn error \n");
                        dev_kfree_skb(skb);
                        goto end;
                    }
                    skb_pull(skb, 8);
                }
            }
            
           if(orignal_rxu_state->fragment_flag){
            struct defrag_elem* defrag_elem_rx = (struct defrag_elem*)kzalloc(sizeof(struct defrag_elem), GFP_ATOMIC);

            if(defrag_elem_rx){
                defrag_elem_rx->tid = orignal_rxu_state->tid;
                defrag_elem_rx->sn = orignal_rxu_state->sn;
                defrag_elem_rx->real_offset = orignal_rxu_state->real_offset;
                defrag_elem_rx->skb = skb;
                ECRNX_DBG("ecrnx_rxdataind:insert_skb:0x%08x, sn:%d, tid:%d, sn:%d \n", skb, orignal_rxu_state->tid, orignal_rxu_state->sn);
                list_add_tail(&defrag_elem_rx->head, &ecrnx_hw->defrag_rx_list);
            }else{
                ECRNX_ERR("no buffer !!!! \n");
            }
        }
        //ECRNX_DBG("status set alloc\n");
    }

end:
    if(frg_ctrl_skb){
        dev_kfree_skb(frg_ctrl_skb);
    }
    spin_unlock_bh(&ecrnx_hw->rx_lock);
    return 0;
}
#endif

#if 0
static void sdio_rx_debug(struct sk_buff *skb)
{
    int i;
  
    if(!skb || !skb->len){
        ECRNX_DBG("skb error \n");
    }

    ECRNX_DBG("%s, len: 0x%x \n", __func__, skb->len);
    for(i = 0; i< skb->len; i++){
        printk("0x%02x ", skb->data[i]);
		if (i && (i % 16) == 0) {
			printk("\n");
		}
    }
}
#endif

int ecrnx_data_cfm_callback(void *priv, void *host_id)
{
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw*)priv;
    struct ipc_host_env_tag *env;
    env = ecrnx_hw->ipc_env;

    ecrnx_hw->data_tx_done++;
    if(!env || !env->pthis || host_id == 0)
    {
        ECRNX_ERR("ecrnx_data_cfm_callback input param error!! \n!");
        return -1;
    }
    spin_lock_bh(&((struct ecrnx_hw *)env->pthis)->tx_lock);
    env->cb.handle_data_cfm(env->pthis, host_id);
    spin_unlock_bh(&((struct ecrnx_hw *)env->pthis)->tx_lock);
    return 0;
}

int ecrnx_msg_cfm_callback(void *priv, void *host_id)
{
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw*)priv;
    struct ipc_host_env_tag *env;
    env = ecrnx_hw->ipc_env;

    if(!env || !env->pthis || host_id == 0)
    {
        ECRNX_ERR("ecrnx_msg_cfm_callback input param error!! \n!");
        return -1;
    }

    env->msga2e_hostid = NULL;
    ecrnx_hw->cmd_mgr.llind(&ecrnx_hw->cmd_mgr, (struct ecrnx_cmd *)host_id);
    return 0;
}

#ifdef CONFIG_ECRNX_ESWIN_SDIO
int ecrnx_rx_callback(void *priv, struct sk_buff *skb)
#elif defined(CONFIG_ECRNX_ESWIN_USB)
extern void usb_skb_debug(struct sk_buff *skb);
int ecrnx_rx_callback(void *priv, struct sk_buff *skb, uint8_t endpoint)
#endif
{
    //struct sk_buff *ret_skb = NULL;
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw*)priv;
    uint32_t frm_type = 0;
    //ECRNX_DBG("%s enter, skb_len: %d, skb: 0x%x !!", __func__, skb->len, skb);

	//print_hex_dump(KERN_DEBUG, "rx skb: ", DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len, false);

    if(!skb || !skb->data)
    {
        ECRNX_PRINT("sdio rx err error \n");
    }

     //usb_rx_debug(skb);
#ifdef CONFIG_ECRNX_ESWIN_SDIO
        frm_type = (skb->data[11] << 24) | (skb->data[10] << 16) | (skb->data[9] << 8) | skb->data[8];
#elif defined(CONFIG_ECRNX_ESWIN_USB)
		frm_type = (skb->data[3] << 24) | (skb->data[2] << 16) | (skb->data[1] << 8) | skb->data[0];
#endif

	//ECRNX_DBG(" frame_type: 0x%x, frame_len: %d, ecrnx_hw: 0x%x", frm_type, skb->len, ecrnx_hw);

#if defined(CONFIG_ECRNX_ESWIN_SDIO)
#ifdef CONFIG_ECRNX_WIFO_CAIL
	if (amt_mode == true) {
		sdio_host_amt_rx_handler(frm_type, skb);
	}
	else
#endif
	sdio_host_rx_handler( frm_type, ecrnx_hw->ipc_env, skb);
#elif defined(CONFIG_ECRNX_ESWIN_USB)
    if(MSG_TYPE_DATA == endpoint)
    {
        //msg is USB_FRM_TYPE_RXDESC
        usb_host_rx_handler( USB_FRM_TYPE_RXDESC, ecrnx_hw->ipc_env, skb); //current just used a point
    }
    else if(MSG_TYPE_CTRL == endpoint)
    {
#ifdef CONFIG_ECRNX_WIFO_CAIL
		if (amt_mode == true) {
			usb_host_amt_rx_handler(frm_type, skb);
		}
		else
#endif
        usb_host_rx_handler( frm_type, ecrnx_hw->ipc_env, skb);
    }
    else
    {
        ECRNX_ERR("endopint error \n");
    }
#endif
    //ECRNX_DBG("%s exit!!", __func__);
    return 0;
}
