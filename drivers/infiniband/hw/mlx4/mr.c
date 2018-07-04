/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/slab.h>
#include <rdma/ib_user_verbs.h>

#include "mlx4_ib.h"

static u32 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? MLX4_PERM_ATOMIC       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ? MLX4_PERM_REMOTE_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ? MLX4_PERM_REMOTE_READ  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? MLX4_PERM_LOCAL_WRITE  : 0) |
	       (acc & IB_ACCESS_MW_BIND	      ? MLX4_PERM_BIND_MW      : 0) |
	       MLX4_PERM_LOCAL_READ;
}

static enum mlx4_mw_type to_mlx4_type(enum ib_mw_type type)
{
	switch (type) {
	case IB_MW_TYPE_1:	return MLX4_MW_TYPE_1;
	case IB_MW_TYPE_2:	return MLX4_MW_TYPE_2;
	default:		return -1;
	}
}

struct ib_mr *mlx4_ib_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct mlx4_ib_mr *mr;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	err = mlx4_mr_alloc(to_mdev(pd->device)->dev, to_mpd(pd)->pdn, 0,
			    ~0ull, convert_access(acc), 0, 0, &mr->mmr);
	if (err)
		goto err_free;

	err = mlx4_mr_enable(to_mdev(pd->device)->dev, &mr->mmr);
	if (err)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_mr:
	(void) mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

int mlx4_ib_umem_write_mtt(struct mlx4_ib_dev *dev, struct mlx4_mtt *mtt,
			   struct ib_umem *umem)
{
	u64 *pages;
	int i, k, entry;
	int n;
	int len;
	int err = 0;
	struct scatterlist *sg;

	pages = (u64 *) __get_free_page(GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	i = n = 0;

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = sg_dma_len(sg) >> mtt->page_shift;
		for (k = 0; k < len; ++k) {
			pages[i++] = sg_dma_address(sg) +
				(k << umem->page_shift);
			/*
			 * Be friendly to mlx4_write_mtt() and
			 * pass it chunks of appropriate size.
			 */
			if (i == PAGE_SIZE / sizeof (u64)) {
				err = mlx4_write_mtt(dev->dev, mtt, n,
						     i, pages);
				if (err)
					goto out;
				n += i;
				i = 0;
			}
		}
	}

	if (i)
		err = mlx4_write_mtt(dev->dev, mtt, n, i, pages);

out:
	free_page((unsigned long) pages);
	return err;
}

static struct ib_umem *mlx4_get_umem_mr(struct ib_ucontext *context, u64 start,
					u64 length, u64 virt_addr,
					int access_flags)
{
	/*
	 * Force registering the memory as writable if the underlying pages
	 * are writable.  This is so rereg can change the access permissions
	 * from readable to writable without having to run through ib_umem_get
	 * again
	 */
	if (!ib_access_writable(access_flags)) {
		struct vm_area_struct *vma;

		down_read(&current->mm->mmap_sem);
		/*
		 * FIXME: Ideally this would iterate over all the vmas that
		 * cover the memory, but for now it requires a single vma to
		 * entirely cover the MR to support RO mappings.
		 */
		vma = find_vma(current->mm, start);
		if (vma && vma->vm_end >= start + length &&
		    vma->vm_start <= start) {
			if (vma->vm_flags & VM_WRITE)
				access_flags |= IB_ACCESS_LOCAL_WRITE;
		} else {
			access_flags |= IB_ACCESS_LOCAL_WRITE;
		}

		up_read(&current->mm->mmap_sem);
	}

	return ib_umem_get(context, start, length, access_flags, 0);
}

struct ib_mr *mlx4_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mr *mr;
	int shift;
	int err;
	int n;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = mlx4_get_umem_mr(pd->uobject->context, start, length,
				    virt_addr, access_flags);
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		goto err_free;
	}

	n = ib_umem_page_count(mr->umem);
	shift = mr->umem->page_shift;

	err = mlx4_mr_alloc(dev->dev, to_mpd(pd)->pdn, virt_addr, length,
			    convert_access(access_flags), n, shift, &mr->mmr);
	if (err)
		goto err_umem;

	err = mlx4_ib_umem_write_mtt(dev, &mr->mmr.mtt, mr->umem);
	if (err)
		goto err_mr;

	err = mlx4_mr_enable(dev->dev, &mr->mmr);
	if (err)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;

	return &mr->ibmr;

err_mr:
	(void) mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_umem:
	ib_umem_release(mr->umem);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

int mlx4_ib_rereg_user_mr(struct ib_mr *mr, int flags,
			  u64 start, u64 length, u64 virt_addr,
			  int mr_access_flags, struct ib_pd *pd,
			  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(mr->device);
	struct mlx4_ib_mr *mmr = to_mmr(mr);
	struct mlx4_mpt_entry *mpt_entry;
	struct mlx4_mpt_entry **pmpt_entry = &mpt_entry;
	int err;

	/* Since we synchronize this call and mlx4_ib_dereg_mr via uverbs,
	 * we assume that the calls can't run concurrently. Otherwise, a
	 * race exists.
	 */
	err =  mlx4_mr_hw_get_mpt(dev->dev, &mmr->mmr, &pmpt_entry);

	if (err)
		return err;

	if (flags & IB_MR_REREG_PD) {
		err = mlx4_mr_hw_change_pd(dev->dev, *pmpt_entry,
					   to_mpd(pd)->pdn);

		if (err)
			goto release_mpt_entry;
	}

	if (flags & IB_MR_REREG_ACCESS) {
		if (ib_access_writable(mr_access_flags) && !mmr->umem->writable)
			return -EPERM;

		err = mlx4_mr_hw_change_access(dev->dev, *pmpt_entry,
					       convert_access(mr_access_flags));

		if (err)
			goto release_mpt_entry;
	}

	if (flags & IB_MR_REREG_TRANS) {
		int shift;
		int n;

		mlx4_mr_rereg_mem_cleanup(dev->dev, &mmr->mmr);
		ib_umem_release(mmr->umem);
		mmr->umem =
			mlx4_get_umem_mr(mr->uobject->context, start, length,
					 virt_addr, mr_access_flags);
		if (IS_ERR(mmr->umem)) {
			err = PTR_ERR(mmr->umem);
			/* Prevent mlx4_ib_dereg_mr from free'ing invalid pointer */
			mmr->umem = NULL;
			goto release_mpt_entry;
		}
		n = ib_umem_page_count(mmr->umem);
		shift = mmr->umem->page_shift;

		err = mlx4_mr_rereg_mem_write(dev->dev, &mmr->mmr,
					      virt_addr, length, n, shift,
					      *pmpt_entry);
		if (err) {
			ib_umem_release(mmr->umem);
			goto release_mpt_entry;
		}
		mmr->mmr.iova       = virt_addr;
		mmr->mmr.size       = length;

		err = mlx4_ib_umem_write_mtt(dev, &mmr->mmr.mtt, mmr->umem);
		if (err) {
			mlx4_mr_rereg_mem_cleanup(dev->dev, &mmr->mmr);
			ib_umem_release(mmr->umem);
			goto release_mpt_entry;
		}
	}

	/* If we couldn't transfer the MR to the HCA, just remember to
	 * return a failure. But dereg_mr will free the resources.
	 */
	err = mlx4_mr_hw_write_mpt(dev->dev, &mmr->mmr, pmpt_entry);
	if (!err && flags & IB_MR_REREG_ACCESS)
		mmr->mmr.access = mr_access_flags;

release_mpt_entry:
	mlx4_mr_hw_put_mpt(dev->dev, pmpt_entry);

	return err;
}

static int
mlx4_alloc_priv_pages(struct ib_device *device,
		      struct mlx4_ib_mr *mr,
		      int max_pages)
{
	int ret;

	/* Ensure that size is aligned to DMA cacheline
	 * requirements.
	 * max_pages is limited to MLX4_MAX_FAST_REG_PAGES
	 * so page_map_size will never cross PAGE_SIZE.
	 */
	mr->page_map_size = roundup(max_pages * sizeof(u64),
				    MLX4_MR_PAGES_ALIGN);

	/* Prevent cross page boundary allocation. */
	mr->pages = (__be64 *)get_zeroed_page(GFP_KERNEL);
	if (!mr->pages)
		return -ENOMEM;

	mr->page_map = dma_map_single(device->dev.parent, mr->pages,
				      mr->page_map_size, DMA_TO_DEVICE);

	if (dma_mapping_error(device->dev.parent, mr->page_map)) {
		ret = -ENOMEM;
		goto err;
	}

	return 0;

err:
	free_page((unsigned long)mr->pages);
	return ret;
}

static void
mlx4_free_priv_pages(struct mlx4_ib_mr *mr)
{
	if (mr->pages) {
		struct ib_device *device = mr->ibmr.device;

		dma_unmap_single(device->dev.parent, mr->page_map,
				 mr->page_map_size, DMA_TO_DEVICE);
		free_page((unsigned long)mr->pages);
		mr->pages = NULL;
	}
}

int mlx4_ib_dereg_mr(struct ib_mr *ibmr)
{
	struct mlx4_ib_mr *mr = to_mmr(ibmr);
	int ret;

	mlx4_free_priv_pages(mr);

	ret = mlx4_mr_free(to_mdev(ibmr->device)->dev, &mr->mmr);
	if (ret)
		return ret;
	if (mr->umem)
		ib_umem_release(mr->umem);
	kfree(mr);

	return 0;
}

struct ib_mw *mlx4_ib_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
			       struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mw *mw;
	int err;

	mw = kmalloc(sizeof(*mw), GFP_KERNEL);
	if (!mw)
		return ERR_PTR(-ENOMEM);

	err = mlx4_mw_alloc(dev->dev, to_mpd(pd)->pdn,
			    to_mlx4_type(type), &mw->mmw);
	if (err)
		goto err_free;

	err = mlx4_mw_enable(dev->dev, &mw->mmw);
	if (err)
		goto err_mw;

	mw->ibmw.rkey = mw->mmw.key;

	return &mw->ibmw;

err_mw:
	mlx4_mw_free(dev->dev, &mw->mmw);

err_free:
	kfree(mw);

	return ERR_PTR(err);
}

int mlx4_ib_dealloc_mw(struct ib_mw *ibmw)
{
	struct mlx4_ib_mw *mw = to_mmw(ibmw);

	mlx4_mw_free(to_mdev(ibmw->device)->dev, &mw->mmw);
	kfree(mw);

	return 0;
}

struct ib_mr *mlx4_ib_alloc_mr(struct ib_pd *pd,
			       enum ib_mr_type mr_type,
			       u32 max_num_sg)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mr *mr;
	int err;

	if (mr_type != IB_MR_TYPE_MEM_REG ||
	    max_num_sg > MLX4_MAX_FAST_REG_PAGES)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	err = mlx4_mr_alloc(dev->dev, to_mpd(pd)->pdn, 0, 0, 0,
			    max_num_sg, 0, &mr->mmr);
	if (err)
		goto err_free;

	err = mlx4_alloc_priv_pages(pd->device, mr, max_num_sg);
	if (err)
		goto err_free_mr;

	mr->max_pages = max_num_sg;
	err = mlx4_mr_enable(dev->dev, &mr->mmr);
	if (err)
		goto err_free_pl;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_free_pl:
	mr->ibmr.device = pd->device;
	mlx4_free_priv_pages(mr);
err_free_mr:
	(void) mlx4_mr_free(dev->dev, &mr->mmr);
err_free:
	kfree(mr);
	return ERR_PTR(err);
}

struct ib_fmr *mlx4_ib_fmr_alloc(struct ib_pd *pd, int acc,
				 struct ib_fmr_attr *fmr_attr)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_fmr *fmr;
	int err = -ENOMEM;

	fmr = kmalloc(sizeof *fmr, GFP_KERNEL);
	if (!fmr)
		return ERR_PTR(-ENOMEM);

	err = mlx4_fmr_alloc(dev->dev, to_mpd(pd)->pdn, convert_access(acc),
			     fmr_attr->max_pages, fmr_attr->max_maps,
			     fmr_attr->page_shift, &fmr->mfmr);
	if (err)
		goto err_free;

	err = mlx4_fmr_enable(to_mdev(pd->device)->dev, &fmr->mfmr);
	if (err)
		goto err_mr;

	fmr->ibfmr.rkey = fmr->ibfmr.lkey = fmr->mfmr.mr.key;

	return &fmr->ibfmr;

err_mr:
	(void) mlx4_mr_free(to_mdev(pd->device)->dev, &fmr->mfmr.mr);

err_free:
	kfree(fmr);

	return ERR_PTR(err);
}

int mlx4_ib_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		      int npages, u64 iova)
{
	struct mlx4_ib_fmr *ifmr = to_mfmr(ibfmr);
	struct mlx4_ib_dev *dev = to_mdev(ifmr->ibfmr.device);

	return mlx4_map_phys_fmr(dev->dev, &ifmr->mfmr, page_list, npages, iova,
				 &ifmr->ibfmr.lkey, &ifmr->ibfmr.rkey);
}

int mlx4_ib_unmap_fmr(struct list_head *fmr_list)
{
	struct ib_fmr *ibfmr;
	int err;
	struct mlx4_dev *mdev = NULL;

	list_for_each_entry(ibfmr, fmr_list, list) {
		if (mdev && to_mdev(ibfmr->device)->dev != mdev)
			return -EINVAL;
		mdev = to_mdev(ibfmr->device)->dev;
	}

	if (!mdev)
		return 0;

	list_for_each_entry(ibfmr, fmr_list, list) {
		struct mlx4_ib_fmr *ifmr = to_mfmr(ibfmr);

		mlx4_fmr_unmap(mdev, &ifmr->mfmr, &ifmr->ibfmr.lkey, &ifmr->ibfmr.rkey);
	}

	/*
	 * Make sure all MPT status updates are visible before issuing
	 * SYNC_TPT firmware command.
	 */
	wmb();

	err = mlx4_SYNC_TPT(mdev);
	if (err)
		pr_warn("SYNC_TPT error %d when "
		       "unmapping FMRs\n", err);

	return 0;
}

int mlx4_ib_fmr_dealloc(struct ib_fmr *ibfmr)
{
	struct mlx4_ib_fmr *ifmr = to_mfmr(ibfmr);
	struct mlx4_ib_dev *dev = to_mdev(ibfmr->device);
	int err;

	err = mlx4_fmr_free(dev->dev, &ifmr->mfmr);

	if (!err)
		kfree(ifmr);

	return err;
}

static int mlx4_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct mlx4_ib_mr *mr = to_mmr(ibmr);

	if (unlikely(mr->npages == mr->max_pages))
		return -ENOMEM;

	mr->pages[mr->npages++] = cpu_to_be64(addr | MLX4_MTT_FLAG_PRESENT);

	return 0;
}

int mlx4_ib_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		      unsigned int *sg_offset)
{
	struct mlx4_ib_mr *mr = to_mmr(ibmr);
	int rc;

	mr->npages = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, mr->page_map,
				   mr->page_map_size, DMA_TO_DEVICE);

	rc = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, mlx4_set_page);

	ib_dma_sync_single_for_device(ibmr->device, mr->page_map,
				      mr->page_map_size, DMA_TO_DEVICE);

	return rc;
}
