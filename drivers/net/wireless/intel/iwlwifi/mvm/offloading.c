/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
#include <net/ipv6.h>
#include <net/addrconf.h>
#include "mvm.h"

void iwl_mvm_set_wowlan_qos_seq(struct iwl_mvm_sta *mvm_ap_sta,
				struct iwl_wowlan_config_cmd *cmd)
{
	int i;

	/*
	 * For QoS counters, we store the one to use next, so subtract 0x10
	 * since the uCode will add 0x10 *before* using the value while we
	 * increment after using the value (i.e. store the next value to use).
	 */
	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		u16 seq = mvm_ap_sta->tid_data[i].seq_number;
		seq -= 0x10;
		cmd->qos_seq[i] = cpu_to_le16(seq);
	}
}

int iwl_mvm_send_proto_offload(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       bool disable_offloading,
			       u32 cmd_flags)
{
	union {
		struct iwl_proto_offload_cmd_v1 v1;
		struct iwl_proto_offload_cmd_v2 v2;
		struct iwl_proto_offload_cmd_v3_small v3s;
		struct iwl_proto_offload_cmd_v3_large v3l;
	} cmd = {};
	struct iwl_host_cmd hcmd = {
		.id = PROT_OFFLOAD_CONFIG_CMD,
		.flags = cmd_flags,
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

	if (!disable_offloading)
		common->enabled = cpu_to_le32(enabled);

	hcmd.len[0] = size;
	return iwl_mvm_send_cmd(mvm, &hcmd);
}
