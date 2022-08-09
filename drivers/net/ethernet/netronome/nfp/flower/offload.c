// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/skbuff.h>
#include <net/devlink.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"
#include "conntrack.h"
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
	 BIT(FLOW_DISSECTOR_KEY_CVLAN) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_PORTS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_OPTS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IP) | \
	 BIT(FLOW_DISSECTOR_KEY_MPLS) | \
	 BIT(FLOW_DISSECTOR_KEY_CT) | \
	 BIT(FLOW_DISSECTOR_KEY_META) | \
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

#define NFP_FLOWER_WHITELIST_TUN_DISSECTOR_V6_R \
	(BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS))

#define NFP_FLOWER_MERGE_FIELDS \
	(NFP_FLOWER_LAYER_PORT | \
	 NFP_FLOWER_LAYER_MAC | \
	 NFP_FLOWER_LAYER_TP | \
	 NFP_FLOWER_LAYER_IPV4 | \
	 NFP_FLOWER_LAYER_IPV6)

#define NFP_FLOWER_PRE_TUN_RULE_FIELDS \
	(NFP_FLOWER_LAYER_EXT_META | \
	 NFP_FLOWER_LAYER_PORT | \
	 NFP_FLOWER_LAYER_MAC | \
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

int
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

static bool nfp_flower_check_higher_than_mac(struct flow_rule *rule)
{
	return flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ICMP);
}

static bool nfp_flower_check_higher_than_l3(struct flow_rule *rule)
{
	return flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS) ||
	       flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ICMP);
}

static int
nfp_flower_calc_opt_layer(struct flow_dissector_key_enc_opts *enc_opts,
			  u32 *key_layer_two, int *key_size, bool ipv6,
			  struct netlink_ext_ack *extack)
{
	if (enc_opts->len > NFP_FL_MAX_GENEVE_OPT_KEY ||
	    (ipv6 && enc_opts->len > NFP_FL_MAX_GENEVE_OPT_KEY_V6)) {
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
			      enum nfp_flower_tun_type *tun_type, bool ipv6,
			      struct netlink_ext_ack *extack)
{
	int err;

	switch (enc_ports->dst) {
	case htons(IANA_VXLAN_UDP_PORT):
		*tun_type = NFP_FL_TUNNEL_VXLAN;
		*key_layer |= NFP_FLOWER_LAYER_VXLAN;

		if (ipv6) {
			*key_layer |= NFP_FLOWER_LAYER_EXT_META;
			*key_size += sizeof(struct nfp_flower_ext_meta);
			*key_layer_two |= NFP_FLOWER_LAYER2_TUN_IPV6;
			*key_size += sizeof(struct nfp_flower_ipv6_udp_tun);
		} else {
			*key_size += sizeof(struct nfp_flower_ipv4_udp_tun);
		}

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

		if (ipv6) {
			*key_layer_two |= NFP_FLOWER_LAYER2_TUN_IPV6;
			*key_size += sizeof(struct nfp_flower_ipv6_udp_tun);
		} else {
			*key_size += sizeof(struct nfp_flower_ipv4_udp_tun);
		}

		if (!enc_op)
			break;
		if (!(priv->flower_ext_feats & NFP_FL_FEATS_GENEVE_OPT)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support geneve option offload");
			return -EOPNOTSUPP;
		}
		err = nfp_flower_calc_opt_layer(enc_op, key_layer_two, key_size,
						ipv6, extack);
		if (err)
			return err;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: tunnel type unknown");
		return -EOPNOTSUPP;
	}

	return 0;
}

int
nfp_flower_calculate_key_layers(struct nfp_app *app,
				struct net_device *netdev,
				struct nfp_fl_key_ls *ret_key_ls,
				struct flow_rule *rule,
				enum nfp_flower_tun_type *tun_type,
				struct netlink_ext_ack *extack)
{
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
	    (dissector->used_keys & NFP_FLOWER_WHITELIST_TUN_DISSECTOR_V6_R)
	    != NFP_FLOWER_WHITELIST_TUN_DISSECTOR_V6_R &&
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
		if (priv->flower_ext_feats & NFP_FL_FEATS_VLAN_QINQ &&
		    !(key_layer_two & NFP_FLOWER_LAYER2_QINQ)) {
			key_layer |= NFP_FLOWER_LAYER_EXT_META;
			key_size += sizeof(struct nfp_flower_ext_meta);
			key_size += sizeof(struct nfp_flower_vlan);
			key_layer_two |= NFP_FLOWER_LAYER2_QINQ;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CVLAN)) {
		struct flow_match_vlan cvlan;

		if (!(priv->flower_ext_feats & NFP_FL_FEATS_VLAN_QINQ)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support VLAN QinQ offload");
			return -EOPNOTSUPP;
		}

		flow_rule_match_vlan(rule, &cvlan);
		if (!(key_layer_two & NFP_FLOWER_LAYER2_QINQ)) {
			key_layer |= NFP_FLOWER_LAYER_EXT_META;
			key_size += sizeof(struct nfp_flower_ext_meta);
			key_size += sizeof(struct nfp_flower_vlan);
			key_layer_two |= NFP_FLOWER_LAYER2_QINQ;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_match_enc_opts enc_op = { NULL, NULL };
		struct flow_match_ipv4_addrs ipv4_addrs;
		struct flow_match_ipv6_addrs ipv6_addrs;
		struct flow_match_control enc_ctl;
		struct flow_match_ports enc_ports;
		bool ipv6_tun = false;

		flow_rule_match_enc_control(rule, &enc_ctl);

		if (enc_ctl.mask->addr_type != 0xffff) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: wildcarded protocols on tunnels are not supported");
			return -EOPNOTSUPP;
		}

		ipv6_tun = enc_ctl.key->addr_type ==
				FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		if (ipv6_tun &&
		    !(priv->flower_ext_feats & NFP_FL_FEATS_IPV6_TUN)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: firmware does not support IPv6 tunnels");
			return -EOPNOTSUPP;
		}

		if (!ipv6_tun &&
		    enc_ctl.key->addr_type != FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: tunnel address type not IPv4 or IPv6");
			return -EOPNOTSUPP;
		}

		if (ipv6_tun) {
			flow_rule_match_enc_ipv6_addrs(rule, &ipv6_addrs);
			if (memchr_inv(&ipv6_addrs.mask->dst, 0xff,
				       sizeof(ipv6_addrs.mask->dst))) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: only an exact match IPv6 destination address is supported");
				return -EOPNOTSUPP;
			}
		} else {
			flow_rule_match_enc_ipv4_addrs(rule, &ipv4_addrs);
			if (ipv4_addrs.mask->dst != cpu_to_be32(~0)) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: only an exact match IPv4 destination address is supported");
				return -EOPNOTSUPP;
			}
		}

		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_OPTS))
			flow_rule_match_enc_opts(rule, &enc_op);

		if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
			/* check if GRE, which has no enc_ports */
			if (!netif_is_gretap(netdev) && !netif_is_ip6gretap(netdev)) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: an exact match on L4 destination port is required for non-GRE tunnels");
				return -EOPNOTSUPP;
			}

			*tun_type = NFP_FL_TUNNEL_GRE;
			key_layer |= NFP_FLOWER_LAYER_EXT_META;
			key_size += sizeof(struct nfp_flower_ext_meta);
			key_layer_two |= NFP_FLOWER_LAYER2_GRE;

			if (ipv6_tun) {
				key_layer_two |= NFP_FLOWER_LAYER2_TUN_IPV6;
				key_size +=
					sizeof(struct nfp_flower_ipv6_udp_tun);
			} else {
				key_size +=
					sizeof(struct nfp_flower_ipv4_udp_tun);
			}

			if (enc_op.key) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: encap options not supported on GRE tunnels");
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
							    tun_type, ipv6_tun,
							    extack);
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
	} else if (nfp_flower_check_higher_than_mac(rule)) {
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
	    nfp_flower_check_higher_than_l3(rule)) {
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

struct nfp_fl_payload *
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
	flow_pay->nfp_tun_ipv6 = NULL;
	flow_pay->meta.flags = 0;
	INIT_LIST_HEAD(&flow_pay->linked_flows);
	flow_pay->in_hw = false;
	flow_pay->pre_tun_rule.dev = NULL;

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
	struct nfp_fl_pre_tunnel *pre_tun;
	struct nfp_fl_set_tport *tport;
	struct nfp_fl_set_eth *eth;
	struct nfp_fl_act_head *a;
	unsigned int act_off = 0;
	bool ipv6_tun = false;
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
		case NFP_FL_ACTION_OPCODE_SET_TUNNEL:
			/* New tunnel header means l2 to l4 can be matched. */
			eth_broadcast_addr(&merge->l2.mac_dst[0]);
			eth_broadcast_addr(&merge->l2.mac_src[0]);
			memset(&merge->l4, 0xff,
			       sizeof(struct nfp_flower_tp_ports));
			if (ipv6_tun)
				memset(&merge->ipv6, 0xff,
				       sizeof(struct nfp_flower_ipv6));
			else
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
			pre_tun = (struct nfp_fl_pre_tunnel *)a;
			ipv6_tun = be16_to_cpu(pre_tun->flags) &
					NFP_FL_PRE_TUN_IPV6;
			break;
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
			fallthrough;
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

static int
nfp_fl_verify_post_tun_acts(char *acts, int len, struct nfp_fl_push_vlan **vlan)
{
	struct nfp_fl_act_head *a;
	unsigned int act_off = 0;

	while (act_off < len) {
		a = (struct nfp_fl_act_head *)&acts[act_off];

		if (a->jump_id == NFP_FL_ACTION_OPCODE_PUSH_VLAN && !act_off)
			*vlan = (struct nfp_fl_push_vlan *)a;
		else if (a->jump_id != NFP_FL_ACTION_OPCODE_OUTPUT)
			return -EOPNOTSUPP;

		act_off += a->len_lw << NFP_FL_LW_SIZ;
	}

	/* Ensure any VLAN push also has an egress action. */
	if (*vlan && act_off <= sizeof(struct nfp_fl_push_vlan))
		return -EOPNOTSUPP;

	return 0;
}

static int
nfp_fl_push_vlan_after_tun(char *acts, int len, struct nfp_fl_push_vlan *vlan)
{
	struct nfp_fl_set_tun *tun;
	struct nfp_fl_act_head *a;
	unsigned int act_off = 0;

	while (act_off < len) {
		a = (struct nfp_fl_act_head *)&acts[act_off];

		if (a->jump_id == NFP_FL_ACTION_OPCODE_SET_TUNNEL) {
			tun = (struct nfp_fl_set_tun *)a;
			tun->outer_vlan_tpid = vlan->vlan_tpid;
			tun->outer_vlan_tci = vlan->vlan_tci;

			return 0;
		}

		act_off += a->len_lw << NFP_FL_LW_SIZ;
	}

	/* Return error if no tunnel action is found. */
	return -EOPNOTSUPP;
}

static int
nfp_flower_merge_action(struct nfp_fl_payload *sub_flow1,
			struct nfp_fl_payload *sub_flow2,
			struct nfp_fl_payload *merge_flow)
{
	unsigned int sub1_act_len, sub2_act_len, pre_off1, pre_off2;
	struct nfp_fl_push_vlan *post_tun_push_vlan = NULL;
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
	 * a tunnel, there are restrictions on what sub_flow 2 actions lead to a
	 * valid merge.
	 */
	if (tunnel_act) {
		char *post_tun_acts = &sub_flow2->action_data[pre_off2];

		err = nfp_fl_verify_post_tun_acts(post_tun_acts, sub2_act_len,
						  &post_tun_push_vlan);
		if (err)
			return err;

		if (post_tun_push_vlan) {
			pre_off2 += sizeof(*post_tun_push_vlan);
			sub2_act_len -= sizeof(*post_tun_push_vlan);
		}
	}

	/* Copy remaining actions from sub_flows 1 and 2. */
	memcpy(merge_act, sub_flow1->action_data + pre_off1, sub1_act_len);

	if (post_tun_push_vlan) {
		/* Update tunnel action in merge to include VLAN push. */
		err = nfp_fl_push_vlan_after_tun(merge_act, sub1_act_len,
						 post_tun_push_vlan);
		if (err)
			return err;

		merge_flow->meta.act_len -= sizeof(*post_tun_push_vlan);
	}

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
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *merge_flow;
	struct nfp_fl_key_ls merge_key_ls;
	struct nfp_merge_info *merge_info;
	u64 parent_ctx = 0;
	int err;

	ASSERT_RTNL();

	if (sub_flow1 == sub_flow2 ||
	    nfp_flower_is_merge_flow(sub_flow1) ||
	    nfp_flower_is_merge_flow(sub_flow2))
		return -EINVAL;

	/* check if the two flows are already merged */
	parent_ctx = (u64)(be32_to_cpu(sub_flow1->meta.host_ctx_id)) << 32;
	parent_ctx |= (u64)(be32_to_cpu(sub_flow2->meta.host_ctx_id));
	if (rhashtable_lookup_fast(&priv->merge_table,
				   &parent_ctx, merge_table_params)) {
		nfp_flower_cmsg_warn(app, "The two flows are already merged.\n");
		return 0;
	}

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

	err = nfp_compile_flow_metadata(app, merge_flow->tc_flower_cookie, merge_flow,
					merge_flow->ingress_dev, NULL);
	if (err)
		goto err_unlink_sub_flow2;

	err = rhashtable_insert_fast(&priv->flow_table, &merge_flow->fl_node,
				     nfp_flower_table_params);
	if (err)
		goto err_release_metadata;

	merge_info = kmalloc(sizeof(*merge_info), GFP_KERNEL);
	if (!merge_info) {
		err = -ENOMEM;
		goto err_remove_rhash;
	}
	merge_info->parent_ctx = parent_ctx;
	err = rhashtable_insert_fast(&priv->merge_table, &merge_info->ht_node,
				     merge_table_params);
	if (err)
		goto err_destroy_merge_info;

	err = nfp_flower_xmit_flow(app, merge_flow,
				   NFP_FLOWER_CMSG_TYPE_FLOW_MOD);
	if (err)
		goto err_remove_merge_info;

	merge_flow->in_hw = true;
	sub_flow1->in_hw = false;

	return 0;

err_remove_merge_info:
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->merge_table,
					    &merge_info->ht_node,
					    merge_table_params));
err_destroy_merge_info:
	kfree(merge_info);
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
 * nfp_flower_validate_pre_tun_rule()
 * @app:	Pointer to the APP handle
 * @flow:	Pointer to NFP flow representation of rule
 * @key_ls:	Pointer to NFP key layers structure
 * @extack:	Netlink extended ACK report
 *
 * Verifies the flow as a pre-tunnel rule.
 *
 * Return: negative value on error, 0 if verified.
 */
static int
nfp_flower_validate_pre_tun_rule(struct nfp_app *app,
				 struct nfp_fl_payload *flow,
				 struct nfp_fl_key_ls *key_ls,
				 struct netlink_ext_ack *extack)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_flower_meta_tci *meta_tci;
	struct nfp_flower_mac_mpls *mac;
	u8 *ext = flow->unmasked_data;
	struct nfp_fl_act_head *act;
	u8 *mask = flow->mask_data;
	bool vlan = false;
	int act_offset;
	u8 key_layer;

	meta_tci = (struct nfp_flower_meta_tci *)flow->unmasked_data;
	key_layer = key_ls->key_layer;
	if (!(priv->flower_ext_feats & NFP_FL_FEATS_VLAN_QINQ)) {
		if (meta_tci->tci & cpu_to_be16(NFP_FLOWER_MASK_VLAN_PRESENT)) {
			u16 vlan_tci = be16_to_cpu(meta_tci->tci);

			vlan_tci &= ~NFP_FLOWER_MASK_VLAN_PRESENT;
			flow->pre_tun_rule.vlan_tci = cpu_to_be16(vlan_tci);
			vlan = true;
		} else {
			flow->pre_tun_rule.vlan_tci = cpu_to_be16(0xffff);
		}
	}

	if (key_layer & ~NFP_FLOWER_PRE_TUN_RULE_FIELDS) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: too many match fields");
		return -EOPNOTSUPP;
	} else if (key_ls->key_layer_two & ~NFP_FLOWER_LAYER2_QINQ) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: non-vlan in extended match fields");
		return -EOPNOTSUPP;
	}

	if (!(key_layer & NFP_FLOWER_LAYER_MAC)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: MAC fields match required");
		return -EOPNOTSUPP;
	}

	if (!(key_layer & NFP_FLOWER_LAYER_IPV4) &&
	    !(key_layer & NFP_FLOWER_LAYER_IPV6)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: match on ipv4/ipv6 eth_type must be present");
		return -EOPNOTSUPP;
	}

	/* Skip fields known to exist. */
	mask += sizeof(struct nfp_flower_meta_tci);
	ext += sizeof(struct nfp_flower_meta_tci);
	if (key_ls->key_layer_two) {
		mask += sizeof(struct nfp_flower_ext_meta);
		ext += sizeof(struct nfp_flower_ext_meta);
	}
	mask += sizeof(struct nfp_flower_in_port);
	ext += sizeof(struct nfp_flower_in_port);

	/* Ensure destination MAC address matches pre_tun_dev. */
	mac = (struct nfp_flower_mac_mpls *)ext;
	if (memcmp(&mac->mac_dst[0], flow->pre_tun_rule.dev->dev_addr, 6)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: dest MAC must match output dev MAC");
		return -EOPNOTSUPP;
	}

	/* Ensure destination MAC address is fully matched. */
	mac = (struct nfp_flower_mac_mpls *)mask;
	if (!is_broadcast_ether_addr(&mac->mac_dst[0])) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: dest MAC field must not be masked");
		return -EOPNOTSUPP;
	}

	if (mac->mpls_lse) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: MPLS not supported");
		return -EOPNOTSUPP;
	}

	mask += sizeof(struct nfp_flower_mac_mpls);
	ext += sizeof(struct nfp_flower_mac_mpls);
	if (key_layer & NFP_FLOWER_LAYER_IPV4 ||
	    key_layer & NFP_FLOWER_LAYER_IPV6) {
		/* Flags and proto fields have same offset in IPv4 and IPv6. */
		int ip_flags = offsetof(struct nfp_flower_ipv4, ip_ext.flags);
		int ip_proto = offsetof(struct nfp_flower_ipv4, ip_ext.proto);
		int size;
		int i;

		size = key_layer & NFP_FLOWER_LAYER_IPV4 ?
			sizeof(struct nfp_flower_ipv4) :
			sizeof(struct nfp_flower_ipv6);


		/* Ensure proto and flags are the only IP layer fields. */
		for (i = 0; i < size; i++)
			if (mask[i] && i != ip_flags && i != ip_proto) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: only flags and proto can be matched in ip header");
				return -EOPNOTSUPP;
			}
		ext += size;
		mask += size;
	}

	if ((priv->flower_ext_feats & NFP_FL_FEATS_VLAN_QINQ)) {
		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_QINQ) {
			struct nfp_flower_vlan *vlan_tags;
			u16 vlan_tci;

			vlan_tags = (struct nfp_flower_vlan *)ext;

			vlan_tci = be16_to_cpu(vlan_tags->outer_tci);

			vlan_tci &= ~NFP_FLOWER_MASK_VLAN_PRESENT;
			flow->pre_tun_rule.vlan_tci = cpu_to_be16(vlan_tci);
			vlan = true;
		} else {
			flow->pre_tun_rule.vlan_tci = cpu_to_be16(0xffff);
		}
	}

	/* Action must be a single egress or pop_vlan and egress. */
	act_offset = 0;
	act = (struct nfp_fl_act_head *)&flow->action_data[act_offset];
	if (vlan) {
		if (act->jump_id != NFP_FL_ACTION_OPCODE_POP_VLAN) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: match on VLAN must have VLAN pop as first action");
			return -EOPNOTSUPP;
		}

		act_offset += act->len_lw << NFP_FL_LW_SIZ;
		act = (struct nfp_fl_act_head *)&flow->action_data[act_offset];
	}

	if (act->jump_id != NFP_FL_ACTION_OPCODE_OUTPUT) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: non egress action detected where egress was expected");
		return -EOPNOTSUPP;
	}

	act_offset += act->len_lw << NFP_FL_LW_SIZ;

	/* Ensure there are no more actions after egress. */
	if (act_offset != flow->meta.act_len) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported pre-tunnel rule: egress is not the last action");
		return -EOPNOTSUPP;
	}

	return 0;
}

static bool offload_pre_check(struct flow_cls_offload *flow)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(flow);
	struct flow_dissector *dissector = rule->match.dissector;

	if (dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_CT))
		return false;

	if (flow->common.chain_index)
		return false;

	return true;
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
	struct flow_rule *rule = flow_cls_offload_flow_rule(flow);
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

	if (is_pre_ct_flow(flow))
		return nfp_fl_ct_handle_pre_ct(priv, netdev, flow, extack);

	if (is_post_ct_flow(flow))
		return nfp_fl_ct_handle_post_ct(priv, netdev, flow, extack);

	if (!offload_pre_check(flow))
		return -EOPNOTSUPP;

	key_layer = kmalloc(sizeof(*key_layer), GFP_KERNEL);
	if (!key_layer)
		return -ENOMEM;

	err = nfp_flower_calculate_key_layers(app, netdev, key_layer, rule,
					      &tun_type, extack);
	if (err)
		goto err_free_key_ls;

	flow_pay = nfp_flower_allocate_new(key_layer);
	if (!flow_pay) {
		err = -ENOMEM;
		goto err_free_key_ls;
	}

	err = nfp_flower_compile_flow_match(app, rule, key_layer, netdev,
					    flow_pay, tun_type, extack);
	if (err)
		goto err_destroy_flow;

	err = nfp_flower_compile_action(app, rule, netdev, flow_pay, extack);
	if (err)
		goto err_destroy_flow;

	if (flow_pay->pre_tun_rule.dev) {
		err = nfp_flower_validate_pre_tun_rule(app, flow_pay, key_layer, extack);
		if (err)
			goto err_destroy_flow;
	}

	err = nfp_compile_flow_metadata(app, flow->cookie, flow_pay, netdev, extack);
	if (err)
		goto err_destroy_flow;

	flow_pay->tc_flower_cookie = flow->cookie;
	err = rhashtable_insert_fast(&priv->flow_table, &flow_pay->fl_node,
				     nfp_flower_table_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot insert flow into tables for offloads");
		goto err_release_metadata;
	}

	if (flow_pay->pre_tun_rule.dev)
		err = nfp_flower_xmit_pre_tun_flow(app, flow_pay);
	else
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
	if (flow_pay->nfp_tun_ipv6)
		nfp_tunnel_put_ipv6_off(app, flow_pay->nfp_tun_ipv6);
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
	struct nfp_merge_info *merge_info;
	struct nfp_fl_payload *origin;
	u64 parent_ctx = 0;
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
				 merge_flow.list) {
		u32 ctx_id = be32_to_cpu(link->sub_flow.flow->meta.host_ctx_id);

		parent_ctx = (parent_ctx << 32) | (u64)(ctx_id);
		nfp_flower_unlink_flow(link);
	}

	merge_info = rhashtable_lookup_fast(&priv->merge_table,
					    &parent_ctx,
					    merge_table_params);
	if (merge_info) {
		WARN_ON_ONCE(rhashtable_remove_fast(&priv->merge_table,
						    &merge_info->ht_node,
						    merge_table_params));
		kfree(merge_info);
	}

	kfree(merge_flow->action_data);
	kfree(merge_flow->mask_data);
	kfree(merge_flow->unmasked_data);
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->flow_table,
					    &merge_flow->fl_node,
					    nfp_flower_table_params));
	kfree_rcu(merge_flow, rcu);
}

void
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
	struct nfp_fl_ct_map_entry *ct_map_ent;
	struct netlink_ext_ack *extack = NULL;
	struct nfp_fl_payload *nfp_flow;
	struct nfp_port *port = NULL;
	int err;

	extack = flow->common.extack;
	if (nfp_netdev_is_nfp_repr(netdev))
		port = nfp_port_from_netdev(netdev);

	/* Check ct_map_table */
	ct_map_ent = rhashtable_lookup_fast(&priv->ct_map_table, &flow->cookie,
					    nfp_ct_map_params);
	if (ct_map_ent) {
		err = nfp_fl_ct_del_flow(ct_map_ent);
		return err;
	}

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

	if (nfp_flow->nfp_tun_ipv6)
		nfp_tunnel_put_ipv6_off(app, nfp_flow->nfp_tun_ipv6);

	if (!nfp_flow->in_hw) {
		err = 0;
		goto err_free_merge_flow;
	}

	if (nfp_flow->pre_tun_rule.dev)
		err = nfp_flower_xmit_pre_tun_del_flow(app, nfp_flow);
	else
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
		priv->stats[ctx_id].used = max_t(u64, used,
						 priv->stats[ctx_id].used);
	}
}

void
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
	struct nfp_fl_ct_map_entry *ct_map_ent;
	struct netlink_ext_ack *extack = NULL;
	struct nfp_fl_payload *nfp_flow;
	u32 ctx_id;

	/* Check ct_map table first */
	ct_map_ent = rhashtable_lookup_fast(&priv->ct_map_table, &flow->cookie,
					    nfp_ct_map_params);
	if (ct_map_ent)
		return nfp_fl_ct_stats(flow, ct_map_ent);

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
			  priv->stats[ctx_id].pkts, 0, priv->stats[ctx_id].used,
			  FLOW_ACTION_HW_STATS_DELAYED);

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
	struct flow_cls_common_offload *common = type_data;
	struct nfp_repr *repr = cb_priv;

	if (!tc_can_offload_extack(repr->netdev, common->extack))
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

	list_for_each_entry(cb_priv, &priv->indr_block_cb_priv, list)
		if (cb_priv->netdev == netdev)
			return cb_priv;

	return NULL;
}

static int nfp_flower_setup_indr_block_cb(enum tc_setup_type type,
					  void *type_data, void *cb_priv)
{
	struct nfp_flower_indr_block_cb_priv *priv = cb_priv;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return nfp_flower_repr_offload(priv->app, priv->netdev,
					       type_data);
	default:
		return -EOPNOTSUPP;
	}
}

void nfp_flower_setup_indr_tc_release(void *cb_priv)
{
	struct nfp_flower_indr_block_cb_priv *priv = cb_priv;

	list_del(&priv->list);
	kfree(priv);
}

static int
nfp_flower_setup_indr_tc_block(struct net_device *netdev, struct Qdisc *sch, struct nfp_app *app,
			       struct flow_block_offload *f, void *data,
			       void (*cleanup)(struct flow_block_cb *block_cb))
{
	struct nfp_flower_indr_block_cb_priv *cb_priv;
	struct nfp_flower_priv *priv = app->priv;
	struct flow_block_cb *block_cb;

	if ((f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS &&
	     !nfp_flower_internal_port_can_offload(app, netdev)) ||
	    (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS &&
	     nfp_flower_internal_port_can_offload(app, netdev)))
		return -EOPNOTSUPP;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		cb_priv = nfp_flower_indr_block_cb_priv_lookup(app, netdev);
		if (cb_priv &&
		    flow_block_cb_is_busy(nfp_flower_setup_indr_block_cb,
					  cb_priv,
					  &nfp_block_cb_list))
			return -EBUSY;

		cb_priv = kmalloc(sizeof(*cb_priv), GFP_KERNEL);
		if (!cb_priv)
			return -ENOMEM;

		cb_priv->netdev = netdev;
		cb_priv->app = app;
		list_add(&cb_priv->list, &priv->indr_block_cb_priv);

		block_cb = flow_indr_block_cb_alloc(nfp_flower_setup_indr_block_cb,
						    cb_priv, cb_priv,
						    nfp_flower_setup_indr_tc_release,
						    f, netdev, sch, data, app, cleanup);
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

		flow_indr_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

int
nfp_flower_indr_setup_tc_cb(struct net_device *netdev, struct Qdisc *sch, void *cb_priv,
			    enum tc_setup_type type, void *type_data,
			    void *data,
			    void (*cleanup)(struct flow_block_cb *block_cb))
{
	if (!nfp_fl_is_netdev_to_offload(netdev))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_BLOCK:
		return nfp_flower_setup_indr_tc_block(netdev, sch, cb_priv,
						      type_data, data, cleanup);
	default:
		return -EOPNOTSUPP;
	}
}
