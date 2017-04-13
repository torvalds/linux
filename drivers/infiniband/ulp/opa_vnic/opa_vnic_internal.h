#ifndef _OPA_VNIC_INTERNAL_H
#define _OPA_VNIC_INTERNAL_H
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
 * This file contains OPA VNIC driver internal declarations
 */

#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/hashtable.h>
#include <linux/sizes.h>
#include <rdma/opa_vnic.h>

#include "opa_vnic_encap.h"

#define OPA_VNIC_VLAN_PCP(vlan_tci)  \
			(((vlan_tci) & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT)

/* Flow to default port redirection table size */
#define OPA_VNIC_FLOW_TBL_SIZE    32

/* Invalid port number */
#define OPA_VNIC_INVALID_PORT     0xff

struct opa_vnic_adapter;

/**
 * struct __opa_vesw_info - OPA vnic virtual switch info
 *
 * Same as opa_vesw_info without bitwise attribute.
 */
struct __opa_vesw_info {
	u16  fabric_id;
	u16  vesw_id;

	u8   rsvd0[6];
	u16  def_port_mask;

	u8   rsvd1[2];
	u16  pkey;

	u8   rsvd2[4];
	u32  u_mcast_dlid;
	u32  u_ucast_dlid[OPA_VESW_MAX_NUM_DEF_PORT];

	u8   rsvd3[44];
	u16  eth_mtu[OPA_VNIC_MAX_NUM_PCP];
	u16  eth_mtu_non_vlan;
	u8   rsvd4[2];
} __packed;

/**
 * struct __opa_per_veswport_info - OPA vnic per port info
 *
 * Same as opa_per_veswport_info without bitwise attribute.
 */
struct __opa_per_veswport_info {
	u32  port_num;

	u8   eth_link_status;
	u8   rsvd0[3];

	u8   base_mac_addr[ETH_ALEN];
	u8   config_state;
	u8   oper_state;

	u16  max_mac_tbl_ent;
	u16  max_smac_ent;
	u32  mac_tbl_digest;
	u8   rsvd1[4];

	u32  encap_slid;

	u8   pcp_to_sc_uc[OPA_VNIC_MAX_NUM_PCP];
	u8   pcp_to_vl_uc[OPA_VNIC_MAX_NUM_PCP];
	u8   pcp_to_sc_mc[OPA_VNIC_MAX_NUM_PCP];
	u8   pcp_to_vl_mc[OPA_VNIC_MAX_NUM_PCP];

	u8   non_vlan_sc_uc;
	u8   non_vlan_vl_uc;
	u8   non_vlan_sc_mc;
	u8   non_vlan_vl_mc;

	u8   rsvd2[48];

	u16  uc_macs_gen_count;
	u16  mc_macs_gen_count;

	u8   rsvd3[8];
} __packed;

/**
 * struct __opa_veswport_info - OPA vnic port info
 *
 * Same as opa_veswport_info without bitwise attribute.
 */
struct __opa_veswport_info {
	struct __opa_vesw_info            vesw;
	struct __opa_per_veswport_info    vport;
};

/**
 * struct __opa_veswport_trap - OPA vnic trap info
 *
 * Same as opa_veswport_trap without bitwise attribute.
 */
struct __opa_veswport_trap {
	u16	fabric_id;
	u16	veswid;
	u32	veswportnum;
	u16	opaportnum;
	u8	veswportindex;
	u8	opcode;
	u32	reserved;
} __packed;

/**
 * struct opa_vnic_adapter - OPA VNIC netdev private data structure
 * @netdev: pointer to associated netdev
 * @ibdev: ib device
 * @rn_ops: rdma netdev's net_device_ops
 * @port_num: OPA port number
 * @vport_num: vesw port number
 * @lock: adapter lock
 * @info: virtual ethernet switch port information
 * @stats_lock: statistics lock
 * @flow_tbl: flow to default port redirection table
 */
struct opa_vnic_adapter {
	struct net_device             *netdev;
	struct ib_device              *ibdev;
	const struct net_device_ops   *rn_ops;

	u8 port_num;
	u8 vport_num;

	/* Lock used around concurrent updates to netdev */
	struct mutex lock;

	struct __opa_veswport_info  info;

	/* Lock used to protect access to vnic counters */
	struct mutex stats_lock;

	u8 flow_tbl[OPA_VNIC_FLOW_TBL_SIZE];
};

#define v_dbg(format, arg...) \
	netdev_dbg(adapter->netdev, format, ## arg)
#define v_err(format, arg...) \
	netdev_err(adapter->netdev, format, ## arg)
#define v_info(format, arg...) \
	netdev_info(adapter->netdev, format, ## arg)
#define v_warn(format, arg...) \
	netdev_warn(adapter->netdev, format, ## arg)

/* The maximum allowed entries in the mac table */
#define OPA_VNIC_MAC_TBL_MAX_ENTRIES  2048
/* Limit of smac entries in mac table */
#define OPA_VNIC_MAX_SMAC_LIMIT       256

/* The last octet of the MAC address is used as the key to the hash table */
#define OPA_VNIC_MAC_HASH_IDX         5

/* The VNIC MAC hash table is of size 2^8 */
#define OPA_VNIC_MAC_TBL_HASH_BITS    8
#define OPA_VNIC_MAC_TBL_SIZE  BIT(OPA_VNIC_MAC_TBL_HASH_BITS)

struct opa_vnic_adapter *opa_vnic_add_netdev(struct ib_device *ibdev,
					     u8 port_num, u8 vport_num);
void opa_vnic_rem_netdev(struct opa_vnic_adapter *adapter);
void opa_vnic_encap_skb(struct opa_vnic_adapter *adapter, struct sk_buff *skb);
u8 opa_vnic_get_vl(struct opa_vnic_adapter *adapter, struct sk_buff *skb);
u8 opa_vnic_calc_entropy(struct opa_vnic_adapter *adapter, struct sk_buff *skb);
void opa_vnic_set_ethtool_ops(struct net_device *netdev);

#endif /* _OPA_VNIC_INTERNAL_H */
