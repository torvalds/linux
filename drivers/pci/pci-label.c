/*
 * Purpose: Export the firmware instance and label associated with
 * a pci device to sysfs
 * Copyright (C) 2010 Dell Inc.
 * by Narendra K <Narendra_K@dell.com>,
 * Jordan Hargrave <Jordan_Hargrave@dell.com>
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
#include "pci.h"

enum smbios_attr_enum {
	SMBIOS_ATTR_NONE = 0,
	SMBIOS_ATTR_LABEL_SHOW,
	SMBIOS_ATTR_INSTANCE_SHOW,
};

static mode_t
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

static mode_t
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
	if (!sysfs_create_group(&pdev->dev.kobj, &smbios_attr_group))
		return 0;
	return -ENODEV;
}

static void
pci_remove_smbiosname_file(struct pci_dev *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &smbios_attr_group);
}

void pci_create_firmware_label_files(struct pci_dev *pdev)
{
	if (!pci_create_smbiosname_file(pdev))
		;
}

void pci_remove_firmware_label_files(struct pci_dev *pdev)
{
	pci_remove_smbiosname_file(pdev);
}
