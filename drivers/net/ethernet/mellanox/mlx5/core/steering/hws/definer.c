// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

/* Pattern tunnel Layer bits. */
#define MLX5_FLOW_LAYER_VXLAN      BIT(12)
#define MLX5_FLOW_LAYER_VXLAN_GPE  BIT(13)
#define MLX5_FLOW_LAYER_GRE        BIT(14)
#define MLX5_FLOW_LAYER_MPLS       BIT(15)

/* Pattern tunnel Layer bits (continued). */
#define MLX5_FLOW_LAYER_IPIP       BIT(23)
#define MLX5_FLOW_LAYER_IPV6_ENCAP BIT(24)
#define MLX5_FLOW_LAYER_NVGRE      BIT(25)
#define MLX5_FLOW_LAYER_GENEVE     BIT(26)

#define MLX5_FLOW_ITEM_FLEX_TUNNEL BIT_ULL(39)

/* Tunnel Masks. */
#define MLX5_FLOW_LAYER_TUNNEL \
	(MLX5_FLOW_LAYER_VXLAN | MLX5_FLOW_LAYER_VXLAN_GPE | \
	 MLX5_FLOW_LAYER_GRE | MLX5_FLOW_LAYER_NVGRE | MLX5_FLOW_LAYER_MPLS | \
	 MLX5_FLOW_LAYER_IPIP | MLX5_FLOW_LAYER_IPV6_ENCAP | \
	 MLX5_FLOW_LAYER_GENEVE | MLX5_FLOW_LAYER_GTP | \
	 MLX5_FLOW_ITEM_FLEX_TUNNEL)

#define GTP_PDU_SC	0x85
#define BAD_PORT	0xBAD
#define ETH_TYPE_IPV4_VXLAN	0x0800
#define ETH_TYPE_IPV6_VXLAN	0x86DD
#define UDP_GTPU_PORT	2152
#define UDP_PORT_MPLS	6635
#define UDP_GENEVE_PORT 6081
#define UDP_ROCEV2_PORT	4791
#define HWS_FLOW_LAYER_TUNNEL_NO_MPLS (MLX5_FLOW_LAYER_TUNNEL & ~MLX5_FLOW_LAYER_MPLS)

#define STE_NO_VLAN	0x0
#define STE_SVLAN	0x1
#define STE_CVLAN	0x2
#define STE_NO_L3	0x0
#define STE_IPV4	0x1
#define STE_IPV6	0x2
#define STE_NO_L4	0x0
#define STE_TCP		0x1
#define STE_UDP		0x2
#define STE_ICMP	0x3
#define STE_ESP		0x3

#define IPV4 0x4
#define IPV6 0x6

/* Setter function based on bit offset and mask, for 32bit DW */
#define _HWS_SET32(p, v, byte_off, bit_off, mask) \
	do { \
		u32 _v = v; \
		*((__be32 *)(p) + ((byte_off) / 4)) = \
		cpu_to_be32((be32_to_cpu(*((__be32 *)(p) + \
			     ((byte_off) / 4))) & \
			     (~((mask) << (bit_off)))) | \
			    (((_v) & (mask)) << \
			      (bit_off))); \
	} while (0)

/* Setter function based on bit offset and mask, for unaligned 32bit DW */
#define HWS_SET32(p, v, byte_off, bit_off, mask) \
	do { \
		if (unlikely((bit_off) < 0)) { \
			u32 _bit_off = -1 * (bit_off); \
			u32 second_dw_mask = (mask) & ((1 << _bit_off) - 1); \
			_HWS_SET32(p, (v) >> _bit_off, byte_off, 0, (mask) >> _bit_off); \
			_HWS_SET32(p, (v) & second_dw_mask, (byte_off) + DW_SIZE, \
				    (bit_off) % BITS_IN_DW, second_dw_mask); \
		} else { \
			_HWS_SET32(p, v, byte_off, (bit_off), (mask)); \
		} \
	} while (0)

/* Getter for up to aligned 32bit DW */
#define HWS_GET32(p, byte_off, bit_off, mask) \
	((be32_to_cpu(*((__be32 *)(p) + ((byte_off) / 4))) >> (bit_off)) & (mask))

#define HWS_CALC_FNAME(field, inner) \
	((inner) ? MLX5HWS_DEFINER_FNAME_##field##_I : \
		   MLX5HWS_DEFINER_FNAME_##field##_O)

#define HWS_GET_MATCH_PARAM(match_param, hdr) \
	MLX5_GET(fte_match_param, match_param, hdr)

#define HWS_IS_FLD_SET(match_param, hdr) \
	(!!(HWS_GET_MATCH_PARAM(match_param, hdr)))

#define HWS_IS_FLD_SET_DW_ARR(match_param, hdr, sz_in_bits) ({ \
		BUILD_BUG_ON((sz_in_bits) % 32); \
		u32 sz = sz_in_bits; \
		u32 res = 0; \
		u32 dw_off = __mlx5_dw_off(fte_match_param, hdr); \
		while (!res && sz >= 32) { \
			res = *((match_param) + (dw_off++)); \
			sz -= 32; \
		} \
		res; \
	})

#define HWS_IS_FLD_SET_SZ(match_param, hdr, sz_in_bits) \
	(((sz_in_bits) > 32) ? HWS_IS_FLD_SET_DW_ARR(match_param, hdr, sz_in_bits) : \
			       !!(HWS_GET_MATCH_PARAM(match_param, hdr)))

#define HWS_GET64_MATCH_PARAM(match_param, hdr) \
	MLX5_GET64(fte_match_param, match_param, hdr)

#define HWS_IS_FLD64_SET(match_param, hdr) \
	(!!(HWS_GET64_MATCH_PARAM(match_param, hdr)))

#define HWS_CALC_HDR_SRC(fc, s_hdr) \
	do { \
		(fc)->s_bit_mask = __mlx5_mask(fte_match_param, s_hdr); \
		(fc)->s_bit_off = __mlx5_dw_bit_off(fte_match_param, s_hdr); \
		(fc)->s_byte_off = MLX5_BYTE_OFF(fte_match_param, s_hdr); \
	} while (0)

#define HWS_CALC_HDR_DST(fc, d_hdr) \
	do { \
		(fc)->bit_mask = __mlx5_mask(definer_hl, d_hdr); \
		(fc)->bit_off = __mlx5_dw_bit_off(definer_hl, d_hdr); \
		(fc)->byte_off = MLX5_BYTE_OFF(definer_hl, d_hdr); \
	} while (0)

#define HWS_CALC_HDR(fc, s_hdr, d_hdr) \
	do { \
		HWS_CALC_HDR_SRC(fc, s_hdr); \
		HWS_CALC_HDR_DST(fc, d_hdr); \
		(fc)->tag_set = &hws_definer_generic_set; \
	} while (0)

#define HWS_SET_HDR(fc_arr, match_param, fname, s_hdr, d_hdr) \
	do { \
		if (HWS_IS_FLD_SET(match_param, s_hdr)) \
			HWS_CALC_HDR(&(fc_arr)[MLX5HWS_DEFINER_FNAME_##fname], s_hdr, d_hdr); \
	} while (0)

struct mlx5hws_definer_sel_ctrl {
	u8 allowed_full_dw; /* Full DW selectors cover all offsets */
	u8 allowed_lim_dw;  /* Limited DW selectors cover offset < 64 */
	u8 allowed_bytes;   /* Bytes selectors, up to offset 255 */
	u8 used_full_dw;
	u8 used_lim_dw;
	u8 used_bytes;
	u8 full_dw_selector[DW_SELECTORS];
	u8 lim_dw_selector[DW_SELECTORS_LIMITED];
	u8 byte_selector[BYTE_SELECTORS];
};

struct mlx5hws_definer_conv_data {
	struct mlx5hws_context *ctx;
	struct mlx5hws_definer_fc *fc;
	/* enum mlx5hws_definer_match_flag */
	u32 match_flags;
};

static void
hws_definer_ones_set(struct mlx5hws_definer_fc *fc,
		     void *match_param,
		     u8 *tag)
{
	HWS_SET32(tag, -1, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_generic_set(struct mlx5hws_definer_fc *fc,
			void *match_param,
			u8 *tag)
{
	/* Can be optimized */
	u32 val = HWS_GET32(match_param, fc->s_byte_off, fc->s_bit_off, fc->s_bit_mask);

	HWS_SET32(tag, val, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_outer_vlan_type_set(struct mlx5hws_definer_fc *fc,
				void *match_param,
				u8 *tag)
{
	if (HWS_GET_MATCH_PARAM(match_param, outer_headers.cvlan_tag))
		HWS_SET32(tag, STE_CVLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
	else if (HWS_GET_MATCH_PARAM(match_param, outer_headers.svlan_tag))
		HWS_SET32(tag, STE_SVLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
	else
		HWS_SET32(tag, STE_NO_VLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_inner_vlan_type_set(struct mlx5hws_definer_fc *fc,
				void *match_param,
				u8 *tag)
{
	if (HWS_GET_MATCH_PARAM(match_param, inner_headers.cvlan_tag))
		HWS_SET32(tag, STE_CVLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
	else if (HWS_GET_MATCH_PARAM(match_param, inner_headers.svlan_tag))
		HWS_SET32(tag, STE_SVLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
	else
		HWS_SET32(tag, STE_NO_VLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_second_vlan_type_set(struct mlx5hws_definer_fc *fc,
				 void *match_param,
				 u8 *tag,
				 bool inner)
{
	u32 second_cvlan_tag = inner ?
		HWS_GET_MATCH_PARAM(match_param, misc_parameters.inner_second_cvlan_tag) :
		HWS_GET_MATCH_PARAM(match_param, misc_parameters.outer_second_cvlan_tag);
	u32 second_svlan_tag = inner ?
		HWS_GET_MATCH_PARAM(match_param, misc_parameters.inner_second_svlan_tag) :
		HWS_GET_MATCH_PARAM(match_param, misc_parameters.outer_second_svlan_tag);

	if (second_cvlan_tag)
		HWS_SET32(tag, STE_CVLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
	else if (second_svlan_tag)
		HWS_SET32(tag, STE_SVLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
	else
		HWS_SET32(tag, STE_NO_VLAN, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_inner_second_vlan_type_set(struct mlx5hws_definer_fc *fc,
				       void *match_param,
				       u8 *tag)
{
	hws_definer_second_vlan_type_set(fc, match_param, tag, true);
}

static void
hws_definer_outer_second_vlan_type_set(struct mlx5hws_definer_fc *fc,
				       void *match_param,
				       u8 *tag)
{
	hws_definer_second_vlan_type_set(fc, match_param, tag, false);
}

static void hws_definer_icmp_dw1_set(struct mlx5hws_definer_fc *fc,
				     void *match_param,
				     u8 *tag)
{
	u32 code = HWS_GET_MATCH_PARAM(match_param, misc_parameters_3.icmp_code);
	u32 type = HWS_GET_MATCH_PARAM(match_param, misc_parameters_3.icmp_type);
	u32 dw = (type << __mlx5_dw_bit_off(header_icmp, type)) |
		 (code << __mlx5_dw_bit_off(header_icmp, code));

	HWS_SET32(tag, dw, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_icmpv6_dw1_set(struct mlx5hws_definer_fc *fc,
			   void *match_param,
			   u8 *tag)
{
	u32 code = HWS_GET_MATCH_PARAM(match_param, misc_parameters_3.icmpv6_code);
	u32 type = HWS_GET_MATCH_PARAM(match_param, misc_parameters_3.icmpv6_type);
	u32 dw = (type << __mlx5_dw_bit_off(header_icmp, type)) |
		 (code << __mlx5_dw_bit_off(header_icmp, code));

	HWS_SET32(tag, dw, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_l3_type_set(struct mlx5hws_definer_fc *fc,
			void *match_param,
			u8 *tag)
{
	u32 val = HWS_GET32(match_param, fc->s_byte_off, fc->s_bit_off, fc->s_bit_mask);

	if (val == IPV4)
		HWS_SET32(tag, STE_IPV4, fc->byte_off, fc->bit_off, fc->bit_mask);
	else if (val == IPV6)
		HWS_SET32(tag, STE_IPV6, fc->byte_off, fc->bit_off, fc->bit_mask);
	else
		HWS_SET32(tag, STE_NO_L3, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_set_source_port_gvmi(struct mlx5hws_definer_fc *fc,
				 void *match_param,
				 u8 *tag,
				 struct mlx5hws_context *peer_ctx)
{
	u16 source_port = HWS_GET_MATCH_PARAM(match_param, misc_parameters.source_port);
	u16 vport_gvmi = 0;
	int ret;

	ret = mlx5hws_vport_get_gvmi(peer_ctx, source_port, &vport_gvmi);
	if (ret) {
		HWS_SET32(tag, BAD_PORT, fc->byte_off, fc->bit_off, fc->bit_mask);
		mlx5hws_err(fc->ctx, "Vport 0x%x is disabled or invalid\n", source_port);
		return;
	}

	if (vport_gvmi)
		HWS_SET32(tag, vport_gvmi, fc->byte_off, fc->bit_off, fc->bit_mask);
}

static void
hws_definer_set_source_gvmi_vhca_id(struct mlx5hws_definer_fc *fc,
				    void *match_param,
				    u8 *tag)
__must_hold(&fc->ctx->ctrl_lock)
{
	int id = HWS_GET_MATCH_PARAM(match_param, misc_parameters.source_eswitch_owner_vhca_id);
	struct mlx5hws_context *peer_ctx;

	if (id == fc->ctx->caps->vhca_id)
		peer_ctx = fc->ctx;
	else
		peer_ctx = xa_load(&fc->ctx->peer_ctx_xa, id);

	if (!peer_ctx) {
		HWS_SET32(tag, BAD_PORT, fc->byte_off, fc->bit_off, fc->bit_mask);
		mlx5hws_err(fc->ctx, "Invalid vhca_id provided 0x%x\n", id);
		return;
	}

	hws_definer_set_source_port_gvmi(fc, match_param, tag, peer_ctx);
}

static void
hws_definer_set_source_gvmi(struct mlx5hws_definer_fc *fc,
			    void *match_param,
			    u8 *tag)
{
	hws_definer_set_source_port_gvmi(fc, match_param, tag, fc->ctx);
}

static struct mlx5hws_definer_fc *
hws_definer_flex_parser_steering_ok_bits_handler(struct mlx5hws_definer_conv_data *cd,
						 u8 parser_id)
{
	struct mlx5hws_definer_fc *fc;

	switch (parser_id) {
	case 0:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER0_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser0_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 1:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER1_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser1_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 2:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER2_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser2_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 3:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER3_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser3_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 4:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER4_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser4_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 5:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER5_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser5_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 6:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER6_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser6_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 7:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER7_OK];
		HWS_CALC_HDR_DST(fc, oks1.flex_parser7_steering_ok);
		fc->tag_set = &hws_definer_generic_set;
		break;
	default:
		mlx5hws_err(cd->ctx, "Unsupported flex parser steering ok index %u\n", parser_id);
		return NULL;
	}

	return fc;
}

static struct mlx5hws_definer_fc *
hws_definer_flex_parser_handler(struct mlx5hws_definer_conv_data *cd,
				u8 parser_id)
{
	struct mlx5hws_definer_fc *fc;

	switch (parser_id) {
	case 0:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_0];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_0);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 1:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_1];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_1);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 2:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_2];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_2);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 3:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_3];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_3);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 4:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_4];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_4);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 5:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_5];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_5);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 6:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_6];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_6);
		fc->tag_set = &hws_definer_generic_set;
		break;
	case 7:
		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_7];
		HWS_CALC_HDR_DST(fc, flex_parser.flex_parser_7);
		fc->tag_set = &hws_definer_generic_set;
		break;
	default:
		mlx5hws_err(cd->ctx, "Unsupported flex parser %u\n", parser_id);
		return NULL;
	}

	return fc;
}

static struct mlx5hws_definer_fc *
hws_definer_misc4_fields_handler(struct mlx5hws_definer_conv_data *cd,
				 bool *parser_is_used,
				 u32 id,
				 u32 value)
{
	if (id || value) {
		if (id >= HWS_NUM_OF_FLEX_PARSERS) {
			mlx5hws_err(cd->ctx, "Unsupported parser id\n");
			return NULL;
		}

		if (parser_is_used[id]) {
			mlx5hws_err(cd->ctx, "Parser id have been used\n");
			return NULL;
		}
	}

	parser_is_used[id] = true;

	return hws_definer_flex_parser_handler(cd, id);
}

static int
hws_definer_check_match_flags(struct mlx5hws_definer_conv_data *cd)
{
	u32 flags;

	flags = cd->match_flags & (MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN_GPE |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_HEADER_0_1);
	if (flags & (flags - 1))
		goto err_conflict;

	flags = cd->match_flags & (MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE_OPT_KEY |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_HEADER_2);

	if (flags & (flags - 1))
		goto err_conflict;

	flags = cd->match_flags & (MLX5HWS_DEFINER_MATCH_FLAG_TNL_MPLS_OVER_GRE |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_MPLS_OVER_UDP);
	if (flags & (flags - 1))
		goto err_conflict;

	flags = cd->match_flags & (MLX5HWS_DEFINER_MATCH_FLAG_ICMPV4 |
				   MLX5HWS_DEFINER_MATCH_FLAG_ICMPV6 |
				   MLX5HWS_DEFINER_MATCH_FLAG_TCP_O |
				   MLX5HWS_DEFINER_MATCH_FLAG_TCP_I);
	if (flags & (flags - 1))
		goto err_conflict;

	return 0;

err_conflict:
	mlx5hws_err(cd->ctx, "Invalid definer fields combination\n");
	return -EINVAL;
}

static int
hws_definer_conv_outer(struct mlx5hws_definer_conv_data *cd,
		       u32 *match_param)
{
	bool is_s_ipv6, is_d_ipv6, smac_set, dmac_set;
	struct mlx5hws_definer_fc *fc = cd->fc;
	struct mlx5hws_definer_fc *curr_fc;
	u32 *s_ipv6, *d_ipv6;

	if (HWS_IS_FLD_SET_SZ(match_param, outer_headers.l4_type, 0x2) ||
	    HWS_IS_FLD_SET_SZ(match_param, outer_headers.reserved_at_c2, 0xe) ||
	    HWS_IS_FLD_SET_SZ(match_param, outer_headers.reserved_at_c4, 0x4)) {
		mlx5hws_err(cd->ctx, "Unsupported outer parameters set\n");
		return -EINVAL;
	}

	/* L2 Check ethertype */
	HWS_SET_HDR(fc, match_param, ETH_TYPE_O,
		    outer_headers.ethertype,
		    eth_l2_outer.l3_ethertype);
	/* L2 Check SMAC 47_16 */
	HWS_SET_HDR(fc, match_param, ETH_SMAC_47_16_O,
		    outer_headers.smac_47_16, eth_l2_src_outer.smac_47_16);
	/* L2 Check SMAC 15_0 */
	HWS_SET_HDR(fc, match_param, ETH_SMAC_15_0_O,
		    outer_headers.smac_15_0, eth_l2_src_outer.smac_15_0);
	/* L2 Check DMAC 47_16 */
	HWS_SET_HDR(fc, match_param, ETH_DMAC_47_16_O,
		    outer_headers.dmac_47_16, eth_l2_outer.dmac_47_16);
	/* L2 Check DMAC 15_0 */
	HWS_SET_HDR(fc, match_param, ETH_DMAC_15_0_O,
		    outer_headers.dmac_15_0, eth_l2_outer.dmac_15_0);

	/* L2 VLAN */
	HWS_SET_HDR(fc, match_param, VLAN_FIRST_PRIO_O,
		    outer_headers.first_prio, eth_l2_outer.first_priority);
	HWS_SET_HDR(fc, match_param, VLAN_CFI_O,
		    outer_headers.first_cfi, eth_l2_outer.first_cfi);
	HWS_SET_HDR(fc, match_param, VLAN_ID_O,
		    outer_headers.first_vid, eth_l2_outer.first_vlan_id);

	/* L2 CVLAN and SVLAN */
	if (HWS_GET_MATCH_PARAM(match_param, outer_headers.cvlan_tag) ||
	    HWS_GET_MATCH_PARAM(match_param, outer_headers.svlan_tag)) {
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VLAN_TYPE_O];
		HWS_CALC_HDR_DST(curr_fc, eth_l2_outer.first_vlan_qualifier);
		curr_fc->tag_set = &hws_definer_outer_vlan_type_set;
		curr_fc->tag_mask_set = &hws_definer_ones_set;
	}

	/* L3 Check IP header */
	HWS_SET_HDR(fc, match_param, IP_PROTOCOL_O,
		    outer_headers.ip_protocol,
		    eth_l3_outer.protocol_next_header);
	HWS_SET_HDR(fc, match_param, IP_TTL_O,
		    outer_headers.ttl_hoplimit,
		    eth_l3_outer.time_to_live_hop_limit);

	/* L3 Check IPv4/IPv6 addresses */
	s_ipv6 = MLX5_ADDR_OF(fte_match_param, match_param,
			      outer_headers.src_ipv4_src_ipv6.ipv6_layout);
	d_ipv6 = MLX5_ADDR_OF(fte_match_param, match_param,
			      outer_headers.dst_ipv4_dst_ipv6.ipv6_layout);

	/* Assume IPv6 is used if ipv6 bits are set */
	is_s_ipv6 = s_ipv6[0] || s_ipv6[1] || s_ipv6[2];
	is_d_ipv6 = d_ipv6[0] || d_ipv6[1] || d_ipv6[2];

	if (is_s_ipv6) {
		/* Handle IPv6 source address */
		HWS_SET_HDR(fc, match_param, IPV6_SRC_127_96_O,
			    outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_127_96,
			    ipv6_src_outer.ipv6_address_127_96);
		HWS_SET_HDR(fc, match_param, IPV6_SRC_95_64_O,
			    outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_95_64,
			    ipv6_src_outer.ipv6_address_95_64);
		HWS_SET_HDR(fc, match_param, IPV6_SRC_63_32_O,
			    outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_63_32,
			    ipv6_src_outer.ipv6_address_63_32);
		HWS_SET_HDR(fc, match_param, IPV6_SRC_31_0_O,
			    outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv6_src_outer.ipv6_address_31_0);
	} else {
		/* Handle IPv4 source address */
		HWS_SET_HDR(fc, match_param, IPV4_SRC_O,
			    outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv4_src_dest_outer.source_address);
	}
	if (is_d_ipv6) {
		/* Handle IPv6 destination address */
		HWS_SET_HDR(fc, match_param, IPV6_DST_127_96_O,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_127_96,
			    ipv6_dst_outer.ipv6_address_127_96);
		HWS_SET_HDR(fc, match_param, IPV6_DST_95_64_O,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_95_64,
			    ipv6_dst_outer.ipv6_address_95_64);
		HWS_SET_HDR(fc, match_param, IPV6_DST_63_32_O,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_63_32,
			    ipv6_dst_outer.ipv6_address_63_32);
		HWS_SET_HDR(fc, match_param, IPV6_DST_31_0_O,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv6_dst_outer.ipv6_address_31_0);
	} else {
		/* Handle IPv4 destination address */
		HWS_SET_HDR(fc, match_param, IPV4_DST_O,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv4_src_dest_outer.destination_address);
	}

	/* L4 Handle TCP/UDP */
	HWS_SET_HDR(fc, match_param, L4_SPORT_O,
		    outer_headers.tcp_sport, eth_l4_outer.source_port);
	HWS_SET_HDR(fc, match_param, L4_DPORT_O,
		    outer_headers.tcp_dport, eth_l4_outer.destination_port);
	HWS_SET_HDR(fc, match_param, L4_SPORT_O,
		    outer_headers.udp_sport, eth_l4_outer.source_port);
	HWS_SET_HDR(fc, match_param, L4_DPORT_O,
		    outer_headers.udp_dport, eth_l4_outer.destination_port);
	HWS_SET_HDR(fc, match_param, TCP_FLAGS_O,
		    outer_headers.tcp_flags, eth_l4_outer.tcp_flags);

	/* L3 Handle DSCP, ECN and IHL  */
	HWS_SET_HDR(fc, match_param, IP_DSCP_O,
		    outer_headers.ip_dscp, eth_l3_outer.dscp);
	HWS_SET_HDR(fc, match_param, IP_ECN_O,
		    outer_headers.ip_ecn, eth_l3_outer.ecn);
	HWS_SET_HDR(fc, match_param, IPV4_IHL_O,
		    outer_headers.ipv4_ihl, eth_l3_outer.ihl);

	/* Set IP fragmented bit */
	if (HWS_IS_FLD_SET(match_param, outer_headers.frag)) {
		smac_set = HWS_IS_FLD_SET(match_param, outer_headers.smac_15_0) ||
				HWS_IS_FLD_SET(match_param, outer_headers.smac_47_16);
		dmac_set = HWS_IS_FLD_SET(match_param, outer_headers.dmac_15_0) ||
				HWS_IS_FLD_SET(match_param, outer_headers.dmac_47_16);
		if (smac_set == dmac_set) {
			HWS_SET_HDR(fc, match_param, IP_FRAG_O,
				    outer_headers.frag, eth_l4_outer.ip_fragmented);
		} else {
			HWS_SET_HDR(fc, match_param, IP_FRAG_O,
				    outer_headers.frag, eth_l2_src_outer.ip_fragmented);
		}
	}

	/* L3_type set */
	if (HWS_IS_FLD_SET(match_param, outer_headers.ip_version)) {
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_O];
		HWS_CALC_HDR_DST(curr_fc, eth_l2_outer.l3_type);
		curr_fc->tag_set = &hws_definer_l3_type_set;
		curr_fc->tag_mask_set = &hws_definer_ones_set;
		HWS_CALC_HDR_SRC(curr_fc, outer_headers.ip_version);
	}

	return 0;
}

static int
hws_definer_conv_inner(struct mlx5hws_definer_conv_data *cd,
		       u32 *match_param)
{
	bool is_s_ipv6, is_d_ipv6, smac_set, dmac_set;
	struct mlx5hws_definer_fc *fc = cd->fc;
	struct mlx5hws_definer_fc *curr_fc;
	u32 *s_ipv6, *d_ipv6;

	if (HWS_IS_FLD_SET_SZ(match_param, inner_headers.l4_type, 0x2) ||
	    HWS_IS_FLD_SET_SZ(match_param, inner_headers.reserved_at_c2, 0xe) ||
	    HWS_IS_FLD_SET_SZ(match_param, inner_headers.reserved_at_c4, 0x4)) {
		mlx5hws_err(cd->ctx, "Unsupported inner parameters set\n");
		return -EINVAL;
	}

	/* L2 Check ethertype */
	HWS_SET_HDR(fc, match_param, ETH_TYPE_I,
		    inner_headers.ethertype,
		    eth_l2_inner.l3_ethertype);
	/* L2 Check SMAC 47_16 */
	HWS_SET_HDR(fc, match_param, ETH_SMAC_47_16_I,
		    inner_headers.smac_47_16, eth_l2_src_inner.smac_47_16);
	/* L2 Check SMAC 15_0 */
	HWS_SET_HDR(fc, match_param, ETH_SMAC_15_0_I,
		    inner_headers.smac_15_0, eth_l2_src_inner.smac_15_0);
	/* L2 Check DMAC 47_16 */
	HWS_SET_HDR(fc, match_param, ETH_DMAC_47_16_I,
		    inner_headers.dmac_47_16, eth_l2_inner.dmac_47_16);
	/* L2 Check DMAC 15_0 */
	HWS_SET_HDR(fc, match_param, ETH_DMAC_15_0_I,
		    inner_headers.dmac_15_0, eth_l2_inner.dmac_15_0);

	/* L2 VLAN */
	HWS_SET_HDR(fc, match_param, VLAN_FIRST_PRIO_I,
		    inner_headers.first_prio, eth_l2_inner.first_priority);
	HWS_SET_HDR(fc, match_param, VLAN_CFI_I,
		    inner_headers.first_cfi, eth_l2_inner.first_cfi);
	HWS_SET_HDR(fc, match_param, VLAN_ID_I,
		    inner_headers.first_vid, eth_l2_inner.first_vlan_id);

	/* L2 CVLAN and SVLAN */
	if (HWS_GET_MATCH_PARAM(match_param, inner_headers.cvlan_tag) ||
	    HWS_GET_MATCH_PARAM(match_param, inner_headers.svlan_tag)) {
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VLAN_TYPE_I];
		HWS_CALC_HDR_DST(curr_fc, eth_l2_inner.first_vlan_qualifier);
		curr_fc->tag_set = &hws_definer_inner_vlan_type_set;
		curr_fc->tag_mask_set = &hws_definer_ones_set;
	}
	/* L3 Check IP header */
	HWS_SET_HDR(fc, match_param, IP_PROTOCOL_I,
		    inner_headers.ip_protocol,
		    eth_l3_inner.protocol_next_header);
	HWS_SET_HDR(fc, match_param, IP_VERSION_I,
		    inner_headers.ip_version,
		    eth_l3_inner.ip_version);
	HWS_SET_HDR(fc, match_param, IP_TTL_I,
		    inner_headers.ttl_hoplimit,
		    eth_l3_inner.time_to_live_hop_limit);

	/* L3 Check IPv4/IPv6 addresses */
	s_ipv6 = MLX5_ADDR_OF(fte_match_param, match_param,
			      inner_headers.src_ipv4_src_ipv6.ipv6_layout);
	d_ipv6 = MLX5_ADDR_OF(fte_match_param, match_param,
			      inner_headers.dst_ipv4_dst_ipv6.ipv6_layout);

	/* Assume IPv6 is used if ipv6 bits are set */
	is_s_ipv6 = s_ipv6[0] || s_ipv6[1] || s_ipv6[2];
	is_d_ipv6 = d_ipv6[0] || d_ipv6[1] || d_ipv6[2];

	if (is_s_ipv6) {
		/* Handle IPv6 source address */
		HWS_SET_HDR(fc, match_param, IPV6_SRC_127_96_I,
			    inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_127_96,
			    ipv6_src_inner.ipv6_address_127_96);
		HWS_SET_HDR(fc, match_param, IPV6_SRC_95_64_I,
			    inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_95_64,
			    ipv6_src_inner.ipv6_address_95_64);
		HWS_SET_HDR(fc, match_param, IPV6_SRC_63_32_I,
			    inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_63_32,
			    ipv6_src_inner.ipv6_address_63_32);
		HWS_SET_HDR(fc, match_param, IPV6_SRC_31_0_I,
			    inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv6_src_inner.ipv6_address_31_0);
	} else {
		/* Handle IPv4 source address */
		HWS_SET_HDR(fc, match_param, IPV4_SRC_I,
			    inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv4_src_dest_inner.source_address);
	}
	if (is_d_ipv6) {
		/* Handle IPv6 destination address */
		HWS_SET_HDR(fc, match_param, IPV6_DST_127_96_I,
			    inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_127_96,
			    ipv6_dst_inner.ipv6_address_127_96);
		HWS_SET_HDR(fc, match_param, IPV6_DST_95_64_I,
			    inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_95_64,
			    ipv6_dst_inner.ipv6_address_95_64);
		HWS_SET_HDR(fc, match_param, IPV6_DST_63_32_I,
			    inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_63_32,
			    ipv6_dst_inner.ipv6_address_63_32);
		HWS_SET_HDR(fc, match_param, IPV6_DST_31_0_I,
			    inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv6_dst_inner.ipv6_address_31_0);
	} else {
		/* Handle IPv4 destination address */
		HWS_SET_HDR(fc, match_param, IPV4_DST_I,
			    inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0,
			    ipv4_src_dest_inner.destination_address);
	}

	/* L4 Handle TCP/UDP */
	HWS_SET_HDR(fc, match_param, L4_SPORT_I,
		    inner_headers.tcp_sport, eth_l4_inner.source_port);
	HWS_SET_HDR(fc, match_param, L4_DPORT_I,
		    inner_headers.tcp_dport, eth_l4_inner.destination_port);
	HWS_SET_HDR(fc, match_param, L4_SPORT_I,
		    inner_headers.udp_sport, eth_l4_inner.source_port);
	HWS_SET_HDR(fc, match_param, L4_DPORT_I,
		    inner_headers.udp_dport, eth_l4_inner.destination_port);
	HWS_SET_HDR(fc, match_param, TCP_FLAGS_I,
		    inner_headers.tcp_flags, eth_l4_inner.tcp_flags);

	/* L3 Handle DSCP, ECN and IHL  */
	HWS_SET_HDR(fc, match_param, IP_DSCP_I,
		    inner_headers.ip_dscp, eth_l3_inner.dscp);
	HWS_SET_HDR(fc, match_param, IP_ECN_I,
		    inner_headers.ip_ecn, eth_l3_inner.ecn);
	HWS_SET_HDR(fc, match_param, IPV4_IHL_I,
		    inner_headers.ipv4_ihl, eth_l3_inner.ihl);

	/* Set IP fragmented bit */
	if (HWS_IS_FLD_SET(match_param, inner_headers.frag)) {
		if (HWS_IS_FLD_SET(match_param, misc_parameters.vxlan_vni)) {
			HWS_SET_HDR(fc, match_param, IP_FRAG_I,
				    inner_headers.frag, eth_l2_inner.ip_fragmented);
		} else {
			smac_set = HWS_IS_FLD_SET(match_param, inner_headers.smac_15_0) ||
				   HWS_IS_FLD_SET(match_param, inner_headers.smac_47_16);
			dmac_set = HWS_IS_FLD_SET(match_param, inner_headers.dmac_15_0) ||
				   HWS_IS_FLD_SET(match_param, inner_headers.dmac_47_16);
			if (smac_set == dmac_set) {
				HWS_SET_HDR(fc, match_param, IP_FRAG_I,
					    inner_headers.frag, eth_l4_inner.ip_fragmented);
			} else {
				HWS_SET_HDR(fc, match_param, IP_FRAG_I,
					    inner_headers.frag, eth_l2_src_inner.ip_fragmented);
			}
		}
	}

	/* L3_type set */
	if (HWS_IS_FLD_SET(match_param, inner_headers.ip_version)) {
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_I];
		HWS_CALC_HDR_DST(curr_fc, eth_l2_inner.l3_type);
		curr_fc->tag_set = &hws_definer_l3_type_set;
		curr_fc->tag_mask_set = &hws_definer_ones_set;
		HWS_CALC_HDR_SRC(curr_fc, inner_headers.ip_version);
	}

	return 0;
}

static int
hws_definer_conv_misc(struct mlx5hws_definer_conv_data *cd,
		      u32 *match_param)
{
	struct mlx5hws_cmd_query_caps *caps = cd->ctx->caps;
	struct mlx5hws_definer_fc *fc = cd->fc;
	struct mlx5hws_definer_fc *curr_fc;

	if (HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_1, 0x1) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_64, 0xc) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_d8, 0x6) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_e0, 0xc) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_100, 0xc) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_120, 0xa) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_140, 0x8) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters.bth_dst_qp) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters.bth_opcode) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters.inner_esp_spi) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters.outer_esp_spi) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters.source_vhca_port) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters.reserved_at_1a0, 0x60)) {
		mlx5hws_err(cd->ctx, "Unsupported misc parameters set\n");
		return -EINVAL;
	}

	/* Check GRE related fields */
	if (HWS_IS_FLD_SET(match_param, misc_parameters.gre_c_present)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GRE_C];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.gre_c_present,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_gre, gre_c_present);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_gre, gre_c_present);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters.gre_k_present)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GRE_K];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.gre_k_present,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_gre, gre_k_present);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_gre, gre_k_present);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters.gre_s_present)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GRE_S];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.gre_s_present,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_gre, gre_s_present);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_gre, gre_s_present);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters.gre_protocol)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GRE_PROTOCOL];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.gre_protocol,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_gre, gre_protocol);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_gre, gre_protocol);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters.gre_key.key)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE |
				   MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE_OPT_KEY;
		HWS_SET_HDR(fc, match_param, GRE_OPT_KEY,
			    misc_parameters.gre_key.key, tunnel_header.tunnel_header_2);
	}

	/* Check GENEVE related fields */
	if (HWS_IS_FLD_SET(match_param, misc_parameters.geneve_vni)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GENEVE_VNI];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.geneve_vni,
			     tunnel_header.tunnel_header_1);
		curr_fc->bit_mask = __mlx5_mask(header_geneve, vni);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_geneve, vni);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters.geneve_opt_len)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GENEVE_OPT_LEN];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.geneve_opt_len,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_geneve, opt_len);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_geneve, opt_len);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters.geneve_protocol_type)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GENEVE_PROTO];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.geneve_protocol_type,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_geneve, protocol_type);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_geneve, protocol_type);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters.geneve_oam)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GENEVE_OAM];
		HWS_CALC_HDR(curr_fc,
			     misc_parameters.geneve_oam,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_geneve, o_flag);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_geneve, o_flag);
	}

	HWS_SET_HDR(fc, match_param, SOURCE_QP,
		    misc_parameters.source_sqn, source_qp_gvmi.source_qp);
	HWS_SET_HDR(fc, match_param, IPV6_FLOW_LABEL_O,
		    misc_parameters.outer_ipv6_flow_label, eth_l3_outer.flow_label);
	HWS_SET_HDR(fc, match_param, IPV6_FLOW_LABEL_I,
		    misc_parameters.inner_ipv6_flow_label, eth_l3_inner.flow_label);

	/* L2 Second VLAN */
	HWS_SET_HDR(fc, match_param, VLAN_SECOND_PRIO_O,
		    misc_parameters.outer_second_prio, eth_l2_outer.second_priority);
	HWS_SET_HDR(fc, match_param, VLAN_SECOND_PRIO_I,
		    misc_parameters.inner_second_prio, eth_l2_inner.second_priority);
	HWS_SET_HDR(fc, match_param, VLAN_SECOND_CFI_O,
		    misc_parameters.outer_second_cfi, eth_l2_outer.second_cfi);
	HWS_SET_HDR(fc, match_param, VLAN_SECOND_CFI_I,
		    misc_parameters.inner_second_cfi, eth_l2_inner.second_cfi);
	HWS_SET_HDR(fc, match_param, VLAN_SECOND_ID_O,
		    misc_parameters.outer_second_vid, eth_l2_outer.second_vlan_id);
	HWS_SET_HDR(fc, match_param, VLAN_SECOND_ID_I,
		    misc_parameters.inner_second_vid, eth_l2_inner.second_vlan_id);

	/* L2 Second CVLAN and SVLAN */
	if (HWS_GET_MATCH_PARAM(match_param, misc_parameters.outer_second_cvlan_tag) ||
	    HWS_GET_MATCH_PARAM(match_param, misc_parameters.outer_second_svlan_tag)) {
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VLAN_SECOND_TYPE_O];
		HWS_CALC_HDR_DST(curr_fc, eth_l2_outer.second_vlan_qualifier);
		curr_fc->tag_set = &hws_definer_outer_second_vlan_type_set;
		curr_fc->tag_mask_set = &hws_definer_ones_set;
	}

	if (HWS_GET_MATCH_PARAM(match_param, misc_parameters.inner_second_cvlan_tag) ||
	    HWS_GET_MATCH_PARAM(match_param, misc_parameters.inner_second_svlan_tag)) {
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VLAN_SECOND_TYPE_I];
		HWS_CALC_HDR_DST(curr_fc, eth_l2_inner.second_vlan_qualifier);
		curr_fc->tag_set = &hws_definer_inner_second_vlan_type_set;
		curr_fc->tag_mask_set = &hws_definer_ones_set;
	}

	/* VXLAN VNI  */
	if (HWS_GET_MATCH_PARAM(match_param, misc_parameters.vxlan_vni)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN;
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VXLAN_VNI];
		HWS_CALC_HDR(curr_fc, misc_parameters.vxlan_vni, tunnel_header.tunnel_header_1);
		curr_fc->bit_mask = __mlx5_mask(header_vxlan, vni);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_vxlan, vni);
	}

	/* Flex protocol steering ok bits */
	if (HWS_GET_MATCH_PARAM(match_param, misc_parameters.geneve_tlv_option_0_exist)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE;

		if (!caps->flex_parser_ok_bits_supp) {
			mlx5hws_err(cd->ctx, "Unsupported flex_parser_ok_bits_supp capability\n");
			return -EOPNOTSUPP;
		}

		curr_fc = hws_definer_flex_parser_steering_ok_bits_handler(
				cd, caps->flex_parser_id_geneve_tlv_option_0);
		if (!curr_fc)
			return -EINVAL;

		HWS_CALC_HDR_SRC(fc, misc_parameters.geneve_tlv_option_0_exist);
	}

	if (HWS_GET_MATCH_PARAM(match_param, misc_parameters.source_port)) {
		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_SOURCE_GVMI];
		HWS_CALC_HDR_DST(curr_fc, source_qp_gvmi.source_gvmi);
		curr_fc->tag_mask_set = &hws_definer_ones_set;
		curr_fc->tag_set = HWS_IS_FLD_SET(match_param,
						  misc_parameters.source_eswitch_owner_vhca_id) ?
						  &hws_definer_set_source_gvmi_vhca_id :
						  &hws_definer_set_source_gvmi;
	} else {
		if (HWS_IS_FLD_SET(match_param, misc_parameters.source_eswitch_owner_vhca_id)) {
			mlx5hws_err(cd->ctx,
				    "Unsupported source_eswitch_owner_vhca_id field usage\n");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int
hws_definer_conv_misc2(struct mlx5hws_definer_conv_data *cd,
		       u32 *match_param)
{
	struct mlx5hws_cmd_query_caps *caps = cd->ctx->caps;
	struct mlx5hws_definer_fc *fc = cd->fc;
	struct mlx5hws_definer_fc *curr_fc;

	if (HWS_IS_FLD_SET_SZ(match_param, misc_parameters_2.reserved_at_1a0, 0x8) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters_2.reserved_at_1b8, 0x8) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters_2.reserved_at_1c0, 0x40) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_2.macsec_syndrome) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_2.ipsec_syndrome)) {
		mlx5hws_err(cd->ctx, "Unsupported misc2 parameters set\n");
		return -EINVAL;
	}

	HWS_SET_HDR(fc, match_param, MPLS0_O,
		    misc_parameters_2.outer_first_mpls, mpls_outer.mpls0_label);
	HWS_SET_HDR(fc, match_param, MPLS0_I,
		    misc_parameters_2.inner_first_mpls, mpls_inner.mpls0_label);
	HWS_SET_HDR(fc, match_param, REG_0,
		    misc_parameters_2.metadata_reg_c_0, registers.register_c_0);
	HWS_SET_HDR(fc, match_param, REG_1,
		    misc_parameters_2.metadata_reg_c_1, registers.register_c_1);
	HWS_SET_HDR(fc, match_param, REG_2,
		    misc_parameters_2.metadata_reg_c_2, registers.register_c_2);
	HWS_SET_HDR(fc, match_param, REG_3,
		    misc_parameters_2.metadata_reg_c_3, registers.register_c_3);
	HWS_SET_HDR(fc, match_param, REG_4,
		    misc_parameters_2.metadata_reg_c_4, registers.register_c_4);
	HWS_SET_HDR(fc, match_param, REG_5,
		    misc_parameters_2.metadata_reg_c_5, registers.register_c_5);
	HWS_SET_HDR(fc, match_param, REG_6,
		    misc_parameters_2.metadata_reg_c_6, registers.register_c_6);
	HWS_SET_HDR(fc, match_param, REG_7,
		    misc_parameters_2.metadata_reg_c_7, registers.register_c_7);
	HWS_SET_HDR(fc, match_param, REG_A,
		    misc_parameters_2.metadata_reg_a, metadata.general_purpose);

	if (HWS_IS_FLD_SET(match_param, misc_parameters_2.outer_first_mpls_over_gre)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_MPLS_OVER_GRE;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_MPLS_OVER_GRE_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported misc2 first mpls over gre parameters set\n");
			return -EOPNOTSUPP;
		}

		curr_fc = hws_definer_flex_parser_handler(cd, caps->flex_parser_id_mpls_over_gre);
		if (!curr_fc)
			return -EINVAL;

		HWS_CALC_HDR_SRC(fc, misc_parameters_2.outer_first_mpls_over_gre);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_2.outer_first_mpls_over_udp)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_MPLS_OVER_UDP;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_MPLS_OVER_UDP_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported misc2 first mpls over udp parameters set\n");
			return -EOPNOTSUPP;
		}

		curr_fc = hws_definer_flex_parser_handler(cd, caps->flex_parser_id_mpls_over_udp);
		if (!curr_fc)
			return -EINVAL;

		HWS_CALC_HDR_SRC(fc, misc_parameters_2.outer_first_mpls_over_udp);
	}

	return 0;
}

static int
hws_definer_conv_misc3(struct mlx5hws_definer_conv_data *cd, u32 *match_param)
{
	struct mlx5hws_cmd_query_caps *caps = cd->ctx->caps;
	struct mlx5hws_definer_fc *fc = cd->fc;
	struct mlx5hws_definer_fc *curr_fc;
	bool vxlan_gpe_flex_parser_enabled;

	/* Check reserved and unsupported fields */
	if (HWS_IS_FLD_SET_SZ(match_param, misc_parameters_3.reserved_at_80, 0x8) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters_3.reserved_at_b0, 0x10) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters_3.reserved_at_170, 0x10) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters_3.reserved_at_1e0, 0x20)) {
		mlx5hws_err(cd->ctx, "Unsupported misc3 parameters set\n");
		return -EINVAL;
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.inner_tcp_seq_num) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_3.inner_tcp_ack_num)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TCP_I;
		HWS_SET_HDR(fc, match_param, TCP_SEQ_NUM,
			    misc_parameters_3.inner_tcp_seq_num, tcp_icmp.tcp_seq);
		HWS_SET_HDR(fc, match_param, TCP_ACK_NUM,
			    misc_parameters_3.inner_tcp_ack_num, tcp_icmp.tcp_ack);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.outer_tcp_seq_num) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_3.outer_tcp_ack_num)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TCP_O;
		HWS_SET_HDR(fc, match_param, TCP_SEQ_NUM,
			    misc_parameters_3.outer_tcp_seq_num, tcp_icmp.tcp_seq);
		HWS_SET_HDR(fc, match_param, TCP_ACK_NUM,
			    misc_parameters_3.outer_tcp_ack_num, tcp_icmp.tcp_ack);
	}

	vxlan_gpe_flex_parser_enabled = caps->flex_protocols & MLX5_FLEX_PARSER_VXLAN_GPE_ENABLED;

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.outer_vxlan_gpe_vni)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN_GPE;

		if (!vxlan_gpe_flex_parser_enabled) {
			mlx5hws_err(cd->ctx, "Unsupported VXLAN GPE flex parser\n");
			return -EOPNOTSUPP;
		}

		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VXLAN_GPE_VNI];
		HWS_CALC_HDR(curr_fc, misc_parameters_3.outer_vxlan_gpe_vni,
			     tunnel_header.tunnel_header_1);
		curr_fc->bit_mask = __mlx5_mask(header_vxlan_gpe, vni);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_vxlan_gpe, vni);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.outer_vxlan_gpe_next_protocol)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN_GPE;

		if (!vxlan_gpe_flex_parser_enabled) {
			mlx5hws_err(cd->ctx, "Unsupported VXLAN GPE flex parser\n");
			return -EOPNOTSUPP;
		}

		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VXLAN_GPE_PROTO];
		HWS_CALC_HDR(curr_fc, misc_parameters_3.outer_vxlan_gpe_next_protocol,
			     tunnel_header.tunnel_header_0);
		curr_fc->byte_off += MLX5_BYTE_OFF(header_vxlan_gpe, protocol);
		curr_fc->bit_mask = __mlx5_mask(header_vxlan_gpe, protocol);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_vxlan_gpe, protocol);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.outer_vxlan_gpe_flags)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN_GPE;

		if (!vxlan_gpe_flex_parser_enabled) {
			mlx5hws_err(cd->ctx, "Unsupported VXLAN GPE flex parser\n");
			return -EOPNOTSUPP;
		}

		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_VXLAN_GPE_FLAGS];
		HWS_CALC_HDR(curr_fc, misc_parameters_3.outer_vxlan_gpe_flags,
			     tunnel_header.tunnel_header_0);
		curr_fc->bit_mask = __mlx5_mask(header_vxlan_gpe, flags);
		curr_fc->bit_off = __mlx5_dw_bit_off(header_vxlan_gpe, flags);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.icmp_header_data) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_3.icmp_type) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_3.icmp_code)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_ICMPV4;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_ICMP_V4_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported ICMPv4 flex parser\n");
			return -EOPNOTSUPP;
		}

		HWS_SET_HDR(fc, match_param, ICMP_DW3,
			    misc_parameters_3.icmp_header_data, tcp_icmp.icmp_dw3);

		if (HWS_IS_FLD_SET(match_param, misc_parameters_3.icmp_type) ||
		    HWS_IS_FLD_SET(match_param, misc_parameters_3.icmp_code)) {
			curr_fc = &fc[MLX5HWS_DEFINER_FNAME_ICMP_DW1];
			HWS_CALC_HDR_DST(curr_fc, tcp_icmp.icmp_dw1);
			curr_fc->tag_set = &hws_definer_icmp_dw1_set;
		}
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.icmpv6_header_data) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_3.icmpv6_type) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_3.icmpv6_code)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_ICMPV6;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_ICMP_V6_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported ICMPv6 parser\n");
			return -EOPNOTSUPP;
		}

		HWS_SET_HDR(fc, match_param, ICMP_DW3,
			    misc_parameters_3.icmpv6_header_data, tcp_icmp.icmp_dw3);

		if (HWS_IS_FLD_SET(match_param, misc_parameters_3.icmpv6_type) ||
		    HWS_IS_FLD_SET(match_param, misc_parameters_3.icmpv6_code)) {
			curr_fc = &fc[MLX5HWS_DEFINER_FNAME_ICMP_DW1];
			HWS_CALC_HDR_DST(curr_fc, tcp_icmp.icmp_dw1);
			curr_fc->tag_set = &hws_definer_icmpv6_dw1_set;
		}
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.geneve_tlv_option_0_data)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE;

		curr_fc =
			hws_definer_flex_parser_handler(cd,
							caps->flex_parser_id_geneve_tlv_option_0);
		if (!curr_fc)
			return -EINVAL;

		HWS_CALC_HDR_SRC(fc, misc_parameters_3.geneve_tlv_option_0_data);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.gtpu_teid)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_TEID_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported GTPU TEID flex parser\n");
			return -EOPNOTSUPP;
		}

		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_GTP_TEID];
		fc->tag_set = &hws_definer_generic_set;
		fc->bit_mask = __mlx5_mask(header_gtp, teid);
		fc->byte_off = caps->format_select_gtpu_dw_1 * DW_SIZE;
		HWS_CALC_HDR_SRC(fc, misc_parameters_3.gtpu_teid);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.gtpu_msg_type)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported GTPU flex parser\n");
			return -EOPNOTSUPP;
		}

		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_GTP_MSG_TYPE];
		fc->tag_set = &hws_definer_generic_set;
		fc->bit_mask = __mlx5_mask(header_gtp, msg_type);
		fc->bit_off = __mlx5_dw_bit_off(header_gtp, msg_type);
		fc->byte_off = caps->format_select_gtpu_dw_0 * DW_SIZE;
		HWS_CALC_HDR_SRC(fc, misc_parameters_3.gtpu_msg_type);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.gtpu_msg_flags)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported GTPU flex parser\n");
			return -EOPNOTSUPP;
		}

		fc = &cd->fc[MLX5HWS_DEFINER_FNAME_GTP_MSG_TYPE];
		fc->tag_set = &hws_definer_generic_set;
		fc->bit_mask = __mlx5_mask(header_gtp, msg_flags);
		fc->bit_off = __mlx5_dw_bit_off(header_gtp, msg_flags);
		fc->byte_off = caps->format_select_gtpu_dw_0 * DW_SIZE;
		HWS_CALC_HDR_SRC(fc, misc_parameters_3.gtpu_msg_flags);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.gtpu_dw_2)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_DW_2_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported GTPU DW2 flex parser\n");
			return -EOPNOTSUPP;
		}

		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GTPU_DW2];
		curr_fc->tag_set = &hws_definer_generic_set;
		curr_fc->bit_mask = -1;
		curr_fc->byte_off = caps->format_select_gtpu_dw_2 * DW_SIZE;
		HWS_CALC_HDR_SRC(fc, misc_parameters_3.gtpu_dw_2);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.gtpu_first_ext_dw_0)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_FIRST_EXT_DW_0_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported GTPU first EXT DW0 flex parser\n");
			return -EOPNOTSUPP;
		}

		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GTPU_FIRST_EXT_DW0];
		curr_fc->tag_set = &hws_definer_generic_set;
		curr_fc->bit_mask = -1;
		curr_fc->byte_off = caps->format_select_gtpu_ext_dw_0 * DW_SIZE;
		HWS_CALC_HDR_SRC(fc, misc_parameters_3.gtpu_first_ext_dw_0);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_3.gtpu_dw_0)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU;

		if (!(caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_DW_0_ENABLED)) {
			mlx5hws_err(cd->ctx, "Unsupported GTPU DW0 flex parser\n");
			return -EOPNOTSUPP;
		}

		curr_fc = &fc[MLX5HWS_DEFINER_FNAME_GTPU_DW0];
		curr_fc->tag_set = &hws_definer_generic_set;
		curr_fc->bit_mask = -1;
		curr_fc->byte_off = caps->format_select_gtpu_dw_0 * DW_SIZE;
		HWS_CALC_HDR_SRC(fc, misc_parameters_3.gtpu_dw_0);
	}

	return 0;
}

static int
hws_definer_conv_misc4(struct mlx5hws_definer_conv_data *cd,
		       u32 *match_param)
{
	bool parser_is_used[HWS_NUM_OF_FLEX_PARSERS] = {};
	struct mlx5hws_definer_fc *fc;
	u32 id, value;

	if (HWS_IS_FLD_SET_SZ(match_param, misc_parameters_4.reserved_at_100, 0x100)) {
		mlx5hws_err(cd->ctx, "Unsupported misc4 parameters set\n");
		return -EINVAL;
	}

	id = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_id_0);
	value = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_value_0);
	fc = hws_definer_misc4_fields_handler(cd, parser_is_used, id, value);
	if (!fc)
		return -EINVAL;

	HWS_CALC_HDR_SRC(fc, misc_parameters_4.prog_sample_field_value_0);

	id = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_id_1);
	value = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_value_1);
	fc = hws_definer_misc4_fields_handler(cd, parser_is_used, id, value);
	if (!fc)
		return -EINVAL;

	HWS_CALC_HDR_SRC(fc, misc_parameters_4.prog_sample_field_value_1);

	id = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_id_2);
	value = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_value_2);
	fc = hws_definer_misc4_fields_handler(cd, parser_is_used, id, value);
	if (!fc)
		return -EINVAL;

	HWS_CALC_HDR_SRC(fc, misc_parameters_4.prog_sample_field_value_2);

	id = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_id_3);
	value = HWS_GET_MATCH_PARAM(match_param, misc_parameters_4.prog_sample_field_value_3);
	fc = hws_definer_misc4_fields_handler(cd, parser_is_used, id, value);
	if (!fc)
		return -EINVAL;

	HWS_CALC_HDR_SRC(fc, misc_parameters_4.prog_sample_field_value_3);

	return 0;
}

static int
hws_definer_conv_misc5(struct mlx5hws_definer_conv_data *cd,
		       u32 *match_param)
{
	struct mlx5hws_definer_fc *fc = cd->fc;

	if (HWS_IS_FLD_SET(match_param, misc_parameters_5.macsec_tag_0) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_5.macsec_tag_1) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_5.macsec_tag_2) ||
	    HWS_IS_FLD_SET(match_param, misc_parameters_5.macsec_tag_3) ||
	    HWS_IS_FLD_SET_SZ(match_param, misc_parameters_5.reserved_at_100, 0x100)) {
		mlx5hws_err(cd->ctx, "Unsupported misc5 parameters set\n");
		return -EINVAL;
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_5.tunnel_header_0)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_HEADER_0_1;
		HWS_SET_HDR(fc, match_param, TNL_HDR_0,
			    misc_parameters_5.tunnel_header_0, tunnel_header.tunnel_header_0);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_5.tunnel_header_1)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_HEADER_0_1;
		HWS_SET_HDR(fc, match_param, TNL_HDR_1,
			    misc_parameters_5.tunnel_header_1, tunnel_header.tunnel_header_1);
	}

	if (HWS_IS_FLD_SET(match_param, misc_parameters_5.tunnel_header_2)) {
		cd->match_flags |= MLX5HWS_DEFINER_MATCH_FLAG_TNL_HEADER_2;
		HWS_SET_HDR(fc, match_param, TNL_HDR_2,
			    misc_parameters_5.tunnel_header_2, tunnel_header.tunnel_header_2);
	}

	HWS_SET_HDR(fc, match_param, TNL_HDR_3,
		    misc_parameters_5.tunnel_header_3, tunnel_header.tunnel_header_3);

	return 0;
}

static int hws_definer_get_fc_size(struct mlx5hws_definer_fc *fc)
{
	u32 fc_sz = 0;
	int i;

	/* For empty matcher, ZERO_SIZE_PTR is returned */
	if (fc == ZERO_SIZE_PTR)
		return 0;

	for (i = 0; i < MLX5HWS_DEFINER_FNAME_MAX; i++)
		if (fc[i].tag_set)
			fc_sz++;
	return fc_sz;
}

static struct mlx5hws_definer_fc *
hws_definer_alloc_compressed_fc(struct mlx5hws_definer_fc *fc)
{
	struct mlx5hws_definer_fc *compressed_fc = NULL;
	u32 definer_size = hws_definer_get_fc_size(fc);
	u32 fc_sz = 0;
	int i;

	compressed_fc = kcalloc(definer_size, sizeof(*compressed_fc), GFP_KERNEL);
	if (!compressed_fc)
		return NULL;

	/* For empty matcher, ZERO_SIZE_PTR is returned */
	if (!definer_size)
		return compressed_fc;

	for (i = 0, fc_sz = 0; i < MLX5HWS_DEFINER_FNAME_MAX; i++) {
		if (!fc[i].tag_set)
			continue;

		fc[i].fname = i;
		memcpy(&compressed_fc[fc_sz++], &fc[i], sizeof(*compressed_fc));
	}

	return compressed_fc;
}

static void
hws_definer_set_hl(u8 *hl, struct mlx5hws_definer_fc *fc)
{
	int i;

	/* nothing to do for empty matcher */
	if (fc == ZERO_SIZE_PTR)
		return;

	for (i = 0; i < MLX5HWS_DEFINER_FNAME_MAX; i++) {
		if (!fc[i].tag_set)
			continue;

		HWS_SET32(hl, -1, fc[i].byte_off, fc[i].bit_off, fc[i].bit_mask);
	}
}

static struct mlx5hws_definer_fc *
hws_definer_alloc_fc(struct mlx5hws_context *ctx,
		     size_t len)
{
	struct mlx5hws_definer_fc *fc;
	int i;

	fc = kcalloc(len, sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return NULL;

	for (i = 0; i < len; i++)
		fc[i].ctx = ctx;

	return fc;
}

static int
hws_definer_conv_match_params_to_hl(struct mlx5hws_context *ctx,
				    struct mlx5hws_match_template *mt,
				    u8 *hl)
{
	struct mlx5hws_definer_conv_data cd = {0};
	struct mlx5hws_definer_fc *fc;
	int ret;

	fc = hws_definer_alloc_fc(ctx, MLX5HWS_DEFINER_FNAME_MAX);
	if (!fc)
		return -ENOMEM;

	cd.fc = fc;
	cd.ctx = ctx;

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC6) {
		mlx5hws_err(ctx, "Unsupported match_criteria_enable provided\n");
		ret = -EOPNOTSUPP;
		goto err_free_fc;
	}

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_OUTER) {
		ret = hws_definer_conv_outer(&cd, mt->match_param);
		if (ret)
			goto err_free_fc;
	}

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_INNER) {
		ret = hws_definer_conv_inner(&cd, mt->match_param);
		if (ret)
			goto err_free_fc;
	}

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC) {
		ret = hws_definer_conv_misc(&cd, mt->match_param);
		if (ret)
			goto err_free_fc;
	}

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC2) {
		ret = hws_definer_conv_misc2(&cd, mt->match_param);
		if (ret)
			goto err_free_fc;
	}

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC3) {
		ret = hws_definer_conv_misc3(&cd, mt->match_param);
		if (ret)
			goto err_free_fc;
	}

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC4) {
		ret = hws_definer_conv_misc4(&cd, mt->match_param);
		if (ret)
			goto err_free_fc;
	}

	if (mt->match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC5) {
		ret = hws_definer_conv_misc5(&cd, mt->match_param);
		if (ret)
			goto err_free_fc;
	}

	/* Check there is no conflicted fields set together */
	ret = hws_definer_check_match_flags(&cd);
	if (ret)
		goto err_free_fc;

	/* Allocate fc array on mt */
	mt->fc = hws_definer_alloc_compressed_fc(fc);
	if (!mt->fc) {
		mlx5hws_err(ctx,
			    "Convert match params: failed to set field copy to match template\n");
		ret = -ENOMEM;
		goto err_free_fc;
	}
	mt->fc_sz = hws_definer_get_fc_size(fc);

	/* Fill in headers layout */
	hws_definer_set_hl(hl, fc);

	kfree(fc);
	return 0;

err_free_fc:
	kfree(fc);
	return ret;
}

struct mlx5hws_definer_fc *
mlx5hws_definer_conv_match_params_to_compressed_fc(struct mlx5hws_context *ctx,
						   u8 match_criteria_enable,
						   u32 *match_param,
						   int *fc_sz)
{
	struct mlx5hws_definer_fc *compressed_fc = NULL;
	struct mlx5hws_definer_conv_data cd = {0};
	struct mlx5hws_definer_fc *fc;
	int ret;

	fc = hws_definer_alloc_fc(ctx, MLX5HWS_DEFINER_FNAME_MAX);
	if (!fc)
		return NULL;

	cd.fc = fc;
	cd.ctx = ctx;

	if (match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_OUTER) {
		ret = hws_definer_conv_outer(&cd, match_param);
		if (ret)
			goto err_free_fc;
	}

	if (match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_INNER) {
		ret = hws_definer_conv_inner(&cd, match_param);
		if (ret)
			goto err_free_fc;
	}

	if (match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC) {
		ret = hws_definer_conv_misc(&cd, match_param);
		if (ret)
			goto err_free_fc;
	}

	if (match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC2) {
		ret = hws_definer_conv_misc2(&cd, match_param);
		if (ret)
			goto err_free_fc;
	}

	if (match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC3) {
		ret = hws_definer_conv_misc3(&cd, match_param);
		if (ret)
			goto err_free_fc;
	}

	if (match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC4) {
		ret = hws_definer_conv_misc4(&cd, match_param);
		if (ret)
			goto err_free_fc;
	}

	if (match_criteria_enable & MLX5HWS_DEFINER_MATCH_CRITERIA_MISC5) {
		ret = hws_definer_conv_misc5(&cd, match_param);
		if (ret)
			goto err_free_fc;
	}

	/* Allocate fc array on mt */
	compressed_fc = hws_definer_alloc_compressed_fc(fc);
	if (!compressed_fc) {
		mlx5hws_err(ctx,
			    "Convert to compressed fc: failed to set field copy to match template\n");
		goto err_free_fc;
	}
	*fc_sz = hws_definer_get_fc_size(fc);

err_free_fc:
	kfree(fc);
	return compressed_fc;
}

static int
hws_definer_find_byte_in_tag(struct mlx5hws_definer *definer,
			     u32 hl_byte_off,
			     u32 *tag_byte_off)
{
	int i, dw_to_scan;
	u8 byte_offset;

	/* Avoid accessing unused DW selectors */
	dw_to_scan = mlx5hws_definer_is_jumbo(definer) ?
		DW_SELECTORS : DW_SELECTORS_MATCH;

	/* Add offset since each DW covers multiple BYTEs */
	byte_offset = hl_byte_off % DW_SIZE;
	for (i = 0; i < dw_to_scan; i++) {
		if (definer->dw_selector[i] == hl_byte_off / DW_SIZE) {
			*tag_byte_off = byte_offset + DW_SIZE * (DW_SELECTORS - i - 1);
			return 0;
		}
	}

	/* Add offset to skip DWs in definer */
	byte_offset = DW_SIZE * DW_SELECTORS;
	/* Iterate in reverse since the code uses bytes from 7 -> 0 */
	for (i = BYTE_SELECTORS; i-- > 0 ;) {
		if (definer->byte_selector[i] == hl_byte_off) {
			*tag_byte_off = byte_offset + (BYTE_SELECTORS - i - 1);
			return 0;
		}
	}

	return -EINVAL;
}

static int
hws_definer_fc_bind(struct mlx5hws_definer *definer,
		    struct mlx5hws_definer_fc *fc,
		    u32 fc_sz)
{
	u32 tag_offset = 0;
	int ret, byte_diff;
	u32 i;

	for (i = 0; i < fc_sz; i++) {
		/* Map header layout byte offset to byte offset in tag */
		ret = hws_definer_find_byte_in_tag(definer, fc->byte_off, &tag_offset);
		if (ret)
			return ret;

		/* Move setter based on the location in the definer */
		byte_diff = fc->byte_off % DW_SIZE - tag_offset % DW_SIZE;
		fc->bit_off = fc->bit_off + byte_diff * BITS_IN_BYTE;

		/* Update offset in headers layout to offset in tag */
		fc->byte_off = tag_offset;
		fc++;
	}

	return 0;
}

static bool
hws_definer_best_hl_fit_recu(struct mlx5hws_definer_sel_ctrl *ctrl,
			     u32 cur_dw,
			     u32 *data)
{
	u8 bytes_set;
	int byte_idx;
	bool ret;
	int i;

	/* Reached end, nothing left to do */
	if (cur_dw == MLX5_ST_SZ_DW(definer_hl))
		return true;

	/* No data set, can skip to next DW */
	while (!*data) {
		cur_dw++;
		data++;

		/* Reached end, nothing left to do */
		if (cur_dw == MLX5_ST_SZ_DW(definer_hl))
			return true;
	}

	/* Used all DW selectors and Byte selectors, no possible solution */
	if (ctrl->allowed_full_dw == ctrl->used_full_dw &&
	    ctrl->allowed_lim_dw == ctrl->used_lim_dw &&
	    ctrl->allowed_bytes == ctrl->used_bytes)
		return false;

	/* Try to use limited DW selectors */
	if (ctrl->allowed_lim_dw > ctrl->used_lim_dw && cur_dw < 64) {
		ctrl->lim_dw_selector[ctrl->used_lim_dw++] = cur_dw;

		ret = hws_definer_best_hl_fit_recu(ctrl, cur_dw + 1, data + 1);
		if (ret)
			return ret;

		ctrl->lim_dw_selector[--ctrl->used_lim_dw] = 0;
	}

	/* Try to use DW selectors */
	if (ctrl->allowed_full_dw > ctrl->used_full_dw) {
		ctrl->full_dw_selector[ctrl->used_full_dw++] = cur_dw;

		ret = hws_definer_best_hl_fit_recu(ctrl, cur_dw + 1, data + 1);
		if (ret)
			return ret;

		ctrl->full_dw_selector[--ctrl->used_full_dw] = 0;
	}

	/* No byte selector for offset bigger than 255 */
	if (cur_dw * DW_SIZE > 255)
		return false;

	bytes_set = !!(0x000000ff & *data) +
		    !!(0x0000ff00 & *data) +
		    !!(0x00ff0000 & *data) +
		    !!(0xff000000 & *data);

	/* Check if there are enough byte selectors left */
	if (bytes_set + ctrl->used_bytes > ctrl->allowed_bytes)
		return false;

	/* Try to use Byte selectors */
	for (i = 0; i < DW_SIZE; i++)
		if ((0xff000000 >> (i * BITS_IN_BYTE)) & be32_to_cpu((__force __be32)*data)) {
			/* Use byte selectors high to low */
			byte_idx = ctrl->allowed_bytes - ctrl->used_bytes - 1;
			ctrl->byte_selector[byte_idx] = cur_dw * DW_SIZE + i;
			ctrl->used_bytes++;
		}

	ret = hws_definer_best_hl_fit_recu(ctrl, cur_dw + 1, data + 1);
	if (ret)
		return ret;

	for (i = 0; i < DW_SIZE; i++)
		if ((0xff << (i * BITS_IN_BYTE)) & be32_to_cpu((__force __be32)*data)) {
			ctrl->used_bytes--;
			byte_idx = ctrl->allowed_bytes - ctrl->used_bytes - 1;
			ctrl->byte_selector[byte_idx] = 0;
		}

	return false;
}

static void
hws_definer_copy_sel_ctrl(struct mlx5hws_definer_sel_ctrl *ctrl,
			  struct mlx5hws_definer *definer)
{
	memcpy(definer->byte_selector, ctrl->byte_selector, ctrl->allowed_bytes);
	memcpy(definer->dw_selector, ctrl->full_dw_selector, ctrl->allowed_full_dw);
	memcpy(definer->dw_selector + ctrl->allowed_full_dw,
	       ctrl->lim_dw_selector, ctrl->allowed_lim_dw);
}

static int
hws_definer_find_best_match_fit(struct mlx5hws_context *ctx,
				struct mlx5hws_definer *definer,
				u8 *hl)
{
	struct mlx5hws_definer_sel_ctrl ctrl = {0};
	bool found;

	/* Try to create a match definer */
	ctrl.allowed_full_dw = DW_SELECTORS_MATCH;
	ctrl.allowed_lim_dw = 0;
	ctrl.allowed_bytes = BYTE_SELECTORS;

	found = hws_definer_best_hl_fit_recu(&ctrl, 0, (u32 *)hl);
	if (found) {
		hws_definer_copy_sel_ctrl(&ctrl, definer);
		definer->type = MLX5HWS_DEFINER_TYPE_MATCH;
		return 0;
	}

	/* Try to create a full/limited jumbo definer */
	ctrl.allowed_full_dw = ctx->caps->full_dw_jumbo_support ? DW_SELECTORS :
								  DW_SELECTORS_MATCH;
	ctrl.allowed_lim_dw = ctx->caps->full_dw_jumbo_support ? 0 :
								 DW_SELECTORS_LIMITED;
	ctrl.allowed_bytes = BYTE_SELECTORS;

	found = hws_definer_best_hl_fit_recu(&ctrl, 0, (u32 *)hl);
	if (found) {
		hws_definer_copy_sel_ctrl(&ctrl, definer);
		definer->type = MLX5HWS_DEFINER_TYPE_JUMBO;
		return 0;
	}

	return -E2BIG;
}

static void
hws_definer_create_tag_mask(u32 *match_param,
			    struct mlx5hws_definer_fc *fc,
			    u32 fc_sz,
			    u8 *tag)
{
	u32 i;

	for (i = 0; i < fc_sz; i++) {
		if (fc->tag_mask_set)
			fc->tag_mask_set(fc, match_param, tag);
		else
			fc->tag_set(fc, match_param, tag);
		fc++;
	}
}

void mlx5hws_definer_create_tag(u32 *match_param,
				struct mlx5hws_definer_fc *fc,
				u32 fc_sz,
				u8 *tag)
{
	u32 i;

	for (i = 0; i < fc_sz; i++) {
		fc->tag_set(fc, match_param, tag);
		fc++;
	}
}

int mlx5hws_definer_get_id(struct mlx5hws_definer *definer)
{
	return definer->obj_id;
}

int mlx5hws_definer_compare(struct mlx5hws_definer *definer_a,
			    struct mlx5hws_definer *definer_b)
{
	int i;

	/* Future: Optimize by comparing selectors with valid mask only */
	for (i = 0; i < BYTE_SELECTORS; i++)
		if (definer_a->byte_selector[i] != definer_b->byte_selector[i])
			return 1;

	for (i = 0; i < DW_SELECTORS; i++)
		if (definer_a->dw_selector[i] != definer_b->dw_selector[i])
			return 1;

	for (i = 0; i < MLX5HWS_JUMBO_TAG_SZ; i++)
		if (definer_a->mask.jumbo[i] != definer_b->mask.jumbo[i])
			return 1;

	return 0;
}

int
mlx5hws_definer_calc_layout(struct mlx5hws_context *ctx,
			    struct mlx5hws_match_template *mt,
			    struct mlx5hws_definer *match_definer)
{
	u8 *match_hl;
	int ret;

	/* Union header-layout (hl) is used for creating a single definer
	 * field layout used with different bitmasks for hash and match.
	 */
	match_hl = kzalloc(MLX5_ST_SZ_BYTES(definer_hl), GFP_KERNEL);
	if (!match_hl)
		return -ENOMEM;

	/* Convert all mt items to header layout (hl)
	 * and allocate the match and range field copy array (fc & fcr).
	 */
	ret = hws_definer_conv_match_params_to_hl(ctx, mt, match_hl);
	if (ret) {
		mlx5hws_err(ctx, "Failed to convert items to header layout\n");
		goto free_match_hl;
	}

	/* Find the match definer layout for header layout match union */
	ret = hws_definer_find_best_match_fit(ctx, match_definer, match_hl);
	if (ret) {
		if (ret == -E2BIG)
			mlx5hws_dbg(ctx,
				    "Failed to create match definer from header layout - E2BIG\n");
		else
			mlx5hws_err(ctx,
				    "Failed to create match definer from header layout (%d)\n",
				    ret);
		goto free_fc;
	}

	kfree(match_hl);
	return 0;

free_fc:
	kfree(mt->fc);
free_match_hl:
	kfree(match_hl);
	return ret;
}

int mlx5hws_definer_init_cache(struct mlx5hws_definer_cache **cache)
{
	struct mlx5hws_definer_cache *new_cache;

	new_cache = kzalloc(sizeof(*new_cache), GFP_KERNEL);
	if (!new_cache)
		return -ENOMEM;

	INIT_LIST_HEAD(&new_cache->list_head);
	*cache = new_cache;

	return 0;
}

void mlx5hws_definer_uninit_cache(struct mlx5hws_definer_cache *cache)
{
	kfree(cache);
}

int mlx5hws_definer_get_obj(struct mlx5hws_context *ctx,
			    struct mlx5hws_definer *definer)
{
	struct mlx5hws_definer_cache *cache = ctx->definer_cache;
	struct mlx5hws_cmd_definer_create_attr def_attr = {0};
	struct mlx5hws_definer_cache_item *cached_definer;
	u32 obj_id;
	int ret;

	/* Search definer cache for requested definer */
	list_for_each_entry(cached_definer, &cache->list_head, list_node) {
		if (mlx5hws_definer_compare(&cached_definer->definer, definer))
			continue;

		/* Reuse definer and set LRU (move to be first in the list) */
		list_del_init(&cached_definer->list_node);
		list_add(&cached_definer->list_node, &cache->list_head);
		cached_definer->refcount++;
		return cached_definer->definer.obj_id;
	}

	/* Allocate and create definer based on the bitmask tag */
	def_attr.match_mask = definer->mask.jumbo;
	def_attr.dw_selector = definer->dw_selector;
	def_attr.byte_selector = definer->byte_selector;

	ret = mlx5hws_cmd_definer_create(ctx->mdev, &def_attr, &obj_id);
	if (ret)
		return -1;

	cached_definer = kzalloc(sizeof(*cached_definer), GFP_KERNEL);
	if (!cached_definer)
		goto free_definer_obj;

	memcpy(&cached_definer->definer, definer, sizeof(*definer));
	cached_definer->definer.obj_id = obj_id;
	cached_definer->refcount = 1;
	list_add(&cached_definer->list_node, &cache->list_head);

	return obj_id;

free_definer_obj:
	mlx5hws_cmd_definer_destroy(ctx->mdev, obj_id);
	return -1;
}

static void
hws_definer_put_obj(struct mlx5hws_context *ctx, u32 obj_id)
{
	struct mlx5hws_definer_cache_item *cached_definer;

	list_for_each_entry(cached_definer, &ctx->definer_cache->list_head, list_node) {
		if (cached_definer->definer.obj_id != obj_id)
			continue;

		/* Object found */
		if (--cached_definer->refcount)
			return;

		list_del_init(&cached_definer->list_node);
		mlx5hws_cmd_definer_destroy(ctx->mdev, cached_definer->definer.obj_id);
		kfree(cached_definer);
		return;
	}

	/* Programming error, object must be part of cache */
	pr_warn("HWS: failed putting definer object\n");
}

static struct mlx5hws_definer *
hws_definer_alloc(struct mlx5hws_context *ctx,
		  struct mlx5hws_definer_fc *fc,
		  int fc_sz,
		  u32 *match_param,
		  struct mlx5hws_definer *layout,
		  bool bind_fc)
{
	struct mlx5hws_definer *definer;
	int ret;

	definer = kmemdup(layout, sizeof(*definer), GFP_KERNEL);
	if (!definer)
		return NULL;

	/* Align field copy array based on given layout */
	if (bind_fc) {
		ret = hws_definer_fc_bind(definer, fc, fc_sz);
		if (ret) {
			mlx5hws_err(ctx, "Failed to bind field copy to definer\n");
			goto free_definer;
		}
	}

	/* Create the tag mask used for definer creation */
	hws_definer_create_tag_mask(match_param, fc, fc_sz, definer->mask.jumbo);

	ret = mlx5hws_definer_get_obj(ctx, definer);
	if (ret < 0)
		goto free_definer;

	definer->obj_id = ret;
	return definer;

free_definer:
	kfree(definer);
	return NULL;
}

void mlx5hws_definer_free(struct mlx5hws_context *ctx,
			  struct mlx5hws_definer *definer)
{
	hws_definer_put_obj(ctx, definer->obj_id);
	kfree(definer);
}

static int
hws_definer_mt_match_init(struct mlx5hws_context *ctx,
			  struct mlx5hws_match_template *mt,
			  struct mlx5hws_definer *match_layout)
{
	/* Create mandatory match definer */
	mt->definer = hws_definer_alloc(ctx,
					mt->fc,
					mt->fc_sz,
					mt->match_param,
					match_layout,
					true);
	if (!mt->definer) {
		mlx5hws_err(ctx, "Failed to create match definer\n");
		return -EINVAL;
	}

	return 0;
}

static void
hws_definer_mt_match_uninit(struct mlx5hws_context *ctx,
			    struct mlx5hws_match_template *mt)
{
	mlx5hws_definer_free(ctx, mt->definer);
}

int mlx5hws_definer_mt_init(struct mlx5hws_context *ctx,
			    struct mlx5hws_match_template *mt)
{
	struct mlx5hws_definer match_layout = {0};
	int ret;

	ret = mlx5hws_definer_calc_layout(ctx, mt, &match_layout);
	if (ret) {
		mlx5hws_err(ctx, "Failed to calculate matcher definer layout\n");
		return ret;
	}

	/* Calculate definers needed for exact match */
	ret = hws_definer_mt_match_init(ctx, mt, &match_layout);
	if (ret) {
		mlx5hws_err(ctx, "Failed to init match definers\n");
		goto free_fc;
	}

	return 0;

free_fc:
	kfree(mt->fc);
	return ret;
}

void mlx5hws_definer_mt_uninit(struct mlx5hws_context *ctx,
			       struct mlx5hws_match_template *mt)
{
	hws_definer_mt_match_uninit(ctx, mt);
	kfree(mt->fc);
}
