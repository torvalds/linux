// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"

static void
nfp_flower_compile_meta_tci(struct nfp_flower_meta_tci *ext,
			    struct nfp_flower_meta_tci *msk,
			    struct tc_cls_flower_offload *flow, u8 key_type)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);
	u16 tmp_tci;

	memset(ext, 0, sizeof(struct nfp_flower_meta_tci));
	memset(msk, 0, sizeof(struct nfp_flower_meta_tci));

	/* Populate the metadata frame. */
	ext->nfp_flow_key_layer = key_type;
	ext->mask_id = ~0;

	msk->nfp_flow_key_layer = key_type;
	msk->mask_id = ~0;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		/* Populate the tci field. */
		tmp_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
		tmp_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
				      match.key->vlan_priority) |
			   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
				      match.key->vlan_id);
		ext->tci = cpu_to_be16(tmp_tci);

		tmp_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
		tmp_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
				      match.mask->vlan_priority) |
			   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
				      match.mask->vlan_id);
		msk->tci = cpu_to_be16(tmp_tci);
	}
}

static void
nfp_flower_compile_ext_meta(struct nfp_flower_ext_meta *frame, u32 key_ext)
{
	frame->nfp_flow_key_layer2 = cpu_to_be32(key_ext);
}

static int
nfp_flower_compile_port(struct nfp_flower_in_port *frame, u32 cmsg_port,
			bool mask_version, enum nfp_flower_tun_type tun_type,
			struct netlink_ext_ack *extack)
{
	if (mask_version) {
		frame->in_port = cpu_to_be32(~0);
		return 0;
	}

	if (tun_type) {
		frame->in_port = cpu_to_be32(NFP_FL_PORT_TYPE_TUN | tun_type);
	} else {
		if (!cmsg_port) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid ingress interface for match offload");
			return -EOPNOTSUPP;
		}
		frame->in_port = cpu_to_be32(cmsg_port);
	}

	return 0;
}

static void
nfp_flower_compile_mac(struct nfp_flower_mac_mpls *ext,
		       struct nfp_flower_mac_mpls *msk,
		       struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	memset(ext, 0, sizeof(struct nfp_flower_mac_mpls));
	memset(msk, 0, sizeof(struct nfp_flower_mac_mpls));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);
		/* Populate mac frame. */
		ether_addr_copy(ext->mac_dst, &match.key->dst[0]);
		ether_addr_copy(ext->mac_src, &match.key->src[0]);
		ether_addr_copy(msk->mac_dst, &match.mask->dst[0]);
		ether_addr_copy(msk->mac_src, &match.mask->src[0]);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_MPLS)) {
		struct flow_match_mpls match;
		u32 t_mpls;

		flow_rule_match_mpls(rule, &match);
		t_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB, match.key->mpls_label) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC, match.key->mpls_tc) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS, match.key->mpls_bos) |
			 NFP_FLOWER_MASK_MPLS_Q;
		ext->mpls_lse = cpu_to_be32(t_mpls);
		t_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB, match.mask->mpls_label) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC, match.mask->mpls_tc) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS, match.mask->mpls_bos) |
			 NFP_FLOWER_MASK_MPLS_Q;
		msk->mpls_lse = cpu_to_be32(t_mpls);
	} else if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		/* Check for mpls ether type and set NFP_FLOWER_MASK_MPLS_Q
		 * bit, which indicates an mpls ether type but without any
		 * mpls fields.
		 */
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (match.key->n_proto == cpu_to_be16(ETH_P_MPLS_UC) ||
		    match.key->n_proto == cpu_to_be16(ETH_P_MPLS_MC)) {
			ext->mpls_lse = cpu_to_be32(NFP_FLOWER_MASK_MPLS_Q);
			msk->mpls_lse = cpu_to_be32(NFP_FLOWER_MASK_MPLS_Q);
		}
	}
}

static void
nfp_flower_compile_tport(struct nfp_flower_tp_ports *ext,
			 struct nfp_flower_tp_ports *msk,
			 struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	memset(ext, 0, sizeof(struct nfp_flower_tp_ports));
	memset(msk, 0, sizeof(struct nfp_flower_tp_ports));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		ext->port_src = match.key->src;
		ext->port_dst = match.key->dst;
		msk->port_src = match.mask->src;
		msk->port_dst = match.mask->dst;
	}
}

static void
nfp_flower_compile_ip_ext(struct nfp_flower_ip_ext *ext,
			  struct nfp_flower_ip_ext *msk,
			  struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		ext->proto = match.key->ip_proto;
		msk->proto = match.mask->ip_proto;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		ext->tos = match.key->tos;
		ext->ttl = match.key->ttl;
		msk->tos = match.mask->tos;
		msk->ttl = match.mask->ttl;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		u16 tcp_flags, tcp_flags_mask;
		struct flow_match_tcp match;

		flow_rule_match_tcp(rule, &match);
		tcp_flags = be16_to_cpu(match.key->flags);
		tcp_flags_mask = be16_to_cpu(match.mask->flags);

		if (tcp_flags & TCPHDR_FIN)
			ext->flags |= NFP_FL_TCP_FLAG_FIN;
		if (tcp_flags_mask & TCPHDR_FIN)
			msk->flags |= NFP_FL_TCP_FLAG_FIN;

		if (tcp_flags & TCPHDR_SYN)
			ext->flags |= NFP_FL_TCP_FLAG_SYN;
		if (tcp_flags_mask & TCPHDR_SYN)
			msk->flags |= NFP_FL_TCP_FLAG_SYN;

		if (tcp_flags & TCPHDR_RST)
			ext->flags |= NFP_FL_TCP_FLAG_RST;
		if (tcp_flags_mask & TCPHDR_RST)
			msk->flags |= NFP_FL_TCP_FLAG_RST;

		if (tcp_flags & TCPHDR_PSH)
			ext->flags |= NFP_FL_TCP_FLAG_PSH;
		if (tcp_flags_mask & TCPHDR_PSH)
			msk->flags |= NFP_FL_TCP_FLAG_PSH;

		if (tcp_flags & TCPHDR_URG)
			ext->flags |= NFP_FL_TCP_FLAG_URG;
		if (tcp_flags_mask & TCPHDR_URG)
			msk->flags |= NFP_FL_TCP_FLAG_URG;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		if (match.key->flags & FLOW_DIS_IS_FRAGMENT)
			ext->flags |= NFP_FL_IP_FRAGMENTED;
		if (match.mask->flags & FLOW_DIS_IS_FRAGMENT)
			msk->flags |= NFP_FL_IP_FRAGMENTED;
		if (match.key->flags & FLOW_DIS_FIRST_FRAG)
			ext->flags |= NFP_FL_IP_FRAG_FIRST;
		if (match.mask->flags & FLOW_DIS_FIRST_FRAG)
			msk->flags |= NFP_FL_IP_FRAG_FIRST;
	}
}

static void
nfp_flower_compile_ipv4(struct nfp_flower_ipv4 *ext,
			struct nfp_flower_ipv4 *msk,
			struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);
	struct flow_match_ipv4_addrs match;

	memset(ext, 0, sizeof(struct nfp_flower_ipv4));
	memset(msk, 0, sizeof(struct nfp_flower_ipv4));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		flow_rule_match_ipv4_addrs(rule, &match);
		ext->ipv4_src = match.key->src;
		ext->ipv4_dst = match.key->dst;
		msk->ipv4_src = match.mask->src;
		msk->ipv4_dst = match.mask->dst;
	}

	nfp_flower_compile_ip_ext(&ext->ip_ext, &msk->ip_ext, flow);
}

static void
nfp_flower_compile_ipv6(struct nfp_flower_ipv6 *ext,
			struct nfp_flower_ipv6 *msk,
			struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	memset(ext, 0, sizeof(struct nfp_flower_ipv6));
	memset(msk, 0, sizeof(struct nfp_flower_ipv6));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		ext->ipv6_src = match.key->src;
		ext->ipv6_dst = match.key->dst;
		msk->ipv6_src = match.mask->src;
		msk->ipv6_dst = match.mask->dst;
	}

	nfp_flower_compile_ip_ext(&ext->ip_ext, &msk->ip_ext, flow);
}

static int
nfp_flower_compile_geneve_opt(void *ext, void *msk,
			      struct tc_cls_flower_offload *flow)
{
	struct flow_match_enc_opts match;

	flow_rule_match_enc_opts(flow->rule, &match);
	memcpy(ext, match.key->data, match.key->len);
	memcpy(msk, match.mask->data, match.mask->len);

	return 0;
}

static void
nfp_flower_compile_tun_ipv4_addrs(struct nfp_flower_tun_ipv4 *ext,
				  struct nfp_flower_tun_ipv4 *msk,
				  struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_enc_ipv4_addrs(rule, &match);
		ext->src = match.key->src;
		ext->dst = match.key->dst;
		msk->src = match.mask->src;
		msk->dst = match.mask->dst;
	}
}

static void
nfp_flower_compile_tun_ip_ext(struct nfp_flower_tun_ip_ext *ext,
			      struct nfp_flower_tun_ip_ext *msk,
			      struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_match_ip match;

		flow_rule_match_enc_ip(rule, &match);
		ext->tos = match.key->tos;
		ext->ttl = match.key->ttl;
		msk->tos = match.mask->tos;
		msk->ttl = match.mask->ttl;
	}
}

static void
nfp_flower_compile_ipv4_gre_tun(struct nfp_flower_ipv4_gre_tun *ext,
				struct nfp_flower_ipv4_gre_tun *msk,
				struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	memset(ext, 0, sizeof(struct nfp_flower_ipv4_gre_tun));
	memset(msk, 0, sizeof(struct nfp_flower_ipv4_gre_tun));

	/* NVGRE is the only supported GRE tunnel type */
	ext->ethertype = cpu_to_be16(ETH_P_TEB);
	msk->ethertype = cpu_to_be16(~0);

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;

		flow_rule_match_enc_keyid(rule, &match);
		ext->tun_key = match.key->keyid;
		msk->tun_key = match.mask->keyid;

		ext->tun_flags = cpu_to_be16(NFP_FL_GRE_FLAG_KEY);
		msk->tun_flags = cpu_to_be16(NFP_FL_GRE_FLAG_KEY);
	}

	nfp_flower_compile_tun_ipv4_addrs(&ext->ipv4, &msk->ipv4, flow);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, flow);
}

static void
nfp_flower_compile_ipv4_udp_tun(struct nfp_flower_ipv4_udp_tun *ext,
				struct nfp_flower_ipv4_udp_tun *msk,
				struct tc_cls_flower_offload *flow)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);

	memset(ext, 0, sizeof(struct nfp_flower_ipv4_udp_tun));
	memset(msk, 0, sizeof(struct nfp_flower_ipv4_udp_tun));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;
		u32 temp_vni;

		flow_rule_match_enc_keyid(rule, &match);
		temp_vni = be32_to_cpu(match.key->keyid) << NFP_FL_TUN_VNI_OFFSET;
		ext->tun_id = cpu_to_be32(temp_vni);
		temp_vni = be32_to_cpu(match.mask->keyid) << NFP_FL_TUN_VNI_OFFSET;
		msk->tun_id = cpu_to_be32(temp_vni);
	}

	nfp_flower_compile_tun_ipv4_addrs(&ext->ipv4, &msk->ipv4, flow);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, flow);
}

int nfp_flower_compile_flow_match(struct nfp_app *app,
				  struct tc_cls_flower_offload *flow,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow,
				  enum nfp_flower_tun_type tun_type,
				  struct netlink_ext_ack *extack)
{
	u32 port_id;
	int err;
	u8 *ext;
	u8 *msk;

	port_id = nfp_flower_get_port_id_from_netdev(app, netdev);

	memset(nfp_flow->unmasked_data, 0, key_ls->key_size);
	memset(nfp_flow->mask_data, 0, key_ls->key_size);

	ext = nfp_flow->unmasked_data;
	msk = nfp_flow->mask_data;

	nfp_flower_compile_meta_tci((struct nfp_flower_meta_tci *)ext,
				    (struct nfp_flower_meta_tci *)msk,
				    flow, key_ls->key_layer);
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
				      port_id, false, tun_type, extack);
	if (err)
		return err;

	/* Populate Mask Port Data. */
	err = nfp_flower_compile_port((struct nfp_flower_in_port *)msk,
				      port_id, true, tun_type, extack);
	if (err)
		return err;

	ext += sizeof(struct nfp_flower_in_port);
	msk += sizeof(struct nfp_flower_in_port);

	if (NFP_FLOWER_LAYER_MAC & key_ls->key_layer) {
		nfp_flower_compile_mac((struct nfp_flower_mac_mpls *)ext,
				       (struct nfp_flower_mac_mpls *)msk,
				       flow);
		ext += sizeof(struct nfp_flower_mac_mpls);
		msk += sizeof(struct nfp_flower_mac_mpls);
	}

	if (NFP_FLOWER_LAYER_TP & key_ls->key_layer) {
		nfp_flower_compile_tport((struct nfp_flower_tp_ports *)ext,
					 (struct nfp_flower_tp_ports *)msk,
					 flow);
		ext += sizeof(struct nfp_flower_tp_ports);
		msk += sizeof(struct nfp_flower_tp_ports);
	}

	if (NFP_FLOWER_LAYER_IPV4 & key_ls->key_layer) {
		nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)ext,
					(struct nfp_flower_ipv4 *)msk,
					flow);
		ext += sizeof(struct nfp_flower_ipv4);
		msk += sizeof(struct nfp_flower_ipv4);
	}

	if (NFP_FLOWER_LAYER_IPV6 & key_ls->key_layer) {
		nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)ext,
					(struct nfp_flower_ipv6 *)msk,
					flow);
		ext += sizeof(struct nfp_flower_ipv6);
		msk += sizeof(struct nfp_flower_ipv6);
	}

	if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_GRE) {
		__be32 tun_dst;

		nfp_flower_compile_ipv4_gre_tun((void *)ext, (void *)msk, flow);
		tun_dst = ((struct nfp_flower_ipv4_gre_tun *)ext)->ipv4.dst;
		ext += sizeof(struct nfp_flower_ipv4_gre_tun);
		msk += sizeof(struct nfp_flower_ipv4_gre_tun);

		/* Store the tunnel destination in the rule data.
		 * This must be present and be an exact match.
		 */
		nfp_flow->nfp_tun_ipv4_addr = tun_dst;
		nfp_tunnel_add_ipv4_off(app, tun_dst);
	}

	if (key_ls->key_layer & NFP_FLOWER_LAYER_VXLAN ||
	    key_ls->key_layer_two & NFP_FLOWER_LAYER2_GENEVE) {
		__be32 tun_dst;

		nfp_flower_compile_ipv4_udp_tun((void *)ext, (void *)msk, flow);
		tun_dst = ((struct nfp_flower_ipv4_udp_tun *)ext)->ipv4.dst;
		ext += sizeof(struct nfp_flower_ipv4_udp_tun);
		msk += sizeof(struct nfp_flower_ipv4_udp_tun);

		/* Store the tunnel destination in the rule data.
		 * This must be present and be an exact match.
		 */
		nfp_flow->nfp_tun_ipv4_addr = tun_dst;
		nfp_tunnel_add_ipv4_off(app, tun_dst);

		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_GENEVE_OP) {
			err = nfp_flower_compile_geneve_opt(ext, msk, flow);
			if (err)
				return err;
		}
	}

	return 0;
}
