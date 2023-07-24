// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2021-2022 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2015 Intel Deutschland GmbH
 */
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <linux/bitops.h>
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
			       bool offload_ns,
			       u32 cmd_flags)
{
	union {
		struct iwl_proto_offload_cmd_v1 v1;
		struct iwl_proto_offload_cmd_v2 v2;
		struct iwl_proto_offload_cmd_v3_small v3s;
		struct iwl_proto_offload_cmd_v4 v4;
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
	int ver = iwl_fw_lookup_cmd_ver(mvm->fw, hcmd.id, 0);

#if IS_ENABLED(CONFIG_IPV6)
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int i;
	/*
	 * Skip tentative address when ns offload is enabled to avoid
	 * violating RFC4862.
	 * Keep tentative address when ns offload is disabled so the NS packets
	 * will not be filtered out and will wake up the host.
	 */
	bool skip_tentative = offload_ns;

	if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL ||
	    capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE) {
		struct iwl_ns_config *nsc;
		struct iwl_targ_addr *addrs;
		int n_nsc, n_addrs;
		int c;
		int num_skipped = 0;

		if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL) {
			nsc = cmd.v3s.ns_config;
			n_nsc = IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3S;
			addrs = cmd.v3s.targ_addrs;
			n_addrs = IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3S;
		} else {
			nsc = cmd.v4.ns_config;
			n_nsc = IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3L;
			addrs = cmd.v4.targ_addrs;
			n_addrs = IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3L;
		}

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

			if (skip_tentative &&
			    test_bit(i, mvmvif->tentative_addrs)) {
				num_skipped++;
				continue;
			}

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

		if (mvmvif->num_target_ipv6_addrs - num_skipped)
			enabled |= IWL_D3_PROTO_IPV6_VALID;

		if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL)
			cmd.v3s.num_valid_ipv6_addrs =
				cpu_to_le32(i - num_skipped);
		else
			cmd.v4.num_valid_ipv6_addrs =
				cpu_to_le32(i - num_skipped);
	} else if (capa_flags & IWL_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS) {
		bool found = false;

		BUILD_BUG_ON(sizeof(cmd.v2.target_ipv6_addr[0]) !=
			     sizeof(mvmvif->target_ipv6_addrs[0]));

		for (i = 0; i < min(mvmvif->num_target_ipv6_addrs,
				    IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V2); i++) {
			if (skip_tentative &&
			    test_bit(i, mvmvif->tentative_addrs))
				continue;

			memcpy(cmd.v2.target_ipv6_addr[i],
			       &mvmvif->target_ipv6_addrs[i],
			       sizeof(cmd.v2.target_ipv6_addr[i]));

			found = true;
		}
		if (found) {
			enabled |= IWL_D3_PROTO_IPV6_VALID;
			memcpy(cmd.v2.ndp_mac_addr, vif->addr, ETH_ALEN);
		}
	} else {
		bool found = false;
		BUILD_BUG_ON(sizeof(cmd.v1.target_ipv6_addr[0]) !=
			     sizeof(mvmvif->target_ipv6_addrs[0]));

		for (i = 0; i < min(mvmvif->num_target_ipv6_addrs,
				    IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V1); i++) {
			if (skip_tentative &&
			    test_bit(i, mvmvif->tentative_addrs))
				continue;

			memcpy(cmd.v1.target_ipv6_addr[i],
			       &mvmvif->target_ipv6_addrs[i],
			       sizeof(cmd.v1.target_ipv6_addr[i]));

			found = true;
		}

		if (found) {
			enabled |= IWL_D3_PROTO_IPV6_VALID;
			memcpy(cmd.v1.ndp_mac_addr, vif->addr, ETH_ALEN);
		}
	}

	if (offload_ns && (enabled & IWL_D3_PROTO_IPV6_VALID))
		enabled |= IWL_D3_PROTO_OFFLOAD_NS;
#endif
	if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL) {
		common = &cmd.v3s.common;
		size = sizeof(cmd.v3s);
	} else if (capa_flags & IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE) {
		common = &cmd.v4.common;
		size = sizeof(cmd.v4);
		if (ver < 4) {
			/*
			 * This basically uses iwl_proto_offload_cmd_v3_large
			 * which doesn't have the sta_id parameter before the
			 * common part.
			 */
			size -= sizeof(cmd.v4.sta_id);
			hcmd.data[0] = common;
		}
	} else if (capa_flags & IWL_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS) {
		common = &cmd.v2.common;
		size = sizeof(cmd.v2);
	} else {
		common = &cmd.v1.common;
		size = sizeof(cmd.v1);
	}

	if (vif->cfg.arp_addr_cnt) {
		enabled |= IWL_D3_PROTO_OFFLOAD_ARP | IWL_D3_PROTO_IPV4_VALID;
		common->host_ipv4_addr = vif->cfg.arp_addr_list[0];
		memcpy(common->arp_mac_addr, vif->addr, ETH_ALEN);
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_OFFLOAD_BTM_SUPPORT))
		enabled |= IWL_D3_PROTO_OFFLOAD_BTM;

	if (!disable_offloading)
		common->enabled = cpu_to_le32(enabled);

	hcmd.len[0] = size;
	return iwl_mvm_send_cmd(mvm, &hcmd);
}
