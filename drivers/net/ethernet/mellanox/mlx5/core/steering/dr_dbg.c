// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include "dr_types.h"

#define DR_DBG_PTR_TO_ID(p) ((u64)(uintptr_t)(p) & 0xFFFFFFFFULL)

enum dr_dump_rec_type {
	DR_DUMP_REC_TYPE_DOMAIN = 3000,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_FLEX_PARSER = 3001,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_DEV_ATTR = 3002,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_VPORT = 3003,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_CAPS = 3004,
	DR_DUMP_REC_TYPE_DOMAIN_SEND_RING = 3005,

	DR_DUMP_REC_TYPE_TABLE = 3100,
	DR_DUMP_REC_TYPE_TABLE_RX = 3101,
	DR_DUMP_REC_TYPE_TABLE_TX = 3102,

	DR_DUMP_REC_TYPE_MATCHER = 3200,
	DR_DUMP_REC_TYPE_MATCHER_MASK_DEPRECATED = 3201,
	DR_DUMP_REC_TYPE_MATCHER_RX = 3202,
	DR_DUMP_REC_TYPE_MATCHER_TX = 3203,
	DR_DUMP_REC_TYPE_MATCHER_BUILDER = 3204,
	DR_DUMP_REC_TYPE_MATCHER_MASK = 3205,

	DR_DUMP_REC_TYPE_RULE = 3300,
	DR_DUMP_REC_TYPE_RULE_RX_ENTRY_V0 = 3301,
	DR_DUMP_REC_TYPE_RULE_TX_ENTRY_V0 = 3302,
	DR_DUMP_REC_TYPE_RULE_RX_ENTRY_V1 = 3303,
	DR_DUMP_REC_TYPE_RULE_TX_ENTRY_V1 = 3304,

	DR_DUMP_REC_TYPE_ACTION_ENCAP_L2 = 3400,
	DR_DUMP_REC_TYPE_ACTION_ENCAP_L3 = 3401,
	DR_DUMP_REC_TYPE_ACTION_MODIFY_HDR = 3402,
	DR_DUMP_REC_TYPE_ACTION_DROP = 3403,
	DR_DUMP_REC_TYPE_ACTION_QP = 3404,
	DR_DUMP_REC_TYPE_ACTION_FT = 3405,
	DR_DUMP_REC_TYPE_ACTION_CTR = 3406,
	DR_DUMP_REC_TYPE_ACTION_TAG = 3407,
	DR_DUMP_REC_TYPE_ACTION_VPORT = 3408,
	DR_DUMP_REC_TYPE_ACTION_DECAP_L2 = 3409,
	DR_DUMP_REC_TYPE_ACTION_DECAP_L3 = 3410,
	DR_DUMP_REC_TYPE_ACTION_DEVX_TIR = 3411,
	DR_DUMP_REC_TYPE_ACTION_PUSH_VLAN = 3412,
	DR_DUMP_REC_TYPE_ACTION_POP_VLAN = 3413,
	DR_DUMP_REC_TYPE_ACTION_SAMPLER = 3415,
	DR_DUMP_REC_TYPE_ACTION_INSERT_HDR = 3420,
	DR_DUMP_REC_TYPE_ACTION_REMOVE_HDR = 3421,
	DR_DUMP_REC_TYPE_ACTION_MATCH_RANGE = 3425,
};

void mlx5dr_dbg_tbl_add(struct mlx5dr_table *tbl)
{
	mutex_lock(&tbl->dmn->dump_info.dbg_mutex);
	list_add_tail(&tbl->dbg_node, &tbl->dmn->dbg_tbl_list);
	mutex_unlock(&tbl->dmn->dump_info.dbg_mutex);
}

void mlx5dr_dbg_tbl_del(struct mlx5dr_table *tbl)
{
	mutex_lock(&tbl->dmn->dump_info.dbg_mutex);
	list_del(&tbl->dbg_node);
	mutex_unlock(&tbl->dmn->dump_info.dbg_mutex);
}

void mlx5dr_dbg_rule_add(struct mlx5dr_rule *rule)
{
	struct mlx5dr_domain *dmn = rule->matcher->tbl->dmn;

	mutex_lock(&dmn->dump_info.dbg_mutex);
	list_add_tail(&rule->dbg_node, &rule->matcher->dbg_rule_list);
	mutex_unlock(&dmn->dump_info.dbg_mutex);
}

void mlx5dr_dbg_rule_del(struct mlx5dr_rule *rule)
{
	struct mlx5dr_domain *dmn = rule->matcher->tbl->dmn;

	mutex_lock(&dmn->dump_info.dbg_mutex);
	list_del(&rule->dbg_node);
	mutex_unlock(&dmn->dump_info.dbg_mutex);
}

static u64 dr_dump_icm_to_idx(u64 icm_addr)
{
	return (icm_addr >> 6) & 0xffffffff;
}

#define DR_HEX_SIZE 256

static void
dr_dump_hex_print(char hex[DR_HEX_SIZE], char *src, u32 size)
{
	if (WARN_ON_ONCE(DR_HEX_SIZE < 2 * size + 1))
		size = DR_HEX_SIZE / 2 - 1; /* truncate */

	bin2hex(hex, src, size);
	hex[2 * size] = 0; /* NULL-terminate */
}

static int
dr_dump_rule_action_mem(struct seq_file *file, const u64 rule_id,
			struct mlx5dr_rule_action_member *action_mem)
{
	struct mlx5dr_action *action = action_mem->action;
	const u64 action_id = DR_DBG_PTR_TO_ID(action);
	u64 hit_tbl_ptr, miss_tbl_ptr;
	u32 hit_tbl_id, miss_tbl_id;

	switch (action->action_type) {
	case DR_ACTION_TYP_DROP:
		seq_printf(file, "%d,0x%llx,0x%llx\n",
			   DR_DUMP_REC_TYPE_ACTION_DROP, action_id, rule_id);
		break;
	case DR_ACTION_TYP_FT:
		if (action->dest_tbl->is_fw_tbl)
			seq_printf(file, "%d,0x%llx,0x%llx,0x%x,0x%x\n",
				   DR_DUMP_REC_TYPE_ACTION_FT, action_id,
				   rule_id, action->dest_tbl->fw_tbl.id,
				   -1);
		else
			seq_printf(file, "%d,0x%llx,0x%llx,0x%x,0x%llx\n",
				   DR_DUMP_REC_TYPE_ACTION_FT, action_id,
				   rule_id, action->dest_tbl->tbl->table_id,
				   DR_DBG_PTR_TO_ID(action->dest_tbl->tbl));

		break;
	case DR_ACTION_TYP_CTR:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_CTR, action_id, rule_id,
			   action->ctr->ctr_id + action->ctr->offset);
		break;
	case DR_ACTION_TYP_TAG:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_TAG, action_id, rule_id,
			   action->flow_tag->flow_tag);
		break;
	case DR_ACTION_TYP_MODIFY_HDR:
	{
		struct mlx5dr_ptrn_obj *ptrn = action->rewrite->ptrn;
		struct mlx5dr_arg_obj *arg = action->rewrite->arg;
		u8 *rewrite_data = action->rewrite->data;
		bool ptrn_arg;
		int i;

		ptrn_arg = !action->rewrite->single_action_opt && ptrn && arg;

		seq_printf(file, "%d,0x%llx,0x%llx,0x%x,%d,0x%x,0x%x,0x%x",
			   DR_DUMP_REC_TYPE_ACTION_MODIFY_HDR, action_id,
			   rule_id, action->rewrite->index,
			   action->rewrite->single_action_opt,
			   ptrn_arg ? action->rewrite->num_of_actions : 0,
			   ptrn_arg ? ptrn->index : 0,
			   ptrn_arg ? mlx5dr_arg_get_obj_id(arg) : 0);

		if (ptrn_arg) {
			for (i = 0; i < action->rewrite->num_of_actions; i++) {
				seq_printf(file, ",0x%016llx",
					   be64_to_cpu(((__be64 *)rewrite_data)[i]));
			}
		}

		seq_puts(file, "\n");
		break;
	}
	case DR_ACTION_TYP_VPORT:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_VPORT, action_id, rule_id,
			   action->vport->caps->num);
		break;
	case DR_ACTION_TYP_TNL_L2_TO_L2:
		seq_printf(file, "%d,0x%llx,0x%llx\n",
			   DR_DUMP_REC_TYPE_ACTION_DECAP_L2, action_id,
			   rule_id);
		break;
	case DR_ACTION_TYP_TNL_L3_TO_L2:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_DECAP_L3, action_id,
			   rule_id,
			   (action->rewrite->ptrn && action->rewrite->arg) ?
			   mlx5dr_arg_get_obj_id(action->rewrite->arg) :
			   action->rewrite->index);
		break;
	case DR_ACTION_TYP_L2_TO_TNL_L2:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_ENCAP_L2, action_id,
			   rule_id, action->reformat->id);
		break;
	case DR_ACTION_TYP_L2_TO_TNL_L3:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_ENCAP_L3, action_id,
			   rule_id, action->reformat->id);
		break;
	case DR_ACTION_TYP_POP_VLAN:
		seq_printf(file, "%d,0x%llx,0x%llx\n",
			   DR_DUMP_REC_TYPE_ACTION_POP_VLAN, action_id,
			   rule_id);
		break;
	case DR_ACTION_TYP_PUSH_VLAN:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_PUSH_VLAN, action_id,
			   rule_id, action->push_vlan->vlan_hdr);
		break;
	case DR_ACTION_TYP_INSERT_HDR:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x,0x%x,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_INSERT_HDR, action_id,
			   rule_id, action->reformat->id,
			   action->reformat->param_0,
			   action->reformat->param_1);
		break;
	case DR_ACTION_TYP_REMOVE_HDR:
		seq_printf(file, "%d,0x%llx,0x%llx,0x%x,0x%x,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_REMOVE_HDR, action_id,
			   rule_id, action->reformat->id,
			   action->reformat->param_0,
			   action->reformat->param_1);
		break;
	case DR_ACTION_TYP_SAMPLER:
		seq_printf(file,
			   "%d,0x%llx,0x%llx,0x%x,0x%x,0x%x,0x%llx,0x%llx\n",
			   DR_DUMP_REC_TYPE_ACTION_SAMPLER, action_id, rule_id,
			   0, 0, action->sampler->sampler_id,
			   action->sampler->rx_icm_addr,
			   action->sampler->tx_icm_addr);
		break;
	case DR_ACTION_TYP_RANGE:
		if (action->range->hit_tbl_action->dest_tbl->is_fw_tbl) {
			hit_tbl_id = action->range->hit_tbl_action->dest_tbl->fw_tbl.id;
			hit_tbl_ptr = 0;
		} else {
			hit_tbl_id = action->range->hit_tbl_action->dest_tbl->tbl->table_id;
			hit_tbl_ptr =
				DR_DBG_PTR_TO_ID(action->range->hit_tbl_action->dest_tbl->tbl);
		}

		if (action->range->miss_tbl_action->dest_tbl->is_fw_tbl) {
			miss_tbl_id = action->range->miss_tbl_action->dest_tbl->fw_tbl.id;
			miss_tbl_ptr = 0;
		} else {
			miss_tbl_id = action->range->miss_tbl_action->dest_tbl->tbl->table_id;
			miss_tbl_ptr =
				DR_DBG_PTR_TO_ID(action->range->miss_tbl_action->dest_tbl->tbl);
		}

		seq_printf(file, "%d,0x%llx,0x%llx,0x%x,0x%llx,0x%x,0x%llx,0x%x\n",
			   DR_DUMP_REC_TYPE_ACTION_MATCH_RANGE, action_id, rule_id,
			   hit_tbl_id, hit_tbl_ptr, miss_tbl_id, miss_tbl_ptr,
			   action->range->definer_id);
		break;
	default:
		return 0;
	}

	return 0;
}

static int
dr_dump_rule_mem(struct seq_file *file, struct mlx5dr_ste *ste,
		 bool is_rx, const u64 rule_id, u8 format_ver)
{
	char hw_ste_dump[DR_HEX_SIZE];
	u32 mem_rec_type;

	if (format_ver == MLX5_STEERING_FORMAT_CONNECTX_5) {
		mem_rec_type = is_rx ? DR_DUMP_REC_TYPE_RULE_RX_ENTRY_V0 :
				       DR_DUMP_REC_TYPE_RULE_TX_ENTRY_V0;
	} else {
		mem_rec_type = is_rx ? DR_DUMP_REC_TYPE_RULE_RX_ENTRY_V1 :
				       DR_DUMP_REC_TYPE_RULE_TX_ENTRY_V1;
	}

	dr_dump_hex_print(hw_ste_dump, (char *)mlx5dr_ste_get_hw_ste(ste),
			  DR_STE_SIZE_REDUCED);

	seq_printf(file, "%d,0x%llx,0x%llx,%s\n", mem_rec_type,
		   dr_dump_icm_to_idx(mlx5dr_ste_get_icm_addr(ste)), rule_id,
		   hw_ste_dump);

	return 0;
}

static int
dr_dump_rule_rx_tx(struct seq_file *file, struct mlx5dr_rule_rx_tx *rule_rx_tx,
		   bool is_rx, const u64 rule_id, u8 format_ver)
{
	struct mlx5dr_ste *ste_arr[DR_RULE_MAX_STES + DR_ACTION_MAX_STES];
	struct mlx5dr_ste *curr_ste = rule_rx_tx->last_rule_ste;
	int ret, i;

	if (mlx5dr_rule_get_reverse_rule_members(ste_arr, curr_ste, &i))
		return 0;

	while (i--) {
		ret = dr_dump_rule_mem(file, ste_arr[i], is_rx, rule_id,
				       format_ver);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dr_dump_rule(struct seq_file *file, struct mlx5dr_rule *rule)
{
	struct mlx5dr_rule_action_member *action_mem;
	const u64 rule_id = DR_DBG_PTR_TO_ID(rule);
	struct mlx5dr_rule_rx_tx *rx = &rule->rx;
	struct mlx5dr_rule_rx_tx *tx = &rule->tx;
	u8 format_ver;
	int ret;

	format_ver = rule->matcher->tbl->dmn->info.caps.sw_format_ver;

	seq_printf(file, "%d,0x%llx,0x%llx\n", DR_DUMP_REC_TYPE_RULE, rule_id,
		   DR_DBG_PTR_TO_ID(rule->matcher));

	if (rx->nic_matcher) {
		ret = dr_dump_rule_rx_tx(file, rx, true, rule_id, format_ver);
		if (ret < 0)
			return ret;
	}

	if (tx->nic_matcher) {
		ret = dr_dump_rule_rx_tx(file, tx, false, rule_id, format_ver);
		if (ret < 0)
			return ret;
	}

	list_for_each_entry(action_mem, &rule->rule_actions_list, list) {
		ret = dr_dump_rule_action_mem(file, rule_id, action_mem);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
dr_dump_matcher_mask(struct seq_file *file, struct mlx5dr_match_param *mask,
		     u8 criteria, const u64 matcher_id)
{
	char dump[DR_HEX_SIZE];

	seq_printf(file, "%d,0x%llx,", DR_DUMP_REC_TYPE_MATCHER_MASK,
		   matcher_id);

	if (criteria & DR_MATCHER_CRITERIA_OUTER) {
		dr_dump_hex_print(dump, (char *)&mask->outer, sizeof(mask->outer));
		seq_printf(file, "%s,", dump);
	} else {
		seq_puts(file, ",");
	}

	if (criteria & DR_MATCHER_CRITERIA_INNER) {
		dr_dump_hex_print(dump, (char *)&mask->inner, sizeof(mask->inner));
		seq_printf(file, "%s,", dump);
	} else {
		seq_puts(file, ",");
	}

	if (criteria & DR_MATCHER_CRITERIA_MISC) {
		dr_dump_hex_print(dump, (char *)&mask->misc, sizeof(mask->misc));
		seq_printf(file, "%s,", dump);
	} else {
		seq_puts(file, ",");
	}

	if (criteria & DR_MATCHER_CRITERIA_MISC2) {
		dr_dump_hex_print(dump, (char *)&mask->misc2, sizeof(mask->misc2));
		seq_printf(file, "%s,", dump);
	} else {
		seq_puts(file, ",");
	}

	if (criteria & DR_MATCHER_CRITERIA_MISC3) {
		dr_dump_hex_print(dump, (char *)&mask->misc3, sizeof(mask->misc3));
		seq_printf(file, "%s\n", dump);
	} else {
		seq_puts(file, ",\n");
	}

	return 0;
}

static int
dr_dump_matcher_builder(struct seq_file *file, struct mlx5dr_ste_build *builder,
			u32 index, bool is_rx, const u64 matcher_id)
{
	seq_printf(file, "%d,0x%llx,%d,%d,0x%x\n",
		   DR_DUMP_REC_TYPE_MATCHER_BUILDER, matcher_id, index, is_rx,
		   builder->lu_type);

	return 0;
}

static int
dr_dump_matcher_rx_tx(struct seq_file *file, bool is_rx,
		      struct mlx5dr_matcher_rx_tx *matcher_rx_tx,
		      const u64 matcher_id)
{
	enum dr_dump_rec_type rec_type;
	u64 s_icm_addr, e_icm_addr;
	int i, ret;

	rec_type = is_rx ? DR_DUMP_REC_TYPE_MATCHER_RX :
			   DR_DUMP_REC_TYPE_MATCHER_TX;

	s_icm_addr = mlx5dr_icm_pool_get_chunk_icm_addr(matcher_rx_tx->s_htbl->chunk);
	e_icm_addr = mlx5dr_icm_pool_get_chunk_icm_addr(matcher_rx_tx->e_anchor->chunk);
	seq_printf(file, "%d,0x%llx,0x%llx,%d,0x%llx,0x%llx\n",
		   rec_type, DR_DBG_PTR_TO_ID(matcher_rx_tx),
		   matcher_id, matcher_rx_tx->num_of_builders,
		   dr_dump_icm_to_idx(s_icm_addr),
		   dr_dump_icm_to_idx(e_icm_addr));

	for (i = 0; i < matcher_rx_tx->num_of_builders; i++) {
		ret = dr_dump_matcher_builder(file,
					      &matcher_rx_tx->ste_builder[i],
					      i, is_rx, matcher_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
dr_dump_matcher(struct seq_file *file, struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_matcher_rx_tx *rx = &matcher->rx;
	struct mlx5dr_matcher_rx_tx *tx = &matcher->tx;
	u64 matcher_id;
	int ret;

	matcher_id = DR_DBG_PTR_TO_ID(matcher);

	seq_printf(file, "%d,0x%llx,0x%llx,%d\n", DR_DUMP_REC_TYPE_MATCHER,
		   matcher_id, DR_DBG_PTR_TO_ID(matcher->tbl), matcher->prio);

	ret = dr_dump_matcher_mask(file, &matcher->mask,
				   matcher->match_criteria, matcher_id);
	if (ret < 0)
		return ret;

	if (rx->nic_tbl) {
		ret = dr_dump_matcher_rx_tx(file, true, rx, matcher_id);
		if (ret < 0)
			return ret;
	}

	if (tx->nic_tbl) {
		ret = dr_dump_matcher_rx_tx(file, false, tx, matcher_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
dr_dump_matcher_all(struct seq_file *file, struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_rule *rule;
	int ret;

	ret = dr_dump_matcher(file, matcher);
	if (ret < 0)
		return ret;

	list_for_each_entry(rule, &matcher->dbg_rule_list, dbg_node) {
		ret = dr_dump_rule(file, rule);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
dr_dump_table_rx_tx(struct seq_file *file, bool is_rx,
		    struct mlx5dr_table_rx_tx *table_rx_tx,
		    const u64 table_id)
{
	enum dr_dump_rec_type rec_type;
	u64 s_icm_addr;

	rec_type = is_rx ? DR_DUMP_REC_TYPE_TABLE_RX :
			   DR_DUMP_REC_TYPE_TABLE_TX;

	s_icm_addr = mlx5dr_icm_pool_get_chunk_icm_addr(table_rx_tx->s_anchor->chunk);
	seq_printf(file, "%d,0x%llx,0x%llx\n", rec_type, table_id,
		   dr_dump_icm_to_idx(s_icm_addr));

	return 0;
}

static int dr_dump_table(struct seq_file *file, struct mlx5dr_table *table)
{
	struct mlx5dr_table_rx_tx *rx = &table->rx;
	struct mlx5dr_table_rx_tx *tx = &table->tx;
	int ret;

	seq_printf(file, "%d,0x%llx,0x%llx,%d,%d\n", DR_DUMP_REC_TYPE_TABLE,
		   DR_DBG_PTR_TO_ID(table), DR_DBG_PTR_TO_ID(table->dmn),
		   table->table_type, table->level);

	if (rx->nic_dmn) {
		ret = dr_dump_table_rx_tx(file, true, rx,
					  DR_DBG_PTR_TO_ID(table));
		if (ret < 0)
			return ret;
	}

	if (tx->nic_dmn) {
		ret = dr_dump_table_rx_tx(file, false, tx,
					  DR_DBG_PTR_TO_ID(table));
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int dr_dump_table_all(struct seq_file *file, struct mlx5dr_table *tbl)
{
	struct mlx5dr_matcher *matcher;
	int ret;

	ret = dr_dump_table(file, tbl);
	if (ret < 0)
		return ret;

	list_for_each_entry(matcher, &tbl->matcher_list, list_node) {
		ret = dr_dump_matcher_all(file, matcher);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int
dr_dump_send_ring(struct seq_file *file, struct mlx5dr_send_ring *ring,
		  const u64 domain_id)
{
	seq_printf(file, "%d,0x%llx,0x%llx,0x%x,0x%x\n",
		   DR_DUMP_REC_TYPE_DOMAIN_SEND_RING, DR_DBG_PTR_TO_ID(ring),
		   domain_id, ring->cq->mcq.cqn, ring->qp->qpn);
	return 0;
}

static int
dr_dump_domain_info_flex_parser(struct seq_file *file,
				const char *flex_parser_name,
				const u8 flex_parser_value,
				const u64 domain_id)
{
	seq_printf(file, "%d,0x%llx,%s,0x%x\n",
		   DR_DUMP_REC_TYPE_DOMAIN_INFO_FLEX_PARSER, domain_id,
		   flex_parser_name, flex_parser_value);
	return 0;
}

static int
dr_dump_domain_info_caps(struct seq_file *file, struct mlx5dr_cmd_caps *caps,
			 const u64 domain_id)
{
	struct mlx5dr_cmd_vport_cap *vport_caps;
	unsigned long i, vports_num;

	xa_for_each(&caps->vports.vports_caps_xa, vports_num, vport_caps)
		; /* count the number of vports in xarray */

	seq_printf(file, "%d,0x%llx,0x%x,0x%llx,0x%llx,0x%x,%lu,%d\n",
		   DR_DUMP_REC_TYPE_DOMAIN_INFO_CAPS, domain_id, caps->gvmi,
		   caps->nic_rx_drop_address, caps->nic_tx_drop_address,
		   caps->flex_protocols, vports_num, caps->eswitch_manager);

	xa_for_each(&caps->vports.vports_caps_xa, i, vport_caps) {
		vport_caps = xa_load(&caps->vports.vports_caps_xa, i);

		seq_printf(file, "%d,0x%llx,%lu,0x%x,0x%llx,0x%llx\n",
			   DR_DUMP_REC_TYPE_DOMAIN_INFO_VPORT, domain_id, i,
			   vport_caps->vport_gvmi, vport_caps->icm_address_rx,
			   vport_caps->icm_address_tx);
	}
	return 0;
}

static int
dr_dump_domain_info(struct seq_file *file, struct mlx5dr_domain_info *info,
		    const u64 domain_id)
{
	int ret;

	ret = dr_dump_domain_info_caps(file, &info->caps, domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(file, "icmp_dw0",
					      info->caps.flex_parser_id_icmp_dw0,
					      domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(file, "icmp_dw1",
					      info->caps.flex_parser_id_icmp_dw1,
					      domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(file, "icmpv6_dw0",
					      info->caps.flex_parser_id_icmpv6_dw0,
					      domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(file, "icmpv6_dw1",
					      info->caps.flex_parser_id_icmpv6_dw1,
					      domain_id);
	if (ret < 0)
		return ret;

	return 0;
}

static int
dr_dump_domain(struct seq_file *file, struct mlx5dr_domain *dmn)
{
	u64 domain_id = DR_DBG_PTR_TO_ID(dmn);
	int ret;

	seq_printf(file, "%d,0x%llx,%d,0%x,%d,%u.%u.%u,%s,%d,%u,%u,%u\n",
		   DR_DUMP_REC_TYPE_DOMAIN,
		   domain_id, dmn->type, dmn->info.caps.gvmi,
		   dmn->info.supp_sw_steering,
		   /* package version */
		   LINUX_VERSION_MAJOR, LINUX_VERSION_PATCHLEVEL,
		   LINUX_VERSION_SUBLEVEL,
		   pci_name(dmn->mdev->pdev),
		   0, /* domain flags */
		   dmn->num_buddies[DR_ICM_TYPE_STE],
		   dmn->num_buddies[DR_ICM_TYPE_MODIFY_ACTION],
		   dmn->num_buddies[DR_ICM_TYPE_MODIFY_HDR_PTRN]);

	ret = dr_dump_domain_info(file, &dmn->info, domain_id);
	if (ret < 0)
		return ret;

	if (dmn->info.supp_sw_steering) {
		ret = dr_dump_send_ring(file, dmn->send_ring, domain_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dr_dump_domain_all(struct seq_file *file, struct mlx5dr_domain *dmn)
{
	struct mlx5dr_table *tbl;
	int ret;

	mutex_lock(&dmn->dump_info.dbg_mutex);
	mlx5dr_domain_lock(dmn);

	ret = dr_dump_domain(file, dmn);
	if (ret < 0)
		goto unlock_mutex;

	list_for_each_entry(tbl, &dmn->dbg_tbl_list, dbg_node) {
		ret = dr_dump_table_all(file, tbl);
		if (ret < 0)
			break;
	}

unlock_mutex:
	mlx5dr_domain_unlock(dmn);
	mutex_unlock(&dmn->dump_info.dbg_mutex);
	return ret;
}

static int dr_dump_show(struct seq_file *file, void *priv)
{
	return dr_dump_domain_all(file, file->private);
}
DEFINE_SHOW_ATTRIBUTE(dr_dump);

void mlx5dr_dbg_init_dump(struct mlx5dr_domain *dmn)
{
	struct mlx5_core_dev *dev = dmn->mdev;
	char file_name[128];

	if (dmn->type != MLX5DR_DOMAIN_TYPE_FDB) {
		mlx5_core_warn(dev,
			       "Steering dump is not supported for NIC RX/TX domains\n");
		return;
	}

	dmn->dump_info.steering_debugfs =
		debugfs_create_dir("steering", mlx5_debugfs_get_dev_root(dev));
	dmn->dump_info.fdb_debugfs =
		debugfs_create_dir("fdb", dmn->dump_info.steering_debugfs);

	sprintf(file_name, "dmn_%p", dmn);
	debugfs_create_file(file_name, 0444, dmn->dump_info.fdb_debugfs,
			    dmn, &dr_dump_fops);

	INIT_LIST_HEAD(&dmn->dbg_tbl_list);
	mutex_init(&dmn->dump_info.dbg_mutex);
}

void mlx5dr_dbg_uninit_dump(struct mlx5dr_domain *dmn)
{
	debugfs_remove_recursive(dmn->dump_info.steering_debugfs);
	mutex_destroy(&dmn->dump_info.dbg_mutex);
}
