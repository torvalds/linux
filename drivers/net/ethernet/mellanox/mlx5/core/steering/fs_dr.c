// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies */

#include <linux/mlx5/vport.h>
#include "mlx5_core.h"
#include "fs_core.h"
#include "fs_cmd.h"
#include "mlx5dr.h"
#include "fs_dr.h"

static bool mlx5_dr_is_fw_table(u32 flags)
{
	if (flags & MLX5_FLOW_TABLE_TERMINATION)
		return true;

	return false;
}

static int mlx5_cmd_dr_update_root_ft(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_flow_table *ft,
				      u32 underlay_qpn,
				      bool disconnect)
{
	return mlx5_fs_cmd_get_fw_cmds()->update_root_ft(ns, ft, underlay_qpn,
							 disconnect);
}

static int set_miss_action(struct mlx5_flow_root_namespace *ns,
			   struct mlx5_flow_table *ft,
			   struct mlx5_flow_table *next_ft)
{
	struct mlx5dr_action *old_miss_action;
	struct mlx5dr_action *action = NULL;
	struct mlx5dr_table *next_tbl;
	int err;

	next_tbl = next_ft ? next_ft->fs_dr_table.dr_table : NULL;
	if (next_tbl) {
		action = mlx5dr_action_create_dest_table(next_tbl);
		if (!action)
			return -EINVAL;
	}
	old_miss_action = ft->fs_dr_table.miss_action;
	err = mlx5dr_table_set_miss_action(ft->fs_dr_table.dr_table, action);
	if (err && action) {
		err = mlx5dr_action_destroy(action);
		if (err)
			mlx5_core_err(ns->dev,
				      "Failed to destroy action (%d)\n", err);
		action = NULL;
	}
	ft->fs_dr_table.miss_action = action;
	if (old_miss_action) {
		err = mlx5dr_action_destroy(old_miss_action);
		if (err)
			mlx5_core_err(ns->dev, "Failed to destroy action (%d)\n",
				      err);
	}

	return err;
}

static int mlx5_cmd_dr_create_flow_table(struct mlx5_flow_root_namespace *ns,
					 struct mlx5_flow_table *ft,
					 unsigned int size,
					 struct mlx5_flow_table *next_ft)
{
	struct mlx5dr_table *tbl;
	u32 flags;
	int err;

	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->create_flow_table(ns, ft,
								    size,
								    next_ft);
	flags = ft->flags;
	/* turn off encap/decap if not supported for sw-str by fw */
	if (!MLX5_CAP_FLOWTABLE(ns->dev, sw_owner_reformat_supported))
		flags = ft->flags & ~(MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT |
				      MLX5_FLOW_TABLE_TUNNEL_EN_DECAP);

	tbl = mlx5dr_table_create(ns->fs_dr_domain.dr_domain, ft->level, flags);
	if (!tbl) {
		mlx5_core_err(ns->dev, "Failed creating dr flow_table\n");
		return -EINVAL;
	}

	ft->fs_dr_table.dr_table = tbl;
	ft->id = mlx5dr_table_get_id(tbl);

	if (next_ft) {
		err = set_miss_action(ns, ft, next_ft);
		if (err) {
			mlx5dr_table_destroy(tbl);
			ft->fs_dr_table.dr_table = NULL;
			return err;
		}
	}

	ft->max_fte = INT_MAX;

	return 0;
}

static int mlx5_cmd_dr_destroy_flow_table(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft)
{
	struct mlx5dr_action *action = ft->fs_dr_table.miss_action;
	int err;

	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->destroy_flow_table(ns, ft);

	err = mlx5dr_table_destroy(ft->fs_dr_table.dr_table);
	if (err) {
		mlx5_core_err(ns->dev, "Failed to destroy flow_table (%d)\n",
			      err);
		return err;
	}
	if (action) {
		err = mlx5dr_action_destroy(action);
		if (err) {
			mlx5_core_err(ns->dev, "Failed to destroy action(%d)\n",
				      err);
			return err;
		}
	}

	return err;
}

static int mlx5_cmd_dr_modify_flow_table(struct mlx5_flow_root_namespace *ns,
					 struct mlx5_flow_table *ft,
					 struct mlx5_flow_table *next_ft)
{
	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->modify_flow_table(ns, ft, next_ft);

	return set_miss_action(ns, ft, next_ft);
}

static int mlx5_cmd_dr_create_flow_group(struct mlx5_flow_root_namespace *ns,
					 struct mlx5_flow_table *ft,
					 u32 *in,
					 struct mlx5_flow_group *fg)
{
	struct mlx5dr_matcher *matcher;
	u32 priority = MLX5_GET(create_flow_group_in, in,
				start_flow_index);
	u8 match_criteria_enable = MLX5_GET(create_flow_group_in,
					    in,
					    match_criteria_enable);
	struct mlx5dr_match_parameters mask;

	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->create_flow_group(ns, ft, in,
								    fg);

	mask.match_buf = MLX5_ADDR_OF(create_flow_group_in,
				      in, match_criteria);
	mask.match_sz = sizeof(fg->mask.match_criteria);

	matcher = mlx5dr_matcher_create(ft->fs_dr_table.dr_table,
					priority,
					match_criteria_enable,
					&mask);
	if (!matcher) {
		mlx5_core_err(ns->dev, "Failed creating matcher\n");
		return -EINVAL;
	}

	fg->fs_dr_matcher.dr_matcher = matcher;
	return 0;
}

static int mlx5_cmd_dr_destroy_flow_group(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft,
					  struct mlx5_flow_group *fg)
{
	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->destroy_flow_group(ns, ft, fg);

	return mlx5dr_matcher_destroy(fg->fs_dr_matcher.dr_matcher);
}

static struct mlx5dr_action *create_vport_action(struct mlx5dr_domain *domain,
						 struct mlx5_flow_rule *dst)
{
	struct mlx5_flow_destination *dest_attr = &dst->dest_attr;

	return mlx5dr_action_create_dest_vport(domain, dest_attr->vport.num,
					       dest_attr->vport.flags &
					       MLX5_FLOW_DEST_VPORT_VHCA_ID,
					       dest_attr->vport.vhca_id);
}

static struct mlx5dr_action *create_uplink_action(struct mlx5dr_domain *domain,
						  struct mlx5_flow_rule *dst)
{
	struct mlx5_flow_destination *dest_attr = &dst->dest_attr;

	return mlx5dr_action_create_dest_vport(domain, MLX5_VPORT_UPLINK, 1,
					       dest_attr->vport.vhca_id);
}

static struct mlx5dr_action *create_ft_action(struct mlx5dr_domain *domain,
					      struct mlx5_flow_rule *dst)
{
	struct mlx5_flow_table *dest_ft = dst->dest_attr.ft;

	if (mlx5_dr_is_fw_table(dest_ft->flags))
		return mlx5dr_action_create_dest_flow_fw_table(domain, dest_ft);
	return mlx5dr_action_create_dest_table(dest_ft->fs_dr_table.dr_table);
}

static struct mlx5dr_action *create_action_push_vlan(struct mlx5dr_domain *domain,
						     struct mlx5_fs_vlan *vlan)
{
	u16 n_ethtype = vlan->ethtype;
	u8  prio = vlan->prio;
	u16 vid = vlan->vid;
	u32 vlan_hdr;

	vlan_hdr = (u32)n_ethtype << 16 | (u32)(prio) << 12 |  (u32)vid;
	return mlx5dr_action_create_push_vlan(domain, htonl(vlan_hdr));
}

static bool contain_vport_reformat_action(struct mlx5_flow_rule *dst)
{
	return (dst->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_VPORT ||
		dst->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_UPLINK) &&
		dst->dest_attr.vport.flags & MLX5_FLOW_DEST_VPORT_REFORMAT_ID;
}

/* We want to support a rule with 32 destinations, which means we need to
 * account for 32 destinations plus usually a counter plus one more action
 * for a multi-destination flow table.
 */
#define MLX5_FLOW_CONTEXT_ACTION_MAX  34
static int mlx5_cmd_dr_create_fte(struct mlx5_flow_root_namespace *ns,
				  struct mlx5_flow_table *ft,
				  struct mlx5_flow_group *group,
				  struct fs_fte *fte)
{
	struct mlx5dr_domain *domain = ns->fs_dr_domain.dr_domain;
	struct mlx5dr_action_dest *term_actions;
	struct mlx5dr_match_parameters params;
	struct mlx5_core_dev *dev = ns->dev;
	struct mlx5dr_action **fs_dr_actions;
	struct mlx5dr_action *tmp_action;
	struct mlx5dr_action **actions;
	bool delay_encap_set = false;
	struct mlx5dr_rule *rule;
	struct mlx5_flow_rule *dst;
	int fs_dr_num_actions = 0;
	int num_term_actions = 0;
	int num_actions = 0;
	size_t match_sz;
	int err = 0;
	int i;

	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->create_fte(ns, ft, group, fte);

	actions = kcalloc(MLX5_FLOW_CONTEXT_ACTION_MAX, sizeof(*actions),
			  GFP_KERNEL);
	if (!actions) {
		err = -ENOMEM;
		goto out_err;
	}

	fs_dr_actions = kcalloc(MLX5_FLOW_CONTEXT_ACTION_MAX,
				sizeof(*fs_dr_actions), GFP_KERNEL);
	if (!fs_dr_actions) {
		err = -ENOMEM;
		goto free_actions_alloc;
	}

	term_actions = kcalloc(MLX5_FLOW_CONTEXT_ACTION_MAX,
			       sizeof(*term_actions), GFP_KERNEL);
	if (!term_actions) {
		err = -ENOMEM;
		goto free_fs_dr_actions_alloc;
	}

	match_sz = sizeof(fte->val);

	/* Drop reformat action bit if destination vport set with reformat */
	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		list_for_each_entry(dst, &fte->node.children, node.list) {
			if (!contain_vport_reformat_action(dst))
				continue;

			fte->action.action &= ~MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
			break;
		}
	}

	/* The order of the actions are must to be keep, only the following
	 * order is supported by SW steering:
	 * TX: modify header -> push vlan -> encap
	 * RX: decap -> pop vlan -> modify header
	 */
	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_DECAP) {
		enum mlx5dr_action_reformat_type decap_type =
			DR_ACTION_REFORMAT_TYP_TNL_L2_TO_L2;

		tmp_action = mlx5dr_action_create_packet_reformat(domain,
								  decap_type,
								  0, 0, 0,
								  NULL);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		actions[num_actions++] = tmp_action;
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT) {
		bool is_decap = fte->action.pkt_reformat->reformat_type ==
			MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2;

		if (is_decap)
			actions[num_actions++] =
				fte->action.pkt_reformat->action.dr_action;
		else
			delay_encap_set = true;
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP) {
		tmp_action =
			mlx5dr_action_create_pop_vlan();
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		actions[num_actions++] = tmp_action;
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2) {
		tmp_action =
			mlx5dr_action_create_pop_vlan();
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		actions[num_actions++] = tmp_action;
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		actions[num_actions++] =
			fte->action.modify_hdr->action.dr_action;

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH) {
		tmp_action = create_action_push_vlan(domain, &fte->action.vlan[0]);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		actions[num_actions++] = tmp_action;
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2) {
		tmp_action = create_action_push_vlan(domain, &fte->action.vlan[1]);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		actions[num_actions++] = tmp_action;
	}

	if (delay_encap_set)
		actions[num_actions++] =
			fte->action.pkt_reformat->action.dr_action;

	/* The order of the actions below is not important */

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_DROP) {
		tmp_action = mlx5dr_action_create_drop();
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		term_actions[num_term_actions++].dest = tmp_action;
	}

	if (fte->flow_context.flow_tag) {
		tmp_action =
			mlx5dr_action_create_tag(fte->flow_context.flow_tag);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		actions[num_actions++] = tmp_action;
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		list_for_each_entry(dst, &fte->node.children, node.list) {
			enum mlx5_flow_destination_type type = dst->dest_attr.type;
			u32 id;

			if (fs_dr_num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX ||
			    num_term_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
				err = -EOPNOTSUPP;
				goto free_actions;
			}

			if (type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			switch (type) {
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
				tmp_action = create_ft_action(domain, dst);
				if (!tmp_action) {
					err = -ENOMEM;
					goto free_actions;
				}
				fs_dr_actions[fs_dr_num_actions++] = tmp_action;
				term_actions[num_term_actions++].dest = tmp_action;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_UPLINK:
			case MLX5_FLOW_DESTINATION_TYPE_VPORT:
				tmp_action = type == MLX5_FLOW_DESTINATION_TYPE_VPORT ?
					     create_vport_action(domain, dst) :
					     create_uplink_action(domain, dst);
				if (!tmp_action) {
					err = -ENOMEM;
					goto free_actions;
				}
				fs_dr_actions[fs_dr_num_actions++] = tmp_action;
				term_actions[num_term_actions].dest = tmp_action;

				if (dst->dest_attr.vport.flags &
				    MLX5_FLOW_DEST_VPORT_REFORMAT_ID)
					term_actions[num_term_actions].reformat =
						dst->dest_attr.vport.pkt_reformat->action.dr_action;

				num_term_actions++;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM:
				id = dst->dest_attr.ft_num;
				tmp_action = mlx5dr_action_create_dest_table_num(domain,
										 id);
				if (!tmp_action) {
					err = -ENOMEM;
					goto free_actions;
				}
				fs_dr_actions[fs_dr_num_actions++] = tmp_action;
				term_actions[num_term_actions++].dest = tmp_action;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER:
				id = dst->dest_attr.sampler_id;
				tmp_action = mlx5dr_action_create_flow_sampler(domain,
									       id);
				if (!tmp_action) {
					err = -ENOMEM;
					goto free_actions;
				}
				fs_dr_actions[fs_dr_num_actions++] = tmp_action;
				term_actions[num_term_actions++].dest = tmp_action;
				break;
			default:
				err = -EOPNOTSUPP;
				goto free_actions;
			}
		}
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		list_for_each_entry(dst, &fte->node.children, node.list) {
			u32 id;

			if (dst->dest_attr.type !=
			    MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX ||
			    fs_dr_num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
				err = -EOPNOTSUPP;
				goto free_actions;
			}

			id = dst->dest_attr.counter_id;
			tmp_action =
				mlx5dr_action_create_flow_counter(id);
			if (!tmp_action) {
				err = -ENOMEM;
				goto free_actions;
			}

			fs_dr_actions[fs_dr_num_actions++] = tmp_action;
			actions[num_actions++] = tmp_action;
		}
	}

	params.match_sz = match_sz;
	params.match_buf = (u64 *)fte->val;
	if (num_term_actions == 1) {
		if (term_actions->reformat) {
			if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
				err = -EOPNOTSUPP;
				goto free_actions;
			}
			actions[num_actions++] = term_actions->reformat;
		}

		if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}
		actions[num_actions++] = term_actions->dest;
	} else if (num_term_actions > 1) {
		bool ignore_flow_level =
			!!(fte->action.flags & FLOW_ACT_IGNORE_FLOW_LEVEL);
		u32 flow_source = fte->flow_context.flow_source;

		if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX ||
		    fs_dr_num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}
		tmp_action = mlx5dr_action_create_mult_dest_tbl(domain,
								term_actions,
								num_term_actions,
								ignore_flow_level,
								flow_source);
		if (!tmp_action) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}
		fs_dr_actions[fs_dr_num_actions++] = tmp_action;
		actions[num_actions++] = tmp_action;
	}

	rule = mlx5dr_rule_create(group->fs_dr_matcher.dr_matcher,
				  &params,
				  num_actions,
				  actions,
				  fte->flow_context.flow_source);
	if (!rule) {
		err = -EINVAL;
		goto free_actions;
	}

	kfree(term_actions);
	kfree(actions);

	fte->fs_dr_rule.dr_rule = rule;
	fte->fs_dr_rule.num_actions = fs_dr_num_actions;
	fte->fs_dr_rule.dr_actions = fs_dr_actions;

	return 0;

free_actions:
	/* Free in reverse order to handle action dependencies */
	for (i = fs_dr_num_actions - 1; i >= 0; i--)
		if (!IS_ERR_OR_NULL(fs_dr_actions[i]))
			mlx5dr_action_destroy(fs_dr_actions[i]);

	kfree(term_actions);
free_fs_dr_actions_alloc:
	kfree(fs_dr_actions);
free_actions_alloc:
	kfree(actions);
out_err:
	mlx5_core_err(dev, "Failed to create dr rule err(%d)\n", err);
	return err;
}

static int mlx5_cmd_dr_packet_reformat_alloc(struct mlx5_flow_root_namespace *ns,
					     struct mlx5_pkt_reformat_params *params,
					     enum mlx5_flow_namespace_type namespace,
					     struct mlx5_pkt_reformat *pkt_reformat)
{
	struct mlx5dr_domain *dr_domain = ns->fs_dr_domain.dr_domain;
	struct mlx5dr_action *action;
	int dr_reformat;

	switch (params->type) {
	case MLX5_REFORMAT_TYPE_L2_TO_VXLAN:
	case MLX5_REFORMAT_TYPE_L2_TO_NVGRE:
	case MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL:
		dr_reformat = DR_ACTION_REFORMAT_TYP_L2_TO_TNL_L2;
		break;
	case MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2:
		dr_reformat = DR_ACTION_REFORMAT_TYP_TNL_L3_TO_L2;
		break;
	case MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL:
		dr_reformat = DR_ACTION_REFORMAT_TYP_L2_TO_TNL_L3;
		break;
	case MLX5_REFORMAT_TYPE_INSERT_HDR:
		dr_reformat = DR_ACTION_REFORMAT_TYP_INSERT_HDR;
		break;
	case MLX5_REFORMAT_TYPE_REMOVE_HDR:
		dr_reformat = DR_ACTION_REFORMAT_TYP_REMOVE_HDR;
		break;
	default:
		mlx5_core_err(ns->dev, "Packet-reformat not supported(%d)\n",
			      params->type);
		return -EOPNOTSUPP;
	}

	action = mlx5dr_action_create_packet_reformat(dr_domain,
						      dr_reformat,
						      params->param_0,
						      params->param_1,
						      params->size,
						      params->data);
	if (!action) {
		mlx5_core_err(ns->dev, "Failed allocating packet-reformat action\n");
		return -EINVAL;
	}

	pkt_reformat->action.dr_action = action;

	return 0;
}

static void mlx5_cmd_dr_packet_reformat_dealloc(struct mlx5_flow_root_namespace *ns,
						struct mlx5_pkt_reformat *pkt_reformat)
{
	mlx5dr_action_destroy(pkt_reformat->action.dr_action);
}

static int mlx5_cmd_dr_modify_header_alloc(struct mlx5_flow_root_namespace *ns,
					   u8 namespace, u8 num_actions,
					   void *modify_actions,
					   struct mlx5_modify_hdr *modify_hdr)
{
	struct mlx5dr_domain *dr_domain = ns->fs_dr_domain.dr_domain;
	struct mlx5dr_action *action;
	size_t actions_sz;

	actions_sz = MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto) *
		num_actions;
	action = mlx5dr_action_create_modify_header(dr_domain, 0,
						    actions_sz,
						    modify_actions);
	if (!action) {
		mlx5_core_err(ns->dev, "Failed allocating modify-header action\n");
		return -EINVAL;
	}

	modify_hdr->action.dr_action = action;

	return 0;
}

static void mlx5_cmd_dr_modify_header_dealloc(struct mlx5_flow_root_namespace *ns,
					      struct mlx5_modify_hdr *modify_hdr)
{
	mlx5dr_action_destroy(modify_hdr->action.dr_action);
}

static int
mlx5_cmd_dr_destroy_match_definer(struct mlx5_flow_root_namespace *ns,
				  int definer_id)
{
	return -EOPNOTSUPP;
}

static int mlx5_cmd_dr_create_match_definer(struct mlx5_flow_root_namespace *ns,
					    u16 format_id, u32 *match_mask)
{
	return -EOPNOTSUPP;
}

static int mlx5_cmd_dr_delete_fte(struct mlx5_flow_root_namespace *ns,
				  struct mlx5_flow_table *ft,
				  struct fs_fte *fte)
{
	struct mlx5_fs_dr_rule *rule = &fte->fs_dr_rule;
	int err;
	int i;

	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->delete_fte(ns, ft, fte);

	err = mlx5dr_rule_destroy(rule->dr_rule);
	if (err)
		return err;

	/* Free in reverse order to handle action dependencies */
	for (i = rule->num_actions - 1; i >= 0; i--)
		if (!IS_ERR_OR_NULL(rule->dr_actions[i]))
			mlx5dr_action_destroy(rule->dr_actions[i]);

	kfree(rule->dr_actions);
	return 0;
}

static int mlx5_cmd_dr_update_fte(struct mlx5_flow_root_namespace *ns,
				  struct mlx5_flow_table *ft,
				  struct mlx5_flow_group *group,
				  int modify_mask,
				  struct fs_fte *fte)
{
	struct fs_fte fte_tmp = {};
	int ret;

	if (mlx5_dr_is_fw_table(ft->flags))
		return mlx5_fs_cmd_get_fw_cmds()->update_fte(ns, ft, group, modify_mask, fte);

	/* Backup current dr rule details */
	fte_tmp.fs_dr_rule = fte->fs_dr_rule;
	memset(&fte->fs_dr_rule, 0, sizeof(struct mlx5_fs_dr_rule));

	/* First add the new updated rule, then delete the old rule */
	ret = mlx5_cmd_dr_create_fte(ns, ft, group, fte);
	if (ret)
		goto restore_fte;

	ret = mlx5_cmd_dr_delete_fte(ns, ft, &fte_tmp);
	WARN_ONCE(ret, "dr update fte duplicate rule deletion failed\n");
	return ret;

restore_fte:
	fte->fs_dr_rule = fte_tmp.fs_dr_rule;
	return ret;
}

static int mlx5_cmd_dr_set_peer(struct mlx5_flow_root_namespace *ns,
				struct mlx5_flow_root_namespace *peer_ns)
{
	struct mlx5dr_domain *peer_domain = NULL;

	if (peer_ns)
		peer_domain = peer_ns->fs_dr_domain.dr_domain;
	mlx5dr_domain_set_peer(ns->fs_dr_domain.dr_domain,
			       peer_domain);
	return 0;
}

static int mlx5_cmd_dr_create_ns(struct mlx5_flow_root_namespace *ns)
{
	ns->fs_dr_domain.dr_domain =
		mlx5dr_domain_create(ns->dev,
				     MLX5DR_DOMAIN_TYPE_FDB);
	if (!ns->fs_dr_domain.dr_domain) {
		mlx5_core_err(ns->dev, "Failed to create dr flow namespace\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int mlx5_cmd_dr_destroy_ns(struct mlx5_flow_root_namespace *ns)
{
	return mlx5dr_domain_destroy(ns->fs_dr_domain.dr_domain);
}

static u32 mlx5_cmd_dr_get_capabilities(struct mlx5_flow_root_namespace *ns,
					enum fs_flow_table_type ft_type)
{
	if (ft_type != FS_FT_FDB ||
	    MLX5_CAP_GEN(ns->dev, steering_format_version) == MLX5_STEERING_FORMAT_CONNECTX_5)
		return 0;

	return MLX5_FLOW_STEERING_CAP_VLAN_PUSH_ON_RX | MLX5_FLOW_STEERING_CAP_VLAN_POP_ON_TX;
}

bool mlx5_fs_dr_is_supported(struct mlx5_core_dev *dev)
{
	return mlx5dr_is_supported(dev);
}

static const struct mlx5_flow_cmds mlx5_flow_cmds_dr = {
	.create_flow_table = mlx5_cmd_dr_create_flow_table,
	.destroy_flow_table = mlx5_cmd_dr_destroy_flow_table,
	.modify_flow_table = mlx5_cmd_dr_modify_flow_table,
	.create_flow_group = mlx5_cmd_dr_create_flow_group,
	.destroy_flow_group = mlx5_cmd_dr_destroy_flow_group,
	.create_fte = mlx5_cmd_dr_create_fte,
	.update_fte = mlx5_cmd_dr_update_fte,
	.delete_fte = mlx5_cmd_dr_delete_fte,
	.update_root_ft = mlx5_cmd_dr_update_root_ft,
	.packet_reformat_alloc = mlx5_cmd_dr_packet_reformat_alloc,
	.packet_reformat_dealloc = mlx5_cmd_dr_packet_reformat_dealloc,
	.modify_header_alloc = mlx5_cmd_dr_modify_header_alloc,
	.modify_header_dealloc = mlx5_cmd_dr_modify_header_dealloc,
	.create_match_definer = mlx5_cmd_dr_create_match_definer,
	.destroy_match_definer = mlx5_cmd_dr_destroy_match_definer,
	.set_peer = mlx5_cmd_dr_set_peer,
	.create_ns = mlx5_cmd_dr_create_ns,
	.destroy_ns = mlx5_cmd_dr_destroy_ns,
	.get_capabilities = mlx5_cmd_dr_get_capabilities,
};

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_dr_cmds(void)
{
		return &mlx5_flow_cmds_dr;
}
