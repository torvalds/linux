// SPDX-License-Identifier: GPL-2.0-or-later
/**
 ******************************************************************************
 *
 * @file rwnx_rx.c
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>

#include "rwnx_defs.h"
#include "rwnx_rx.h"
#include "rwnx_tx.h"
#include "rwnx_prof.h"
#include "ipc_host.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "aicwf_txrxif.h"
#ifdef AICWF_ARP_OFFLOAD
#include <linux/ip.h>
#include <linux/udp.h>
#include "rwnx_msg_tx.h"
#endif

#ifndef IEEE80211_MAX_CHAINS
#define IEEE80211_MAX_CHAINS 4
#endif

u8 dhcped = 0;

u16 tx_legrates_lut_rate[] = {
    10 ,
    20 ,
    55 ,
    110 ,
    60 ,
    90 ,
    120,
    180,
    240,
    360,
    480,
    540
};


u16 legrates_lut_rate[] = {
    10 ,
    20 ,
    55 ,
    110 ,
    0 ,
    0 ,
    0 ,
    0 ,
    480 ,
    240 ,
    120 ,
    60 ,
    540 ,
    360 ,
    180 ,
    90
};


const u8 legrates_lut[] = {
    0,                          /* 0 */
    1,                          /* 1 */
    2,                          /* 2 */
    3,                          /* 3 */
    -1,                         /* 4 */
    -1,                         /* 5 */
    -1,                         /* 6 */
    -1,                         /* 7 */
    10,                         /* 8 */
    8,                          /* 9 */
    6,                          /* 10 */
    4,                          /* 11 */
    11,                         /* 12 */
    9,                          /* 13 */
    7,                          /* 14 */
    5                           /* 15 */
};

struct vendor_radiotap_hdr {
    u8 oui[3];
    u8 subns;
    u16 len;
    u8 data[];
};

void rwnx_skb_align_8bytes(struct sk_buff *skb);


/**
 * rwnx_rx_get_vif - Return pointer to the destination vif
 *
 * @rwnx_hw: main driver data
 * @vif_idx: vif index present in rx descriptor
 *
 * Select the vif that should receive this frame. Returns NULL if the destination
 * vif is not active or vif is not specified in the descriptor.
 */
static inline
struct rwnx_vif *rwnx_rx_get_vif(struct rwnx_hw *rwnx_hw, int vif_idx)
{
    struct rwnx_vif *rwnx_vif = NULL;

    if (vif_idx < NX_VIRT_DEV_MAX) {
        rwnx_vif = rwnx_hw->vif_table[vif_idx];
        if (!rwnx_vif || !rwnx_vif->up)
            return NULL;
    }

    return rwnx_vif;
}

/**
 * rwnx_rx_vector_convert - Convert a legacy RX vector into a new RX vector format
 *
 * @rwnx_hw: main driver data.
 * @rx_vect1: Rx vector 1 descriptor of the received frame.
 * @rx_vect2: Rx vector 2 descriptor of the received frame.
 */
static void rwnx_rx_vector_convert(struct rwnx_hw *rwnx_hw,
                                   struct rx_vector_1 *rx_vect1,
                                   struct rx_vector_2 *rx_vect2)
{
    struct rx_vector_1_old rx_vect1_leg;
    struct rx_vector_2_old rx_vect2_leg;
    u32_l phy_vers = rwnx_hw->version_cfm.version_phy_2;

    // Check if we need to do the conversion. Only if old modem is used
    if (__MDM_MAJOR_VERSION(phy_vers) > 0) {
        rx_vect1->rssi1 = rx_vect1->rssi_leg;
        return;
    }

    // Copy the received vector locally
    memcpy(&rx_vect1_leg, rx_vect1, sizeof(struct rx_vector_1_old));

    // Reset it
    memset(rx_vect1, 0, sizeof(struct rx_vector_1));

    // Perform the conversion
    rx_vect1->format_mod = rx_vect1_leg.format_mod;
    rx_vect1->ch_bw = rx_vect1_leg.ch_bw;
    rx_vect1->antenna_set = rx_vect1_leg.antenna_set;
    rx_vect1->leg_length = rx_vect1_leg.leg_length;
    rx_vect1->leg_rate = rx_vect1_leg.leg_rate;
    rx_vect1->rssi1 = rx_vect1_leg.rssi1;

    switch (rx_vect1->format_mod) {
        case FORMATMOD_NON_HT:
        case FORMATMOD_NON_HT_DUP_OFDM:
            rx_vect1->leg.lsig_valid = rx_vect1_leg.lsig_valid;
            rx_vect1->leg.chn_bw_in_non_ht = rx_vect1_leg.num_extn_ss;
            rx_vect1->leg.dyn_bw_in_non_ht = rx_vect1_leg.dyn_bw;
            break;
        case FORMATMOD_HT_MF:
        case FORMATMOD_HT_GF:
            rx_vect1->ht.aggregation = rx_vect1_leg.aggregation;
            rx_vect1->ht.fec = rx_vect1_leg.fec_coding;
            rx_vect1->ht.lsig_valid = rx_vect1_leg.lsig_valid;
            rx_vect1->ht.length = rx_vect1_leg.ht_length;
            rx_vect1->ht.mcs = rx_vect1_leg.mcs;
            rx_vect1->ht.num_extn_ss = rx_vect1_leg.num_extn_ss;
            rx_vect1->ht.short_gi = rx_vect1_leg.short_gi;
            rx_vect1->ht.smoothing = rx_vect1_leg.smoothing;
            rx_vect1->ht.sounding = rx_vect1_leg.sounding;
            rx_vect1->ht.stbc = rx_vect1_leg.stbc;
            break;
        case FORMATMOD_VHT:
            rx_vect1->vht.beamformed = !rx_vect1_leg.smoothing;
            rx_vect1->vht.fec = rx_vect1_leg.fec_coding;
            rx_vect1->vht.length = rx_vect1_leg.ht_length | rx_vect1_leg._ht_length << 8;
            rx_vect1->vht.mcs = rx_vect1_leg.mcs & 0x0F;
            rx_vect1->vht.nss = rx_vect1_leg.stbc ? rx_vect1_leg.n_sts/2 : rx_vect1_leg.n_sts;
            rx_vect1->vht.doze_not_allowed = rx_vect1_leg.doze_not_allowed;
            rx_vect1->vht.short_gi = rx_vect1_leg.short_gi;
            rx_vect1->vht.sounding = rx_vect1_leg.sounding;
            rx_vect1->vht.stbc = rx_vect1_leg.stbc;
            rx_vect1->vht.group_id = rx_vect1_leg.group_id;
            rx_vect1->vht.partial_aid = rx_vect1_leg.partial_aid;
            rx_vect1->vht.first_user = rx_vect1_leg.first_user;
            break;
    }

    if (!rx_vect2)
        return;

    // Copy the received vector 2 locally
    memcpy(&rx_vect2_leg, rx_vect2, sizeof(struct rx_vector_2_old));

    // Reset it
    memset(rx_vect2, 0, sizeof(struct rx_vector_2));

    rx_vect2->rcpi1 = rx_vect2_leg.rcpi;
    rx_vect2->rcpi2 = rx_vect2_leg.rcpi;
    rx_vect2->rcpi3 = rx_vect2_leg.rcpi;
    rx_vect2->rcpi4 = rx_vect2_leg.rcpi;

    rx_vect2->evm1 = rx_vect2_leg.evm1;
    rx_vect2->evm2 = rx_vect2_leg.evm2;
    rx_vect2->evm3 = rx_vect2_leg.evm3;
    rx_vect2->evm4 = rx_vect2_leg.evm4;
}

/**
 * rwnx_rx_statistic - save some statistics about received frames
 *
 * @rwnx_hw: main driver data.
 * @hw_rxhdr: Rx Hardware descriptor of the received frame.
 * @sta: STA that sent the frame.
 */
static void rwnx_rx_statistic(struct rwnx_hw *rwnx_hw, struct hw_rxhdr *hw_rxhdr,
                              struct rwnx_sta *sta)
{
#if 1//def CONFIG_RWNX_DEBUGFS
    struct rwnx_stats *stats = &rwnx_hw->stats;
    struct rwnx_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
    struct rx_vector_1 *rxvect = &hw_rxhdr->hwvect.rx_vect1;
    int mpdu, ampdu, mpdu_prev, rate_idx;

    /* save complete hwvect */
    sta->stats.last_rx = hw_rxhdr->hwvect;

    /* update ampdu rx stats */
    mpdu = hw_rxhdr->hwvect.mpdu_cnt;
    ampdu = hw_rxhdr->hwvect.ampdu_cnt;
    mpdu_prev = stats->ampdus_rx_map[ampdu];

    if (mpdu_prev < mpdu ) {
        stats->ampdus_rx_miss += mpdu - mpdu_prev - 1;
    } else {
        stats->ampdus_rx[mpdu_prev]++;
    }
    stats->ampdus_rx_map[ampdu] = mpdu;

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
        int idx = legrates_lut[rxvect->leg_rate];
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
        wiphy_err(rwnx_hw->wiphy, "RX: Invalid index conversion => %d/%d\n",
                  rate_idx, rate_stats->size);
    }
#endif
}

/**
 * rwnx_rx_data_skb - Process one data frame
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: vif that received the buffer
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
#define RAISE_RX_SOFTIRQ() \
    cpu_raise_softirq(smp_processor_id(), NET_RX_SOFTIRQ)
#endif /* LINUX_VERSION_CODE  */

void rwnx_rx_data_skb_resend(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
							 struct sk_buff *skb,  struct hw_rxhdr *rxhdr)
{
	struct sk_buff *rx_skb = skb;
	//bool amsdu = rxhdr->flags_is_amsdu;
	const struct ethhdr *eth;
	struct sk_buff *skb_copy;

	rx_skb->dev = rwnx_vif->ndev;
	skb_reset_mac_header(rx_skb);
	eth = eth_hdr(rx_skb);

    //printk("resend\n");
	/* resend pkt on wireless interface */
	/* always need to copy buffer when forward=0 to get enough headrom for tsdesc */
	skb_copy = skb_copy_expand(rx_skb, sizeof(struct rwnx_txhdr) +
		RWNX_SWTXHDR_ALIGN_SZ + 3 + 24 + 8, 0, GFP_ATOMIC);

	if (skb_copy) {
		int res;
		skb_copy->protocol = htons(ETH_P_802_3);
		skb_reset_network_header(skb_copy);
		skb_reset_mac_header(skb_copy);

		rwnx_vif->is_resending = true;
		res = dev_queue_xmit(skb_copy);
		rwnx_vif->is_resending = false;
		/* note: buffer is always consummed by dev_queue_xmit */
		if (res == NET_XMIT_DROP) {
			rwnx_vif->net_stats.rx_dropped++;
			rwnx_vif->net_stats.tx_dropped++;
		} else if (res != NET_XMIT_SUCCESS) {
			netdev_err(rwnx_vif->ndev,
					   "Failed to re-send buffer to driver (res=%d)",
					   res);
			rwnx_vif->net_stats.tx_errors++;
		}
	} else {
		netdev_err(rwnx_vif->ndev, "Failed to copy skb");
	}
}
#ifdef AICWF_RX_REORDER
static void rwnx_rx_data_skb_forward(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
							 struct sk_buff *skb,  struct hw_rxhdr *rxhdr)
{
	struct sk_buff *rx_skb;

    rx_skb = skb;
	rx_skb->dev = rwnx_vif->ndev;
    skb_reset_mac_header(rx_skb);

	/* Update statistics */
	rwnx_vif->net_stats.rx_packets++;
	rwnx_vif->net_stats.rx_bytes += rx_skb->len;

    //printk("forward\n");
#ifdef CONFIG_BR_SUPPORT
    void *br_port = NULL;

    if (1) {//(check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) == _TRUE) {
        /* Insert NAT2.5 RX here! */
	#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
        br_port = rwnx_vif->ndev->br_port;
	#else
        rcu_read_lock();
        br_port = rcu_dereference(rwnx_vif->ndev->rx_handler_data);
        rcu_read_unlock();
	#endif

        if (br_port) {
            int nat25_handle_frame(struct rwnx_vif *vif, struct sk_buff *skb);

            if (nat25_handle_frame(rwnx_vif, rx_skb) == -1) {
                /* priv->ext_stats.rx_data_drops++; */
                /* DEBUG_ERR("RX DROP: nat25_handle_frame fail!\n"); */
                /* return FAIL; */

            }
        }
    }
#endif /* CONFIG_BR_SUPPORT */

	rwnx_skb_align_8bytes(rx_skb);

	rx_skb->protocol = eth_type_trans(rx_skb, rwnx_vif->ndev);
	memset(rx_skb->cb, 0, sizeof(rx_skb->cb));
	REG_SW_SET_PROFILING(rwnx_hw, SW_PROF_IEEE80211RX);
	#ifdef CONFIG_RX_NETIF_RECV_SKB //modify by aic
	netif_receive_skb(rx_skb);
	#else
	if (in_interrupt()) {
		netif_rx(rx_skb);
	} else {
	/*
	* If the receive is not processed inside an ISR, the softirqd must be woken explicitly to service the NET_RX_SOFTIRQ.
	* * In 2.6 kernels, this is handledby netif_rx_ni(), but in earlier kernels, we need to do it manually.
	*/
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
		netif_rx_ni(rx_skb);
	#else
		ulong flags;
		netif_rx(rx_skb);
		local_irq_save(flags);
		RAISE_RX_SOFTIRQ();
		local_irq_restore(flags);
	#endif
	}
	#endif
	REG_SW_CLEAR_PROFILING(rwnx_hw, SW_PROF_IEEE80211RX);

	rwnx_hw->stats.last_rx = jiffies;
}
#endif


static bool rwnx_rx_data_skb(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                             struct sk_buff *skb,  struct hw_rxhdr *rxhdr)
{
    struct sk_buff_head list;
    struct sk_buff *rx_skb;
    bool amsdu = rxhdr->flags_is_amsdu;
    bool resend = false, forward = true;

    skb->dev = rwnx_vif->ndev;

    __skb_queue_head_init(&list);

    if (amsdu) {
        int count;
        ieee80211_amsdu_to_8023s(skb, &list, rwnx_vif->ndev->dev_addr,
                                 RWNX_VIF_TYPE(rwnx_vif), 0, NULL, NULL);

        count = skb_queue_len(&list);
        if (count > ARRAY_SIZE(rwnx_hw->stats.amsdus_rx))
            count = ARRAY_SIZE(rwnx_hw->stats.amsdus_rx);
        rwnx_hw->stats.amsdus_rx[count - 1]++;
    } else {
        rwnx_hw->stats.amsdus_rx[0]++;
        __skb_queue_head(&list, skb);
    }

    if (((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP) ||
         (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) ||
         (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
        !(rwnx_vif->ap.flags & RWNX_AP_ISOLATE)) {
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
            if (rxhdr->flags_dst_idx != RWNX_INVALID_STA)
            {
                struct rwnx_sta *sta = &rwnx_hw->sta_table[rxhdr->flags_dst_idx];
                if (sta->valid && (sta->vlan_idx == rwnx_vif->vif_index))
                {
                    forward = false;
                    resend = true;
                }
            }
        }
    } else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT) {
        const struct ethhdr *eth;
        rx_skb = skb_peek(&list);
        skb_reset_mac_header(rx_skb);
        eth = eth_hdr(rx_skb);

        if (!is_multicast_ether_addr(eth->h_dest)) {
            /* unicast pkt for STA inside the BSS, no need to forward to upper
               layer simply resend on wireless interface */
            if (rxhdr->flags_dst_idx != RWNX_INVALID_STA)
            {
                forward = false;
                resend = true;
            }
        }
    }

    while (!skb_queue_empty(&list)) {
        rx_skb = __skb_dequeue(&list);

        /* resend pkt on wireless interface */
        if (resend) {
            struct sk_buff *skb_copy;
            /* always need to copy buffer when forward=0 to get enough headrom for tsdesc */
            skb_copy = skb_copy_expand(rx_skb, sizeof(struct rwnx_txhdr) +
                                       RWNX_SWTXHDR_ALIGN_SZ + 3 + 24 + 8, 0, GFP_ATOMIC);

            if (skb_copy) {
                int res;
                skb_copy->protocol = htons(ETH_P_802_3);
                skb_reset_network_header(skb_copy);
                skb_reset_mac_header(skb_copy);

                rwnx_vif->is_resending = true;
                res = dev_queue_xmit(skb_copy);
                rwnx_vif->is_resending = false;
                /* note: buffer is always consummed by dev_queue_xmit */
                if (res == NET_XMIT_DROP) {
                    rwnx_vif->net_stats.rx_dropped++;
                    rwnx_vif->net_stats.tx_dropped++;
                } else if (res != NET_XMIT_SUCCESS) {
                    netdev_err(rwnx_vif->ndev,
                               "Failed to re-send buffer to driver (res=%d)",
                               res);
                    rwnx_vif->net_stats.tx_errors++;
                }
            } else {
                netdev_err(rwnx_vif->ndev, "Failed to copy skb");
            }
        }

        /* forward pkt to upper layer */
        if (forward) {
#ifdef CONFIG_BR_SUPPORT
            void *br_port = NULL;


            /* Update statistics */
            rwnx_vif->net_stats.rx_packets++;
            rwnx_vif->net_stats.rx_bytes += rx_skb->len;

            if (1) {//(check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) == _TRUE) {
                /* Insert NAT2.5 RX here! */
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
                br_port = rwnx_vif->ndev->br_port;
#else
                rcu_read_lock();
                br_port = rcu_dereference(rwnx_vif->ndev->rx_handler_data);
                rcu_read_unlock();
#endif

                if (br_port) {
                    int nat25_handle_frame(struct rwnx_vif *vif, struct sk_buff *skb);

                    if (nat25_handle_frame(rwnx_vif, rx_skb) == -1) {
                        /* priv->ext_stats.rx_data_drops++; */
                        /* DEBUG_ERR("RX DROP: nat25_handle_frame fail!\n"); */
                        /* return FAIL; */
                    }
                }
            }
#endif /* CONFIG_BR_SUPPORT */

		rwnx_skb_align_8bytes(rx_skb);

        rx_skb->protocol = eth_type_trans(rx_skb, rwnx_vif->ndev);

#ifdef AICWF_ARP_OFFLOAD
            if(RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION || RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT)
                arpoffload_proc(rx_skb, rwnx_vif);
#endif
            memset(rx_skb->cb, 0, sizeof(rx_skb->cb));
            REG_SW_SET_PROFILING(rwnx_hw, SW_PROF_IEEE80211RX);
            #ifdef CONFIG_RX_NETIF_RECV_SKB //modify by aic
            netif_receive_skb(rx_skb);
            #else
            if (in_interrupt()) {
                netif_rx(rx_skb);
            } else {
            /*
            * If the receive is not processed inside an ISR, the softirqd must be woken explicitly to service the NET_RX_SOFTIRQ.
            * * In 2.6 kernels, this is handledby netif_rx_ni(), but in earlier kernels, we need to do it manually.
            */
            #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
                netif_rx_ni(rx_skb);
            #else
                ulong flags;
                netif_rx(rx_skb);
                local_irq_save(flags);
                RAISE_RX_SOFTIRQ();
                local_irq_restore(flags);
            #endif
            }
            #endif
            REG_SW_CLEAR_PROFILING(rwnx_hw, SW_PROF_IEEE80211RX);

            rwnx_hw->stats.last_rx = jiffies;
        }
    }

    return forward;
}

#ifdef CONFIG_HE_FOR_OLD_KERNEL
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
const u8 *cfg80211_find_ie_match(u8 eid, const u8 *ies, int len,
				 const u8 *match, int match_len,
				 int match_offset)
{
	const struct element *elem;

	/* match_offset can't be smaller than 2, unless match_len is
	 * zero, in which case match_offset must be zero as well.
	 */
	if (WARN_ON((match_len && match_offset < 2) ||
		    (!match_len && match_offset)))
		return NULL;

	for_each_element_id(elem, eid, ies, len) {
		if (elem->datalen >= match_offset - 2 + match_len &&
		    !memcmp(elem->data + match_offset - 2, match, match_len))
			return (void *)elem;
	}

	return NULL;
}
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))

static inline const u8 *cfg80211_find_ext_ie(u8 ext_eid, const u8* ies, int len)
{
        return cfg80211_find_ie_match(WLAN_EID_EXTENSION, ies, len,
                                                        &ext_eid, 1, 2);
}
#endif

#endif


/**
 * rwnx_rx_mgmt - Process one 802.11 management frame
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: vif to upload the buffer to
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Forward the management frame to a given interface.
 */
static void rwnx_rx_mgmt(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                         struct sk_buff *skb,  struct hw_rxhdr *hw_rxhdr)
{
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
    struct rx_vector_1 *rxvect = &hw_rxhdr->hwvect.rx_vect1;
#ifdef CONFIG_HE_FOR_OLD_KERNEL
	struct ieee80211_he_cap_elem *he;
#endif

    //printk("rwnx_rx_mgmt\n");
#if (defined CONFIG_HE_FOR_OLD_KERNEL) || (defined CONFIG_VHT_FOR_OLD_KERNEL)
	struct aic_sta *sta = &rwnx_hw->aic_table[rwnx_vif->ap.aic_index];
	const u8* ie;
	u32 len;

    if (ieee80211_is_assoc_req(mgmt->frame_control) && rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP) {
        printk("ASSOC_REQ: sta_idx %d MAC %pM\n", rwnx_vif->ap.aic_index, mgmt->sa);
        sta->sta_idx = rwnx_vif->ap.aic_index;
        len = skb->len - (mgmt->u.assoc_req.variable - skb->data);

        #ifdef CONFIG_HE_FOR_OLD_KERNEL
        ie = cfg80211_find_ext_ie(WLAN_EID_EXT_HE_CAPABILITY, mgmt->u.assoc_req.variable, len);
        if (ie && ie[1] >= sizeof(*he) + 1) {
            printk("assoc_req: find he\n");
            sta->he = true;
        }
        else {
            printk("assoc_req: no find he\n");
            sta->he = false;
        }
        #endif

        #ifdef CONFIG_VHT_FOR_OLD_KERNEL
        struct ieee80211_vht_cap *vht;
        ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, mgmt->u.assoc_req.variable, len);
        if (ie && ie[1] >= sizeof(*vht)) {
            printk("assoc_req: find vht\n");
            sta->vht = true;
        } else {
            printk("assoc_req: no find vht\n");
            sta->vht = false;
        }
        #endif
    }
#endif

    if (ieee80211_is_beacon(mgmt->frame_control)) {
        if ((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT) &&
            hw_rxhdr->flags_new_peer) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0))
            cfg80211_notify_new_peer_candidate(rwnx_vif->ndev, mgmt->sa,
                                               mgmt->u.beacon.variable,
                                               skb->len - offsetof(struct ieee80211_mgmt,
                                                                   u.beacon.variable),
                                               GFP_ATOMIC);
#else
            /* TODO: the value of parameter sig_dbm need to be confirmed */
            cfg80211_notify_new_peer_candidate(rwnx_vif->ndev, mgmt->sa,
                                               mgmt->u.beacon.variable,
                                               skb->len - offsetof(struct ieee80211_mgmt,
                                                                   u.beacon.variable),
                                               rxvect->rssi1, GFP_ATOMIC);
#endif
        } else {

    #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
            cfg80211_report_obss_beacon(rwnx_hw->wiphy, skb->data, skb->len,
                                        hw_rxhdr->phy_info.phy_prim20_freq,
                                        rxvect->rssi1, GFP_KERNEL);
    #else
            cfg80211_report_obss_beacon(rwnx_hw->wiphy, skb->data, skb->len,
                                        hw_rxhdr->phy_info.phy_prim20_freq,
                                        rxvect->rssi1);
    #endif
        }
    } else if ((ieee80211_is_deauth(mgmt->frame_control) ||
                ieee80211_is_disassoc(mgmt->frame_control)) &&
               (mgmt->u.deauth.reason_code == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
                mgmt->u.deauth.reason_code == WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA)) {
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)  // TODO: process unprot mgmt
        cfg80211_rx_unprot_mlme_mgmt(rwnx_vif->ndev, skb->data, skb->len);
        #endif
    } else if ((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) &&
               (ieee80211_is_action(mgmt->frame_control) &&
                (mgmt->u.action.category == 6))) {
        struct cfg80211_ft_event_params ft_event;
        ft_event.target_ap = (uint8_t *)&mgmt->u.action + ETH_ALEN + 2;
        ft_event.ies = (uint8_t *)&mgmt->u.action + ETH_ALEN * 2 + 2;
        ft_event.ies_len = skb->len - (ft_event.ies - (uint8_t *)mgmt);
        ft_event.ric_ies = NULL;
        ft_event.ric_ies_len = 0;
        cfg80211_ft_event(rwnx_vif->ndev, &ft_event);
    } else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
        cfg80211_rx_mgmt(&rwnx_vif->wdev, hw_rxhdr->phy_info.phy_prim20_freq,
                         rxvect->rssi1, skb->data, skb->len, 0);
#else
        cfg80211_rx_mgmt(rwnx_vif->ndev, hw_rxhdr->phy_info.phy_prim20_freq,
                         rxvect->rssi1, skb->data, skb->len, 0);
#endif
    }
}

/**
 * rwnx_rx_mgmt_any - Process one 802.11 management frame
 *
 * @rwnx_hw: main driver data
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Process the management frame and free the corresponding skb.
 * If vif is not specified in the rx descriptor, the the frame is uploaded
 * on all active vifs.
 */
static void rwnx_rx_mgmt_any(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
                             struct hw_rxhdr *hw_rxhdr)
{
    struct rwnx_vif *rwnx_vif;
    int vif_idx = hw_rxhdr->flags_vif_idx;
#ifdef CREATE_TRACE_POINTS
    trace_mgmt_rx(hw_rxhdr->phy_info.phy_prim20_freq, vif_idx,
                  hw_rxhdr->flags_sta_idx, (struct ieee80211_mgmt *)skb->data);
#endif

    if (vif_idx == RWNX_INVALID_VIF) {
        list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
            if (! rwnx_vif->up)
                continue;
            rwnx_rx_mgmt(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
        }
    } else {
        rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
        if (rwnx_vif)
            rwnx_rx_mgmt(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
    }

    dev_kfree_skb(skb);
}

/**
 * rwnx_rx_rtap_hdrlen - Return radiotap header length
 *
 * @rxvect: Rx vector used to fill the radiotap header
 * @has_vend_rtap: boolean indicating if vendor specific data is present
 *
 * Compute the length of the radiotap header based on @rxvect and vendor
 * specific data (if any).
 */
static u8 rwnx_rx_rtap_hdrlen(struct rx_vector_1 *rxvect,
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
 * rwnx_rx_add_rtap_hdr - Add radiotap header to sk_buff
 *
 * @rwnx_hw: main driver data
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
static void rwnx_rx_add_rtap_hdr(struct rwnx_hw* rwnx_hw,
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

    it_present = &rtap->it_present;

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
    if (hwvect && (!hwvect->frm_successful_rx))
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
                rwnx_hw->wiphy->bands[phy_info->phy_band];
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RATE);
        BUG_ON((rate_idx = legrates_lut[rxvect->leg_rate]) == -1);
        if (phy_info->phy_band == NL80211_BAND_5GHZ)
            rate_idx -= 4;  /* rwnx_ratetable_5ghz[0].hw_value == 4 */
        *pos = DIV_ROUND_UP(band->bitrates[rate_idx].bitrate, 5);
    }
    pos++;

    // IEEE80211_RADIOTAP_CHANNEL
    rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_CHANNEL);
    put_unaligned_le16(phy_info->phy_prim20_freq, pos);
    pos += 2;

    if (phy_info->phy_band == NL80211_BAND_5GHZ)
        put_unaligned_le16(IEEE80211_CHAN_OFDM | IEEE80211_CHAN_5GHZ, pos);
    else if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM)
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
    if ((rxvect->format_mod == FORMATMOD_HT_MF)
            || (rxvect->format_mod == FORMATMOD_HT_GF)) {
        rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_MCS);
        *pos++ = IEEE80211_RADIOTAP_MCS_HAVE_MCS |
                 IEEE80211_RADIOTAP_MCS_HAVE_GI |
                 IEEE80211_RADIOTAP_MCS_HAVE_BW;
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
        *pos = (rate_idx << 4) | vht_nss;
        pos += 4;
        if (fec_coding)
            #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
            *pos |= 0x01;
            #else
            *pos |= IEEE80211_RADIOTAP_CODING_LDPC_USER0;
            #endif
        pos++;
        // group ID
        pos++;
        // partial_aid
        pos += 2;
    }

    // Check for HE frames
    if (rxvect->format_mod == FORMATMOD_HE_SU) {
        struct ieee80211_radiotap_he he;
        #define HE_PREP(f, val) cpu_to_le16(FIELD_PREP(IEEE80211_RADIOTAP_HE_##f, val))
        #define D1_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_##f##_KNOWN)
        #define D2_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_##f##_KNOWN)

        he.data1 = D1_KNOWN(DATA_MCS) | D1_KNOWN(BSS_COLOR) | D1_KNOWN(BEAM_CHANGE) |
                   D1_KNOWN(UL_DL) | D1_KNOWN(CODING) |  D1_KNOWN(STBC) |
                   D1_KNOWN(BW_RU_ALLOC) | D1_KNOWN(DOPPLER) | D1_KNOWN(DATA_DCM);
        he.data2 = D2_KNOWN(GI) | D2_KNOWN(TXBF);

        if (stbc) {
            he.data6 |= HE_PREP(DATA6_NSTS, 2);
            he.data3 |= HE_PREP(DATA3_STBC, 1);
        } else {
            he.data6 |= HE_PREP(DATA6_NSTS, rxvect->he.nss);
        }

        he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
        he.data3 |= HE_PREP(DATA3_BEAM_CHANGE, rxvect->he.beam_change);
        he.data3 |= HE_PREP(DATA3_UL_DL, rxvect->he.uplink_flag);
        he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
        he.data3 |= HE_PREP(DATA3_DATA_MCS, rxvect->he.mcs);
        he.data3 |= HE_PREP(DATA3_DATA_DCM, rxvect->he.dcm);
        he.data3 |= HE_PREP(DATA3_CODING, rxvect->he.fec);

        he.data5 |= HE_PREP(DATA5_GI, rxvect->he.gi_type);
        he.data5 |= HE_PREP(DATA5_TXBF, rxvect->he.beamformed);
        he.data5 |= HE_PREP(DATA5_LTF_SIZE, rxvect->he.he_ltf_type + 1);

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

        he.data6 |= HE_PREP(DATA6_DOPPLER, rxvect->he.doppler);

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
 * rwnx_rx_monitor - Build radiotap header for skb an send it to netdev
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: vif that received the buffer
 * @skb: sk_buff received
 * @hw_rxhdr_ptr: Pointer to HW RX header
 * @rtap_len: Radiotap Header length
 *
 * Add radiotap header to the receved skb and send it to netdev
 */
static int rwnx_rx_monitor(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                           struct sk_buff *skb,  struct hw_rxhdr *hw_rxhdr_ptr,
                           u8 rtap_len)
{
    skb->dev = rwnx_vif->ndev;

    if (rwnx_vif->wdev.iftype != NL80211_IFTYPE_MONITOR) {
        netdev_err(rwnx_vif->ndev, "not a monitor vif\n");
        return -1;
    }

    /* Add RadioTap Header */
    rwnx_rx_add_rtap_hdr(rwnx_hw, skb, &hw_rxhdr_ptr->hwvect.rx_vect1,
                         &hw_rxhdr_ptr->phy_info, &hw_rxhdr_ptr->hwvect,
                         rtap_len, 0, 0);

    skb_reset_mac_header(skb);
    skb->ip_summed = CHECKSUM_UNNECESSARY;
    skb->pkt_type = PACKET_OTHERHOST;
    skb->protocol = htons(ETH_P_802_2);

    netif_receive_skb(skb);

    return 0;
}

#ifdef AICWF_ARP_OFFLOAD
void arpoffload_proc(struct sk_buff *skb, struct rwnx_vif *rwnx_vif)
{
    struct iphdr *iphead = (struct iphdr *)(skb->data);
    struct udphdr *udph;
    struct DHCPInfo *dhcph;

    if(skb->protocol == htons(ETH_P_IP)) { // IP
        if(iphead->protocol == IPPROTO_UDP) { // UDP
            udph = (struct udphdr *)((u8 *)iphead + (iphead->ihl << 2));
            if((udph->source == __constant_htons(SERVER_PORT))
                && (udph->dest == __constant_htons(CLIENT_PORT))) { // DHCP offset/ack
                dhcph =	(struct DHCPInfo *)((u8 *)udph + sizeof(struct udphdr));
                if(dhcph->cookie == htonl(DHCP_MAGIC) && dhcph->op == 2 &&
                    !memcmp(dhcph->chaddr, rwnx_vif->ndev->dev_addr, 6)) { // match magic word
                    u32 length = ntohs(udph->len) - sizeof(struct udphdr) - offsetof(struct DHCPInfo, options);
                    u16 offset = 0;
                    u8 *option = dhcph->options;
                    while (option[offset]!= DHCP_OPTION_END && offset<length) {
                        if (option[offset] == DHCP_OPTION_MESSAGE_TYPE) {
                            if (option[offset+2] == DHCP_ACK) {
                                dhcped = 1;
                                if(rwnx_vif->sta.group_cipher_type == WLAN_CIPHER_SUITE_CCMP)
                                    rwnx_send_arpoffload_en_req(rwnx_vif->rwnx_hw, rwnx_vif, dhcph->yiaddr, 1);
                                else
                                    rwnx_send_arpoffload_en_req(rwnx_vif->rwnx_hw, rwnx_vif, dhcph->yiaddr, 0);
                             }
                        }
                        offset += 2 + option[offset+1];
                    }
                }
            }
        }
    }
}
#endif

#ifdef AICWF_RX_REORDER
void reord_rxframe_free(spinlock_t *lock, struct list_head *q, struct list_head *list)
{
    spin_lock_bh(lock);
    list_add(list, q);
    spin_unlock_bh(lock);
}

struct recv_msdu *reord_rxframe_alloc(spinlock_t *lock, struct list_head *q)
{
    struct recv_msdu *rxframe;

    spin_lock_bh(lock);
    if (list_empty(q)) {
        spin_unlock_bh(lock);
        return NULL;
    }
    rxframe = list_entry(q->next, struct recv_msdu, rxframe_list);
    list_del_init(q->next);
    spin_unlock_bh(lock);
    return rxframe;
}

struct reord_ctrl_info *reord_init_sta(struct aicwf_rx_priv* rx_priv, const u8 *mac_addr)
{
    u8 i = 0;
    struct reord_ctrl *preorder_ctrl = NULL;
    struct reord_ctrl_info *reord_info;
#ifdef AICWF_SDIO_SUPPORT
    struct aicwf_bus *bus_if = rx_priv->sdiodev->bus_if;
#else
    struct aicwf_bus *bus_if = rx_priv->usbdev->bus_if;
#endif

    if (bus_if->state == BUS_DOWN_ST || rx_priv == NULL) {
        printk("bad stat!\n");
        return NULL;
    }

    reord_info = kmalloc(sizeof(struct reord_ctrl_info), GFP_ATOMIC);
    if (!reord_info)
        return NULL;

    memcpy(reord_info->mac_addr, mac_addr, ETH_ALEN);
    for (i=0; i < 8; i++) {
        preorder_ctrl = &reord_info->preorder_ctrl[i];
        preorder_ctrl->enable = true;
        preorder_ctrl->ind_sn = 0xffff;
        preorder_ctrl->wsize_b = AICWF_REORDER_WINSIZE;
        preorder_ctrl->rx_priv= rx_priv;
        INIT_LIST_HEAD(&preorder_ctrl->reord_list);
        spin_lock_init(&preorder_ctrl->reord_list_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        init_timer(&preorder_ctrl->reord_timer);
        preorder_ctrl->reord_timer.data = (ulong) preorder_ctrl;
        preorder_ctrl->reord_timer.function = reord_timeout_handler;
#else
        timer_setup(&preorder_ctrl->reord_timer, reord_timeout_handler, 0);
#endif
        INIT_WORK(&preorder_ctrl->reord_timer_work, reord_timeout_worker);
    }

    return reord_info;
}

int reord_flush_tid(struct aicwf_rx_priv *rx_priv, struct sk_buff *skb, u8 tid)
{
    struct reord_ctrl_info *reord_info;
    struct reord_ctrl *preorder_ctrl;
    struct rwnx_vif *rwnx_vif = (struct rwnx_vif *)rx_priv->rwnx_vif;
    struct ethhdr *eh = (struct ethhdr *)(skb->data);
    u8 *mac;
    unsigned long flags;
    u8 found = 0;
    struct list_head *phead, *plist;
    struct recv_msdu *prframe;
    int ret;

    if((rwnx_vif->wdev.iftype == NL80211_IFTYPE_STATION) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT))
        mac = eh->h_dest;
    else if((rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO))
        mac = eh->h_source;
    else {
        printk("error mode:%d!\n", rwnx_vif->wdev.iftype);
        dev_kfree_skb(skb);
        return -1;
    }

    spin_lock_bh(&rx_priv->stas_reord_lock);
    list_for_each_entry(reord_info, &rx_priv->stas_reord_list, list) {
        if (!memcmp(mac, reord_info->mac_addr, ETH_ALEN)) {
            found = 1;
            preorder_ctrl = &reord_info->preorder_ctrl[tid];
            break;
        }
    }
    if (!found) {
        spin_unlock_bh(&rx_priv->stas_reord_lock);
        return 0;
    }
    spin_unlock_bh(&rx_priv->stas_reord_lock);

    if(preorder_ctrl->enable == false)
        return 0;
    spin_lock_irqsave(&preorder_ctrl->reord_list_lock, flags);
    phead = &preorder_ctrl->reord_list;
    while (1) {
        if (list_empty(phead)) {
            break;
        }
        plist = phead->next;
        prframe = list_entry(plist, struct recv_msdu, reord_pending_list);
        reord_single_frame_ind(rx_priv, prframe);
        list_del_init(&(prframe->reord_pending_list));
    }

	AICWFDBG(LOGINFO, "flush:tid=%d", tid);
    preorder_ctrl->enable = false;
    spin_unlock_irqrestore(&preorder_ctrl->reord_list_lock, flags);
    if (timer_pending(&preorder_ctrl->reord_timer))
        ret = del_timer_sync(&preorder_ctrl->reord_timer);
    cancel_work_sync(&preorder_ctrl->reord_timer_work);

    return 0;
}

void reord_deinit_sta(struct aicwf_rx_priv* rx_priv, struct reord_ctrl_info *reord_info)
{
    u8 i = 0;
    //unsigned long flags;
    struct reord_ctrl *preorder_ctrl = NULL;
    int ret;

    if (rx_priv == NULL) {
        txrx_err("bad rx_priv!\n");
        return;
    }

	AICWFDBG(LOGINFO, "%s\n", __func__);

    for (i=0; i < 8; i++) {
        struct recv_msdu *req, *next;
        preorder_ctrl = &reord_info->preorder_ctrl[i];
		if(preorder_ctrl->enable){
			preorder_ctrl->enable = false;
	        if (timer_pending(&preorder_ctrl->reord_timer)) {
	            ret = del_timer_sync(&preorder_ctrl->reord_timer);
	        }
	        cancel_work_sync(&preorder_ctrl->reord_timer_work);
		}

        spin_lock_bh(&preorder_ctrl->reord_list_lock);
        list_for_each_entry_safe(req, next, &preorder_ctrl->reord_list, reord_pending_list) {
            list_del_init(&req->reord_pending_list);
            if(req->pkt != NULL)
                dev_kfree_skb(req->pkt);
            req->pkt = NULL;
            reord_rxframe_free(&rx_priv->freeq_lock, &rx_priv->rxframes_freequeue, &req->rxframe_list);
        }

		AICWFDBG(LOGINFO, "reord dinit in_irq():%d in_atomic:%d in_softirq:%d\r\n", (int)in_irq()
			,(int)in_atomic(), (int)in_softirq());
        spin_unlock_bh(&preorder_ctrl->reord_list_lock);
    }

    spin_lock_bh(&rx_priv->stas_reord_lock);
    list_del(&reord_info->list);
    spin_unlock_bh(&rx_priv->stas_reord_lock);
    kfree(reord_info);
}

int reord_single_frame_ind(struct aicwf_rx_priv *rx_priv, struct recv_msdu *prframe)
{
    struct list_head *rxframes_freequeue = NULL;
    struct sk_buff *skb = NULL;
    struct rwnx_vif *rwnx_vif = (struct rwnx_vif *)rx_priv->rwnx_vif;

    rxframes_freequeue = &rx_priv->rxframes_freequeue;
    skb = prframe->pkt;
    if (skb == NULL) {
        txrx_err("skb is NULL\n");
        return -1;
    }

	if(!prframe->forward) {
		//printk("single: %d not forward: drop\n", prframe->seq_num);
		dev_kfree_skb(skb);
		prframe->pkt = NULL;
		reord_rxframe_free(&rx_priv->freeq_lock, rxframes_freequeue, &prframe->rxframe_list);
		return 0;
	}

    skb->data = prframe->rx_data;
    skb_set_tail_pointer(skb, prframe->len);
    skb->len = prframe->len;
    //printk("netif sn=%d, len=%d\n", precv_frame->attrib.seq_num, skb->len);

#ifdef CONFIG_BR_SUPPORT
    void *br_port = NULL;

    if (1) {//(check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) == _TRUE) {
        /* Insert NAT2.5 RX here! */
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
        br_port = rwnx_vif->ndev->br_port;
#else
        rcu_read_lock();
        br_port = rcu_dereference(rwnx_vif->ndev->rx_handler_data);
        rcu_read_unlock();
#endif

        if (br_port) {
            int nat25_handle_frame(struct rwnx_vif *vif, struct sk_buff *skb);

            if (nat25_handle_frame(rwnx_vif, skb) == -1) {
                /* priv->ext_stats.rx_data_drops++; */
                /* DEBUG_ERR("RX DROP: nat25_handle_frame fail!\n"); */
                /* return FAIL; */
            }
        }
    }
#endif /* CONFIG_BR_SUPPORT */

    skb->dev = rwnx_vif->ndev;

#if 0
	if(test_log_flag){
		if(skb->data[42] == 0x80){
			printk("AIDEN : SN:%d R_SN:%d pid:%d\r\n",
				prframe->seq_num, (skb->data[44] << 8| skb->data[45]), current->pid);
			}
	}
#endif
	rwnx_skb_align_8bytes(skb);

    skb->protocol = eth_type_trans(skb, rwnx_vif->ndev);

#ifdef AICWF_ARP_OFFLOAD
    if(RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION || RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
        arpoffload_proc(skb, rwnx_vif);
    }
#endif
    memset(skb->cb, 0, sizeof(skb->cb));

#ifdef CONFIG_RX_NETIF_RECV_SKB//AIDEN test
	netif_receive_skb(skb);
#else
    if (in_interrupt()) {
        netif_rx(skb);
    } else {
    /*
    * If the receive is not processed inside an ISR, the softirqd must be woken explicitly to service the NET_RX_SOFTIRQ.
    * * In 2.6 kernels, this is handledby netif_rx_ni(), but in earlier kernels, we need to do it manually.
    */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
    netif_rx_ni(skb);
#else
    ulong flags;
    netif_rx(skb);
    local_irq_save(flags);
    RAISE_RX_SOFTIRQ();
    local_irq_restore(flags);
#endif
    }
#endif//CONFIG_RX_NETIF_RECV_SKB
    rwnx_vif->net_stats.rx_packets++;
    rwnx_vif->net_stats.rx_bytes += skb->len;
    prframe->pkt = NULL;
    reord_rxframe_free(&rx_priv->freeq_lock, rxframes_freequeue, &prframe->rxframe_list);

    return 0;
}

bool reord_rxframes_process(struct aicwf_rx_priv *rx_priv, struct reord_ctrl *preorder_ctrl, int bforced)
{
    struct list_head *phead, *plist;
    struct recv_msdu *prframe;
    bool bPktInBuf = false;

    if (bforced == true) {
        phead = &preorder_ctrl->reord_list;
        if (list_empty(phead)) {
            return false;
        }

        plist = phead->next;
        prframe = list_entry(plist, struct recv_msdu, reord_pending_list);
        preorder_ctrl->ind_sn = prframe->seq_num;
    }

    phead = &preorder_ctrl->reord_list;
    if (list_empty(phead)) {
        return bPktInBuf;
    }

    list_for_each_entry(prframe, phead, reord_pending_list) {
        if (!SN_LESS(preorder_ctrl->ind_sn, prframe->seq_num)) {
            if (SN_EQUAL(preorder_ctrl->ind_sn, prframe->seq_num)) {
                preorder_ctrl->ind_sn = (preorder_ctrl->ind_sn + 1) & 0xFFF;
            }
        } else {
            bPktInBuf = true;
            break;
        }
    }

    return bPktInBuf;
}

void reord_rxframes_ind(struct aicwf_rx_priv *rx_priv,
    struct reord_ctrl *preorder_ctrl)
{
    struct list_head *phead, *plist;
    struct recv_msdu *prframe;

	//spin_lock_bh(&preorder_ctrl->reord_list_lock);//AIDEN
    phead = &preorder_ctrl->reord_list;
    while (1) {
        //spin_lock_bh(&preorder_ctrl->reord_list_lock);
        if (list_empty(phead)) {
            //spin_unlock_bh(&preorder_ctrl->reord_list_lock);
            break;
        }

        plist = phead->next;
        prframe = list_entry(plist, struct recv_msdu, reord_pending_list);

        if (!SN_LESS(preorder_ctrl->ind_sn, prframe->seq_num)) {
            list_del_init(&(prframe->reord_pending_list));
            //spin_unlock_bh(&preorder_ctrl->reord_list_lock);
			reord_single_frame_ind(rx_priv, prframe);
        } else {
            //spin_unlock_bh(&preorder_ctrl->reord_list_lock);
            break;
        }
    }
	//spin_unlock_bh(&preorder_ctrl->reord_list_lock);//AIDEN
}

int reorder_timeout = REORDER_UPDATE_TIME;
module_param(reorder_timeout, int, 0660);


#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
void reord_timeout_handler (ulong data)
#else
void reord_timeout_handler (struct timer_list *t)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	struct reord_ctrl *preorder_ctrl = (struct reord_ctrl *)data;
#else
	struct reord_ctrl *preorder_ctrl = from_timer(preorder_ctrl, t, reord_timer);
#endif

	AICWFDBG(LOGTRACE, "%s Enter \r\n", __func__);


	if (g_rwnx_plat->usbdev->state == USB_DOWN_ST) {
        usb_err("bus is down\n");
        return;
	}
#if 0//AIDEN
    struct aicwf_rx_priv *rx_priv = preorder_ctrl->rx_priv;

    if (reord_rxframes_process(rx_priv, preorder_ctrl, true)==true) {
        mod_timer(&preorder_ctrl->reord_timer, jiffies + msecs_to_jiffies(reorder_timeout/*REORDER_UPDATE_TIME*/));
    }
#endif

    if(!work_pending(&preorder_ctrl->reord_timer_work))
        schedule_work(&preorder_ctrl->reord_timer_work);

}

void reord_timeout_worker(struct work_struct *work)
{
    struct reord_ctrl *preorder_ctrl = container_of(work, struct reord_ctrl, reord_timer_work);
    struct aicwf_rx_priv *rx_priv = preorder_ctrl->rx_priv;

	spin_lock_bh(&preorder_ctrl->reord_list_lock);
#if 1//AIDEN
	if (reord_rxframes_process(rx_priv, preorder_ctrl, true)==true) {
		mod_timer(&preorder_ctrl->reord_timer, jiffies + msecs_to_jiffies(reorder_timeout/*REORDER_UPDATE_TIME*/));
	}
#endif

    reord_rxframes_ind(rx_priv, preorder_ctrl);
	spin_unlock_bh(&preorder_ctrl->reord_list_lock);

    return ;
}

int reord_process_unit(struct aicwf_rx_priv *rx_priv, struct sk_buff *skb, u16 seq_num, u8 tid, u8 forward)
{
    int ret=0;
    u8 *mac;
    struct recv_msdu *pframe;
    struct reord_ctrl *preorder_ctrl;
    struct reord_ctrl_info *reord_info;
    struct rwnx_vif *rwnx_vif = (struct rwnx_vif *)rx_priv->rwnx_vif;
    struct ethhdr *eh = (struct ethhdr *)(skb->data);
    //u8 *da = eh->h_dest;
    //u8 is_mcast = ((*da) & 0x01)? 1 : 0;

    if (rwnx_vif == NULL || skb->len <= 14) {
        dev_kfree_skb(skb);
        return -1;
    }

    pframe = reord_rxframe_alloc(&rx_priv->freeq_lock, &rx_priv->rxframes_freequeue);
    if (!pframe) {
        dev_kfree_skb(skb);
        return -1;
    }

    INIT_LIST_HEAD(&pframe->reord_pending_list);
    pframe->seq_num = seq_num;
    pframe->tid = tid;
    pframe->rx_data = skb->data;
    pframe->len = skb->len;
    pframe->pkt = skb;
	pframe->forward = forward;
    preorder_ctrl = pframe->preorder_ctrl;

#if 0
    if ((ntohs(eh->h_proto) == ETH_P_PAE) || is_mcast){
		printk("%s AIDEN pframe->seq_num:%d is bcast or mcast\r\n", __func__, pframe->seq_num);
        ret = reord_single_frame_ind(rx_priv, pframe);
		return ret;
    }
#endif

    if((rwnx_vif->wdev.iftype == NL80211_IFTYPE_STATION) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT))
        mac = eh->h_dest;
    else if((rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO))
        mac = eh->h_source;
    else {
        dev_kfree_skb(skb);
        return -1;
    }

    spin_lock_bh(&rx_priv->stas_reord_lock);
    list_for_each_entry(reord_info, &rx_priv->stas_reord_list, list) {
        if (!memcmp(mac, reord_info->mac_addr, ETH_ALEN)) {
            preorder_ctrl = &reord_info->preorder_ctrl[pframe->tid];
            break;
        }
    }


    if (&reord_info->list == &rx_priv->stas_reord_list) {//first time???
        reord_info = reord_init_sta(rx_priv, mac);
		//reord_info has 8 preorder_ctrl,
		//There is a one-to-one matched preorder_ctrl and tid
        if (!reord_info) {
            spin_unlock_bh(&rx_priv->stas_reord_lock);
            dev_kfree_skb(skb);
            return -1;
        }
        list_add_tail(&reord_info->list, &rx_priv->stas_reord_list);
        preorder_ctrl = &reord_info->preorder_ctrl[pframe->tid];
    } else {
        if(preorder_ctrl->enable == false) {
            preorder_ctrl->enable = true;
            preorder_ctrl->ind_sn = 0xffff;
            preorder_ctrl->wsize_b = AICWF_REORDER_WINSIZE;
            preorder_ctrl->rx_priv= rx_priv;
        }
    }
    spin_unlock_bh(&rx_priv->stas_reord_lock);

    if (preorder_ctrl->enable == false) {
        preorder_ctrl->ind_sn = pframe->seq_num;
		spin_lock_bh(&preorder_ctrl->reord_list_lock);//AIDEN
        reord_single_frame_ind(rx_priv, pframe);
		spin_unlock_bh(&preorder_ctrl->reord_list_lock);//AIDEN
        preorder_ctrl->ind_sn = (preorder_ctrl->ind_sn + 1)%4096;
        return 0;
    }

    spin_lock_bh(&preorder_ctrl->reord_list_lock);
    if (reord_need_check(preorder_ctrl, pframe->seq_num)) {
		if(pframe->rx_data[42] == 0x80){//this is rtp package
			if(pframe->seq_num == preorder_ctrl->ind_sn){
				printk("%s pframe->seq_num1:%d \r\n", __func__, pframe->seq_num);
	        	reord_single_frame_ind(rx_priv, pframe);//not need to reorder
			}else{
				printk("%s free pframe->seq_num:%d \r\n", __func__, pframe->seq_num);
			    if (pframe->pkt){
			        dev_kfree_skb(pframe->pkt);
			        pframe->pkt = NULL;
			    }
			   	reord_rxframe_free(&rx_priv->freeq_lock, &rx_priv->rxframes_freequeue, &pframe->rxframe_list);
			}
		}else{
			//printk("%s pframe->seq_num2:%d \r\n", __func__, pframe->seq_num);
			reord_single_frame_ind(rx_priv, pframe);//not need to reorder
		}

        spin_unlock_bh(&preorder_ctrl->reord_list_lock);
		return 0;
    }

    if (reord_rxframe_enqueue(preorder_ctrl, pframe)) {
        spin_unlock_bh(&preorder_ctrl->reord_list_lock);
        goto fail;
    }

    if (reord_rxframes_process(rx_priv, preorder_ctrl, false) == true) {
        if (!timer_pending(&preorder_ctrl->reord_timer)) {//if this timer not countdown, mod_timer timer to start count down
            ret = mod_timer(&preorder_ctrl->reord_timer, jiffies + msecs_to_jiffies(reorder_timeout/*REORDER_UPDATE_TIME*/));
        }
    } else {
		if(timer_pending(&preorder_ctrl->reord_timer)) {
	        	ret = del_timer(&preorder_ctrl->reord_timer);
		}
    }

	reord_rxframes_ind(rx_priv, preorder_ctrl);

    spin_unlock_bh(&preorder_ctrl->reord_list_lock);



    return 0;

fail:
    if (pframe->pkt){
        dev_kfree_skb(pframe->pkt);
        pframe->pkt = NULL;
    }
   	reord_rxframe_free(&rx_priv->freeq_lock, &rx_priv->rxframes_freequeue, &pframe->rxframe_list);
    return ret;
}

int reord_need_check(struct reord_ctrl *preorder_ctrl, u16 seq_num)
{
    u8 wsize = preorder_ctrl->wsize_b;
    u16 wend = (preorder_ctrl->ind_sn + wsize -1) & 0xFFF;//0xFFF: 12 bits for seq num

	//first time: wend = 0 + 64 - 1= 63

    if (preorder_ctrl->ind_sn == 0xFFFF) {//first time
        preorder_ctrl->ind_sn = seq_num;
    }

    if( SN_LESS(seq_num, preorder_ctrl->ind_sn)) {//first time seq_num = preorder_ctrl->ind_sn
        return -1;//no need to reord
    }

    if (SN_EQUAL(seq_num, preorder_ctrl->ind_sn)) {
        preorder_ctrl->ind_sn = (preorder_ctrl->ind_sn + 1) & 0xFFF;
    } else if (SN_LESS(wend, seq_num)) {
        if (seq_num >= (wsize-1))
            preorder_ctrl->ind_sn = seq_num-(wsize-1);
        else
            preorder_ctrl->ind_sn = 0xFFF - (wsize - (seq_num + 1)) + 1;
    }

    return 0;
}

int reord_rxframe_enqueue(struct reord_ctrl *preorder_ctrl, struct recv_msdu *prframe)
{
    struct list_head *preord_list = &preorder_ctrl->reord_list;
    struct list_head *phead, *plist;
    struct recv_msdu *pnextrframe;

//first time:not any prframe in preord_list, so phead = phead->next
    phead = preord_list;
    plist = phead->next;

    while(phead != plist) {
        pnextrframe = list_entry(plist, struct recv_msdu, reord_pending_list);
        if(SN_LESS(pnextrframe->seq_num, prframe->seq_num))	{
            plist = plist->next;
            continue;
        } else if(SN_EQUAL(pnextrframe->seq_num, prframe->seq_num)) {
            return -1;
        } else {
            break;
        }
    }

	//link prframe in plist
    list_add_tail(&(prframe->reord_pending_list), plist);

    return 0;
}
#endif /* AICWF_RX_REORDER */

void remove_sec_hdr_mgmt_frame(struct hw_rxhdr *hw_rxhdr,struct sk_buff *skb)
{
    u8 hdr_len = 24;
    u8 mgmt_header[24] = {0};

    if(!hw_rxhdr->hwvect.ga_frame){
        if(((skb->data[0] & 0x0C) == 0) && (skb->data[1] & 0x40) == 0x40){ //protect management frame
            printk("frame type %x\n",skb->data[0]);
            if(hw_rxhdr->hwvect.decr_status == RWNX_RX_HD_DECR_CCMP128){
                memcpy(mgmt_header,skb->data,hdr_len);
                skb_pull(skb,8);
                memcpy(skb->data,mgmt_header,hdr_len);
                hw_rxhdr->hwvect.len -= 8;
            }
            else {
                printk("unsupport decr_status:%d\n",hw_rxhdr->hwvect.decr_status);
            }
        }
    }
}

u8 rwnx_rxdataind_aicwf(struct rwnx_hw *rwnx_hw, void *hostid, void *rx_priv)
{
    struct hw_rxhdr *hw_rxhdr;
    struct rxdesc_tag *rxdesc = NULL;
    struct rwnx_vif *rwnx_vif;
    struct sk_buff *skb  = hostid;
    int msdu_offset = sizeof(struct hw_rxhdr) + 2;
    u16_l status = 0;
    u8 hdr_len = 24;
    u8 ra[MAC_ADDR_LEN] = {0};
    u8 ta[MAC_ADDR_LEN] = {0};
    u8 ether_type[2] = {0};
    u8 pull_len = 0;
    u8 tid = 0;
    u8 is_qos = 0;
#ifdef AICWF_RX_REORDER
    struct aicwf_rx_priv *rx_priv_tmp;
	u16 seq_num = 0;
    bool resend = false;
    bool forward = false;
#endif

    REG_SW_SET_PROFILING(rwnx_hw, SW_PROF_RWNXDATAIND);
    hw_rxhdr = (struct hw_rxhdr *)skb->data;

#ifdef AICWF_RX_REORDER
    if(hw_rxhdr->is_monitor_vif) {
        status = RX_STAT_MONITOR;
        //printk("monitor rx\n");
    }
#endif

    if(hw_rxhdr->flags_upload)
        status |= RX_STAT_FORWARD;

    /* Check if we need to delete the buffer */
    if (status & RX_STAT_DELETE) {
        /* Remove the SK buffer from the rxbuf_elems table */
    #if 0
        rwnx_ipc_rxbuf_elem_pull(rwnx_hw, skb);
    #endif
        /* Free the buffer */
        dev_kfree_skb(skb);
        goto end;
    }

    /* Check if we need to forward the buffer coming from a monitor interface */
    if (status & RX_STAT_MONITOR) {
        struct sk_buff *skb_monitor = NULL;
        struct hw_rxhdr hw_rxhdr_copy;
        u8 rtap_len;
        u16 frm_len = 0;

        //Check if monitor interface exists and is open
        rwnx_vif = rwnx_rx_get_vif(rwnx_hw, rwnx_hw->monitor_vif);
        if (!rwnx_vif) {
            dev_err(rwnx_hw->dev, "Received monitor frame but there is no monitor interface open\n");
            goto check_len_update;
        }

        rwnx_rx_vector_convert(rwnx_hw,
                               &hw_rxhdr->hwvect.rx_vect1,
                               &hw_rxhdr->hwvect.rx_vect2);
        rtap_len = rwnx_rx_rtap_hdrlen(&hw_rxhdr->hwvect.rx_vect1, false);

        if (status == RX_STAT_MONITOR) 
        {
            /* Remove the SK buffer from the rxbuf_elems table. It will also
               unmap the buffer and then sync the buffer for the cpu */
            //rwnx_ipc_rxbuf_elem_pull(rwnx_hw, skb);
            skb->data += (msdu_offset + 2); //sdio/usb word allign

            //Save frame length
            frm_len = le32_to_cpu(hw_rxhdr->hwvect.len);

            // Reserve space for frame
            skb->len = frm_len;

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
        } else {
        #ifdef CONFIG_RWNX_MON_DATA
        skb_monitor = skb_copy_expand(skb, rtap_len, 0, GFP_ATOMIC);
        skb_monitor->data += (msdu_offset + 2); //sdio/usb word allign

        //Save frame length
        frm_len = le32_to_cpu(hw_rxhdr->hwvect.len);
        #endif
        }

        skb_reset_tail_pointer(skb_monitor);
        skb_monitor->len = 0;
        skb_put(skb_monitor, frm_len);

        if (rwnx_rx_monitor(rwnx_hw, rwnx_vif, skb_monitor, hw_rxhdr, rtap_len))
            dev_kfree_skb(skb_monitor);

        if (status == RX_STAT_MONITOR) {
            if (skb_monitor != skb) {
                dev_kfree_skb(skb);
            }
        }
    }

check_len_update:
    /* Check if we need to update the length */
    if (status & RX_STAT_LEN_UPDATE) {
        if (rxdesc){
            hw_rxhdr->hwvect.len = rxdesc->frame_len;
        }

        if (status & RX_STAT_ETH_LEN_UPDATE) {
            /* Update Length Field inside the Ethernet Header */
            struct ethhdr *hdr = (struct ethhdr *)((u8 *)hw_rxhdr + msdu_offset);

			if (rxdesc){
            	hdr->h_proto = htons(rxdesc->frame_len - sizeof(struct ethhdr));
			}
        }

        goto end;
    }

    /* Check if it must be discarded after informing upper layer */
    if (status & RX_STAT_SPURIOUS) {
        struct ieee80211_hdr *hdr;

        hdr = (struct ieee80211_hdr *)(skb->data + msdu_offset);
        rwnx_vif = rwnx_rx_get_vif(rwnx_hw, hw_rxhdr->flags_vif_idx);
        if (rwnx_vif) {
            cfg80211_rx_spurious_frame(rwnx_vif->ndev, hdr->addr2, GFP_ATOMIC);
        }
        goto end;
    }

    /* Check if we need to forward the buffer */
    if (status & RX_STAT_FORWARD) {
        rwnx_rx_vector_convert(rwnx_hw,
                               &hw_rxhdr->hwvect.rx_vect1,
                               &hw_rxhdr->hwvect.rx_vect2);
        skb_pull(skb, msdu_offset + 2); //+2 since sdio allign 58->60

	if(skb->data[1] & 0x80)//htc
		hdr_len += 4;

        if((skb->data[0] & 0x0f)==0x08) {
            if((skb->data[0] & 0x80) == 0x80) {//qos data
                hdr_len += 2;//802.11 mac header len
                tid = skb->data[24] & 0x0F;
                is_qos = 1;
            }

            if((skb->data[1] & 0x3) == 0x1)  {// to ds
                memcpy(ra, &skb->data[16], MAC_ADDR_LEN);//destination addr
                memcpy(ta, &skb->data[10], MAC_ADDR_LEN);//source addr
            } else if((skb->data[1] & 0x3) == 0x2) { //from ds
                memcpy(ta, &skb->data[16], MAC_ADDR_LEN);//destination addr
                memcpy(ra, &skb->data[4], MAC_ADDR_LEN);//BSSID
            }

            pull_len += (hdr_len + 8);
#ifdef AICWF_RX_REORDER
            seq_num = ((skb->data[22]&0xf0)>>4) | (skb->data[23]<<4);
#endif
            switch(hw_rxhdr->hwvect.decr_status)
            {
                case RWNX_RX_HD_DECR_CCMP128:
                    pull_len += 8;//ccmp_header
                    //skb_pull(&skb->data[skb->len-8], 8); //ccmp_mic_len
                    memcpy(ether_type, &skb->data[hdr_len + 6 + 8], 2);
                    break;
                case RWNX_RX_HD_DECR_TKIP:
                    pull_len += 8;//tkip_header
                    memcpy(ether_type, &skb->data[hdr_len + 6 + 8], 2);
                    break;
                case RWNX_RX_HD_DECR_WEP:
                    pull_len += 4;//wep_header
                    memcpy(ether_type, &skb->data[hdr_len + 6 + 4], 2);
                    break;

                default:
                    memcpy(ether_type, &skb->data[hdr_len + 6], 2);
                    break;
            }

            skb_pull(skb, pull_len);
            skb_push(skb, 14);
			//fill 802.3 header
            memcpy(skb->data, ra, MAC_ADDR_LEN);//destination addr
            memcpy(&skb->data[6], ta, MAC_ADDR_LEN);//source addr
            memcpy(&skb->data[12], ether_type, 2);//802.3 type
        }

        if (hw_rxhdr->flags_is_80211_mpdu) {
            remove_sec_hdr_mgmt_frame(hw_rxhdr,skb);
            rwnx_rx_mgmt_any(rwnx_hw, skb, hw_rxhdr);
        } else {
            rwnx_vif = rwnx_rx_get_vif(rwnx_hw, hw_rxhdr->flags_vif_idx);

            if (!rwnx_vif) {
                dev_err(rwnx_hw->dev, "Frame received but no active vif (%d)",
                        hw_rxhdr->flags_vif_idx);
                dev_kfree_skb(skb);
                goto end;
            }

            if (hw_rxhdr->flags_sta_idx != RWNX_INVALID_STA) {
                struct rwnx_sta *sta;

                sta = &rwnx_hw->sta_table[hw_rxhdr->flags_sta_idx];
                rwnx_rx_statistic(rwnx_hw, hw_rxhdr, sta);

                if (sta->vlan_idx != rwnx_vif->vif_index) {
                    rwnx_vif = rwnx_hw->vif_table[sta->vlan_idx];
                    if (!rwnx_vif) {
                        dev_kfree_skb(skb);
                        goto end;
                    }
                }

                if (hw_rxhdr->flags_is_4addr && !rwnx_vif->use_4addr) {
                    cfg80211_rx_unexpected_4addr_frame(rwnx_vif->ndev,
                                                       sta->mac_addr, GFP_ATOMIC);
                }
            }

            skb->priority = 256 + tid;//hw_rxhdr->flags_user_prio;

#ifdef AICWF_RX_REORDER
	     rx_priv_tmp = rx_priv;
            rx_priv_tmp->rwnx_vif = (void *)rwnx_vif;

	    if( (rwnx_vif->wdev.iftype == NL80211_IFTYPE_STATION) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) ){
            if(is_qos && hw_rxhdr->flags_need_reord){
                reord_process_unit((struct aicwf_rx_priv *)rx_priv, skb, seq_num, tid, 1);
            }else if(is_qos  && !hw_rxhdr->flags_need_reord) {
                 reord_flush_tid((struct aicwf_rx_priv *)rx_priv, skb, tid);
                if (!rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr))
                    dev_kfree_skb(skb);
            }
            else {
                if (!rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr))
                    dev_kfree_skb(skb);
            }
	    } else if( (rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO) ) {
                #if 1
                const struct ethhdr *eth;
                resend = false;
				forward = true;
                skb_reset_mac_header(skb);
                eth = eth_hdr(skb);
                //printk("da:%pM, %x,%x, len=%d\n", eth->h_dest, skb->data[12], skb->data[13], skb->len);

                if (unlikely(is_multicast_ether_addr(eth->h_dest))) {
                    /* broadcast pkt need to be forwared to upper layer and resent
                       on wireless interface */
                    resend = true;
                } else {
                    /* unicast pkt for STA inside the BSS, no need to forward to upper
                       layer simply resend on wireless interface */
                    if (hw_rxhdr->flags_dst_idx != RWNX_INVALID_STA) {
                        struct rwnx_sta *sta = &rwnx_hw->sta_table[hw_rxhdr->flags_dst_idx];
                        if (sta->valid && (sta->vlan_idx == rwnx_vif->vif_index)) {
                            resend = true;
                            forward = false;
                        }
                    }
                }

                if(resend){
                    rwnx_rx_data_skb_resend(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
                }

                if(forward) {
                    if (is_qos && hw_rxhdr->flags_need_reord)
                        reord_process_unit((struct aicwf_rx_priv *)rx_priv, skb, seq_num, tid, 1);
                    else if (is_qos  && !hw_rxhdr->flags_need_reord) {
                        reord_flush_tid((struct aicwf_rx_priv *)rx_priv, skb, tid);
                        rwnx_rx_data_skb_forward(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
                    } else
                        rwnx_rx_data_skb_forward(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
                } else if(resend) {
					if (is_qos && hw_rxhdr->flags_need_reord)
						reord_process_unit((struct aicwf_rx_priv *)rx_priv, skb, seq_num, tid, 0);
					else if (is_qos  && !hw_rxhdr->flags_need_reord) {
						reord_flush_tid((struct aicwf_rx_priv *)rx_priv, skb, tid);
						dev_kfree_skb(skb);
					}
				} else
                    dev_kfree_skb(skb);
                #else
                if (!rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr))
					dev_kfree_skb(skb);
                #endif
			}
#else
            if (!rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr))
                    dev_kfree_skb(skb);
#endif
        }
    }

end:
    REG_SW_CLEAR_PROFILING(rwnx_hw, SW_PROF_RWNXDATAIND);
    return 0;
}

