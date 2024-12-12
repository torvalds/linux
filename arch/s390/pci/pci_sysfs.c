// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/pci.h>

#include "../../../drivers/pci/pci.h"

#include <asm/sclp.h>

#define zpci_attr(name, fmt, member)					\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));		\
									\
	return sysfs_emit(buf, fmt, zdev->member);				\
}									\
static DEVICE_ATTR_RO(name)

zpci_attr(function_id, "0x%08x\n", fid);
zpci_attr(function_handle, "0x%08x\n", fh);
zpci_attr(pchid, "0x%04x\n", pchid);
zpci_attr(pfgid, "0x%02x\n", pfgid);
zpci_attr(vfn, "0x%04x\n", vfn);
zpci_attr(pft, "0x%02x\n", pft);
zpci_attr(port, "%d\n", port);
zpci_attr(fidparm, "0x%02x\n", fidparm);
zpci_attr(uid, "0x%x\n", uid);
zpci_attr(segment0, "0x%02x\n", pfip[0]);
zpci_attr(segment1, "0x%02x\n", pfip[1]);
zpci_attr(segment2, "0x%02x\n", pfip[2]);
zpci_attr(segment3, "0x%02x\n", pfip[3]);

static ssize_t mio_enabled_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));

	return sysfs_emit(buf, zpci_use_mio(zdev) ? "1\n" : "0\n");
}
static DEVICE_ATTR_RO(mio_enabled);

static int _do_recover(struct pci_dev *pdev, struct zpci_dev *zdev)
{
	u8 status;
	int ret;

	pci_stop_and_remove_bus_device(pdev);
	if (zdev_enabled(zdev)) {
		ret = zpci_disable_device(zdev);
		/*
		 * Due to a z/VM vs LPAR inconsistency in the error
		 * state the FH may indicate an enabled device but
		 * disable says the device is already disabled don't
		 * treat it as an error here.
		 */
		if (ret == -EINVAL)
			ret = 0;
		if (ret)
			return ret;
	}

	ret = zpci_enable_device(zdev);
	if (ret)
		return ret;

	if (zdev->dma_table) {
		ret = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
					 virt_to_phys(zdev->dma_table), &status);
		if (ret)
			zpci_disable_device(zdev);
	}
	return ret;
}

static ssize_t recover_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct kernfs_node *kn;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = to_zpci(pdev);
	int ret = 0;

	/* Can't use device_remove_self() here as that would lead us to lock
	 * the pci_rescan_remove_lock while holding the device' kernfs lock.
	 * This would create a possible deadlock with disable_slot() which is
	 * not directly protected by the device' kernfs lock but takes it
	 * during the device removal which happens under
	 * pci_rescan_remove_lock.
	 *
	 * This is analogous to sdev_store_delete() in
	 * drivers/scsi/scsi_sysfs.c
	 */
	kn = sysfs_break_active_protection(&dev->kobj, &attr->attr);
	WARN_ON_ONCE(!kn);

	/* Device needs to be configured and state must not change */
	mutex_lock(&zdev->state_lock);
	if (zdev->state != ZPCI_FN_STATE_CONFIGURED)
		goto out;

	/* device_remove_file() serializes concurrent calls ignoring all but
	 * the first
	 */
	device_remove_file(dev, attr);

	/* A concurrent call to recover_store() may slip between
	 * sysfs_break_active_protection() and the sysfs file removal.
	 * Once it unblocks from pci_lock_rescan_remove() the original pdev
	 * will already be removed.
	 */
	pci_lock_rescan_remove();
	if (pci_dev_is_added(pdev)) {
		ret = _do_recover(pdev, zdev);
	}
	pci_rescan_bus(zdev->zbus->bus);
	pci_unlock_rescan_remove();

out:
	mutex_unlock(&zdev->state_lock);
	if (kn)
		sysfs_unbreak_active_protection(kn);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(recover);

static ssize_t util_string_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr, char *buf,
				loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = to_zpci(pdev);

	return memory_read_from_buffer(buf, count, &off, zdev->util_str,
				       sizeof(zdev->util_str));
}
static BIN_ATTR_RO(util_string, CLP_UTIL_STR_LEN);

static ssize_t report_error_write(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *attr, char *buf,
				  loff_t off, size_t count)
{
	struct zpci_report_error_header *report = (void *) buf;
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = to_zpci(pdev);
	int ret;

	if (off || (count < sizeof(*report)))
		return -EINVAL;

	ret = sclp_pci_report(report, zdev->fh, zdev->fid);

	return ret ? ret : count;
}
static BIN_ATTR(report_error, S_IWUSR, NULL, report_error_write, PAGE_SIZE);

static ssize_t uid_is_unique_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", zpci_unique_uid ? 1 : 0);
}
static DEVICE_ATTR_RO(uid_is_unique);

/* analogous to smbios index */
static ssize_t index_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	u32 index = ~0;

	if (zpci_unique_uid)
		index = zdev->uid;

	return sysfs_emit(buf, "%u\n", index);
}
static DEVICE_ATTR_RO(index);

static umode_t zpci_index_is_visible(struct kobject *kobj,
				     struct attribute *attr, int n)
{
	return zpci_unique_uid ? attr->mode : 0;
}

static struct attribute *zpci_ident_attrs[] = {
	&dev_attr_index.attr,
	NULL,
};

const struct attribute_group zpci_ident_attr_group = {
	.attrs = zpci_ident_attrs,
	.is_visible = zpci_index_is_visible,
};

static struct bin_attribute *zpci_bin_attrs[] = {
	&bin_attr_util_string,
	&bin_attr_report_error,
	NULL,
};

static struct attribute *zpci_dev_attrs[] = {
	&dev_attr_function_id.attr,
	&dev_attr_function_handle.attr,
	&dev_attr_pchid.attr,
	&dev_attr_pfgid.attr,
	&dev_attr_pft.attr,
	&dev_attr_port.attr,
	&dev_attr_fidparm.attr,
	&dev_attr_vfn.attr,
	&dev_attr_uid.attr,
	&dev_attr_recover.attr,
	&dev_attr_mio_enabled.attr,
	&dev_attr_uid_is_unique.attr,
	NULL,
};

const struct attribute_group zpci_attr_group = {
	.attrs = zpci_dev_attrs,
	.bin_attrs = zpci_bin_attrs,
};

static struct attribute *pfip_attrs[] = {
	&dev_attr_segment0.attr,
	&dev_attr_segment1.attr,
	&dev_attr_segment2.attr,
	&dev_attr_segment3.attr,
	NULL,
};

const struct attribute_group pfip_attr_group = {
	.name = "pfip",
	.attrs = pfip_attrs,
};
