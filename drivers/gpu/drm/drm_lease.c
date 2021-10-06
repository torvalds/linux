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

/**
 * DOC: drm leasing
 *
 * DRM leases provide information about whether a DRM master may control a DRM
 * mode setting object. This enables the creation of multiple DRM masters that
 * manage subsets of display resources.
 *
 * The original DRM master of a device 'owns' the available drm resources. It
 * may create additional DRM masters and 'lease' resources which it controls
 * to the new DRM master. This gives the new DRM master control over the
 * leased resources until the owner revokes the lease, or the new DRM master
 * is closed. Some helpful terminology:
 *
 * - An 'owner' is a &struct drm_master that is not leasing objects from
 *   another &struct drm_master, and hence 'owns' the objects. The owner can be
 *   identified as the &struct drm_master for which &drm_master.lessor is NULL.
 *
 * - A 'lessor' is a &struct drm_master which is leasing objects to one or more
 *   other &struct drm_master. Currently, lessees are not allowed to
 *   create sub-leases, hence the lessor is the same as the owner.
 *
 * - A 'lessee' is a &struct drm_master which is leasing objects from some
 *   other &struct drm_master. Each lessee only leases resources from a single
 *   lessor recorded in &drm_master.lessor, and holds the set of objects that
 *   it is leasing in &drm_master.leases.
 *
 * - A 'lease' is a contract between the lessor and lessee that identifies
 *   which resources may be controlled by the lessee. All of the resources
 *   that are leased must be owned by or leased to the lessor, and lessors are
 *   not permitted to lease the same object to multiple lessees.
 *
 * The set of objects any &struct drm_master 'controls' is limited to the set
 * of objects it leases (for lessees) or all objects (for owners).
 *
 * Objects not controlled by a &struct drm_master cannot be modified through
 * the various state manipulating ioctls, and any state reported back to user
 * space will be edited to make them appear idle and/or unusable. For
 * instance, connectors always report 'disconnected', while encoders
 * report no possible crtcs or clones.
 *
 * Since each lessee may lease objects from a single lessor, display resource
 * leases form a tree of &struct drm_master. As lessees are currently not
 * allowed to create sub-leases, the tree depth is limited to 1. All of
 * these get activated simultaneously when the top level device owner changes
 * through the SETMASTER or DROPMASTER IOCTL, so &drm_device.master points to
 * the owner at the top of the lease tree (i.e. the &struct drm_master for which
 * &drm_master.lessor is NULL). The full list of lessees that are leasing
 * objects from the owner can be searched via the owner's
 * &drm_master.lessee_idr.
 */

#define drm_for_each_lessee(lessee, lessor) \
	list_for_each_entry((lessee), &(lessor)->lessees, lessee_list)

static uint64_t drm_lease_idr_object;

struct drm_master *drm_lease_owner(struct drm_master *master)
{
	while (master->lessor != NULL)
		master = master->lessor;
	return master;
}

static struct drm_master*
_drm_find_lessee(struct drm_master *master, int lessee_id)
{
	lockdep_assert_held(&master->dev->mode_config.idr_mutex);
	return idr_find(&drm_lease_owner(master)->lessee_idr, lessee_id);
}

static int _drm_lease_held_master(struct drm_master *master, int id)
{
	lockdep_assert_held(&master->dev->mode_config.idr_mutex);
	if (master->lessor)
		return idr_find(&master->leases, id) != NULL;
	return true;
}

/* Checks if the given object has been leased to some lessee of drm_master */
static bool _drm_has_leased(struct drm_master *master, int id)
{
	struct drm_master *lessee;

	lockdep_assert_held(&master->dev->mode_config.idr_mutex);
	drm_for_each_lessee(lessee, master)
		if (_drm_lease_held_master(lessee, id))
			return true;
	return false;
}

/* Called with idr_mutex held */
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

/*
 * Given a bitmask of crtcs to check, reconstructs a crtc mask based on the
 * crtcs which are visible through the specified file.
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

/*
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

/* Return the list of leased objects for the specified lessee */
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

/*
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
