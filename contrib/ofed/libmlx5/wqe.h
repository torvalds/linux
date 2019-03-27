/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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

struct mlx5_sg_copy_ptr {
	int	index;
	int	offset;
};

struct mlx5_eqe_comp {
	uint32_t	reserved[6];
	uint32_t	cqn;
};

struct mlx5_eqe_qp_srq {
	uint32_t	reserved[6];
	uint32_t	qp_srq_n;
};

struct mlx5_wqe_xrc_seg {
	uint32_t	xrc_srqn;
	uint8_t		rsvd[12];
};

struct mlx5_wqe_masked_atomic_seg {
	uint64_t	swap_add;
	uint64_t	compare;
	uint64_t	swap_add_mask;
	uint64_t	compare_mask;
};

enum {
	MLX5_ETH_L2_INLINE_HEADER_SIZE	= 18,
	MLX5_ETH_L2_MIN_HEADER_SIZE	= 14,
};

enum {
	MLX5_WQE_UMR_CTRL_FLAG_INLINE =			1 << 7,
	MLX5_WQE_UMR_CTRL_FLAG_CHECK_FREE =		1 << 5,
	MLX5_WQE_UMR_CTRL_FLAG_TRNSLATION_OFFSET =	1 << 4,
	MLX5_WQE_UMR_CTRL_FLAG_CHECK_QPN =		1 << 3,
};

enum {
	MLX5_WQE_UMR_CTRL_MKEY_MASK_LEN			= 1 << 0,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_START_ADDR		= 1 << 6,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_MKEY		= 1 << 13,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_QPN			= 1 << 14,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_LOCAL_WRITE	= 1 << 18,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_READ	= 1 << 19,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_WRITE	= 1 << 20,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_ATOMIC	= 1 << 21,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_FREE		= 1 << 29,
};

struct mlx5_wqe_umr_ctrl_seg {
	uint8_t		flags;
	uint8_t		rsvd0[3];
	uint16_t	klm_octowords;
	uint16_t	translation_offset;
	uint64_t	mkey_mask;
	uint8_t		rsvd1[32];
};

struct mlx5_wqe_umr_klm_seg {
	/* up to 2GB */
	uint32_t	byte_count;
	uint32_t	mkey;
	uint64_t	address;
};

union mlx5_wqe_umr_inline_seg {
	struct mlx5_wqe_umr_klm_seg	klm;
};

enum {
	MLX5_WQE_MKEY_CONTEXT_FREE = 1 << 6
};

enum {
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_ATOMIC = 1 << 6,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_WRITE = 1 << 5,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_READ = 1 << 4,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_LOCAL_WRITE = 1 << 3,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_LOCAL_READ = 1 << 2
};

struct mlx5_wqe_mkey_context_seg {
	uint8_t		free;
	uint8_t		reserved1;
	uint8_t		access_flags;
	uint8_t		sf;
	uint32_t	qpn_mkey;
	uint32_t	reserved2;
	uint32_t	flags_pd;
	uint64_t	start_addr;
	uint64_t	len;
	uint32_t	bsf_octword_size;
	uint32_t	reserved3[4];
	uint32_t	translations_octword_size;
	uint8_t		reserved4[3];
	uint8_t		log_page_size;
	uint32_t	reserved;
	union mlx5_wqe_umr_inline_seg inseg[0];
};

struct mlx5_seg_set_psv {
	uint8_t		rsvd[4];
	uint16_t	syndrome;
	uint16_t	status;
	uint16_t	block_guard;
	uint16_t	app_tag;
	uint32_t	ref_tag;
	uint32_t	mkey;
	uint64_t	va;
};

struct mlx5_seg_get_psv {
	uint8_t		rsvd[19];
	uint8_t		num_psv;
	uint32_t	l_key;
	uint64_t	va;
	uint32_t	psv_index[4];
};

struct mlx5_seg_check_psv {
	uint8_t		rsvd0[2];
	uint16_t	err_coalescing_op;
	uint8_t		rsvd1[2];
	uint16_t	xport_err_op;
	uint8_t		rsvd2[2];
	uint16_t	xport_err_mask;
	uint8_t		rsvd3[7];
	uint8_t		num_psv;
	uint32_t	l_key;
	uint64_t	va;
	uint32_t	psv_index[4];
};

struct mlx5_rwqe_sig {
	uint8_t		rsvd0[4];
	uint8_t		signature;
	uint8_t		rsvd1[11];
};

struct mlx5_wqe_signature_seg {
	uint8_t		rsvd0[4];
	uint8_t		signature;
	uint8_t		rsvd1[11];
};

struct mlx5_wqe_inline_seg {
	uint32_t	byte_count;
};


#endif /* WQE_H */
