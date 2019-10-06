// SPDX-License-Identifier: GPL-2.0-only
/*
 * Persistent Memory Driver
 *
 * Copyright (c) 2014-2015, Intel Corporation.
 * Copyright (c) 2015, Christoph Hellwig <hch@lst.de>.
 * Copyright (c) 2015, Boaz Harrosh <boaz@plexistor.com>.
 */

#include <asm/cacheflush.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/set_memory.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/badblocks.h>
#include <linux/memremap.h>
#include <linux/vmalloc.h>
#include <linux/blk-mq.h>
#include <linux/pfn_t.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/dax.h>
#include <linux/nd.h>
#include <linux/backing-dev.h>
#include "pmem.h"
#include "pfn.h"
#include "nd.h"
#include "nd-core.h"

static struct device *to_dev(struct pmem_device *pmem)
{
	/*
	 * nvdimm bus services need a 'dev' parameter, and we record the device
	 * at init in bb.dev.
	 */
	return pmem->bb.dev;
}

static struct nd_region *to_region(struct pmem_device *pmem)
{
	return to_nd_region(to_dev(pmem)->parent);
}

static void hwpoison_clear(struct pmem_device *pmem,
		phys_addr_t phys, unsigned int len)
{
	unsigned long pfn_start, pfn_end, pfn;

	/* only pmem in the linear map supports HWPoison */
	if (is_vmalloc_addr(pmem->virt_addr))
		return;

	pfn_start = PHYS_PFN(phys);
	pfn_end = pfn_start + PHYS_PFN(len);
	for (pfn = pfn_start; pfn < pfn_end; pfn++) {
		struct page *page = pfn_to_page(pfn);

		/*
		 * Note, no need to hold a get_dev_pagemap() reference
		 * here since we're in the driver I/O path and
		 * outstanding I/O requests pin the dev_pagemap.
		 */
		if (test_and_clear_pmem_poison(page))
			clear_mce_nospec(pfn);
	}
}

static blk_status_t pmem_clear_poison(struct pmem_device *pmem,
		phys_addr_t offset, unsigned int len)
{
	struct device *dev = to_dev(pmem);
	sector_t sector;
	long cleared;
	blk_status_t rc = BLK_STS_OK;

	sector = (offset - pmem->data_offset) / 512;

	cleared = nvdimm_clear_poison(dev, pmem->phys_addr + offset, len);
	if (cleared < len)
		rc = BLK_STS_IOERR;
	if (cleared > 0 && cleared / 512) {
		hwpoison_clear(pmem, pmem->phys_addr + offset, cleared);
		cleared /= 512;
		dev_dbg(dev, "%#llx clear %ld sector%s\n",
				(unsigned long long) sector, cleared,
				cleared > 1 ? "s" : "");
		badblocks_clear(&pmem->bb, sector, cleared);
		if (pmem->bb_state)
			sysfs_notify_dirent(pmem->bb_state);
	}

	arch_invalidate_pmem(pmem->virt_addr + offset, len);

	return rc;
}

static void write_pmem(void *pmem_addr, struct page *page,
		unsigned int off, unsigned int len)
{
	unsigned int chunk;
	void *mem;

	while (len) {
		mem = kmap_atomic(page);
		chunk = min_t(unsigned int, len, PAGE_SIZE - off);
		memcpy_flushcache(pmem_addr, mem + off, chunk);
		kunmap_atomic(mem);
		len -= chunk;
		off = 0;
		page++;
		pmem_addr += chunk;
	}
}

static blk_status_t read_pmem(struct page *page, unsigned int off,
		void *pmem_addr, unsigned int len)
{
	unsigned int chunk;
	unsigned long rem;
	void *mem;

	while (len) {
		mem = kmap_atomic(page);
		chunk = min_t(unsigned int, len, PAGE_SIZE - off);
		rem = memcpy_mcsafe(mem + off, pmem_addr, chunk);
		kunmap_atomic(mem);
		if (rem)
			return BLK_STS_IOERR;
		len -= chunk;
		off = 0;
		page++;
		pmem_addr += chunk;
	}
	return BLK_STS_OK;
}

static blk_status_t pmem_do_bvec(struct pmem_device *pmem, struct page *page,
			unsigned int len, unsigned int off, unsigned int op,
			sector_t sector)
{
	blk_status_t rc = BLK_STS_OK;
	bool bad_pmem = false;
	phys_addr_t pmem_off = sector * 512 + pmem->data_offset;
	void *pmem_addr = pmem->virt_addr + pmem_off;

	if (unlikely(is_bad_pmem(&pmem->bb, sector, len)))
		bad_pmem = true;

	if (!op_is_write(op)) {
		if (unlikely(bad_pmem))
			rc = BLK_STS_IOERR;
		else {
			rc = read_pmem(page, off, pmem_addr, len);
			flush_dcache_page(page);
		}
	} else {
		/*
		 * Note that we write the data both before and after
		 * clearing poison.  The write before clear poison
		 * handles situations where the latest written data is
		 * preserved and the clear poison operation simply marks
		 * the address range as valid without changing the data.
		 * In this case application software can assume that an
		 * interrupted write will either return the new good
		 * data or an error.
		 *
		 * However, if pmem_clear_poison() leaves the data in an
		 * indeterminate state we need to perform the write
		 * after clear poison.
		 */
		flush_dcache_page(page);
		write_pmem(pmem_addr, page, off, len);
		if (unlikely(bad_pmem)) {
			rc = pmem_clear_poison(pmem, pmem_off, len);
			write_pmem(pmem_addr, page, off, len);
		}
	}

	return rc;
}

static blk_qc_t pmem_make_request(struct request_queue *q, struct bio *bio)
{
	int ret = 0;
	blk_status_t rc = 0;
	bool do_acct;
	unsigned long start;
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct pmem_device *pmem = q->queuedata;
	struct nd_region *nd_region = to_region(pmem);

	if (bio->bi_opf & REQ_PREFLUSH)
		ret = nvdimm_flush(nd_region, bio);

	do_acct = nd_iostat_start(bio, &start);
	bio_for_each_segment(bvec, bio, iter) {
		rc = pmem_do_bvec(pmem, bvec.bv_page, bvec.bv_len,
				bvec.bv_offset, bio_op(bio), iter.bi_sector);
		if (rc) {
			bio->bi_status = rc;
			break;
		}
	}
	if (do_acct)
		nd_iostat_end(bio, start);

	if (bio->bi_opf & REQ_FUA)
		ret = nvdimm_flush(nd_region, bio);

	if (ret)
		bio->bi_status = errno_to_blk_status(ret);

	bio_endio(bio);
	return BLK_QC_T_NONE;
}

static int pmem_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, unsigned int op)
{
	struct pmem_device *pmem = bdev->bd_queue->queuedata;
	blk_status_t rc;

	rc = pmem_do_bvec(pmem, page, hpage_nr_pages(page) * PAGE_SIZE,
			  0, op, sector);

	/*
	 * The ->rw_page interface is subtle and tricky.  The core
	 * retries on any error, so we can only invoke page_endio() in
	 * the successful completion case.  Otherwise, we'll see crashes
	 * caused by double completion.
	 */
	if (rc == 0)
		page_endio(page, op_is_write(op), 0);

	return blk_status_to_errno(rc);
}

/* see "strong" declaration in tools/testing/nvdimm/pmem-dax.c */
__weak long __pmem_direct_access(struct pmem_device *pmem, pgoff_t pgoff,
		long nr_pages, void **kaddr, pfn_t *pfn)
{
	resource_size_t offset = PFN_PHYS(pgoff) + pmem->data_offset;

	if (unlikely(is_bad_pmem(&pmem->bb, PFN_PHYS(pgoff) / 512,
					PFN_PHYS(nr_pages))))
		return -EIO;

	if (kaddr)
		*kaddr = pmem->virt_addr + offset;
	if (pfn)
		*pfn = phys_to_pfn_t(pmem->phys_addr + offset, pmem->pfn_flags);

	/*
	 * If badblocks are present, limit known good range to the
	 * requested range.
	 */
	if (unlikely(pmem->bb.count))
		return nr_pages;
	return PHYS_PFN(pmem->size - pmem->pfn_pad - offset);
}

static const struct block_device_operations pmem_fops = {
	.owner =		THIS_MODULE,
	.rw_page =		pmem_rw_page,
	.revalidate_disk =	nvdimm_revalidate_disk,
};

static long pmem_dax_direct_access(struct dax_device *dax_dev,
		pgoff_t pgoff, long nr_pages, void **kaddr, pfn_t *pfn)
{
	struct pmem_device *pmem = dax_get_private(dax_dev);

	return __pmem_direct_access(pmem, pgoff, nr_pages, kaddr, pfn);
}

/*
 * Use the 'no check' versions of copy_from_iter_flushcache() and
 * copy_to_iter_mcsafe() to bypass HARDENED_USERCOPY overhead. Bounds
 * checking, both file offset and device offset, is handled by
 * dax_iomap_actor()
 */
static size_t pmem_copy_from_iter(struct dax_device *dax_dev, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i)
{
	return _copy_from_iter_flushcache(addr, bytes, i);
}

static size_t pmem_copy_to_iter(struct dax_device *dax_dev, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i)
{
	return _copy_to_iter_mcsafe(addr, bytes, i);
}

static const struct dax_operations pmem_dax_ops = {
	.direct_access = pmem_dax_direct_access,
	.dax_supported = generic_fsdax_supported,
	.copy_from_iter = pmem_copy_from_iter,
	.copy_to_iter = pmem_copy_to_iter,
};

static const struct attribute_group *pmem_attribute_groups[] = {
	&dax_attribute_group,
	NULL,
};

static void pmem_pagemap_cleanup(struct dev_pagemap *pgmap)
{
	struct request_queue *q =
		container_of(pgmap->ref, struct request_queue, q_usage_counter);

	blk_cleanup_queue(q);
}

static void pmem_release_queue(void *pgmap)
{
	pmem_pagemap_cleanup(pgmap);
}

static void pmem_pagemap_kill(struct dev_pagemap *pgmap)
{
	struct request_queue *q =
		container_of(pgmap->ref, struct request_queue, q_usage_counter);

	blk_freeze_queue_start(q);
}

static void pmem_release_disk(void *__pmem)
{
	struct pmem_device *pmem = __pmem;

	kill_dax(pmem->dax_dev);
	put_dax(pmem->dax_dev);
	del_gendisk(pmem->disk);
	put_disk(pmem->disk);
}

static void pmem_pagemap_page_free(struct page *page)
{
	wake_up_var(&page->_refcount);
}

static const struct dev_pagemap_ops fsdax_pagemap_ops = {
	.page_free		= pmem_pagemap_page_free,
	.kill			= pmem_pagemap_kill,
	.cleanup		= pmem_pagemap_cleanup,
};

static int pmem_attach_disk(struct device *dev,
		struct nd_namespace_common *ndns)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	struct nd_region *nd_region = to_nd_region(dev->parent);
	int nid = dev_to_node(dev), fua;
	struct resource *res = &nsio->res;
	struct resource bb_res;
	struct nd_pfn *nd_pfn = NULL;
	struct dax_device *dax_dev;
	struct nd_pfn_sb *pfn_sb;
	struct pmem_device *pmem;
	struct request_queue *q;
	struct device *gendev;
	struct gendisk *disk;
	void *addr;
	int rc;
	unsigned long flags = 0UL;

	pmem = devm_kzalloc(dev, sizeof(*pmem), GFP_KERNEL);
	if (!pmem)
		return -ENOMEM;

	/* while nsio_rw_bytes is active, parse a pfn info block if present */
	if (is_nd_pfn(dev)) {
		nd_pfn = to_nd_pfn(dev);
		rc = nvdimm_setup_pfn(nd_pfn, &pmem->pgmap);
		if (rc)
			return rc;
	}

	/* we're attaching a block device, disable raw namespace access */
	devm_nsio_disable(dev, nsio);

	dev_set_drvdata(dev, pmem);
	pmem->phys_addr = res->start;
	pmem->size = resource_size(res);
	fua = nvdimm_has_flush(nd_region);
	if (!IS_ENABLED(CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE) || fua < 0) {
		dev_warn(dev, "unable to guarantee persistence of writes\n");
		fua = 0;
	}

	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				dev_name(&ndns->dev))) {
		dev_warn(dev, "could not reserve region %pR\n", res);
		return -EBUSY;
	}

	q = blk_alloc_queue_node(GFP_KERNEL, dev_to_node(dev));
	if (!q)
		return -ENOMEM;

	pmem->pfn_flags = PFN_DEV;
	pmem->pgmap.ref = &q->q_usage_counter;
	if (is_nd_pfn(dev)) {
		pmem->pgmap.type = MEMORY_DEVICE_FS_DAX;
		pmem->pgmap.ops = &fsdax_pagemap_ops;
		addr = devm_memremap_pages(dev, &pmem->pgmap);
		pfn_sb = nd_pfn->pfn_sb;
		pmem->data_offset = le64_to_cpu(pfn_sb->dataoff);
		pmem->pfn_pad = resource_size(res) -
			resource_size(&pmem->pgmap.res);
		pmem->pfn_flags |= PFN_MAP;
		memcpy(&bb_res, &pmem->pgmap.res, sizeof(bb_res));
		bb_res.start += pmem->data_offset;
	} else if (pmem_should_map_pages(dev)) {
		memcpy(&pmem->pgmap.res, &nsio->res, sizeof(pmem->pgmap.res));
		pmem->pgmap.type = MEMORY_DEVICE_FS_DAX;
		pmem->pgmap.ops = &fsdax_pagemap_ops;
		addr = devm_memremap_pages(dev, &pmem->pgmap);
		pmem->pfn_flags |= PFN_MAP;
		memcpy(&bb_res, &pmem->pgmap.res, sizeof(bb_res));
	} else {
		if (devm_add_action_or_reset(dev, pmem_release_queue,
					&pmem->pgmap))
			return -ENOMEM;
		addr = devm_memremap(dev, pmem->phys_addr,
				pmem->size, ARCH_MEMREMAP_PMEM);
		memcpy(&bb_res, &nsio->res, sizeof(bb_res));
	}

	if (IS_ERR(addr))
		return PTR_ERR(addr);
	pmem->virt_addr = addr;

	blk_queue_write_cache(q, true, fua);
	blk_queue_make_request(q, pmem_make_request);
	blk_queue_physical_block_size(q, PAGE_SIZE);
	blk_queue_logical_block_size(q, pmem_sector_size(ndns));
	blk_queue_max_hw_sectors(q, UINT_MAX);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, q);
	if (pmem->pfn_flags & PFN_MAP)
		blk_queue_flag_set(QUEUE_FLAG_DAX, q);
	q->queuedata = pmem;

	disk = alloc_disk_node(0, nid);
	if (!disk)
		return -ENOMEM;
	pmem->disk = disk;

	disk->fops		= &pmem_fops;
	disk->queue		= q;
	disk->flags		= GENHD_FL_EXT_DEVT;
	disk->queue->backing_dev_info->capabilities |= BDI_CAP_SYNCHRONOUS_IO;
	nvdimm_namespace_disk_name(ndns, disk->disk_name);
	set_capacity(disk, (pmem->size - pmem->pfn_pad - pmem->data_offset)
			/ 512);
	if (devm_init_badblocks(dev, &pmem->bb))
		return -ENOMEM;
	nvdimm_badblocks_populate(nd_region, &pmem->bb, &bb_res);
	disk->bb = &pmem->bb;

	if (is_nvdimm_sync(nd_region))
		flags = DAXDEV_F_SYNC;
	dax_dev = alloc_dax(pmem, disk->disk_name, &pmem_dax_ops, flags);
	if (!dax_dev) {
		put_disk(disk);
		return -ENOMEM;
	}
	dax_write_cache(dax_dev, nvdimm_has_cache(nd_region));
	pmem->dax_dev = dax_dev;
	gendev = disk_to_dev(disk);
	gendev->groups = pmem_attribute_groups;

	device_add_disk(dev, disk, NULL);
	if (devm_add_action_or_reset(dev, pmem_release_disk, pmem))
		return -ENOMEM;

	revalidate_disk(disk);

	pmem->bb_state = sysfs_get_dirent(disk_to_dev(disk)->kobj.sd,
					  "badblocks");
	if (!pmem->bb_state)
		dev_warn(dev, "'badblocks' notification disabled\n");

	return 0;
}

static int nd_pmem_probe(struct device *dev)
{
	int ret;
	struct nd_namespace_common *ndns;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);

	if (devm_nsio_enable(dev, to_nd_namespace_io(&ndns->dev)))
		return -ENXIO;

	if (is_nd_btt(dev))
		return nvdimm_namespace_attach_btt(ndns);

	if (is_nd_pfn(dev))
		return pmem_attach_disk(dev, ndns);

	ret = nd_btt_probe(dev, ndns);
	if (ret == 0)
		return -ENXIO;

	/*
	 * We have two failure conditions here, there is no
	 * info reserver block or we found a valid info reserve block
	 * but failed to initialize the pfn superblock.
	 *
	 * For the first case consider namespace as a raw pmem namespace
	 * and attach a disk.
	 *
	 * For the latter, consider this a success and advance the namespace
	 * seed.
	 */
	ret = nd_pfn_probe(dev, ndns);
	if (ret == 0)
		return -ENXIO;
	else if (ret == -EOPNOTSUPP)
		return ret;

	ret = nd_dax_probe(dev, ndns);
	if (ret == 0)
		return -ENXIO;
	else if (ret == -EOPNOTSUPP)
		return ret;
	return pmem_attach_disk(dev, ndns);
}

static int nd_pmem_remove(struct device *dev)
{
	struct pmem_device *pmem = dev_get_drvdata(dev);

	if (is_nd_btt(dev))
		nvdimm_namespace_detach_btt(to_nd_btt(dev));
	else {
		/*
		 * Note, this assumes nd_device_lock() context to not
		 * race nd_pmem_notify()
		 */
		sysfs_put(pmem->bb_state);
		pmem->bb_state = NULL;
	}
	nvdimm_flush(to_nd_region(dev->parent), NULL);

	return 0;
}

static void nd_pmem_shutdown(struct device *dev)
{
	nvdimm_flush(to_nd_region(dev->parent), NULL);
}

static void nd_pmem_notify(struct device *dev, enum nvdimm_event event)
{
	struct nd_region *nd_region;
	resource_size_t offset = 0, end_trunc = 0;
	struct nd_namespace_common *ndns;
	struct nd_namespace_io *nsio;
	struct resource res;
	struct badblocks *bb;
	struct kernfs_node *bb_state;

	if (event != NVDIMM_REVALIDATE_POISON)
		return;

	if (is_nd_btt(dev)) {
		struct nd_btt *nd_btt = to_nd_btt(dev);

		ndns = nd_btt->ndns;
		nd_region = to_nd_region(ndns->dev.parent);
		nsio = to_nd_namespace_io(&ndns->dev);
		bb = &nsio->bb;
		bb_state = NULL;
	} else {
		struct pmem_device *pmem = dev_get_drvdata(dev);

		nd_region = to_region(pmem);
		bb = &pmem->bb;
		bb_state = pmem->bb_state;

		if (is_nd_pfn(dev)) {
			struct nd_pfn *nd_pfn = to_nd_pfn(dev);
			struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;

			ndns = nd_pfn->ndns;
			offset = pmem->data_offset +
					__le32_to_cpu(pfn_sb->start_pad);
			end_trunc = __le32_to_cpu(pfn_sb->end_trunc);
		} else {
			ndns = to_ndns(dev);
		}

		nsio = to_nd_namespace_io(&ndns->dev);
	}

	res.start = nsio->res.start + offset;
	res.end = nsio->res.end - end_trunc;
	nvdimm_badblocks_populate(nd_region, bb, &res);
	if (bb_state)
		sysfs_notify_dirent(bb_state);
}

MODULE_ALIAS("pmem");
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_IO);
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_PMEM);
static struct nd_device_driver nd_pmem_driver = {
	.probe = nd_pmem_probe,
	.remove = nd_pmem_remove,
	.notify = nd_pmem_notify,
	.shutdown = nd_pmem_shutdown,
	.drv = {
		.name = "nd_pmem",
	},
	.type = ND_DRIVER_NAMESPACE_IO | ND_DRIVER_NAMESPACE_PMEM,
};

module_nd_driver(nd_pmem_driver);

MODULE_AUTHOR("Ross Zwisler <ross.zwisler@linux.intel.com>");
MODULE_LICENSE("GPL v2");
