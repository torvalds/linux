#ifndef _OPA_VNIC_ENCAP_H
#define _OPA_VNIC_ENCAP_H
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
 * This file contains all OPA VNIC declaration required for encapsulation
 * and decapsulation of Ethernet packets
 */

#include <linux/types.h>
#include <rdma/ib_mad.h>

/* EMA class version */
#define OPA_EMA_CLASS_VERSION               0x80

/*
 * Define the Intel vendor management class for OPA
 * ETHERNET MANAGEMENT
 */
#define OPA_MGMT_CLASS_INTEL_EMA            0x34

/* EM attribute IDs */
#define OPA_EM_ATTR_CLASS_PORT_INFO                 0x0001
#define OPA_EM_ATTR_VESWPORT_INFO                   0x0011
#define OPA_EM_ATTR_VESWPORT_MAC_ENTRIES            0x0012
#define OPA_EM_ATTR_IFACE_UCAST_MACS                0x0013
#define OPA_EM_ATTR_IFACE_MCAST_MACS                0x0014
#define OPA_EM_ATTR_DELETE_VESW                     0x0015
#define OPA_EM_ATTR_VESWPORT_SUMMARY_COUNTERS       0x0020
#define OPA_EM_ATTR_VESWPORT_ERROR_COUNTERS         0x0022

/* VNIC configured and operational state values */
#define OPA_VNIC_STATE_DROP_ALL        0x1
#define OPA_VNIC_STATE_FORWARDING      0x3

#define OPA_VESW_MAX_NUM_DEF_PORT   16
#define OPA_VNIC_MAX_NUM_PCP        8

#define OPA_VNIC_EMA_DATA    (OPA_MGMT_MAD_SIZE - IB_MGMT_VENDOR_HDR)

/* Defines for vendor specific notice(trap) attributes */
#define OPA_INTEL_EMA_NOTICE_TYPE_INFO 0x04

/* INTEL OUI */
#define INTEL_OUI_1 0x00
#define INTEL_OUI_2 0x06
#define INTEL_OUI_3 0x6a

/* Trap opcodes sent from VNIC */
#define OPA_VESWPORT_TRAP_IFACE_UCAST_MAC_CHANGE 0x1
#define OPA_VESWPORT_TRAP_IFACE_MCAST_MAC_CHANGE 0x2
#define OPA_VESWPORT_TRAP_ETH_LINK_STATUS_CHANGE 0x3

#define OPA_VNIC_DLID_SD_IS_SRC_MAC(dlid_sd)  (!!((dlid_sd) & 0x20))
#define OPA_VNIC_DLID_SD_GET_DLID(dlid_sd)    ((dlid_sd) >> 8)

/* VNIC Ethernet link status */
#define OPA_VNIC_ETH_LINK_UP     1
#define OPA_VNIC_ETH_LINK_DOWN   2

/* routing control */
#define OPA_VNIC_ENCAP_RC_DEFAULT   0
#define OPA_VNIC_ENCAP_RC_IPV4      4
#define OPA_VNIC_ENCAP_RC_IPV4_UDP  8
#define OPA_VNIC_ENCAP_RC_IPV4_TCP  12
#define OPA_VNIC_ENCAP_RC_IPV6      16
#define OPA_VNIC_ENCAP_RC_IPV6_TCP  20
#define OPA_VNIC_ENCAP_RC_IPV6_UDP  24

#define OPA_VNIC_ENCAP_RC_EXT(w, b) (((w) >> OPA_VNIC_ENCAP_RC_ ## b) & 0x7)

/**
 * struct opa_vesw_info - OPA vnic switch information
 * @fabric_id: 10-bit fabric id
 * @vesw_id: 12-bit virtual ethernet switch id
 * @def_port_mask: bitmask of default ports
 * @pkey: partition key
 * @u_mcast_dlid: unknown multicast dlid
 * @u_ucast_dlid: array of unknown unicast dlids
 * @rc: routing control
 * @eth_mtu: Ethernet MTU
 */
struct opa_vesw_info {
	__be16  fabric_id;
	__be16  vesw_id;

	u8      rsvd0[6];
	__be16  def_port_mask;

	u8      rsvd1[2];
	__be16  pkey;

	u8      rsvd2[4];
	__be32  u_mcast_dlid;
	__be32  u_ucast_dlid[OPA_VESW_MAX_NUM_DEF_PORT];

	__be32  rc;

	u8      rsvd3[56];
	__be16  eth_mtu;
	u8      rsvd4[2];
} __packed;

/**
 * struct opa_per_veswport_info - OPA vnic per port information
 * @port_num: port number
 * @eth_link_status: current ethernet link state
 * @base_mac_addr: base mac address
 * @config_state: configured port state
 * @oper_state: operational port state
 * @max_mac_tbl_ent: max number of mac table entries
 * @max_smac_ent: max smac entries in mac table
 * @mac_tbl_digest: mac table digest
 * @encap_slid: base slid for the port
 * @pcp_to_sc_uc: sc by pcp index for unicast ethernet packets
 * @pcp_to_vl_uc: vl by pcp index for unicast ethernet packets
 * @pcp_to_sc_mc: sc by pcp index for multicast ethernet packets
 * @pcp_to_vl_mc: vl by pcp index for multicast ethernet packets
 * @non_vlan_sc_uc: sc for non-vlan unicast ethernet packets
 * @non_vlan_vl_uc: vl for non-vlan unicast ethernet packets
 * @non_vlan_sc_mc: sc for non-vlan multicast ethernet packets
 * @non_vlan_vl_mc: vl for non-vlan multicast ethernet packets
 * @uc_macs_gen_count: generation count for unicast macs list
 * @mc_macs_gen_count: generation count for multicast macs list
 */
struct opa_per_veswport_info {
	__be32  port_num;

	u8      eth_link_status;
	u8      rsvd0[3];

	u8      base_mac_addr[ETH_ALEN];
	u8      config_state;
	u8      oper_state;

	__be16  max_mac_tbl_ent;
	__be16  max_smac_ent;
	__be32  mac_tbl_digest;
	u8      rsvd1[4];

	__be32  encap_slid;

	u8      pcp_to_sc_uc[OPA_VNIC_MAX_NUM_PCP];
	u8      pcp_to_vl_uc[OPA_VNIC_MAX_NUM_PCP];
	u8      pcp_to_sc_mc[OPA_VNIC_MAX_NUM_PCP];
	u8      pcp_to_vl_mc[OPA_VNIC_MAX_NUM_PCP];

	u8      non_vlan_sc_uc;
	u8      non_vlan_vl_uc;
	u8      non_vlan_sc_mc;
	u8      non_vlan_vl_mc;

	u8      rsvd2[48];

	__be16  uc_macs_gen_count;
	__be16  mc_macs_gen_count;

	u8      rsvd3[8];
} __packed;

/**
 * struct opa_veswport_info - OPA vnic port information
 * @vesw: OPA vnic switch information
 * @vport: OPA vnic per port information
 *
 * On host, each of the virtual ethernet ports belongs
 * to a different virtual ethernet switches.
 */
struct opa_veswport_info {
	struct opa_vesw_info          vesw;
	struct opa_per_veswport_info  vport;
};

/**
 * struct opa_veswport_mactable_entry - single entry in the forwarding table
 * @mac_addr: MAC address
 * @mac_addr_mask: MAC address bit mask
 * @dlid_sd: Matching DLID and side data
 *
 * On the host each virtual ethernet port will have
 * a forwarding table. These tables are used to
 * map a MAC to a LID and other data. For more
 * details see struct opa_veswport_mactable_entries.
 * This is the structure of a single mactable entry
 */
struct opa_veswport_mactable_entry {
	u8      mac_addr[ETH_ALEN];
	u8      mac_addr_mask[ETH_ALEN];
	__be32  dlid_sd;
} __packed;

/**
 * struct opa_veswport_mactable - Forwarding table array
 * @offset: mac table starting offset
 * @num_entries: Number of entries to get or set
 * @mac_tbl_digest: mac table digest
 * @tbl_entries: Array of table entries
 *
 * The EM sends down this structure in a MAD indicating
 * the starting offset in the forwarding table that this
 * entry is to be loaded into and the number of entries
 * that that this MAD instance contains
 * The mac_tbl_digest has been added to this MAD structure. It will be set by
 * the EM and it will be used by the EM to check if there are any
 * discrepancies with this value and the value
 * maintained by the EM in the case of VNIC port being deleted or unloaded
 * A new instantiation of a VNIC will always have a value of zero.
 * This value is stored as part of the vnic adapter structure and will be
 * accessed by the GET and SET routines for both the mactable entries and the
 * veswport info.
 */
struct opa_veswport_mactable {
	__be16                              offset;
	__be16                              num_entries;
	__be32                              mac_tbl_digest;
	struct opa_veswport_mactable_entry  tbl_entries[];
} __packed;

/**
 * struct opa_veswport_summary_counters - summary counters
 * @vp_instance: vport instance on the OPA port
 * @vesw_id: virtual ethernet switch id
 * @veswport_num: virtual ethernet switch port number
 * @tx_errors: transmit errors
 * @rx_errors: receive errors
 * @tx_packets: transmit packets
 * @rx_packets: receive packets
 * @tx_bytes: transmit bytes
 * @rx_bytes: receive bytes
 * @tx_unicast: unicast packets transmitted
 * @tx_mcastbcast: multicast/broadcast packets transmitted
 * @tx_untagged: non-vlan packets transmitted
 * @tx_vlan: vlan packets transmitted
 * @tx_64_size: transmit packet length is 64 bytes
 * @tx_65_127: transmit packet length is >=65 and < 127 bytes
 * @tx_128_255: transmit packet length is >=128 and < 255 bytes
 * @tx_256_511: transmit packet length is >=256 and < 511 bytes
 * @tx_512_1023: transmit packet length is >=512 and < 1023 bytes
 * @tx_1024_1518: transmit packet length is >=1024 and < 1518 bytes
 * @tx_1519_max: transmit packet length >= 1519 bytes
 * @rx_unicast: unicast packets received
 * @rx_mcastbcast: multicast/broadcast packets received
 * @rx_untagged: non-vlan packets received
 * @rx_vlan: vlan packets received
 * @rx_64_size: received packet length is 64 bytes
 * @rx_65_127: received packet length is >=65 and < 127 bytes
 * @rx_128_255: received packet length is >=128 and < 255 bytes
 * @rx_256_511: received packet length is >=256 and < 511 bytes
 * @rx_512_1023: received packet length is >=512 and < 1023 bytes
 * @rx_1024_1518: received packet length is >=1024 and < 1518 bytes
 * @rx_1519_max: received packet length >= 1519 bytes
 *
 * All the above are counters of corresponding conditions.
 */
struct opa_veswport_summary_counters {
	__be16  vp_instance;
	__be16  vesw_id;
	__be32  veswport_num;

	__be64  tx_errors;
	__be64  rx_errors;
	__be64  tx_packets;
	__be64  rx_packets;
	__be64  tx_bytes;
	__be64  rx_bytes;

	__be64  tx_unicast;
	__be64  tx_mcastbcast;

	__be64  tx_untagged;
	__be64  tx_vlan;

	__be64  tx_64_size;
	__be64  tx_65_127;
	__be64  tx_128_255;
	__be64  tx_256_511;
	__be64  tx_512_1023;
	__be64  tx_1024_1518;
	__be64  tx_1519_max;

	__be64  rx_unicast;
	__be64  rx_mcastbcast;

	__be64  rx_untagged;
	__be64  rx_vlan;

	__be64  rx_64_size;
	__be64  rx_65_127;
	__be64  rx_128_255;
	__be64  rx_256_511;
	__be64  rx_512_1023;
	__be64  rx_1024_1518;
	__be64  rx_1519_max;

	__be64  reserved[16];
} __packed;

/**
 * struct opa_veswport_error_counters - error counters
 * @vp_instance: vport instance on the OPA port
 * @vesw_id: virtual ethernet switch id
 * @veswport_num: virtual ethernet switch port number
 * @tx_errors: transmit errors
 * @rx_errors: receive errors
 * @tx_smac_filt: smac filter errors
 * @tx_dlid_zero: transmit packets with invalid dlid
 * @tx_logic: other transmit errors
 * @tx_drop_state: packet tansmission in non-forward port state
 * @rx_bad_veswid: received packet with invalid vesw id
 * @rx_runt: received ethernet packet with length < 64 bytes
 * @rx_oversize: received ethernet packet with length > MTU size
 * @rx_eth_down: received packets when interface is down
 * @rx_drop_state: received packets in non-forwarding port state
 * @rx_logic: other receive errors
 *
 * All the above are counters of corresponding error conditions.
 */
struct opa_veswport_error_counters {
	__be16  vp_instance;
	__be16  vesw_id;
	__be32  veswport_num;

	__be64  tx_errors;
	__be64  rx_errors;

	__be64  rsvd0;
	__be64  tx_smac_filt;
	__be64  rsvd1;
	__be64  rsvd2;
	__be64  rsvd3;
	__be64  tx_dlid_zero;
	__be64  rsvd4;
	__be64  tx_logic;
	__be64  rsvd5;
	__be64  tx_drop_state;

	__be64  rx_bad_veswid;
	__be64  rsvd6;
	__be64  rx_runt;
	__be64  rx_oversize;
	__be64  rsvd7;
	__be64  rx_eth_down;
	__be64  rx_drop_state;
	__be64  rx_logic;
	__be64  rsvd8;

	__be64  rsvd9[16];
} __packed;

/**
 * struct opa_veswport_trap - Trap message sent to EM by VNIC
 * @fabric_id: 10 bit fabric id
 * @veswid: 12 bit virtual ethernet switch id
 * @veswportnum: logical port number on the Virtual switch
 * @opaportnum: physical port num (redundant on host)
 * @veswportindex: switch port index on opa port 0 based
 * @opcode: operation
 * @reserved: 32 bit for alignment
 *
 * The VNIC will send trap messages to the Ethernet manager to
 * inform it about changes to the VNIC config, behaviour etc.
 * This is the format of the trap payload.
 */
struct opa_veswport_trap {
	__be16  fabric_id;
	__be16  veswid;
	__be32  veswportnum;
	__be16  opaportnum;
	u8      veswportindex;
	u8      opcode;
	__be32  reserved;
} __packed;

/**
 * struct opa_vnic_iface_macs_entry - single entry in the mac list
 * @mac_addr: MAC address
 */
struct opa_vnic_iface_mac_entry {
	u8 mac_addr[ETH_ALEN];
};

/**
 * struct opa_veswport_iface_macs - Msg to set globally administered MAC
 * @start_idx: position of first entry (0 based)
 * @num_macs_in_msg: number of MACs in this message
 * @tot_macs_in_lst: The total number of MACs the agent has
 * @gen_count: gen_count to indicate change
 * @entry: The mac list entry
 *
 * Same attribute IDS and attribute modifiers as in locally administered
 * addresses used to set globally administered addresses
 */
struct opa_veswport_iface_macs {
	__be16 start_idx;
	__be16 num_macs_in_msg;
	__be16 tot_macs_in_lst;
	__be16 gen_count;
	struct opa_vnic_iface_mac_entry entry[];
} __packed;

/**
 * struct opa_vnic_vema_mad - Generic VEMA MAD
 * @mad_hdr: Generic MAD header
 * @rmpp_hdr: RMPP header for vendor specific MADs
 * @oui: Unique org identifier
 * @data: MAD data
 */
struct opa_vnic_vema_mad {
	struct ib_mad_hdr  mad_hdr;
	struct ib_rmpp_hdr rmpp_hdr;
	u8                 reserved;
	u8                 oui[3];
	u8                 data[OPA_VNIC_EMA_DATA];
};

/**
 * struct opa_vnic_notice_attr - Generic Notice MAD
 * @gen_type: Generic/Specific bit and type of notice
 * @oui_1: Vendor ID byte 1
 * @oui_2: Vendor ID byte 2
 * @oui_3: Vendor ID byte 3
 * @trap_num: Trap number
 * @toggle_count: Notice toggle bit and count value
 * @issuer_lid: Trap issuer's lid
 * @issuer_gid: Issuer GID (only if Report method)
 * @raw_data: Trap message body
 */
struct opa_vnic_notice_attr {
	u8     gen_type;
	u8     oui_1;
	u8     oui_2;
	u8     oui_3;
	__be16 trap_num;
	__be16 toggle_count;
	__be32 issuer_lid;
	__be32 reserved;
	u8     issuer_gid[16];
	u8     raw_data[64];
} __packed;

/**
 * struct opa_vnic_vema_mad_trap - Generic VEMA MAD Trap
 * @mad_hdr: Generic MAD header
 * @rmpp_hdr: RMPP header for vendor specific MADs
 * @oui: Unique org identifier
 * @notice: Notice structure
 */
struct opa_vnic_vema_mad_trap {
	struct ib_mad_hdr            mad_hdr;
	struct ib_rmpp_hdr           rmpp_hdr;
	u8                           reserved;
	u8                           oui[3];
	struct opa_vnic_notice_attr  notice;
};

#endif /* _OPA_VNIC_ENCAP_H */
