// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright © 2017 Keith Packard <keithp@keithp.com>
 */
#include <linux/file.h>
#include <linux/uaccess.h>

#include <drm/drm_auth.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_lease.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

#define drm_for_each_lessee(lessee, lessor) \
	list_for_each_entry((lessee), &(lessor)->lessees, lessee_list)

static uint64_t drm_lease_idr_object;

/**
 * drm_lease_owner - return ancestor owner drm_master
 * @master: drm_master somewhere within tree of lessees and lessors
 *
 * RETURN:
 *
 * drm_master at the top of the tree (i.e, with lessor NULL
 */
struct drm_master *drm_lease_owner(struct drm_master *master)
{
	while (master->lessor != NULL)
		master = master->lessor;
	return master;
}

/**
 * _drm_find_lessee - find lessee by id (idr_mutex held)
 * @master: drm_master of lessor
 * @lessee_id: id
 *
 * RETURN:
 *
 * drm_master of the lessee if valid, NULL otherwise
 */

static struct drm_master*
_drm_find_lessee(struct drm_master *master, int lessee_id)
{
	lockdep_assert_held(&master->dev->mode_config.idr_mutex);
	return idr_find(&drm_lease_owner(master)->lessee_idr, lessee_id);
}

/**
 * _drm_lease_held_master - check to see if an object is leased (or owned) by master (idr_mutex held)
 * @master: the master to check the lease status of
 * @id: the id to check
 *
 * Checks if the specified master holds a lease on the object. Return
 * value:
 *
 *	true		'master' holds a lease on (or owns) the object
 *	false		'master' does not hold a lease.
 */
static int _drm_lease_held_master(struct drm_master *master, int id)
{
	lockdep_assert_held(&master->dev->mode_config.idr_mutex);
	if (master->lessor)
		return idr_find(&master->leases, id) != NULL;
	return true;
}

/**
 * _drm_has_leased - check to see if an object has been leased (idr_mutex held)
 * @master: the master to check the lease status of
 * @id: the id to check
 *
 * Checks if any lessee of 'master' holds a lease on 'id'. Return
 * value:
 *
 *	true		Some lessee holds a lease on the object.
 *	false		No lessee has a lease on the object.
 */
static bool _drm_has_leased(struct drm_master *master, int id)
{
	struct drm_master *lessee;

	lockdep_assert_held(&master->dev->mode_config.idr_mutex);
	drm_for_each_lessee(lessee, master)
		if (_drm_lease_held_master(lessee, id))
			return true;
	return false;
}

/**
 * _drm_lease_held - check drm_mode_object lease status (idr_mutex held)
 * @file_priv: the master drm_file
 * @id: the object id
 *
 * Checks if the specified master holds a lease on the object. Return
 * value:
 *
 *	true		'master' holds a lease on (or owns) the object
 *	false		'master' does not hold a lease.
 */
bool _drm_lease_held(struct drm_file *file_priv, int id)
{
	bool ret;
	struct drm_master *master;

	if (!file_priv)
		return true;

	master = drm_file_get_master(file_priv);
	if (!master)
		return true;
	ret = _drm_lease_held_master(master, id);
	drm_master_put(&master);

	return ret;
}

/**
 * drm_lease_held - check drm_mode_object lease status (idr_mutex not held)
 * @file_priv: the master drm_file
 * @id: the object id
 *
 * Checks if the specified master holds a lease on the object. Return
 * value:
 *
 *	true		'master' holds a lease on (or owns) the object
 *	false		'master' does not hold a lease.
 */
bool drm_lease_held(struct drm_file *file_priv, int id)
{
	struct drm_master *master;
	bool ret;

	if (!file_priv)
		return true;

	master = drm_file_get_master(file_priv);
	if (!master)
		return true;
	if (!master->lessor) {
		ret = true;
		goto out;
	}
	mutex_lock(&master->dev->mode_config.idr_mutex);
	ret = _drm_lease_held_master(master, id);
	mutex_unlock(&master->dev->mode_config.idr_mutex);

out:
	drm_master_put(&master);
	return ret;
}

/**
 * drm_lease_filter_crtcs - restricted crtc set to leased values (idr_mutex not held)
 * @file_priv: requestor file
 * @crtcs_in: bitmask of crtcs to check
 *
 * Reconstructs a crtc mask based on the crtcs which are visible
 * through the specified file.
 */
uint32_t drm_lease_filter_crtcs(struct drm_file *file_priv, uint32_t crtcs_in)
{
	struct drm_master *master;
	struct drm_device *dev;
	struct drm_crtc *crtc;
	int count_in, count_out;
	uint32_t crtcs_out = 0;

	if (!file_priv)
		return crtcs_in;

	master = drm_file_get_master(file_priv);
	if (!master)
		return crtcs_in;
	if (!master->lessor) {
		crtcs_out = crtcs_in;
		goto out;
	}
	dev = master->dev;

	count_in = count_out = 0;
	mutex_lock(&master->dev->mode_config.idr_mutex);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (_drm_lease_held_master(master, crtc->base.id)) {
			uint32_t mask_in = 1ul << count_in;

			if ((crtcs_in & mask_in) != 0) {
				uint32_t mask_out = 1ul << count_out;

				crtcs_out |= mask_out;
			}
			count_out++;
		}
		count_in++;
	}
	mutex_unlock(&master->dev->mode_config.idr_mutex);

out:
	drm_master_put(&master);
	return crtcs_out;
}

/*
 * drm_lease_create - create a new drm_master with leased objects (idr_mutex not held)
 * @lessor: lease holder (or owner) of objects
 * @leases: objects to lease to the new drm_master
 *
 * Uses drm_master_create to allocate a new drm_master, then checks to
 * make sure all of the desired objects can be leased, atomically
 * leasing them to the new drmmaster.
 *
 * 	ERR_PTR(-EACCES)	some other master holds the title to any object
 * 	ERR_PTR(-ENOENT)	some object is not a valid DRM object for this device
 * 	ERR_PTR(-EBUSY)		some other lessee holds title to this object
 *	ERR_PTR(-EEXIST)	same object specified more than once in the provided list
 *	ERR_PTR(-ENOMEM)	allocation failed
 */
static struct drm_master *drm_lease_create(struct drm_master *lessor, struct idr *leases)
{
	struct drm_device *dev = lessor->dev;
	int error;
	struct drm_master *lessee;
	int object;
	int id;
	void *entry;

	DRM_DEBUG_LEASE("lessor %d\n", lessor->lessee_id);

	lessee = drm_master_create(lessor->dev);
	if (!lessee) {
		DRM_DEBUG_LEASE("drm_master_create failed\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&dev->mode_config.idr_mutex);

	idr_for_each_entry(leases, entry, object) {
		error = 0;
		if (!idr_find(&dev->mode_config.object_idr, object))
			error = -ENOENT;
		else if (_drm_has_leased(lessor, object))
			error = -EBUSY;

		if (error != 0) {
			DRM_DEBUG_LEASE("object %d failed %d\n", object, error);
			goto out_lessee;
		}
	}

	/* Insert the new lessee into the tree */
	id = idr_alloc(&(drm_lease_owner(lessor)->lessee_idr), lessee, 1, 0, GFP_KERNEL);
	if (id < 0) {
		error = id;
		goto out_lessee;
	}

	lessee->lessee_id = id;
	lessee->lessor = drm_master_get(lessor);
	list_add_tail(&lessee->lessee_list, &lessor->lessees);

	/* Move the leases over */
	lessee->leases = *leases;
	DRM_DEBUG_LEASE("new lessee %d %p, lessor %d %p\n", lessee->lessee_id, lessee, lessor->lessee_id, lessor);

	mutex_unlock(&dev->mode_config.idr_mutex);
	return lessee;

out_lessee:
	mutex_unlock(&dev->mode_config.idr_mutex);

	drm_master_put(&lessee);

	return ERR_PTR(error);
}

/**
 * drm_lease_destroy - a master is going away (idr_mutex not held)
 * @master: the drm_master being destroyed
 *
 * All lessees will have been destroyed as they
 * hold a reference on their lessor. Notify any
 * lessor for this master so that it can check
 * the list of lessees.
 */
void drm_lease_destroy(struct drm_master *master)
{
	struct drm_device *dev = master->dev;

	mutex_lock(&dev->mode_config.idr_mutex);

	DRM_DEBUG_LEASE("drm_lease_destroy %d\n", master->lessee_id);

	/* This master is referenced by all lessees, hence it cannot be destroyed
	 * until all of them have been
	 */
	WARN_ON(!list_empty(&master->lessees));

	/* Remove this master from the lessee idr in the owner */
	if (master->lessee_id != 0) {
		DRM_DEBUG_LEASE("remove master %d from device list of lessees\n", master->lessee_id);
		idr_remove(&(drm_lease_owner(master)->lessee_idr), master->lessee_id);
	}

	/* Remove this master from any lessee list it may be on */
	list_del(&master->lessee_list);

	mutex_unlock(&dev->mode_config.idr_mutex);

	if (master->lessor) {
		/* Tell the master to check the lessee list */
		drm_sysfs_lease_event(dev);
		drm_master_put(&master->lessor);
	}

	DRM_DEBUG_LEASE("drm_lease_destroy done %d\n", master->lessee_id);
}

/**
 * _drm_lease_revoke - revoke access to all leased objects (idr_mutex held)
 * @top: the master losing its lease
 */
static void _drm_lease_revoke(struct drm_master *top)
{
	int object;
	void *entry;
	struct drm_master *master = top;

	lockdep_assert_held(&top->dev->mode_config.idr_mutex);

	/*
	 * Walk the tree starting at 'top' emptying all leases. Because
	 * the tree is fully connected, we can do this without recursing
	 */
	for (;;) {
		DRM_DEBUG_LEASE("revoke leases for %p %d\n", master, master->lessee_id);

		/* Evacuate the lease */
		idr_for_each_entry(&master->leases, entry, object)
			idr_remove(&master->leases, object);

		/* Depth-first list walk */

		/* Down */
		if (!list_empty(&master->lessees)) {
			master = list_first_entry(&master->lessees, struct drm_master, lessee_list);
		} else {
			/* Up */
			while (master != top && master == list_last_entry(&master->lessor->lessees, struct drm_master, lessee_list))
				master = master->lessor;

			if (master == top)
				break;

			/* Over */
			master = list_next_entry(master, lessee_list);
		}
	}
}

/**
 * drm_lease_revoke - revoke access to all leased objects (idr_mutex not held)
 * @top: the master losing its lease
 */
void drm_lease_revoke(struct drm_master *top)
{
	mutex_lock(&top->dev->mode_config.idr_mutex);
	_drm_lease_revoke(top);
	mutex_unlock(&top->dev->mode_config.idr_mutex);
}

static int validate_lease(struct drm_device *dev,
			  int object_count,
			  struct drm_mode_object **objects,
			  bool universal_planes)
{
	int o;
	int has_crtc = -1;
	int has_connector = -1;
	int has_plane = -1;

	/* we want to confirm that there is at least one crtc, plane
	   connector object. */

	for (o = 0; o < object_count; o++) {
		if (objects[o]->type == DRM_MODE_OBJECT_CRTC && has_crtc == -1) {
			has_crtc = o;
		}
		if (objects[o]->type == DRM_MODE_OBJECT_CONNECTOR && has_connector == -1)
			has_connector = o;

		if (universal_planes) {
			if (objects[o]->type == DRM_MODE_OBJECT_PLANE && has_plane == -1)
				has_plane = o;
		}
	}
	if (has_crtc == -1 || has_connector == -1)
		return -EINVAL;
	if (universal_planes && has_plane == -1)
		return -EINVAL;
	return 0;
}

static int fill_object_idr(struct drm_device *dev,
			   struct drm_file *lessor_priv,
			   struct idr *leases,
			   int object_count,
			   u32 *object_ids)
{
	struct drm_mode_object **objects;
	u32 o;
	int ret;
	bool universal_planes = READ_ONCE(lessor_priv->universal_planes);

	objects = kcalloc(object_count, sizeof(struct drm_mode_object *),
			  GFP_KERNEL);
	if (!objects)
		return -ENOMEM;

	/* step one - get references to all the mode objects
	   and check for validity. */
	for (o = 0; o < object_count; o++) {
		objects[o] = drm_mode_object_find(dev, lessor_priv,
						  object_ids[o],
						  DRM_MODE_OBJECT_ANY);
		if (!objects[o]) {
			ret = -ENOENT;
			goto out_free_objects;
		}

		if (!drm_mode_object_lease_required(objects[o]->type)) {
			DRM_DEBUG_KMS("invalid object for lease\n");
			ret = -EINVAL;
			goto out_free_objects;
		}
	}

	ret = validate_lease(dev, object_count, objects, universal_planes);
	if (ret) {
		DRM_DEBUG_LEASE("lease validation failed\n");
		goto out_free_objects;
	}

	/* add their IDs to the lease request - taking into account
	   universal planes */
	for (o = 0; o < object_count; o++) {
		struct drm_mode_object *obj = objects[o];
		u32 object_id = objects[o]->id;

		DRM_DEBUG_LEASE("Adding object %d to lease\n", object_id);

		/*
		 * We're using an IDR to hold the set of leased
		 * objects, but we don't need to point at the object's
		 * data structure from the lease as the main object_idr
		 * will be used to actually find that. Instead, all we
		 * really want is a 'leased/not-leased' result, for
		 * which any non-NULL pointer will work fine.
		 */
		ret = idr_alloc(leases, &drm_lease_idr_object , object_id, object_id + 1, GFP_KERNEL);
		if (ret < 0) {
			DRM_DEBUG_LEASE("Object %d cannot be inserted into leases (%d)\n",
					object_id, ret);
			goto out_free_objects;
		}
		if (obj->type == DRM_MODE_OBJECT_CRTC && !universal_planes) {
			struct drm_crtc *crtc = obj_to_crtc(obj);

			ret = idr_alloc(leases, &drm_lease_idr_object, crtc->primary->base.id, crtc->primary->base.id + 1, GFP_KERNEL);
			if (ret < 0) {
				DRM_DEBUG_LEASE("Object primary plane %d cannot be inserted into leases (%d)\n",
						object_id, ret);
				goto out_free_objects;
			}
			if (crtc->cursor) {
				ret = idr_alloc(leases, &drm_lease_idr_object, crtc->cursor->base.id, crtc->cursor->base.id + 1, GFP_KERNEL);
				if (ret < 0) {
					DRM_DEBUG_LEASE("Object cursor plane %d cannot be inserted into leases (%d)\n",
							object_id, ret);
					goto out_free_objects;
				}
			}
		}
	}

	ret = 0;
out_free_objects:
	for (o = 0; o < object_count; o++) {
		if (objects[o])
			drm_mode_object_put(objects[o]);
	}
	kfree(objects);
	return ret;
}

/**
 * drm_mode_create_lease_ioctl - create a new lease
 * @dev: the drm device
 * @data: pointer to struct drm_mode_create_lease
 * @lessor_priv: the file being manipulated
 *
 * The master associated with the specified file will have a lease
 * created containing the objects specified in the ioctl structure.
 * A file descriptor will be allocated for that and returned to the
 * application.
 */
int drm_mode_create_lease_ioctl(struct drm_device *dev,
				void *data, struct drm_file *lessor_priv)
{
	struct drm_mode_create_lease *cl = data;
	size_t object_count;
	int ret = 0;
	struct idr leases;
	struct drm_master *lessor;
	struct drm_master *lessee = NULL;
	struct file *lessee_file = NULL;
	struct file *lessor_file = lessor_priv->filp;
	struct drm_file *lessee_priv;
	int fd = -1;
	uint32_t *object_ids;

	/* Can't lease without MODESET */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	/* need some objects */
	if (cl->object_count == 0) {
		DRM_DEBUG_LEASE("no objects in lease\n");
		return -EINVAL;
	}

	if (cl->flags && (cl->flags & ~(O_CLOEXEC | O_NONBLOCK))) {
		DRM_DEBUG_LEASE("invalid flags\n");
		return -EINVAL;
	}

	lessor = drm_file_get_master(lessor_priv);
	/* Do not allow sub-leases */
	if (lessor->lessor) {
		DRM_DEBUG_LEASE("recursive leasing not allowed\n");
		ret = -EINVAL;
		goto out_lessor;
	}

	object_count = cl->object_count;

	object_ids = memdup_user(u64_to_user_ptr(cl->object_ids),
			array_size(object_count, sizeof(__u32)));
	if (IS_ERR(object_ids)) {
		ret = PTR_ERR(object_ids);
		goto out_lessor;
	}

	idr_init(&leases);

	/* fill and validate the object idr */
	ret = fill_object_idr(dev, lessor_priv, &leases,
			      object_count, object_ids);
	kfree(object_ids);
	if (ret) {
		DRM_DEBUG_LEASE("lease object lookup failed: %i\n", ret);
		idr_destroy(&leases);
		goto out_lessor;
	}

	/* Allocate a file descriptor for the lease */
	fd = get_unused_fd_flags(cl->flags & (O_CLOEXEC | O_NONBLOCK));
	if (fd < 0) {
		idr_destroy(&leases);
		ret = fd;
		goto out_lessor;
	}

	DRM_DEBUG_LEASE("Creating lease\n");
	/* lessee will take the ownership of leases */
	lessee = drm_lease_create(lessor, &leases);

	if (IS_ERR(lessee)) {
		ret = PTR_ERR(lessee);
		idr_destroy(&leases);
		goto out_leases;
	}

	/* Clone the lessor file to create a new file for us */
	DRM_DEBUG_LEASE("Allocating lease file\n");
	lessee_file = file_clone_open(lessor_file);
	if (IS_ERR(lessee_file)) {
		ret = PTR_ERR(lessee_file);
		goto out_lessee;
	}

	lessee_priv = lessee_file->private_data;
	/* Change the file to a master one */
	drm_master_put(&lessee_priv->master);
	lessee_priv->master = lessee;
	lessee_priv->is_master = 1;
	lessee_priv->authenticated = 1;

	/* Pass fd back to userspace */
	DRM_DEBUG_LEASE("Returning fd %d id %d\n", fd, lessee->lessee_id);
	cl->fd = fd;
	cl->lessee_id = lessee->lessee_id;

	/* Hook up the fd */
	fd_install(fd, lessee_file);

	drm_master_put(&lessor);
	DRM_DEBUG_LEASE("drm_mode_create_lease_ioctl succeeded\n");
	return 0;

out_lessee:
	drm_master_put(&lessee);

out_leases:
	put_unused_fd(fd);

out_lessor:
	drm_master_put(&lessor);
	DRM_DEBUG_LEASE("drm_mode_create_lease_ioctl failed: %d\n", ret);
	return ret;
}

/**
 * drm_mode_list_lessees_ioctl - list lessee ids
 * @dev: the drm device
 * @data: pointer to struct drm_mode_list_lessees
 * @lessor_priv: the file being manipulated
 *
 * Starting from the master associated with the specified file,
 * the master with the provided lessee_id is found, and then
 * an array of lessee ids associated with leases from that master
 * are returned.
 */

int drm_mode_list_lessees_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *lessor_priv)
{
	struct drm_mode_list_lessees *arg = data;
	__u32 __user *lessee_ids = (__u32 __user *) (uintptr_t) (arg->lessees_ptr);
	__u32 count_lessees = arg->count_lessees;
	struct drm_master *lessor, *lessee;
	int count;
	int ret = 0;

	if (arg->pad)
		return -EINVAL;

	/* Can't lease without MODESET */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	lessor = drm_file_get_master(lessor_priv);
	DRM_DEBUG_LEASE("List lessees for %d\n", lessor->lessee_id);

	mutex_lock(&dev->mode_config.idr_mutex);

	count = 0;
	drm_for_each_lessee(lessee, lessor) {
		/* Only list un-revoked leases */
		if (!idr_is_empty(&lessee->leases)) {
			if (count_lessees > count) {
				DRM_DEBUG_LEASE("Add lessee %d\n", lessee->lessee_id);
				ret = put_user(lessee->lessee_id, lessee_ids + count);
				if (ret)
					break;
			}
			count++;
		}
	}

	DRM_DEBUG_LEASE("Lessor leases to %d\n", count);
	if (ret == 0)
		arg->count_lessees = count;

	mutex_unlock(&dev->mode_config.idr_mutex);
	drm_master_put(&lessor);

	return ret;
}

/**
 * drm_mode_get_lease_ioctl - list leased objects
 * @dev: the drm device
 * @data: pointer to struct drm_mode_get_lease
 * @lessee_priv: the file being manipulated
 *
 * Return the list of leased objects for the specified lessee
 */

int drm_mode_get_lease_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *lessee_priv)
{
	struct drm_mode_get_lease *arg = data;
	__u32 __user *object_ids = (__u32 __user *) (uintptr_t) (arg->objects_ptr);
	__u32 count_objects = arg->count_objects;
	struct drm_master *lessee;
	struct idr *object_idr;
	int count;
	void *entry;
	int object;
	int ret = 0;

	if (arg->pad)
		return -EINVAL;

	/* Can't lease without MODESET */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	lessee = drm_file_get_master(lessee_priv);
	DRM_DEBUG_LEASE("get lease for %d\n", lessee->lessee_id);

	mutex_lock(&dev->mode_config.idr_mutex);

	if (lessee->lessor == NULL)
		/* owner can use all objects */
		object_idr = &lessee->dev->mode_config.object_idr;
	else
		/* lessee can only use allowed object */
		object_idr = &lessee->leases;

	count = 0;
	idr_for_each_entry(object_idr, entry, object) {
		if (count_objects > count) {
			DRM_DEBUG_LEASE("adding object %d\n", object);
			ret = put_user(object, object_ids + count);
			if (ret)
				break;
		}
		count++;
	}

	DRM_DEBUG("lease holds %d objects\n", count);
	if (ret == 0)
		arg->count_objects = count;

	mutex_unlock(&dev->mode_config.idr_mutex);
	drm_master_put(&lessee);

	return ret;
}

/**
 * drm_mode_revoke_lease_ioctl - revoke lease
 * @dev: the drm device
 * @data: pointer to struct drm_mode_revoke_lease
 * @lessor_priv: the file being manipulated
 *
 * This removes all of the objects from the lease without
 * actually getting rid of the lease itself; that way all
 * references to it still work correctly
 */
int drm_mode_revoke_lease_ioctl(struct drm_device *dev,
				void *data, struct drm_file *lessor_priv)
{
	struct drm_mode_revoke_lease *arg = data;
	struct drm_master *lessor;
	struct drm_master *lessee;
	int ret = 0;

	DRM_DEBUG_LEASE("revoke lease for %d\n", arg->lessee_id);

	/* Can't lease without MODESET */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	lessor = drm_file_get_master(lessor_priv);
	mutex_lock(&dev->mode_config.idr_mutex);

	lessee = _drm_find_lessee(lessor, arg->lessee_id);

	/* No such lessee */
	if (!lessee) {
		ret = -ENOENT;
		goto fail;
	}

	/* Lease is not held by lessor */
	if (lessee->lessor != lessor) {
		ret = -EACCES;
		goto fail;
	}

	_drm_lease_revoke(lessee);

fail:
	mutex_unlock(&dev->mode_config.idr_mutex);
	drm_master_put(&lessor);

	return ret;
}
