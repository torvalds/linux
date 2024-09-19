// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitory Technology Telemetry driver
 *
 * Copyright (c) 2020, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: "Alexander Duyck" <alexander.h.duyck@linux.intel.com>
 */

#include <linux/kernel.h>
#include <linux/intel_vsec.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pci.h>

#include "class.h"

#define PMT_XA_START		1
#define PMT_XA_MAX		INT_MAX
#define PMT_XA_LIMIT		XA_LIMIT(PMT_XA_START, PMT_XA_MAX)
#define GUID_SPR_PUNIT		0x9956f43f

bool intel_pmt_is_early_client_hw(struct device *dev)
{
	struct intel_vsec_device *ivdev = dev_to_ivdev(dev);

	/*
	 * Early implementations of PMT on client platforms have some
	 * differences from the server platforms (which use the Out Of Band
	 * Management Services Module OOBMSM).
	 */
	return !!(ivdev->quirks & VSEC_QUIRK_EARLY_HW);
}
EXPORT_SYMBOL_NS_GPL(intel_pmt_is_early_client_hw, INTEL_PMT);

static inline int
pmt_memcpy64_fromio(void *to, const u64 __iomem *from, size_t count)
{
	int i, remain;
	u64 *buf = to;

	if (!IS_ALIGNED((unsigned long)from, 8))
		return -EFAULT;

	for (i = 0; i < count/8; i++)
		buf[i] = readq(&from[i]);

	/* Copy any remaining bytes */
	remain = count % 8;
	if (remain) {
		u64 tmp = readq(&from[i]);

		memcpy(&buf[i], &tmp, remain);
	}

	return count;
}

int pmt_telem_read_mmio(struct pci_dev *pdev, struct pmt_callbacks *cb, u32 guid, void *buf,
			void __iomem *addr, u32 count)
{
	if (cb && cb->read_telem)
		return cb->read_telem(pdev, guid, buf, count);

	if (guid == GUID_SPR_PUNIT)
		/* PUNIT on SPR only supports aligned 64-bit read */
		return pmt_memcpy64_fromio(buf, addr, count);

	memcpy_fromio(buf, addr, count);

	return count;
}
EXPORT_SYMBOL_NS_GPL(pmt_telem_read_mmio, INTEL_PMT);

/*
 * sysfs
 */
static ssize_t
intel_pmt_read(struct file *filp, struct kobject *kobj,
	       struct bin_attribute *attr, char *buf, loff_t off,
	       size_t count)
{
	struct intel_pmt_entry *entry = container_of(attr,
						     struct intel_pmt_entry,
						     pmt_bin_attr);

	if (off < 0)
		return -EINVAL;

	if (off >= entry->size)
		return 0;

	if (count > entry->size - off)
		count = entry->size - off;

	count = pmt_telem_read_mmio(entry->ep->pcidev, entry->cb, entry->header.guid, buf,
				    entry->base + off, count);

	return count;
}

static int
intel_pmt_mmap(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, struct vm_area_struct *vma)
{
	struct intel_pmt_entry *entry = container_of(attr,
						     struct intel_pmt_entry,
						     pmt_bin_attr);
	unsigned long vsize = vma->vm_end - vma->vm_start;
	struct device *dev = kobj_to_dev(kobj);
	unsigned long phys = entry->base_addr;
	unsigned long pfn = PFN_DOWN(phys);
	unsigned long psize;

	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		return -EROFS;

	psize = (PFN_UP(entry->base_addr + entry->size) - pfn) * PAGE_SIZE;
	if (vsize > psize) {
		dev_err(dev, "Requested mmap size is too large\n");
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (io_remap_pfn_range(vma, vma->vm_start, pfn,
		vsize, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static ssize_t
guid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct intel_pmt_entry *entry = dev_get_drvdata(dev);

	return sprintf(buf, "0x%x\n", entry->guid);
}
static DEVICE_ATTR_RO(guid);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct intel_pmt_entry *entry = dev_get_drvdata(dev);

	return sprintf(buf, "%zu\n", entry->size);
}
static DEVICE_ATTR_RO(size);

static ssize_t
offset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct intel_pmt_entry *entry = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", offset_in_page(entry->base_addr));
}
static DEVICE_ATTR_RO(offset);

static struct attribute *intel_pmt_attrs[] = {
	&dev_attr_guid.attr,
	&dev_attr_size.attr,
	&dev_attr_offset.attr,
	NULL
};
ATTRIBUTE_GROUPS(intel_pmt);

static struct class intel_pmt_class = {
	.name = "intel_pmt",
	.dev_groups = intel_pmt_groups,
};

static int intel_pmt_populate_entry(struct intel_pmt_entry *entry,
				    struct intel_vsec_device *ivdev,
				    struct resource *disc_res)
{
	struct pci_dev *pci_dev = ivdev->pcidev;
	struct device *dev = &ivdev->auxdev.dev;
	struct intel_pmt_header *header = &entry->header;
	u8 bir;

	/*
	 * The base offset should always be 8 byte aligned.
	 *
	 * For non-local access types the lower 3 bits of base offset
	 * contains the index of the base address register where the
	 * telemetry can be found.
	 */
	bir = GET_BIR(header->base_offset);

	/* Local access and BARID only for now */
	switch (header->access_type) {
	case ACCESS_LOCAL:
		if (bir) {
			dev_err(dev,
				"Unsupported BAR index %d for access type %d\n",
				bir, header->access_type);
			return -EINVAL;
		}
		/*
		 * For access_type LOCAL, the base address is as follows:
		 * base address = end of discovery region + base offset
		 */
		entry->base_addr = disc_res->end + 1 + header->base_offset;

		/*
		 * Some hardware use a different calculation for the base address
		 * when access_type == ACCESS_LOCAL. On the these systems
		 * ACCCESS_LOCAL refers to an address in the same BAR as the
		 * header but at a fixed offset. But as the header address was
		 * supplied to the driver, we don't know which BAR it was in.
		 * So search for the bar whose range includes the header address.
		 */
		if (intel_pmt_is_early_client_hw(dev)) {
			int i;

			entry->base_addr = 0;
			for (i = 0; i < 6; i++)
				if (disc_res->start >= pci_resource_start(pci_dev, i) &&
				   (disc_res->start <= pci_resource_end(pci_dev, i))) {
					entry->base_addr = pci_resource_start(pci_dev, i) +
							   header->base_offset;
					break;
				}
			if (!entry->base_addr)
				return -EINVAL;
		}

		break;
	case ACCESS_BARID:
		/* Use the provided base address if it exists */
		if (ivdev->base_addr) {
			entry->base_addr = ivdev->base_addr +
				   GET_ADDRESS(header->base_offset);
			break;
		}

		/*
		 * If another BAR was specified then the base offset
		 * represents the offset within that BAR. SO retrieve the
		 * address from the parent PCI device and add offset.
		 */
		entry->base_addr = pci_resource_start(pci_dev, bir) +
				   GET_ADDRESS(header->base_offset);
		break;
	default:
		dev_err(dev, "Unsupported access type %d\n",
			header->access_type);
		return -EINVAL;
	}

	entry->guid = header->guid;
	entry->size = header->size;
	entry->cb = ivdev->priv_data;

	return 0;
}

static int intel_pmt_dev_register(struct intel_pmt_entry *entry,
				  struct intel_pmt_namespace *ns,
				  struct device *parent)
{
	struct intel_vsec_device *ivdev = dev_to_ivdev(parent);
	struct resource res = {0};
	struct device *dev;
	int ret;

	ret = xa_alloc(ns->xa, &entry->devid, entry, PMT_XA_LIMIT, GFP_KERNEL);
	if (ret)
		return ret;

	dev = device_create(&intel_pmt_class, parent, MKDEV(0, 0), entry,
			    "%s%d", ns->name, entry->devid);

	if (IS_ERR(dev)) {
		dev_err(parent, "Could not create %s%d device node\n",
			ns->name, entry->devid);
		ret = PTR_ERR(dev);
		goto fail_dev_create;
	}

	entry->kobj = &dev->kobj;

	if (ns->attr_grp) {
		ret = sysfs_create_group(entry->kobj, ns->attr_grp);
		if (ret)
			goto fail_sysfs_create_group;
	}

	/* if size is 0 assume no data buffer, so no file needed */
	if (!entry->size)
		return 0;

	res.start = entry->base_addr;
	res.end = res.start + entry->size - 1;
	res.flags = IORESOURCE_MEM;

	entry->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(entry->base)) {
		ret = PTR_ERR(entry->base);
		goto fail_ioremap;
	}

	sysfs_bin_attr_init(&entry->pmt_bin_attr);
	entry->pmt_bin_attr.attr.name = ns->name;
	entry->pmt_bin_attr.attr.mode = 0440;
	entry->pmt_bin_attr.mmap = intel_pmt_mmap;
	entry->pmt_bin_attr.read = intel_pmt_read;
	entry->pmt_bin_attr.size = entry->size;

	ret = sysfs_create_bin_file(&dev->kobj, &entry->pmt_bin_attr);
	if (ret)
		goto fail_ioremap;

	if (ns->pmt_add_endpoint) {
		ret = ns->pmt_add_endpoint(ivdev, entry);
		if (ret)
			goto fail_add_endpoint;
	}

	return 0;

fail_add_endpoint:
	sysfs_remove_bin_file(entry->kobj, &entry->pmt_bin_attr);
fail_ioremap:
	if (ns->attr_grp)
		sysfs_remove_group(entry->kobj, ns->attr_grp);
fail_sysfs_create_group:
	device_unregister(dev);
fail_dev_create:
	xa_erase(ns->xa, entry->devid);

	return ret;
}

int intel_pmt_dev_create(struct intel_pmt_entry *entry, struct intel_pmt_namespace *ns,
			 struct intel_vsec_device *intel_vsec_dev, int idx)
{
	struct device *dev = &intel_vsec_dev->auxdev.dev;
	struct resource	*disc_res;
	int ret;

	disc_res = &intel_vsec_dev->resource[idx];

	entry->disc_table = devm_ioremap_resource(dev, disc_res);
	if (IS_ERR(entry->disc_table))
		return PTR_ERR(entry->disc_table);

	ret = ns->pmt_header_decode(entry, dev);
	if (ret)
		return ret;

	ret = intel_pmt_populate_entry(entry, intel_vsec_dev, disc_res);
	if (ret)
		return ret;

	return intel_pmt_dev_register(entry, ns, dev);
}
EXPORT_SYMBOL_NS_GPL(intel_pmt_dev_create, INTEL_PMT);

void intel_pmt_dev_destroy(struct intel_pmt_entry *entry,
			   struct intel_pmt_namespace *ns)
{
	struct device *dev = kobj_to_dev(entry->kobj);

	if (entry->size)
		sysfs_remove_bin_file(entry->kobj, &entry->pmt_bin_attr);

	if (ns->attr_grp)
		sysfs_remove_group(entry->kobj, ns->attr_grp);

	device_unregister(dev);
	xa_erase(ns->xa, entry->devid);
}
EXPORT_SYMBOL_NS_GPL(intel_pmt_dev_destroy, INTEL_PMT);

static int __init pmt_class_init(void)
{
	return class_register(&intel_pmt_class);
}

static void __exit pmt_class_exit(void)
{
	class_unregister(&intel_pmt_class);
}

module_init(pmt_class_init);
module_exit(pmt_class_exit);

MODULE_AUTHOR("Alexander Duyck <alexander.h.duyck@linux.intel.com>");
MODULE_DESCRIPTION("Intel PMT Class driver");
MODULE_LICENSE("GPL v2");
