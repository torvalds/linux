// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 *
 * Kernel side components to support tools/testing/selftests/iommu
 */
#include <linux/anon_inodes.h>
#include <linux/debugfs.h>
#include <linux/fault-inject.h>
#include <linux/file.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <uapi/linux/iommufd.h>

#include "../iommu-priv.h"
#include "io_pagetable.h"
#include "iommufd_private.h"
#include "iommufd_test.h"

static DECLARE_FAULT_ATTR(fail_iommufd);
static struct dentry *dbgfs_root;
static struct platform_device *selftest_iommu_dev;
static const struct iommu_ops mock_ops;
static struct iommu_domain_ops domain_nested_ops;

size_t iommufd_test_memory_limit = 65536;

struct mock_bus_type {
	struct bus_type bus;
	struct notifier_block nb;
};

static struct mock_bus_type iommufd_mock_bus_type = {
	.bus = {
		.name = "iommufd_mock",
	},
};

static DEFINE_IDA(mock_dev_ida);

enum {
	MOCK_DIRTY_TRACK = 1,
	MOCK_IO_PAGE_SIZE = PAGE_SIZE / 2,
	MOCK_HUGE_PAGE_SIZE = 512 * MOCK_IO_PAGE_SIZE,

	/*
	 * Like a real page table alignment requires the low bits of the address
	 * to be zero. xarray also requires the high bit to be zero, so we store
	 * the pfns shifted. The upper bits are used for metadata.
	 */
	MOCK_PFN_MASK = ULONG_MAX / MOCK_IO_PAGE_SIZE,

	_MOCK_PFN_START = MOCK_PFN_MASK + 1,
	MOCK_PFN_START_IOVA = _MOCK_PFN_START,
	MOCK_PFN_LAST_IOVA = _MOCK_PFN_START,
	MOCK_PFN_DIRTY_IOVA = _MOCK_PFN_START << 1,
	MOCK_PFN_HUGE_IOVA = _MOCK_PFN_START << 2,
};

/*
 * Syzkaller has trouble randomizing the correct iova to use since it is linked
 * to the map ioctl's output, and it has no ide about that. So, simplify things.
 * In syzkaller mode the 64 bit IOVA is converted into an nth area and offset
 * value. This has a much smaller randomization space and syzkaller can hit it.
 */
static unsigned long __iommufd_test_syz_conv_iova(struct io_pagetable *iopt,
						  u64 *iova)
{
	struct syz_layout {
		__u32 nth_area;
		__u32 offset;
	};
	struct syz_layout *syz = (void *)iova;
	unsigned int nth = syz->nth_area;
	struct iopt_area *area;

	down_read(&iopt->iova_rwsem);
	for (area = iopt_area_iter_first(iopt, 0, ULONG_MAX); area;
	     area = iopt_area_iter_next(area, 0, ULONG_MAX)) {
		if (nth == 0) {
			up_read(&iopt->iova_rwsem);
			return iopt_area_iova(area) + syz->offset;
		}
		nth--;
	}
	up_read(&iopt->iova_rwsem);

	return 0;
}

static unsigned long iommufd_test_syz_conv_iova(struct iommufd_access *access,
						u64 *iova)
{
	unsigned long ret;

	mutex_lock(&access->ioas_lock);
	if (!access->ioas) {
		mutex_unlock(&access->ioas_lock);
		return 0;
	}
	ret = __iommufd_test_syz_conv_iova(&access->ioas->iopt, iova);
	mutex_unlock(&access->ioas_lock);
	return ret;
}

void iommufd_test_syz_conv_iova_id(struct iommufd_ucmd *ucmd,
				   unsigned int ioas_id, u64 *iova, u32 *flags)
{
	struct iommufd_ioas *ioas;

	if (!(*flags & MOCK_FLAGS_ACCESS_SYZ))
		return;
	*flags &= ~(u32)MOCK_FLAGS_ACCESS_SYZ;

	ioas = iommufd_get_ioas(ucmd->ictx, ioas_id);
	if (IS_ERR(ioas))
		return;
	*iova = __iommufd_test_syz_conv_iova(&ioas->iopt, iova);
	iommufd_put_object(ucmd->ictx, &ioas->obj);
}

struct mock_iommu_domain {
	unsigned long flags;
	struct iommu_domain domain;
	struct xarray pfns;
};

static inline struct mock_iommu_domain *
to_mock_domain(struct iommu_domain *domain)
{
	return container_of(domain, struct mock_iommu_domain, domain);
}

struct mock_iommu_domain_nested {
	struct iommu_domain domain;
	struct mock_viommu *mock_viommu;
	struct mock_iommu_domain *parent;
	u32 iotlb[MOCK_NESTED_DOMAIN_IOTLB_NUM];
};

static inline struct mock_iommu_domain_nested *
to_mock_nested(struct iommu_domain *domain)
{
	return container_of(domain, struct mock_iommu_domain_nested, domain);
}

struct mock_viommu {
	struct iommufd_viommu core;
	struct mock_iommu_domain *s2_parent;
};

static inline struct mock_viommu *to_mock_viommu(struct iommufd_viommu *viommu)
{
	return container_of(viommu, struct mock_viommu, core);
}

enum selftest_obj_type {
	TYPE_IDEV,
};

struct mock_dev {
	struct device dev;
	unsigned long flags;
	int id;
	u32 cache[MOCK_DEV_CACHE_NUM];
};

static inline struct mock_dev *to_mock_dev(struct device *dev)
{
	return container_of(dev, struct mock_dev, dev);
}

struct selftest_obj {
	struct iommufd_object obj;
	enum selftest_obj_type type;

	union {
		struct {
			struct iommufd_device *idev;
			struct iommufd_ctx *ictx;
			struct mock_dev *mock_dev;
		} idev;
	};
};

static inline struct selftest_obj *to_selftest_obj(struct iommufd_object *obj)
{
	return container_of(obj, struct selftest_obj, obj);
}

static int mock_domain_nop_attach(struct iommu_domain *domain,
				  struct device *dev)
{
	struct mock_dev *mdev = to_mock_dev(dev);

	if (domain->dirty_ops && (mdev->flags & MOCK_FLAGS_DEVICE_NO_DIRTY))
		return -EINVAL;

	return 0;
}

static const struct iommu_domain_ops mock_blocking_ops = {
	.attach_dev = mock_domain_nop_attach,
};

static struct iommu_domain mock_blocking_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &mock_blocking_ops,
};

static void *mock_domain_hw_info(struct device *dev, u32 *length, u32 *type)
{
	struct iommu_test_hw_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->test_reg = IOMMU_HW_INFO_SELFTEST_REGVAL;
	*length = sizeof(*info);
	*type = IOMMU_HW_INFO_TYPE_SELFTEST;

	return info;
}

static int mock_domain_set_dirty_tracking(struct iommu_domain *domain,
					  bool enable)
{
	struct mock_iommu_domain *mock = to_mock_domain(domain);
	unsigned long flags = mock->flags;

	if (enable && !domain->dirty_ops)
		return -EINVAL;

	/* No change? */
	if (!(enable ^ !!(flags & MOCK_DIRTY_TRACK)))
		return 0;

	flags = (enable ? flags | MOCK_DIRTY_TRACK : flags & ~MOCK_DIRTY_TRACK);

	mock->flags = flags;
	return 0;
}

static bool mock_test_and_clear_dirty(struct mock_iommu_domain *mock,
				      unsigned long iova, size_t page_size,
				      unsigned long flags)
{
	unsigned long cur, end = iova + page_size - 1;
	bool dirty = false;
	void *ent, *old;

	for (cur = iova; cur < end; cur += MOCK_IO_PAGE_SIZE) {
		ent = xa_load(&mock->pfns, cur / MOCK_IO_PAGE_SIZE);
		if (!ent || !(xa_to_value(ent) & MOCK_PFN_DIRTY_IOVA))
			continue;

		dirty = true;
		/* Clear dirty */
		if (!(flags & IOMMU_DIRTY_NO_CLEAR)) {
			unsigned long val;

			val = xa_to_value(ent) & ~MOCK_PFN_DIRTY_IOVA;
			old = xa_store(&mock->pfns, cur / MOCK_IO_PAGE_SIZE,
				       xa_mk_value(val), GFP_KERNEL);
			WARN_ON_ONCE(ent != old);
		}
	}

	return dirty;
}

static int mock_domain_read_and_clear_dirty(struct iommu_domain *domain,
					    unsigned long iova, size_t size,
					    unsigned long flags,
					    struct iommu_dirty_bitmap *dirty)
{
	struct mock_iommu_domain *mock = to_mock_domain(domain);
	unsigned long end = iova + size;
	void *ent;

	if (!(mock->flags & MOCK_DIRTY_TRACK) && dirty->bitmap)
		return -EINVAL;

	do {
		unsigned long pgsize = MOCK_IO_PAGE_SIZE;
		unsigned long head;

		ent = xa_load(&mock->pfns, iova / MOCK_IO_PAGE_SIZE);
		if (!ent) {
			iova += pgsize;
			continue;
		}

		if (xa_to_value(ent) & MOCK_PFN_HUGE_IOVA)
			pgsize = MOCK_HUGE_PAGE_SIZE;
		head = iova & ~(pgsize - 1);

		/* Clear dirty */
		if (mock_test_and_clear_dirty(mock, head, pgsize, flags))
			iommu_dirty_bitmap_record(dirty, iova, pgsize);
		iova += pgsize;
	} while (iova < end);

	return 0;
}

static const struct iommu_dirty_ops dirty_ops = {
	.set_dirty_tracking = mock_domain_set_dirty_tracking,
	.read_and_clear_dirty = mock_domain_read_and_clear_dirty,
};

static struct iommu_domain *mock_domain_alloc_paging(struct device *dev)
{
	struct mock_dev *mdev = to_mock_dev(dev);
	struct mock_iommu_domain *mock;

	mock = kzalloc(sizeof(*mock), GFP_KERNEL);
	if (!mock)
		return NULL;
	mock->domain.geometry.aperture_start = MOCK_APERTURE_START;
	mock->domain.geometry.aperture_end = MOCK_APERTURE_LAST;
	mock->domain.pgsize_bitmap = MOCK_IO_PAGE_SIZE;
	if (dev && mdev->flags & MOCK_FLAGS_DEVICE_HUGE_IOVA)
		mock->domain.pgsize_bitmap |= MOCK_HUGE_PAGE_SIZE;
	mock->domain.ops = mock_ops.default_domain_ops;
	mock->domain.type = IOMMU_DOMAIN_UNMANAGED;
	xa_init(&mock->pfns);
	return &mock->domain;
}

static struct mock_iommu_domain_nested *
__mock_domain_alloc_nested(const struct iommu_user_data *user_data)
{
	struct mock_iommu_domain_nested *mock_nested;
	struct iommu_hwpt_selftest user_cfg;
	int rc, i;

	if (user_data->type != IOMMU_HWPT_DATA_SELFTEST)
		return ERR_PTR(-EOPNOTSUPP);

	rc = iommu_copy_struct_from_user(&user_cfg, user_data,
					 IOMMU_HWPT_DATA_SELFTEST, iotlb);
	if (rc)
		return ERR_PTR(rc);

	mock_nested = kzalloc(sizeof(*mock_nested), GFP_KERNEL);
	if (!mock_nested)
		return ERR_PTR(-ENOMEM);
	mock_nested->domain.ops = &domain_nested_ops;
	mock_nested->domain.type = IOMMU_DOMAIN_NESTED;
	for (i = 0; i < MOCK_NESTED_DOMAIN_IOTLB_NUM; i++)
		mock_nested->iotlb[i] = user_cfg.iotlb;
	return mock_nested;
}

static struct iommu_domain *
mock_domain_alloc_nested(struct iommu_domain *parent, u32 flags,
			 const struct iommu_user_data *user_data)
{
	struct mock_iommu_domain_nested *mock_nested;
	struct mock_iommu_domain *mock_parent;

	if (flags)
		return ERR_PTR(-EOPNOTSUPP);
	if (!parent || parent->ops != mock_ops.default_domain_ops)
		return ERR_PTR(-EINVAL);

	mock_parent = to_mock_domain(parent);
	if (!mock_parent)
		return ERR_PTR(-EINVAL);

	mock_nested = __mock_domain_alloc_nested(user_data);
	if (IS_ERR(mock_nested))
		return ERR_CAST(mock_nested);
	mock_nested->parent = mock_parent;
	return &mock_nested->domain;
}

static struct iommu_domain *
mock_domain_alloc_user(struct device *dev, u32 flags,
		       struct iommu_domain *parent,
		       const struct iommu_user_data *user_data)
{
	bool has_dirty_flag = flags & IOMMU_HWPT_ALLOC_DIRTY_TRACKING;
	const u32 PAGING_FLAGS = IOMMU_HWPT_ALLOC_DIRTY_TRACKING |
				 IOMMU_HWPT_ALLOC_NEST_PARENT;
	bool no_dirty_ops = to_mock_dev(dev)->flags &
			    MOCK_FLAGS_DEVICE_NO_DIRTY;
	struct iommu_domain *domain;

	if (parent)
		return mock_domain_alloc_nested(parent, flags, user_data);

	if (user_data)
		return ERR_PTR(-EOPNOTSUPP);
	if ((flags & ~PAGING_FLAGS) || (has_dirty_flag && no_dirty_ops))
		return ERR_PTR(-EOPNOTSUPP);

	domain = mock_domain_alloc_paging(dev);
	if (!domain)
		return ERR_PTR(-ENOMEM);
	if (has_dirty_flag)
		domain->dirty_ops = &dirty_ops;
	return domain;
}

static void mock_domain_free(struct iommu_domain *domain)
{
	struct mock_iommu_domain *mock = to_mock_domain(domain);

	WARN_ON(!xa_empty(&mock->pfns));
	kfree(mock);
}

static int mock_domain_map_pages(struct iommu_domain *domain,
				 unsigned long iova, phys_addr_t paddr,
				 size_t pgsize, size_t pgcount, int prot,
				 gfp_t gfp, size_t *mapped)
{
	struct mock_iommu_domain *mock = to_mock_domain(domain);
	unsigned long flags = MOCK_PFN_START_IOVA;
	unsigned long start_iova = iova;

	/*
	 * xarray does not reliably work with fault injection because it does a
	 * retry allocation, so put our own failure point.
	 */
	if (iommufd_should_fail())
		return -ENOENT;

	WARN_ON(iova % MOCK_IO_PAGE_SIZE);
	WARN_ON(pgsize % MOCK_IO_PAGE_SIZE);
	for (; pgcount; pgcount--) {
		size_t cur;

		for (cur = 0; cur != pgsize; cur += MOCK_IO_PAGE_SIZE) {
			void *old;

			if (pgcount == 1 && cur + MOCK_IO_PAGE_SIZE == pgsize)
				flags = MOCK_PFN_LAST_IOVA;
			if (pgsize != MOCK_IO_PAGE_SIZE) {
				flags |= MOCK_PFN_HUGE_IOVA;
			}
			old = xa_store(&mock->pfns, iova / MOCK_IO_PAGE_SIZE,
				       xa_mk_value((paddr / MOCK_IO_PAGE_SIZE) |
						   flags),
				       gfp);
			if (xa_is_err(old)) {
				for (; start_iova != iova;
				     start_iova += MOCK_IO_PAGE_SIZE)
					xa_erase(&mock->pfns,
						 start_iova /
							 MOCK_IO_PAGE_SIZE);
				return xa_err(old);
			}
			WARN_ON(old);
			iova += MOCK_IO_PAGE_SIZE;
			paddr += MOCK_IO_PAGE_SIZE;
			*mapped += MOCK_IO_PAGE_SIZE;
			flags = 0;
		}
	}
	return 0;
}

static size_t mock_domain_unmap_pages(struct iommu_domain *domain,
				      unsigned long iova, size_t pgsize,
				      size_t pgcount,
				      struct iommu_iotlb_gather *iotlb_gather)
{
	struct mock_iommu_domain *mock = to_mock_domain(domain);
	bool first = true;
	size_t ret = 0;
	void *ent;

	WARN_ON(iova % MOCK_IO_PAGE_SIZE);
	WARN_ON(pgsize % MOCK_IO_PAGE_SIZE);

	for (; pgcount; pgcount--) {
		size_t cur;

		for (cur = 0; cur != pgsize; cur += MOCK_IO_PAGE_SIZE) {
			ent = xa_erase(&mock->pfns, iova / MOCK_IO_PAGE_SIZE);

			/*
			 * iommufd generates unmaps that must be a strict
			 * superset of the map's performend So every
			 * starting/ending IOVA should have been an iova passed
			 * to map.
			 *
			 * This simple logic doesn't work when the HUGE_PAGE is
			 * turned on since the core code will automatically
			 * switch between the two page sizes creating a break in
			 * the unmap calls. The break can land in the middle of
			 * contiguous IOVA.
			 */
			if (!(domain->pgsize_bitmap & MOCK_HUGE_PAGE_SIZE)) {
				if (first) {
					WARN_ON(ent && !(xa_to_value(ent) &
							 MOCK_PFN_START_IOVA));
					first = false;
				}
				if (pgcount == 1 &&
				    cur + MOCK_IO_PAGE_SIZE == pgsize)
					WARN_ON(ent && !(xa_to_value(ent) &
							 MOCK_PFN_LAST_IOVA));
			}

			iova += MOCK_IO_PAGE_SIZE;
			ret += MOCK_IO_PAGE_SIZE;
		}
	}
	return ret;
}

static phys_addr_t mock_domain_iova_to_phys(struct iommu_domain *domain,
					    dma_addr_t iova)
{
	struct mock_iommu_domain *mock = to_mock_domain(domain);
	void *ent;

	WARN_ON(iova % MOCK_IO_PAGE_SIZE);
	ent = xa_load(&mock->pfns, iova / MOCK_IO_PAGE_SIZE);
	WARN_ON(!ent);
	return (xa_to_value(ent) & MOCK_PFN_MASK) * MOCK_IO_PAGE_SIZE;
}

static bool mock_domain_capable(struct device *dev, enum iommu_cap cap)
{
	struct mock_dev *mdev = to_mock_dev(dev);

	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_DIRTY_TRACKING:
		return !(mdev->flags & MOCK_FLAGS_DEVICE_NO_DIRTY);
	default:
		break;
	}

	return false;
}

static struct iopf_queue *mock_iommu_iopf_queue;

static struct mock_iommu_device {
	struct iommu_device iommu_dev;
	struct completion complete;
	refcount_t users;
} mock_iommu;

static struct iommu_device *mock_probe_device(struct device *dev)
{
	if (dev->bus != &iommufd_mock_bus_type.bus)
		return ERR_PTR(-ENODEV);
	return &mock_iommu.iommu_dev;
}

static void mock_domain_page_response(struct device *dev, struct iopf_fault *evt,
				      struct iommu_page_response *msg)
{
}

static int mock_dev_enable_feat(struct device *dev, enum iommu_dev_features feat)
{
	if (feat != IOMMU_DEV_FEAT_IOPF || !mock_iommu_iopf_queue)
		return -ENODEV;

	return iopf_queue_add_device(mock_iommu_iopf_queue, dev);
}

static int mock_dev_disable_feat(struct device *dev, enum iommu_dev_features feat)
{
	if (feat != IOMMU_DEV_FEAT_IOPF || !mock_iommu_iopf_queue)
		return -ENODEV;

	iopf_queue_remove_device(mock_iommu_iopf_queue, dev);

	return 0;
}

static void mock_viommu_destroy(struct iommufd_viommu *viommu)
{
	struct mock_iommu_device *mock_iommu = container_of(
		viommu->iommu_dev, struct mock_iommu_device, iommu_dev);

	if (refcount_dec_and_test(&mock_iommu->users))
		complete(&mock_iommu->complete);

	/* iommufd core frees mock_viommu and viommu */
}

static struct iommu_domain *
mock_viommu_alloc_domain_nested(struct iommufd_viommu *viommu, u32 flags,
				const struct iommu_user_data *user_data)
{
	struct mock_viommu *mock_viommu = to_mock_viommu(viommu);
	struct mock_iommu_domain_nested *mock_nested;

	if (flags & ~IOMMU_HWPT_FAULT_ID_VALID)
		return ERR_PTR(-EOPNOTSUPP);

	mock_nested = __mock_domain_alloc_nested(user_data);
	if (IS_ERR(mock_nested))
		return ERR_CAST(mock_nested);
	mock_nested->mock_viommu = mock_viommu;
	mock_nested->parent = mock_viommu->s2_parent;
	return &mock_nested->domain;
}

static int mock_viommu_cache_invalidate(struct iommufd_viommu *viommu,
					struct iommu_user_data_array *array)
{
	struct iommu_viommu_invalidate_selftest *cmds;
	struct iommu_viommu_invalidate_selftest *cur;
	struct iommu_viommu_invalidate_selftest *end;
	int rc;

	/* A zero-length array is allowed to validate the array type */
	if (array->entry_num == 0 &&
	    array->type == IOMMU_VIOMMU_INVALIDATE_DATA_SELFTEST) {
		array->entry_num = 0;
		return 0;
	}

	cmds = kcalloc(array->entry_num, sizeof(*cmds), GFP_KERNEL);
	if (!cmds)
		return -ENOMEM;
	cur = cmds;
	end = cmds + array->entry_num;

	static_assert(sizeof(*cmds) == 3 * sizeof(u32));
	rc = iommu_copy_struct_from_full_user_array(
		cmds, sizeof(*cmds), array,
		IOMMU_VIOMMU_INVALIDATE_DATA_SELFTEST);
	if (rc)
		goto out;

	while (cur != end) {
		struct mock_dev *mdev;
		struct device *dev;
		int i;

		if (cur->flags & ~IOMMU_TEST_INVALIDATE_FLAG_ALL) {
			rc = -EOPNOTSUPP;
			goto out;
		}

		if (cur->cache_id > MOCK_DEV_CACHE_ID_MAX) {
			rc = -EINVAL;
			goto out;
		}

		xa_lock(&viommu->vdevs);
		dev = iommufd_viommu_find_dev(viommu,
					      (unsigned long)cur->vdev_id);
		if (!dev) {
			xa_unlock(&viommu->vdevs);
			rc = -EINVAL;
			goto out;
		}
		mdev = container_of(dev, struct mock_dev, dev);

		if (cur->flags & IOMMU_TEST_INVALIDATE_FLAG_ALL) {
			/* Invalidate all cache entries and ignore cache_id */
			for (i = 0; i < MOCK_DEV_CACHE_NUM; i++)
				mdev->cache[i] = 0;
		} else {
			mdev->cache[cur->cache_id] = 0;
		}
		xa_unlock(&viommu->vdevs);

		cur++;
	}
out:
	array->entry_num = cur - cmds;
	kfree(cmds);
	return rc;
}

static struct iommufd_viommu_ops mock_viommu_ops = {
	.destroy = mock_viommu_destroy,
	.alloc_domain_nested = mock_viommu_alloc_domain_nested,
	.cache_invalidate = mock_viommu_cache_invalidate,
};

static struct iommufd_viommu *mock_viommu_alloc(struct device *dev,
						struct iommu_domain *domain,
						struct iommufd_ctx *ictx,
						unsigned int viommu_type)
{
	struct mock_iommu_device *mock_iommu =
		iommu_get_iommu_dev(dev, struct mock_iommu_device, iommu_dev);
	struct mock_viommu *mock_viommu;

	if (viommu_type != IOMMU_VIOMMU_TYPE_SELFTEST)
		return ERR_PTR(-EOPNOTSUPP);

	mock_viommu = iommufd_viommu_alloc(ictx, struct mock_viommu, core,
					   &mock_viommu_ops);
	if (IS_ERR(mock_viommu))
		return ERR_CAST(mock_viommu);

	refcount_inc(&mock_iommu->users);
	return &mock_viommu->core;
}

static const struct iommu_ops mock_ops = {
	/*
	 * IOMMU_DOMAIN_BLOCKED cannot be returned from def_domain_type()
	 * because it is zero.
	 */
	.default_domain = &mock_blocking_domain,
	.blocked_domain = &mock_blocking_domain,
	.owner = THIS_MODULE,
	.pgsize_bitmap = MOCK_IO_PAGE_SIZE,
	.hw_info = mock_domain_hw_info,
	.domain_alloc_paging = mock_domain_alloc_paging,
	.domain_alloc_user = mock_domain_alloc_user,
	.capable = mock_domain_capable,
	.device_group = generic_device_group,
	.probe_device = mock_probe_device,
	.page_response = mock_domain_page_response,
	.dev_enable_feat = mock_dev_enable_feat,
	.dev_disable_feat = mock_dev_disable_feat,
	.user_pasid_table = true,
	.viommu_alloc = mock_viommu_alloc,
	.default_domain_ops =
		&(struct iommu_domain_ops){
			.free = mock_domain_free,
			.attach_dev = mock_domain_nop_attach,
			.map_pages = mock_domain_map_pages,
			.unmap_pages = mock_domain_unmap_pages,
			.iova_to_phys = mock_domain_iova_to_phys,
		},
};

static void mock_domain_free_nested(struct iommu_domain *domain)
{
	kfree(to_mock_nested(domain));
}

static int
mock_domain_cache_invalidate_user(struct iommu_domain *domain,
				  struct iommu_user_data_array *array)
{
	struct mock_iommu_domain_nested *mock_nested = to_mock_nested(domain);
	struct iommu_hwpt_invalidate_selftest inv;
	u32 processed = 0;
	int i = 0, j;
	int rc = 0;

	if (array->type != IOMMU_HWPT_INVALIDATE_DATA_SELFTEST) {
		rc = -EINVAL;
		goto out;
	}

	for ( ; i < array->entry_num; i++) {
		rc = iommu_copy_struct_from_user_array(&inv, array,
						       IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
						       i, iotlb_id);
		if (rc)
			break;

		if (inv.flags & ~IOMMU_TEST_INVALIDATE_FLAG_ALL) {
			rc = -EOPNOTSUPP;
			break;
		}

		if (inv.iotlb_id > MOCK_NESTED_DOMAIN_IOTLB_ID_MAX) {
			rc = -EINVAL;
			break;
		}

		if (inv.flags & IOMMU_TEST_INVALIDATE_FLAG_ALL) {
			/* Invalidate all mock iotlb entries and ignore iotlb_id */
			for (j = 0; j < MOCK_NESTED_DOMAIN_IOTLB_NUM; j++)
				mock_nested->iotlb[j] = 0;
		} else {
			mock_nested->iotlb[inv.iotlb_id] = 0;
		}

		processed++;
	}

out:
	array->entry_num = processed;
	return rc;
}

static struct iommu_domain_ops domain_nested_ops = {
	.free = mock_domain_free_nested,
	.attach_dev = mock_domain_nop_attach,
	.cache_invalidate_user = mock_domain_cache_invalidate_user,
};

static inline struct iommufd_hw_pagetable *
__get_md_pagetable(struct iommufd_ucmd *ucmd, u32 mockpt_id, u32 hwpt_type)
{
	struct iommufd_object *obj;

	obj = iommufd_get_object(ucmd->ictx, mockpt_id, hwpt_type);
	if (IS_ERR(obj))
		return ERR_CAST(obj);
	return container_of(obj, struct iommufd_hw_pagetable, obj);
}

static inline struct iommufd_hw_pagetable *
get_md_pagetable(struct iommufd_ucmd *ucmd, u32 mockpt_id,
		 struct mock_iommu_domain **mock)
{
	struct iommufd_hw_pagetable *hwpt;

	hwpt = __get_md_pagetable(ucmd, mockpt_id, IOMMUFD_OBJ_HWPT_PAGING);
	if (IS_ERR(hwpt))
		return hwpt;
	if (hwpt->domain->type != IOMMU_DOMAIN_UNMANAGED ||
	    hwpt->domain->ops != mock_ops.default_domain_ops) {
		iommufd_put_object(ucmd->ictx, &hwpt->obj);
		return ERR_PTR(-EINVAL);
	}
	*mock = to_mock_domain(hwpt->domain);
	return hwpt;
}

static inline struct iommufd_hw_pagetable *
get_md_pagetable_nested(struct iommufd_ucmd *ucmd, u32 mockpt_id,
			struct mock_iommu_domain_nested **mock_nested)
{
	struct iommufd_hw_pagetable *hwpt;

	hwpt = __get_md_pagetable(ucmd, mockpt_id, IOMMUFD_OBJ_HWPT_NESTED);
	if (IS_ERR(hwpt))
		return hwpt;
	if (hwpt->domain->type != IOMMU_DOMAIN_NESTED ||
	    hwpt->domain->ops != &domain_nested_ops) {
		iommufd_put_object(ucmd->ictx, &hwpt->obj);
		return ERR_PTR(-EINVAL);
	}
	*mock_nested = to_mock_nested(hwpt->domain);
	return hwpt;
}

static void mock_dev_release(struct device *dev)
{
	struct mock_dev *mdev = to_mock_dev(dev);

	ida_free(&mock_dev_ida, mdev->id);
	kfree(mdev);
}

static struct mock_dev *mock_dev_create(unsigned long dev_flags)
{
	struct mock_dev *mdev;
	int rc, i;

	if (dev_flags &
	    ~(MOCK_FLAGS_DEVICE_NO_DIRTY | MOCK_FLAGS_DEVICE_HUGE_IOVA))
		return ERR_PTR(-EINVAL);

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	device_initialize(&mdev->dev);
	mdev->flags = dev_flags;
	mdev->dev.release = mock_dev_release;
	mdev->dev.bus = &iommufd_mock_bus_type.bus;
	for (i = 0; i < MOCK_DEV_CACHE_NUM; i++)
		mdev->cache[i] = IOMMU_TEST_DEV_CACHE_DEFAULT;

	rc = ida_alloc(&mock_dev_ida, GFP_KERNEL);
	if (rc < 0)
		goto err_put;
	mdev->id = rc;

	rc = dev_set_name(&mdev->dev, "iommufd_mock%u", mdev->id);
	if (rc)
		goto err_put;

	rc = device_add(&mdev->dev);
	if (rc)
		goto err_put;
	return mdev;

err_put:
	put_device(&mdev->dev);
	return ERR_PTR(rc);
}

static void mock_dev_destroy(struct mock_dev *mdev)
{
	device_unregister(&mdev->dev);
}

bool iommufd_selftest_is_mock_dev(struct device *dev)
{
	return dev->release == mock_dev_release;
}

/* Create an hw_pagetable with the mock domain so we can test the domain ops */
static int iommufd_test_mock_domain(struct iommufd_ucmd *ucmd,
				    struct iommu_test_cmd *cmd)
{
	struct iommufd_device *idev;
	struct selftest_obj *sobj;
	u32 pt_id = cmd->id;
	u32 dev_flags = 0;
	u32 idev_id;
	int rc;

	sobj = iommufd_object_alloc(ucmd->ictx, sobj, IOMMUFD_OBJ_SELFTEST);
	if (IS_ERR(sobj))
		return PTR_ERR(sobj);

	sobj->idev.ictx = ucmd->ictx;
	sobj->type = TYPE_IDEV;

	if (cmd->op == IOMMU_TEST_OP_MOCK_DOMAIN_FLAGS)
		dev_flags = cmd->mock_domain_flags.dev_flags;

	sobj->idev.mock_dev = mock_dev_create(dev_flags);
	if (IS_ERR(sobj->idev.mock_dev)) {
		rc = PTR_ERR(sobj->idev.mock_dev);
		goto out_sobj;
	}

	idev = iommufd_device_bind(ucmd->ictx, &sobj->idev.mock_dev->dev,
				   &idev_id);
	if (IS_ERR(idev)) {
		rc = PTR_ERR(idev);
		goto out_mdev;
	}
	sobj->idev.idev = idev;

	rc = iommufd_device_attach(idev, &pt_id);
	if (rc)
		goto out_unbind;

	/* Userspace must destroy the device_id to destroy the object */
	cmd->mock_domain.out_hwpt_id = pt_id;
	cmd->mock_domain.out_stdev_id = sobj->obj.id;
	cmd->mock_domain.out_idev_id = idev_id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_detach;
	iommufd_object_finalize(ucmd->ictx, &sobj->obj);
	return 0;

out_detach:
	iommufd_device_detach(idev);
out_unbind:
	iommufd_device_unbind(idev);
out_mdev:
	mock_dev_destroy(sobj->idev.mock_dev);
out_sobj:
	iommufd_object_abort(ucmd->ictx, &sobj->obj);
	return rc;
}

/* Replace the mock domain with a manually allocated hw_pagetable */
static int iommufd_test_mock_domain_replace(struct iommufd_ucmd *ucmd,
					    unsigned int device_id, u32 pt_id,
					    struct iommu_test_cmd *cmd)
{
	struct iommufd_object *dev_obj;
	struct selftest_obj *sobj;
	int rc;

	/*
	 * Prefer to use the OBJ_SELFTEST because the destroy_rwsem will ensure
	 * it doesn't race with detach, which is not allowed.
	 */
	dev_obj =
		iommufd_get_object(ucmd->ictx, device_id, IOMMUFD_OBJ_SELFTEST);
	if (IS_ERR(dev_obj))
		return PTR_ERR(dev_obj);

	sobj = to_selftest_obj(dev_obj);
	if (sobj->type != TYPE_IDEV) {
		rc = -EINVAL;
		goto out_dev_obj;
	}

	rc = iommufd_device_replace(sobj->idev.idev, &pt_id);
	if (rc)
		goto out_dev_obj;

	cmd->mock_domain_replace.pt_id = pt_id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));

out_dev_obj:
	iommufd_put_object(ucmd->ictx, dev_obj);
	return rc;
}

/* Add an additional reserved IOVA to the IOAS */
static int iommufd_test_add_reserved(struct iommufd_ucmd *ucmd,
				     unsigned int mockpt_id,
				     unsigned long start, size_t length)
{
	struct iommufd_ioas *ioas;
	int rc;

	ioas = iommufd_get_ioas(ucmd->ictx, mockpt_id);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);
	down_write(&ioas->iopt.iova_rwsem);
	rc = iopt_reserve_iova(&ioas->iopt, start, start + length - 1, NULL);
	up_write(&ioas->iopt.iova_rwsem);
	iommufd_put_object(ucmd->ictx, &ioas->obj);
	return rc;
}

/* Check that every pfn under each iova matches the pfn under a user VA */
static int iommufd_test_md_check_pa(struct iommufd_ucmd *ucmd,
				    unsigned int mockpt_id, unsigned long iova,
				    size_t length, void __user *uptr)
{
	struct iommufd_hw_pagetable *hwpt;
	struct mock_iommu_domain *mock;
	uintptr_t end;
	int rc;

	if (iova % MOCK_IO_PAGE_SIZE || length % MOCK_IO_PAGE_SIZE ||
	    (uintptr_t)uptr % MOCK_IO_PAGE_SIZE ||
	    check_add_overflow((uintptr_t)uptr, (uintptr_t)length, &end))
		return -EINVAL;

	hwpt = get_md_pagetable(ucmd, mockpt_id, &mock);
	if (IS_ERR(hwpt))
		return PTR_ERR(hwpt);

	for (; length; length -= MOCK_IO_PAGE_SIZE) {
		struct page *pages[1];
		unsigned long pfn;
		long npages;
		void *ent;

		npages = get_user_pages_fast((uintptr_t)uptr & PAGE_MASK, 1, 0,
					     pages);
		if (npages < 0) {
			rc = npages;
			goto out_put;
		}
		if (WARN_ON(npages != 1)) {
			rc = -EFAULT;
			goto out_put;
		}
		pfn = page_to_pfn(pages[0]);
		put_page(pages[0]);

		ent = xa_load(&mock->pfns, iova / MOCK_IO_PAGE_SIZE);
		if (!ent ||
		    (xa_to_value(ent) & MOCK_PFN_MASK) * MOCK_IO_PAGE_SIZE !=
			    pfn * PAGE_SIZE + ((uintptr_t)uptr % PAGE_SIZE)) {
			rc = -EINVAL;
			goto out_put;
		}
		iova += MOCK_IO_PAGE_SIZE;
		uptr += MOCK_IO_PAGE_SIZE;
	}
	rc = 0;

out_put:
	iommufd_put_object(ucmd->ictx, &hwpt->obj);
	return rc;
}

/* Check that the page ref count matches, to look for missing pin/unpins */
static int iommufd_test_md_check_refs(struct iommufd_ucmd *ucmd,
				      void __user *uptr, size_t length,
				      unsigned int refs)
{
	uintptr_t end;

	if (length % PAGE_SIZE || (uintptr_t)uptr % PAGE_SIZE ||
	    check_add_overflow((uintptr_t)uptr, (uintptr_t)length, &end))
		return -EINVAL;

	for (; length; length -= PAGE_SIZE) {
		struct page *pages[1];
		long npages;

		npages = get_user_pages_fast((uintptr_t)uptr, 1, 0, pages);
		if (npages < 0)
			return npages;
		if (WARN_ON(npages != 1))
			return -EFAULT;
		if (!PageCompound(pages[0])) {
			unsigned int count;

			count = page_ref_count(pages[0]);
			if (count / GUP_PIN_COUNTING_BIAS != refs) {
				put_page(pages[0]);
				return -EIO;
			}
		}
		put_page(pages[0]);
		uptr += PAGE_SIZE;
	}
	return 0;
}

static int iommufd_test_md_check_iotlb(struct iommufd_ucmd *ucmd,
				       u32 mockpt_id, unsigned int iotlb_id,
				       u32 iotlb)
{
	struct mock_iommu_domain_nested *mock_nested;
	struct iommufd_hw_pagetable *hwpt;
	int rc = 0;

	hwpt = get_md_pagetable_nested(ucmd, mockpt_id, &mock_nested);
	if (IS_ERR(hwpt))
		return PTR_ERR(hwpt);

	mock_nested = to_mock_nested(hwpt->domain);

	if (iotlb_id > MOCK_NESTED_DOMAIN_IOTLB_ID_MAX ||
	    mock_nested->iotlb[iotlb_id] != iotlb)
		rc = -EINVAL;
	iommufd_put_object(ucmd->ictx, &hwpt->obj);
	return rc;
}

static int iommufd_test_dev_check_cache(struct iommufd_ucmd *ucmd, u32 idev_id,
					unsigned int cache_id, u32 cache)
{
	struct iommufd_device *idev;
	struct mock_dev *mdev;
	int rc = 0;

	idev = iommufd_get_device(ucmd, idev_id);
	if (IS_ERR(idev))
		return PTR_ERR(idev);
	mdev = container_of(idev->dev, struct mock_dev, dev);

	if (cache_id > MOCK_DEV_CACHE_ID_MAX || mdev->cache[cache_id] != cache)
		rc = -EINVAL;
	iommufd_put_object(ucmd->ictx, &idev->obj);
	return rc;
}

struct selftest_access {
	struct iommufd_access *access;
	struct file *file;
	struct mutex lock;
	struct list_head items;
	unsigned int next_id;
	bool destroying;
};

struct selftest_access_item {
	struct list_head items_elm;
	unsigned long iova;
	size_t length;
	unsigned int id;
};

static const struct file_operations iommfd_test_staccess_fops;

static struct selftest_access *iommufd_access_get(int fd)
{
	struct file *file;

	file = fget(fd);
	if (!file)
		return ERR_PTR(-EBADFD);

	if (file->f_op != &iommfd_test_staccess_fops) {
		fput(file);
		return ERR_PTR(-EBADFD);
	}
	return file->private_data;
}

static void iommufd_test_access_unmap(void *data, unsigned long iova,
				      unsigned long length)
{
	unsigned long iova_last = iova + length - 1;
	struct selftest_access *staccess = data;
	struct selftest_access_item *item;
	struct selftest_access_item *tmp;

	mutex_lock(&staccess->lock);
	list_for_each_entry_safe(item, tmp, &staccess->items, items_elm) {
		if (iova > item->iova + item->length - 1 ||
		    iova_last < item->iova)
			continue;
		list_del(&item->items_elm);
		iommufd_access_unpin_pages(staccess->access, item->iova,
					   item->length);
		kfree(item);
	}
	mutex_unlock(&staccess->lock);
}

static int iommufd_test_access_item_destroy(struct iommufd_ucmd *ucmd,
					    unsigned int access_id,
					    unsigned int item_id)
{
	struct selftest_access_item *item;
	struct selftest_access *staccess;

	staccess = iommufd_access_get(access_id);
	if (IS_ERR(staccess))
		return PTR_ERR(staccess);

	mutex_lock(&staccess->lock);
	list_for_each_entry(item, &staccess->items, items_elm) {
		if (item->id == item_id) {
			list_del(&item->items_elm);
			iommufd_access_unpin_pages(staccess->access, item->iova,
						   item->length);
			mutex_unlock(&staccess->lock);
			kfree(item);
			fput(staccess->file);
			return 0;
		}
	}
	mutex_unlock(&staccess->lock);
	fput(staccess->file);
	return -ENOENT;
}

static int iommufd_test_staccess_release(struct inode *inode,
					 struct file *filep)
{
	struct selftest_access *staccess = filep->private_data;

	if (staccess->access) {
		iommufd_test_access_unmap(staccess, 0, ULONG_MAX);
		iommufd_access_destroy(staccess->access);
	}
	mutex_destroy(&staccess->lock);
	kfree(staccess);
	return 0;
}

static const struct iommufd_access_ops selftest_access_ops_pin = {
	.needs_pin_pages = 1,
	.unmap = iommufd_test_access_unmap,
};

static const struct iommufd_access_ops selftest_access_ops = {
	.unmap = iommufd_test_access_unmap,
};

static const struct file_operations iommfd_test_staccess_fops = {
	.release = iommufd_test_staccess_release,
};

static struct selftest_access *iommufd_test_alloc_access(void)
{
	struct selftest_access *staccess;
	struct file *filep;

	staccess = kzalloc(sizeof(*staccess), GFP_KERNEL_ACCOUNT);
	if (!staccess)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&staccess->items);
	mutex_init(&staccess->lock);

	filep = anon_inode_getfile("[iommufd_test_staccess]",
				   &iommfd_test_staccess_fops, staccess,
				   O_RDWR);
	if (IS_ERR(filep)) {
		kfree(staccess);
		return ERR_CAST(filep);
	}
	staccess->file = filep;
	return staccess;
}

static int iommufd_test_create_access(struct iommufd_ucmd *ucmd,
				      unsigned int ioas_id, unsigned int flags)
{
	struct iommu_test_cmd *cmd = ucmd->cmd;
	struct selftest_access *staccess;
	struct iommufd_access *access;
	u32 id;
	int fdno;
	int rc;

	if (flags & ~MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES)
		return -EOPNOTSUPP;

	staccess = iommufd_test_alloc_access();
	if (IS_ERR(staccess))
		return PTR_ERR(staccess);

	fdno = get_unused_fd_flags(O_CLOEXEC);
	if (fdno < 0) {
		rc = -ENOMEM;
		goto out_free_staccess;
	}

	access = iommufd_access_create(
		ucmd->ictx,
		(flags & MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES) ?
			&selftest_access_ops_pin :
			&selftest_access_ops,
		staccess, &id);
	if (IS_ERR(access)) {
		rc = PTR_ERR(access);
		goto out_put_fdno;
	}
	rc = iommufd_access_attach(access, ioas_id);
	if (rc)
		goto out_destroy;
	cmd->create_access.out_access_fd = fdno;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_destroy;

	staccess->access = access;
	fd_install(fdno, staccess->file);
	return 0;

out_destroy:
	iommufd_access_destroy(access);
out_put_fdno:
	put_unused_fd(fdno);
out_free_staccess:
	fput(staccess->file);
	return rc;
}

static int iommufd_test_access_replace_ioas(struct iommufd_ucmd *ucmd,
					    unsigned int access_id,
					    unsigned int ioas_id)
{
	struct selftest_access *staccess;
	int rc;

	staccess = iommufd_access_get(access_id);
	if (IS_ERR(staccess))
		return PTR_ERR(staccess);

	rc = iommufd_access_replace(staccess->access, ioas_id);
	fput(staccess->file);
	return rc;
}

/* Check that the pages in a page array match the pages in the user VA */
static int iommufd_test_check_pages(void __user *uptr, struct page **pages,
				    size_t npages)
{
	for (; npages; npages--) {
		struct page *tmp_pages[1];
		long rc;

		rc = get_user_pages_fast((uintptr_t)uptr, 1, 0, tmp_pages);
		if (rc < 0)
			return rc;
		if (WARN_ON(rc != 1))
			return -EFAULT;
		put_page(tmp_pages[0]);
		if (tmp_pages[0] != *pages)
			return -EBADE;
		pages++;
		uptr += PAGE_SIZE;
	}
	return 0;
}

static int iommufd_test_access_pages(struct iommufd_ucmd *ucmd,
				     unsigned int access_id, unsigned long iova,
				     size_t length, void __user *uptr,
				     u32 flags)
{
	struct iommu_test_cmd *cmd = ucmd->cmd;
	struct selftest_access_item *item;
	struct selftest_access *staccess;
	struct page **pages;
	size_t npages;
	int rc;

	/* Prevent syzkaller from triggering a WARN_ON in kvzalloc() */
	if (length > 16*1024*1024)
		return -ENOMEM;

	if (flags & ~(MOCK_FLAGS_ACCESS_WRITE | MOCK_FLAGS_ACCESS_SYZ))
		return -EOPNOTSUPP;

	staccess = iommufd_access_get(access_id);
	if (IS_ERR(staccess))
		return PTR_ERR(staccess);

	if (staccess->access->ops != &selftest_access_ops_pin) {
		rc = -EOPNOTSUPP;
		goto out_put;
	}

	if (flags & MOCK_FLAGS_ACCESS_SYZ)
		iova = iommufd_test_syz_conv_iova(staccess->access,
					&cmd->access_pages.iova);

	npages = (ALIGN(iova + length, PAGE_SIZE) -
		  ALIGN_DOWN(iova, PAGE_SIZE)) /
		 PAGE_SIZE;
	pages = kvcalloc(npages, sizeof(*pages), GFP_KERNEL_ACCOUNT);
	if (!pages) {
		rc = -ENOMEM;
		goto out_put;
	}

	/*
	 * Drivers will need to think very carefully about this locking. The
	 * core code can do multiple unmaps instantaneously after
	 * iommufd_access_pin_pages() and *all* the unmaps must not return until
	 * the range is unpinned. This simple implementation puts a global lock
	 * around the pin, which may not suit drivers that want this to be a
	 * performance path. drivers that get this wrong will trigger WARN_ON
	 * races and cause EDEADLOCK failures to userspace.
	 */
	mutex_lock(&staccess->lock);
	rc = iommufd_access_pin_pages(staccess->access, iova, length, pages,
				      flags & MOCK_FLAGS_ACCESS_WRITE);
	if (rc)
		goto out_unlock;

	/* For syzkaller allow uptr to be NULL to skip this check */
	if (uptr) {
		rc = iommufd_test_check_pages(
			uptr - (iova - ALIGN_DOWN(iova, PAGE_SIZE)), pages,
			npages);
		if (rc)
			goto out_unaccess;
	}

	item = kzalloc(sizeof(*item), GFP_KERNEL_ACCOUNT);
	if (!item) {
		rc = -ENOMEM;
		goto out_unaccess;
	}

	item->iova = iova;
	item->length = length;
	item->id = staccess->next_id++;
	list_add_tail(&item->items_elm, &staccess->items);

	cmd->access_pages.out_access_pages_id = item->id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_free_item;
	goto out_unlock;

out_free_item:
	list_del(&item->items_elm);
	kfree(item);
out_unaccess:
	iommufd_access_unpin_pages(staccess->access, iova, length);
out_unlock:
	mutex_unlock(&staccess->lock);
	kvfree(pages);
out_put:
	fput(staccess->file);
	return rc;
}

static int iommufd_test_access_rw(struct iommufd_ucmd *ucmd,
				  unsigned int access_id, unsigned long iova,
				  size_t length, void __user *ubuf,
				  unsigned int flags)
{
	struct iommu_test_cmd *cmd = ucmd->cmd;
	struct selftest_access *staccess;
	void *tmp;
	int rc;

	/* Prevent syzkaller from triggering a WARN_ON in kvzalloc() */
	if (length > 16*1024*1024)
		return -ENOMEM;

	if (flags & ~(MOCK_ACCESS_RW_WRITE | MOCK_ACCESS_RW_SLOW_PATH |
		      MOCK_FLAGS_ACCESS_SYZ))
		return -EOPNOTSUPP;

	staccess = iommufd_access_get(access_id);
	if (IS_ERR(staccess))
		return PTR_ERR(staccess);

	tmp = kvzalloc(length, GFP_KERNEL_ACCOUNT);
	if (!tmp) {
		rc = -ENOMEM;
		goto out_put;
	}

	if (flags & MOCK_ACCESS_RW_WRITE) {
		if (copy_from_user(tmp, ubuf, length)) {
			rc = -EFAULT;
			goto out_free;
		}
	}

	if (flags & MOCK_FLAGS_ACCESS_SYZ)
		iova = iommufd_test_syz_conv_iova(staccess->access,
				&cmd->access_rw.iova);

	rc = iommufd_access_rw(staccess->access, iova, tmp, length, flags);
	if (rc)
		goto out_free;
	if (!(flags & MOCK_ACCESS_RW_WRITE)) {
		if (copy_to_user(ubuf, tmp, length)) {
			rc = -EFAULT;
			goto out_free;
		}
	}

out_free:
	kvfree(tmp);
out_put:
	fput(staccess->file);
	return rc;
}
static_assert((unsigned int)MOCK_ACCESS_RW_WRITE == IOMMUFD_ACCESS_RW_WRITE);
static_assert((unsigned int)MOCK_ACCESS_RW_SLOW_PATH ==
	      __IOMMUFD_ACCESS_RW_SLOW_PATH);

static int iommufd_test_dirty(struct iommufd_ucmd *ucmd, unsigned int mockpt_id,
			      unsigned long iova, size_t length,
			      unsigned long page_size, void __user *uptr,
			      u32 flags)
{
	unsigned long i, max;
	struct iommu_test_cmd *cmd = ucmd->cmd;
	struct iommufd_hw_pagetable *hwpt;
	struct mock_iommu_domain *mock;
	int rc, count = 0;
	void *tmp;

	if (!page_size || !length || iova % page_size || length % page_size ||
	    !uptr)
		return -EINVAL;

	hwpt = get_md_pagetable(ucmd, mockpt_id, &mock);
	if (IS_ERR(hwpt))
		return PTR_ERR(hwpt);

	if (!(mock->flags & MOCK_DIRTY_TRACK)) {
		rc = -EINVAL;
		goto out_put;
	}

	max = length / page_size;
	tmp = kvzalloc(DIV_ROUND_UP(max, BITS_PER_LONG) * sizeof(unsigned long),
		       GFP_KERNEL_ACCOUNT);
	if (!tmp) {
		rc = -ENOMEM;
		goto out_put;
	}

	if (copy_from_user(tmp, uptr,DIV_ROUND_UP(max, BITS_PER_BYTE))) {
		rc = -EFAULT;
		goto out_free;
	}

	for (i = 0; i < max; i++) {
		unsigned long cur = iova + i * page_size;
		void *ent, *old;

		if (!test_bit(i, (unsigned long *)tmp))
			continue;

		ent = xa_load(&mock->pfns, cur / page_size);
		if (ent) {
			unsigned long val;

			val = xa_to_value(ent) | MOCK_PFN_DIRTY_IOVA;
			old = xa_store(&mock->pfns, cur / page_size,
				       xa_mk_value(val), GFP_KERNEL);
			WARN_ON_ONCE(ent != old);
			count++;
		}
	}

	cmd->dirty.out_nr_dirty = count;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
out_free:
	kvfree(tmp);
out_put:
	iommufd_put_object(ucmd->ictx, &hwpt->obj);
	return rc;
}

static int iommufd_test_trigger_iopf(struct iommufd_ucmd *ucmd,
				     struct iommu_test_cmd *cmd)
{
	struct iopf_fault event = { };
	struct iommufd_device *idev;

	idev = iommufd_get_device(ucmd, cmd->trigger_iopf.dev_id);
	if (IS_ERR(idev))
		return PTR_ERR(idev);

	event.fault.prm.flags = IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE;
	if (cmd->trigger_iopf.pasid != IOMMU_NO_PASID)
		event.fault.prm.flags |= IOMMU_FAULT_PAGE_REQUEST_PASID_VALID;
	event.fault.type = IOMMU_FAULT_PAGE_REQ;
	event.fault.prm.addr = cmd->trigger_iopf.addr;
	event.fault.prm.pasid = cmd->trigger_iopf.pasid;
	event.fault.prm.grpid = cmd->trigger_iopf.grpid;
	event.fault.prm.perm = cmd->trigger_iopf.perm;

	iommu_report_device_fault(idev->dev, &event);
	iommufd_put_object(ucmd->ictx, &idev->obj);

	return 0;
}

void iommufd_selftest_destroy(struct iommufd_object *obj)
{
	struct selftest_obj *sobj = to_selftest_obj(obj);

	switch (sobj->type) {
	case TYPE_IDEV:
		iommufd_device_detach(sobj->idev.idev);
		iommufd_device_unbind(sobj->idev.idev);
		mock_dev_destroy(sobj->idev.mock_dev);
		break;
	}
}

int iommufd_test(struct iommufd_ucmd *ucmd)
{
	struct iommu_test_cmd *cmd = ucmd->cmd;

	switch (cmd->op) {
	case IOMMU_TEST_OP_ADD_RESERVED:
		return iommufd_test_add_reserved(ucmd, cmd->id,
						 cmd->add_reserved.start,
						 cmd->add_reserved.length);
	case IOMMU_TEST_OP_MOCK_DOMAIN:
	case IOMMU_TEST_OP_MOCK_DOMAIN_FLAGS:
		return iommufd_test_mock_domain(ucmd, cmd);
	case IOMMU_TEST_OP_MOCK_DOMAIN_REPLACE:
		return iommufd_test_mock_domain_replace(
			ucmd, cmd->id, cmd->mock_domain_replace.pt_id, cmd);
	case IOMMU_TEST_OP_MD_CHECK_MAP:
		return iommufd_test_md_check_pa(
			ucmd, cmd->id, cmd->check_map.iova,
			cmd->check_map.length,
			u64_to_user_ptr(cmd->check_map.uptr));
	case IOMMU_TEST_OP_MD_CHECK_REFS:
		return iommufd_test_md_check_refs(
			ucmd, u64_to_user_ptr(cmd->check_refs.uptr),
			cmd->check_refs.length, cmd->check_refs.refs);
	case IOMMU_TEST_OP_MD_CHECK_IOTLB:
		return iommufd_test_md_check_iotlb(ucmd, cmd->id,
						   cmd->check_iotlb.id,
						   cmd->check_iotlb.iotlb);
	case IOMMU_TEST_OP_DEV_CHECK_CACHE:
		return iommufd_test_dev_check_cache(ucmd, cmd->id,
						    cmd->check_dev_cache.id,
						    cmd->check_dev_cache.cache);
	case IOMMU_TEST_OP_CREATE_ACCESS:
		return iommufd_test_create_access(ucmd, cmd->id,
						  cmd->create_access.flags);
	case IOMMU_TEST_OP_ACCESS_REPLACE_IOAS:
		return iommufd_test_access_replace_ioas(
			ucmd, cmd->id, cmd->access_replace_ioas.ioas_id);
	case IOMMU_TEST_OP_ACCESS_PAGES:
		return iommufd_test_access_pages(
			ucmd, cmd->id, cmd->access_pages.iova,
			cmd->access_pages.length,
			u64_to_user_ptr(cmd->access_pages.uptr),
			cmd->access_pages.flags);
	case IOMMU_TEST_OP_ACCESS_RW:
		return iommufd_test_access_rw(
			ucmd, cmd->id, cmd->access_rw.iova,
			cmd->access_rw.length,
			u64_to_user_ptr(cmd->access_rw.uptr),
			cmd->access_rw.flags);
	case IOMMU_TEST_OP_DESTROY_ACCESS_PAGES:
		return iommufd_test_access_item_destroy(
			ucmd, cmd->id, cmd->destroy_access_pages.access_pages_id);
	case IOMMU_TEST_OP_SET_TEMP_MEMORY_LIMIT:
		/* Protect _batch_init(), can not be less than elmsz */
		if (cmd->memory_limit.limit <
		    sizeof(unsigned long) + sizeof(u32))
			return -EINVAL;
		iommufd_test_memory_limit = cmd->memory_limit.limit;
		return 0;
	case IOMMU_TEST_OP_DIRTY:
		return iommufd_test_dirty(ucmd, cmd->id, cmd->dirty.iova,
					  cmd->dirty.length,
					  cmd->dirty.page_size,
					  u64_to_user_ptr(cmd->dirty.uptr),
					  cmd->dirty.flags);
	case IOMMU_TEST_OP_TRIGGER_IOPF:
		return iommufd_test_trigger_iopf(ucmd, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

bool iommufd_should_fail(void)
{
	return should_fail(&fail_iommufd, 1);
}

int __init iommufd_test_init(void)
{
	struct platform_device_info pdevinfo = {
		.name = "iommufd_selftest_iommu",
	};
	int rc;

	dbgfs_root =
		fault_create_debugfs_attr("fail_iommufd", NULL, &fail_iommufd);

	selftest_iommu_dev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(selftest_iommu_dev)) {
		rc = PTR_ERR(selftest_iommu_dev);
		goto err_dbgfs;
	}

	rc = bus_register(&iommufd_mock_bus_type.bus);
	if (rc)
		goto err_platform;

	rc = iommu_device_sysfs_add(&mock_iommu.iommu_dev,
				    &selftest_iommu_dev->dev, NULL, "%s",
				    dev_name(&selftest_iommu_dev->dev));
	if (rc)
		goto err_bus;

	rc = iommu_device_register_bus(&mock_iommu.iommu_dev, &mock_ops,
				  &iommufd_mock_bus_type.bus,
				  &iommufd_mock_bus_type.nb);
	if (rc)
		goto err_sysfs;

	refcount_set(&mock_iommu.users, 1);
	init_completion(&mock_iommu.complete);

	mock_iommu_iopf_queue = iopf_queue_alloc("mock-iopfq");

	return 0;

err_sysfs:
	iommu_device_sysfs_remove(&mock_iommu.iommu_dev);
err_bus:
	bus_unregister(&iommufd_mock_bus_type.bus);
err_platform:
	platform_device_unregister(selftest_iommu_dev);
err_dbgfs:
	debugfs_remove_recursive(dbgfs_root);
	return rc;
}

static void iommufd_test_wait_for_users(void)
{
	if (refcount_dec_and_test(&mock_iommu.users))
		return;
	/*
	 * Time out waiting for iommu device user count to become 0.
	 *
	 * Note that this is just making an example here, since the selftest is
	 * built into the iommufd module, i.e. it only unplugs the iommu device
	 * when unloading the module. So, it is expected that this WARN_ON will
	 * not trigger, as long as any iommufd FDs are open.
	 */
	WARN_ON(!wait_for_completion_timeout(&mock_iommu.complete,
					     msecs_to_jiffies(10000)));
}

void iommufd_test_exit(void)
{
	if (mock_iommu_iopf_queue) {
		iopf_queue_free(mock_iommu_iopf_queue);
		mock_iommu_iopf_queue = NULL;
	}

	iommufd_test_wait_for_users();
	iommu_device_sysfs_remove(&mock_iommu.iommu_dev);
	iommu_device_unregister_bus(&mock_iommu.iommu_dev,
				    &iommufd_mock_bus_type.bus,
				    &iommufd_mock_bus_type.nb);
	bus_unregister(&iommufd_mock_bus_type.bus);
	platform_device_unregister(selftest_iommu_dev);
	debugfs_remove_recursive(dbgfs_root);
}
