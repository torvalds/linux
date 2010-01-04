/*
 * Core registration and callback routines for MTD
 * drivers and users.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/mtd/compatmac.h>
#include <linux/proc_fs.h>

#include <linux/mtd/mtd.h>
#include "internal.h"

#include "mtdcore.h"

static int mtd_cls_suspend(struct device *dev, pm_message_t state);
static int mtd_cls_resume(struct device *dev);

static struct class mtd_class = {
	.name = "mtd",
	.owner = THIS_MODULE,
	.suspend = mtd_cls_suspend,
	.resume = mtd_cls_resume,
};

/* These are exported solely for the purpose of mtd_blkdevs.c. You
   should not use them for _anything_ else */
DEFINE_MUTEX(mtd_table_mutex);
struct mtd_info *mtd_table[MAX_MTD_DEVICES];

EXPORT_SYMBOL_GPL(mtd_table_mutex);
EXPORT_SYMBOL_GPL(mtd_table);

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
	dev_t index = MTD_DEVT(dev_to_mtd(dev)->index);

	/* remove /dev/mtdXro node if needed */
	if (index)
		device_destroy(&mtd_class, index + 1);
}

static int mtd_cls_suspend(struct device *dev, pm_message_t state)
{
	struct mtd_info *mtd = dev_to_mtd(dev);

	if (mtd && mtd->suspend)
		return mtd->suspend(mtd);
	else
		return 0;
}

static int mtd_cls_resume(struct device *dev)
{
	struct mtd_info *mtd = dev_to_mtd(dev);
	
	if (mtd && mtd->resume)
		mtd->resume(mtd);
	return 0;
}

static ssize_t mtd_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);
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
	struct mtd_info *mtd = dev_to_mtd(dev);

	return snprintf(buf, PAGE_SIZE, "0x%lx\n", (unsigned long)mtd->flags);

}
static DEVICE_ATTR(flags, S_IRUGO, mtd_flags_show, NULL);

static ssize_t mtd_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);

	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)mtd->size);

}
static DEVICE_ATTR(size, S_IRUGO, mtd_size_show, NULL);

static ssize_t mtd_erasesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);

	return snprintf(buf, PAGE_SIZE, "%lu\n", (unsigned long)mtd->erasesize);

}
static DEVICE_ATTR(erasesize, S_IRUGO, mtd_erasesize_show, NULL);

static ssize_t mtd_writesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);

	return snprintf(buf, PAGE_SIZE, "%lu\n", (unsigned long)mtd->writesize);

}
static DEVICE_ATTR(writesize, S_IRUGO, mtd_writesize_show, NULL);

static ssize_t mtd_subpagesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);
	unsigned int subpagesize = mtd->writesize >> mtd->subpage_sft;

	return snprintf(buf, PAGE_SIZE, "%u\n", subpagesize);

}
static DEVICE_ATTR(subpagesize, S_IRUGO, mtd_subpagesize_show, NULL);

static ssize_t mtd_oobsize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);

	return snprintf(buf, PAGE_SIZE, "%lu\n", (unsigned long)mtd->oobsize);

}
static DEVICE_ATTR(oobsize, S_IRUGO, mtd_oobsize_show, NULL);

static ssize_t mtd_numeraseregions_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", mtd->numeraseregions);

}
static DEVICE_ATTR(numeraseregions, S_IRUGO, mtd_numeraseregions_show,
	NULL);

static ssize_t mtd_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_to_mtd(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", mtd->name);

}
static DEVICE_ATTR(name, S_IRUGO, mtd_name_show, NULL);

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
 *	if the number of present devices exceeds MAX_MTD_DEVICES (i.e. 16)
 *	or there's a sysfs error.
 */

int add_mtd_device(struct mtd_info *mtd)
{
	int i;

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

	for (i=0; i < MAX_MTD_DEVICES; i++)
		if (!mtd_table[i]) {
			struct mtd_notifier *not;

			mtd_table[i] = mtd;
			mtd->index = i;
			mtd->usecount = 0;

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
			if ((mtd->flags & MTD_WRITEABLE)
			    && (mtd->flags & MTD_POWERUP_LOCK) && mtd->unlock) {
				if (mtd->unlock(mtd, 0, mtd->size))
					printk(KERN_WARNING
					       "%s: unlock failed, "
					       "writes may not work\n",
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
			if (device_register(&mtd->dev) != 0) {
				mtd_table[i] = NULL;
				break;
			}

			if (MTD_DEVT(i))
				device_create(&mtd_class, mtd->dev.parent,
						MTD_DEVT(i) + 1,
						NULL, "mtd%dro", i);

			DEBUG(0, "mtd: Giving out device %d to %s\n",i, mtd->name);
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
		}

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

int del_mtd_device (struct mtd_info *mtd)
{
	int ret;

	mutex_lock(&mtd_table_mutex);

	if (mtd_table[mtd->index] != mtd) {
		ret = -ENODEV;
	} else if (mtd->usecount) {
		printk(KERN_NOTICE "Removing MTD device #%d (%s) with use count %d\n",
		       mtd->index, mtd->name, mtd->usecount);
		ret = -EBUSY;
	} else {
		struct mtd_notifier *not;

		device_unregister(&mtd->dev);

		/* No need to get a refcount on the module containing
		   the notifier, since we hold the mtd_table_mutex */
		list_for_each_entry(not, &mtd_notifiers, list)
			not->remove(mtd);

		mtd_table[mtd->index] = NULL;

		module_put(THIS_MODULE);
		ret = 0;
	}

	mutex_unlock(&mtd_table_mutex);
	return ret;
}

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
	int i;

	mutex_lock(&mtd_table_mutex);

	list_add(&new->list, &mtd_notifiers);

 	__module_get(THIS_MODULE);

	for (i=0; i< MAX_MTD_DEVICES; i++)
		if (mtd_table[i])
			new->add(mtd_table[i]);

	mutex_unlock(&mtd_table_mutex);
}

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
	int i;

	mutex_lock(&mtd_table_mutex);

	module_put(THIS_MODULE);

	for (i=0; i< MAX_MTD_DEVICES; i++)
		if (mtd_table[i])
			old->remove(mtd_table[i]);

	list_del(&old->list);
	mutex_unlock(&mtd_table_mutex);
	return 0;
}


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
	struct mtd_info *ret = NULL;
	int i, err = -ENODEV;

	mutex_lock(&mtd_table_mutex);

	if (num == -1) {
		for (i=0; i< MAX_MTD_DEVICES; i++)
			if (mtd_table[i] == mtd)
				ret = mtd_table[i];
	} else if (num >= 0 && num < MAX_MTD_DEVICES) {
		ret = mtd_table[num];
		if (mtd && mtd != ret)
			ret = NULL;
	}

	if (!ret)
		goto out_unlock;

	if (!try_module_get(ret->owner))
		goto out_unlock;

	if (ret->get_device) {
		err = ret->get_device(ret);
		if (err)
			goto out_put;
	}

	ret->usecount++;
	mutex_unlock(&mtd_table_mutex);
	return ret;

out_put:
	module_put(ret->owner);
out_unlock:
	mutex_unlock(&mtd_table_mutex);
	return ERR_PTR(err);
}

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
	int i, err = -ENODEV;
	struct mtd_info *mtd = NULL;

	mutex_lock(&mtd_table_mutex);

	for (i = 0; i < MAX_MTD_DEVICES; i++) {
		if (mtd_table[i] && !strcmp(name, mtd_table[i]->name)) {
			mtd = mtd_table[i];
			break;
		}
	}

	if (!mtd)
		goto out_unlock;

	if (!try_module_get(mtd->owner))
		goto out_unlock;

	if (mtd->get_device) {
		err = mtd->get_device(mtd);
		if (err)
			goto out_put;
	}

	mtd->usecount++;
	mutex_unlock(&mtd_table_mutex);
	return mtd;

out_put:
	module_put(mtd->owner);
out_unlock:
	mutex_unlock(&mtd_table_mutex);
	return ERR_PTR(err);
}

void put_mtd_device(struct mtd_info *mtd)
{
	int c;

	mutex_lock(&mtd_table_mutex);
	c = --mtd->usecount;
	if (mtd->put_device)
		mtd->put_device(mtd);
	mutex_unlock(&mtd_table_mutex);
	BUG_ON(c < 0);

	module_put(mtd->owner);
}

/* default_mtd_writev - default mtd writev method for MTD devices that
 *			don't implement their own
 */

int default_mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
		       unsigned long count, loff_t to, size_t *retlen)
{
	unsigned long i;
	size_t totlen = 0, thislen;
	int ret = 0;

	if(!mtd->write) {
		ret = -EROFS;
	} else {
		for (i=0; i<count; i++) {
			if (!vecs[i].iov_len)
				continue;
			ret = mtd->write(mtd, to, vecs[i].iov_len, &thislen, vecs[i].iov_base);
			totlen += thislen;
			if (ret || thislen != vecs[i].iov_len)
				break;
			to += vecs[i].iov_len;
		}
	}
	if (retlen)
		*retlen = totlen;
	return ret;
}

EXPORT_SYMBOL_GPL(add_mtd_device);
EXPORT_SYMBOL_GPL(del_mtd_device);
EXPORT_SYMBOL_GPL(get_mtd_device);
EXPORT_SYMBOL_GPL(get_mtd_device_nm);
EXPORT_SYMBOL_GPL(put_mtd_device);
EXPORT_SYMBOL_GPL(register_mtd_user);
EXPORT_SYMBOL_GPL(unregister_mtd_user);
EXPORT_SYMBOL_GPL(default_mtd_writev);

#ifdef CONFIG_PROC_FS

/*====================================================================*/
/* Support for /proc/mtd */

static struct proc_dir_entry *proc_mtd;

static inline int mtd_proc_info (char *buf, int i)
{
	struct mtd_info *this = mtd_table[i];

	if (!this)
		return 0;

	return sprintf(buf, "mtd%d: %8.8llx %8.8x \"%s\"\n", i,
		       (unsigned long long)this->size,
		       this->erasesize, this->name);
}

static int mtd_read_proc (char *page, char **start, off_t off, int count,
			  int *eof, void *data_unused)
{
	int len, l, i;
        off_t   begin = 0;

	mutex_lock(&mtd_table_mutex);

	len = sprintf(page, "dev:    size   erasesize  name\n");
        for (i=0; i< MAX_MTD_DEVICES; i++) {

                l = mtd_proc_info(page + len, i);
                len += l;
                if (len+begin > off+count)
                        goto done;
                if (len+begin < off) {
                        begin += len;
                        len = 0;
                }
        }

        *eof = 1;

done:
	mutex_unlock(&mtd_table_mutex);
        if (off >= len+begin)
                return 0;
        *start = page + (off-begin);
        return ((count < begin+len-off) ? count : begin+len-off);
}

#endif /* CONFIG_PROC_FS */

/*====================================================================*/
/* Init code */

static int __init init_mtd(void)
{
	int ret;
	ret = class_register(&mtd_class);

	if (ret) {
		pr_err("Error registering mtd class: %d\n", ret);
		return ret;
	}
#ifdef CONFIG_PROC_FS
	if ((proc_mtd = create_proc_entry( "mtd", 0, NULL )))
		proc_mtd->read_proc = mtd_read_proc;
#endif /* CONFIG_PROC_FS */
	return 0;
}

static void __exit cleanup_mtd(void)
{
#ifdef CONFIG_PROC_FS
        if (proc_mtd)
		remove_proc_entry( "mtd", NULL);
#endif /* CONFIG_PROC_FS */
	class_unregister(&mtd_class);
}

module_init(init_mtd);
module_exit(cleanup_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Core MTD registration and access routines");
