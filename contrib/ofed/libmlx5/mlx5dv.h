/*
 * Copyright (c) 2017 Mellanox Technologies, Inc.  All rights reserved.
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

#ifndef _MLX5DV_H_
#define _MLX5DV_H_

#include <infiniband/types.h> /* For the __be64 type */
#include <infiniband/endian.h>

#if defined(__SSE3__)
#include <emmintrin.h>
#include <tmmintrin.h>
#endif /* defined(__SSE3__) */

#include <infiniband/verbs.h>

/* Always inline the functions */
#ifdef __GNUC__
#define MLX5DV_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define MLX5DV_ALWAYS_INLINE inline
#endif

enum {
	MLX5_RCV_DBR	= 0,
	MLX5_SND_DBR	= 1,
};

enum mlx5dv_context_comp_mask {
	MLX5DV_CONTEXT_MASK_CQE_COMPRESION	= 1 << 0,
	MLX5DV_CONTEXT_MASK_RESERVED		= 1 << 1,
};

struct mlx5dv_cqe_comp_caps {
	uint32_t max_num;
	uint32_t supported_format; /* enum mlx5dv_cqe_comp_res_format */
};

/*
 * Direct verbs device-specific attributes
 */
struct mlx5dv_context {
	uint8_t		version;
	uint64_t	flags;
	uint64_t	comp_mask;
	struct mlx5dv_cqe_comp_caps	cqe_comp_caps;
};

enum mlx5dv_context_flags {
	/*
	 * This flag indicates if CQE version 0 or 1 is needed.
	 */
	MLX5DV_CONTEXT_FLAGS_CQE_V1	= (1 << 0),
	MLX5DV_CONTEXT_FLAGS_MPW	= (1 << 1),
};

enum mlx5dv_cq_init_attr_mask {
	MLX5DV_CQ_INIT_ATTR_MASK_COMPRESSED_CQE	= 1 << 0,
	MLX5DV_CQ_INIT_ATTR_MASK_RESERVED	= 1 << 1,
};

struct mlx5dv_cq_init_attr {
	uint64_t comp_mask; /* Use enum mlx5dv_cq_init_attr_mask */
	uint8_t cqe_comp_res_format; /* Use enum mlx5dv_cqe_comp_res_format */
};

struct ibv_cq_ex *mlx5dv_create_cq(struct ibv_context *context,
				   struct ibv_cq_init_attr_ex *cq_attr,
				   struct mlx5dv_cq_init_attr *mlx5_cq_attr);
/*
 * Most device capabilities are exported by ibv_query_device(...),
 * but there is HW device-specific information which is important
 * for data-path, but isn't provided.
 *
 * Return 0 on success.
 */
int mlx5dv_query_device(struct ibv_context *ctx_in,
			struct mlx5dv_context *attrs_out);

struct mlx5dv_qp {
	uint32_t		*dbrec;
	struct {
		void		*buf;
		uint32_t	wqe_cnt;
		uint32_t	stride;
	} sq;
	struct {
		void		*buf;
		uint32_t	wqe_cnt;
		uint32_t	stride;
	} rq;
	struct {
		void		*reg;
		uint32_t	size;
	} bf;
	uint64_t		comp_mask;
};

struct mlx5dv_cq {
	void			*buf;
	uint32_t		*dbrec;
	uint32_t		cqe_cnt;
	uint32_t		cqe_size;
	void			*uar;
	uint32_t		cqn;
	uint64_t		comp_mask;
};

struct mlx5dv_srq {
	void			*buf;
	uint32_t		*dbrec;
	uint32_t		stride;
	uint32_t		head;
	uint32_t		tail;
	uint64_t		comp_mask;
};

struct mlx5dv_rwq {
	void		*buf;
	uint32_t	*dbrec;
	uint32_t	wqe_cnt;
	uint32_t	stride;
	uint64_t	comp_mask;
};

struct mlx5dv_obj {
	struct {
		struct ibv_qp		*in;
		struct mlx5dv_qp	*out;
	} qp;
	struct {
		struct ibv_cq		*in;
		struct mlx5dv_cq	*out;
	} cq;
	struct {
		struct ibv_srq		*in;
		struct mlx5dv_srq	*out;
	} srq;
	struct {
		struct ibv_wq		*in;
		struct mlx5dv_rwq	*out;
	} rwq;
};

enum mlx5dv_obj_type {
	MLX5DV_OBJ_QP	= 1 << 0,
	MLX5DV_OBJ_CQ	= 1 << 1,
	MLX5DV_OBJ_SRQ	= 1 << 2,
	MLX5DV_OBJ_RWQ	= 1 << 3,
};

/*
 * This function will initialize mlx5dv_xxx structs based on supplied type.
 * The information for initialization is taken from ibv_xx structs supplied
 * as part of input.
 *
 * Request information of CQ marks its owned by DV for all consumer index
 * related actions.
 *
 * The initialization type can be combination of several types together.
 *
 * Return: 0 in case of success.
 */
int mlx5dv_init_obj(struct mlx5dv_obj *obj, uint64_t obj_type);

enum {
	MLX5_OPCODE_NOP			= 0x00,
	MLX5_OPCODE_SEND_INVAL		= 0x01,
	MLX5_OPCODE_RDMA_WRITE		= 0x08,
	MLX5_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX5_OPCODE_SEND		= 0x0a,
	MLX5_OPCODE_SEND_IMM		= 0x0b,
	MLX5_OPCODE_TSO			= 0x0e,
	MLX5_OPCODE_RDMA_READ		= 0x10,
	MLX5_OPCODE_ATOMIC_CS		= 0x11,
	MLX5_OPCODE_ATOMIC_FA		= 0x12,
	MLX5_OPCODE_ATOMIC_MASKED_CS	= 0x14,
	MLX5_OPCODE_ATOMIC_MASKED_FA	= 0x15,
	MLX5_OPCODE_FMR			= 0x19,
	MLX5_OPCODE_LOCAL_INVAL		= 0x1b,
	MLX5_OPCODE_CONFIG_CMD		= 0x1f,
	MLX5_OPCODE_UMR			= 0x25,
};

/*
 * CQE related part
 */

enum {
	MLX5_INLINE_SCATTER_32	= 0x4,
	MLX5_INLINE_SCATTER_64	= 0x8,
};

enum {
	MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR		= 0x01,
	MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR		= 0x02,
	MLX5_CQE_SYNDROME_LOCAL_PROT_ERR		= 0x04,
	MLX5_CQE_SYNDROME_WR_FLUSH_ERR			= 0x05,
	MLX5_CQE_SYNDROME_MW_BIND_ERR			= 0x06,
	MLX5_CQE_SYNDROME_BAD_RESP_ERR			= 0x10,
	MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR		= 0x11,
	MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR		= 0x12,
	MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR		= 0x13,
	MLX5_CQE_SYNDROME_REMOTE_OP_ERR			= 0x14,
	MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR	= 0x15,
	MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR		= 0x16,
	MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR		= 0x22,
};

enum {
	MLX5_CQE_L2_OK = 1 << 0,
	MLX5_CQE_L3_OK = 1 << 1,
	MLX5_CQE_L4_OK = 1 << 2,
};

enum {
	MLX5_CQE_L3_HDR_TYPE_NONE = 0x0,
	MLX5_CQE_L3_HDR_TYPE_IPV6 = 0x1,
	MLX5_CQE_L3_HDR_TYPE_IPV4 = 0x2,
};

enum {
	MLX5_CQE_OWNER_MASK	= 1,
	MLX5_CQE_REQ		= 0,
	MLX5_CQE_RESP_WR_IMM	= 1,
	MLX5_CQE_RESP_SEND	= 2,
	MLX5_CQE_RESP_SEND_IMM	= 3,
	MLX5_CQE_RESP_SEND_INV	= 4,
	MLX5_CQE_RESIZE_CQ	= 5,
	MLX5_CQE_REQ_ERR	= 13,
	MLX5_CQE_RESP_ERR	= 14,
	MLX5_CQE_INVALID	= 15,
};

enum {
	MLX5_CQ_DOORBELL			= 0x20
};

enum {
	MLX5_CQ_DB_REQ_NOT_SOL	= 1 << 24,
	MLX5_CQ_DB_REQ_NOT	= 0 << 24,
};

struct mlx5_err_cqe {
	uint8_t		rsvd0[32];
	uint32_t	srqn;
	uint8_t		rsvd1[18];
	uint8_t		vendor_err_synd;
	uint8_t		syndrome;
	uint32_t	s_wqe_opcode_qpn;
	uint16_t	wqe_counter;
	uint8_t		signature;
	uint8_t		op_own;
};

struct mlx5_cqe64 {
	uint8_t		rsvd0[17];
	uint8_t		ml_path;
	uint8_t		rsvd20[4];
	uint16_t	slid;
	uint32_t	flags_rqpn;
	uint8_t		hds_ip_ext;
	uint8_t		l4_hdr_type_etc;
	uint16_t	vlan_info;
	uint32_t	srqn_uidx;
	uint32_t	imm_inval_pkey;
	uint8_t		rsvd40[4];
	uint32_t	byte_cnt;
	__be64		timestamp;
	uint32_t	sop_drop_qpn;
	uint16_t	wqe_counter;
	uint8_t		signature;
	uint8_t		op_own;
};

enum mlx5dv_cqe_comp_res_format {
	MLX5DV_CQE_RES_FORMAT_HASH		= 1 << 0,
	MLX5DV_CQE_RES_FORMAT_CSUM		= 1 << 1,
	MLX5DV_CQE_RES_FORMAT_RESERVED		= 1 << 2,
};

static MLX5DV_ALWAYS_INLINE
uint8_t mlx5dv_get_cqe_owner(struct mlx5_cqe64 *cqe)
{
	return cqe->op_own & 0x1;
}

static MLX5DV_ALWAYS_INLINE
void mlx5dv_set_cqe_owner(struct mlx5_cqe64 *cqe, uint8_t val)
{
	cqe->op_own = (val & 0x1) | (cqe->op_own & ~0x1);
}

/* Solicited event */
static MLX5DV_ALWAYS_INLINE
uint8_t mlx5dv_get_cqe_se(struct mlx5_cqe64 *cqe)
{
	return (cqe->op_own >> 1) & 0x1;
}

static MLX5DV_ALWAYS_INLINE
uint8_t mlx5dv_get_cqe_format(struct mlx5_cqe64 *cqe)
{
	return (cqe->op_own >> 2) & 0x3;
}

static MLX5DV_ALWAYS_INLINE
uint8_t mlx5dv_get_cqe_opcode(struct mlx5_cqe64 *cqe)
{
	return cqe->op_own >> 4;
}

/*
 * WQE related part
 */
enum {
	MLX5_INVALID_LKEY	= 0x100,
};

enum {
	MLX5_EXTENDED_UD_AV	= 0x80000000,
};

enum {
	MLX5_WQE_CTRL_CQ_UPDATE	= 2 << 2,
	MLX5_WQE_CTRL_SOLICITED	= 1 << 1,
	MLX5_WQE_CTRL_FENCE	= 4 << 5,
	MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE = 1 << 5,
};

enum {
	MLX5_SEND_WQE_BB	= 64,
	MLX5_SEND_WQE_SHIFT	= 6,
};

enum {
	MLX5_INLINE_SEG	= 0x80000000,
};

enum {
	MLX5_ETH_WQE_L3_CSUM = (1 << 6),
	MLX5_ETH_WQE_L4_CSUM = (1 << 7),
};

struct mlx5_wqe_srq_next_seg {
	uint8_t			rsvd0[2];
	uint16_t		next_wqe_index;
	uint8_t			signature;
	uint8_t			rsvd1[11];
};

struct mlx5_wqe_data_seg {
	uint32_t		byte_count;
	uint32_t		lkey;
	uint64_t		addr;
};

struct mlx5_wqe_ctrl_seg {
	uint32_t	opmod_idx_opcode;
	uint32_t	qpn_ds;
	uint8_t		signature;
	uint8_t		rsvd[2];
	uint8_t		fm_ce_se;
	uint32_t	imm;
};

struct mlx5_wqe_av {
	union {
		struct {
			uint32_t	qkey;
			uint32_t	reserved;
		} qkey;
		uint64_t	dc_key;
	} key;
	uint32_t	dqp_dct;
	uint8_t		stat_rate_sl;
	uint8_t		fl_mlid;
	uint16_t	rlid;
	uint8_t		reserved0[4];
	uint8_t		rmac[6];
	uint8_t		tclass;
	uint8_t		hop_limit;
	uint32_t	grh_gid_fl;
	uint8_t		rgid[16];
};

struct mlx5_wqe_datagram_seg {
	struct mlx5_wqe_av	av;
};

struct mlx5_wqe_raddr_seg {
	uint64_t	raddr;
	uint32_t	rkey;
	uint32_t	reserved;
};

struct mlx5_wqe_atomic_seg {
	uint64_t	swap_add;
	uint64_t	compare;
};

struct mlx5_wqe_inl_data_seg {
	uint32_t	byte_count;
};

struct mlx5_wqe_eth_seg {
	uint32_t	rsvd0;
	uint8_t		cs_flags;
	uint8_t		rsvd1;
	uint16_t	mss;
	uint32_t	rsvd2;
	uint16_t	inline_hdr_sz;
	uint8_t		inline_hdr_start[2];
	uint8_t		inline_hdr[16];
};

/*
 * Control segment - contains some control information for the current WQE.
 *
 * Output:
 *	seg	  - control segment to be filled
 * Input:
 *	pi	  - WQEBB number of the first block of this WQE.
 *		    This number should wrap at 0xffff, regardless of
 *		    size of the WQ.
 *	opcode	  - Opcode of this WQE. Encodes the type of operation
 *		    to be executed on the QP.
 *	opmod	  - Opcode modifier.
 *	qp_num	  - QP/SQ number this WQE is posted to.
 *	fm_ce_se  - FM (fence mode), CE (completion and event mode)
 *		    and SE (solicited event).
 *	ds	  - WQE size in octowords (16-byte units). DS accounts for all
 *		    the segments in the WQE as summarized in WQE construction.
 *	signature - WQE signature.
 *	imm	  - Immediate data/Invalidation key/UMR mkey.
 */
static MLX5DV_ALWAYS_INLINE
void mlx5dv_set_ctrl_seg(struct mlx5_wqe_ctrl_seg *seg, uint16_t pi,
			 uint8_t opcode, uint8_t opmod, uint32_t qp_num,
			 uint8_t fm_ce_se, uint8_t ds,
			 uint8_t signature, uint32_t imm)
{
	seg->opmod_idx_opcode	= htobe32(((uint32_t)opmod << 24) | ((uint32_t)pi << 8) | opcode);
	seg->qpn_ds		= htobe32((qp_num << 8) | ds);
	seg->fm_ce_se		= fm_ce_se;
	seg->signature		= signature;
	/*
	 * The caller should prepare "imm" in advance based on WR opcode.
	 * For IBV_WR_SEND_WITH_IMM and IBV_WR_RDMA_WRITE_WITH_IMM,
	 * the "imm" should be assigned as is.
	 * For the IBV_WR_SEND_WITH_INV, it should be htobe32(imm).
	 */
	seg->imm		= imm;
}

/* x86 optimized version of mlx5dv_set_ctrl_seg()
 *
 * This is useful when doing calculations on large data sets
 * for parallel calculations.
 *
 * It doesn't suit for serialized algorithms.
 */
#if defined(__SSE3__)
static MLX5DV_ALWAYS_INLINE
void mlx5dv_x86_set_ctrl_seg(struct mlx5_wqe_ctrl_seg *seg, uint16_t pi,
			     uint8_t opcode, uint8_t opmod, uint32_t qp_num,
			     uint8_t fm_ce_se, uint8_t ds,
			     uint8_t signature, uint32_t imm)
{
	__m128i val  = _mm_set_epi32(imm, qp_num, (ds << 16) | pi,
				     (signature << 24) | (opcode << 16) | (opmod << 8) | fm_ce_se);
	__m128i mask = _mm_set_epi8(15, 14, 13, 12,	/* immediate */
				     0,			/* signal/fence_mode */
				     0x80, 0x80,	/* reserved */
				     3,			/* signature */
				     6,			/* data size */
				     8, 9, 10,		/* QP num */
				     2,			/* opcode */
				     4, 5,		/* sw_pi in BE */
				     1			/* opmod */
				     );
	*(__m128i *) seg = _mm_shuffle_epi8(val, mask);
}
#endif /* defined(__SSE3__) */

/*
 * Datagram Segment - contains address information required in order
 * to form a datagram message.
 *
 * Output:
 *	seg		- datagram segment to be filled.
 * Input:
 *	key		- Q_key/access key.
 *	dqp_dct		- Destination QP number for UD and DCT for DC.
 *	ext		- Address vector extension.
 *	stat_rate_sl	- Maximum static rate control, SL/ethernet priority.
 *	fl_mlid		- Force loopback and source LID for IB.
 *	rlid		- Remote LID
 *	rmac		- Remote MAC
 *	tclass		- GRH tclass/IPv6 tclass/IPv4 ToS
 *	hop_limit	- GRH hop limit/IPv6 hop limit/IPv4 TTL
 *	grh_gid_fi	- GRH, source GID address and IPv6 flow label.
 *	rgid		- Remote GID/IP address.
 */
static MLX5DV_ALWAYS_INLINE
void mlx5dv_set_dgram_seg(struct mlx5_wqe_datagram_seg *seg,
			  uint64_t key, uint32_t dqp_dct,
			  uint8_t ext, uint8_t stat_rate_sl,
			  uint8_t fl_mlid, uint16_t rlid,
			  uint8_t *rmac, uint8_t tclass,
			  uint8_t hop_limit, uint32_t grh_gid_fi,
			  uint8_t *rgid)
{

	/* Always put 64 bits, in q_key, the reserved part will be 0 */
	seg->av.key.dc_key	= htobe64(key);
	seg->av.dqp_dct		= htobe32(((uint32_t)ext << 31) | dqp_dct);
	seg->av.stat_rate_sl	= stat_rate_sl;
	seg->av.fl_mlid		= fl_mlid;
	seg->av.rlid		= htobe16(rlid);
	memcpy(seg->av.rmac, rmac, 6);
	seg->av.tclass		= tclass;
	seg->av.hop_limit	= hop_limit;
	seg->av.grh_gid_fl	= htobe32(grh_gid_fi);
	memcpy(seg->av.rgid, rgid, 16);
}

/*
 * Data Segments - contain pointers and a byte count for the scatter/gather list.
 * They can optionally contain data, which will save a memory read access for
 * gather Work Requests.
 */
static MLX5DV_ALWAYS_INLINE
void mlx5dv_set_data_seg(struct mlx5_wqe_data_seg *seg,
			 uint32_t length, uint32_t lkey,
			 uintptr_t address)
{
	seg->byte_count = htobe32(length);
	seg->lkey       = htobe32(lkey);
	seg->addr       = htobe64(address);
}
/*
 * x86 optimized version of mlx5dv_set_data_seg()
 *
 * This is useful when doing calculations on large data sets
 * for parallel calculations.
 *
 * It doesn't suit for serialized algorithms.
 */
#if defined(__SSE3__)
static MLX5DV_ALWAYS_INLINE
void mlx5dv_x86_set_data_seg(struct mlx5_wqe_data_seg *seg,
			     uint32_t length, uint32_t lkey,
			     uintptr_t address)
{
	__m128i val  = _mm_set_epi32((uint32_t)address, (uint32_t)(address >> 32), lkey, length);
	__m128i mask = _mm_set_epi8(12, 13, 14, 15,	/* local address low */
				     8, 9, 10, 11,	/* local address high */
				     4, 5, 6, 7,	/* l_key */
				     0, 1, 2, 3		/* byte count */
				     );
	*(__m128i *) seg = _mm_shuffle_epi8(val, mask);
}
#endif /* defined(__SSE3__) */

/*
 * Eth Segment - contains packet headers and information for stateless L2, L3, L4 offloading.
 *
 * Output:
 *	 seg		 - Eth segment to be filled.
 * Input:
 *	cs_flags	 - l3cs/l3cs_inner/l4cs/l4cs_inner.
 *	mss		 - Maximum segment size. For TSO WQEs, the number of bytes
 *			   in the TCP payload to be transmitted in each packet. Must
 *			   be 0 on non TSO WQEs.
 *	inline_hdr_sz	 - Length of the inlined packet headers.
 *	inline_hdr_start - Inlined packet header.
 */
static MLX5DV_ALWAYS_INLINE
void mlx5dv_set_eth_seg(struct mlx5_wqe_eth_seg *seg, uint8_t cs_flags,
			uint16_t mss, uint16_t inline_hdr_sz,
			uint8_t *inline_hdr_start)
{
	seg->cs_flags		= cs_flags;
	seg->mss		= htobe16(mss);
	seg->inline_hdr_sz	= htobe16(inline_hdr_sz);
	memcpy(seg->inline_hdr_start, inline_hdr_start, inline_hdr_sz);
}
#endif /* _MLX5DV_H_ */
