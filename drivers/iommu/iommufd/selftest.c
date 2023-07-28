// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 *
 * Kernel side components to support tools/testing/selftests/iommu
 */
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/xarray.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/fault-inject.h>
#include <uapi/linux/iommufd.h>

#include "io_pagetable.h"
#include "iommufd_private.h"
#include "iommufd_test.h"

static DECLARE_FAULT_ATTR(fail_iommufd);
static struct dentry *dbgfs_root;

size_t iommufd_test_memory_limit = 65536;

enum {
	MOCK_IO_PAGE_SIZE = PAGE_SIZE / 2,

	/*
	 * Like a real page table alignment requires the low bits of the address
	 * to be zero. xarray also requires the high bit to be zero, so we store
	 * the pfns shifted. The upper bits are used for metadata.
	 */
	MOCK_PFN_MASK = ULONG_MAX / MOCK_IO_PAGE_SIZE,

	_MOCK_PFN_START = MOCK_PFN_MASK + 1,
	MOCK_PFN_START_IOVA = _MOCK_PFN_START,
	MOCK_PFN_LAST_IOVA = _MOCK_PFN_START,
};

/*
 * Syzkaller has trouble randomizing the correct iova to use since it is linked
 * to the map ioctl's output, and it has no ide about that. So, simplify things.
 * In syzkaller mode the 64 bit IOVA is converted into an nth area and offset
 * value. This has a much smaller randomization space and syzkaller can hit it.
 */
static unsigned long iommufd_test_syz_conv_iova(struct io_pagetable *iopt,
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
	*iova = iommufd_test_syz_conv_iova(&ioas->iopt, iova);
	iommufd_put_object(&ioas->obj);
}

struct mock_iommu_domain {
	struct iommu_domain domain;
	struct xarray pfns;
};

enum selftest_obj_type {
	TYPE_IDEV,
};

struct mock_dev {
	struct device dev;
};

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

static void mock_domain_blocking_free(struct iommu_domain *domain)
{
}

static int mock_domain_nop_attach(struct iommu_domain *domain,
				  struct device *dev)
{
	return 0;
}

static const struct iommu_domain_ops mock_blocking_ops = {
	.free = mock_domain_blocking_free,
	.attach_dev = mock_domain_nop_attach,
};

static struct iommu_domain mock_blocking_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &mock_blocking_ops,
};

static struct iommu_domain *mock_domain_alloc(unsigned int iommu_domain_type)
{
	struct mock_iommu_domain *mock;

	if (iommu_domain_type == IOMMU_DOMAIN_BLOCKED)
		return &mock_blocking_domain;

	if (WARN_ON(iommu_domain_type != IOMMU_DOMAIN_UNMANAGED))
		return NULL;

	mock = kzalloc(sizeof(*mock), GFP_KERNEL);
	if (!mock)
		return NULL;
	mock->domain.geometry.aperture_start = MOCK_APERTURE_START;
	mock->domain.geometry.aperture_end = MOCK_APERTURE_LAST;
	mock->domain.pgsize_bitmap = MOCK_IO_PAGE_SIZE;
	xa_init(&mock->pfns);
	return &mock->domain;
}

static void mock_domain_free(struct iommu_domain *domain)
{
	struct mock_iommu_domain *mock =
		container_of(domain, struct mock_iommu_domain, domain);

	WARN_ON(!xa_empty(&mock->pfns));
	kfree(mock);
}

static int mock_domain_map_pages(struct iommu_domain *domain,
				 unsigned long iova, phys_addr_t paddr,
				 size_t pgsize, size_t pgcount, int prot,
				 gfp_t gfp, size_t *mapped)
{
	struct mock_iommu_domain *mock =
		container_of(domain, struct mock_iommu_domain, domain);
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
	struct mock_iommu_domain *mock =
		container_of(domain, struct mock_iommu_domain, domain);
	bool first = true;
	size_t ret = 0;
	void *ent;

	WARN_ON(iova % MOCK_IO_PAGE_SIZE);
	WARN_ON(pgsize % MOCK_IO_PAGE_SIZE);

	for (; pgcount; pgcount--) {
		size_t cur;

		for (cur = 0; cur != pgsize; cur += MOCK_IO_PAGE_SIZE) {
			ent = xa_erase(&mock->pfns, iova / MOCK_IO_PAGE_SIZE);
			WARN_ON(!ent);
			/*
			 * iommufd generates unmaps that must be a strict
			 * superset of the map's performend So every starting
			 * IOVA should have been an iova passed to map, and the
			 *
			 * First IOVA must be present and have been a first IOVA
			 * passed to map_pages
			 */
			if (first) {
				WARN_ON(!(xa_to_value(ent) &
					  MOCK_PFN_START_IOVA));
				first = false;
			}
			if (pgcount == 1 && cur + MOCK_IO_PAGE_SIZE == pgsize)
				WARN_ON(!(xa_to_value(ent) &
					  MOCK_PFN_LAST_IOVA));

			iova += MOCK_IO_PAGE_SIZE;
			ret += MOCK_IO_PAGE_SIZE;
		}
	}
	return ret;
}

static phys_addr_t mock_domain_iova_to_phys(struct iommu_domain *domain,
					    dma_addr_t iova)
{
	struct mock_iommu_domain *mock =
		container_of(domain, struct mock_iommu_domain, domain);
	void *ent;

	WARN_ON(iova % MOCK_IO_PAGE_SIZE);
	ent = xa_load(&mock->pfns, iova / MOCK_IO_PAGE_SIZE);
	WARN_ON(!ent);
	return (xa_to_value(ent) & MOCK_PFN_MASK) * MOCK_IO_PAGE_SIZE;
}

static bool mock_domain_capable(struct device *dev, enum iommu_cap cap)
{
	return cap == IOMMU_CAP_CACHE_COHERENCY;
}

static void mock_domain_set_plaform_dma_ops(struct device *dev)
{
	/*
	 * mock doesn't setup default domains because we can't hook into the
	 * normal probe path
	 */
}

static const struct iommu_ops mock_ops = {
	.owner = THIS_MODULE,
	.pgsize_bitmap = MOCK_IO_PAGE_SIZE,
	.domain_alloc = mock_domain_alloc,
	.capable = mock_domain_capable,
	.set_platform_dma_ops = mock_domain_set_plaform_dma_ops,
	.default_domain_ops =
		&(struct iommu_domain_ops){
			.free = mock_domain_free,
			.attach_dev = mock_domain_nop_attach,
			.map_pages = mock_domain_map_pages,
			.unmap_pages = mock_domain_unmap_pages,
			.iova_to_phys = mock_domain_iova_to_phys,
		},
};

static struct iommu_device mock_iommu_device = {
	.ops = &mock_ops,
};

static inline struct iommufd_hw_pagetable *
get_md_pagetable(struct iommufd_ucmd *ucmd, u32 mockpt_id,
		 struct mock_iommu_domain **mock)
{
	struct iommufd_hw_pagetable *hwpt;
	struct iommufd_object *obj;

	obj = iommufd_get_object(ucmd->ictx, mockpt_id,
				 IOMMUFD_OBJ_HW_PAGETABLE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);
	hwpt = container_of(obj, struct iommufd_hw_pagetable, obj);
	if (hwpt->domain->ops != mock_ops.default_domain_ops) {
		iommufd_put_object(&hwpt->obj);
		return ERR_PTR(-EINVAL);
	}
	*mock = container_of(hwpt->domain, struct mock_iommu_domain, domain);
	return hwpt;
}

static struct bus_type iommufd_mock_bus_type = {
	.name = "iommufd_mock",
	.iommu_ops = &mock_ops,
};

static void mock_dev_release(struct device *dev)
{
	struct mock_dev *mdev = container_of(dev, struct mock_dev, dev);

	kfree(mdev);
}

static struct mock_dev *mock_dev_create(void)
{
	struct iommu_group *iommu_group;
	struct dev_iommu *dev_iommu;
	struct mock_dev *mdev;
	int rc;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	device_initialize(&mdev->dev);
	mdev->dev.release = mock_dev_release;
	mdev->dev.bus = &iommufd_mock_bus_type;

	iommu_group = iommu_group_alloc();
	if (IS_ERR(iommu_group)) {
		rc = PTR_ERR(iommu_group);
		goto err_put;
	}

	rc = dev_set_name(&mdev->dev, "iommufd_mock%u",
			  iommu_group_id(iommu_group));
	if (rc)
		goto err_group;

	/*
	 * The iommu core has no way to associate a single device with an iommu
	 * driver (heck currently it can't even support two iommu_drivers
	 * registering). Hack it together with an open coded dev_iommu_get().
	 * Notice that the normal notifier triggered iommu release process also
	 * does not work here because this bus is not in iommu_buses.
	 */
	mdev->dev.iommu = kzalloc(sizeof(*dev_iommu), GFP_KERNEL);
	if (!mdev->dev.iommu) {
		rc = -ENOMEM;
		goto err_group;
	}
	mutex_init(&mdev->dev.iommu->lock);
	mdev->dev.iommu->iommu_dev = &mock_iommu_device;

	rc = device_add(&mdev->dev);
	if (rc)
		goto err_dev_iommu;

	rc = iommu_group_add_device(iommu_group, &mdev->dev);
	if (rc)
		goto err_del;
	iommu_group_put(iommu_group);
	return mdev;

err_del:
	device_del(&mdev->dev);
err_dev_iommu:
	kfree(mdev->dev.iommu);
	mdev->dev.iommu = NULL;
err_group:
	iommu_group_put(iommu_group);
err_put:
	put_device(&mdev->dev);
	return ERR_PTR(rc);
}

static void mock_dev_destroy(struct mock_dev *mdev)
{
	iommu_group_remove_device(&mdev->dev);
	device_del(&mdev->dev);
	kfree(mdev->dev.iommu);
	mdev->dev.iommu = NULL;
	put_device(&mdev->dev);
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
	u32 idev_id;
	int rc;

	sobj = iommufd_object_alloc(ucmd->ictx, sobj, IOMMUFD_OBJ_SELFTEST);
	if (IS_ERR(sobj))
		return PTR_ERR(sobj);

	sobj->idev.ictx = ucmd->ictx;
	sobj->type = TYPE_IDEV;

	sobj->idev.mock_dev = mock_dev_create();
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
	iommufd_object_finalize(ucmd->ictx, &sobj->obj);
	return iommufd_ucmd_respond(ucmd, sizeof(*cmd));

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

	sobj = container_of(dev_obj, struct selftest_obj, obj);
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
	iommufd_put_object(dev_obj);
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
	iommufd_put_object(&ioas->obj);
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
	iommufd_put_object(&hwpt->obj);
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
		iova = iommufd_test_syz_conv_iova(&staccess->access->ioas->iopt,
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
		iova = iommufd_test_syz_conv_iova(&staccess->access->ioas->iopt,
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

void iommufd_selftest_destroy(struct iommufd_object *obj)
{
	struct selftest_obj *sobj = container_of(obj, struct selftest_obj, obj);

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
	default:
		return -EOPNOTSUPP;
	}
}

bool iommufd_should_fail(void)
{
	return should_fail(&fail_iommufd, 1);
}

void __init iommufd_test_init(void)
{
	dbgfs_root =
		fault_create_debugfs_attr("fail_iommufd", NULL, &fail_iommufd);
	WARN_ON(bus_register(&iommufd_mock_bus_type));
}

void iommufd_test_exit(void)
{
	debugfs_remove_recursive(dbgfs_root);
	bus_unregister(&iommufd_mock_bus_type);
}
