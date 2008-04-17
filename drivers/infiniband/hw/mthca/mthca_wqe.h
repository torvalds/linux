/*
 * Copyright (c) 2005 Cisco Systems. All rights reserved.
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
 *
 * $Id: mthca_wqe.h 3047 2005-08-10 03:59:35Z roland $
 */

#ifndef MTHCA_WQE_H
#define MTHCA_WQE_H

#include <linux/types.h>

enum {
	MTHCA_NEXT_DBD		= 1 << 7,
	MTHCA_NEXT_FENCE	= 1 << 6,
	MTHCA_NEXT_CQ_UPDATE	= 1 << 3,
	MTHCA_NEXT_EVENT_GEN	= 1 << 2,
	MTHCA_NEXT_SOLICIT	= 1 << 1,
	MTHCA_NEXT_IP_CSUM	= 1 << 4,
	MTHCA_NEXT_TCP_UDP_CSUM = 1 << 5,

	MTHCA_MLX_VL15		= 1 << 17,
	MTHCA_MLX_SLR		= 1 << 16
};

enum {
	MTHCA_INVAL_LKEY			= 0x100,
	MTHCA_TAVOR_MAX_WQES_PER_RECV_DB	= 256,
	MTHCA_ARBEL_MAX_WQES_PER_SEND_DB	= 255
};

struct mthca_next_seg {
	__be32 nda_op;		/* [31:6] next WQE [4:0] next opcode */
	__be32 ee_nds;		/* [31:8] next EE  [7] DBD [6] F [5:0] next WQE size */
	__be32 flags;		/* [3] CQ [2] Event [1] Solicit */
	__be32 imm;		/* immediate data */
};

struct mthca_tavor_ud_seg {
	u32    reserved1;
	__be32 lkey;
	__be64 av_addr;
	u32    reserved2[4];
	__be32 dqpn;
	__be32 qkey;
	u32    reserved3[2];
};

struct mthca_arbel_ud_seg {
	__be32 av[8];
	__be32 dqpn;
	__be32 qkey;
	u32    reserved[2];
};

struct mthca_bind_seg {
	__be32 flags;		/* [31] Atomic [30] rem write [29] rem read */
	u32    reserved;
	__be32 new_rkey;
	__be32 lkey;
	__be64 addr;
	__be64 length;
};

struct mthca_raddr_seg {
	__be64 raddr;
	__be32 rkey;
	u32    reserved;
};

struct mthca_atomic_seg {
	__be64 swap_add;
	__be64 compare;
};

struct mthca_data_seg {
	__be32 byte_count;
	__be32 lkey;
	__be64 addr;
};

struct mthca_mlx_seg {
	__be32 nda_op;
	__be32 nds;
	__be32 flags;		/* [17] VL15 [16] SLR [14:12] static rate
				   [11:8] SL [3] C [2] E */
	__be16 rlid;
	__be16 vcrc;
};

static __always_inline void mthca_set_data_seg(struct mthca_data_seg *dseg,
					       struct ib_sge *sg)
{
	dseg->byte_count = cpu_to_be32(sg->length);
	dseg->lkey       = cpu_to_be32(sg->lkey);
	dseg->addr       = cpu_to_be64(sg->addr);
}

static __always_inline void mthca_set_data_seg_inval(struct mthca_data_seg *dseg)
{
	dseg->byte_count = 0;
	dseg->lkey       = cpu_to_be32(MTHCA_INVAL_LKEY);
	dseg->addr       = 0;
}

#endif /* MTHCA_WQE_H */
