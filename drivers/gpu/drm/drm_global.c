/**************************************************************************
 *
 * Copyright 2008-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <drm/drm_global.h>

struct drm_global_item {
	struct mutex mutex;
	void *object;
	int refcount;
};

static struct drm_global_item glob[DRM_GLOBAL_NUM];

void drm_global_init(void)
{
	int i;

	for (i = 0; i < DRM_GLOBAL_NUM; ++i) {
		struct drm_global_item *item = &glob[i];
		mutex_init(&item->mutex);
		item->object = NULL;
		item->refcount = 0;
	}
}

void drm_global_release(void)
{
	int i;
	for (i = 0; i < DRM_GLOBAL_NUM; ++i) {
		struct drm_global_item *item = &glob[i];
		BUG_ON(item->object != NULL);
		BUG_ON(item->refcount != 0);
	}
}

int drm_global_item_ref(struct drm_global_reference *ref)
{
	int ret = 0;
	struct drm_global_item *item = &glob[ref->global_type];

	mutex_lock(&item->mutex);
	if (item->refcount == 0) {
		ref->object = kzalloc(ref->size, GFP_KERNEL);
		if (unlikely(ref->object == NULL)) {
			ret = -ENOMEM;
			goto error_unlock;
		}
		ret = ref->init(ref);
		if (unlikely(ret != 0))
			goto error_free;

		item->object = ref->object;
	} else {
		ref->object = item->object;
	}

	++item->refcount;
	mutex_unlock(&item->mutex);
	return 0;

error_free:
	kfree(ref->object);
	ref->object = NULL;
error_unlock:
	mutex_unlock(&item->mutex);
	return ret;
}
EXPORT_SYMBOL(drm_global_item_ref);

void drm_global_item_unref(struct drm_global_reference *ref)
{
	struct drm_global_item *item = &glob[ref->global_type];

	mutex_lock(&item->mutex);
	BUG_ON(item->refcount == 0);
	BUG_ON(ref->object != item->object);
	if (--item->refcount == 0) {
		ref->release(ref);
		item->object = NULL;
	}
	mutex_unlock(&item->mutex);
}
EXPORT_SYMBOL(drm_global_item_unref);

