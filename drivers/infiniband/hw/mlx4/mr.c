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
	struct ib_block_iter biter;
	int err, i = 0;
	u64 addr;

	rdma_umem_for_each_dma_block(umem, &biter, BIT(mtt->page_shift)) {
		addr = rdma_block_iter_dma_address(&biter);
		err = mlx4_write_mtt(dev->dev, mtt, i++, 1, &addr);
		if (err)
			return err;
	}
	return 0;
}

static struct ib_umem *mlx4_get_umem_mr(struct ib_device *device, u64 start,
					u64 length, int access_flags)
{
	/*
	 * Force registering the memory as writable if the underlying pages
	 * are writable.  This is so rereg can change the access permissions
	 * from readable to writable without having to run through ib_umem_get
	 * again
	 */
	if (!ib_access_writable(access_flags)) {
		unsigned long untagged_start = untagged_addr(start);
		struct vm_area_struct *vma;

		mmap_read_lock(current->mm);
		/*
		 * FIXME: Ideally this would iterate over all the vmas that
		 * cover the memory, but for now it requires a single vma to
		 * entirely cover the MR to support RO mappings.
		 */
		vma = find_vma(current->mm, untagged_start);
		if (vma && vma->vm_end >= untagged_start + length &&
		    vma->vm_start <= untagged_start) {
			if (vma->vm_flags & VM_WRITE)
				access_flags |= IB_ACCESS_LOCAL_WRITE;
		} else {
			access_flags |= IB_ACCESS_LOCAL_WRITE;
		}

		mmap_read_unlock(current->mm);
	}

	return ib_umem_get(device, start, length, access_flags);
}

struct ib_mr *mlx4_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_dmah *dmah,
				  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mr *mr;
	int shift;
	int err;
	int n;

	if (dmah)
		return ERR_PTR(-EOPNOTSUPP);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = mlx4_get_umem_mr(pd->device, start, length, access_flags);
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		goto err_free;
	}

	shift = mlx4_ib_umem_calc_optimal_mtt_size(mr->umem, start, &n);
	if (shift < 0) {
		err = shift;
		goto err_umem;
	}

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
	mr->ibmr.page_size = 1U << shift;

	return &mr->ibmr;

err_mr:
	(void) mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_umem:
	ib_umem_release(mr->umem);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

struct ib_mr *mlx4_ib_rereg_user_mr(struct ib_mr *mr, int flags, u64 start,
				    u64 length, u64 virt_addr,
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
		return ERR_PTR(err);

	if (flags & IB_MR_REREG_PD) {
		err = mlx4_mr_hw_change_pd(dev->dev, *pmpt_entry,
					   to_mpd(pd)->pdn);

		if (err)
			goto release_mpt_entry;
	}

	if (flags & IB_MR_REREG_ACCESS) {
		if (ib_access_writable(mr_access_flags) &&
		    !mmr->umem->writable) {
			err = -EPERM;
			goto release_mpt_entry;
		}

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
		mmr->umem = mlx4_get_umem_mr(mr->device, start, length,
					     mr_access_flags);
		if (IS_ERR(mmr->umem)) {
			err = PTR_ERR(mmr->umem);
			/* Prevent mlx4_ib_dereg_mr from free'ing invalid pointer */
			mmr->umem = NULL;
			goto release_mpt_entry;
		}
		n = ib_umem_num_dma_blocks(mmr->umem, PAGE_SIZE);
		shift = PAGE_SHIFT;

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
	if (err)
		return ERR_PTR(err);
	return NULL;
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

int mlx4_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
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

int mlx4_ib_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibmw->device);
	struct mlx4_ib_mw *mw = to_mmw(ibmw);
	int err;

	err = mlx4_mw_alloc(dev->dev, to_mpd(ibmw->pd)->pdn,
			    to_mlx4_type(ibmw->type), &mw->mmw);
	if (err)
		return err;

	err = mlx4_mw_enable(dev->dev, &mw->mmw);
	if (err)
		goto err_mw;

	ibmw->rkey = mw->mmw.key;
	return 0;

err_mw:
	mlx4_mw_free(dev->dev, &mw->mmw);
	return err;
}

int mlx4_ib_dealloc_mw(struct ib_mw *ibmw)
{
	struct mlx4_ib_mw *mw = to_mmw(ibmw);

	mlx4_mw_free(to_mdev(ibmw->device)->dev, &mw->mmw);
	return 0;
}

struct ib_mr *mlx4_ib_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
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
