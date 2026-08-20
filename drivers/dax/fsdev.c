// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2026 Micron Technology, Inc. */
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/uio.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "dax-private.h"
#include "bus.h"

/*
 * FS-DAX compatible devdax driver
 *
 * Unlike drivers/dax/device.c which pre-initializes compound folios based
 * on device alignment (via vmemmap_shift), this driver leaves folios
 * uninitialized similar to pmem. This allows fs-dax filesystems like famfs
 * to work without needing special handling for pre-initialized folios.
 *
 * Key differences from device.c:
 * - pgmap type is MEMORY_DEVICE_FS_DAX (not MEMORY_DEVICE_GENERIC)
 * - vmemmap_shift is NOT set (folios remain order-0)
 * - fs-dax can dynamically create compound folios as needed
 * - No mmap support - all access is through fs-dax/iomap
 */

static void fsdev_write_dax(void *addr, struct page *page,
		unsigned int off, unsigned int len)
{
	while (len) {
		void *mem = kmap_local_page(page);
		unsigned int chunk = min_t(unsigned int, len, PAGE_SIZE - off);

		memcpy_flushcache(addr, mem + off, chunk);
		kunmap_local(mem);
		len -= chunk;
		off = 0;
		page++;
		addr += chunk;
	}
}

static long __fsdev_dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff,
		long nr_pages, enum dax_access_mode mode, void **kaddr,
		unsigned long *pfn)
{
	struct dev_dax *dev_dax = dax_get_private(dax_dev);
	size_t size = nr_pages << PAGE_SHIFT;
	size_t offset = pgoff << PAGE_SHIFT;
	phys_addr_t phys;

	phys = dax_pgoff_to_phys(dev_dax, pgoff, size);
	if (phys == -1) {
		dev_dbg(&dev_dax->dev,
			"pgoff (%#lx) out of range\n", pgoff);
		return -EFAULT;
	}

	if (kaddr)
		*kaddr = __va(phys);

	if (pfn)
		*pfn = PHYS_PFN(phys);

	/*
	 * Use cached_size which was computed at probe time. The size cannot
	 * change while the driver is bound (resize returns -EBUSY).
	 */
	return PHYS_PFN(min(size, dev_dax->cached_size - offset));
}

static int fsdev_dax_zero_page_range(struct dax_device *dax_dev,
			pgoff_t pgoff, size_t nr_pages)
{
	void *kaddr;
	long rc;

	WARN_ONCE(nr_pages > 1, "%s: nr_pages > 1\n", __func__);
	rc = __fsdev_dax_direct_access(dax_dev, pgoff, 1, DAX_ACCESS,
				       &kaddr, NULL);
	if (rc < 0)
		return rc;
	fsdev_write_dax(kaddr, ZERO_PAGE(0), 0, PAGE_SIZE);
	return 0;
}

static long fsdev_dax_direct_access(struct dax_device *dax_dev,
		pgoff_t pgoff, long nr_pages, enum dax_access_mode mode,
		void **kaddr, unsigned long *pfn)
{
	return __fsdev_dax_direct_access(dax_dev, pgoff, nr_pages, mode,
					 kaddr, pfn);
}

static size_t fsdev_dax_recovery_write(struct dax_device *dax_dev,
		pgoff_t pgoff, void *addr, size_t bytes, struct iov_iter *i)
{
	return _copy_from_iter_flushcache(addr, bytes, i);
}

static const struct dax_operations dev_dax_ops = {
	.direct_access = fsdev_dax_direct_access,
	.zero_page_range = fsdev_dax_zero_page_range,
	.recovery_write = fsdev_dax_recovery_write,
};

static void fsdev_cdev_del(void *cdev)
{
	cdev_del(cdev);
}

static void fsdev_kill(void *dev_dax)
{
	kill_dev_dax(dev_dax);
}

static void fsdev_clear_ops(void *data)
{
	struct dev_dax *dev_dax = data;

	dax_set_ops(dev_dax->dax_dev, NULL);
}

static void fsdev_clear_pgmap_ops(void *data)
{
	struct dev_pagemap *pgmap = data;

	/*
	 * fsdev installs pgmap->ops and ->owner at probe. For a static device
	 * the pgmap is shared and long-lived (owned by the dax bus), so
	 * leaving fsdev's ops behind on unbind would let a later
	 * memory_failure -- after rebind to another driver, or after this
	 * module is unloaded -- dispatch through a stale or freed
	 * ->memory_failure handler. Clear them so the pgmap carries no fsdev
	 * state once we are unbound.
	 */
	pgmap->ops = NULL;
	pgmap->owner = NULL;
}

/*
 * Page map operations for FS-DAX mode
 * Similar to fsdax_pagemap_ops in drivers/nvdimm/pmem.c
 *
 * Note: folio_free callback is not needed for MEMORY_DEVICE_FS_DAX.
 * The core mm code in free_zone_device_folio() handles the wake_up_var()
 * directly for this memory type.
 */
static u64 fsdev_pfn_to_offset(struct dev_dax *dev_dax, unsigned long pfn)
{
	phys_addr_t phys = PFN_PHYS(pfn);
	u64 offset = 0;

	for (int i = 0; i < dev_dax->nr_range; i++) {
		struct range *range = &dev_dax->ranges[i].range;

		if (phys >= range->start && phys <= range->end)
			return offset + (phys - range->start);
		offset += range_len(range);
	}
	return -1ULL;
}

static int fsdev_pagemap_memory_failure(struct dev_pagemap *pgmap,
		unsigned long pfn, unsigned long nr_pages, int mf_flags)
{
	struct dev_dax *dev_dax = pgmap->owner;
	u64 offset = fsdev_pfn_to_offset(dev_dax, pfn);
	u64 len = nr_pages << PAGE_SHIFT;

	return dax_holder_notify_failure(dev_dax->dax_dev, offset,
					 len, mf_flags);
}

static const struct dev_pagemap_ops fsdev_pagemap_ops = {
	.memory_failure		= fsdev_pagemap_memory_failure,
};

/*
 * Clear any stale folio state from pages in the given range.
 * This is necessary because device_dax pre-initializes compound folios
 * based on vmemmap_shift, and that state may persist after driver unbind.
 * Since fsdev_dax uses MEMORY_DEVICE_FS_DAX without vmemmap_shift, fs-dax
 * expects to find clean order-0 folios that it can build into compound
 * folios on demand.
 *
 * At probe time, no filesystem should be mounted yet, so all mappings
 * are stale and must be cleared along with compound state.
 */
static void fsdev_clear_folio_state(struct dev_dax *dev_dax)
{
	for (int i = 0; i < dev_dax->nr_range; i++) {
		struct range *range = &dev_dax->ranges[i].range;
		unsigned long pfn = PHYS_PFN(range->start);
		unsigned long end_pfn = PHYS_PFN(range->end) + 1;

		while (pfn < end_pfn) {
			struct folio *folio = pfn_folio(pfn);
			int order = dax_folio_reset_order(folio);

			pfn += 1UL << order;
		}
	}
}

static void fsdev_clear_folio_state_action(void *data)
{
	fsdev_clear_folio_state(data);
}

static int fsdev_open(struct inode *inode, struct file *filp)
{
	struct dax_device *dax_dev = inode_dax(inode);
	struct dev_dax *dev_dax = dax_get_private(dax_dev);

	filp->private_data = dev_dax;

	return 0;
}

static int fsdev_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations fsdev_fops = {
	.llseek = noop_llseek,
	.owner = THIS_MODULE,
	.open = fsdev_open,
	.release = fsdev_release,
};

/*
 * Acquire the dev_pagemap for probe: the static (pre-populated) one if
 * present, or a devm-allocated one for the dynamic case. Note that
 * dev_dax->pgmap is not set here; fsdev_dax_probe() sets it only once
 * probe succeeds, so a failed probe never leaves a dangling pointer
 * to a devres-freed pgmap.
 */
static struct dev_pagemap *fsdev_acquire_pgmap(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	struct dev_pagemap *pgmap;
	size_t pgmap_size;

	if (static_dev_dax(dev_dax)) {
		if (dev_dax->nr_range > 1) {
			dev_warn(dev,
				 "static pgmap / multi-range device conflict\n");
			return ERR_PTR(-EINVAL);
		}

		pgmap = dev_dax->pgmap;
		pgmap->vmemmap_shift = 0;
		return pgmap;
	}

	if (dev_dax->pgmap) {
		dev_warn(dev, "dynamic-dax with pre-populated page map\n");
		return ERR_PTR(-EINVAL);
	}

	pgmap_size = struct_size(pgmap, ranges, dev_dax->nr_range - 1);
	pgmap = devm_kzalloc(dev, pgmap_size, GFP_KERNEL);
	if (!pgmap)
		return ERR_PTR(-ENOMEM);

	pgmap->nr_range = dev_dax->nr_range;
	for (int i = 0; i < dev_dax->nr_range; i++)
		pgmap->ranges[i] = dev_dax->ranges[i].range;

	return pgmap;
}

static int fsdev_dax_probe(struct dev_dax *dev_dax)
{
	struct dax_device *dax_dev = dev_dax->dax_dev;
	struct device *dev = &dev_dax->dev;
	struct dev_pagemap *pgmap;
	struct inode *inode;
	u64 data_offset = 0;
	struct cdev *cdev;
	void *addr;
	int rc, i;

	pgmap = fsdev_acquire_pgmap(dev_dax);
	if (IS_ERR(pgmap))
		return PTR_ERR(pgmap);

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range *range = &dev_dax->ranges[i].range;

		if (!devm_request_mem_region(dev, range->start,
					range_len(range), dev_name(dev))) {
			dev_warn(dev, "mapping%d: %#llx-%#llx could not reserve range\n",
				 i, range->start, range->end);
			return -EBUSY;
		}
	}

	/* Cache size now; it cannot change while driver is bound */
	dev_dax->cached_size = 0;
	for (i = 0; i < dev_dax->nr_range; i++)
		dev_dax->cached_size += range_len(&dev_dax->ranges[i].range);

	/*
	 * Use MEMORY_DEVICE_FS_DAX without setting vmemmap_shift, leaving
	 * folios at order-0. Unlike device.c (MEMORY_DEVICE_GENERIC), this
	 * lets fs-dax dynamically build compound folios as needed, similar
	 * to pmem behavior.
	 */
	pgmap->type = MEMORY_DEVICE_FS_DAX;
	pgmap->ops = &fsdev_pagemap_ops;
	pgmap->owner = dev_dax;

	addr = devm_memremap_pages(dev, pgmap);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	/* Drop fsdev's pgmap->ops/owner on unbind so no stale ops survive. */
	rc = devm_add_action_or_reset(dev, fsdev_clear_pgmap_ops, pgmap);
	if (rc)
		return rc;

	/*
	 * Clear any stale compound folio state left over from a previous
	 * driver (e.g., device_dax with vmemmap_shift). Also register this
	 * as a devm action so folio state is cleared on unbind, ensuring
	 * clean pages for subsequent drivers (e.g., kmem for system-ram).
	 */
	fsdev_clear_folio_state(dev_dax);
	rc = devm_add_action_or_reset(dev, fsdev_clear_folio_state_action,
				      dev_dax);
	if (rc)
		return rc;

	/* Detect whether the data is at a non-zero offset into the memory */
	if (pgmap->range.start != dev_dax->ranges[0].range.start) {
		u64 phys = dev_dax->ranges[0].range.start;
		u64 pgmap_phys = pgmap[0].range.start;

		if (pgmap_phys > phys) {
			dev_err(dev, "pgmap start %#llx exceeds data start %#llx\n",
				pgmap_phys, phys);
			return -EINVAL;
		}
		data_offset = phys - pgmap_phys;

		pr_debug("%s: offset detected phys=%llx pgmap_phys=%llx offset=%llx\n",
		       __func__, phys, pgmap_phys, data_offset);
	}

	inode = dax_inode(dax_dev);
	cdev = inode->i_cdev;
	cdev_init(cdev, &fsdev_fops);
	cdev->owner = dev->driver->owner;
	cdev_set_parent(cdev, &dev->kobj);
	rc = cdev_add(cdev, dev->devt, 1);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, fsdev_cdev_del, cdev);
	if (rc)
		return rc;

	/* Set the dax operations for fs-dax access path */
	rc = dax_set_ops(dax_dev, &dev_dax_ops);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, fsdev_clear_ops, dev_dax);
	if (rc)
		return rc;

	run_dax(dax_dev);
	rc = devm_add_action_or_reset(dev, fsdev_kill, dev_dax);
	if (rc)
		return rc;

	/* Probe can no longer fail; expose the pgmap via dev_dax */
	dev_dax->pgmap = pgmap;
	return 0;
}

static struct dax_device_driver fsdev_dax_driver = {
	.probe = fsdev_dax_probe,
	.type = DAXDRV_FSDEV_TYPE,
};

static int __init dax_init(void)
{
	return dax_driver_register(&fsdev_dax_driver);
}

static void __exit dax_exit(void)
{
	dax_driver_unregister(&fsdev_dax_driver);
}

MODULE_AUTHOR("John Groves");
MODULE_DESCRIPTION("FS-DAX Device: fs-dax compatible devdax driver");
MODULE_LICENSE("GPL");
module_init(dax_init);
module_exit(dax_exit);
MODULE_ALIAS_DAX_DEVICE(0);
