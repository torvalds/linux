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

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/srcu.h>

#include <drm/drm_client.h>
#include <drm/drm_drv.h>
#include <drm/drmP.h>

#include "drm_crtc_internal.h"
#include "drm_legacy.h"
#include "drm_internal.h"

/*
 * drm_debug: Enable debug output.
 * Bitmask of DRM_UT_x. See include/drm/drmP.h for details.
 */
unsigned int drm_debug = 0;
EXPORT_SYMBOL(drm_debug);

MODULE_AUTHOR("Gareth Hughes, Leif Delgass, José Fonseca, Jon Smirl");
MODULE_DESCRIPTION("DRM shared core routines");
MODULE_LICENSE("GPL and additional rights");
MODULE_PARM_DESC(debug, "Enable debug output, where each bit enables a debug category.\n"
"\t\tBit 0 (0x01)  will enable CORE messages (drm core code)\n"
"\t\tBit 1 (0x02)  will enable DRIVER messages (drm controller code)\n"
"\t\tBit 2 (0x04)  will enable KMS messages (modesetting code)\n"
"\t\tBit 3 (0x08)  will enable PRIME messages (prime code)\n"
"\t\tBit 4 (0x10)  will enable ATOMIC messages (atomic code)\n"
"\t\tBit 5 (0x20)  will enable VBL messages (vblank code)\n"
"\t\tBit 7 (0x80)  will enable LEASE messages (leasing code)\n"
"\t\tBit 8 (0x100) will enable DP messages (displayport code)");
module_param_named(debug, drm_debug, int, 0600);

static DEFINE_SPINLOCK(drm_minor_lock);
static struct idr drm_minors_idr;

/*
 * If the drm core fails to init for whatever reason,
 * we should prevent any drivers from registering with it.
 * It's best to check this at drm_dev_init(), as some drivers
 * prefer to embed struct drm_device into their own device
 * structure and call drm_dev_init() themselves.
 */
static bool drm_core_init_complete = false;

static struct dentry *drm_debugfs_root;

DEFINE_STATIC_SRCU(drm_unplug_srcu);

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
	case DRM_MINOR_PRIMARY:
		return &dev->primary;
	case DRM_MINOR_RENDER:
		return &dev->render;
	default:
		BUG();
	}
}

static int drm_minor_alloc(struct drm_device *dev, unsigned int type)
{
	struct drm_minor *minor;
	unsigned long flags;
	int r;

	minor = kzalloc(sizeof(*minor), GFP_KERNEL);
	if (!minor)
		return -ENOMEM;

	minor->type = type;
	minor->dev = dev;

	idr_preload(GFP_KERNEL);
	spin_lock_irqsave(&drm_minor_lock, flags);
	r = idr_alloc(&drm_minors_idr,
		      NULL,
		      64 * type,
		      64 * (type + 1),
		      GFP_NOWAIT);
	spin_unlock_irqrestore(&drm_minor_lock, flags);
	idr_preload_end();

	if (r < 0)
		goto err_free;

	minor->index = r;

	minor->kdev = drm_sysfs_minor_alloc(minor);
	if (IS_ERR(minor->kdev)) {
		r = PTR_ERR(minor->kdev);
		goto err_index;
	}

	*drm_minor_get_slot(dev, type) = minor;
	return 0;

err_index:
	spin_lock_irqsave(&drm_minor_lock, flags);
	idr_remove(&drm_minors_idr, minor->index);
	spin_unlock_irqrestore(&drm_minor_lock, flags);
err_free:
	kfree(minor);
	return r;
}

static void drm_minor_free(struct drm_device *dev, unsigned int type)
{
	struct drm_minor **slot, *minor;
	unsigned long flags;

	slot = drm_minor_get_slot(dev, type);
	minor = *slot;
	if (!minor)
		return;

	put_device(minor->kdev);

	spin_lock_irqsave(&drm_minor_lock, flags);
	idr_remove(&drm_minors_idr, minor->index);
	spin_unlock_irqrestore(&drm_minor_lock, flags);

	kfree(minor);
	*slot = NULL;
}

static int drm_minor_register(struct drm_device *dev, unsigned int type)
{
	struct drm_minor *minor;
	unsigned long flags;
	int ret;

	DRM_DEBUG("\n");

	minor = *drm_minor_get_slot(dev, type);
	if (!minor)
		return 0;

	ret = drm_debugfs_init(minor, minor->index, drm_debugfs_root);
	if (ret) {
		DRM_ERROR("DRM: Failed to initialize /sys/kernel/debug/dri.\n");
		goto err_debugfs;
	}

	ret = device_add(minor->kdev);
	if (ret)
		goto err_debugfs;

	/* replace NULL with @minor so lookups will succeed from now on */
	spin_lock_irqsave(&drm_minor_lock, flags);
	idr_replace(&drm_minors_idr, minor, minor->index);
	spin_unlock_irqrestore(&drm_minor_lock, flags);

	DRM_DEBUG("new minor registered %d\n", minor->index);
	return 0;

err_debugfs:
	drm_debugfs_cleanup(minor);
	return ret;
}

static void drm_minor_unregister(struct drm_device *dev, unsigned int type)
{
	struct drm_minor *minor;
	unsigned long flags;

	minor = *drm_minor_get_slot(dev, type);
	if (!minor || !device_is_registered(minor->kdev))
		return;

	/* replace @minor with NULL so lookups will fail from now on */
	spin_lock_irqsave(&drm_minor_lock, flags);
	idr_replace(&drm_minors_idr, NULL, minor->index);
	spin_unlock_irqrestore(&drm_minor_lock, flags);

	device_del(minor->kdev);
	dev_set_drvdata(minor->kdev, NULL); /* safety belt */
	drm_debugfs_cleanup(minor);
}

/*
 * Looks up the given minor-ID and returns the respective DRM-minor object. The
 * refence-count of the underlying device is increased so you must release this
 * object with drm_minor_release().
 *
 * As long as you hold this minor, it is guaranteed that the object and the
 * minor->dev pointer will stay valid! However, the device may get unplugged and
 * unregistered while you hold the minor.
 */
struct drm_minor *drm_minor_acquire(unsigned int minor_id)
{
	struct drm_minor *minor;
	unsigned long flags;

	spin_lock_irqsave(&drm_minor_lock, flags);
	minor = idr_find(&drm_minors_idr, minor_id);
	if (minor)
		drm_dev_get(minor->dev);
	spin_unlock_irqrestore(&drm_minor_lock, flags);

	if (!minor) {
		return ERR_PTR(-ENODEV);
	} else if (drm_dev_is_unplugged(minor->dev)) {
		drm_dev_put(minor->dev);
		return ERR_PTR(-ENODEV);
	}

	return minor;
}

void drm_minor_release(struct drm_minor *minor)
{
	drm_dev_put(minor->dev);
}

/**
 * DOC: driver instance overview
 *
 * A device instance for a drm driver is represented by &struct drm_device. This
 * is initialized with drm_dev_init(), usually from bus-specific ->probe()
 * callbacks implemented by the driver. The driver then needs to initialize all
 * the various subsystems for the drm device like memory management, vblank
 * handling, modesetting support and intial output configuration plus obviously
 * initialize all the corresponding hardware bits. Finally when everything is up
 * and running and ready for userspace the device instance can be published
 * using drm_dev_register().
 *
 * There is also deprecated support for initalizing device instances using
 * bus-specific helpers and the &drm_driver.load callback. But due to
 * backwards-compatibility needs the device instance have to be published too
 * early, which requires unpretty global locking to make safe and is therefore
 * only support for existing drivers not yet converted to the new scheme.
 *
 * When cleaning up a device instance everything needs to be done in reverse:
 * First unpublish the device instance with drm_dev_unregister(). Then clean up
 * any other resources allocated at device initialization and drop the driver's
 * reference to &drm_device using drm_dev_put().
 *
 * Note that the lifetime rules for &drm_device instance has still a lot of
 * historical baggage. Hence use the reference counting provided by
 * drm_dev_get() and drm_dev_put() only carefully.
 *
 * Display driver example
 * ~~~~~~~~~~~~~~~~~~~~~~
 *
 * The following example shows a typical structure of a DRM display driver.
 * The example focus on the probe() function and the other functions that is
 * almost always present and serves as a demonstration of devm_drm_dev_init()
 * usage with its accompanying drm_driver->release callback.
 *
 * .. code-block:: c
 *
 *	struct driver_device {
 *		struct drm_device drm;
 *		void *userspace_facing;
 *		struct clk *pclk;
 *	};
 *
 *	static void driver_drm_release(struct drm_device *drm)
 *	{
 *		struct driver_device *priv = container_of(...);
 *
 *		drm_mode_config_cleanup(drm);
 *		drm_dev_fini(drm);
 *		kfree(priv->userspace_facing);
 *		kfree(priv);
 *	}
 *
 *	static struct drm_driver driver_drm_driver = {
 *		[...]
 *		.release = driver_drm_release,
 *	};
 *
 *	static int driver_probe(struct platform_device *pdev)
 *	{
 *		struct driver_device *priv;
 *		struct drm_device *drm;
 *		int ret;
 *
 *		[
 *		  devm_kzalloc() can't be used here because the drm_device
 *		  lifetime can exceed the device lifetime if driver unbind
 *		  happens when userspace still has open file descriptors.
 *		]
 *		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
 *		if (!priv)
 *			return -ENOMEM;
 *
 *		drm = &priv->drm;
 *
 *		ret = devm_drm_dev_init(&pdev->dev, drm, &driver_drm_driver);
 *		if (ret) {
 *			kfree(drm);
 *			return ret;
 *		}
 *
 *		drm_mode_config_init(drm);
 *
 *		priv->userspace_facing = kzalloc(..., GFP_KERNEL);
 *		if (!priv->userspace_facing)
 *			return -ENOMEM;
 *
 *		priv->pclk = devm_clk_get(dev, "PCLK");
 *		if (IS_ERR(priv->pclk))
 *			return PTR_ERR(priv->pclk);
 *
 *		[ Further setup, display pipeline etc ]
 *
 *		platform_set_drvdata(pdev, drm);
 *
 *		drm_mode_config_reset(drm);
 *
 *		ret = drm_dev_register(drm);
 *		if (ret)
 *			return ret;
 *
 *		drm_fbdev_generic_setup(drm, 32);
 *
 *		return 0;
 *	}
 *
 *	[ This function is called before the devm_ resources are released ]
 *	static int driver_remove(struct platform_device *pdev)
 *	{
 *		struct drm_device *drm = platform_get_drvdata(pdev);
 *
 *		drm_dev_unregister(drm);
 *		drm_atomic_helper_shutdown(drm)
 *
 *		return 0;
 *	}
 *
 *	[ This function is called on kernel restart and shutdown ]
 *	static void driver_shutdown(struct platform_device *pdev)
 *	{
 *		drm_atomic_helper_shutdown(platform_get_drvdata(pdev));
 *	}
 *
 *	static int __maybe_unused driver_pm_suspend(struct device *dev)
 *	{
 *		return drm_mode_config_helper_suspend(dev_get_drvdata(dev));
 *	}
 *
 *	static int __maybe_unused driver_pm_resume(struct device *dev)
 *	{
 *		drm_mode_config_helper_resume(dev_get_drvdata(dev));
 *
 *		return 0;
 *	}
 *
 *	static const struct dev_pm_ops driver_pm_ops = {
 *		SET_SYSTEM_SLEEP_PM_OPS(driver_pm_suspend, driver_pm_resume)
 *	};
 *
 *	static struct platform_driver driver_driver = {
 *		.driver = {
 *			[...]
 *			.pm = &driver_pm_ops,
 *		},
 *		.probe = driver_probe,
 *		.remove = driver_remove,
 *		.shutdown = driver_shutdown,
 *	};
 *	module_platform_driver(driver_driver);
 *
 * Drivers that want to support device unplugging (USB, DT overlay unload) should
 * use drm_dev_unplug() instead of drm_dev_unregister(). The driver must protect
 * regions that is accessing device resources to prevent use after they're
 * released. This is done using drm_dev_enter() and drm_dev_exit(). There is one
 * shortcoming however, drm_dev_unplug() marks the drm_device as unplugged before
 * drm_atomic_helper_shutdown() is called. This means that if the disable code
 * paths are protected, they will not run on regular driver module unload,
 * possibily leaving the hardware enabled.
 */

/**
 * drm_put_dev - Unregister and release a DRM device
 * @dev: DRM device
 *
 * Called at module unload time or when a PCI device is unplugged.
 *
 * Cleans up all DRM device, calling drm_lastclose().
 *
 * Note: Use of this function is deprecated. It will eventually go away
 * completely.  Please use drm_dev_unregister() and drm_dev_put() explicitly
 * instead to make sure that the device isn't userspace accessible any more
 * while teardown is in progress, ensuring that userspace can't access an
 * inconsistent state.
 */
void drm_put_dev(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_dev_unregister(dev);
	drm_dev_put(dev);
}
EXPORT_SYMBOL(drm_put_dev);

/**
 * drm_dev_enter - Enter device critical section
 * @dev: DRM device
 * @idx: Pointer to index that will be passed to the matching drm_dev_exit()
 *
 * This function marks and protects the beginning of a section that should not
 * be entered after the device has been unplugged. The section end is marked
 * with drm_dev_exit(). Calls to this function can be nested.
 *
 * Returns:
 * True if it is OK to enter the section, false otherwise.
 */
bool drm_dev_enter(struct drm_device *dev, int *idx)
{
	*idx = srcu_read_lock(&drm_unplug_srcu);

	if (dev->unplugged) {
		srcu_read_unlock(&drm_unplug_srcu, *idx);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(drm_dev_enter);

/**
 * drm_dev_exit - Exit device critical section
 * @idx: index returned from drm_dev_enter()
 *
 * This function marks the end of a section that should not be entered after
 * the device has been unplugged.
 */
void drm_dev_exit(int idx)
{
	srcu_read_unlock(&drm_unplug_srcu, idx);
}
EXPORT_SYMBOL(drm_dev_exit);

/**
 * drm_dev_unplug - unplug a DRM device
 * @dev: DRM device
 *
 * This unplugs a hotpluggable DRM device, which makes it inaccessible to
 * userspace operations. Entry-points can use drm_dev_enter() and
 * drm_dev_exit() to protect device resources in a race free manner. This
 * essentially unregisters the device like drm_dev_unregister(), but can be
 * called while there are still open users of @dev.
 */
void drm_dev_unplug(struct drm_device *dev)
{
	/*
	 * After synchronizing any critical read section is guaranteed to see
	 * the new value of ->unplugged, and any critical section which might
	 * still have seen the old value of ->unplugged is guaranteed to have
	 * finished.
	 */
	dev->unplugged = true;
	synchronize_srcu(&drm_unplug_srcu);

	drm_dev_unregister(dev);
}
EXPORT_SYMBOL(drm_dev_unplug);

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
 * DOC: component helper usage recommendations
 *
 * DRM drivers that drive hardware where a logical device consists of a pile of
 * independent hardware blocks are recommended to use the :ref:`component helper
 * library<component>`. For consistency and better options for code reuse the
 * following guidelines apply:
 *
 *  - The entire device initialization procedure should be run from the
 *    &component_master_ops.master_bind callback, starting with drm_dev_init(),
 *    then binding all components with component_bind_all() and finishing with
 *    drm_dev_register().
 *
 *  - The opaque pointer passed to all components through component_bind_all()
 *    should point at &struct drm_device of the device instance, not some driver
 *    specific private structure.
 *
 *  - The component helper fills the niche where further standardization of
 *    interfaces is not practical. When there already is, or will be, a
 *    standardized interface like &drm_bridge or &drm_panel, providing its own
 *    functions to find such components at driver load time, like
 *    drm_of_find_panel_or_bridge(), then the component helper should not be
 *    used.
 */

/**
 * drm_dev_init - Initialise new DRM device
 * @dev: DRM device
 * @driver: DRM driver
 * @parent: Parent device object
 *
 * Initialize a new DRM device. No device registration is done.
 * Call drm_dev_register() to advertice the device to user space and register it
 * with other core subsystems. This should be done last in the device
 * initialization sequence to make sure userspace can't access an inconsistent
 * state.
 *
 * The initial ref-count of the object is 1. Use drm_dev_get() and
 * drm_dev_put() to take and drop further ref-counts.
 *
 * It is recommended that drivers embed &struct drm_device into their own device
 * structure.
 *
 * Drivers that do not want to allocate their own device struct
 * embedding &struct drm_device can call drm_dev_alloc() instead. For drivers
 * that do embed &struct drm_device it must be placed first in the overall
 * structure, and the overall structure must be allocated using kmalloc(): The
 * drm core's release function unconditionally calls kfree() on the @dev pointer
 * when the final reference is released. To override this behaviour, and so
 * allow embedding of the drm_device inside the driver's device struct at an
 * arbitrary offset, you must supply a &drm_driver.release callback and control
 * the finalization explicitly.
 *
 * RETURNS:
 * 0 on success, or error code on failure.
 */
int drm_dev_init(struct drm_device *dev,
		 struct drm_driver *driver,
		 struct device *parent)
{
	int ret;

	if (!drm_core_init_complete) {
		DRM_ERROR("DRM core is not initialized\n");
		return -ENODEV;
	}

	BUG_ON(!parent);

	kref_init(&dev->ref);
	dev->dev = get_device(parent);
	dev->driver = driver;

	/* no per-device feature limits by default */
	dev->driver_features = ~0u;

	drm_legacy_init_members(dev);
	INIT_LIST_HEAD(&dev->filelist);
	INIT_LIST_HEAD(&dev->filelist_internal);
	INIT_LIST_HEAD(&dev->clientlist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	spin_lock_init(&dev->event_lock);
	mutex_init(&dev->struct_mutex);
	mutex_init(&dev->filelist_mutex);
	mutex_init(&dev->clientlist_mutex);
	mutex_init(&dev->master_mutex);

	dev->anon_inode = drm_fs_inode_new();
	if (IS_ERR(dev->anon_inode)) {
		ret = PTR_ERR(dev->anon_inode);
		DRM_ERROR("Cannot allocate anonymous inode: %d\n", ret);
		goto err_free;
	}

	if (drm_core_check_feature(dev, DRIVER_RENDER)) {
		ret = drm_minor_alloc(dev, DRM_MINOR_RENDER);
		if (ret)
			goto err_minors;
	}

	ret = drm_minor_alloc(dev, DRM_MINOR_PRIMARY);
	if (ret)
		goto err_minors;

	ret = drm_legacy_create_map_hash(dev);
	if (ret)
		goto err_minors;

	drm_legacy_ctxbitmap_init(dev);

	if (drm_core_check_feature(dev, DRIVER_GEM)) {
		ret = drm_gem_init(dev);
		if (ret) {
			DRM_ERROR("Cannot initialize graphics execution manager (GEM)\n");
			goto err_ctxbitmap;
		}
	}

	ret = drm_dev_set_unique(dev, dev_name(parent));
	if (ret)
		goto err_setunique;

	return 0;

err_setunique:
	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_destroy(dev);
err_ctxbitmap:
	drm_legacy_ctxbitmap_cleanup(dev);
	drm_legacy_remove_map_hash(dev);
err_minors:
	drm_minor_free(dev, DRM_MINOR_PRIMARY);
	drm_minor_free(dev, DRM_MINOR_RENDER);
	drm_fs_inode_free(dev->anon_inode);
err_free:
	put_device(dev->dev);
	mutex_destroy(&dev->master_mutex);
	mutex_destroy(&dev->clientlist_mutex);
	mutex_destroy(&dev->filelist_mutex);
	mutex_destroy(&dev->struct_mutex);
	drm_legacy_destroy_members(dev);
	return ret;
}
EXPORT_SYMBOL(drm_dev_init);

static void devm_drm_dev_init_release(void *data)
{
	drm_dev_put(data);
}

/**
 * devm_drm_dev_init - Resource managed drm_dev_init()
 * @parent: Parent device object
 * @dev: DRM device
 * @driver: DRM driver
 *
 * Managed drm_dev_init(). The DRM device initialized with this function is
 * automatically put on driver detach using drm_dev_put(). You must supply a
 * &drm_driver.release callback to control the finalization explicitly.
 *
 * RETURNS:
 * 0 on success, or error code on failure.
 */
int devm_drm_dev_init(struct device *parent,
		      struct drm_device *dev,
		      struct drm_driver *driver)
{
	int ret;

	if (WARN_ON(!parent || !driver->release))
		return -EINVAL;

	ret = drm_dev_init(dev, driver, parent);
	if (ret)
		return ret;

	ret = devm_add_action(parent, devm_drm_dev_init_release, dev);
	if (ret)
		devm_drm_dev_init_release(dev);

	return ret;
}
EXPORT_SYMBOL(devm_drm_dev_init);

/**
 * drm_dev_fini - Finalize a dead DRM device
 * @dev: DRM device
 *
 * Finalize a dead DRM device. This is the converse to drm_dev_init() and
 * frees up all data allocated by it. All driver private data should be
 * finalized first. Note that this function does not free the @dev, that is
 * left to the caller.
 *
 * The ref-count of @dev must be zero, and drm_dev_fini() should only be called
 * from a &drm_driver.release callback.
 */
void drm_dev_fini(struct drm_device *dev)
{
	drm_vblank_cleanup(dev);

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_destroy(dev);

	drm_legacy_ctxbitmap_cleanup(dev);
	drm_legacy_remove_map_hash(dev);
	drm_fs_inode_free(dev->anon_inode);

	drm_minor_free(dev, DRM_MINOR_PRIMARY);
	drm_minor_free(dev, DRM_MINOR_RENDER);

	put_device(dev->dev);

	mutex_destroy(&dev->master_mutex);
	mutex_destroy(&dev->clientlist_mutex);
	mutex_destroy(&dev->filelist_mutex);
	mutex_destroy(&dev->struct_mutex);
	drm_legacy_destroy_members(dev);
	kfree(dev->unique);
}
EXPORT_SYMBOL(drm_dev_fini);

/**
 * drm_dev_alloc - Allocate new DRM device
 * @driver: DRM driver to allocate device for
 * @parent: Parent device object
 *
 * Allocate and initialize a new DRM device. No device registration is done.
 * Call drm_dev_register() to advertice the device to user space and register it
 * with other core subsystems. This should be done last in the device
 * initialization sequence to make sure userspace can't access an inconsistent
 * state.
 *
 * The initial ref-count of the object is 1. Use drm_dev_get() and
 * drm_dev_put() to take and drop further ref-counts.
 *
 * Note that for purely virtual devices @parent can be NULL.
 *
 * Drivers that wish to subclass or embed &struct drm_device into their
 * own struct should look at using drm_dev_init() instead.
 *
 * RETURNS:
 * Pointer to new DRM device, or ERR_PTR on failure.
 */
struct drm_device *drm_dev_alloc(struct drm_driver *driver,
				 struct device *parent)
{
	struct drm_device *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	ret = drm_dev_init(dev, driver, parent);
	if (ret) {
		kfree(dev);
		return ERR_PTR(ret);
	}

	return dev;
}
EXPORT_SYMBOL(drm_dev_alloc);

static void drm_dev_release(struct kref *ref)
{
	struct drm_device *dev = container_of(ref, struct drm_device, ref);

	if (dev->driver->release) {
		dev->driver->release(dev);
	} else {
		drm_dev_fini(dev);
		kfree(dev);
	}
}

/**
 * drm_dev_get - Take reference of a DRM device
 * @dev: device to take reference of or NULL
 *
 * This increases the ref-count of @dev by one. You *must* already own a
 * reference when calling this. Use drm_dev_put() to drop this reference
 * again.
 *
 * This function never fails. However, this function does not provide *any*
 * guarantee whether the device is alive or running. It only provides a
 * reference to the object and the memory associated with it.
 */
void drm_dev_get(struct drm_device *dev)
{
	if (dev)
		kref_get(&dev->ref);
}
EXPORT_SYMBOL(drm_dev_get);

/**
 * drm_dev_put - Drop reference of a DRM device
 * @dev: device to drop reference of or NULL
 *
 * This decreases the ref-count of @dev by one. The device is destroyed if the
 * ref-count drops to zero.
 */
void drm_dev_put(struct drm_device *dev)
{
	if (dev)
		kref_put(&dev->ref, drm_dev_release);
}
EXPORT_SYMBOL(drm_dev_put);

static int create_compat_control_link(struct drm_device *dev)
{
	struct drm_minor *minor;
	char *name;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	minor = *drm_minor_get_slot(dev, DRM_MINOR_PRIMARY);
	if (!minor)
		return 0;

	/*
	 * Some existing userspace out there uses the existing of the controlD*
	 * sysfs files to figure out whether it's a modeset driver. It only does
	 * readdir, hence a symlink is sufficient (and the least confusing
	 * option). Otherwise controlD* is entirely unused.
	 *
	 * Old controlD chardev have been allocated in the range
	 * 64-127.
	 */
	name = kasprintf(GFP_KERNEL, "controlD%d", minor->index + 64);
	if (!name)
		return -ENOMEM;

	ret = sysfs_create_link(minor->kdev->kobj.parent,
				&minor->kdev->kobj,
				name);

	kfree(name);

	return ret;
}

static void remove_compat_control_link(struct drm_device *dev)
{
	struct drm_minor *minor;
	char *name;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	minor = *drm_minor_get_slot(dev, DRM_MINOR_PRIMARY);
	if (!minor)
		return;

	name = kasprintf(GFP_KERNEL, "controlD%d", minor->index + 64);
	if (!name)
		return;

	sysfs_remove_link(minor->kdev->kobj.parent, name);

	kfree(name);
}

/**
 * drm_dev_register - Register DRM device
 * @dev: Device to register
 * @flags: Flags passed to the driver's .load() function
 *
 * Register the DRM device @dev with the system, advertise device to user-space
 * and start normal device operation. @dev must be initialized via drm_dev_init()
 * previously.
 *
 * Never call this twice on any device!
 *
 * NOTE: To ensure backward compatibility with existing drivers method this
 * function calls the &drm_driver.load method after registering the device
 * nodes, creating race conditions. Usage of the &drm_driver.load methods is
 * therefore deprecated, drivers must perform all initialization before calling
 * drm_dev_register().
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
int drm_dev_register(struct drm_device *dev, unsigned long flags)
{
	struct drm_driver *driver = dev->driver;
	int ret;

	mutex_lock(&drm_global_mutex);

	ret = drm_minor_register(dev, DRM_MINOR_RENDER);
	if (ret)
		goto err_minors;

	ret = drm_minor_register(dev, DRM_MINOR_PRIMARY);
	if (ret)
		goto err_minors;

	ret = create_compat_control_link(dev);
	if (ret)
		goto err_minors;

	dev->registered = true;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, flags);
		if (ret)
			goto err_minors;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_modeset_register_all(dev);

	ret = 0;

	DRM_INFO("Initialized %s %d.%d.%d %s for %s on minor %d\n",
		 driver->name, driver->major, driver->minor,
		 driver->patchlevel, driver->date,
		 dev->dev ? dev_name(dev->dev) : "virtual device",
		 dev->primary->index);

	goto out_unlock;

err_minors:
	remove_compat_control_link(dev);
	drm_minor_unregister(dev, DRM_MINOR_PRIMARY);
	drm_minor_unregister(dev, DRM_MINOR_RENDER);
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
 * drm_dev_put() to drop their final reference.
 *
 * A special form of unregistering for hotpluggable devices is drm_dev_unplug(),
 * which can be called while there are still open users of @dev.
 *
 * This should be called first in the device teardown code to make sure
 * userspace can't access the device instance any more.
 */
void drm_dev_unregister(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_LEGACY))
		drm_lastclose(dev);

	dev->registered = false;

	drm_client_dev_unregister(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_modeset_unregister_all(dev);

	if (dev->driver->unload)
		dev->driver->unload(dev);

	if (dev->agp)
		drm_pci_agp_destroy(dev);

	drm_legacy_rmmaps(dev);

	remove_compat_control_link(dev);
	drm_minor_unregister(dev, DRM_MINOR_PRIMARY);
	drm_minor_unregister(dev, DRM_MINOR_RENDER);
}
EXPORT_SYMBOL(drm_dev_unregister);

/**
 * drm_dev_set_unique - Set the unique name of a DRM device
 * @dev: device of which to set the unique name
 * @name: unique name
 *
 * Sets the unique name of a DRM device using the specified string. This is
 * already done by drm_dev_init(), drivers should only override the default
 * unique name for backwards compatibility reasons.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_dev_set_unique(struct drm_device *dev, const char *name)
{
	kfree(dev->unique);
	dev->unique = kstrdup(name, GFP_KERNEL);

	return dev->unique ? 0 : -ENOMEM;
}
EXPORT_SYMBOL(drm_dev_set_unique);

/*
 * DRM Core
 * The DRM core module initializes all global DRM objects and makes them
 * available to drivers. Once setup, drivers can probe their respective
 * devices.
 * Currently, core management includes:
 *  - The "DRM-Global" key/value database
 *  - Global ID management for connectors
 *  - DRM major number allocation
 *  - DRM minor management
 *  - DRM sysfs class
 *  - DRM debugfs root
 *
 * Furthermore, the DRM core provides dynamic char-dev lookups. For each
 * interface registered on a DRM device, you can request minor numbers from DRM
 * core. DRM core takes care of major-number management and char-dev
 * registration. A stub ->open() callback forwards any open() requests to the
 * registered minor.
 */

static int drm_stub_open(struct inode *inode, struct file *filp)
{
	const struct file_operations *new_fops;
	struct drm_minor *minor;
	int err;

	DRM_DEBUG("\n");

	mutex_lock(&drm_global_mutex);
	minor = drm_minor_acquire(iminor(inode));
	if (IS_ERR(minor)) {
		err = PTR_ERR(minor);
		goto out_unlock;
	}

	new_fops = fops_get(minor->dev->driver->fops);
	if (!new_fops) {
		err = -ENODEV;
		goto out_release;
	}

	replace_fops(filp, new_fops);
	if (filp->f_op->open)
		err = filp->f_op->open(inode, filp);
	else
		err = 0;

out_release:
	drm_minor_release(minor);
out_unlock:
	mutex_unlock(&drm_global_mutex);
	return err;
}

static const struct file_operations drm_stub_fops = {
	.owner = THIS_MODULE,
	.open = drm_stub_open,
	.llseek = noop_llseek,
};

static void drm_core_exit(void)
{
	unregister_chrdev(DRM_MAJOR, "drm");
	debugfs_remove(drm_debugfs_root);
	drm_sysfs_destroy();
	idr_destroy(&drm_minors_idr);
	drm_connector_ida_destroy();
}

static int __init drm_core_init(void)
{
	int ret;

	drm_connector_ida_init();
	idr_init(&drm_minors_idr);

	ret = drm_sysfs_init();
	if (ret < 0) {
		DRM_ERROR("Cannot create DRM class: %d\n", ret);
		goto error;
	}

	drm_debugfs_root = debugfs_create_dir("dri", NULL);
	if (!drm_debugfs_root) {
		ret = -ENOMEM;
		DRM_ERROR("Cannot create debugfs-root: %d\n", ret);
		goto error;
	}

	ret = register_chrdev(DRM_MAJOR, "drm", &drm_stub_fops);
	if (ret < 0)
		goto error;

	drm_core_init_complete = true;

	DRM_DEBUG("Initialized\n");
	return 0;

error:
	drm_core_exit();
	return ret;
}

module_init(drm_core_init);
module_exit(drm_core_exit);
