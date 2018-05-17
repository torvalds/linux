// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <linux/sysfs.h>
#include "ocxl_internal.h"

static ssize_t global_mmio_size_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	struct ocxl_afu *afu = to_ocxl_afu(device);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			afu->config.global_mmio_size);
}

static ssize_t pp_mmio_size_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	struct ocxl_afu *afu = to_ocxl_afu(device);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			afu->config.pp_mmio_stride);
}

static ssize_t afu_version_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	struct ocxl_afu *afu = to_ocxl_afu(device);

	return scnprintf(buf, PAGE_SIZE, "%hhu:%hhu\n",
			afu->config.version_major,
			afu->config.version_minor);
}

static ssize_t contexts_show(struct device *device,
		struct device_attribute *attr,
		char *buf)
{
	struct ocxl_afu *afu = to_ocxl_afu(device);

	return scnprintf(buf, PAGE_SIZE, "%d/%d\n",
			afu->pasid_count, afu->pasid_max);
}

static struct device_attribute afu_attrs[] = {
	__ATTR_RO(global_mmio_size),
	__ATTR_RO(pp_mmio_size),
	__ATTR_RO(afu_version),
	__ATTR_RO(contexts),
};

static ssize_t global_mmio_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	struct ocxl_afu *afu = to_ocxl_afu(kobj_to_dev(kobj));

	if (count == 0 || off < 0 ||
		off >= afu->config.global_mmio_size)
		return 0;
	memcpy_fromio(buf, afu->global_mmio_ptr + off, count);
	return count;
}

static int global_mmio_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct ocxl_afu *afu = vma->vm_private_data;
	unsigned long offset;

	if (vmf->pgoff >= (afu->config.global_mmio_size >> PAGE_SHIFT))
		return VM_FAULT_SIGBUS;

	offset = vmf->pgoff;
	offset += (afu->global_mmio_start >> PAGE_SHIFT);
	vm_insert_pfn(vma, vmf->address, offset);
	return VM_FAULT_NOPAGE;
}

static const struct vm_operations_struct global_mmio_vmops = {
	.fault = global_mmio_fault,
};

static int global_mmio_mmap(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			struct vm_area_struct *vma)
{
	struct ocxl_afu *afu = to_ocxl_afu(kobj_to_dev(kobj));

	if ((vma_pages(vma) + vma->vm_pgoff) >
		(afu->config.global_mmio_size >> PAGE_SHIFT))
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = &global_mmio_vmops;
	vma->vm_private_data = afu;
	return 0;
}

int ocxl_sysfs_add_afu(struct ocxl_afu *afu)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(afu_attrs); i++) {
		rc = device_create_file(&afu->dev, &afu_attrs[i]);
		if (rc)
			goto err;
	}

	sysfs_attr_init(&afu->attr_global_mmio.attr);
	afu->attr_global_mmio.attr.name = "global_mmio_area";
	afu->attr_global_mmio.attr.mode = 0600;
	afu->attr_global_mmio.size = afu->config.global_mmio_size;
	afu->attr_global_mmio.read = global_mmio_read;
	afu->attr_global_mmio.mmap = global_mmio_mmap;
	rc = device_create_bin_file(&afu->dev, &afu->attr_global_mmio);
	if (rc) {
		dev_err(&afu->dev,
			"Unable to create global mmio attr for afu: %d\n",
			rc);
		goto err;
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		device_remove_file(&afu->dev, &afu_attrs[i]);
	return rc;
}

void ocxl_sysfs_remove_afu(struct ocxl_afu *afu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(afu_attrs); i++)
		device_remove_file(&afu->dev, &afu_attrs[i]);
	device_remove_bin_file(&afu->dev, &afu->attr_global_mmio);
}
