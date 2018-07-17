/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#if !defined(_MLX5_FS_TP_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_FS_TP_

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>
#include "../fs_core.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#define __parse_fs_hdrs(match_criteria_enable, mouter, mmisc, minner, vouter, \
			vinner, vmisc)					      \
	parse_fs_hdrs(p, match_criteria_enable, mouter, mmisc, minner, vouter,\
		      vinner, vmisc)

const char *parse_fs_hdrs(struct trace_seq *p,
			  u8 match_criteria_enable,
			  const u32 *mask_outer,
			  const u32 *mask_misc,
			  const u32 *mask_inner,
			  const u32 *value_outer,
			  const u32 *value_misc,
			  const u32 *value_inner);

#define __parse_fs_dst(dst, counter_id) \
	parse_fs_dst(p, (const struct mlx5_flow_destination *)dst, counter_id)

const char *parse_fs_dst(struct trace_seq *p,
			 const struct mlx5_flow_destination *dst,
			 u32 counter_id);

TRACE_EVENT(mlx5_fs_add_fg,
	    TP_PROTO(const struct mlx5_flow_group *fg),
	    TP_ARGS(fg),
	    TP_STRUCT__entry(
		__field(const struct mlx5_flow_group *, fg)
		__field(const struct mlx5_flow_table *, ft)
		__field(u32, start_index)
		__field(u32, end_index)
		__field(u32, id)
		__field(u8, mask_enable)
		__array(u32, mask_outer, MLX5_ST_SZ_DW(fte_match_set_lyr_2_4))
		__array(u32, mask_inner, MLX5_ST_SZ_DW(fte_match_set_lyr_2_4))
		__array(u32, mask_misc, MLX5_ST_SZ_DW(fte_match_set_misc))
	    ),
	    TP_fast_assign(
			   __entry->fg = fg;
			   fs_get_obj(__entry->ft, fg->node.parent);
			   __entry->start_index = fg->start_index;
			   __entry->end_index = fg->start_index + fg->max_ftes;
			   __entry->id = fg->id;
			   __entry->mask_enable = fg->mask.match_criteria_enable;
			   memcpy(__entry->mask_outer,
				  MLX5_ADDR_OF(fte_match_param,
					       &fg->mask.match_criteria,
					       outer_headers),
				  sizeof(__entry->mask_outer));
			   memcpy(__entry->mask_inner,
				  MLX5_ADDR_OF(fte_match_param,
					       &fg->mask.match_criteria,
					       inner_headers),
				  sizeof(__entry->mask_inner));
			   memcpy(__entry->mask_misc,
				  MLX5_ADDR_OF(fte_match_param,
					       &fg->mask.match_criteria,
					       misc_parameters),
				  sizeof(__entry->mask_misc));

	    ),
	    TP_printk("fg=%p ft=%p id=%u start=%u end=%u bit_mask=%02x %s\n",
		      __entry->fg, __entry->ft, __entry->id,
		      __entry->start_index, __entry->end_index,
		      __entry->mask_enable,
		      __parse_fs_hdrs(__entry->mask_enable,
				      __entry->mask_outer,
				      __entry->mask_misc,
				      __entry->mask_inner,
				      __entry->mask_outer,
				      __entry->mask_misc,
				      __entry->mask_inner))
	    );

TRACE_EVENT(mlx5_fs_del_fg,
	    TP_PROTO(const struct mlx5_flow_group *fg),
	    TP_ARGS(fg),
	    TP_STRUCT__entry(
		__field(const struct mlx5_flow_group *, fg)
		__field(u32, id)
	    ),
	    TP_fast_assign(
			   __entry->fg = fg;
			   __entry->id = fg->id;

	    ),
	    TP_printk("fg=%p id=%u\n",
		      __entry->fg, __entry->id)
	    );

#define ACTION_FLAGS \
	{MLX5_FLOW_CONTEXT_ACTION_ALLOW,	 "ALLOW"},\
	{MLX5_FLOW_CONTEXT_ACTION_DROP,		 "DROP"},\
	{MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,	 "FWD"},\
	{MLX5_FLOW_CONTEXT_ACTION_COUNT,	 "CNT"},\
	{MLX5_FLOW_CONTEXT_ACTION_ENCAP,	 "ENCAP"},\
	{MLX5_FLOW_CONTEXT_ACTION_DECAP,	 "DECAP"},\
	{MLX5_FLOW_CONTEXT_ACTION_MOD_HDR,	 "MOD_HDR"},\
	{MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH,	 "VLAN_PUSH"},\
	{MLX5_FLOW_CONTEXT_ACTION_VLAN_POP,	 "VLAN_POP"},\
	{MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2,	 "VLAN_PUSH_2"},\
	{MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2,	 "VLAN_POP_2"},\
	{MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO, "NEXT_PRIO"}

TRACE_EVENT(mlx5_fs_set_fte,
	    TP_PROTO(const struct fs_fte *fte, int new_fte),
	    TP_ARGS(fte, new_fte),
	    TP_STRUCT__entry(
		__field(const struct fs_fte *, fte)
		__field(const struct mlx5_flow_group *, fg)
		__field(u32, group_index)
		__field(u32, index)
		__field(u32, action)
		__field(u32, flow_tag)
		__field(u8,  mask_enable)
		__field(int, new_fte)
		__array(u32, mask_outer, MLX5_ST_SZ_DW(fte_match_set_lyr_2_4))
		__array(u32, mask_inner, MLX5_ST_SZ_DW(fte_match_set_lyr_2_4))
		__array(u32, mask_misc, MLX5_ST_SZ_DW(fte_match_set_misc))
		__array(u32, value_outer, MLX5_ST_SZ_DW(fte_match_set_lyr_2_4))
		__array(u32, value_inner, MLX5_ST_SZ_DW(fte_match_set_lyr_2_4))
		__array(u32, value_misc, MLX5_ST_SZ_DW(fte_match_set_misc))
	    ),
	    TP_fast_assign(
			   __entry->fte = fte;
			   __entry->new_fte = new_fte;
			   fs_get_obj(__entry->fg, fte->node.parent);
			   __entry->group_index = __entry->fg->id;
			   __entry->index = fte->index;
			   __entry->action = fte->action.action;
			   __entry->mask_enable = __entry->fg->mask.match_criteria_enable;
			   __entry->flow_tag = fte->action.flow_tag;
			   memcpy(__entry->mask_outer,
				  MLX5_ADDR_OF(fte_match_param,
					       &__entry->fg->mask.match_criteria,
					       outer_headers),
				  sizeof(__entry->mask_outer));
			   memcpy(__entry->mask_inner,
				  MLX5_ADDR_OF(fte_match_param,
					       &__entry->fg->mask.match_criteria,
					       inner_headers),
				  sizeof(__entry->mask_inner));
			   memcpy(__entry->mask_misc,
				  MLX5_ADDR_OF(fte_match_param,
					       &__entry->fg->mask.match_criteria,
					       misc_parameters),
				  sizeof(__entry->mask_misc));
			   memcpy(__entry->value_outer,
				  MLX5_ADDR_OF(fte_match_param,
					       &fte->val,
					       outer_headers),
				  sizeof(__entry->value_outer));
			   memcpy(__entry->value_inner,
				  MLX5_ADDR_OF(fte_match_param,
					       &fte->val,
					       inner_headers),
				  sizeof(__entry->value_inner));
			   memcpy(__entry->value_misc,
				  MLX5_ADDR_OF(fte_match_param,
					       &fte->val,
					       misc_parameters),
				  sizeof(__entry->value_misc));

	    ),
	    TP_printk("op=%s fte=%p fg=%p index=%u group_index=%u action=<%s> flow_tag=%x %s\n",
		      __entry->new_fte ? "add" : "set",
		      __entry->fte, __entry->fg, __entry->index,
		      __entry->group_index, __print_flags(__entry->action, "|",
							  ACTION_FLAGS),
		      __entry->flow_tag,
		      __parse_fs_hdrs(__entry->mask_enable,
				      __entry->mask_outer,
				      __entry->mask_misc,
				      __entry->mask_inner,
				      __entry->value_outer,
				      __entry->value_misc,
				      __entry->value_inner))
	    );

TRACE_EVENT(mlx5_fs_del_fte,
	    TP_PROTO(const struct fs_fte *fte),
	    TP_ARGS(fte),
	    TP_STRUCT__entry(
		__field(const struct fs_fte *, fte)
		__field(u32, index)
	    ),
	    TP_fast_assign(
			   __entry->fte = fte;
			   __entry->index = fte->index;

	    ),
	    TP_printk("fte=%p index=%u\n",
		      __entry->fte, __entry->index)
	    );

TRACE_EVENT(mlx5_fs_add_rule,
	    TP_PROTO(const struct mlx5_flow_rule *rule),
	    TP_ARGS(rule),
	    TP_STRUCT__entry(
		__field(const struct mlx5_flow_rule *, rule)
		__field(const struct fs_fte *, fte)
		__field(u32, sw_action)
		__field(u32, index)
		__field(u32, counter_id)
		__array(u8, destination, sizeof(struct mlx5_flow_destination))
	    ),
	    TP_fast_assign(
			   __entry->rule = rule;
			   fs_get_obj(__entry->fte, rule->node.parent);
			   __entry->index = __entry->fte->dests_size - 1;
			   __entry->sw_action = rule->sw_action;
			   memcpy(__entry->destination,
				  &rule->dest_attr,
				  sizeof(__entry->destination));
			   if (rule->dest_attr.type & MLX5_FLOW_DESTINATION_TYPE_COUNTER &&
			       rule->dest_attr.counter)
				__entry->counter_id =
				rule->dest_attr.counter->id;
	    ),
	    TP_printk("rule=%p fte=%p index=%u sw_action=<%s> [dst] %s\n",
		      __entry->rule, __entry->fte, __entry->index,
		      __print_flags(__entry->sw_action, "|", ACTION_FLAGS),
		      __parse_fs_dst(__entry->destination, __entry->counter_id))
	    );

TRACE_EVENT(mlx5_fs_del_rule,
	    TP_PROTO(const struct mlx5_flow_rule *rule),
	    TP_ARGS(rule),
	    TP_STRUCT__entry(
		__field(const struct mlx5_flow_rule *, rule)
		__field(const struct fs_fte *, fte)
	    ),
	    TP_fast_assign(
			   __entry->rule = rule;
			   fs_get_obj(__entry->fte, rule->node.parent);
	    ),
	    TP_printk("rule=%p fte=%p\n",
		      __entry->rule, __entry->fte)
	    );
#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE fs_tracepoint
#include <trace/define_trace.h>
