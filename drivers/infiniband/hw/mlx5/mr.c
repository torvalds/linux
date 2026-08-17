/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
 * Copyright (c) 2020, Intel Corporation. All rights reserved.
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

#include <linux/bitfield.h>
#include <linux/kref.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <rdma/frmr_pools.h>
#include <rdma/ib_umem_odp.h>
#include "dm.h"
#include "mlx5_ib.h"
#include "umr.h"
#include "data_direct.h"
#include "dmah.h"

static int mkey_max_umr_order(struct mlx5_ib_dev *dev)
{
	if (MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset))
		return MLX5_MAX_UMR_EXTENDED_SHIFT;
	return MLX5_MAX_UMR_SHIFT;
}

static struct mlx5_ib_mr *reg_create(struct ib_pd *pd, struct ib_umem *umem,
				     u64 iova, int access_flags,
				     unsigned long page_size, bool populate,
				     int access_mode, u16 st_index, u8 ph);
static int __mlx5_ib_dereg_mr(struct ib_mr *ibmr);

static void set_mkc_access_pd_addr_fields(void *mkc, int acc, u64 start_addr,
					  struct ib_pd *pd)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);

	MLX5_SET(mkc, mkc, a, !!(acc & IB_ACCESS_REMOTE_ATOMIC));
	MLX5_SET(mkc, mkc, rw, !!(acc & IB_ACCESS_REMOTE_WRITE));
	MLX5_SET(mkc, mkc, rr, !!(acc & IB_ACCESS_REMOTE_READ));
	MLX5_SET(mkc, mkc, lw, !!(acc & IB_ACCESS_LOCAL_WRITE));
	MLX5_SET(mkc, mkc, lr, 1);

	if (acc & IB_ACCESS_RELAXED_ORDERING) {
		if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write))
			MLX5_SET(mkc, mkc, relaxed_ordering_write, 1);

		if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read) ||
		    (MLX5_CAP_GEN(dev->mdev,
				  relaxed_ordering_read_pci_enabled) &&
		     pcie_relaxed_ordering_enabled(dev->mdev->pdev)))
			MLX5_SET(mkc, mkc, relaxed_ordering_read, 1);
	}

	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET64(mkc, mkc, start_addr, start_addr);
}

static void assign_mkey_variant(struct mlx5_ib_dev *dev, u32 *mkey, u32 *in)
{
	u8 key = atomic_inc_return(&dev->mkey_var);
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, mkey_7_0, key);
	*mkey = key;
}

static int mlx5_ib_create_mkey(struct mlx5_ib_dev *dev,
			       struct mlx5_ib_mkey *mkey, u32 *in, int inlen)
{
	int ret;

	assign_mkey_variant(dev, &mkey->key, in);
	ret = mlx5_core_create_mkey(dev->mdev, &mkey->key, in, inlen);
	if (!ret)
		init_waitqueue_head(&mkey->wait);

	return ret;
}

static int destroy_mkey(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	WARN_ON(xa_load(&dev->odp_mkeys, mlx5_base_mkey(mr->mmkey.key)));

	return mlx5_core_destroy_mkey(dev->mdev, mr->mmkey.key);
}

static int get_mkc_octo_size(unsigned int access_mode, unsigned int ndescs)
{
	int ret = 0;

	switch (access_mode) {
	case MLX5_MKC_ACCESS_MODE_MTT:
		ret = DIV_ROUND_UP(ndescs, MLX5_IB_UMR_OCTOWORD /
						   sizeof(struct mlx5_mtt));
		break;
	case MLX5_MKC_ACCESS_MODE_KSM:
		ret = DIV_ROUND_UP(ndescs, MLX5_IB_UMR_OCTOWORD /
						   sizeof(struct mlx5_klm));
		break;
	default:
		WARN_ON(1);
	}
	return ret;
}

static int get_unchangeable_access_flags(struct mlx5_ib_dev *dev,
					 int access_flags)
{
	int ret = 0;

	if ((access_flags & IB_ACCESS_REMOTE_ATOMIC) &&
	    MLX5_CAP_GEN(dev->mdev, atomic) &&
	    MLX5_CAP_GEN(dev->mdev, umr_modify_atomic_disabled))
		ret |= IB_ACCESS_REMOTE_ATOMIC;

	if ((access_flags & IB_ACCESS_RELAXED_ORDERING) &&
	    MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write) &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write_umr))
		ret |= IB_ACCESS_RELAXED_ORDERING;

	if ((access_flags & IB_ACCESS_RELAXED_ORDERING) &&
	    (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read) ||
	     MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_pci_enabled)) &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_umr))
		ret |= IB_ACCESS_RELAXED_ORDERING;

	return ret;
}

#define MLX5_FRMR_POOLS_KEY_ACCESS_MODE_KSM_MASK 1ULL
#define MLX5_FRMR_POOLS_KEY_VENDOR_KEY_SUPPORTED \
	MLX5_FRMR_POOLS_KEY_ACCESS_MODE_KSM_MASK

#define MLX5_FRMR_POOLS_KERNEL_KEY_PH_MASK GENMASK_ULL(23, 16)
#define MLX5_FRMR_POOLS_KERNEL_KEY_ST_INDEX_MASK GENMASK_ULL(15, 0)

static struct mlx5_ib_mr *
_mlx5_frmr_pool_alloc(struct mlx5_ib_dev *dev, struct ib_umem *umem,
		      int access_flags, int access_mode,
		      unsigned long page_size, u16 st_index, u8 ph)
{
	struct mlx5_ib_mr *mr;
	int err;

	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->ibmr.frmr.key.ats = mlx5_umem_needs_ats(dev, umem, access_flags);
	mr->ibmr.frmr.key.access_flags =
		get_unchangeable_access_flags(dev, access_flags);
	mr->ibmr.frmr.key.num_dma_blocks =
		ib_umem_num_dma_blocks(umem, page_size);
	mr->ibmr.frmr.key.vendor_key =
		access_mode == MLX5_MKC_ACCESS_MODE_KSM ?
			MLX5_FRMR_POOLS_KEY_ACCESS_MODE_KSM_MASK :
			0;

	/* Normalize ph: swap 0 and MLX5_IB_NO_PH */
	if (ph == MLX5_IB_NO_PH || ph == 0)
		ph ^= MLX5_IB_NO_PH;

	mr->ibmr.frmr.key.kernel_vendor_key =
		FIELD_PREP(MLX5_FRMR_POOLS_KERNEL_KEY_ST_INDEX_MASK, st_index) |
		FIELD_PREP(MLX5_FRMR_POOLS_KERNEL_KEY_PH_MASK, ph);
	err = ib_frmr_pool_pop(&dev->ib_dev, &mr->ibmr);
	if (err) {
		kfree(mr);
		return ERR_PTR(err);
	}
	mr->mmkey.key = mr->ibmr.frmr.handle;
	init_waitqueue_head(&mr->mmkey.wait);

	return mr;
}

struct mlx5_ib_mr *mlx5_mr_cache_alloc(struct mlx5_ib_dev *dev,
				       int access_flags, int access_mode,
				       int ndescs)
{
	struct ib_frmr_key key = {
		.access_flags =
			get_unchangeable_access_flags(dev, access_flags),
		.vendor_key = access_mode == MLX5_MKC_ACCESS_MODE_MTT ?
				      0 :
				      MLX5_FRMR_POOLS_KEY_ACCESS_MODE_KSM_MASK,
		.num_dma_blocks = ndescs,
		.kernel_vendor_key = 0, /* no PH and no ST index */
	};
	struct mlx5_ib_mr *mr;
	int ret;

	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&mr->mmkey.wait);

	mr->ibmr.frmr.key = key;
	ret = ib_frmr_pool_pop(&dev->ib_dev, &mr->ibmr);
	if (ret) {
		kfree(mr);
		return ERR_PTR(ret);
	}
	mr->mmkey.key = mr->ibmr.frmr.handle;
	mr->mmkey.type = MLX5_MKEY_MR;

	return mr;
}

static int mlx5r_create_mkeys(struct ib_device *device, struct ib_frmr_key *key,
			      u32 *handles, unsigned int count)
{
	int access_mode =
		key->vendor_key & MLX5_FRMR_POOLS_KEY_ACCESS_MODE_KSM_MASK ?
			MLX5_MKC_ACCESS_MODE_KSM :
			MLX5_MKC_ACCESS_MODE_MTT;

	struct mlx5_ib_dev *dev = to_mdev(device);
	size_t inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	u16 st_index;
	void *mkc;
	u32 *in;
	int err, i;
	u8 ph;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	set_mkc_access_pd_addr_fields(mkc, key->access_flags, 0, dev->umrc.pd);
	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, access_mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, (access_mode >> 2) & 0x7);
	MLX5_SET(mkc, mkc, ma_translation_mode, !!key->ats);
	MLX5_SET(mkc, mkc, translations_octword_size,
		 get_mkc_octo_size(access_mode, key->num_dma_blocks));
	MLX5_SET(mkc, mkc, log_page_size, PAGE_SHIFT);

	st_index = FIELD_GET(MLX5_FRMR_POOLS_KERNEL_KEY_ST_INDEX_MASK,
			     key->kernel_vendor_key);
	ph = FIELD_GET(MLX5_FRMR_POOLS_KERNEL_KEY_PH_MASK,
		       key->kernel_vendor_key);
	if (ph) {
		/* Normalize ph: swap MLX5_IB_NO_PH for 0 */
		if (ph == MLX5_IB_NO_PH)
			ph = 0;
		MLX5_SET(mkc, mkc, pcie_tph_en, 1);
		MLX5_SET(mkc, mkc, pcie_tph_ph, ph);
		if (st_index != MLX5_MKC_PCIE_TPH_NO_STEERING_TAG_INDEX)
			MLX5_SET(mkc, mkc, pcie_tph_steering_tag_index,
				 st_index);
	}

	for (i = 0; i < count; i++) {
		assign_mkey_variant(dev, handles + i, in);
		err = mlx5_core_create_mkey(dev->mdev, handles + i, in, inlen);
		if (err)
			goto free_in;
	}
free_in:
	kfree(in);
	if (err)
		for (i--; i >= 0; i--)
			mlx5_core_destroy_mkey(dev->mdev, handles[i]);
	return err;
}

static void mlx5r_destroy_mkeys(struct ib_device *device, u32 *handles,
				unsigned int count)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	int i, err;

	for (i = 0; i < count; i++) {
		err = mlx5_core_destroy_mkey(dev->mdev, handles[i]);
		if (err)
			pr_warn_ratelimited(
				"mlx5_ib: failed to destroy mkey %d: %d",
				handles[i], err);
	}
}

static int mlx5r_build_frmr_key(struct ib_device *device,
				const struct ib_frmr_key *in,
				struct ib_frmr_key *out)
{
	struct mlx5_ib_dev *dev = to_mdev(device);

	/* check HW capabilities of users requested frmr key */
	if ((in->ats && !MLX5_CAP_GEN(dev->mdev, ats)) ||
	    ilog2(in->num_dma_blocks) > mkey_max_umr_order(dev))
		return -EOPNOTSUPP;

	if (in->vendor_key & ~MLX5_FRMR_POOLS_KEY_VENDOR_KEY_SUPPORTED)
		return -EOPNOTSUPP;

	out->ats = in->ats;
	out->access_flags =
		get_unchangeable_access_flags(dev, in->access_flags);
	out->vendor_key = in->vendor_key;
	out->num_dma_blocks = in->num_dma_blocks;

	return 0;
}

static struct ib_frmr_pool_ops mlx5r_frmr_pool_ops = {
	.create_frmrs = mlx5r_create_mkeys,
	.destroy_frmrs = mlx5r_destroy_mkeys,
	.build_key = mlx5r_build_frmr_key,
};

int mlx5r_frmr_pools_init(struct ib_device *device)
{
	struct mlx5_ib_dev *dev = to_mdev(device);

	mutex_init(&dev->slow_path_mutex);
	return ib_frmr_pools_init(device, &mlx5r_frmr_pool_ops);
}

void mlx5r_frmr_pools_cleanup(struct ib_device *device)
{
	ib_frmr_pools_cleanup(device);
}

struct ib_mr *mlx5_ib_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_ib_mr *mr;
	void *mkc;
	u32 *in;
	int err;

	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, length64, 1);
	set_mkc_access_pd_addr_fields(mkc, acc | IB_ACCESS_RELAXED_ORDERING, 0,
				      pd);
	MLX5_SET(mkc, mkc, ma_translation_mode, MLX5_CAP_GEN(dev->mdev, ats));

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err)
		goto err_in;

	kfree(in);
	mr->mmkey.type = MLX5_MKEY_MR;
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_in:
	kfree(in);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

static int get_octo_len(u64 addr, u64 len, int page_shift)
{
	u64 page_size = 1ULL << page_shift;
	u64 offset;
	int npages;

	offset = addr & (page_size - 1);
	npages = ALIGN(len + offset, page_size) >> page_shift;
	return (npages + 1) / 2;
}

static void set_mr_fields(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr,
			  u64 length, int access_flags, u64 iova)
{
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;
	mr->ibmr.length = length;
	mr->ibmr.device = &dev->ib_dev;
	mr->ibmr.iova = iova;
	mr->access_flags = access_flags;
}

static unsigned int mlx5_umem_dmabuf_default_pgsz(struct ib_umem *umem,
						  u64 iova)
{
	/*
	 * The alignment of iova has already been checked upon entering
	 * UVERBS_METHOD_REG_DMABUF_MR
	 */
	umem->iova = iova;
	return PAGE_SIZE;
}

static struct mlx5_ib_mr *alloc_cacheable_mr(struct ib_pd *pd,
					     struct ib_umem *umem, u64 iova,
					     int access_flags, int access_mode,
					     u16 st_index, u8 ph)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr;
	unsigned long page_size;

	if (umem->is_dmabuf)
		page_size = mlx5_umem_dmabuf_default_pgsz(umem, iova);
	else
		page_size = mlx5_umem_mkc_find_best_pgsz(dev, umem, iova,
							 access_mode);
	if (WARN_ON(!page_size))
		return ERR_PTR(-EINVAL);

	mr = _mlx5_frmr_pool_alloc(dev, umem, access_flags, access_mode,
				   page_size, st_index, ph);
	if (IS_ERR(mr))
		return mr;

	mr->mmkey.type = MLX5_MKEY_MR;
	mr->ibmr.pd = pd;
	mr->umem = umem;
	mr->page_shift = order_base_2(page_size);
	set_mr_fields(dev, mr, umem->length, access_flags, iova);

	return mr;
}

static struct ib_mr *
reg_create_crossing_vhca_mr(struct ib_pd *pd, u64 iova, u64 length, int access_flags,
			    u32 crossed_lkey)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int access_mode = MLX5_MKC_ACCESS_MODE_CROSSING;
	struct mlx5_ib_mr *mr;
	void *mkc;
	int inlen;
	u32 *in;
	int err;

	if (!MLX5_CAP_GEN(dev->mdev, crossing_vhca_mkey))
		return ERR_PTR(-EOPNOTSUPP);

	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_1;
	}

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, crossing_target_vhca_id,
		 MLX5_CAP_GEN(dev->mdev, vhca_id));
	MLX5_SET(mkc, mkc, translations_octword_size, crossed_lkey);
	MLX5_SET(mkc, mkc, access_mode_1_0, access_mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, (access_mode >> 2) & 0x7);

	/* for this crossing mkey IOVA should be 0 and len should be IOVA + len */
	set_mkc_access_pd_addr_fields(mkc, access_flags, 0, pd);
	MLX5_SET64(mkc, mkc, len, iova + length);

	MLX5_SET(mkc, mkc, free, 0);
	MLX5_SET(mkc, mkc, umr_en, 0);
	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err)
		goto err_2;

	mr->mmkey.type = MLX5_MKEY_MR;
	set_mr_fields(dev, mr, length, access_flags, iova);
	mr->ibmr.pd = pd;
	kvfree(in);
	mlx5_ib_dbg(dev, "crossing mkey = 0x%x\n", mr->mmkey.key);

	return &mr->ibmr;
err_2:
	kvfree(in);
err_1:
	kfree(mr);
	return ERR_PTR(err);
}

/*
 * If ibmr is NULL it will be allocated by reg_create.
 * Else, the given ibmr will be used.
 */
static struct mlx5_ib_mr *reg_create(struct ib_pd *pd, struct ib_umem *umem,
				     u64 iova, int access_flags,
				     unsigned long page_size, bool populate,
				     int access_mode, u16 st_index, u8 ph)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr;
	__be64 *pas;
	void *mkc;
	int inlen;
	u32 *in;
	int err;
	bool pg_cap = !!(MLX5_CAP_GEN(dev->mdev, pg)) &&
		(access_mode == MLX5_MKC_ACCESS_MODE_MTT) &&
		(ph == MLX5_IB_NO_PH);
	bool ksm_mode = (access_mode == MLX5_MKC_ACCESS_MODE_KSM);

	if (!page_size)
		return ERR_PTR(-EINVAL);
	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->ibmr.pd = pd;
	mr->access_flags = access_flags;
	mr->page_shift = order_base_2(page_size);

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	if (populate)
		inlen += sizeof(*pas) *
			 roundup(ib_umem_num_dma_blocks(umem, page_size), 2);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_1;
	}
	pas = (__be64 *)MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
	if (populate) {
		if (WARN_ON(access_flags & IB_ACCESS_ON_DEMAND || ksm_mode)) {
			err = -EINVAL;
			goto err_2;
		}
		mlx5_ib_populate_pas(umem, 1UL << mr->page_shift, pas,
				     pg_cap ? MLX5_IB_MTT_PRESENT : 0);
	}

	/* The pg_access bit allows setting the access flags
	 * in the page list submitted with the command.
	 */
	MLX5_SET(create_mkey_in, in, pg_access, !!(pg_cap));

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	set_mkc_access_pd_addr_fields(mkc, access_flags, iova,
				      populate ? pd : dev->umrc.pd);
	/* In case a data direct flow, overwrite the pdn field by its internal kernel PD */
	if (umem->is_dmabuf && ksm_mode)
		MLX5_SET(mkc, mkc, pd, dev->ddr.pdn);

	MLX5_SET(mkc, mkc, free, !populate);
	MLX5_SET(mkc, mkc, access_mode_1_0, access_mode);
	MLX5_SET(mkc, mkc, umr_en, 1);

	MLX5_SET64(mkc, mkc, len, umem->length);
	MLX5_SET(mkc, mkc, bsf_octword_size, 0);
	if (ksm_mode)
		MLX5_SET(mkc, mkc, translations_octword_size,
			 get_octo_len(iova, umem->length, mr->page_shift) * 2);
	else
		MLX5_SET(mkc, mkc, translations_octword_size,
			 get_octo_len(iova, umem->length, mr->page_shift));
	MLX5_SET(mkc, mkc, log_page_size, mr->page_shift);
	if (mlx5_umem_needs_ats(dev, umem, access_flags))
		MLX5_SET(mkc, mkc, ma_translation_mode, 1);
	if (populate) {
		MLX5_SET(create_mkey_in, in, translations_octword_actual_size,
			 get_octo_len(iova, umem->length, mr->page_shift));
	}

	if (ph != MLX5_IB_NO_PH) {
		MLX5_SET(mkc, mkc, pcie_tph_en, 1);
		MLX5_SET(mkc, mkc, pcie_tph_ph, ph);
		if (st_index != MLX5_MKC_PCIE_TPH_NO_STEERING_TAG_INDEX)
			MLX5_SET(mkc, mkc, pcie_tph_steering_tag_index, st_index);
	}

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err) {
		mlx5_ib_warn(dev, "create mkey failed\n");
		goto err_2;
	}
	mr->mmkey.type = MLX5_MKEY_MR;
	mr->mmkey.ndescs = get_octo_len(iova, umem->length, mr->page_shift);
	mr->umem = umem;
	set_mr_fields(dev, mr, umem->length, access_flags, iova);
	kvfree(in);

	mlx5_ib_dbg(dev, "mkey = 0x%x\n", mr->mmkey.key);

	return mr;

err_2:
	kvfree(in);
err_1:
	kfree(mr);
	return ERR_PTR(err);
}

static struct ib_mr *mlx5_ib_get_dm_mr(struct ib_pd *pd, u64 start_addr,
				       u64 length, int acc, int mode)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_ib_mr *mr;
	void *mkc;
	u32 *in;
	int err;

	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, access_mode_1_0, mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, (mode >> 2) & 0x7);
	MLX5_SET64(mkc, mkc, len, length);
	set_mkc_access_pd_addr_fields(mkc, acc, start_addr, pd);

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err)
		goto err_in;

	kfree(in);

	set_mr_fields(dev, mr, length, acc, start_addr);

	return &mr->ibmr;

err_in:
	kfree(in);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

int mlx5_ib_advise_mr(struct ib_pd *pd,
		      enum ib_uverbs_advise_mr_advice advice,
		      u32 flags,
		      struct ib_sge *sg_list,
		      u32 num_sge,
		      struct uverbs_attr_bundle *attrs)
{
	if (advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH &&
	    advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_WRITE &&
	    advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT)
		return -EOPNOTSUPP;

	return mlx5_ib_advise_mr_prefetch(pd, advice, flags,
					 sg_list, num_sge);
}

struct ib_mr *mlx5_ib_reg_dm_mr(struct ib_pd *pd, struct ib_dm *dm,
				struct ib_dm_mr_attr *attr,
				struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_dm *mdm = to_mdm(dm);
	struct mlx5_core_dev *dev = to_mdev(dm->device)->mdev;
	u64 start_addr = mdm->dev_addr + attr->offset;
	int mode;

	switch (mdm->type) {
	case MLX5_IB_UAPI_DM_TYPE_MEMIC:
		if (attr->access_flags & ~MLX5_IB_DM_MEMIC_ALLOWED_ACCESS)
			return ERR_PTR(-EINVAL);

		mode = MLX5_MKC_ACCESS_MODE_MEMIC;
		start_addr -= pci_resource_start(dev->pdev, 0);
		break;
	case MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM:
	case MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_SW_ICM:
	case MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_PATTERN_SW_ICM:
	case MLX5_IB_UAPI_DM_TYPE_ENCAP_SW_ICM:
		if (attr->access_flags & ~MLX5_IB_DM_SW_ICM_ALLOWED_ACCESS)
			return ERR_PTR(-EINVAL);

		mode = MLX5_MKC_ACCESS_MODE_SW_ICM;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return mlx5_ib_get_dm_mr(pd, start_addr, attr->length,
				 attr->access_flags, mode);
}

static struct ib_mr *create_real_mr(struct ib_pd *pd, struct ib_umem *umem,
				    u64 iova, int access_flags,
				    struct ib_dmah *dmah)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr = NULL;
	bool xlt_with_umr;
	u16 st_index = MLX5_MKC_PCIE_TPH_NO_STEERING_TAG_INDEX;
	u8 ph = MLX5_IB_NO_PH;
	int err;

	if (dmah) {
		struct mlx5_ib_dmah *mdmah = to_mdmah(dmah);

		ph = dmah->ph;
		if (dmah->valid_fields & BIT(IB_DMAH_CPU_ID_EXISTS))
			st_index = mdmah->st_index;
	}

	xlt_with_umr = mlx5r_umr_can_load_pas(dev, umem->length);
	if (xlt_with_umr) {
		mr = alloc_cacheable_mr(pd, umem, iova, access_flags,
					MLX5_MKC_ACCESS_MODE_MTT,
					st_index, ph);
	} else {
		unsigned long page_size = mlx5_umem_mkc_find_best_pgsz(
				dev, umem, iova, MLX5_MKC_ACCESS_MODE_MTT);

		mutex_lock(&dev->slow_path_mutex);
		mr = reg_create(pd, umem, iova, access_flags, page_size,
				true, MLX5_MKC_ACCESS_MODE_MTT,
				st_index, ph);
		mutex_unlock(&dev->slow_path_mutex);
	}
	if (IS_ERR(mr)) {
		ib_umem_release(umem);
		return ERR_CAST(mr);
	}

	mlx5_ib_dbg(dev, "mkey 0x%x\n", mr->mmkey.key);

	atomic_add(ib_umem_num_pages(umem), &dev->mdev->priv.reg_pages);

	if (xlt_with_umr) {
		/*
		 * If the MR was created with reg_create then it will be
		 * configured properly but left disabled. It is safe to go ahead
		 * and configure it again via UMR while enabling it.
		 */
		err = mlx5r_umr_update_mr_pas(mr, MLX5_IB_UPD_XLT_ENABLE,
					      to_mpd(pd)->pdn);
		if (err) {
			mlx5_ib_dereg_mr(&mr->ibmr, NULL);
			return ERR_PTR(err);
		}
	}
	return &mr->ibmr;
}

static struct ib_mr *create_user_odp_mr(struct ib_pd *pd, u64 start, u64 length,
					u64 iova, int access_flags,
					struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct ib_umem_odp *odp;
	struct mlx5_ib_mr *mr;
	int err;

	if (!IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING))
		return ERR_PTR(-EOPNOTSUPP);

	err = mlx5r_odp_create_eq(dev, &dev->odp_pf_eq);
	if (err)
		return ERR_PTR(err);
	if (!start && length == U64_MAX) {
		if (iova != 0)
			return ERR_PTR(-EINVAL);
		if (!(dev->odp_caps.general_caps & IB_ODP_SUPPORT_IMPLICIT))
			return ERR_PTR(-EINVAL);

		mr = mlx5_ib_alloc_implicit_mr(to_mpd(pd), access_flags);
		if (IS_ERR(mr))
			return ERR_CAST(mr);
		return &mr->ibmr;
	}

	/* ODP requires xlt update via umr to work. */
	if (!mlx5r_umr_can_load_pas(dev, length))
		return ERR_PTR(-EINVAL);

	odp = ib_umem_odp_get(&dev->ib_dev, start, length, access_flags,
			      &mlx5_mn_ops);
	if (IS_ERR(odp))
		return ERR_CAST(odp);

	mr = alloc_cacheable_mr(pd, &odp->umem, iova, access_flags,
				MLX5_MKC_ACCESS_MODE_MTT,
				MLX5_MKC_PCIE_TPH_NO_STEERING_TAG_INDEX,
				MLX5_IB_NO_PH);
	if (IS_ERR(mr)) {
		ib_umem_release(&odp->umem);
		return ERR_CAST(mr);
	}
	xa_init(&mr->implicit_children);

	odp->private = mr;
	err = mlx5r_store_odp_mkey(dev, &mr->mmkey);
	if (err)
		goto err_dereg_mr;

	err = mlx5_ib_init_odp_mr(mr, pd);
	if (err)
		goto err_dereg_mr;
	return &mr->ibmr;

err_dereg_mr:
	mlx5_ib_dereg_mr(&mr->ibmr, NULL);
	return ERR_PTR(err);
}

struct ib_mr *mlx5_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 iova, int access_flags,
				  struct ib_dmah *dmah,
				  struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct ib_umem *umem;
	int err;

	if (!IS_ENABLED(CONFIG_INFINIBAND_USER_MEM) ||
	    ((access_flags & IB_ACCESS_ON_DEMAND) && dmah))
		return ERR_PTR(-EOPNOTSUPP);

	mlx5_ib_dbg(dev, "start 0x%llx, iova 0x%llx, length 0x%llx, access_flags 0x%x\n",
		    start, iova, length, access_flags);

	err = mlx5r_umr_resource_init(dev);
	if (err)
		return ERR_PTR(err);

	if (access_flags & IB_ACCESS_ON_DEMAND)
		return create_user_odp_mr(pd, start, length, iova, access_flags,
					  udata);
	umem = ib_umem_get_va(&dev->ib_dev, start, length, access_flags);
	if (IS_ERR(umem))
		return ERR_CAST(umem);
	return create_real_mr(pd, umem, iova, access_flags, dmah);
}

static void mlx5_ib_dmabuf_invalidate_cb(struct dma_buf_attachment *attach)
{
	struct ib_umem_dmabuf *umem_dmabuf = attach->importer_priv;
	struct mlx5_ib_mr *mr = umem_dmabuf->private;

	dma_resv_assert_held(umem_dmabuf->attach->dmabuf->resv);

	if (!umem_dmabuf->sgt || !mr)
		return;

	/* MLX5_IB_UPD_XLT_ZAP does not change the pdn */
	mlx5r_umr_update_mr_pas(mr, MLX5_IB_UPD_XLT_ZAP, 0);
	ib_umem_dmabuf_unmap_pages(umem_dmabuf);
}

static struct dma_buf_attach_ops mlx5_ib_dmabuf_attach_ops = {
	.allow_peer2peer = 1,
	.invalidate_mappings = mlx5_ib_dmabuf_invalidate_cb,
};

static struct ib_mr *
reg_user_mr_dmabuf(struct ib_pd *pd, struct device *dma_device,
		   u64 offset, u64 length, u64 virt_addr,
		   int fd, int access_flags, int access_mode,
		   struct ib_dmah *dmah)
{
	bool pinned_mode = (access_mode == MLX5_MKC_ACCESS_MODE_KSM);
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr = NULL;
	struct ib_umem_dmabuf *umem_dmabuf;
	u16 st_index = MLX5_MKC_PCIE_TPH_NO_STEERING_TAG_INDEX;
	u8 ph = MLX5_IB_NO_PH;
	int err;

	err = mlx5r_umr_resource_init(dev);
	if (err)
		return ERR_PTR(err);

	if (!pinned_mode)
		umem_dmabuf = ib_umem_dmabuf_get(&dev->ib_dev,
						 offset, length, fd,
						 access_flags,
						 &mlx5_ib_dmabuf_attach_ops);
	else if (dma_device)
		umem_dmabuf = ib_umem_dmabuf_get_pinned_with_dma_device(&dev->ib_dev,
				dma_device, offset, length,
				fd, access_flags);
	else
		umem_dmabuf = ib_umem_dmabuf_get_pinned(
			&dev->ib_dev, offset, length, fd, access_flags);

	if (IS_ERR(umem_dmabuf)) {
		mlx5_ib_dbg(dev, "umem_dmabuf get failed (%pe)\n", umem_dmabuf);
		return ERR_CAST(umem_dmabuf);
	}

	if (dmah) {
		struct mlx5_ib_dmah *mdmah = to_mdmah(dmah);

		ph = dmah->ph;
		if (dmah->valid_fields & BIT(IB_DMAH_CPU_ID_EXISTS))
			st_index = mdmah->st_index;
	}

	mr = alloc_cacheable_mr(pd, &umem_dmabuf->umem, virt_addr,
				access_flags, access_mode,
				st_index, ph);
	if (IS_ERR(mr)) {
		ib_umem_release(&umem_dmabuf->umem);
		return ERR_CAST(mr);
	}

	mlx5_ib_dbg(dev, "mkey 0x%x\n", mr->mmkey.key);

	atomic_add(ib_umem_num_pages(mr->umem), &dev->mdev->priv.reg_pages);
	umem_dmabuf->private = mr;
	if (!pinned_mode) {
		err = mlx5r_odp_create_eq(dev, &dev->odp_pf_eq);
		if (err)
			goto err_dereg_mr;

		err = mlx5r_store_odp_mkey(dev, &mr->mmkey);
		if (err)
			goto err_dereg_mr;
	} else {
		mr->data_direct = true;
	}

	err = mlx5_ib_init_dmabuf_mr(mr, pd);
	if (err)
		goto err_dereg_mr;
	return &mr->ibmr;

err_dereg_mr:
	__mlx5_ib_dereg_mr(&mr->ibmr);
	return ERR_PTR(err);
}

static struct ib_mr *
reg_user_mr_dmabuf_by_data_direct(struct ib_pd *pd, u64 offset,
				  u64 length, u64 virt_addr,
				  int fd, int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_data_direct_dev *data_direct_dev;
	struct ib_mr *crossing_mr;
	struct ib_mr *crossed_mr;
	int ret = 0;

	/* As of HW behaviour the IOVA must be page aligned in KSM mode */
	if (!PAGE_ALIGNED(virt_addr) || (access_flags & IB_ACCESS_ON_DEMAND))
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&dev->data_direct_lock);
	data_direct_dev = dev->data_direct_dev;
	if (!data_direct_dev) {
		ret = -EINVAL;
		goto end;
	}

	/* If no device's 'data direct mkey' with RO flags exists
	 * mask it out accordingly.
	 */
	if (!dev->ddr.mkey_ro_valid)
		access_flags &= ~IB_ACCESS_RELAXED_ORDERING;
	crossed_mr = reg_user_mr_dmabuf(pd, &data_direct_dev->pdev->dev,
					offset, length, virt_addr, fd,
					access_flags, MLX5_MKC_ACCESS_MODE_KSM,
					NULL);
	if (IS_ERR(crossed_mr)) {
		ret = PTR_ERR(crossed_mr);
		goto end;
	}

	mutex_lock(&dev->slow_path_mutex);
	crossing_mr = reg_create_crossing_vhca_mr(pd, virt_addr, length, access_flags,
						  crossed_mr->lkey);
	mutex_unlock(&dev->slow_path_mutex);
	if (IS_ERR(crossing_mr)) {
		__mlx5_ib_dereg_mr(crossed_mr);
		ret = PTR_ERR(crossing_mr);
		goto end;
	}

	list_add_tail(&to_mmr(crossed_mr)->dd_node, &dev->data_direct_mr_list);
	to_mmr(crossing_mr)->dd_crossed_mr = to_mmr(crossed_mr);
	to_mmr(crossing_mr)->data_direct = true;
end:
	mutex_unlock(&dev->data_direct_lock);
	return ret ? ERR_PTR(ret) : crossing_mr;
}

struct ib_mr *mlx5_ib_reg_user_mr_dmabuf(struct ib_pd *pd, u64 offset,
					 u64 length, u64 virt_addr,
					 int fd, int access_flags,
					 struct ib_dmah *dmah,
					 struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int mlx5_access_flags = 0;
	int err;

	if (!IS_ENABLED(CONFIG_INFINIBAND_USER_MEM) ||
	    !IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING))
		return ERR_PTR(-EOPNOTSUPP);

	if (uverbs_attr_is_valid(attrs, MLX5_IB_ATTR_REG_DMABUF_MR_ACCESS_FLAGS)) {
		err = uverbs_get_flags32(&mlx5_access_flags, attrs,
					 MLX5_IB_ATTR_REG_DMABUF_MR_ACCESS_FLAGS,
					 MLX5_IB_UAPI_REG_DMABUF_ACCESS_DATA_DIRECT);
		if (err)
			return ERR_PTR(err);
	}

	mlx5_ib_dbg(dev,
		    "offset 0x%llx, virt_addr 0x%llx, length 0x%llx, fd %d, access_flags 0x%x, mlx5_access_flags 0x%x\n",
		    offset, virt_addr, length, fd, access_flags, mlx5_access_flags);

	/* dmabuf requires xlt update via umr to work. */
	if (!mlx5r_umr_can_load_pas(dev, length))
		return ERR_PTR(-EINVAL);

	if (mlx5_access_flags & MLX5_IB_UAPI_REG_DMABUF_ACCESS_DATA_DIRECT)
		return reg_user_mr_dmabuf_by_data_direct(pd, offset, length, virt_addr,
							 fd, access_flags);

	return reg_user_mr_dmabuf(pd, NULL, offset, length, virt_addr, fd,
				  access_flags, MLX5_MKC_ACCESS_MODE_MTT, dmah);
}

/*
 * True if the change in access flags can be done via UMR, only some access
 * flags can be updated.
 */
static bool can_use_umr_rereg_access(struct mlx5_ib_dev *dev,
				     unsigned int current_access_flags,
				     unsigned int target_access_flags)
{
	unsigned int diffs = current_access_flags ^ target_access_flags;

	if (diffs & ~(IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_WRITE |
		      IB_ACCESS_REMOTE_READ | IB_ACCESS_RELAXED_ORDERING |
		      IB_ACCESS_REMOTE_ATOMIC))
		return false;
	return mlx5r_umr_can_reconfig(dev, current_access_flags,
				      target_access_flags);
}

static bool can_use_umr_rereg_pas(struct mlx5_ib_mr *mr,
				  struct ib_umem *new_umem,
				  int new_access_flags, u64 iova,
				  unsigned long *page_size)
{
	struct mlx5_ib_dev *dev = to_mdev(mr->ibmr.device);
	u8 access_mode;

	/* We only track the allocated sizes of MRs from the frmr pools */
	if (!mr->ibmr.frmr.pool)
		return false;
	if (!mlx5r_umr_can_load_pas(dev, new_umem->length))
		return false;

	access_mode = mr->ibmr.frmr.key.vendor_key &
				      MLX5_FRMR_POOLS_KEY_ACCESS_MODE_KSM_MASK ?
			      MLX5_MKC_ACCESS_MODE_KSM :
			      MLX5_MKC_ACCESS_MODE_MTT;

	*page_size =
		mlx5_umem_mkc_find_best_pgsz(dev, new_umem, iova, access_mode);
	if (WARN_ON(!*page_size))
		return false;
	return (mr->ibmr.frmr.key.num_dma_blocks) >=
	       ib_umem_num_dma_blocks(new_umem, *page_size);
}

static int umr_rereg_pas(struct mlx5_ib_mr *mr, struct ib_pd *pd,
			 int access_flags, int flags, struct ib_umem *new_umem,
			 u64 iova, unsigned long page_size)
{
	struct mlx5_ib_dev *dev = to_mdev(mr->ibmr.device);
	int upd_flags = MLX5_IB_UPD_XLT_ADDR | MLX5_IB_UPD_XLT_ENABLE;
	struct ib_umem *old_umem = mr->umem;
	int err;

	/*
	 * To keep everything simple the MR is revoked before we start to mess
	 * with it. This ensure the change is atomic relative to any use of the
	 * MR.
	 */
	err = mlx5r_umr_revoke_mr(mr);
	if (err)
		return err;

	if (flags & IB_MR_REREG_PD)
		upd_flags |= MLX5_IB_UPD_XLT_PD;
	if (flags & IB_MR_REREG_ACCESS) {
		mr->access_flags = access_flags;
		upd_flags |= MLX5_IB_UPD_XLT_ACCESS;
	}

	mr->ibmr.iova = iova;
	mr->ibmr.length = new_umem->length;
	mr->page_shift = order_base_2(page_size);
	mr->umem = new_umem;
	err = mlx5r_umr_update_mr_pas(mr, upd_flags, to_mpd(pd)->pdn);
	if (err) {
		/*
		 * The MR is revoked at this point so there is no issue to free
		 * new_umem.
		 */
		mr->umem = old_umem;
		return err;
	}

	atomic_sub(ib_umem_num_pages(old_umem), &dev->mdev->priv.reg_pages);
	ib_umem_release(old_umem);
	atomic_add(ib_umem_num_pages(new_umem), &dev->mdev->priv.reg_pages);
	return 0;
}

struct ib_mr *mlx5_ib_rereg_user_mr(struct ib_mr *ib_mr, int flags, u64 start,
				    u64 length, u64 iova, int new_access_flags,
				    struct ib_pd *new_pd,
				    struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(ib_mr->device);
	struct mlx5_ib_mr *mr = to_mmr(ib_mr);
	int err;

	if (!IS_ENABLED(CONFIG_INFINIBAND_USER_MEM) || mr->data_direct ||
	    (mr->ibmr.frmr.key.kernel_vendor_key &
	     MLX5_FRMR_POOLS_KERNEL_KEY_PH_MASK) != 0)
		return ERR_PTR(-EOPNOTSUPP);

	mlx5_ib_dbg(
		dev,
		"start 0x%llx, iova 0x%llx, length 0x%llx, access_flags 0x%x\n",
		start, iova, length, new_access_flags);

	if (flags & ~(IB_MR_REREG_TRANS | IB_MR_REREG_PD | IB_MR_REREG_ACCESS))
		return ERR_PTR(-EOPNOTSUPP);

	err = ib_umem_check_rereg(mr->umem, flags, new_access_flags);
	if (err)
		return ERR_PTR(err);

	if (!(flags & IB_MR_REREG_ACCESS))
		new_access_flags = mr->access_flags;
	if (!(flags & IB_MR_REREG_PD))
		new_pd = ib_mr->pd;

	if (mr->is_odp_implicit && !(flags & IB_MR_REREG_TRANS)) {
		if (!(new_access_flags & IB_ACCESS_ON_DEMAND))
			return ERR_PTR(-EOPNOTSUPP);

		/*
		 * Due to all the child mkeys we cannot actually change an
		 * implicit MR in place. If the user did not specify a new
		 * translation then force the fixed implicit MR values.
		 */
		start = 0;
		iova = 0;
		length = U64_MAX;
		flags |= IB_MR_REREG_TRANS;
	}

	if (!(flags & IB_MR_REREG_TRANS)) {
		struct ib_umem *umem;

		/* Fast path for PD/access change */
		if (can_use_umr_rereg_access(dev, mr->access_flags,
					     new_access_flags)) {
			err = mlx5r_umr_rereg_pd_access(mr, new_pd,
							new_access_flags);
			if (err)
				return ERR_PTR(err);
			return NULL;
		}
		/* DM or ODP MR's don't have a normal umem so we can't re-use it */
		if (!mr->umem || is_odp_mr(mr) || is_dmabuf_mr(mr))
			return ERR_PTR(-EOPNOTSUPP);

		/*
		 * Only one active MR can refer to a umem at one time, revoke
		 * the old MR before assigning the umem to the new one.
		 */
		err = mlx5r_umr_revoke_mr(mr);
		if (err)
			return ERR_PTR(err);
		umem = mr->umem;
		mr->umem = NULL;
		atomic_sub(ib_umem_num_pages(umem), &dev->mdev->priv.reg_pages);

		return create_real_mr(new_pd, umem, mr->ibmr.iova,
				      new_access_flags, NULL);
	}

	/*
	 * DM doesn't have a PAS list so we can't re-use it, odp/dmabuf does
	 * but the logic around releasing the umem is different
	 */
	if (!mr->umem || is_odp_mr(mr) || is_dmabuf_mr(mr))
		goto recreate;

	if (!(new_access_flags & IB_ACCESS_ON_DEMAND) &&
	    can_use_umr_rereg_access(dev, mr->access_flags, new_access_flags)) {
		struct ib_umem *new_umem;
		unsigned long page_size;

		new_umem = ib_umem_get_va(&dev->ib_dev, start, length,
					  new_access_flags);
		if (IS_ERR(new_umem))
			return ERR_CAST(new_umem);

		/* Fast path for PAS change */
		if (can_use_umr_rereg_pas(mr, new_umem, new_access_flags, iova,
					  &page_size)) {
			err = umr_rereg_pas(mr, new_pd, new_access_flags, flags,
					    new_umem, iova, page_size);
			if (err) {
				ib_umem_release(new_umem);
				return ERR_PTR(err);
			}
			return NULL;
		}
		return create_real_mr(new_pd, new_umem, iova, new_access_flags, NULL);
	}

	/*
	 * Everything else has no state we can preserve, just create a new MR
	 * from scratch
	 */
recreate:
	return mlx5_ib_reg_user_mr(new_pd, start, length, iova,
				   new_access_flags, NULL, udata);
}

static int
mlx5_alloc_priv_descs(struct ib_device *device,
		      struct mlx5_ib_mr *mr,
		      int ndescs,
		      int desc_size)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	struct device *ddev = &dev->mdev->pdev->dev;
	int size = ndescs * desc_size;
	int add_size;
	int ret;

	add_size = max_t(int, MLX5_UMR_ALIGN - ARCH_KMALLOC_MINALIGN, 0);
	if (is_power_of_2(MLX5_UMR_ALIGN) && add_size) {
		int end = max_t(int, MLX5_UMR_ALIGN, roundup_pow_of_two(size));

		add_size = min_t(int, end - size, add_size);
	}

	mr->descs_alloc = kzalloc(size + add_size, GFP_KERNEL);
	if (!mr->descs_alloc)
		return -ENOMEM;

	mr->descs = PTR_ALIGN(mr->descs_alloc, MLX5_UMR_ALIGN);

	mr->desc_map = dma_map_single(ddev, mr->descs, size, DMA_TO_DEVICE);
	if (dma_mapping_error(ddev, mr->desc_map)) {
		ret = -ENOMEM;
		goto err;
	}

	return 0;
err:
	kfree(mr->descs_alloc);

	return ret;
}

static void
mlx5_free_priv_descs(struct mlx5_ib_mr *mr)
{
	if (!mr->umem && !mr->data_direct &&
	    mr->ibmr.type != IB_MR_TYPE_DM && mr->descs) {
		struct ib_device *device = mr->ibmr.device;
		int size = mr->max_descs * mr->desc_size;
		struct mlx5_ib_dev *dev = to_mdev(device);

		dma_unmap_single(&dev->mdev->pdev->dev, mr->desc_map, size,
				 DMA_TO_DEVICE);
		kfree(mr->descs_alloc);
		mr->descs = NULL;
	}
}

static int mlx5_ib_revoke_data_direct_mr(struct mlx5_ib_mr *mr)
{
	struct mlx5_ib_dev *dev = to_mdev(mr->ibmr.device);
	struct ib_umem_dmabuf *umem_dmabuf = to_ib_umem_dmabuf(mr->umem);
	int err;

	lockdep_assert_held(&dev->data_direct_lock);
	mr->revoked = true;
	err = mlx5r_umr_revoke_mr(mr);
	if (WARN_ON(err))
		return err;

	ib_umem_dmabuf_revoke(umem_dmabuf);
	return 0;
}

void mlx5_ib_revoke_data_direct_mrs(struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_mr *mr, *next;

	lockdep_assert_held(&dev->data_direct_lock);

	list_for_each_entry_safe(mr, next, &dev->data_direct_mr_list, dd_node) {
		list_del(&mr->dd_node);
		mlx5_ib_revoke_data_direct_mr(mr);
	}
}

static int mlx5_umr_revoke_mr_with_lock(struct mlx5_ib_mr *mr)
{
	bool is_odp_dma_buf = is_dmabuf_mr(mr) &&
			      !to_ib_umem_dmabuf(mr->umem)->pinned;
	bool is_odp = is_odp_mr(mr);
	int ret;

	if (is_odp)
		mutex_lock(&to_ib_umem_odp(mr->umem)->umem_mutex);

	if (is_odp_dma_buf)
		dma_resv_lock(to_ib_umem_dmabuf(mr->umem)->attach->dmabuf->resv,
			      NULL);

	ret = mlx5r_umr_revoke_mr(mr);

	if (is_odp) {
		if (!ret)
			to_ib_umem_odp(mr->umem)->private = NULL;
		mutex_unlock(&to_ib_umem_odp(mr->umem)->umem_mutex);
	}

	if (is_odp_dma_buf) {
		if (!ret)
			to_ib_umem_dmabuf(mr->umem)->private = NULL;
		dma_resv_unlock(
			to_ib_umem_dmabuf(mr->umem)->attach->dmabuf->resv);
	}

	return ret;
}

static int mlx5r_handle_mkey_cleanup(struct mlx5_ib_mr *mr)
{
	bool is_odp_dma_buf = is_dmabuf_mr(mr) &&
			      !to_ib_umem_dmabuf(mr->umem)->pinned;
	struct mlx5_ib_dev *dev = to_mdev(mr->ibmr.device);
	bool is_odp = is_odp_mr(mr);
	int ret;

	if (mr->ibmr.frmr.pool) {
		if (!mlx5_umr_revoke_mr_with_lock(mr)) {
			ib_frmr_pool_push(mr->ibmr.device, &mr->ibmr);
			return 0;
		}
	}

	if (is_odp)
		mutex_lock(&to_ib_umem_odp(mr->umem)->umem_mutex);

	if (is_odp_dma_buf)
		dma_resv_lock(to_ib_umem_dmabuf(mr->umem)->attach->dmabuf->resv,
			      NULL);
	ret = destroy_mkey(dev, mr);
	if (is_odp) {
		if (!ret)
			to_ib_umem_odp(mr->umem)->private = NULL;
		mutex_unlock(&to_ib_umem_odp(mr->umem)->umem_mutex);
	}

	if (is_odp_dma_buf) {
		if (!ret)
			to_ib_umem_dmabuf(mr->umem)->private = NULL;
		dma_resv_unlock(
			to_ib_umem_dmabuf(mr->umem)->attach->dmabuf->resv);
	}

	if (mr->ibmr.frmr.pool && !ret)
		ib_frmr_pool_drop(&mr->ibmr);

	return ret;
}

static int __mlx5_ib_dereg_mr(struct ib_mr *ibmr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_dev *dev = to_mdev(ibmr->device);
	int rc;

	/*
	 * Any async use of the mr must hold the refcount, once the refcount
	 * goes to zero no other thread, such as ODP page faults, prefetch, any
	 * UMR activity, etc can touch the mkey. Thus it is safe to destroy it.
	 */
	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING) &&
	    refcount_read(&mr->mmkey.usecount) != 0 &&
	    xa_erase(&mr_to_mdev(mr)->odp_mkeys, mlx5_base_mkey(mr->mmkey.key)))
		mlx5r_deref_wait_odp_mkey(&mr->mmkey);

	if (ibmr->type == IB_MR_TYPE_INTEGRITY) {
		xa_cmpxchg(&dev->sig_mrs, mlx5_base_mkey(mr->mmkey.key),
			   mr->sig, NULL, GFP_KERNEL);

		if (mr->mtt_mr) {
			rc = mlx5_ib_dereg_mr(&mr->mtt_mr->ibmr, NULL);
			if (rc)
				return rc;
			mr->mtt_mr = NULL;
		}
		if (mr->klm_mr) {
			rc = mlx5_ib_dereg_mr(&mr->klm_mr->ibmr, NULL);
			if (rc)
				return rc;
			mr->klm_mr = NULL;
		}

		if (mlx5_core_destroy_psv(dev->mdev,
					  mr->sig->psv_memory.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy mem psv %d\n",
				     mr->sig->psv_memory.psv_idx);
		if (mlx5_core_destroy_psv(dev->mdev, mr->sig->psv_wire.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy wire psv %d\n",
				     mr->sig->psv_wire.psv_idx);
		kfree(mr->sig);
		mr->sig = NULL;
	}

	/* Stop DMA */
	rc = mlx5r_handle_mkey_cleanup(mr);
	if (rc)
		return rc;

	if (mr->umem) {
		bool is_odp = is_odp_mr(mr);

		if (!is_odp)
			atomic_sub(ib_umem_num_pages(mr->umem),
				   &dev->mdev->priv.reg_pages);
		ib_umem_release(mr->umem);
		if (is_odp)
			mlx5_ib_free_odp_mr(mr);
	}

	if (!mr->ibmr.frmr.pool)
		mlx5_free_priv_descs(mr);

	kfree(mr);
	return 0;
}

static int dereg_crossing_data_direct_mr(struct mlx5_ib_dev *dev,
					struct mlx5_ib_mr *mr)
{
	struct mlx5_ib_mr *dd_crossed_mr = mr->dd_crossed_mr;
	int ret;

	ret = __mlx5_ib_dereg_mr(&mr->ibmr);
	if (ret)
		return ret;

	mutex_lock(&dev->data_direct_lock);
	if (!dd_crossed_mr->revoked)
		list_del(&dd_crossed_mr->dd_node);

	ret = __mlx5_ib_dereg_mr(&dd_crossed_mr->ibmr);
	mutex_unlock(&dev->data_direct_lock);
	return ret;
}

int mlx5_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_dev *dev = to_mdev(ibmr->device);

	if (mr->data_direct)
		return dereg_crossing_data_direct_mr(dev, mr);

	return __mlx5_ib_dereg_mr(ibmr);
}

static void mlx5_set_umr_free_mkey(struct ib_pd *pd, u32 *in, int ndescs,
				   int access_mode, int page_shift)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	/* This is only used from the kernel, so setting the PD is OK. */
	set_mkc_access_pd_addr_fields(mkc, IB_ACCESS_RELAXED_ORDERING, 0, pd);
	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, translations_octword_size, ndescs);
	MLX5_SET(mkc, mkc, access_mode_1_0, access_mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, (access_mode >> 2) & 0x7);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, log_page_size, page_shift);
	if (access_mode == MLX5_MKC_ACCESS_MODE_PA ||
	    access_mode == MLX5_MKC_ACCESS_MODE_MTT)
		MLX5_SET(mkc, mkc, ma_translation_mode, MLX5_CAP_GEN(dev->mdev, ats));
}

static int _mlx5_alloc_mkey_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				  int ndescs, int desc_size, int page_shift,
				  int access_mode, u32 *in, int inlen)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int err;

	mr->access_mode = access_mode;
	mr->desc_size = desc_size;
	mr->max_descs = ndescs;

	err = mlx5_alloc_priv_descs(pd->device, mr, ndescs, desc_size);
	if (err)
		return err;

	mlx5_set_umr_free_mkey(pd, in, ndescs, access_mode, page_shift);

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err)
		goto err_free_descs;

	mr->mmkey.type = MLX5_MKEY_MR;
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;

	return 0;

err_free_descs:
	mlx5_free_priv_descs(mr);
	return err;
}

static struct mlx5_ib_mr *mlx5_ib_alloc_pi_mr(struct ib_pd *pd,
				u32 max_num_sg, u32 max_num_meta_sg,
				int desc_size, int access_mode)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	int ndescs = ALIGN(max_num_sg + max_num_meta_sg, 4);
	int page_shift = 0;
	struct mlx5_ib_mr *mr;
	u32 *in;
	int err;

	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->ibmr.pd = pd;
	mr->ibmr.device = pd->device;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	if (access_mode == MLX5_MKC_ACCESS_MODE_MTT)
		page_shift = PAGE_SHIFT;

	err = _mlx5_alloc_mkey_descs(pd, mr, ndescs, desc_size, page_shift,
				     access_mode, in, inlen);
	if (err)
		goto err_free_in;

	mr->umem = NULL;
	kfree(in);

	return mr;

err_free_in:
	kfree(in);
err_free:
	kfree(mr);
	return ERR_PTR(err);
}

static int mlx5_alloc_mem_reg_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				    int ndescs, u32 *in, int inlen)
{
	return _mlx5_alloc_mkey_descs(pd, mr, ndescs, sizeof(struct mlx5_mtt),
				      PAGE_SHIFT, MLX5_MKC_ACCESS_MODE_MTT, in,
				      inlen);
}

static int mlx5_alloc_sg_gaps_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				    int ndescs, u32 *in, int inlen)
{
	return _mlx5_alloc_mkey_descs(pd, mr, ndescs, sizeof(struct mlx5_klm),
				      0, MLX5_MKC_ACCESS_MODE_KLMS, in, inlen);
}

static int mlx5_alloc_integrity_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				      int max_num_sg, int max_num_meta_sg,
				      u32 *in, int inlen)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	u32 psv_index[2];
	void *mkc;
	int err;

	mr->sig = kzalloc_obj(*mr->sig);
	if (!mr->sig)
		return -ENOMEM;

	/* create mem & wire PSVs */
	err = mlx5_core_create_psv(dev->mdev, to_mpd(pd)->pdn, 2, psv_index);
	if (err)
		goto err_free_sig;

	mr->sig->psv_memory.psv_idx = psv_index[0];
	mr->sig->psv_wire.psv_idx = psv_index[1];

	mr->sig->sig_status_checked = true;
	mr->sig->sig_err_exists = false;
	/* Next UMR, Arm SIGERR */
	++mr->sig->sigerr_count;
	mr->klm_mr = mlx5_ib_alloc_pi_mr(pd, max_num_sg, max_num_meta_sg,
					 sizeof(struct mlx5_klm),
					 MLX5_MKC_ACCESS_MODE_KLMS);
	if (IS_ERR(mr->klm_mr)) {
		err = PTR_ERR(mr->klm_mr);
		goto err_destroy_psv;
	}
	mr->mtt_mr = mlx5_ib_alloc_pi_mr(pd, max_num_sg, max_num_meta_sg,
					 sizeof(struct mlx5_mtt),
					 MLX5_MKC_ACCESS_MODE_MTT);
	if (IS_ERR(mr->mtt_mr)) {
		err = PTR_ERR(mr->mtt_mr);
		goto err_free_klm_mr;
	}

	/* Set bsf descriptors for mkey */
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, bsf_en, 1);
	MLX5_SET(mkc, mkc, bsf_octword_size, MLX5_MKEY_BSF_OCTO_SIZE);

	err = _mlx5_alloc_mkey_descs(pd, mr, 4, sizeof(struct mlx5_klm), 0,
				     MLX5_MKC_ACCESS_MODE_KLMS, in, inlen);
	if (err)
		goto err_free_mtt_mr;

	err = xa_err(xa_store(&dev->sig_mrs, mlx5_base_mkey(mr->mmkey.key),
			      mr->sig, GFP_KERNEL));
	if (err)
		goto err_free_descs;
	return 0;

err_free_descs:
	destroy_mkey(dev, mr);
	mlx5_free_priv_descs(mr);
err_free_mtt_mr:
	mlx5_ib_dereg_mr(&mr->mtt_mr->ibmr, NULL);
	mr->mtt_mr = NULL;
err_free_klm_mr:
	mlx5_ib_dereg_mr(&mr->klm_mr->ibmr, NULL);
	mr->klm_mr = NULL;
err_destroy_psv:
	if (mlx5_core_destroy_psv(dev->mdev, mr->sig->psv_memory.psv_idx))
		mlx5_ib_warn(dev, "failed to destroy mem psv %d\n",
			     mr->sig->psv_memory.psv_idx);
	if (mlx5_core_destroy_psv(dev->mdev, mr->sig->psv_wire.psv_idx))
		mlx5_ib_warn(dev, "failed to destroy wire psv %d\n",
			     mr->sig->psv_wire.psv_idx);
err_free_sig:
	kfree(mr->sig);

	return err;
}

static struct ib_mr *__mlx5_ib_alloc_mr(struct ib_pd *pd,
					enum ib_mr_type mr_type, u32 max_num_sg,
					u32 max_num_meta_sg)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	int ndescs = ALIGN(max_num_sg, 4);
	struct mlx5_ib_mr *mr;
	u32 *in;
	int err;

	mr = kzalloc_obj(*mr);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	mr->ibmr.device = pd->device;
	mr->umem = NULL;

	switch (mr_type) {
	case IB_MR_TYPE_MEM_REG:
		err = mlx5_alloc_mem_reg_descs(pd, mr, ndescs, in, inlen);
		break;
	case IB_MR_TYPE_SG_GAPS:
		err = mlx5_alloc_sg_gaps_descs(pd, mr, ndescs, in, inlen);
		break;
	case IB_MR_TYPE_INTEGRITY:
		err = mlx5_alloc_integrity_descs(pd, mr, max_num_sg,
						 max_num_meta_sg, in, inlen);
		break;
	default:
		mlx5_ib_warn(dev, "Invalid mr type %d\n", mr_type);
		err = -EINVAL;
	}

	if (err)
		goto err_free_in;

	kfree(in);

	return &mr->ibmr;

err_free_in:
	kfree(in);
err_free:
	kfree(mr);
	return ERR_PTR(err);
}

struct ib_mr *mlx5_ib_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			       u32 max_num_sg)
{
	return __mlx5_ib_alloc_mr(pd, mr_type, max_num_sg, 0);
}

struct ib_mr *mlx5_ib_alloc_mr_integrity(struct ib_pd *pd,
					 u32 max_num_sg, u32 max_num_meta_sg)
{
	return __mlx5_ib_alloc_mr(pd, IB_MR_TYPE_INTEGRITY, max_num_sg,
				  max_num_meta_sg);
}

int mlx5_ib_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(ibmw->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_ib_mw *mw = to_mmw(ibmw);
	unsigned int ndescs;
	u32 *in = NULL;
	void *mkc;
	int err;
	struct mlx5_ib_alloc_mw req = {};
	struct {
		__u32	comp_mask;
		__u32	response_length;
	} resp = {};

	if (udata->inlen) {
		err = ib_copy_validate_udata_in_cm(udata, req, reserved2, 0);
		if (err)
			return err;
	}

	if (req.reserved1 || req.reserved2)
		return -EOPNOTSUPP;

	ndescs = req.num_klms ? roundup(req.num_klms, 4) : roundup(1, 4);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, translations_octword_size, ndescs);
	MLX5_SET(mkc, mkc, pd, to_mpd(ibmw->pd)->pdn);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_KLMS);
	MLX5_SET(mkc, mkc, en_rinval, !!((ibmw->type == IB_MW_TYPE_2)));
	MLX5_SET(mkc, mkc, qpn, 0xffffff);

	err = mlx5_ib_create_mkey(dev, &mw->mmkey, in, inlen);
	if (err)
		goto free;

	mw->mmkey.type = MLX5_MKEY_MW;
	ibmw->rkey = mw->mmkey.key;
	mw->mmkey.ndescs = ndescs;

	resp.response_length =
		min(offsetofend(typeof(resp), response_length), udata->outlen);
	if (resp.response_length) {
		err = ib_respond_udata(udata, resp);
		if (err)
			goto free_mkey;
	}

	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING)) {
		err = mlx5r_store_odp_mkey(dev, &mw->mmkey);
		if (err)
			goto free_mkey;
	}

	kfree(in);
	return 0;

free_mkey:
	mlx5_core_destroy_mkey(dev->mdev, mw->mmkey.key);
free:
	kfree(in);
	return err;
}

int mlx5_ib_dealloc_mw(struct ib_mw *mw)
{
	struct mlx5_ib_dev *dev = to_mdev(mw->device);
	struct mlx5_ib_mw *mmw = to_mmw(mw);

	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING) &&
	    xa_erase(&dev->odp_mkeys, mlx5_base_mkey(mmw->mmkey.key)))
		/*
		 * pagefault_single_data_segment() may be accessing mmw
		 * if the user bound an ODP MR to this MW.
		 */
		mlx5r_deref_wait_odp_mkey(&mmw->mmkey);

	return mlx5_core_destroy_mkey(dev->mdev, mmw->mmkey.key);
}

int mlx5_ib_check_mr_status(struct ib_mr *ibmr, u32 check_mask,
			    struct ib_mr_status *mr_status)
{
	struct mlx5_ib_mr *mmr = to_mmr(ibmr);
	int ret = 0;

	if (check_mask & ~IB_MR_CHECK_SIG_STATUS) {
		pr_err("Invalid status check mask\n");
		ret = -EINVAL;
		goto done;
	}

	mr_status->fail_status = 0;
	if (check_mask & IB_MR_CHECK_SIG_STATUS) {
		if (!mmr->sig) {
			ret = -EINVAL;
			pr_err("signature status check requested on a non-signature enabled MR\n");
			goto done;
		}

		mmr->sig->sig_status_checked = true;
		if (!mmr->sig->sig_err_exists)
			goto done;

		if (ibmr->lkey == mmr->sig->err_item.key)
			memcpy(&mr_status->sig_err, &mmr->sig->err_item,
			       sizeof(mr_status->sig_err));
		else {
			mr_status->sig_err.err_type = IB_SIG_BAD_GUARD;
			mr_status->sig_err.sig_err_offset = 0;
			mr_status->sig_err.key = mmr->sig->err_item.key;
		}

		mmr->sig->sig_err_exists = false;
		mr_status->fail_status |= IB_MR_CHECK_SIG_STATUS;
	}

done:
	return ret;
}

static int
mlx5_ib_map_pa_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			int data_sg_nents, unsigned int *data_sg_offset,
			struct scatterlist *meta_sg, int meta_sg_nents,
			unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	unsigned int sg_offset = 0;
	int n = 0;

	mr->meta_length = 0;
	if (data_sg_nents == 1) {
		n++;
		mr->mmkey.ndescs = 1;
		if (data_sg_offset)
			sg_offset = *data_sg_offset;
		mr->data_length = sg_dma_len(data_sg) - sg_offset;
		mr->data_iova = sg_dma_address(data_sg) + sg_offset;
		if (meta_sg_nents == 1) {
			n++;
			mr->meta_ndescs = 1;
			if (meta_sg_offset)
				sg_offset = *meta_sg_offset;
			else
				sg_offset = 0;
			mr->meta_length = sg_dma_len(meta_sg) - sg_offset;
			mr->pi_iova = sg_dma_address(meta_sg) + sg_offset;
		}
		ibmr->length = mr->data_length + mr->meta_length;
	}

	return n;
}

static int
mlx5_ib_sg_to_klms(struct mlx5_ib_mr *mr,
		   struct scatterlist *sgl,
		   unsigned short sg_nents,
		   unsigned int *sg_offset_p,
		   struct scatterlist *meta_sgl,
		   unsigned short meta_sg_nents,
		   unsigned int *meta_sg_offset_p)
{
	struct scatterlist *sg = sgl;
	struct mlx5_klm *klms = mr->descs;
	unsigned int sg_offset = sg_offset_p ? *sg_offset_p : 0;
	u32 lkey = mr->ibmr.pd->local_dma_lkey;
	int i, j = 0;

	mr->ibmr.iova = sg_dma_address(sg) + sg_offset;
	mr->ibmr.length = 0;

	for_each_sg(sgl, sg, sg_nents, i) {
		if (unlikely(i >= mr->max_descs))
			break;
		klms[i].va = cpu_to_be64(sg_dma_address(sg) + sg_offset);
		klms[i].bcount = cpu_to_be32(sg_dma_len(sg) - sg_offset);
		klms[i].key = cpu_to_be32(lkey);
		mr->ibmr.length += sg_dma_len(sg) - sg_offset;

		sg_offset = 0;
	}

	if (sg_offset_p)
		*sg_offset_p = sg_offset;

	mr->mmkey.ndescs = i;
	mr->data_length = mr->ibmr.length;

	if (meta_sg_nents) {
		sg = meta_sgl;
		sg_offset = meta_sg_offset_p ? *meta_sg_offset_p : 0;
		for_each_sg(meta_sgl, sg, meta_sg_nents, j) {
			if (unlikely(i + j >= mr->max_descs))
				break;
			klms[i + j].va = cpu_to_be64(sg_dma_address(sg) +
						     sg_offset);
			klms[i + j].bcount = cpu_to_be32(sg_dma_len(sg) -
							 sg_offset);
			klms[i + j].key = cpu_to_be32(lkey);
			mr->ibmr.length += sg_dma_len(sg) - sg_offset;

			sg_offset = 0;
		}
		if (meta_sg_offset_p)
			*meta_sg_offset_p = sg_offset;

		mr->meta_ndescs = j;
		mr->meta_length = mr->ibmr.length - mr->data_length;
	}

	return i + j;
}

static int mlx5_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	__be64 *descs;

	if (unlikely(mr->mmkey.ndescs == mr->max_descs))
		return -ENOMEM;

	descs = mr->descs;
	descs[mr->mmkey.ndescs++] = cpu_to_be64(addr | MLX5_EN_RD | MLX5_EN_WR);

	return 0;
}

static int mlx5_set_page_pi(struct ib_mr *ibmr, u64 addr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	__be64 *descs;

	if (unlikely(mr->mmkey.ndescs + mr->meta_ndescs == mr->max_descs))
		return -ENOMEM;

	descs = mr->descs;
	descs[mr->mmkey.ndescs + mr->meta_ndescs++] =
		cpu_to_be64(addr | MLX5_EN_RD | MLX5_EN_WR);

	return 0;
}

static int
mlx5_ib_map_mtt_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			 int data_sg_nents, unsigned int *data_sg_offset,
			 struct scatterlist *meta_sg, int meta_sg_nents,
			 unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_mr *pi_mr = mr->mtt_mr;
	int n;

	pi_mr->mmkey.ndescs = 0;
	pi_mr->meta_ndescs = 0;
	pi_mr->meta_length = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, pi_mr->desc_map,
				   pi_mr->desc_size * pi_mr->max_descs,
				   DMA_TO_DEVICE);

	pi_mr->ibmr.page_size = ibmr->page_size;
	n = ib_sg_to_pages(&pi_mr->ibmr, data_sg, data_sg_nents, data_sg_offset,
			   mlx5_set_page);
	if (n != data_sg_nents)
		return n;

	pi_mr->data_iova = pi_mr->ibmr.iova;
	pi_mr->data_length = pi_mr->ibmr.length;
	pi_mr->ibmr.length = pi_mr->data_length;
	ibmr->length = pi_mr->data_length;

	if (meta_sg_nents) {
		u64 page_mask = ~((u64)ibmr->page_size - 1);
		u64 iova = pi_mr->data_iova;

		n += ib_sg_to_pages(&pi_mr->ibmr, meta_sg, meta_sg_nents,
				    meta_sg_offset, mlx5_set_page_pi);

		pi_mr->meta_length = pi_mr->ibmr.length;
		/*
		 * PI address for the HW is the offset of the metadata address
		 * relative to the first data page address.
		 * It equals to first data page address + size of data pages +
		 * metadata offset at the first metadata page
		 */
		pi_mr->pi_iova = (iova & page_mask) +
				 pi_mr->mmkey.ndescs * ibmr->page_size +
				 (pi_mr->ibmr.iova & ~page_mask);
		/*
		 * In order to use one MTT MR for data and metadata, we register
		 * also the gaps between the end of the data and the start of
		 * the metadata (the sig MR will verify that the HW will access
		 * to right addresses). This mapping is safe because we use
		 * internal mkey for the registration.
		 */
		pi_mr->ibmr.length = pi_mr->pi_iova + pi_mr->meta_length - iova;
		pi_mr->ibmr.iova = iova;
		ibmr->length += pi_mr->meta_length;
	}

	ib_dma_sync_single_for_device(ibmr->device, pi_mr->desc_map,
				      pi_mr->desc_size * pi_mr->max_descs,
				      DMA_TO_DEVICE);

	return n;
}

static int
mlx5_ib_map_klm_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			 int data_sg_nents, unsigned int *data_sg_offset,
			 struct scatterlist *meta_sg, int meta_sg_nents,
			 unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_mr *pi_mr = mr->klm_mr;
	int n;

	pi_mr->mmkey.ndescs = 0;
	pi_mr->meta_ndescs = 0;
	pi_mr->meta_length = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, pi_mr->desc_map,
				   pi_mr->desc_size * pi_mr->max_descs,
				   DMA_TO_DEVICE);

	n = mlx5_ib_sg_to_klms(pi_mr, data_sg, data_sg_nents, data_sg_offset,
			       meta_sg, meta_sg_nents, meta_sg_offset);

	ib_dma_sync_single_for_device(ibmr->device, pi_mr->desc_map,
				      pi_mr->desc_size * pi_mr->max_descs,
				      DMA_TO_DEVICE);

	/* This is zero-based memory region */
	pi_mr->data_iova = 0;
	pi_mr->ibmr.iova = 0;
	pi_mr->pi_iova = pi_mr->data_length;
	ibmr->length = pi_mr->ibmr.length;

	return n;
}

int mlx5_ib_map_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			 int data_sg_nents, unsigned int *data_sg_offset,
			 struct scatterlist *meta_sg, int meta_sg_nents,
			 unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_mr *pi_mr = NULL;
	int n;

	WARN_ON(ibmr->type != IB_MR_TYPE_INTEGRITY);

	mr->mmkey.ndescs = 0;
	mr->data_length = 0;
	mr->data_iova = 0;
	mr->meta_ndescs = 0;
	mr->pi_iova = 0;
	/*
	 * As a performance optimization, if possible, there is no need to
	 * perform UMR operation to register the data/metadata buffers.
	 * First try to map the sg lists to PA descriptors with local_dma_lkey.
	 * Fallback to UMR only in case of a failure.
	 */
	n = mlx5_ib_map_pa_mr_sg_pi(ibmr, data_sg, data_sg_nents,
				    data_sg_offset, meta_sg, meta_sg_nents,
				    meta_sg_offset);
	if (n == data_sg_nents + meta_sg_nents)
		goto out;
	/*
	 * As a performance optimization, if possible, there is no need to map
	 * the sg lists to KLM descriptors. First try to map the sg lists to MTT
	 * descriptors and fallback to KLM only in case of a failure.
	 * It's more efficient for the HW to work with MTT descriptors
	 * (especially in high load).
	 * Use KLM (indirect access) only if it's mandatory.
	 */
	pi_mr = mr->mtt_mr;
	n = mlx5_ib_map_mtt_mr_sg_pi(ibmr, data_sg, data_sg_nents,
				     data_sg_offset, meta_sg, meta_sg_nents,
				     meta_sg_offset);
	if (n == data_sg_nents + meta_sg_nents)
		goto out;

	pi_mr = mr->klm_mr;
	n = mlx5_ib_map_klm_mr_sg_pi(ibmr, data_sg, data_sg_nents,
				     data_sg_offset, meta_sg, meta_sg_nents,
				     meta_sg_offset);
	if (unlikely(n != data_sg_nents + meta_sg_nents))
		return -ENOMEM;

out:
	/* This is zero-based memory region */
	ibmr->iova = 0;
	mr->pi_mr = pi_mr;
	if (pi_mr)
		ibmr->sig_attrs->meta_length = pi_mr->meta_length;
	else
		ibmr->sig_attrs->meta_length = mr->meta_length;

	return 0;
}

int mlx5_ib_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		      unsigned int *sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	int n;

	mr->mmkey.ndescs = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, mr->desc_map,
				   mr->desc_size * mr->max_descs,
				   DMA_TO_DEVICE);

	if (mr->access_mode == MLX5_MKC_ACCESS_MODE_KLMS)
		n = mlx5_ib_sg_to_klms(mr, sg, sg_nents, sg_offset, NULL, 0,
				       NULL);
	else
		n = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset,
				mlx5_set_page);

	ib_dma_sync_single_for_device(ibmr->device, mr->desc_map,
				      mr->desc_size * mr->max_descs,
				      DMA_TO_DEVICE);

	return n;
}
