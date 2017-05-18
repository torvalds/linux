/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
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
 */

/*
 * This file contains OPA VNIC EMA Interface functions.
 */

#include "opa_vnic_internal.h"

/**
 * opa_vnic_vema_report_event - sent trap to report the specified event
 * @adapter: vnic port adapter
 * @event: event to be reported
 *
 * This function calls vema api to sent a trap for the given event.
 */
void opa_vnic_vema_report_event(struct opa_vnic_adapter *adapter, u8 event)
{
	struct __opa_veswport_info *info = &adapter->info;
	struct __opa_veswport_trap trap_data;

	trap_data.fabric_id = info->vesw.fabric_id;
	trap_data.veswid = info->vesw.vesw_id;
	trap_data.veswportnum = info->vport.port_num;
	trap_data.opaportnum = adapter->port_num;
	trap_data.veswportindex = adapter->vport_num;
	trap_data.opcode = event;

	opa_vnic_vema_send_trap(adapter, &trap_data, info->vport.encap_slid);
}

/**
 * opa_vnic_get_error_counters - get summary counters
 * @adapter: vnic port adapter
 * @cntrs: pointer to destination summary counters structure
 *
 * This function populates the summary counters that is maintained by the
 * given adapter to destination address provided.
 */
void opa_vnic_get_summary_counters(struct opa_vnic_adapter *adapter,
				   struct opa_veswport_summary_counters *cntrs)
{
	struct opa_vnic_stats vstats;
	__be64 *dst;
	u64 *src;

	memset(&vstats, 0, sizeof(vstats));
	mutex_lock(&adapter->stats_lock);
	adapter->rn_ops->ndo_get_stats64(adapter->netdev, &vstats.netstats);
	mutex_unlock(&adapter->stats_lock);

	cntrs->vp_instance = cpu_to_be16(adapter->vport_num);
	cntrs->vesw_id = cpu_to_be16(adapter->info.vesw.vesw_id);
	cntrs->veswport_num = cpu_to_be32(adapter->port_num);

	cntrs->tx_errors = cpu_to_be64(vstats.netstats.tx_errors);
	cntrs->rx_errors = cpu_to_be64(vstats.netstats.rx_errors);
	cntrs->tx_packets = cpu_to_be64(vstats.netstats.tx_packets);
	cntrs->rx_packets = cpu_to_be64(vstats.netstats.rx_packets);
	cntrs->tx_bytes = cpu_to_be64(vstats.netstats.tx_bytes);
	cntrs->rx_bytes = cpu_to_be64(vstats.netstats.rx_bytes);

	/*
	 * This loop depends on layout of
	 * opa_veswport_summary_counters opa_vnic_stats structures.
	 */
	for (dst = &cntrs->tx_unicast, src = &vstats.tx_grp.unicast;
	     dst < &cntrs->reserved[0]; dst++, src++) {
		*dst = cpu_to_be64(*src);
	}
}

/**
 * opa_vnic_get_error_counters - get error counters
 * @adapter: vnic port adapter
 * @cntrs: pointer to destination error counters structure
 *
 * This function populates the error counters that is maintained by the
 * given adapter to destination address provided.
 */
void opa_vnic_get_error_counters(struct opa_vnic_adapter *adapter,
				 struct opa_veswport_error_counters *cntrs)
{
	struct opa_vnic_stats vstats;

	memset(&vstats, 0, sizeof(vstats));
	mutex_lock(&adapter->stats_lock);
	adapter->rn_ops->ndo_get_stats64(adapter->netdev, &vstats.netstats);
	mutex_unlock(&adapter->stats_lock);

	cntrs->vp_instance = cpu_to_be16(adapter->vport_num);
	cntrs->vesw_id = cpu_to_be16(adapter->info.vesw.vesw_id);
	cntrs->veswport_num = cpu_to_be32(adapter->port_num);

	cntrs->tx_errors = cpu_to_be64(vstats.netstats.tx_errors);
	cntrs->rx_errors = cpu_to_be64(vstats.netstats.rx_errors);
	cntrs->tx_dlid_zero = cpu_to_be64(vstats.tx_dlid_zero);
	cntrs->tx_drop_state = cpu_to_be64(vstats.tx_drop_state);
	cntrs->tx_logic = cpu_to_be64(vstats.netstats.tx_fifo_errors +
				      vstats.netstats.tx_carrier_errors);

	cntrs->rx_bad_veswid = cpu_to_be64(vstats.netstats.rx_nohandler);
	cntrs->rx_runt = cpu_to_be64(vstats.rx_runt);
	cntrs->rx_oversize = cpu_to_be64(vstats.rx_oversize);
	cntrs->rx_drop_state = cpu_to_be64(vstats.rx_drop_state);
	cntrs->rx_logic = cpu_to_be64(vstats.netstats.rx_fifo_errors);
}

/**
 * opa_vnic_get_vesw_info -- Get the vesw information
 * @adapter: vnic port adapter
 * @info: pointer to destination vesw info structure
 *
 * This function copies the vesw info that is maintained by the
 * given adapter to destination address provided.
 */
void opa_vnic_get_vesw_info(struct opa_vnic_adapter *adapter,
			    struct opa_vesw_info *info)
{
	struct __opa_vesw_info *src = &adapter->info.vesw;
	int i;

	info->fabric_id = cpu_to_be16(src->fabric_id);
	info->vesw_id = cpu_to_be16(src->vesw_id);
	memcpy(info->rsvd0, src->rsvd0, ARRAY_SIZE(src->rsvd0));
	info->def_port_mask = cpu_to_be16(src->def_port_mask);
	memcpy(info->rsvd1, src->rsvd1, ARRAY_SIZE(src->rsvd1));
	info->pkey = cpu_to_be16(src->pkey);

	memcpy(info->rsvd2, src->rsvd2, ARRAY_SIZE(src->rsvd2));
	info->u_mcast_dlid = cpu_to_be32(src->u_mcast_dlid);
	for (i = 0; i < OPA_VESW_MAX_NUM_DEF_PORT; i++)
		info->u_ucast_dlid[i] = cpu_to_be32(src->u_ucast_dlid[i]);

	memcpy(info->rsvd3, src->rsvd3, ARRAY_SIZE(src->rsvd3));
	for (i = 0; i < OPA_VNIC_MAX_NUM_PCP; i++)
		info->eth_mtu[i] = cpu_to_be16(src->eth_mtu[i]);

	info->eth_mtu_non_vlan = cpu_to_be16(src->eth_mtu_non_vlan);
	memcpy(info->rsvd4, src->rsvd4, ARRAY_SIZE(src->rsvd4));
}

/**
 * opa_vnic_set_vesw_info -- Set the vesw information
 * @adapter: vnic port adapter
 * @info: pointer to vesw info structure
 *
 * This function updates the vesw info that is maintained by the
 * given adapter with vesw info provided. Reserved fields are stored
 * and returned back to EM as is.
 */
void opa_vnic_set_vesw_info(struct opa_vnic_adapter *adapter,
			    struct opa_vesw_info *info)
{
	struct __opa_vesw_info *dst = &adapter->info.vesw;
	int i;

	dst->fabric_id = be16_to_cpu(info->fabric_id);
	dst->vesw_id = be16_to_cpu(info->vesw_id);
	memcpy(dst->rsvd0, info->rsvd0, ARRAY_SIZE(info->rsvd0));
	dst->def_port_mask = be16_to_cpu(info->def_port_mask);
	memcpy(dst->rsvd1, info->rsvd1, ARRAY_SIZE(info->rsvd1));
	dst->pkey = be16_to_cpu(info->pkey);

	memcpy(dst->rsvd2, info->rsvd2, ARRAY_SIZE(info->rsvd2));
	dst->u_mcast_dlid = be32_to_cpu(info->u_mcast_dlid);
	for (i = 0; i < OPA_VESW_MAX_NUM_DEF_PORT; i++)
		dst->u_ucast_dlid[i] = be32_to_cpu(info->u_ucast_dlid[i]);

	memcpy(dst->rsvd3, info->rsvd3, ARRAY_SIZE(info->rsvd3));
	for (i = 0; i < OPA_VNIC_MAX_NUM_PCP; i++)
		dst->eth_mtu[i] = be16_to_cpu(info->eth_mtu[i]);

	dst->eth_mtu_non_vlan = be16_to_cpu(info->eth_mtu_non_vlan);
	memcpy(dst->rsvd4, info->rsvd4, ARRAY_SIZE(info->rsvd4));
}

/**
 * opa_vnic_get_per_veswport_info -- Get the vesw per port information
 * @adapter: vnic port adapter
 * @info: pointer to destination vport info structure
 *
 * This function copies the vesw per port info that is maintained by the
 * given adapter to destination address provided.
 * Note that the read only fields are not copied.
 */
void opa_vnic_get_per_veswport_info(struct opa_vnic_adapter *adapter,
				    struct opa_per_veswport_info *info)
{
	struct __opa_per_veswport_info *src = &adapter->info.vport;

	info->port_num = cpu_to_be32(src->port_num);
	info->eth_link_status = src->eth_link_status;
	memcpy(info->rsvd0, src->rsvd0, ARRAY_SIZE(src->rsvd0));

	memcpy(info->base_mac_addr, src->base_mac_addr,
	       ARRAY_SIZE(info->base_mac_addr));
	info->config_state = src->config_state;
	info->oper_state = src->oper_state;
	info->max_mac_tbl_ent = cpu_to_be16(src->max_mac_tbl_ent);
	info->max_smac_ent = cpu_to_be16(src->max_smac_ent);
	info->mac_tbl_digest = cpu_to_be32(src->mac_tbl_digest);
	memcpy(info->rsvd1, src->rsvd1, ARRAY_SIZE(src->rsvd1));

	info->encap_slid = cpu_to_be32(src->encap_slid);
	memcpy(info->pcp_to_sc_uc, src->pcp_to_sc_uc,
	       ARRAY_SIZE(info->pcp_to_sc_uc));
	memcpy(info->pcp_to_vl_uc, src->pcp_to_vl_uc,
	       ARRAY_SIZE(info->pcp_to_vl_uc));
	memcpy(info->pcp_to_sc_mc, src->pcp_to_sc_mc,
	       ARRAY_SIZE(info->pcp_to_sc_mc));
	memcpy(info->pcp_to_vl_mc, src->pcp_to_vl_mc,
	       ARRAY_SIZE(info->pcp_to_vl_mc));
	info->non_vlan_sc_uc = src->non_vlan_sc_uc;
	info->non_vlan_vl_uc = src->non_vlan_vl_uc;
	info->non_vlan_sc_mc = src->non_vlan_sc_mc;
	info->non_vlan_vl_mc = src->non_vlan_vl_mc;
	memcpy(info->rsvd2, src->rsvd2, ARRAY_SIZE(src->rsvd2));

	info->uc_macs_gen_count = cpu_to_be16(src->uc_macs_gen_count);
	info->mc_macs_gen_count = cpu_to_be16(src->mc_macs_gen_count);
	memcpy(info->rsvd3, src->rsvd3, ARRAY_SIZE(src->rsvd3));
}

/**
 * opa_vnic_set_per_veswport_info -- Set vesw per port information
 * @adapter: vnic port adapter
 * @info: pointer to vport info structure
 *
 * This function updates the vesw per port info that is maintained by the
 * given adapter with vesw per port info provided. Reserved fields are
 * stored and returned back to EM as is.
 */
void opa_vnic_set_per_veswport_info(struct opa_vnic_adapter *adapter,
				    struct opa_per_veswport_info *info)
{
	struct __opa_per_veswport_info *dst = &adapter->info.vport;

	dst->port_num = be32_to_cpu(info->port_num);
	memcpy(dst->rsvd0, info->rsvd0, ARRAY_SIZE(info->rsvd0));

	memcpy(dst->base_mac_addr, info->base_mac_addr,
	       ARRAY_SIZE(dst->base_mac_addr));
	dst->config_state = info->config_state;
	memcpy(dst->rsvd1, info->rsvd1, ARRAY_SIZE(info->rsvd1));

	dst->encap_slid = be32_to_cpu(info->encap_slid);
	memcpy(dst->pcp_to_sc_uc, info->pcp_to_sc_uc,
	       ARRAY_SIZE(dst->pcp_to_sc_uc));
	memcpy(dst->pcp_to_vl_uc, info->pcp_to_vl_uc,
	       ARRAY_SIZE(dst->pcp_to_vl_uc));
	memcpy(dst->pcp_to_sc_mc, info->pcp_to_sc_mc,
	       ARRAY_SIZE(dst->pcp_to_sc_mc));
	memcpy(dst->pcp_to_vl_mc, info->pcp_to_vl_mc,
	       ARRAY_SIZE(dst->pcp_to_vl_mc));
	dst->non_vlan_sc_uc = info->non_vlan_sc_uc;
	dst->non_vlan_vl_uc = info->non_vlan_vl_uc;
	dst->non_vlan_sc_mc = info->non_vlan_sc_mc;
	dst->non_vlan_vl_mc = info->non_vlan_vl_mc;
	memcpy(dst->rsvd2, info->rsvd2, ARRAY_SIZE(info->rsvd2));
	memcpy(dst->rsvd3, info->rsvd3, ARRAY_SIZE(info->rsvd3));
}

/**
 * opa_vnic_query_mcast_macs - query multicast mac list
 * @adapter: vnic port adapter
 * @macs: pointer mac list
 *
 * This function populates the provided mac list with the configured
 * multicast addresses in the adapter.
 */
void opa_vnic_query_mcast_macs(struct opa_vnic_adapter *adapter,
			       struct opa_veswport_iface_macs *macs)
{
	u16 start_idx, num_macs, idx = 0, count = 0;
	struct netdev_hw_addr *ha;

	start_idx = be16_to_cpu(macs->start_idx);
	num_macs = be16_to_cpu(macs->num_macs_in_msg);
	netdev_for_each_mc_addr(ha, adapter->netdev) {
		struct opa_vnic_iface_mac_entry *entry = &macs->entry[count];

		if (start_idx > idx++)
			continue;
		else if (num_macs == count)
			break;
		memcpy(entry, ha->addr, sizeof(*entry));
		count++;
	}

	macs->tot_macs_in_lst = cpu_to_be16(netdev_mc_count(adapter->netdev));
	macs->num_macs_in_msg = cpu_to_be16(count);
	macs->gen_count = cpu_to_be16(adapter->info.vport.mc_macs_gen_count);
}

/**
 * opa_vnic_query_ucast_macs - query unicast mac list
 * @adapter: vnic port adapter
 * @macs: pointer mac list
 *
 * This function populates the provided mac list with the configured
 * unicast addresses in the adapter.
 */
void opa_vnic_query_ucast_macs(struct opa_vnic_adapter *adapter,
			       struct opa_veswport_iface_macs *macs)
{
	u16 start_idx, tot_macs, num_macs, idx = 0, count = 0;
	struct netdev_hw_addr *ha;

	start_idx = be16_to_cpu(macs->start_idx);
	num_macs = be16_to_cpu(macs->num_macs_in_msg);
	/* loop through dev_addrs list first */
	for_each_dev_addr(adapter->netdev, ha) {
		struct opa_vnic_iface_mac_entry *entry = &macs->entry[count];

		/* Do not include EM specified MAC address */
		if (!memcmp(adapter->info.vport.base_mac_addr, ha->addr,
			    ARRAY_SIZE(adapter->info.vport.base_mac_addr)))
			continue;

		if (start_idx > idx++)
			continue;
		else if (num_macs == count)
			break;
		memcpy(entry, ha->addr, sizeof(*entry));
		count++;
	}

	/* loop through uc list */
	netdev_for_each_uc_addr(ha, adapter->netdev) {
		struct opa_vnic_iface_mac_entry *entry = &macs->entry[count];

		if (start_idx > idx++)
			continue;
		else if (num_macs == count)
			break;
		memcpy(entry, ha->addr, sizeof(*entry));
		count++;
	}

	tot_macs = netdev_hw_addr_list_count(&adapter->netdev->dev_addrs) +
		   netdev_uc_count(adapter->netdev);
	macs->tot_macs_in_lst = cpu_to_be16(tot_macs);
	macs->num_macs_in_msg = cpu_to_be16(count);
	macs->gen_count = cpu_to_be16(adapter->info.vport.uc_macs_gen_count);
}
