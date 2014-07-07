/*
 * Created: Fri Jan 19 10:48:35 2001 by faith@acm.org
 *
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Author Rickard E. (Rik) Faith <faith@valinux.com>
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

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/drm_core.h>

unsigned int drm_debug = 0;	/* 1 to enable debug output */
EXPORT_SYMBOL(drm_debug);

unsigned int drm_rnodes = 0;	/* 1 to enable experimental render nodes API */
EXPORT_SYMBOL(drm_rnodes);

/* 1 to allow user space to request universal planes (experimental) */
unsigned int drm_universal_planes = 0;
EXPORT_SYMBOL(drm_universal_planes);

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
MODULE_PARM_DESC(rnodes, "Enable experimental render nodes API");
MODULE_PARM_DESC(vblankoffdelay, "Delay until vblank irq auto-disable [msecs]");
MODULE_PARM_DESC(timestamp_precision_usec, "Max. error on timestamps [usecs]");
MODULE_PARM_DESC(timestamp_monotonic, "Use monotonic timestamps");

module_param_named(debug, drm_debug, int, 0600);
module_param_named(rnodes, drm_rnodes, int, 0600);
module_param_named(universal_planes, drm_universal_planes, int, 0600);
module_param_named(vblankoffdelay, drm_vblank_offdelay, int, 0600);
module_param_named(timestamp_precision_usec, drm_timestamp_precision, int, 0600);
module_param_named(timestamp_monotonic, drm_timestamp_monotonic, int, 0600);

static DEFINE_SPINLOCK(drm_minor_lock);
struct idr drm_minors_idr;

struct class *drm_class;
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

void drm_ut_debug_printk(const char *function_name, const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	printk(KERN_DEBUG "[" DRM_NAME ":%s] %pV", function_name, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_ut_debug_printk);

struct drm_master *drm_master_create(struct drm_minor *minor)
{
	struct drm_master *master;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return NULL;

	kref_init(&master->refcount);
	spin_lock_init(&master->lock.spinlock);
	init_waitqueue_head(&master->lock.lock_queue);
	if (drm_ht_create(&master->magiclist, DRM_MAGIC_HASH_ORDER)) {
		kfree(master);
		return NULL;
	}
	INIT_LIST_HEAD(&master->magicfree);
	master->minor = minor;

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

	mutex_lock(&dev->struct_mutex);
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

	list_for_each_entry_safe(pt, next, &master->magicfree, head) {
		list_del(&pt->head);
		drm_ht_remove_item(&master->magiclist, &pt->hash_item);
		kfree(pt);
	}

	drm_ht_remove(&master->magiclist);

	mutex_unlock(&dev->struct_mutex);
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

	mutex_lock(&dev->master_mutex);
	if (file_priv->is_master)
		goto out_unlock;

	if (file_priv->minor->master) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!file_priv->master) {
		ret = -EINVAL;
		goto out_unlock;
	}

	file_priv->minor->master = drm_master_get(file_priv->master);
	file_priv->is_master = 1;
	if (dev->driver->master_set) {
		ret = dev->driver->master_set(dev, file_priv, false);
		if (unlikely(ret != 0)) {
			file_priv->is_master = 0;
			drm_master_put(&file_priv->minor->master);
		}
	}

out_unlock:
	mutex_unlock(&dev->master_mutex);
	return ret;
}

int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	int ret = -EINVAL;

	mutex_lock(&dev->master_mutex);
	if (!file_priv->is_master)
		goto out_unlock;

	if (!file_priv->minor->master)
		goto out_unlock;

	ret = 0;
	if (dev->driver->master_drop)
		dev->driver->master_drop(dev, file_priv, false);
	drm_master_put(&file_priv->minor->master);
	file_priv->is_master = 0;

out_unlock:
	mutex_unlock(&dev->master_mutex);
	return ret;
}

/*
 * DRM Minors
 * A DRM device can provide several char-dev interfaces on the DRM-Major. Each
 * of them is represented by a drm_minor object. Depending on the capabilities
 * of the device-driver, different interfaces are registered.
 *
 * Minors can be accessed via dev->$minor_name. This pointer is either
 * NULL or a valid drm_minor pointer and stays valid as long as the device is
 * valid. This means, DRM minors have the same life-time as the underlying
 * device. However, this doesn't mean that the minor is active. Minors are
 * registered and unregistered dynamically according to device-state.
 */

static struct drm_minor **drm_minor_get_slot(struct drm_device *dev,
					     unsigned int type)
{
	switch (type) {
	case DRM_MINOR_LEGACY:
		return &dev->primary;
	case DRM_MINOR_RENDER:
		return &dev->render;
	case DRM_MINOR_CONTROL:
		return &dev->control;
	default:
		return NULL;
	}
}

static int drm_minor_alloc(struct drm_device *dev, unsigned int type)
{
	struct drm_minor *minor;

	minor = kzalloc(sizeof(*minor), GFP_KERNEL);
	if (!minor)
		return -ENOMEM;

	minor->type = type;
	minor->dev = dev;

	*drm_minor_get_slot(dev, type) = minor;
	return 0;
}

static void drm_minor_free(struct drm_device *dev, unsigned int type)
{
	struct drm_minor **slot;

	slot = drm_minor_get_slot(dev, type);
	if (*slot) {
		drm_mode_group_destroy(&(*slot)->mode_group);
		kfree(*slot);
		*slot = NULL;
	}
}

static int drm_minor_register(struct drm_device *dev, unsigned int type)
{
	struct drm_minor *new_minor;
	unsigned long flags;
	int ret;
	int minor_id;

	DRM_DEBUG("\n");

	new_minor = *drm_minor_get_slot(dev, type);
	if (!new_minor)
		return 0;

	idr_preload(GFP_KERNEL);
	spin_lock_irqsave(&drm_minor_lock, flags);
	minor_id = idr_alloc(&drm_minors_idr,
			     NULL,
			     64 * type,
			     64 * (type + 1),
			     GFP_NOWAIT);
	spin_unlock_irqrestore(&drm_minor_lock, flags);
	idr_preload_end();

	if (minor_id < 0)
		return minor_id;

	new_minor->index = minor_id;

	ret = drm_debugfs_init(new_minor, minor_id, drm_debugfs_root);
	if (ret) {
		DRM_ERROR("DRM: Failed to initialize /sys/kernel/debug/dri.\n");
		goto err_id;
	}

	ret = drm_sysfs_device_add(new_minor);
	if (ret) {
		DRM_ERROR("DRM: Error sysfs_device_add.\n");
		goto err_debugfs;
	}

	/* replace NULL with @minor so lookups will succeed from now on */
	spin_lock_irqsave(&drm_minor_lock, flags);
	idr_replace(&drm_minors_idr, new_minor, new_minor->index);
	spin_unlock_irqrestore(&drm_minor_lock, flags);

	DRM_DEBUG("new minor assigned %d\n", minor_id);
	return 0;

err_debugfs:
	drm_debugfs_cleanup(new_minor);
err_id:
	spin_lock_irqsave(&drm_minor_lock, flags);
	idr_remove(&drm_minors_idr, minor_id);
	spin_unlock_irqrestore(&drm_minor_lock, flags);
	new_minor->index = 0;
	return ret;
}

static void drm_minor_unregister(struct drm_device *dev, unsigned int type)
{
	struct drm_minor *minor;
	unsigned long flags;

	minor = *drm_minor_get_slot(dev, type);
	if (!minor || !minor->kdev)
		return;

	spin_lock_irqsave(&drm_minor_lock, flags);
	idr_remove(&drm_minors_idr, minor->index);
	spin_unlock_irqrestore(&drm_minor_lock, flags);
	minor->index = 0;

	drm_debugfs_cleanup(minor);
	drm_sysfs_device_remove(minor);
}

/**
 * drm_minor_acquire - Acquire a DRM minor
 * @minor_id: Minor ID of the DRM-minor
 *
 * Looks up the given minor-ID and returns the respective DRM-minor object. The
 * refence-count of the underlying device is increased so you must release this
 * object with drm_minor_release().
 *
 * As long as you hold this minor, it is guaranteed that the object and the
 * minor->dev pointer will stay valid! However, the device may get unplugged and
 * unregistered while you hold the minor.
 *
 * Returns:
 * Pointer to minor-object with increased device-refcount, or PTR_ERR on
 * failure.
 */
struct drm_minor *drm_minor_acquire(unsigned int minor_id)
{
	struct drm_minor *minor;
	unsigned long flags;

	spin_lock_irqsave(&drm_minor_lock, flags);
	minor = idr_find(&drm_minors_idr, minor_id);
	if (minor)
		drm_dev_ref(minor->dev);
	spin_unlock_irqrestore(&drm_minor_lock, flags);

	if (!minor) {
		return ERR_PTR(-ENODEV);
	} else if (drm_device_is_unplugged(minor->dev)) {
		drm_dev_unref(minor->dev);
		return ERR_PTR(-ENODEV);
	}

	return minor;
}

/**
 * drm_minor_release - Release DRM minor
 * @minor: Pointer to DRM minor object
 *
 * Release a minor that was previously acquired via drm_minor_acquire().
 */
void drm_minor_release(struct drm_minor *minor)
{
	drm_dev_unref(minor->dev);
}

/**
 * drm_put_dev - Unregister and release a DRM device
 * @dev: DRM device
 *
 * Called at module unload time or when a PCI device is unplugged.
 *
 * Use of this function is discouraged. It will eventually go away completely.
 * Please use drm_dev_unregister() and drm_dev_unref() explicitly instead.
 *
 * Cleans up all DRM device, calling drm_lastclose().
 */
void drm_put_dev(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_dev_unregister(dev);
	drm_dev_unref(dev);
}
EXPORT_SYMBOL(drm_put_dev);

void drm_unplug_dev(struct drm_device *dev)
{
	/* for a USB device */
	drm_minor_unregister(dev, DRM_MINOR_LEGACY);
	drm_minor_unregister(dev, DRM_MINOR_RENDER);
	drm_minor_unregister(dev, DRM_MINOR_CONTROL);

	mutex_lock(&drm_global_mutex);

	drm_device_set_unplugged(dev);

	if (dev->open_count == 0) {
		drm_put_dev(dev);
	}
	mutex_unlock(&drm_global_mutex);
}
EXPORT_SYMBOL(drm_unplug_dev);

/*
 * DRM internal mount
 * We want to be able to allocate our own "struct address_space" to control
 * memory-mappings in VRAM (or stolen RAM, ...). However, core MM does not allow
 * stand-alone address_space objects, so we need an underlying inode. As there
 * is no way to allocate an independent inode easily, we need a fake internal
 * VFS mount-point.
 *
 * The drm_fs_inode_new() function allocates a new inode, drm_fs_inode_free()
 * frees it again. You are allowed to use iget() and iput() to get references to
 * the inode. But each drm_fs_inode_new() call must be paired with exactly one
 * drm_fs_inode_free() call (which does not have to be the last iput()).
 * We use drm_fs_inode_*() to manage our internal VFS mount-point and share it
 * between multiple inode-users. You could, technically, call
 * iget() + drm_fs_inode_free() directly after alloc and sometime later do an
 * iput(), but this way you'd end up with a new vfsmount for each inode.
 */

static int drm_fs_cnt;
static struct vfsmount *drm_fs_mnt;

static const struct dentry_operations drm_fs_dops = {
	.d_dname	= simple_dname,
};

static const struct super_operations drm_fs_sops = {
	.statfs		= simple_statfs,
};

static struct dentry *drm_fs_mount(struct file_system_type *fs_type, int flags,
				   const char *dev_name, void *data)
{
	return mount_pseudo(fs_type,
			    "drm:",
			    &drm_fs_sops,
			    &drm_fs_dops,
			    0x010203ff);
}

static struct file_system_type drm_fs_type = {
	.name		= "drm",
	.owner		= THIS_MODULE,
	.mount		= drm_fs_mount,
	.kill_sb	= kill_anon_super,
};

static struct inode *drm_fs_inode_new(void)
{
	struct inode *inode;
	int r;

	r = simple_pin_fs(&drm_fs_type, &drm_fs_mnt, &drm_fs_cnt);
	if (r < 0) {
		DRM_ERROR("Cannot mount pseudo fs: %d\n", r);
		return ERR_PTR(r);
	}

	inode = alloc_anon_inode(drm_fs_mnt->mnt_sb);
	if (IS_ERR(inode))
		simple_release_fs(&drm_fs_mnt, &drm_fs_cnt);

	return inode;
}

static void drm_fs_inode_free(struct inode *inode)
{
	if (inode) {
		iput(inode);
		simple_release_fs(&drm_fs_mnt, &drm_fs_cnt);
	}
}

/**
 * drm_dev_alloc - Allocate new DRM device
 * @driver: DRM driver to allocate device for
 * @parent: Parent device object
 *
 * Allocate and initialize a new DRM device. No device registration is done.
 * Call drm_dev_register() to advertice the device to user space and register it
 * with other core subsystems.
 *
 * The initial ref-count of the object is 1. Use drm_dev_ref() and
 * drm_dev_unref() to take and drop further ref-counts.
 *
 * RETURNS:
 * Pointer to new DRM device, or NULL if out of memory.
 */
struct drm_device *drm_dev_alloc(struct drm_driver *driver,
				 struct device *parent)
{
	struct drm_device *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	kref_init(&dev->ref);
	dev->dev = parent;
	dev->driver = driver;

	INIT_LIST_HEAD(&dev->filelist);
	INIT_LIST_HEAD(&dev->ctxlist);
	INIT_LIST_HEAD(&dev->vmalist);
	INIT_LIST_HEAD(&dev->maplist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	spin_lock_init(&dev->buf_lock);
	spin_lock_init(&dev->event_lock);
	mutex_init(&dev->struct_mutex);
	mutex_init(&dev->ctxlist_mutex);
	mutex_init(&dev->master_mutex);

	dev->anon_inode = drm_fs_inode_new();
	if (IS_ERR(dev->anon_inode)) {
		ret = PTR_ERR(dev->anon_inode);
		DRM_ERROR("Cannot allocate anonymous inode: %d\n", ret);
		goto err_free;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_minor_alloc(dev, DRM_MINOR_CONTROL);
		if (ret)
			goto err_minors;
	}

	if (drm_core_check_feature(dev, DRIVER_RENDER) && drm_rnodes) {
		ret = drm_minor_alloc(dev, DRM_MINOR_RENDER);
		if (ret)
			goto err_minors;
	}

	ret = drm_minor_alloc(dev, DRM_MINOR_LEGACY);
	if (ret)
		goto err_minors;

	if (drm_ht_create(&dev->map_hash, 12))
		goto err_minors;

	ret = drm_ctxbitmap_init(dev);
	if (ret) {
		DRM_ERROR("Cannot allocate memory for context bitmap.\n");
		goto err_ht;
	}

	if (driver->driver_features & DRIVER_GEM) {
		ret = drm_gem_init(dev);
		if (ret) {
			DRM_ERROR("Cannot initialize graphics execution manager (GEM)\n");
			goto err_ctxbitmap;
		}
	}

	return dev;

err_ctxbitmap:
	drm_ctxbitmap_cleanup(dev);
err_ht:
	drm_ht_remove(&dev->map_hash);
err_minors:
	drm_minor_free(dev, DRM_MINOR_LEGACY);
	drm_minor_free(dev, DRM_MINOR_RENDER);
	drm_minor_free(dev, DRM_MINOR_CONTROL);
	drm_fs_inode_free(dev->anon_inode);
err_free:
	mutex_destroy(&dev->master_mutex);
	kfree(dev);
	return NULL;
}
EXPORT_SYMBOL(drm_dev_alloc);

static void drm_dev_release(struct kref *ref)
{
	struct drm_device *dev = container_of(ref, struct drm_device, ref);

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_destroy(dev);

	drm_ctxbitmap_cleanup(dev);
	drm_ht_remove(&dev->map_hash);
	drm_fs_inode_free(dev->anon_inode);

	drm_minor_free(dev, DRM_MINOR_LEGACY);
	drm_minor_free(dev, DRM_MINOR_RENDER);
	drm_minor_free(dev, DRM_MINOR_CONTROL);

	mutex_destroy(&dev->master_mutex);
	kfree(dev->unique);
	kfree(dev);
}

/**
 * drm_dev_ref - Take reference of a DRM device
 * @dev: device to take reference of or NULL
 *
 * This increases the ref-count of @dev by one. You *must* already own a
 * reference when calling this. Use drm_dev_unref() to drop this reference
 * again.
 *
 * This function never fails. However, this function does not provide *any*
 * guarantee whether the device is alive or running. It only provides a
 * reference to the object and the memory associated with it.
 */
void drm_dev_ref(struct drm_device *dev)
{
	if (dev)
		kref_get(&dev->ref);
}
EXPORT_SYMBOL(drm_dev_ref);

/**
 * drm_dev_unref - Drop reference of a DRM device
 * @dev: device to drop reference of or NULL
 *
 * This decreases the ref-count of @dev by one. The device is destroyed if the
 * ref-count drops to zero.
 */
void drm_dev_unref(struct drm_device *dev)
{
	if (dev)
		kref_put(&dev->ref, drm_dev_release);
}
EXPORT_SYMBOL(drm_dev_unref);

/**
 * drm_dev_register - Register DRM device
 * @dev: Device to register
 * @flags: Flags passed to the driver's .load() function
 *
 * Register the DRM device @dev with the system, advertise device to user-space
 * and start normal device operation. @dev must be allocated via drm_dev_alloc()
 * previously.
 *
 * Never call this twice on any device!
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
int drm_dev_register(struct drm_device *dev, unsigned long flags)
{
	int ret;

	mutex_lock(&drm_global_mutex);

	ret = drm_minor_register(dev, DRM_MINOR_CONTROL);
	if (ret)
		goto err_minors;

	ret = drm_minor_register(dev, DRM_MINOR_RENDER);
	if (ret)
		goto err_minors;

	ret = drm_minor_register(dev, DRM_MINOR_LEGACY);
	if (ret)
		goto err_minors;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, flags);
		if (ret)
			goto err_minors;
	}

	/* setup grouping for legacy outputs */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_mode_group_init_legacy_group(dev,
				&dev->primary->mode_group);
		if (ret)
			goto err_unload;
	}

	ret = 0;
	goto out_unlock;

err_unload:
	if (dev->driver->unload)
		dev->driver->unload(dev);
err_minors:
	drm_minor_unregister(dev, DRM_MINOR_LEGACY);
	drm_minor_unregister(dev, DRM_MINOR_RENDER);
	drm_minor_unregister(dev, DRM_MINOR_CONTROL);
out_unlock:
	mutex_unlock(&drm_global_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_dev_register);

/**
 * drm_dev_unregister - Unregister DRM device
 * @dev: Device to unregister
 *
 * Unregister the DRM device from the system. This does the reverse of
 * drm_dev_register() but does not deallocate the device. The caller must call
 * drm_dev_unref() to drop their final reference.
 */
void drm_dev_unregister(struct drm_device *dev)
{
	struct drm_map_list *r_list, *list_temp;

	drm_lastclose(dev);

	if (dev->driver->unload)
		dev->driver->unload(dev);

	if (dev->agp)
		drm_pci_agp_destroy(dev);

	drm_vblank_cleanup(dev);

	list_for_each_entry_safe(r_list, list_temp, &dev->maplist, head)
		drm_rmmap(dev, r_list->map);

	drm_minor_unregister(dev, DRM_MINOR_LEGACY);
	drm_minor_unregister(dev, DRM_MINOR_RENDER);
	drm_minor_unregister(dev, DRM_MINOR_CONTROL);
}
EXPORT_SYMBOL(drm_dev_unregister);

/**
 * drm_dev_set_unique - Set the unique name of a DRM device
 * @dev: device of which to set the unique name
 * @fmt: format string for unique name
 *
 * Sets the unique name of a DRM device using the specified format string and
 * a variable list of arguments. Drivers can use this at driver probe time if
 * the unique name of the devices they drive is static.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_dev_set_unique(struct drm_device *dev, const char *fmt, ...)
{
	va_list ap;

	kfree(dev->unique);

	va_start(ap, fmt);
	dev->unique = kvasprintf(GFP_KERNEL, fmt, ap);
	va_end(ap);

	return dev->unique ? 0 : -ENOMEM;
}
EXPORT_SYMBOL(drm_dev_set_unique);
