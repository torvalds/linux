/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */


#include "qxl_drv.h"

/* QXL fencing-

   When we submit operations to the GPU we pass a release reference to the GPU
   with them, the release reference is then added to the release ring when
   the GPU is finished with that particular operation and has removed it from
   its tree.

   So we have can have multiple outstanding non linear fences per object.

   From a TTM POV we only care if the object has any outstanding releases on
   it.

   we wait until all outstanding releases are processeed.

   sync object is just a list of release ids that represent that fence on
   that buffer.

   we just add new releases onto the sync object attached to the object.

   This currently uses a radix tree to store the list of release ids.

   For some reason every so often qxl hw fails to release, things go wrong.
*/


int qxl_fence_add_release(struct qxl_fence *qfence, uint32_t rel_id)
{
	struct qxl_bo *bo = container_of(qfence, struct qxl_bo, fence);

	spin_lock(&bo->tbo.bdev->fence_lock);
	radix_tree_insert(&qfence->tree, rel_id, qfence);
	qfence->num_active_releases++;
	spin_unlock(&bo->tbo.bdev->fence_lock);
	return 0;
}

int qxl_fence_remove_release(struct qxl_fence *qfence, uint32_t rel_id)
{
	void *ret;
	int retval = 0;
	struct qxl_bo *bo = container_of(qfence, struct qxl_bo, fence);

	spin_lock(&bo->tbo.bdev->fence_lock);

	ret = radix_tree_delete(&qfence->tree, rel_id);
	if (ret == qfence)
		qfence->num_active_releases--;
	else {
		DRM_DEBUG("didn't find fence in radix tree for %d\n", rel_id);
		retval = -ENOENT;
	}
	spin_unlock(&bo->tbo.bdev->fence_lock);
	return retval;
}


int qxl_fence_init(struct qxl_device *qdev, struct qxl_fence *qfence)
{
	qfence->qdev = qdev;
	qfence->num_active_releases = 0;
	INIT_RADIX_TREE(&qfence->tree, GFP_ATOMIC);
	return 0;
}

void qxl_fence_fini(struct qxl_fence *qfence)
{
	kfree(qfence->release_ids);
	qfence->num_active_releases = 0;
}
