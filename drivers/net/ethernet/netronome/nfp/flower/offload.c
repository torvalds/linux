// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/skbuff.h>
#include <net/devlink.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"
#include "../nfpcore/nfp_cpp.h"
#include "../nfpcore/nfp_nsp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "../nfp_port.h"

#define NFP_FLOWER_SUPPORTED_TCPFLAGS \
	(TCPHDR_FIN | TCPHDR_SYN | TCPHDR_RST | \
	 TCPHDR_PSH | TCPHDR_URG)

#define NFP_FLOWER_SUPPORTED_CTLFLAGS \
	(FLOW_DIS_IS_FRAGMENT | \
	 FLOW_DIS_FIRST_FRAG)

#define NFP_FLOWER_WHITELIST_DISSECTOR \
	(BIT(FLOW_DISSECTOR_KEY_CONTROL) | \
	 BIT(FLOW_DISSECTOR_KEY_BASIC) | \
	 BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_TCP) | \
	 BIT(FLOW_DISSECTOR_KEY_PORTS) | \
	 BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_VLAN) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_PORTS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_OPTS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IP) | \
	 BIT(FLOW_DISSECTOR_KEY_MPLS) | \
	 BIT(FLOW_DISSECTOR_KEY_IP))

#define NFP_FLOWER_WHITELIST_TUN_DISSECTOR \
	(BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_OPTS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_PORTS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IP))

#define NFP_FLOWER_WHITELIST_TUN_DISSECTOR_R \
	(BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS))

#define NFP_FLOWER_MERGE_FIELDS \
	(NFP_FLOWER_LAYER_PORT | \
	 NFP_FLOWER_LAYER_MAC | \
	 NFP_FLOWER_LAYER_TP | \
	 NFP_FLOWER_LAYER_IPV4 | \
	 NFP_FLOWER_LAYER_IPV6)

struct nfp_flower_merge_check {
	union {
		struct {
			__be16 tci;
			struct nfp_flower_mac_mpls l2;
			struct nfp_flower_tp_ports l4;
			union {
				struct nfp_flower_ipv4 ipv4;
				struct nfp_flower_ipv6 ipv6;
			};
		};
		unsigned long vals[8];
	};
};

static int
nfp_flower_xmit_flow(struct nfp_app *app, struct nfp_fl_payload *nfp_flow,
		     u8 mtype)
{
	u32 meta_len, key_len, mask_len, act_len, tot_len;
	struct sk_buff *skb;
	unsigned char *msg;

	meta_len =  sizeof(struct nfp_fl_rule_metadata);
	key_len = nfp_flow->meta.key_len;
	mask_len = nfp_flow->meta.mask_len;
	act_len = nfp_flow->meta.act_len;

	tot_len = meta_len + key_len + mask_len + act_len;

	/* Convert to long words as firmware expects
	 * lengths in units of NFP_FL_LW_SIZ.
	 */
	nfp_flow->meta.key_len >>= NFP_FL_LW_SIZ;
	nfp_flow->meta.mask_len >>= NFP_FL_LW_SIZ;
	nfp_flow->meta.act_len >>= NFP_FL_LW_SIZ;

	skb = nfp_flower_cmsg_alloc(app, tot_len, mtype, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	msg = nfp_flower_cmsg_get_data(skb);
	memcpy(msg, &nfp_flow->meta, meta_len);
	memcpy(&msg[meta_len], nfp_flow->unmasked_data, key_len);
	memcpy(&msg[meta_len + key_len], nfp_flow->mask_data, mask_len);
	memcpy(&msg[meta_len + key_len + mask_len],
	       nfp_flow->action_data, act_len);

	/* Convert back to bytes as software expects
	 * lengths in units of bytes.
	 */
	nfp_flow->meta.key_len <<= NFP_FL_LW_SIZ;
	nfp_flow->meta.mask_len <<= NFP_FL_LW_SIZ;
	nfp_flow->meta.act_len <<= NFP_FL_LW_SIZ;

	nfp_ctrl_tx(app->ctrl, skb);

	return 0;
}

static bool nfp_flower_check_higher_than_mac(struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);

	return flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ICMP);
}

static bool nfp_flower_check_higher_than_l3(struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);

	return flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ICMP);
}

static int
nfp_flower_calc_opt_layer(struct flow_dissector_key_enc_opts *enc_opts,
			  u32 *key_layer_two, int *key_size,
			  struct netlink_ext_ack *extack)
{
	if (enc_opts->len > NFP_FL_MAX_GENEVE_OPT_KEY) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: geneve options exceed maximum length");
		return -EOPNOTSUPP;
	}

	if (enc_opts->len > 0) {
		*key_layer_two |= NFP_FLOWER_LAYER2_GENEVE_OP;
		*key_size += sizeof(struct nfp_flower_geneve_options);
	}

	return 0;
}

static int
nfp_flower_calc_udp_tun_layer(struct flow_dissector_key_ports *enc_ports,
			      struct flow_dissector_key_enc_opts *enc_op,
			      u32 *key_layer_two, u8 *key_layer, int *key_size,
			      struct nfp_flower_priv *priv,
			      enum nfp_flower_tun_type *tun_type,
			      struct netlink_ext_ack *extack)
{
	int err;

	switch (enc_ports->dst) {
	case htons(IANA_VXLAN_UDP_PORT):
		*tun_type = NFP_FL_TUNNEL_VXLAN;
		*key_layer |= NFP_FLOWER_LAYER_VXLAN;
		*key_size += sizeof(struct nfp_flower_ipv4_udp_tun);

		if (enc_op) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: encap options not supported on vxlan tunnels");
			return -EOPNOTSUPP;
		}
		break;
	case htons(GENEVE_UDP_PORT):
		if (!(priv->flower_ext_feats & NFP_FL_FEATS_GENEVE)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support geneve offload");
			return -EOPNOTSUPP;
		}
		*tun_type = NFP_FL_TUNNEL_GENEVE;
		*key_layer |= NFP_FLOWER_LAYER_EXT_META;
		*key_size += sizeof(struct nfp_flower_ext_meta);
		*key_layer_two |= NFP_FLOWER_LAYER2_GENEVE;
		*key_size += sizeof(struct nfp_flower_ipv4_udp_tun);

		if (!enc_op)
			break;
		if (!(priv->flower_ext_feats & NFP_FL_FEATS_GENEVE_OPT)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support geneve option offload");
			return -EOPNOTSUPP;
		}
		err = nfp_flower_calc_opt_layer(enc_op, key_layer_two,
						key_size, extack);
		if (err)
			return err;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: tunnel type unknown");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nfp_flower_calculate_key_layers(struct nfp_app *app,
				struct net_device *netdev,
				struct nfp_fl_key_ls *ret_key_ls,
				struct flow_cls_offload *flow,
				enum nfp_flower_tun_type *tun_type,
				struct netlink_ext_ack *extack)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(flow);
	struct flow_dissector *dissector = rule->match.dissector;
	struct flow_match_basic basic = { NULL, NULL};
	struct nfp_flower_priv *priv = app->priv;
	u32 key_layer_two;
	u8 key_layer;
	int key_size;
	int err;

	if (dissector->used_keys & ~NFP_FLOWER_WHITELIST_DISSECTOR) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: match not supported");
		return -EOPNOTSUPP;
	}

	/* If any tun dissector is used then the required set must be used. */
	if (dissector->used_keys & NFP_FLOWER_WHITELIST_TUN_DISSECTOR &&
	    (dissector->used_keys & NFP_FLOWER_WHITELIST_TUN_DISSECTOR_R)
	    != NFP_FLOWER_WHITELIST_TUN_DISSECTOR_R) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: tunnel match not supported");
		return -EOPNOTSUPP;
	}

	key_layer_two = 0;
	key_layer = NFP_FLOWER_LAYER_PORT;
	key_size = sizeof(struct nfp_flower_meta_tci) +
		   sizeof(struct nfp_flower_in_port);

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS) ||
	    flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_MPLS)) {
		key_layer |= NFP_FLOWER_LAYER_MAC;
		key_size += sizeof(struct nfp_flower_mac_mpls);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan vlan;

		flow_rule_match_vlan(rule, &vlan);
		if (!(priv->flower_ext_feats & NFP_FL_FEATS_VLAN_PCP) &&
		    vlan.key->vlan_priority) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support VLAN PCP offload");
			return -EOPNOTSUPP;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_match_enc_opts enc_op = { NULL, NULL };
		struct flow_match_ipv4_addrs ipv4_addrs;
		struct flow_match_control enc_ctl;
		struct flow_match_ports enc_ports;

		flow_rule_match_enc_control(rule, &enc_ctl);

		if (enc_ctl.mask->addr_type != 0xffff) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: wildcarded protocols on tunnels are not supported");
			return -EOPNOTSUPP;
		}
		if (enc_ctl.key->addr_type != FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: only IPv4 tunnels are supported");
			return -EOPNOTSUPP;
		}

		/* These fields are already verified as used. */
		flow_rule_match_enc_ipv4_addrs(rule, &ipv4_addrs);
		if (ipv4_addrs.mask->dst != cpu_to_be32(~0)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: only an exact match IPv4 destination address is supported");
			return -EOPNOTSUPP;
		}

		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_OPTS))
			flow_rule_match_enc_opts(rule, &enc_op);


		if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
			/* check if GRE, which has no enc_ports */
			if (netif_is_gretap(netdev)) {
				*tun_type = NFP_FL_TUNNEL_GRE;
				key_layer |= NFP_FLOWER_LAYER_EXT_META;
				key_size += sizeof(struct nfp_flower_ext_meta);
				key_layer_two |= NFP_FLOWER_LAYER2_GRE;
				key_size +=
					sizeof(struct nfp_flower_ipv4_gre_tun);

				if (enc_op.key) {
					NL_SET_ERR_MSG_MOD(extack, "unsupported offload: encap options not supported on GRE tunnels");
					return -EOPNOTSUPP;
				}
			} else {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: an exact match on L4 destination port is required for non-GRE tunnels");
				return -EOPNOTSUPP;
			}
		} else {
			flow_rule_match_enc_ports(rule, &enc_ports);
			if (enc_ports.mask->dst != cpu_to_be16(~0)) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: only an exact match L4 destination port is supported");
				return -EOPNOTSUPP;
			}

			err = nfp_flower_calc_udp_tun_layer(enc_ports.key,
							    enc_op.key,
							    &key_layer_two,
							    &key_layer,
							    &key_size, priv,
							    tun_type, extack);
			if (err)
				return err;

			/* Ensure the ingress netdev matches the expected
			 * tun type.
			 */
			if (!nfp_fl_netdev_is_tunnel_type(netdev, *tun_type)) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: ingress netdev does not match the expected tunnel type");
				return -EOPNOTSUPP;
			}
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC))
		flow_rule_match_basic(rule, &basic);

	if (basic.mask && basic.mask->n_proto) {
		/* Ethernet type is present in the key. */
		switch (basic.key->n_proto) {
		case cpu_to_be16(ETH_P_IP):
			key_layer |= NFP_FLOWER_LAYER_IPV4;
			key_size += sizeof(struct nfp_flower_ipv4);
			break;

		case cpu_to_be16(ETH_P_IPV6):
			key_layer |= NFP_FLOWER_LAYER_IPV6;
			key_size += sizeof(struct nfp_flower_ipv6);
			break;

		/* Currently we do not offload ARP
		 * because we rely on it to get to the host.
		 */
		case cpu_to_be16(ETH_P_ARP):
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: ARP not supported");
			return -EOPNOTSUPP;

		case cpu_to_be16(ETH_P_MPLS_UC):
		case cpu_to_be16(ETH_P_MPLS_MC):
			if (!(key_layer & NFP_FLOWER_LAYER_MAC)) {
				key_layer |= NFP_FLOWER_LAYER_MAC;
				key_size += sizeof(struct nfp_flower_mac_mpls);
			}
			break;

		/* Will be included in layer 2. */
		case cpu_to_be16(ETH_P_8021Q):
			break;

		default:
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: match on given EtherType is not supported");
			return -EOPNOTSUPP;
		}
	} else if (nfp_flower_check_higher_than_mac(flow)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: cannot match above L2 without specified EtherType");
		return -EOPNOTSUPP;
	}

	if (basic.mask && basic.mask->ip_proto) {
		switch (basic.key->ip_proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
		case IPPROTO_SCTP:
		case IPPROTO_ICMP:
		case IPPROTO_ICMPV6:
			key_layer |= NFP_FLOWER_LAYER_TP;
			key_size += sizeof(struct nfp_flower_tp_ports);
			break;
		}
	}

	if (!(key_layer & NFP_FLOWER_LAYER_TP) &&
	    nfp_flower_check_higher_than_l3(flow)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: cannot match on L4 information without specified IP protocol type");
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_match_tcp tcp;
		u32 tcp_flags;

		flow_rule_match_tcp(rule, &tcp);
		tcp_flags = be16_to_cpu(tcp.key->flags);

		if (tcp_flags & ~NFP_FLOWER_SUPPORTED_TCPFLAGS) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: no match support for selected TCP flags");
			return -EOPNOTSUPP;
		}

		/* We only support PSH and URG flags when either
		 * FIN, SYN or RST is present as well.
		 */
		if ((tcp_flags & (TCPHDR_PSH | TCPHDR_URG)) &&
		    !(tcp_flags & (TCPHDR_FIN | TCPHDR_SYN | TCPHDR_RST))) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: PSH and URG is only supported when used with FIN, SYN or RST");
			return -EOPNOTSUPP;
		}

		/* We need to store TCP flags in the either the IPv4 or IPv6 key
		 * space, thus we need to ensure we include a IPv4/IPv6 key
		 * layer if we have not done so already.
		 */
		if (!basic.key) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: match on TCP flags requires a match on L3 protocol");
			return -EOPNOTSUPP;
		}

		if (!(key_layer & NFP_FLOWER_LAYER_IPV4) &&
		    !(key_layer & NFP_FLOWER_LAYER_IPV6)) {
			switch (basic.key->n_proto) {
			case cpu_to_be16(ETH_P_IP):
				key_layer |= NFP_FLOWER_LAYER_IPV4;
				key_size += sizeof(struct nfp_flower_ipv4);
				break;

			case cpu_to_be16(ETH_P_IPV6):
					key_layer |= NFP_FLOWER_LAYER_IPV6;
				key_size += sizeof(struct nfp_flower_ipv6);
				break;

			default:
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: match on TCP flags requires a match on IPv4/IPv6");
				return -EOPNOTSUPP;
			}
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control ctl;

		flow_rule_match_control(rule, &ctl);
		if (ctl.key->flags & ~NFP_FLOWER_SUPPORTED_CTLFLAGS) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: match on unknown control flag");
			return -EOPNOTSUPP;
		}
	}

	ret_key_ls->key_layer = key_layer;
	ret_key_ls->key_layer_two = key_layer_two;
	ret_key_ls->key_size = key_size;

	return 0;
}

static struct nfp_fl_payload *
nfp_flower_allocate_new(struct nfp_fl_key_ls *key_layer)
{
	struct nfp_fl_payload *flow_pay;

	flow_pay = kmalloc(sizeof(*flow_pay), GFP_KERNEL);
	if (!flow_pay)
		return NULL;

	flow_pay->meta.key_len = key_layer->key_size;
	flow_pay->unmasked_data = kmalloc(key_layer->key_size, GFP_KERNEL);
	if (!flow_pay->unmasked_data)
		goto err_free_flow;

	flow_pay->meta.mask_len = key_layer->key_size;
	flow_pay->mask_data = kmalloc(key_layer->key_size, GFP_KERNEL);
	if (!flow_pay->mask_data)
		goto err_free_unmasked;

	flow_pay->action_data = kmalloc(NFP_FL_MAX_A_SIZ, GFP_KERNEL);
	if (!flow_pay->action_data)
		goto err_free_mask;

	flow_pay->nfp_tun_ipv4_addr = 0;
	flow_pay->meta.flags = 0;
	INIT_LIST_HEAD(&flow_pay->linked_flows);
	flow_pay->in_hw = false;

	return flow_pay;

err_free_mask:
	kfree(flow_pay->mask_data);
err_free_unmasked:
	kfree(flow_pay->unmasked_data);
err_free_flow:
	kfree(flow_pay);
	return NULL;
}

static int
nfp_flower_update_merge_with_actions(struct nfp_fl_payload *flow,
				     struct nfp_flower_merge_check *merge,
				     u8 *last_act_id, int *act_out)
{
	struct nfp_fl_set_ipv6_tc_hl_fl *ipv6_tc_hl_fl;
	struct nfp_fl_set_ip4_ttl_tos *ipv4_ttl_tos;
	struct nfp_fl_set_ip4_addrs *ipv4_add;
	struct nfp_fl_set_ipv6_addr *ipv6_add;
	struct nfp_fl_push_vlan *push_vlan;
	struct nfp_fl_set_tport *tport;
	struct nfp_fl_set_eth *eth;
	struct nfp_fl_act_head *a;
	unsigned int act_off = 0;
	u8 act_id = 0;
	u8 *ports;
	int i;

	while (act_off < flow->meta.act_len) {
		a = (struct nfp_fl_act_head *)&flow->action_data[act_off];
		act_id = a->jump_id;

		switch (act_id) {
		case NFP_FL_ACTION_OPCODE_OUTPUT:
			if (act_out)
				(*act_out)++;
			break;
		case NFP_FL_ACTION_OPCODE_PUSH_VLAN:
			push_vlan = (struct nfp_fl_push_vlan *)a;
			if (push_vlan->vlan_tci)
				merge->tci = cpu_to_be16(0xffff);
			break;
		case NFP_FL_ACTION_OPCODE_POP_VLAN:
			merge->tci = cpu_to_be16(0);
			break;
		case NFP_FL_ACTION_OPCODE_SET_IPV4_TUNNEL:
			/* New tunnel header means l2 to l4 can be matched. */
			eth_broadcast_addr(&merge->l2.mac_dst[0]);
			eth_broadcast_addr(&merge->l2.mac_src[0]);
			memset(&merge->l4, 0xff,
			       sizeof(struct nfp_flower_tp_ports));
			memset(&merge->ipv4, 0xff,
			       sizeof(struct nfp_flower_ipv4));
			break;
		case NFP_FL_ACTION_OPCODE_SET_ETHERNET:
			eth = (struct nfp_fl_set_eth *)a;
			for (i = 0; i < ETH_ALEN; i++)
				merge->l2.mac_dst[i] |= eth->eth_addr_mask[i];
			for (i = 0; i < ETH_ALEN; i++)
				merge->l2.mac_src[i] |=
					eth->eth_addr_mask[ETH_ALEN + i];
			break;
		case NFP_FL_ACTION_OPCODE_SET_IPV4_ADDRS:
			ipv4_add = (struct nfp_fl_set_ip4_addrs *)a;
			merge->ipv4.ipv4_src |= ipv4_add->ipv4_src_mask;
			merge->ipv4.ipv4_dst |= ipv4_add->ipv4_dst_mask;
			break;
		case NFP_FL_ACTION_OPCODE_SET_IPV4_TTL_TOS:
			ipv4_ttl_tos = (struct nfp_fl_set_ip4_ttl_tos *)a;
			merge->ipv4.ip_ext.ttl |= ipv4_ttl_tos->ipv4_ttl_mask;
			merge->ipv4.ip_ext.tos |= ipv4_ttl_tos->ipv4_tos_mask;
			break;
		case NFP_FL_ACTION_OPCODE_SET_IPV6_SRC:
			ipv6_add = (struct nfp_fl_set_ipv6_addr *)a;
			for (i = 0; i < 4; i++)
				merge->ipv6.ipv6_src.in6_u.u6_addr32[i] |=
					ipv6_add->ipv6[i].mask;
			break;
		case NFP_FL_ACTION_OPCODE_SET_IPV6_DST:
			ipv6_add = (struct nfp_fl_set_ipv6_addr *)a;
			for (i = 0; i < 4; i++)
				merge->ipv6.ipv6_dst.in6_u.u6_addr32[i] |=
					ipv6_add->ipv6[i].mask;
			break;
		case NFP_FL_ACTION_OPCODE_SET_IPV6_TC_HL_FL:
			ipv6_tc_hl_fl = (struct nfp_fl_set_ipv6_tc_hl_fl *)a;
			merge->ipv6.ip_ext.ttl |=
				ipv6_tc_hl_fl->ipv6_hop_limit_mask;
			merge->ipv6.ip_ext.tos |= ipv6_tc_hl_fl->ipv6_tc_mask;
			merge->ipv6.ipv6_flow_label_exthdr |=
				ipv6_tc_hl_fl->ipv6_label_mask;
			break;
		case NFP_FL_ACTION_OPCODE_SET_UDP:
		case NFP_FL_ACTION_OPCODE_SET_TCP:
			tport = (struct nfp_fl_set_tport *)a;
			ports = (u8 *)&merge->l4.port_src;
			for (i = 0; i < 4; i++)
				ports[i] |= tport->tp_port_mask[i];
			break;
		case NFP_FL_ACTION_OPCODE_PRE_TUNNEL:
		case NFP_FL_ACTION_OPCODE_PRE_LAG:
		case NFP_FL_ACTION_OPCODE_PUSH_GENEVE:
			break;
		default:
			return -EOPNOTSUPP;
		}

		act_off += a->len_lw << NFP_FL_LW_SIZ;
	}

	if (last_act_id)
		*last_act_id = act_id;

	return 0;
}

static int
nfp_flower_populate_merge_match(struct nfp_fl_payload *flow,
				struct nfp_flower_merge_check *merge,
				bool extra_fields)
{
	struct nfp_flower_meta_tci *meta_tci;
	u8 *mask = flow->mask_data;
	u8 key_layer, match_size;

	memset(merge, 0, sizeof(struct nfp_flower_merge_check));

	meta_tci = (struct nfp_flower_meta_tci *)mask;
	key_layer = meta_tci->nfp_flow_key_layer;

	if (key_layer & ~NFP_FLOWER_MERGE_FIELDS && !extra_fields)
		return -EOPNOTSUPP;

	merge->tci = meta_tci->tci;
	mask += sizeof(struct nfp_flower_meta_tci);

	if (key_layer & NFP_FLOWER_LAYER_EXT_META)
		mask += sizeof(struct nfp_flower_ext_meta);

	mask += sizeof(struct nfp_flower_in_port);

	if (key_layer & NFP_FLOWER_LAYER_MAC) {
		match_size = sizeof(struct nfp_flower_mac_mpls);
		memcpy(&merge->l2, mask, match_size);
		mask += match_size;
	}

	if (key_layer & NFP_FLOWER_LAYER_TP) {
		match_size = sizeof(struct nfp_flower_tp_ports);
		memcpy(&merge->l4, mask, match_size);
		mask += match_size;
	}

	if (key_layer & NFP_FLOWER_LAYER_IPV4) {
		match_size = sizeof(struct nfp_flower_ipv4);
		memcpy(&merge->ipv4, mask, match_size);
	}

	if (key_layer & NFP_FLOWER_LAYER_IPV6) {
		match_size = sizeof(struct nfp_flower_ipv6);
		memcpy(&merge->ipv6, mask, match_size);
	}

	return 0;
}

static int
nfp_flower_can_merge(struct nfp_fl_payload *sub_flow1,
		     struct nfp_fl_payload *sub_flow2)
{
	/* Two flows can be merged if sub_flow2 only matches on bits that are
	 * either matched by sub_flow1 or set by a sub_flow1 action. This
	 * ensures that every packet that hits sub_flow1 and recirculates is
	 * guaranteed to hit sub_flow2.
	 */
	struct nfp_flower_merge_check sub_flow1_merge, sub_flow2_merge;
	int err, act_out = 0;
	u8 last_act_id = 0;

	err = nfp_flower_populate_merge_match(sub_flow1, &sub_flow1_merge,
					      true);
	if (err)
		return err;

	err = nfp_flower_populate_merge_match(sub_flow2, &sub_flow2_merge,
					      false);
	if (err)
		return err;

	err = nfp_flower_update_merge_with_actions(sub_flow1, &sub_flow1_merge,
						   &last_act_id, &act_out);
	if (err)
		return err;

	/* Must only be 1 output action and it must be the last in sequence. */
	if (act_out != 1 || last_act_id != NFP_FL_ACTION_OPCODE_OUTPUT)
		return -EOPNOTSUPP;

	/* Reject merge if sub_flow2 matches on something that is not matched
	 * on or set in an action by sub_flow1.
	 */
	err = bitmap_andnot(sub_flow2_merge.vals, sub_flow2_merge.vals,
			    sub_flow1_merge.vals,
			    sizeof(struct nfp_flower_merge_check) * 8);
	if (err)
		return -EINVAL;

	return 0;
}

static unsigned int
nfp_flower_copy_pre_actions(char *act_dst, char *act_src, int len,
			    bool *tunnel_act)
{
	unsigned int act_off = 0, act_len;
	struct nfp_fl_act_head *a;
	u8 act_id = 0;

	while (act_off < len) {
		a = (struct nfp_fl_act_head *)&act_src[act_off];
		act_len = a->len_lw << NFP_FL_LW_SIZ;
		act_id = a->jump_id;

		switch (act_id) {
		case NFP_FL_ACTION_OPCODE_PRE_TUNNEL:
			if (tunnel_act)
				*tunnel_act = true;
			/* fall through */
		case NFP_FL_ACTION_OPCODE_PRE_LAG:
			memcpy(act_dst + act_off, act_src + act_off, act_len);
			break;
		default:
			return act_off;
		}

		act_off += act_len;
	}

	return act_off;
}

static int nfp_fl_verify_post_tun_acts(char *acts, int len)
{
	struct nfp_fl_act_head *a;
	unsigned int act_off = 0;

	while (act_off < len) {
		a = (struct nfp_fl_act_head *)&acts[act_off];
		if (a->jump_id != NFP_FL_ACTION_OPCODE_OUTPUT)
			return -EOPNOTSUPP;

		act_off += a->len_lw << NFP_FL_LW_SIZ;
	}

	return 0;
}

static int
nfp_flower_merge_action(struct nfp_fl_payload *sub_flow1,
			struct nfp_fl_payload *sub_flow2,
			struct nfp_fl_payload *merge_flow)
{
	unsigned int sub1_act_len, sub2_act_len, pre_off1, pre_off2;
	bool tunnel_act = false;
	char *merge_act;
	int err;

	/* The last action of sub_flow1 must be output - do not merge this. */
	sub1_act_len = sub_flow1->meta.act_len - sizeof(struct nfp_fl_output);
	sub2_act_len = sub_flow2->meta.act_len;

	if (!sub2_act_len)
		return -EINVAL;

	if (sub1_act_len + sub2_act_len > NFP_FL_MAX_A_SIZ)
		return -EINVAL;

	/* A shortcut can only be applied if there is a single action. */
	if (sub1_act_len)
		merge_flow->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);
	else
		merge_flow->meta.shortcut = sub_flow2->meta.shortcut;

	merge_flow->meta.act_len = sub1_act_len + sub2_act_len;
	merge_act = merge_flow->action_data;

	/* Copy any pre-actions to the start of merge flow action list. */
	pre_off1 = nfp_flower_copy_pre_actions(merge_act,
					       sub_flow1->action_data,
					       sub1_act_len, &tunnel_act);
	merge_act += pre_off1;
	sub1_act_len -= pre_off1;
	pre_off2 = nfp_flower_copy_pre_actions(merge_act,
					       sub_flow2->action_data,
					       sub2_act_len, NULL);
	merge_act += pre_off2;
	sub2_act_len -= pre_off2;

	/* FW does a tunnel push when egressing, therefore, if sub_flow 1 pushes
	 * a tunnel, sub_flow 2 can only have output actions for a valid merge.
	 */
	if (tunnel_act) {
		char *post_tun_acts = &sub_flow2->action_data[pre_off2];

		err = nfp_fl_verify_post_tun_acts(post_tun_acts, sub2_act_len);
		if (err)
			return err;
	}

	/* Copy remaining actions from sub_flows 1 and 2. */
	memcpy(merge_act, sub_flow1->action_data + pre_off1, sub1_act_len);
	merge_act += sub1_act_len;
	memcpy(merge_act, sub_flow2->action_data + pre_off2, sub2_act_len);

	return 0;
}

/* Flow link code should only be accessed under RTNL. */
static void nfp_flower_unlink_flow(struct nfp_fl_payload_link *link)
{
	list_del(&link->merge_flow.list);
	list_del(&link->sub_flow.list);
	kfree(link);
}

static void nfp_flower_unlink_flows(struct nfp_fl_payload *merge_flow,
				    struct nfp_fl_payload *sub_flow)
{
	struct nfp_fl_payload_link *link;

	list_for_each_entry(link, &merge_flow->linked_flows, merge_flow.list)
		if (link->sub_flow.flow == sub_flow) {
			nfp_flower_unlink_flow(link);
			return;
		}
}

static int nfp_flower_link_flows(struct nfp_fl_payload *merge_flow,
				 struct nfp_fl_payload *sub_flow)
{
	struct nfp_fl_payload_link *link;

	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	link->merge_flow.flow = merge_flow;
	list_add_tail(&link->merge_flow.list, &merge_flow->linked_flows);
	link->sub_flow.flow = sub_flow;
	list_add_tail(&link->sub_flow.list, &sub_flow->linked_flows);

	return 0;
}

/**
 * nfp_flower_merge_offloaded_flows() - Merge 2 existing flows to single flow.
 * @app:	Pointer to the APP handle
 * @sub_flow1:	Initial flow matched to produce merge hint
 * @sub_flow2:	Post recirculation flow matched in merge hint
 *
 * Combines 2 flows (if valid) to a single flow, removing the initial from hw
 * and offloading the new, merged flow.
 *
 * Return: negative value on error, 0 in success.
 */
int nfp_flower_merge_offloaded_flows(struct nfp_app *app,
				     struct nfp_fl_payload *sub_flow1,
				     struct nfp_fl_payload *sub_flow2)
{
	struct flow_cls_offload merge_tc_off;
	struct nfp_flower_priv *priv = app->priv;
	struct netlink_ext_ack *extack = NULL;
	struct nfp_fl_payload *merge_flow;
	struct nfp_fl_key_ls merge_key_ls;
	int err;

	ASSERT_RTNL();

	extack = merge_tc_off.common.extack;
	if (sub_flow1 == sub_flow2 ||
	    nfp_flower_is_merge_flow(sub_flow1) ||
	    nfp_flower_is_merge_flow(sub_flow2))
		return -EINVAL;

	err = nfp_flower_can_merge(sub_flow1, sub_flow2);
	if (err)
		return err;

	merge_key_ls.key_size = sub_flow1->meta.key_len;

	merge_flow = nfp_flower_allocate_new(&merge_key_ls);
	if (!merge_flow)
		return -ENOMEM;

	merge_flow->tc_flower_cookie = (unsigned long)merge_flow;
	merge_flow->ingress_dev = sub_flow1->ingress_dev;

	memcpy(merge_flow->unmasked_data, sub_flow1->unmasked_data,
	       sub_flow1->meta.key_len);
	memcpy(merge_flow->mask_data, sub_flow1->mask_data,
	       sub_flow1->meta.mask_len);

	err = nfp_flower_merge_action(sub_flow1, sub_flow2, merge_flow);
	if (err)
		goto err_destroy_merge_flow;

	err = nfp_flower_link_flows(merge_flow, sub_flow1);
	if (err)
		goto err_destroy_merge_flow;

	err = nfp_flower_link_flows(merge_flow, sub_flow2);
	if (err)
		goto err_unlink_sub_flow1;

	merge_tc_off.cookie = merge_flow->tc_flower_cookie;
	err = nfp_compile_flow_metadata(app, &merge_tc_off, merge_flow,
					merge_flow->ingress_dev, extack);
	if (err)
		goto err_unlink_sub_flow2;

	err = rhashtable_insert_fast(&priv->flow_table, &merge_flow->fl_node,
				     nfp_flower_table_params);
	if (err)
		goto err_release_metadata;

	err = nfp_flower_xmit_flow(app, merge_flow,
				   NFP_FLOWER_CMSG_TYPE_FLOW_MOD);
	if (err)
		goto err_remove_rhash;

	merge_flow->in_hw = true;
	sub_flow1->in_hw = false;

	return 0;

err_remove_rhash:
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->flow_table,
					    &merge_flow->fl_node,
					    nfp_flower_table_params));
err_release_metadata:
	nfp_modify_flow_metadata(app, merge_flow);
err_unlink_sub_flow2:
	nfp_flower_unlink_flows(merge_flow, sub_flow2);
err_unlink_sub_flow1:
	nfp_flower_unlink_flows(merge_flow, sub_flow1);
err_destroy_merge_flow:
	kfree(merge_flow->action_data);
	kfree(merge_flow->mask_data);
	kfree(merge_flow->unmasked_data);
	kfree(merge_flow);
	return err;
}

/**
 * nfp_flower_add_offload() - Adds a new flow to hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure.
 *
 * Adds a new flow to the repeated hash structure and action payload.
 *
 * Return: negative value on error, 0 if configured successfully.
 */
static int
nfp_flower_add_offload(struct nfp_app *app, struct net_device *netdev,
		       struct flow_cls_offload *flow)
{
	enum nfp_flower_tun_type tun_type = NFP_FL_TUNNEL_NONE;
	struct nfp_flower_priv *priv = app->priv;
	struct netlink_ext_ack *extack = NULL;
	struct nfp_fl_payload *flow_pay;
	struct nfp_fl_key_ls *key_layer;
	struct nfp_port *port = NULL;
	int err;

	extack = flow->common.extack;
	if (nfp_netdev_is_nfp_repr(netdev))
		port = nfp_port_from_netdev(netdev);

	key_layer = kmalloc(sizeof(*key_layer), GFP_KERNEL);
	if (!key_layer)
		return -ENOMEM;

	err = nfp_flower_calculate_key_layers(app, netdev, key_layer, flow,
					      &tun_type, extack);
	if (err)
		goto err_free_key_ls;

	flow_pay = nfp_flower_allocate_new(key_layer);
	if (!flow_pay) {
		err = -ENOMEM;
		goto err_free_key_ls;
	}

	err = nfp_flower_compile_flow_match(app, flow, key_layer, netdev,
					    flow_pay, tun_type, extack);
	if (err)
		goto err_destroy_flow;

	err = nfp_flower_compile_action(app, flow, netdev, flow_pay, extack);
	if (err)
		goto err_destroy_flow;

	err = nfp_compile_flow_metadata(app, flow, flow_pay, netdev, extack);
	if (err)
		goto err_destroy_flow;

	flow_pay->tc_flower_cookie = flow->cookie;
	err = rhashtable_insert_fast(&priv->flow_table, &flow_pay->fl_node,
				     nfp_flower_table_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot insert flow into tables for offloads");
		goto err_release_metadata;
	}

	err = nfp_flower_xmit_flow(app, flow_pay,
				   NFP_FLOWER_CMSG_TYPE_FLOW_ADD);
	if (err)
		goto err_remove_rhash;

	if (port)
		port->tc_offload_cnt++;

	flow_pay->in_hw = true;

	/* Deallocate flow payload when flower rule has been destroyed. */
	kfree(key_layer);

	return 0;

err_remove_rhash:
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->flow_table,
					    &flow_pay->fl_node,
					    nfp_flower_table_params));
err_release_metadata:
	nfp_modify_flow_metadata(app, flow_pay);
err_destroy_flow:
	kfree(flow_pay->action_data);
	kfree(flow_pay->mask_data);
	kfree(flow_pay->unmasked_data);
	kfree(flow_pay);
err_free_key_ls:
	kfree(key_layer);
	return err;
}

static void
nfp_flower_remove_merge_flow(struct nfp_app *app,
			     struct nfp_fl_payload *del_sub_flow,
			     struct nfp_fl_payload *merge_flow)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload_link *link, *temp;
	struct nfp_fl_payload *origin;
	bool mod = false;
	int err;

	link = list_first_entry(&merge_flow->linked_flows,
				struct nfp_fl_payload_link, merge_flow.list);
	origin = link->sub_flow.flow;

	/* Re-add rule the merge had overwritten if it has not been deleted. */
	if (origin != del_sub_flow)
		mod = true;

	err = nfp_modify_flow_metadata(app, merge_flow);
	if (err) {
		nfp_flower_cmsg_warn(app, "Metadata fail for merge flow delete.\n");
		goto err_free_links;
	}

	if (!mod) {
		err = nfp_flower_xmit_flow(app, merge_flow,
					   NFP_FLOWER_CMSG_TYPE_FLOW_DEL);
		if (err) {
			nfp_flower_cmsg_warn(app, "Failed to delete merged flow.\n");
			goto err_free_links;
		}
	} else {
		__nfp_modify_flow_metadata(priv, origin);
		err = nfp_flower_xmit_flow(app, origin,
					   NFP_FLOWER_CMSG_TYPE_FLOW_MOD);
		if (err)
			nfp_flower_cmsg_warn(app, "Failed to revert merge flow.\n");
		origin->in_hw = true;
	}

err_free_links:
	/* Clean any links connected with the merged flow. */
	list_for_each_entry_safe(link, temp, &merge_flow->linked_flows,
				 merge_flow.list)
		nfp_flower_unlink_flow(link);

	kfree(merge_flow->action_data);
	kfree(merge_flow->mask_data);
	kfree(merge_flow->unmasked_data);
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->flow_table,
					    &merge_flow->fl_node,
					    nfp_flower_table_params));
	kfree_rcu(merge_flow, rcu);
}

static void
nfp_flower_del_linked_merge_flows(struct nfp_app *app,
				  struct nfp_fl_payload *sub_flow)
{
	struct nfp_fl_payload_link *link, *temp;

	/* Remove any merge flow formed from the deleted sub_flow. */
	list_for_each_entry_safe(link, temp, &sub_flow->linked_flows,
				 sub_flow.list)
		nfp_flower_remove_merge_flow(app, sub_flow,
					     link->merge_flow.flow);
}

/**
 * nfp_flower_del_offload() - Removes a flow from hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure
 *
 * Removes a flow from the repeated hash structure and clears the
 * action payload. Any flows merged from this are also deleted.
 *
 * Return: negative value on error, 0 if removed successfully.
 */
static int
nfp_flower_del_offload(struct nfp_app *app, struct net_device *netdev,
		       struct flow_cls_offload *flow)
{
	struct nfp_flower_priv *priv = app->priv;
	struct netlink_ext_ack *extack = NULL;
	struct nfp_fl_payload *nfp_flow;
	struct nfp_port *port = NULL;
	int err;

	extack = flow->common.extack;
	if (nfp_netdev_is_nfp_repr(netdev))
		port = nfp_port_from_netdev(netdev);

	nfp_flow = nfp_flower_search_fl_table(app, flow->cookie, netdev);
	if (!nfp_flow) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot remove flow that does not exist");
		return -ENOENT;
	}

	err = nfp_modify_flow_metadata(app, nfp_flow);
	if (err)
		goto err_free_merge_flow;

	if (nfp_flow->nfp_tun_ipv4_addr)
		nfp_tunnel_del_ipv4_off(app, nfp_flow->nfp_tun_ipv4_addr);

	if (!nfp_flow->in_hw) {
		err = 0;
		goto err_free_merge_flow;
	}

	err = nfp_flower_xmit_flow(app, nfp_flow,
				   NFP_FLOWER_CMSG_TYPE_FLOW_DEL);
	/* Fall through on error. */

err_free_merge_flow:
	nfp_flower_del_linked_merge_flows(app, nfp_flow);
	if (port)
		port->tc_offload_cnt--;
	kfree(nfp_flow->action_data);
	kfree(nfp_flow->mask_data);
	kfree(nfp_flow->unmasked_data);
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->flow_table,
					    &nfp_flow->fl_node,
					    nfp_flower_table_params));
	kfree_rcu(nfp_flow, rcu);
	return err;
}

static void
__nfp_flower_update_merge_stats(struct nfp_app *app,
				struct nfp_fl_payload *merge_flow)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload_link *link;
	struct nfp_fl_payload *sub_flow;
	u64 pkts, bytes, used;
	u32 ctx_id;

	ctx_id = be32_to_cpu(merge_flow->meta.host_ctx_id);
	pkts = priv->stats[ctx_id].pkts;
	/* Do not cycle subflows if no stats to distribute. */
	if (!pkts)
		return;
	bytes = priv->stats[ctx_id].bytes;
	used = priv->stats[ctx_id].used;

	/* Reset stats for the merge flow. */
	priv->stats[ctx_id].pkts = 0;
	priv->stats[ctx_id].bytes = 0;

	/* The merge flow has received stats updates from firmware.
	 * Distribute these stats to all subflows that form the merge.
	 * The stats will collected from TC via the subflows.
	 */
	list_for_each_entry(link, &merge_flow->linked_flows, merge_flow.list) {
		sub_flow = link->sub_flow.flow;
		ctx_id = be32_to_cpu(sub_flow->meta.host_ctx_id);
		priv->stats[ctx_id].pkts += pkts;
		priv->stats[ctx_id].bytes += bytes;
		max_t(u64, priv->stats[ctx_id].used, used);
	}
}

static void
nfp_flower_update_merge_stats(struct nfp_app *app,
			      struct nfp_fl_payload *sub_flow)
{
	struct nfp_fl_payload_link *link;

	/* Get merge flows that the subflow forms to distribute their stats. */
	list_for_each_entry(link, &sub_flow->linked_flows, sub_flow.list)
		__nfp_flower_update_merge_stats(app, link->merge_flow.flow);
}

/**
 * nfp_flower_get_stats() - Populates flow stats obtained from hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	Netdev structure.
 * @flow:	TC flower classifier offload structure
 *
 * Populates a flow statistics structure which which corresponds to a
 * specific flow.
 *
 * Return: negative value on error, 0 if stats populated successfully.
 */
static int
nfp_flower_get_stats(struct nfp_app *app, struct net_device *netdev,
		     struct flow_cls_offload *flow)
{
	struct nfp_flower_priv *priv = app->priv;
	struct netlink_ext_ack *extack = NULL;
	struct nfp_fl_payload *nfp_flow;
	u32 ctx_id;

	extack = flow->common.extack;
	nfp_flow = nfp_flower_search_fl_table(app, flow->cookie, netdev);
	if (!nfp_flow) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot dump stats for flow that does not exist");
		return -EINVAL;
	}

	ctx_id = be32_to_cpu(nfp_flow->meta.host_ctx_id);

	spin_lock_bh(&priv->stats_lock);
	/* If request is for a sub_flow, update stats from merged flows. */
	if (!list_empty(&nfp_flow->linked_flows))
		nfp_flower_update_merge_stats(app, nfp_flow);

	flow_stats_update(&flow->stats, priv->stats[ctx_id].bytes,
			  priv->stats[ctx_id].pkts, priv->stats[ctx_id].used);

	priv->stats[ctx_id].pkts = 0;
	priv->stats[ctx_id].bytes = 0;
	spin_unlock_bh(&priv->stats_lock);

	return 0;
}

static int
nfp_flower_repr_offload(struct nfp_app *app, struct net_device *netdev,
			struct flow_cls_offload *flower)
{
	if (!eth_proto_is_802_3(flower->common.protocol))
		return -EOPNOTSUPP;

	switch (flower->command) {
	case FLOW_CLS_REPLACE:
		return nfp_flower_add_offload(app, netdev, flower);
	case FLOW_CLS_DESTROY:
		return nfp_flower_del_offload(app, netdev, flower);
	case FLOW_CLS_STATS:
		return nfp_flower_get_stats(app, netdev, flower);
	default:
		return -EOPNOTSUPP;
	}
}

static int nfp_flower_setup_tc_block_cb(enum tc_setup_type type,
					void *type_data, void *cb_priv)
{
	struct nfp_repr *repr = cb_priv;

	if (!tc_cls_can_offload_and_chain0(repr->netdev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return nfp_flower_repr_offload(repr->app, repr->netdev,
					       type_data);
	case TC_SETUP_CLSMATCHALL:
		return nfp_flower_setup_qos_offload(repr->app, repr->netdev,
						    type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(nfp_block_cb_list);

static int nfp_flower_setup_tc_block(struct net_device *netdev,
				     struct flow_block_offload *f)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	struct nfp_flower_repr_priv *repr_priv;
	struct flow_block_cb *block_cb;

	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	repr_priv = repr->app_priv;
	repr_priv->block_shared = f->block_shared;
	f->driver_block_list = &nfp_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		if (flow_block_cb_is_busy(nfp_flower_setup_tc_block_cb, repr,
					  &nfp_block_cb_list))
			return -EBUSY;

		block_cb = flow_block_cb_alloc(nfp_flower_setup_tc_block_cb,
					       repr, repr, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &nfp_block_cb_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block,
						nfp_flower_setup_tc_block_cb,
						repr);
		if (!block_cb)
			return -ENOENT;

		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

int nfp_flower_setup_tc(struct nfp_app *app, struct net_device *netdev,
			enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return nfp_flower_setup_tc_block(netdev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

struct nfp_flower_indr_block_cb_priv {
	struct net_device *netdev;
	struct nfp_app *app;
	struct list_head list;
};

static struct nfp_flower_indr_block_cb_priv *
nfp_flower_indr_block_cb_priv_lookup(struct nfp_app *app,
				     struct net_device *netdev)
{
	struct nfp_flower_indr_block_cb_priv *cb_priv;
	struct nfp_flower_priv *priv = app->priv;

	/* All callback list access should be protected by RTNL. */
	ASSERT_RTNL();

	list_for_each_entry(cb_priv, &priv->indr_block_cb_priv, list)
		if (cb_priv->netdev == netdev)
			return cb_priv;

	return NULL;
}

static int nfp_flower_setup_indr_block_cb(enum tc_setup_type type,
					  void *type_data, void *cb_priv)
{
	struct nfp_flower_indr_block_cb_priv *priv = cb_priv;
	struct flow_cls_offload *flower = type_data;

	if (flower->common.chain_index)
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return nfp_flower_repr_offload(priv->app, priv->netdev,
					       type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static void nfp_flower_setup_indr_tc_release(void *cb_priv)
{
	struct nfp_flower_indr_block_cb_priv *priv = cb_priv;

	list_del(&priv->list);
	kfree(priv);
}

static int
nfp_flower_setup_indr_tc_block(struct net_device *netdev, struct nfp_app *app,
			       struct flow_block_offload *f)
{
	struct nfp_flower_indr_block_cb_priv *cb_priv;
	struct nfp_flower_priv *priv = app->priv;
	struct flow_block_cb *block_cb;

	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS &&
	    !(f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS &&
	      nfp_flower_internal_port_can_offload(app, netdev)))
		return -EOPNOTSUPP;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		cb_priv = kmalloc(sizeof(*cb_priv), GFP_KERNEL);
		if (!cb_priv)
			return -ENOMEM;

		cb_priv->netdev = netdev;
		cb_priv->app = app;
		list_add(&cb_priv->list, &priv->indr_block_cb_priv);

		block_cb = flow_block_cb_alloc(nfp_flower_setup_indr_block_cb,
					       cb_priv, cb_priv,
					       nfp_flower_setup_indr_tc_release);
		if (IS_ERR(block_cb)) {
			list_del(&cb_priv->list);
			kfree(cb_priv);
			return PTR_ERR(block_cb);
		}

		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &nfp_block_cb_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		cb_priv = nfp_flower_indr_block_cb_priv_lookup(app, netdev);
		if (!cb_priv)
			return -ENOENT;

		block_cb = flow_block_cb_lookup(f->block,
						nfp_flower_setup_indr_block_cb,
						cb_priv);
		if (!block_cb)
			return -ENOENT;

		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int
nfp_flower_indr_setup_tc_cb(struct net_device *netdev, void *cb_priv,
			    enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return nfp_flower_setup_indr_tc_block(netdev, cb_priv,
						      type_data);
	default:
		return -EOPNOTSUPP;
	}
}

int nfp_flower_reg_indir_block_handler(struct nfp_app *app,
				       struct net_device *netdev,
				       unsigned long event)
{
	int err;

	if (!nfp_fl_is_netdev_to_offload(netdev))
		return NOTIFY_OK;

	if (event == NETDEV_REGISTER) {
		err = __tc_indr_block_cb_register(netdev, app,
						  nfp_flower_indr_setup_tc_cb,
						  app);
		if (err)
			nfp_flower_cmsg_warn(app,
					     "Indirect block reg failed - %s\n",
					     netdev->name);
	} else if (event == NETDEV_UNREGISTER) {
		__tc_indr_block_cb_unregister(netdev,
					      nfp_flower_indr_setup_tc_cb, app);
	}

	return NOTIFY_OK;
}
