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
nfp_flower_compile_meta_tci(struct nfp_flower_meta_tci *frame,
			    struct tc_cls_flower_offload *flow, u8 key_type,
			    bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_vlan *flow_vlan;
	u16 tmp_tci;

	memset(frame, 0, sizeof(struct nfp_flower_meta_tci));
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
nfp_flower_compile_ext_meta(struct nfp_flower_ext_meta *frame, u32 key_ext)
{
	frame->nfp_flow_key_layer2 = cpu_to_be32(key_ext);
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

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_MPLS)) {
		struct flow_dissector_key_mpls *mpls;
		u32 t_mpls;

		mpls = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_MPLS,
						 target);

		t_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB, mpls->mpls_label) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC, mpls->mpls_tc) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS, mpls->mpls_bos) |
			 NFP_FLOWER_MASK_MPLS_Q;

		frame->mpls_lse = cpu_to_be32(t_mpls);
	}
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
nfp_flower_compile_ip_ext(struct nfp_flower_ip_ext *frame,
			  struct tc_cls_flower_offload *flow,
			  bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *basic;

		basic = skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  target);
		frame->proto = basic->ip_proto;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_dissector_key_ip *flow_ip;

		flow_ip = skb_flow_dissector_target(flow->dissector,
						    FLOW_DISSECTOR_KEY_IP,
						    target);
		frame->tos = flow_ip->tos;
		frame->ttl = flow_ip->ttl;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_dissector_key_tcp *tcp;
		u32 tcp_flags;

		tcp = skb_flow_dissector_target(flow->dissector,
						FLOW_DISSECTOR_KEY_TCP, target);
		tcp_flags = be16_to_cpu(tcp->flags);

		if (tcp_flags & TCPHDR_FIN)
			frame->flags |= NFP_FL_TCP_FLAG_FIN;
		if (tcp_flags & TCPHDR_SYN)
			frame->flags |= NFP_FL_TCP_FLAG_SYN;
		if (tcp_flags & TCPHDR_RST)
			frame->flags |= NFP_FL_TCP_FLAG_RST;
		if (tcp_flags & TCPHDR_PSH)
			frame->flags |= NFP_FL_TCP_FLAG_PSH;
		if (tcp_flags & TCPHDR_URG)
			frame->flags |= NFP_FL_TCP_FLAG_URG;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key;

		key = skb_flow_dissector_target(flow->dissector,
						FLOW_DISSECTOR_KEY_CONTROL,
						target);
		if (key->flags & FLOW_DIS_IS_FRAGMENT)
			frame->flags |= NFP_FL_IP_FRAGMENTED;
		if (key->flags & FLOW_DIS_FIRST_FRAG)
			frame->flags |= NFP_FL_IP_FRAG_FIRST;
	}
}

static void
nfp_flower_compile_ipv4(struct nfp_flower_ipv4 *frame,
			struct tc_cls_flower_offload *flow,
			bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv4_addrs *addr;

	memset(frame, 0, sizeof(struct nfp_flower_ipv4));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						 target);
		frame->ipv4_src = addr->src;
		frame->ipv4_dst = addr->dst;
	}

	nfp_flower_compile_ip_ext(&frame->ip_ext, flow, mask_version);
}

static void
nfp_flower_compile_ipv6(struct nfp_flower_ipv6 *frame,
			struct tc_cls_flower_offload *flow,
			bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv6_addrs *addr;

	memset(frame, 0, sizeof(struct nfp_flower_ipv6));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						 target);
		frame->ipv6_src = addr->src;
		frame->ipv6_dst = addr->dst;
	}

	nfp_flower_compile_ip_ext(&frame->ip_ext, flow, mask_version);
}

static void
nfp_flower_compile_ipv4_udp_tun(struct nfp_flower_ipv4_udp_tun *frame,
				struct tc_cls_flower_offload *flow,
				bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv4_addrs *tun_ips;
	struct flow_dissector_key_keyid *vni;

	memset(frame, 0, sizeof(struct nfp_flower_ipv4_udp_tun));

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
		tun_ips =
		   skb_flow_dissector_target(flow->dissector,
					     FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
					     target);
		frame->ip_src = tun_ips->src;
		frame->ip_dst = tun_ips->dst;
	}
}

int nfp_flower_compile_flow_match(struct tc_cls_flower_offload *flow,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow,
				  enum nfp_flower_tun_type tun_type)
{
	struct nfp_repr *netdev_repr;
	int err;
	u8 *ext;
	u8 *msk;

	memset(nfp_flow->unmasked_data, 0, key_ls->key_size);
	memset(nfp_flow->mask_data, 0, key_ls->key_size);

	ext = nfp_flow->unmasked_data;
	msk = nfp_flow->mask_data;

	/* Populate Exact Metadata. */
	nfp_flower_compile_meta_tci((struct nfp_flower_meta_tci *)ext,
				    flow, key_ls->key_layer, false);
	/* Populate Mask Metadata. */
	nfp_flower_compile_meta_tci((struct nfp_flower_meta_tci *)msk,
				    flow, key_ls->key_layer, true);
	ext += sizeof(struct nfp_flower_meta_tci);
	msk += sizeof(struct nfp_flower_meta_tci);

	/* Populate Extended Metadata if Required. */
	if (NFP_FLOWER_LAYER_EXT_META & key_ls->key_layer) {
		nfp_flower_compile_ext_meta((struct nfp_flower_ext_meta *)ext,
					    key_ls->key_layer_two);
		nfp_flower_compile_ext_meta((struct nfp_flower_ext_meta *)msk,
					    key_ls->key_layer_two);
		ext += sizeof(struct nfp_flower_ext_meta);
		msk += sizeof(struct nfp_flower_ext_meta);
	}

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

	if (key_ls->key_layer & NFP_FLOWER_LAYER_VXLAN ||
	    key_ls->key_layer_two & NFP_FLOWER_LAYER2_GENEVE) {
		__be32 tun_dst;

		/* Populate Exact VXLAN Data. */
		nfp_flower_compile_ipv4_udp_tun((void *)ext, flow, false);
		/* Populate Mask VXLAN Data. */
		nfp_flower_compile_ipv4_udp_tun((void *)msk, flow, true);
		tun_dst = ((struct nfp_flower_ipv4_udp_tun *)ext)->ip_dst;
		ext += sizeof(struct nfp_flower_ipv4_udp_tun);
		msk += sizeof(struct nfp_flower_ipv4_udp_tun);

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
