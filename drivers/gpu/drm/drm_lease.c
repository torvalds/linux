/*
 * Copyright © 2017 Keith Packard <keithp@keithp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <drm/drmP.h>
#include "drm_internal.h"
#include "drm_legacy.h"
#include "drm_crtc_internal.h"
#include <drm/drm_lease.h>
#include <drm/drm_auth.h>
#include <drm/drm_crtc_helper.h>

#define drm_for_each_lessee(lessee, lessor) \
	list_for_each_entry((lessee), &(lessor)->lessees, lessee_list)

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
EXPORT_SYMBOL(drm_lease_owner);

/**
 * _drm_find_lessee - find lessee by id (idr_mutex held)
 * @master: drm_master of lessor
 * @id: lessee_id
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
 * @master: the drm_master
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
	if (file_priv == NULL || file_priv->master == NULL)
		return true;

	return _drm_lease_held_master(file_priv->master, id);
}
EXPORT_SYMBOL(_drm_lease_held);

/**
 * drm_lease_held - check drm_mode_object lease status (idr_mutex not held)
 * @master: the drm_master
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

	if (file_priv == NULL || file_priv->master == NULL)
		return true;

	master = file_priv->master;
	mutex_lock(&master->dev->mode_config.idr_mutex);
	ret = _drm_lease_held_master(master, id);
	mutex_unlock(&master->dev->mode_config.idr_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_lease_held);

/**
 * drm_lease_filter_crtcs - restricted crtc set to leased values (idr_mutex not held)
 * @file_priv: requestor file
 * @crtcs: bitmask of crtcs to check
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

	if (file_priv == NULL || file_priv->master == NULL)
		return crtcs_in;

	master = file_priv->master;
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
	return crtcs_out;
}
EXPORT_SYMBOL(drm_lease_filter_crtcs);

/*
 * drm_lease_create - create a new drm_master with leased objects (idr_mutex not held)
 * @lessor: lease holder (or owner) of objects
 * @leases: objects to lease to the new drm_master
 *
 * Uses drm_master_create to allocate a new drm_master, then checks to
 * make sure all of the desired objects can be leased, atomically
 * leasing them to the new drmmaster.
 *
 * 	ERR_PTR(-EACCESS)	some other master holds the title to any object
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

	/* Insert the new lessee into the tree */
	id = idr_alloc(&(drm_lease_owner(lessor)->lessee_idr), lessee, 1, 0, GFP_KERNEL);
	if (id < 0) {
		error = id;
		goto out_lessee;
	}

	lessee->lessee_id = id;
	lessee->lessor = drm_master_get(lessor);
	list_add_tail(&lessee->lessee_list, &lessor->lessees);

	idr_for_each_entry(leases, entry, object) {
		error = 0;
		if (!idr_find(&dev->mode_config.crtc_idr, object))
			error = -ENOENT;
		else if (!_drm_lease_held_master(lessor, object))
			error = -EACCES;
		else if (_drm_has_leased(lessor, object))
			error = -EBUSY;

		if (error != 0) {
			DRM_DEBUG_LEASE("object %d failed %d\n", object, error);
			goto out_lessee;
		}
	}

	/* Move the leases over */
	lessee->leases = *leases;
	DRM_DEBUG_LEASE("new lessee %d %p, lessor %d %p\n", lessee->lessee_id, lessee, lessor->lessee_id, lessor);

	mutex_unlock(&dev->mode_config.idr_mutex);
	return lessee;

out_lessee:
	drm_master_put(&lessee);

	mutex_unlock(&dev->mode_config.idr_mutex);

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
		drm_sysfs_hotplug_event(dev);
		drm_master_put(&master->lessor);
	}

	DRM_DEBUG_LEASE("drm_lease_destroy done %d\n", master->lessee_id);
}

/**
 * _drm_lease_revoke - revoke access to all leased objects (idr_mutex held)
 * @master: the master losing its lease
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
			master = list_entry(master->lessee_list.next, struct drm_master, lessee_list);
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
