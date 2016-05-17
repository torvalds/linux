/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/pci_regs.h>

#include "cxl.h"

#define to_afu_chardev_m(d) dev_get_drvdata(d)

/*********  Adapter attributes  **********************************************/

static ssize_t caia_version_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	struct cxl *adapter = to_cxl_adapter(device);

	return scnprintf(buf, PAGE_SIZE, "%i.%i\n", adapter->caia_major,
			 adapter->caia_minor);
}

static ssize_t psl_revision_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	struct cxl *adapter = to_cxl_adapter(device);

	return scnprintf(buf, PAGE_SIZE, "%i\n", adapter->psl_rev);
}

static ssize_t base_image_show(struct device *device,
			       struct device_attribute *attr,
			       char *buf)
{
	struct cxl *adapter = to_cxl_adapter(device);

	return scnprintf(buf, PAGE_SIZE, "%i\n", adapter->base_image);
}

static ssize_t image_loaded_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	struct cxl *adapter = to_cxl_adapter(device);

	if (adapter->user_image_loaded)
		return scnprintf(buf, PAGE_SIZE, "user\n");
	return scnprintf(buf, PAGE_SIZE, "factory\n");
}

static ssize_t reset_adapter_store(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct cxl *adapter = to_cxl_adapter(device);
	int rc;
	int val;

	rc = sscanf(buf, "%i", &val);
	if ((rc != 1) || (val != 1))
		return -EINVAL;

	if ((rc = cxl_ops->adapter_reset(adapter)))
		return rc;
	return count;
}

static ssize_t load_image_on_perst_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	struct cxl *adapter = to_cxl_adapter(device);

	if (!adapter->perst_loads_image)
		return scnprintf(buf, PAGE_SIZE, "none\n");

	if (adapter->perst_select_user)
		return scnprintf(buf, PAGE_SIZE, "user\n");
	return scnprintf(buf, PAGE_SIZE, "factory\n");
}

static ssize_t load_image_on_perst_store(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct cxl *adapter = to_cxl_adapter(device);
	int rc;

	if (!strncmp(buf, "none", 4))
		adapter->perst_loads_image = false;
	else if (!strncmp(buf, "user", 4)) {
		adapter->perst_select_user = true;
		adapter->perst_loads_image = true;
	} else if (!strncmp(buf, "factory", 7)) {
		adapter->perst_select_user = false;
		adapter->perst_loads_image = true;
	} else
		return -EINVAL;

	if ((rc = cxl_update_image_control(adapter)))
		return rc;

	return count;
}

static ssize_t perst_reloads_same_image_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	struct cxl *adapter = to_cxl_adapter(device);

	return scnprintf(buf, PAGE_SIZE, "%i\n", adapter->perst_same_image);
}

static ssize_t perst_reloads_same_image_store(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct cxl *adapter = to_cxl_adapter(device);
	int rc;
	int val;

	rc = sscanf(buf, "%i", &val);
	if ((rc != 1) || !(val == 1 || val == 0))
		return -EINVAL;

	adapter->perst_same_image = (val == 1 ? true : false);
	return count;
}

static struct device_attribute adapter_attrs[] = {
	__ATTR_RO(caia_version),
	__ATTR_RO(psl_revision),
	__ATTR_RO(base_image),
	__ATTR_RO(image_loaded),
	__ATTR_RW(load_image_on_perst),
	__ATTR_RW(perst_reloads_same_image),
	__ATTR(reset, S_IWUSR, NULL, reset_adapter_store),
};


/*********  AFU master specific attributes  **********************************/

static ssize_t mmio_size_show_master(struct device *device,
				     struct device_attribute *attr,
				     char *buf)
{
	struct cxl_afu *afu = to_afu_chardev_m(device);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", afu->adapter->ps_size);
}

static ssize_t pp_mmio_off_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	struct cxl_afu *afu = to_afu_chardev_m(device);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", afu->native->pp_offset);
}

static ssize_t pp_mmio_len_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	struct cxl_afu *afu = to_afu_chardev_m(device);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", afu->pp_size);
}

static struct device_attribute afu_master_attrs[] = {
	__ATTR(mmio_size, S_IRUGO, mmio_size_show_master, NULL),
	__ATTR_RO(pp_mmio_off),
	__ATTR_RO(pp_mmio_len),
};


/*********  AFU attributes  **************************************************/

static ssize_t mmio_size_show(struct device *device,
			      struct device_attribute *attr,
			      char *buf)
{
	struct cxl_afu *afu = to_cxl_afu(device);

	if (afu->pp_size)
		return scnprintf(buf, PAGE_SIZE, "%llu\n", afu->pp_size);
	return scnprintf(buf, PAGE_SIZE, "%llu\n", afu->adapter->ps_size);
}

static ssize_t reset_store_afu(struct device *device,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct cxl_afu *afu = to_cxl_afu(device);
	int rc;

	/* Not safe to reset if it is currently in use */
	mutex_lock(&afu->contexts_lock);
	if (!idr_is_empty(&afu->contexts_idr)) {
		rc = -EBUSY;
		goto err;
	}

	if ((rc = cxl_ops->afu_reset(afu)))
		goto err;

	rc = count;
err:
	mutex_unlock(&afu->contexts_lock);
	return rc;
}

static ssize_t irqs_min_show(struct device *device,
			     struct device_attribute *attr,
			     char *buf)
{
	struct cxl_afu *afu = to_cxl_afu(device);

	return scnprintf(buf, PAGE_SIZE, "%i\n", afu->pp_irqs);
}

static ssize_t irqs_max_show(struct device *device,
				  struct device_attribute *attr,
				  char *buf)
{
	struct cxl_afu *afu = to_cxl_afu(device);

	return scnprintf(buf, PAGE_SIZE, "%i\n", afu->irqs_max);
}

static ssize_t irqs_max_store(struct device *device,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct cxl_afu *afu = to_cxl_afu(device);
	ssize_t ret;
	int irqs_max;

	ret = sscanf(buf, "%i", &irqs_max);
	if (ret != 1)
		return -EINVAL;

	if (irqs_max < afu->pp_irqs)
		return -EINVAL;

	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		if (irqs_max > afu->adapter->user_irqs)
			return -EINVAL;
	} else {
		/* pHyp sets a per-AFU limit */
		if (irqs_max > afu->guest->max_ints)
			return -EINVAL;
	}

	afu->irqs_max = irqs_max;
	return count;
}

static ssize_t modes_supported_show(struct device *device,
				    struct device_attribute *attr, char *buf)
{
	struct cxl_afu *afu = to_cxl_afu(device);
	char *p = buf, *end = buf + PAGE_SIZE;

	if (afu->modes_supported & CXL_MODE_DEDICATED)
		p += scnprintf(p, end - p, "dedicated_process\n");
	if (afu->modes_supported & CXL_MODE_DIRECTED)
		p += scnprintf(p, end - p, "afu_directed\n");
	return (p - buf);
}

static ssize_t prefault_mode_show(struct device *device,
				  struct device_attribute *attr,
				  char *buf)
{
	struct cxl_afu *afu = to_cxl_afu(device);

	switch (afu->prefault_mode) {
	case CXL_PREFAULT_WED:
		return scnprintf(buf, PAGE_SIZE, "work_element_descriptor\n");
	case CXL_PREFAULT_ALL:
		return scnprintf(buf, PAGE_SIZE, "all\n");
	default:
		return scnprintf(buf, PAGE_SIZE, "none\n");
	}
}

static ssize_t prefault_mode_store(struct device *device,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct cxl_afu *afu = to_cxl_afu(device);
	enum prefault_modes mode = -1;

	if (!strncmp(buf, "work_element_descriptor", 23))
		mode = CXL_PREFAULT_WED;
	if (!strncmp(buf, "all", 3))
		mode = CXL_PREFAULT_ALL;
	if (!strncmp(buf, "none", 4))
		mode = CXL_PREFAULT_NONE;

	if (mode == -1)
		return -EINVAL;

	afu->prefault_mode = mode;
	return count;
}

static ssize_t mode_show(struct device *device,
			 struct device_attribute *attr,
			 char *buf)
{
	struct cxl_afu *afu = to_cxl_afu(device);

	if (afu->current_mode == CXL_MODE_DEDICATED)
		return scnprintf(buf, PAGE_SIZE, "dedicated_process\n");
	if (afu->current_mode == CXL_MODE_DIRECTED)
		return scnprintf(buf, PAGE_SIZE, "afu_directed\n");
	return scnprintf(buf, PAGE_SIZE, "none\n");
}

static ssize_t mode_store(struct device *device, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct cxl_afu *afu = to_cxl_afu(device);
	int old_mode, mode = -1;
	int rc = -EBUSY;

	/* can't change this if we have a user */
	mutex_lock(&afu->contexts_lock);
	if (!idr_is_empty(&afu->contexts_idr))
		goto err;

	if (!strncmp(buf, "dedicated_process", 17))
		mode = CXL_MODE_DEDICATED;
	if (!strncmp(buf, "afu_directed", 12))
		mode = CXL_MODE_DIRECTED;
	if (!strncmp(buf, "none", 4))
		mode = 0;

	if (mode == -1) {
		rc = -EINVAL;
		goto err;
	}

	/*
	 * afu_deactivate_mode needs to be done outside the lock, prevent
	 * other contexts coming in before we are ready:
	 */
	old_mode = afu->current_mode;
	afu->current_mode = 0;
	afu->num_procs = 0;

	mutex_unlock(&afu->contexts_lock);

	if ((rc = cxl_ops->afu_deactivate_mode(afu, old_mode)))
		return rc;
	if ((rc = cxl_ops->afu_activate_mode(afu, mode)))
		return rc;

	return count;
err:
	mutex_unlock(&afu->contexts_lock);
	return rc;
}

static ssize_t api_version_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%i\n", CXL_API_VERSION);
}

static ssize_t api_version_compatible_show(struct device *device,
					   struct device_attribute *attr,
					   char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%i\n", CXL_API_VERSION_COMPATIBLE);
}

static ssize_t afu_eb_read(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t off, size_t count)
{
	struct cxl_afu *afu = to_cxl_afu(kobj_to_dev(kobj));

	return cxl_ops->afu_read_err_buffer(afu, buf, off, count);
}

static struct device_attribute afu_attrs[] = {
	__ATTR_RO(mmio_size),
	__ATTR_RO(irqs_min),
	__ATTR_RW(irqs_max),
	__ATTR_RO(modes_supported),
	__ATTR_RW(mode),
	__ATTR_RW(prefault_mode),
	__ATTR_RO(api_version),
	__ATTR_RO(api_version_compatible),
	__ATTR(reset, S_IWUSR, NULL, reset_store_afu),
};

int cxl_sysfs_adapter_add(struct cxl *adapter)
{
	struct device_attribute *dev_attr;
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(adapter_attrs); i++) {
		dev_attr = &adapter_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_ADAPTER_ATTRS)) {
			if ((rc = device_create_file(&adapter->dev, dev_attr)))
				goto err;
		}
	}
	return 0;
err:
	for (i--; i >= 0; i--) {
		dev_attr = &adapter_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_ADAPTER_ATTRS))
			device_remove_file(&adapter->dev, dev_attr);
	}
	return rc;
}

void cxl_sysfs_adapter_remove(struct cxl *adapter)
{
	struct device_attribute *dev_attr;
	int i;

	for (i = 0; i < ARRAY_SIZE(adapter_attrs); i++) {
		dev_attr = &adapter_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_ADAPTER_ATTRS))
			device_remove_file(&adapter->dev, dev_attr);
	}
}

struct afu_config_record {
	struct kobject kobj;
	struct bin_attribute config_attr;
	struct list_head list;
	int cr;
	u16 device;
	u16 vendor;
	u32 class;
};

#define to_cr(obj) container_of(obj, struct afu_config_record, kobj)

static ssize_t vendor_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	struct afu_config_record *cr = to_cr(kobj);

	return scnprintf(buf, PAGE_SIZE, "0x%.4x\n", cr->vendor);
}

static ssize_t device_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	struct afu_config_record *cr = to_cr(kobj);

	return scnprintf(buf, PAGE_SIZE, "0x%.4x\n", cr->device);
}

static ssize_t class_show(struct kobject *kobj,
			  struct kobj_attribute *attr, char *buf)
{
	struct afu_config_record *cr = to_cr(kobj);

	return scnprintf(buf, PAGE_SIZE, "0x%.6x\n", cr->class);
}

static ssize_t afu_read_config(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t off, size_t count)
{
	struct afu_config_record *cr = to_cr(kobj);
	struct cxl_afu *afu = to_cxl_afu(kobj_to_dev(kobj->parent));

	u64 i, j, val, rc;

	for (i = 0; i < count;) {
		rc = cxl_ops->afu_cr_read64(afu, cr->cr, off & ~0x7, &val);
		if (rc)
			val = ~0ULL;
		for (j = off & 0x7; j < 8 && i < count; i++, j++, off++)
			buf[i] = (val >> (j * 8)) & 0xff;
	}

	return count;
}

static struct kobj_attribute vendor_attribute =
	__ATTR_RO(vendor);
static struct kobj_attribute device_attribute =
	__ATTR_RO(device);
static struct kobj_attribute class_attribute =
	__ATTR_RO(class);

static struct attribute *afu_cr_attrs[] = {
	&vendor_attribute.attr,
	&device_attribute.attr,
	&class_attribute.attr,
	NULL,
};

static void release_afu_config_record(struct kobject *kobj)
{
	struct afu_config_record *cr = to_cr(kobj);

	kfree(cr);
}

static struct kobj_type afu_config_record_type = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = release_afu_config_record,
	.default_attrs = afu_cr_attrs,
};

static struct afu_config_record *cxl_sysfs_afu_new_cr(struct cxl_afu *afu, int cr_idx)
{
	struct afu_config_record *cr;
	int rc;

	cr = kzalloc(sizeof(struct afu_config_record), GFP_KERNEL);
	if (!cr)
		return ERR_PTR(-ENOMEM);

	cr->cr = cr_idx;

	rc = cxl_ops->afu_cr_read16(afu, cr_idx, PCI_DEVICE_ID, &cr->device);
	if (rc)
		goto err;
	rc = cxl_ops->afu_cr_read16(afu, cr_idx, PCI_VENDOR_ID, &cr->vendor);
	if (rc)
		goto err;
	rc = cxl_ops->afu_cr_read32(afu, cr_idx, PCI_CLASS_REVISION, &cr->class);
	if (rc)
		goto err;
	cr->class >>= 8;

	/*
	 * Export raw AFU PCIe like config record. For now this is read only by
	 * root - we can expand that later to be readable by non-root and maybe
	 * even writable provided we have a good use-case. Once we support
	 * exposing AFUs through a virtual PHB they will get that for free from
	 * Linux' PCI infrastructure, but until then it's not clear that we
	 * need it for anything since the main use case is just identifying
	 * AFUs, which can be done via the vendor, device and class attributes.
	 */
	sysfs_bin_attr_init(&cr->config_attr);
	cr->config_attr.attr.name = "config";
	cr->config_attr.attr.mode = S_IRUSR;
	cr->config_attr.size = afu->crs_len;
	cr->config_attr.read = afu_read_config;

	rc = kobject_init_and_add(&cr->kobj, &afu_config_record_type,
				  &afu->dev.kobj, "cr%i", cr->cr);
	if (rc)
		goto err;

	rc = sysfs_create_bin_file(&cr->kobj, &cr->config_attr);
	if (rc)
		goto err1;

	rc = kobject_uevent(&cr->kobj, KOBJ_ADD);
	if (rc)
		goto err2;

	return cr;
err2:
	sysfs_remove_bin_file(&cr->kobj, &cr->config_attr);
err1:
	kobject_put(&cr->kobj);
	return ERR_PTR(rc);
err:
	kfree(cr);
	return ERR_PTR(rc);
}

void cxl_sysfs_afu_remove(struct cxl_afu *afu)
{
	struct device_attribute *dev_attr;
	struct afu_config_record *cr, *tmp;
	int i;

	/* remove the err buffer bin attribute */
	if (afu->eb_len)
		device_remove_bin_file(&afu->dev, &afu->attr_eb);

	for (i = 0; i < ARRAY_SIZE(afu_attrs); i++) {
		dev_attr = &afu_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_AFU_ATTRS))
			device_remove_file(&afu->dev, &afu_attrs[i]);
	}

	list_for_each_entry_safe(cr, tmp, &afu->crs, list) {
		sysfs_remove_bin_file(&cr->kobj, &cr->config_attr);
		kobject_put(&cr->kobj);
	}
}

int cxl_sysfs_afu_add(struct cxl_afu *afu)
{
	struct device_attribute *dev_attr;
	struct afu_config_record *cr;
	int i, rc;

	INIT_LIST_HEAD(&afu->crs);

	for (i = 0; i < ARRAY_SIZE(afu_attrs); i++) {
		dev_attr = &afu_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_AFU_ATTRS)) {
			if ((rc = device_create_file(&afu->dev, &afu_attrs[i])))
				goto err;
		}
	}

	/* conditionally create the add the binary file for error info buffer */
	if (afu->eb_len) {
		sysfs_attr_init(&afu->attr_eb.attr);

		afu->attr_eb.attr.name = "afu_err_buff";
		afu->attr_eb.attr.mode = S_IRUGO;
		afu->attr_eb.size = afu->eb_len;
		afu->attr_eb.read = afu_eb_read;

		rc = device_create_bin_file(&afu->dev, &afu->attr_eb);
		if (rc) {
			dev_err(&afu->dev,
				"Unable to create eb attr for the afu. Err(%d)\n",
				rc);
			goto err;
		}
	}

	for (i = 0; i < afu->crs_num; i++) {
		cr = cxl_sysfs_afu_new_cr(afu, i);
		if (IS_ERR(cr)) {
			rc = PTR_ERR(cr);
			goto err1;
		}
		list_add(&cr->list, &afu->crs);
	}

	return 0;

err1:
	cxl_sysfs_afu_remove(afu);
	return rc;
err:
	/* reset the eb_len as we havent created the bin attr */
	afu->eb_len = 0;

	for (i--; i >= 0; i--) {
		dev_attr = &afu_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_AFU_ATTRS))
		device_remove_file(&afu->dev, &afu_attrs[i]);
	}
	return rc;
}

int cxl_sysfs_afu_m_add(struct cxl_afu *afu)
{
	struct device_attribute *dev_attr;
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(afu_master_attrs); i++) {
		dev_attr = &afu_master_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_AFU_MASTER_ATTRS)) {
			if ((rc = device_create_file(afu->chardev_m, &afu_master_attrs[i])))
				goto err;
		}
	}

	return 0;

err:
	for (i--; i >= 0; i--) {
		dev_attr = &afu_master_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_AFU_MASTER_ATTRS))
			device_remove_file(afu->chardev_m, &afu_master_attrs[i]);
	}
	return rc;
}

void cxl_sysfs_afu_m_remove(struct cxl_afu *afu)
{
	struct device_attribute *dev_attr;
	int i;

	for (i = 0; i < ARRAY_SIZE(afu_master_attrs); i++) {
		dev_attr = &afu_master_attrs[i];
		if (cxl_ops->support_attributes(dev_attr->attr.name,
						CXL_AFU_MASTER_ATTRS))
			device_remove_file(afu->chardev_m, &afu_master_attrs[i]);
	}
}
