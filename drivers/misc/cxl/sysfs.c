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

static struct device_attribute adapter_attrs[] = {
	__ATTR_RO(caia_version),
	__ATTR_RO(psl_revision),
	__ATTR_RO(base_image),
	__ATTR_RO(image_loaded),
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

	return scnprintf(buf, PAGE_SIZE, "%llu\n", afu->pp_offset);
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

	if ((rc = cxl_afu_reset(afu)))
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

	if (irqs_max > afu->adapter->user_irqs)
		return -EINVAL;

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
	 * cxl_afu_deactivate_mode needs to be done outside the lock, prevent
	 * other contexts coming in before we are ready:
	 */
	old_mode = afu->current_mode;
	afu->current_mode = 0;
	afu->num_procs = 0;

	mutex_unlock(&afu->contexts_lock);

	if ((rc = _cxl_afu_deactivate_mode(afu, old_mode)))
		return rc;
	if ((rc = cxl_afu_activate_mode(afu, mode)))
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
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(adapter_attrs); i++) {
		if ((rc = device_create_file(&adapter->dev, &adapter_attrs[i])))
			goto err;
	}
	return 0;
err:
	for (i--; i >= 0; i--)
		device_remove_file(&adapter->dev, &adapter_attrs[i]);
	return rc;
}
void cxl_sysfs_adapter_remove(struct cxl *adapter)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adapter_attrs); i++)
		device_remove_file(&adapter->dev, &adapter_attrs[i]);
}

int cxl_sysfs_afu_add(struct cxl_afu *afu)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(afu_attrs); i++) {
		if ((rc = device_create_file(&afu->dev, &afu_attrs[i])))
			goto err;
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		device_remove_file(&afu->dev, &afu_attrs[i]);
	return rc;
}

void cxl_sysfs_afu_remove(struct cxl_afu *afu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(afu_attrs); i++)
		device_remove_file(&afu->dev, &afu_attrs[i]);
}

int cxl_sysfs_afu_m_add(struct cxl_afu *afu)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(afu_master_attrs); i++) {
		if ((rc = device_create_file(afu->chardev_m, &afu_master_attrs[i])))
			goto err;
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		device_remove_file(afu->chardev_m, &afu_master_attrs[i]);
	return rc;
}

void cxl_sysfs_afu_m_remove(struct cxl_afu *afu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(afu_master_attrs); i++)
		device_remove_file(afu->chardev_m, &afu_master_attrs[i]);
}
