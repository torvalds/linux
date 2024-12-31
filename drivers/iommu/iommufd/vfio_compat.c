// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/file.h>
#include <linux/interval_tree.h>
#include <linux/iommu.h>
#include <linux/iommufd.h>
#include <linux/slab.h>
#include <linux/vfio.h>
#include <uapi/linux/vfio.h>
#include <uapi/linux/iommufd.h>

#include "iommufd_private.h"

static struct iommufd_ioas *get_compat_ioas(struct iommufd_ctx *ictx)
{
	struct iommufd_ioas *ioas = ERR_PTR(-ENODEV);

	xa_lock(&ictx->objects);
	if (!ictx->vfio_ioas || !iommufd_lock_obj(&ictx->vfio_ioas->obj))
		goto out_unlock;
	ioas = ictx->vfio_ioas;
out_unlock:
	xa_unlock(&ictx->objects);
	return ioas;
}

/**
 * iommufd_vfio_compat_ioas_get_id - Ensure a compat IOAS exists
 * @ictx: Context to operate on
 * @out_ioas_id: The IOAS ID of the compatibility IOAS
 *
 * Return the ID of the current compatibility IOAS. The ID can be passed into
 * other functions that take an ioas_id.
 */
int iommufd_vfio_compat_ioas_get_id(struct iommufd_ctx *ictx, u32 *out_ioas_id)
{
	struct iommufd_ioas *ioas;

	ioas = get_compat_ioas(ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);
	*out_ioas_id = ioas->obj.id;
	iommufd_put_object(ictx, &ioas->obj);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(iommufd_vfio_compat_ioas_get_id, "IOMMUFD_VFIO");

/**
 * iommufd_vfio_compat_set_no_iommu - Called when a no-iommu device is attached
 * @ictx: Context to operate on
 *
 * This allows selecting the VFIO_NOIOMMU_IOMMU and blocks normal types.
 */
int iommufd_vfio_compat_set_no_iommu(struct iommufd_ctx *ictx)
{
	int ret;

	xa_lock(&ictx->objects);
	if (!ictx->vfio_ioas) {
		ictx->no_iommu_mode = 1;
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	xa_unlock(&ictx->objects);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(iommufd_vfio_compat_set_no_iommu, "IOMMUFD_VFIO");

/**
 * iommufd_vfio_compat_ioas_create - Ensure the compat IOAS is created
 * @ictx: Context to operate on
 *
 * The compatibility IOAS is the IOAS that the vfio compatibility ioctls operate
 * on since they do not have an IOAS ID input in their ABI. Only attaching a
 * group should cause a default creation of the internal ioas, this does nothing
 * if an existing ioas has already been assigned somehow.
 */
int iommufd_vfio_compat_ioas_create(struct iommufd_ctx *ictx)
{
	struct iommufd_ioas *ioas = NULL;
	int ret;

	ioas = iommufd_ioas_alloc(ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	xa_lock(&ictx->objects);
	/*
	 * VFIO won't allow attaching a container to both iommu and no iommu
	 * operation
	 */
	if (ictx->no_iommu_mode) {
		ret = -EINVAL;
		goto out_abort;
	}

	if (ictx->vfio_ioas && iommufd_lock_obj(&ictx->vfio_ioas->obj)) {
		ret = 0;
		iommufd_put_object(ictx, &ictx->vfio_ioas->obj);
		goto out_abort;
	}
	ictx->vfio_ioas = ioas;
	xa_unlock(&ictx->objects);

	/*
	 * An automatically created compat IOAS is treated as a userspace
	 * created object. Userspace can learn the ID via IOMMU_VFIO_IOAS_GET,
	 * and if not manually destroyed it will be destroyed automatically
	 * at iommufd release.
	 */
	iommufd_object_finalize(ictx, &ioas->obj);
	return 0;

out_abort:
	xa_unlock(&ictx->objects);
	iommufd_object_abort(ictx, &ioas->obj);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(iommufd_vfio_compat_ioas_create, "IOMMUFD_VFIO");

int iommufd_vfio_ioas(struct iommufd_ucmd *ucmd)
{
	struct iommu_vfio_ioas *cmd = ucmd->cmd;
	struct iommufd_ioas *ioas;

	if (cmd->__reserved)
		return -EOPNOTSUPP;
	switch (cmd->op) {
	case IOMMU_VFIO_IOAS_GET:
		ioas = get_compat_ioas(ucmd->ictx);
		if (IS_ERR(ioas))
			return PTR_ERR(ioas);
		cmd->ioas_id = ioas->obj.id;
		iommufd_put_object(ucmd->ictx, &ioas->obj);
		return iommufd_ucmd_respond(ucmd, sizeof(*cmd));

	case IOMMU_VFIO_IOAS_SET:
		ioas = iommufd_get_ioas(ucmd->ictx, cmd->ioas_id);
		if (IS_ERR(ioas))
			return PTR_ERR(ioas);
		xa_lock(&ucmd->ictx->objects);
		ucmd->ictx->vfio_ioas = ioas;
		xa_unlock(&ucmd->ictx->objects);
		iommufd_put_object(ucmd->ictx, &ioas->obj);
		return 0;

	case IOMMU_VFIO_IOAS_CLEAR:
		xa_lock(&ucmd->ictx->objects);
		ucmd->ictx->vfio_ioas = NULL;
		xa_unlock(&ucmd->ictx->objects);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int iommufd_vfio_map_dma(struct iommufd_ctx *ictx, unsigned int cmd,
				void __user *arg)
{
	u32 supported_flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
	size_t minsz = offsetofend(struct vfio_iommu_type1_dma_map, size);
	struct vfio_iommu_type1_dma_map map;
	int iommu_prot = IOMMU_CACHE;
	struct iommufd_ioas *ioas;
	unsigned long iova;
	int rc;

	if (copy_from_user(&map, arg, minsz))
		return -EFAULT;

	if (map.argsz < minsz || map.flags & ~supported_flags)
		return -EINVAL;

	if (map.flags & VFIO_DMA_MAP_FLAG_READ)
		iommu_prot |= IOMMU_READ;
	if (map.flags & VFIO_DMA_MAP_FLAG_WRITE)
		iommu_prot |= IOMMU_WRITE;

	ioas = get_compat_ioas(ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	/*
	 * Maps created through the legacy interface always use VFIO compatible
	 * rlimit accounting. If the user wishes to use the faster user based
	 * rlimit accounting then they must use the new interface.
	 */
	iova = map.iova;
	rc = iopt_map_user_pages(ictx, &ioas->iopt, &iova, u64_to_user_ptr(map.vaddr),
				 map.size, iommu_prot, 0);
	iommufd_put_object(ictx, &ioas->obj);
	return rc;
}

static int iommufd_vfio_unmap_dma(struct iommufd_ctx *ictx, unsigned int cmd,
				  void __user *arg)
{
	size_t minsz = offsetofend(struct vfio_iommu_type1_dma_unmap, size);
	/*
	 * VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP is obsoleted by the new
	 * dirty tracking direction:
	 *  https://lore.kernel.org/kvm/20220731125503.142683-1-yishaih@nvidia.com/
	 *  https://lore.kernel.org/kvm/20220428210933.3583-1-joao.m.martins@oracle.com/
	 */
	u32 supported_flags = VFIO_DMA_UNMAP_FLAG_ALL;
	struct vfio_iommu_type1_dma_unmap unmap;
	unsigned long unmapped = 0;
	struct iommufd_ioas *ioas;
	int rc;

	if (copy_from_user(&unmap, arg, minsz))
		return -EFAULT;

	if (unmap.argsz < minsz || unmap.flags & ~supported_flags)
		return -EINVAL;

	ioas = get_compat_ioas(ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	if (unmap.flags & VFIO_DMA_UNMAP_FLAG_ALL) {
		if (unmap.iova != 0 || unmap.size != 0) {
			rc = -EINVAL;
			goto err_put;
		}
		rc = iopt_unmap_all(&ioas->iopt, &unmapped);
	} else {
		if (READ_ONCE(ioas->iopt.disable_large_pages)) {
			/*
			 * Create cuts at the start and last of the requested
			 * range. If the start IOVA is 0 then it doesn't need to
			 * be cut.
			 */
			unsigned long iovas[] = { unmap.iova + unmap.size - 1,
						  unmap.iova - 1 };

			rc = iopt_cut_iova(&ioas->iopt, iovas,
					   unmap.iova ? 2 : 1);
			if (rc)
				goto err_put;
		}
		rc = iopt_unmap_iova(&ioas->iopt, unmap.iova, unmap.size,
				     &unmapped);
	}
	unmap.size = unmapped;
	if (copy_to_user(arg, &unmap, minsz))
		rc = -EFAULT;

err_put:
	iommufd_put_object(ictx, &ioas->obj);
	return rc;
}

static int iommufd_vfio_cc_iommu(struct iommufd_ctx *ictx)
{
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommufd_ioas *ioas;
	int rc = 1;

	ioas = get_compat_ioas(ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	mutex_lock(&ioas->mutex);
	list_for_each_entry(hwpt_paging, &ioas->hwpt_list, hwpt_item) {
		if (!hwpt_paging->enforce_cache_coherency) {
			rc = 0;
			break;
		}
	}
	mutex_unlock(&ioas->mutex);

	iommufd_put_object(ictx, &ioas->obj);
	return rc;
}

static int iommufd_vfio_check_extension(struct iommufd_ctx *ictx,
					unsigned long type)
{
	switch (type) {
	case VFIO_TYPE1_IOMMU:
	case VFIO_TYPE1v2_IOMMU:
	case VFIO_UNMAP_ALL:
		return 1;

	case VFIO_NOIOMMU_IOMMU:
		return IS_ENABLED(CONFIG_VFIO_NOIOMMU);

	case VFIO_DMA_CC_IOMMU:
		return iommufd_vfio_cc_iommu(ictx);

	case __VFIO_RESERVED_TYPE1_NESTING_IOMMU:
		return 0;

	/*
	 * VFIO_DMA_MAP_FLAG_VADDR
	 * https://lore.kernel.org/kvm/1611939252-7240-1-git-send-email-steven.sistare@oracle.com/
	 * https://lore.kernel.org/all/Yz777bJZjTyLrHEQ@nvidia.com/
	 *
	 * It is hard to see how this could be implemented safely.
	 */
	case VFIO_UPDATE_VADDR:
	default:
		return 0;
	}
}

static int iommufd_vfio_set_iommu(struct iommufd_ctx *ictx, unsigned long type)
{
	bool no_iommu_mode = READ_ONCE(ictx->no_iommu_mode);
	struct iommufd_ioas *ioas = NULL;
	int rc = 0;

	/*
	 * Emulation for NOIOMMU is imperfect in that VFIO blocks almost all
	 * other ioctls. We let them keep working but they mostly fail since no
	 * IOAS should exist.
	 */
	if (IS_ENABLED(CONFIG_VFIO_NOIOMMU) && type == VFIO_NOIOMMU_IOMMU &&
	    no_iommu_mode) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		return 0;
	}

	if ((type != VFIO_TYPE1_IOMMU && type != VFIO_TYPE1v2_IOMMU) ||
	    no_iommu_mode)
		return -EINVAL;

	/* VFIO fails the set_iommu if there is no group */
	ioas = get_compat_ioas(ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	/*
	 * The difference between TYPE1 and TYPE1v2 is the ability to unmap in
	 * the middle of mapped ranges. This is complicated by huge page support
	 * which creates single large IOPTEs that cannot be split by the iommu
	 * driver. TYPE1 is very old at this point and likely nothing uses it,
	 * however it is simple enough to emulate by simply disabling the
	 * problematic large IOPTEs. Then we can safely unmap within any range.
	 */
	if (type == VFIO_TYPE1_IOMMU)
		rc = iopt_disable_large_pages(&ioas->iopt);
	iommufd_put_object(ictx, &ioas->obj);
	return rc;
}

static unsigned long iommufd_get_pagesizes(struct iommufd_ioas *ioas)
{
	struct io_pagetable *iopt = &ioas->iopt;
	unsigned long pgsize_bitmap = ULONG_MAX;
	struct iommu_domain *domain;
	unsigned long index;

	down_read(&iopt->domains_rwsem);
	xa_for_each(&iopt->domains, index, domain)
		pgsize_bitmap &= domain->pgsize_bitmap;

	/* See vfio_update_pgsize_bitmap() */
	if (pgsize_bitmap & ~PAGE_MASK) {
		pgsize_bitmap &= PAGE_MASK;
		pgsize_bitmap |= PAGE_SIZE;
	}
	pgsize_bitmap = max(pgsize_bitmap, ioas->iopt.iova_alignment);
	up_read(&iopt->domains_rwsem);
	return pgsize_bitmap;
}

static int iommufd_fill_cap_iova(struct iommufd_ioas *ioas,
				 struct vfio_info_cap_header __user *cur,
				 size_t avail)
{
	struct vfio_iommu_type1_info_cap_iova_range __user *ucap_iovas =
		container_of(cur,
			     struct vfio_iommu_type1_info_cap_iova_range __user,
			     header);
	struct vfio_iommu_type1_info_cap_iova_range cap_iovas = {
		.header = {
			.id = VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE,
			.version = 1,
		},
	};
	struct interval_tree_span_iter span;

	interval_tree_for_each_span(&span, &ioas->iopt.reserved_itree, 0,
				    ULONG_MAX) {
		struct vfio_iova_range range;

		if (!span.is_hole)
			continue;
		range.start = span.start_hole;
		range.end = span.last_hole;
		if (avail >= struct_size(&cap_iovas, iova_ranges,
					 cap_iovas.nr_iovas + 1) &&
		    copy_to_user(&ucap_iovas->iova_ranges[cap_iovas.nr_iovas],
				 &range, sizeof(range)))
			return -EFAULT;
		cap_iovas.nr_iovas++;
	}
	if (avail >= struct_size(&cap_iovas, iova_ranges, cap_iovas.nr_iovas) &&
	    copy_to_user(ucap_iovas, &cap_iovas, sizeof(cap_iovas)))
		return -EFAULT;
	return struct_size(&cap_iovas, iova_ranges, cap_iovas.nr_iovas);
}

static int iommufd_fill_cap_dma_avail(struct iommufd_ioas *ioas,
				      struct vfio_info_cap_header __user *cur,
				      size_t avail)
{
	struct vfio_iommu_type1_info_dma_avail cap_dma = {
		.header = {
			.id = VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL,
			.version = 1,
		},
		/*
		 * iommufd's limit is based on the cgroup's memory limit.
		 * Normally vfio would return U16_MAX here, and provide a module
		 * parameter to adjust it. Since S390 qemu userspace actually
		 * pays attention and needs a value bigger than U16_MAX return
		 * U32_MAX.
		 */
		.avail = U32_MAX,
	};

	if (avail >= sizeof(cap_dma) &&
	    copy_to_user(cur, &cap_dma, sizeof(cap_dma)))
		return -EFAULT;
	return sizeof(cap_dma);
}

static int iommufd_vfio_iommu_get_info(struct iommufd_ctx *ictx,
				       void __user *arg)
{
	typedef int (*fill_cap_fn)(struct iommufd_ioas *ioas,
				   struct vfio_info_cap_header __user *cur,
				   size_t avail);
	static const fill_cap_fn fill_fns[] = {
		iommufd_fill_cap_dma_avail,
		iommufd_fill_cap_iova,
	};
	size_t minsz = offsetofend(struct vfio_iommu_type1_info, iova_pgsizes);
	struct vfio_info_cap_header __user *last_cap = NULL;
	struct vfio_iommu_type1_info info = {};
	struct iommufd_ioas *ioas;
	size_t total_cap_size;
	int rc;
	int i;

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;
	minsz = min_t(size_t, info.argsz, sizeof(info));

	ioas = get_compat_ioas(ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	info.flags = VFIO_IOMMU_INFO_PGSIZES;
	info.iova_pgsizes = iommufd_get_pagesizes(ioas);
	info.cap_offset = 0;

	down_read(&ioas->iopt.iova_rwsem);
	total_cap_size = sizeof(info);
	for (i = 0; i != ARRAY_SIZE(fill_fns); i++) {
		int cap_size;

		if (info.argsz > total_cap_size)
			cap_size = fill_fns[i](ioas, arg + total_cap_size,
					       info.argsz - total_cap_size);
		else
			cap_size = fill_fns[i](ioas, NULL, 0);
		if (cap_size < 0) {
			rc = cap_size;
			goto out_put;
		}
		cap_size = ALIGN(cap_size, sizeof(u64));

		if (last_cap && info.argsz >= total_cap_size &&
		    put_user(total_cap_size, &last_cap->next)) {
			rc = -EFAULT;
			goto out_put;
		}
		last_cap = arg + total_cap_size;
		total_cap_size += cap_size;
	}

	/*
	 * If the user did not provide enough space then only some caps are
	 * returned and the argsz will be updated to the correct amount to get
	 * all caps.
	 */
	if (info.argsz >= total_cap_size)
		info.cap_offset = sizeof(info);
	info.argsz = total_cap_size;
	info.flags |= VFIO_IOMMU_INFO_CAPS;
	if (copy_to_user(arg, &info, minsz)) {
		rc = -EFAULT;
		goto out_put;
	}
	rc = 0;

out_put:
	up_read(&ioas->iopt.iova_rwsem);
	iommufd_put_object(ictx, &ioas->obj);
	return rc;
}

int iommufd_vfio_ioctl(struct iommufd_ctx *ictx, unsigned int cmd,
		       unsigned long arg)
{
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case VFIO_GET_API_VERSION:
		return VFIO_API_VERSION;
	case VFIO_SET_IOMMU:
		return iommufd_vfio_set_iommu(ictx, arg);
	case VFIO_CHECK_EXTENSION:
		return iommufd_vfio_check_extension(ictx, arg);
	case VFIO_IOMMU_GET_INFO:
		return iommufd_vfio_iommu_get_info(ictx, uarg);
	case VFIO_IOMMU_MAP_DMA:
		return iommufd_vfio_map_dma(ictx, cmd, uarg);
	case VFIO_IOMMU_UNMAP_DMA:
		return iommufd_vfio_unmap_dma(ictx, cmd, uarg);
	case VFIO_IOMMU_DIRTY_PAGES:
	default:
		return -ENOIOCTLCMD;
	}
	return -ENOIOCTLCMD;
}
