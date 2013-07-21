/**
 * \file drm_stub.h
 * Stub support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 */

/*
 * Created: Fri Jan 19 10:48:35 2001 by faith@acm.org
 *
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/drm_core.h>

unsigned int drm_debug = 0;	/* 1 to enable debug output */
EXPORT_SYMBOL(drm_debug);

unsigned int drm_vblank_offdelay = 5000;    /* Default to 5000 msecs. */
EXPORT_SYMBOL(drm_vblank_offdelay);

unsigned int drm_timestamp_precision = 20;  /* Default to 20 usecs. */
EXPORT_SYMBOL(drm_timestamp_precision);

/*
 * Default to use monotonic timestamps for wait-for-vblank and page-flip
 * complete events.
 */
unsigned int drm_timestamp_monotonic = 1;

MODULE_AUTHOR(CORE_AUTHOR);
MODULE_DESCRIPTION(CORE_DESC);
MODULE_LICENSE("GPL and additional rights");
MODULE_PARM_DESC(debug, "Enable debug output");
MODULE_PARM_DESC(vblankoffdelay, "Delay until vblank irq auto-disable [msecs]");
MODULE_PARM_DESC(timestamp_precision_usec, "Max. error on timestamps [usecs]");
MODULE_PARM_DESC(timestamp_monotonic, "Use monotonic timestamps");

module_param_named(debug, drm_debug, int, 0600);
module_param_named(vblankoffdelay, drm_vblank_offdelay, int, 0600);
module_param_named(timestamp_precision_usec, drm_timestamp_precision, int, 0600);
module_param_named(timestamp_monotonic, drm_timestamp_monotonic, int, 0600);

struct idr drm_minors_idr;

struct class *drm_class;
struct proc_dir_entry *drm_proc_root;
struct dentry *drm_debugfs_root;

int drm_err(const char *func, const char *format, ...)
{
	struct va_format vaf;
	va_list args;
	int r;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	r = printk(KERN_ERR "[" DRM_NAME ":%s] *ERROR* %pV", func, &vaf);

	va_end(args);

	return r;
}
EXPORT_SYMBOL(drm_err);

void drm_ut_debug_printk(unsigned int request_level,
			 const char *prefix,
			 const char *function_name,
			 const char *format, ...)
{
	va_list args;

	if (drm_debug & request_level) {
		if (function_name)
			printk(KERN_DEBUG "[%s:%s], ", prefix, function_name);
		va_start(args, format);
		vprintk(format, args);
		va_end(args);
	}
}
EXPORT_SYMBOL(drm_ut_debug_printk);

static int drm_minor_get_id(struct drm_device *dev, int type)
{
	int ret;
	int base = 0, limit = 63;

	if (type == DRM_MINOR_CONTROL) {
                base += 64;
                limit = base + 127;
        } else if (type == DRM_MINOR_RENDER) {
                base += 128;
                limit = base + 255;
        }

	mutex_lock(&dev->struct_mutex);
	ret = idr_alloc(&drm_minors_idr, NULL, base, limit, GFP_KERNEL);
	mutex_unlock(&dev->struct_mutex);

	return ret == -ENOSPC ? -EINVAL : ret;
}

struct drm_master *drm_master_create(struct drm_minor *minor)
{
	struct drm_master *master;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return NULL;

	kref_init(&master->refcount);
	spin_lock_init(&master->lock.spinlock);
	init_waitqueue_head(&master->lock.lock_queue);
	drm_ht_create(&master->magiclist, DRM_MAGIC_HASH_ORDER);
	INIT_LIST_HEAD(&master->magicfree);
	master->minor = minor;

	list_add_tail(&master->head, &minor->master_list);

	return master;
}

struct drm_master *drm_master_get(struct drm_master *master)
{
	kref_get(&master->refcount);
	return master;
}
EXPORT_SYMBOL(drm_master_get);

static void drm_master_destroy(struct kref *kref)
{
	struct drm_master *master = container_of(kref, struct drm_master, refcount);
	struct drm_magic_entry *pt, *next;
	struct drm_device *dev = master->minor->dev;
	struct drm_map_list *r_list, *list_temp;

	list_del(&master->head);

	if (dev->driver->master_destroy)
		dev->driver->master_destroy(dev, master);

	list_for_each_entry_safe(r_list, list_temp, &dev->maplist, head) {
		if (r_list->master == master) {
			drm_rmmap_locked(dev, r_list->map);
			r_list = NULL;
		}
	}

	if (master->unique) {
		kfree(master->unique);
		master->unique = NULL;
		master->unique_len = 0;
	}

	kfree(dev->devname);
	dev->devname = NULL;

	list_for_each_entry_safe(pt, next, &master->magicfree, head) {
		list_del(&pt->head);
		drm_ht_remove_item(&master->magiclist, &pt->hash_item);
		kfree(pt);
	}

	drm_ht_remove(&master->magiclist);

	kfree(master);
}

void drm_master_put(struct drm_master **master)
{
	kref_put(&(*master)->refcount, drm_master_destroy);
	*master = NULL;
}
EXPORT_SYMBOL(drm_master_put);

int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	int ret = 0;

	if (file_priv->is_master)
		return 0;

	if (file_priv->minor->master && file_priv->minor->master != file_priv->master)
		return -EINVAL;

	if (!file_priv->master)
		return -EINVAL;

	if (file_priv->minor->master)
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	file_priv->minor->master = drm_master_get(file_priv->master);
	file_priv->is_master = 1;
	if (dev->driver->master_set) {
		ret = dev->driver->master_set(dev, file_priv, false);
		if (unlikely(ret != 0)) {
			file_priv->is_master = 0;
			drm_master_put(&file_priv->minor->master);
		}
	}
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	if (!file_priv->is_master)
		return -EINVAL;

	if (!file_priv->minor->master)
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	if (dev->driver->master_drop)
		dev->driver->master_drop(dev, file_priv, false);
	drm_master_put(&file_priv->minor->master);
	file_priv->is_master = 0;
	mutex_unlock(&dev->struct_mutex);
	return 0;
}

int drm_fill_in_dev(struct drm_device *dev,
			   const struct pci_device_id *ent,
			   struct drm_driver *driver)
{
	int retcode;

	INIT_LIST_HEAD(&dev->filelist);
	INIT_LIST_HEAD(&dev->ctxlist);
	INIT_LIST_HEAD(&dev->vmalist);
	INIT_LIST_HEAD(&dev->maplist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	spin_lock_init(&dev->count_lock);
	spin_lock_init(&dev->event_lock);
	mutex_init(&dev->struct_mutex);
	mutex_init(&dev->ctxlist_mutex);

	if (drm_ht_create(&dev->map_hash, 12)) {
		return -ENOMEM;
	}

	/* the DRM has 6 basic counters */
	dev->counters = 6;
	dev->types[0] = _DRM_STAT_LOCK;
	dev->types[1] = _DRM_STAT_OPENS;
	dev->types[2] = _DRM_STAT_CLOSES;
	dev->types[3] = _DRM_STAT_IOCTLS;
	dev->types[4] = _DRM_STAT_LOCKS;
	dev->types[5] = _DRM_STAT_UNLOCKS;

	dev->driver = driver;

	if (dev->driver->bus->agp_init) {
		retcode = dev->driver->bus->agp_init(dev);
		if (retcode)
			goto error_out_unreg;
	}



	retcode = drm_ctxbitmap_init(dev);
	if (retcode) {
		DRM_ERROR("Cannot allocate memory for context bitmap.\n");
		goto error_out_unreg;
	}

	if (driver->driver_features & DRIVER_GEM) {
		retcode = drm_gem_init(dev);
		if (retcode) {
			DRM_ERROR("Cannot initialize graphics execution "
				  "manager (GEM)\n");
			goto error_out_unreg;
		}
	}

	return 0;

      error_out_unreg:
	drm_lastclose(dev);
	return retcode;
}
EXPORT_SYMBOL(drm_fill_in_dev);


/**
 * Get a secondary minor number.
 *
 * \param dev device data structure
 * \param sec-minor structure to hold the assigned minor
 * \return negative number on failure.
 *
 * Search an empty entry and initialize it to the given parameters, and
 * create the proc init entry via proc_init(). This routines assigns
 * minor numbers to secondary heads of multi-headed cards
 */
int drm_get_minor(struct drm_device *dev, struct drm_minor **minor, int type)
{
	struct drm_minor *new_minor;
	int ret;
	int minor_id;

	DRM_DEBUG("\n");

	minor_id = drm_minor_get_id(dev, type);
	if (minor_id < 0)
		return minor_id;

	new_minor = kzalloc(sizeof(struct drm_minor), GFP_KERNEL);
	if (!new_minor) {
		ret = -ENOMEM;
		goto err_idr;
	}

	new_minor->type = type;
	new_minor->device = MKDEV(DRM_MAJOR, minor_id);
	new_minor->dev = dev;
	new_minor->index = minor_id;
	INIT_LIST_HEAD(&new_minor->master_list);

	idr_replace(&drm_minors_idr, new_minor, minor_id);

	if (type == DRM_MINOR_LEGACY) {
		ret = drm_proc_init(new_minor, drm_proc_root);
		if (ret) {
			DRM_ERROR("DRM: Failed to initialize /proc/dri.\n");
			goto err_mem;
		}
	} else
		new_minor->proc_root = NULL;

#if defined(CONFIG_DEBUG_FS)
	ret = drm_debugfs_init(new_minor, minor_id, drm_debugfs_root);
	if (ret) {
		DRM_ERROR("DRM: Failed to initialize /sys/kernel/debug/dri.\n");
		goto err_g2;
	}
#endif

	ret = drm_sysfs_device_add(new_minor);
	if (ret) {
		printk(KERN_ERR
		       "DRM: Error sysfs_device_add.\n");
		goto err_g2;
	}
	*minor = new_minor;

	DRM_DEBUG("new minor assigned %d\n", minor_id);
	return 0;


err_g2:
	if (new_minor->type == DRM_MINOR_LEGACY)
		drm_proc_cleanup(new_minor, drm_proc_root);
err_mem:
	kfree(new_minor);
err_idr:
	idr_remove(&drm_minors_idr, minor_id);
	*minor = NULL;
	return ret;
}
EXPORT_SYMBOL(drm_get_minor);

/**
 * Put a secondary minor number.
 *
 * \param sec_minor - structure to be released
 * \return always zero
 *
 * Cleans up the proc resources. Not legal for this to be the
 * last minor released.
 *
 */
int drm_put_minor(struct drm_minor **minor_p)
{
	struct drm_minor *minor = *minor_p;

	DRM_DEBUG("release secondary minor %d\n", minor->index);

	if (minor->type == DRM_MINOR_LEGACY)
		drm_proc_cleanup(minor, drm_proc_root);
#if defined(CONFIG_DEBUG_FS)
	drm_debugfs_cleanup(minor);
#endif

	drm_sysfs_device_remove(minor);

	idr_remove(&drm_minors_idr, minor->index);

	kfree(minor);
	*minor_p = NULL;
	return 0;
}
EXPORT_SYMBOL(drm_put_minor);

static void drm_unplug_minor(struct drm_minor *minor)
{
	drm_sysfs_device_remove(minor);
}

/**
 * Called via drm_exit() at module unload time or when pci device is
 * unplugged.
 *
 * Cleans up all DRM device, calling drm_lastclose().
 *
 */
void drm_put_dev(struct drm_device *dev)
{
	struct drm_driver *driver;
	struct drm_map_list *r_list, *list_temp;

	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}
	driver = dev->driver;

	drm_lastclose(dev);

	if (drm_core_has_MTRR(dev) && drm_core_has_AGP(dev) && dev->agp)
		arch_phys_wc_del(dev->agp->agp_mtrr);

	if (dev->driver->unload)
		dev->driver->unload(dev);

	if (drm_core_has_AGP(dev) && dev->agp) {
		kfree(dev->agp);
		dev->agp = NULL;
	}

	drm_vblank_cleanup(dev);

	list_for_each_entry_safe(r_list, list_temp, &dev->maplist, head)
		drm_rmmap(dev, r_list->map);
	drm_ht_remove(&dev->map_hash);

	drm_ctxbitmap_cleanup(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_put_minor(&dev->control);

	if (driver->driver_features & DRIVER_GEM)
		drm_gem_destroy(dev);

	drm_put_minor(&dev->primary);

	list_del(&dev->driver_item);
	kfree(dev->devname);
	kfree(dev);
}
EXPORT_SYMBOL(drm_put_dev);

void drm_unplug_dev(struct drm_device *dev)
{
	/* for a USB device */
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_unplug_minor(dev->control);
	drm_unplug_minor(dev->primary);

	mutex_lock(&drm_global_mutex);

	drm_device_set_unplugged(dev);

	if (dev->open_count == 0) {
		drm_put_dev(dev);
	}
	mutex_unlock(&drm_global_mutex);
}
EXPORT_SYMBOL(drm_unplug_dev);
