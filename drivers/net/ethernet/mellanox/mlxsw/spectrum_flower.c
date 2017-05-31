/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_flower.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <net/flow_dissector.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>

#include "spectrum.h"
#include "core_acl_flex_keys.h"

static int mlxsw_sp_flower_parse_actions(struct mlxsw_sp *mlxsw_sp,
					 struct net_device *dev,
					 struct mlxsw_sp_acl_rule_info *rulei,
					 struct tcf_exts *exts)
{
	const struct tc_action *a;
	LIST_HEAD(actions);
	int err;

	if (tc_no_actions(exts))
		return 0;

	/* Count action is inserted first */
	err = mlxsw_sp_acl_rulei_act_count(mlxsw_sp, rulei);
	if (err)
		return err;

	tcf_exts_to_list(exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (is_tcf_gact_shot(a)) {
			err = mlxsw_sp_acl_rulei_act_drop(rulei);
			if (err)
				return err;
		} else if (is_tcf_mirred_egress_redirect(a)) {
			int ifindex = tcf_mirred_ifindex(a);
			struct net_device *out_dev;
			struct mlxsw_sp_fid *fid;
			u16 fid_index;

			fid = mlxsw_sp_acl_dummy_fid(mlxsw_sp);
			fid_index = mlxsw_sp_fid_index(fid);
			err = mlxsw_sp_acl_rulei_act_fid_set(mlxsw_sp, rulei,
							     fid_index);
			if (err)
				return err;

			out_dev = __dev_get_by_index(dev_net(dev), ifindex);
			if (out_dev == dev)
				out_dev = NULL;

			err = mlxsw_sp_acl_rulei_act_fwd(mlxsw_sp, rulei,
							 out_dev);
			if (err)
				return err;
		} else if (is_tcf_vlan(a)) {
			u16 proto = be16_to_cpu(tcf_vlan_push_proto(a));
			u32 action = tcf_vlan_action(a);
			u8 prio = tcf_vlan_push_prio(a);
			u16 vid = tcf_vlan_push_vid(a);

			return mlxsw_sp_acl_rulei_act_vlan(mlxsw_sp, rulei,
							   action, vid,
							   proto, prio);
		} else {
			dev_err(mlxsw_sp->bus_info->dev, "Unsupported action\n");
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

static void mlxsw_sp_flower_parse_ipv4(struct mlxsw_sp_acl_rule_info *rulei,
				       struct tc_cls_flower_offload *f)
{
	struct flow_dissector_key_ipv4_addrs *key =
		skb_flow_dissector_target(f->dissector,
					  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
					  f->key);
	struct flow_dissector_key_ipv4_addrs *mask =
		skb_flow_dissector_target(f->dissector,
					  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
					  f->mask);

	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_SRC_IP4,
				       ntohl(key->src), ntohl(mask->src));
	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_DST_IP4,
				       ntohl(key->dst), ntohl(mask->dst));
}

static void mlxsw_sp_flower_parse_ipv6(struct mlxsw_sp_acl_rule_info *rulei,
				       struct tc_cls_flower_offload *f)
{
	struct flow_dissector_key_ipv6_addrs *key =
		skb_flow_dissector_target(f->dissector,
					  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
					  f->key);
	struct flow_dissector_key_ipv6_addrs *mask =
		skb_flow_dissector_target(f->dissector,
					  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
					  f->mask);
	size_t addr_half_size = sizeof(key->src) / 2;

	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP6_HI,
				       &key->src.s6_addr[0],
				       &mask->src.s6_addr[0],
				       addr_half_size);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP6_LO,
				       &key->src.s6_addr[addr_half_size],
				       &mask->src.s6_addr[addr_half_size],
				       addr_half_size);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP6_HI,
				       &key->dst.s6_addr[0],
				       &mask->dst.s6_addr[0],
				       addr_half_size);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP6_LO,
				       &key->dst.s6_addr[addr_half_size],
				       &mask->dst.s6_addr[addr_half_size],
				       addr_half_size);
}

static int mlxsw_sp_flower_parse_ports(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_rule_info *rulei,
				       struct tc_cls_flower_offload *f,
				       u8 ip_proto)
{
	struct flow_dissector_key_ports *key, *mask;

	if (!dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_PORTS))
		return 0;

	if (ip_proto != IPPROTO_TCP && ip_proto != IPPROTO_UDP) {
		dev_err(mlxsw_sp->bus_info->dev, "Only UDP and TCP keys are supported\n");
		return -EINVAL;
	}

	key = skb_flow_dissector_target(f->dissector,
					FLOW_DISSECTOR_KEY_PORTS,
					f->key);
	mask = skb_flow_dissector_target(f->dissector,
					 FLOW_DISSECTOR_KEY_PORTS,
					 f->mask);
	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_DST_L4_PORT,
				       ntohs(key->dst), ntohs(mask->dst));
	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_SRC_L4_PORT,
				       ntohs(key->src), ntohs(mask->src));
	return 0;
}

static int mlxsw_sp_flower_parse_tcp(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_rule_info *rulei,
				     struct tc_cls_flower_offload *f,
				     u8 ip_proto)
{
	struct flow_dissector_key_tcp *key, *mask;

	if (!dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_TCP))
		return 0;

	if (ip_proto != IPPROTO_TCP) {
		dev_err(mlxsw_sp->bus_info->dev, "TCP keys supported only for TCP\n");
		return -EINVAL;
	}

	key = skb_flow_dissector_target(f->dissector,
					FLOW_DISSECTOR_KEY_TCP,
					f->key);
	mask = skb_flow_dissector_target(f->dissector,
					 FLOW_DISSECTOR_KEY_TCP,
					 f->mask);
	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_TCP_FLAGS,
				       ntohs(key->flags), ntohs(mask->flags));
	return 0;
}

static int mlxsw_sp_flower_parse(struct mlxsw_sp *mlxsw_sp,
				 struct net_device *dev,
				 struct mlxsw_sp_acl_rule_info *rulei,
				 struct tc_cls_flower_offload *f)
{
	u16 addr_type = 0;
	u8 ip_proto = 0;
	int err;

	if (f->dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_TCP) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN))) {
		dev_err(mlxsw_sp->bus_info->dev, "Unsupported key\n");
		return -EOPNOTSUPP;
	}

	mlxsw_sp_acl_rulei_priority(rulei, f->prio);

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_CONTROL,
						  f->key);
		addr_type = key->addr_type;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->key);
		struct flow_dissector_key_basic *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->mask);
		u16 n_proto_key = ntohs(key->n_proto);
		u16 n_proto_mask = ntohs(mask->n_proto);

		if (n_proto_key == ETH_P_ALL) {
			n_proto_key = 0;
			n_proto_mask = 0;
		}
		mlxsw_sp_acl_rulei_keymask_u32(rulei,
					       MLXSW_AFK_ELEMENT_ETHERTYPE,
					       n_proto_key, n_proto_mask);

		ip_proto = key->ip_proto;
		mlxsw_sp_acl_rulei_keymask_u32(rulei,
					       MLXSW_AFK_ELEMENT_IP_PROTO,
					       key->ip_proto, mask->ip_proto);
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_dissector_key_eth_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ETH_ADDRS,
						  f->key);
		struct flow_dissector_key_eth_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ETH_ADDRS,
						  f->mask);

		mlxsw_sp_acl_rulei_keymask_buf(rulei,
					       MLXSW_AFK_ELEMENT_DMAC,
					       key->dst, mask->dst,
					       sizeof(key->dst));
		mlxsw_sp_acl_rulei_keymask_buf(rulei,
					       MLXSW_AFK_ELEMENT_SMAC,
					       key->src, mask->src,
					       sizeof(key->src));
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_dissector_key_vlan *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLAN,
						  f->key);
		struct flow_dissector_key_vlan *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLAN,
						  f->mask);
		if (mask->vlan_id != 0)
			mlxsw_sp_acl_rulei_keymask_u32(rulei,
						       MLXSW_AFK_ELEMENT_VID,
						       key->vlan_id,
						       mask->vlan_id);
		if (mask->vlan_priority != 0)
			mlxsw_sp_acl_rulei_keymask_u32(rulei,
						       MLXSW_AFK_ELEMENT_PCP,
						       key->vlan_priority,
						       mask->vlan_priority);
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS)
		mlxsw_sp_flower_parse_ipv4(rulei, f);

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS)
		mlxsw_sp_flower_parse_ipv6(rulei, f);

	err = mlxsw_sp_flower_parse_ports(mlxsw_sp, rulei, f, ip_proto);
	if (err)
		return err;
	err = mlxsw_sp_flower_parse_tcp(mlxsw_sp, rulei, f, ip_proto);
	if (err)
		return err;

	return mlxsw_sp_flower_parse_actions(mlxsw_sp, dev, rulei, f->exts);
}

int mlxsw_sp_flower_replace(struct mlxsw_sp_port *mlxsw_sp_port, bool ingress,
			    __be16 protocol, struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *dev = mlxsw_sp_port->dev;
	struct mlxsw_sp_acl_rule_info *rulei;
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;
	int err;

	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, dev, ingress,
					   MLXSW_SP_ACL_PROFILE_FLOWER);
	if (IS_ERR(ruleset))
		return PTR_ERR(ruleset);

	rule = mlxsw_sp_acl_rule_create(mlxsw_sp, ruleset, f->cookie);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto err_rule_create;
	}

	rulei = mlxsw_sp_acl_rule_rulei(rule);
	err = mlxsw_sp_flower_parse(mlxsw_sp, dev, rulei, f);
	if (err)
		goto err_flower_parse;

	err = mlxsw_sp_acl_rulei_commit(rulei);
	if (err)
		goto err_rulei_commit;

	err = mlxsw_sp_acl_rule_add(mlxsw_sp, rule);
	if (err)
		goto err_rule_add;

	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
	return 0;

err_rule_add:
err_rulei_commit:
err_flower_parse:
	mlxsw_sp_acl_rule_destroy(mlxsw_sp, rule);
err_rule_create:
	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
	return err;
}

void mlxsw_sp_flower_destroy(struct mlxsw_sp_port *mlxsw_sp_port, bool ingress,
			     struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;

	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, mlxsw_sp_port->dev,
					   ingress,
					   MLXSW_SP_ACL_PROFILE_FLOWER);
	if (IS_ERR(ruleset))
		return;

	rule = mlxsw_sp_acl_rule_lookup(mlxsw_sp, ruleset, f->cookie);
	if (rule) {
		mlxsw_sp_acl_rule_del(mlxsw_sp, rule);
		mlxsw_sp_acl_rule_destroy(mlxsw_sp, rule);
	}

	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
}

int mlxsw_sp_flower_stats(struct mlxsw_sp_port *mlxsw_sp_port, bool ingress,
			  struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;
	u64 packets;
	u64 lastuse;
	u64 bytes;
	int err;

	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, mlxsw_sp_port->dev,
					   ingress,
					   MLXSW_SP_ACL_PROFILE_FLOWER);
	if (WARN_ON(IS_ERR(ruleset)))
		return -EINVAL;

	rule = mlxsw_sp_acl_rule_lookup(mlxsw_sp, ruleset, f->cookie);
	if (!rule)
		return -EINVAL;

	err = mlxsw_sp_acl_rule_get_stats(mlxsw_sp, rule, &packets, &bytes,
					  &lastuse);
	if (err)
		goto err_rule_get_stats;

	tcf_exts_stats_update(f->exts, bytes, packets, lastuse);

	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
	return 0;

err_rule_get_stats:
	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
	return err;
}
