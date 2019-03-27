/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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

#ifndef WQE_H
#define WQE_H

#include <stdint.h>

enum {
	MLX4_SEND_DOORBELL	= 0x14,
};

enum {
	MLX4_WQE_CTRL_SOLICIT		= 1 << 1,
	MLX4_WQE_CTRL_CQ_UPDATE		= 3 << 2,
	MLX4_WQE_CTRL_IP_HDR_CSUM	= 1 << 4,
	MLX4_WQE_CTRL_TCP_UDP_CSUM	= 1 << 5,
	MLX4_WQE_CTRL_FENCE		= 1 << 6,
	MLX4_WQE_CTRL_STRONG_ORDER	= 1 << 7
};

enum {
	MLX4_WQE_BIND_TYPE_2		= (1<<31),
	MLX4_WQE_BIND_ZERO_BASED	= (1<<30),
};

enum {
	MLX4_INLINE_SEG		= 1 << 31,
	MLX4_INLINE_ALIGN	= 64,
};

enum {
	MLX4_INVALID_LKEY	= 0x100,
};

struct mlx4_wqe_ctrl_seg {
	uint32_t		owner_opcode;
	union {
		struct {
			uint8_t			reserved[3];
			uint8_t			fence_size;
		};
		uint32_t	bf_qpn;
	};
	/*
	 * High 24 bits are SRC remote buffer; low 8 bits are flags:
	 * [7]   SO (strong ordering)
	 * [5]   TCP/UDP checksum
	 * [4]   IP checksum
	 * [3:2] C (generate completion queue entry)
	 * [1]   SE (solicited event)
	 * [0]   FL (force loopback)
	 */
	uint32_t		srcrb_flags;
	/*
	 * imm is immediate data for send/RDMA write w/ immediate;
	 * also invalidation key for send with invalidate; input
	 * modifier for WQEs on CCQs.
	 */
	uint32_t		imm;
};

struct mlx4_wqe_datagram_seg {
	uint32_t		av[8];
	uint32_t		dqpn;
	uint32_t		qkey;
	uint16_t		vlan;
	uint8_t			mac[6];
};

struct mlx4_wqe_data_seg {
	uint32_t		byte_count;
	uint32_t		lkey;
	uint64_t		addr;
};

struct mlx4_wqe_inline_seg {
	uint32_t		byte_count;
};

struct mlx4_wqe_srq_next_seg {
	uint16_t		reserved1;
	uint16_t		next_wqe_index;
	uint32_t		reserved2[3];
};

struct mlx4_wqe_local_inval_seg {
	uint64_t		reserved1;
	uint32_t		mem_key;
	uint32_t		reserved2;
	uint64_t		reserved3[2];
};

enum {
	MLX4_WQE_MW_REMOTE_READ   = 1 << 29,
	MLX4_WQE_MW_REMOTE_WRITE  = 1 << 30,
	MLX4_WQE_MW_ATOMIC        = 1 << 31
};

struct mlx4_wqe_raddr_seg {
	uint64_t		raddr;
	uint32_t		rkey;
	uint32_t		reserved;
};

struct mlx4_wqe_atomic_seg {
	uint64_t		swap_add;
	uint64_t		compare;
};

struct mlx4_wqe_bind_seg {
	uint32_t		flags1;
	uint32_t		flags2;
	uint32_t		new_rkey;
	uint32_t		lkey;
	uint64_t		addr;
	uint64_t		length;
};

#endif /* WQE_H */
