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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <rdma/ib_umem.h>
#include <linux/atomic.h>
#include <rdma/ib_user_verbs.h>

#include "iw_cxgb4.h"

int use_dsgl = 1;
module_param(use_dsgl, int, 0644);
MODULE_PARM_DESC(use_dsgl, "Use DSGL for PBL/FastReg (default=1) (DEPRECATED)");

#define T4_ULPTX_MIN_IO 32
#define C4IW_MAX_INLINE_SIZE 96
#define T4_ULPTX_MAX_DMA 1024
#define C4IW_INLINE_THRESHOLD 128

static int inline_threshold = C4IW_INLINE_THRESHOLD;
module_param(inline_threshold, int, 0644);
MODULE_PARM_DESC(inline_threshold, "inline vs dsgl threshold (default=128)");

static int mr_exceeds_hw_limits(struct c4iw_dev *dev, u64 length)
{
	return (is_t4(dev->rdev.lldi.adapter_type) ||
		is_t5(dev->rdev.lldi.adapter_type)) &&
		length >= 8*1024*1024*1024ULL;
}

static int _c4iw_write_mem_dma_aligned(struct c4iw_rdev *rdev, u32 addr,
				       u32 len, dma_addr_t data,
				       struct sk_buff *skb,
				       struct c4iw_wr_wait *wr_waitp)
{
	struct ulp_mem_io *req;
	struct ulptx_sgl *sgl;
	u8 wr_len;
	int ret = 0;

	addr &= 0x7FFFFFF;

	if (wr_waitp)
		c4iw_init_wr_wait(wr_waitp);
	wr_len = roundup(sizeof(*req) + sizeof(*sgl), 16);

	if (!skb) {
		skb = alloc_skb(wr_len, GFP_KERNEL | __GFP_NOFAIL);
		if (!skb)
			return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

	req = __skb_put_zero(skb, wr_len);
	INIT_ULPTX_WR(req, wr_len, 0, 0);
	req->wr.wr_hi = cpu_to_be32(FW_WR_OP_V(FW_ULPTX_WR) |
			(wr_waitp ? FW_WR_COMPL_F : 0));
	req->wr.wr_lo = wr_waitp ? (__force __be64)(unsigned long)wr_waitp : 0L;
	req->wr.wr_mid = cpu_to_be32(FW_WR_LEN16_V(DIV_ROUND_UP(wr_len, 16)));
	req->cmd = cpu_to_be32(ULPTX_CMD_V(ULP_TX_MEM_WRITE) |
			       T5_ULP_MEMIO_ORDER_V(1) |
			       T5_ULP_MEMIO_FID_V(rdev->lldi.rxq_ids[0]));
	req->dlen = cpu_to_be32(ULP_MEMIO_DATA_LEN_V(len>>5));
	req->len16 = cpu_to_be32(DIV_ROUND_UP(wr_len-sizeof(req->wr), 16));
	req->lock_addr = cpu_to_be32(ULP_MEMIO_ADDR_V(addr));

	sgl = (struct ulptx_sgl *)(req + 1);
	sgl->cmd_nsge = cpu_to_be32(ULPTX_CMD_V(ULP_TX_SC_DSGL) |
				    ULPTX_NSGE_V(1));
	sgl->len0 = cpu_to_be32(len);
	sgl->addr0 = cpu_to_be64(data);

	if (wr_waitp)
		ret = c4iw_ref_send_wait(rdev, skb, wr_waitp, 0, 0, __func__);
	else
		ret = c4iw_ofld_send(rdev, skb);
	return ret;
}

static int _c4iw_write_mem_inline(struct c4iw_rdev *rdev, u32 addr, u32 len,
				  void *data, struct sk_buff *skb,
				  struct c4iw_wr_wait *wr_waitp)
{
	struct ulp_mem_io *req;
	struct ulptx_idata *sc;
	u8 wr_len, *to_dp, *from_dp;
	int copy_len, num_wqe, i, ret = 0;
	__be32 cmd = cpu_to_be32(ULPTX_CMD_V(ULP_TX_MEM_WRITE));

	if (is_t4(rdev->lldi.adapter_type))
		cmd |= cpu_to_be32(ULP_MEMIO_ORDER_F);
	else
		cmd |= cpu_to_be32(T5_ULP_MEMIO_IMM_F);

	addr &= 0x7FFFFFF;
	pr_debug("addr 0x%x len %u\n", addr, len);
	num_wqe = DIV_ROUND_UP(len, C4IW_MAX_INLINE_SIZE);
	c4iw_init_wr_wait(wr_waitp);
	for (i = 0; i < num_wqe; i++) {

		copy_len = len > C4IW_MAX_INLINE_SIZE ? C4IW_MAX_INLINE_SIZE :
			   len;
		wr_len = roundup(sizeof *req + sizeof *sc +
				 roundup(copy_len, T4_ULPTX_MIN_IO), 16);

		if (!skb) {
			skb = alloc_skb(wr_len, GFP_KERNEL | __GFP_NOFAIL);
			if (!skb)
				return -ENOMEM;
		}
		set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

		req = __skb_put_zero(skb, wr_len);
		INIT_ULPTX_WR(req, wr_len, 0, 0);

		if (i == (num_wqe-1)) {
			req->wr.wr_hi = cpu_to_be32(FW_WR_OP_V(FW_ULPTX_WR) |
						    FW_WR_COMPL_F);
			req->wr.wr_lo = (__force __be64)(unsigned long)wr_waitp;
		} else
			req->wr.wr_hi = cpu_to_be32(FW_WR_OP_V(FW_ULPTX_WR));
		req->wr.wr_mid = cpu_to_be32(
				       FW_WR_LEN16_V(DIV_ROUND_UP(wr_len, 16)));

		req->cmd = cmd;
		req->dlen = cpu_to_be32(ULP_MEMIO_DATA_LEN_V(
				DIV_ROUND_UP(copy_len, T4_ULPTX_MIN_IO)));
		req->len16 = cpu_to_be32(DIV_ROUND_UP(wr_len-sizeof(req->wr),
						      16));
		req->lock_addr = cpu_to_be32(ULP_MEMIO_ADDR_V(addr + i * 3));

		sc = (struct ulptx_idata *)(req + 1);
		sc->cmd_more = cpu_to_be32(ULPTX_CMD_V(ULP_TX_SC_IMM));
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
		if (i == (num_wqe-1))
			ret = c4iw_ref_send_wait(rdev, skb, wr_waitp, 0, 0,
						 __func__);
		else
			ret = c4iw_ofld_send(rdev, skb);
		if (ret)
			break;
		skb = NULL;
		len -= C4IW_MAX_INLINE_SIZE;
	}

	return ret;
}

static int _c4iw_write_mem_dma(struct c4iw_rdev *rdev, u32 addr, u32 len,
			       void *data, struct sk_buff *skb,
			       struct c4iw_wr_wait *wr_waitp)
{
	u32 remain = len;
	u32 dmalen;
	int ret = 0;
	dma_addr_t daddr;
	dma_addr_t save;

	daddr = dma_map_single(&rdev->lldi.pdev->dev, data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(&rdev->lldi.pdev->dev, daddr))
		return -1;
	save = daddr;

	while (remain > inline_threshold) {
		if (remain < T4_ULPTX_MAX_DMA) {
			if (remain & ~T4_ULPTX_MIN_IO)
				dmalen = remain & ~(T4_ULPTX_MIN_IO-1);
			else
				dmalen = remain;
		} else
			dmalen = T4_ULPTX_MAX_DMA;
		remain -= dmalen;
		ret = _c4iw_write_mem_dma_aligned(rdev, addr, dmalen, daddr,
						 skb, remain ? NULL : wr_waitp);
		if (ret)
			goto out;
		addr += dmalen >> 5;
		data += dmalen;
		daddr += dmalen;
	}
	if (remain)
		ret = _c4iw_write_mem_inline(rdev, addr, remain, data, skb,
					     wr_waitp);
out:
	dma_unmap_single(&rdev->lldi.pdev->dev, save, len, DMA_TO_DEVICE);
	return ret;
}

/*
 * write len bytes of data into addr (32B aligned address)
 * If data is NULL, clear len byte of memory to zero.
 */
static int write_adapter_mem(struct c4iw_rdev *rdev, u32 addr, u32 len,
			     void *data, struct sk_buff *skb,
			     struct c4iw_wr_wait *wr_waitp)
{
	int ret;

	if (!rdev->lldi.ulptx_memwrite_dsgl || !use_dsgl) {
		ret = _c4iw_write_mem_inline(rdev, addr, len, data, skb,
					      wr_waitp);
		goto out;
	}

	if (len <= inline_threshold) {
		ret = _c4iw_write_mem_inline(rdev, addr, len, data, skb,
					      wr_waitp);
		goto out;
	}

	ret = _c4iw_write_mem_dma(rdev, addr, len, data, skb, wr_waitp);
	if (ret) {
		pr_warn_ratelimited("%s: dma map failure (non fatal)\n",
				    pci_name(rdev->lldi.pdev));
		ret = _c4iw_write_mem_inline(rdev, addr, len, data, skb,
					      wr_waitp);
	}
out:
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
			   u64 len, u8 page_size, u32 pbl_size, u32 pbl_addr,
			   struct sk_buff *skb, struct c4iw_wr_wait *wr_waitp)
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
		if (!stag_idx) {
			mutex_lock(&rdev->stats.lock);
			rdev->stats.stag.fail++;
			mutex_unlock(&rdev->stats.lock);
			return -ENOMEM;
		}
		mutex_lock(&rdev->stats.lock);
		rdev->stats.stag.cur += 32;
		if (rdev->stats.stag.cur > rdev->stats.stag.max)
			rdev->stats.stag.max = rdev->stats.stag.cur;
		mutex_unlock(&rdev->stats.lock);
		*stag = (stag_idx << 8) | (atomic_inc_return(&key) & 0xff);
	}
	pr_debug("stag_state 0x%0x type 0x%0x pdid 0x%0x, stag_idx 0x%x\n",
		 stag_state, type, pdid, stag_idx);

	/* write TPT entry */
	if (reset_tpt_entry)
		memset(&tpt, 0, sizeof(tpt));
	else {
		tpt.valid_to_pdid = cpu_to_be32(FW_RI_TPTE_VALID_F |
			FW_RI_TPTE_STAGKEY_V((*stag & FW_RI_TPTE_STAGKEY_M)) |
			FW_RI_TPTE_STAGSTATE_V(stag_state) |
			FW_RI_TPTE_STAGTYPE_V(type) | FW_RI_TPTE_PDID_V(pdid));
		tpt.locread_to_qpid = cpu_to_be32(FW_RI_TPTE_PERM_V(perm) |
			(bind_enabled ? FW_RI_TPTE_MWBINDEN_F : 0) |
			FW_RI_TPTE_ADDRTYPE_V((zbva ? FW_RI_ZERO_BASED_TO :
						      FW_RI_VA_BASED_TO))|
			FW_RI_TPTE_PS_V(page_size));
		tpt.nosnoop_pbladdr = !pbl_size ? 0 : cpu_to_be32(
			FW_RI_TPTE_PBLADDR_V(PBL_OFF(rdev, pbl_addr)>>3));
		tpt.len_lo = cpu_to_be32((u32)(len & 0xffffffffUL));
		tpt.va_hi = cpu_to_be32((u32)(to >> 32));
		tpt.va_lo_fbo = cpu_to_be32((u32)(to & 0xffffffffUL));
		tpt.dca_mwbcnt_pstag = cpu_to_be32(0);
		tpt.len_hi = cpu_to_be32((u32)(len >> 32));
	}
	err = write_adapter_mem(rdev, stag_idx +
				(rdev->lldi.vr->stag.start >> 5),
				sizeof(tpt), &tpt, skb, wr_waitp);

	if (reset_tpt_entry) {
		c4iw_put_resource(&rdev->resource.tpt_table, stag_idx);
		mutex_lock(&rdev->stats.lock);
		rdev->stats.stag.cur -= 32;
		mutex_unlock(&rdev->stats.lock);
	}
	return err;
}

static int write_pbl(struct c4iw_rdev *rdev, __be64 *pbl,
		     u32 pbl_addr, u32 pbl_size, struct c4iw_wr_wait *wr_waitp)
{
	int err;

	pr_debug("*pdb_addr 0x%x, pbl_base 0x%x, pbl_size %d\n",
		 pbl_addr, rdev->lldi.vr->pbl.start,
		 pbl_size);

	err = write_adapter_mem(rdev, pbl_addr >> 5, pbl_size << 3, pbl, NULL,
				wr_waitp);
	return err;
}

static int dereg_mem(struct c4iw_rdev *rdev, u32 stag, u32 pbl_size,
		     u32 pbl_addr, struct sk_buff *skb,
		     struct c4iw_wr_wait *wr_waitp)
{
	return write_tpt_entry(rdev, 1, &stag, 0, 0, 0, 0, 0, 0, 0UL, 0, 0,
			       pbl_size, pbl_addr, skb, wr_waitp);
}

static int allocate_window(struct c4iw_rdev *rdev, u32 *stag, u32 pdid,
			   struct c4iw_wr_wait *wr_waitp)
{
	*stag = T4_STAG_UNSET;
	return write_tpt_entry(rdev, 0, stag, 0, pdid, FW_RI_STAG_MW, 0, 0, 0,
			       0UL, 0, 0, 0, 0, NULL, wr_waitp);
}

static int deallocate_window(struct c4iw_rdev *rdev, u32 stag,
			     struct sk_buff *skb,
			     struct c4iw_wr_wait *wr_waitp)
{
	return write_tpt_entry(rdev, 1, &stag, 0, 0, 0, 0, 0, 0, 0UL, 0, 0, 0,
			       0, skb, wr_waitp);
}

static int allocate_stag(struct c4iw_rdev *rdev, u32 *stag, u32 pdid,
			 u32 pbl_size, u32 pbl_addr,
			 struct c4iw_wr_wait *wr_waitp)
{
	*stag = T4_STAG_UNSET;
	return write_tpt_entry(rdev, 0, stag, 0, pdid, FW_RI_STAG_NSMR, 0, 0, 0,
			       0UL, 0, 0, pbl_size, pbl_addr, NULL, wr_waitp);
}

static int finish_mem_reg(struct c4iw_mr *mhp, u32 stag)
{
	u32 mmid;

	mhp->attr.state = 1;
	mhp->attr.stag = stag;
	mmid = stag >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	mhp->ibmr.length = mhp->attr.len;
	mhp->ibmr.iova = mhp->attr.va_fbo;
	mhp->ibmr.page_size = 1U << (mhp->attr.page_size + 12);
	pr_debug("mmid 0x%x mhp %p\n", mmid, mhp);
	return insert_handle(mhp->rhp, &mhp->rhp->mmidr, mhp, mmid);
}

static int register_mem(struct c4iw_dev *rhp, struct c4iw_pd *php,
		      struct c4iw_mr *mhp, int shift)
{
	u32 stag = T4_STAG_UNSET;
	int ret;

	ret = write_tpt_entry(&rhp->rdev, 0, &stag, 1, mhp->attr.pdid,
			      FW_RI_STAG_NSMR, mhp->attr.len ?
			      mhp->attr.perms : 0,
			      mhp->attr.mw_bind_enable, mhp->attr.zbva,
			      mhp->attr.va_fbo, mhp->attr.len ?
			      mhp->attr.len : -1, shift - 12,
			      mhp->attr.pbl_size, mhp->attr.pbl_addr, NULL,
			      mhp->wr_waitp);
	if (ret)
		return ret;

	ret = finish_mem_reg(mhp, stag);
	if (ret) {
		dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
			  mhp->attr.pbl_addr, mhp->dereg_skb, mhp->wr_waitp);
		mhp->dereg_skb = NULL;
	}
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

struct ib_mr *c4iw_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;
	int ret;
	u32 stag = T4_STAG_UNSET;

	pr_debug("ib_pd %p\n", pd);
	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);
	mhp->wr_waitp = c4iw_alloc_wr_wait(GFP_KERNEL);
	if (!mhp->wr_waitp) {
		ret = -ENOMEM;
		goto err_free_mhp;
	}
	c4iw_init_wr_wait(mhp->wr_waitp);

	mhp->dereg_skb = alloc_skb(SGE_MAX_WR_LEN, GFP_KERNEL);
	if (!mhp->dereg_skb) {
		ret = -ENOMEM;
		goto err_free_wr_wait;
	}

	mhp->rhp = rhp;
	mhp->attr.pdid = php->pdid;
	mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	mhp->attr.mw_bind_enable = (acc&IB_ACCESS_MW_BIND) == IB_ACCESS_MW_BIND;
	mhp->attr.zbva = 0;
	mhp->attr.va_fbo = 0;
	mhp->attr.page_size = 0;
	mhp->attr.len = ~0ULL;
	mhp->attr.pbl_size = 0;

	ret = write_tpt_entry(&rhp->rdev, 0, &stag, 1, php->pdid,
			      FW_RI_STAG_NSMR, mhp->attr.perms,
			      mhp->attr.mw_bind_enable, 0, 0, ~0ULL, 0, 0, 0,
			      NULL, mhp->wr_waitp);
	if (ret)
		goto err_free_skb;

	ret = finish_mem_reg(mhp, stag);
	if (ret)
		goto err_dereg_mem;
	return &mhp->ibmr;
err_dereg_mem:
	dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		  mhp->attr.pbl_addr, mhp->dereg_skb, mhp->wr_waitp);
err_free_skb:
	kfree_skb(mhp->dereg_skb);
err_free_wr_wait:
	c4iw_put_wr_wait(mhp->wr_waitp);
err_free_mhp:
	kfree(mhp);
	return ERR_PTR(ret);
}

struct ib_mr *c4iw_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			       u64 virt, int acc, struct ib_udata *udata)
{
	__be64 *pages;
	int shift, n, i;
	int err = -ENOMEM;
	struct sg_dma_page_iter sg_iter;
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;

	pr_debug("ib_pd %p\n", pd);

	if (length == ~0ULL)
		return ERR_PTR(-EINVAL);

	if ((length + start) < start)
		return ERR_PTR(-EINVAL);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	if (mr_exceeds_hw_limits(rhp, length))
		return ERR_PTR(-EINVAL);

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);
	mhp->wr_waitp = c4iw_alloc_wr_wait(GFP_KERNEL);
	if (!mhp->wr_waitp)
		goto err_free_mhp;

	mhp->dereg_skb = alloc_skb(SGE_MAX_WR_LEN, GFP_KERNEL);
	if (!mhp->dereg_skb)
		goto err_free_wr_wait;

	mhp->rhp = rhp;

	mhp->umem = ib_umem_get(udata, start, length, acc, 0);
	if (IS_ERR(mhp->umem))
		goto err_free_skb;

	shift = PAGE_SHIFT;

	n = mhp->umem->nmap;
	err = alloc_pbl(mhp, n);
	if (err)
		goto err_umem_release;

	pages = (__be64 *) __get_free_page(GFP_KERNEL);
	if (!pages) {
		err = -ENOMEM;
		goto err_pbl_free;
	}

	i = n = 0;

	for_each_sg_dma_page(mhp->umem->sg_head.sgl, &sg_iter, mhp->umem->nmap, 0) {
		pages[i++] = cpu_to_be64(sg_page_iter_dma_address(&sg_iter));
		if (i == PAGE_SIZE / sizeof(*pages)) {
			err = write_pbl(&mhp->rhp->rdev, pages,
					mhp->attr.pbl_addr + (n << 3), i,
					mhp->wr_waitp);
			if (err)
				goto pbl_done;
			n += i;
			i = 0;
		}
	}

	if (i)
		err = write_pbl(&mhp->rhp->rdev, pages,
				mhp->attr.pbl_addr + (n << 3), i,
				mhp->wr_waitp);

pbl_done:
	free_page((unsigned long) pages);
	if (err)
		goto err_pbl_free;

	mhp->attr.pdid = php->pdid;
	mhp->attr.zbva = 0;
	mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	mhp->attr.va_fbo = virt;
	mhp->attr.page_size = shift - 12;
	mhp->attr.len = length;

	err = register_mem(rhp, php, mhp, shift);
	if (err)
		goto err_pbl_free;

	return &mhp->ibmr;

err_pbl_free:
	c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);
err_umem_release:
	ib_umem_release(mhp->umem);
err_free_skb:
	kfree_skb(mhp->dereg_skb);
err_free_wr_wait:
	c4iw_put_wr_wait(mhp->wr_waitp);
err_free_mhp:
	kfree(mhp);
	return ERR_PTR(err);
}

struct ib_mw *c4iw_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
			    struct ib_udata *udata)
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

	mhp->wr_waitp = c4iw_alloc_wr_wait(GFP_KERNEL);
	if (!mhp->wr_waitp) {
		ret = -ENOMEM;
		goto free_mhp;
	}

	mhp->dereg_skb = alloc_skb(SGE_MAX_WR_LEN, GFP_KERNEL);
	if (!mhp->dereg_skb) {
		ret = -ENOMEM;
		goto free_wr_wait;
	}

	ret = allocate_window(&rhp->rdev, &stag, php->pdid, mhp->wr_waitp);
	if (ret)
		goto free_skb;
	mhp->rhp = rhp;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = FW_RI_STAG_MW;
	mhp->attr.stag = stag;
	mmid = (stag) >> 8;
	mhp->ibmw.rkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid)) {
		ret = -ENOMEM;
		goto dealloc_win;
	}
	pr_debug("mmid 0x%x mhp %p stag 0x%x\n", mmid, mhp, stag);
	return &(mhp->ibmw);

dealloc_win:
	deallocate_window(&rhp->rdev, mhp->attr.stag, mhp->dereg_skb,
			  mhp->wr_waitp);
free_skb:
	kfree_skb(mhp->dereg_skb);
free_wr_wait:
	c4iw_put_wr_wait(mhp->wr_waitp);
free_mhp:
	kfree(mhp);
	return ERR_PTR(ret);
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
	deallocate_window(&rhp->rdev, mhp->attr.stag, mhp->dereg_skb,
			  mhp->wr_waitp);
	kfree_skb(mhp->dereg_skb);
	c4iw_put_wr_wait(mhp->wr_waitp);
	pr_debug("ib_mw %p mmid 0x%x ptr %p\n", mw, mmid, mhp);
	kfree(mhp);
	return 0;
}

struct ib_mr *c4iw_alloc_mr(struct ib_pd *pd,
			    enum ib_mr_type mr_type,
			    u32 max_num_sg)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;
	u32 mmid;
	u32 stag = 0;
	int ret = 0;
	int length = roundup(max_num_sg * sizeof(u64), 32);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	if (mr_type != IB_MR_TYPE_MEM_REG ||
	    max_num_sg > t4_max_fr_depth(rhp->rdev.lldi.ulptx_memwrite_dsgl &&
					 use_dsgl))
		return ERR_PTR(-EINVAL);

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp) {
		ret = -ENOMEM;
		goto err;
	}

	mhp->wr_waitp = c4iw_alloc_wr_wait(GFP_KERNEL);
	if (!mhp->wr_waitp) {
		ret = -ENOMEM;
		goto err_free_mhp;
	}
	c4iw_init_wr_wait(mhp->wr_waitp);

	mhp->mpl = dma_alloc_coherent(&rhp->rdev.lldi.pdev->dev,
				      length, &mhp->mpl_addr, GFP_KERNEL);
	if (!mhp->mpl) {
		ret = -ENOMEM;
		goto err_free_wr_wait;
	}
	mhp->max_mpl_len = length;

	mhp->rhp = rhp;
	ret = alloc_pbl(mhp, max_num_sg);
	if (ret)
		goto err_free_dma;
	mhp->attr.pbl_size = max_num_sg;
	ret = allocate_stag(&rhp->rdev, &stag, php->pdid,
			    mhp->attr.pbl_size, mhp->attr.pbl_addr,
			    mhp->wr_waitp);
	if (ret)
		goto err_free_pbl;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = FW_RI_STAG_NSMR;
	mhp->attr.stag = stag;
	mhp->attr.state = 0;
	mmid = (stag) >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid)) {
		ret = -ENOMEM;
		goto err_dereg;
	}

	pr_debug("mmid 0x%x mhp %p stag 0x%x\n", mmid, mhp, stag);
	return &(mhp->ibmr);
err_dereg:
	dereg_mem(&rhp->rdev, stag, mhp->attr.pbl_size,
		  mhp->attr.pbl_addr, mhp->dereg_skb, mhp->wr_waitp);
err_free_pbl:
	c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);
err_free_dma:
	dma_free_coherent(&mhp->rhp->rdev.lldi.pdev->dev,
			  mhp->max_mpl_len, mhp->mpl, mhp->mpl_addr);
err_free_wr_wait:
	c4iw_put_wr_wait(mhp->wr_waitp);
err_free_mhp:
	kfree(mhp);
err:
	return ERR_PTR(ret);
}

static int c4iw_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct c4iw_mr *mhp = to_c4iw_mr(ibmr);

	if (unlikely(mhp->mpl_len == mhp->attr.pbl_size))
		return -ENOMEM;

	mhp->mpl[mhp->mpl_len++] = addr;

	return 0;
}

int c4iw_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		   unsigned int *sg_offset)
{
	struct c4iw_mr *mhp = to_c4iw_mr(ibmr);

	mhp->mpl_len = 0;

	return ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, c4iw_set_page);
}

int c4iw_dereg_mr(struct ib_mr *ib_mr)
{
	struct c4iw_dev *rhp;
	struct c4iw_mr *mhp;
	u32 mmid;

	pr_debug("ib_mr %p\n", ib_mr);

	mhp = to_c4iw_mr(ib_mr);
	rhp = mhp->rhp;
	mmid = mhp->attr.stag >> 8;
	remove_handle(rhp, &rhp->mmidr, mmid);
	if (mhp->mpl)
		dma_free_coherent(&mhp->rhp->rdev.lldi.pdev->dev,
				  mhp->max_mpl_len, mhp->mpl, mhp->mpl_addr);
	dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		  mhp->attr.pbl_addr, mhp->dereg_skb, mhp->wr_waitp);
	if (mhp->attr.pbl_size)
		c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
				  mhp->attr.pbl_size << 3);
	if (mhp->kva)
		kfree((void *) (unsigned long) mhp->kva);
	if (mhp->umem)
		ib_umem_release(mhp->umem);
	pr_debug("mmid 0x%x ptr %p\n", mmid, mhp);
	c4iw_put_wr_wait(mhp->wr_waitp);
	kfree(mhp);
	return 0;
}

void c4iw_invalidate_mr(struct c4iw_dev *rhp, u32 rkey)
{
	struct c4iw_mr *mhp;
	unsigned long flags;

	spin_lock_irqsave(&rhp->lock, flags);
	mhp = get_mhp(rhp, rkey >> 8);
	if (mhp)
		mhp->attr.state = 0;
	spin_unlock_irqrestore(&rhp->lock, flags);
}
