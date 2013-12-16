/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "msm_drv.h"
#include "msm_gem.h"


struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	BUG_ON(!msm_obj->sgt);  /* should have already pinned! */
	return msm_obj->sgt;
}

void *msm_gem_prime_vmap(struct drm_gem_object *obj)
{
	return msm_gem_vaddr(obj);
}

void msm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	/* TODO msm_gem_vunmap() */
}

struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		size_t size, struct sg_table *sg)
{
	return msm_gem_import(dev, size, sg);
}

int msm_gem_prime_pin(struct drm_gem_object *obj)
{
	if (!obj->import_attach)
		msm_gem_get_pages(obj);
	return 0;
}

void msm_gem_prime_unpin(struct drm_gem_object *obj)
{
	if (!obj->import_attach)
		msm_gem_put_pages(obj);
}
