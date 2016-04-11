/*******************************************************************************
 *
 * Intel 10 Gigabit PCI Express Linux drive
 * Copyright(c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

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
	{ .jump = NULL } /* terminal node */
};
#endif /* _IXGBE_MODEL_H_ */
