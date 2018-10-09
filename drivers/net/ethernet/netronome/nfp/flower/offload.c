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
	 BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) | \
	 BIT(FLOW_DISSECTOR_KEY_ENC_PORTS))

static int
nfp_flower_xmit_flow(struct net_device *netdev,
		     struct nfp_fl_payload *nfp_flow, u8 mtype)
{
	u32 meta_len, key_len, mask_len, act_len, tot_len;
	struct nfp_repr *priv = netdev_priv(netdev);
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

	skb = nfp_flower_cmsg_alloc(priv->app, tot_len, mtype, GFP_KERNEL);
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

	nfp_ctrl_tx(priv->app->ctrl, skb);

	return 0;
}

static bool nfp_flower_check_higher_than_mac(struct tc_cls_flower_offload *f)
{
	return dissector_uses_key(f->dissector,
				  FLOW_DISSECTOR_KEY_IPV4_ADDRS) ||
		dissector_uses_key(f->dissector,
				   FLOW_DISSECTOR_KEY_IPV6_ADDRS) ||
		dissector_uses_key(f->dissector,
				   FLOW_DISSECTOR_KEY_PORTS) ||
		dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ICMP);
}

static int
nfp_flower_calc_opt_layer(struct flow_dissector_key_enc_opts *enc_opts,
			  u32 *key_layer_two, int *key_size)
{
	if (enc_opts->len > NFP_FL_MAX_GENEVE_OPT_KEY)
		return -EOPNOTSUPP;

	if (enc_opts->len > 0) {
		*key_layer_two |= NFP_FLOWER_LAYER2_GENEVE_OP;
		*key_size += sizeof(struct nfp_flower_geneve_options);
	}

	return 0;
}

static int
nfp_flower_calculate_key_layers(struct nfp_app *app,
				struct nfp_fl_key_ls *ret_key_ls,
				struct tc_cls_flower_offload *flow,
				bool egress,
				enum nfp_flower_tun_type *tun_type)
{
	struct flow_dissector_key_basic *mask_basic = NULL;
	struct flow_dissector_key_basic *key_basic = NULL;
	struct nfp_flower_priv *priv = app->priv;
	u32 key_layer_two;
	u8 key_layer;
	int key_size;
	int err;

	if (flow->dissector->used_keys & ~NFP_FLOWER_WHITELIST_DISSECTOR)
		return -EOPNOTSUPP;

	/* If any tun dissector is used then the required set must be used. */
	if (flow->dissector->used_keys & NFP_FLOWER_WHITELIST_TUN_DISSECTOR &&
	    (flow->dissector->used_keys & NFP_FLOWER_WHITELIST_TUN_DISSECTOR_R)
	    != NFP_FLOWER_WHITELIST_TUN_DISSECTOR_R)
		return -EOPNOTSUPP;

	key_layer_two = 0;
	key_layer = NFP_FLOWER_LAYER_PORT;
	key_size = sizeof(struct nfp_flower_meta_tci) +
		   sizeof(struct nfp_flower_in_port);

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS) ||
	    dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_MPLS)) {
		key_layer |= NFP_FLOWER_LAYER_MAC;
		key_size += sizeof(struct nfp_flower_mac_mpls);
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_dissector_key_vlan *flow_vlan;

		flow_vlan = skb_flow_dissector_target(flow->dissector,
						      FLOW_DISSECTOR_KEY_VLAN,
						      flow->mask);
		if (!(priv->flower_ext_feats & NFP_FL_FEATS_VLAN_PCP) &&
		    flow_vlan->vlan_priority)
			return -EOPNOTSUPP;
	}

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_dissector_key_ipv4_addrs *mask_ipv4 = NULL;
		struct flow_dissector_key_ports *mask_enc_ports = NULL;
		struct flow_dissector_key_enc_opts *enc_op = NULL;
		struct flow_dissector_key_ports *enc_ports = NULL;
		struct flow_dissector_key_control *mask_enc_ctl =
			skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_ENC_CONTROL,
						  flow->mask);
		struct flow_dissector_key_control *enc_ctl =
			skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_ENC_CONTROL,
						  flow->key);
		if (!egress)
			return -EOPNOTSUPP;

		if (mask_enc_ctl->addr_type != 0xffff ||
		    enc_ctl->addr_type != FLOW_DISSECTOR_KEY_IPV4_ADDRS)
			return -EOPNOTSUPP;

		/* These fields are already verified as used. */
		mask_ipv4 =
			skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
						  flow->mask);
		if (mask_ipv4->dst != cpu_to_be32(~0))
			return -EOPNOTSUPP;

		mask_enc_ports =
			skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_ENC_PORTS,
						  flow->mask);
		enc_ports =
			skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_ENC_PORTS,
						  flow->key);

		if (mask_enc_ports->dst != cpu_to_be16(~0))
			return -EOPNOTSUPP;

		if (dissector_uses_key(flow->dissector,
				       FLOW_DISSECTOR_KEY_ENC_OPTS)) {
			enc_op = skb_flow_dissector_target(flow->dissector,
							   FLOW_DISSECTOR_KEY_ENC_OPTS,
							   flow->key);
		}

		switch (enc_ports->dst) {
		case htons(NFP_FL_VXLAN_PORT):
			*tun_type = NFP_FL_TUNNEL_VXLAN;
			key_layer |= NFP_FLOWER_LAYER_VXLAN;
			key_size += sizeof(struct nfp_flower_ipv4_udp_tun);

			if (enc_op)
				return -EOPNOTSUPP;
			break;
		case htons(NFP_FL_GENEVE_PORT):
			if (!(priv->flower_ext_feats & NFP_FL_FEATS_GENEVE))
				return -EOPNOTSUPP;
			*tun_type = NFP_FL_TUNNEL_GENEVE;
			key_layer |= NFP_FLOWER_LAYER_EXT_META;
			key_size += sizeof(struct nfp_flower_ext_meta);
			key_layer_two |= NFP_FLOWER_LAYER2_GENEVE;
			key_size += sizeof(struct nfp_flower_ipv4_udp_tun);

			if (!enc_op)
				break;
			if (!(priv->flower_ext_feats & NFP_FL_FEATS_GENEVE_OPT))
				return -EOPNOTSUPP;
			err = nfp_flower_calc_opt_layer(enc_op, &key_layer_two,
							&key_size);
			if (err)
				return err;
			break;
		default:
			return -EOPNOTSUPP;
		}
	} else if (egress) {
		/* Reject non tunnel matches offloaded to egress repr. */
		return -EOPNOTSUPP;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		mask_basic = skb_flow_dissector_target(flow->dissector,
						       FLOW_DISSECTOR_KEY_BASIC,
						       flow->mask);

		key_basic = skb_flow_dissector_target(flow->dissector,
						      FLOW_DISSECTOR_KEY_BASIC,
						      flow->key);
	}

	if (mask_basic && mask_basic->n_proto) {
		/* Ethernet type is present in the key. */
		switch (key_basic->n_proto) {
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
			/* Other ethtype - we need check the masks for the
			 * remainder of the key to ensure we can offload.
			 */
			if (nfp_flower_check_higher_than_mac(flow))
				return -EOPNOTSUPP;
			break;
		}
	}

	if (mask_basic && mask_basic->ip_proto) {
		/* Ethernet type is present in the key. */
		switch (key_basic->ip_proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
		case IPPROTO_SCTP:
		case IPPROTO_ICMP:
		case IPPROTO_ICMPV6:
			key_layer |= NFP_FLOWER_LAYER_TP;
			key_size += sizeof(struct nfp_flower_tp_ports);
			break;
		default:
			/* Other ip proto - we need check the masks for the
			 * remainder of the key to ensure we can offload.
			 */
			return -EOPNOTSUPP;
		}
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_dissector_key_tcp *tcp;
		u32 tcp_flags;

		tcp = skb_flow_dissector_target(flow->dissector,
						FLOW_DISSECTOR_KEY_TCP,
						flow->key);
		tcp_flags = be16_to_cpu(tcp->flags);

		if (tcp_flags & ~NFP_FLOWER_SUPPORTED_TCPFLAGS)
			return -EOPNOTSUPP;

		/* We only support PSH and URG flags when either
		 * FIN, SYN or RST is present as well.
		 */
		if ((tcp_flags & (TCPHDR_PSH | TCPHDR_URG)) &&
		    !(tcp_flags & (TCPHDR_FIN | TCPHDR_SYN | TCPHDR_RST)))
			return -EOPNOTSUPP;

		/* We need to store TCP flags in the IPv4 key space, thus
		 * we need to ensure we include a IPv4 key layer if we have
		 * not done so already.
		 */
		if (!(key_layer & NFP_FLOWER_LAYER_IPV4)) {
			key_layer |= NFP_FLOWER_LAYER_IPV4;
			key_size += sizeof(struct nfp_flower_ipv4);
		}
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key_ctl;

		key_ctl = skb_flow_dissector_target(flow->dissector,
						    FLOW_DISSECTOR_KEY_CONTROL,
						    flow->key);

		if (key_ctl->flags & ~NFP_FLOWER_SUPPORTED_CTLFLAGS)
			return -EOPNOTSUPP;
	}

	ret_key_ls->key_layer = key_layer;
	ret_key_ls->key_layer_two = key_layer_two;
	ret_key_ls->key_size = key_size;

	return 0;
}

static struct nfp_fl_payload *
nfp_flower_allocate_new(struct nfp_fl_key_ls *key_layer, bool egress)
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
	flow_pay->ingress_offload = !egress;

	return flow_pay;

err_free_mask:
	kfree(flow_pay->mask_data);
err_free_unmasked:
	kfree(flow_pay->unmasked_data);
err_free_flow:
	kfree(flow_pay);
	return NULL;
}

/**
 * nfp_flower_add_offload() - Adds a new flow to hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure.
 * @egress:	NFP netdev is the egress.
 *
 * Adds a new flow to the repeated hash structure and action payload.
 *
 * Return: negative value on error, 0 if configured successfully.
 */
static int
nfp_flower_add_offload(struct nfp_app *app, struct net_device *netdev,
		       struct tc_cls_flower_offload *flow, bool egress)
{
	enum nfp_flower_tun_type tun_type = NFP_FL_TUNNEL_NONE;
	struct nfp_port *port = nfp_port_from_netdev(netdev);
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *flow_pay;
	struct nfp_fl_key_ls *key_layer;
	struct net_device *ingr_dev;
	int err;

	ingr_dev = egress ? NULL : netdev;
	flow_pay = nfp_flower_search_fl_table(app, flow->cookie, ingr_dev,
					      NFP_FL_STATS_CTX_DONT_CARE);
	if (flow_pay) {
		/* Ignore as duplicate if it has been added by different cb. */
		if (flow_pay->ingress_offload && egress)
			return 0;
		else
			return -EOPNOTSUPP;
	}

	key_layer = kmalloc(sizeof(*key_layer), GFP_KERNEL);
	if (!key_layer)
		return -ENOMEM;

	err = nfp_flower_calculate_key_layers(app, key_layer, flow, egress,
					      &tun_type);
	if (err)
		goto err_free_key_ls;

	flow_pay = nfp_flower_allocate_new(key_layer, egress);
	if (!flow_pay) {
		err = -ENOMEM;
		goto err_free_key_ls;
	}

	flow_pay->ingress_dev = egress ? NULL : netdev;

	err = nfp_flower_compile_flow_match(flow, key_layer, netdev, flow_pay,
					    tun_type);
	if (err)
		goto err_destroy_flow;

	err = nfp_flower_compile_action(app, flow, netdev, flow_pay);
	if (err)
		goto err_destroy_flow;

	err = nfp_compile_flow_metadata(app, flow, flow_pay,
					flow_pay->ingress_dev);
	if (err)
		goto err_destroy_flow;

	err = nfp_flower_xmit_flow(netdev, flow_pay,
				   NFP_FLOWER_CMSG_TYPE_FLOW_ADD);
	if (err)
		goto err_destroy_flow;

	flow_pay->tc_flower_cookie = flow->cookie;
	err = rhashtable_insert_fast(&priv->flow_table, &flow_pay->fl_node,
				     nfp_flower_table_params);
	if (err)
		goto err_destroy_flow;

	port->tc_offload_cnt++;

	/* Deallocate flow payload when flower rule has been destroyed. */
	kfree(key_layer);

	return 0;

err_destroy_flow:
	kfree(flow_pay->action_data);
	kfree(flow_pay->mask_data);
	kfree(flow_pay->unmasked_data);
	kfree(flow_pay);
err_free_key_ls:
	kfree(key_layer);
	return err;
}

/**
 * nfp_flower_del_offload() - Removes a flow from hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure
 * @egress:	Netdev is the egress dev.
 *
 * Removes a flow from the repeated hash structure and clears the
 * action payload.
 *
 * Return: negative value on error, 0 if removed successfully.
 */
static int
nfp_flower_del_offload(struct nfp_app *app, struct net_device *netdev,
		       struct tc_cls_flower_offload *flow, bool egress)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *nfp_flow;
	struct net_device *ingr_dev;
	int err;

	ingr_dev = egress ? NULL : netdev;
	nfp_flow = nfp_flower_search_fl_table(app, flow->cookie, ingr_dev,
					      NFP_FL_STATS_CTX_DONT_CARE);
	if (!nfp_flow)
		return egress ? 0 : -ENOENT;

	err = nfp_modify_flow_metadata(app, nfp_flow);
	if (err)
		goto err_free_flow;

	if (nfp_flow->nfp_tun_ipv4_addr)
		nfp_tunnel_del_ipv4_off(app, nfp_flow->nfp_tun_ipv4_addr);

	err = nfp_flower_xmit_flow(netdev, nfp_flow,
				   NFP_FLOWER_CMSG_TYPE_FLOW_DEL);
	if (err)
		goto err_free_flow;

err_free_flow:
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

/**
 * nfp_flower_get_stats() - Populates flow stats obtained from hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	Netdev structure.
 * @flow:	TC flower classifier offload structure
 * @egress:	Netdev is the egress dev.
 *
 * Populates a flow statistics structure which which corresponds to a
 * specific flow.
 *
 * Return: negative value on error, 0 if stats populated successfully.
 */
static int
nfp_flower_get_stats(struct nfp_app *app, struct net_device *netdev,
		     struct tc_cls_flower_offload *flow, bool egress)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *nfp_flow;
	struct net_device *ingr_dev;
	u32 ctx_id;

	ingr_dev = egress ? NULL : netdev;
	nfp_flow = nfp_flower_search_fl_table(app, flow->cookie, ingr_dev,
					      NFP_FL_STATS_CTX_DONT_CARE);
	if (!nfp_flow)
		return -EINVAL;

	if (nfp_flow->ingress_offload && egress)
		return 0;

	ctx_id = be32_to_cpu(nfp_flow->meta.host_ctx_id);

	spin_lock_bh(&priv->stats_lock);
	tcf_exts_stats_update(flow->exts, priv->stats[ctx_id].bytes,
			      priv->stats[ctx_id].pkts,
			      priv->stats[ctx_id].used);

	priv->stats[ctx_id].pkts = 0;
	priv->stats[ctx_id].bytes = 0;
	spin_unlock_bh(&priv->stats_lock);

	return 0;
}

static int
nfp_flower_repr_offload(struct nfp_app *app, struct net_device *netdev,
			struct tc_cls_flower_offload *flower, bool egress)
{
	if (!eth_proto_is_802_3(flower->common.protocol))
		return -EOPNOTSUPP;

	switch (flower->command) {
	case TC_CLSFLOWER_REPLACE:
		return nfp_flower_add_offload(app, netdev, flower, egress);
	case TC_CLSFLOWER_DESTROY:
		return nfp_flower_del_offload(app, netdev, flower, egress);
	case TC_CLSFLOWER_STATS:
		return nfp_flower_get_stats(app, netdev, flower, egress);
	default:
		return -EOPNOTSUPP;
	}
}

int nfp_flower_setup_tc_egress_cb(enum tc_setup_type type, void *type_data,
				  void *cb_priv)
{
	struct nfp_repr *repr = cb_priv;

	if (!tc_cls_can_offload_and_chain0(repr->netdev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return nfp_flower_repr_offload(repr->app, repr->netdev,
					       type_data, true);
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
					       type_data, false);
	default:
		return -EOPNOTSUPP;
	}
}

static int nfp_flower_setup_tc_block(struct net_device *netdev,
				     struct tc_block_offload *f)
{
	struct nfp_repr *repr = netdev_priv(netdev);

	if (f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block,
					     nfp_flower_setup_tc_block_cb,
					     repr, repr, f->extack);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block,
					nfp_flower_setup_tc_block_cb,
					repr);
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
