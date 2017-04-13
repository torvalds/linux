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
 * This file contains OPA VNIC encapsulation/decapsulation function.
 */

#include <linux/if_ether.h>
#include <linux/if_vlan.h>

#include "opa_vnic_internal.h"

/* OPA 16B Header fields */
#define OPA_16B_LID_MASK        0xFFFFFull
#define OPA_16B_SLID_HIGH_SHFT  8
#define OPA_16B_SLID_MASK       0xF00ull
#define OPA_16B_DLID_MASK       0xF000ull
#define OPA_16B_DLID_HIGH_SHFT  12
#define OPA_16B_LEN_SHFT        20
#define OPA_16B_SC_SHFT         20
#define OPA_16B_RC_SHFT         25
#define OPA_16B_PKEY_SHFT       16

#define OPA_VNIC_L4_HDR_SHFT    16

/* L2+L4 hdr len is 20 bytes (5 quad words) */
#define OPA_VNIC_HDR_QW_LEN   5

static inline void opa_vnic_make_header(u8 *hdr, u32 slid, u32 dlid, u16 len,
					u16 pkey, u16 entropy, u8 sc, u8 rc,
					u8 l4_type, u16 l4_hdr)
{
	/* h[1]: LT=1, 16B L2=10 */
	u32 h[OPA_VNIC_HDR_QW_LEN] = {0, 0xc0000000, 0, 0, 0};

	h[2] = l4_type;
	h[3] = entropy;
	h[4] = l4_hdr << OPA_VNIC_L4_HDR_SHFT;

	/* Extract and set 4 upper bits and 20 lower bits of the lids */
	h[0] |= (slid & OPA_16B_LID_MASK);
	h[2] |= ((slid >> (20 - OPA_16B_SLID_HIGH_SHFT)) & OPA_16B_SLID_MASK);

	h[1] |= (dlid & OPA_16B_LID_MASK);
	h[2] |= ((dlid >> (20 - OPA_16B_DLID_HIGH_SHFT)) & OPA_16B_DLID_MASK);

	h[0] |= (len << OPA_16B_LEN_SHFT);
	h[1] |= (rc << OPA_16B_RC_SHFT);
	h[1] |= (sc << OPA_16B_SC_SHFT);
	h[2] |= ((u32)pkey << OPA_16B_PKEY_SHFT);

	memcpy(hdr, h, OPA_VNIC_HDR_LEN);
}

/* opa_vnic_get_dlid - find and return the DLID */
static uint32_t opa_vnic_get_dlid(struct opa_vnic_adapter *adapter,
				  struct sk_buff *skb, u8 def_port)
{
	struct __opa_veswport_info *info = &adapter->info;
	struct ethhdr *mac_hdr = (struct ethhdr *)skb_mac_header(skb);
	u32 dlid;

	if (is_multicast_ether_addr(mac_hdr->h_dest)) {
		dlid = info->vesw.u_mcast_dlid;
	} else {
		if (is_local_ether_addr(mac_hdr->h_dest)) {
			dlid = ((uint32_t)mac_hdr->h_dest[5] << 16) |
				((uint32_t)mac_hdr->h_dest[4] << 8)  |
				mac_hdr->h_dest[3];
			if (unlikely(!dlid))
				v_warn("Null dlid in MAC address\n");
		} else if (def_port != OPA_VNIC_INVALID_PORT) {
			dlid = info->vesw.u_ucast_dlid[def_port];
		}
	}

	return dlid;
}

/* opa_vnic_get_sc - return the service class */
static u8 opa_vnic_get_sc(struct __opa_veswport_info *info,
			  struct sk_buff *skb)
{
	struct ethhdr *mac_hdr = (struct ethhdr *)skb_mac_header(skb);
	u16 vlan_tci;
	u8 sc;

	if (!__vlan_get_tag(skb, &vlan_tci)) {
		u8 pcp = OPA_VNIC_VLAN_PCP(vlan_tci);

		if (is_multicast_ether_addr(mac_hdr->h_dest))
			sc = info->vport.pcp_to_sc_mc[pcp];
		else
			sc = info->vport.pcp_to_sc_uc[pcp];
	} else {
		if (is_multicast_ether_addr(mac_hdr->h_dest))
			sc = info->vport.non_vlan_sc_mc;
		else
			sc = info->vport.non_vlan_sc_uc;
	}

	return sc;
}

u8 opa_vnic_get_vl(struct opa_vnic_adapter *adapter, struct sk_buff *skb)
{
	struct ethhdr *mac_hdr = (struct ethhdr *)skb_mac_header(skb);
	struct __opa_veswport_info *info = &adapter->info;
	u8 vl;

	if (skb_vlan_tag_present(skb)) {
		u8 pcp = skb_vlan_tag_get(skb) >> VLAN_PRIO_SHIFT;

		if (is_multicast_ether_addr(mac_hdr->h_dest))
			vl = info->vport.pcp_to_vl_mc[pcp];
		else
			vl = info->vport.pcp_to_vl_uc[pcp];
	} else {
		if (is_multicast_ether_addr(mac_hdr->h_dest))
			vl = info->vport.non_vlan_vl_mc;
		else
			vl = info->vport.non_vlan_vl_uc;
	}

	return vl;
}

/* opa_vnic_calc_entropy - calculate the packet entropy */
u8 opa_vnic_calc_entropy(struct opa_vnic_adapter *adapter, struct sk_buff *skb)
{
	u16 hash16;

	/*
	 * Get flow based 16-bit hash and then XOR the upper and lower bytes
	 * to get the entropy.
	 * __skb_tx_hash limits qcount to 16 bits. Hence, get 15-bit hash.
	 */
	hash16 = __skb_tx_hash(adapter->netdev, skb, BIT(15));
	return (u8)((hash16 >> 8) ^ (hash16 & 0xff));
}

/* opa_vnic_get_def_port - get default port based on entropy */
static inline u8 opa_vnic_get_def_port(struct opa_vnic_adapter *adapter,
				       u8 entropy)
{
	u8 flow_id;

	/* Add the upper and lower 4-bits of entropy to get the flow id */
	flow_id = ((entropy & 0xf) + (entropy >> 4));
	return adapter->flow_tbl[flow_id & (OPA_VNIC_FLOW_TBL_SIZE - 1)];
}

/* Calculate packet length including OPA header, crc and padding */
static inline int opa_vnic_wire_length(struct sk_buff *skb)
{
	u32 pad_len;

	/* padding for 8 bytes size alignment */
	pad_len = -(skb->len + OPA_VNIC_ICRC_TAIL_LEN) & 0x7;
	pad_len += OPA_VNIC_ICRC_TAIL_LEN;

	return (skb->len + pad_len) >> 3;
}

/* opa_vnic_encap_skb - encapsulate skb packet with OPA header and meta data */
void opa_vnic_encap_skb(struct opa_vnic_adapter *adapter, struct sk_buff *skb)
{
	struct __opa_veswport_info *info = &adapter->info;
	struct opa_vnic_skb_mdata *mdata;
	u8 def_port, sc, entropy, *hdr;
	u16 len, l4_hdr;
	u32 dlid;

	hdr = skb_push(skb, OPA_VNIC_HDR_LEN);

	entropy = opa_vnic_calc_entropy(adapter, skb);
	def_port = opa_vnic_get_def_port(adapter, entropy);
	len = opa_vnic_wire_length(skb);
	dlid = opa_vnic_get_dlid(adapter, skb, def_port);
	sc = opa_vnic_get_sc(info, skb);
	l4_hdr = info->vesw.vesw_id;

	mdata = (struct opa_vnic_skb_mdata *)skb_push(skb, sizeof(*mdata));
	mdata->vl = opa_vnic_get_vl(adapter, skb);
	mdata->entropy = entropy;
	mdata->flags = 0;
	if (unlikely(!dlid)) {
		mdata->flags = OPA_VNIC_SKB_MDATA_ENCAP_ERR;
		return;
	}

	opa_vnic_make_header(hdr, info->vport.encap_slid, dlid, len,
			     info->vesw.pkey, entropy, sc, 0,
			     OPA_VNIC_L4_ETHR, l4_hdr);
}
