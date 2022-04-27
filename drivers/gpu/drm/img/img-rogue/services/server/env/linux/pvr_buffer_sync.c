/*
 * @File
 * @Title       Linux buffer sync interface
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/dma-buf.h>

#include "services_kernel_client.h"
#include "pvr_dma_resv.h"
#include "pvr_buffer_sync.h"
#include "pvr_buffer_sync_shared.h"
#include "pvr_drv.h"
#include "pvr_fence.h"


struct pvr_buffer_sync_context {
	struct mutex ctx_lock;
	struct pvr_fence_context *fence_ctx;
	struct ww_acquire_ctx acquire_ctx;
};

struct pvr_buffer_sync_check_data {
	struct dma_fence_cb base;

	u32 nr_fences;
	struct pvr_fence **fences;
};

struct pvr_buffer_sync_append_data {
	struct pvr_buffer_sync_context *ctx;

	u32 nr_pmrs;
	struct _PMR_ **pmrs;
	u32 *pmr_flags;

	struct pvr_fence *update_fence;
	struct pvr_buffer_sync_check_data *check_data;
};


static struct dma_resv *
pmr_reservation_object_get(struct _PMR_ *pmr)
{
	struct dma_buf *dmabuf;

	dmabuf = PhysmemGetDmaBuf(pmr);
	if (dmabuf)
		return dmabuf->resv;

	return NULL;
}

static int
pvr_buffer_sync_pmrs_lock(struct pvr_buffer_sync_context *ctx,
			  u32 nr_pmrs,
			  struct _PMR_ **pmrs)
{
	struct dma_resv *resv, *cresv = NULL, *lresv = NULL;
	int i, err;
	struct ww_acquire_ctx *acquire_ctx = &ctx->acquire_ctx;

	mutex_lock(&ctx->ctx_lock);

	ww_acquire_init(acquire_ctx, &reservation_ww_class);
retry:
	for (i = 0; i < nr_pmrs; i++) {
		resv = pmr_reservation_object_get(pmrs[i]);
		if (!resv) {
			pr_err("%s: Failed to get reservation object from pmr %p\n",
			       __func__, pmrs[i]);
			err = -EINVAL;
			goto fail;
		}

		if (resv != lresv) {
			err = ww_mutex_lock_interruptible(&resv->lock,
							  acquire_ctx);
			if (err) {
				cresv = (err == -EDEADLK) ? resv : NULL;
				goto fail;
			}
		} else {
			lresv = NULL;
		}
	}

	ww_acquire_done(acquire_ctx);

	return 0;

fail:
	while (i--) {
		resv = pmr_reservation_object_get(pmrs[i]);
		if (WARN_ON_ONCE(!resv))
			continue;
		ww_mutex_unlock(&resv->lock);
	}

	if (lresv)
		ww_mutex_unlock(&lresv->lock);

	if (cresv) {
		err = ww_mutex_lock_slow_interruptible(&cresv->lock,
						       acquire_ctx);
		if (!err) {
			lresv = cresv;
			cresv = NULL;
			goto retry;
		}
	}

	ww_acquire_fini(acquire_ctx);

	mutex_unlock(&ctx->ctx_lock);
	return err;
}

static void
pvr_buffer_sync_pmrs_unlock(struct pvr_buffer_sync_context *ctx,
			    u32 nr_pmrs,
			    struct _PMR_ **pmrs)
{
	struct dma_resv *resv;
	int i;
	struct ww_acquire_ctx *acquire_ctx = &ctx->acquire_ctx;

	for (i = 0; i < nr_pmrs; i++) {
		resv = pmr_reservation_object_get(pmrs[i]);
		if (WARN_ON_ONCE(!resv))
			continue;
		ww_mutex_unlock(&resv->lock);
	}

	ww_acquire_fini(acquire_ctx);

	mutex_unlock(&ctx->ctx_lock);
}

static u32
pvr_buffer_sync_pmrs_fence_count(u32 nr_pmrs, struct _PMR_ **pmrs,
				 u32 *pmr_flags)
{
	struct dma_resv *resv;
	struct dma_resv_list *resv_list;
	struct dma_fence *fence;
	u32 fence_count = 0;
	bool exclusive;
	int i;

	for (i = 0; i < nr_pmrs; i++) {
		exclusive = !!(pmr_flags[i] & PVR_BUFFER_FLAG_WRITE);

		resv = pmr_reservation_object_get(pmrs[i]);
		if (WARN_ON_ONCE(!resv))
			continue;

		resv_list = dma_resv_get_list(resv);
		fence = dma_resv_get_excl(resv);

		if (fence &&
		    (!exclusive || !resv_list || !resv_list->shared_count))
			fence_count++;

		if (exclusive && resv_list)
			fence_count += resv_list->shared_count;
	}

	return fence_count;
}

static struct pvr_buffer_sync_check_data *
pvr_buffer_sync_check_fences_create(struct pvr_fence_context *fence_ctx,
				    PSYNC_CHECKPOINT_CONTEXT sync_checkpoint_ctx,
				    u32 nr_pmrs,
				    struct _PMR_ **pmrs,
				    u32 *pmr_flags)
{
	struct pvr_buffer_sync_check_data *data;
	struct dma_resv *resv;
	struct dma_resv_list *resv_list;
	struct dma_fence *fence;
	u32 fence_count;
	bool exclusive;
	int i, j;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	fence_count = pvr_buffer_sync_pmrs_fence_count(nr_pmrs, pmrs,
						       pmr_flags);
	if (fence_count) {
		data->fences = kcalloc(fence_count, sizeof(*data->fences),
				       GFP_KERNEL);
		if (!data->fences)
			goto err_check_data_free;
	}

	for (i = 0; i < nr_pmrs; i++) {
		resv = pmr_reservation_object_get(pmrs[i]);
		if (WARN_ON_ONCE(!resv))
			continue;

		exclusive = !!(pmr_flags[i] & PVR_BUFFER_FLAG_WRITE);
		if (!exclusive) {
			err = dma_resv_reserve_shared(resv
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
						      , 1
#endif
				);
			if (err)
				goto err_destroy_fences;
		}

		resv_list = dma_resv_get_list(resv);
		fence = dma_resv_get_excl(resv);

		if (fence &&
		    (!exclusive || !resv_list || !resv_list->shared_count)) {
			data->fences[data->nr_fences++] =
				pvr_fence_create_from_fence(fence_ctx,
							    sync_checkpoint_ctx,
							    fence,
							    PVRSRV_NO_FENCE,
							    "exclusive check fence");
			if (!data->fences[data->nr_fences - 1]) {
				data->nr_fences--;
				PVR_FENCE_TRACE(fence,
						"waiting on exclusive fence\n");
				WARN_ON(dma_fence_wait(fence, true) <= 0);
			}
		}

		if (exclusive && resv_list) {
			for (j = 0; j < resv_list->shared_count; j++) {
				fence = rcu_dereference_protected(resv_list->shared[j],
								  dma_resv_held(resv));
				data->fences[data->nr_fences++] =
					pvr_fence_create_from_fence(fence_ctx,
								    sync_checkpoint_ctx,
								    fence,
								    PVRSRV_NO_FENCE,
								    "check fence");
				if (!data->fences[data->nr_fences - 1]) {
					data->nr_fences--;
					PVR_FENCE_TRACE(fence,
							"waiting on non-exclusive fence\n");
					WARN_ON(dma_fence_wait(fence, true) <= 0);
				}
			}
		}
	}

	WARN_ON((i != nr_pmrs) || (data->nr_fences != fence_count));

	return data;

err_destroy_fences:
	for (i = 0; i < data->nr_fences; i++)
		pvr_fence_destroy(data->fences[i]);
	kfree(data->fences);
err_check_data_free:
	kfree(data);
	return NULL;
}

static void
pvr_buffer_sync_check_fences_destroy(struct pvr_buffer_sync_check_data *data)
{
	int i;

	for (i = 0; i < data->nr_fences; i++)
		pvr_fence_destroy(data->fences[i]);

	kfree(data->fences);
	kfree(data);
}

struct pvr_buffer_sync_context *
pvr_buffer_sync_context_create(struct device *dev, const char *name)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = ddev->dev_private;
	struct pvr_buffer_sync_context *ctx;
	int err;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		err = -ENOMEM;
		goto err_exit;
	}

	ctx->fence_ctx = pvr_fence_context_create(priv->dev_node,
						  priv->fence_status_wq,
						  name);
	if (!ctx->fence_ctx) {
		err = -ENOMEM;
		goto err_free_ctx;
	}

	mutex_init(&ctx->ctx_lock);

	return ctx;

err_free_ctx:
	kfree(ctx);
err_exit:
	return ERR_PTR(err);
}

void
pvr_buffer_sync_context_destroy(struct pvr_buffer_sync_context *ctx)
{
	pvr_fence_context_destroy(ctx->fence_ctx);
	kfree(ctx);
}

int
pvr_buffer_sync_resolve_and_create_fences(struct pvr_buffer_sync_context *ctx,
					  PSYNC_CHECKPOINT_CONTEXT sync_checkpoint_ctx,
					  u32 nr_pmrs,
					  struct _PMR_ **pmrs,
					  u32 *pmr_flags,
					  u32 *nr_fence_checkpoints_out,
					  PSYNC_CHECKPOINT **fence_checkpoints_out,
					  PSYNC_CHECKPOINT *update_checkpoints_out,
					  struct pvr_buffer_sync_append_data **data_out)
{
	struct pvr_buffer_sync_append_data *data;
	PSYNC_CHECKPOINT *fence_checkpoints;
	const size_t data_size = sizeof(*data);
	const size_t pmrs_size = sizeof(*pmrs) * nr_pmrs;
	const size_t pmr_flags_size = sizeof(*pmr_flags) * nr_pmrs;
	int i;
	int j;
	int err;

	if (unlikely((nr_pmrs && !(pmrs && pmr_flags)) ||
	    !nr_fence_checkpoints_out || !fence_checkpoints_out ||
	    !update_checkpoints_out))
		return -EINVAL;

	for (i = 0; i < nr_pmrs; i++) {
		if (unlikely(!(pmr_flags[i] & PVR_BUFFER_FLAG_MASK))) {
			pr_err("%s: Invalid flags %#08x for pmr %p\n",
			       __func__, pmr_flags[i], pmrs[i]);
			return -EINVAL;
		}
	}

#if defined(NO_HARDWARE)
	/*
	 * For NO_HARDWARE there's no checking or updating of sync checkpoints
	 * which means SW waits on our fences will cause a deadlock (since they
	 * will never be signalled). Avoid this by not creating any fences.
	 */
	nr_pmrs = 0;
#endif

	if (!nr_pmrs) {
		*nr_fence_checkpoints_out = 0;
		*fence_checkpoints_out = NULL;
		*update_checkpoints_out = NULL;
		*data_out = NULL;

		return 0;
	}

	data = kzalloc(data_size + pmrs_size + pmr_flags_size, GFP_KERNEL);
	if (unlikely(!data))
		return -ENOMEM;

	data->ctx = ctx;
	data->pmrs = (struct _PMR_ **)(void *)(data + 1);
	data->pmr_flags = (u32 *)(void *)(data->pmrs + nr_pmrs);

	/*
	 * It's expected that user space will provide a set of unique PMRs
	 * but, as a PMR can have multiple handles, it's still possible to
	 * end up here with duplicates. Take this opportunity to filter out
	 * any remaining duplicates (updating flags when necessary) before
	 * trying to process them further.
	 */
	for (i = 0; i < nr_pmrs; i++) {
		for (j = 0; j < data->nr_pmrs; j++) {
			if (data->pmrs[j] == pmrs[i]) {
				data->pmr_flags[j] |= pmr_flags[i];
				break;
			}
		}

		if (j == data->nr_pmrs) {
			data->pmrs[j] = pmrs[i];
			data->pmr_flags[j] = pmr_flags[i];
			data->nr_pmrs++;
		}
	}

	err = pvr_buffer_sync_pmrs_lock(ctx, data->nr_pmrs, data->pmrs);
	if (unlikely(err)) {
		/*
		 * -EINTR is returned if a signal arrives while trying to acquire a PMR
		 * lock. In this case the operation should be retried after the signal
		 * has been serviced. As this is expected behaviour, don't print an
		 * error in this case.
		 */
		if (err != -EINTR) {
			pr_err("%s: failed to lock pmrs (errno=%d)\n",
			       __func__, err);
		}
		goto err_free_data;
	}

	/* create the check data */
	data->check_data = pvr_buffer_sync_check_fences_create(ctx->fence_ctx,
							 sync_checkpoint_ctx,
							 data->nr_pmrs,
							 data->pmrs,
							 data->pmr_flags);
	if (unlikely(!data->check_data)) {
		err = -ENOMEM;
		goto err_pmrs_unlock;
	}

	fence_checkpoints = kcalloc(data->check_data->nr_fences,
				    sizeof(*fence_checkpoints),
				    GFP_KERNEL);
	if (fence_checkpoints) {
		pvr_fence_get_checkpoints(data->check_data->fences,
					  data->check_data->nr_fences,
					  fence_checkpoints);
	} else {
		if (unlikely(data->check_data->nr_fences)) {
			err = -ENOMEM;
			goto err_free_check_data;
		}
	}

	/* create the update fence */
	data->update_fence = pvr_fence_create(ctx->fence_ctx,
			sync_checkpoint_ctx,
			SYNC_CHECKPOINT_FOREIGN_CHECKPOINT, "update fence");
	if (unlikely(!data->update_fence)) {
		err = -ENOMEM;
		goto err_free_fence_checkpoints;
	}

	/*
	 * We need to clean up the fences once the HW has finished with them.
	 * We can do this using fence callbacks. However, instead of adding a
	 * callback to every fence, which would result in more work, we can
	 * simply add one to the update fence since this will be the last fence
	 * to be signalled. This callback can do all the necessary clean up.
	 *
	 * Note: we take an additional reference on the update fence in case
	 * it signals before we can add it to a reservation object.
	 */
	PVR_FENCE_TRACE(&data->update_fence->base,
			"create fence calling dma_fence_get\n");
	dma_fence_get(&data->update_fence->base);

	*nr_fence_checkpoints_out = data->check_data->nr_fences;
	*fence_checkpoints_out = fence_checkpoints;
	*update_checkpoints_out = pvr_fence_get_checkpoint(data->update_fence);
	*data_out = data;

	return 0;

err_free_fence_checkpoints:
	kfree(fence_checkpoints);
err_free_check_data:
	pvr_buffer_sync_check_fences_destroy(data->check_data);
err_pmrs_unlock:
	pvr_buffer_sync_pmrs_unlock(ctx, data->nr_pmrs, data->pmrs);
err_free_data:
	kfree(data);
	return err;
}

void
pvr_buffer_sync_kick_succeeded(struct pvr_buffer_sync_append_data *data)
{
	struct dma_resv *resv;
	int i;

	dma_fence_enable_sw_signaling(&data->update_fence->base);

	for (i = 0; i < data->nr_pmrs; i++) {
		resv = pmr_reservation_object_get(data->pmrs[i]);
		if (WARN_ON_ONCE(!resv))
			continue;

		if (data->pmr_flags[i] & PVR_BUFFER_FLAG_WRITE) {
			PVR_FENCE_TRACE(&data->update_fence->base,
					"added exclusive fence (%s) to resv %p\n",
					data->update_fence->name, resv);
			dma_resv_add_excl_fence(resv,
						&data->update_fence->base);
		} else if (data->pmr_flags[i] & PVR_BUFFER_FLAG_READ) {
			PVR_FENCE_TRACE(&data->update_fence->base,
					"added non-exclusive fence (%s) to resv %p\n",
					data->update_fence->name, resv);
			dma_resv_add_shared_fence(resv,
						  &data->update_fence->base);
		}
	}

	/*
	 * Now that the fence has been added to the necessary
	 * reservation objects we can safely drop the extra reference
	 * we took in pvr_buffer_sync_resolve_and_create_fences().
	 */
	dma_fence_put(&data->update_fence->base);
	pvr_buffer_sync_pmrs_unlock(data->ctx, data->nr_pmrs,
					data->pmrs);

	/* destroy the check fences */
	pvr_buffer_sync_check_fences_destroy(data->check_data);
	/* destroy the update fence */
	pvr_fence_destroy(data->update_fence);

	/* free the append data */
	kfree(data);
}

void
pvr_buffer_sync_kick_failed(struct pvr_buffer_sync_append_data *data)
{

	/* drop the extra reference we took on the update fence in
	 * pvr_buffer_sync_resolve_and_create_fences().
	 */
	dma_fence_put(&data->update_fence->base);

	if (data->nr_pmrs > 0)
		pvr_buffer_sync_pmrs_unlock(data->ctx, data->nr_pmrs,
					    data->pmrs);

	/* destroy the check fences */
	pvr_buffer_sync_check_fences_destroy(data->check_data);
	/* destroy the update fence */
	pvr_fence_destroy(data->update_fence);

	/* free the append data */
	kfree(data);
}
