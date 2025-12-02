// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 Intel Deutschland GmbH
 *
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/devcoredump.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/workqueue.h>

static struct class devcd_class;

/* global disable flag, for security purposes */
static bool devcd_disabled;

struct devcd_entry {
	struct device devcd_dev;
	void *data;
	size_t datalen;
	/*
	 * There are 2 races for which mutex is required.
	 *
	 * The first race is between device creation and userspace writing to
	 * schedule immediately destruction.
	 *
	 * This race is handled by arming the timer before device creation, but
	 * when device creation fails the timer still exists.
	 *
	 * To solve this, hold the mutex during device_add(), and set
	 * init_completed on success before releasing the mutex.
	 *
	 * That way the timer will never fire until device_add() is called,
	 * it will do nothing if init_completed is not set. The timer is also
	 * cancelled in that case.
	 *
	 * The second race involves multiple parallel invocations of devcd_free(),
	 * add a deleted flag so only 1 can call the destructor.
	 */
	struct mutex mutex;
	bool init_completed, deleted;
	struct module *owner;
	ssize_t (*read)(char *buffer, loff_t offset, size_t count,
			void *data, size_t datalen);
	void (*free)(void *data);
	/*
	 * If nothing interferes and device_add() was returns success,
	 * del_wk will destroy the device after the timer fires.
	 *
	 * Multiple userspace processes can interfere in the working of the timer:
	 * - Writing to the coredump will reschedule the timer to run immediately,
	 *   if still armed.
	 *
	 *   This is handled by using "if (cancel_delayed_work()) {
	 *   schedule_delayed_work() }", to prevent re-arming after having
	 *   been previously fired.
	 * - Writing to /sys/class/devcoredump/disabled will destroy the
	 *   coredump synchronously.
	 *   This is handled by using disable_delayed_work_sync(), and then
	 *   checking if deleted flag is set with &devcd->mutex held.
	 */
	struct delayed_work del_wk;
	struct device *failing_dev;
};

static struct devcd_entry *dev_to_devcd(struct device *dev)
{
	return container_of(dev, struct devcd_entry, devcd_dev);
}

static void devcd_dev_release(struct device *dev)
{
	struct devcd_entry *devcd = dev_to_devcd(dev);

	devcd->free(devcd->data);
	module_put(devcd->owner);

	/*
	 * this seems racy, but I don't see a notifier or such on
	 * a struct device to know when it goes away?
	 */
	if (devcd->failing_dev->kobj.sd)
		sysfs_delete_link(&devcd->failing_dev->kobj, &dev->kobj,
				  "devcoredump");

	put_device(devcd->failing_dev);
	kfree(devcd);
}

static void __devcd_del(struct devcd_entry *devcd)
{
	devcd->deleted = true;
	device_del(&devcd->devcd_dev);
	put_device(&devcd->devcd_dev);
}

static void devcd_del(struct work_struct *wk)
{
	struct devcd_entry *devcd;
	bool init_completed;

	devcd = container_of(wk, struct devcd_entry, del_wk.work);

	/* devcd->mutex serializes against dev_coredumpm_timeout */
	mutex_lock(&devcd->mutex);
	init_completed = devcd->init_completed;
	mutex_unlock(&devcd->mutex);

	if (init_completed)
		__devcd_del(devcd);
}

static ssize_t devcd_data_read(struct file *filp, struct kobject *kobj,
			       const struct bin_attribute *bin_attr,
			       char *buffer, loff_t offset, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct devcd_entry *devcd = dev_to_devcd(dev);

	return devcd->read(buffer, offset, count, devcd->data, devcd->datalen);
}

static ssize_t devcd_data_write(struct file *filp, struct kobject *kobj,
				const struct bin_attribute *bin_attr,
				char *buffer, loff_t offset, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct devcd_entry *devcd = dev_to_devcd(dev);

	/*
	 * Although it's tempting to use mod_delayed work here,
	 * that will cause a reschedule if the timer already fired.
	 */
	if (cancel_delayed_work(&devcd->del_wk))
		schedule_delayed_work(&devcd->del_wk, 0);

	return count;
}

static const struct bin_attribute devcd_attr_data =
	__BIN_ATTR(data, 0600, devcd_data_read, devcd_data_write, 0);

static const struct bin_attribute *const devcd_dev_bin_attrs[] = {
	&devcd_attr_data, NULL,
};

static const struct attribute_group devcd_dev_group = {
	.bin_attrs = devcd_dev_bin_attrs,
};

static const struct attribute_group *devcd_dev_groups[] = {
	&devcd_dev_group, NULL,
};

static int devcd_free(struct device *dev, void *data)
{
	struct devcd_entry *devcd = dev_to_devcd(dev);

	/*
	 * To prevent a race with devcd_data_write(), disable work and
	 * complete manually instead.
	 *
	 * We cannot rely on the return value of
	 * disable_delayed_work_sync() here, because it might be in the
	 * middle of a cancel_delayed_work + schedule_delayed_work pair.
	 *
	 * devcd->mutex here guards against multiple parallel invocations
	 * of devcd_free().
	 */
	disable_delayed_work_sync(&devcd->del_wk);
	mutex_lock(&devcd->mutex);
	if (!devcd->deleted)
		__devcd_del(devcd);
	mutex_unlock(&devcd->mutex);
	return 0;
}

static ssize_t disabled_show(const struct class *class, const struct class_attribute *attr,
			     char *buf)
{
	return sysfs_emit(buf, "%d\n", devcd_disabled);
}

/*
 *
 *	disabled_store()                                	worker()
 *	 class_for_each_device(&devcd_class,
 *		NULL, NULL, devcd_free)
 *         ...
 *         ...
 *	   while ((dev = class_dev_iter_next(&iter))
 *                                                             devcd_del()
 *                                                               device_del()
 *                                                                 put_device() <- last reference
 *             error = fn(dev, data)                           devcd_dev_release()
 *             devcd_free(dev, data)                           kfree(devcd)
 *
 *
 * In the above diagram, it looks like disabled_store() would be racing with parallelly
 * running devcd_del() and result in memory abort after dropping its last reference with
 * put_device(). However, this will not happens as fn(dev, data) runs
 * with its own reference to device via klist_node so it is not its last reference.
 * so, above situation would not occur.
 */

static ssize_t disabled_store(const struct class *class, const struct class_attribute *attr,
			      const char *buf, size_t count)
{
	long tmp = simple_strtol(buf, NULL, 10);

	/*
	 * This essentially makes the attribute write-once, since you can't
	 * go back to not having it disabled. This is intentional, it serves
	 * as a system lockdown feature.
	 */
	if (tmp != 1)
		return -EINVAL;

	devcd_disabled = true;

	class_for_each_device(&devcd_class, NULL, NULL, devcd_free);

	return count;
}
static CLASS_ATTR_RW(disabled);

static struct attribute *devcd_class_attrs[] = {
	&class_attr_disabled.attr,
	NULL,
};
ATTRIBUTE_GROUPS(devcd_class);

static struct class devcd_class = {
	.name		= "devcoredump",
	.dev_release	= devcd_dev_release,
	.dev_groups	= devcd_dev_groups,
	.class_groups	= devcd_class_groups,
};

static ssize_t devcd_readv(char *buffer, loff_t offset, size_t count,
			   void *data, size_t datalen)
{
	return memory_read_from_buffer(buffer, count, &offset, data, datalen);
}

static void devcd_freev(void *data)
{
	vfree(data);
}

/**
 * dev_coredumpv - create device coredump with vmalloc data
 * @dev: the struct device for the crashed device
 * @data: vmalloc data containing the device coredump
 * @datalen: length of the data
 * @gfp: allocation flags
 *
 * This function takes ownership of the vmalloc'ed data and will free
 * it when it is no longer used. See dev_coredumpm() for more information.
 */
void dev_coredumpv(struct device *dev, void *data, size_t datalen,
		   gfp_t gfp)
{
	dev_coredumpm(dev, NULL, data, datalen, gfp, devcd_readv, devcd_freev);
}
EXPORT_SYMBOL_GPL(dev_coredumpv);

static int devcd_match_failing(struct device *dev, const void *failing)
{
	struct devcd_entry *devcd = dev_to_devcd(dev);

	return devcd->failing_dev == failing;
}

/**
 * devcd_free_sgtable - free all the memory of the given scatterlist table
 * (i.e. both pages and scatterlist instances)
 * NOTE: if two tables allocated with devcd_alloc_sgtable and then chained
 * using the sg_chain function then that function should be called only once
 * on the chained table
 * @data: pointer to sg_table to free
 */
static void devcd_free_sgtable(void *data)
{
	_devcd_free_sgtable(data);
}

/**
 * devcd_read_from_sgtable - copy data from sg_table to a given buffer
 * and return the number of bytes read
 * @buffer: the buffer to copy the data to it
 * @buf_len: the length of the buffer
 * @data: the scatterlist table to copy from
 * @offset: start copy from @offset@ bytes from the head of the data
 *	in the given scatterlist
 * @data_len: the length of the data in the sg_table
 *
 * Returns: the number of bytes copied
 */
static ssize_t devcd_read_from_sgtable(char *buffer, loff_t offset,
				       size_t buf_len, void *data,
				       size_t data_len)
{
	struct scatterlist *table = data;

	if (offset > data_len)
		return -EINVAL;

	if (offset + buf_len > data_len)
		buf_len = data_len - offset;
	return sg_pcopy_to_buffer(table, sg_nents(table), buffer, buf_len,
				  offset);
}

/**
 * dev_coredump_put - remove device coredump
 * @dev: the struct device for the crashed device
 *
 * dev_coredump_put() removes coredump, if exists, for a given device from
 * the file system and free its associated data otherwise, does nothing.
 *
 * It is useful for modules that do not want to keep coredump
 * available after its unload.
 */
void dev_coredump_put(struct device *dev)
{
	struct device *existing;

	existing = class_find_device(&devcd_class, NULL, dev,
				     devcd_match_failing);
	if (existing) {
		devcd_free(existing, NULL);
		put_device(existing);
	}
}
EXPORT_SYMBOL_GPL(dev_coredump_put);

/**
 * dev_coredumpm_timeout - create device coredump with read/free methods with a
 * custom timeout.
 * @dev: the struct device for the crashed device
 * @owner: the module that contains the read/free functions, use %THIS_MODULE
 * @data: data cookie for the @read/@free functions
 * @datalen: length of the data
 * @gfp: allocation flags
 * @read: function to read from the given buffer
 * @free: function to free the given buffer
 * @timeout: time in jiffies to remove coredump
 *
 * Creates a new device coredump for the given device. If a previous one hasn't
 * been read yet, the new coredump is discarded. The data lifetime is determined
 * by the device coredump framework and when it is no longer needed the @free
 * function will be called to free the data.
 */
void dev_coredumpm_timeout(struct device *dev, struct module *owner,
			   void *data, size_t datalen, gfp_t gfp,
			   ssize_t (*read)(char *buffer, loff_t offset,
					   size_t count, void *data,
					   size_t datalen),
			   void (*free)(void *data),
			   unsigned long timeout)
{
	static atomic_t devcd_count = ATOMIC_INIT(0);
	struct devcd_entry *devcd;
	struct device *existing;

	if (devcd_disabled)
		goto free;

	existing = class_find_device(&devcd_class, NULL, dev,
				     devcd_match_failing);
	if (existing) {
		put_device(existing);
		goto free;
	}

	if (!try_module_get(owner))
		goto free;

	devcd = kzalloc(sizeof(*devcd), gfp);
	if (!devcd)
		goto put_module;

	devcd->owner = owner;
	devcd->data = data;
	devcd->datalen = datalen;
	devcd->read = read;
	devcd->free = free;
	devcd->failing_dev = get_device(dev);
	devcd->deleted = false;

	mutex_init(&devcd->mutex);
	device_initialize(&devcd->devcd_dev);

	dev_set_name(&devcd->devcd_dev, "devcd%d",
		     atomic_inc_return(&devcd_count));
	devcd->devcd_dev.class = &devcd_class;

	dev_set_uevent_suppress(&devcd->devcd_dev, true);

	/* devcd->mutex prevents devcd_del() completing until init finishes */
	mutex_lock(&devcd->mutex);
	devcd->init_completed = false;
	INIT_DELAYED_WORK(&devcd->del_wk, devcd_del);
	schedule_delayed_work(&devcd->del_wk, timeout);

	if (device_add(&devcd->devcd_dev))
		goto put_device;

	/*
	 * These should normally not fail, but there is no problem
	 * continuing without the links, so just warn instead of
	 * failing.
	 */
	if (sysfs_create_link(&devcd->devcd_dev.kobj, &dev->kobj,
			      "failing_device") ||
	    sysfs_create_link(&dev->kobj, &devcd->devcd_dev.kobj,
		              "devcoredump"))
		dev_warn(dev, "devcoredump create_link failed\n");

	dev_set_uevent_suppress(&devcd->devcd_dev, false);
	kobject_uevent(&devcd->devcd_dev.kobj, KOBJ_ADD);

	/*
	 * Safe to run devcd_del() now that we are done with devcd_dev.
	 * Alternatively we could have taken a ref on devcd_dev before
	 * dropping the lock.
	 */
	devcd->init_completed = true;
	mutex_unlock(&devcd->mutex);
	return;
 put_device:
	mutex_unlock(&devcd->mutex);
	cancel_delayed_work_sync(&devcd->del_wk);
	put_device(&devcd->devcd_dev);

 put_module:
	module_put(owner);
 free:
	free(data);
}
EXPORT_SYMBOL_GPL(dev_coredumpm_timeout);

/**
 * dev_coredumpsg - create device coredump that uses scatterlist as data
 * parameter
 * @dev: the struct device for the crashed device
 * @table: the dump data
 * @datalen: length of the data
 * @gfp: allocation flags
 *
 * Creates a new device coredump for the given device. If a previous one hasn't
 * been read yet, the new coredump is discarded. The data lifetime is determined
 * by the device coredump framework and when it is no longer needed
 * it will free the data.
 */
void dev_coredumpsg(struct device *dev, struct scatterlist *table,
		    size_t datalen, gfp_t gfp)
{
	dev_coredumpm(dev, NULL, table, datalen, gfp, devcd_read_from_sgtable,
		      devcd_free_sgtable);
}
EXPORT_SYMBOL_GPL(dev_coredumpsg);

static int __init devcoredump_init(void)
{
	return class_register(&devcd_class);
}
__initcall(devcoredump_init);

static void __exit devcoredump_exit(void)
{
	class_for_each_device(&devcd_class, NULL, NULL, devcd_free);
	class_unregister(&devcd_class);
}
__exitcall(devcoredump_exit);
