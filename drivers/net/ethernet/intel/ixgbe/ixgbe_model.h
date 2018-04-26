/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#ifndef _IXGBE_MODEL_H_
#define _IXGBE_MODEL_H_

#include "ixgbe.h"
#include "ixgbe_type.h"

struct ixgbe_mat_field {
	unsigned int off;
	int (*val)(struct ixgbe_fdir_filter *input,
		   union ixgbe_atr_input *mask,
		   u32 val, u32 m);
	unsigned int type;
};

struct ixgbe_jump_table {
	struct ixgbe_mat_field *mat;
	struct ixgbe_fdir_filter *input;
	union ixgbe_atr_input *mask;
	u32 link_hdl;
	unsigned long child_loc_map[32];
};

#define IXGBE_MAX_HW_ENTRIES 2045

static inline int ixgbe_mat_prgm_sip(struct ixgbe_fdir_filter *input,
				     union ixgbe_atr_input *mask,
				     u32 val, u32 m)
{
	input->filter.formatted.src_ip[0] = val;
	mask->formatted.src_ip[0] = m;
	return 0;
}

static inline int ixgbe_mat_prgm_dip(struct ixgbe_fdir_filter *input,
				     union ixgbe_atr_input *mask,
				     u32 val, u32 m)
{
	input->filter.formatted.dst_ip[0] = val;
	mask->formatted.dst_ip[0] = m;
	return 0;
}

static struct ixgbe_mat_field ixgbe_ipv4_fields[] = {
	{ .off = 12, .val = ixgbe_mat_prgm_sip,
	  .type = IXGBE_ATR_FLOW_TYPE_IPV4},
	{ .off = 16, .val = ixgbe_mat_prgm_dip,
	  .type = IXGBE_ATR_FLOW_TYPE_IPV4},
	{ .val = NULL } /* terminal node */
};

static inline int ixgbe_mat_prgm_ports(struct ixgbe_fdir_filter *input,
				       union ixgbe_atr_input *mask,
				       u32 val, u32 m)
{
	input->filter.formatted.src_port = val & 0xffff;
	mask->formatted.src_port = m & 0xffff;
	input->filter.formatted.dst_port = val >> 16;
	mask->formatted.dst_port = m >> 16;

	return 0;
};

static struct ixgbe_mat_field ixgbe_tcp_fields[] = {
	{.off = 0, .val = ixgbe_mat_prgm_ports,
	 .type = IXGBE_ATR_FLOW_TYPE_TCPV4},
	{ .val = NULL } /* terminal node */
};

static struct ixgbe_mat_field ixgbe_udp_fields[] = {
	{.off = 0, .val = ixgbe_mat_prgm_ports,
	 .type = IXGBE_ATR_FLOW_TYPE_UDPV4},
	{ .val = NULL } /* terminal node */
};

struct ixgbe_nexthdr {
	/* offset, shift, and mask of position to next header */
	unsigned int o;
	u32 s;
	u32 m;
	/* match criteria to make this jump*/
	unsigned int off;
	u32 val;
	u32 mask;
	/* location of jump to make */
	struct ixgbe_mat_field *jump;
};

static struct ixgbe_nexthdr ixgbe_ipv4_jumps[] = {
	{ .o = 0, .s = 6, .m = 0xf,
	  .off = 8, .val = 0x600, .mask = 0xff00, .jump = ixgbe_tcp_fields},
	{ .o = 0, .s = 6, .m = 0xf,
	  .off = 8, .val = 0x1100, .mask = 0xff00, .jump = ixgbe_udp_fields},
	{ .jump = NULL } /* terminal node */
};
#endif /* _IXGBE_MODEL_H_ */
