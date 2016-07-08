/*
 * Copyright(c) 2013-2016 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
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

	dev_dbg(dev, "%s\n", __func__);
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
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(mode);

static ssize_t align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);

	return sprintf(buf, "%lx\n", nd_pfn->align);
}

static ssize_t __align_store(struct nd_pfn *nd_pfn, const char *buf)
{
	unsigned long val;
	int rc;

	rc = kstrtoul(buf, 0, &val);
	if (rc)
		return rc;

	if (!is_power_of_2(val) || val < PAGE_SIZE || val > SZ_1G)
		return -EINVAL;

	if (nd_pfn->dev.driver)
		return -EBUSY;
	else
		nd_pfn->align = val;

	return 0;
}

static ssize_t align_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	rc = __align_store(nd_pfn, buf);
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
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
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
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
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
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

static struct attribute *nd_pfn_attributes[] = {
	&dev_attr_mode.attr,
	&dev_attr_namespace.attr,
	&dev_attr_uuid.attr,
	&dev_attr_align.attr,
	&dev_attr_resource.attr,
	&dev_attr_size.attr,
	NULL,
};

struct attribute_group nd_pfn_attribute_group = {
	.attrs = nd_pfn_attributes,
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
	struct device *dev = &nd_pfn->dev;

	if (!nd_pfn)
		return NULL;

	nd_pfn->mode = PFN_MODE_NONE;
	nd_pfn->align = HPAGE_SIZE;
	dev = &nd_pfn->dev;
	device_initialize(&nd_pfn->dev);
	if (ndns && !__nd_attach_ndns(&nd_pfn->dev, ndns, &nd_pfn->ndns)) {
		dev_dbg(&ndns->dev, "%s failed, already claimed by %s\n",
				__func__, dev_name(ndns->claim));
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

	if (!is_nd_pmem(&nd_region->dev))
		return NULL;

	nd_pfn = nd_pfn_alloc(nd_region);
	dev = nd_pfn_devinit(nd_pfn, NULL);

	__nd_device_register(dev);
	return dev;
}

int nd_pfn_validate(struct nd_pfn *nd_pfn, const char *sig)
{
	u64 checksum, offset;
	unsigned long align;
	enum nd_pfn_mode mode;
	struct nd_namespace_io *nsio;
	struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	const u8 *parent_uuid = nd_dev_to_uuid(&ndns->dev);

	if (!pfn_sb || !ndns)
		return -ENODEV;

	if (!is_nd_pmem(nd_pfn->dev.parent))
		return -ENODEV;

	if (nvdimm_read_bytes(ndns, SZ_4K, pfn_sb, sizeof(*pfn_sb)))
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

	if ((align && !IS_ALIGNED(offset, align))
			|| !IS_ALIGNED(offset, PAGE_SIZE)) {
		dev_err(&nd_pfn->dev,
				"bad offset: %#llx dax disabled align: %#lx\n",
				offset, align);
		return -ENXIO;
	}

	return 0;
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

	nvdimm_bus_lock(&ndns->dev);
	nd_pfn = nd_pfn_alloc(nd_region);
	pfn_dev = nd_pfn_devinit(nd_pfn, ndns);
	nvdimm_bus_unlock(&ndns->dev);
	if (!pfn_dev)
		return -ENOMEM;
	pfn_sb = devm_kzalloc(dev, sizeof(*pfn_sb), GFP_KERNEL);
	nd_pfn = to_nd_pfn(pfn_dev);
	nd_pfn->pfn_sb = pfn_sb;
	rc = nd_pfn_validate(nd_pfn, PFN_SIG);
	dev_dbg(dev, "%s: pfn: %s\n", __func__,
			rc == 0 ? dev_name(pfn_dev) : "<none>");
	if (rc < 0) {
		__nd_detach_ndns(pfn_dev, &nd_pfn->ndns);
		put_device(pfn_dev);
	} else
		__nd_device_register(pfn_dev);

	return rc;
}
EXPORT_SYMBOL(nd_pfn_probe);

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
	unsigned long reserve = PHYS_PFN(SZ_8K);
	unsigned long base_pfn = PHYS_PFN(base);

	reserve += base_pfn - PFN_SECTION_ALIGN_DOWN(base_pfn);
	return reserve;
}

static struct vmem_altmap *__nvdimm_setup_pfn(struct nd_pfn *nd_pfn,
		struct resource *res, struct vmem_altmap *altmap)
{
	struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
	u64 offset = le64_to_cpu(pfn_sb->dataoff);
	u32 start_pad = __le32_to_cpu(pfn_sb->start_pad);
	u32 end_trunc = __le32_to_cpu(pfn_sb->end_trunc);
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
		if (offset < SZ_8K)
			return ERR_PTR(-EINVAL);
		nd_pfn->npfns = le64_to_cpu(pfn_sb->npfns);
		altmap = NULL;
	} else if (nd_pfn->mode == PFN_MODE_PMEM) {
		nd_pfn->npfns = (resource_size(res) - offset) / PAGE_SIZE;
		if (le64_to_cpu(nd_pfn->pfn_sb->npfns) > nd_pfn->npfns)
			dev_info(&nd_pfn->dev,
					"number of pfns truncated from %lld to %ld\n",
					le64_to_cpu(nd_pfn->pfn_sb->npfns),
					nd_pfn->npfns);
		memcpy(altmap, &__altmap, sizeof(*altmap));
		altmap->free = PHYS_PFN(offset - SZ_8K);
		altmap->alloc = 0;
	} else
		return ERR_PTR(-ENXIO);

	return altmap;
}

static int nd_pfn_init(struct nd_pfn *nd_pfn)
{
	u32 dax_label_reserve = is_nd_dax(&nd_pfn->dev) ? SZ_128K : 0;
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	u32 start_pad = 0, end_trunc = 0;
	resource_size_t start, size;
	struct nd_namespace_io *nsio;
	struct nd_region *nd_region;
	struct nd_pfn_sb *pfn_sb;
	unsigned long npfns;
	phys_addr_t offset;
	const char *sig;
	u64 checksum;
	int rc;

	pfn_sb = devm_kzalloc(&nd_pfn->dev, sizeof(*pfn_sb), GFP_KERNEL);
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
	nd_region = to_nd_region(nd_pfn->dev.parent);
	if (nd_region->ro) {
		dev_info(&nd_pfn->dev,
				"%s is read-only, unable to init metadata\n",
				dev_name(&nd_region->dev));
		return -ENXIO;
	}

	memset(pfn_sb, 0, sizeof(*pfn_sb));

	/*
	 * Check if pmem collides with 'System RAM' when section aligned and
	 * trim it accordingly
	 */
	nsio = to_nd_namespace_io(&ndns->dev);
	start = PHYS_SECTION_ALIGN_DOWN(nsio->res.start);
	size = resource_size(&nsio->res);
	if (region_intersects(start, size, IORESOURCE_SYSTEM_RAM,
				IORES_DESC_NONE) == REGION_MIXED) {
		start = nsio->res.start;
		start_pad = PHYS_SECTION_ALIGN_UP(start) - start;
	}

	start = nsio->res.start;
	size = PHYS_SECTION_ALIGN_UP(start + size) - start;
	if (region_intersects(start, size, IORESOURCE_SYSTEM_RAM,
				IORES_DESC_NONE) == REGION_MIXED) {
		size = resource_size(&nsio->res);
		end_trunc = start + size - PHYS_SECTION_ALIGN_DOWN(start + size);
	}

	if (start_pad + end_trunc)
		dev_info(&nd_pfn->dev, "%s section collision, truncate %d bytes\n",
				dev_name(&ndns->dev), start_pad + end_trunc);

	/*
	 * Note, we use 64 here for the standard size of struct page,
	 * debugging options may cause it to be larger in which case the
	 * implementation will limit the pfns advertised through
	 * ->direct_access() to those that are included in the memmap.
	 */
	start += start_pad;
	size = resource_size(&nsio->res);
	npfns = (size - start_pad - end_trunc - SZ_8K) / SZ_4K;
	if (nd_pfn->mode == PFN_MODE_PMEM) {
		unsigned long memmap_size;

		/*
		 * vmemmap_populate_hugepages() allocates the memmap array in
		 * HPAGE_SIZE chunks.
		 */
		memmap_size = ALIGN(64 * npfns, HPAGE_SIZE);
		offset = ALIGN(start + SZ_8K + memmap_size + dax_label_reserve,
				nd_pfn->align) - start;
	} else if (nd_pfn->mode == PFN_MODE_RAM)
		offset = ALIGN(start + SZ_8K + dax_label_reserve,
				nd_pfn->align) - start;
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
	pfn_sb->version_minor = cpu_to_le16(2);
	pfn_sb->start_pad = cpu_to_le32(start_pad);
	pfn_sb->end_trunc = cpu_to_le32(end_trunc);
	pfn_sb->align = cpu_to_le32(nd_pfn->align);
	checksum = nd_sb_checksum((struct nd_gen_sb *) pfn_sb);
	pfn_sb->checksum = cpu_to_le64(checksum);

	return nvdimm_write_bytes(ndns, SZ_4K, pfn_sb, sizeof(*pfn_sb));
}

/*
 * Determine the effective resource range and vmem_altmap from an nd_pfn
 * instance.
 */
struct vmem_altmap *nvdimm_setup_pfn(struct nd_pfn *nd_pfn,
		struct resource *res, struct vmem_altmap *altmap)
{
	int rc;

	if (!nd_pfn->uuid || !nd_pfn->ndns)
		return ERR_PTR(-ENODEV);

	rc = nd_pfn_init(nd_pfn);
	if (rc)
		return ERR_PTR(rc);

	/* we need a valid pfn_sb before we can init a vmem_altmap */
	return __nvdimm_setup_pfn(nd_pfn, res, altmap);
}
EXPORT_SYMBOL_GPL(nvdimm_setup_pfn);
