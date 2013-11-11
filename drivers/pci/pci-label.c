/*
 * Purpose: Export the firmware instance and label associated with
 * a pci device to sysfs
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
 * Please see http://linux.dell.com/wiki/index.php/Oss/libnetdevname for more
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
#include <acpi/acpi_bus.h>
#include "pci.h"

#define	DEVICE_LABEL_DSM	0x07

#ifndef CONFIG_DMI

static inline int
pci_create_smbiosname_file(struct pci_dev *pdev)
{
	return -1;
}

static inline void
pci_remove_smbiosname_file(struct pci_dev *pdev)
{
}

#else

enum smbios_attr_enum {
	SMBIOS_ATTR_NONE = 0,
	SMBIOS_ATTR_LABEL_SHOW,
	SMBIOS_ATTR_INSTANCE_SHOW,
};

static size_t
find_smbios_instance_string(struct pci_dev *pdev, char *buf,
			    enum smbios_attr_enum attribute)
{
	const struct dmi_device *dmi;
	struct dmi_dev_onboard *donboard;
	int bus;
	int devfn;

	bus = pdev->bus->number;
	devfn = pdev->devfn;

	dmi = NULL;
	while ((dmi = dmi_find_device(DMI_DEV_TYPE_DEV_ONBOARD,
				      NULL, dmi)) != NULL) {
		donboard = dmi->device_data;
		if (donboard && donboard->bus == bus &&
					donboard->devfn == devfn) {
			if (buf) {
				if (attribute == SMBIOS_ATTR_INSTANCE_SHOW)
					return scnprintf(buf, PAGE_SIZE,
							 "%d\n",
							 donboard->instance);
				else if (attribute == SMBIOS_ATTR_LABEL_SHOW)
					return scnprintf(buf, PAGE_SIZE,
							 "%s\n",
							 dmi->name);
			}
			return strlen(dmi->name);
		}
	}
	return 0;
}

static umode_t
smbios_instance_string_exist(struct kobject *kobj, struct attribute *attr,
			     int n)
{
	struct device *dev;
	struct pci_dev *pdev;

	dev = container_of(kobj, struct device, kobj);
	pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, NULL, SMBIOS_ATTR_NONE) ?
					   S_IRUGO : 0;
}

static ssize_t
smbioslabel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev;
	pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, buf,
					   SMBIOS_ATTR_LABEL_SHOW);
}

static ssize_t
smbiosinstance_show(struct device *dev,
		    struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev;
	pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, buf,
					   SMBIOS_ATTR_INSTANCE_SHOW);
}

static struct device_attribute smbios_attr_label = {
	.attr = {.name = "label", .mode = 0444},
	.show = smbioslabel_show,
};

static struct device_attribute smbios_attr_instance = {
	.attr = {.name = "index", .mode = 0444},
	.show = smbiosinstance_show,
};

static struct attribute *smbios_attributes[] = {
	&smbios_attr_label.attr,
	&smbios_attr_instance.attr,
	NULL,
};

static struct attribute_group smbios_attr_group = {
	.attrs = smbios_attributes,
	.is_visible = smbios_instance_string_exist,
};

static int
pci_create_smbiosname_file(struct pci_dev *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &smbios_attr_group);
}

static void
pci_remove_smbiosname_file(struct pci_dev *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &smbios_attr_group);
}

#endif

#ifndef CONFIG_ACPI

static inline int
pci_create_acpi_index_label_files(struct pci_dev *pdev)
{
	return -1;
}

static inline int
pci_remove_acpi_index_label_files(struct pci_dev *pdev)
{
	return -1;
}

static inline bool
device_has_dsm(struct device *dev)
{
	return false;
}

#else

static const char device_label_dsm_uuid[] = {
	0xD0, 0x37, 0xC9, 0xE5, 0x53, 0x35, 0x7A, 0x4D,
	0x91, 0x17, 0xEA, 0x4D, 0x19, 0xC3, 0x43, 0x4D
};

enum acpi_attr_enum {
	ACPI_ATTR_NONE = 0,
	ACPI_ATTR_LABEL_SHOW,
	ACPI_ATTR_INDEX_SHOW,
};

static void dsm_label_utf16s_to_utf8s(union acpi_object *obj, char *buf)
{
	int len;
	len = utf16s_to_utf8s((const wchar_t *)obj->
			      package.elements[1].string.pointer,
			      obj->package.elements[1].string.length,
			      UTF16_LITTLE_ENDIAN,
			      buf, PAGE_SIZE);
	buf[len] = '\n';
}

static int
dsm_get_label(acpi_handle handle, int func,
	      struct acpi_buffer *output,
	      char *buf, enum acpi_attr_enum attribute)
{
	struct acpi_object_list input;
	union acpi_object params[4];
	union acpi_object *obj;
	int len = 0;

	int err;

	input.count = 4;
	input.pointer = params;
	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = sizeof(device_label_dsm_uuid);
	params[0].buffer.pointer = (char *)device_label_dsm_uuid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = 0x02;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = func;
	params[3].type = ACPI_TYPE_PACKAGE;
	params[3].package.count = 0;
	params[3].package.elements = NULL;

	err = acpi_evaluate_object(handle, "_DSM", &input, output);
	if (err)
		return -1;

	obj = (union acpi_object *)output->pointer;

	switch (obj->type) {
	case ACPI_TYPE_PACKAGE:
		if (obj->package.count != 2)
			break;
		len = obj->package.elements[0].integer.value;
		if (buf) {
			if (attribute == ACPI_ATTR_INDEX_SHOW)
				scnprintf(buf, PAGE_SIZE, "%llu\n",
				obj->package.elements[0].integer.value);
			else if (attribute == ACPI_ATTR_LABEL_SHOW)
				dsm_label_utf16s_to_utf8s(obj, buf);
			kfree(output->pointer);
			return strlen(buf);
		}
		kfree(output->pointer);
		return len;
	break;
	default:
		kfree(output->pointer);
	}
	return -1;
}

static bool
device_has_dsm(struct device *dev)
{
	acpi_handle handle;
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};

	handle = DEVICE_ACPI_HANDLE(dev);

	if (!handle)
		return FALSE;

	if (dsm_get_label(handle, DEVICE_LABEL_DSM, &output, NULL,
			  ACPI_ATTR_NONE) > 0)
		return TRUE;

	return FALSE;
}

static umode_t
acpi_index_string_exist(struct kobject *kobj, struct attribute *attr, int n)
{
	struct device *dev;

	dev = container_of(kobj, struct device, kobj);

	if (device_has_dsm(dev))
		return S_IRUGO;

	return 0;
}

static ssize_t
acpilabel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_handle handle;
	int length;

	handle = DEVICE_ACPI_HANDLE(dev);

	if (!handle)
		return -1;

	length = dsm_get_label(handle, DEVICE_LABEL_DSM,
			       &output, buf, ACPI_ATTR_LABEL_SHOW);

	if (length < 1)
		return -1;

	return length;
}

static ssize_t
acpiindex_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_handle handle;
	int length;

	handle = DEVICE_ACPI_HANDLE(dev);

	if (!handle)
		return -1;

	length = dsm_get_label(handle, DEVICE_LABEL_DSM,
			       &output, buf, ACPI_ATTR_INDEX_SHOW);

	if (length < 0)
		return -1;

	return length;

}

static struct device_attribute acpi_attr_label = {
	.attr = {.name = "label", .mode = 0444},
	.show = acpilabel_show,
};

static struct device_attribute acpi_attr_index = {
	.attr = {.name = "acpi_index", .mode = 0444},
	.show = acpiindex_show,
};

static struct attribute *acpi_attributes[] = {
	&acpi_attr_label.attr,
	&acpi_attr_index.attr,
	NULL,
};

static struct attribute_group acpi_attr_group = {
	.attrs = acpi_attributes,
	.is_visible = acpi_index_string_exist,
};

static int
pci_create_acpi_index_label_files(struct pci_dev *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &acpi_attr_group);
}

static int
pci_remove_acpi_index_label_files(struct pci_dev *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &acpi_attr_group);
	return 0;
}
#endif

void pci_create_firmware_label_files(struct pci_dev *pdev)
{
	if (device_has_dsm(&pdev->dev))
		pci_create_acpi_index_label_files(pdev);
	else
		pci_create_smbiosname_file(pdev);
}

void pci_remove_firmware_label_files(struct pci_dev *pdev)
{
	if (device_has_dsm(&pdev->dev))
		pci_remove_acpi_index_label_files(pdev);
	else
		pci_remove_smbiosname_file(pdev);
}
