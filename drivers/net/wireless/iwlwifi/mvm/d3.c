/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/fs.h>
#include <net/cfg80211.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/addrconf.h>
#include "iwl-modparams.h"
#include "fw-api.h"
#include "mvm.h"

void iwl_mvm_set_rekey_data(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct cfg80211_gtk_rekey_data *data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (iwlwifi_mod_params.sw_crypto)
		return;

	mutex_lock(&mvm->mutex);

	memcpy(mvmvif->rekey_data.kek, data->kek, NL80211_KEK_LEN);
	memcpy(mvmvif->rekey_data.kck, data->kck, NL80211_KCK_LEN);
	mvmvif->rekey_data.replay_ctr =
		cpu_to_le64(be64_to_cpup((__be64 *)&data->replay_ctr));
	mvmvif->rekey_data.valid = true;

	mutex_unlock(&mvm->mutex);
}

#if IS_ENABLED(CONFIG_IPV6)
void iwl_mvm_ipv6_addr_change(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct inet6_dev *idev)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct inet6_ifaddr *ifa;
	int idx = 0;

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		mvmvif->target_ipv6_addrs[idx] = ifa->addr;
		idx++;
		if (idx >= IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX)
			break;
	}
	read_unlock_bh(&idev->lock);

	mvmvif->num_target_ipv6_addrs = idx;
}
#endif

void iwl_mvm_set_default_unicast_key(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif, int idx)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->tx_key_idx = idx;
}

static void iwl_mvm_convert_p1k(u16 *p1k, __le16 *out)
{
	int i;

	for (i = 0; i < IWL_P1K_SIZE; i++)
		out[i] = cpu_to_le16(p1k[i]);
}

struct wowlan_key_data {
	struct iwl_wowlan_rsc_tsc_params_cmd *rsc_tsc;
	struct iwl_wowlan_tkip_params_cmd *tkip;
	bool error, use_rsc_tsc, use_tkip;
	int wep_key_idx;
};

static void iwl_mvm_wowlan_program_keys(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct ieee80211_key_conf *key,
					void *_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct wowlan_key_data *data = _data;
	struct aes_sc *aes_sc, *aes_tx_sc = NULL;
	struct tkip_sc *tkip_sc, *tkip_tx_sc = NULL;
	struct iwl_p1k_cache *rx_p1ks;
	u8 *rx_mic_key;
	struct ieee80211_key_seq seq;
	u32 cur_rx_iv32 = 0;
	u16 p1k[IWL_P1K_SIZE];
	int ret, i;

	mutex_lock(&mvm->mutex);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104: { /* hack it for now */
		struct {
			struct iwl_mvm_wep_key_cmd wep_key_cmd;
			struct iwl_mvm_wep_key wep_key;
		} __packed wkc = {
			.wep_key_cmd.mac_id_n_color =
				cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
								mvmvif->color)),
			.wep_key_cmd.num_keys = 1,
			/* firmware sets STA_KEY_FLG_WEP_13BYTES */
			.wep_key_cmd.decryption_type = STA_KEY_FLG_WEP,
			.wep_key.key_index = key->keyidx,
			.wep_key.key_size = key->keylen,
		};

		/*
		 * This will fail -- the key functions don't set support
		 * pairwise WEP keys. However, that's better than silently
		 * failing WoWLAN. Or maybe not?
		 */
		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
			break;

		memcpy(&wkc.wep_key.key[3], key->key, key->keylen);
		if (key->keyidx == mvmvif->tx_key_idx) {
			/* TX key must be at offset 0 */
			wkc.wep_key.key_offset = 0;
		} else {
			/* others start at 1 */
			data->wep_key_idx++;
			wkc.wep_key.key_offset = data->wep_key_idx;
		}

		ret = iwl_mvm_send_cmd_pdu(mvm, WEP_KEY, CMD_SYNC,
					   sizeof(wkc), &wkc);
		data->error = ret != 0;

		mvm->ptk_ivlen = key->iv_len;
		mvm->ptk_icvlen = key->icv_len;
		mvm->gtk_ivlen = key->iv_len;
		mvm->gtk_icvlen = key->icv_len;

		/* don't upload key again */
		goto out_unlock;
	}
	default:
		data->error = true;
		goto out_unlock;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		/*
		 * Ignore CMAC keys -- the WoWLAN firmware doesn't support them
		 * but we also shouldn't abort suspend due to that. It does have
		 * support for the IGTK key renewal, but doesn't really use the
		 * IGTK for anything. This means we could spuriously wake up or
		 * be deauthenticated, but that was considered acceptable.
		 */
		goto out_unlock;
	case WLAN_CIPHER_SUITE_TKIP:
		if (sta) {
			tkip_sc = data->rsc_tsc->all_tsc_rsc.tkip.unicast_rsc;
			tkip_tx_sc = &data->rsc_tsc->all_tsc_rsc.tkip.tsc;

			rx_p1ks = data->tkip->rx_uni;

			ieee80211_get_key_tx_seq(key, &seq);
			tkip_tx_sc->iv16 = cpu_to_le16(seq.tkip.iv16);
			tkip_tx_sc->iv32 = cpu_to_le32(seq.tkip.iv32);

			ieee80211_get_tkip_p1k_iv(key, seq.tkip.iv32, p1k);
			iwl_mvm_convert_p1k(p1k, data->tkip->tx.p1k);

			memcpy(data->tkip->mic_keys.tx,
			       &key->key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY],
			       IWL_MIC_KEY_SIZE);

			rx_mic_key = data->tkip->mic_keys.rx_unicast;
		} else {
			tkip_sc =
				data->rsc_tsc->all_tsc_rsc.tkip.multicast_rsc;
			rx_p1ks = data->tkip->rx_multi;
			rx_mic_key = data->tkip->mic_keys.rx_mcast;
		}

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 (as they need to to avoid replay attacks)
		 * for checking the IV in the frames.
		 */
		for (i = 0; i < IWL_NUM_RSC; i++) {
			ieee80211_get_key_rx_seq(key, i, &seq);
			tkip_sc[i].iv16 = cpu_to_le16(seq.tkip.iv16);
			tkip_sc[i].iv32 = cpu_to_le32(seq.tkip.iv32);
			/* wrapping isn't allowed, AP must rekey */
			if (seq.tkip.iv32 > cur_rx_iv32)
				cur_rx_iv32 = seq.tkip.iv32;
		}

		ieee80211_get_tkip_rx_p1k(key, vif->bss_conf.bssid,
					  cur_rx_iv32, p1k);
		iwl_mvm_convert_p1k(p1k, rx_p1ks[0].p1k);
		ieee80211_get_tkip_rx_p1k(key, vif->bss_conf.bssid,
					  cur_rx_iv32 + 1, p1k);
		iwl_mvm_convert_p1k(p1k, rx_p1ks[1].p1k);

		memcpy(rx_mic_key,
		       &key->key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY],
		       IWL_MIC_KEY_SIZE);

		data->use_tkip = true;
		data->use_rsc_tsc = true;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (sta) {
			u8 *pn = seq.ccmp.pn;

			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.unicast_rsc;
			aes_tx_sc = &data->rsc_tsc->all_tsc_rsc.aes.tsc;

			ieee80211_get_key_tx_seq(key, &seq);
			aes_tx_sc->pn = cpu_to_le64((u64)pn[5] |
						    ((u64)pn[4] << 8) |
						    ((u64)pn[3] << 16) |
						    ((u64)pn[2] << 24) |
						    ((u64)pn[1] << 32) |
						    ((u64)pn[0] << 40));
		} else {
			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.multicast_rsc;
		}

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 for checking the IV in the frames.
		 */
		for (i = 0; i < IWL_NUM_RSC; i++) {
			u8 *pn = seq.ccmp.pn;

			ieee80211_get_key_rx_seq(key, i, &seq);
			aes_sc->pn = cpu_to_le64((u64)pn[5] |
						 ((u64)pn[4] << 8) |
						 ((u64)pn[3] << 16) |
						 ((u64)pn[2] << 24) |
						 ((u64)pn[1] << 32) |
						 ((u64)pn[0] << 40));
		}
		data->use_rsc_tsc = true;
		break;
	}

	/*
	 * The D3 firmware hardcodes the key offset 0 as the key it uses
	 * to transmit packets to the AP, i.e. the PTK.
	 */
	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
		key->hw_key_idx = 0;
		mvm->ptk_ivlen = key->iv_len;
		mvm->ptk_icvlen = key->icv_len;
	} else {
		/*
		 * firmware only supports TSC/RSC for a single key,
		 * so if there are multiple keep overwriting them
		 * with new ones -- this relies on mac80211 doing
		 * list_add_tail().
		 */
		key->hw_key_idx = 1;
		mvm->gtk_ivlen = key->iv_len;
		mvm->gtk_icvlen = key->icv_len;
	}

	ret = iwl_mvm_set_sta_key(mvm, vif, sta, key, true);
	data->error = ret != 0;
out_unlock:
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_send_patterns(struct iwl_mvm *mvm,
				 struct cfg80211_wowlan *wowlan)
{
	struct iwl_wowlan_patterns_cmd *pattern_cmd;
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_PATTERNS,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
		.flags = CMD_SYNC,
	};
	int i, err;

	if (!wowlan->n_patterns)
		return 0;

	cmd.len[0] = sizeof(*pattern_cmd) +
		wowlan->n_patterns * sizeof(struct iwl_wowlan_pattern);

	pattern_cmd = kmalloc(cmd.len[0], GFP_KERNEL);
	if (!pattern_cmd)
		return -ENOMEM;

	pattern_cmd->n_patterns = cpu_to_le32(wowlan->n_patterns);

	for (i = 0; i < wowlan->n_patterns; i++) {
		int mask_len = DIV_ROUND_UP(wowlan->patterns[i].pattern_len, 8);

		memcpy(&pattern_cmd->patterns[i].mask,
		       wowlan->patterns[i].mask, mask_len);
		memcpy(&pattern_cmd->patterns[i].pattern,
		       wowlan->patterns[i].pattern,
		       wowlan->patterns[i].pattern_len);
		pattern_cmd->patterns[i].mask_size = mask_len;
		pattern_cmd->patterns[i].pattern_size =
			wowlan->patterns[i].pattern_len;
	}

	cmd.data[0] = pattern_cmd;
	err = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(pattern_cmd);
	return err;
}

static int iwl_mvm_send_proto_offload(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif)
{
	union {
		struct iwl_proto_offload_cmd_v1 v1;
		struct iwl_proto_offload_cmd_v2 v2;
		struct iwl_proto_offload_cmd_v3_small v3s;
		struct iwl_proto_offload_cmd_v3_large v3l;
	} cmd = {};
	struct iwl_host_cmd hcmd = {
		.id = PROT_OFFLOAD_CONFIG_CMD,
		.flags = CMD_SYNC,
		.data[0] = &cmd,
		.dataflags[0] = IWL_HCMD_DFL_DUP,
	};
	struct iwl_proto_offload_cmd_common *common;
	u32 enabled = 0, size;
	u32 capa_flags = mvm->fw->ucode_capa.flags;
#if IS_ENABLED(CONFIG_IPV6)
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int i;

	if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL ||
	    capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE) {
		struct iwl_ns_config *nsc;
		struct iwl_targ_addr *addrs;
		int n_nsc, n_addrs;
		int c;

		if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL) {
			nsc = cmd.v3s.ns_config;
			n_nsc = IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3S;
			addrs = cmd.v3s.targ_addrs;
			n_addrs = IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3S;
		} else {
			nsc = cmd.v3l.ns_config;
			n_nsc = IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3L;
			addrs = cmd.v3l.targ_addrs;
			n_addrs = IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3L;
		}

		if (mvmvif->num_target_ipv6_addrs)
			enabled |= IWL_D3_PROTO_OFFLOAD_NS;

		/*
		 * For each address we have (and that will fit) fill a target
		 * address struct and combine for NS offload structs with the
		 * solicited node addresses.
		 */
		for (i = 0, c = 0;
		     i < mvmvif->num_target_ipv6_addrs &&
		     i < n_addrs && c < n_nsc; i++) {
			struct in6_addr solicited_addr;
			int j;

			addrconf_addr_solict_mult(&mvmvif->target_ipv6_addrs[i],
						  &solicited_addr);
			for (j = 0; j < c; j++)
				if (ipv6_addr_cmp(&nsc[j].dest_ipv6_addr,
						  &solicited_addr) == 0)
					break;
			if (j == c)
				c++;
			addrs[i].addr = mvmvif->target_ipv6_addrs[i];
			addrs[i].config_num = cpu_to_le32(j);
			nsc[j].dest_ipv6_addr = solicited_addr;
			memcpy(nsc[j].target_mac_addr, vif->addr, ETH_ALEN);
		}

		if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL)
			cmd.v3s.num_valid_ipv6_addrs = cpu_to_le32(i);
		else
			cmd.v3l.num_valid_ipv6_addrs = cpu_to_le32(i);
	} else if (capa_flags & IWL_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS) {
		if (mvmvif->num_target_ipv6_addrs) {
			enabled |= IWL_D3_PROTO_OFFLOAD_NS;
			memcpy(cmd.v2.ndp_mac_addr, vif->addr, ETH_ALEN);
		}

		BUILD_BUG_ON(sizeof(cmd.v2.target_ipv6_addr[0]) !=
			     sizeof(mvmvif->target_ipv6_addrs[0]));

		for (i = 0; i < min(mvmvif->num_target_ipv6_addrs,
				    IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V2); i++)
			memcpy(cmd.v2.target_ipv6_addr[i],
			       &mvmvif->target_ipv6_addrs[i],
			       sizeof(cmd.v2.target_ipv6_addr[i]));
	} else {
		if (mvmvif->num_target_ipv6_addrs) {
			enabled |= IWL_D3_PROTO_OFFLOAD_NS;
			memcpy(cmd.v1.ndp_mac_addr, vif->addr, ETH_ALEN);
		}

		BUILD_BUG_ON(sizeof(cmd.v1.target_ipv6_addr[0]) !=
			     sizeof(mvmvif->target_ipv6_addrs[0]));

		for (i = 0; i < min(mvmvif->num_target_ipv6_addrs,
				    IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V1); i++)
			memcpy(cmd.v1.target_ipv6_addr[i],
			       &mvmvif->target_ipv6_addrs[i],
			       sizeof(cmd.v1.target_ipv6_addr[i]));
	}
#endif

	if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL) {
		common = &cmd.v3s.common;
		size = sizeof(cmd.v3s);
	} else if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE) {
		common = &cmd.v3l.common;
		size = sizeof(cmd.v3l);
	} else if (capa_flags & IWL_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS) {
		common = &cmd.v2.common;
		size = sizeof(cmd.v2);
	} else {
		common = &cmd.v1.common;
		size = sizeof(cmd.v1);
	}

	if (vif->bss_conf.arp_addr_cnt) {
		enabled |= IWL_D3_PROTO_OFFLOAD_ARP;
		common->host_ipv4_addr = vif->bss_conf.arp_addr_list[0];
		memcpy(common->arp_mac_addr, vif->addr, ETH_ALEN);
	}

	if (!enabled)
		return 0;

	common->enabled = cpu_to_le32(enabled);

	hcmd.len[0] = size;
	return iwl_mvm_send_cmd(mvm, &hcmd);
}

enum iwl_mvm_tcp_packet_type {
	MVM_TCP_TX_SYN,
	MVM_TCP_RX_SYNACK,
	MVM_TCP_TX_DATA,
	MVM_TCP_RX_ACK,
	MVM_TCP_RX_WAKE,
	MVM_TCP_TX_FIN,
};

static __le16 pseudo_hdr_check(int len, __be32 saddr, __be32 daddr)
{
	__sum16 check = tcp_v4_check(len, saddr, daddr, 0);
	return cpu_to_le16(be16_to_cpu((__force __be16)check));
}

static void iwl_mvm_build_tcp_packet(struct ieee80211_vif *vif,
				     struct cfg80211_wowlan_tcp *tcp,
				     void *_pkt, u8 *mask,
				     __le16 *pseudo_hdr_csum,
				     enum iwl_mvm_tcp_packet_type ptype)
{
	struct {
		struct ethhdr eth;
		struct iphdr ip;
		struct tcphdr tcp;
		u8 data[];
	} __packed *pkt = _pkt;
	u16 ip_tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr);
	int i;

	pkt->eth.h_proto = cpu_to_be16(ETH_P_IP),
	pkt->ip.version = 4;
	pkt->ip.ihl = 5;
	pkt->ip.protocol = IPPROTO_TCP;

	switch (ptype) {
	case MVM_TCP_TX_SYN:
	case MVM_TCP_TX_DATA:
	case MVM_TCP_TX_FIN:
		memcpy(pkt->eth.h_dest, tcp->dst_mac, ETH_ALEN);
		memcpy(pkt->eth.h_source, vif->addr, ETH_ALEN);
		pkt->ip.ttl = 128;
		pkt->ip.saddr = tcp->src;
		pkt->ip.daddr = tcp->dst;
		pkt->tcp.source = cpu_to_be16(tcp->src_port);
		pkt->tcp.dest = cpu_to_be16(tcp->dst_port);
		/* overwritten for TX SYN later */
		pkt->tcp.doff = sizeof(struct tcphdr) / 4;
		pkt->tcp.window = cpu_to_be16(65000);
		break;
	case MVM_TCP_RX_SYNACK:
	case MVM_TCP_RX_ACK:
	case MVM_TCP_RX_WAKE:
		memcpy(pkt->eth.h_dest, vif->addr, ETH_ALEN);
		memcpy(pkt->eth.h_source, tcp->dst_mac, ETH_ALEN);
		pkt->ip.saddr = tcp->dst;
		pkt->ip.daddr = tcp->src;
		pkt->tcp.source = cpu_to_be16(tcp->dst_port);
		pkt->tcp.dest = cpu_to_be16(tcp->src_port);
		break;
	default:
		WARN_ON(1);
		return;
	}

	switch (ptype) {
	case MVM_TCP_TX_SYN:
		/* firmware assumes 8 option bytes - 8 NOPs for now */
		memset(pkt->data, 0x01, 8);
		ip_tot_len += 8;
		pkt->tcp.doff = (sizeof(struct tcphdr) + 8) / 4;
		pkt->tcp.syn = 1;
		break;
	case MVM_TCP_TX_DATA:
		ip_tot_len += tcp->payload_len;
		memcpy(pkt->data, tcp->payload, tcp->payload_len);
		pkt->tcp.psh = 1;
		pkt->tcp.ack = 1;
		break;
	case MVM_TCP_TX_FIN:
		pkt->tcp.fin = 1;
		pkt->tcp.ack = 1;
		break;
	case MVM_TCP_RX_SYNACK:
		pkt->tcp.syn = 1;
		pkt->tcp.ack = 1;
		break;
	case MVM_TCP_RX_ACK:
		pkt->tcp.ack = 1;
		break;
	case MVM_TCP_RX_WAKE:
		ip_tot_len += tcp->wake_len;
		pkt->tcp.psh = 1;
		pkt->tcp.ack = 1;
		memcpy(pkt->data, tcp->wake_data, tcp->wake_len);
		break;
	}

	switch (ptype) {
	case MVM_TCP_TX_SYN:
	case MVM_TCP_TX_DATA:
	case MVM_TCP_TX_FIN:
		pkt->ip.tot_len = cpu_to_be16(ip_tot_len);
		pkt->ip.check = ip_fast_csum(&pkt->ip, pkt->ip.ihl);
		break;
	case MVM_TCP_RX_WAKE:
		for (i = 0; i < DIV_ROUND_UP(tcp->wake_len, 8); i++) {
			u8 tmp = tcp->wake_mask[i];
			mask[i + 6] |= tmp << 6;
			if (i + 1 < DIV_ROUND_UP(tcp->wake_len, 8))
				mask[i + 7] = tmp >> 2;
		}
		/* fall through for ethernet/IP/TCP headers mask */
	case MVM_TCP_RX_SYNACK:
	case MVM_TCP_RX_ACK:
		mask[0] = 0xff; /* match ethernet */
		/*
		 * match ethernet, ip.version, ip.ihl
		 * the ip.ihl half byte is really masked out by firmware
		 */
		mask[1] = 0x7f;
		mask[2] = 0x80; /* match ip.protocol */
		mask[3] = 0xfc; /* match ip.saddr, ip.daddr */
		mask[4] = 0x3f; /* match ip.daddr, tcp.source, tcp.dest */
		mask[5] = 0x80; /* match tcp flags */
		/* leave rest (0 or set for MVM_TCP_RX_WAKE) */
		break;
	};

	*pseudo_hdr_csum = pseudo_hdr_check(ip_tot_len - sizeof(struct iphdr),
					    pkt->ip.saddr, pkt->ip.daddr);
}

static int iwl_mvm_send_remote_wake_cfg(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct cfg80211_wowlan_tcp *tcp)
{
	struct iwl_wowlan_remote_wake_config *cfg;
	struct iwl_host_cmd cmd = {
		.id = REMOTE_WAKE_CONFIG_CMD,
		.len = { sizeof(*cfg), },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
		.flags = CMD_SYNC,
	};
	int ret;

	if (!tcp)
		return 0;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;
	cmd.data[0] = cfg;

	cfg->max_syn_retries = 10;
	cfg->max_data_retries = 10;
	cfg->tcp_syn_ack_timeout = 1; /* seconds */
	cfg->tcp_ack_timeout = 1; /* seconds */

	/* SYN (TX) */
	iwl_mvm_build_tcp_packet(
		vif, tcp, cfg->syn_tx.data, NULL,
		&cfg->syn_tx.info.tcp_pseudo_header_checksum,
		MVM_TCP_TX_SYN);
	cfg->syn_tx.info.tcp_payload_length = 0;

	/* SYN/ACK (RX) */
	iwl_mvm_build_tcp_packet(
		vif, tcp, cfg->synack_rx.data, cfg->synack_rx.rx_mask,
		&cfg->synack_rx.info.tcp_pseudo_header_checksum,
		MVM_TCP_RX_SYNACK);
	cfg->synack_rx.info.tcp_payload_length = 0;

	/* KEEPALIVE/ACK (TX) */
	iwl_mvm_build_tcp_packet(
		vif, tcp, cfg->keepalive_tx.data, NULL,
		&cfg->keepalive_tx.info.tcp_pseudo_header_checksum,
		MVM_TCP_TX_DATA);
	cfg->keepalive_tx.info.tcp_payload_length =
		cpu_to_le16(tcp->payload_len);
	cfg->sequence_number_offset = tcp->payload_seq.offset;
	/* length must be 0..4, the field is little endian */
	cfg->sequence_number_length = tcp->payload_seq.len;
	cfg->initial_sequence_number = cpu_to_le32(tcp->payload_seq.start);
	cfg->keepalive_interval = cpu_to_le16(tcp->data_interval);
	if (tcp->payload_tok.len) {
		cfg->token_offset = tcp->payload_tok.offset;
		cfg->token_length = tcp->payload_tok.len;
		cfg->num_tokens =
			cpu_to_le16(tcp->tokens_size % tcp->payload_tok.len);
		memcpy(cfg->tokens, tcp->payload_tok.token_stream,
		       tcp->tokens_size);
	} else {
		/* set tokens to max value to almost never run out */
		cfg->num_tokens = cpu_to_le16(65535);
	}

	/* ACK (RX) */
	iwl_mvm_build_tcp_packet(
		vif, tcp, cfg->keepalive_ack_rx.data,
		cfg->keepalive_ack_rx.rx_mask,
		&cfg->keepalive_ack_rx.info.tcp_pseudo_header_checksum,
		MVM_TCP_RX_ACK);
	cfg->keepalive_ack_rx.info.tcp_payload_length = 0;

	/* WAKEUP (RX) */
	iwl_mvm_build_tcp_packet(
		vif, tcp, cfg->wake_rx.data, cfg->wake_rx.rx_mask,
		&cfg->wake_rx.info.tcp_pseudo_header_checksum,
		MVM_TCP_RX_WAKE);
	cfg->wake_rx.info.tcp_payload_length =
		cpu_to_le16(tcp->wake_len);

	/* FIN */
	iwl_mvm_build_tcp_packet(
		vif, tcp, cfg->fin_tx.data, NULL,
		&cfg->fin_tx.info.tcp_pseudo_header_checksum,
		MVM_TCP_TX_FIN);
	cfg->fin_tx.info.tcp_payload_length = 0;

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(cfg);

	return ret;
}

struct iwl_d3_iter_data {
	struct iwl_mvm *mvm;
	struct ieee80211_vif *vif;
	bool error;
};

static void iwl_mvm_d3_iface_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_d3_iter_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return;

	if (mvmvif->ap_sta_id == IWL_MVM_STATION_COUNT)
		return;

	if (data->vif) {
		IWL_ERR(data->mvm, "More than one managed interface active!\n");
		data->error = true;
		return;
	}

	data->vif = vif;
}

static int iwl_mvm_d3_reprogram(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct ieee80211_sta *ap_sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_chanctx_conf *ctx;
	u8 chains_static, chains_dynamic;
	struct cfg80211_chan_def chandef;
	int ret, i;
	struct iwl_binding_cmd binding_cmd = {};
	struct iwl_time_quota_cmd quota_cmd = {};
	u32 status;

	/* add back the PHY */
	if (WARN_ON(!mvmvif->phy_ctxt))
		return -EINVAL;

	rcu_read_lock();
	ctx = rcu_dereference(vif->chanctx_conf);
	if (WARN_ON(!ctx)) {
		rcu_read_unlock();
		return -EINVAL;
	}
	chandef = ctx->def;
	chains_static = ctx->rx_chains_static;
	chains_dynamic = ctx->rx_chains_dynamic;
	rcu_read_unlock();

	ret = iwl_mvm_phy_ctxt_add(mvm, mvmvif->phy_ctxt, &chandef,
				   chains_static, chains_dynamic);
	if (ret)
		return ret;

	/* add back the MAC */
	mvmvif->uploaded = false;

	if (WARN_ON(!vif->bss_conf.assoc))
		return -EINVAL;
	/* hack */
	vif->bss_conf.assoc = false;
	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	vif->bss_conf.assoc = true;
	if (ret)
		return ret;

	/* add back binding - XXX refactor? */
	binding_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->phy_ctxt->id,
						mvmvif->phy_ctxt->color));
	binding_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	binding_cmd.phy =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->phy_ctxt->id,
						mvmvif->phy_ctxt->color));
	binding_cmd.macs[0] = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							      mvmvif->color));
	for (i = 1; i < MAX_MACS_IN_BINDING; i++)
		binding_cmd.macs[i] = cpu_to_le32(FW_CTXT_INVALID);

	status = 0;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, BINDING_CONTEXT_CMD,
					  sizeof(binding_cmd), &binding_cmd,
					  &status);
	if (ret) {
		IWL_ERR(mvm, "Failed to add binding: %d\n", ret);
		return ret;
	}

	if (status) {
		IWL_ERR(mvm, "Binding command failed: %u\n", status);
		return -EIO;
	}

	ret = iwl_mvm_sta_send_to_fw(mvm, ap_sta, false);
	if (ret)
		return ret;
	rcu_assign_pointer(mvm->fw_id_to_mac_id[mvmvif->ap_sta_id], ap_sta);

	ret = iwl_mvm_mac_ctxt_changed(mvm, vif);
	if (ret)
		return ret;

	/* and some quota */
	quota_cmd.quotas[0].id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->phy_ctxt->id,
						mvmvif->phy_ctxt->color));
	quota_cmd.quotas[0].quota = cpu_to_le32(100);
	quota_cmd.quotas[0].max_duration = cpu_to_le32(1000);

	for (i = 1; i < MAX_BINDINGS; i++)
		quota_cmd.quotas[i].id_and_color = cpu_to_le32(FW_CTXT_INVALID);

	ret = iwl_mvm_send_cmd_pdu(mvm, TIME_QUOTA_CMD, CMD_SYNC,
				   sizeof(quota_cmd), &quota_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send quota: %d\n", ret);

	return 0;
}

static int iwl_mvm_get_last_nonqos_seq(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_nonqos_seq_query_cmd query_cmd = {
		.get_set_flag = cpu_to_le32(IWL_NONQOS_SEQ_GET),
		.mac_id_n_color =
			cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							mvmvif->color)),
	};
	struct iwl_host_cmd cmd = {
		.id = NON_QOS_TX_COUNTER_CMD,
		.flags = CMD_SYNC | CMD_WANT_SKB,
	};
	int err;
	u32 size;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_D3_CONTINUITY_API) {
		cmd.data[0] = &query_cmd;
		cmd.len[0] = sizeof(query_cmd);
	}

	err = iwl_mvm_send_cmd(mvm, &cmd);
	if (err)
		return err;

	size = iwl_rx_packet_payload_len(cmd.resp_pkt);
	if (size < sizeof(__le16)) {
		err = -EINVAL;
	} else {
		err = le16_to_cpup((__le16 *)cmd.resp_pkt->data);
		/* new API returns next, not last-used seqno */
		if (mvm->fw->ucode_capa.flags &
				IWL_UCODE_TLV_FLAGS_D3_CONTINUITY_API)
			err = (u16) (err - 0x10);
	}

	iwl_free_resp(&cmd);
	return err;
}

void iwl_mvm_set_last_nonqos_seq(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_nonqos_seq_query_cmd query_cmd = {
		.get_set_flag = cpu_to_le32(IWL_NONQOS_SEQ_SET),
		.mac_id_n_color =
			cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							mvmvif->color)),
		.value = cpu_to_le16(mvmvif->seqno),
	};

	/* return if called during restart, not resume from D3 */
	if (!mvmvif->seqno_valid)
		return;

	mvmvif->seqno_valid = false;

	if (!(mvm->fw->ucode_capa.flags &
			IWL_UCODE_TLV_FLAGS_D3_CONTINUITY_API))
		return;

	if (iwl_mvm_send_cmd_pdu(mvm, NON_QOS_TX_COUNTER_CMD, CMD_SYNC,
				 sizeof(query_cmd), &query_cmd))
		IWL_ERR(mvm, "failed to set non-QoS seqno\n");
}

static int __iwl_mvm_suspend(struct ieee80211_hw *hw,
			     struct cfg80211_wowlan *wowlan,
			     bool test)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_d3_iter_data suspend_iter_data = {
		.mvm = mvm,
	};
	struct ieee80211_vif *vif;
	struct iwl_mvm_vif *mvmvif;
	struct ieee80211_sta *ap_sta;
	struct iwl_mvm_sta *mvm_ap_sta;
	struct iwl_wowlan_config_cmd wowlan_config_cmd = {};
	struct iwl_wowlan_kek_kck_material_cmd kek_kck_cmd = {};
	struct iwl_wowlan_tkip_params_cmd tkip_cmd = {};
	struct iwl_d3_manager_config d3_cfg_cmd_data = {
		/*
		 * Program the minimum sleep time to 10 seconds, as many
		 * platforms have issues processing a wakeup signal while
		 * still being in the process of suspending.
		 */
		.min_sleep_time = cpu_to_le32(10 * 1000 * 1000),
	};
	struct iwl_host_cmd d3_cfg_cmd = {
		.id = D3_CONFIG_CMD,
		.flags = CMD_SYNC | CMD_WANT_SKB,
		.data[0] = &d3_cfg_cmd_data,
		.len[0] = sizeof(d3_cfg_cmd_data),
	};
	struct wowlan_key_data key_data = {
		.use_rsc_tsc = false,
		.tkip = &tkip_cmd,
		.use_tkip = false,
	};
	int ret, i;
	int len __maybe_unused;
	u8 old_aux_sta_id, old_ap_sta_id = IWL_MVM_STATION_COUNT;

	if (!wowlan) {
		/*
		 * mac80211 shouldn't get here, but for D3 test
		 * it doesn't warrant a warning
		 */
		WARN_ON(!test);
		return -EINVAL;
	}

	key_data.rsc_tsc = kzalloc(sizeof(*key_data.rsc_tsc), GFP_KERNEL);
	if (!key_data.rsc_tsc)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);

	old_aux_sta_id = mvm->aux_sta.sta_id;

	/* see if there's only a single BSS vif and it's associated */
	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_d3_iface_iterator, &suspend_iter_data);

	if (suspend_iter_data.error || !suspend_iter_data.vif) {
		ret = 1;
		goto out_noreset;
	}

	vif = suspend_iter_data.vif;
	mvmvif = iwl_mvm_vif_from_mac80211(vif);

	ap_sta = rcu_dereference_protected(
			mvm->fw_id_to_mac_id[mvmvif->ap_sta_id],
			lockdep_is_held(&mvm->mutex));
	if (IS_ERR_OR_NULL(ap_sta)) {
		ret = -EINVAL;
		goto out_noreset;
	}

	mvm_ap_sta = (struct iwl_mvm_sta *)ap_sta->drv_priv;

	/* TODO: wowlan_config_cmd.wowlan_ba_teardown_tids */

	wowlan_config_cmd.is_11n_connection = ap_sta->ht_cap.ht_supported;

	/* Query the last used seqno and set it */
	ret = iwl_mvm_get_last_nonqos_seq(mvm, vif);
	if (ret < 0)
		goto out_noreset;
	wowlan_config_cmd.non_qos_seq = cpu_to_le16(ret);

	/*
	 * For QoS counters, we store the one to use next, so subtract 0x10
	 * since the uCode will add 0x10 *before* using the value while we
	 * increment after using the value (i.e. store the next value to use).
	 */
	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		u16 seq = mvm_ap_sta->tid_data[i].seq_number;
		seq -= 0x10;
		wowlan_config_cmd.qos_seq[i] = cpu_to_le16(seq);
	}

	if (wowlan->disconnect)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_BEACON_MISS |
				    IWL_WOWLAN_WAKEUP_LINK_CHANGE);
	if (wowlan->magic_pkt)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_MAGIC_PACKET);
	if (wowlan->gtk_rekey_failure)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_GTK_REKEY_FAIL);
	if (wowlan->eap_identity_req)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_EAP_IDENT_REQ);
	if (wowlan->four_way_handshake)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_4WAY_HANDSHAKE);
	if (wowlan->n_patterns)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_PATTERN_MATCH);

	if (wowlan->rfkill_release)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_RF_KILL_DEASSERT);

	if (wowlan->tcp) {
		/*
		 * Set the "link change" (really "link lost") flag as well
		 * since that implies losing the TCP connection.
		 */
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_REMOTE_LINK_LOSS |
				    IWL_WOWLAN_WAKEUP_REMOTE_SIGNATURE_TABLE |
				    IWL_WOWLAN_WAKEUP_REMOTE_WAKEUP_PACKET |
				    IWL_WOWLAN_WAKEUP_LINK_CHANGE);
	}

	iwl_mvm_cancel_scan(mvm);

	iwl_trans_stop_device(mvm->trans);

	/*
	 * The D3 firmware still hardcodes the AP station ID for the
	 * BSS we're associated with as 0. Store the real STA ID here
	 * and assign 0. When we leave this function, we'll restore
	 * the original value for the resume code.
	 */
	old_ap_sta_id = mvm_ap_sta->sta_id;
	mvm_ap_sta->sta_id = 0;
	mvmvif->ap_sta_id = 0;

	/*
	 * Set the HW restart bit -- this is mostly true as we're
	 * going to load new firmware and reprogram that, though
	 * the reprogramming is going to be manual to avoid adding
	 * all the MACs that aren't support.
	 * We don't have to clear up everything though because the
	 * reprogramming is manual. When we resume, we'll actually
	 * go through a proper restart sequence again to switch
	 * back to the runtime firmware image.
	 */
	set_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);

	/* We reprogram keys and shouldn't allocate new key indices */
	memset(mvm->fw_key_table, 0, sizeof(mvm->fw_key_table));

	mvm->ptk_ivlen = 0;
	mvm->ptk_icvlen = 0;
	mvm->ptk_ivlen = 0;
	mvm->ptk_icvlen = 0;

	/*
	 * The D3 firmware still hardcodes the AP station ID for the
	 * BSS we're associated with as 0. As a result, we have to move
	 * the auxiliary station to ID 1 so the ID 0 remains free for
	 * the AP station for later.
	 * We set the sta_id to 1 here, and reset it to its previous
	 * value (that we stored above) later.
	 */
	mvm->aux_sta.sta_id = 1;

	ret = iwl_mvm_load_d3_fw(mvm);
	if (ret)
		goto out;

	ret = iwl_mvm_d3_reprogram(mvm, vif, ap_sta);
	if (ret)
		goto out;

	if (!iwlwifi_mod_params.sw_crypto) {
		/*
		 * This needs to be unlocked due to lock ordering
		 * constraints. Since we're in the suspend path
		 * that isn't really a problem though.
		 */
		mutex_unlock(&mvm->mutex);
		ieee80211_iter_keys(mvm->hw, vif,
				    iwl_mvm_wowlan_program_keys,
				    &key_data);
		mutex_lock(&mvm->mutex);
		if (key_data.error) {
			ret = -EIO;
			goto out;
		}

		if (key_data.use_rsc_tsc) {
			struct iwl_host_cmd rsc_tsc_cmd = {
				.id = WOWLAN_TSC_RSC_PARAM,
				.flags = CMD_SYNC,
				.data[0] = key_data.rsc_tsc,
				.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
				.len[0] = sizeof(*key_data.rsc_tsc),
			};

			ret = iwl_mvm_send_cmd(mvm, &rsc_tsc_cmd);
			if (ret)
				goto out;
		}

		if (key_data.use_tkip) {
			ret = iwl_mvm_send_cmd_pdu(mvm,
						   WOWLAN_TKIP_PARAM,
						   CMD_SYNC, sizeof(tkip_cmd),
						   &tkip_cmd);
			if (ret)
				goto out;
		}

		if (mvmvif->rekey_data.valid) {
			memset(&kek_kck_cmd, 0, sizeof(kek_kck_cmd));
			memcpy(kek_kck_cmd.kck, mvmvif->rekey_data.kck,
			       NL80211_KCK_LEN);
			kek_kck_cmd.kck_len = cpu_to_le16(NL80211_KCK_LEN);
			memcpy(kek_kck_cmd.kek, mvmvif->rekey_data.kek,
			       NL80211_KEK_LEN);
			kek_kck_cmd.kek_len = cpu_to_le16(NL80211_KEK_LEN);
			kek_kck_cmd.replay_ctr = mvmvif->rekey_data.replay_ctr;

			ret = iwl_mvm_send_cmd_pdu(mvm,
						   WOWLAN_KEK_KCK_MATERIAL,
						   CMD_SYNC,
						   sizeof(kek_kck_cmd),
						   &kek_kck_cmd);
			if (ret)
				goto out;
		}
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, WOWLAN_CONFIGURATION,
				   CMD_SYNC, sizeof(wowlan_config_cmd),
				   &wowlan_config_cmd);
	if (ret)
		goto out;

	ret = iwl_mvm_send_patterns(mvm, wowlan);
	if (ret)
		goto out;

	ret = iwl_mvm_send_proto_offload(mvm, vif);
	if (ret)
		goto out;

	ret = iwl_mvm_send_remote_wake_cfg(mvm, vif, wowlan->tcp);
	if (ret)
		goto out;

	ret = iwl_mvm_power_update_device_mode(mvm);
	if (ret)
		goto out;

	ret = iwl_mvm_power_update_mode(mvm, vif);
	if (ret)
		goto out;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvm->d3_wake_sysassert)
		d3_cfg_cmd_data.wakeup_flags |=
			cpu_to_le32(IWL_WAKEUP_D3_CONFIG_FW_ERROR);
#endif

	/* must be last -- this switches firmware state */
	ret = iwl_mvm_send_cmd(mvm, &d3_cfg_cmd);
	if (ret)
		goto out;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	len = iwl_rx_packet_payload_len(d3_cfg_cmd.resp_pkt);
	if (len >= sizeof(u32)) {
		mvm->d3_test_pme_ptr =
			le32_to_cpup((__le32 *)d3_cfg_cmd.resp_pkt->data);
	}
#endif
	iwl_free_resp(&d3_cfg_cmd);

	clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);

	iwl_trans_d3_suspend(mvm->trans, test);
 out:
	mvm->aux_sta.sta_id = old_aux_sta_id;
	mvm_ap_sta->sta_id = old_ap_sta_id;
	mvmvif->ap_sta_id = old_ap_sta_id;

	if (ret < 0)
		ieee80211_restart_hw(mvm->hw);
 out_noreset:
	kfree(key_data.rsc_tsc);

	mutex_unlock(&mvm->mutex);

	return ret;
}

int iwl_mvm_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan)
{
	return __iwl_mvm_suspend(hw, wowlan, false);
}

/* converted data from the different status responses */
struct iwl_wowlan_status_data {
	u16 pattern_number;
	u16 qos_seq_ctr[8];
	u32 wakeup_reasons;
	u32 wake_packet_length;
	u32 wake_packet_bufsize;
	const u8 *wake_packet;
};

static void iwl_mvm_report_wakeup_reasons(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct iwl_wowlan_status_data *status)
{
	struct sk_buff *pkt = NULL;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup;
	u32 reasons = status->wakeup_reasons;

	if (reasons == IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS) {
		wakeup_report = NULL;
		goto report;
	}

	if (reasons & IWL_WOWLAN_WAKEUP_BY_MAGIC_PACKET)
		wakeup.magic_pkt = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_PATTERN)
		wakeup.pattern_idx =
			status->pattern_number;

	if (reasons & (IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_MISSED_BEACON |
		       IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH))
		wakeup.disconnect = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_GTK_REKEY_FAILURE)
		wakeup.gtk_rekey_failure = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_RFKILL_DEASSERTED)
		wakeup.rfkill_release = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_EAPOL_REQUEST)
		wakeup.eap_identity_req = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_FOUR_WAY_HANDSHAKE)
		wakeup.four_way_handshake = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_REM_WAKE_LINK_LOSS)
		wakeup.tcp_connlost = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_REM_WAKE_SIGNATURE_TABLE)
		wakeup.tcp_nomoretokens = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_REM_WAKE_WAKEUP_PACKET)
		wakeup.tcp_match = true;

	if (status->wake_packet_bufsize) {
		int pktsize = status->wake_packet_bufsize;
		int pktlen = status->wake_packet_length;
		const u8 *pktdata = status->wake_packet;
		struct ieee80211_hdr *hdr = (void *)pktdata;
		int truncated = pktlen - pktsize;

		/* this would be a firmware bug */
		if (WARN_ON_ONCE(truncated < 0))
			truncated = 0;

		if (ieee80211_is_data(hdr->frame_control)) {
			int hdrlen = ieee80211_hdrlen(hdr->frame_control);
			int ivlen = 0, icvlen = 4; /* also FCS */

			pkt = alloc_skb(pktsize, GFP_KERNEL);
			if (!pkt)
				goto report;

			memcpy(skb_put(pkt, hdrlen), pktdata, hdrlen);
			pktdata += hdrlen;
			pktsize -= hdrlen;

			if (ieee80211_has_protected(hdr->frame_control)) {
				/*
				 * This is unlocked and using gtk_i(c)vlen,
				 * but since everything is under RTNL still
				 * that's not really a problem - changing
				 * it would be difficult.
				 */
				if (is_multicast_ether_addr(hdr->addr1)) {
					ivlen = mvm->gtk_ivlen;
					icvlen += mvm->gtk_icvlen;
				} else {
					ivlen = mvm->ptk_ivlen;
					icvlen += mvm->ptk_icvlen;
				}
			}

			/* if truncated, FCS/ICV is (partially) gone */
			if (truncated >= icvlen) {
				icvlen = 0;
				truncated -= icvlen;
			} else {
				icvlen -= truncated;
				truncated = 0;
			}

			pktsize -= ivlen + icvlen;
			pktdata += ivlen;

			memcpy(skb_put(pkt, pktsize), pktdata, pktsize);

			if (ieee80211_data_to_8023(pkt, vif->addr, vif->type))
				goto report;
			wakeup.packet = pkt->data;
			wakeup.packet_present_len = pkt->len;
			wakeup.packet_len = pkt->len - truncated;
			wakeup.packet_80211 = false;
		} else {
			int fcslen = 4;

			if (truncated >= 4) {
				truncated -= 4;
				fcslen = 0;
			} else {
				fcslen -= truncated;
				truncated = 0;
			}
			pktsize -= fcslen;
			wakeup.packet = status->wake_packet;
			wakeup.packet_present_len = pktsize;
			wakeup.packet_len = pktlen - truncated;
			wakeup.packet_80211 = true;
		}
	}

 report:
	ieee80211_report_wowlan_wakeup(vif, wakeup_report, GFP_KERNEL);
	kfree_skb(pkt);
}

static void iwl_mvm_aes_sc_to_seq(struct aes_sc *sc,
				  struct ieee80211_key_seq *seq)
{
	u64 pn;

	pn = le64_to_cpu(sc->pn);
	seq->ccmp.pn[0] = pn >> 40;
	seq->ccmp.pn[1] = pn >> 32;
	seq->ccmp.pn[2] = pn >> 24;
	seq->ccmp.pn[3] = pn >> 16;
	seq->ccmp.pn[4] = pn >> 8;
	seq->ccmp.pn[5] = pn;
}

static void iwl_mvm_tkip_sc_to_seq(struct tkip_sc *sc,
				   struct ieee80211_key_seq *seq)
{
	seq->tkip.iv32 = le32_to_cpu(sc->iv32);
	seq->tkip.iv16 = le16_to_cpu(sc->iv16);
}

static void iwl_mvm_set_aes_rx_seq(struct aes_sc *scs,
				   struct ieee80211_key_conf *key)
{
	int tid;

	BUILD_BUG_ON(IWL_NUM_RSC != IEEE80211_NUM_TIDS);

	for (tid = 0; tid < IWL_NUM_RSC; tid++) {
		struct ieee80211_key_seq seq = {};

		iwl_mvm_aes_sc_to_seq(&scs[tid], &seq);
		ieee80211_set_key_rx_seq(key, tid, &seq);
	}
}

static void iwl_mvm_set_tkip_rx_seq(struct tkip_sc *scs,
				    struct ieee80211_key_conf *key)
{
	int tid;

	BUILD_BUG_ON(IWL_NUM_RSC != IEEE80211_NUM_TIDS);

	for (tid = 0; tid < IWL_NUM_RSC; tid++) {
		struct ieee80211_key_seq seq = {};

		iwl_mvm_tkip_sc_to_seq(&scs[tid], &seq);
		ieee80211_set_key_rx_seq(key, tid, &seq);
	}
}

static void iwl_mvm_set_key_rx_seq(struct ieee80211_key_conf *key,
				   struct iwl_wowlan_status_v6 *status)
{
	union iwl_all_tsc_rsc *rsc = &status->gtk.rsc.all_tsc_rsc;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		iwl_mvm_set_aes_rx_seq(rsc->aes.multicast_rsc, key);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		iwl_mvm_set_tkip_rx_seq(rsc->tkip.multicast_rsc, key);
		break;
	default:
		WARN_ON(1);
	}
}

struct iwl_mvm_d3_gtk_iter_data {
	struct iwl_wowlan_status_v6 *status;
	void *last_gtk;
	u32 cipher;
	bool find_phase, unhandled_cipher;
	int num_keys;
};

static void iwl_mvm_d3_update_gtks(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct ieee80211_key_conf *key,
				   void *_data)
{
	struct iwl_mvm_d3_gtk_iter_data *data = _data;

	if (data->unhandled_cipher)
		return;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* ignore WEP completely, nothing to do */
		return;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_TKIP:
		/* we support these */
		break;
	default:
		/* everything else (even CMAC for MFP) - disconnect from AP */
		data->unhandled_cipher = true;
		return;
	}

	data->num_keys++;

	/*
	 * pairwise key - update sequence counters only;
	 * note that this assumes no TDLS sessions are active
	 */
	if (sta) {
		struct ieee80211_key_seq seq = {};
		union iwl_all_tsc_rsc *sc = &data->status->gtk.rsc.all_tsc_rsc;

		if (data->find_phase)
			return;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
			iwl_mvm_aes_sc_to_seq(&sc->aes.tsc, &seq);
			iwl_mvm_set_aes_rx_seq(sc->aes.unicast_rsc, key);
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			iwl_mvm_tkip_sc_to_seq(&sc->tkip.tsc, &seq);
			iwl_mvm_set_tkip_rx_seq(sc->tkip.unicast_rsc, key);
			break;
		}
		ieee80211_set_key_tx_seq(key, &seq);

		/* that's it for this key */
		return;
	}

	if (data->find_phase) {
		data->last_gtk = key;
		data->cipher = key->cipher;
		return;
	}

	if (data->status->num_of_gtk_rekeys)
		ieee80211_remove_key(key);
	else if (data->last_gtk == key)
		iwl_mvm_set_key_rx_seq(key, data->status);
}

static bool iwl_mvm_setup_connection_keep(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct iwl_wowlan_status_v6 *status)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_d3_gtk_iter_data gtkdata = {
		.status = status,
	};
	u32 disconnection_reasons =
		IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_MISSED_BEACON |
		IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH;

	if (!status || !vif->bss_conf.bssid)
		return false;

	if (le32_to_cpu(status->wakeup_reasons) & disconnection_reasons)
		return false;

	/* find last GTK that we used initially, if any */
	gtkdata.find_phase = true;
	ieee80211_iter_keys(mvm->hw, vif,
			    iwl_mvm_d3_update_gtks, &gtkdata);
	/* not trying to keep connections with MFP/unhandled ciphers */
	if (gtkdata.unhandled_cipher)
		return false;
	if (!gtkdata.num_keys)
		goto out;
	if (!gtkdata.last_gtk)
		return false;

	/*
	 * invalidate all other GTKs that might still exist and update
	 * the one that we used
	 */
	gtkdata.find_phase = false;
	ieee80211_iter_keys(mvm->hw, vif,
			    iwl_mvm_d3_update_gtks, &gtkdata);

	if (status->num_of_gtk_rekeys) {
		struct ieee80211_key_conf *key;
		struct {
			struct ieee80211_key_conf conf;
			u8 key[32];
		} conf = {
			.conf.cipher = gtkdata.cipher,
			.conf.keyidx = status->gtk.key_index,
		};

		switch (gtkdata.cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
			conf.conf.keylen = WLAN_KEY_LEN_CCMP;
			memcpy(conf.conf.key, status->gtk.decrypt_key,
			       WLAN_KEY_LEN_CCMP);
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			conf.conf.keylen = WLAN_KEY_LEN_TKIP;
			memcpy(conf.conf.key, status->gtk.decrypt_key, 16);
			/* leave TX MIC key zeroed, we don't use it anyway */
			memcpy(conf.conf.key +
			       NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
			       status->gtk.tkip_mic_key, 8);
			break;
		}

		key = ieee80211_gtk_rekey_add(vif, &conf.conf);
		if (IS_ERR(key))
			return false;
		iwl_mvm_set_key_rx_seq(key, status);
	}

	if (status->num_of_gtk_rekeys) {
		__be64 replay_ctr =
			cpu_to_be64(le64_to_cpu(status->replay_ctr));
		ieee80211_gtk_rekey_notify(vif, vif->bss_conf.bssid,
					   (void *)&replay_ctr, GFP_KERNEL);
	}

out:
	mvmvif->seqno_valid = true;
	/* +0x10 because the set API expects next-to-use, not last-used */
	mvmvif->seqno = le16_to_cpu(status->non_qos_seq_ctr) + 0x10;

	return true;
}

/* releases the MVM mutex */
static bool iwl_mvm_query_wakeup_reasons(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif)
{
	u32 base = mvm->error_event_table;
	struct error_table_start {
		/* cf. struct iwl_error_event_table */
		u32 valid;
		u32 error_id;
	} err_info;
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_GET_STATUSES,
		.flags = CMD_SYNC | CMD_WANT_SKB,
	};
	struct iwl_wowlan_status_data status;
	struct iwl_wowlan_status_v6 *status_v6;
	int ret, len, status_size, i;
	bool keep;
	struct ieee80211_sta *ap_sta;
	struct iwl_mvm_sta *mvm_ap_sta;

	iwl_trans_read_mem_bytes(mvm->trans, base,
				 &err_info, sizeof(err_info));

	if (err_info.valid) {
		IWL_INFO(mvm, "error table is valid (%d)\n",
			 err_info.valid);
		if (err_info.error_id == RF_KILL_INDICATOR_FOR_WOWLAN) {
			struct cfg80211_wowlan_wakeup wakeup = {
				.rfkill_release = true,
			};
			ieee80211_report_wowlan_wakeup(vif, &wakeup,
						       GFP_KERNEL);
		}
		goto out_unlock;
	}

	/* only for tracing for now */
	ret = iwl_mvm_send_cmd_pdu(mvm, OFFLOADS_QUERY_CMD, CMD_SYNC, 0, NULL);
	if (ret)
		IWL_ERR(mvm, "failed to query offload statistics (%d)\n", ret);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret) {
		IWL_ERR(mvm, "failed to query status (%d)\n", ret);
		goto out_unlock;
	}

	/* RF-kill already asserted again... */
	if (!cmd.resp_pkt)
		goto out_unlock;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_D3_CONTINUITY_API)
		status_size = sizeof(struct iwl_wowlan_status_v6);
	else
		status_size = sizeof(struct iwl_wowlan_status_v4);

	len = iwl_rx_packet_payload_len(cmd.resp_pkt);
	if (len < status_size) {
		IWL_ERR(mvm, "Invalid WoWLAN status response!\n");
		goto out_free_resp;
	}

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_D3_CONTINUITY_API) {
		status_v6 = (void *)cmd.resp_pkt->data;

		status.pattern_number = le16_to_cpu(status_v6->pattern_number);
		for (i = 0; i < 8; i++)
			status.qos_seq_ctr[i] =
				le16_to_cpu(status_v6->qos_seq_ctr[i]);
		status.wakeup_reasons = le32_to_cpu(status_v6->wakeup_reasons);
		status.wake_packet_length =
			le32_to_cpu(status_v6->wake_packet_length);
		status.wake_packet_bufsize =
			le32_to_cpu(status_v6->wake_packet_bufsize);
		status.wake_packet = status_v6->wake_packet;
	} else {
		struct iwl_wowlan_status_v4 *status_v4;
		status_v6 = NULL;
		status_v4 = (void *)cmd.resp_pkt->data;

		status.pattern_number = le16_to_cpu(status_v4->pattern_number);
		for (i = 0; i < 8; i++)
			status.qos_seq_ctr[i] =
				le16_to_cpu(status_v4->qos_seq_ctr[i]);
		status.wakeup_reasons = le32_to_cpu(status_v4->wakeup_reasons);
		status.wake_packet_length =
			le32_to_cpu(status_v4->wake_packet_length);
		status.wake_packet_bufsize =
			le32_to_cpu(status_v4->wake_packet_bufsize);
		status.wake_packet = status_v4->wake_packet;
	}

	if (len != status_size + ALIGN(status.wake_packet_bufsize, 4)) {
		IWL_ERR(mvm, "Invalid WoWLAN status response!\n");
		goto out_free_resp;
	}

	/* still at hard-coded place 0 for D3 image */
	ap_sta = rcu_dereference_protected(
			mvm->fw_id_to_mac_id[0],
			lockdep_is_held(&mvm->mutex));
	if (IS_ERR_OR_NULL(ap_sta))
		goto out_free_resp;

	mvm_ap_sta = (struct iwl_mvm_sta *)ap_sta->drv_priv;
	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		u16 seq = status.qos_seq_ctr[i];
		/* firmware stores last-used value, we store next value */
		seq += 0x10;
		mvm_ap_sta->tid_data[i].seq_number = seq;
	}

	/* now we have all the data we need, unlock to avoid mac80211 issues */
	mutex_unlock(&mvm->mutex);

	iwl_mvm_report_wakeup_reasons(mvm, vif, &status);

	keep = iwl_mvm_setup_connection_keep(mvm, vif, status_v6);

	iwl_free_resp(&cmd);
	return keep;

 out_free_resp:
	iwl_free_resp(&cmd);
 out_unlock:
	mutex_unlock(&mvm->mutex);
	return false;
}

static void iwl_mvm_read_d3_sram(struct iwl_mvm *mvm)
{
#ifdef CONFIG_IWLWIFI_DEBUGFS
	const struct fw_img *img = &mvm->fw->img[IWL_UCODE_WOWLAN];
	u32 len = img->sec[IWL_UCODE_SECTION_DATA].len;
	u32 offs = img->sec[IWL_UCODE_SECTION_DATA].offset;

	if (!mvm->store_d3_resume_sram)
		return;

	if (!mvm->d3_resume_sram) {
		mvm->d3_resume_sram = kzalloc(len, GFP_KERNEL);
		if (!mvm->d3_resume_sram)
			return;
	}

	iwl_trans_read_mem_bytes(mvm->trans, offs, mvm->d3_resume_sram, len);
#endif
}

static void iwl_mvm_d3_disconnect_iter(void *data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	/* skip the one we keep connection on */
	if (data == vif)
		return;

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_resume_disconnect(vif);
}

static int __iwl_mvm_resume(struct iwl_mvm *mvm, bool test)
{
	struct iwl_d3_iter_data resume_iter_data = {
		.mvm = mvm,
	};
	struct ieee80211_vif *vif = NULL;
	int ret;
	enum iwl_d3_status d3_status;
	bool keep = false;

	mutex_lock(&mvm->mutex);

	/* get the BSS vif pointer again */
	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_d3_iface_iterator, &resume_iter_data);

	if (WARN_ON(resume_iter_data.error || !resume_iter_data.vif))
		goto out_unlock;

	vif = resume_iter_data.vif;

	ret = iwl_trans_d3_resume(mvm->trans, &d3_status, test);
	if (ret)
		goto out_unlock;

	if (d3_status != IWL_D3_STATUS_ALIVE) {
		IWL_INFO(mvm, "Device was reset during suspend\n");
		goto out_unlock;
	}

	/* query SRAM first in case we want event logging */
	iwl_mvm_read_d3_sram(mvm);

	keep = iwl_mvm_query_wakeup_reasons(mvm, vif);
#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (keep)
		mvm->keep_vif = vif;
#endif
	/* has unlocked the mutex, so skip that */
	goto out;

 out_unlock:
	mutex_unlock(&mvm->mutex);

 out:
	if (!test)
		ieee80211_iterate_active_interfaces_rtnl(mvm->hw,
			IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_d3_disconnect_iter, keep ? vif : NULL);

	/* return 1 to reconfigure the device */
	set_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);
	return 1;
}

int iwl_mvm_resume(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	return __iwl_mvm_resume(mvm, false);
}

void iwl_mvm_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	device_set_wakeup_enable(mvm->trans->dev, enabled);
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
static int iwl_mvm_d3_test_open(struct inode *inode, struct file *file)
{
	struct iwl_mvm *mvm = inode->i_private;
	int err;

	if (mvm->d3_test_active)
		return -EBUSY;

	file->private_data = inode->i_private;

	ieee80211_stop_queues(mvm->hw);
	synchronize_net();

	/* start pseudo D3 */
	rtnl_lock();
	err = __iwl_mvm_suspend(mvm->hw, mvm->hw->wiphy->wowlan_config, true);
	rtnl_unlock();
	if (err > 0)
		err = -EINVAL;
	if (err) {
		ieee80211_wake_queues(mvm->hw);
		return err;
	}
	mvm->d3_test_active = true;
	mvm->keep_vif = NULL;
	return 0;
}

static ssize_t iwl_mvm_d3_test_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	u32 pme_asserted;

	while (true) {
		/* read pme_ptr if available */
		if (mvm->d3_test_pme_ptr) {
			pme_asserted = iwl_trans_read_mem32(mvm->trans,
						mvm->d3_test_pme_ptr);
			if (pme_asserted)
				break;
		}

		if (msleep_interruptible(100))
			break;
	}

	return 0;
}

static void iwl_mvm_d3_test_disconn_work_iter(void *_data, u8 *mac,
					      struct ieee80211_vif *vif)
{
	/* skip the one we keep connection on */
	if (_data == vif)
		return;

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_connection_loss(vif);
}

static int iwl_mvm_d3_test_release(struct inode *inode, struct file *file)
{
	struct iwl_mvm *mvm = inode->i_private;
	int remaining_time = 10;

	mvm->d3_test_active = false;
	__iwl_mvm_resume(mvm, true);
	iwl_abort_notification_waits(&mvm->notif_wait);
	ieee80211_restart_hw(mvm->hw);

	/* wait for restart and disconnect all interfaces */
	while (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
	       remaining_time > 0) {
		remaining_time--;
		msleep(1000);
	}

	if (remaining_time == 0)
		IWL_ERR(mvm, "Timed out waiting for HW restart to finish!\n");

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_d3_test_disconn_work_iter, mvm->keep_vif);

	ieee80211_wake_queues(mvm->hw);

	return 0;
}

const struct file_operations iwl_dbgfs_d3_test_ops = {
	.llseek = no_llseek,
	.open = iwl_mvm_d3_test_open,
	.read = iwl_mvm_d3_test_read,
	.release = iwl_mvm_d3_test_release,
};
#endif
