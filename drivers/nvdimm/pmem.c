/*
 * Persistent Memory Driver
 *
 * Copyright (c) 2014-2015, Intel Corporation.
 * Copyright (c) 2015, Christoph Hellwig <hch@lst.de>.
 * Copyright (c) 2015, Boaz Harrosh <boaz@plexistor.com>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <asm/cacheflush.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/badblocks.h>
#include <linux/memremap.h>
#include <linux/vmalloc.h>
#include <linux/blk-mq.h>
#include <linux/pfn_t.h>
#include <linux/slab.h>
#include <linux/pmem.h>
#include <linux/dax.h>
#include <linux/nd.h>
#include "pmem.h"
#include "pfn.h"
#include "nd.h"

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

static int pmem_clear_poison(struct pmem_device *pmem, phys_addr_t offset,
		unsigned int len)
{
	struct device *dev = to_dev(pmem);
	sector_t sector;
	long cleared;
	int rc = 0;

	sector = (offset - pmem->data_offset) / 512;

	cleared = nvdimm_clear_poison(dev, pmem->phys_addr + offset, len);
	if (cleared < len)
		rc = -EIO;
	if (cleared > 0 && cleared / 512) {
		cleared /= 512;
		dev_dbg(dev, "%s: %#llx clear %ld sector%s\n", __func__,
				(unsigned long long) sector, cleared,
				cleared > 1 ? "s" : "");
		badblocks_clear(&pmem->bb, sector, cleared);
	}

	invalidate_pmem(pmem->virt_addr + offset, len);

	return rc;
}

static void write_pmem(void *pmem_addr, struct page *page,
		unsigned int off, unsigned int len)
{
	void *mem = kmap_atomic(page);

	memcpy_to_pmem(pmem_addr, mem + off, len);
	kunmap_atomic(mem);
}

static int read_pmem(struct page *page, unsigned int off,
		void *pmem_addr, unsigned int len)
{
	int rc;
	void *mem = kmap_atomic(page);

	rc = memcpy_mcsafe(mem + off, pmem_addr, len);
	kunmap_atomic(mem);
	if (rc)
		return -EIO;
	return 0;
}

static int pmem_do_bvec(struct pmem_device *pmem, struct page *page,
			unsigned int len, unsigned int off, bool is_write,
			sector_t sector)
{
	int rc = 0;
	bool bad_pmem = false;
	phys_addr_t pmem_off = sector * 512 + pmem->data_offset;
	void *pmem_addr = pmem->virt_addr + pmem_off;

	if (unlikely(is_bad_pmem(&pmem->bb, sector, len)))
		bad_pmem = true;

	if (!is_write) {
		if (unlikely(bad_pmem))
			rc = -EIO;
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

/* account for REQ_FLUSH rename, replace with REQ_PREFLUSH after v4.8-rc1 */
#ifndef REQ_FLUSH
#define REQ_FLUSH REQ_PREFLUSH
#endif

static blk_qc_t pmem_make_request(struct request_queue *q, struct bio *bio)
{
	int rc = 0;
	bool do_acct;
	unsigned long start;
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct pmem_device *pmem = q->queuedata;
	struct nd_region *nd_region = to_region(pmem);

	if (bio->bi_opf & REQ_FLUSH)
		nvdimm_flush(nd_region);

	do_acct = nd_iostat_start(bio, &start);
	bio_for_each_segment(bvec, bio, iter) {
		rc = pmem_do_bvec(pmem, bvec.bv_page, bvec.bv_len,
				bvec.bv_offset, op_is_write(bio_op(bio)),
				iter.bi_sector);
		if (rc) {
			bio->bi_error = rc;
			break;
		}
	}
	if (do_acct)
		nd_iostat_end(bio, start);

	if (bio->bi_opf & REQ_FUA)
		nvdimm_flush(nd_region);

	bio_endio(bio);
	return BLK_QC_T_NONE;
}

static int pmem_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, bool is_write)
{
	struct pmem_device *pmem = bdev->bd_queue->queuedata;
	int rc;

	rc = pmem_do_bvec(pmem, page, PAGE_SIZE, 0, is_write, sector);

	/*
	 * The ->rw_page interface is subtle and tricky.  The core
	 * retries on any error, so we can only invoke page_endio() in
	 * the successful completion case.  Otherwise, we'll see crashes
	 * caused by double completion.
	 */
	if (rc == 0)
		page_endio(page, is_write, 0);

	return rc;
}

/* see "strong" declaration in tools/testing/nvdimm/pmem-dax.c */
__weak long __pmem_direct_access(struct pmem_device *pmem, pgoff_t pgoff,
		long nr_pages, void **kaddr, pfn_t *pfn)
{
	resource_size_t offset = PFN_PHYS(pgoff) + pmem->data_offset;

	if (unlikely(is_bad_pmem(&pmem->bb, PFN_PHYS(pgoff) / 512,
					PFN_PHYS(nr_pages))))
		return -EIO;
	*kaddr = pmem->virt_addr + offset;
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

static const struct dax_operations pmem_dax_ops = {
	.direct_access = pmem_dax_direct_access,
};

static void pmem_release_queue(void *q)
{
	blk_cleanup_queue(q);
}

static void pmem_freeze_queue(void *q)
{
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

static int pmem_attach_disk(struct device *dev,
		struct nd_namespace_common *ndns)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct vmem_altmap __altmap, *altmap = NULL;
	struct resource *res = &nsio->res;
	struct nd_pfn *nd_pfn = NULL;
	struct dax_device *dax_dev;
	int nid = dev_to_node(dev);
	struct nd_pfn_sb *pfn_sb;
	struct pmem_device *pmem;
	struct resource pfn_res;
	struct request_queue *q;
	struct gendisk *disk;
	void *addr;

	/* while nsio_rw_bytes is active, parse a pfn info block if present */
	if (is_nd_pfn(dev)) {
		nd_pfn = to_nd_pfn(dev);
		altmap = nvdimm_setup_pfn(nd_pfn, &pfn_res, &__altmap);
		if (IS_ERR(altmap))
			return PTR_ERR(altmap);
	}

	/* we're attaching a block device, disable raw namespace access */
	devm_nsio_disable(dev, nsio);

	pmem = devm_kzalloc(dev, sizeof(*pmem), GFP_KERNEL);
	if (!pmem)
		return -ENOMEM;

	dev_set_drvdata(dev, pmem);
	pmem->phys_addr = res->start;
	pmem->size = resource_size(res);
	if (nvdimm_has_flush(nd_region) < 0)
		dev_warn(dev, "unable to guarantee persistence of writes\n");

	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				dev_name(&ndns->dev))) {
		dev_warn(dev, "could not reserve region %pR\n", res);
		return -EBUSY;
	}

	q = blk_alloc_queue_node(GFP_KERNEL, dev_to_node(dev));
	if (!q)
		return -ENOMEM;

	if (devm_add_action_or_reset(dev, pmem_release_queue, q))
		return -ENOMEM;

	pmem->pfn_flags = PFN_DEV;
	if (is_nd_pfn(dev)) {
		addr = devm_memremap_pages(dev, &pfn_res, &q->q_usage_counter,
				altmap);
		pfn_sb = nd_pfn->pfn_sb;
		pmem->data_offset = le64_to_cpu(pfn_sb->dataoff);
		pmem->pfn_pad = resource_size(res) - resource_size(&pfn_res);
		pmem->pfn_flags |= PFN_MAP;
		res = &pfn_res; /* for badblocks populate */
		res->start += pmem->data_offset;
	} else if (pmem_should_map_pages(dev)) {
		addr = devm_memremap_pages(dev, &nsio->res,
				&q->q_usage_counter, NULL);
		pmem->pfn_flags |= PFN_MAP;
	} else
		addr = devm_memremap(dev, pmem->phys_addr,
				pmem->size, ARCH_MEMREMAP_PMEM);

	/*
	 * At release time the queue must be frozen before
	 * devm_memremap_pages is unwound
	 */
	if (devm_add_action_or_reset(dev, pmem_freeze_queue, q))
		return -ENOMEM;

	if (IS_ERR(addr))
		return PTR_ERR(addr);
	pmem->virt_addr = addr;

	blk_queue_write_cache(q, true, true);
	blk_queue_make_request(q, pmem_make_request);
	blk_queue_physical_block_size(q, PAGE_SIZE);
	blk_queue_max_hw_sectors(q, UINT_MAX);
	blk_queue_bounce_limit(q, BLK_BOUNCE_ANY);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, q);
	queue_flag_set_unlocked(QUEUE_FLAG_DAX, q);
	q->queuedata = pmem;

	disk = alloc_disk_node(0, nid);
	if (!disk)
		return -ENOMEM;
	pmem->disk = disk;

	disk->fops		= &pmem_fops;
	disk->queue		= q;
	disk->flags		= GENHD_FL_EXT_DEVT;
	nvdimm_namespace_disk_name(ndns, disk->disk_name);
	set_capacity(disk, (pmem->size - pmem->pfn_pad - pmem->data_offset)
			/ 512);
	if (devm_init_badblocks(dev, &pmem->bb))
		return -ENOMEM;
	nvdimm_badblocks_populate(nd_region, &pmem->bb, res);
	disk->bb = &pmem->bb;

	dax_dev = alloc_dax(pmem, disk->disk_name, &pmem_dax_ops);
	if (!dax_dev) {
		put_disk(disk);
		return -ENOMEM;
	}
	pmem->dax_dev = dax_dev;

	device_add_disk(dev, disk);
	if (devm_add_action_or_reset(dev, pmem_release_disk, pmem))
		return -ENOMEM;

	revalidate_disk(disk);

	return 0;
}

static int nd_pmem_probe(struct device *dev)
{
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

	/* if we find a valid info-block we'll come back as that personality */
	if (nd_btt_probe(dev, ndns) == 0 || nd_pfn_probe(dev, ndns) == 0
			|| nd_dax_probe(dev, ndns) == 0)
		return -ENXIO;

	/* ...otherwise we're just a raw pmem device */
	return pmem_attach_disk(dev, ndns);
}

static int nd_pmem_remove(struct device *dev)
{
	if (is_nd_btt(dev))
		nvdimm_namespace_detach_btt(to_nd_btt(dev));
	nvdimm_flush(to_nd_region(dev->parent));

	return 0;
}

static void nd_pmem_shutdown(struct device *dev)
{
	nvdimm_flush(to_nd_region(dev->parent));
}

static void nd_pmem_notify(struct device *dev, enum nvdimm_event event)
{
	struct nd_region *nd_region;
	resource_size_t offset = 0, end_trunc = 0;
	struct nd_namespace_common *ndns;
	struct nd_namespace_io *nsio;
	struct resource res;
	struct badblocks *bb;

	if (event != NVDIMM_REVALIDATE_POISON)
		return;

	if (is_nd_btt(dev)) {
		struct nd_btt *nd_btt = to_nd_btt(dev);

		ndns = nd_btt->ndns;
		nd_region = to_nd_region(ndns->dev.parent);
		nsio = to_nd_namespace_io(&ndns->dev);
		bb = &nsio->bb;
	} else {
		struct pmem_device *pmem = dev_get_drvdata(dev);

		nd_region = to_region(pmem);
		bb = &pmem->bb;

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

static int __init pmem_init(void)
{
	return nd_driver_register(&nd_pmem_driver);
}
module_init(pmem_init);

static void pmem_exit(void)
{
	driver_unregister(&nd_pmem_driver.drv);
}
module_exit(pmem_exit);

MODULE_AUTHOR("Ross Zwisler <ross.zwisler@linux.intel.com>");
MODULE_LICENSE("GPL v2");
