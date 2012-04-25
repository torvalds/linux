/*
 * Core registration and callback routines for MTD
 * drivers and users.
 *
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org>
 * Copyright © 2006      Red Hat UK Limited 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/idr.h>
#include <linux/backing-dev.h>
#include <linux/gfp.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include "mtdcore.h"
/*
 * backing device capabilities for non-mappable devices (such as NAND flash)
 * - permits private mappings, copies are taken of the data
 */
static struct backing_dev_info mtd_bdi_unmappable = {
	.capabilities	= BDI_CAP_MAP_COPY,
};

/*
 * backing device capabilities for R/O mappable devices (such as ROM)
 * - permits private mappings, copies are taken of the data
 * - permits non-writable shared mappings
 */
static struct backing_dev_info mtd_bdi_ro_mappable = {
	.capabilities	= (BDI_CAP_MAP_COPY | BDI_CAP_MAP_DIRECT |
			   BDI_CAP_EXEC_MAP | BDI_CAP_READ_MAP),
};

/*
 * backing device capabilities for writable mappable devices (such as RAM)
 * - permits private mappings, copies are taken of the data
 * - permits non-writable shared mappings
 */
static struct backing_dev_info mtd_bdi_rw_mappable = {
	.capabilities	= (BDI_CAP_MAP_COPY | BDI_CAP_MAP_DIRECT |
			   BDI_CAP_EXEC_MAP | BDI_CAP_READ_MAP |
			   BDI_CAP_WRITE_MAP),
};

static int mtd_cls_suspend(struct device *dev, pm_message_t state);
static int mtd_cls_resume(struct device *dev);

static struct class mtd_class = {
	.name = "mtd",
	.owner = THIS_MODULE,
	.suspend = mtd_cls_suspend,
	.resume = mtd_cls_resume,
};

static DEFINE_IDR(mtd_idr);

/* These are exported solely for the purpose of mtd_blkdevs.c. You
   should not use them for _anything_ else */
DEFINE_MUTEX(mtd_table_mutex);
EXPORT_SYMBOL_GPL(mtd_table_mutex);

struct mtd_info *__mtd_next_device(int i)
{
	return idr_get_next(&mtd_idr, &i);
}
EXPORT_SYMBOL_GPL(__mtd_next_device);

static LIST_HEAD(mtd_notifiers);


#if defined(CONFIG_MTD_CHAR) || defined(CONFIG_MTD_CHAR_MODULE)
#define MTD_DEVT(index) MKDEV(MTD_CHAR_MAJOR, (index)*2)
#else
#define MTD_DEVT(index) 0
#endif

/* REVISIT once MTD uses the driver model better, whoever allocates
 * the mtd_info will probably want to use the release() hook...
 */
static void mtd_release(struct device *dev)
{
	struct mtd_info __maybe_unused *mtd = dev_get_drvdata(dev);
	dev_t index = MTD_DEVT(mtd->index);

	/* remove /dev/mtdXro node if needed */
	if (index)
		device_destroy(&mtd_class, index + 1);
}

static int mtd_cls_suspend(struct device *dev, pm_message_t state)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return mtd ? mtd_suspend(mtd) : 0;
}

static int mtd_cls_resume(struct device *dev)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	if (mtd)
		mtd_resume(mtd);
	return 0;
}

static ssize_t mtd_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	char *type;

	switch (mtd->type) {
	case MTD_ABSENT:
		type = "absent";
		break;
	case MTD_RAM:
		type = "ram";
		break;
	case MTD_ROM:
		type = "rom";
		break;
	case MTD_NORFLASH:
		type = "nor";
		break;
	case MTD_NANDFLASH:
		type = "nand";
		break;
	case MTD_DATAFLASH:
		type = "dataflash";
		break;
	case MTD_UBIVOLUME:
		type = "ubi";
		break;
	default:
		type = "unknown";
	}

	return snprintf(buf, PAGE_SIZE, "%s\n", type);
}
static DEVICE_ATTR(type, S_IRUGO, mtd_type_show, NULL);

static ssize_t mtd_flags_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%lx\n", (unsigned long)mtd->flags);

}
static DEVICE_ATTR(flags, S_IRUGO, mtd_flags_show, NULL);

static ssize_t mtd_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)mtd->size);

}
static DEVICE_ATTR(size, S_IRUGO, mtd_size_show, NULL);

static ssize_t mtd_erasesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%lu\n", (unsigned long)mtd->erasesize);

}
static DEVICE_ATTR(erasesize, S_IRUGO, mtd_erasesize_show, NULL);

static ssize_t mtd_writesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%lu\n", (unsigned long)mtd->writesize);

}
static DEVICE_ATTR(writesize, S_IRUGO, mtd_writesize_show, NULL);

static ssize_t mtd_subpagesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	unsigned int subpagesize = mtd->writesize >> mtd->subpage_sft;

	return snprintf(buf, PAGE_SIZE, "%u\n", subpagesize);

}
static DEVICE_ATTR(subpagesize, S_IRUGO, mtd_subpagesize_show, NULL);

static ssize_t mtd_oobsize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%lu\n", (unsigned long)mtd->oobsize);

}
static DEVICE_ATTR(oobsize, S_IRUGO, mtd_oobsize_show, NULL);

static ssize_t mtd_numeraseregions_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", mtd->numeraseregions);

}
static DEVICE_ATTR(numeraseregions, S_IRUGO, mtd_numeraseregions_show,
	NULL);

static ssize_t mtd_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", mtd->name);

}
static DEVICE_ATTR(name, S_IRUGO, mtd_name_show, NULL);

static ssize_t mtd_ecc_strength_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", mtd->ecc_strength);
}
static DEVICE_ATTR(ecc_strength, S_IRUGO, mtd_ecc_strength_show, NULL);

static ssize_t mtd_bitflip_threshold_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", mtd->bitflip_threshold);
}

static ssize_t mtd_bitflip_threshold_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	unsigned int bitflip_threshold;
	int retval;

	retval = kstrtouint(buf, 0, &bitflip_threshold);
	if (retval)
		return retval;

	mtd->bitflip_threshold = bitflip_threshold;
	return count;
}
static DEVICE_ATTR(bitflip_threshold, S_IRUGO | S_IWUSR,
		   mtd_bitflip_threshold_show,
		   mtd_bitflip_threshold_store);

static struct attribute *mtd_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_flags.attr,
	&dev_attr_size.attr,
	&dev_attr_erasesize.attr,
	&dev_attr_writesize.attr,
	&dev_attr_subpagesize.attr,
	&dev_attr_oobsize.attr,
	&dev_attr_numeraseregions.attr,
	&dev_attr_name.attr,
	&dev_attr_ecc_strength.attr,
	&dev_attr_bitflip_threshold.attr,
	NULL,
};

static struct attribute_group mtd_group = {
	.attrs		= mtd_attrs,
};

static const struct attribute_group *mtd_groups[] = {
	&mtd_group,
	NULL,
};

static struct device_type mtd_devtype = {
	.name		= "mtd",
	.groups		= mtd_groups,
	.release	= mtd_release,
};

/**
 *	add_mtd_device - register an MTD device
 *	@mtd: pointer to new MTD device info structure
 *
 *	Add a device to the list of MTD devices present in the system, and
 *	notify each currently active MTD 'user' of its arrival. Returns
 *	zero on success or 1 on failure, which currently will only happen
 *	if there is insufficient memory or a sysfs error.
 */

int add_mtd_device(struct mtd_info *mtd)
{
	struct mtd_notifier *not;
	int i, error;

	if (!mtd->backing_dev_info) {
		switch (mtd->type) {
		case MTD_RAM:
			mtd->backing_dev_info = &mtd_bdi_rw_mappable;
			break;
		case MTD_ROM:
			mtd->backing_dev_info = &mtd_bdi_ro_mappable;
			break;
		default:
			mtd->backing_dev_info = &mtd_bdi_unmappable;
			break;
		}
	}

	BUG_ON(mtd->writesize == 0);
	mutex_lock(&mtd_table_mutex);

	do {
		if (!idr_pre_get(&mtd_idr, GFP_KERNEL))
			goto fail_locked;
		error = idr_get_new(&mtd_idr, mtd, &i);
	} while (error == -EAGAIN);

	if (error)
		goto fail_locked;

	mtd->index = i;
	mtd->usecount = 0;

	/* default value if not set by driver */
	if (mtd->bitflip_threshold == 0)
		mtd->bitflip_threshold = mtd->ecc_strength;

	if (is_power_of_2(mtd->erasesize))
		mtd->erasesize_shift = ffs(mtd->erasesize) - 1;
	else
		mtd->erasesize_shift = 0;

	if (is_power_of_2(mtd->writesize))
		mtd->writesize_shift = ffs(mtd->writesize) - 1;
	else
		mtd->writesize_shift = 0;

	mtd->erasesize_mask = (1 << mtd->erasesize_shift) - 1;
	mtd->writesize_mask = (1 << mtd->writesize_shift) - 1;

	/* Some chips always power up locked. Unlock them now */
	if ((mtd->flags & MTD_WRITEABLE) && (mtd->flags & MTD_POWERUP_LOCK)) {
		error = mtd_unlock(mtd, 0, mtd->size);
		if (error && error != -EOPNOTSUPP)
			printk(KERN_WARNING
			       "%s: unlock failed, writes may not work\n",
			       mtd->name);
	}

	/* Caller should have set dev.parent to match the
	 * physical device.
	 */
	mtd->dev.type = &mtd_devtype;
	mtd->dev.class = &mtd_class;
	mtd->dev.devt = MTD_DEVT(i);
	dev_set_name(&mtd->dev, "mtd%d", i);
	dev_set_drvdata(&mtd->dev, mtd);
	if (device_register(&mtd->dev) != 0)
		goto fail_added;

	if (MTD_DEVT(i))
		device_create(&mtd_class, mtd->dev.parent,
			      MTD_DEVT(i) + 1,
			      NULL, "mtd%dro", i);

	pr_debug("mtd: Giving out device %d to %s\n", i, mtd->name);
	/* No need to get a refcount on the module containing
	   the notifier, since we hold the mtd_table_mutex */
	list_for_each_entry(not, &mtd_notifiers, list)
		not->add(mtd);

	mutex_unlock(&mtd_table_mutex);
	/* We _know_ we aren't being removed, because
	   our caller is still holding us here. So none
	   of this try_ nonsense, and no bitching about it
	   either. :) */
	__module_get(THIS_MODULE);
	return 0;

fail_added:
	idr_remove(&mtd_idr, i);
fail_locked:
	mutex_unlock(&mtd_table_mutex);
	return 1;
}

/**
 *	del_mtd_device - unregister an MTD device
 *	@mtd: pointer to MTD device info structure
 *
 *	Remove a device from the list of MTD devices present in the system,
 *	and notify each currently active MTD 'user' of its departure.
 *	Returns zero on success or 1 on failure, which currently will happen
 *	if the requested device does not appear to be present in the list.
 */

int del_mtd_device(struct mtd_info *mtd)
{
	int ret;
	struct mtd_notifier *not;

	mutex_lock(&mtd_table_mutex);

	if (idr_find(&mtd_idr, mtd->index) != mtd) {
		ret = -ENODEV;
		goto out_error;
	}

	/* No need to get a refcount on the module containing
		the notifier, since we hold the mtd_table_mutex */
	list_for_each_entry(not, &mtd_notifiers, list)
		not->remove(mtd);

	if (mtd->usecount) {
		printk(KERN_NOTICE "Removing MTD device #%d (%s) with use count %d\n",
		       mtd->index, mtd->name, mtd->usecount);
		ret = -EBUSY;
	} else {
		device_unregister(&mtd->dev);

		idr_remove(&mtd_idr, mtd->index);

		module_put(THIS_MODULE);
		ret = 0;
	}

out_error:
	mutex_unlock(&mtd_table_mutex);
	return ret;
}

/**
 * mtd_device_parse_register - parse partitions and register an MTD device.
 *
 * @mtd: the MTD device to register
 * @types: the list of MTD partition probes to try, see
 *         'parse_mtd_partitions()' for more information
 * @parser_data: MTD partition parser-specific data
 * @parts: fallback partition information to register, if parsing fails;
 *         only valid if %nr_parts > %0
 * @nr_parts: the number of partitions in parts, if zero then the full
 *            MTD device is registered if no partition info is found
 *
 * This function aggregates MTD partitions parsing (done by
 * 'parse_mtd_partitions()') and MTD device and partitions registering. It
 * basically follows the most common pattern found in many MTD drivers:
 *
 * * It first tries to probe partitions on MTD device @mtd using parsers
 *   specified in @types (if @types is %NULL, then the default list of parsers
 *   is used, see 'parse_mtd_partitions()' for more information). If none are
 *   found this functions tries to fallback to information specified in
 *   @parts/@nr_parts.
 * * If any partitioning info was found, this function registers the found
 *   partitions.
 * * If no partitions were found this function just registers the MTD device
 *   @mtd and exits.
 *
 * Returns zero in case of success and a negative error code in case of failure.
 */
int mtd_device_parse_register(struct mtd_info *mtd, const char **types,
			      struct mtd_part_parser_data *parser_data,
			      const struct mtd_partition *parts,
			      int nr_parts)
{
	int err;
	struct mtd_partition *real_parts;

	err = parse_mtd_partitions(mtd, types, &real_parts, parser_data);
	if (err <= 0 && nr_parts && parts) {
		real_parts = kmemdup(parts, sizeof(*parts) * nr_parts,
				     GFP_KERNEL);
		if (!real_parts)
			err = -ENOMEM;
		else
			err = nr_parts;
	}

	if (err > 0) {
		err = add_mtd_partitions(mtd, real_parts, err);
		kfree(real_parts);
	} else if (err == 0) {
		err = add_mtd_device(mtd);
		if (err == 1)
			err = -ENODEV;
	}

	return err;
}
EXPORT_SYMBOL_GPL(mtd_device_parse_register);

/**
 * mtd_device_unregister - unregister an existing MTD device.
 *
 * @master: the MTD device to unregister.  This will unregister both the master
 *          and any partitions if registered.
 */
int mtd_device_unregister(struct mtd_info *master)
{
	int err;

	err = del_mtd_partitions(master);
	if (err)
		return err;

	if (!device_is_registered(&master->dev))
		return 0;

	return del_mtd_device(master);
}
EXPORT_SYMBOL_GPL(mtd_device_unregister);

/**
 *	register_mtd_user - register a 'user' of MTD devices.
 *	@new: pointer to notifier info structure
 *
 *	Registers a pair of callbacks function to be called upon addition
 *	or removal of MTD devices. Causes the 'add' callback to be immediately
 *	invoked for each MTD device currently present in the system.
 */
void register_mtd_user (struct mtd_notifier *new)
{
	struct mtd_info *mtd;

	mutex_lock(&mtd_table_mutex);

	list_add(&new->list, &mtd_notifiers);

	__module_get(THIS_MODULE);

	mtd_for_each_device(mtd)
		new->add(mtd);

	mutex_unlock(&mtd_table_mutex);
}
EXPORT_SYMBOL_GPL(register_mtd_user);

/**
 *	unregister_mtd_user - unregister a 'user' of MTD devices.
 *	@old: pointer to notifier info structure
 *
 *	Removes a callback function pair from the list of 'users' to be
 *	notified upon addition or removal of MTD devices. Causes the
 *	'remove' callback to be immediately invoked for each MTD device
 *	currently present in the system.
 */
int unregister_mtd_user (struct mtd_notifier *old)
{
	struct mtd_info *mtd;

	mutex_lock(&mtd_table_mutex);

	module_put(THIS_MODULE);

	mtd_for_each_device(mtd)
		old->remove(mtd);

	list_del(&old->list);
	mutex_unlock(&mtd_table_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_mtd_user);

/**
 *	get_mtd_device - obtain a validated handle for an MTD device
 *	@mtd: last known address of the required MTD device
 *	@num: internal device number of the required MTD device
 *
 *	Given a number and NULL address, return the num'th entry in the device
 *	table, if any.	Given an address and num == -1, search the device table
 *	for a device with that address and return if it's still present. Given
 *	both, return the num'th driver only if its address matches. Return
 *	error code if not.
 */
struct mtd_info *get_mtd_device(struct mtd_info *mtd, int num)
{
	struct mtd_info *ret = NULL, *other;
	int err = -ENODEV;

	mutex_lock(&mtd_table_mutex);

	if (num == -1) {
		mtd_for_each_device(other) {
			if (other == mtd) {
				ret = mtd;
				break;
			}
		}
	} else if (num >= 0) {
		ret = idr_find(&mtd_idr, num);
		if (mtd && mtd != ret)
			ret = NULL;
	}

	if (!ret) {
		ret = ERR_PTR(err);
		goto out;
	}

	err = __get_mtd_device(ret);
	if (err)
		ret = ERR_PTR(err);
out:
	mutex_unlock(&mtd_table_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(get_mtd_device);


int __get_mtd_device(struct mtd_info *mtd)
{
	int err;

	if (!try_module_get(mtd->owner))
		return -ENODEV;

	if (mtd->_get_device) {
		err = mtd->_get_device(mtd);

		if (err) {
			module_put(mtd->owner);
			return err;
		}
	}
	mtd->usecount++;
	return 0;
}
EXPORT_SYMBOL_GPL(__get_mtd_device);

/**
 *	get_mtd_device_nm - obtain a validated handle for an MTD device by
 *	device name
 *	@name: MTD device name to open
 *
 * 	This function returns MTD device description structure in case of
 * 	success and an error code in case of failure.
 */
struct mtd_info *get_mtd_device_nm(const char *name)
{
	int err = -ENODEV;
	struct mtd_info *mtd = NULL, *other;

	mutex_lock(&mtd_table_mutex);

	mtd_for_each_device(other) {
		if (!strcmp(name, other->name)) {
			mtd = other;
			break;
		}
	}

	if (!mtd)
		goto out_unlock;

	err = __get_mtd_device(mtd);
	if (err)
		goto out_unlock;

	mutex_unlock(&mtd_table_mutex);
	return mtd;

out_unlock:
	mutex_unlock(&mtd_table_mutex);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(get_mtd_device_nm);

void put_mtd_device(struct mtd_info *mtd)
{
	mutex_lock(&mtd_table_mutex);
	__put_mtd_device(mtd);
	mutex_unlock(&mtd_table_mutex);

}
EXPORT_SYMBOL_GPL(put_mtd_device);

void __put_mtd_device(struct mtd_info *mtd)
{
	--mtd->usecount;
	BUG_ON(mtd->usecount < 0);

	if (mtd->_put_device)
		mtd->_put_device(mtd);

	module_put(mtd->owner);
}
EXPORT_SYMBOL_GPL(__put_mtd_device);

/*
 * Erase is an asynchronous operation.  Device drivers are supposed
 * to call instr->callback() whenever the operation completes, even
 * if it completes with a failure.
 * Callers are supposed to pass a callback function and wait for it
 * to be called before writing to the block.
 */
int mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	if (instr->addr > mtd->size || instr->len > mtd->size - instr->addr)
		return -EINVAL;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	instr->fail_addr = MTD_FAIL_ADDR_UNKNOWN;
	if (!instr->len) {
		instr->state = MTD_ERASE_DONE;
		mtd_erase_callback(instr);
		return 0;
	}
	return mtd->_erase(mtd, instr);
}
EXPORT_SYMBOL_GPL(mtd_erase);

/*
 * This stuff for eXecute-In-Place. phys is optional and may be set to NULL.
 */
int mtd_point(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
	      void **virt, resource_size_t *phys)
{
	*retlen = 0;
	*virt = NULL;
	if (phys)
		*phys = 0;
	if (!mtd->_point)
		return -EOPNOTSUPP;
	if (from < 0 || from > mtd->size || len > mtd->size - from)
		return -EINVAL;
	if (!len)
		return 0;
	return mtd->_point(mtd, from, len, retlen, virt, phys);
}
EXPORT_SYMBOL_GPL(mtd_point);

/* We probably shouldn't allow XIP if the unpoint isn't a NULL */
int mtd_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	if (!mtd->_point)
		return -EOPNOTSUPP;
	if (from < 0 || from > mtd->size || len > mtd->size - from)
		return -EINVAL;
	if (!len)
		return 0;
	return mtd->_unpoint(mtd, from, len);
}
EXPORT_SYMBOL_GPL(mtd_unpoint);

/*
 * Allow NOMMU mmap() to directly map the device (if not NULL)
 * - return the address to which the offset maps
 * - return -ENOSYS to indicate refusal to do the mapping
 */
unsigned long mtd_get_unmapped_area(struct mtd_info *mtd, unsigned long len,
				    unsigned long offset, unsigned long flags)
{
	if (!mtd->_get_unmapped_area)
		return -EOPNOTSUPP;
	if (offset > mtd->size || len > mtd->size - offset)
		return -EINVAL;
	return mtd->_get_unmapped_area(mtd, len, offset, flags);
}
EXPORT_SYMBOL_GPL(mtd_get_unmapped_area);

int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
	     u_char *buf)
{
	*retlen = 0;
	if (from < 0 || from > mtd->size || len > mtd->size - from)
		return -EINVAL;
	if (!len)
		return 0;
	return mtd->_read(mtd, from, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_read);

int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
	      const u_char *buf)
{
	*retlen = 0;
	if (to < 0 || to > mtd->size || len > mtd->size - to)
		return -EINVAL;
	if (!mtd->_write || !(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (!len)
		return 0;
	return mtd->_write(mtd, to, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_write);

/*
 * In blackbox flight recorder like scenarios we want to make successful writes
 * in interrupt context. panic_write() is only intended to be called when its
 * known the kernel is about to panic and we need the write to succeed. Since
 * the kernel is not going to be running for much longer, this function can
 * break locks and delay to ensure the write succeeds (but not sleep).
 */
int mtd_panic_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
		    const u_char *buf)
{
	*retlen = 0;
	if (!mtd->_panic_write)
		return -EOPNOTSUPP;
	if (to < 0 || to > mtd->size || len > mtd->size - to)
		return -EINVAL;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (!len)
		return 0;
	return mtd->_panic_write(mtd, to, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_panic_write);

/*
 * Method to access the protection register area, present in some flash
 * devices. The user data is one time programmable but the factory data is read
 * only.
 */
int mtd_get_fact_prot_info(struct mtd_info *mtd, struct otp_info *buf,
			   size_t len)
{
	if (!mtd->_get_fact_prot_info)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return mtd->_get_fact_prot_info(mtd, buf, len);
}
EXPORT_SYMBOL_GPL(mtd_get_fact_prot_info);

int mtd_read_fact_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf)
{
	*retlen = 0;
	if (!mtd->_read_fact_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return mtd->_read_fact_prot_reg(mtd, from, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_read_fact_prot_reg);

int mtd_get_user_prot_info(struct mtd_info *mtd, struct otp_info *buf,
			   size_t len)
{
	if (!mtd->_get_user_prot_info)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return mtd->_get_user_prot_info(mtd, buf, len);
}
EXPORT_SYMBOL_GPL(mtd_get_user_prot_info);

int mtd_read_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf)
{
	*retlen = 0;
	if (!mtd->_read_user_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return mtd->_read_user_prot_reg(mtd, from, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_read_user_prot_reg);

int mtd_write_user_prot_reg(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, u_char *buf)
{
	*retlen = 0;
	if (!mtd->_write_user_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return mtd->_write_user_prot_reg(mtd, to, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_write_user_prot_reg);

int mtd_lock_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len)
{
	if (!mtd->_lock_user_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return mtd->_lock_user_prot_reg(mtd, from, len);
}
EXPORT_SYMBOL_GPL(mtd_lock_user_prot_reg);

/* Chip-supported device locking */
int mtd_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	if (!mtd->_lock)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs > mtd->size || len > mtd->size - ofs)
		return -EINVAL;
	if (!len)
		return 0;
	return mtd->_lock(mtd, ofs, len);
}
EXPORT_SYMBOL_GPL(mtd_lock);

int mtd_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	if (!mtd->_unlock)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs > mtd->size || len > mtd->size - ofs)
		return -EINVAL;
	if (!len)
		return 0;
	return mtd->_unlock(mtd, ofs, len);
}
EXPORT_SYMBOL_GPL(mtd_unlock);

int mtd_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	if (!mtd->_is_locked)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs > mtd->size || len > mtd->size - ofs)
		return -EINVAL;
	if (!len)
		return 0;
	return mtd->_is_locked(mtd, ofs, len);
}
EXPORT_SYMBOL_GPL(mtd_is_locked);

int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	if (!mtd->_block_isbad)
		return 0;
	if (ofs < 0 || ofs > mtd->size)
		return -EINVAL;
	return mtd->_block_isbad(mtd, ofs);
}
EXPORT_SYMBOL_GPL(mtd_block_isbad);

int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	if (!mtd->_block_markbad)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs > mtd->size)
		return -EINVAL;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	return mtd->_block_markbad(mtd, ofs);
}
EXPORT_SYMBOL_GPL(mtd_block_markbad);

/*
 * default_mtd_writev - the default writev method
 * @mtd: mtd device description object pointer
 * @vecs: the vectors to write
 * @count: count of vectors in @vecs
 * @to: the MTD device offset to write to
 * @retlen: on exit contains the count of bytes written to the MTD device.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int default_mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
			      unsigned long count, loff_t to, size_t *retlen)
{
	unsigned long i;
	size_t totlen = 0, thislen;
	int ret = 0;

	for (i = 0; i < count; i++) {
		if (!vecs[i].iov_len)
			continue;
		ret = mtd_write(mtd, to, vecs[i].iov_len, &thislen,
				vecs[i].iov_base);
		totlen += thislen;
		if (ret || thislen != vecs[i].iov_len)
			break;
		to += vecs[i].iov_len;
	}
	*retlen = totlen;
	return ret;
}

/*
 * mtd_writev - the vector-based MTD write method
 * @mtd: mtd device description object pointer
 * @vecs: the vectors to write
 * @count: count of vectors in @vecs
 * @to: the MTD device offset to write to
 * @retlen: on exit contains the count of bytes written to the MTD device.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
	       unsigned long count, loff_t to, size_t *retlen)
{
	*retlen = 0;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (!mtd->_writev)
		return default_mtd_writev(mtd, vecs, count, to, retlen);
	return mtd->_writev(mtd, vecs, count, to, retlen);
}
EXPORT_SYMBOL_GPL(mtd_writev);

/**
 * mtd_kmalloc_up_to - allocate a contiguous buffer up to the specified size
 * @mtd: mtd device description object pointer
 * @size: a pointer to the ideal or maximum size of the allocation, points
 *        to the actual allocation size on success.
 *
 * This routine attempts to allocate a contiguous kernel buffer up to
 * the specified size, backing off the size of the request exponentially
 * until the request succeeds or until the allocation size falls below
 * the system page size. This attempts to make sure it does not adversely
 * impact system performance, so when allocating more than one page, we
 * ask the memory allocator to avoid re-trying, swapping, writing back
 * or performing I/O.
 *
 * Note, this function also makes sure that the allocated buffer is aligned to
 * the MTD device's min. I/O unit, i.e. the "mtd->writesize" value.
 *
 * This is called, for example by mtd_{read,write} and jffs2_scan_medium,
 * to handle smaller (i.e. degraded) buffer allocations under low- or
 * fragmented-memory situations where such reduced allocations, from a
 * requested ideal, are allowed.
 *
 * Returns a pointer to the allocated buffer on success; otherwise, NULL.
 */
void *mtd_kmalloc_up_to(const struct mtd_info *mtd, size_t *size)
{
	gfp_t flags = __GFP_NOWARN | __GFP_WAIT |
		       __GFP_NORETRY | __GFP_NO_KSWAPD;
	size_t min_alloc = max_t(size_t, mtd->writesize, PAGE_SIZE);
	void *kbuf;

	*size = min_t(size_t, *size, KMALLOC_MAX_SIZE);

	while (*size > min_alloc) {
		kbuf = kmalloc(*size, flags);
		if (kbuf)
			return kbuf;

		*size >>= 1;
		*size = ALIGN(*size, mtd->writesize);
	}

	/*
	 * For the last resort allocation allow 'kmalloc()' to do all sorts of
	 * things (write-back, dropping caches, etc) by using GFP_KERNEL.
	 */
	return kmalloc(*size, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(mtd_kmalloc_up_to);

#ifdef CONFIG_PROC_FS

/*====================================================================*/
/* Support for /proc/mtd */

static struct proc_dir_entry *proc_mtd;

static int mtd_proc_show(struct seq_file *m, void *v)
{
	struct mtd_info *mtd;

	seq_puts(m, "dev:    size   erasesize  name\n");
	mutex_lock(&mtd_table_mutex);
	mtd_for_each_device(mtd) {
		seq_printf(m, "mtd%d: %8.8llx %8.8x \"%s\"\n",
			   mtd->index, (unsigned long long)mtd->size,
			   mtd->erasesize, mtd->name);
	}
	mutex_unlock(&mtd_table_mutex);
	return 0;
}

static int mtd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, mtd_proc_show, NULL);
}

static const struct file_operations mtd_proc_ops = {
	.open		= mtd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* CONFIG_PROC_FS */

/*====================================================================*/
/* Init code */

static int __init mtd_bdi_init(struct backing_dev_info *bdi, const char *name)
{
	int ret;

	ret = bdi_init(bdi);
	if (!ret)
		ret = bdi_register(bdi, NULL, name);

	if (ret)
		bdi_destroy(bdi);

	return ret;
}

static int __init init_mtd(void)
{
	int ret;

	ret = class_register(&mtd_class);
	if (ret)
		goto err_reg;

	ret = mtd_bdi_init(&mtd_bdi_unmappable, "mtd-unmap");
	if (ret)
		goto err_bdi1;

	ret = mtd_bdi_init(&mtd_bdi_ro_mappable, "mtd-romap");
	if (ret)
		goto err_bdi2;

	ret = mtd_bdi_init(&mtd_bdi_rw_mappable, "mtd-rwmap");
	if (ret)
		goto err_bdi3;

#ifdef CONFIG_PROC_FS
	proc_mtd = proc_create("mtd", 0, NULL, &mtd_proc_ops);
#endif /* CONFIG_PROC_FS */
	return 0;

err_bdi3:
	bdi_destroy(&mtd_bdi_ro_mappable);
err_bdi2:
	bdi_destroy(&mtd_bdi_unmappable);
err_bdi1:
	class_unregister(&mtd_class);
err_reg:
	pr_err("Error registering mtd class or bdi: %d\n", ret);
	return ret;
}

static void __exit cleanup_mtd(void)
{
#ifdef CONFIG_PROC_FS
	if (proc_mtd)
		remove_proc_entry( "mtd", NULL);
#endif /* CONFIG_PROC_FS */
	class_unregister(&mtd_class);
	bdi_destroy(&mtd_bdi_unmappable);
	bdi_destroy(&mtd_bdi_ro_mappable);
	bdi_destroy(&mtd_bdi_rw_mappable);
}

module_init(init_mtd);
module_exit(cleanup_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Core MTD registration and access routines");
