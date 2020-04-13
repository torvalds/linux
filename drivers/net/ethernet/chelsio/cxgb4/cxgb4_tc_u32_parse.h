/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2016 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __CXGB4_TC_U32_PARSE_H
#define __CXGB4_TC_U32_PARSE_H

struct cxgb4_match_field {
	int off; /* Offset from the beginning of the header to match */
	/* Fill the value/mask pair in the spec if matched */
	int (*val)(struct ch_filter_specification *f, u32 val, u32 mask);
};

/* IPv4 match fields */
static inline int cxgb4_fill_ipv4_tos(struct ch_filter_specification *f,
				      u32 val, u32 mask)
{
	f->val.tos  = (ntohl(val)  >> 16) & 0x000000FF;
	f->mask.tos = (ntohl(mask) >> 16) & 0x000000FF;

	return 0;
}

static inline int cxgb4_fill_ipv4_frag(struct ch_filter_specification *f,
				       u32 val, u32 mask)
{
	u32 mask_val;
	u8 frag_val;

	frag_val = (ntohl(val) >> 13) & 0x00000007;
	mask_val = ntohl(mask) & 0x0000FFFF;

	if (frag_val == 0x1 && mask_val != 0x3FFF) { /* MF set */
		f->val.frag = 1;
		f->mask.frag = 1;
	} else if (frag_val == 0x2 && mask_val != 0x3FFF) { /* DF set */
		f->val.frag = 0;
		f->mask.frag = 1;
	} else {
		return -EINVAL;
	}

	return 0;
}

static inline int cxgb4_fill_ipv4_proto(struct ch_filter_specification *f,
					u32 val, u32 mask)
{
	f->val.proto  = (ntohl(val)  >> 16) & 0x000000FF;
	f->mask.proto = (ntohl(mask) >> 16) & 0x000000FF;

	return 0;
}

static inline int cxgb4_fill_ipv4_src_ip(struct ch_filter_specification *f,
					 u32 val, u32 mask)
{
	memcpy(&f->val.fip[0],  &val,  sizeof(u32));
	memcpy(&f->mask.fip[0], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv4_dst_ip(struct ch_filter_specification *f,
					 u32 val, u32 mask)
{
	memcpy(&f->val.lip[0],  &val,  sizeof(u32));
	memcpy(&f->mask.lip[0], &mask, sizeof(u32));

	return 0;
}

static const struct cxgb4_match_field cxgb4_ipv4_fields[] = {
	{ .off = 0,  .val = cxgb4_fill_ipv4_tos },
	{ .off = 4,  .val = cxgb4_fill_ipv4_frag },
	{ .off = 8,  .val = cxgb4_fill_ipv4_proto },
	{ .off = 12, .val = cxgb4_fill_ipv4_src_ip },
	{ .off = 16, .val = cxgb4_fill_ipv4_dst_ip },
	{ .val = NULL }
};

/* IPv6 match fields */
static inline int cxgb4_fill_ipv6_tos(struct ch_filter_specification *f,
				      u32 val, u32 mask)
{
	f->val.tos  = (ntohl(val)  >> 20) & 0x000000FF;
	f->mask.tos = (ntohl(mask) >> 20) & 0x000000FF;

	return 0;
}

static inline int cxgb4_fill_ipv6_proto(struct ch_filter_specification *f,
					u32 val, u32 mask)
{
	f->val.proto  = (ntohl(val)  >> 8) & 0x000000FF;
	f->mask.proto = (ntohl(mask) >> 8) & 0x000000FF;

	return 0;
}

static inline int cxgb4_fill_ipv6_src_ip0(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.fip[0],  &val,  sizeof(u32));
	memcpy(&f->mask.fip[0], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv6_src_ip1(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.fip[4],  &val,  sizeof(u32));
	memcpy(&f->mask.fip[4], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv6_src_ip2(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.fip[8],  &val,  sizeof(u32));
	memcpy(&f->mask.fip[8], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv6_src_ip3(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.fip[12],  &val,  sizeof(u32));
	memcpy(&f->mask.fip[12], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv6_dst_ip0(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.lip[0],  &val,  sizeof(u32));
	memcpy(&f->mask.lip[0], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv6_dst_ip1(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.lip[4],  &val,  sizeof(u32));
	memcpy(&f->mask.lip[4], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv6_dst_ip2(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.lip[8],  &val,  sizeof(u32));
	memcpy(&f->mask.lip[8], &mask, sizeof(u32));

	return 0;
}

static inline int cxgb4_fill_ipv6_dst_ip3(struct ch_filter_specification *f,
					  u32 val, u32 mask)
{
	memcpy(&f->val.lip[12],  &val,  sizeof(u32));
	memcpy(&f->mask.lip[12], &mask, sizeof(u32));

	return 0;
}

static const struct cxgb4_match_field cxgb4_ipv6_fields[] = {
	{ .off = 0,  .val = cxgb4_fill_ipv6_tos },
	{ .off = 4,  .val = cxgb4_fill_ipv6_proto },
	{ .off = 8,  .val = cxgb4_fill_ipv6_src_ip0 },
	{ .off = 12, .val = cxgb4_fill_ipv6_src_ip1 },
	{ .off = 16, .val = cxgb4_fill_ipv6_src_ip2 },
	{ .off = 20, .val = cxgb4_fill_ipv6_src_ip3 },
	{ .off = 24, .val = cxgb4_fill_ipv6_dst_ip0 },
	{ .off = 28, .val = cxgb4_fill_ipv6_dst_ip1 },
	{ .off = 32, .val = cxgb4_fill_ipv6_dst_ip2 },
	{ .off = 36, .val = cxgb4_fill_ipv6_dst_ip3 },
	{ .val = NULL }
};

/* TCP/UDP match */
static inline int cxgb4_fill_l4_ports(struct ch_filter_specification *f,
				      u32 val, u32 mask)
{
	f->val.fport  = ntohl(val)  >> 16;
	f->mask.fport = ntohl(mask) >> 16;
	f->val.lport  = ntohl(val)  & 0x0000FFFF;
	f->mask.lport = ntohl(mask) & 0x0000FFFF;

	return 0;
};

static const struct cxgb4_match_field cxgb4_tcp_fields[] = {
	{ .off = 0, .val = cxgb4_fill_l4_ports },
	{ .val = NULL }
};

static const struct cxgb4_match_field cxgb4_udp_fields[] = {
	{ .off = 0, .val = cxgb4_fill_l4_ports },
	{ .val = NULL }
};

struct cxgb4_next_header {
	unsigned int offset; /* Offset to next header */
	/* offset, shift, and mask added to offset above
	 * to get to next header.  Useful when using a header
	 * field's value to jump to next header such as IHL field
	 * in IPv4 header.
	 */
	unsigned int offoff;
	u32 shift;
	u32 mask;
	/* match criteria to make this jump */
	unsigned int match_off;
	u32 match_val;
	u32 match_mask;
	/* location of jump to make */
	const struct cxgb4_match_field *jump;
};

/* Accept a rule with a jump to transport layer header based on IHL field in
 * IPv4 header.
 */
static const struct cxgb4_next_header cxgb4_ipv4_jumps[] = {
	{ .offset = 0, .offoff = 0, .shift = 6, .mask = 0xF,
	  .match_off = 8, .match_val = 0x600, .match_mask = 0xFF00,
	  .jump = cxgb4_tcp_fields },
	{ .offset = 0, .offoff = 0, .shift = 6, .mask = 0xF,
	  .match_off = 8, .match_val = 0x1100, .match_mask = 0xFF00,
	  .jump = cxgb4_udp_fields },
	{ .jump = NULL }
};

/* Accept a rule with a jump directly past the 40 Bytes of IPv6 fixed header
 * to get to transport layer header.
 */
static const struct cxgb4_next_header cxgb4_ipv6_jumps[] = {
	{ .offset = 0x28, .offoff = 0, .shift = 0, .mask = 0,
	  .match_off = 4, .match_val = 0x60000, .match_mask = 0xFF0000,
	  .jump = cxgb4_tcp_fields },
	{ .offset = 0x28, .offoff = 0, .shift = 0, .mask = 0,
	  .match_off = 4, .match_val = 0x110000, .match_mask = 0xFF0000,
	  .jump = cxgb4_udp_fields },
	{ .jump = NULL }
};

struct cxgb4_link {
	const struct cxgb4_match_field *match_field;  /* Next header */
	struct ch_filter_specification fs; /* Match spec associated with link */
	u32 link_handle;         /* Knode handle associated with the link */
	unsigned long *tid_map;  /* Bitmap for filter tids */
};

struct cxgb4_tc_u32_table {
	unsigned int size;          /* number of entries in table */
	struct cxgb4_link table[]; /* Jump table */
};
#endif /* __CXGB4_TC_U32_PARSE_H */
