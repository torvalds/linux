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

#include <linux/slab.h>

#include <drm/drm_auth.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_lease.h>
#include <drm/drm_print.h>

#include "drm_internal.h"
#include "drm_legacy.h"

/**
 * DOC: master and authentication
 *
 * &struct drm_master is used to track groups of clients with open
 * primary/legacy device nodes. For every &struct drm_file which has had at
 * least once successfully became the device master (either through the
 * SET_MASTER IOCTL, or implicitly through opening the primary device node when
 * no one else is the current master that time) there exists one &drm_master.
 * This is noted in &drm_file.is_master. All other clients have just a pointer
 * to the &drm_master they are associated with.
 *
 * In addition only one &drm_master can be the current master for a &drm_device.
 * It can be switched through the DROP_MASTER and SET_MASTER IOCTL, or
 * implicitly through closing/opening the primary device node. See also
 * drm_is_current_master().
 *
 * Clients can authenticate against the current master (if it matches their own)
 * using the GETMAGIC and AUTHMAGIC IOCTLs. Together with exchanging masters,
 * this allows controlled access to the device for an entire group of mutually
 * trusted clients.
 */

static bool drm_is_current_master_locked(struct drm_file *fpriv)
{
	lockdep_assert_once(lockdep_is_held(&fpriv->master_lookup_lock) ||
			    lockdep_is_held(&fpriv->minor->dev->master_mutex));

	return fpriv->is_master && drm_lease_owner(fpriv->master) == fpriv->minor->dev->master;
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
	bool ret;

	spin_lock(&fpriv->master_lookup_lock);
	ret = drm_is_current_master_locked(fpriv);
	spin_unlock(&fpriv->master_lookup_lock);

	return ret;
}
EXPORT_SYMBOL(drm_is_current_master);

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

	drm_dbg_core(dev, "%u\n", auth->magic);

	return ret < 0 ? ret : 0;
}

int drm_authmagic(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_auth *auth = data;
	struct drm_file *file;

	drm_dbg_core(dev, "%u\n", auth->magic);

	mutex_lock(&dev->master_mutex);
	file = idr_find(&file_priv->master->magic_map, auth->magic);
	if (file) {
		file->authenticated = 1;
		idr_replace(&file_priv->master->magic_map, NULL, auth->magic);
	}
	mutex_unlock(&dev->master_mutex);

	return file ? 0 : -EINVAL;
}

struct drm_master *drm_master_create(struct drm_device *dev)
{
	struct drm_master *master;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return NULL;

	kref_init(&master->refcount);
	drm_master_legacy_init(master);
	idr_init_base(&master->magic_map, 1);
	master->dev = dev;

	/* initialize the tree of output resource lessees */
	INIT_LIST_HEAD(&master->lessees);
	INIT_LIST_HEAD(&master->lessee_list);
	idr_init(&master->leases);
	idr_init_base(&master->lessee_idr, 1);

	return master;
}

static void drm_set_master(struct drm_device *dev, struct drm_file *fpriv,
			   bool new_master)
{
	dev->master = drm_master_get(fpriv->master);
	if (dev->driver->master_set)
		dev->driver->master_set(dev, fpriv, new_master);

	fpriv->was_master = true;
}

static int drm_new_set_master(struct drm_device *dev, struct drm_file *fpriv)
{
	struct drm_master *old_master;
	struct drm_master *new_master;

	lockdep_assert_held_once(&dev->master_mutex);

	WARN_ON(fpriv->is_master);
	old_master = fpriv->master;
	new_master = drm_master_create(dev);
	if (!new_master)
		return -ENOMEM;
	spin_lock(&fpriv->master_lookup_lock);
	fpriv->master = new_master;
	spin_unlock(&fpriv->master_lookup_lock);

	fpriv->is_master = 1;
	fpriv->authenticated = 1;

	drm_set_master(dev, fpriv, true);

	if (old_master)
		drm_master_put(&old_master);

	return 0;
}

/*
 * In the olden days the SET/DROP_MASTER ioctls used to return EACCES when
 * CAP_SYS_ADMIN was not set. This was used to prevent rogue applications
 * from becoming master and/or failing to release it.
 *
 * At the same time, the first client (for a given VT) is _always_ master.
 * Thus in order for the ioctls to succeed, one had to _explicitly_ run the
 * application as root or flip the setuid bit.
 *
 * If the CAP_SYS_ADMIN was missing, no other client could become master...
 * EVER :-( Leading to a) the graphics session dying badly or b) a completely
 * locked session.
 *
 *
 * As some point systemd-logind was introduced to orchestrate and delegate
 * master as applicable. It does so by opening the fd and passing it to users
 * while in itself logind a) does the set/drop master per users' request and
 * b)  * implicitly drops master on VT switch.
 *
 * Even though logind looks like the future, there are a few issues:
 *  - some platforms don't have equivalent (Android, CrOS, some BSDs) so
 * root is required _solely_ for SET/DROP MASTER.
 *  - applications may not be updated to use it,
 *  - any client which fails to drop master* can DoS the application using
 * logind, to a varying degree.
 *
 * * Either due missing CAP_SYS_ADMIN or simply not calling DROP_MASTER.
 *
 *
 * Here we implement the next best thing:
 *  - ensure the logind style of fd passing works unchanged, and
 *  - allow a client to drop/set master, iff it is/was master at a given point
 * in time.
 *
 * Note: DROP_MASTER cannot be free for all, as an arbitrator user could:
 *  - DoS/crash the arbitrator - details would be implementation specific
 *  - open the node, become master implicitly and cause issues
 *
 * As a result this fixes the following when using root-less build w/o logind
 * - startx
 * - weston
 * - various compositors based on wlroots
 */
static int
drm_master_check_perm(struct drm_device *dev, struct drm_file *file_priv)
{
	if (file_priv->pid == task_pid(current) && file_priv->was_master)
		return 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	return 0;
}

int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	int ret;

	mutex_lock(&dev->master_mutex);

	ret = drm_master_check_perm(dev, file_priv);
	if (ret)
		goto out_unlock;

	if (drm_is_current_master_locked(file_priv))
		goto out_unlock;

	if (dev->master) {
		ret = -EBUSY;
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

	if (file_priv->master->lessor != NULL) {
		drm_dbg_lease(dev,
			      "Attempt to set lessee %d as master\n",
			      file_priv->master->lessee_id);
		ret = -EINVAL;
		goto out_unlock;
	}

	drm_set_master(dev, file_priv, false);
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
	int ret;

	mutex_lock(&dev->master_mutex);

	ret = drm_master_check_perm(dev, file_priv);
	if (ret)
		goto out_unlock;

	if (!drm_is_current_master_locked(file_priv)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!dev->master) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (file_priv->master->lessor != NULL) {
		drm_dbg_lease(dev,
			      "Attempt to drop lessee %d as master\n",
			      file_priv->master->lessee_id);
		ret = -EINVAL;
		goto out_unlock;
	}

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
	 * any master object for render clients
	 */
	mutex_lock(&dev->master_mutex);
	if (!dev->master) {
		ret = drm_new_set_master(dev, file_priv);
	} else {
		spin_lock(&file_priv->master_lookup_lock);
		file_priv->master = drm_master_get(dev->master);
		spin_unlock(&file_priv->master_lookup_lock);
	}
	mutex_unlock(&dev->master_mutex);

	return ret;
}

void drm_master_release(struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_master *master;

	mutex_lock(&dev->master_mutex);
	master = file_priv->master;
	if (file_priv->magic)
		idr_remove(&file_priv->master->magic_map, file_priv->magic);

	if (!drm_is_current_master_locked(file_priv))
		goto out;

	drm_legacy_lock_master_cleanup(dev, master);

	if (dev->master == file_priv->master)
		drm_drop_master(dev, file_priv);
out:
	if (drm_core_check_feature(dev, DRIVER_MODESET) && file_priv->is_master) {
		/* Revoke any leases held by this or lessees, but only if
		 * this is the "real" master
		 */
		drm_lease_revoke(master);
	}

	/* drop the master reference held by the file priv */
	if (file_priv->master)
		drm_master_put(&file_priv->master);
	mutex_unlock(&dev->master_mutex);
}

/**
 * drm_master_get - reference a master pointer
 * @master: &struct drm_master
 *
 * Increments the reference count of @master and returns a pointer to @master.
 */
struct drm_master *drm_master_get(struct drm_master *master)
{
	kref_get(&master->refcount);
	return master;
}
EXPORT_SYMBOL(drm_master_get);

/**
 * drm_file_get_master - reference &drm_file.master of @file_priv
 * @file_priv: DRM file private
 *
 * Increments the reference count of @file_priv's &drm_file.master and returns
 * the &drm_file.master. If @file_priv has no &drm_file.master, returns NULL.
 *
 * Master pointers returned from this function should be unreferenced using
 * drm_master_put().
 */
struct drm_master *drm_file_get_master(struct drm_file *file_priv)
{
	struct drm_master *master = NULL;

	spin_lock(&file_priv->master_lookup_lock);
	if (!file_priv->master)
		goto unlock;
	master = drm_master_get(file_priv->master);

unlock:
	spin_unlock(&file_priv->master_lookup_lock);
	return master;
}
EXPORT_SYMBOL(drm_file_get_master);

static void drm_master_destroy(struct kref *kref)
{
	struct drm_master *master = container_of(kref, struct drm_master, refcount);
	struct drm_device *dev = master->dev;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_lease_destroy(master);

	drm_legacy_master_rmmaps(dev, master);

	idr_destroy(&master->magic_map);
	idr_destroy(&master->leases);
	idr_destroy(&master->lessee_idr);

	kfree(master->unique);
	kfree(master);
}

/**
 * drm_master_put - unreference and clear a master pointer
 * @master: pointer to a pointer of &struct drm_master
 *
 * This decrements the &drm_master behind @master and sets it to NULL.
 */
void drm_master_put(struct drm_master **master)
{
	kref_put(&(*master)->refcount, drm_master_destroy);
	*master = NULL;
}
EXPORT_SYMBOL(drm_master_put);

/* Used by drm_client and drm_fb_helper */
bool drm_master_internal_acquire(struct drm_device *dev)
{
	mutex_lock(&dev->master_mutex);
	if (dev->master) {
		mutex_unlock(&dev->master_mutex);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(drm_master_internal_acquire);

/* Used by drm_client and drm_fb_helper */
void drm_master_internal_release(struct drm_device *dev)
{
	mutex_unlock(&dev->master_mutex);
}
EXPORT_SYMBOL(drm_master_internal_release);
