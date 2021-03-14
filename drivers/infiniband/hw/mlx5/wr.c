// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2020, Mellanox Technologies inc. All rights reserved.
 */

#include <linux/gfp.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/driver.h>
#include "wr.h"

static const u32 mlx5_ib_opcode[] = {
	[IB_WR_SEND]				= MLX5_OPCODE_SEND,
	[IB_WR_LSO]				= MLX5_OPCODE_LSO,
	[IB_WR_SEND_WITH_IMM]			= MLX5_OPCODE_SEND_IMM,
	[IB_WR_RDMA_WRITE]			= MLX5_OPCODE_RDMA_WRITE,
	[IB_WR_RDMA_WRITE_WITH_IMM]		= MLX5_OPCODE_RDMA_WRITE_IMM,
	[IB_WR_RDMA_READ]			= MLX5_OPCODE_RDMA_READ,
	[IB_WR_ATOMIC_CMP_AND_SWP]		= MLX5_OPCODE_ATOMIC_CS,
	[IB_WR_ATOMIC_FETCH_AND_ADD]		= MLX5_OPCODE_ATOMIC_FA,
	[IB_WR_SEND_WITH_INV]			= MLX5_OPCODE_SEND_INVAL,
	[IB_WR_LOCAL_INV]			= MLX5_OPCODE_UMR,
	[IB_WR_REG_MR]				= MLX5_OPCODE_UMR,
	[IB_WR_MASKED_ATOMIC_CMP_AND_SWP]	= MLX5_OPCODE_ATOMIC_MASKED_CS,
	[IB_WR_MASKED_ATOMIC_FETCH_AND_ADD]	= MLX5_OPCODE_ATOMIC_MASKED_FA,
	[MLX5_IB_WR_UMR]			= MLX5_OPCODE_UMR,
};

/* handle_post_send_edge - Check if we get to SQ edge. If yes, update to the
 * next nearby edge and get new address translation for current WQE position.
 * @sq - SQ buffer.
 * @seg: Current WQE position (16B aligned).
 * @wqe_sz: Total current WQE size [16B].
 * @cur_edge: Updated current edge.
 */
static inline void handle_post_send_edge(struct mlx5_ib_wq *sq, void **seg,
					 u32 wqe_sz, void **cur_edge)
{
	u32 idx;

	if (likely(*seg != *cur_edge))
		return;

	idx = (sq->cur_post + (wqe_sz >> 2)) & (sq->wqe_cnt - 1);
	*cur_edge = get_sq_edge(sq, idx);

	*seg = mlx5_frag_buf_get_wqe(&sq->fbc, idx);
}

/* memcpy_send_wqe - copy data from src to WQE and update the relevant WQ's
 * pointers. At the end @seg is aligned to 16B regardless the copied size.
 * @sq - SQ buffer.
 * @cur_edge: Updated current edge.
 * @seg: Current WQE position (16B aligned).
 * @wqe_sz: Total current WQE size [16B].
 * @src: Pointer to copy from.
 * @n: Number of bytes to copy.
 */
static inline void memcpy_send_wqe(struct mlx5_ib_wq *sq, void **cur_edge,
				   void **seg, u32 *wqe_sz, const void *src,
				   size_t n)
{
	while (likely(n)) {
		size_t leftlen = *cur_edge - *seg;
		size_t copysz = min_t(size_t, leftlen, n);
		size_t stride;

		memcpy(*seg, src, copysz);

		n -= copysz;
		src += copysz;
		stride = !n ? ALIGN(copysz, 16) : copysz;
		*seg += stride;
		*wqe_sz += stride >> 4;
		handle_post_send_edge(sq, seg, *wqe_sz, cur_edge);
	}
}

static int mlx5_wq_overflow(struct mlx5_ib_wq *wq, int nreq,
			    struct ib_cq *ib_cq)
{
	struct mlx5_ib_cq *cq;
	unsigned int cur;

	cur = wq->head - wq->tail;
	if (likely(cur + nreq < wq->max_post))
		return 0;

	cq = to_mcq(ib_cq);
	spin_lock(&cq->lock);
	cur = wq->head - wq->tail;
	spin_unlock(&cq->lock);

	return cur + nreq >= wq->max_post;
}

static __always_inline void set_raddr_seg(struct mlx5_wqe_raddr_seg *rseg,
					  u64 remote_addr, u32 rkey)
{
	rseg->raddr    = cpu_to_be64(remote_addr);
	rseg->rkey     = cpu_to_be32(rkey);
	rseg->reserved = 0;
}

static void set_eth_seg(const struct ib_send_wr *wr, struct mlx5_ib_qp *qp,
			void **seg, int *size, void **cur_edge)
{
	struct mlx5_wqe_eth_seg *eseg = *seg;

	memset(eseg, 0, sizeof(struct mlx5_wqe_eth_seg));

	if (wr->send_flags & IB_SEND_IP_CSUM)
		eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM |
				 MLX5_ETH_WQE_L4_CSUM;

	if (wr->opcode == IB_WR_LSO) {
		struct ib_ud_wr *ud_wr = container_of(wr, struct ib_ud_wr, wr);
		size_t left, copysz;
		void *pdata = ud_wr->header;
		size_t stride;

		left = ud_wr->hlen;
		eseg->mss = cpu_to_be16(ud_wr->mss);
		eseg->inline_hdr.sz = cpu_to_be16(left);

		/* memcpy_send_wqe should get a 16B align address. Hence, we
		 * first copy up to the current edge and then, if needed,
		 * continue to memcpy_send_wqe.
		 */
		copysz = min_t(u64, *cur_edge - (void *)eseg->inline_hdr.start,
			       left);
		memcpy(eseg->inline_hdr.start, pdata, copysz);
		stride = ALIGN(sizeof(struct mlx5_wqe_eth_seg) -
			       sizeof(eseg->inline_hdr.start) + copysz, 16);
		*size += stride / 16;
		*seg += stride;

		if (copysz < left) {
			handle_post_send_edge(&qp->sq, seg, *size, cur_edge);
			left -= copysz;
			pdata += copysz;
			memcpy_send_wqe(&qp->sq, cur_edge, seg, size, pdata,
					left);
		}

		return;
	}

	*seg += sizeof(struct mlx5_wqe_eth_seg);
	*size += sizeof(struct mlx5_wqe_eth_seg) / 16;
}

static void set_datagram_seg(struct mlx5_wqe_datagram_seg *dseg,
			     const struct ib_send_wr *wr)
{
	memcpy(&dseg->av, &to_mah(ud_wr(wr)->ah)->av, sizeof(struct mlx5_av));
	dseg->av.dqp_dct =
		cpu_to_be32(ud_wr(wr)->remote_qpn | MLX5_EXTENDED_UD_AV);
	dseg->av.key.qkey.qkey = cpu_to_be32(ud_wr(wr)->remote_qkey);
}

static void set_data_ptr_seg(struct mlx5_wqe_data_seg *dseg, struct ib_sge *sg)
{
	dseg->byte_count = cpu_to_be32(sg->length);
	dseg->lkey       = cpu_to_be32(sg->lkey);
	dseg->addr       = cpu_to_be64(sg->addr);
}

static u64 get_xlt_octo(u64 bytes)
{
	return ALIGN(bytes, MLX5_IB_UMR_XLT_ALIGNMENT) /
	       MLX5_IB_UMR_OCTOWORD;
}

static __be64 frwr_mkey_mask(bool atomic)
{
	u64 result;

	result = MLX5_MKEY_MASK_LEN		|
		MLX5_MKEY_MASK_PAGE_SIZE	|
		MLX5_MKEY_MASK_START_ADDR	|
		MLX5_MKEY_MASK_EN_RINVAL	|
		MLX5_MKEY_MASK_KEY		|
		MLX5_MKEY_MASK_LR		|
		MLX5_MKEY_MASK_LW		|
		MLX5_MKEY_MASK_RR		|
		MLX5_MKEY_MASK_RW		|
		MLX5_MKEY_MASK_SMALL_FENCE	|
		MLX5_MKEY_MASK_FREE;

	if (atomic)
		result |= MLX5_MKEY_MASK_A;

	return cpu_to_be64(result);
}

static __be64 sig_mkey_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_LEN		|
		MLX5_MKEY_MASK_PAGE_SIZE	|
		MLX5_MKEY_MASK_START_ADDR	|
		MLX5_MKEY_MASK_EN_SIGERR	|
		MLX5_MKEY_MASK_EN_RINVAL	|
		MLX5_MKEY_MASK_KEY		|
		MLX5_MKEY_MASK_LR		|
		MLX5_MKEY_MASK_LW		|
		MLX5_MKEY_MASK_RR		|
		MLX5_MKEY_MASK_RW		|
		MLX5_MKEY_MASK_SMALL_FENCE	|
		MLX5_MKEY_MASK_FREE		|
		MLX5_MKEY_MASK_BSF_EN;

	return cpu_to_be64(result);
}

static void set_reg_umr_seg(struct mlx5_wqe_umr_ctrl_seg *umr,
			    struct mlx5_ib_mr *mr, u8 flags, bool atomic)
{
	int size = (mr->ndescs + mr->meta_ndescs) * mr->desc_size;

	memset(umr, 0, sizeof(*umr));

	umr->flags = flags;
	umr->xlt_octowords = cpu_to_be16(get_xlt_octo(size));
	umr->mkey_mask = frwr_mkey_mask(atomic);
}

static void set_linv_umr_seg(struct mlx5_wqe_umr_ctrl_seg *umr)
{
	memset(umr, 0, sizeof(*umr));
	umr->mkey_mask = cpu_to_be64(MLX5_MKEY_MASK_FREE);
	umr->flags = MLX5_UMR_INLINE;
}

static __be64 get_umr_enable_mr_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_KEY |
		 MLX5_MKEY_MASK_FREE;

	return cpu_to_be64(result);
}

static __be64 get_umr_disable_mr_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_FREE;

	return cpu_to_be64(result);
}

static __be64 get_umr_update_translation_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_LEN |
		 MLX5_MKEY_MASK_PAGE_SIZE |
		 MLX5_MKEY_MASK_START_ADDR;

	return cpu_to_be64(result);
}

static __be64 get_umr_update_access_mask(int atomic,
					 int relaxed_ordering_write,
					 int relaxed_ordering_read)
{
	u64 result;

	result = MLX5_MKEY_MASK_LR |
		 MLX5_MKEY_MASK_LW |
		 MLX5_MKEY_MASK_RR |
		 MLX5_MKEY_MASK_RW;

	if (atomic)
		result |= MLX5_MKEY_MASK_A;

	if (relaxed_ordering_write)
		result |= MLX5_MKEY_MASK_RELAXED_ORDERING_WRITE;

	if (relaxed_ordering_read)
		result |= MLX5_MKEY_MASK_RELAXED_ORDERING_READ;

	return cpu_to_be64(result);
}

static __be64 get_umr_update_pd_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_PD;

	return cpu_to_be64(result);
}

static int umr_check_mkey_mask(struct mlx5_ib_dev *dev, u64 mask)
{
	if (mask & MLX5_MKEY_MASK_PAGE_SIZE &&
	    MLX5_CAP_GEN(dev->mdev, umr_modify_entity_size_disabled))
		return -EPERM;

	if (mask & MLX5_MKEY_MASK_A &&
	    MLX5_CAP_GEN(dev->mdev, umr_modify_atomic_disabled))
		return -EPERM;

	if (mask & MLX5_MKEY_MASK_RELAXED_ORDERING_WRITE &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write_umr))
		return -EPERM;

	if (mask & MLX5_MKEY_MASK_RELAXED_ORDERING_READ &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_umr))
		return -EPERM;

	return 0;
}

static int set_reg_umr_segment(struct mlx5_ib_dev *dev,
			       struct mlx5_wqe_umr_ctrl_seg *umr,
			       const struct ib_send_wr *wr)
{
	const struct mlx5_umr_wr *umrwr = umr_wr(wr);

	memset(umr, 0, sizeof(*umr));

	if (!umrwr->ignore_free_state) {
		if (wr->send_flags & MLX5_IB_SEND_UMR_FAIL_IF_FREE)
			 /* fail if free */
			umr->flags = MLX5_UMR_CHECK_FREE;
		else
			/* fail if not free */
			umr->flags = MLX5_UMR_CHECK_NOT_FREE;
	}

	umr->xlt_octowords = cpu_to_be16(get_xlt_octo(umrwr->xlt_size));
	if (wr->send_flags & MLX5_IB_SEND_UMR_UPDATE_XLT) {
		u64 offset = get_xlt_octo(umrwr->offset);

		umr->xlt_offset = cpu_to_be16(offset & 0xffff);
		umr->xlt_offset_47_16 = cpu_to_be32(offset >> 16);
		umr->flags |= MLX5_UMR_TRANSLATION_OFFSET_EN;
	}
	if (wr->send_flags & MLX5_IB_SEND_UMR_UPDATE_TRANSLATION)
		umr->mkey_mask |= get_umr_update_translation_mask();
	if (wr->send_flags & MLX5_IB_SEND_UMR_UPDATE_PD_ACCESS) {
		umr->mkey_mask |= get_umr_update_access_mask(
			!!(MLX5_CAP_GEN(dev->mdev, atomic)),
			!!(MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write_umr)),
			!!(MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_umr)));
		umr->mkey_mask |= get_umr_update_pd_mask();
	}
	if (wr->send_flags & MLX5_IB_SEND_UMR_ENABLE_MR)
		umr->mkey_mask |= get_umr_enable_mr_mask();
	if (wr->send_flags & MLX5_IB_SEND_UMR_DISABLE_MR)
		umr->mkey_mask |= get_umr_disable_mr_mask();

	if (!wr->num_sge)
		umr->flags |= MLX5_UMR_INLINE;

	return umr_check_mkey_mask(dev, be64_to_cpu(umr->mkey_mask));
}

static u8 get_umr_flags(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? MLX5_PERM_ATOMIC       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ? MLX5_PERM_REMOTE_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ? MLX5_PERM_REMOTE_READ  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? MLX5_PERM_LOCAL_WRITE  : 0) |
		MLX5_PERM_LOCAL_READ | MLX5_PERM_UMR_EN;
}

static void set_reg_mkey_seg(struct mlx5_mkey_seg *seg,
			     struct mlx5_ib_mr *mr,
			     u32 key, int access)
{
	int ndescs = ALIGN(mr->ndescs + mr->meta_ndescs, 8) >> 1;

	memset(seg, 0, sizeof(*seg));

	if (mr->access_mode == MLX5_MKC_ACCESS_MODE_MTT)
		seg->log2_page_size = ilog2(mr->ibmr.page_size);
	else if (mr->access_mode == MLX5_MKC_ACCESS_MODE_KLMS)
		/* KLMs take twice the size of MTTs */
		ndescs *= 2;

	seg->flags = get_umr_flags(access) | mr->access_mode;
	seg->qpn_mkey7_0 = cpu_to_be32((key & 0xff) | 0xffffff00);
	seg->flags_pd = cpu_to_be32(MLX5_MKEY_REMOTE_INVAL);
	seg->start_addr = cpu_to_be64(mr->ibmr.iova);
	seg->len = cpu_to_be64(mr->ibmr.length);
	seg->xlt_oct_size = cpu_to_be32(ndescs);
}

static void set_linv_mkey_seg(struct mlx5_mkey_seg *seg)
{
	memset(seg, 0, sizeof(*seg));
	seg->status = MLX5_MKEY_STATUS_FREE;
}

static void set_reg_mkey_segment(struct mlx5_ib_dev *dev,
				 struct mlx5_mkey_seg *seg,
				 const struct ib_send_wr *wr)
{
	const struct mlx5_umr_wr *umrwr = umr_wr(wr);

	memset(seg, 0, sizeof(*seg));
	if (wr->send_flags & MLX5_IB_SEND_UMR_DISABLE_MR)
		MLX5_SET(mkc, seg, free, 1);

	MLX5_SET(mkc, seg, a,
		 !!(umrwr->access_flags & IB_ACCESS_REMOTE_ATOMIC));
	MLX5_SET(mkc, seg, rw,
		 !!(umrwr->access_flags & IB_ACCESS_REMOTE_WRITE));
	MLX5_SET(mkc, seg, rr, !!(umrwr->access_flags & IB_ACCESS_REMOTE_READ));
	MLX5_SET(mkc, seg, lw, !!(umrwr->access_flags & IB_ACCESS_LOCAL_WRITE));
	MLX5_SET(mkc, seg, lr, 1);
	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write_umr))
		MLX5_SET(mkc, seg, relaxed_ordering_write,
			 !!(umrwr->access_flags & IB_ACCESS_RELAXED_ORDERING));
	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_umr))
		MLX5_SET(mkc, seg, relaxed_ordering_read,
			 !!(umrwr->access_flags & IB_ACCESS_RELAXED_ORDERING));

	if (umrwr->pd)
		MLX5_SET(mkc, seg, pd, to_mpd(umrwr->pd)->pdn);
	if (wr->send_flags & MLX5_IB_SEND_UMR_UPDATE_TRANSLATION &&
	    !umrwr->length)
		MLX5_SET(mkc, seg, length64, 1);

	MLX5_SET64(mkc, seg, start_addr, umrwr->virt_addr);
	MLX5_SET64(mkc, seg, len, umrwr->length);
	MLX5_SET(mkc, seg, log_page_size, umrwr->page_shift);
	MLX5_SET(mkc, seg, qpn, 0xffffff);
	MLX5_SET(mkc, seg, mkey_7_0, mlx5_mkey_variant(umrwr->mkey));
}

static void set_reg_data_seg(struct mlx5_wqe_data_seg *dseg,
			     struct mlx5_ib_mr *mr,
			     struct mlx5_ib_pd *pd)
{
	int bcount = mr->desc_size * (mr->ndescs + mr->meta_ndescs);

	dseg->addr = cpu_to_be64(mr->desc_map);
	dseg->byte_count = cpu_to_be32(ALIGN(bcount, 64));
	dseg->lkey = cpu_to_be32(pd->ibpd.local_dma_lkey);
}

static __be32 send_ieth(const struct ib_send_wr *wr)
{
	switch (wr->opcode) {
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		return wr->ex.imm_data;

	case IB_WR_SEND_WITH_INV:
		return cpu_to_be32(wr->ex.invalidate_rkey);

	default:
		return 0;
	}
}

static u8 calc_sig(void *wqe, int size)
{
	u8 *p = wqe;
	u8 res = 0;
	int i;

	for (i = 0; i < size; i++)
		res ^= p[i];

	return ~res;
}

static u8 wq_sig(void *wqe)
{
	return calc_sig(wqe, (*((u8 *)wqe + 8) & 0x3f) << 4);
}

static int set_data_inl_seg(struct mlx5_ib_qp *qp, const struct ib_send_wr *wr,
			    void **wqe, int *wqe_sz, void **cur_edge)
{
	struct mlx5_wqe_inline_seg *seg;
	size_t offset;
	int inl = 0;
	int i;

	seg = *wqe;
	*wqe += sizeof(*seg);
	offset = sizeof(*seg);

	for (i = 0; i < wr->num_sge; i++) {
		size_t len  = wr->sg_list[i].length;
		void *addr = (void *)(unsigned long)(wr->sg_list[i].addr);

		inl += len;

		if (unlikely(inl > qp->max_inline_data))
			return -ENOMEM;

		while (likely(len)) {
			size_t leftlen;
			size_t copysz;

			handle_post_send_edge(&qp->sq, wqe,
					      *wqe_sz + (offset >> 4),
					      cur_edge);

			leftlen = *cur_edge - *wqe;
			copysz = min_t(size_t, leftlen, len);

			memcpy(*wqe, addr, copysz);
			len -= copysz;
			addr += copysz;
			*wqe += copysz;
			offset += copysz;
		}
	}

	seg->byte_count = cpu_to_be32(inl | MLX5_INLINE_SEG);

	*wqe_sz +=  ALIGN(inl + sizeof(seg->byte_count), 16) / 16;

	return 0;
}

static u16 prot_field_size(enum ib_signature_type type)
{
	switch (type) {
	case IB_SIG_TYPE_T10_DIF:
		return MLX5_DIF_SIZE;
	default:
		return 0;
	}
}

static u8 bs_selector(int block_size)
{
	switch (block_size) {
	case 512:	    return 0x1;
	case 520:	    return 0x2;
	case 4096:	    return 0x3;
	case 4160:	    return 0x4;
	case 1073741824:    return 0x5;
	default:	    return 0;
	}
}

static void mlx5_fill_inl_bsf(struct ib_sig_domain *domain,
			      struct mlx5_bsf_inl *inl)
{
	/* Valid inline section and allow BSF refresh */
	inl->vld_refresh = cpu_to_be16(MLX5_BSF_INL_VALID |
				       MLX5_BSF_REFRESH_DIF);
	inl->dif_apptag = cpu_to_be16(domain->sig.dif.app_tag);
	inl->dif_reftag = cpu_to_be32(domain->sig.dif.ref_tag);
	/* repeating block */
	inl->rp_inv_seed = MLX5_BSF_REPEAT_BLOCK;
	inl->sig_type = domain->sig.dif.bg_type == IB_T10DIF_CRC ?
			MLX5_DIF_CRC : MLX5_DIF_IPCS;

	if (domain->sig.dif.ref_remap)
		inl->dif_inc_ref_guard_check |= MLX5_BSF_INC_REFTAG;

	if (domain->sig.dif.app_escape) {
		if (domain->sig.dif.ref_escape)
			inl->dif_inc_ref_guard_check |= MLX5_BSF_APPREF_ESCAPE;
		else
			inl->dif_inc_ref_guard_check |= MLX5_BSF_APPTAG_ESCAPE;
	}

	inl->dif_app_bitmask_check =
		cpu_to_be16(domain->sig.dif.apptag_check_mask);
}

static int mlx5_set_bsf(struct ib_mr *sig_mr,
			struct ib_sig_attrs *sig_attrs,
			struct mlx5_bsf *bsf, u32 data_size)
{
	struct mlx5_core_sig_ctx *msig = to_mmr(sig_mr)->sig;
	struct mlx5_bsf_basic *basic = &bsf->basic;
	struct ib_sig_domain *mem = &sig_attrs->mem;
	struct ib_sig_domain *wire = &sig_attrs->wire;

	memset(bsf, 0, sizeof(*bsf));

	/* Basic + Extended + Inline */
	basic->bsf_size_sbs = 1 << 7;
	/* Input domain check byte mask */
	basic->check_byte_mask = sig_attrs->check_mask;
	basic->raw_data_size = cpu_to_be32(data_size);

	/* Memory domain */
	switch (sig_attrs->mem.sig_type) {
	case IB_SIG_TYPE_NONE:
		break;
	case IB_SIG_TYPE_T10_DIF:
		basic->mem.bs_selector = bs_selector(mem->sig.dif.pi_interval);
		basic->m_bfs_psv = cpu_to_be32(msig->psv_memory.psv_idx);
		mlx5_fill_inl_bsf(mem, &bsf->m_inl);
		break;
	default:
		return -EINVAL;
	}

	/* Wire domain */
	switch (sig_attrs->wire.sig_type) {
	case IB_SIG_TYPE_NONE:
		break;
	case IB_SIG_TYPE_T10_DIF:
		if (mem->sig.dif.pi_interval == wire->sig.dif.pi_interval &&
		    mem->sig_type == wire->sig_type) {
			/* Same block structure */
			basic->bsf_size_sbs |= 1 << 4;
			if (mem->sig.dif.bg_type == wire->sig.dif.bg_type)
				basic->wire.copy_byte_mask |= MLX5_CPY_GRD_MASK;
			if (mem->sig.dif.app_tag == wire->sig.dif.app_tag)
				basic->wire.copy_byte_mask |= MLX5_CPY_APP_MASK;
			if (mem->sig.dif.ref_tag == wire->sig.dif.ref_tag)
				basic->wire.copy_byte_mask |= MLX5_CPY_REF_MASK;
		} else
			basic->wire.bs_selector =
				bs_selector(wire->sig.dif.pi_interval);

		basic->w_bfs_psv = cpu_to_be32(msig->psv_wire.psv_idx);
		mlx5_fill_inl_bsf(wire, &bsf->w_inl);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int set_sig_data_segment(const struct ib_send_wr *send_wr,
				struct ib_mr *sig_mr,
				struct ib_sig_attrs *sig_attrs,
				struct mlx5_ib_qp *qp, void **seg, int *size,
				void **cur_edge)
{
	struct mlx5_bsf *bsf;
	u32 data_len;
	u32 data_key;
	u64 data_va;
	u32 prot_len = 0;
	u32 prot_key = 0;
	u64 prot_va = 0;
	bool prot = false;
	int ret;
	int wqe_size;
	struct mlx5_ib_mr *mr = to_mmr(sig_mr);
	struct mlx5_ib_mr *pi_mr = mr->pi_mr;

	data_len = pi_mr->data_length;
	data_key = pi_mr->ibmr.lkey;
	data_va = pi_mr->data_iova;
	if (pi_mr->meta_ndescs) {
		prot_len = pi_mr->meta_length;
		prot_key = pi_mr->ibmr.lkey;
		prot_va = pi_mr->pi_iova;
		prot = true;
	}

	if (!prot || (data_key == prot_key && data_va == prot_va &&
		      data_len == prot_len)) {
		/**
		 * Source domain doesn't contain signature information
		 * or data and protection are interleaved in memory.
		 * So need construct:
		 *                  ------------------
		 *                 |     data_klm     |
		 *                  ------------------
		 *                 |       BSF        |
		 *                  ------------------
		 **/
		struct mlx5_klm *data_klm = *seg;

		data_klm->bcount = cpu_to_be32(data_len);
		data_klm->key = cpu_to_be32(data_key);
		data_klm->va = cpu_to_be64(data_va);
		wqe_size = ALIGN(sizeof(*data_klm), 64);
	} else {
		/**
		 * Source domain contains signature information
		 * So need construct a strided block format:
		 *               ---------------------------
		 *              |     stride_block_ctrl     |
		 *               ---------------------------
		 *              |          data_klm         |
		 *               ---------------------------
		 *              |          prot_klm         |
		 *               ---------------------------
		 *              |             BSF           |
		 *               ---------------------------
		 **/
		struct mlx5_stride_block_ctrl_seg *sblock_ctrl;
		struct mlx5_stride_block_entry *data_sentry;
		struct mlx5_stride_block_entry *prot_sentry;
		u16 block_size = sig_attrs->mem.sig.dif.pi_interval;
		int prot_size;

		sblock_ctrl = *seg;
		data_sentry = (void *)sblock_ctrl + sizeof(*sblock_ctrl);
		prot_sentry = (void *)data_sentry + sizeof(*data_sentry);

		prot_size = prot_field_size(sig_attrs->mem.sig_type);
		if (!prot_size) {
			pr_err("Bad block size given: %u\n", block_size);
			return -EINVAL;
		}
		sblock_ctrl->bcount_per_cycle = cpu_to_be32(block_size +
							    prot_size);
		sblock_ctrl->op = cpu_to_be32(MLX5_STRIDE_BLOCK_OP);
		sblock_ctrl->repeat_count = cpu_to_be32(data_len / block_size);
		sblock_ctrl->num_entries = cpu_to_be16(2);

		data_sentry->bcount = cpu_to_be16(block_size);
		data_sentry->key = cpu_to_be32(data_key);
		data_sentry->va = cpu_to_be64(data_va);
		data_sentry->stride = cpu_to_be16(block_size);

		prot_sentry->bcount = cpu_to_be16(prot_size);
		prot_sentry->key = cpu_to_be32(prot_key);
		prot_sentry->va = cpu_to_be64(prot_va);
		prot_sentry->stride = cpu_to_be16(prot_size);

		wqe_size = ALIGN(sizeof(*sblock_ctrl) + sizeof(*data_sentry) +
				 sizeof(*prot_sentry), 64);
	}

	*seg += wqe_size;
	*size += wqe_size / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);

	bsf = *seg;
	ret = mlx5_set_bsf(sig_mr, sig_attrs, bsf, data_len);
	if (ret)
		return -EINVAL;

	*seg += sizeof(*bsf);
	*size += sizeof(*bsf) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);

	return 0;
}

static void set_sig_mkey_segment(struct mlx5_mkey_seg *seg,
				 struct ib_mr *sig_mr, int access_flags,
				 u32 size, u32 length, u32 pdn)
{
	u32 sig_key = sig_mr->rkey;
	u8 sigerr = to_mmr(sig_mr)->sig->sigerr_count & 1;

	memset(seg, 0, sizeof(*seg));

	seg->flags = get_umr_flags(access_flags) | MLX5_MKC_ACCESS_MODE_KLMS;
	seg->qpn_mkey7_0 = cpu_to_be32((sig_key & 0xff) | 0xffffff00);
	seg->flags_pd = cpu_to_be32(MLX5_MKEY_REMOTE_INVAL | sigerr << 26 |
				    MLX5_MKEY_BSF_EN | pdn);
	seg->len = cpu_to_be64(length);
	seg->xlt_oct_size = cpu_to_be32(get_xlt_octo(size));
	seg->bsfs_octo_size = cpu_to_be32(MLX5_MKEY_BSF_OCTO_SIZE);
}

static void set_sig_umr_segment(struct mlx5_wqe_umr_ctrl_seg *umr,
				u32 size)
{
	memset(umr, 0, sizeof(*umr));

	umr->flags = MLX5_FLAGS_INLINE | MLX5_FLAGS_CHECK_FREE;
	umr->xlt_octowords = cpu_to_be16(get_xlt_octo(size));
	umr->bsf_octowords = cpu_to_be16(MLX5_MKEY_BSF_OCTO_SIZE);
	umr->mkey_mask = sig_mkey_mask();
}

static int set_pi_umr_wr(const struct ib_send_wr *send_wr,
			 struct mlx5_ib_qp *qp, void **seg, int *size,
			 void **cur_edge)
{
	const struct ib_reg_wr *wr = reg_wr(send_wr);
	struct mlx5_ib_mr *sig_mr = to_mmr(wr->mr);
	struct mlx5_ib_mr *pi_mr = sig_mr->pi_mr;
	struct ib_sig_attrs *sig_attrs = sig_mr->ibmr.sig_attrs;
	u32 pdn = to_mpd(qp->ibqp.pd)->pdn;
	u32 xlt_size;
	int region_len, ret;

	if (unlikely(send_wr->num_sge != 0) ||
	    unlikely(wr->access & IB_ACCESS_REMOTE_ATOMIC) ||
	    unlikely(!sig_mr->sig) || unlikely(!qp->ibqp.integrity_en) ||
	    unlikely(!sig_mr->sig->sig_status_checked))
		return -EINVAL;

	/* length of the protected region, data + protection */
	region_len = pi_mr->ibmr.length;

	/**
	 * KLM octoword size - if protection was provided
	 * then we use strided block format (3 octowords),
	 * else we use single KLM (1 octoword)
	 **/
	if (sig_attrs->mem.sig_type != IB_SIG_TYPE_NONE)
		xlt_size = 0x30;
	else
		xlt_size = sizeof(struct mlx5_klm);

	set_sig_umr_segment(*seg, xlt_size);
	*seg += sizeof(struct mlx5_wqe_umr_ctrl_seg);
	*size += sizeof(struct mlx5_wqe_umr_ctrl_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);

	set_sig_mkey_segment(*seg, wr->mr, wr->access, xlt_size, region_len,
			     pdn);
	*seg += sizeof(struct mlx5_mkey_seg);
	*size += sizeof(struct mlx5_mkey_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);

	ret = set_sig_data_segment(send_wr, wr->mr, sig_attrs, qp, seg, size,
				   cur_edge);
	if (ret)
		return ret;

	sig_mr->sig->sig_status_checked = false;
	return 0;
}

static int set_psv_wr(struct ib_sig_domain *domain,
		      u32 psv_idx, void **seg, int *size)
{
	struct mlx5_seg_set_psv *psv_seg = *seg;

	memset(psv_seg, 0, sizeof(*psv_seg));
	psv_seg->psv_num = cpu_to_be32(psv_idx);
	switch (domain->sig_type) {
	case IB_SIG_TYPE_NONE:
		break;
	case IB_SIG_TYPE_T10_DIF:
		psv_seg->transient_sig = cpu_to_be32(domain->sig.dif.bg << 16 |
						     domain->sig.dif.app_tag);
		psv_seg->ref_tag = cpu_to_be32(domain->sig.dif.ref_tag);
		break;
	default:
		pr_err("Bad signature type (%d) is given.\n",
		       domain->sig_type);
		return -EINVAL;
	}

	*seg += sizeof(*psv_seg);
	*size += sizeof(*psv_seg) / 16;

	return 0;
}

static int set_reg_wr(struct mlx5_ib_qp *qp,
		      const struct ib_reg_wr *wr,
		      void **seg, int *size, void **cur_edge,
		      bool check_not_free)
{
	struct mlx5_ib_mr *mr = to_mmr(wr->mr);
	struct mlx5_ib_pd *pd = to_mpd(qp->ibqp.pd);
	struct mlx5_ib_dev *dev = to_mdev(pd->ibpd.device);
	int mr_list_size = (mr->ndescs + mr->meta_ndescs) * mr->desc_size;
	bool umr_inline = mr_list_size <= MLX5_IB_SQ_UMR_INLINE_THRESHOLD;
	bool atomic = wr->access & IB_ACCESS_REMOTE_ATOMIC;
	u8 flags = 0;

	/* Matches access in mlx5_set_umr_free_mkey() */
	if (!mlx5_ib_can_reconfig_with_umr(dev, 0, wr->access)) {
		mlx5_ib_warn(
			to_mdev(qp->ibqp.device),
			"Fast update for MR access flags is not possible\n");
		return -EINVAL;
	}

	if (unlikely(wr->wr.send_flags & IB_SEND_INLINE)) {
		mlx5_ib_warn(to_mdev(qp->ibqp.device),
			     "Invalid IB_SEND_INLINE send flag\n");
		return -EINVAL;
	}

	if (check_not_free)
		flags |= MLX5_UMR_CHECK_NOT_FREE;
	if (umr_inline)
		flags |= MLX5_UMR_INLINE;

	set_reg_umr_seg(*seg, mr, flags, atomic);
	*seg += sizeof(struct mlx5_wqe_umr_ctrl_seg);
	*size += sizeof(struct mlx5_wqe_umr_ctrl_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);

	set_reg_mkey_seg(*seg, mr, wr->key, wr->access);
	*seg += sizeof(struct mlx5_mkey_seg);
	*size += sizeof(struct mlx5_mkey_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);

	if (umr_inline) {
		memcpy_send_wqe(&qp->sq, cur_edge, seg, size, mr->descs,
				mr_list_size);
		*size = ALIGN(*size, MLX5_SEND_WQE_BB >> 4);
	} else {
		set_reg_data_seg(*seg, mr, pd);
		*seg += sizeof(struct mlx5_wqe_data_seg);
		*size += (sizeof(struct mlx5_wqe_data_seg) / 16);
	}
	return 0;
}

static void set_linv_wr(struct mlx5_ib_qp *qp, void **seg, int *size,
			void **cur_edge)
{
	set_linv_umr_seg(*seg);
	*seg += sizeof(struct mlx5_wqe_umr_ctrl_seg);
	*size += sizeof(struct mlx5_wqe_umr_ctrl_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);
	set_linv_mkey_seg(*seg);
	*seg += sizeof(struct mlx5_mkey_seg);
	*size += sizeof(struct mlx5_mkey_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);
}

static void dump_wqe(struct mlx5_ib_qp *qp, u32 idx, int size_16)
{
	__be32 *p = NULL;
	int i, j;

	pr_debug("dump WQE index %u:\n", idx);
	for (i = 0, j = 0; i < size_16 * 4; i += 4, j += 4) {
		if ((i & 0xf) == 0) {
			p = mlx5_frag_buf_get_wqe(&qp->sq.fbc, idx);
			pr_debug("WQBB at %p:\n", (void *)p);
			j = 0;
			idx = (idx + 1) & (qp->sq.wqe_cnt - 1);
		}
		pr_debug("%08x %08x %08x %08x\n", be32_to_cpu(p[j]),
			 be32_to_cpu(p[j + 1]), be32_to_cpu(p[j + 2]),
			 be32_to_cpu(p[j + 3]));
	}
}

static int __begin_wqe(struct mlx5_ib_qp *qp, void **seg,
		       struct mlx5_wqe_ctrl_seg **ctrl,
		       const struct ib_send_wr *wr, unsigned int *idx,
		       int *size, void **cur_edge, int nreq,
		       bool send_signaled, bool solicited)
{
	if (unlikely(mlx5_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)))
		return -ENOMEM;

	*idx = qp->sq.cur_post & (qp->sq.wqe_cnt - 1);
	*seg = mlx5_frag_buf_get_wqe(&qp->sq.fbc, *idx);
	*ctrl = *seg;
	*(uint32_t *)(*seg + 8) = 0;
	(*ctrl)->imm = send_ieth(wr);
	(*ctrl)->fm_ce_se = qp->sq_signal_bits |
		(send_signaled ? MLX5_WQE_CTRL_CQ_UPDATE : 0) |
		(solicited ? MLX5_WQE_CTRL_SOLICITED : 0);

	*seg += sizeof(**ctrl);
	*size = sizeof(**ctrl) / 16;
	*cur_edge = qp->sq.cur_edge;

	return 0;
}

static int begin_wqe(struct mlx5_ib_qp *qp, void **seg,
		     struct mlx5_wqe_ctrl_seg **ctrl,
		     const struct ib_send_wr *wr, unsigned int *idx, int *size,
		     void **cur_edge, int nreq)
{
	return __begin_wqe(qp, seg, ctrl, wr, idx, size, cur_edge, nreq,
			   wr->send_flags & IB_SEND_SIGNALED,
			   wr->send_flags & IB_SEND_SOLICITED);
}

static void finish_wqe(struct mlx5_ib_qp *qp,
		       struct mlx5_wqe_ctrl_seg *ctrl,
		       void *seg, u8 size, void *cur_edge,
		       unsigned int idx, u64 wr_id, int nreq, u8 fence,
		       u32 mlx5_opcode)
{
	u8 opmod = 0;

	ctrl->opmod_idx_opcode = cpu_to_be32(((u32)(qp->sq.cur_post) << 8) |
					     mlx5_opcode | ((u32)opmod << 24));
	ctrl->qpn_ds = cpu_to_be32(size | (qp->trans_qp.base.mqp.qpn << 8));
	ctrl->fm_ce_se |= fence;
	if (unlikely(qp->flags_en & MLX5_QP_FLAG_SIGNATURE))
		ctrl->signature = wq_sig(ctrl);

	qp->sq.wrid[idx] = wr_id;
	qp->sq.w_list[idx].opcode = mlx5_opcode;
	qp->sq.wqe_head[idx] = qp->sq.head + nreq;
	qp->sq.cur_post += DIV_ROUND_UP(size * 16, MLX5_SEND_WQE_BB);
	qp->sq.w_list[idx].next = qp->sq.cur_post;

	/* We save the edge which was possibly updated during the WQE
	 * construction, into SQ's cache.
	 */
	seg = PTR_ALIGN(seg, MLX5_SEND_WQE_BB);
	qp->sq.cur_edge = (unlikely(seg == cur_edge)) ?
			  get_sq_edge(&qp->sq, qp->sq.cur_post &
				      (qp->sq.wqe_cnt - 1)) :
			  cur_edge;
}

static void handle_rdma_op(const struct ib_send_wr *wr, void **seg, int *size)
{
	set_raddr_seg(*seg, rdma_wr(wr)->remote_addr, rdma_wr(wr)->rkey);
	*seg += sizeof(struct mlx5_wqe_raddr_seg);
	*size += sizeof(struct mlx5_wqe_raddr_seg) / 16;
}

static void handle_local_inv(struct mlx5_ib_qp *qp, const struct ib_send_wr *wr,
			     struct mlx5_wqe_ctrl_seg **ctrl, void **seg,
			     int *size, void **cur_edge, unsigned int idx)
{
	qp->sq.wr_data[idx] = IB_WR_LOCAL_INV;
	(*ctrl)->imm = cpu_to_be32(wr->ex.invalidate_rkey);
	set_linv_wr(qp, seg, size, cur_edge);
}

static int handle_reg_mr(struct mlx5_ib_qp *qp, const struct ib_send_wr *wr,
			 struct mlx5_wqe_ctrl_seg **ctrl, void **seg, int *size,
			 void **cur_edge, unsigned int idx)
{
	qp->sq.wr_data[idx] = IB_WR_REG_MR;
	(*ctrl)->imm = cpu_to_be32(reg_wr(wr)->key);
	return set_reg_wr(qp, reg_wr(wr), seg, size, cur_edge, true);
}

static int handle_psv(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
		      const struct ib_send_wr *wr,
		      struct mlx5_wqe_ctrl_seg **ctrl, void **seg, int *size,
		      void **cur_edge, unsigned int *idx, int nreq,
		      struct ib_sig_domain *domain, u32 psv_index,
		      u8 next_fence)
{
	int err;

	/*
	 * SET_PSV WQEs are not signaled and solicited on error.
	 */
	err = __begin_wqe(qp, seg, ctrl, wr, idx, size, cur_edge, nreq,
			  false, true);
	if (unlikely(err)) {
		mlx5_ib_warn(dev, "\n");
		err = -ENOMEM;
		goto out;
	}
	err = set_psv_wr(domain, psv_index, seg, size);
	if (unlikely(err)) {
		mlx5_ib_warn(dev, "\n");
		goto out;
	}
	finish_wqe(qp, *ctrl, *seg, *size, *cur_edge, *idx, wr->wr_id, nreq,
		   next_fence, MLX5_OPCODE_SET_PSV);

out:
	return err;
}

static int handle_reg_mr_integrity(struct mlx5_ib_dev *dev,
				   struct mlx5_ib_qp *qp,
				   const struct ib_send_wr *wr,
				   struct mlx5_wqe_ctrl_seg **ctrl, void **seg,
				   int *size, void **cur_edge,
				   unsigned int *idx, int nreq, u8 fence,
				   u8 next_fence)
{
	struct mlx5_ib_mr *mr;
	struct mlx5_ib_mr *pi_mr;
	struct mlx5_ib_mr pa_pi_mr;
	struct ib_sig_attrs *sig_attrs;
	struct ib_reg_wr reg_pi_wr;
	int err;

	qp->sq.wr_data[*idx] = IB_WR_REG_MR_INTEGRITY;

	mr = to_mmr(reg_wr(wr)->mr);
	pi_mr = mr->pi_mr;

	if (pi_mr) {
		memset(&reg_pi_wr, 0,
		       sizeof(struct ib_reg_wr));

		reg_pi_wr.mr = &pi_mr->ibmr;
		reg_pi_wr.access = reg_wr(wr)->access;
		reg_pi_wr.key = pi_mr->ibmr.rkey;

		(*ctrl)->imm = cpu_to_be32(reg_pi_wr.key);
		/* UMR for data + prot registration */
		err = set_reg_wr(qp, &reg_pi_wr, seg, size, cur_edge, false);
		if (unlikely(err))
			goto out;

		finish_wqe(qp, *ctrl, *seg, *size, *cur_edge, *idx, wr->wr_id,
			   nreq, fence, MLX5_OPCODE_UMR);

		err = begin_wqe(qp, seg, ctrl, wr, idx, size, cur_edge, nreq);
		if (unlikely(err)) {
			mlx5_ib_warn(dev, "\n");
			err = -ENOMEM;
			goto out;
		}
	} else {
		memset(&pa_pi_mr, 0, sizeof(struct mlx5_ib_mr));
		/* No UMR, use local_dma_lkey */
		pa_pi_mr.ibmr.lkey = mr->ibmr.pd->local_dma_lkey;
		pa_pi_mr.ndescs = mr->ndescs;
		pa_pi_mr.data_length = mr->data_length;
		pa_pi_mr.data_iova = mr->data_iova;
		if (mr->meta_ndescs) {
			pa_pi_mr.meta_ndescs = mr->meta_ndescs;
			pa_pi_mr.meta_length = mr->meta_length;
			pa_pi_mr.pi_iova = mr->pi_iova;
		}

		pa_pi_mr.ibmr.length = mr->ibmr.length;
		mr->pi_mr = &pa_pi_mr;
	}
	(*ctrl)->imm = cpu_to_be32(mr->ibmr.rkey);
	/* UMR for sig MR */
	err = set_pi_umr_wr(wr, qp, seg, size, cur_edge);
	if (unlikely(err)) {
		mlx5_ib_warn(dev, "\n");
		goto out;
	}
	finish_wqe(qp, *ctrl, *seg, *size, *cur_edge, *idx, wr->wr_id, nreq,
		   fence, MLX5_OPCODE_UMR);

	sig_attrs = mr->ibmr.sig_attrs;
	err = handle_psv(dev, qp, wr, ctrl, seg, size, cur_edge, idx, nreq,
			 &sig_attrs->mem, mr->sig->psv_memory.psv_idx,
			 next_fence);
	if (unlikely(err))
		goto out;

	err = handle_psv(dev, qp, wr, ctrl, seg, size, cur_edge, idx, nreq,
			 &sig_attrs->wire, mr->sig->psv_wire.psv_idx,
			 next_fence);
	if (unlikely(err))
		goto out;

	qp->next_fence = MLX5_FENCE_MODE_INITIATOR_SMALL;

out:
	return err;
}

static int handle_qpt_rc(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
			 const struct ib_send_wr *wr,
			 struct mlx5_wqe_ctrl_seg **ctrl, void **seg, int *size,
			 void **cur_edge, unsigned int *idx, int nreq, u8 fence,
			 u8 next_fence, int *num_sge)
{
	int err = 0;

	switch (wr->opcode) {
	case IB_WR_RDMA_READ:
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		handle_rdma_op(wr, seg, size);
		break;

	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
	case IB_WR_MASKED_ATOMIC_CMP_AND_SWP:
		mlx5_ib_warn(dev, "Atomic operations are not supported yet\n");
		err = -EOPNOTSUPP;
		goto out;

	case IB_WR_LOCAL_INV:
		handle_local_inv(qp, wr, ctrl, seg, size, cur_edge, *idx);
		*num_sge = 0;
		break;

	case IB_WR_REG_MR:
		err = handle_reg_mr(qp, wr, ctrl, seg, size, cur_edge, *idx);
		if (unlikely(err))
			goto out;
		*num_sge = 0;
		break;

	case IB_WR_REG_MR_INTEGRITY:
		err = handle_reg_mr_integrity(dev, qp, wr, ctrl, seg, size,
					      cur_edge, idx, nreq, fence,
					      next_fence);
		if (unlikely(err))
			goto out;
		*num_sge = 0;
		break;

	default:
		break;
	}

out:
	return err;
}

static void handle_qpt_uc(const struct ib_send_wr *wr, void **seg, int *size)
{
	switch (wr->opcode) {
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		handle_rdma_op(wr, seg, size);
		break;
	default:
		break;
	}
}

static void handle_qpt_hw_gsi(struct mlx5_ib_qp *qp,
			      const struct ib_send_wr *wr, void **seg,
			      int *size, void **cur_edge)
{
	set_datagram_seg(*seg, wr);
	*seg += sizeof(struct mlx5_wqe_datagram_seg);
	*size += sizeof(struct mlx5_wqe_datagram_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);
}

static void handle_qpt_ud(struct mlx5_ib_qp *qp, const struct ib_send_wr *wr,
			  void **seg, int *size, void **cur_edge)
{
	set_datagram_seg(*seg, wr);
	*seg += sizeof(struct mlx5_wqe_datagram_seg);
	*size += sizeof(struct mlx5_wqe_datagram_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);

	/* handle qp that supports ud offload */
	if (qp->flags & IB_QP_CREATE_IPOIB_UD_LSO) {
		struct mlx5_wqe_eth_pad *pad;

		pad = *seg;
		memset(pad, 0, sizeof(struct mlx5_wqe_eth_pad));
		*seg += sizeof(struct mlx5_wqe_eth_pad);
		*size += sizeof(struct mlx5_wqe_eth_pad) / 16;
		set_eth_seg(wr, qp, seg, size, cur_edge);
		handle_post_send_edge(&qp->sq, seg, *size, cur_edge);
	}
}

static int handle_qpt_reg_umr(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
			      const struct ib_send_wr *wr,
			      struct mlx5_wqe_ctrl_seg **ctrl, void **seg,
			      int *size, void **cur_edge, unsigned int idx)
{
	int err = 0;

	if (unlikely(wr->opcode != MLX5_IB_WR_UMR)) {
		err = -EINVAL;
		mlx5_ib_warn(dev, "bad opcode %d\n", wr->opcode);
		goto out;
	}

	qp->sq.wr_data[idx] = MLX5_IB_WR_UMR;
	(*ctrl)->imm = cpu_to_be32(umr_wr(wr)->mkey);
	err = set_reg_umr_segment(dev, *seg, wr);
	if (unlikely(err))
		goto out;
	*seg += sizeof(struct mlx5_wqe_umr_ctrl_seg);
	*size += sizeof(struct mlx5_wqe_umr_ctrl_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);
	set_reg_mkey_segment(dev, *seg, wr);
	*seg += sizeof(struct mlx5_mkey_seg);
	*size += sizeof(struct mlx5_mkey_seg) / 16;
	handle_post_send_edge(&qp->sq, seg, *size, cur_edge);
out:
	return err;
}

int mlx5_ib_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		      const struct ib_send_wr **bad_wr, bool drain)
{
	struct mlx5_wqe_ctrl_seg *ctrl = NULL;  /* compiler warning */
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_ib_qp *qp;
	struct mlx5_wqe_xrc_seg *xrc;
	struct mlx5_bf *bf;
	void *cur_edge;
	int size;
	unsigned long flags;
	unsigned int idx;
	int err = 0;
	int num_sge;
	void *seg;
	int nreq;
	int i;
	u8 next_fence = 0;
	u8 fence;

	if (unlikely(mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR &&
		     !drain)) {
		*bad_wr = wr;
		return -EIO;
	}

	if (unlikely(ibqp->qp_type == IB_QPT_GSI))
		return mlx5_ib_gsi_post_send(ibqp, wr, bad_wr);

	qp = to_mqp(ibqp);
	bf = &qp->bf;

	spin_lock_irqsave(&qp->sq.lock, flags);

	for (nreq = 0; wr; nreq++, wr = wr->next) {
		if (unlikely(wr->opcode >= ARRAY_SIZE(mlx5_ib_opcode))) {
			mlx5_ib_warn(dev, "\n");
			err = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		num_sge = wr->num_sge;
		if (unlikely(num_sge > qp->sq.max_gs)) {
			mlx5_ib_warn(dev, "\n");
			err = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		err = begin_wqe(qp, &seg, &ctrl, wr, &idx, &size, &cur_edge,
				nreq);
		if (err) {
			mlx5_ib_warn(dev, "\n");
			err = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (wr->opcode == IB_WR_REG_MR ||
		    wr->opcode == IB_WR_REG_MR_INTEGRITY) {
			fence = dev->umr_fence;
			next_fence = MLX5_FENCE_MODE_INITIATOR_SMALL;
		} else  {
			if (wr->send_flags & IB_SEND_FENCE) {
				if (qp->next_fence)
					fence = MLX5_FENCE_MODE_SMALL_AND_FENCE;
				else
					fence = MLX5_FENCE_MODE_FENCE;
			} else {
				fence = qp->next_fence;
			}
		}

		switch (ibqp->qp_type) {
		case IB_QPT_XRC_INI:
			xrc = seg;
			seg += sizeof(*xrc);
			size += sizeof(*xrc) / 16;
			fallthrough;
		case IB_QPT_RC:
			err = handle_qpt_rc(dev, qp, wr, &ctrl, &seg, &size,
					    &cur_edge, &idx, nreq, fence,
					    next_fence, &num_sge);
			if (unlikely(err)) {
				*bad_wr = wr;
				goto out;
			} else if (wr->opcode == IB_WR_REG_MR_INTEGRITY) {
				goto skip_psv;
			}
			break;

		case IB_QPT_UC:
			handle_qpt_uc(wr, &seg, &size);
			break;
		case IB_QPT_SMI:
			if (unlikely(!dev->port_caps[qp->port - 1].has_smi)) {
				mlx5_ib_warn(dev, "Send SMP MADs is not allowed\n");
				err = -EPERM;
				*bad_wr = wr;
				goto out;
			}
			fallthrough;
		case MLX5_IB_QPT_HW_GSI:
			handle_qpt_hw_gsi(qp, wr, &seg, &size, &cur_edge);
			break;
		case IB_QPT_UD:
			handle_qpt_ud(qp, wr, &seg, &size, &cur_edge);
			break;
		case MLX5_IB_QPT_REG_UMR:
			err = handle_qpt_reg_umr(dev, qp, wr, &ctrl, &seg,
						       &size, &cur_edge, idx);
			if (unlikely(err))
				goto out;
			break;

		default:
			break;
		}

		if (wr->send_flags & IB_SEND_INLINE && num_sge) {
			err = set_data_inl_seg(qp, wr, &seg, &size, &cur_edge);
			if (unlikely(err)) {
				mlx5_ib_warn(dev, "\n");
				*bad_wr = wr;
				goto out;
			}
		} else {
			for (i = 0; i < num_sge; i++) {
				handle_post_send_edge(&qp->sq, &seg, size,
						      &cur_edge);
				if (unlikely(!wr->sg_list[i].length))
					continue;

				set_data_ptr_seg(
					(struct mlx5_wqe_data_seg *)seg,
					wr->sg_list + i);
				size += sizeof(struct mlx5_wqe_data_seg) / 16;
				seg += sizeof(struct mlx5_wqe_data_seg);
			}
		}

		qp->next_fence = next_fence;
		finish_wqe(qp, ctrl, seg, size, cur_edge, idx, wr->wr_id, nreq,
			   fence, mlx5_ib_opcode[wr->opcode]);
skip_psv:
		if (0)
			dump_wqe(qp, idx, size);
	}

out:
	if (likely(nreq)) {
		qp->sq.head += nreq;

		/* Make sure that descriptors are written before
		 * updating doorbell record and ringing the doorbell
		 */
		wmb();

		qp->db.db[MLX5_SND_DBR] = cpu_to_be32(qp->sq.cur_post);

		/* Make sure doorbell record is visible to the HCA before
		 * we hit doorbell.
		 */
		wmb();

		mlx5_write64((__be32 *)ctrl, bf->bfreg->map + bf->offset);
		/* Make sure doorbells don't leak out of SQ spinlock
		 * and reach the HCA out of order.
		 */
		bf->offset ^= bf->buf_size;
	}

	spin_unlock_irqrestore(&qp->sq.lock, flags);

	return err;
}

static void set_sig_seg(struct mlx5_rwqe_sig *sig, int max_gs)
{
	 sig->signature = calc_sig(sig, (max_gs + 1) << 2);
}

int mlx5_ib_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr, bool drain)
{
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	struct mlx5_wqe_data_seg *scat;
	struct mlx5_rwqe_sig *sig;
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_core_dev *mdev = dev->mdev;
	unsigned long flags;
	int err = 0;
	int nreq;
	int ind;
	int i;

	if (unlikely(mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR &&
		     !drain)) {
		*bad_wr = wr;
		return -EIO;
	}

	if (unlikely(ibqp->qp_type == IB_QPT_GSI))
		return mlx5_ib_gsi_post_recv(ibqp, wr, bad_wr);

	spin_lock_irqsave(&qp->rq.lock, flags);

	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; nreq++, wr = wr->next) {
		if (mlx5_wq_overflow(&qp->rq, nreq, qp->ibqp.recv_cq)) {
			err = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->rq.max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		scat = mlx5_frag_buf_get_wqe(&qp->rq.fbc, ind);
		if (qp->flags_en & MLX5_QP_FLAG_SIGNATURE)
			scat++;

		for (i = 0; i < wr->num_sge; i++)
			set_data_ptr_seg(scat + i, wr->sg_list + i);

		if (i < qp->rq.max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = cpu_to_be32(MLX5_INVALID_LKEY);
			scat[i].addr       = 0;
		}

		if (qp->flags_en & MLX5_QP_FLAG_SIGNATURE) {
			sig = (struct mlx5_rwqe_sig *)scat;
			set_sig_seg(sig, qp->rq.max_gs);
		}

		qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (qp->rq.wqe_cnt - 1);
	}

out:
	if (likely(nreq)) {
		qp->rq.head += nreq;

		/* Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*qp->db.db = cpu_to_be32(qp->rq.head & 0xffff);
	}

	spin_unlock_irqrestore(&qp->rq.lock, flags);

	return err;
}
