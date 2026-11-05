// SPDX-License-Identifier: GPL-2.0
/*
 * Export the firmware instance and label associated with a PCI device to
 * sysfs
 *
 * Copyright (C) 2010 Dell Inc.
 * by Narendra K <Narendra_K@dell.com>,
 * Jordan Hargrave <Jordan_Hargrave@dell.com>
 *
 * PCI Firmware Specification Revision 3.1 section 4.6.7 (DSM for Naming a
 * PCI or PCI Express Device Under Operating Systems) defines an instance
 * number and string name. This code retrieves them and exports them to sysfs.
 * If the system firmware does not provide the ACPI _DSM (Device Specific
 * Method), then the SMBIOS type 41 instance number and string is exported to
 * sysfs.
 *
 * SMBIOS defines type 41 for onboard pci devices. This code retrieves
 * the instance number and string from the type 41 record and exports
 * it to sysfs.
 *
 * Please see https://linux.dell.com/files/biosdevname/ for more
 * information.
 */

#include <linux/dmi.h>
#include <linux/sysfs.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/nls.h>
#include <linux/acpi.h>
#include <linux/pci-acpi.h>
#include "pci.h"

static bool device_has_acpi_name(struct device *dev)
{
#ifdef CONFIG_ACPI
	acpi_handle handle = ACPI_HANDLE(dev);

	if (!handle)
		return false;

	return acpi_check_dsm(handle, &pci_acpi_dsm_guid, 0x2,
			      1 << DSM_PCI_DEVICE_NAME);
#else
	return false;
#endif
}

#ifdef CONFIG_DMI
enum smbios_attr_enum {
	SMBIOS_ATTR_NONE = 0,
	SMBIOS_ATTR_LABEL_SHOW,
	SMBIOS_ATTR_INSTANCE_SHOW,
};

static size_t find_smbios_instance_string(struct pci_dev *pdev, char *buf,
					  enum smbios_attr_enum attribute)
{
	const struct dmi_device *dmi;
	struct dmi_dev_onboard *donboard;
	int domain_nr = pci_domain_nr(pdev->bus);
	int bus = pdev->bus->number;
	int devfn = pdev->devfn;

	dmi = NULL;
	while ((dmi = dmi_find_device(DMI_DEV_TYPE_DEV_ONBOARD,
				      NULL, dmi)) != NULL) {
		donboard = dmi->device_data;
		if (donboard && donboard->segment == domain_nr &&
				donboard->bus == bus &&
				donboard->devfn == devfn) {
			if (buf) {
				if (attribute == SMBIOS_ATTR_INSTANCE_SHOW)
					return sysfs_emit(buf, "%d\n",
							  donboard->instance);
				else if (attribute == SMBIOS_ATTR_LABEL_SHOW)
					return sysfs_emit(buf, "%s\n",
							  dmi->name);
			}
			return strlen(dmi->name);
		}
	}
	return 0;
}

static ssize_t smbios_label_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, buf,
					   SMBIOS_ATTR_LABEL_SHOW);
}
static struct device_attribute dev_attr_smbios_label = __ATTR(label, 0444,
						    smbios_label_show, NULL);

static ssize_t index_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, buf,
					   SMBIOS_ATTR_INSTANCE_SHOW);
}
static DEVICE_ATTR_RO(index);

static struct attribute *smbios_attrs[] = {
	&dev_attr_smbios_label.attr,
	&dev_attr_index.attr,
	NULL,
};

static umode_t smbios_attr_is_visible(struct kobject *kobj, struct attribute *a,
				      int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (device_has_acpi_name(dev))
		return 0;

	if (!find_smbios_instance_string(pdev, NULL, SMBIOS_ATTR_NONE))
		return 0;

	return a->mode;
}

const struct attribute_group pci_dev_smbios_attr_group = {
	.attrs = smbios_attrs,
	.is_visible = smbios_attr_is_visible,
};
#endif

#ifdef CONFIG_ACPI
enum acpi_attr_enum {
	ACPI_ATTR_LABEL_SHOW,
	ACPI_ATTR_INDEX_SHOW,
};

static int dsm_label_utf16s_to_utf8s(union acpi_object *obj, char *buf)
{
	int len;

	len = utf16s_to_utf8s((const wchar_t *)obj->buffer.pointer,
			      obj->buffer.length / sizeof(wchar_t),
			      UTF16_LITTLE_ENDIAN,
			      buf, PAGE_SIZE - 1);
	buf[len++] = '\n';

	return len;
}

static int dsm_get_label(struct device *dev, char *buf,
			 enum acpi_attr_enum attr)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object *obj, *tmp;
	int len;

	if (!handle)
		return -ENODEV;

	obj = acpi_evaluate_dsm(handle, &pci_acpi_dsm_guid, 0x2,
				DSM_PCI_DEVICE_NAME, NULL);
	if (!obj)
		return -EIO;

	if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 2) {
		len = -EIO;
		goto out;
	}

	tmp = obj->package.elements;
	if (tmp[0].type != ACPI_TYPE_INTEGER) {
		len = -EIO;
		goto out;
	}

	if (attr == ACPI_ATTR_INDEX_SHOW) {
		len = sysfs_emit(buf, "%llu\n", tmp[0].integer.value);
		goto out;
	}

	/*
	 * Per PCI Firmware r3.3, sec 4.6.7, the device name is optional
	 * even when this _DSM is implemented. When not implemented, this
	 * entry must return a NULL string.
	 */
	switch (tmp[1].type) {
	case ACPI_TYPE_STRING:
		len = sysfs_emit(buf, "%s\n", tmp[1].string.pointer);
		break;
	case ACPI_TYPE_BUFFER:
		len = dsm_label_utf16s_to_utf8s(&tmp[1], buf);
		break;
	default:
		len = -EIO;
		break;
	}

out:
	ACPI_FREE(obj);

	return len;
}

static ssize_t label_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return dsm_get_label(dev, buf, ACPI_ATTR_LABEL_SHOW);
}
static DEVICE_ATTR_RO(label);

static ssize_t acpi_index_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return dsm_get_label(dev, buf, ACPI_ATTR_INDEX_SHOW);
}
static DEVICE_ATTR_RO(acpi_index);

static struct attribute *acpi_attrs[] = {
	&dev_attr_label.attr,
	&dev_attr_acpi_index.attr,
	NULL,
};

static umode_t acpi_attr_is_visible(struct kobject *kobj, struct attribute *a,
				    int n)
{
	struct device *dev = kobj_to_dev(kobj);
	union acpi_object *obj, *tmp;
	umode_t mode = 0;

	if (!device_has_acpi_name(dev))
		return 0;

	/*
	 * The bitmap from _DSM function 0 only advertises function 7,
	 * and whether the returned object can be parsed is a separate
	 * question. Evaluate it and expose each attribute only if the
	 * element it exports has one of the types the read path
	 * accepts, mirroring the checks in dsm_get_label().
	 */
	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), &pci_acpi_dsm_guid, 0x2,
				DSM_PCI_DEVICE_NAME, NULL);
	if (!obj)
		return 0;

	if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 2)
		goto out;

	tmp = obj->package.elements;
	if (tmp[0].type != ACPI_TYPE_INTEGER)
		goto out;

	if (a == &dev_attr_acpi_index.attr)
		mode = a->mode;
	else if (a == &dev_attr_label.attr &&
		 (tmp[1].type == ACPI_TYPE_STRING ||
		  tmp[1].type == ACPI_TYPE_BUFFER))
		mode = a->mode;

out:
	ACPI_FREE(obj);

	return mode;
}

const struct attribute_group pci_dev_acpi_attr_group = {
	.attrs = acpi_attrs,
	.is_visible = acpi_attr_is_visible,
};
#endif
