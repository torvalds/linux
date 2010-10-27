/*
 * linux/drivers/firmware/edd.c
 *  Copyright (C) 2002, 2003, 2004 Dell Inc.
 *  by Matt Domsch <Matt_Domsch@dell.com>
 *  disk signature by Matt Domsch, Andrew Wilks, and Sandeep K. Shandilya
 *  legacy CHS by Patrick J. LoPresti <patl@users.sourceforge.net>
 *
 * BIOS Enhanced Disk Drive Services (EDD)
 * conformant to T13 Committee www.t13.org
 *   projects 1572D, 1484D, 1386D, 1226DT
 *
 * This code takes information provided by BIOS EDD calls
 * fn41 - Check Extensions Present and
 * fn48 - Get Device Parameters with EDD extensions
 * made in setup.S, copied to safe structures in setup.c,
 * and presents it in sysfs.
 *
 * Please see http://linux.dell.com/edd30/results.html for
 * the list of BIOSs which have been reported to implement EDD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/limits.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/edd.h>

#define EDD_VERSION "0.16"
#define EDD_DATE    "2004-Jun-25"

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@Dell.com>");
MODULE_DESCRIPTION("sysfs interface to BIOS EDD information");
MODULE_LICENSE("GPL");
MODULE_VERSION(EDD_VERSION);

#define left (PAGE_SIZE - (p - buf) - 1)

struct edd_device {
	unsigned int index;
	unsigned int mbr_signature;
	struct edd_info *info;
	struct kobject kobj;
};

struct edd_attribute {
	struct attribute attr;
	ssize_t(*show) (struct edd_device * edev, char *buf);
	int (*test) (struct edd_device * edev);
};

/* forward declarations */
static int edd_dev_is_type(struct edd_device *edev, const char *type);
static struct pci_dev *edd_get_pci_dev(struct edd_device *edev);

static struct edd_device *edd_devices[EDD_MBR_SIG_MAX];

#define EDD_DEVICE_ATTR(_name,_mode,_show,_test) \
struct edd_attribute edd_attr_##_name = { 	\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,				\
	.test	= _test,				\
};

static int
edd_has_mbr_signature(struct edd_device *edev)
{
	return edev->index < min_t(unsigned char, edd.mbr_signature_nr, EDD_MBR_SIG_MAX);
}

static int
edd_has_edd_info(struct edd_device *edev)
{
	return edev->index < min_t(unsigned char, edd.edd_info_nr, EDDMAXNR);
}

static inline struct edd_info *
edd_dev_get_info(struct edd_device *edev)
{
	return edev->info;
}

static inline void
edd_dev_set_info(struct edd_device *edev, int i)
{
	edev->index = i;
	if (edd_has_mbr_signature(edev))
		edev->mbr_signature = edd.mbr_signature[i];
	if (edd_has_edd_info(edev))
		edev->info = &edd.edd_info[i];
}

#define to_edd_attr(_attr) container_of(_attr,struct edd_attribute,attr)
#define to_edd_device(obj) container_of(obj,struct edd_device,kobj)

static ssize_t
edd_attr_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct edd_device *dev = to_edd_device(kobj);
	struct edd_attribute *edd_attr = to_edd_attr(attr);
	ssize_t ret = -EIO;

	if (edd_attr->show)
		ret = edd_attr->show(dev, buf);
	return ret;
}

static const struct sysfs_ops edd_attr_ops = {
	.show = edd_attr_show,
};

static ssize_t
edd_show_host_bus(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	int i;

	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	for (i = 0; i < 4; i++) {
		if (isprint(info->params.host_bus_type[i])) {
			p += scnprintf(p, left, "%c", info->params.host_bus_type[i]);
		} else {
			p += scnprintf(p, left, " ");
		}
	}

	if (!strncmp(info->params.host_bus_type, "ISA", 3)) {
		p += scnprintf(p, left, "\tbase_address: %x\n",
			     info->params.interface_path.isa.base_address);
	} else if (!strncmp(info->params.host_bus_type, "PCIX", 4) ||
		   !strncmp(info->params.host_bus_type, "PCI", 3)) {
		p += scnprintf(p, left,
			     "\t%02x:%02x.%d  channel: %u\n",
			     info->params.interface_path.pci.bus,
			     info->params.interface_path.pci.slot,
			     info->params.interface_path.pci.function,
			     info->params.interface_path.pci.channel);
	} else if (!strncmp(info->params.host_bus_type, "IBND", 4) ||
		   !strncmp(info->params.host_bus_type, "XPRS", 4) ||
		   !strncmp(info->params.host_bus_type, "HTPT", 4)) {
		p += scnprintf(p, left,
			     "\tTBD: %llx\n",
			     info->params.interface_path.ibnd.reserved);

	} else {
		p += scnprintf(p, left, "\tunknown: %llx\n",
			     info->params.interface_path.unknown.reserved);
	}
	return (p - buf);
}

static ssize_t
edd_show_interface(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	int i;

	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	for (i = 0; i < 8; i++) {
		if (isprint(info->params.interface_type[i])) {
			p += scnprintf(p, left, "%c", info->params.interface_type[i]);
		} else {
			p += scnprintf(p, left, " ");
		}
	}
	if (!strncmp(info->params.interface_type, "ATAPI", 5)) {
		p += scnprintf(p, left, "\tdevice: %u  lun: %u\n",
			     info->params.device_path.atapi.device,
			     info->params.device_path.atapi.lun);
	} else if (!strncmp(info->params.interface_type, "ATA", 3)) {
		p += scnprintf(p, left, "\tdevice: %u\n",
			     info->params.device_path.ata.device);
	} else if (!strncmp(info->params.interface_type, "SCSI", 4)) {
		p += scnprintf(p, left, "\tid: %u  lun: %llu\n",
			     info->params.device_path.scsi.id,
			     info->params.device_path.scsi.lun);
	} else if (!strncmp(info->params.interface_type, "USB", 3)) {
		p += scnprintf(p, left, "\tserial_number: %llx\n",
			     info->params.device_path.usb.serial_number);
	} else if (!strncmp(info->params.interface_type, "1394", 4)) {
		p += scnprintf(p, left, "\teui: %llx\n",
			     info->params.device_path.i1394.eui);
	} else if (!strncmp(info->params.interface_type, "FIBRE", 5)) {
		p += scnprintf(p, left, "\twwid: %llx lun: %llx\n",
			     info->params.device_path.fibre.wwid,
			     info->params.device_path.fibre.lun);
	} else if (!strncmp(info->params.interface_type, "I2O", 3)) {
		p += scnprintf(p, left, "\tidentity_tag: %llx\n",
			     info->params.device_path.i2o.identity_tag);
	} else if (!strncmp(info->params.interface_type, "RAID", 4)) {
		p += scnprintf(p, left, "\tidentity_tag: %x\n",
			     info->params.device_path.raid.array_number);
	} else if (!strncmp(info->params.interface_type, "SATA", 4)) {
		p += scnprintf(p, left, "\tdevice: %u\n",
			     info->params.device_path.sata.device);
	} else {
		p += scnprintf(p, left, "\tunknown: %llx %llx\n",
			     info->params.device_path.unknown.reserved1,
			     info->params.device_path.unknown.reserved2);
	}

	return (p - buf);
}

/**
 * edd_show_raw_data() - copies raw data to buffer for userspace to parse
 * @edev: target edd_device
 * @buf: output buffer
 *
 * Returns: number of bytes written, or -EINVAL on failure
 */
static ssize_t
edd_show_raw_data(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	ssize_t len = sizeof (info->params);
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	if (!(info->params.key == 0xBEDD || info->params.key == 0xDDBE))
		len = info->params.length;

	/* In case of buggy BIOSs */
	if (len > (sizeof(info->params)))
		len = sizeof(info->params);

	memcpy(buf, &info->params, len);
	return len;
}

static ssize_t
edd_show_version(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += scnprintf(p, left, "0x%02x\n", info->version);
	return (p - buf);
}

static ssize_t
edd_show_mbr_signature(struct edd_device *edev, char *buf)
{
	char *p = buf;
	p += scnprintf(p, left, "0x%08x\n", edev->mbr_signature);
	return (p - buf);
}

static ssize_t
edd_show_extensions(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	if (info->interface_support & EDD_EXT_FIXED_DISK_ACCESS) {
		p += scnprintf(p, left, "Fixed disk access\n");
	}
	if (info->interface_support & EDD_EXT_DEVICE_LOCKING_AND_EJECTING) {
		p += scnprintf(p, left, "Device locking and ejecting\n");
	}
	if (info->interface_support & EDD_EXT_ENHANCED_DISK_DRIVE_SUPPORT) {
		p += scnprintf(p, left, "Enhanced Disk Drive support\n");
	}
	if (info->interface_support & EDD_EXT_64BIT_EXTENSIONS) {
		p += scnprintf(p, left, "64-bit extensions\n");
	}
	return (p - buf);
}

static ssize_t
edd_show_info_flags(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	if (info->params.info_flags & EDD_INFO_DMA_BOUNDARY_ERROR_TRANSPARENT)
		p += scnprintf(p, left, "DMA boundary error transparent\n");
	if (info->params.info_flags & EDD_INFO_GEOMETRY_VALID)
		p += scnprintf(p, left, "geometry valid\n");
	if (info->params.info_flags & EDD_INFO_REMOVABLE)
		p += scnprintf(p, left, "removable\n");
	if (info->params.info_flags & EDD_INFO_WRITE_VERIFY)
		p += scnprintf(p, left, "write verify\n");
	if (info->params.info_flags & EDD_INFO_MEDIA_CHANGE_NOTIFICATION)
		p += scnprintf(p, left, "media change notification\n");
	if (info->params.info_flags & EDD_INFO_LOCKABLE)
		p += scnprintf(p, left, "lockable\n");
	if (info->params.info_flags & EDD_INFO_NO_MEDIA_PRESENT)
		p += scnprintf(p, left, "no media present\n");
	if (info->params.info_flags & EDD_INFO_USE_INT13_FN50)
		p += scnprintf(p, left, "use int13 fn50\n");
	return (p - buf);
}

static ssize_t
edd_show_legacy_max_cylinder(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += snprintf(p, left, "%u\n", info->legacy_max_cylinder);
	return (p - buf);
}

static ssize_t
edd_show_legacy_max_head(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += snprintf(p, left, "%u\n", info->legacy_max_head);
	return (p - buf);
}

static ssize_t
edd_show_legacy_sectors_per_track(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += snprintf(p, left, "%u\n", info->legacy_sectors_per_track);
	return (p - buf);
}

static ssize_t
edd_show_default_cylinders(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += scnprintf(p, left, "%u\n", info->params.num_default_cylinders);
	return (p - buf);
}

static ssize_t
edd_show_default_heads(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += scnprintf(p, left, "%u\n", info->params.num_default_heads);
	return (p - buf);
}

static ssize_t
edd_show_default_sectors_per_track(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += scnprintf(p, left, "%u\n", info->params.sectors_per_track);
	return (p - buf);
}

static ssize_t
edd_show_sectors(struct edd_device *edev, char *buf)
{
	struct edd_info *info;
	char *p = buf;
	if (!edev)
		return -EINVAL;
	info = edd_dev_get_info(edev);
	if (!info || !buf)
		return -EINVAL;

	p += scnprintf(p, left, "%llu\n", info->params.number_of_sectors);
	return (p - buf);
}


/*
 * Some device instances may not have all the above attributes,
 * or the attribute values may be meaningless (i.e. if
 * the device is < EDD 3.0, it won't have host_bus and interface
 * information), so don't bother making files for them.  Likewise
 * if the default_{cylinders,heads,sectors_per_track} values
 * are zero, the BIOS doesn't provide sane values, don't bother
 * creating files for them either.
 */

static int
edd_has_legacy_max_cylinder(struct edd_device *edev)
{
	struct edd_info *info;
	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);
	if (!info)
		return 0;
	return info->legacy_max_cylinder > 0;
}

static int
edd_has_legacy_max_head(struct edd_device *edev)
{
	struct edd_info *info;
	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);
	if (!info)
		return 0;
	return info->legacy_max_head > 0;
}

static int
edd_has_legacy_sectors_per_track(struct edd_device *edev)
{
	struct edd_info *info;
	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);
	if (!info)
		return 0;
	return info->legacy_sectors_per_track > 0;
}

static int
edd_has_default_cylinders(struct edd_device *edev)
{
	struct edd_info *info;
	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);
	if (!info)
		return 0;
	return info->params.num_default_cylinders > 0;
}

static int
edd_has_default_heads(struct edd_device *edev)
{
	struct edd_info *info;
	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);
	if (!info)
		return 0;
	return info->params.num_default_heads > 0;
}

static int
edd_has_default_sectors_per_track(struct edd_device *edev)
{
	struct edd_info *info;
	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);
	if (!info)
		return 0;
	return info->params.sectors_per_track > 0;
}

static int
edd_has_edd30(struct edd_device *edev)
{
	struct edd_info *info;
	int i, nonzero_path = 0;
	char c;

	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);
	if (!info)
		return 0;

	if (!(info->params.key == 0xBEDD || info->params.key == 0xDDBE)) {
		return 0;
	}

	for (i = 30; i <= 73; i++) {
		c = *(((uint8_t *) info) + i + 4);
		if (c) {
			nonzero_path++;
			break;
		}
	}
	if (!nonzero_path) {
		return 0;
	}

	return 1;
}


static EDD_DEVICE_ATTR(raw_data, 0444, edd_show_raw_data, edd_has_edd_info);
static EDD_DEVICE_ATTR(version, 0444, edd_show_version, edd_has_edd_info);
static EDD_DEVICE_ATTR(extensions, 0444, edd_show_extensions, edd_has_edd_info);
static EDD_DEVICE_ATTR(info_flags, 0444, edd_show_info_flags, edd_has_edd_info);
static EDD_DEVICE_ATTR(sectors, 0444, edd_show_sectors, edd_has_edd_info);
static EDD_DEVICE_ATTR(legacy_max_cylinder, 0444,
                       edd_show_legacy_max_cylinder,
		       edd_has_legacy_max_cylinder);
static EDD_DEVICE_ATTR(legacy_max_head, 0444, edd_show_legacy_max_head,
		       edd_has_legacy_max_head);
static EDD_DEVICE_ATTR(legacy_sectors_per_track, 0444,
                       edd_show_legacy_sectors_per_track,
		       edd_has_legacy_sectors_per_track);
static EDD_DEVICE_ATTR(default_cylinders, 0444, edd_show_default_cylinders,
		       edd_has_default_cylinders);
static EDD_DEVICE_ATTR(default_heads, 0444, edd_show_default_heads,
		       edd_has_default_heads);
static EDD_DEVICE_ATTR(default_sectors_per_track, 0444,
		       edd_show_default_sectors_per_track,
		       edd_has_default_sectors_per_track);
static EDD_DEVICE_ATTR(interface, 0444, edd_show_interface, edd_has_edd30);
static EDD_DEVICE_ATTR(host_bus, 0444, edd_show_host_bus, edd_has_edd30);
static EDD_DEVICE_ATTR(mbr_signature, 0444, edd_show_mbr_signature, edd_has_mbr_signature);


/* These are default attributes that are added for every edd
 * device discovered.  There are none.
 */
static struct attribute * def_attrs[] = {
	NULL,
};

/* These attributes are conditional and only added for some devices. */
static struct edd_attribute * edd_attrs[] = {
	&edd_attr_raw_data,
	&edd_attr_version,
	&edd_attr_extensions,
	&edd_attr_info_flags,
	&edd_attr_sectors,
	&edd_attr_legacy_max_cylinder,
	&edd_attr_legacy_max_head,
	&edd_attr_legacy_sectors_per_track,
	&edd_attr_default_cylinders,
	&edd_attr_default_heads,
	&edd_attr_default_sectors_per_track,
	&edd_attr_interface,
	&edd_attr_host_bus,
	&edd_attr_mbr_signature,
	NULL,
};

/**
 *	edd_release - free edd structure
 *	@kobj:	kobject of edd structure
 *
 *	This is called when the refcount of the edd structure
 *	reaches 0. This should happen right after we unregister,
 *	but just in case, we use the release callback anyway.
 */

static void edd_release(struct kobject * kobj)
{
	struct edd_device * dev = to_edd_device(kobj);
	kfree(dev);
}

static struct kobj_type edd_ktype = {
	.release	= edd_release,
	.sysfs_ops	= &edd_attr_ops,
	.default_attrs	= def_attrs,
};

static struct kset *edd_kset;


/**
 * edd_dev_is_type() - is this EDD device a 'type' device?
 * @edev: target edd_device
 * @type: a host bus or interface identifier string per the EDD spec
 *
 * Returns 1 (TRUE) if it is a 'type' device, 0 otherwise.
 */
static int
edd_dev_is_type(struct edd_device *edev, const char *type)
{
	struct edd_info *info;
	if (!edev)
		return 0;
	info = edd_dev_get_info(edev);

	if (type && info) {
		if (!strncmp(info->params.host_bus_type, type, strlen(type)) ||
		    !strncmp(info->params.interface_type, type, strlen(type)))
			return 1;
	}
	return 0;
}

/**
 * edd_get_pci_dev() - finds pci_dev that matches edev
 * @edev: edd_device
 *
 * Returns pci_dev if found, or NULL
 */
static struct pci_dev *
edd_get_pci_dev(struct edd_device *edev)
{
	struct edd_info *info = edd_dev_get_info(edev);

	if (edd_dev_is_type(edev, "PCI")) {
		return pci_get_bus_and_slot(info->params.interface_path.pci.bus,
				     PCI_DEVFN(info->params.interface_path.pci.slot,
					       info->params.interface_path.pci.
					       function));
	}
	return NULL;
}

static int
edd_create_symlink_to_pcidev(struct edd_device *edev)
{

	struct pci_dev *pci_dev = edd_get_pci_dev(edev);
	int ret;
	if (!pci_dev)
		return 1;
	ret = sysfs_create_link(&edev->kobj,&pci_dev->dev.kobj,"pci_dev");
	pci_dev_put(pci_dev);
	return ret;
}

static inline void
edd_device_unregister(struct edd_device *edev)
{
	kobject_put(&edev->kobj);
}

static void edd_populate_dir(struct edd_device * edev)
{
	struct edd_attribute * attr;
	int error = 0;
	int i;

	for (i = 0; (attr = edd_attrs[i]) && !error; i++) {
		if (!attr->test ||
		    (attr->test && attr->test(edev)))
			error = sysfs_create_file(&edev->kobj,&attr->attr);
	}

	if (!error) {
		edd_create_symlink_to_pcidev(edev);
	}
}

static int
edd_device_register(struct edd_device *edev, int i)
{
	int error;

	if (!edev)
		return 1;
	edd_dev_set_info(edev, i);
	edev->kobj.kset = edd_kset;
	error = kobject_init_and_add(&edev->kobj, &edd_ktype, NULL,
				     "int13_dev%02x", 0x80 + i);
	if (!error) {
		edd_populate_dir(edev);
		kobject_uevent(&edev->kobj, KOBJ_ADD);
	}
	return error;
}

static inline int edd_num_devices(void)
{
	return max_t(unsigned char,
		     min_t(unsigned char, EDD_MBR_SIG_MAX, edd.mbr_signature_nr),
		     min_t(unsigned char, EDDMAXNR, edd.edd_info_nr));
}

/**
 * edd_init() - creates sysfs tree of EDD data
 */
static int __init
edd_init(void)
{
	int i;
	int rc=0;
	struct edd_device *edev;

	printk(KERN_INFO "BIOS EDD facility v%s %s, %d devices found\n",
	       EDD_VERSION, EDD_DATE, edd_num_devices());

	if (!edd_num_devices()) {
		printk(KERN_INFO "EDD information not available.\n");
		return -ENODEV;
	}

	edd_kset = kset_create_and_add("edd", NULL, firmware_kobj);
	if (!edd_kset)
		return -ENOMEM;

	for (i = 0; i < edd_num_devices(); i++) {
		edev = kzalloc(sizeof (*edev), GFP_KERNEL);
		if (!edev) {
			rc = -ENOMEM;
			goto out;
		}

		rc = edd_device_register(edev, i);
		if (rc) {
			kfree(edev);
			goto out;
		}
		edd_devices[i] = edev;
	}

	return 0;

out:
	while (--i >= 0)
		edd_device_unregister(edd_devices[i]);
	kset_unregister(edd_kset);
	return rc;
}

static void __exit
edd_exit(void)
{
	int i;
	struct edd_device *edev;

	for (i = 0; i < edd_num_devices(); i++) {
		if ((edev = edd_devices[i]))
			edd_device_unregister(edev);
	}
	kset_unregister(edd_kset);
}

late_initcall(edd_init);
module_exit(edd_exit);
