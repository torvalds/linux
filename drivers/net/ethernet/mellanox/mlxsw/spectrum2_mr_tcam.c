// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>

#include "core_acl_flex_actions.h"
#include "spectrum.h"
#include "spectrum_mr.h"

struct mlxsw_sp2_mr_tcam {
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_flow_block *flow_block;
	struct mlxsw_sp_acl_ruleset *ruleset4;
	struct mlxsw_sp_acl_ruleset *ruleset6;
};

struct mlxsw_sp2_mr_route {
	struct mlxsw_sp2_mr_tcam *mr_tcam;
};

static struct mlxsw_sp_acl_ruleset *
mlxsw_sp2_mr_tcam_proto_ruleset(struct mlxsw_sp2_mr_tcam *mr_tcam,
				enum mlxsw_sp_l3proto proto)
{
	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return mr_tcam->ruleset4;
	case MLXSW_SP_L3_PROTO_IPV6:
		return mr_tcam->ruleset6;
	}
	return NULL;
}

static int mlxsw_sp2_mr_tcam_bind_group(struct mlxsw_sp *mlxsw_sp,
					enum mlxsw_reg_pemrbt_protocol protocol,
					struct mlxsw_sp_acl_ruleset *ruleset)
{
	char pemrbt_pl[MLXSW_REG_PEMRBT_LEN];
	u16 group_id;

	group_id = mlxsw_sp_acl_ruleset_group_id(ruleset);

	mlxsw_reg_pemrbt_pack(pemrbt_pl, protocol, group_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pemrbt), pemrbt_pl);
}

static const enum mlxsw_afk_element mlxsw_sp2_mr_tcam_usage_ipv4[] = {
		MLXSW_AFK_ELEMENT_VIRT_ROUTER,
		MLXSW_AFK_ELEMENT_SRC_IP_0_31,
		MLXSW_AFK_ELEMENT_DST_IP_0_31,
};

static int mlxsw_sp2_mr_tcam_ipv4_init(struct mlxsw_sp2_mr_tcam *mr_tcam)
{
	struct mlxsw_afk_element_usage elusage;
	int err;

	/* Initialize IPv4 ACL group. */
	mlxsw_afk_element_usage_fill(&elusage,
				     mlxsw_sp2_mr_tcam_usage_ipv4,
				     ARRAY_SIZE(mlxsw_sp2_mr_tcam_usage_ipv4));
	mr_tcam->ruleset4 = mlxsw_sp_acl_ruleset_get(mr_tcam->mlxsw_sp,
						     mr_tcam->flow_block,
						     MLXSW_SP_L3_PROTO_IPV4,
						     MLXSW_SP_ACL_PROFILE_MR,
						     &elusage);

	if (IS_ERR(mr_tcam->ruleset4))
		return PTR_ERR(mr_tcam->ruleset4);

	/* MC Router groups should be bound before routes are inserted. */
	err = mlxsw_sp2_mr_tcam_bind_group(mr_tcam->mlxsw_sp,
					   MLXSW_REG_PEMRBT_PROTO_IPV4,
					   mr_tcam->ruleset4);
	if (err)
		goto err_bind_group;

	return 0;

err_bind_group:
	mlxsw_sp_acl_ruleset_put(mr_tcam->mlxsw_sp, mr_tcam->ruleset4);
	return err;
}

static void mlxsw_sp2_mr_tcam_ipv4_fini(struct mlxsw_sp2_mr_tcam *mr_tcam)
{
	mlxsw_sp_acl_ruleset_put(mr_tcam->mlxsw_sp, mr_tcam->ruleset4);
}

static const enum mlxsw_afk_element mlxsw_sp2_mr_tcam_usage_ipv6[] = {
		MLXSW_AFK_ELEMENT_VIRT_ROUTER,
		MLXSW_AFK_ELEMENT_SRC_IP_96_127,
		MLXSW_AFK_ELEMENT_SRC_IP_64_95,
		MLXSW_AFK_ELEMENT_SRC_IP_32_63,
		MLXSW_AFK_ELEMENT_SRC_IP_0_31,
		MLXSW_AFK_ELEMENT_DST_IP_96_127,
		MLXSW_AFK_ELEMENT_DST_IP_64_95,
		MLXSW_AFK_ELEMENT_DST_IP_32_63,
		MLXSW_AFK_ELEMENT_DST_IP_0_31,
};

static int mlxsw_sp2_mr_tcam_ipv6_init(struct mlxsw_sp2_mr_tcam *mr_tcam)
{
	struct mlxsw_afk_element_usage elusage;
	int err;

	/* Initialize IPv6 ACL group */
	mlxsw_afk_element_usage_fill(&elusage,
				     mlxsw_sp2_mr_tcam_usage_ipv6,
				     ARRAY_SIZE(mlxsw_sp2_mr_tcam_usage_ipv6));
	mr_tcam->ruleset6 = mlxsw_sp_acl_ruleset_get(mr_tcam->mlxsw_sp,
						     mr_tcam->flow_block,
						     MLXSW_SP_L3_PROTO_IPV6,
						     MLXSW_SP_ACL_PROFILE_MR,
						     &elusage);

	if (IS_ERR(mr_tcam->ruleset6))
		return PTR_ERR(mr_tcam->ruleset6);

	/* MC Router groups should be bound before routes are inserted. */
	err = mlxsw_sp2_mr_tcam_bind_group(mr_tcam->mlxsw_sp,
					   MLXSW_REG_PEMRBT_PROTO_IPV6,
					   mr_tcam->ruleset6);
	if (err)
		goto err_bind_group;

	return 0;

err_bind_group:
	mlxsw_sp_acl_ruleset_put(mr_tcam->mlxsw_sp, mr_tcam->ruleset6);
	return err;
}

static void mlxsw_sp2_mr_tcam_ipv6_fini(struct mlxsw_sp2_mr_tcam *mr_tcam)
{
	mlxsw_sp_acl_ruleset_put(mr_tcam->mlxsw_sp, mr_tcam->ruleset6);
}

static void
mlxsw_sp2_mr_tcam_rule_parse4(struct mlxsw_sp_acl_rule_info *rulei,
			      struct mlxsw_sp_mr_route_key *key)
{
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_0_31,
				       (char *) &key->source.addr4,
				       (char *) &key->source_mask.addr4, 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_0_31,
				       (char *) &key->group.addr4,
				       (char *) &key->group_mask.addr4, 4);
}

static void
mlxsw_sp2_mr_tcam_rule_parse6(struct mlxsw_sp_acl_rule_info *rulei,
			      struct mlxsw_sp_mr_route_key *key)
{
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_96_127,
				       &key->source.addr6.s6_addr[0x0],
				       &key->source_mask.addr6.s6_addr[0x0], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_64_95,
				       &key->source.addr6.s6_addr[0x4],
				       &key->source_mask.addr6.s6_addr[0x4], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_32_63,
				       &key->source.addr6.s6_addr[0x8],
				       &key->source_mask.addr6.s6_addr[0x8], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_SRC_IP_0_31,
				       &key->source.addr6.s6_addr[0xc],
				       &key->source_mask.addr6.s6_addr[0xc], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_96_127,
				       &key->group.addr6.s6_addr[0x0],
				       &key->group_mask.addr6.s6_addr[0x0], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_64_95,
				       &key->group.addr6.s6_addr[0x4],
				       &key->group_mask.addr6.s6_addr[0x4], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_32_63,
				       &key->group.addr6.s6_addr[0x8],
				       &key->group_mask.addr6.s6_addr[0x8], 4);
	mlxsw_sp_acl_rulei_keymask_buf(rulei, MLXSW_AFK_ELEMENT_DST_IP_0_31,
				       &key->group.addr6.s6_addr[0xc],
				       &key->group_mask.addr6.s6_addr[0xc], 4);
}

static void
mlxsw_sp2_mr_tcam_rule_parse(struct mlxsw_sp_acl_rule *rule,
			     struct mlxsw_sp_mr_route_key *key,
			     unsigned int priority)
{
	struct mlxsw_sp_acl_rule_info *rulei;

	rulei = mlxsw_sp_acl_rule_rulei(rule);
	rulei->priority = priority;
	mlxsw_sp_acl_rulei_keymask_u32(rulei, MLXSW_AFK_ELEMENT_VIRT_ROUTER,
				       key->vrid, GENMASK(11, 0));
	switch (key->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return mlxsw_sp2_mr_tcam_rule_parse4(rulei, key);
	case MLXSW_SP_L3_PROTO_IPV6:
		return mlxsw_sp2_mr_tcam_rule_parse6(rulei, key);
	}
}

static int
mlxsw_sp2_mr_tcam_route_create(struct mlxsw_sp *mlxsw_sp, void *priv,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block,
			       enum mlxsw_sp_mr_route_prio prio)
{
	struct mlxsw_sp2_mr_route *mr_route = route_priv;
	struct mlxsw_sp2_mr_tcam *mr_tcam = priv;
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;
	int err;

	mr_route->mr_tcam = mr_tcam;
	ruleset = mlxsw_sp2_mr_tcam_proto_ruleset(mr_tcam, key->proto);
	if (WARN_ON(!ruleset))
		return -EINVAL;

	rule = mlxsw_sp_acl_rule_create(mlxsw_sp, ruleset,
					(unsigned long) route_priv, afa_block,
					NULL);
	if (IS_ERR(rule))
		return PTR_ERR(rule);

	mlxsw_sp2_mr_tcam_rule_parse(rule, key, prio);
	err = mlxsw_sp_acl_rule_add(mlxsw_sp, rule);
	if (err)
		goto err_rule_add;

	return 0;

err_rule_add:
	mlxsw_sp_acl_rule_destroy(mlxsw_sp, rule);
	return err;
}

static void
mlxsw_sp2_mr_tcam_route_destroy(struct mlxsw_sp *mlxsw_sp, void *priv,
				void *route_priv,
				struct mlxsw_sp_mr_route_key *key)
{
	struct mlxsw_sp2_mr_tcam *mr_tcam = priv;
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;

	ruleset = mlxsw_sp2_mr_tcam_proto_ruleset(mr_tcam, key->proto);
	if (WARN_ON(!ruleset))
		return;

	rule = mlxsw_sp_acl_rule_lookup(mlxsw_sp, ruleset,
					(unsigned long) route_priv);
	if (WARN_ON(!rule))
		return;

	mlxsw_sp_acl_rule_del(mlxsw_sp, rule);
	mlxsw_sp_acl_rule_destroy(mlxsw_sp, rule);
}

static int
mlxsw_sp2_mr_tcam_route_update(struct mlxsw_sp *mlxsw_sp,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block)
{
	struct mlxsw_sp2_mr_route *mr_route = route_priv;
	struct mlxsw_sp2_mr_tcam *mr_tcam = mr_route->mr_tcam;
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule *rule;

	ruleset = mlxsw_sp2_mr_tcam_proto_ruleset(mr_tcam, key->proto);
	if (WARN_ON(!ruleset))
		return -EINVAL;

	rule = mlxsw_sp_acl_rule_lookup(mlxsw_sp, ruleset,
					(unsigned long) route_priv);
	if (WARN_ON(!rule))
		return -EINVAL;

	return mlxsw_sp_acl_rule_action_replace(mlxsw_sp, rule, afa_block);
}

static int mlxsw_sp2_mr_tcam_init(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	struct mlxsw_sp2_mr_tcam *mr_tcam = priv;
	int err;

	mr_tcam->mlxsw_sp = mlxsw_sp;
	mr_tcam->flow_block = mlxsw_sp_flow_block_create(mlxsw_sp, NULL);
	if (!mr_tcam->flow_block)
		return -ENOMEM;

	err = mlxsw_sp2_mr_tcam_ipv4_init(mr_tcam);
	if (err)
		goto err_ipv4_init;

	err = mlxsw_sp2_mr_tcam_ipv6_init(mr_tcam);
	if (err)
		goto err_ipv6_init;

	return 0;

err_ipv6_init:
	mlxsw_sp2_mr_tcam_ipv4_fini(mr_tcam);
err_ipv4_init:
	mlxsw_sp_flow_block_destroy(mr_tcam->flow_block);
	return err;
}

static void mlxsw_sp2_mr_tcam_fini(void *priv)
{
	struct mlxsw_sp2_mr_tcam *mr_tcam = priv;

	mlxsw_sp2_mr_tcam_ipv6_fini(mr_tcam);
	mlxsw_sp2_mr_tcam_ipv4_fini(mr_tcam);
	mlxsw_sp_flow_block_destroy(mr_tcam->flow_block);
}

const struct mlxsw_sp_mr_tcam_ops mlxsw_sp2_mr_tcam_ops = {
	.priv_size = sizeof(struct mlxsw_sp2_mr_tcam),
	.init = mlxsw_sp2_mr_tcam_init,
	.fini = mlxsw_sp2_mr_tcam_fini,
	.route_priv_size = sizeof(struct mlxsw_sp2_mr_route),
	.route_create = mlxsw_sp2_mr_tcam_route_create,
	.route_destroy = mlxsw_sp2_mr_tcam_route_destroy,
	.route_update = mlxsw_sp2_mr_tcam_route_update,
};
