/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
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

#include <rdma/ib_umem.h>
#include <linux/atomic.h>

#include "iw_cxgb4.h"

#define T4_ULPTX_MIN_IO 32
#define C4IW_MAX_INLINE_SIZE 96

static int write_adapter_mem(struct c4iw_rdev *rdev, u32 addr, u32 len,
			     void *data)
{
	struct sk_buff *skb;
	struct ulp_mem_io *req;
	struct ulptx_idata *sc;
	u8 wr_len, *to_dp, *from_dp;
	int copy_len, num_wqe, i, ret = 0;
	struct c4iw_wr_wait wr_wait;

	addr &= 0x7FFFFFF;
	PDBG("%s addr 0x%x len %u\n", __func__, addr, len);
	num_wqe = DIV_ROUND_UP(len, C4IW_MAX_INLINE_SIZE);
	c4iw_init_wr_wait(&wr_wait);
	for (i = 0; i < num_wqe; i++) {

		copy_len = len > C4IW_MAX_INLINE_SIZE ? C4IW_MAX_INLINE_SIZE :
			   len;
		wr_len = roundup(sizeof *req + sizeof *sc +
				 roundup(copy_len, T4_ULPTX_MIN_IO), 16);

		skb = alloc_skb(wr_len, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;
		set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

		req = (struct ulp_mem_io *)__skb_put(skb, wr_len);
		memset(req, 0, wr_len);
		INIT_ULPTX_WR(req, wr_len, 0, 0);

		if (i == (num_wqe-1)) {
			req->wr.wr_hi = cpu_to_be32(FW_WR_OP(FW_ULPTX_WR) |
						    FW_WR_COMPL(1));
			req->wr.wr_lo = (__force __be64)(unsigned long) &wr_wait;
		} else
			req->wr.wr_hi = cpu_to_be32(FW_WR_OP(FW_ULPTX_WR));
		req->wr.wr_mid = cpu_to_be32(
				       FW_WR_LEN16(DIV_ROUND_UP(wr_len, 16)));

		req->cmd = cpu_to_be32(ULPTX_CMD(ULP_TX_MEM_WRITE) | (1<<23));
		req->dlen = cpu_to_be32(ULP_MEMIO_DATA_LEN(
				DIV_ROUND_UP(copy_len, T4_ULPTX_MIN_IO)));
		req->len16 = cpu_to_be32(DIV_ROUND_UP(wr_len-sizeof(req->wr),
						      16));
		req->lock_addr = cpu_to_be32(ULP_MEMIO_ADDR(addr + i * 3));

		sc = (struct ulptx_idata *)(req + 1);
		sc->cmd_more = cpu_to_be32(ULPTX_CMD(ULP_TX_SC_IMM));
		sc->len = cpu_to_be32(roundup(copy_len, T4_ULPTX_MIN_IO));

		to_dp = (u8 *)(sc + 1);
		from_dp = (u8 *)data + i * C4IW_MAX_INLINE_SIZE;
		if (data)
			memcpy(to_dp, from_dp, copy_len);
		else
			memset(to_dp, 0, copy_len);
		if (copy_len % T4_ULPTX_MIN_IO)
			memset(to_dp + copy_len, 0, T4_ULPTX_MIN_IO -
			       (copy_len % T4_ULPTX_MIN_IO));
		ret = c4iw_ofld_send(rdev, skb);
		if (ret)
			return ret;
		len -= C4IW_MAX_INLINE_SIZE;
	}

	ret = c4iw_wait_for_reply(rdev, &wr_wait, 0, 0, __func__);
	return ret;
}

/*
 * Build and write a TPT entry.
 * IN: stag key, pdid, perm, bind_enabled, zbva, to, len, page_size,
 *     pbl_size and pbl_addr
 * OUT: stag index
 */
static int write_tpt_entry(struct c4iw_rdev *rdev, u32 reset_tpt_entry,
			   u32 *stag, u8 stag_state, u32 pdid,
			   enum fw_ri_stag_type type, enum fw_ri_mem_perms perm,
			   int bind_enabled, u32 zbva, u64 to,
			   u64 len, u8 page_size, u32 pbl_size, u32 pbl_addr)
{
	int err;
	struct fw_ri_tpte tpt;
	u32 stag_idx;
	static atomic_t key;

	if (c4iw_fatal_error(rdev))
		return -EIO;

	stag_state = stag_state > 0;
	stag_idx = (*stag) >> 8;

	if ((!reset_tpt_entry) && (*stag == T4_STAG_UNSET)) {
		stag_idx = c4iw_get_resource(&rdev->resource.tpt_table);
		if (!stag_idx)
			return -ENOMEM;
		mutex_lock(&rdev->stats.lock);
		rdev->stats.stag.cur += 32;
		if (rdev->stats.stag.cur > rdev->stats.stag.max)
			rdev->stats.stag.max = rdev->stats.stag.cur;
		mutex_unlock(&rdev->stats.lock);
		*stag = (stag_idx << 8) | (atomic_inc_return(&key) & 0xff);
	}
	PDBG("%s stag_state 0x%0x type 0x%0x pdid 0x%0x, stag_idx 0x%x\n",
	     __func__, stag_state, type, pdid, stag_idx);

	/* write TPT entry */
	if (reset_tpt_entry)
		memset(&tpt, 0, sizeof(tpt));
	else {
		tpt.valid_to_pdid = cpu_to_be32(F_FW_RI_TPTE_VALID |
			V_FW_RI_TPTE_STAGKEY((*stag & M_FW_RI_TPTE_STAGKEY)) |
			V_FW_RI_TPTE_STAGSTATE(stag_state) |
			V_FW_RI_TPTE_STAGTYPE(type) | V_FW_RI_TPTE_PDID(pdid));
		tpt.locread_to_qpid = cpu_to_be32(V_FW_RI_TPTE_PERM(perm) |
			(bind_enabled ? F_FW_RI_TPTE_MWBINDEN : 0) |
			V_FW_RI_TPTE_ADDRTYPE((zbva ? FW_RI_ZERO_BASED_TO :
						      FW_RI_VA_BASED_TO))|
			V_FW_RI_TPTE_PS(page_size));
		tpt.nosnoop_pbladdr = !pbl_size ? 0 : cpu_to_be32(
			V_FW_RI_TPTE_PBLADDR(PBL_OFF(rdev, pbl_addr)>>3));
		tpt.len_lo = cpu_to_be32((u32)(len & 0xffffffffUL));
		tpt.va_hi = cpu_to_be32((u32)(to >> 32));
		tpt.va_lo_fbo = cpu_to_be32((u32)(to & 0xffffffffUL));
		tpt.dca_mwbcnt_pstag = cpu_to_be32(0);
		tpt.len_hi = cpu_to_be32((u32)(len >> 32));
	}
	err = write_adapter_mem(rdev, stag_idx +
				(rdev->lldi.vr->stag.start >> 5),
				sizeof(tpt), &tpt);

	if (reset_tpt_entry) {
		c4iw_put_resource(&rdev->resource.tpt_table, stag_idx);
		mutex_lock(&rdev->stats.lock);
		rdev->stats.stag.cur -= 32;
		mutex_unlock(&rdev->stats.lock);
	}
	return err;
}

static int write_pbl(struct c4iw_rdev *rdev, __be64 *pbl,
		     u32 pbl_addr, u32 pbl_size)
{
	int err;

	PDBG("%s *pdb_addr 0x%x, pbl_base 0x%x, pbl_size %d\n",
	     __func__, pbl_addr, rdev->lldi.vr->pbl.start,
	     pbl_size);

	err = write_adapter_mem(rdev, pbl_addr >> 5, pbl_size << 3, pbl);
	return err;
}

static int dereg_mem(struct c4iw_rdev *rdev, u32 stag, u32 pbl_size,
		     u32 pbl_addr)
{
	return write_tpt_entry(rdev, 1, &stag, 0, 0, 0, 0, 0, 0, 0UL, 0, 0,
			       pbl_size, pbl_addr);
}

static int allocate_window(struct c4iw_rdev *rdev, u32 * stag, u32 pdid)
{
	*stag = T4_STAG_UNSET;
	return write_tpt_entry(rdev, 0, stag, 0, pdid, FW_RI_STAG_MW, 0, 0, 0,
			       0UL, 0, 0, 0, 0);
}

static int deallocate_window(struct c4iw_rdev *rdev, u32 stag)
{
	return write_tpt_entry(rdev, 1, &stag, 0, 0, 0, 0, 0, 0, 0UL, 0, 0, 0,
			       0);
}

static int allocate_stag(struct c4iw_rdev *rdev, u32 *stag, u32 pdid,
			 u32 pbl_size, u32 pbl_addr)
{
	*stag = T4_STAG_UNSET;
	return write_tpt_entry(rdev, 0, stag, 0, pdid, FW_RI_STAG_NSMR, 0, 0, 0,
			       0UL, 0, 0, pbl_size, pbl_addr);
}

static int finish_mem_reg(struct c4iw_mr *mhp, u32 stag)
{
	u32 mmid;

	mhp->attr.state = 1;
	mhp->attr.stag = stag;
	mmid = stag >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	PDBG("%s mmid 0x%x mhp %p\n", __func__, mmid, mhp);
	return insert_handle(mhp->rhp, &mhp->rhp->mmidr, mhp, mmid);
}

static int register_mem(struct c4iw_dev *rhp, struct c4iw_pd *php,
		      struct c4iw_mr *mhp, int shift)
{
	u32 stag = T4_STAG_UNSET;
	int ret;

	ret = write_tpt_entry(&rhp->rdev, 0, &stag, 1, mhp->attr.pdid,
			      FW_RI_STAG_NSMR, mhp->attr.perms,
			      mhp->attr.mw_bind_enable, mhp->attr.zbva,
			      mhp->attr.va_fbo, mhp->attr.len, shift - 12,
			      mhp->attr.pbl_size, mhp->attr.pbl_addr);
	if (ret)
		return ret;

	ret = finish_mem_reg(mhp, stag);
	if (ret)
		dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
	return ret;
}

static int reregister_mem(struct c4iw_dev *rhp, struct c4iw_pd *php,
			  struct c4iw_mr *mhp, int shift, int npages)
{
	u32 stag;
	int ret;

	if (npages > mhp->attr.pbl_size)
		return -ENOMEM;

	stag = mhp->attr.stag;
	ret = write_tpt_entry(&rhp->rdev, 0, &stag, 1, mhp->attr.pdid,
			      FW_RI_STAG_NSMR, mhp->attr.perms,
			      mhp->attr.mw_bind_enable, mhp->attr.zbva,
			      mhp->attr.va_fbo, mhp->attr.len, shift - 12,
			      mhp->attr.pbl_size, mhp->attr.pbl_addr);
	if (ret)
		return ret;

	ret = finish_mem_reg(mhp, stag);
	if (ret)
		dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);

	return ret;
}

static int alloc_pbl(struct c4iw_mr *mhp, int npages)
{
	mhp->attr.pbl_addr = c4iw_pblpool_alloc(&mhp->rhp->rdev,
						    npages << 3);

	if (!mhp->attr.pbl_addr)
		return -ENOMEM;

	mhp->attr.pbl_size = npages;

	return 0;
}

static int build_phys_page_list(struct ib_phys_buf *buffer_list,
				int num_phys_buf, u64 *iova_start,
				u64 *total_size, int *npages,
				int *shift, __be64 **page_list)
{
	u64 mask;
	int i, j, n;

	mask = 0;
	*total_size = 0;
	for (i = 0; i < num_phys_buf; ++i) {
		if (i != 0 && buffer_list[i].addr & ~PAGE_MASK)
			return -EINVAL;
		if (i != 0 && i != num_phys_buf - 1 &&
		    (buffer_list[i].size & ~PAGE_MASK))
			return -EINVAL;
		*total_size += buffer_list[i].size;
		if (i > 0)
			mask |= buffer_list[i].addr;
		else
			mask |= buffer_list[i].addr & PAGE_MASK;
		if (i != num_phys_buf - 1)
			mask |= buffer_list[i].addr + buffer_list[i].size;
		else
			mask |= (buffer_list[i].addr + buffer_list[i].size +
				PAGE_SIZE - 1) & PAGE_MASK;
	}

	if (*total_size > 0xFFFFFFFFULL)
		return -ENOMEM;

	/* Find largest page shift we can use to cover buffers */
	for (*shift = PAGE_SHIFT; *shift < 27; ++(*shift))
		if ((1ULL << *shift) & mask)
			break;

	buffer_list[0].size += buffer_list[0].addr & ((1ULL << *shift) - 1);
	buffer_list[0].addr &= ~0ull << *shift;

	*npages = 0;
	for (i = 0; i < num_phys_buf; ++i)
		*npages += (buffer_list[i].size +
			(1ULL << *shift) - 1) >> *shift;

	if (!*npages)
		return -EINVAL;

	*page_list = kmalloc(sizeof(u64) * *npages, GFP_KERNEL);
	if (!*page_list)
		return -ENOMEM;

	n = 0;
	for (i = 0; i < num_phys_buf; ++i)
		for (j = 0;
		     j < (buffer_list[i].size + (1ULL << *shift) - 1) >> *shift;
		     ++j)
			(*page_list)[n++] = cpu_to_be64(buffer_list[i].addr +
			    ((u64) j << *shift));

	PDBG("%s va 0x%llx mask 0x%llx shift %d len %lld pbl_size %d\n",
	     __func__, (unsigned long long)*iova_start,
	     (unsigned long long)mask, *shift, (unsigned long long)*total_size,
	     *npages);

	return 0;

}

int c4iw_reregister_phys_mem(struct ib_mr *mr, int mr_rereg_mask,
			     struct ib_pd *pd, struct ib_phys_buf *buffer_list,
			     int num_phys_buf, int acc, u64 *iova_start)
{

	struct c4iw_mr mh, *mhp;
	struct c4iw_pd *php;
	struct c4iw_dev *rhp;
	__be64 *page_list = NULL;
	int shift = 0;
	u64 total_size;
	int npages;
	int ret;

	PDBG("%s ib_mr %p ib_pd %p\n", __func__, mr, pd);

	/* There can be no memory windows */
	if (atomic_read(&mr->usecnt))
		return -EINVAL;

	mhp = to_c4iw_mr(mr);
	rhp = mhp->rhp;
	php = to_c4iw_pd(mr->pd);

	/* make sure we are on the same adapter */
	if (rhp != php->rhp)
		return -EINVAL;

	memcpy(&mh, mhp, sizeof *mhp);

	if (mr_rereg_mask & IB_MR_REREG_PD)
		php = to_c4iw_pd(pd);
	if (mr_rereg_mask & IB_MR_REREG_ACCESS) {
		mh.attr.perms = c4iw_ib_to_tpt_access(acc);
		mh.attr.mw_bind_enable = (acc & IB_ACCESS_MW_BIND) ==
					 IB_ACCESS_MW_BIND;
	}
	if (mr_rereg_mask & IB_MR_REREG_TRANS) {
		ret = build_phys_page_list(buffer_list, num_phys_buf,
						iova_start,
						&total_size, &npages,
						&shift, &page_list);
		if (ret)
			return ret;
	}

	ret = reregister_mem(rhp, php, &mh, shift, npages);
	kfree(page_list);
	if (ret)
		return ret;
	if (mr_rereg_mask & IB_MR_REREG_PD)
		mhp->attr.pdid = php->pdid;
	if (mr_rereg_mask & IB_MR_REREG_ACCESS)
		mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	if (mr_rereg_mask & IB_MR_REREG_TRANS) {
		mhp->attr.zbva = 0;
		mhp->attr.va_fbo = *iova_start;
		mhp->attr.page_size = shift - 12;
		mhp->attr.len = (u32) total_size;
		mhp->attr.pbl_size = npages;
	}

	return 0;
}

struct ib_mr *c4iw_register_phys_mem(struct ib_pd *pd,
				     struct ib_phys_buf *buffer_list,
				     int num_phys_buf, int acc, u64 *iova_start)
{
	__be64 *page_list;
	int shift;
	u64 total_size;
	int npages;
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;
	int ret;

	PDBG("%s ib_pd %p\n", __func__, pd);
	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);

	mhp->rhp = rhp;

	/* First check that we have enough alignment */
	if ((*iova_start & ~PAGE_MASK) != (buffer_list[0].addr & ~PAGE_MASK)) {
		ret = -EINVAL;
		goto err;
	}

	if (num_phys_buf > 1 &&
	    ((buffer_list[0].addr + buffer_list[0].size) & ~PAGE_MASK)) {
		ret = -EINVAL;
		goto err;
	}

	ret = build_phys_page_list(buffer_list, num_phys_buf, iova_start,
					&total_size, &npages, &shift,
					&page_list);
	if (ret)
		goto err;

	ret = alloc_pbl(mhp, npages);
	if (ret) {
		kfree(page_list);
		goto err;
	}

	ret = write_pbl(&mhp->rhp->rdev, page_list, mhp->attr.pbl_addr,
			     npages);
	kfree(page_list);
	if (ret)
		goto err_pbl;

	mhp->attr.pdid = php->pdid;
	mhp->attr.zbva = 0;

	mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	mhp->attr.va_fbo = *iova_start;
	mhp->attr.page_size = shift - 12;

	mhp->attr.len = (u32) total_size;
	mhp->attr.pbl_size = npages;
	ret = register_mem(rhp, php, mhp, shift);
	if (ret)
		goto err_pbl;

	return &mhp->ibmr;

err_pbl:
	c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);

err:
	kfree(mhp);
	return ERR_PTR(ret);

}

struct ib_mr *c4iw_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;
	int ret;
	u32 stag = T4_STAG_UNSET;

	PDBG("%s ib_pd %p\n", __func__, pd);
	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);

	mhp->rhp = rhp;
	mhp->attr.pdid = php->pdid;
	mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	mhp->attr.mw_bind_enable = (acc&IB_ACCESS_MW_BIND) == IB_ACCESS_MW_BIND;
	mhp->attr.zbva = 0;
	mhp->attr.va_fbo = 0;
	mhp->attr.page_size = 0;
	mhp->attr.len = ~0UL;
	mhp->attr.pbl_size = 0;

	ret = write_tpt_entry(&rhp->rdev, 0, &stag, 1, php->pdid,
			      FW_RI_STAG_NSMR, mhp->attr.perms,
			      mhp->attr.mw_bind_enable, 0, 0, ~0UL, 0, 0, 0);
	if (ret)
		goto err1;

	ret = finish_mem_reg(mhp, stag);
	if (ret)
		goto err2;
	return &mhp->ibmr;
err2:
	dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		  mhp->attr.pbl_addr);
err1:
	kfree(mhp);
	return ERR_PTR(ret);
}

struct ib_mr *c4iw_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			       u64 virt, int acc, struct ib_udata *udata)
{
	__be64 *pages;
	int shift, n, len;
	int i, j, k;
	int err = 0;
	struct ib_umem_chunk *chunk;
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;

	PDBG("%s ib_pd %p\n", __func__, pd);

	if (length == ~0ULL)
		return ERR_PTR(-EINVAL);

	if ((length + start) < start)
		return ERR_PTR(-EINVAL);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;
	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);

	mhp->rhp = rhp;

	mhp->umem = ib_umem_get(pd->uobject->context, start, length, acc, 0);
	if (IS_ERR(mhp->umem)) {
		err = PTR_ERR(mhp->umem);
		kfree(mhp);
		return ERR_PTR(err);
	}

	shift = ffs(mhp->umem->page_size) - 1;

	n = 0;
	list_for_each_entry(chunk, &mhp->umem->chunk_list, list)
		n += chunk->nents;

	err = alloc_pbl(mhp, n);
	if (err)
		goto err;

	pages = (__be64 *) __get_free_page(GFP_KERNEL);
	if (!pages) {
		err = -ENOMEM;
		goto err_pbl;
	}

	i = n = 0;

	list_for_each_entry(chunk, &mhp->umem->chunk_list, list)
		for (j = 0; j < chunk->nmap; ++j) {
			len = sg_dma_len(&chunk->page_list[j]) >> shift;
			for (k = 0; k < len; ++k) {
				pages[i++] = cpu_to_be64(sg_dma_address(
					&chunk->page_list[j]) +
					mhp->umem->page_size * k);
				if (i == PAGE_SIZE / sizeof *pages) {
					err = write_pbl(&mhp->rhp->rdev,
					      pages,
					      mhp->attr.pbl_addr + (n << 3), i);
					if (err)
						goto pbl_done;
					n += i;
					i = 0;
				}
			}
		}

	if (i)
		err = write_pbl(&mhp->rhp->rdev, pages,
				     mhp->attr.pbl_addr + (n << 3), i);

pbl_done:
	free_page((unsigned long) pages);
	if (err)
		goto err_pbl;

	mhp->attr.pdid = php->pdid;
	mhp->attr.zbva = 0;
	mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	mhp->attr.va_fbo = virt;
	mhp->attr.page_size = shift - 12;
	mhp->attr.len = length;

	err = register_mem(rhp, php, mhp, shift);
	if (err)
		goto err_pbl;

	return &mhp->ibmr;

err_pbl:
	c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);

err:
	ib_umem_release(mhp->umem);
	kfree(mhp);
	return ERR_PTR(err);
}

struct ib_mw *c4iw_alloc_mw(struct ib_pd *pd, enum ib_mw_type type)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mw *mhp;
	u32 mmid;
	u32 stag = 0;
	int ret;

	if (type != IB_MW_TYPE_1)
		return ERR_PTR(-EINVAL);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;
	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);
	ret = allocate_window(&rhp->rdev, &stag, php->pdid);
	if (ret) {
		kfree(mhp);
		return ERR_PTR(ret);
	}
	mhp->rhp = rhp;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = FW_RI_STAG_MW;
	mhp->attr.stag = stag;
	mmid = (stag) >> 8;
	mhp->ibmw.rkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid)) {
		deallocate_window(&rhp->rdev, mhp->attr.stag);
		kfree(mhp);
		return ERR_PTR(-ENOMEM);
	}
	PDBG("%s mmid 0x%x mhp %p stag 0x%x\n", __func__, mmid, mhp, stag);
	return &(mhp->ibmw);
}

int c4iw_dealloc_mw(struct ib_mw *mw)
{
	struct c4iw_dev *rhp;
	struct c4iw_mw *mhp;
	u32 mmid;

	mhp = to_c4iw_mw(mw);
	rhp = mhp->rhp;
	mmid = (mw->rkey) >> 8;
	remove_handle(rhp, &rhp->mmidr, mmid);
	deallocate_window(&rhp->rdev, mhp->attr.stag);
	kfree(mhp);
	PDBG("%s ib_mw %p mmid 0x%x ptr %p\n", __func__, mw, mmid, mhp);
	return 0;
}

struct ib_mr *c4iw_alloc_fast_reg_mr(struct ib_pd *pd, int pbl_depth)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;
	u32 mmid;
	u32 stag = 0;
	int ret = 0;

	php = to_c4iw_pd(pd);
	rhp = php->rhp;
	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp) {
		ret = -ENOMEM;
		goto err;
	}

	mhp->rhp = rhp;
	ret = alloc_pbl(mhp, pbl_depth);
	if (ret)
		goto err1;
	mhp->attr.pbl_size = pbl_depth;
	ret = allocate_stag(&rhp->rdev, &stag, php->pdid,
				 mhp->attr.pbl_size, mhp->attr.pbl_addr);
	if (ret)
		goto err2;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = FW_RI_STAG_NSMR;
	mhp->attr.stag = stag;
	mhp->attr.state = 1;
	mmid = (stag) >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid)) {
		ret = -ENOMEM;
		goto err3;
	}

	PDBG("%s mmid 0x%x mhp %p stag 0x%x\n", __func__, mmid, mhp, stag);
	return &(mhp->ibmr);
err3:
	dereg_mem(&rhp->rdev, stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
err2:
	c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);
err1:
	kfree(mhp);
err:
	return ERR_PTR(ret);
}

struct ib_fast_reg_page_list *c4iw_alloc_fastreg_pbl(struct ib_device *device,
						     int page_list_len)
{
	struct c4iw_fr_page_list *c4pl;
	struct c4iw_dev *dev = to_c4iw_dev(device);
	dma_addr_t dma_addr;
	int size = sizeof *c4pl + page_list_len * sizeof(u64);

	c4pl = dma_alloc_coherent(&dev->rdev.lldi.pdev->dev, size,
				  &dma_addr, GFP_KERNEL);
	if (!c4pl)
		return ERR_PTR(-ENOMEM);

	dma_unmap_addr_set(c4pl, mapping, dma_addr);
	c4pl->dma_addr = dma_addr;
	c4pl->dev = dev;
	c4pl->size = size;
	c4pl->ibpl.page_list = (u64 *)(c4pl + 1);
	c4pl->ibpl.max_page_list_len = page_list_len;

	return &c4pl->ibpl;
}

void c4iw_free_fastreg_pbl(struct ib_fast_reg_page_list *ibpl)
{
	struct c4iw_fr_page_list *c4pl = to_c4iw_fr_page_list(ibpl);

	dma_free_coherent(&c4pl->dev->rdev.lldi.pdev->dev, c4pl->size,
			  c4pl, dma_unmap_addr(c4pl, mapping));
}

int c4iw_dereg_mr(struct ib_mr *ib_mr)
{
	struct c4iw_dev *rhp;
	struct c4iw_mr *mhp;
	u32 mmid;

	PDBG("%s ib_mr %p\n", __func__, ib_mr);
	/* There can be no memory windows */
	if (atomic_read(&ib_mr->usecnt))
		return -EINVAL;

	mhp = to_c4iw_mr(ib_mr);
	rhp = mhp->rhp;
	mmid = mhp->attr.stag >> 8;
	remove_handle(rhp, &rhp->mmidr, mmid);
	dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
	if (mhp->attr.pbl_size)
		c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
				  mhp->attr.pbl_size << 3);
	if (mhp->kva)
		kfree((void *) (unsigned long) mhp->kva);
	if (mhp->umem)
		ib_umem_release(mhp->umem);
	PDBG("%s mmid 0x%x ptr %p\n", __func__, mmid, mhp);
	kfree(mhp);
	return 0;
}
