// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/uverbs_std_types.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/mlx5_user_ioctl_verbs.h>
#include <rdma/ib_umem.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/fs_helpers.h>
#include <linux/mlx5/accel.h>
#include <linux/mlx5/eswitch.h>
#include "mlx5_ib.h"
#include "counters.h"
#include "devx.h"
#include "fs.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

enum {
	MATCH_CRITERIA_ENABLE_OUTER_BIT,
	MATCH_CRITERIA_ENABLE_MISC_BIT,
	MATCH_CRITERIA_ENABLE_INNER_BIT,
	MATCH_CRITERIA_ENABLE_MISC2_BIT
};

#define HEADER_IS_ZERO(match_criteria, headers)			           \
	!(memchr_inv(MLX5_ADDR_OF(fte_match_param, match_criteria, headers), \
		    0, MLX5_FLD_SZ_BYTES(fte_match_param, headers)))       \

static u8 get_match_criteria_enable(u32 *match_criteria)
{
	u8 match_criteria_enable;

	match_criteria_enable =
		(!HEADER_IS_ZERO(match_criteria, outer_headers)) <<
		MATCH_CRITERIA_ENABLE_OUTER_BIT;
	match_criteria_enable |=
		(!HEADER_IS_ZERO(match_criteria, misc_parameters)) <<
		MATCH_CRITERIA_ENABLE_MISC_BIT;
	match_criteria_enable |=
		(!HEADER_IS_ZERO(match_criteria, inner_headers)) <<
		MATCH_CRITERIA_ENABLE_INNER_BIT;
	match_criteria_enable |=
		(!HEADER_IS_ZERO(match_criteria, misc_parameters_2)) <<
		MATCH_CRITERIA_ENABLE_MISC2_BIT;

	return match_criteria_enable;
}

static int set_proto(void *outer_c, void *outer_v, u8 mask, u8 val)
{
	u8 entry_mask;
	u8 entry_val;
	int err = 0;

	if (!mask)
		goto out;

	entry_mask = MLX5_GET(fte_match_set_lyr_2_4, outer_c,
			      ip_protocol);
	entry_val = MLX5_GET(fte_match_set_lyr_2_4, outer_v,
			     ip_protocol);
	if (!entry_mask) {
		MLX5_SET(fte_match_set_lyr_2_4, outer_c, ip_protocol, mask);
		MLX5_SET(fte_match_set_lyr_2_4, outer_v, ip_protocol, val);
		goto out;
	}
	/* Don't override existing ip protocol */
	if (mask != entry_mask || val != entry_val)
		err = -EINVAL;
out:
	return err;
}

static void set_flow_label(void *misc_c, void *misc_v, u32 mask, u32 val,
			   bool inner)
{
	if (inner) {
		MLX5_SET(fte_match_set_misc,
			 misc_c, inner_ipv6_flow_label, mask);
		MLX5_SET(fte_match_set_misc,
			 misc_v, inner_ipv6_flow_label, val);
	} else {
		MLX5_SET(fte_match_set_misc,
			 misc_c, outer_ipv6_flow_label, mask);
		MLX5_SET(fte_match_set_misc,
			 misc_v, outer_ipv6_flow_label, val);
	}
}

static void set_tos(void *outer_c, void *outer_v, u8 mask, u8 val)
{
	MLX5_SET(fte_match_set_lyr_2_4, outer_c, ip_ecn, mask);
	MLX5_SET(fte_match_set_lyr_2_4, outer_v, ip_ecn, val);
	MLX5_SET(fte_match_set_lyr_2_4, outer_c, ip_dscp, mask >> 2);
	MLX5_SET(fte_match_set_lyr_2_4, outer_v, ip_dscp, val >> 2);
}

static int check_mpls_supp_fields(u32 field_support, const __be32 *set_mask)
{
	if (MLX5_GET(fte_match_mpls, set_mask, mpls_label) &&
	    !(field_support & MLX5_FIELD_SUPPORT_MPLS_LABEL))
		return -EOPNOTSUPP;

	if (MLX5_GET(fte_match_mpls, set_mask, mpls_exp) &&
	    !(field_support & MLX5_FIELD_SUPPORT_MPLS_EXP))
		return -EOPNOTSUPP;

	if (MLX5_GET(fte_match_mpls, set_mask, mpls_s_bos) &&
	    !(field_support & MLX5_FIELD_SUPPORT_MPLS_S_BOS))
		return -EOPNOTSUPP;

	if (MLX5_GET(fte_match_mpls, set_mask, mpls_ttl) &&
	    !(field_support & MLX5_FIELD_SUPPORT_MPLS_TTL))
		return -EOPNOTSUPP;

	return 0;
}

#define LAST_ETH_FIELD vlan_tag
#define LAST_IB_FIELD sl
#define LAST_IPV4_FIELD tos
#define LAST_IPV6_FIELD traffic_class
#define LAST_TCP_UDP_FIELD src_port
#define LAST_TUNNEL_FIELD tunnel_id
#define LAST_FLOW_TAG_FIELD tag_id
#define LAST_DROP_FIELD size
#define LAST_COUNTERS_FIELD counters

/* Field is the last supported field */
#define FIELDS_NOT_SUPPORTED(filter, field)                                    \
	memchr_inv((void *)&filter.field + sizeof(filter.field), 0,            \
		   sizeof(filter) - offsetofend(typeof(filter), field))

int parse_flow_flow_action(struct mlx5_ib_flow_action *maction,
			   bool is_egress,
			   struct mlx5_flow_act *action)
{

	switch (maction->ib_action.type) {
	case IB_FLOW_ACTION_ESP:
		if (action->action & (MLX5_FLOW_CONTEXT_ACTION_ENCRYPT |
				      MLX5_FLOW_CONTEXT_ACTION_DECRYPT))
			return -EINVAL;
		/* Currently only AES_GCM keymat is supported by the driver */
		action->esp_id = (uintptr_t)maction->esp_aes_gcm.ctx;
		action->action |= is_egress ?
			MLX5_FLOW_CONTEXT_ACTION_ENCRYPT :
			MLX5_FLOW_CONTEXT_ACTION_DECRYPT;
		return 0;
	case IB_FLOW_ACTION_UNSPECIFIED:
		if (maction->flow_action_raw.sub_type ==
		    MLX5_IB_FLOW_ACTION_MODIFY_HEADER) {
			if (action->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
				return -EINVAL;
			action->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
			action->modify_hdr =
				maction->flow_action_raw.modify_hdr;
			return 0;
		}
		if (maction->flow_action_raw.sub_type ==
		    MLX5_IB_FLOW_ACTION_DECAP) {
			if (action->action & MLX5_FLOW_CONTEXT_ACTION_DECAP)
				return -EINVAL;
			action->action |= MLX5_FLOW_CONTEXT_ACTION_DECAP;
			return 0;
		}
		if (maction->flow_action_raw.sub_type ==
		    MLX5_IB_FLOW_ACTION_PACKET_REFORMAT) {
			if (action->action &
			    MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT)
				return -EINVAL;
			action->action |=
				MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
			action->pkt_reformat =
				maction->flow_action_raw.pkt_reformat;
			return 0;
		}
		fallthrough;
	default:
		return -EOPNOTSUPP;
	}
}

static int parse_flow_attr(struct mlx5_core_dev *mdev,
			   struct mlx5_flow_spec *spec,
			   const union ib_flow_spec *ib_spec,
			   const struct ib_flow_attr *flow_attr,
			   struct mlx5_flow_act *action, u32 prev_type)
{
	struct mlx5_flow_context *flow_context = &spec->flow_context;
	u32 *match_c = spec->match_criteria;
	u32 *match_v = spec->match_value;
	void *misc_params_c = MLX5_ADDR_OF(fte_match_param, match_c,
					   misc_parameters);
	void *misc_params_v = MLX5_ADDR_OF(fte_match_param, match_v,
					   misc_parameters);
	void *misc_params2_c = MLX5_ADDR_OF(fte_match_param, match_c,
					    misc_parameters_2);
	void *misc_params2_v = MLX5_ADDR_OF(fte_match_param, match_v,
					    misc_parameters_2);
	void *headers_c;
	void *headers_v;
	int match_ipv;
	int ret;

	if (ib_spec->type & IB_FLOW_SPEC_INNER) {
		headers_c = MLX5_ADDR_OF(fte_match_param, match_c,
					 inner_headers);
		headers_v = MLX5_ADDR_OF(fte_match_param, match_v,
					 inner_headers);
		match_ipv = MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					ft_field_support.inner_ip_version);
	} else {
		headers_c = MLX5_ADDR_OF(fte_match_param, match_c,
					 outer_headers);
		headers_v = MLX5_ADDR_OF(fte_match_param, match_v,
					 outer_headers);
		match_ipv = MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					ft_field_support.outer_ip_version);
	}

	switch (ib_spec->type & ~IB_FLOW_SPEC_INNER) {
	case IB_FLOW_SPEC_ETH:
		if (FIELDS_NOT_SUPPORTED(ib_spec->eth.mask, LAST_ETH_FIELD))
			return -EOPNOTSUPP;

		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     dmac_47_16),
				ib_spec->eth.mask.dst_mac);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     dmac_47_16),
				ib_spec->eth.val.dst_mac);

		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     smac_47_16),
				ib_spec->eth.mask.src_mac);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     smac_47_16),
				ib_spec->eth.val.src_mac);

		if (ib_spec->eth.mask.vlan_tag) {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 cvlan_tag, 1);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 cvlan_tag, 1);

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 first_vid, ntohs(ib_spec->eth.mask.vlan_tag));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 first_vid, ntohs(ib_spec->eth.val.vlan_tag));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 first_cfi,
				 ntohs(ib_spec->eth.mask.vlan_tag) >> 12);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 first_cfi,
				 ntohs(ib_spec->eth.val.vlan_tag) >> 12);

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 first_prio,
				 ntohs(ib_spec->eth.mask.vlan_tag) >> 13);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 first_prio,
				 ntohs(ib_spec->eth.val.vlan_tag) >> 13);
		}
		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 ethertype, ntohs(ib_spec->eth.mask.ether_type));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 ethertype, ntohs(ib_spec->eth.val.ether_type));
		break;
	case IB_FLOW_SPEC_IPV4:
		if (FIELDS_NOT_SUPPORTED(ib_spec->ipv4.mask, LAST_IPV4_FIELD))
			return -EOPNOTSUPP;

		if (match_ipv) {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 ip_version, 0xf);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 ip_version, MLX5_FS_IPV4_VERSION);
		} else {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 ethertype, 0xffff);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 ethertype, ETH_P_IP);
		}

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &ib_spec->ipv4.mask.src_ip,
		       sizeof(ib_spec->ipv4.mask.src_ip));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &ib_spec->ipv4.val.src_ip,
		       sizeof(ib_spec->ipv4.val.src_ip));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &ib_spec->ipv4.mask.dst_ip,
		       sizeof(ib_spec->ipv4.mask.dst_ip));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &ib_spec->ipv4.val.dst_ip,
		       sizeof(ib_spec->ipv4.val.dst_ip));

		set_tos(headers_c, headers_v,
			ib_spec->ipv4.mask.tos, ib_spec->ipv4.val.tos);

		if (set_proto(headers_c, headers_v,
			      ib_spec->ipv4.mask.proto,
			      ib_spec->ipv4.val.proto))
			return -EINVAL;
		break;
	case IB_FLOW_SPEC_IPV6:
		if (FIELDS_NOT_SUPPORTED(ib_spec->ipv6.mask, LAST_IPV6_FIELD))
			return -EOPNOTSUPP;

		if (match_ipv) {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 ip_version, 0xf);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 ip_version, MLX5_FS_IPV6_VERSION);
		} else {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 ethertype, 0xffff);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 ethertype, ETH_P_IPV6);
		}

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &ib_spec->ipv6.mask.src_ip,
		       sizeof(ib_spec->ipv6.mask.src_ip));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &ib_spec->ipv6.val.src_ip,
		       sizeof(ib_spec->ipv6.val.src_ip));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &ib_spec->ipv6.mask.dst_ip,
		       sizeof(ib_spec->ipv6.mask.dst_ip));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &ib_spec->ipv6.val.dst_ip,
		       sizeof(ib_spec->ipv6.val.dst_ip));

		set_tos(headers_c, headers_v,
			ib_spec->ipv6.mask.traffic_class,
			ib_spec->ipv6.val.traffic_class);

		if (set_proto(headers_c, headers_v,
			      ib_spec->ipv6.mask.next_hdr,
			      ib_spec->ipv6.val.next_hdr))
			return -EINVAL;

		set_flow_label(misc_params_c, misc_params_v,
			       ntohl(ib_spec->ipv6.mask.flow_label),
			       ntohl(ib_spec->ipv6.val.flow_label),
			       ib_spec->type & IB_FLOW_SPEC_INNER);
		break;
	case IB_FLOW_SPEC_ESP:
		if (ib_spec->esp.mask.seq)
			return -EOPNOTSUPP;

		MLX5_SET(fte_match_set_misc, misc_params_c, outer_esp_spi,
			 ntohl(ib_spec->esp.mask.spi));
		MLX5_SET(fte_match_set_misc, misc_params_v, outer_esp_spi,
			 ntohl(ib_spec->esp.val.spi));
		break;
	case IB_FLOW_SPEC_TCP:
		if (FIELDS_NOT_SUPPORTED(ib_spec->tcp_udp.mask,
					 LAST_TCP_UDP_FIELD))
			return -EOPNOTSUPP;

		if (set_proto(headers_c, headers_v, 0xff, IPPROTO_TCP))
			return -EINVAL;

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, tcp_sport,
			 ntohs(ib_spec->tcp_udp.mask.src_port));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_sport,
			 ntohs(ib_spec->tcp_udp.val.src_port));

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, tcp_dport,
			 ntohs(ib_spec->tcp_udp.mask.dst_port));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_dport,
			 ntohs(ib_spec->tcp_udp.val.dst_port));
		break;
	case IB_FLOW_SPEC_UDP:
		if (FIELDS_NOT_SUPPORTED(ib_spec->tcp_udp.mask,
					 LAST_TCP_UDP_FIELD))
			return -EOPNOTSUPP;

		if (set_proto(headers_c, headers_v, 0xff, IPPROTO_UDP))
			return -EINVAL;

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_sport,
			 ntohs(ib_spec->tcp_udp.mask.src_port));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_sport,
			 ntohs(ib_spec->tcp_udp.val.src_port));

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_dport,
			 ntohs(ib_spec->tcp_udp.mask.dst_port));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_dport,
			 ntohs(ib_spec->tcp_udp.val.dst_port));
		break;
	case IB_FLOW_SPEC_GRE:
		if (ib_spec->gre.mask.c_ks_res0_ver)
			return -EOPNOTSUPP;

		if (set_proto(headers_c, headers_v, 0xff, IPPROTO_GRE))
			return -EINVAL;

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_protocol,
			 0xff);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol,
			 IPPROTO_GRE);

		MLX5_SET(fte_match_set_misc, misc_params_c, gre_protocol,
			 ntohs(ib_spec->gre.mask.protocol));
		MLX5_SET(fte_match_set_misc, misc_params_v, gre_protocol,
			 ntohs(ib_spec->gre.val.protocol));

		memcpy(MLX5_ADDR_OF(fte_match_set_misc, misc_params_c,
				    gre_key.nvgre.hi),
		       &ib_spec->gre.mask.key,
		       sizeof(ib_spec->gre.mask.key));
		memcpy(MLX5_ADDR_OF(fte_match_set_misc, misc_params_v,
				    gre_key.nvgre.hi),
		       &ib_spec->gre.val.key,
		       sizeof(ib_spec->gre.val.key));
		break;
	case IB_FLOW_SPEC_MPLS:
		switch (prev_type) {
		case IB_FLOW_SPEC_UDP:
			if (check_mpls_supp_fields(MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
						   ft_field_support.outer_first_mpls_over_udp),
						   &ib_spec->mpls.mask.tag))
				return -EOPNOTSUPP;

			memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_v,
					    outer_first_mpls_over_udp),
			       &ib_spec->mpls.val.tag,
			       sizeof(ib_spec->mpls.val.tag));
			memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_c,
					    outer_first_mpls_over_udp),
			       &ib_spec->mpls.mask.tag,
			       sizeof(ib_spec->mpls.mask.tag));
			break;
		case IB_FLOW_SPEC_GRE:
			if (check_mpls_supp_fields(MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
						   ft_field_support.outer_first_mpls_over_gre),
						   &ib_spec->mpls.mask.tag))
				return -EOPNOTSUPP;

			memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_v,
					    outer_first_mpls_over_gre),
			       &ib_spec->mpls.val.tag,
			       sizeof(ib_spec->mpls.val.tag));
			memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_c,
					    outer_first_mpls_over_gre),
			       &ib_spec->mpls.mask.tag,
			       sizeof(ib_spec->mpls.mask.tag));
			break;
		default:
			if (ib_spec->type & IB_FLOW_SPEC_INNER) {
				if (check_mpls_supp_fields(MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
							   ft_field_support.inner_first_mpls),
							   &ib_spec->mpls.mask.tag))
					return -EOPNOTSUPP;

				memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_v,
						    inner_first_mpls),
				       &ib_spec->mpls.val.tag,
				       sizeof(ib_spec->mpls.val.tag));
				memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_c,
						    inner_first_mpls),
				       &ib_spec->mpls.mask.tag,
				       sizeof(ib_spec->mpls.mask.tag));
			} else {
				if (check_mpls_supp_fields(MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
							   ft_field_support.outer_first_mpls),
							   &ib_spec->mpls.mask.tag))
					return -EOPNOTSUPP;

				memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_v,
						    outer_first_mpls),
				       &ib_spec->mpls.val.tag,
				       sizeof(ib_spec->mpls.val.tag));
				memcpy(MLX5_ADDR_OF(fte_match_set_misc2, misc_params2_c,
						    outer_first_mpls),
				       &ib_spec->mpls.mask.tag,
				       sizeof(ib_spec->mpls.mask.tag));
			}
		}
		break;
	case IB_FLOW_SPEC_VXLAN_TUNNEL:
		if (FIELDS_NOT_SUPPORTED(ib_spec->tunnel.mask,
					 LAST_TUNNEL_FIELD))
			return -EOPNOTSUPP;

		MLX5_SET(fte_match_set_misc, misc_params_c, vxlan_vni,
			 ntohl(ib_spec->tunnel.mask.tunnel_id));
		MLX5_SET(fte_match_set_misc, misc_params_v, vxlan_vni,
			 ntohl(ib_spec->tunnel.val.tunnel_id));
		break;
	case IB_FLOW_SPEC_ACTION_TAG:
		if (FIELDS_NOT_SUPPORTED(ib_spec->flow_tag,
					 LAST_FLOW_TAG_FIELD))
			return -EOPNOTSUPP;
		if (ib_spec->flow_tag.tag_id >= BIT(24))
			return -EINVAL;

		flow_context->flow_tag = ib_spec->flow_tag.tag_id;
		flow_context->flags |= FLOW_CONTEXT_HAS_TAG;
		break;
	case IB_FLOW_SPEC_ACTION_DROP:
		if (FIELDS_NOT_SUPPORTED(ib_spec->drop,
					 LAST_DROP_FIELD))
			return -EOPNOTSUPP;
		action->action |= MLX5_FLOW_CONTEXT_ACTION_DROP;
		break;
	case IB_FLOW_SPEC_ACTION_HANDLE:
		ret = parse_flow_flow_action(to_mflow_act(ib_spec->action.act),
			flow_attr->flags & IB_FLOW_ATTR_FLAGS_EGRESS, action);
		if (ret)
			return ret;
		break;
	case IB_FLOW_SPEC_ACTION_COUNT:
		if (FIELDS_NOT_SUPPORTED(ib_spec->flow_count,
					 LAST_COUNTERS_FIELD))
			return -EOPNOTSUPP;

		/* for now support only one counters spec per flow */
		if (action->action & MLX5_FLOW_CONTEXT_ACTION_COUNT)
			return -EINVAL;

		action->counters = ib_spec->flow_count.counters;
		action->action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* If a flow could catch both multicast and unicast packets,
 * it won't fall into the multicast flow steering table and this rule
 * could steal other multicast packets.
 */
static bool flow_is_multicast_only(const struct ib_flow_attr *ib_attr)
{
	union ib_flow_spec *flow_spec;

	if (ib_attr->type != IB_FLOW_ATTR_NORMAL ||
	    ib_attr->num_of_specs < 1)
		return false;

	flow_spec = (union ib_flow_spec *)(ib_attr + 1);
	if (flow_spec->type == IB_FLOW_SPEC_IPV4) {
		struct ib_flow_spec_ipv4 *ipv4_spec;

		ipv4_spec = (struct ib_flow_spec_ipv4 *)flow_spec;
		if (ipv4_is_multicast(ipv4_spec->val.dst_ip))
			return true;

		return false;
	}

	if (flow_spec->type == IB_FLOW_SPEC_ETH) {
		struct ib_flow_spec_eth *eth_spec;

		eth_spec = (struct ib_flow_spec_eth *)flow_spec;
		return is_multicast_ether_addr(eth_spec->mask.dst_mac) &&
		       is_multicast_ether_addr(eth_spec->val.dst_mac);
	}

	return false;
}

enum valid_spec {
	VALID_SPEC_INVALID,
	VALID_SPEC_VALID,
	VALID_SPEC_NA,
};

static enum valid_spec
is_valid_esp_aes_gcm(struct mlx5_core_dev *mdev,
		     const struct mlx5_flow_spec *spec,
		     const struct mlx5_flow_act *flow_act,
		     bool egress)
{
	const u32 *match_c = spec->match_criteria;
	bool is_crypto =
		(flow_act->action & (MLX5_FLOW_CONTEXT_ACTION_ENCRYPT |
				     MLX5_FLOW_CONTEXT_ACTION_DECRYPT));
	bool is_ipsec = mlx5_fs_is_ipsec_flow(match_c);
	bool is_drop = flow_act->action & MLX5_FLOW_CONTEXT_ACTION_DROP;

	/*
	 * Currently only crypto is supported in egress, when regular egress
	 * rules would be supported, always return VALID_SPEC_NA.
	 */
	if (!is_crypto)
		return VALID_SPEC_NA;

	return is_crypto && is_ipsec &&
		(!egress || (!is_drop &&
			     !(spec->flow_context.flags & FLOW_CONTEXT_HAS_TAG))) ?
		VALID_SPEC_VALID : VALID_SPEC_INVALID;
}

static bool is_valid_spec(struct mlx5_core_dev *mdev,
			  const struct mlx5_flow_spec *spec,
			  const struct mlx5_flow_act *flow_act,
			  bool egress)
{
	/* We curretly only support ipsec egress flow */
	return is_valid_esp_aes_gcm(mdev, spec, flow_act, egress) != VALID_SPEC_INVALID;
}

static bool is_valid_ethertype(struct mlx5_core_dev *mdev,
			       const struct ib_flow_attr *flow_attr,
			       bool check_inner)
{
	union ib_flow_spec *ib_spec = (union ib_flow_spec *)(flow_attr + 1);
	int match_ipv = check_inner ?
			MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					ft_field_support.inner_ip_version) :
			MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					ft_field_support.outer_ip_version);
	int inner_bit = check_inner ? IB_FLOW_SPEC_INNER : 0;
	bool ipv4_spec_valid, ipv6_spec_valid;
	unsigned int ip_spec_type = 0;
	bool has_ethertype = false;
	unsigned int spec_index;
	bool mask_valid = true;
	u16 eth_type = 0;
	bool type_valid;

	/* Validate that ethertype is correct */
	for (spec_index = 0; spec_index < flow_attr->num_of_specs; spec_index++) {
		if ((ib_spec->type == (IB_FLOW_SPEC_ETH | inner_bit)) &&
		    ib_spec->eth.mask.ether_type) {
			mask_valid = (ib_spec->eth.mask.ether_type ==
				      htons(0xffff));
			has_ethertype = true;
			eth_type = ntohs(ib_spec->eth.val.ether_type);
		} else if ((ib_spec->type == (IB_FLOW_SPEC_IPV4 | inner_bit)) ||
			   (ib_spec->type == (IB_FLOW_SPEC_IPV6 | inner_bit))) {
			ip_spec_type = ib_spec->type;
		}
		ib_spec = (void *)ib_spec + ib_spec->size;
	}

	type_valid = (!has_ethertype) || (!ip_spec_type);
	if (!type_valid && mask_valid) {
		ipv4_spec_valid = (eth_type == ETH_P_IP) &&
			(ip_spec_type == (IB_FLOW_SPEC_IPV4 | inner_bit));
		ipv6_spec_valid = (eth_type == ETH_P_IPV6) &&
			(ip_spec_type == (IB_FLOW_SPEC_IPV6 | inner_bit));

		type_valid = (ipv4_spec_valid) || (ipv6_spec_valid) ||
			     (((eth_type == ETH_P_MPLS_UC) ||
			       (eth_type == ETH_P_MPLS_MC)) && match_ipv);
	}

	return type_valid;
}

static bool is_valid_attr(struct mlx5_core_dev *mdev,
			  const struct ib_flow_attr *flow_attr)
{
	return is_valid_ethertype(mdev, flow_attr, false) &&
	       is_valid_ethertype(mdev, flow_attr, true);
}

static void put_flow_table(struct mlx5_ib_dev *dev,
			   struct mlx5_ib_flow_prio *prio, bool ft_added)
{
	prio->refcount -= !!ft_added;
	if (!prio->refcount) {
		mlx5_destroy_flow_table(prio->flow_table);
		prio->flow_table = NULL;
	}
}

static int mlx5_ib_destroy_flow(struct ib_flow *flow_id)
{
	struct mlx5_ib_flow_handler *handler = container_of(flow_id,
							  struct mlx5_ib_flow_handler,
							  ibflow);
	struct mlx5_ib_flow_handler *iter, *tmp;
	struct mlx5_ib_dev *dev = handler->dev;

	mutex_lock(&dev->flow_db->lock);

	list_for_each_entry_safe(iter, tmp, &handler->list, list) {
		mlx5_del_flow_rules(iter->rule);
		put_flow_table(dev, iter->prio, true);
		list_del(&iter->list);
		kfree(iter);
	}

	mlx5_del_flow_rules(handler->rule);
	put_flow_table(dev, handler->prio, true);
	mlx5_ib_counters_clear_description(handler->ibcounters);
	mutex_unlock(&dev->flow_db->lock);
	if (handler->flow_matcher)
		atomic_dec(&handler->flow_matcher->usecnt);
	kfree(handler);

	return 0;
}

static int ib_prio_to_core_prio(unsigned int priority, bool dont_trap)
{
	priority *= 2;
	if (!dont_trap)
		priority++;
	return priority;
}

enum flow_table_type {
	MLX5_IB_FT_RX,
	MLX5_IB_FT_TX
};

#define MLX5_FS_MAX_TYPES	 6
#define MLX5_FS_MAX_ENTRIES	 BIT(16)

static struct mlx5_ib_flow_prio *_get_prio(struct mlx5_flow_namespace *ns,
					   struct mlx5_ib_flow_prio *prio,
					   int priority,
					   int num_entries, int num_groups,
					   u32 flags)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table *ft;

	ft_attr.prio = priority;
	ft_attr.max_fte = num_entries;
	ft_attr.flags = flags;
	ft_attr.autogroup.max_num_groups = num_groups;
	ft = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
	if (IS_ERR(ft))
		return ERR_CAST(ft);

	prio->flow_table = ft;
	prio->refcount = 0;
	return prio;
}

static struct mlx5_ib_flow_prio *get_flow_table(struct mlx5_ib_dev *dev,
						struct ib_flow_attr *flow_attr,
						enum flow_table_type ft_type)
{
	bool dont_trap = flow_attr->flags & IB_FLOW_ATTR_FLAGS_DONT_TRAP;
	struct mlx5_flow_namespace *ns = NULL;
	enum mlx5_flow_namespace_type fn_type;
	struct mlx5_ib_flow_prio *prio;
	struct mlx5_flow_table *ft;
	int max_table_size;
	int num_entries;
	int num_groups;
	bool esw_encap;
	u32 flags = 0;
	int priority;

	max_table_size = BIT(MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev,
						       log_max_ft_size));
	esw_encap = mlx5_eswitch_get_encap_mode(dev->mdev) !=
		DEVLINK_ESWITCH_ENCAP_MODE_NONE;
	switch (flow_attr->type) {
	case IB_FLOW_ATTR_NORMAL:
		if (flow_is_multicast_only(flow_attr) && !dont_trap)
			priority = MLX5_IB_FLOW_MCAST_PRIO;
		else
			priority = ib_prio_to_core_prio(flow_attr->priority,
							dont_trap);
		if (ft_type == MLX5_IB_FT_RX) {
			fn_type = MLX5_FLOW_NAMESPACE_BYPASS;
			prio = &dev->flow_db->prios[priority];
			if (!dev->is_rep && !esw_encap &&
			    MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev, decap))
				flags |= MLX5_FLOW_TABLE_TUNNEL_EN_DECAP;
			if (!dev->is_rep && !esw_encap &&
			    MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev,
						      reformat_l3_tunnel_to_l2))
				flags |= MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
		} else {
			max_table_size = BIT(MLX5_CAP_FLOWTABLE_NIC_TX(
				dev->mdev, log_max_ft_size));
			fn_type = MLX5_FLOW_NAMESPACE_EGRESS;
			prio = &dev->flow_db->egress_prios[priority];
			if (!dev->is_rep && !esw_encap &&
			    MLX5_CAP_FLOWTABLE_NIC_TX(dev->mdev, reformat))
				flags |= MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
		}
		ns = mlx5_get_flow_namespace(dev->mdev, fn_type);
		num_entries = MLX5_FS_MAX_ENTRIES;
		num_groups = MLX5_FS_MAX_TYPES;
		break;
	case IB_FLOW_ATTR_ALL_DEFAULT:
	case IB_FLOW_ATTR_MC_DEFAULT:
		ns = mlx5_get_flow_namespace(dev->mdev,
					     MLX5_FLOW_NAMESPACE_LEFTOVERS);
		build_leftovers_ft_param(&priority, &num_entries, &num_groups);
		prio = &dev->flow_db->prios[MLX5_IB_FLOW_LEFTOVERS_PRIO];
		break;
	case IB_FLOW_ATTR_SNIFFER:
		if (!MLX5_CAP_FLOWTABLE(dev->mdev,
					allow_sniffer_and_nic_rx_shared_tir))
			return ERR_PTR(-EOPNOTSUPP);

		ns = mlx5_get_flow_namespace(
			dev->mdev, ft_type == MLX5_IB_FT_RX ?
					   MLX5_FLOW_NAMESPACE_SNIFFER_RX :
					   MLX5_FLOW_NAMESPACE_SNIFFER_TX);

		prio = &dev->flow_db->sniffer[ft_type];
		priority = 0;
		num_entries = 1;
		num_groups = 1;
		break;
	default:
		break;
	}

	if (!ns)
		return ERR_PTR(-EOPNOTSUPP);

	max_table_size = min_t(int, num_entries, max_table_size);

	ft = prio->flow_table;
	if (!ft)
		return _get_prio(ns, prio, priority, max_table_size, num_groups,
				 flags);

	return prio;
}

static void set_underlay_qp(struct mlx5_ib_dev *dev,
			    struct mlx5_flow_spec *spec,
			    u32 underlay_qpn)
{
	void *misc_params_c = MLX5_ADDR_OF(fte_match_param,
					   spec->match_criteria,
					   misc_parameters);
	void *misc_params_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
					   misc_parameters);

	if (underlay_qpn &&
	    MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev,
				      ft_field_support.bth_dst_qp)) {
		MLX5_SET(fte_match_set_misc,
			 misc_params_v, bth_dst_qp, underlay_qpn);
		MLX5_SET(fte_match_set_misc,
			 misc_params_c, bth_dst_qp, 0xffffff);
	}
}

static void mlx5_ib_set_rule_source_port(struct mlx5_ib_dev *dev,
					 struct mlx5_flow_spec *spec,
					 struct mlx5_eswitch_rep *rep)
{
	struct mlx5_eswitch *esw = dev->mdev->priv.eswitch;
	void *misc;

	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters_2);

		MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_for_match(rep->esw,
								   rep->vport));
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters_2);

		MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_mask());
	} else {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);

		MLX5_SET(fte_match_set_misc, misc, source_port, rep->vport);

		misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters);

		MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);
	}
}

static struct mlx5_ib_flow_handler *_create_flow_rule(struct mlx5_ib_dev *dev,
						      struct mlx5_ib_flow_prio *ft_prio,
						      const struct ib_flow_attr *flow_attr,
						      struct mlx5_flow_destination *dst,
						      u32 underlay_qpn,
						      struct mlx5_ib_create_flow *ucmd)
{
	struct mlx5_flow_table	*ft = ft_prio->flow_table;
	struct mlx5_ib_flow_handler *handler;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_destination dest_arr[2] = {};
	struct mlx5_flow_destination *rule_dst = dest_arr;
	const void *ib_flow = (const void *)flow_attr + sizeof(*flow_attr);
	unsigned int spec_index;
	u32 prev_type = 0;
	int err = 0;
	int dest_num = 0;
	bool is_egress = flow_attr->flags & IB_FLOW_ATTR_FLAGS_EGRESS;

	if (!is_valid_attr(dev->mdev, flow_attr))
		return ERR_PTR(-EINVAL);

	if (dev->is_rep && is_egress)
		return ERR_PTR(-EINVAL);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	handler = kzalloc(sizeof(*handler), GFP_KERNEL);
	if (!handler || !spec) {
		err = -ENOMEM;
		goto free;
	}

	INIT_LIST_HEAD(&handler->list);

	for (spec_index = 0; spec_index < flow_attr->num_of_specs; spec_index++) {
		err = parse_flow_attr(dev->mdev, spec,
				      ib_flow, flow_attr, &flow_act,
				      prev_type);
		if (err < 0)
			goto free;

		prev_type = ((union ib_flow_spec *)ib_flow)->type;
		ib_flow += ((union ib_flow_spec *)ib_flow)->size;
	}

	if (dst && !(flow_act.action & MLX5_FLOW_CONTEXT_ACTION_DROP)) {
		memcpy(&dest_arr[0], dst, sizeof(*dst));
		dest_num++;
	}

	if (!flow_is_multicast_only(flow_attr))
		set_underlay_qp(dev, spec, underlay_qpn);

	if (dev->is_rep && flow_attr->type != IB_FLOW_ATTR_SNIFFER) {
		struct mlx5_eswitch_rep *rep;

		rep = dev->port[flow_attr->port - 1].rep;
		if (!rep) {
			err = -EINVAL;
			goto free;
		}

		mlx5_ib_set_rule_source_port(dev, spec, rep);
	}

	spec->match_criteria_enable = get_match_criteria_enable(spec->match_criteria);

	if (is_egress &&
	    !is_valid_spec(dev->mdev, spec, &flow_act, is_egress)) {
		err = -EINVAL;
		goto free;
	}

	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		struct mlx5_ib_mcounters *mcounters;

		err = mlx5_ib_flow_counters_set_data(flow_act.counters, ucmd);
		if (err)
			goto free;

		mcounters = to_mcounters(flow_act.counters);
		handler->ibcounters = flow_act.counters;
		dest_arr[dest_num].type =
			MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest_arr[dest_num].counter_id =
			mlx5_fc_id(mcounters->hw_cntrs_hndl);
		dest_num++;
	}

	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_DROP) {
		if (!dest_num)
			rule_dst = NULL;
	} else {
		if (flow_attr->flags & IB_FLOW_ATTR_FLAGS_DONT_TRAP)
			flow_act.action |=
				MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO;
		if (is_egress)
			flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_ALLOW;
		else if (dest_num)
			flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	}

	if ((spec->flow_context.flags & FLOW_CONTEXT_HAS_TAG)  &&
	    (flow_attr->type == IB_FLOW_ATTR_ALL_DEFAULT ||
	     flow_attr->type == IB_FLOW_ATTR_MC_DEFAULT)) {
		mlx5_ib_warn(dev, "Flow tag %u and attribute type %x isn't allowed in leftovers\n",
			     spec->flow_context.flow_tag, flow_attr->type);
		err = -EINVAL;
		goto free;
	}
	handler->rule = mlx5_add_flow_rules(ft, spec,
					    &flow_act,
					    rule_dst, dest_num);

	if (IS_ERR(handler->rule)) {
		err = PTR_ERR(handler->rule);
		goto free;
	}

	ft_prio->refcount++;
	handler->prio = ft_prio;
	handler->dev = dev;

	ft_prio->flow_table = ft;
free:
	if (err && handler) {
		mlx5_ib_counters_clear_description(handler->ibcounters);
		kfree(handler);
	}
	kvfree(spec);
	return err ? ERR_PTR(err) : handler;
}

static struct mlx5_ib_flow_handler *create_flow_rule(struct mlx5_ib_dev *dev,
						     struct mlx5_ib_flow_prio *ft_prio,
						     const struct ib_flow_attr *flow_attr,
						     struct mlx5_flow_destination *dst)
{
	return _create_flow_rule(dev, ft_prio, flow_attr, dst, 0, NULL);
}

enum {
	LEFTOVERS_MC,
	LEFTOVERS_UC,
};

static struct mlx5_ib_flow_handler *create_leftovers_rule(struct mlx5_ib_dev *dev,
							  struct mlx5_ib_flow_prio *ft_prio,
							  struct ib_flow_attr *flow_attr,
							  struct mlx5_flow_destination *dst)
{
	struct mlx5_ib_flow_handler *handler_ucast = NULL;
	struct mlx5_ib_flow_handler *handler = NULL;

	static struct {
		struct ib_flow_attr	flow_attr;
		struct ib_flow_spec_eth eth_flow;
	} leftovers_specs[] = {
		[LEFTOVERS_MC] = {
			.flow_attr = {
				.num_of_specs = 1,
				.size = sizeof(leftovers_specs[0])
			},
			.eth_flow = {
				.type = IB_FLOW_SPEC_ETH,
				.size = sizeof(struct ib_flow_spec_eth),
				.mask = {.dst_mac = {0x1} },
				.val =  {.dst_mac = {0x1} }
			}
		},
		[LEFTOVERS_UC] = {
			.flow_attr = {
				.num_of_specs = 1,
				.size = sizeof(leftovers_specs[0])
			},
			.eth_flow = {
				.type = IB_FLOW_SPEC_ETH,
				.size = sizeof(struct ib_flow_spec_eth),
				.mask = {.dst_mac = {0x1} },
				.val = {.dst_mac = {} }
			}
		}
	};

	handler = create_flow_rule(dev, ft_prio,
				   &leftovers_specs[LEFTOVERS_MC].flow_attr,
				   dst);
	if (!IS_ERR(handler) &&
	    flow_attr->type == IB_FLOW_ATTR_ALL_DEFAULT) {
		handler_ucast = create_flow_rule(dev, ft_prio,
						 &leftovers_specs[LEFTOVERS_UC].flow_attr,
						 dst);
		if (IS_ERR(handler_ucast)) {
			mlx5_del_flow_rules(handler->rule);
			ft_prio->refcount--;
			kfree(handler);
			handler = handler_ucast;
		} else {
			list_add(&handler_ucast->list, &handler->list);
		}
	}

	return handler;
}

static struct mlx5_ib_flow_handler *create_sniffer_rule(struct mlx5_ib_dev *dev,
							struct mlx5_ib_flow_prio *ft_rx,
							struct mlx5_ib_flow_prio *ft_tx,
							struct mlx5_flow_destination *dst)
{
	struct mlx5_ib_flow_handler *handler_rx;
	struct mlx5_ib_flow_handler *handler_tx;
	int err;
	static const struct ib_flow_attr flow_attr  = {
		.num_of_specs = 0,
		.type = IB_FLOW_ATTR_SNIFFER,
		.size = sizeof(flow_attr)
	};

	handler_rx = create_flow_rule(dev, ft_rx, &flow_attr, dst);
	if (IS_ERR(handler_rx)) {
		err = PTR_ERR(handler_rx);
		goto err;
	}

	handler_tx = create_flow_rule(dev, ft_tx, &flow_attr, dst);
	if (IS_ERR(handler_tx)) {
		err = PTR_ERR(handler_tx);
		goto err_tx;
	}

	list_add(&handler_tx->list, &handler_rx->list);

	return handler_rx;

err_tx:
	mlx5_del_flow_rules(handler_rx->rule);
	ft_rx->refcount--;
	kfree(handler_rx);
err:
	return ERR_PTR(err);
}

static struct ib_flow *mlx5_ib_create_flow(struct ib_qp *qp,
					   struct ib_flow_attr *flow_attr,
					   struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	struct mlx5_ib_flow_handler *handler = NULL;
	struct mlx5_flow_destination *dst = NULL;
	struct mlx5_ib_flow_prio *ft_prio_tx = NULL;
	struct mlx5_ib_flow_prio *ft_prio;
	bool is_egress = flow_attr->flags & IB_FLOW_ATTR_FLAGS_EGRESS;
	struct mlx5_ib_create_flow *ucmd = NULL, ucmd_hdr;
	size_t min_ucmd_sz, required_ucmd_sz;
	int err;
	int underlay_qpn;

	if (udata && udata->inlen) {
		min_ucmd_sz = offsetofend(struct mlx5_ib_create_flow, reserved);
		if (udata->inlen < min_ucmd_sz)
			return ERR_PTR(-EOPNOTSUPP);

		err = ib_copy_from_udata(&ucmd_hdr, udata, min_ucmd_sz);
		if (err)
			return ERR_PTR(err);

		/* currently supports only one counters data */
		if (ucmd_hdr.ncounters_data > 1)
			return ERR_PTR(-EINVAL);

		required_ucmd_sz = min_ucmd_sz +
			sizeof(struct mlx5_ib_flow_counters_data) *
			ucmd_hdr.ncounters_data;
		if (udata->inlen > required_ucmd_sz &&
		    !ib_is_udata_cleared(udata, required_ucmd_sz,
					 udata->inlen - required_ucmd_sz))
			return ERR_PTR(-EOPNOTSUPP);

		ucmd = kzalloc(required_ucmd_sz, GFP_KERNEL);
		if (!ucmd)
			return ERR_PTR(-ENOMEM);

		err = ib_copy_from_udata(ucmd, udata, required_ucmd_sz);
		if (err)
			goto free_ucmd;
	}

	if (flow_attr->priority > MLX5_IB_FLOW_LAST_PRIO) {
		err = -ENOMEM;
		goto free_ucmd;
	}

	if (flow_attr->flags &
	    ~(IB_FLOW_ATTR_FLAGS_DONT_TRAP | IB_FLOW_ATTR_FLAGS_EGRESS)) {
		err = -EINVAL;
		goto free_ucmd;
	}

	if (is_egress &&
	    (flow_attr->type == IB_FLOW_ATTR_ALL_DEFAULT ||
	     flow_attr->type == IB_FLOW_ATTR_MC_DEFAULT)) {
		err = -EINVAL;
		goto free_ucmd;
	}

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst) {
		err = -ENOMEM;
		goto free_ucmd;
	}

	mutex_lock(&dev->flow_db->lock);

	ft_prio = get_flow_table(dev, flow_attr,
				 is_egress ? MLX5_IB_FT_TX : MLX5_IB_FT_RX);
	if (IS_ERR(ft_prio)) {
		err = PTR_ERR(ft_prio);
		goto unlock;
	}
	if (flow_attr->type == IB_FLOW_ATTR_SNIFFER) {
		ft_prio_tx = get_flow_table(dev, flow_attr, MLX5_IB_FT_TX);
		if (IS_ERR(ft_prio_tx)) {
			err = PTR_ERR(ft_prio_tx);
			ft_prio_tx = NULL;
			goto destroy_ft;
		}
	}

	if (is_egress) {
		dst->type = MLX5_FLOW_DESTINATION_TYPE_PORT;
	} else {
		dst->type = MLX5_FLOW_DESTINATION_TYPE_TIR;
		if (mqp->is_rss)
			dst->tir_num = mqp->rss_qp.tirn;
		else
			dst->tir_num = mqp->raw_packet_qp.rq.tirn;
	}

	switch (flow_attr->type) {
	case IB_FLOW_ATTR_NORMAL:
		underlay_qpn = (mqp->flags & IB_QP_CREATE_SOURCE_QPN) ?
				       mqp->underlay_qpn :
				       0;
		handler = _create_flow_rule(dev, ft_prio, flow_attr, dst,
					    underlay_qpn, ucmd);
		break;
	case IB_FLOW_ATTR_ALL_DEFAULT:
	case IB_FLOW_ATTR_MC_DEFAULT:
		handler = create_leftovers_rule(dev, ft_prio, flow_attr, dst);
		break;
	case IB_FLOW_ATTR_SNIFFER:
		handler = create_sniffer_rule(dev, ft_prio, ft_prio_tx, dst);
		break;
	default:
		err = -EINVAL;
		goto destroy_ft;
	}

	if (IS_ERR(handler)) {
		err = PTR_ERR(handler);
		handler = NULL;
		goto destroy_ft;
	}

	mutex_unlock(&dev->flow_db->lock);
	kfree(dst);
	kfree(ucmd);

	return &handler->ibflow;

destroy_ft:
	put_flow_table(dev, ft_prio, false);
	if (ft_prio_tx)
		put_flow_table(dev, ft_prio_tx, false);
unlock:
	mutex_unlock(&dev->flow_db->lock);
	kfree(dst);
free_ucmd:
	kfree(ucmd);
	return ERR_PTR(err);
}

static struct mlx5_ib_flow_prio *
_get_flow_table(struct mlx5_ib_dev *dev,
		struct mlx5_ib_flow_matcher *fs_matcher,
		bool mcast)
{
	struct mlx5_flow_namespace *ns = NULL;
	struct mlx5_ib_flow_prio *prio = NULL;
	int max_table_size = 0;
	bool esw_encap;
	u32 flags = 0;
	int priority;

	if (mcast)
		priority = MLX5_IB_FLOW_MCAST_PRIO;
	else
		priority = ib_prio_to_core_prio(fs_matcher->priority, false);

	esw_encap = mlx5_eswitch_get_encap_mode(dev->mdev) !=
		DEVLINK_ESWITCH_ENCAP_MODE_NONE;
	switch (fs_matcher->ns_type) {
	case MLX5_FLOW_NAMESPACE_BYPASS:
		max_table_size = BIT(
			MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev, log_max_ft_size));
		if (MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev, decap) && !esw_encap)
			flags |= MLX5_FLOW_TABLE_TUNNEL_EN_DECAP;
		if (MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev,
					      reformat_l3_tunnel_to_l2) &&
		    !esw_encap)
			flags |= MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
		break;
	case MLX5_FLOW_NAMESPACE_EGRESS:
		max_table_size = BIT(
			MLX5_CAP_FLOWTABLE_NIC_TX(dev->mdev, log_max_ft_size));
		if (MLX5_CAP_FLOWTABLE_NIC_TX(dev->mdev, reformat) &&
		    !esw_encap)
			flags |= MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
		break;
	case MLX5_FLOW_NAMESPACE_FDB:
		max_table_size = BIT(
			MLX5_CAP_ESW_FLOWTABLE_FDB(dev->mdev, log_max_ft_size));
		if (MLX5_CAP_ESW_FLOWTABLE_FDB(dev->mdev, decap) && esw_encap)
			flags |= MLX5_FLOW_TABLE_TUNNEL_EN_DECAP;
		if (MLX5_CAP_ESW_FLOWTABLE_FDB(dev->mdev,
					       reformat_l3_tunnel_to_l2) &&
		    esw_encap)
			flags |= MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
		priority = FDB_BYPASS_PATH;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_RX:
		max_table_size = BIT(
			MLX5_CAP_FLOWTABLE_RDMA_RX(dev->mdev, log_max_ft_size));
		priority = fs_matcher->priority;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_TX:
		max_table_size = BIT(
			MLX5_CAP_FLOWTABLE_RDMA_TX(dev->mdev, log_max_ft_size));
		priority = fs_matcher->priority;
		break;
	default:
		break;
	}

	max_table_size = min_t(int, max_table_size, MLX5_FS_MAX_ENTRIES);

	ns = mlx5_get_flow_namespace(dev->mdev, fs_matcher->ns_type);
	if (!ns)
		return ERR_PTR(-EOPNOTSUPP);

	switch (fs_matcher->ns_type) {
	case MLX5_FLOW_NAMESPACE_BYPASS:
		prio = &dev->flow_db->prios[priority];
		break;
	case MLX5_FLOW_NAMESPACE_EGRESS:
		prio = &dev->flow_db->egress_prios[priority];
		break;
	case MLX5_FLOW_NAMESPACE_FDB:
		prio = &dev->flow_db->fdb;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_RX:
		prio = &dev->flow_db->rdma_rx[priority];
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_TX:
		prio = &dev->flow_db->rdma_tx[priority];
		break;
	default: return ERR_PTR(-EINVAL);
	}

	if (!prio)
		return ERR_PTR(-EINVAL);

	if (prio->flow_table)
		return prio;

	return _get_prio(ns, prio, priority, max_table_size,
			 MLX5_FS_MAX_TYPES, flags);
}

static struct mlx5_ib_flow_handler *
_create_raw_flow_rule(struct mlx5_ib_dev *dev,
		      struct mlx5_ib_flow_prio *ft_prio,
		      struct mlx5_flow_destination *dst,
		      struct mlx5_ib_flow_matcher  *fs_matcher,
		      struct mlx5_flow_context *flow_context,
		      struct mlx5_flow_act *flow_act,
		      void *cmd_in, int inlen,
		      int dst_num)
{
	struct mlx5_ib_flow_handler *handler;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_table *ft = ft_prio->flow_table;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	handler = kzalloc(sizeof(*handler), GFP_KERNEL);
	if (!handler || !spec) {
		err = -ENOMEM;
		goto free;
	}

	INIT_LIST_HEAD(&handler->list);

	memcpy(spec->match_value, cmd_in, inlen);
	memcpy(spec->match_criteria, fs_matcher->matcher_mask.match_params,
	       fs_matcher->mask_len);
	spec->match_criteria_enable = fs_matcher->match_criteria_enable;
	spec->flow_context = *flow_context;

	handler->rule = mlx5_add_flow_rules(ft, spec,
					    flow_act, dst, dst_num);

	if (IS_ERR(handler->rule)) {
		err = PTR_ERR(handler->rule);
		goto free;
	}

	ft_prio->refcount++;
	handler->prio = ft_prio;
	handler->dev = dev;
	ft_prio->flow_table = ft;

free:
	if (err)
		kfree(handler);
	kvfree(spec);
	return err ? ERR_PTR(err) : handler;
}

static bool raw_fs_is_multicast(struct mlx5_ib_flow_matcher *fs_matcher,
				void *match_v)
{
	void *match_c;
	void *match_v_set_lyr_2_4, *match_c_set_lyr_2_4;
	void *dmac, *dmac_mask;
	void *ipv4, *ipv4_mask;

	if (!(fs_matcher->match_criteria_enable &
	      (1 << MATCH_CRITERIA_ENABLE_OUTER_BIT)))
		return false;

	match_c = fs_matcher->matcher_mask.match_params;
	match_v_set_lyr_2_4 = MLX5_ADDR_OF(fte_match_param, match_v,
					   outer_headers);
	match_c_set_lyr_2_4 = MLX5_ADDR_OF(fte_match_param, match_c,
					   outer_headers);

	dmac = MLX5_ADDR_OF(fte_match_set_lyr_2_4, match_v_set_lyr_2_4,
			    dmac_47_16);
	dmac_mask = MLX5_ADDR_OF(fte_match_set_lyr_2_4, match_c_set_lyr_2_4,
				 dmac_47_16);

	if (is_multicast_ether_addr(dmac) &&
	    is_multicast_ether_addr(dmac_mask))
		return true;

	ipv4 = MLX5_ADDR_OF(fte_match_set_lyr_2_4, match_v_set_lyr_2_4,
			    dst_ipv4_dst_ipv6.ipv4_layout.ipv4);

	ipv4_mask = MLX5_ADDR_OF(fte_match_set_lyr_2_4, match_c_set_lyr_2_4,
				 dst_ipv4_dst_ipv6.ipv4_layout.ipv4);

	if (ipv4_is_multicast(*(__be32 *)(ipv4)) &&
	    ipv4_is_multicast(*(__be32 *)(ipv4_mask)))
		return true;

	return false;
}

static struct mlx5_ib_flow_handler *raw_fs_rule_add(
	struct mlx5_ib_dev *dev, struct mlx5_ib_flow_matcher *fs_matcher,
	struct mlx5_flow_context *flow_context, struct mlx5_flow_act *flow_act,
	u32 counter_id, void *cmd_in, int inlen, int dest_id, int dest_type)
{
	struct mlx5_flow_destination *dst;
	struct mlx5_ib_flow_prio *ft_prio;
	struct mlx5_ib_flow_handler *handler;
	int dst_num = 0;
	bool mcast;
	int err;

	if (fs_matcher->flow_type != MLX5_IB_FLOW_TYPE_NORMAL)
		return ERR_PTR(-EOPNOTSUPP);

	if (fs_matcher->priority > MLX5_IB_FLOW_LAST_PRIO)
		return ERR_PTR(-ENOMEM);

	dst = kcalloc(2, sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return ERR_PTR(-ENOMEM);

	mcast = raw_fs_is_multicast(fs_matcher, cmd_in);
	mutex_lock(&dev->flow_db->lock);

	ft_prio = _get_flow_table(dev, fs_matcher, mcast);
	if (IS_ERR(ft_prio)) {
		err = PTR_ERR(ft_prio);
		goto unlock;
	}

	switch (dest_type) {
	case MLX5_FLOW_DESTINATION_TYPE_TIR:
		dst[dst_num].type = dest_type;
		dst[dst_num++].tir_num = dest_id;
		flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		break;
	case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
		dst[dst_num].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM;
		dst[dst_num++].ft_num = dest_id;
		flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		break;
	case MLX5_FLOW_DESTINATION_TYPE_PORT:
		dst[dst_num++].type = MLX5_FLOW_DESTINATION_TYPE_PORT;
		flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_ALLOW;
		break;
	default:
		break;
	}

	if (flow_act->action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		dst[dst_num].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dst[dst_num].counter_id = counter_id;
		dst_num++;
	}

	handler = _create_raw_flow_rule(dev, ft_prio, dst_num ? dst : NULL,
					fs_matcher, flow_context, flow_act,
					cmd_in, inlen, dst_num);

	if (IS_ERR(handler)) {
		err = PTR_ERR(handler);
		goto destroy_ft;
	}

	mutex_unlock(&dev->flow_db->lock);
	atomic_inc(&fs_matcher->usecnt);
	handler->flow_matcher = fs_matcher;

	kfree(dst);

	return handler;

destroy_ft:
	put_flow_table(dev, ft_prio, false);
unlock:
	mutex_unlock(&dev->flow_db->lock);
	kfree(dst);

	return ERR_PTR(err);
}

static u32 mlx5_ib_flow_action_flags_to_accel_xfrm_flags(u32 mlx5_flags)
{
	u32 flags = 0;

	if (mlx5_flags & MLX5_IB_UAPI_FLOW_ACTION_FLAGS_REQUIRE_METADATA)
		flags |= MLX5_ACCEL_XFRM_FLAG_REQUIRE_METADATA;

	return flags;
}

#define MLX5_FLOW_ACTION_ESP_CREATE_LAST_SUPPORTED                             \
	MLX5_IB_UAPI_FLOW_ACTION_FLAGS_REQUIRE_METADATA
static struct ib_flow_action *
mlx5_ib_create_flow_action_esp(struct ib_device *device,
			       const struct ib_flow_action_attrs_esp *attr,
			       struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_dev *mdev = to_mdev(device);
	struct ib_uverbs_flow_action_esp_keymat_aes_gcm *aes_gcm;
	struct mlx5_accel_esp_xfrm_attrs accel_attrs = {};
	struct mlx5_ib_flow_action *action;
	u64 action_flags;
	u64 flags;
	int err = 0;

	err = uverbs_get_flags64(
		&action_flags, attrs, MLX5_IB_ATTR_CREATE_FLOW_ACTION_FLAGS,
		((MLX5_FLOW_ACTION_ESP_CREATE_LAST_SUPPORTED << 1) - 1));
	if (err)
		return ERR_PTR(err);

	flags = mlx5_ib_flow_action_flags_to_accel_xfrm_flags(action_flags);

	/* We current only support a subset of the standard features. Only a
	 * keymat of type AES_GCM, with icv_len == 16, iv_algo == SEQ and esn
	 * (with overlap). Full offload mode isn't supported.
	 */
	if (!attr->keymat || attr->replay || attr->encap ||
	    attr->spi || attr->seq || attr->tfc_pad ||
	    attr->hard_limit_pkts ||
	    (attr->flags & ~(IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED |
			     IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ENCRYPT)))
		return ERR_PTR(-EOPNOTSUPP);

	if (attr->keymat->protocol !=
	    IB_UVERBS_FLOW_ACTION_ESP_KEYMAT_AES_GCM)
		return ERR_PTR(-EOPNOTSUPP);

	aes_gcm = &attr->keymat->keymat.aes_gcm;

	if (aes_gcm->icv_len != 16 ||
	    aes_gcm->iv_algo != IB_UVERBS_FLOW_ACTION_IV_ALGO_SEQ)
		return ERR_PTR(-EOPNOTSUPP);

	action = kmalloc(sizeof(*action), GFP_KERNEL);
	if (!action)
		return ERR_PTR(-ENOMEM);

	action->esp_aes_gcm.ib_flags = attr->flags;
	memcpy(&accel_attrs.keymat.aes_gcm.aes_key, &aes_gcm->aes_key,
	       sizeof(accel_attrs.keymat.aes_gcm.aes_key));
	accel_attrs.keymat.aes_gcm.key_len = aes_gcm->key_len * 8;
	memcpy(&accel_attrs.keymat.aes_gcm.salt, &aes_gcm->salt,
	       sizeof(accel_attrs.keymat.aes_gcm.salt));
	memcpy(&accel_attrs.keymat.aes_gcm.seq_iv, &aes_gcm->iv,
	       sizeof(accel_attrs.keymat.aes_gcm.seq_iv));
	accel_attrs.keymat.aes_gcm.icv_len = aes_gcm->icv_len * 8;
	accel_attrs.keymat.aes_gcm.iv_algo = MLX5_ACCEL_ESP_AES_GCM_IV_ALGO_SEQ;
	accel_attrs.keymat_type = MLX5_ACCEL_ESP_KEYMAT_AES_GCM;

	accel_attrs.esn = attr->esn;
	if (attr->flags & IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED)
		accel_attrs.flags |= MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED;
	if (attr->flags & IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW)
		accel_attrs.flags |= MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP;

	if (attr->flags & IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ENCRYPT)
		accel_attrs.action |= MLX5_ACCEL_ESP_ACTION_ENCRYPT;

	action->esp_aes_gcm.ctx =
		mlx5_accel_esp_create_xfrm(mdev->mdev, &accel_attrs, flags);
	if (IS_ERR(action->esp_aes_gcm.ctx)) {
		err = PTR_ERR(action->esp_aes_gcm.ctx);
		goto err_parse;
	}

	action->esp_aes_gcm.ib_flags = attr->flags;

	return &action->ib_action;

err_parse:
	kfree(action);
	return ERR_PTR(err);
}

static int
mlx5_ib_modify_flow_action_esp(struct ib_flow_action *action,
			       const struct ib_flow_action_attrs_esp *attr,
			       struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_flow_action *maction = to_mflow_act(action);
	struct mlx5_accel_esp_xfrm_attrs accel_attrs;
	int err = 0;

	if (attr->keymat || attr->replay || attr->encap ||
	    attr->spi || attr->seq || attr->tfc_pad ||
	    attr->hard_limit_pkts ||
	    (attr->flags & ~(IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED |
			     IB_FLOW_ACTION_ESP_FLAGS_MOD_ESP_ATTRS |
			     IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW)))
		return -EOPNOTSUPP;

	/* Only the ESN value or the MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP can
	 * be modified.
	 */
	if (!(maction->esp_aes_gcm.ib_flags &
	      IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED) &&
	    attr->flags & (IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED |
			   IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW))
		return -EINVAL;

	memcpy(&accel_attrs, &maction->esp_aes_gcm.ctx->attrs,
	       sizeof(accel_attrs));

	accel_attrs.esn = attr->esn;
	if (attr->flags & IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW)
		accel_attrs.flags |= MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP;
	else
		accel_attrs.flags &= ~MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP;

	err = mlx5_accel_esp_modify_xfrm(maction->esp_aes_gcm.ctx,
					 &accel_attrs);
	if (err)
		return err;

	maction->esp_aes_gcm.ib_flags &=
		~IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW;
	maction->esp_aes_gcm.ib_flags |=
		attr->flags & IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW;

	return 0;
}

static void destroy_flow_action_raw(struct mlx5_ib_flow_action *maction)
{
	switch (maction->flow_action_raw.sub_type) {
	case MLX5_IB_FLOW_ACTION_MODIFY_HEADER:
		mlx5_modify_header_dealloc(maction->flow_action_raw.dev->mdev,
					   maction->flow_action_raw.modify_hdr);
		break;
	case MLX5_IB_FLOW_ACTION_PACKET_REFORMAT:
		mlx5_packet_reformat_dealloc(maction->flow_action_raw.dev->mdev,
					     maction->flow_action_raw.pkt_reformat);
		break;
	case MLX5_IB_FLOW_ACTION_DECAP:
		break;
	default:
		break;
	}
}

static int mlx5_ib_destroy_flow_action(struct ib_flow_action *action)
{
	struct mlx5_ib_flow_action *maction = to_mflow_act(action);

	switch (action->type) {
	case IB_FLOW_ACTION_ESP:
		/*
		 * We only support aes_gcm by now, so we implicitly know this is
		 * the underline crypto.
		 */
		mlx5_accel_esp_destroy_xfrm(maction->esp_aes_gcm.ctx);
		break;
	case IB_FLOW_ACTION_UNSPECIFIED:
		destroy_flow_action_raw(maction);
		break;
	default:
		WARN_ON(true);
		break;
	}

	kfree(maction);
	return 0;
}

static int
mlx5_ib_ft_type_to_namespace(enum mlx5_ib_uapi_flow_table_type table_type,
			     enum mlx5_flow_namespace_type *namespace)
{
	switch (table_type) {
	case MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX:
		*namespace = MLX5_FLOW_NAMESPACE_BYPASS;
		break;
	case MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX:
		*namespace = MLX5_FLOW_NAMESPACE_EGRESS;
		break;
	case MLX5_IB_UAPI_FLOW_TABLE_TYPE_FDB:
		*namespace = MLX5_FLOW_NAMESPACE_FDB;
		break;
	case MLX5_IB_UAPI_FLOW_TABLE_TYPE_RDMA_RX:
		*namespace = MLX5_FLOW_NAMESPACE_RDMA_RX;
		break;
	case MLX5_IB_UAPI_FLOW_TABLE_TYPE_RDMA_TX:
		*namespace = MLX5_FLOW_NAMESPACE_RDMA_TX;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct uverbs_attr_spec mlx5_ib_flow_type[] = {
	[MLX5_IB_FLOW_TYPE_NORMAL] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		.u.ptr = {
			.len = sizeof(u16), /* data is priority */
			.min_len = sizeof(u16),
		}
	},
	[MLX5_IB_FLOW_TYPE_SNIFFER] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
	[MLX5_IB_FLOW_TYPE_ALL_DEFAULT] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
	[MLX5_IB_FLOW_TYPE_MC_DEFAULT] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
};

static bool is_flow_dest(void *obj, int *dest_id, int *dest_type)
{
	struct devx_obj *devx_obj = obj;
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, devx_obj->dinbox, opcode);

	switch (opcode) {
	case MLX5_CMD_OP_DESTROY_TIR:
		*dest_type = MLX5_FLOW_DESTINATION_TYPE_TIR;
		*dest_id = MLX5_GET(general_obj_in_cmd_hdr, devx_obj->dinbox,
				    obj_id);
		return true;

	case MLX5_CMD_OP_DESTROY_FLOW_TABLE:
		*dest_type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		*dest_id = MLX5_GET(destroy_flow_table_in, devx_obj->dinbox,
				    table_id);
		return true;
	default:
		return false;
	}
}

static int get_dests(struct uverbs_attr_bundle *attrs,
		     struct mlx5_ib_flow_matcher *fs_matcher, int *dest_id,
		     int *dest_type, struct ib_qp **qp, u32 *flags)
{
	bool dest_devx, dest_qp;
	void *devx_obj;
	int err;

	dest_devx = uverbs_attr_is_valid(attrs,
					 MLX5_IB_ATTR_CREATE_FLOW_DEST_DEVX);
	dest_qp = uverbs_attr_is_valid(attrs,
				       MLX5_IB_ATTR_CREATE_FLOW_DEST_QP);

	*flags = 0;
	err = uverbs_get_flags32(flags, attrs, MLX5_IB_ATTR_CREATE_FLOW_FLAGS,
				 MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DEFAULT_MISS |
					 MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DROP);
	if (err)
		return err;

	/* Both flags are not allowed */
	if (*flags & MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DEFAULT_MISS &&
	    *flags & MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DROP)
		return -EINVAL;

	if (fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_BYPASS) {
		if (dest_devx && (dest_qp || *flags))
			return -EINVAL;
		else if (dest_qp && *flags)
			return -EINVAL;
	}

	/* Allow only DEVX object, drop as dest for FDB */
	if (fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_FDB && !(dest_devx ||
	     (*flags & MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DROP)))
		return -EINVAL;

	/* Allow only DEVX object or QP as dest when inserting to RDMA_RX */
	if ((fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_RDMA_RX) &&
	    ((!dest_devx && !dest_qp) || (dest_devx && dest_qp)))
		return -EINVAL;

	*qp = NULL;
	if (dest_devx) {
		devx_obj =
			uverbs_attr_get_obj(attrs,
					    MLX5_IB_ATTR_CREATE_FLOW_DEST_DEVX);

		/* Verify that the given DEVX object is a flow
		 * steering destination.
		 */
		if (!is_flow_dest(devx_obj, dest_id, dest_type))
			return -EINVAL;
		/* Allow only flow table as dest when inserting to FDB or RDMA_RX */
		if ((fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_FDB ||
		     fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_RDMA_RX) &&
		    *dest_type != MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE)
			return -EINVAL;
	} else if (dest_qp) {
		struct mlx5_ib_qp *mqp;

		*qp = uverbs_attr_get_obj(attrs,
					  MLX5_IB_ATTR_CREATE_FLOW_DEST_QP);
		if (IS_ERR(*qp))
			return PTR_ERR(*qp);

		if ((*qp)->qp_type != IB_QPT_RAW_PACKET)
			return -EINVAL;

		mqp = to_mqp(*qp);
		if (mqp->is_rss)
			*dest_id = mqp->rss_qp.tirn;
		else
			*dest_id = mqp->raw_packet_qp.rq.tirn;
		*dest_type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	} else if ((fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_EGRESS ||
		    fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_RDMA_TX) &&
		   !(*flags & MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DROP)) {
		*dest_type = MLX5_FLOW_DESTINATION_TYPE_PORT;
	}

	if (*dest_type == MLX5_FLOW_DESTINATION_TYPE_TIR &&
	    (fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_EGRESS ||
	     fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_RDMA_TX))
		return -EINVAL;

	return 0;
}

static bool is_flow_counter(void *obj, u32 offset, u32 *counter_id)
{
	struct devx_obj *devx_obj = obj;
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, devx_obj->dinbox, opcode);

	if (opcode == MLX5_CMD_OP_DEALLOC_FLOW_COUNTER) {

		if (offset && offset >= devx_obj->flow_counter_bulk_size)
			return false;

		*counter_id = MLX5_GET(dealloc_flow_counter_in,
				       devx_obj->dinbox,
				       flow_counter_id);
		*counter_id += offset;
		return true;
	}

	return false;
}

#define MLX5_IB_CREATE_FLOW_MAX_FLOW_ACTIONS 2
static int UVERBS_HANDLER(MLX5_IB_METHOD_CREATE_FLOW)(
	struct uverbs_attr_bundle *attrs)
{
	struct mlx5_flow_context flow_context = {.flow_tag =
		MLX5_FS_DEFAULT_FLOW_TAG};
	u32 *offset_attr, offset = 0, counter_id = 0;
	int dest_id, dest_type = -1, inlen, len, ret, i;
	struct mlx5_ib_flow_handler *flow_handler;
	struct mlx5_ib_flow_matcher *fs_matcher;
	struct ib_uobject **arr_flow_actions;
	struct ib_uflow_resources *uflow_res;
	struct mlx5_flow_act flow_act = {};
	struct ib_qp *qp = NULL;
	void *devx_obj, *cmd_in;
	struct ib_uobject *uobj;
	struct mlx5_ib_dev *dev;
	u32 flags;

	if (!capable(CAP_NET_RAW))
		return -EPERM;

	fs_matcher = uverbs_attr_get_obj(attrs,
					 MLX5_IB_ATTR_CREATE_FLOW_MATCHER);
	uobj =  uverbs_attr_get_uobject(attrs, MLX5_IB_ATTR_CREATE_FLOW_HANDLE);
	dev = mlx5_udata_to_mdev(&attrs->driver_udata);

	if (get_dests(attrs, fs_matcher, &dest_id, &dest_type, &qp, &flags))
		return -EINVAL;

	if (flags & MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DEFAULT_MISS)
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_NS;

	if (flags & MLX5_IB_ATTR_CREATE_FLOW_FLAGS_DROP)
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_DROP;

	len = uverbs_attr_get_uobjs_arr(attrs,
		MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX, &arr_flow_actions);
	if (len) {
		devx_obj = arr_flow_actions[0]->object;

		if (uverbs_attr_is_valid(attrs,
					 MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX_OFFSET)) {

			int num_offsets = uverbs_attr_ptr_get_array_size(
				attrs,
				MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX_OFFSET,
				sizeof(u32));

			if (num_offsets != 1)
				return -EINVAL;

			offset_attr = uverbs_attr_get_alloced_ptr(
				attrs,
				MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX_OFFSET);
			offset = *offset_attr;
		}

		if (!is_flow_counter(devx_obj, offset, &counter_id))
			return -EINVAL;

		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
	}

	cmd_in = uverbs_attr_get_alloced_ptr(
		attrs, MLX5_IB_ATTR_CREATE_FLOW_MATCH_VALUE);
	inlen = uverbs_attr_get_len(attrs,
				    MLX5_IB_ATTR_CREATE_FLOW_MATCH_VALUE);

	uflow_res = flow_resources_alloc(MLX5_IB_CREATE_FLOW_MAX_FLOW_ACTIONS);
	if (!uflow_res)
		return -ENOMEM;

	len = uverbs_attr_get_uobjs_arr(attrs,
		MLX5_IB_ATTR_CREATE_FLOW_ARR_FLOW_ACTIONS, &arr_flow_actions);
	for (i = 0; i < len; i++) {
		struct mlx5_ib_flow_action *maction =
			to_mflow_act(arr_flow_actions[i]->object);

		ret = parse_flow_flow_action(maction, false, &flow_act);
		if (ret)
			goto err_out;
		flow_resources_add(uflow_res, IB_FLOW_SPEC_ACTION_HANDLE,
				   arr_flow_actions[i]->object);
	}

	ret = uverbs_copy_from(&flow_context.flow_tag, attrs,
			       MLX5_IB_ATTR_CREATE_FLOW_TAG);
	if (!ret) {
		if (flow_context.flow_tag >= BIT(24)) {
			ret = -EINVAL;
			goto err_out;
		}
		flow_context.flags |= FLOW_CONTEXT_HAS_TAG;
	}

	flow_handler =
		raw_fs_rule_add(dev, fs_matcher, &flow_context, &flow_act,
				counter_id, cmd_in, inlen, dest_id, dest_type);
	if (IS_ERR(flow_handler)) {
		ret = PTR_ERR(flow_handler);
		goto err_out;
	}

	ib_set_flow(uobj, &flow_handler->ibflow, qp, &dev->ib_dev, uflow_res);

	return 0;
err_out:
	ib_uverbs_flow_resources_free(uflow_res);
	return ret;
}

static int flow_matcher_cleanup(struct ib_uobject *uobject,
				enum rdma_remove_reason why,
				struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_flow_matcher *obj = uobject->object;

	if (atomic_read(&obj->usecnt))
		return -EBUSY;

	kfree(obj);
	return 0;
}

static int mlx5_ib_matcher_ns(struct uverbs_attr_bundle *attrs,
			      struct mlx5_ib_flow_matcher *obj)
{
	enum mlx5_ib_uapi_flow_table_type ft_type =
		MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX;
	u32 flags;
	int err;

	/* New users should use MLX5_IB_ATTR_FLOW_MATCHER_FT_TYPE and older
	 * users should switch to it. We leave this to not break userspace
	 */
	if (uverbs_attr_is_valid(attrs, MLX5_IB_ATTR_FLOW_MATCHER_FT_TYPE) &&
	    uverbs_attr_is_valid(attrs, MLX5_IB_ATTR_FLOW_MATCHER_FLOW_FLAGS))
		return -EINVAL;

	if (uverbs_attr_is_valid(attrs, MLX5_IB_ATTR_FLOW_MATCHER_FT_TYPE)) {
		err = uverbs_get_const(&ft_type, attrs,
				       MLX5_IB_ATTR_FLOW_MATCHER_FT_TYPE);
		if (err)
			return err;

		err = mlx5_ib_ft_type_to_namespace(ft_type, &obj->ns_type);
		if (err)
			return err;

		return 0;
	}

	if (uverbs_attr_is_valid(attrs, MLX5_IB_ATTR_FLOW_MATCHER_FLOW_FLAGS)) {
		err = uverbs_get_flags32(&flags, attrs,
					 MLX5_IB_ATTR_FLOW_MATCHER_FLOW_FLAGS,
					 IB_FLOW_ATTR_FLAGS_EGRESS);
		if (err)
			return err;

		if (flags) {
			mlx5_ib_ft_type_to_namespace(
				MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX,
				&obj->ns_type);
			return 0;
		}
	}

	obj->ns_type = MLX5_FLOW_NAMESPACE_BYPASS;

	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_FLOW_MATCHER_CREATE)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_CREATE_HANDLE);
	struct mlx5_ib_dev *dev = mlx5_udata_to_mdev(&attrs->driver_udata);
	struct mlx5_ib_flow_matcher *obj;
	int err;

	obj = kzalloc(sizeof(struct mlx5_ib_flow_matcher), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	obj->mask_len = uverbs_attr_get_len(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK);
	err = uverbs_copy_from(&obj->matcher_mask,
			       attrs,
			       MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK);
	if (err)
		goto end;

	obj->flow_type = uverbs_attr_get_enum_id(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE);

	if (obj->flow_type == MLX5_IB_FLOW_TYPE_NORMAL) {
		err = uverbs_copy_from(&obj->priority,
				       attrs,
				       MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE);
		if (err)
			goto end;
	}

	err = uverbs_copy_from(&obj->match_criteria_enable,
			       attrs,
			       MLX5_IB_ATTR_FLOW_MATCHER_MATCH_CRITERIA);
	if (err)
		goto end;

	err = mlx5_ib_matcher_ns(attrs, obj);
	if (err)
		goto end;

	if (obj->ns_type == MLX5_FLOW_NAMESPACE_FDB &&
	    mlx5_eswitch_mode(dev->mdev) != MLX5_ESWITCH_OFFLOADS) {
		err = -EINVAL;
		goto end;
	}

	uobj->object = obj;
	obj->mdev = dev->mdev;
	atomic_set(&obj->usecnt, 0);
	return 0;

end:
	kfree(obj);
	return err;
}

static struct ib_flow_action *
mlx5_ib_create_modify_header(struct mlx5_ib_dev *dev,
			     enum mlx5_ib_uapi_flow_table_type ft_type,
			     u8 num_actions, void *in)
{
	enum mlx5_flow_namespace_type namespace;
	struct mlx5_ib_flow_action *maction;
	int ret;

	ret = mlx5_ib_ft_type_to_namespace(ft_type, &namespace);
	if (ret)
		return ERR_PTR(-EINVAL);

	maction = kzalloc(sizeof(*maction), GFP_KERNEL);
	if (!maction)
		return ERR_PTR(-ENOMEM);

	maction->flow_action_raw.modify_hdr =
		mlx5_modify_header_alloc(dev->mdev, namespace, num_actions, in);

	if (IS_ERR(maction->flow_action_raw.modify_hdr)) {
		ret = PTR_ERR(maction->flow_action_raw.modify_hdr);
		kfree(maction);
		return ERR_PTR(ret);
	}
	maction->flow_action_raw.sub_type =
		MLX5_IB_FLOW_ACTION_MODIFY_HEADER;
	maction->flow_action_raw.dev = dev;

	return &maction->ib_action;
}

static bool mlx5_ib_modify_header_supported(struct mlx5_ib_dev *dev)
{
	return MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev,
					 max_modify_header_actions) ||
	       MLX5_CAP_FLOWTABLE_NIC_TX(dev->mdev,
					 max_modify_header_actions) ||
	       MLX5_CAP_FLOWTABLE_RDMA_TX(dev->mdev,
					 max_modify_header_actions);
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_FLOW_ACTION_CREATE_MODIFY_HEADER)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_CREATE_MODIFY_HEADER_HANDLE);
	struct mlx5_ib_dev *mdev = mlx5_udata_to_mdev(&attrs->driver_udata);
	enum mlx5_ib_uapi_flow_table_type ft_type;
	struct ib_flow_action *action;
	int num_actions;
	void *in;
	int ret;

	if (!mlx5_ib_modify_header_supported(mdev))
		return -EOPNOTSUPP;

	in = uverbs_attr_get_alloced_ptr(attrs,
		MLX5_IB_ATTR_CREATE_MODIFY_HEADER_ACTIONS_PRM);

	num_actions = uverbs_attr_ptr_get_array_size(
		attrs, MLX5_IB_ATTR_CREATE_MODIFY_HEADER_ACTIONS_PRM,
		MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto));
	if (num_actions < 0)
		return num_actions;

	ret = uverbs_get_const(&ft_type, attrs,
			       MLX5_IB_ATTR_CREATE_MODIFY_HEADER_FT_TYPE);
	if (ret)
		return ret;
	action = mlx5_ib_create_modify_header(mdev, ft_type, num_actions, in);
	if (IS_ERR(action))
		return PTR_ERR(action);

	uverbs_flow_action_fill_action(action, uobj, &mdev->ib_dev,
				       IB_FLOW_ACTION_UNSPECIFIED);

	return 0;
}

static bool mlx5_ib_flow_action_packet_reformat_valid(struct mlx5_ib_dev *ibdev,
						      u8 packet_reformat_type,
						      u8 ft_type)
{
	switch (packet_reformat_type) {
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L2_TUNNEL:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX)
			return MLX5_CAP_FLOWTABLE(ibdev->mdev,
						  encap_general_header);
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L3_TUNNEL:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX)
			return MLX5_CAP_FLOWTABLE_NIC_TX(ibdev->mdev,
				reformat_l2_to_l3_tunnel);
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L3_TUNNEL_TO_L2:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX)
			return MLX5_CAP_FLOWTABLE_NIC_RX(ibdev->mdev,
				reformat_l3_tunnel_to_l2);
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TUNNEL_TO_L2:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX)
			return MLX5_CAP_FLOWTABLE_NIC_RX(ibdev->mdev, decap);
		break;
	default:
		break;
	}

	return false;
}

static int mlx5_ib_dv_to_prm_packet_reforamt_type(u8 dv_prt, u8 *prm_prt)
{
	switch (dv_prt) {
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L2_TUNNEL:
		*prm_prt = MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL;
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L3_TUNNEL_TO_L2:
		*prm_prt = MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2;
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L3_TUNNEL:
		*prm_prt = MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mlx5_ib_flow_action_create_packet_reformat_ctx(
	struct mlx5_ib_dev *dev,
	struct mlx5_ib_flow_action *maction,
	u8 ft_type, u8 dv_prt,
	void *in, size_t len)
{
	enum mlx5_flow_namespace_type namespace;
	u8 prm_prt;
	int ret;

	ret = mlx5_ib_ft_type_to_namespace(ft_type, &namespace);
	if (ret)
		return ret;

	ret = mlx5_ib_dv_to_prm_packet_reforamt_type(dv_prt, &prm_prt);
	if (ret)
		return ret;

	maction->flow_action_raw.pkt_reformat =
		mlx5_packet_reformat_alloc(dev->mdev, prm_prt, len,
					   in, namespace);
	if (IS_ERR(maction->flow_action_raw.pkt_reformat)) {
		ret = PTR_ERR(maction->flow_action_raw.pkt_reformat);
		return ret;
	}

	maction->flow_action_raw.sub_type =
		MLX5_IB_FLOW_ACTION_PACKET_REFORMAT;
	maction->flow_action_raw.dev = dev;

	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_FLOW_ACTION_CREATE_PACKET_REFORMAT)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs,
		MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_HANDLE);
	struct mlx5_ib_dev *mdev = mlx5_udata_to_mdev(&attrs->driver_udata);
	enum mlx5_ib_uapi_flow_action_packet_reformat_type dv_prt;
	enum mlx5_ib_uapi_flow_table_type ft_type;
	struct mlx5_ib_flow_action *maction;
	int ret;

	ret = uverbs_get_const(&ft_type, attrs,
			       MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_FT_TYPE);
	if (ret)
		return ret;

	ret = uverbs_get_const(&dv_prt, attrs,
			       MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_TYPE);
	if (ret)
		return ret;

	if (!mlx5_ib_flow_action_packet_reformat_valid(mdev, dv_prt, ft_type))
		return -EOPNOTSUPP;

	maction = kzalloc(sizeof(*maction), GFP_KERNEL);
	if (!maction)
		return -ENOMEM;

	if (dv_prt ==
	    MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TUNNEL_TO_L2) {
		maction->flow_action_raw.sub_type =
			MLX5_IB_FLOW_ACTION_DECAP;
		maction->flow_action_raw.dev = mdev;
	} else {
		void *in;
		int len;

		in = uverbs_attr_get_alloced_ptr(attrs,
			MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_DATA_BUF);
		if (IS_ERR(in)) {
			ret = PTR_ERR(in);
			goto free_maction;
		}

		len = uverbs_attr_get_len(attrs,
			MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_DATA_BUF);

		ret = mlx5_ib_flow_action_create_packet_reformat_ctx(mdev,
			maction, ft_type, dv_prt, in, len);
		if (ret)
			goto free_maction;
	}

	uverbs_flow_action_fill_action(&maction->ib_action, uobj, &mdev->ib_dev,
				       IB_FLOW_ACTION_UNSPECIFIED);
	return 0;

free_maction:
	kfree(maction);
	return ret;
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_CREATE_FLOW,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_HANDLE,
			UVERBS_OBJECT_FLOW,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_CREATE_FLOW_MATCH_VALUE,
		UVERBS_ATTR_SIZE(1, sizeof(struct mlx5_ib_match_params)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_MATCHER,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_DEST_QP,
			UVERBS_OBJECT_QP,
			UVERBS_ACCESS_READ),
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_DEST_DEVX,
			MLX5_IB_OBJECT_DEVX_OBJ,
			UVERBS_ACCESS_READ),
	UVERBS_ATTR_IDRS_ARR(MLX5_IB_ATTR_CREATE_FLOW_ARR_FLOW_ACTIONS,
			     UVERBS_OBJECT_FLOW_ACTION,
			     UVERBS_ACCESS_READ, 1,
			     MLX5_IB_CREATE_FLOW_MAX_FLOW_ACTIONS,
			     UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_CREATE_FLOW_TAG,
			   UVERBS_ATTR_TYPE(u32),
			   UA_OPTIONAL),
	UVERBS_ATTR_IDRS_ARR(MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX,
			     MLX5_IB_OBJECT_DEVX_OBJ,
			     UVERBS_ACCESS_READ, 1, 1,
			     UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX_OFFSET,
			   UVERBS_ATTR_MIN_SIZE(sizeof(u32)),
			   UA_OPTIONAL,
			   UA_ALLOC_AND_COPY),
	UVERBS_ATTR_FLAGS_IN(MLX5_IB_ATTR_CREATE_FLOW_FLAGS,
			     enum mlx5_ib_create_flow_flags,
			     UA_OPTIONAL));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_DESTROY_FLOW,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_HANDLE,
			UVERBS_OBJECT_FLOW,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

ADD_UVERBS_METHODS(mlx5_ib_fs,
		   UVERBS_OBJECT_FLOW,
		   &UVERBS_METHOD(MLX5_IB_METHOD_CREATE_FLOW),
		   &UVERBS_METHOD(MLX5_IB_METHOD_DESTROY_FLOW));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_FLOW_ACTION_CREATE_MODIFY_HEADER,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_MODIFY_HEADER_HANDLE,
			UVERBS_OBJECT_FLOW_ACTION,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_CREATE_MODIFY_HEADER_ACTIONS_PRM,
			   UVERBS_ATTR_MIN_SIZE(MLX5_UN_SZ_BYTES(
				   set_add_copy_action_in_auto)),
			   UA_MANDATORY,
			   UA_ALLOC_AND_COPY),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_CREATE_MODIFY_HEADER_FT_TYPE,
			     enum mlx5_ib_uapi_flow_table_type,
			     UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_FLOW_ACTION_CREATE_PACKET_REFORMAT,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_HANDLE,
			UVERBS_OBJECT_FLOW_ACTION,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_DATA_BUF,
			   UVERBS_ATTR_MIN_SIZE(1),
			   UA_ALLOC_AND_COPY,
			   UA_OPTIONAL),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_TYPE,
			     enum mlx5_ib_uapi_flow_action_packet_reformat_type,
			     UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_FT_TYPE,
			     enum mlx5_ib_uapi_flow_table_type,
			     UA_MANDATORY));

ADD_UVERBS_METHODS(
	mlx5_ib_flow_actions,
	UVERBS_OBJECT_FLOW_ACTION,
	&UVERBS_METHOD(MLX5_IB_METHOD_FLOW_ACTION_CREATE_MODIFY_HEADER),
	&UVERBS_METHOD(MLX5_IB_METHOD_FLOW_ACTION_CREATE_PACKET_REFORMAT));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_FLOW_MATCHER_CREATE,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_FLOW_MATCHER_CREATE_HANDLE,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK,
		UVERBS_ATTR_SIZE(1, sizeof(struct mlx5_ib_match_params)),
		UA_MANDATORY),
	UVERBS_ATTR_ENUM_IN(MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE,
			    mlx5_ib_flow_type,
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_FLOW_MATCHER_MATCH_CRITERIA,
			   UVERBS_ATTR_TYPE(u8),
			   UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(MLX5_IB_ATTR_FLOW_MATCHER_FLOW_FLAGS,
			     enum ib_flow_flags,
			     UA_OPTIONAL),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_FLOW_MATCHER_FT_TYPE,
			     enum mlx5_ib_uapi_flow_table_type,
			     UA_OPTIONAL));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_FLOW_MATCHER_DESTROY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_FLOW_MATCHER_DESTROY_HANDLE,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_FLOW_MATCHER,
			    UVERBS_TYPE_ALLOC_IDR(flow_matcher_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_FLOW_MATCHER_CREATE),
			    &UVERBS_METHOD(MLX5_IB_METHOD_FLOW_MATCHER_DESTROY));

const struct uapi_definition mlx5_ib_flow_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_FLOW_MATCHER),
	UAPI_DEF_CHAIN_OBJ_TREE(
		UVERBS_OBJECT_FLOW,
		&mlx5_ib_fs),
	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_FLOW_ACTION,
				&mlx5_ib_flow_actions),
	{},
};

static const struct ib_device_ops flow_ops = {
	.create_flow = mlx5_ib_create_flow,
	.destroy_flow = mlx5_ib_destroy_flow,
	.destroy_flow_action = mlx5_ib_destroy_flow_action,
};

static const struct ib_device_ops flow_ipsec_ops = {
	.create_flow_action_esp = mlx5_ib_create_flow_action_esp,
	.modify_flow_action_esp = mlx5_ib_modify_flow_action_esp,
};

int mlx5_ib_fs_init(struct mlx5_ib_dev *dev)
{
	dev->flow_db = kzalloc(sizeof(*dev->flow_db), GFP_KERNEL);

	if (!dev->flow_db)
		return -ENOMEM;

	mutex_init(&dev->flow_db->lock);

	ib_set_device_ops(&dev->ib_dev, &flow_ops);
	if (mlx5_accel_ipsec_device_caps(dev->mdev) &
	    MLX5_ACCEL_IPSEC_CAP_DEVICE)
		ib_set_device_ops(&dev->ib_dev, &flow_ipsec_ops);

	return 0;
}
