// SPDX-License-Identifier: GPL-2.0
/*
 * File:	vpd.c
 * Purpose:	Provide PCI VPD support
 *
 * Copyright (C) 2010 Broadcom Corporation.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/sched/signal.h>
#include "pci.h"

/* VPD access through PCI 2.2+ VPD capability */

/**
 * pci_read_vpd - Read one entry from Vital Product Data
 * @dev:	pci device struct
 * @pos:	offset in vpd space
 * @count:	number of bytes to read
 * @buf:	pointer to where to store result
 */
ssize_t pci_read_vpd(struct pci_dev *dev, loff_t pos, size_t count, void *buf)
{
	if (!dev->vpd || !dev->vpd->ops)
		return -ENODEV;
	return dev->vpd->ops->read(dev, pos, count, buf);
}
EXPORT_SYMBOL(pci_read_vpd);

/**
 * pci_write_vpd - Write entry to Vital Product Data
 * @dev:	pci device struct
 * @pos:	offset in vpd space
 * @count:	number of bytes to write
 * @buf:	buffer containing write data
 */
ssize_t pci_write_vpd(struct pci_dev *dev, loff_t pos, size_t count, const void *buf)
{
	if (!dev->vpd || !dev->vpd->ops)
		return -ENODEV;
	return dev->vpd->ops->write(dev, pos, count, buf);
}
EXPORT_SYMBOL(pci_write_vpd);

/**
 * pci_set_vpd_size - Set size of Vital Product Data space
 * @dev:	pci device struct
 * @len:	size of vpd space
 */
int pci_set_vpd_size(struct pci_dev *dev, size_t len)
{
	if (!dev->vpd || !dev->vpd->ops)
		return -ENODEV;
	return dev->vpd->ops->set_size(dev, len);
}
EXPORT_SYMBOL(pci_set_vpd_size);

#define PCI_VPD_MAX_SIZE (PCI_VPD_ADDR_MASK + 1)

/**
 * pci_vpd_size - determine actual size of Vital Product Data
 * @dev:	pci device struct
 * @old_size:	current assumed size, also maximum allowed size
 */
static size_t pci_vpd_size(struct pci_dev *dev, size_t old_size)
{
	size_t off = 0;
	unsigned char header[1+2];	/* 1 byte tag, 2 bytes length */

	while (off < old_size &&
	       pci_read_vpd(dev, off, 1, header) == 1) {
		unsigned char tag;

		if (header[0] & PCI_VPD_LRDT) {
			/* Large Resource Data Type Tag */
			tag = pci_vpd_lrdt_tag(header);
			/* Only read length from known tag items */
			if ((tag == PCI_VPD_LTIN_ID_STRING) ||
			    (tag == PCI_VPD_LTIN_RO_DATA) ||
			    (tag == PCI_VPD_LTIN_RW_DATA)) {
				if (pci_read_vpd(dev, off+1, 2,
						 &header[1]) != 2) {
					pci_warn(dev, "invalid large VPD tag %02x size at offset %zu",
						 tag, off + 1);
					return 0;
				}
				off += PCI_VPD_LRDT_TAG_SIZE +
					pci_vpd_lrdt_size(header);
			}
		} else {
			/* Short Resource Data Type Tag */
			off += PCI_VPD_SRDT_TAG_SIZE +
				pci_vpd_srdt_size(header);
			tag = pci_vpd_srdt_tag(header);
		}

		if (tag == PCI_VPD_STIN_END)	/* End tag descriptor */
			return off;

		if ((tag != PCI_VPD_LTIN_ID_STRING) &&
		    (tag != PCI_VPD_LTIN_RO_DATA) &&
		    (tag != PCI_VPD_LTIN_RW_DATA)) {
			pci_warn(dev, "invalid %s VPD tag %02x at offset %zu",
				 (header[0] & PCI_VPD_LRDT) ? "large" : "short",
				 tag, off);
			return 0;
		}
	}
	return 0;
}

/*
 * Wait for last operation to complete.
 * This code has to spin since there is no other notification from the PCI
 * hardware. Since the VPD is often implemented by serial attachment to an
 * EEPROM, it may take many milliseconds to complete.
 *
 * Returns 0 on success, negative values indicate error.
 */
static int pci_vpd_wait(struct pci_dev *dev)
{
	struct pci_vpd *vpd = dev->vpd;
	unsigned long timeout = jiffies + msecs_to_jiffies(125);
	unsigned long max_sleep = 16;
	u16 status;
	int ret;

	if (!vpd->busy)
		return 0;

	while (time_before(jiffies, timeout)) {
		ret = pci_user_read_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						&status);
		if (ret < 0)
			return ret;

		if ((status & PCI_VPD_ADDR_F) == vpd->flag) {
			vpd->busy = 0;
			return 0;
		}

		if (fatal_signal_pending(current))
			return -EINTR;

		usleep_range(10, max_sleep);
		if (max_sleep < 1024)
			max_sleep *= 2;
	}

	pci_warn(dev, "VPD access failed.  This is likely a firmware bug on this device.  Contact the card vendor for a firmware update\n");
	return -ETIMEDOUT;
}

static ssize_t pci_vpd_read(struct pci_dev *dev, loff_t pos, size_t count,
			    void *arg)
{
	struct pci_vpd *vpd = dev->vpd;
	int ret;
	loff_t end = pos + count;
	u8 *buf = arg;

	if (pos < 0)
		return -EINVAL;

	if (!vpd->valid) {
		vpd->valid = 1;
		vpd->len = pci_vpd_size(dev, vpd->len);
	}

	if (vpd->len == 0)
		return -EIO;

	if (pos > vpd->len)
		return 0;

	if (end > vpd->len) {
		end = vpd->len;
		count = end - pos;
	}

	if (mutex_lock_killable(&vpd->lock))
		return -EINTR;

	ret = pci_vpd_wait(dev);
	if (ret < 0)
		goto out;

	while (pos < end) {
		u32 val;
		unsigned int i, skip;

		ret = pci_user_write_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						 pos & ~3);
		if (ret < 0)
			break;
		vpd->busy = 1;
		vpd->flag = PCI_VPD_ADDR_F;
		ret = pci_vpd_wait(dev);
		if (ret < 0)
			break;

		ret = pci_user_read_config_dword(dev, vpd->cap + PCI_VPD_DATA, &val);
		if (ret < 0)
			break;

		skip = pos & 3;
		for (i = 0;  i < sizeof(u32); i++) {
			if (i >= skip) {
				*buf++ = val;
				if (++pos == end)
					break;
			}
			val >>= 8;
		}
	}
out:
	mutex_unlock(&vpd->lock);
	return ret ? ret : count;
}

static ssize_t pci_vpd_write(struct pci_dev *dev, loff_t pos, size_t count,
			     const void *arg)
{
	struct pci_vpd *vpd = dev->vpd;
	const u8 *buf = arg;
	loff_t end = pos + count;
	int ret = 0;

	if (pos < 0 || (pos & 3) || (count & 3))
		return -EINVAL;

	if (!vpd->valid) {
		vpd->valid = 1;
		vpd->len = pci_vpd_size(dev, vpd->len);
	}

	if (vpd->len == 0)
		return -EIO;

	if (end > vpd->len)
		return -EINVAL;

	if (mutex_lock_killable(&vpd->lock))
		return -EINTR;

	ret = pci_vpd_wait(dev);
	if (ret < 0)
		goto out;

	while (pos < end) {
		u32 val;

		val = *buf++;
		val |= *buf++ << 8;
		val |= *buf++ << 16;
		val |= *buf++ << 24;

		ret = pci_user_write_config_dword(dev, vpd->cap + PCI_VPD_DATA, val);
		if (ret < 0)
			break;
		ret = pci_user_write_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						 pos | PCI_VPD_ADDR_F);
		if (ret < 0)
			break;

		vpd->busy = 1;
		vpd->flag = 0;
		ret = pci_vpd_wait(dev);
		if (ret < 0)
			break;

		pos += sizeof(u32);
	}
out:
	mutex_unlock(&vpd->lock);
	return ret ? ret : count;
}

static int pci_vpd_set_size(struct pci_dev *dev, size_t len)
{
	struct pci_vpd *vpd = dev->vpd;

	if (len == 0 || len > PCI_VPD_MAX_SIZE)
		return -EIO;

	vpd->valid = 1;
	vpd->len = len;

	return 0;
}

static const struct pci_vpd_ops pci_vpd_ops = {
	.read = pci_vpd_read,
	.write = pci_vpd_write,
	.set_size = pci_vpd_set_size,
};

static ssize_t pci_vpd_f0_read(struct pci_dev *dev, loff_t pos, size_t count,
			       void *arg)
{
	struct pci_dev *tdev = pci_get_slot(dev->bus,
					    PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	ssize_t ret;

	if (!tdev)
		return -ENODEV;

	ret = pci_read_vpd(tdev, pos, count, arg);
	pci_dev_put(tdev);
	return ret;
}

static ssize_t pci_vpd_f0_write(struct pci_dev *dev, loff_t pos, size_t count,
				const void *arg)
{
	struct pci_dev *tdev = pci_get_slot(dev->bus,
					    PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	ssize_t ret;

	if (!tdev)
		return -ENODEV;

	ret = pci_write_vpd(tdev, pos, count, arg);
	pci_dev_put(tdev);
	return ret;
}

static int pci_vpd_f0_set_size(struct pci_dev *dev, size_t len)
{
	struct pci_dev *tdev = pci_get_slot(dev->bus,
					    PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	int ret;

	if (!tdev)
		return -ENODEV;

	ret = pci_set_vpd_size(tdev, len);
	pci_dev_put(tdev);
	return ret;
}

static const struct pci_vpd_ops pci_vpd_f0_ops = {
	.read = pci_vpd_f0_read,
	.write = pci_vpd_f0_write,
	.set_size = pci_vpd_f0_set_size,
};

int pci_vpd_init(struct pci_dev *dev)
{
	struct pci_vpd *vpd;
	u8 cap;

	cap = pci_find_capability(dev, PCI_CAP_ID_VPD);
	if (!cap)
		return -ENODEV;

	vpd = kzalloc(sizeof(*vpd), GFP_ATOMIC);
	if (!vpd)
		return -ENOMEM;

	vpd->len = PCI_VPD_MAX_SIZE;
	if (dev->dev_flags & PCI_DEV_FLAGS_VPD_REF_F0)
		vpd->ops = &pci_vpd_f0_ops;
	else
		vpd->ops = &pci_vpd_ops;
	mutex_init(&vpd->lock);
	vpd->cap = cap;
	vpd->busy = 0;
	vpd->valid = 0;
	dev->vpd = vpd;
	return 0;
}

void pci_vpd_release(struct pci_dev *dev)
{
	kfree(dev->vpd);
}

static ssize_t read_vpd_attr(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(kobj_to_dev(kobj));

	if (bin_attr->size > 0) {
		if (off > bin_attr->size)
			count = 0;
		else if (count > bin_attr->size - off)
			count = bin_attr->size - off;
	}

	return pci_read_vpd(dev, off, count, buf);
}

static ssize_t write_vpd_attr(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr, char *buf,
			      loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(kobj_to_dev(kobj));

	if (bin_attr->size > 0) {
		if (off > bin_attr->size)
			count = 0;
		else if (count > bin_attr->size - off)
			count = bin_attr->size - off;
	}

	return pci_write_vpd(dev, off, count, buf);
}

void pcie_vpd_create_sysfs_dev_files(struct pci_dev *dev)
{
	int retval;
	struct bin_attribute *attr;

	if (!dev->vpd)
		return;

	attr = kzalloc(sizeof(*attr), GFP_ATOMIC);
	if (!attr)
		return;

	sysfs_bin_attr_init(attr);
	attr->size = 0;
	attr->attr.name = "vpd";
	attr->attr.mode = S_IRUSR | S_IWUSR;
	attr->read = read_vpd_attr;
	attr->write = write_vpd_attr;
	retval = sysfs_create_bin_file(&dev->dev.kobj, attr);
	if (retval) {
		kfree(attr);
		return;
	}

	dev->vpd->attr = attr;
}

void pcie_vpd_remove_sysfs_dev_files(struct pci_dev *dev)
{
	if (dev->vpd && dev->vpd->attr) {
		sysfs_remove_bin_file(&dev->dev.kobj, dev->vpd->attr);
		kfree(dev->vpd->attr);
	}
}

int pci_vpd_find_tag(const u8 *buf, unsigned int off, unsigned int len, u8 rdt)
{
	int i;

	for (i = off; i < len; ) {
		u8 val = buf[i];

		if (val & PCI_VPD_LRDT) {
			/* Don't return success of the tag isn't complete */
			if (i + PCI_VPD_LRDT_TAG_SIZE > len)
				break;

			if (val == rdt)
				return i;

			i += PCI_VPD_LRDT_TAG_SIZE +
			     pci_vpd_lrdt_size(&buf[i]);
		} else {
			u8 tag = val & ~PCI_VPD_SRDT_LEN_MASK;

			if (tag == rdt)
				return i;

			if (tag == PCI_VPD_SRDT_END)
				break;

			i += PCI_VPD_SRDT_TAG_SIZE +
			     pci_vpd_srdt_size(&buf[i]);
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(pci_vpd_find_tag);

int pci_vpd_find_info_keyword(const u8 *buf, unsigned int off,
			      unsigned int len, const char *kw)
{
	int i;

	for (i = off; i + PCI_VPD_INFO_FLD_HDR_SIZE <= off + len;) {
		if (buf[i + 0] == kw[0] &&
		    buf[i + 1] == kw[1])
			return i;

		i += PCI_VPD_INFO_FLD_HDR_SIZE +
		     pci_vpd_info_field_size(&buf[i]);
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(pci_vpd_find_info_keyword);
