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
#include <linux/memory_hotplug.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/pmem.h>
#include <linux/nd.h>
#include "pfn.h"
#include "nd.h"

struct pmem_device {
	struct request_queue	*pmem_queue;
	struct gendisk		*pmem_disk;
	struct nd_namespace_common *ndns;

	/* One contiguous memory region per device */
	phys_addr_t		phys_addr;
	/* when non-zero this device is hosting a 'pfn' instance */
	phys_addr_t		data_offset;
	void __pmem		*virt_addr;
	size_t			size;
};

static int pmem_major;

static void pmem_do_bvec(struct pmem_device *pmem, struct page *page,
			unsigned int len, unsigned int off, int rw,
			sector_t sector)
{
	void *mem = kmap_atomic(page);
	phys_addr_t pmem_off = sector * 512 + pmem->data_offset;
	void __pmem *pmem_addr = pmem->virt_addr + pmem_off;

	if (rw == READ) {
		memcpy_from_pmem(mem + off, pmem_addr, len);
		flush_dcache_page(page);
	} else {
		flush_dcache_page(page);
		memcpy_to_pmem(pmem_addr, mem + off, len);
	}

	kunmap_atomic(mem);
}

static blk_qc_t pmem_make_request(struct request_queue *q, struct bio *bio)
{
	bool do_acct;
	unsigned long start;
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct block_device *bdev = bio->bi_bdev;
	struct pmem_device *pmem = bdev->bd_disk->private_data;

	do_acct = nd_iostat_start(bio, &start);
	bio_for_each_segment(bvec, bio, iter)
		pmem_do_bvec(pmem, bvec.bv_page, bvec.bv_len, bvec.bv_offset,
				bio_data_dir(bio), iter.bi_sector);
	if (do_acct)
		nd_iostat_end(bio, start);

	if (bio_data_dir(bio))
		wmb_pmem();

	bio_endio(bio);
	return BLK_QC_T_NONE;
}

static int pmem_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, int rw)
{
	struct pmem_device *pmem = bdev->bd_disk->private_data;

	pmem_do_bvec(pmem, page, PAGE_CACHE_SIZE, 0, rw, sector);
	if (rw & WRITE)
		wmb_pmem();
	page_endio(page, rw & WRITE, 0);

	return 0;
}

static long pmem_direct_access(struct block_device *bdev, sector_t sector,
		      void __pmem **kaddr, unsigned long *pfn)
{
	struct pmem_device *pmem = bdev->bd_disk->private_data;
	resource_size_t offset = sector * 512 + pmem->data_offset;

	*kaddr = pmem->virt_addr + offset;
	*pfn = (pmem->phys_addr + offset) >> PAGE_SHIFT;

	return pmem->size - offset;
}

static const struct block_device_operations pmem_fops = {
	.owner =		THIS_MODULE,
	.rw_page =		pmem_rw_page,
	.direct_access =	pmem_direct_access,
	.revalidate_disk =	nvdimm_revalidate_disk,
};

static struct pmem_device *pmem_alloc(struct device *dev,
		struct resource *res, int id)
{
	struct pmem_device *pmem;

	pmem = devm_kzalloc(dev, sizeof(*pmem), GFP_KERNEL);
	if (!pmem)
		return ERR_PTR(-ENOMEM);

	pmem->phys_addr = res->start;
	pmem->size = resource_size(res);
	if (!arch_has_wmb_pmem())
		dev_warn(dev, "unable to guarantee persistence of writes\n");

	if (!devm_request_mem_region(dev, pmem->phys_addr, pmem->size,
			dev_name(dev))) {
		dev_warn(dev, "could not reserve region [0x%pa:0x%zx]\n",
				&pmem->phys_addr, pmem->size);
		return ERR_PTR(-EBUSY);
	}

	if (pmem_should_map_pages(dev))
		pmem->virt_addr = (void __pmem *) devm_memremap_pages(dev, res);
	else
		pmem->virt_addr = (void __pmem *) devm_memremap(dev,
				pmem->phys_addr, pmem->size,
				ARCH_MEMREMAP_PMEM);

	if (IS_ERR(pmem->virt_addr))
		return (void __force *) pmem->virt_addr;

	return pmem;
}

static void pmem_detach_disk(struct pmem_device *pmem)
{
	if (!pmem->pmem_disk)
		return;

	del_gendisk(pmem->pmem_disk);
	put_disk(pmem->pmem_disk);
	blk_cleanup_queue(pmem->pmem_queue);
}

static int pmem_attach_disk(struct device *dev,
		struct nd_namespace_common *ndns, struct pmem_device *pmem)
{
	int nid = dev_to_node(dev);
	struct gendisk *disk;

	pmem->pmem_queue = blk_alloc_queue_node(GFP_KERNEL, nid);
	if (!pmem->pmem_queue)
		return -ENOMEM;

	blk_queue_make_request(pmem->pmem_queue, pmem_make_request);
	blk_queue_physical_block_size(pmem->pmem_queue, PAGE_SIZE);
	blk_queue_max_hw_sectors(pmem->pmem_queue, UINT_MAX);
	blk_queue_bounce_limit(pmem->pmem_queue, BLK_BOUNCE_ANY);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, pmem->pmem_queue);

	disk = alloc_disk_node(0, nid);
	if (!disk) {
		blk_cleanup_queue(pmem->pmem_queue);
		return -ENOMEM;
	}

	disk->major		= pmem_major;
	disk->first_minor	= 0;
	disk->fops		= &pmem_fops;
	disk->private_data	= pmem;
	disk->queue		= pmem->pmem_queue;
	disk->flags		= GENHD_FL_EXT_DEVT;
	nvdimm_namespace_disk_name(ndns, disk->disk_name);
	disk->driverfs_dev = dev;
	set_capacity(disk, (pmem->size - pmem->data_offset) / 512);
	pmem->pmem_disk = disk;

	add_disk(disk);
	revalidate_disk(disk);

	return 0;
}

static int pmem_rw_bytes(struct nd_namespace_common *ndns,
		resource_size_t offset, void *buf, size_t size, int rw)
{
	struct pmem_device *pmem = dev_get_drvdata(ndns->claim);

	if (unlikely(offset + size > pmem->size)) {
		dev_WARN_ONCE(&ndns->dev, 1, "request out of range\n");
		return -EFAULT;
	}

	if (rw == READ)
		memcpy_from_pmem(buf, pmem->virt_addr + offset, size);
	else {
		memcpy_to_pmem(pmem->virt_addr + offset, buf, size);
		wmb_pmem();
	}

	return 0;
}

static int nd_pfn_init(struct nd_pfn *nd_pfn)
{
	struct nd_pfn_sb *pfn_sb = kzalloc(sizeof(*pfn_sb), GFP_KERNEL);
	struct pmem_device *pmem = dev_get_drvdata(&nd_pfn->dev);
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	struct nd_region *nd_region;
	unsigned long npfns;
	phys_addr_t offset;
	u64 checksum;
	int rc;

	if (!pfn_sb)
		return -ENOMEM;

	nd_pfn->pfn_sb = pfn_sb;
	rc = nd_pfn_validate(nd_pfn);
	if (rc == 0 || rc == -EBUSY)
		return rc;

	/* section alignment for simple hotplug */
	if (nvdimm_namespace_capacity(ndns) < ND_PFN_ALIGN
			|| pmem->phys_addr & ND_PFN_MASK)
		return -ENODEV;

	nd_region = to_nd_region(nd_pfn->dev.parent);
	if (nd_region->ro) {
		dev_info(&nd_pfn->dev,
				"%s is read-only, unable to init metadata\n",
				dev_name(&nd_region->dev));
		goto err;
	}

	memset(pfn_sb, 0, sizeof(*pfn_sb));
	npfns = (pmem->size - SZ_8K) / SZ_4K;
	/*
	 * Note, we use 64 here for the standard size of struct page,
	 * debugging options may cause it to be larger in which case the
	 * implementation will limit the pfns advertised through
	 * ->direct_access() to those that are included in the memmap.
	 */
	if (nd_pfn->mode == PFN_MODE_PMEM)
		offset = ALIGN(SZ_8K + 64 * npfns, PMD_SIZE);
	else if (nd_pfn->mode == PFN_MODE_RAM)
		offset = SZ_8K;
	else
		goto err;

	npfns = (pmem->size - offset) / SZ_4K;
	pfn_sb->mode = cpu_to_le32(nd_pfn->mode);
	pfn_sb->dataoff = cpu_to_le64(offset);
	pfn_sb->npfns = cpu_to_le64(npfns);
	memcpy(pfn_sb->signature, PFN_SIG, PFN_SIG_LEN);
	memcpy(pfn_sb->uuid, nd_pfn->uuid, 16);
	pfn_sb->version_major = cpu_to_le16(1);
	checksum = nd_sb_checksum((struct nd_gen_sb *) pfn_sb);
	pfn_sb->checksum = cpu_to_le64(checksum);

	rc = nvdimm_write_bytes(ndns, SZ_4K, pfn_sb, sizeof(*pfn_sb));
	if (rc)
		goto err;

	return 0;
 err:
	nd_pfn->pfn_sb = NULL;
	kfree(pfn_sb);
	return -ENXIO;
}

static int nvdimm_namespace_detach_pfn(struct nd_namespace_common *ndns)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(ndns->claim);
	struct pmem_device *pmem;

	/* free pmem disk */
	pmem = dev_get_drvdata(&nd_pfn->dev);
	pmem_detach_disk(pmem);

	/* release nd_pfn resources */
	kfree(nd_pfn->pfn_sb);
	nd_pfn->pfn_sb = NULL;

	return 0;
}

static int nvdimm_namespace_attach_pfn(struct nd_namespace_common *ndns)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	struct nd_pfn *nd_pfn = to_nd_pfn(ndns->claim);
	struct device *dev = &nd_pfn->dev;
	struct vmem_altmap *altmap;
	struct nd_region *nd_region;
	struct nd_pfn_sb *pfn_sb;
	struct pmem_device *pmem;
	phys_addr_t offset;
	int rc;

	if (!nd_pfn->uuid || !nd_pfn->ndns)
		return -ENODEV;

	nd_region = to_nd_region(dev->parent);
	rc = nd_pfn_init(nd_pfn);
	if (rc)
		return rc;

	if (PAGE_SIZE != SZ_4K) {
		dev_err(dev, "only supported on systems with 4K PAGE_SIZE\n");
		return -ENXIO;
	}
	if (nsio->res.start & ND_PFN_MASK) {
		dev_err(dev, "%s not memory hotplug section aligned\n",
				dev_name(&ndns->dev));
		return -ENXIO;
	}

	pfn_sb = nd_pfn->pfn_sb;
	offset = le64_to_cpu(pfn_sb->dataoff);
	nd_pfn->mode = le32_to_cpu(nd_pfn->pfn_sb->mode);
	if (nd_pfn->mode == PFN_MODE_RAM) {
		if (offset != SZ_8K)
			return -EINVAL;
		nd_pfn->npfns = le64_to_cpu(pfn_sb->npfns);
		altmap = NULL;
	} else {
		rc = -ENXIO;
		goto err;
	}

	/* establish pfn range for lookup, and switch to direct map */
	pmem = dev_get_drvdata(dev);
	devm_memunmap(dev, (void __force *) pmem->virt_addr);
	pmem->virt_addr = (void __pmem *) devm_memremap_pages(dev, &nsio->res);
	if (IS_ERR(pmem->virt_addr)) {
		rc = PTR_ERR(pmem->virt_addr);
		goto err;
	}

	/* attach pmem disk in "pfn-mode" */
	pmem->data_offset = offset;
	rc = pmem_attach_disk(dev, ndns, pmem);
	if (rc)
		goto err;

	return rc;
 err:
	nvdimm_namespace_detach_pfn(ndns);
	return rc;
}

static int nd_pmem_probe(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_namespace_common *ndns;
	struct nd_namespace_io *nsio;
	struct pmem_device *pmem;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);

	nsio = to_nd_namespace_io(&ndns->dev);
	pmem = pmem_alloc(dev, &nsio->res, nd_region->id);
	if (IS_ERR(pmem))
		return PTR_ERR(pmem);

	pmem->ndns = ndns;
	dev_set_drvdata(dev, pmem);
	ndns->rw_bytes = pmem_rw_bytes;

	if (is_nd_btt(dev))
		return nvdimm_namespace_attach_btt(ndns);

	if (is_nd_pfn(dev))
		return nvdimm_namespace_attach_pfn(ndns);

	if (nd_btt_probe(ndns, pmem) == 0) {
		/* we'll come back as btt-pmem */
		return -ENXIO;
	}

	if (nd_pfn_probe(ndns, pmem) == 0) {
		/* we'll come back as pfn-pmem */
		return -ENXIO;
	}

	return pmem_attach_disk(dev, ndns, pmem);
}

static int nd_pmem_remove(struct device *dev)
{
	struct pmem_device *pmem = dev_get_drvdata(dev);

	if (is_nd_btt(dev))
		nvdimm_namespace_detach_btt(pmem->ndns);
	else if (is_nd_pfn(dev))
		nvdimm_namespace_detach_pfn(pmem->ndns);
	else
		pmem_detach_disk(pmem);

	return 0;
}

MODULE_ALIAS("pmem");
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_IO);
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_PMEM);
static struct nd_device_driver nd_pmem_driver = {
	.probe = nd_pmem_probe,
	.remove = nd_pmem_remove,
	.drv = {
		.name = "nd_pmem",
	},
	.type = ND_DRIVER_NAMESPACE_IO | ND_DRIVER_NAMESPACE_PMEM,
};

static int __init pmem_init(void)
{
	int error;

	pmem_major = register_blkdev(0, "pmem");
	if (pmem_major < 0)
		return pmem_major;

	error = nd_driver_register(&nd_pmem_driver);
	if (error) {
		unregister_blkdev(pmem_major, "pmem");
		return error;
	}

	return 0;
}
module_init(pmem_init);

static void pmem_exit(void)
{
	driver_unregister(&nd_pmem_driver.drv);
	unregister_blkdev(pmem_major, "pmem");
}
module_exit(pmem_exit);

MODULE_AUTHOR("Ross Zwisler <ross.zwisler@linux.intel.com>");
MODULE_LICENSE("GPL v2");
