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
#include "pci.h"

#define	DEVICE_LABEL_DSM	0x07

#ifdef CONFIG_DMI
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
#else
static inline int
pci_create_smbiosname_file(struct pci_dev *pdev)
{
	return -1;
}

static inline void
pci_remove_smbiosname_file(struct pci_dev *pdev)
{
}
#endif

#ifdef CONFIG_ACPI
static const char device_label_dsm_uuid[] = {
	0xD0, 0x37, 0xC9, 0xE5, 0x53, 0x35, 0x7A, 0x4D,
	0x91, 0x17, 0xEA, 0x4D, 0x19, 0xC3, 0x43, 0x4D
};

enum acpi_attr_enum {
	ACPI_ATTR_LABEL_SHOW,
	ACPI_ATTR_INDEX_SHOW,
};

static void dsm_label_utf16s_to_utf8s(union acpi_object *obj, char *buf)
{
	int len;
	len = utf16s_to_utf8s((const wchar_t *)obj->string.pointer,
			      obj->string.length,
			      UTF16_LITTLE_ENDIAN,
			      buf, PAGE_SIZE);
	buf[len] = '\n';
}

static int
dsm_get_label(struct device *dev, char *buf, enum acpi_attr_enum attr)
{
	acpi_handle handle;
	union acpi_object *obj, *tmp;
	int len = -1;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -1;

	obj = acpi_evaluate_dsm(handle, device_label_dsm_uuid, 0x2,
				DEVICE_LABEL_DSM, NULL);
	if (!obj)
		return -1;

	tmp = obj->package.elements;
	if (obj->type == ACPI_TYPE_PACKAGE && obj->package.count == 2 &&
	    tmp[0].type == ACPI_TYPE_INTEGER &&
	    tmp[1].type == ACPI_TYPE_STRING) {
		/*
		 * The second string element is optional even when
		 * this _DSM is implemented; when not implemented,
		 * this entry must return a null string.
		 */
		if (attr == ACPI_ATTR_INDEX_SHOW)
			scnprintf(buf, PAGE_SIZE, "%llu\n", tmp->integer.value);
		else if (attr == ACPI_ATTR_LABEL_SHOW)
			dsm_label_utf16s_to_utf8s(tmp + 1, buf);
		len = strlen(buf) > 0 ? strlen(buf) : -1;
	}

	ACPI_FREE(obj);

	return len;
}

static bool
device_has_dsm(struct device *dev)
{
	acpi_handle handle;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return false;

	return !!acpi_check_dsm(handle, device_label_dsm_uuid, 0x2,
				1 << DEVICE_LABEL_DSM);
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
	return dsm_get_label(dev, buf, ACPI_ATTR_LABEL_SHOW);
}

static ssize_t
acpiindex_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return dsm_get_label(dev, buf, ACPI_ATTR_INDEX_SHOW);
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
#else
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
