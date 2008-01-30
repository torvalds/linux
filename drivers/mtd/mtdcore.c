/*
 * $Id: mtdcore.c,v 1.47 2005/11/07 11:14:20 gleixner Exp $
 *
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

#include "mtdcore.h"

/* These are exported solely for the purpose of mtd_blkdevs.c. You
   should not use them for _anything_ else */
DEFINE_MUTEX(mtd_table_mutex);
struct mtd_info *mtd_table[MAX_MTD_DEVICES];

EXPORT_SYMBOL_GPL(mtd_table_mutex);
EXPORT_SYMBOL_GPL(mtd_table);

static LIST_HEAD(mtd_notifiers);

/**
 *	add_mtd_device - register an MTD device
 *	@mtd: pointer to new MTD device info structure
 *
 *	Add a device to the list of MTD devices present in the system, and
 *	notify each currently active MTD 'user' of its arrival. Returns
 *	zero on success or 1 on failure, which currently will only happen
 *	if the number of present devices exceeds MAX_MTD_DEVICES (i.e. 16)
 */

int add_mtd_device(struct mtd_info *mtd)
{
	int i;

	BUG_ON(mtd->writesize == 0);
	mutex_lock(&mtd_table_mutex);

	for (i=0; i < MAX_MTD_DEVICES; i++)
		if (!mtd_table[i]) {
			struct list_head *this;

			mtd_table[i] = mtd;
			mtd->index = i;
			mtd->usecount = 0;

			/* Some chips always power up locked. Unlock them now */
			if ((mtd->flags & MTD_WRITEABLE)
			    && (mtd->flags & MTD_POWERUP_LOCK) && mtd->unlock) {
				if (mtd->unlock(mtd, 0, mtd->size))
					printk(KERN_WARNING
					       "%s: unlock failed, "
					       "writes may not work\n",
					       mtd->name);
			}

			DEBUG(0, "mtd: Giving out device %d to %s\n",i, mtd->name);
			/* No need to get a refcount on the module containing
			   the notifier, since we hold the mtd_table_mutex */
			list_for_each(this, &mtd_notifiers) {
				struct mtd_notifier *not = list_entry(this, struct mtd_notifier, list);
				not->add(mtd);
			}

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
		struct list_head *this;

		/* No need to get a refcount on the module containing
		   the notifier, since we hold the mtd_table_mutex */
		list_for_each(this, &mtd_notifiers) {
			struct mtd_notifier *not = list_entry(this, struct mtd_notifier, list);
			not->remove(mtd);
		}

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
	} else if (num < MAX_MTD_DEVICES) {
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

	return sprintf(buf, "mtd%d: %8.8x %8.8x \"%s\"\n", i, this->size,
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

/*====================================================================*/
/* Init code */

static int __init init_mtd(void)
{
	if ((proc_mtd = create_proc_entry( "mtd", 0, NULL )))
		proc_mtd->read_proc = mtd_read_proc;
	return 0;
}

static void __exit cleanup_mtd(void)
{
        if (proc_mtd)
		remove_proc_entry( "mtd", NULL);
}

module_init(init_mtd);
module_exit(cleanup_mtd);

#endif /* CONFIG_PROC_FS */


MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Core MTD registration and access routines");
