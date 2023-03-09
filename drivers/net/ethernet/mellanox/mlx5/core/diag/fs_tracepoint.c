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

#define CREATE_TRACE_POINTS

#include "fs_tracepoint.h"
#include <linux/stringify.h>

#define DECLARE_MASK_VAL(type, name) struct {type m; type v; } name
#define MASK_VAL(type, spec, name, mask, val, fld)	\
		DECLARE_MASK_VAL(type, name) =		\
			{.m = MLX5_GET(spec, mask, fld),\
			 .v = MLX5_GET(spec, val, fld)}
#define MASK_VAL_BE(type, spec, name, mask, val, fld)	\
		    DECLARE_MASK_VAL(type, name) =	\
			{.m = MLX5_GET_BE(type, spec, mask, fld),\
			 .v = MLX5_GET_BE(type, spec, val, fld)}
#define GET_MASKED_VAL(name) (name.m & name.v)

#define GET_MASK_VAL(name, type, mask, val, fld)	\
		(name.m = MLX5_GET(type, mask, fld),	\
		 name.v = MLX5_GET(type, val, fld),	\
		 name.m & name.v)
#define PRINT_MASKED_VAL(name, p, format) {		\
	if (name.m)			\
		trace_seq_printf(p, __stringify(name) "=" format " ", name.v); \
	}
#define PRINT_MASKED_VALP(name, cast, p, format) {	\
	if (name.m)			\
		trace_seq_printf(p, __stringify(name) "=" format " ",	       \
				 (cast)&name.v);\
	}

static void print_lyr_2_4_hdrs(struct trace_seq *p,
			       const u32 *mask, const u32 *value)
{
#define MASK_VAL_L2(type, name, fld) \
	MASK_VAL(type, fte_match_set_lyr_2_4, name, mask, value, fld)
	DECLARE_MASK_VAL(u64, smac) = {
		.m = MLX5_GET(fte_match_set_lyr_2_4, mask, smac_47_16) << 16 |
		     MLX5_GET(fte_match_set_lyr_2_4, mask, smac_15_0),
		.v = MLX5_GET(fte_match_set_lyr_2_4, value, smac_47_16) << 16 |
		     MLX5_GET(fte_match_set_lyr_2_4, value, smac_15_0)};
	DECLARE_MASK_VAL(u64, dmac) = {
		.m = MLX5_GET(fte_match_set_lyr_2_4, mask, dmac_47_16) << 16 |
		     MLX5_GET(fte_match_set_lyr_2_4, mask, dmac_15_0),
		.v = MLX5_GET(fte_match_set_lyr_2_4, value, dmac_47_16) << 16 |
		     MLX5_GET(fte_match_set_lyr_2_4, value, dmac_15_0)};
	MASK_VAL_L2(u16, ethertype, ethertype);
	MASK_VAL_L2(u8, ip_version, ip_version);

	PRINT_MASKED_VALP(smac, u8 *, p, "%pM");
	PRINT_MASKED_VALP(dmac, u8 *, p, "%pM");
	PRINT_MASKED_VAL(ethertype, p, "%04x");

	if ((ethertype.m == 0xffff && ethertype.v == ETH_P_IP) ||
	    (ip_version.m == 0xf && ip_version.v == 4)) {
#define MASK_VAL_L2_BE(type, name, fld) \
	MASK_VAL_BE(type, fte_match_set_lyr_2_4, name, mask, value, fld)
		MASK_VAL_L2_BE(u32, src_ipv4,
			       src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MASK_VAL_L2_BE(u32, dst_ipv4,
			       dst_ipv4_dst_ipv6.ipv4_layout.ipv4);

		PRINT_MASKED_VALP(src_ipv4, typeof(&src_ipv4.v), p,
				  "%pI4");
		PRINT_MASKED_VALP(dst_ipv4, typeof(&dst_ipv4.v), p,
				  "%pI4");
	} else if ((ethertype.m == 0xffff && ethertype.v == ETH_P_IPV6) ||
		   (ip_version.m == 0xf && ip_version.v == 6)) {
		static const struct in6_addr full_ones = {
			.in6_u.u6_addr32 = {__constant_htonl(0xffffffff),
					    __constant_htonl(0xffffffff),
					    __constant_htonl(0xffffffff),
					    __constant_htonl(0xffffffff)},
		};
		DECLARE_MASK_VAL(struct in6_addr, src_ipv6);
		DECLARE_MASK_VAL(struct in6_addr, dst_ipv6);

		memcpy(src_ipv6.m.in6_u.u6_addr8,
		       MLX5_ADDR_OF(fte_match_set_lyr_2_4, mask,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       sizeof(src_ipv6.m));
		memcpy(dst_ipv6.m.in6_u.u6_addr8,
		       MLX5_ADDR_OF(fte_match_set_lyr_2_4, mask,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       sizeof(dst_ipv6.m));
		memcpy(src_ipv6.v.in6_u.u6_addr8,
		       MLX5_ADDR_OF(fte_match_set_lyr_2_4, value,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       sizeof(src_ipv6.v));
		memcpy(dst_ipv6.v.in6_u.u6_addr8,
		       MLX5_ADDR_OF(fte_match_set_lyr_2_4, value,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       sizeof(dst_ipv6.v));

		if (!memcmp(&src_ipv6.m, &full_ones, sizeof(full_ones)))
			trace_seq_printf(p, "src_ipv6=%pI6 ",
					 &src_ipv6.v);
		if (!memcmp(&dst_ipv6.m, &full_ones, sizeof(full_ones)))
			trace_seq_printf(p, "dst_ipv6=%pI6 ",
					 &dst_ipv6.v);
	}

#define PRINT_MASKED_VAL_L2(type, name, fld, p, format) {\
	MASK_VAL_L2(type, name, fld);		         \
	PRINT_MASKED_VAL(name, p, format);		 \
}

	PRINT_MASKED_VAL_L2(u8, ip_protocol, ip_protocol, p, "%02x");
	PRINT_MASKED_VAL_L2(u16, tcp_flags, tcp_flags, p, "%x");
	PRINT_MASKED_VAL_L2(u16, tcp_sport, tcp_sport, p, "%u");
	PRINT_MASKED_VAL_L2(u16, tcp_dport, tcp_dport, p, "%u");
	PRINT_MASKED_VAL_L2(u16, udp_sport, udp_sport, p, "%u");
	PRINT_MASKED_VAL_L2(u16, udp_dport, udp_dport, p, "%u");
	PRINT_MASKED_VAL_L2(u16, first_vid, first_vid, p, "%04x");
	PRINT_MASKED_VAL_L2(u8, first_prio, first_prio, p, "%x");
	PRINT_MASKED_VAL_L2(u8, first_cfi, first_cfi, p, "%d");
	PRINT_MASKED_VAL_L2(u8, ip_dscp, ip_dscp, p, "%02x");
	PRINT_MASKED_VAL_L2(u8, ip_ecn, ip_ecn, p, "%x");
	PRINT_MASKED_VAL_L2(u8, cvlan_tag, cvlan_tag, p, "%d");
	PRINT_MASKED_VAL_L2(u8, svlan_tag, svlan_tag, p, "%d");
	PRINT_MASKED_VAL_L2(u8, frag, frag, p, "%d");
}

static void print_misc_parameters_hdrs(struct trace_seq *p,
				       const u32 *mask, const u32 *value)
{
#define MASK_VAL_MISC(type, name, fld) \
	MASK_VAL(type, fte_match_set_misc, name, mask, value, fld)
#define PRINT_MASKED_VAL_MISC(type, name, fld, p, format) {\
	MASK_VAL_MISC(type, name, fld);			   \
	PRINT_MASKED_VAL(name, p, format);		   \
}
	DECLARE_MASK_VAL(u64, gre_key) = {
		.m = MLX5_GET(fte_match_set_misc, mask, gre_key.nvgre.hi) << 8 |
		     MLX5_GET(fte_match_set_misc, mask, gre_key.nvgre.lo),
		.v = MLX5_GET(fte_match_set_misc, value, gre_key.nvgre.hi) << 8 |
		     MLX5_GET(fte_match_set_misc, value, gre_key.nvgre.lo)};

	PRINT_MASKED_VAL(gre_key, p, "%llu");
	PRINT_MASKED_VAL_MISC(u32, source_sqn, source_sqn, p, "%u");
	PRINT_MASKED_VAL_MISC(u16, source_port, source_port, p, "%u");
	PRINT_MASKED_VAL_MISC(u8, outer_second_prio, outer_second_prio,
			      p, "%u");
	PRINT_MASKED_VAL_MISC(u8, outer_second_cfi, outer_second_cfi, p, "%u");
	PRINT_MASKED_VAL_MISC(u16, outer_second_vid, outer_second_vid, p, "%u");
	PRINT_MASKED_VAL_MISC(u8, inner_second_prio, inner_second_prio,
			      p, "%u");
	PRINT_MASKED_VAL_MISC(u8, inner_second_cfi, inner_second_cfi, p, "%u");
	PRINT_MASKED_VAL_MISC(u16, inner_second_vid, inner_second_vid, p, "%u");

	PRINT_MASKED_VAL_MISC(u8, outer_second_cvlan_tag,
			      outer_second_cvlan_tag, p, "%u");
	PRINT_MASKED_VAL_MISC(u8, inner_second_cvlan_tag,
			      inner_second_cvlan_tag, p, "%u");
	PRINT_MASKED_VAL_MISC(u8, outer_second_svlan_tag,
			      outer_second_svlan_tag, p, "%u");
	PRINT_MASKED_VAL_MISC(u8, inner_second_svlan_tag,
			      inner_second_svlan_tag, p, "%u");

	PRINT_MASKED_VAL_MISC(u8, gre_protocol, gre_protocol, p, "%u");

	PRINT_MASKED_VAL_MISC(u32, vxlan_vni, vxlan_vni, p, "%u");
	PRINT_MASKED_VAL_MISC(u32, outer_ipv6_flow_label, outer_ipv6_flow_label,
			      p, "%x");
	PRINT_MASKED_VAL_MISC(u32, inner_ipv6_flow_label, inner_ipv6_flow_label,
			      p, "%x");
}

const char *parse_fs_hdrs(struct trace_seq *p,
			  u8 match_criteria_enable,
			  const u32 *mask_outer,
			  const u32 *mask_misc,
			  const u32 *mask_inner,
			  const u32 *value_outer,
			  const u32 *value_misc,
			  const u32 *value_inner)
{
	const char *ret = trace_seq_buffer_ptr(p);

	if (match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS) {
		trace_seq_printf(p, "[outer] ");
		print_lyr_2_4_hdrs(p, mask_outer, value_outer);
	}

	if (match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS) {
		trace_seq_printf(p, "[misc] ");
		print_misc_parameters_hdrs(p, mask_misc, value_misc);
	}
	if (match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_INNER_HEADERS) {
		trace_seq_printf(p, "[inner] ");
		print_lyr_2_4_hdrs(p, mask_inner, value_inner);
	}
	trace_seq_putc(p, 0);
	return ret;
}

static const char
*fs_dest_range_field_to_str(enum mlx5_flow_dest_range_field field)
{
	switch (field) {
	case MLX5_FLOW_DEST_RANGE_FIELD_PKT_LEN:
		return "packet len";
	default:
		return "unknown dest range field";
	}
}

const char *parse_fs_dst(struct trace_seq *p,
			 const struct mlx5_flow_destination *dst,
			 u32 counter_id)
{
	const char *ret = trace_seq_buffer_ptr(p);

	switch (dst->type) {
	case MLX5_FLOW_DESTINATION_TYPE_UPLINK:
		trace_seq_printf(p, "uplink\n");
		break;
	case MLX5_FLOW_DESTINATION_TYPE_VPORT:
		trace_seq_printf(p, "vport=%u\n", dst->vport.num);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
		trace_seq_printf(p, "ft=%p\n", dst->ft);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM:
		trace_seq_printf(p, "ft_num=%u\n", dst->ft_num);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_TIR:
		trace_seq_printf(p, "tir=%u\n", dst->tir_num);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER:
		trace_seq_printf(p, "sampler_id=%u\n", dst->sampler_id);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_COUNTER:
		trace_seq_printf(p, "counter_id=%u\n", counter_id);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_PORT:
		trace_seq_printf(p, "port\n");
		break;
	case MLX5_FLOW_DESTINATION_TYPE_RANGE:
		trace_seq_printf(p, "field=%s min=%d max=%d\n",
				 fs_dest_range_field_to_str(dst->range.field),
				 dst->range.min, dst->range.max);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE:
		trace_seq_printf(p, "flow_table_type=%u id:%u\n", dst->ft->type,
				 dst->ft->id);
		break;
	case MLX5_FLOW_DESTINATION_TYPE_NONE:
		trace_seq_printf(p, "none\n");
		break;
	}

	trace_seq_putc(p, 0);
	return ret;
}

EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_add_ft);
EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_del_ft);
EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_add_fg);
EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_del_fg);
EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_set_fte);
EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_del_fte);
EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_add_rule);
EXPORT_TRACEPOINT_SYMBOL(mlx5_fs_del_rule);

