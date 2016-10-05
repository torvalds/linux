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
 * DOC: master and authentication
 *
 * struct &drm_master is used to track groups of clients with open
 * primary/legacy device nodes. For every struct &drm_file which has had at
 * least once successfully became the device master (either through the
 * SET_MASTER IOCTL, or implicitly through opening the primary device node when
 * no one else is the current master that time) there exists one &drm_master.
 * This is noted in the is_master member of &drm_file. All other clients have
 * just a pointer to the &drm_master they are associated with.
 *
 * In addition only one &drm_master can be the current master for a &drm_device.
 * It can be switched through the DROP_MASTER and SET_MASTER IOCTL, or
 * implicitly through closing/openeing the primary device node. See also
 * drm_is_current_master().
 *
 * Clients can authenticate against the current master (if it matches their own)
 * using the GETMAGIC and AUTHMAGIC IOCTLs. Together with exchanging masters,
 * this allows controlled access to the device for an entire group of mutually
 * trusted clients.
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

/**
 * drm_is_current_master - checks whether @priv is the current master
 * @fpriv: DRM file private
 *
 * Checks whether @fpriv is current master on its device. This decides whether a
 * client is allowed to run DRM_MASTER IOCTLs.
 *
 * Most of the modern IOCTL which require DRM_MASTER are for kernel modesetting
 * - the current master is assumed to own the non-shareable display hardware.
 */
bool drm_is_current_master(struct drm_file *fpriv)
{
	return fpriv->is_master && fpriv->master == fpriv->minor->dev->master;
}
EXPORT_SYMBOL(drm_is_current_master);

/**
 * drm_master_get - reference a master pointer
 * @master: struct &drm_master
 *
 * Increments the reference count of @master and returns a pointer to @master.
 */
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

/**
 * drm_master_put - unreference and clear a master pointer
 * @master: pointer to a pointer of struct &drm_master
 *
 * This decrements the &drm_master behind @master and sets it to NULL.
 */
void drm_master_put(struct drm_master **master)
{
	kref_put(&(*master)->refcount, drm_master_destroy);
	*master = NULL;
}
EXPORT_SYMBOL(drm_master_put);
