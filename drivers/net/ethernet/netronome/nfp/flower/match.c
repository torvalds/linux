/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/bitfield.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"

static void
nfp_flower_compile_meta_tci(struct nfp_flower_meta_two *frame,
			    struct tc_cls_flower_offload *flow, u8 key_type,
			    bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_vlan *flow_vlan;
	u16 tmp_tci;

	memset(frame, 0, sizeof(struct nfp_flower_meta_two));
	/* Populate the metadata frame. */
	frame->nfp_flow_key_layer = key_type;
	frame->mask_id = ~0;

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		flow_vlan = skb_flow_dissector_target(flow->dissector,
						      FLOW_DISSECTOR_KEY_VLAN,
						      target);
		/* Populate the tci field. */
		if (flow_vlan->vlan_id) {
			tmp_tci = FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
					     flow_vlan->vlan_priority) |
				  FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
					     flow_vlan->vlan_id) |
				  NFP_FLOWER_MASK_VLAN_CFI;
			frame->tci = cpu_to_be16(tmp_tci);
		}
	}
}

static void
nfp_flower_compile_meta(struct nfp_flower_meta_one *frame, u8 key_type)
{
	frame->nfp_flow_key_layer = key_type;
	frame->mask_id = 0;
	frame->reserved = 0;
}

static int
nfp_flower_compile_port(struct nfp_flower_in_port *frame, u32 cmsg_port,
			bool mask_version, enum nfp_flower_tun_type tun_type)
{
	if (mask_version) {
		frame->in_port = cpu_to_be32(~0);
		return 0;
	}

	if (tun_type)
		frame->in_port = cpu_to_be32(NFP_FL_PORT_TYPE_TUN | tun_type);
	else
		frame->in_port = cpu_to_be32(cmsg_port);

	return 0;
}

static void
nfp_flower_compile_mac(struct nfp_flower_mac_mpls *frame,
		       struct tc_cls_flower_offload *flow,
		       bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_eth_addrs *addr;

	memset(frame, 0, sizeof(struct nfp_flower_mac_mpls));

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_ETH_ADDRS,
						 target);
		/* Populate mac frame. */
		ether_addr_copy(frame->mac_dst, &addr->dst[0]);
		ether_addr_copy(frame->mac_src, &addr->src[0]);
	}

	if (mask_version)
		frame->mpls_lse = cpu_to_be32(~0);
}

static void
nfp_flower_compile_tport(struct nfp_flower_tp_ports *frame,
			 struct tc_cls_flower_offload *flow,
			 bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ports *tp;

	memset(frame, 0, sizeof(struct nfp_flower_tp_ports));

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		tp = skb_flow_dissector_target(flow->dissector,
					       FLOW_DISSECTOR_KEY_PORTS,
					       target);
		frame->port_src = tp->src;
		frame->port_dst = tp->dst;
	}
}

static void
nfp_flower_compile_ipv4(struct nfp_flower_ipv4 *frame,
			struct tc_cls_flower_offload *flow,
			bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv4_addrs *addr;
	struct flow_dissector_key_basic *basic;

	/* Wildcard TOS/TTL for now. */
	memset(frame, 0, sizeof(struct nfp_flower_ipv4));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						 target);
		frame->ipv4_src = addr->src;
		frame->ipv4_dst = addr->dst;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		basic = skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  target);
		frame->proto = basic->ip_proto;
	}
}

static void
nfp_flower_compile_ipv6(struct nfp_flower_ipv6 *frame,
			struct tc_cls_flower_offload *flow,
			bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv6_addrs *addr;
	struct flow_dissector_key_basic *basic;

	/* Wildcard LABEL/TOS/TTL for now. */
	memset(frame, 0, sizeof(struct nfp_flower_ipv6));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						 target);
		frame->ipv6_src = addr->src;
		frame->ipv6_dst = addr->dst;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		basic = skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  target);
		frame->proto = basic->ip_proto;
	}
}

static void
nfp_flower_compile_vxlan(struct nfp_flower_vxlan *frame,
			 struct tc_cls_flower_offload *flow,
			 bool mask_version, __be32 *tun_dst)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv4_addrs *vxlan_ips;
	struct flow_dissector_key_keyid *vni;

	/* Wildcard TOS/TTL/GPE_FLAGS/NXT_PROTO for now. */
	memset(frame, 0, sizeof(struct nfp_flower_vxlan));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		u32 temp_vni;

		vni = skb_flow_dissector_target(flow->dissector,
						FLOW_DISSECTOR_KEY_ENC_KEYID,
						target);
		temp_vni = be32_to_cpu(vni->keyid) << NFP_FL_TUN_VNI_OFFSET;
		frame->tun_id = cpu_to_be32(temp_vni);
	}

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		vxlan_ips =
		   skb_flow_dissector_target(flow->dissector,
					     FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
					     target);
		frame->ip_src = vxlan_ips->src;
		frame->ip_dst = vxlan_ips->dst;
		*tun_dst = vxlan_ips->dst;
	}
}

int nfp_flower_compile_flow_match(struct tc_cls_flower_offload *flow,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow)
{
	enum nfp_flower_tun_type tun_type = NFP_FL_TUNNEL_NONE;
	__be32 tun_dst, tun_dst_mask = 0;
	struct nfp_repr *netdev_repr;
	int err;
	u8 *ext;
	u8 *msk;

	if (key_ls->key_layer & NFP_FLOWER_LAYER_VXLAN)
		tun_type = NFP_FL_TUNNEL_VXLAN;

	memset(nfp_flow->unmasked_data, 0, key_ls->key_size);
	memset(nfp_flow->mask_data, 0, key_ls->key_size);

	ext = nfp_flow->unmasked_data;
	msk = nfp_flow->mask_data;
	if (NFP_FLOWER_LAYER_PORT & key_ls->key_layer) {
		/* Populate Exact Metadata. */
		nfp_flower_compile_meta_tci((struct nfp_flower_meta_two *)ext,
					    flow, key_ls->key_layer, false);
		/* Populate Mask Metadata. */
		nfp_flower_compile_meta_tci((struct nfp_flower_meta_two *)msk,
					    flow, key_ls->key_layer, true);
		ext += sizeof(struct nfp_flower_meta_two);
		msk += sizeof(struct nfp_flower_meta_two);

		/* Populate Exact Port data. */
		err = nfp_flower_compile_port((struct nfp_flower_in_port *)ext,
					      nfp_repr_get_port_id(netdev),
					      false, tun_type);
		if (err)
			return err;

		/* Populate Mask Port Data. */
		err = nfp_flower_compile_port((struct nfp_flower_in_port *)msk,
					      nfp_repr_get_port_id(netdev),
					      true, tun_type);
		if (err)
			return err;

		ext += sizeof(struct nfp_flower_in_port);
		msk += sizeof(struct nfp_flower_in_port);
	} else {
		/* Populate Exact Metadata. */
		nfp_flower_compile_meta((struct nfp_flower_meta_one *)ext,
					key_ls->key_layer);
		/* Populate Mask Metadata. */
		nfp_flower_compile_meta((struct nfp_flower_meta_one *)msk,
					key_ls->key_layer);
		ext += sizeof(struct nfp_flower_meta_one);
		msk += sizeof(struct nfp_flower_meta_one);
	}

	if (NFP_FLOWER_LAYER_META & key_ls->key_layer) {
		/* Additional Metadata Fields.
		 * Currently unsupported.
		 */
		return -EOPNOTSUPP;
	}

	if (NFP_FLOWER_LAYER_MAC & key_ls->key_layer) {
		/* Populate Exact MAC Data. */
		nfp_flower_compile_mac((struct nfp_flower_mac_mpls *)ext,
				       flow, false);
		/* Populate Mask MAC Data. */
		nfp_flower_compile_mac((struct nfp_flower_mac_mpls *)msk,
				       flow, true);
		ext += sizeof(struct nfp_flower_mac_mpls);
		msk += sizeof(struct nfp_flower_mac_mpls);
	}

	if (NFP_FLOWER_LAYER_TP & key_ls->key_layer) {
		/* Populate Exact TP Data. */
		nfp_flower_compile_tport((struct nfp_flower_tp_ports *)ext,
					 flow, false);
		/* Populate Mask TP Data. */
		nfp_flower_compile_tport((struct nfp_flower_tp_ports *)msk,
					 flow, true);
		ext += sizeof(struct nfp_flower_tp_ports);
		msk += sizeof(struct nfp_flower_tp_ports);
	}

	if (NFP_FLOWER_LAYER_IPV4 & key_ls->key_layer) {
		/* Populate Exact IPv4 Data. */
		nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)ext,
					flow, false);
		/* Populate Mask IPv4 Data. */
		nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)msk,
					flow, true);
		ext += sizeof(struct nfp_flower_ipv4);
		msk += sizeof(struct nfp_flower_ipv4);
	}

	if (NFP_FLOWER_LAYER_IPV6 & key_ls->key_layer) {
		/* Populate Exact IPv4 Data. */
		nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)ext,
					flow, false);
		/* Populate Mask IPv4 Data. */
		nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)msk,
					flow, true);
		ext += sizeof(struct nfp_flower_ipv6);
		msk += sizeof(struct nfp_flower_ipv6);
	}

	if (key_ls->key_layer & NFP_FLOWER_LAYER_VXLAN) {
		/* Populate Exact VXLAN Data. */
		nfp_flower_compile_vxlan((struct nfp_flower_vxlan *)ext,
					 flow, false, &tun_dst);
		/* Populate Mask VXLAN Data. */
		nfp_flower_compile_vxlan((struct nfp_flower_vxlan *)msk,
					 flow, true, &tun_dst_mask);
		ext += sizeof(struct nfp_flower_vxlan);
		msk += sizeof(struct nfp_flower_vxlan);

		/* Configure tunnel end point MAC. */
		if (nfp_netdev_is_nfp_repr(netdev)) {
			netdev_repr = netdev_priv(netdev);
			nfp_tunnel_write_macs(netdev_repr->app);

			/* Store the tunnel destination in the rule data.
			 * This must be present and be an exact match.
			 */
			nfp_flow->nfp_tun_ipv4_addr = tun_dst;
			nfp_tunnel_add_ipv4_off(netdev_repr->app, tun_dst);
		}
	}

	return 0;
}
