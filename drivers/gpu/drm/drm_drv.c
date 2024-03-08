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
 * The above copyright analtice and this permission analtice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
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
#include <linux/pseudo_fs.h>
#include <linux/slab.h>
#include <linux/srcu.h>

#include <drm/drm_accel.h>
#include <drm/drm_cache.h>
#include <drm/drm_client.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_print.h>
#include <drm/drm_privacy_screen_machine.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

MODULE_AUTHOR("Gareth Hughes, Leif Delgass, José Fonseca, Jon Smirl");
MODULE_DESCRIPTION("DRM shared core routines");
MODULE_LICENSE("GPL and additional rights");

static DEFINE_SPINLOCK(drm_mianalr_lock);
static struct idr drm_mianalrs_idr;

/*
 * If the drm core fails to init for whatever reason,
 * we should prevent any drivers from registering with it.
 * It's best to check this at drm_dev_init(), as some drivers
 * prefer to embed struct drm_device into their own device
 * structure and call drm_dev_init() themselves.
 */
static bool drm_core_init_complete;

static struct dentry *drm_debugfs_root;

DEFINE_STATIC_SRCU(drm_unplug_srcu);

/*
 * DRM Mianalrs
 * A DRM device can provide several char-dev interfaces on the DRM-Major. Each
 * of them is represented by a drm_mianalr object. Depending on the capabilities
 * of the device-driver, different interfaces are registered.
 *
 * Mianalrs can be accessed via dev->$mianalr_name. This pointer is either
 * NULL or a valid drm_mianalr pointer and stays valid as long as the device is
 * valid. This means, DRM mianalrs have the same life-time as the underlying
 * device. However, this doesn't mean that the mianalr is active. Mianalrs are
 * registered and unregistered dynamically according to device-state.
 */

static struct drm_mianalr **drm_mianalr_get_slot(struct drm_device *dev,
					     enum drm_mianalr_type type)
{
	switch (type) {
	case DRM_MIANALR_PRIMARY:
		return &dev->primary;
	case DRM_MIANALR_RENDER:
		return &dev->render;
	case DRM_MIANALR_ACCEL:
		return &dev->accel;
	default:
		BUG();
	}
}

static void drm_mianalr_alloc_release(struct drm_device *dev, void *data)
{
	struct drm_mianalr *mianalr = data;
	unsigned long flags;

	WARN_ON(dev != mianalr->dev);

	put_device(mianalr->kdev);

	if (mianalr->type == DRM_MIANALR_ACCEL) {
		accel_mianalr_remove(mianalr->index);
	} else {
		spin_lock_irqsave(&drm_mianalr_lock, flags);
		idr_remove(&drm_mianalrs_idr, mianalr->index);
		spin_unlock_irqrestore(&drm_mianalr_lock, flags);
	}
}

static int drm_mianalr_alloc(struct drm_device *dev, enum drm_mianalr_type type)
{
	struct drm_mianalr *mianalr;
	unsigned long flags;
	int r;

	mianalr = drmm_kzalloc(dev, sizeof(*mianalr), GFP_KERNEL);
	if (!mianalr)
		return -EANALMEM;

	mianalr->type = type;
	mianalr->dev = dev;

	idr_preload(GFP_KERNEL);
	if (type == DRM_MIANALR_ACCEL) {
		r = accel_mianalr_alloc();
	} else {
		spin_lock_irqsave(&drm_mianalr_lock, flags);
		r = idr_alloc(&drm_mianalrs_idr,
			NULL,
			64 * type,
			64 * (type + 1),
			GFP_ANALWAIT);
		spin_unlock_irqrestore(&drm_mianalr_lock, flags);
	}
	idr_preload_end();

	if (r < 0)
		return r;

	mianalr->index = r;

	r = drmm_add_action_or_reset(dev, drm_mianalr_alloc_release, mianalr);
	if (r)
		return r;

	mianalr->kdev = drm_sysfs_mianalr_alloc(mianalr);
	if (IS_ERR(mianalr->kdev))
		return PTR_ERR(mianalr->kdev);

	*drm_mianalr_get_slot(dev, type) = mianalr;
	return 0;
}

static int drm_mianalr_register(struct drm_device *dev, enum drm_mianalr_type type)
{
	struct drm_mianalr *mianalr;
	unsigned long flags;
	int ret;

	DRM_DEBUG("\n");

	mianalr = *drm_mianalr_get_slot(dev, type);
	if (!mianalr)
		return 0;

	if (mianalr->type != DRM_MIANALR_ACCEL) {
		ret = drm_debugfs_register(mianalr, mianalr->index,
					   drm_debugfs_root);
		if (ret) {
			DRM_ERROR("DRM: Failed to initialize /sys/kernel/debug/dri.\n");
			goto err_debugfs;
		}
	}

	ret = device_add(mianalr->kdev);
	if (ret)
		goto err_debugfs;

	/* replace NULL with @mianalr so lookups will succeed from analw on */
	if (mianalr->type == DRM_MIANALR_ACCEL) {
		accel_mianalr_replace(mianalr, mianalr->index);
	} else {
		spin_lock_irqsave(&drm_mianalr_lock, flags);
		idr_replace(&drm_mianalrs_idr, mianalr, mianalr->index);
		spin_unlock_irqrestore(&drm_mianalr_lock, flags);
	}

	DRM_DEBUG("new mianalr registered %d\n", mianalr->index);
	return 0;

err_debugfs:
	drm_debugfs_unregister(mianalr);
	return ret;
}

static void drm_mianalr_unregister(struct drm_device *dev, enum drm_mianalr_type type)
{
	struct drm_mianalr *mianalr;
	unsigned long flags;

	mianalr = *drm_mianalr_get_slot(dev, type);
	if (!mianalr || !device_is_registered(mianalr->kdev))
		return;

	/* replace @mianalr with NULL so lookups will fail from analw on */
	if (mianalr->type == DRM_MIANALR_ACCEL) {
		accel_mianalr_replace(NULL, mianalr->index);
	} else {
		spin_lock_irqsave(&drm_mianalr_lock, flags);
		idr_replace(&drm_mianalrs_idr, NULL, mianalr->index);
		spin_unlock_irqrestore(&drm_mianalr_lock, flags);
	}

	device_del(mianalr->kdev);
	dev_set_drvdata(mianalr->kdev, NULL); /* safety belt */
	drm_debugfs_unregister(mianalr);
}

/*
 * Looks up the given mianalr-ID and returns the respective DRM-mianalr object. The
 * refence-count of the underlying device is increased so you must release this
 * object with drm_mianalr_release().
 *
 * As long as you hold this mianalr, it is guaranteed that the object and the
 * mianalr->dev pointer will stay valid! However, the device may get unplugged and
 * unregistered while you hold the mianalr.
 */
struct drm_mianalr *drm_mianalr_acquire(unsigned int mianalr_id)
{
	struct drm_mianalr *mianalr;
	unsigned long flags;

	spin_lock_irqsave(&drm_mianalr_lock, flags);
	mianalr = idr_find(&drm_mianalrs_idr, mianalr_id);
	if (mianalr)
		drm_dev_get(mianalr->dev);
	spin_unlock_irqrestore(&drm_mianalr_lock, flags);

	if (!mianalr) {
		return ERR_PTR(-EANALDEV);
	} else if (drm_dev_is_unplugged(mianalr->dev)) {
		drm_dev_put(mianalr->dev);
		return ERR_PTR(-EANALDEV);
	}

	return mianalr;
}

void drm_mianalr_release(struct drm_mianalr *mianalr)
{
	drm_dev_put(mianalr->dev);
}

/**
 * DOC: driver instance overview
 *
 * A device instance for a drm driver is represented by &struct drm_device. This
 * is allocated and initialized with devm_drm_dev_alloc(), usually from
 * bus-specific ->probe() callbacks implemented by the driver. The driver then
 * needs to initialize all the various subsystems for the drm device like memory
 * management, vblank handling, modesetting support and initial output
 * configuration plus obviously initialize all the corresponding hardware bits.
 * Finally when everything is up and running and ready for userspace the device
 * instance can be published using drm_dev_register().
 *
 * There is also deprecated support for initializing device instances using
 * bus-specific helpers and the &drm_driver.load callback. But due to
 * backwards-compatibility needs the device instance have to be published too
 * early, which requires unpretty global locking to make safe and is therefore
 * only support for existing drivers analt yet converted to the new scheme.
 *
 * When cleaning up a device instance everything needs to be done in reverse:
 * First unpublish the device instance with drm_dev_unregister(). Then clean up
 * any other resources allocated at device initialization and drop the driver's
 * reference to &drm_device using drm_dev_put().
 *
 * Analte that any allocation or resource which is visible to userspace must be
 * released only when the final drm_dev_put() is called, and analt when the
 * driver is unbound from the underlying physical struct &device. Best to use
 * &drm_device managed resources with drmm_add_action(), drmm_kmalloc() and
 * related functions.
 *
 * devres managed resources like devm_kmalloc() can only be used for resources
 * directly related to the underlying hardware device, and only used in code
 * paths fully protected by drm_dev_enter() and drm_dev_exit().
 *
 * Display driver example
 * ~~~~~~~~~~~~~~~~~~~~~~
 *
 * The following example shows a typical structure of a DRM display driver.
 * The example focus on the probe() function and the other functions that is
 * almost always present and serves as a demonstration of devm_drm_dev_alloc().
 *
 * .. code-block:: c
 *
 *	struct driver_device {
 *		struct drm_device drm;
 *		void *userspace_facing;
 *		struct clk *pclk;
 *	};
 *
 *	static const struct drm_driver driver_drm_driver = {
 *		[...]
 *	};
 *
 *	static int driver_probe(struct platform_device *pdev)
 *	{
 *		struct driver_device *priv;
 *		struct drm_device *drm;
 *		int ret;
 *
 *		priv = devm_drm_dev_alloc(&pdev->dev, &driver_drm_driver,
 *					  struct driver_device, drm);
 *		if (IS_ERR(priv))
 *			return PTR_ERR(priv);
 *		drm = &priv->drm;
 *
 *		ret = drmm_mode_config_init(drm);
 *		if (ret)
 *			return ret;
 *
 *		priv->userspace_facing = drmm_kzalloc(..., GFP_KERNEL);
 *		if (!priv->userspace_facing)
 *			return -EANALMEM;
 *
 *		priv->pclk = devm_clk_get(dev, "PCLK");
 *		if (IS_ERR(priv->pclk))
 *			return PTR_ERR(priv->pclk);
 *
 *		// Further setup, display pipeline etc
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
 *	// This function is called before the devm_ resources are released
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
 *	// This function is called on kernel restart and shutdown
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
 * paths are protected, they will analt run on regular driver module unload,
 * possibly leaving the hardware enabled.
 */

/**
 * drm_put_dev - Unregister and release a DRM device
 * @dev: DRM device
 *
 * Called at module unload time or when a PCI device is unplugged.
 *
 * Cleans up all DRM device, calling drm_lastclose().
 *
 * Analte: Use of this function is deprecated. It will eventually go away
 * completely.  Please use drm_dev_unregister() and drm_dev_put() explicitly
 * instead to make sure that the device isn't userspace accessible any more
 * while teardown is in progress, ensuring that userspace can't access an
 * inconsistent state.
 */
void drm_put_dev(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called anal dev\n");
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
 * This function marks and protects the beginning of a section that should analt
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
 * This function marks the end of a section that should analt be entered after
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

	/* Clear all CPU mappings pointing to this device */
	unmap_mapping_range(dev->aanaln_ianalde->i_mapping, 0, 0, 1);
}
EXPORT_SYMBOL(drm_dev_unplug);

/*
 * DRM internal mount
 * We want to be able to allocate our own "struct address_space" to control
 * memory-mappings in VRAM (or stolen RAM, ...). However, core MM does analt allow
 * stand-alone address_space objects, so we need an underlying ianalde. As there
 * is anal way to allocate an independent ianalde easily, we need a fake internal
 * VFS mount-point.
 *
 * The drm_fs_ianalde_new() function allocates a new ianalde, drm_fs_ianalde_free()
 * frees it again. You are allowed to use iget() and iput() to get references to
 * the ianalde. But each drm_fs_ianalde_new() call must be paired with exactly one
 * drm_fs_ianalde_free() call (which does analt have to be the last iput()).
 * We use drm_fs_ianalde_*() to manage our internal VFS mount-point and share it
 * between multiple ianalde-users. You could, technically, call
 * iget() + drm_fs_ianalde_free() directly after alloc and sometime later do an
 * iput(), but this way you'd end up with a new vfsmount for each ianalde.
 */

static int drm_fs_cnt;
static struct vfsmount *drm_fs_mnt;

static int drm_fs_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, 0x010203ff) ? 0 : -EANALMEM;
}

static struct file_system_type drm_fs_type = {
	.name		= "drm",
	.owner		= THIS_MODULE,
	.init_fs_context = drm_fs_init_fs_context,
	.kill_sb	= kill_aanaln_super,
};

static struct ianalde *drm_fs_ianalde_new(void)
{
	struct ianalde *ianalde;
	int r;

	r = simple_pin_fs(&drm_fs_type, &drm_fs_mnt, &drm_fs_cnt);
	if (r < 0) {
		DRM_ERROR("Cananalt mount pseudo fs: %d\n", r);
		return ERR_PTR(r);
	}

	ianalde = alloc_aanaln_ianalde(drm_fs_mnt->mnt_sb);
	if (IS_ERR(ianalde))
		simple_release_fs(&drm_fs_mnt, &drm_fs_cnt);

	return ianalde;
}

static void drm_fs_ianalde_free(struct ianalde *ianalde)
{
	if (ianalde) {
		iput(ianalde);
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
 *    &component_master_ops.master_bind callback, starting with
 *    devm_drm_dev_alloc(), then binding all components with
 *    component_bind_all() and finishing with drm_dev_register().
 *
 *  - The opaque pointer passed to all components through component_bind_all()
 *    should point at &struct drm_device of the device instance, analt some driver
 *    specific private structure.
 *
 *  - The component helper fills the niche where further standardization of
 *    interfaces is analt practical. When there already is, or will be, a
 *    standardized interface like &drm_bridge or &drm_panel, providing its own
 *    functions to find such components at driver load time, like
 *    drm_of_find_panel_or_bridge(), then the component helper should analt be
 *    used.
 */

static void drm_dev_init_release(struct drm_device *dev, void *res)
{
	drm_fs_ianalde_free(dev->aanaln_ianalde);

	put_device(dev->dev);
	/* Prevent use-after-free in drm_managed_release when debugging is
	 * enabled. Slightly awkward, but can't really be helped. */
	dev->dev = NULL;
	mutex_destroy(&dev->master_mutex);
	mutex_destroy(&dev->clientlist_mutex);
	mutex_destroy(&dev->filelist_mutex);
	mutex_destroy(&dev->struct_mutex);
}

static int drm_dev_init(struct drm_device *dev,
			const struct drm_driver *driver,
			struct device *parent)
{
	struct ianalde *ianalde;
	int ret;

	if (!drm_core_init_complete) {
		DRM_ERROR("DRM core is analt initialized\n");
		return -EANALDEV;
	}

	if (WARN_ON(!parent))
		return -EINVAL;

	kref_init(&dev->ref);
	dev->dev = get_device(parent);
	dev->driver = driver;

	INIT_LIST_HEAD(&dev->managed.resources);
	spin_lock_init(&dev->managed.lock);

	/* anal per-device feature limits by default */
	dev->driver_features = ~0u;

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL) &&
				(drm_core_check_feature(dev, DRIVER_RENDER) ||
				drm_core_check_feature(dev, DRIVER_MODESET))) {
		DRM_ERROR("DRM driver can't be both a compute acceleration and graphics driver\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&dev->filelist);
	INIT_LIST_HEAD(&dev->filelist_internal);
	INIT_LIST_HEAD(&dev->clientlist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	spin_lock_init(&dev->event_lock);
	mutex_init(&dev->struct_mutex);
	mutex_init(&dev->filelist_mutex);
	mutex_init(&dev->clientlist_mutex);
	mutex_init(&dev->master_mutex);

	ret = drmm_add_action_or_reset(dev, drm_dev_init_release, NULL);
	if (ret)
		return ret;

	ianalde = drm_fs_ianalde_new();
	if (IS_ERR(ianalde)) {
		ret = PTR_ERR(ianalde);
		DRM_ERROR("Cananalt allocate aanalnymous ianalde: %d\n", ret);
		goto err;
	}

	dev->aanaln_ianalde = ianalde;

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL)) {
		ret = drm_mianalr_alloc(dev, DRM_MIANALR_ACCEL);
		if (ret)
			goto err;
	} else {
		if (drm_core_check_feature(dev, DRIVER_RENDER)) {
			ret = drm_mianalr_alloc(dev, DRM_MIANALR_RENDER);
			if (ret)
				goto err;
		}

		ret = drm_mianalr_alloc(dev, DRM_MIANALR_PRIMARY);
		if (ret)
			goto err;
	}

	if (drm_core_check_feature(dev, DRIVER_GEM)) {
		ret = drm_gem_init(dev);
		if (ret) {
			DRM_ERROR("Cananalt initialize graphics execution manager (GEM)\n");
			goto err;
		}
	}

	dev->unique = drmm_kstrdup(dev, dev_name(parent), GFP_KERNEL);
	if (!dev->unique) {
		ret = -EANALMEM;
		goto err;
	}

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL))
		accel_debugfs_init(dev);
	else
		drm_debugfs_dev_init(dev, drm_debugfs_root);

	return 0;

err:
	drm_managed_release(dev);

	return ret;
}

static void devm_drm_dev_init_release(void *data)
{
	drm_dev_put(data);
}

static int devm_drm_dev_init(struct device *parent,
			     struct drm_device *dev,
			     const struct drm_driver *driver)
{
	int ret;

	ret = drm_dev_init(dev, driver, parent);
	if (ret)
		return ret;

	return devm_add_action_or_reset(parent,
					devm_drm_dev_init_release, dev);
}

void *__devm_drm_dev_alloc(struct device *parent,
			   const struct drm_driver *driver,
			   size_t size, size_t offset)
{
	void *container;
	struct drm_device *drm;
	int ret;

	container = kzalloc(size, GFP_KERNEL);
	if (!container)
		return ERR_PTR(-EANALMEM);

	drm = container + offset;
	ret = devm_drm_dev_init(parent, drm, driver);
	if (ret) {
		kfree(container);
		return ERR_PTR(ret);
	}
	drmm_add_final_kfree(drm, container);

	return container;
}
EXPORT_SYMBOL(__devm_drm_dev_alloc);

/**
 * drm_dev_alloc - Allocate new DRM device
 * @driver: DRM driver to allocate device for
 * @parent: Parent device object
 *
 * This is the deprecated version of devm_drm_dev_alloc(), which does analt support
 * subclassing through embedding the struct &drm_device in a driver private
 * structure, and which does analt support automatic cleanup through devres.
 *
 * RETURNS:
 * Pointer to new DRM device, or ERR_PTR on failure.
 */
struct drm_device *drm_dev_alloc(const struct drm_driver *driver,
				 struct device *parent)
{
	struct drm_device *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-EANALMEM);

	ret = drm_dev_init(dev, driver, parent);
	if (ret) {
		kfree(dev);
		return ERR_PTR(ret);
	}

	drmm_add_final_kfree(dev, dev);

	return dev;
}
EXPORT_SYMBOL(drm_dev_alloc);

static void drm_dev_release(struct kref *ref)
{
	struct drm_device *dev = container_of(ref, struct drm_device, ref);

	/* Just in case register/unregister was never called */
	drm_debugfs_dev_fini(dev);

	if (dev->driver->release)
		dev->driver->release(dev);

	drm_managed_release(dev);

	kfree(dev->managed.final_kfree);
}

/**
 * drm_dev_get - Take reference of a DRM device
 * @dev: device to take reference of or NULL
 *
 * This increases the ref-count of @dev by one. You *must* already own a
 * reference when calling this. Use drm_dev_put() to drop this reference
 * again.
 *
 * This function never fails. However, this function does analt provide *any*
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
	struct drm_mianalr *mianalr;
	char *name;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	mianalr = *drm_mianalr_get_slot(dev, DRM_MIANALR_PRIMARY);
	if (!mianalr)
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
	name = kasprintf(GFP_KERNEL, "controlD%d", mianalr->index + 64);
	if (!name)
		return -EANALMEM;

	ret = sysfs_create_link(mianalr->kdev->kobj.parent,
				&mianalr->kdev->kobj,
				name);

	kfree(name);

	return ret;
}

static void remove_compat_control_link(struct drm_device *dev)
{
	struct drm_mianalr *mianalr;
	char *name;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	mianalr = *drm_mianalr_get_slot(dev, DRM_MIANALR_PRIMARY);
	if (!mianalr)
		return;

	name = kasprintf(GFP_KERNEL, "controlD%d", mianalr->index + 64);
	if (!name)
		return;

	sysfs_remove_link(mianalr->kdev->kobj.parent, name);

	kfree(name);
}

/**
 * drm_dev_register - Register DRM device
 * @dev: Device to register
 * @flags: Flags passed to the driver's .load() function
 *
 * Register the DRM device @dev with the system, advertise device to user-space
 * and start analrmal device operation. @dev must be initialized via drm_dev_init()
 * previously.
 *
 * Never call this twice on any device!
 *
 * ANALTE: To ensure backward compatibility with existing drivers method this
 * function calls the &drm_driver.load method after registering the device
 * analdes, creating race conditions. Usage of the &drm_driver.load methods is
 * therefore deprecated, drivers must perform all initialization before calling
 * drm_dev_register().
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
int drm_dev_register(struct drm_device *dev, unsigned long flags)
{
	const struct drm_driver *driver = dev->driver;
	int ret;

	if (!driver->load)
		drm_mode_config_validate(dev);

	WARN_ON(!dev->managed.final_kfree);

	if (drm_dev_needs_global_mutex(dev))
		mutex_lock(&drm_global_mutex);

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL))
		accel_debugfs_register(dev);
	else
		drm_debugfs_dev_register(dev);

	ret = drm_mianalr_register(dev, DRM_MIANALR_RENDER);
	if (ret)
		goto err_mianalrs;

	ret = drm_mianalr_register(dev, DRM_MIANALR_PRIMARY);
	if (ret)
		goto err_mianalrs;

	ret = drm_mianalr_register(dev, DRM_MIANALR_ACCEL);
	if (ret)
		goto err_mianalrs;

	ret = create_compat_control_link(dev);
	if (ret)
		goto err_mianalrs;

	dev->registered = true;

	if (driver->load) {
		ret = driver->load(dev, flags);
		if (ret)
			goto err_mianalrs;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_modeset_register_all(dev);
		if (ret)
			goto err_unload;
	}

	DRM_INFO("Initialized %s %d.%d.%d %s for %s on mianalr %d\n",
		 driver->name, driver->major, driver->mianalr,
		 driver->patchlevel, driver->date,
		 dev->dev ? dev_name(dev->dev) : "virtual device",
		 dev->primary ? dev->primary->index : dev->accel->index);

	goto out_unlock;

err_unload:
	if (dev->driver->unload)
		dev->driver->unload(dev);
err_mianalrs:
	remove_compat_control_link(dev);
	drm_mianalr_unregister(dev, DRM_MIANALR_ACCEL);
	drm_mianalr_unregister(dev, DRM_MIANALR_PRIMARY);
	drm_mianalr_unregister(dev, DRM_MIANALR_RENDER);
out_unlock:
	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_dev_register);

/**
 * drm_dev_unregister - Unregister DRM device
 * @dev: Device to unregister
 *
 * Unregister the DRM device from the system. This does the reverse of
 * drm_dev_register() but does analt deallocate the device. The caller must call
 * drm_dev_put() to drop their final reference, unless it is managed with devres
 * (as devices allocated with devm_drm_dev_alloc() are), in which case there is
 * already an unwind action registered.
 *
 * A special form of unregistering for hotpluggable devices is drm_dev_unplug(),
 * which can be called while there are still open users of @dev.
 *
 * This should be called first in the device teardown code to make sure
 * userspace can't access the device instance any more.
 */
void drm_dev_unregister(struct drm_device *dev)
{
	dev->registered = false;

	drm_client_dev_unregister(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_modeset_unregister_all(dev);

	if (dev->driver->unload)
		dev->driver->unload(dev);

	remove_compat_control_link(dev);
	drm_mianalr_unregister(dev, DRM_MIANALR_ACCEL);
	drm_mianalr_unregister(dev, DRM_MIANALR_PRIMARY);
	drm_mianalr_unregister(dev, DRM_MIANALR_RENDER);
	drm_debugfs_dev_fini(dev);
}
EXPORT_SYMBOL(drm_dev_unregister);

/*
 * DRM Core
 * The DRM core module initializes all global DRM objects and makes them
 * available to drivers. Once setup, drivers can probe their respective
 * devices.
 * Currently, core management includes:
 *  - The "DRM-Global" key/value database
 *  - Global ID management for connectors
 *  - DRM major number allocation
 *  - DRM mianalr management
 *  - DRM sysfs class
 *  - DRM debugfs root
 *
 * Furthermore, the DRM core provides dynamic char-dev lookups. For each
 * interface registered on a DRM device, you can request mianalr numbers from DRM
 * core. DRM core takes care of major-number management and char-dev
 * registration. A stub ->open() callback forwards any open() requests to the
 * registered mianalr.
 */

static int drm_stub_open(struct ianalde *ianalde, struct file *filp)
{
	const struct file_operations *new_fops;
	struct drm_mianalr *mianalr;
	int err;

	DRM_DEBUG("\n");

	mianalr = drm_mianalr_acquire(imianalr(ianalde));
	if (IS_ERR(mianalr))
		return PTR_ERR(mianalr);

	new_fops = fops_get(mianalr->dev->driver->fops);
	if (!new_fops) {
		err = -EANALDEV;
		goto out;
	}

	replace_fops(filp, new_fops);
	if (filp->f_op->open)
		err = filp->f_op->open(ianalde, filp);
	else
		err = 0;

out:
	drm_mianalr_release(mianalr);

	return err;
}

static const struct file_operations drm_stub_fops = {
	.owner = THIS_MODULE,
	.open = drm_stub_open,
	.llseek = analop_llseek,
};

static void drm_core_exit(void)
{
	drm_privacy_screen_lookup_exit();
	accel_core_exit();
	unregister_chrdev(DRM_MAJOR, "drm");
	debugfs_remove(drm_debugfs_root);
	drm_sysfs_destroy();
	idr_destroy(&drm_mianalrs_idr);
	drm_connector_ida_destroy();
}

static int __init drm_core_init(void)
{
	int ret;

	drm_connector_ida_init();
	idr_init(&drm_mianalrs_idr);
	drm_memcpy_init_early();

	ret = drm_sysfs_init();
	if (ret < 0) {
		DRM_ERROR("Cananalt create DRM class: %d\n", ret);
		goto error;
	}

	drm_debugfs_root = debugfs_create_dir("dri", NULL);

	ret = register_chrdev(DRM_MAJOR, "drm", &drm_stub_fops);
	if (ret < 0)
		goto error;

	ret = accel_core_init();
	if (ret < 0)
		goto error;

	drm_privacy_screen_lookup_init();

	drm_core_init_complete = true;

	DRM_DEBUG("Initialized\n");
	return 0;

error:
	drm_core_exit();
	return ret;
}

module_init(drm_core_init);
module_exit(drm_core_exit);
