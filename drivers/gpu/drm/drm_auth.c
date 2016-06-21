/*
 * Created: Tue Feb  2 08:37:54 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Author Rickard E. (Rik) Faith <faith@valinux.com>
 * Author Gareth Hughes <gareth@valinux.com>
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include "drm_internal.h"
#include "drm_legacy.h"

/**
 * drm_getmagic - Get unique magic of a client
 * @dev: DRM device to operate on
 * @data: ioctl data containing the drm_auth object
 * @file_priv: DRM file that performs the operation
 *
 * This looks up the unique magic of the passed client and returns it. If the
 * client did not have a magic assigned, yet, a new one is registered. The magic
 * is stored in the passed drm_auth object.
 *
 * Returns: 0 on success, negative error code on failure.
 */
int drm_getmagic(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_auth *auth = data;
	int ret = 0;

	mutex_lock(&dev->master_mutex);
	if (!file_priv->magic) {
		ret = idr_alloc(&file_priv->master->magic_map, file_priv,
				1, 0, GFP_KERNEL);
		if (ret >= 0)
			file_priv->magic = ret;
	}
	auth->magic = file_priv->magic;
	mutex_unlock(&dev->master_mutex);

	DRM_DEBUG("%u\n", auth->magic);

	return ret < 0 ? ret : 0;
}

/**
 * drm_authmagic - Authenticate client with a magic
 * @dev: DRM device to operate on
 * @data: ioctl data containing the drm_auth object
 * @file_priv: DRM file that performs the operation
 *
 * This looks up a DRM client by the passed magic and authenticates it.
 *
 * Returns: 0 on success, negative error code on failure.
 */
int drm_authmagic(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_auth *auth = data;
	struct drm_file *file;

	DRM_DEBUG("%u\n", auth->magic);

	mutex_lock(&dev->master_mutex);
	file = idr_find(&file_priv->master->magic_map, auth->magic);
	if (file) {
		file->authenticated = 1;
		idr_replace(&file_priv->master->magic_map, NULL, auth->magic);
	}
	mutex_unlock(&dev->master_mutex);

	return file ? 0 : -EINVAL;
}

static struct drm_master *drm_master_create(struct drm_device *dev)
{
	struct drm_master *master;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return NULL;

	kref_init(&master->refcount);
	spin_lock_init(&master->lock.spinlock);
	init_waitqueue_head(&master->lock.lock_queue);
	idr_init(&master->magic_map);
	master->dev = dev;

	return master;
}

static int drm_set_master(struct drm_device *dev, struct drm_file *fpriv,
			  bool new_master)
{
	int ret = 0;

	dev->master = drm_master_get(fpriv->master);
	if (dev->driver->master_set) {
		ret = dev->driver->master_set(dev, fpriv, new_master);
		if (unlikely(ret != 0)) {
			drm_master_put(&dev->master);
		}
	}

	return ret;
}

/*
 * drm_new_set_master - Allocate a new master object and become master for the
 * associated master realm.
 *
 * @dev: The associated device.
 * @fpriv: File private identifying the client.
 *
 * This function must be called with dev::master_mutex held.
 * Returns negative error code on failure. Zero on success.
 */
static int drm_new_set_master(struct drm_device *dev, struct drm_file *fpriv)
{
	struct drm_master *old_master;
	int ret;

	lockdep_assert_held_once(&dev->master_mutex);

	old_master = fpriv->master;
	fpriv->master = drm_master_create(dev);
	if (!fpriv->master) {
		fpriv->master = old_master;
		return -ENOMEM;
	}

	if (dev->driver->master_create) {
		ret = dev->driver->master_create(dev, fpriv->master);
		if (ret)
			goto out_err;
	}
	fpriv->is_master = 1;
	fpriv->authenticated = 1;

	ret = drm_set_master(dev, fpriv, true);
	if (ret)
		goto out_err;

	if (old_master)
		drm_master_put(&old_master);

	return 0;

out_err:
	/* drop references and restore old master on failure */
	drm_master_put(&fpriv->master);
	fpriv->master = old_master;

	return ret;
}

int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	int ret = 0;

	mutex_lock(&dev->master_mutex);
	if (drm_is_current_master(file_priv))
		goto out_unlock;

	if (dev->master) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!file_priv->master) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!file_priv->is_master) {
		ret = drm_new_set_master(dev, file_priv);
		goto out_unlock;
	}

	ret = drm_set_master(dev, file_priv, false);
out_unlock:
	mutex_unlock(&dev->master_mutex);
	return ret;
}

static void drm_drop_master(struct drm_device *dev,
			    struct drm_file *fpriv)
{
	if (dev->driver->master_drop)
		dev->driver->master_drop(dev, fpriv);
	drm_master_put(&dev->master);
}

int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	int ret = -EINVAL;

	mutex_lock(&dev->master_mutex);
	if (!drm_is_current_master(file_priv))
		goto out_unlock;

	if (!dev->master)
		goto out_unlock;

	ret = 0;
	drm_drop_master(dev, file_priv);
out_unlock:
	mutex_unlock(&dev->master_mutex);
	return ret;
}

int drm_master_open(struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	int ret = 0;

	/* if there is no current master make this fd it, but do not create
	 * any master object for render clients */
	mutex_lock(&dev->master_mutex);
	if (!dev->master)
		ret = drm_new_set_master(dev, file_priv);
	else
		file_priv->master = drm_master_get(dev->master);
	mutex_unlock(&dev->master_mutex);

	return ret;
}

void drm_master_release(struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_master *master = file_priv->master;

	mutex_lock(&dev->master_mutex);
	if (file_priv->magic)
		idr_remove(&file_priv->master->magic_map, file_priv->magic);

	if (!drm_is_current_master(file_priv))
		goto out;

	if (!drm_core_check_feature(dev, DRIVER_MODESET)) {
		/*
		 * Since the master is disappearing, so is the
		 * possibility to lock.
		 */
		mutex_lock(&dev->struct_mutex);
		if (master->lock.hw_lock) {
			if (dev->sigdata.lock == master->lock.hw_lock)
				dev->sigdata.lock = NULL;
			master->lock.hw_lock = NULL;
			master->lock.file_priv = NULL;
			wake_up_interruptible_all(&master->lock.lock_queue);
		}
		mutex_unlock(&dev->struct_mutex);
	}

	if (dev->master == file_priv->master)
		drm_drop_master(dev, file_priv);
out:
	/* drop the master reference held by the file priv */
	if (file_priv->master)
		drm_master_put(&file_priv->master);
	mutex_unlock(&dev->master_mutex);
}

bool drm_is_current_master(struct drm_file *fpriv)
{
	return fpriv->is_master && fpriv->master == fpriv->minor->dev->master;
}
EXPORT_SYMBOL(drm_is_current_master);

struct drm_master *drm_master_get(struct drm_master *master)
{
	kref_get(&master->refcount);
	return master;
}
EXPORT_SYMBOL(drm_master_get);

static void drm_master_destroy(struct kref *kref)
{
	struct drm_master *master = container_of(kref, struct drm_master, refcount);
	struct drm_device *dev = master->dev;

	if (dev->driver->master_destroy)
		dev->driver->master_destroy(dev, master);

	drm_legacy_master_rmmaps(dev, master);

	idr_destroy(&master->magic_map);
	kfree(master->unique);
	kfree(master);
}

void drm_master_put(struct drm_master **master)
{
	kref_put(&(*master)->refcount, drm_master_destroy);
	*master = NULL;
}
EXPORT_SYMBOL(drm_master_put);
