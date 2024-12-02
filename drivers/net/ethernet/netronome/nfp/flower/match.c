// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"

void
nfp_flower_compile_meta(struct nfp_flower_meta_tci *ext,
			struct nfp_flower_meta_tci *msk, u8 key_type)
{
	/* Populate the metadata frame. */
	ext->nfp_flow_key_layer = key_type;
	ext->mask_id = ~0;

	msk->nfp_flow_key_layer = key_type;
	msk->mask_id = ~0;
}

void
nfp_flower_compile_tci(struct nfp_flower_meta_tci *ext,
		       struct nfp_flower_meta_tci *msk,
		       struct flow_rule *rule)
{
	u16 msk_tci, key_tci;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		/* Populate the tci field. */
		key_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
		key_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
				      match.key->vlan_priority) |
			   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
				      match.key->vlan_id);

		msk_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
		msk_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
				      match.mask->vlan_priority) |
			   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
				      match.mask->vlan_id);

		ext->tci |= cpu_to_be16((key_tci & msk_tci));
		msk->tci |= cpu_to_be16(msk_tci);
	}
}

static void
nfp_flower_compile_meta_tci(struct nfp_flower_meta_tci *ext,
			    struct nfp_flower_meta_tci *msk,
			    struct flow_rule *rule, u8 key_type, bool qinq_sup)
{
	memset(ext, 0, sizeof(struct nfp_flower_meta_tci));
	memset(msk, 0, sizeof(struct nfp_flower_meta_tci));

	nfp_flower_compile_meta(ext, msk, key_type);

	if (!qinq_sup)
		nfp_flower_compile_tci(ext, msk, rule);
}

void
nfp_flower_compile_ext_meta(struct nfp_flower_ext_meta *frame, u32 key_ext)
{
	frame->nfp_flow_key_layer2 = cpu_to_be32(key_ext);
}

int
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

void
nfp_flower_compile_mac(struct nfp_flower_mac_mpls *ext,
		       struct nfp_flower_mac_mpls *msk,
		       struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;
		u8 tmp;
		int i;

		flow_rule_match_eth_addrs(rule, &match);
		/* Populate mac frame. */
		for (i = 0; i < ETH_ALEN; i++) {
			tmp = match.key->dst[i] & match.mask->dst[i];
			ext->mac_dst[i] |= tmp & (~msk->mac_dst[i]);
			msk->mac_dst[i] |= match.mask->dst[i];

			tmp = match.key->src[i] & match.mask->src[i];
			ext->mac_src[i] |= tmp & (~msk->mac_src[i]);
			msk->mac_src[i] |= match.mask->src[i];
		}
	}
}

int
nfp_flower_compile_mpls(struct nfp_flower_mac_mpls *ext,
			struct nfp_flower_mac_mpls *msk,
			struct flow_rule *rule,
			struct netlink_ext_ack *extack)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_MPLS)) {
		struct flow_match_mpls match;
		u32 key_mpls, msk_mpls;

		flow_rule_match_mpls(rule, &match);

		/* Only support matching the first LSE */
		if (match.mask->used_lses != 1) {
			NL_SET_ERR_MSG_MOD(extack,
					   "unsupported offload: invalid LSE depth for MPLS match offload");
			return -EOPNOTSUPP;
		}

		key_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB,
				      match.key->ls[0].mpls_label) |
			   FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC,
				      match.key->ls[0].mpls_tc) |
			   FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS,
				      match.key->ls[0].mpls_bos) |
			   NFP_FLOWER_MASK_MPLS_Q;

		msk_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB,
				      match.mask->ls[0].mpls_label) |
			   FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC,
				      match.mask->ls[0].mpls_tc) |
			   FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS,
				      match.mask->ls[0].mpls_bos) |
			   NFP_FLOWER_MASK_MPLS_Q;

		ext->mpls_lse |= cpu_to_be32((key_mpls & msk_mpls));
		msk->mpls_lse |= cpu_to_be32(msk_mpls);
	} else if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		/* Check for mpls ether type and set NFP_FLOWER_MASK_MPLS_Q
		 * bit, which indicates an mpls ether type but without any
		 * mpls fields.
		 */
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (match.key->n_proto == cpu_to_be16(ETH_P_MPLS_UC) ||
		    match.key->n_proto == cpu_to_be16(ETH_P_MPLS_MC)) {
			ext->mpls_lse |= cpu_to_be32(NFP_FLOWER_MASK_MPLS_Q);
			msk->mpls_lse |= cpu_to_be32(NFP_FLOWER_MASK_MPLS_Q);
		}
	}

	return 0;
}

static int
nfp_flower_compile_mac_mpls(struct nfp_flower_mac_mpls *ext,
			    struct nfp_flower_mac_mpls *msk,
			    struct flow_rule *rule,
			    struct netlink_ext_ack *extack)
{
	memset(ext, 0, sizeof(struct nfp_flower_mac_mpls));
	memset(msk, 0, sizeof(struct nfp_flower_mac_mpls));

	nfp_flower_compile_mac(ext, msk, rule);

	return nfp_flower_compile_mpls(ext, msk, rule, extack);
}

void
nfp_flower_compile_tport(struct nfp_flower_tp_ports *ext,
			 struct nfp_flower_tp_ports *msk,
			 struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;
		__be16 tmp;

		flow_rule_match_ports(rule, &match);

		tmp = match.key->src & match.mask->src;
		ext->port_src |= tmp & (~msk->port_src);
		msk->port_src |= match.mask->src;

		tmp = match.key->dst & match.mask->dst;
		ext->port_dst |= tmp & (~msk->port_dst);
		msk->port_dst |= match.mask->dst;
	}
}

static void
nfp_flower_compile_ip_ext(struct nfp_flower_ip_ext *ext,
			  struct nfp_flower_ip_ext *msk, struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		ext->proto |= match.key->ip_proto & match.mask->ip_proto;
		msk->proto |= match.mask->ip_proto;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;
		u8 tmp;

		flow_rule_match_ip(rule, &match);

		tmp = match.key->tos & match.mask->tos;
		ext->tos |= tmp & (~msk->tos);
		msk->tos |= match.mask->tos;

		tmp = match.key->ttl & match.mask->ttl;
		ext->ttl |= tmp & (~msk->ttl);
		msk->ttl |= match.mask->ttl;
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
nfp_flower_fill_vlan(struct flow_match_vlan *match,
		     struct nfp_flower_vlan *ext,
		     struct nfp_flower_vlan *msk, bool outer_vlan)
{
	struct flow_dissector_key_vlan *mask = match->mask;
	struct flow_dissector_key_vlan *key = match->key;
	u16 msk_tci, key_tci;

	key_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
	key_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
			      key->vlan_priority) |
		   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
			      key->vlan_id);
	msk_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
	msk_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
			      mask->vlan_priority) |
		   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
			      mask->vlan_id);

	if (outer_vlan) {
		ext->outer_tci |= cpu_to_be16((key_tci & msk_tci));
		ext->outer_tpid |= key->vlan_tpid & mask->vlan_tpid;
		msk->outer_tci |= cpu_to_be16(msk_tci);
		msk->outer_tpid |= mask->vlan_tpid;
	} else {
		ext->inner_tci |= cpu_to_be16((key_tci & msk_tci));
		ext->inner_tpid |= key->vlan_tpid & mask->vlan_tpid;
		msk->inner_tci |= cpu_to_be16(msk_tci);
		msk->inner_tpid |= mask->vlan_tpid;
	}
}

void
nfp_flower_compile_vlan(struct nfp_flower_vlan *ext,
			struct nfp_flower_vlan *msk,
			struct flow_rule *rule)
{
	struct flow_match_vlan match;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		flow_rule_match_vlan(rule, &match);
		nfp_flower_fill_vlan(&match, ext, msk, true);
	}
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CVLAN)) {
		flow_rule_match_cvlan(rule, &match);
		nfp_flower_fill_vlan(&match, ext, msk, false);
	}
}

void
nfp_flower_compile_ipv4(struct nfp_flower_ipv4 *ext,
			struct nfp_flower_ipv4 *msk, struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;
		__be32 tmp;

		flow_rule_match_ipv4_addrs(rule, &match);

		tmp = match.key->src & match.mask->src;
		ext->ipv4_src |= tmp & (~msk->ipv4_src);
		msk->ipv4_src |= match.mask->src;

		tmp = match.key->dst & match.mask->dst;
		ext->ipv4_dst |= tmp & (~msk->ipv4_dst);
		msk->ipv4_dst |= match.mask->dst;
	}

	nfp_flower_compile_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
}

void
nfp_flower_compile_ipv6(struct nfp_flower_ipv6 *ext,
			struct nfp_flower_ipv6 *msk, struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;
		u8 tmp;
		int i;

		flow_rule_match_ipv6_addrs(rule, &match);
		for (i = 0; i < sizeof(ext->ipv6_src); i++) {
			tmp = match.key->src.s6_addr[i] &
			      match.mask->src.s6_addr[i];
			ext->ipv6_src.s6_addr[i] |= tmp &
						    (~msk->ipv6_src.s6_addr[i]);
			msk->ipv6_src.s6_addr[i] |= match.mask->src.s6_addr[i];

			tmp = match.key->dst.s6_addr[i] &
			      match.mask->dst.s6_addr[i];
			ext->ipv6_dst.s6_addr[i] |= tmp &
						    (~msk->ipv6_dst.s6_addr[i]);
			msk->ipv6_dst.s6_addr[i] |= match.mask->dst.s6_addr[i];
		}
	}

	nfp_flower_compile_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
}

void
nfp_flower_compile_geneve_opt(u8 *ext, u8 *msk, struct flow_rule *rule)
{
	struct flow_match_enc_opts match;
	int i;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_OPTS)) {
		flow_rule_match_enc_opts(rule, &match);

		for (i = 0; i < match.mask->len; i++) {
			ext[i] |= match.key->data[i] & match.mask->data[i];
			msk[i] |= match.mask->data[i];
		}
	}
}

static void
nfp_flower_compile_tun_ipv4_addrs(struct nfp_flower_tun_ipv4 *ext,
				  struct nfp_flower_tun_ipv4 *msk,
				  struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_enc_ipv4_addrs(rule, &match);
		ext->src |= match.key->src & match.mask->src;
		ext->dst |= match.key->dst & match.mask->dst;
		msk->src |= match.mask->src;
		msk->dst |= match.mask->dst;
	}
}

static void
nfp_flower_compile_tun_ipv6_addrs(struct nfp_flower_tun_ipv6 *ext,
				  struct nfp_flower_tun_ipv6 *msk,
				  struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;
		int i;

		flow_rule_match_enc_ipv6_addrs(rule, &match);
		for (i = 0; i < sizeof(ext->src); i++) {
			ext->src.s6_addr[i] |= match.key->src.s6_addr[i] &
					       match.mask->src.s6_addr[i];
			ext->dst.s6_addr[i] |= match.key->dst.s6_addr[i] &
					       match.mask->dst.s6_addr[i];
			msk->src.s6_addr[i] |= match.mask->src.s6_addr[i];
			msk->dst.s6_addr[i] |= match.mask->dst.s6_addr[i];
		}
	}
}

static void
nfp_flower_compile_tun_ip_ext(struct nfp_flower_tun_ip_ext *ext,
			      struct nfp_flower_tun_ip_ext *msk,
			      struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_match_ip match;

		flow_rule_match_enc_ip(rule, &match);
		ext->tos |= match.key->tos & match.mask->tos;
		ext->ttl |= match.key->ttl & match.mask->ttl;
		msk->tos |= match.mask->tos;
		msk->ttl |= match.mask->ttl;
	}
}

static void
nfp_flower_compile_tun_udp_key(__be32 *key, __be32 *key_msk,
			       struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;
		u32 vni;

		flow_rule_match_enc_keyid(rule, &match);
		vni = be32_to_cpu((match.key->keyid & match.mask->keyid)) <<
		      NFP_FL_TUN_VNI_OFFSET;
		*key |= cpu_to_be32(vni);
		vni = be32_to_cpu(match.mask->keyid) << NFP_FL_TUN_VNI_OFFSET;
		*key_msk |= cpu_to_be32(vni);
	}
}

static void
nfp_flower_compile_tun_gre_key(__be32 *key, __be32 *key_msk, __be16 *flags,
			       __be16 *flags_msk, struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;

		flow_rule_match_enc_keyid(rule, &match);
		*key |= match.key->keyid & match.mask->keyid;
		*key_msk |= match.mask->keyid;

		*flags = cpu_to_be16(NFP_FL_GRE_FLAG_KEY);
		*flags_msk = cpu_to_be16(NFP_FL_GRE_FLAG_KEY);
	}
}

void
nfp_flower_compile_ipv4_gre_tun(struct nfp_flower_ipv4_gre_tun *ext,
				struct nfp_flower_ipv4_gre_tun *msk,
				struct flow_rule *rule)
{
	/* NVGRE is the only supported GRE tunnel type */
	ext->ethertype = cpu_to_be16(ETH_P_TEB);
	msk->ethertype = cpu_to_be16(~0);

	nfp_flower_compile_tun_ipv4_addrs(&ext->ipv4, &msk->ipv4, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_gre_key(&ext->tun_key, &msk->tun_key,
				       &ext->tun_flags, &msk->tun_flags, rule);
}

void
nfp_flower_compile_ipv4_udp_tun(struct nfp_flower_ipv4_udp_tun *ext,
				struct nfp_flower_ipv4_udp_tun *msk,
				struct flow_rule *rule)
{
	nfp_flower_compile_tun_ipv4_addrs(&ext->ipv4, &msk->ipv4, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_udp_key(&ext->tun_id, &msk->tun_id, rule);
}

void
nfp_flower_compile_ipv6_udp_tun(struct nfp_flower_ipv6_udp_tun *ext,
				struct nfp_flower_ipv6_udp_tun *msk,
				struct flow_rule *rule)
{
	nfp_flower_compile_tun_ipv6_addrs(&ext->ipv6, &msk->ipv6, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_udp_key(&ext->tun_id, &msk->tun_id, rule);
}

void
nfp_flower_compile_ipv6_gre_tun(struct nfp_flower_ipv6_gre_tun *ext,
				struct nfp_flower_ipv6_gre_tun *msk,
				struct flow_rule *rule)
{
	/* NVGRE is the only supported GRE tunnel type */
	ext->ethertype = cpu_to_be16(ETH_P_TEB);
	msk->ethertype = cpu_to_be16(~0);

	nfp_flower_compile_tun_ipv6_addrs(&ext->ipv6, &msk->ipv6, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_gre_key(&ext->tun_key, &msk->tun_key,
				       &ext->tun_flags, &msk->tun_flags, rule);
}

int nfp_flower_compile_flow_match(struct nfp_app *app,
				  struct flow_rule *rule,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow,
				  enum nfp_flower_tun_type tun_type,
				  struct netlink_ext_ack *extack)
{
	struct nfp_flower_priv *priv = app->priv;
	bool qinq_sup;
	u32 port_id;
	int ext_len;
	int err;
	u8 *ext;
	u8 *msk;

	port_id = nfp_flower_get_port_id_from_netdev(app, netdev);

	memset(nfp_flow->unmasked_data, 0, key_ls->key_size);
	memset(nfp_flow->mask_data, 0, key_ls->key_size);

	ext = nfp_flow->unmasked_data;
	msk = nfp_flow->mask_data;

	qinq_sup = !!(priv->flower_ext_feats & NFP_FL_FEATS_VLAN_QINQ);

	nfp_flower_compile_meta_tci((struct nfp_flower_meta_tci *)ext,
				    (struct nfp_flower_meta_tci *)msk,
				    rule, key_ls->key_layer, qinq_sup);
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
		err = nfp_flower_compile_mac_mpls((struct nfp_flower_mac_mpls *)ext,
						  (struct nfp_flower_mac_mpls *)msk,
						  rule, extack);
		if (err)
			return err;

		ext += sizeof(struct nfp_flower_mac_mpls);
		msk += sizeof(struct nfp_flower_mac_mpls);
	}

	if (NFP_FLOWER_LAYER_TP & key_ls->key_layer) {
		nfp_flower_compile_tport((struct nfp_flower_tp_ports *)ext,
					 (struct nfp_flower_tp_ports *)msk,
					 rule);
		ext += sizeof(struct nfp_flower_tp_ports);
		msk += sizeof(struct nfp_flower_tp_ports);
	}

	if (NFP_FLOWER_LAYER_IPV4 & key_ls->key_layer) {
		nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)ext,
					(struct nfp_flower_ipv4 *)msk,
					rule);
		ext += sizeof(struct nfp_flower_ipv4);
		msk += sizeof(struct nfp_flower_ipv4);
	}

	if (NFP_FLOWER_LAYER_IPV6 & key_ls->key_layer) {
		nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)ext,
					(struct nfp_flower_ipv6 *)msk,
					rule);
		ext += sizeof(struct nfp_flower_ipv6);
		msk += sizeof(struct nfp_flower_ipv6);
	}

	if (NFP_FLOWER_LAYER2_QINQ & key_ls->key_layer_two) {
		nfp_flower_compile_vlan((struct nfp_flower_vlan *)ext,
					(struct nfp_flower_vlan *)msk,
					rule);
		ext += sizeof(struct nfp_flower_vlan);
		msk += sizeof(struct nfp_flower_vlan);
	}

	if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_GRE) {
		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6) {
			struct nfp_flower_ipv6_gre_tun *gre_match;
			struct nfp_ipv6_addr_entry *entry;
			struct in6_addr *dst;

			nfp_flower_compile_ipv6_gre_tun((void *)ext,
							(void *)msk, rule);
			gre_match = (struct nfp_flower_ipv6_gre_tun *)ext;
			dst = &gre_match->ipv6.dst;
			ext += sizeof(struct nfp_flower_ipv6_gre_tun);
			msk += sizeof(struct nfp_flower_ipv6_gre_tun);

			entry = nfp_tunnel_add_ipv6_off(app, dst);
			if (!entry)
				return -EOPNOTSUPP;

			nfp_flow->nfp_tun_ipv6 = entry;
		} else {
			__be32 dst;

			nfp_flower_compile_ipv4_gre_tun((void *)ext,
							(void *)msk, rule);
			dst = ((struct nfp_flower_ipv4_gre_tun *)ext)->ipv4.dst;
			ext += sizeof(struct nfp_flower_ipv4_gre_tun);
			msk += sizeof(struct nfp_flower_ipv4_gre_tun);

			/* Store the tunnel destination in the rule data.
			 * This must be present and be an exact match.
			 */
			nfp_flow->nfp_tun_ipv4_addr = dst;
			nfp_tunnel_add_ipv4_off(app, dst);
		}
	}

	if (key_ls->key_layer & NFP_FLOWER_LAYER_VXLAN ||
	    key_ls->key_layer_two & NFP_FLOWER_LAYER2_GENEVE) {
		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6) {
			struct nfp_flower_ipv6_udp_tun *udp_match;
			struct nfp_ipv6_addr_entry *entry;
			struct in6_addr *dst;

			nfp_flower_compile_ipv6_udp_tun((void *)ext,
							(void *)msk, rule);
			udp_match = (struct nfp_flower_ipv6_udp_tun *)ext;
			dst = &udp_match->ipv6.dst;
			ext += sizeof(struct nfp_flower_ipv6_udp_tun);
			msk += sizeof(struct nfp_flower_ipv6_udp_tun);

			entry = nfp_tunnel_add_ipv6_off(app, dst);
			if (!entry)
				return -EOPNOTSUPP;

			nfp_flow->nfp_tun_ipv6 = entry;
		} else {
			__be32 dst;

			nfp_flower_compile_ipv4_udp_tun((void *)ext,
							(void *)msk, rule);
			dst = ((struct nfp_flower_ipv4_udp_tun *)ext)->ipv4.dst;
			ext += sizeof(struct nfp_flower_ipv4_udp_tun);
			msk += sizeof(struct nfp_flower_ipv4_udp_tun);

			/* Store the tunnel destination in the rule data.
			 * This must be present and be an exact match.
			 */
			nfp_flow->nfp_tun_ipv4_addr = dst;
			nfp_tunnel_add_ipv4_off(app, dst);
		}

		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_GENEVE_OP) {
			nfp_flower_compile_geneve_opt(ext, msk, rule);
		}
	}

	/* Check that the flow key does not exceed the maximum limit.
	 * All structures in the key is multiples of 4 bytes, so use u32.
	 */
	ext_len = (u32 *)ext - (u32 *)nfp_flow->unmasked_data;
	if (ext_len > NFP_FLOWER_KEY_MAX_LW) {
		NL_SET_ERR_MSG_MOD(extack,
				   "unsupported offload: flow key too long");
		return -EOPNOTSUPP;
	}

	return 0;
}
