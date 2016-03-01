/*
 * Copyright 2013 Intel Corporation
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author:
 * Naresh Kumar Kachhi <naresh.kumar.kachhi@intel.com>
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_drv.h"
#include <linux/console.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <linux/proc_fs.h>  /* Needed for procfs access */
#include <linux/fs.h>	    /* For the basic file system */
#include <linux/kernel.h>

#define RPM_AUTOSUSPEND_DELAY 500

#ifdef CONFIG_PM_RUNTIME

/**
 * - Where should we use get/put?
 *   Get/put should be used very carefully as we might end up in weird states
 *   if not used properly (see the Note below). We want to cover all the
 *   acesses that might result in accessing rings/display/registers/gtt etc
 *   Mostly covering ioctls and drm callbacks should be enough. You can
 *   avoid those which does not access any HW.
 *
 * - When should we avoid get/put?
 *   WQ and interrupts should be taken care in suspend path. We should
 *   disable all the interrupts and cancel any pending WQs. Never try to
 *   cover interrupt/WQ with get/put unless you are sure about it.
 *
 * Note:Following scenarios should be strictly avoided while using get_sync
 * 1. Calling get_sync with struct_mutex or mode_config.mutex locked
 *    - we acquire these locks in runtime_resume, so any call to get_sync
 *    with these mutex locked might end up in a dead lock.
 *    check_mutex_current function can be used to debug this scenario.
 *    - Or let's say thread1 has done get_sync and is currently executing
 *    runtime_resume function. Before thread1 is able to acquire these
 *    mutex, thread2 acquires the mutex and does a get_sync. Now thread1
 *    is waiting for mutex and thread2 is waiting for dev->power.lock
 *    resulting in a deadlock. Use check_mutex to debug this.
 * 2. Calling get_sync from runtime_resume path
 *    runtime_resume is called with dev->power.lock held. doing get_sync
 *    in same path will end up in deadlock
 */

#define RPM_PROC_ENTRY_FILENAME		"i915_rpm_op"
#define RPM_PROC_ENTRY_DIRECTORY	"driver/i915rpm"

int i915_rpm_get_procfs(struct inode *inode, struct file *file);
int i915_rpm_put_procfs(struct inode *inode, struct file *file);
/* proc file operations supported */
static const struct file_operations rpm_file_ops = {
	.owner		= THIS_MODULE,
	.open		= i915_rpm_get_procfs,
	.release	= i915_rpm_put_procfs,
};

bool i915_pm_runtime_enabled(struct device *dev)
{
	return pm_runtime_enabled(dev);
}

void i915_rpm_enable(struct device *dev)
{
	int cur_status = pm_runtime_enabled(dev);

	if (!cur_status) {
		pm_runtime_enable(dev);
		pm_runtime_allow(dev);
	}
}

void i915_rpm_disable(struct drm_device *drm_dev)
{
	struct device *dev = drm_dev->dev;
	int cur_status = pm_runtime_enabled(dev);

	if (cur_status) {
		pm_runtime_forbid(dev);
		pm_runtime_disable(dev);
	}
}

static int i915_rpm_procfs_init(struct drm_device *drm_dev)
{
	struct drm_i915_private *dev_priv = drm_dev->dev_private;

	dev_priv->rpm.i915_proc_dir = NULL;
	dev_priv->rpm.i915_proc_file = NULL;

	/**
	 * Create directory for rpm file(s)
	 */
	dev_priv->rpm.i915_proc_dir = proc_mkdir(RPM_PROC_ENTRY_DIRECTORY,
						 NULL);
	if (dev_priv->rpm.i915_proc_dir == NULL) {
		DRM_ERROR("Could not initialize %s\n",
				RPM_PROC_ENTRY_DIRECTORY);
		return -ENOMEM;
	}
	/**
	 * Create the /proc file
	 */
	dev_priv->rpm.i915_proc_file = proc_create_data(
						RPM_PROC_ENTRY_FILENAME,
						S_IRUGO | S_IWUSR,
						dev_priv->rpm.i915_proc_dir,
						&rpm_file_ops,
						drm_dev);
	/* check if file is created successfuly */
	if (dev_priv->rpm.i915_proc_file == NULL) {
		DRM_ERROR("Could not initialize %s/%s\n",
			RPM_PROC_ENTRY_DIRECTORY, RPM_PROC_ENTRY_FILENAME);
		return -ENOMEM;
	}
	return 0;
}

static int i915_rpm_procfs_deinit(struct drm_device *drm_dev)
{
	struct drm_i915_private *dev_priv = drm_dev->dev_private;
	/* Clean up proc file */
	if (dev_priv->rpm.i915_proc_file) {
		remove_proc_entry(RPM_PROC_ENTRY_FILENAME,
				 dev_priv->rpm.i915_proc_dir);
		dev_priv->rpm.i915_proc_file = NULL;
	}
	if (dev_priv->rpm.i915_proc_dir) {
		remove_proc_entry(RPM_PROC_ENTRY_DIRECTORY, NULL);
		dev_priv->rpm.i915_proc_dir = NULL;
	}
	return 0;
}

/* RPM init */
int i915_rpm_init(struct drm_device *drm_dev)
{
	int ret = 0;
	struct device *dev = drm_dev->dev;
	struct drm_i915_private *dev_priv = drm_dev->dev_private;

	ret = i915_rpm_procfs_init(drm_dev);
	if (ret)
		DRM_ERROR("unable to initialize procfs entry");
	ret = pm_runtime_set_active(dev);
	dev_priv->rpm.ring_active = false;
	atomic_set(&dev_priv->rpm.procfs_count, 0);
	pm_runtime_allow(dev);
	/* enable Auto Suspend */
	pm_runtime_set_autosuspend_delay(dev, RPM_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	if (dev->power.runtime_error)
		DRM_ERROR("rpm init: error = %d\n", dev->power.runtime_error);

	return ret;
}

int i915_rpm_deinit(struct drm_device *drm_dev)
{
	struct device *dev = drm_dev->dev;

	pm_runtime_forbid(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_get_noresume(dev);
	if (dev->power.runtime_error)
		DRM_ERROR("rpm init: error = %d\n", dev->power.runtime_error);

	i915_rpm_procfs_deinit(drm_dev);
	return 0;
}

/**
 * We have different flavour of get/put based on access type (ring/disp/
 * vxd etc). this is done based on different requirements and to make
 * debugging a little easier. Debugfs introduces separate counter for
 * each type.
 */

/**
 * Once we have scheduled commands on GPU, it might take a while GPU
 * to execute them. Following is done to make sure Gfx is in D0i0 while
 * GPU is executing the commands.
 * 1. For IOCTLS make sure we are in D0i0 by calling "get_ioctl".
 * 2. if IOCTL scheudles GPU commands using rings do the following
 *  a. For all ring accesses make sure we add a request in the request
 *     list and schedule a work item to track the "seq no". This
 *     is done by using "i915_add_request" or "i915_add_request_no_flush"
 *     functions.
 *  b. If request list was empty, we do a "get_ring". This will increment
 *     ref count to make sure GPU will be in D0 state.
 *  c. Once the list becomes empty call put_ring
 *
 * Note: All the ring accesses are covered with struct_mutex. So we
 * don't need any synchronization to protect ring_active.
 */
int i915_rpm_get_ring(struct drm_device *drm_dev)
{
	struct intel_engine_cs *ring;
	struct drm_i915_private *dev_priv = drm_dev->dev_private;
	int i;
	bool idle = true;

	for_each_ring(ring, dev_priv, i)
		idle &= list_empty(&ring->request_list);

	if (idle) {
		if (!dev_priv->rpm.ring_active) {
			dev_priv->rpm.ring_active = true;
			pm_runtime_get_noresume(drm_dev->dev);
		}
	}

	return 0;
}

int i915_rpm_put_ring(struct drm_device *drm_dev)
{
	struct drm_i915_private *dev_priv = drm_dev->dev_private;

	if (dev_priv->rpm.ring_active) {
		/* Mark last time it was busy and schedule a autosuspend */
		pm_runtime_mark_last_busy(drm_dev->dev);
		pm_runtime_put_autosuspend(drm_dev->dev);
		dev_priv->rpm.ring_active = false;
	}
	return 0;
}

/**
 * To cover the function pointers that are assigned to drm structures
 * and can be called from drm
 */
int i915_rpm_get_callback(struct drm_device *drm_dev)
{
	return pm_runtime_get_sync(drm_dev->dev);
}

int i915_rpm_put_callback(struct drm_device *drm_dev)
{
	pm_runtime_mark_last_busy(drm_dev->dev);
	return pm_runtime_put_autosuspend(drm_dev->dev);
}

/**
 * early_suspend/DSR should call this function to notify PM Core about
 * display idleness
 */
int i915_rpm_get_disp(struct drm_device *drm_dev)
{
	return pm_runtime_get_sync(drm_dev->dev);
}

int i915_rpm_put_disp(struct drm_device *drm_dev)
{
	pm_runtime_mark_last_busy(drm_dev->dev);
	return pm_runtime_put_autosuspend(drm_dev->dev);
}

/** to cover the ioctls with get/put*/
int i915_rpm_get_ioctl(struct drm_device *drm_dev)
{
	/* Don't do anything if device is not ready */
	if (drm_device_is_unplugged(drm_dev))
		return 0;

	return pm_runtime_get_sync(drm_dev->dev);
}

int i915_rpm_put_ioctl(struct drm_device *drm_dev)
{
	/* Don't do anything if device is not ready */
	if (drm_device_is_unplugged(drm_dev))
		return 0;

	pm_runtime_mark_last_busy(drm_dev->dev);
	return pm_runtime_put_autosuspend(drm_dev->dev);
}

/* these operations are caled from user mode (CoreU) to make sure
 * Gfx is up before register accesses from user mode
 */
int i915_rpm_get_procfs(struct inode *inode, struct file *file)
{
	struct drm_device *dev = PDE_DATA(inode);
	struct drm_i915_private *dev_priv = dev->dev_private;

	atomic_inc(&dev_priv->rpm.procfs_count);
	pm_runtime_get_sync(dev->dev);
	return 0;
}

int i915_rpm_put_procfs(struct inode *inode, struct file *file)
{
	struct drm_device *dev = PDE_DATA(inode);
	struct drm_i915_private *dev_priv = dev->dev_private;

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	atomic_dec(&dev_priv->rpm.procfs_count);
	return 0;
}

/**
 * VXD driver need to call this to make sure Gfx is in D0i0
 * while VXD is on
 */
#ifdef CONFIG_DRM_VXD_BYT
int i915_rpm_get_vxd(struct drm_device *drm_dev)
{
	return pm_runtime_get_sync(drm_dev->dev);
}
EXPORT_SYMBOL(i915_rpm_get_vxd);

/**
 * VXD driver need to call this to notify Gfx that it is
 * done with HW accesses
 */
int i915_rpm_put_vxd(struct drm_device *drm_dev)
{
	pm_runtime_mark_last_busy(drm_dev->dev);
	return pm_runtime_put_autosuspend(drm_dev->dev);
}
EXPORT_SYMBOL(i915_rpm_put_vxd);
#endif

/* mainly for debug purpose, check if the access is valid */
bool i915_rpm_access_check(struct drm_device *dev)
{
	if (dev->dev->power.runtime_status == RPM_SUSPENDED) {
		DRM_ERROR("invalid access, will cause Hard Hang\n");
		dump_stack();
		return false;
	}
	return true;
}

/* mainly for debug purpose, check if mutex is locked by
 * current thread
 */
int check_mutex_current(struct drm_device *drm_dev)
{
	int ret = 0;

	if ((mutex_is_locked(&drm_dev->mode_config.mutex)) &&
			(drm_dev->mode_config.mutex.owner == current)) {
		DRM_ERROR("config mutex locked by current thread\n");
		dump_stack();
		ret = -1;
	}
	if ((mutex_is_locked(&drm_dev->struct_mutex)) &&
			(drm_dev->struct_mutex.owner == current)) {
		DRM_ERROR("struct mutex locked by current thread\n");
		dump_stack();
		ret = -2;
	}
	return ret;
}

int check_mutex(struct drm_device *drm_dev)
{
	int ret = 0;

	if (mutex_is_locked(&drm_dev->mode_config.mutex)) {
		DRM_ERROR("config mutex locked\n");
		dump_stack();
		ret = -1;
	}
	if (mutex_is_locked(&drm_dev->struct_mutex)) {
		DRM_ERROR("struct mutex locked\n");
		dump_stack();
		ret = -2;
	}
	return ret;
}

/* Check for current runtime state */
bool i915_is_device_active(struct drm_device *dev)
{
	return (dev->dev->power.runtime_status == RPM_ACTIVE);
}

bool i915_is_device_resuming(struct drm_device *dev)
{
	return (dev->dev->power.runtime_status == RPM_RESUMING);
}

bool i915_is_device_suspended(struct drm_device *dev)
{
	return (dev->dev->power.runtime_status == RPM_SUSPENDED);
}

bool i915_is_device_suspending(struct drm_device *dev)
{
	return (dev->dev->power.runtime_status == RPM_SUSPENDING);
}

#else /*CONFIG_PM_RUNTIME*/
int i915_rpm_init(struct drm_device *dev) {return 0; }
int i915_rpm_deinit(struct drm_device *dev) {return 0; }
int i915_rpm_get(struct drm_device *dev, u32 flags) {return 0; }
int i915_rpm_put(struct drm_device *dev, u32 flags) {return 0; }
int i915_rpm_get_ring(struct drm_device *dev) {return 0; }
int i915_rpm_put_ring(struct drm_device *dev) {return 0; }
int i915_rpm_get_callback(struct drm_device *dev) {return 0; }
int i915_rpm_put_callback(struct drm_device *dev) {return 0; }
int i915_rpm_get_ioctl(struct drm_device *dev) {return 0; }
int i915_rpm_put_ioctl(struct drm_device *dev) {return 0; }
int i915_rpm_get_disp(struct drm_device *dev) {return 0; }
int i915_rpm_put_disp(struct drm_device *dev) {return 0; }
int i915_rpm_get_procfs(struct inode *inode,
			      struct file *file) {return 0; }
int i915_rpm_put_procfs(struct inode *inode,
			      struct file *file) {return 0; }
#ifdef CONFIG_DRM_VXD_BYT
int i915_rpm_get_vxd(struct drm_device *dev) {return 0; }
int i915_rpm_put_vxd(struct drm_device *dev) {return 0; }
#endif

bool i915_is_device_active(struct drm_device *dev)
{
	return true;
}

bool i915_is_device_resuming(struct drm_device *dev)
{
	return false;
}

bool i915_is_device_suspended(struct drm_device *dev)
{
	return false;
}

bool i915_is_device_suspending(struct drm_device *dev)
{
	return false;
}

bool i915_rpm_access_check(struct drm_device *dev)
{
	return true;
}
bool i915_pm_runtime_enabled(struct device *dev)
{
	return false;
}

void i915_rpm_enable(struct device *dev) {}

void i915_rpm_disable(struct drm_device *drm_dev) {}

#endif /*CONFIG_PM_RUNTIME*/
