// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2013-2016 Intel Corporation. All rights reserved.
 */
#include <linux/memremap.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "nd-core.h"
#include "pfn.h"
#include "nd.h"

static void nd_pfn_release(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);

	dev_dbg(dev, "trace\n");
	nd_detach_ndns(&nd_pfn->dev, &nd_pfn->ndns);
	ida_simple_remove(&nd_region->pfn_ida, nd_pfn->id);
	kfree(nd_pfn->uuid);
	kfree(nd_pfn);
}

static struct device_type nd_pfn_device_type = {
	.name = "nd_pfn",
	.release = nd_pfn_release,
};

bool is_nd_pfn(struct device *dev)
{
	return dev ? dev->type == &nd_pfn_device_type : false;
}
EXPORT_SYMBOL(is_nd_pfn);

struct nd_pfn *to_nd_pfn(struct device *dev)
{
	struct nd_pfn *nd_pfn = container_of(dev, struct nd_pfn, dev);

	WARN_ON(!is_nd_pfn(dev));
	return nd_pfn;
}
EXPORT_SYMBOL(to_nd_pfn);

static ssize_t mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);

	switch (nd_pfn->mode) {
	case PFN_MODE_RAM:
		return sprintf(buf, "ram\n");
	case PFN_MODE_PMEM:
		return sprintf(buf, "pmem\n");
	default:
		return sprintf(buf, "none\n");
	}
}

static ssize_t mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc = 0;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	if (dev->driver)
		rc = -EBUSY;
	else {
		size_t n = len - 1;

		if (strncmp(buf, "pmem\n", n) == 0
				|| strncmp(buf, "pmem", n) == 0) {
			nd_pfn->mode = PFN_MODE_PMEM;
		} else if (strncmp(buf, "ram\n", n) == 0
				|| strncmp(buf, "ram", n) == 0)
			nd_pfn->mode = PFN_MODE_RAM;
		else if (strncmp(buf, "none\n", n) == 0
				|| strncmp(buf, "none", n) == 0)
			nd_pfn->mode = PFN_MODE_NONE;
		else
			rc = -EINVAL;
	}
	dev_dbg(dev, "result: %zd wrote: %s%s", rc, buf,
			buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(mode);

static ssize_t align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);

	return sprintf(buf, "%ld\n", nd_pfn->align);
}

static const unsigned long *nd_pfn_supported_alignments(void)
{
	/*
	 * This needs to be a non-static variable because the *_SIZE
	 * macros aren't always constants.
	 */
	const unsigned long supported_alignments[] = {
		PAGE_SIZE,
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		HPAGE_PMD_SIZE,
#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
		HPAGE_PUD_SIZE,
#endif
#endif
		0,
	};
	static unsigned long data[ARRAY_SIZE(supported_alignments)];

	memcpy(data, supported_alignments, sizeof(data));

	return data;
}

static ssize_t align_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	rc = nd_size_select_store(dev, buf, &nd_pfn->align,
			nd_pfn_supported_alignments());
	dev_dbg(dev, "result: %zd wrote: %s%s", rc, buf,
			buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(align);

static ssize_t uuid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);

	if (nd_pfn->uuid)
		return sprintf(buf, "%pUb\n", nd_pfn->uuid);
	return sprintf(buf, "\n");
}

static ssize_t uuid_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc;

	device_lock(dev);
	rc = nd_uuid_store(dev, &nd_pfn->uuid, buf, len);
	dev_dbg(dev, "result: %zd wrote: %s%s", rc, buf,
			buf[len - 1] == '\n' ? "" : "\n");
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(uuid);

static ssize_t namespace_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	rc = sprintf(buf, "%s\n", nd_pfn->ndns
			? dev_name(&nd_pfn->ndns->dev) : "");
	nvdimm_bus_unlock(dev);
	return rc;
}

static ssize_t namespace_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	rc = nd_namespace_store(dev, &nd_pfn->ndns, buf, len);
	dev_dbg(dev, "result: %zd wrote: %s%s", rc, buf,
			buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RW(namespace);

static ssize_t resource_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc;

	device_lock(dev);
	if (dev->driver) {
		struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
		u64 offset = __le64_to_cpu(pfn_sb->dataoff);
		struct nd_namespace_common *ndns = nd_pfn->ndns;
		u32 start_pad = __le32_to_cpu(pfn_sb->start_pad);
		struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);

		rc = sprintf(buf, "%#llx\n", (unsigned long long) nsio->res.start
				+ start_pad + offset);
	} else {
		/* no address to convey if the pfn instance is disabled */
		rc = -ENXIO;
	}
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(resource);

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc;

	device_lock(dev);
	if (dev->driver) {
		struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
		u64 offset = __le64_to_cpu(pfn_sb->dataoff);
		struct nd_namespace_common *ndns = nd_pfn->ndns;
		u32 start_pad = __le32_to_cpu(pfn_sb->start_pad);
		u32 end_trunc = __le32_to_cpu(pfn_sb->end_trunc);
		struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);

		rc = sprintf(buf, "%llu\n", (unsigned long long)
				resource_size(&nsio->res) - start_pad
				- end_trunc - offset);
	} else {
		/* no size to convey if the pfn instance is disabled */
		rc = -ENXIO;
	}
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(size);

static ssize_t supported_alignments_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return nd_size_select_show(0, nd_pfn_supported_alignments(), buf);
}
static DEVICE_ATTR_RO(supported_alignments);

static struct attribute *nd_pfn_attributes[] = {
	&dev_attr_mode.attr,
	&dev_attr_namespace.attr,
	&dev_attr_uuid.attr,
	&dev_attr_align.attr,
	&dev_attr_resource.attr,
	&dev_attr_size.attr,
	&dev_attr_supported_alignments.attr,
	NULL,
};

static umode_t pfn_visible(struct kobject *kobj, struct attribute *a, int n)
{
	if (a == &dev_attr_resource.attr)
		return 0400;
	return a->mode;
}

struct attribute_group nd_pfn_attribute_group = {
	.attrs = nd_pfn_attributes,
	.is_visible = pfn_visible,
};

static const struct attribute_group *nd_pfn_attribute_groups[] = {
	&nd_pfn_attribute_group,
	&nd_device_attribute_group,
	&nd_numa_attribute_group,
	NULL,
};

struct device *nd_pfn_devinit(struct nd_pfn *nd_pfn,
		struct nd_namespace_common *ndns)
{
	struct device *dev;

	if (!nd_pfn)
		return NULL;

	nd_pfn->mode = PFN_MODE_NONE;
	nd_pfn->align = PFN_DEFAULT_ALIGNMENT;
	dev = &nd_pfn->dev;
	device_initialize(&nd_pfn->dev);
	if (ndns && !__nd_attach_ndns(&nd_pfn->dev, ndns, &nd_pfn->ndns)) {
		dev_dbg(&ndns->dev, "failed, already claimed by %s\n",
				dev_name(ndns->claim));
		put_device(dev);
		return NULL;
	}
	return dev;
}

static struct nd_pfn *nd_pfn_alloc(struct nd_region *nd_region)
{
	struct nd_pfn *nd_pfn;
	struct device *dev;

	nd_pfn = kzalloc(sizeof(*nd_pfn), GFP_KERNEL);
	if (!nd_pfn)
		return NULL;

	nd_pfn->id = ida_simple_get(&nd_region->pfn_ida, 0, 0, GFP_KERNEL);
	if (nd_pfn->id < 0) {
		kfree(nd_pfn);
		return NULL;
	}

	dev = &nd_pfn->dev;
	dev_set_name(dev, "pfn%d.%d", nd_region->id, nd_pfn->id);
	dev->groups = nd_pfn_attribute_groups;
	dev->type = &nd_pfn_device_type;
	dev->parent = &nd_region->dev;

	return nd_pfn;
}

struct device *nd_pfn_create(struct nd_region *nd_region)
{
	struct nd_pfn *nd_pfn;
	struct device *dev;

	if (!is_memory(&nd_region->dev))
		return NULL;

	nd_pfn = nd_pfn_alloc(nd_region);
	dev = nd_pfn_devinit(nd_pfn, NULL);

	__nd_device_register(dev);
	return dev;
}

/*
 * nd_pfn_clear_memmap_errors() clears any errors in the volatile memmap
 * space associated with the namespace. If the memmap is set to DRAM, then
 * this is a no-op. Since the memmap area is freshly initialized during
 * probe, we have an opportunity to clear any badblocks in this area.
 */
static int nd_pfn_clear_memmap_errors(struct nd_pfn *nd_pfn)
{
	struct nd_region *nd_region = to_nd_region(nd_pfn->dev.parent);
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	void *zero_page = page_address(ZERO_PAGE(0));
	struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
	int num_bad, meta_num, rc, bb_present;
	sector_t first_bad, meta_start;
	struct nd_namespace_io *nsio;

	if (nd_pfn->mode != PFN_MODE_PMEM)
		return 0;

	nsio = to_nd_namespace_io(&ndns->dev);
	meta_start = (SZ_4K + sizeof(*pfn_sb)) >> 9;
	meta_num = (le64_to_cpu(pfn_sb->dataoff) >> 9) - meta_start;

	do {
		unsigned long zero_len;
		u64 nsoff;

		bb_present = badblocks_check(&nd_region->bb, meta_start,
				meta_num, &first_bad, &num_bad);
		if (bb_present) {
			dev_dbg(&nd_pfn->dev, "meta: %x badblocks at %llx\n",
					num_bad, first_bad);
			nsoff = ALIGN_DOWN((nd_region->ndr_start
					+ (first_bad << 9)) - nsio->res.start,
					PAGE_SIZE);
			zero_len = ALIGN(num_bad << 9, PAGE_SIZE);
			while (zero_len) {
				unsigned long chunk = min(zero_len, PAGE_SIZE);

				rc = nvdimm_write_bytes(ndns, nsoff, zero_page,
							chunk, 0);
				if (rc)
					break;

				zero_len -= chunk;
				nsoff += chunk;
			}
			if (rc) {
				dev_err(&nd_pfn->dev,
					"error clearing %x badblocks at %llx\n",
					num_bad, first_bad);
				return rc;
			}
		}
	} while (bb_present);

	return 0;
}

/**
 * nd_pfn_validate - read and validate info-block
 * @nd_pfn: fsdax namespace runtime state / properties
 * @sig: 'devdax' or 'fsdax' signature
 *
 * Upon return the info-block buffer contents (->pfn_sb) are
 * indeterminate when validation fails, and a coherent info-block
 * otherwise.
 */
int nd_pfn_validate(struct nd_pfn *nd_pfn, const char *sig)
{
	u64 checksum, offset;
	enum nd_pfn_mode mode;
	struct nd_namespace_io *nsio;
	unsigned long align, start_pad;
	struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	const u8 *parent_uuid = nd_dev_to_uuid(&ndns->dev);

	if (!pfn_sb || !ndns)
		return -ENODEV;

	if (!is_memory(nd_pfn->dev.parent))
		return -ENODEV;

	if (nvdimm_read_bytes(ndns, SZ_4K, pfn_sb, sizeof(*pfn_sb), 0))
		return -ENXIO;

	if (memcmp(pfn_sb->signature, sig, PFN_SIG_LEN) != 0)
		return -ENODEV;

	checksum = le64_to_cpu(pfn_sb->checksum);
	pfn_sb->checksum = 0;
	if (checksum != nd_sb_checksum((struct nd_gen_sb *) pfn_sb))
		return -ENODEV;
	pfn_sb->checksum = cpu_to_le64(checksum);

	if (memcmp(pfn_sb->parent_uuid, parent_uuid, 16) != 0)
		return -ENODEV;

	if (__le16_to_cpu(pfn_sb->version_minor) < 1) {
		pfn_sb->start_pad = 0;
		pfn_sb->end_trunc = 0;
	}

	if (__le16_to_cpu(pfn_sb->version_minor) < 2)
		pfn_sb->align = 0;

	switch (le32_to_cpu(pfn_sb->mode)) {
	case PFN_MODE_RAM:
	case PFN_MODE_PMEM:
		break;
	default:
		return -ENXIO;
	}

	align = le32_to_cpu(pfn_sb->align);
	offset = le64_to_cpu(pfn_sb->dataoff);
	start_pad = le32_to_cpu(pfn_sb->start_pad);
	if (align == 0)
		align = 1UL << ilog2(offset);
	mode = le32_to_cpu(pfn_sb->mode);

	if (!nd_pfn->uuid) {
		/*
		 * When probing a namepace via nd_pfn_probe() the uuid
		 * is NULL (see: nd_pfn_devinit()) we init settings from
		 * pfn_sb
		 */
		nd_pfn->uuid = kmemdup(pfn_sb->uuid, 16, GFP_KERNEL);
		if (!nd_pfn->uuid)
			return -ENOMEM;
		nd_pfn->align = align;
		nd_pfn->mode = mode;
	} else {
		/*
		 * When probing a pfn / dax instance we validate the
		 * live settings against the pfn_sb
		 */
		if (memcmp(nd_pfn->uuid, pfn_sb->uuid, 16) != 0)
			return -ENODEV;

		/*
		 * If the uuid validates, but other settings mismatch
		 * return EINVAL because userspace has managed to change
		 * the configuration without specifying new
		 * identification.
		 */
		if (nd_pfn->align != align || nd_pfn->mode != mode) {
			dev_err(&nd_pfn->dev,
					"init failed, settings mismatch\n");
			dev_dbg(&nd_pfn->dev, "align: %lx:%lx mode: %d:%d\n",
					nd_pfn->align, align, nd_pfn->mode,
					mode);
			return -EINVAL;
		}
	}

	if (align > nvdimm_namespace_capacity(ndns)) {
		dev_err(&nd_pfn->dev, "alignment: %lx exceeds capacity %llx\n",
				align, nvdimm_namespace_capacity(ndns));
		return -EINVAL;
	}

	/*
	 * These warnings are verbose because they can only trigger in
	 * the case where the physical address alignment of the
	 * namespace has changed since the pfn superblock was
	 * established.
	 */
	nsio = to_nd_namespace_io(&ndns->dev);
	if (offset >= resource_size(&nsio->res)) {
		dev_err(&nd_pfn->dev, "pfn array size exceeds capacity of %s\n",
				dev_name(&ndns->dev));
		return -EBUSY;
	}

	if ((align && !IS_ALIGNED(nsio->res.start + offset + start_pad, align))
			|| !IS_ALIGNED(offset, PAGE_SIZE)) {
		dev_err(&nd_pfn->dev,
				"bad offset: %#llx dax disabled align: %#lx\n",
				offset, align);
		return -ENXIO;
	}

	return nd_pfn_clear_memmap_errors(nd_pfn);
}
EXPORT_SYMBOL(nd_pfn_validate);

int nd_pfn_probe(struct device *dev, struct nd_namespace_common *ndns)
{
	int rc;
	struct nd_pfn *nd_pfn;
	struct device *pfn_dev;
	struct nd_pfn_sb *pfn_sb;
	struct nd_region *nd_region = to_nd_region(ndns->dev.parent);

	if (ndns->force_raw)
		return -ENODEV;

	switch (ndns->claim_class) {
	case NVDIMM_CCLASS_NONE:
	case NVDIMM_CCLASS_PFN:
		break;
	default:
		return -ENODEV;
	}

	nvdimm_bus_lock(&ndns->dev);
	nd_pfn = nd_pfn_alloc(nd_region);
	pfn_dev = nd_pfn_devinit(nd_pfn, ndns);
	nvdimm_bus_unlock(&ndns->dev);
	if (!pfn_dev)
		return -ENOMEM;
	pfn_sb = devm_kmalloc(dev, sizeof(*pfn_sb), GFP_KERNEL);
	nd_pfn = to_nd_pfn(pfn_dev);
	nd_pfn->pfn_sb = pfn_sb;
	rc = nd_pfn_validate(nd_pfn, PFN_SIG);
	dev_dbg(dev, "pfn: %s\n", rc == 0 ? dev_name(pfn_dev) : "<none>");
	if (rc < 0) {
		nd_detach_ndns(pfn_dev, &nd_pfn->ndns);
		put_device(pfn_dev);
	} else
		__nd_device_register(pfn_dev);

	return rc;
}
EXPORT_SYMBOL(nd_pfn_probe);

static u32 info_block_reserve(void)
{
	return ALIGN(SZ_8K, PAGE_SIZE);
}

/*
 * We hotplug memory at section granularity, pad the reserved area from
 * the previous section base to the namespace base address.
 */
static unsigned long init_altmap_base(resource_size_t base)
{
	unsigned long base_pfn = PHYS_PFN(base);

	return PFN_SECTION_ALIGN_DOWN(base_pfn);
}

static unsigned long init_altmap_reserve(resource_size_t base)
{
	unsigned long reserve = info_block_reserve() >> PAGE_SHIFT;
	unsigned long base_pfn = PHYS_PFN(base);

	reserve += base_pfn - PFN_SECTION_ALIGN_DOWN(base_pfn);
	return reserve;
}

static int __nvdimm_setup_pfn(struct nd_pfn *nd_pfn, struct dev_pagemap *pgmap)
{
	struct resource *res = &pgmap->res;
	struct vmem_altmap *altmap = &pgmap->altmap;
	struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
	u64 offset = le64_to_cpu(pfn_sb->dataoff);
	u32 start_pad = __le32_to_cpu(pfn_sb->start_pad);
	u32 end_trunc = __le32_to_cpu(pfn_sb->end_trunc);
	u32 reserve = info_block_reserve();
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	resource_size_t base = nsio->res.start + start_pad;
	struct vmem_altmap __altmap = {
		.base_pfn = init_altmap_base(base),
		.reserve = init_altmap_reserve(base),
	};

	memcpy(res, &nsio->res, sizeof(*res));
	res->start += start_pad;
	res->end -= end_trunc;

	if (nd_pfn->mode == PFN_MODE_RAM) {
		if (offset < reserve)
			return -EINVAL;
		nd_pfn->npfns = le64_to_cpu(pfn_sb->npfns);
		pgmap->altmap_valid = false;
	} else if (nd_pfn->mode == PFN_MODE_PMEM) {
		nd_pfn->npfns = PFN_SECTION_ALIGN_UP((resource_size(res)
					- offset) / PAGE_SIZE);
		if (le64_to_cpu(nd_pfn->pfn_sb->npfns) > nd_pfn->npfns)
			dev_info(&nd_pfn->dev,
					"number of pfns truncated from %lld to %ld\n",
					le64_to_cpu(nd_pfn->pfn_sb->npfns),
					nd_pfn->npfns);
		memcpy(altmap, &__altmap, sizeof(*altmap));
		altmap->free = PHYS_PFN(offset - reserve);
		altmap->alloc = 0;
		pgmap->altmap_valid = true;
	} else
		return -ENXIO;

	return 0;
}

static u64 phys_pmem_align_down(struct nd_pfn *nd_pfn, u64 phys)
{
	return min_t(u64, PHYS_SECTION_ALIGN_DOWN(phys),
			ALIGN_DOWN(phys, nd_pfn->align));
}

/*
 * Check if pmem collides with 'System RAM', or other regions when
 * section aligned.  Trim it accordingly.
 */
static void trim_pfn_device(struct nd_pfn *nd_pfn, u32 *start_pad, u32 *end_trunc)
{
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	struct nd_region *nd_region = to_nd_region(nd_pfn->dev.parent);
	const resource_size_t start = nsio->res.start;
	const resource_size_t end = start + resource_size(&nsio->res);
	resource_size_t adjust, size;

	*start_pad = 0;
	*end_trunc = 0;

	adjust = start - PHYS_SECTION_ALIGN_DOWN(start);
	size = resource_size(&nsio->res) + adjust;
	if (region_intersects(start - adjust, size, IORESOURCE_SYSTEM_RAM,
				IORES_DESC_NONE) == REGION_MIXED
			|| nd_region_conflict(nd_region, start - adjust, size))
		*start_pad = PHYS_SECTION_ALIGN_UP(start) - start;

	/* Now check that end of the range does not collide. */
	adjust = PHYS_SECTION_ALIGN_UP(end) - end;
	size = resource_size(&nsio->res) + adjust;
	if (region_intersects(start, size, IORESOURCE_SYSTEM_RAM,
				IORES_DESC_NONE) == REGION_MIXED
			|| !IS_ALIGNED(end, nd_pfn->align)
			|| nd_region_conflict(nd_region, start, size))
		*end_trunc = end - phys_pmem_align_down(nd_pfn, end);
}

static int nd_pfn_init(struct nd_pfn *nd_pfn)
{
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	u32 start_pad, end_trunc, reserve = info_block_reserve();
	resource_size_t start, size;
	struct nd_region *nd_region;
	struct nd_pfn_sb *pfn_sb;
	unsigned long npfns;
	phys_addr_t offset;
	const char *sig;
	u64 checksum;
	int rc;

	pfn_sb = devm_kmalloc(&nd_pfn->dev, sizeof(*pfn_sb), GFP_KERNEL);
	if (!pfn_sb)
		return -ENOMEM;

	nd_pfn->pfn_sb = pfn_sb;
	if (is_nd_dax(&nd_pfn->dev))
		sig = DAX_SIG;
	else
		sig = PFN_SIG;

	rc = nd_pfn_validate(nd_pfn, sig);
	if (rc != -ENODEV)
		return rc;

	/* no info block, do init */;
	memset(pfn_sb, 0, sizeof(*pfn_sb));

	nd_region = to_nd_region(nd_pfn->dev.parent);
	if (nd_region->ro) {
		dev_info(&nd_pfn->dev,
				"%s is read-only, unable to init metadata\n",
				dev_name(&nd_region->dev));
		return -ENXIO;
	}

	memset(pfn_sb, 0, sizeof(*pfn_sb));

	trim_pfn_device(nd_pfn, &start_pad, &end_trunc);
	if (start_pad + end_trunc)
		dev_info(&nd_pfn->dev, "%s alignment collision, truncate %d bytes\n",
				dev_name(&ndns->dev), start_pad + end_trunc);

	/*
	 * Note, we use 64 here for the standard size of struct page,
	 * debugging options may cause it to be larger in which case the
	 * implementation will limit the pfns advertised through
	 * ->direct_access() to those that are included in the memmap.
	 */
	start = nsio->res.start + start_pad;
	size = resource_size(&nsio->res);
	npfns = PFN_SECTION_ALIGN_UP((size - start_pad - end_trunc - reserve)
			/ PAGE_SIZE);
	if (nd_pfn->mode == PFN_MODE_PMEM) {
		/*
		 * The altmap should be padded out to the block size used
		 * when populating the vmemmap. This *should* be equal to
		 * PMD_SIZE for most architectures.
		 */
		offset = ALIGN(start + reserve + 64 * npfns,
				max(nd_pfn->align, PMD_SIZE)) - start;
	} else if (nd_pfn->mode == PFN_MODE_RAM)
		offset = ALIGN(start + reserve, nd_pfn->align) - start;
	else
		return -ENXIO;

	if (offset + start_pad + end_trunc >= size) {
		dev_err(&nd_pfn->dev, "%s unable to satisfy requested alignment\n",
				dev_name(&ndns->dev));
		return -ENXIO;
	}

	npfns = (size - offset - start_pad - end_trunc) / SZ_4K;
	pfn_sb->mode = cpu_to_le32(nd_pfn->mode);
	pfn_sb->dataoff = cpu_to_le64(offset);
	pfn_sb->npfns = cpu_to_le64(npfns);
	memcpy(pfn_sb->signature, sig, PFN_SIG_LEN);
	memcpy(pfn_sb->uuid, nd_pfn->uuid, 16);
	memcpy(pfn_sb->parent_uuid, nd_dev_to_uuid(&ndns->dev), 16);
	pfn_sb->version_major = cpu_to_le16(1);
	pfn_sb->version_minor = cpu_to_le16(3);
	pfn_sb->start_pad = cpu_to_le32(start_pad);
	pfn_sb->end_trunc = cpu_to_le32(end_trunc);
	pfn_sb->align = cpu_to_le32(nd_pfn->align);
	checksum = nd_sb_checksum((struct nd_gen_sb *) pfn_sb);
	pfn_sb->checksum = cpu_to_le64(checksum);

	return nvdimm_write_bytes(ndns, SZ_4K, pfn_sb, sizeof(*pfn_sb), 0);
}

/*
 * Determine the effective resource range and vmem_altmap from an nd_pfn
 * instance.
 */
int nvdimm_setup_pfn(struct nd_pfn *nd_pfn, struct dev_pagemap *pgmap)
{
	int rc;

	if (!nd_pfn->uuid || !nd_pfn->ndns)
		return -ENODEV;

	rc = nd_pfn_init(nd_pfn);
	if (rc)
		return rc;

	/* we need a valid pfn_sb before we can init a dev_pagemap */
	return __nvdimm_setup_pfn(nd_pfn, pgmap);
}
EXPORT_SYMBOL_GPL(nvdimm_setup_pfn);
