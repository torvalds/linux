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

	ioas = iommufd_get_ioas(ucmd, ioas_id);
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

struct selftest_obj {
	struct iommufd_object obj;
	enum selftest_obj_type type;

	union {
		struct {
			struct iommufd_hw_pagetable *hwpt;
			struct iommufd_ctx *ictx;
			struct device mock_dev;
		} idev;
	};
};

static struct iommu_domain *mock_domain_alloc(unsigned int iommu_domain_type)
{
	struct mock_iommu_domain *mock;

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

static const struct iommu_ops mock_ops = {
	.owner = THIS_MODULE,
	.pgsize_bitmap = MOCK_IO_PAGE_SIZE,
	.domain_alloc = mock_domain_alloc,
	.default_domain_ops =
		&(struct iommu_domain_ops){
			.free = mock_domain_free,
			.map_pages = mock_domain_map_pages,
			.unmap_pages = mock_domain_unmap_pages,
			.iova_to_phys = mock_domain_iova_to_phys,
		},
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

/* Create an hw_pagetable with the mock domain so we can test the domain ops */
static int iommufd_test_mock_domain(struct iommufd_ucmd *ucmd,
				    struct iommu_test_cmd *cmd)
{
	static struct bus_type mock_bus = { .iommu_ops = &mock_ops };
	struct iommufd_hw_pagetable *hwpt;
	struct selftest_obj *sobj;
	struct iommufd_ioas *ioas;
	int rc;

	ioas = iommufd_get_ioas(ucmd, cmd->id);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	sobj = iommufd_object_alloc(ucmd->ictx, sobj, IOMMUFD_OBJ_SELFTEST);
	if (IS_ERR(sobj)) {
		rc = PTR_ERR(sobj);
		goto out_ioas;
	}
	sobj->idev.ictx = ucmd->ictx;
	sobj->type = TYPE_IDEV;
	sobj->idev.mock_dev.bus = &mock_bus;

	hwpt = iommufd_device_selftest_attach(ucmd->ictx, ioas,
					      &sobj->idev.mock_dev);
	if (IS_ERR(hwpt)) {
		rc = PTR_ERR(hwpt);
		goto out_sobj;
	}
	sobj->idev.hwpt = hwpt;

	/* Userspace must destroy both of these IDs to destroy the object */
	cmd->mock_domain.out_hwpt_id = hwpt->obj.id;
	cmd->mock_domain.out_device_id = sobj->obj.id;
	iommufd_object_finalize(ucmd->ictx, &sobj->obj);
	iommufd_put_object(&ioas->obj);
	return iommufd_ucmd_respond(ucmd, sizeof(*cmd));

out_sobj:
	iommufd_object_abort(ucmd->ictx, &sobj->obj);
out_ioas:
	iommufd_put_object(&ioas->obj);
	return rc;
}

/* Add an additional reserved IOVA to the IOAS */
static int iommufd_test_add_reserved(struct iommufd_ucmd *ucmd,
				     unsigned int mockpt_id,
				     unsigned long start, size_t length)
{
	struct iommufd_ioas *ioas;
	int rc;

	ioas = iommufd_get_ioas(ucmd, mockpt_id);
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
	int rc;

	if (iova % MOCK_IO_PAGE_SIZE || length % MOCK_IO_PAGE_SIZE ||
	    (uintptr_t)uptr % MOCK_IO_PAGE_SIZE)
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
	if (length % PAGE_SIZE || (uintptr_t)uptr % PAGE_SIZE)
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
		ucmd->ictx, ioas_id,
		(flags & MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES) ?
			&selftest_access_ops_pin :
			&selftest_access_ops,
		staccess);
	if (IS_ERR(access)) {
		rc = PTR_ERR(access);
		goto out_put_fdno;
	}
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
		iommufd_device_selftest_detach(sobj->idev.ictx,
					       sobj->idev.hwpt);
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
}

void iommufd_test_exit(void)
{
	debugfs_remove_recursive(dbgfs_root);
}
