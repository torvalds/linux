// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <net/flow_dissector.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>

#include "spectrum.h"
#include "core_acl_flex_keys.h"

static int mlxsw_sp_flower_parse_actions(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_acl_block *block,
					 struct mlxsw_sp_acl_rule_info *rulei,
					 struct tcf_exts *exts,
					 struct netlink_ext_ack *extack)
{
	const struct tc_action *a;
	int err, i;

	if (!tcf_exts_has_actions(exts))
		return 0;

	/* Count action is inserted first */
	err = mlxsw_sp_acl_rulei_act_count(mlxsw_sp, rulei, extack);
	if (err)
		return err;

	tcf_exts_for_each_action(i, a, exts) {
		if (is_tcf_gact_ok(a)) {
			err = mlxsw_sp_acl_rulei_act_terminate(rulei);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Cannot append terminate action");
				return err;
			}
		} else if (is_tcf_gact_shot(a)) {
			err = mlxsw_sp_acl_rulei_act_drop(rulei);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Cannot append drop action");
				return err;
			}
		} else if (is_tcf_gact_trap(a)) {
			err = mlxsw_sp_acl_rulei_act_trap(rulei);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Cannot append trap action");
				return err;
			}
		} else if (is_tcf_gact_goto_chain(a)) {
			u32 chain_index = tcf_gact_goto_chain_index(a);
			struct mlxsw_sp_acl_ruleset *ruleset;
			u16 group_id;

			ruleset = mlxsw_sp_acl_ruleset_lookup(mlxsw_sp, block,
							      chain_index,
							      MLXSW_SP_ACL_PROFILE_FLOWER);
			if (IS_ERR(ruleset))
				return PTR_ERR(ruleset);

			group_id = mlxsw_sp_acl_ruleset_group_id(ruleset);
			err = mlxsw_sp_acl_rulei_act_jump(rulei, group_id);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Cannot append jump action");
				return err;
			}
		} else if (is_tcf_mirred_egress_redirect(a)) {
			struct net_device *out_dev;
			struct mlxsw_sp_fid *fid;
			u16 fid_index;

			fid = mlxsw_sp_acl_dummy_fid(mlxsw_sp);
			fid_index = mlxsw_sp_fid_index(fid);
			err = mlxsw_sp_acl_rulei_act_fid_set(mlxsw_sp, rulei,
							     fid_index, extack);
			if (err)
				return err;

			out_dev = tcf_mirred_dev(a);
			err = mlxsw_sp_acl_rulei_act_fwd(mlxsw_sp, rulei,
							 out_dev, extack);
			if (err)
				return err;
		} else if (is_tcf_mirred_egress_mirror(a)) {
			struct net_device *out_dev = tcf_mirred_dev(a);

			err = mlxsw_sp_acl_rulei_act_mirror(mlxsw_sp, rulei,
							    block, out_dev,
							    extack);
			if (err)
				return err;
		} else if (is_tcf_vlan(a)) {
			u16 proto = be16_to_cpu(tcf_vlan_push_proto(a));
			u32 action = tcf_vlan_action(a);
			u8 prio = tcf_vlan_push_prio(a);
			u16 vid = tcf_vlan_push_vid(a);

			err = mlxsw_sp_acl_rulei_act_vlan(mlxsw_sp, rulei,
							  action, vid,
							  proto, prio, extack);
			if (err)
				return err;
		} else {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported action");
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

	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_0_31,
				       (char *) &key->src,
				       (char *) &mask->src, 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_0_31,
				       (char *) &key->dst,
				       (char *) &mask->dst, 4);
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

	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_96_127,
				       &key->src.s6_addr[0x0],
				       &mask->src.s6_addr[0x0], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_64_95,
				       &key->src.s6_addr[0x4],
				       &mask->src.s6_addr[0x4], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_32_63,
				       &key->src.s6_addr[0x8],
				       &mask->src.s6_addr[0x8], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_0_31,
				       &key->src.s6_addr[0xC],
				       &mask->src.s6_addr[0xC], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_96_127,
				       &key->dst.s6_addr[0x0],
				       &mask->dst.s6_addr[0x0], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_64_95,
				       &key->dst.s6_addr[0x4],
				       &mask->dst.s6_addr[0x4], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_32_63,
				       &key->dst.s6_addr[0x8],
				       &mask->dst.s6_addr[0x8], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_0_31,
				       &key->dst.s6_addr[0xC],
				       &mask->dst.s6_addr[0xC], 4);
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
		NL_SET_ERR_MSG_MOD(f->common.extack, "Only UDP and TCP keys are supported");
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
		NL_SET_ERR_MSG_MOD(f->common.extack, "TCP keys supported only for TCP");
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

static int mlxsw_sp_flower_parse_ip(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_acl_rule_info *rulei,
				    struct tc_cls_flower_offload *f,
				    u16 n_proto)
{
	struct flow_dissector_key_ip *key, *mask;

	if (!dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_IP))
		return 0;

	if (n_proto != ETH_P_IP && n_proto != ETH_P_IPV6) {
		NL_SET_ERR_MSG_MOD(f->common.extack, "IP keys supported only for IPv4/6");
		dev_err(mlxsw_sp->bus_info->dev, "IP keys supported only for IPv4/6\n");
		return -EINVAL;
	}

	key = skb_flow_dissector_target(f->dissector,
					FLOW_DISSECTOR_KEY_IP,
					f->key);
	mask = skb_flow_dissector_target(f->dissector,
					 FLOW_DISSECTOR_KEY_IP,
					 f->mask);
	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_IP_TTL_,
				       key->ttl, mask->ttl);

	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_IP_ECN,
				       key->tos & 0x3, mask->tos & 0x3);

	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_IP_DSCP,
				       key->tos >> 6, mask->tos >> 6);

	return 0;
}

static int mlxsw_sp_flower_parse(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_block *block,
				 struct mlxsw_sp_acl_rule_info *rulei,
				 struct tc_cls_flower_offload *f)
{
	u16 n_proto_mask = 0;
	u16 n_proto_key = 0;
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
	      BIT(FLOW_DISSECTOR_KEY_IP) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN))) {
		dev_err(mlxsw_sp->bus_info->dev, "Unsupported key\n");
		NL_SET_ERR_MSG_MOD(f->common.extack, "Unsupported key");
		return -EOPNOTSUPP;
	}

	mlxsw_sp_acl_rulei_priority(rulei, f->common.prio);

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
		n_proto_key = ntohs(key->n_proto);
		n_proto_mask = ntohs(mask->n_proto);

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
					       MLXSW_AFK_ELEMENT_DMAC_32_47,
					       key->dst, mask->dst, 2);
		mlxsw_sp_acl_rulei_keymask_buf(rulei,
					       MLXSW_AFK_ELEMENT_DMAC_0_31,
					       key->dst + 2, mask->dst + 2, 4);
		mlxsw_sp_acl_rulei_keymask_buf(rulei,
					       MLXSW_AFK_ELEMENT_SMAC_32_47,
					       key->src, mask->src, 2);
		mlxsw_sp_acl_rulei_keymask_buf(rulei,
					       MLXSW_AFK_ELEMENT_SMAC_0_31,
					       key->src + 2, mask->src + 2, 4);
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

		if (mlxsw_sp_acl_block_is_egress_bound(block)) {
			NL_SET_ERR_MSG_MOD(f->common.extack, "vlan_id key is not supported on egress");
			return -EOPNOTSUPP;
		}
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

	err = mlxsw_sp_flower_parse_ip(mlxsw_sp, rulei, f, n_proto_key & n_proto_mask);
	if (err)
		return err;

	return mlxsw_sp_flower_parse_actions(mlxsw_sp, block, rulei, f->exts,
					     f->common.extack);
}

int mlxsw_sp_flower_replace(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_block *block,
			    struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp_acl_rule_info *rulei;
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;
	int err;

	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, block,
					   f->common.chain_index,
					   MLXSW_SP_ACL_PROFILE_FLOWER, NULL);
	if (IS_ERR(ruleset))
		return PTR_ERR(ruleset);

	rule = mlxsw_sp_acl_rule_create(mlxsw_sp, ruleset, f->cookie,
					f->common.extack);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto err_rule_create;
	}

	rulei = mlxsw_sp_acl_rule_rulei(rule);
	err = mlxsw_sp_flower_parse(mlxsw_sp, block, rulei, f);
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

void mlxsw_sp_flower_destroy(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_block *block,
			     struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;

	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, block,
					   f->common.chain_index,
					   MLXSW_SP_ACL_PROFILE_FLOWER, NULL);
	if (IS_ERR(ruleset))
		return;

	rule = mlxsw_sp_acl_rule_lookup(mlxsw_sp, ruleset, f->cookie);
	if (rule) {
		mlxsw_sp_acl_rule_del(mlxsw_sp, rule);
		mlxsw_sp_acl_rule_destroy(mlxsw_sp, rule);
	}

	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
}

int mlxsw_sp_flower_stats(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_block *block,
			  struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;
	u64 packets;
	u64 lastuse;
	u64 bytes;
	int err;

	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, block,
					   f->common.chain_index,
					   MLXSW_SP_ACL_PROFILE_FLOWER, NULL);
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

int mlxsw_sp_flower_tmplt_create(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_block *block,
				 struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule_info rulei;
	int err;

	memset(&rulei, 0, sizeof(rulei));
	err = mlxsw_sp_flower_parse(mlxsw_sp, block, &rulei, f);
	if (err)
		return err;
	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, block,
					   f->common.chain_index,
					   MLXSW_SP_ACL_PROFILE_FLOWER,
					   &rulei.values.elusage);

	/* keep the reference to the ruleset */
	return PTR_ERR_OR_ZERO(ruleset);
}

void mlxsw_sp_flower_tmplt_destroy(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_block *block,
				   struct tc_cls_flower_offload *f)
{
	struct mlxsw_sp_acl_ruleset *ruleset;

	ruleset = mlxsw_sp_acl_ruleset_get(mlxsw_sp, block,
					   f->common.chain_index,
					   MLXSW_SP_ACL_PROFILE_FLOWER, NULL);
	if (IS_ERR(ruleset))
		return;
	/* put the reference to the ruleset kept in create */
	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
	mlxsw_sp_acl_ruleset_put(mlxsw_sp, ruleset);
}
