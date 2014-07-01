/*
 * Copyright (C) 2012-2014 Canonical Ltd (Maarten Lankhorst)
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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

#include <linux/reservation.h>
#include <linux/export.h>

DEFINE_WW_CLASS(reservation_ww_class);
EXPORT_SYMBOL(reservation_ww_class);

/*
 * Reserve space to add a shared fence to a reservation_object,
 * must be called with obj->lock held.
 */
int reservation_object_reserve_shared(struct reservation_object *obj)
{
	struct reservation_object_list *fobj, *old;
	u32 max;

	old = reservation_object_get_list(obj);

	if (old && old->shared_max) {
		if (old->shared_count < old->shared_max) {
			/* perform an in-place update */
			kfree(obj->staged);
			obj->staged = NULL;
			return 0;
		} else
			max = old->shared_max * 2;
	} else
		max = 4;

	/*
	 * resize obj->staged or allocate if it doesn't exist,
	 * noop if already correct size
	 */
	fobj = krealloc(obj->staged, offsetof(typeof(*fobj), shared[max]),
			GFP_KERNEL);
	if (!fobj)
		return -ENOMEM;

	obj->staged = fobj;
	fobj->shared_max = max;
	return 0;
}
EXPORT_SYMBOL(reservation_object_reserve_shared);

static void
reservation_object_add_shared_inplace(struct reservation_object *obj,
				      struct reservation_object_list *fobj,
				      struct fence *fence)
{
	u32 i;

	for (i = 0; i < fobj->shared_count; ++i) {
		if (fobj->shared[i]->context == fence->context) {
			struct fence *old_fence = fobj->shared[i];

			fence_get(fence);

			fobj->shared[i] = fence;

			fence_put(old_fence);
			return;
		}
	}

	fence_get(fence);
	fobj->shared[fobj->shared_count] = fence;
	/*
	 * make the new fence visible before incrementing
	 * fobj->shared_count
	 */
	smp_wmb();
	fobj->shared_count++;
}

static void
reservation_object_add_shared_replace(struct reservation_object *obj,
				      struct reservation_object_list *old,
				      struct reservation_object_list *fobj,
				      struct fence *fence)
{
	unsigned i;

	fence_get(fence);

	if (!old) {
		fobj->shared[0] = fence;
		fobj->shared_count = 1;
		goto done;
	}

	/*
	 * no need to bump fence refcounts, rcu_read access
	 * requires the use of kref_get_unless_zero, and the
	 * references from the old struct are carried over to
	 * the new.
	 */
	fobj->shared_count = old->shared_count;

	for (i = 0; i < old->shared_count; ++i) {
		if (fence && old->shared[i]->context == fence->context) {
			fence_put(old->shared[i]);
			fobj->shared[i] = fence;
			fence = NULL;
		} else
			fobj->shared[i] = old->shared[i];
	}
	if (fence)
		fobj->shared[fobj->shared_count++] = fence;

done:
	obj->fence = fobj;
	kfree(old);
}

/*
 * Add a fence to a shared slot, obj->lock must be held, and
 * reservation_object_reserve_shared_fence has been called.
 */
void reservation_object_add_shared_fence(struct reservation_object *obj,
					 struct fence *fence)
{
	struct reservation_object_list *old, *fobj = obj->staged;

	old = reservation_object_get_list(obj);
	obj->staged = NULL;

	if (!fobj) {
		BUG_ON(old->shared_count == old->shared_max);
		reservation_object_add_shared_inplace(obj, old, fence);
	} else
		reservation_object_add_shared_replace(obj, old, fobj, fence);
}
EXPORT_SYMBOL(reservation_object_add_shared_fence);

void reservation_object_add_excl_fence(struct reservation_object *obj,
				       struct fence *fence)
{
	struct fence *old_fence = obj->fence_excl;
	struct reservation_object_list *old;
	u32 i = 0;

	old = reservation_object_get_list(obj);
	if (old) {
		i = old->shared_count;
		old->shared_count = 0;
	}

	if (fence)
		fence_get(fence);

	obj->fence_excl = fence;

	/* inplace update, no shared fences */
	while (i--)
		fence_put(old->shared[i]);

	if (old_fence)
		fence_put(old_fence);
}
EXPORT_SYMBOL(reservation_object_add_excl_fence);
