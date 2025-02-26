// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_syncobj.h>
#include <linux/hmm.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <trace/events/amdxdna.h>

#include "aie2_msg_priv.h"
#include "aie2_pci.h"
#include "aie2_solver.h"
#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_pci_drv.h"

static bool force_cmdlist;
module_param(force_cmdlist, bool, 0600);
MODULE_PARM_DESC(force_cmdlist, "Force use command list (Default false)");

#define HWCTX_MAX_TIMEOUT	60000 /* milliseconds */

static void aie2_job_release(struct kref *ref)
{
	struct amdxdna_sched_job *job;

	job = container_of(ref, struct amdxdna_sched_job, refcnt);
	amdxdna_sched_job_cleanup(job);
	if (job->out_fence)
		dma_fence_put(job->out_fence);
	kfree(job);
}

static void aie2_job_put(struct amdxdna_sched_job *job)
{
	kref_put(&job->refcnt, aie2_job_release);
}

/* The bad_job is used in aie2_sched_job_timedout, otherwise, set it to NULL */
static void aie2_hwctx_stop(struct amdxdna_dev *xdna, struct amdxdna_hwctx *hwctx,
			    struct drm_sched_job *bad_job)
{
	drm_sched_stop(&hwctx->priv->sched, bad_job);
	aie2_destroy_context(xdna->dev_handle, hwctx);
}

static int aie2_hwctx_restart(struct amdxdna_dev *xdna, struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_gem_obj *heap = hwctx->priv->heap;
	int ret;

	ret = aie2_create_context(xdna->dev_handle, hwctx);
	if (ret) {
		XDNA_ERR(xdna, "Create hwctx failed, ret %d", ret);
		goto out;
	}

	ret = aie2_map_host_buf(xdna->dev_handle, hwctx->fw_ctx_id,
				heap->mem.userptr, heap->mem.size);
	if (ret) {
		XDNA_ERR(xdna, "Map host buf failed, ret %d", ret);
		goto out;
	}

	if (hwctx->status != HWCTX_STAT_READY) {
		XDNA_DBG(xdna, "hwctx is not ready, status %d", hwctx->status);
		goto out;
	}

	ret = aie2_config_cu(hwctx);
	if (ret) {
		XDNA_ERR(xdna, "Config cu failed, ret %d", ret);
		goto out;
	}

out:
	drm_sched_start(&hwctx->priv->sched, 0);
	XDNA_DBG(xdna, "%s restarted, ret %d", hwctx->name, ret);
	return ret;
}

void aie2_restart_ctx(struct amdxdna_client *client)
{
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_hwctx *hwctx;
	unsigned long hwctx_id;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));
	mutex_lock(&client->hwctx_lock);
	amdxdna_for_each_hwctx(client, hwctx_id, hwctx) {
		if (hwctx->status != HWCTX_STAT_STOP)
			continue;

		hwctx->status = hwctx->old_status;
		XDNA_DBG(xdna, "Resetting %s", hwctx->name);
		aie2_hwctx_restart(xdna, hwctx);
	}
	mutex_unlock(&client->hwctx_lock);
}

static struct dma_fence *aie2_cmd_get_out_fence(struct amdxdna_hwctx *hwctx, u64 seq)
{
	struct dma_fence *fence, *out_fence = NULL;
	int ret;

	fence = drm_syncobj_fence_get(hwctx->priv->syncobj);
	if (!fence)
		return NULL;

	ret = dma_fence_chain_find_seqno(&fence,  seq);
	if (ret)
		goto out;

	out_fence = dma_fence_get(dma_fence_chain_contained(fence));

out:
	dma_fence_put(fence);
	return out_fence;
}

static void aie2_hwctx_wait_for_idle(struct amdxdna_hwctx *hwctx)
{
	struct dma_fence *fence;

	fence = aie2_cmd_get_out_fence(hwctx, hwctx->priv->seq - 1);
	if (!fence)
		return;

	dma_fence_wait(fence, false);
	dma_fence_put(fence);
}

void aie2_hwctx_suspend(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;

	/*
	 * Command timeout is unlikely. But if it happens, it doesn't
	 * break the system. aie2_hwctx_stop() will destroy mailbox
	 * and abort all commands.
	 */
	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));
	aie2_hwctx_wait_for_idle(hwctx);
	aie2_hwctx_stop(xdna, hwctx, NULL);
	hwctx->old_status = hwctx->status;
	hwctx->status = HWCTX_STAT_STOP;
}

void aie2_hwctx_resume(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;

	/*
	 * The resume path cannot guarantee that mailbox channel can be
	 * regenerated. If this happen, when submit message to this
	 * mailbox channel, error will return.
	 */
	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));
	hwctx->status = hwctx->old_status;
	aie2_hwctx_restart(xdna, hwctx);
}

static void
aie2_sched_notify(struct amdxdna_sched_job *job)
{
	struct dma_fence *fence = job->fence;

	trace_xdna_job(&job->base, job->hwctx->name, "signaled fence", job->seq);
	job->hwctx->priv->completed++;
	dma_fence_signal(fence);

	up(&job->hwctx->priv->job_sem);
	job->job_done = true;
	dma_fence_put(fence);
	mmput_async(job->mm);
	aie2_job_put(job);
}

static int
aie2_sched_resp_handler(void *handle, const u32 *data, size_t size)
{
	struct amdxdna_sched_job *job = handle;
	struct amdxdna_gem_obj *cmd_abo;
	u32 ret = 0;
	u32 status;

	cmd_abo = job->cmd_bo;

	if (unlikely(!data))
		goto out;

	if (unlikely(size != sizeof(u32))) {
		amdxdna_cmd_set_state(cmd_abo, ERT_CMD_STATE_ABORT);
		ret = -EINVAL;
		goto out;
	}

	status = *data;
	XDNA_DBG(job->hwctx->client->xdna, "Resp status 0x%x", status);
	if (status == AIE2_STATUS_SUCCESS)
		amdxdna_cmd_set_state(cmd_abo, ERT_CMD_STATE_COMPLETED);
	else
		amdxdna_cmd_set_state(cmd_abo, ERT_CMD_STATE_ERROR);

out:
	aie2_sched_notify(job);
	return ret;
}

static int
aie2_sched_nocmd_resp_handler(void *handle, const u32 *data, size_t size)
{
	struct amdxdna_sched_job *job = handle;
	u32 ret = 0;
	u32 status;

	if (unlikely(!data))
		goto out;

	if (unlikely(size != sizeof(u32))) {
		ret = -EINVAL;
		goto out;
	}

	status = *data;
	XDNA_DBG(job->hwctx->client->xdna, "Resp status 0x%x", status);

out:
	aie2_sched_notify(job);
	return ret;
}

static int
aie2_sched_cmdlist_resp_handler(void *handle, const u32 *data, size_t size)
{
	struct amdxdna_sched_job *job = handle;
	struct amdxdna_gem_obj *cmd_abo;
	struct cmd_chain_resp *resp;
	struct amdxdna_dev *xdna;
	u32 fail_cmd_status;
	u32 fail_cmd_idx;
	u32 ret = 0;

	cmd_abo = job->cmd_bo;
	if (unlikely(!data) || unlikely(size != sizeof(u32) * 3)) {
		amdxdna_cmd_set_state(cmd_abo, ERT_CMD_STATE_ABORT);
		ret = -EINVAL;
		goto out;
	}

	resp = (struct cmd_chain_resp *)data;
	xdna = job->hwctx->client->xdna;
	XDNA_DBG(xdna, "Status 0x%x", resp->status);
	if (resp->status == AIE2_STATUS_SUCCESS) {
		amdxdna_cmd_set_state(cmd_abo, ERT_CMD_STATE_COMPLETED);
		goto out;
	}

	/* Slow path to handle error, read from ringbuf on BAR */
	fail_cmd_idx = resp->fail_cmd_idx;
	fail_cmd_status = resp->fail_cmd_status;
	XDNA_DBG(xdna, "Failed cmd idx %d, status 0x%x",
		 fail_cmd_idx, fail_cmd_status);

	if (fail_cmd_status == AIE2_STATUS_SUCCESS) {
		amdxdna_cmd_set_state(cmd_abo, ERT_CMD_STATE_ABORT);
		ret = -EINVAL;
		goto out;
	}
	amdxdna_cmd_set_state(cmd_abo, fail_cmd_status);

	if (amdxdna_cmd_get_op(cmd_abo) == ERT_CMD_CHAIN) {
		struct amdxdna_cmd_chain *cc = amdxdna_cmd_get_payload(cmd_abo, NULL);

		cc->error_index = fail_cmd_idx;
		if (cc->error_index >= cc->command_count)
			cc->error_index = 0;
	}
out:
	aie2_sched_notify(job);
	return ret;
}

static struct dma_fence *
aie2_sched_job_run(struct drm_sched_job *sched_job)
{
	struct amdxdna_sched_job *job = drm_job_to_xdna_job(sched_job);
	struct amdxdna_gem_obj *cmd_abo = job->cmd_bo;
	struct amdxdna_hwctx *hwctx = job->hwctx;
	struct dma_fence *fence;
	int ret;

	if (!mmget_not_zero(job->mm))
		return ERR_PTR(-ESRCH);

	kref_get(&job->refcnt);
	fence = dma_fence_get(job->fence);

	if (unlikely(!cmd_abo)) {
		ret = aie2_sync_bo(hwctx, job, aie2_sched_nocmd_resp_handler);
		goto out;
	}

	amdxdna_cmd_set_state(cmd_abo, ERT_CMD_STATE_NEW);

	if (amdxdna_cmd_get_op(cmd_abo) == ERT_CMD_CHAIN)
		ret = aie2_cmdlist_multi_execbuf(hwctx, job, aie2_sched_cmdlist_resp_handler);
	else if (force_cmdlist)
		ret = aie2_cmdlist_single_execbuf(hwctx, job, aie2_sched_cmdlist_resp_handler);
	else
		ret = aie2_execbuf(hwctx, job, aie2_sched_resp_handler);

out:
	if (ret) {
		dma_fence_put(job->fence);
		aie2_job_put(job);
		mmput(job->mm);
		fence = ERR_PTR(ret);
	}
	trace_xdna_job(sched_job, hwctx->name, "sent to device", job->seq);

	return fence;
}

static void aie2_sched_job_free(struct drm_sched_job *sched_job)
{
	struct amdxdna_sched_job *job = drm_job_to_xdna_job(sched_job);
	struct amdxdna_hwctx *hwctx = job->hwctx;

	trace_xdna_job(sched_job, hwctx->name, "job free", job->seq);
	if (!job->job_done)
		up(&hwctx->priv->job_sem);

	drm_sched_job_cleanup(sched_job);
	aie2_job_put(job);
}

static enum drm_gpu_sched_stat
aie2_sched_job_timedout(struct drm_sched_job *sched_job)
{
	struct amdxdna_sched_job *job = drm_job_to_xdna_job(sched_job);
	struct amdxdna_hwctx *hwctx = job->hwctx;
	struct amdxdna_dev *xdna;

	xdna = hwctx->client->xdna;
	trace_xdna_job(sched_job, hwctx->name, "job timedout", job->seq);
	mutex_lock(&xdna->dev_lock);
	aie2_hwctx_stop(xdna, hwctx, sched_job);

	aie2_hwctx_restart(xdna, hwctx);
	mutex_unlock(&xdna->dev_lock);

	return DRM_GPU_SCHED_STAT_NOMINAL;
}

const struct drm_sched_backend_ops sched_ops = {
	.run_job = aie2_sched_job_run,
	.free_job = aie2_sched_job_free,
	.timedout_job = aie2_sched_job_timedout,
};

static int aie2_hwctx_col_list(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_dev_hdl *ndev;
	int start, end, first, last;
	u32 width = 1, entries = 0;
	int i;

	if (!hwctx->num_tiles) {
		XDNA_ERR(xdna, "Number of tiles is zero");
		return -EINVAL;
	}

	ndev = xdna->dev_handle;
	if (unlikely(!ndev->metadata.core.row_count)) {
		XDNA_WARN(xdna, "Core tile row count is zero");
		return -EINVAL;
	}

	hwctx->num_col = hwctx->num_tiles / ndev->metadata.core.row_count;
	if (!hwctx->num_col || hwctx->num_col > ndev->total_col) {
		XDNA_ERR(xdna, "Invalid num_col %d", hwctx->num_col);
		return -EINVAL;
	}

	if (ndev->priv->col_align == COL_ALIGN_NATURE)
		width = hwctx->num_col;

	/*
	 * In range [start, end], find out columns that is multiple of width.
	 *	'first' is the first column,
	 *	'last' is the last column,
	 *	'entries' is the total number of columns.
	 */
	start =  xdna->dev_info->first_col;
	end =  ndev->total_col - hwctx->num_col;
	if (start > 0 && end == 0) {
		XDNA_DBG(xdna, "Force start from col 0");
		start = 0;
	}
	first = start + (width - start % width) % width;
	last = end - end % width;
	if (last >= first)
		entries = (last - first) / width + 1;
	XDNA_DBG(xdna, "start %d end %d first %d last %d",
		 start, end, first, last);

	if (unlikely(!entries)) {
		XDNA_ERR(xdna, "Start %d end %d width %d",
			 start, end, width);
		return -EINVAL;
	}

	hwctx->col_list = kmalloc_array(entries, sizeof(*hwctx->col_list), GFP_KERNEL);
	if (!hwctx->col_list)
		return -ENOMEM;

	hwctx->col_list_len = entries;
	hwctx->col_list[0] = first;
	for (i = 1; i < entries; i++)
		hwctx->col_list[i] = hwctx->col_list[i - 1] + width;

	print_hex_dump_debug("col_list: ", DUMP_PREFIX_OFFSET, 16, 4, hwctx->col_list,
			     entries * sizeof(*hwctx->col_list), false);
	return 0;
}

static int aie2_alloc_resource(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct alloc_requests *xrs_req;
	int ret;

	xrs_req = kzalloc(sizeof(*xrs_req), GFP_KERNEL);
	if (!xrs_req)
		return -ENOMEM;

	xrs_req->cdo.start_cols = hwctx->col_list;
	xrs_req->cdo.cols_len = hwctx->col_list_len;
	xrs_req->cdo.ncols = hwctx->num_col;
	xrs_req->cdo.qos_cap.opc = hwctx->max_opc;

	xrs_req->rqos.gops = hwctx->qos.gops;
	xrs_req->rqos.fps = hwctx->qos.fps;
	xrs_req->rqos.dma_bw = hwctx->qos.dma_bandwidth;
	xrs_req->rqos.latency = hwctx->qos.latency;
	xrs_req->rqos.exec_time = hwctx->qos.frame_exec_time;
	xrs_req->rqos.priority = hwctx->qos.priority;

	xrs_req->rid = (uintptr_t)hwctx;

	ret = xrs_allocate_resource(xdna->xrs_hdl, xrs_req, hwctx);
	if (ret)
		XDNA_ERR(xdna, "Allocate AIE resource failed, ret %d", ret);

	kfree(xrs_req);
	return ret;
}

static void aie2_release_resource(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	int ret;

	ret = xrs_release_resource(xdna->xrs_hdl, (uintptr_t)hwctx);
	if (ret)
		XDNA_ERR(xdna, "Release AIE resource failed, ret %d", ret);
}

static int aie2_ctx_syncobj_create(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct drm_file *filp = hwctx->client->filp;
	struct drm_syncobj *syncobj;
	u32 hdl;
	int ret;

	hwctx->syncobj_hdl = AMDXDNA_INVALID_FENCE_HANDLE;

	ret = drm_syncobj_create(&syncobj, 0, NULL);
	if (ret) {
		XDNA_ERR(xdna, "Create ctx syncobj failed, ret %d", ret);
		return ret;
	}
	ret = drm_syncobj_get_handle(filp, syncobj, &hdl);
	if (ret) {
		drm_syncobj_put(syncobj);
		XDNA_ERR(xdna, "Create ctx syncobj handle failed, ret %d", ret);
		return ret;
	}
	hwctx->priv->syncobj = syncobj;
	hwctx->syncobj_hdl = hdl;

	return 0;
}

static void aie2_ctx_syncobj_destroy(struct amdxdna_hwctx *hwctx)
{
	/*
	 * The syncobj_hdl is owned by user space and will be cleaned up
	 * separately.
	 */
	drm_syncobj_put(hwctx->priv->syncobj);
}

int aie2_hwctx_init(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_client *client = hwctx->client;
	struct amdxdna_dev *xdna = client->xdna;
	struct drm_gpu_scheduler *sched;
	struct amdxdna_hwctx_priv *priv;
	struct amdxdna_gem_obj *heap;
	struct amdxdna_dev_hdl *ndev;
	int i, ret;

	priv = kzalloc(sizeof(*hwctx->priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	hwctx->priv = priv;

	mutex_lock(&client->mm_lock);
	heap = client->dev_heap;
	if (!heap) {
		XDNA_ERR(xdna, "The client dev heap object not exist");
		mutex_unlock(&client->mm_lock);
		ret = -ENOENT;
		goto free_priv;
	}
	drm_gem_object_get(to_gobj(heap));
	mutex_unlock(&client->mm_lock);
	priv->heap = heap;
	sema_init(&priv->job_sem, HWCTX_MAX_CMDS);

	ret = amdxdna_gem_pin(heap);
	if (ret) {
		XDNA_ERR(xdna, "Dev heap pin failed, ret %d", ret);
		goto put_heap;
	}

	for (i = 0; i < ARRAY_SIZE(priv->cmd_buf); i++) {
		struct amdxdna_gem_obj *abo;
		struct amdxdna_drm_create_bo args = {
			.flags = 0,
			.type = AMDXDNA_BO_DEV,
			.vaddr = 0,
			.size = MAX_CHAIN_CMDBUF_SIZE,
		};

		abo = amdxdna_drm_alloc_dev_bo(&xdna->ddev, &args, client->filp, true);
		if (IS_ERR(abo)) {
			ret = PTR_ERR(abo);
			goto free_cmd_bufs;
		}

		XDNA_DBG(xdna, "Command buf %d addr 0x%llx size 0x%lx",
			 i, abo->mem.dev_addr, abo->mem.size);
		priv->cmd_buf[i] = abo;
	}

	sched = &priv->sched;
	mutex_init(&priv->io_lock);

	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&priv->io_lock);
	fs_reclaim_release(GFP_KERNEL);

	ret = drm_sched_init(sched, &sched_ops, NULL, DRM_SCHED_PRIORITY_COUNT,
			     HWCTX_MAX_CMDS, 0, msecs_to_jiffies(HWCTX_MAX_TIMEOUT),
			     NULL, NULL, hwctx->name, xdna->ddev.dev);
	if (ret) {
		XDNA_ERR(xdna, "Failed to init DRM scheduler. ret %d", ret);
		goto free_cmd_bufs;
	}

	ret = drm_sched_entity_init(&priv->entity, DRM_SCHED_PRIORITY_NORMAL,
				    &sched, 1, NULL);
	if (ret) {
		XDNA_ERR(xdna, "Failed to initial sched entiry. ret %d", ret);
		goto free_sched;
	}

	ret = aie2_hwctx_col_list(hwctx);
	if (ret) {
		XDNA_ERR(xdna, "Create col list failed, ret %d", ret);
		goto free_entity;
	}

	ret = aie2_alloc_resource(hwctx);
	if (ret) {
		XDNA_ERR(xdna, "Alloc hw resource failed, ret %d", ret);
		goto free_col_list;
	}

	ret = aie2_map_host_buf(xdna->dev_handle, hwctx->fw_ctx_id,
				heap->mem.userptr, heap->mem.size);
	if (ret) {
		XDNA_ERR(xdna, "Map host buffer failed, ret %d", ret);
		goto release_resource;
	}

	ret = aie2_ctx_syncobj_create(hwctx);
	if (ret) {
		XDNA_ERR(xdna, "Create syncobj failed, ret %d", ret);
		goto release_resource;
	}

	hwctx->status = HWCTX_STAT_INIT;
	ndev = xdna->dev_handle;
	ndev->hwctx_num++;

	XDNA_DBG(xdna, "hwctx %s init completed", hwctx->name);

	return 0;

release_resource:
	aie2_release_resource(hwctx);
free_col_list:
	kfree(hwctx->col_list);
free_entity:
	drm_sched_entity_destroy(&priv->entity);
free_sched:
	drm_sched_fini(&priv->sched);
free_cmd_bufs:
	for (i = 0; i < ARRAY_SIZE(priv->cmd_buf); i++) {
		if (!priv->cmd_buf[i])
			continue;
		drm_gem_object_put(to_gobj(priv->cmd_buf[i]));
	}
	amdxdna_gem_unpin(heap);
put_heap:
	drm_gem_object_put(to_gobj(heap));
free_priv:
	kfree(priv);
	return ret;
}

void aie2_hwctx_fini(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev_hdl *ndev;
	struct amdxdna_dev *xdna;
	int idx;

	xdna = hwctx->client->xdna;
	ndev = xdna->dev_handle;
	ndev->hwctx_num--;
	drm_sched_wqueue_stop(&hwctx->priv->sched);

	/* Now, scheduler will not send command to device. */
	aie2_release_resource(hwctx);

	/*
	 * All submitted commands are aborted.
	 * Restart scheduler queues to cleanup jobs. The amdxdna_sched_job_run()
	 * will return NODEV if it is called.
	 */
	drm_sched_wqueue_start(&hwctx->priv->sched);

	aie2_hwctx_wait_for_idle(hwctx);
	drm_sched_entity_destroy(&hwctx->priv->entity);
	drm_sched_fini(&hwctx->priv->sched);
	aie2_ctx_syncobj_destroy(hwctx);

	XDNA_DBG(xdna, "%s sequence number %lld", hwctx->name, hwctx->priv->seq);

	for (idx = 0; idx < ARRAY_SIZE(hwctx->priv->cmd_buf); idx++)
		drm_gem_object_put(to_gobj(hwctx->priv->cmd_buf[idx]));
	amdxdna_gem_unpin(hwctx->priv->heap);
	drm_gem_object_put(to_gobj(hwctx->priv->heap));

	mutex_destroy(&hwctx->priv->io_lock);
	kfree(hwctx->col_list);
	kfree(hwctx->priv);
	kfree(hwctx->cus);
}

static int aie2_hwctx_cu_config(struct amdxdna_hwctx *hwctx, void *buf, u32 size)
{
	struct amdxdna_hwctx_param_config_cu *config = buf;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	u32 total_size;
	int ret;

	XDNA_DBG(xdna, "Config %d CU to %s", config->num_cus, hwctx->name);
	if (XDNA_MBZ_DBG(xdna, config->pad, sizeof(config->pad)))
		return -EINVAL;

	if (hwctx->status != HWCTX_STAT_INIT) {
		XDNA_ERR(xdna, "Not support re-config CU");
		return -EINVAL;
	}

	if (!config->num_cus) {
		XDNA_ERR(xdna, "Number of CU is zero");
		return -EINVAL;
	}

	total_size = struct_size(config, cu_configs, config->num_cus);
	if (total_size > size) {
		XDNA_ERR(xdna, "CU config larger than size");
		return -EINVAL;
	}

	hwctx->cus = kmemdup(config, total_size, GFP_KERNEL);
	if (!hwctx->cus)
		return -ENOMEM;

	ret = aie2_config_cu(hwctx);
	if (ret) {
		XDNA_ERR(xdna, "Config CU to firmware failed, ret %d", ret);
		goto free_cus;
	}

	wmb(); /* To avoid locking in command submit when check status */
	hwctx->status = HWCTX_STAT_READY;

	return 0;

free_cus:
	kfree(hwctx->cus);
	hwctx->cus = NULL;
	return ret;
}

int aie2_hwctx_config(struct amdxdna_hwctx *hwctx, u32 type, u64 value, void *buf, u32 size)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));
	switch (type) {
	case DRM_AMDXDNA_HWCTX_CONFIG_CU:
		return aie2_hwctx_cu_config(hwctx, buf, size);
	case DRM_AMDXDNA_HWCTX_ASSIGN_DBG_BUF:
	case DRM_AMDXDNA_HWCTX_REMOVE_DBG_BUF:
		return -EOPNOTSUPP;
	default:
		XDNA_DBG(xdna, "Not supported type %d", type);
		return -EOPNOTSUPP;
	}
}

static int aie2_populate_range(struct amdxdna_gem_obj *abo)
{
	struct amdxdna_dev *xdna = to_xdna_dev(to_gobj(abo)->dev);
	struct mm_struct *mm = abo->mem.notifier.mm;
	struct hmm_range range = { 0 };
	unsigned long timeout;
	int ret;

	XDNA_INFO_ONCE(xdna, "populate memory range %llx size %lx",
		       abo->mem.userptr, abo->mem.size);
	range.notifier = &abo->mem.notifier;
	range.start = abo->mem.userptr;
	range.end = abo->mem.userptr + abo->mem.size;
	range.hmm_pfns = abo->mem.pfns;
	range.default_flags = HMM_PFN_REQ_FAULT;

	if (!mmget_not_zero(mm))
		return -EFAULT;

	timeout = jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
again:
	range.notifier_seq = mmu_interval_read_begin(&abo->mem.notifier);
	mmap_read_lock(mm);
	ret = hmm_range_fault(&range);
	mmap_read_unlock(mm);
	if (ret) {
		if (time_after(jiffies, timeout)) {
			ret = -ETIME;
			goto put_mm;
		}

		if (ret == -EBUSY)
			goto again;

		goto put_mm;
	}

	down_read(&xdna->notifier_lock);
	if (mmu_interval_read_retry(&abo->mem.notifier, range.notifier_seq)) {
		up_read(&xdna->notifier_lock);
		goto again;
	}
	abo->mem.map_invalid = false;
	up_read(&xdna->notifier_lock);

put_mm:
	mmput(mm);
	return ret;
}

int aie2_cmd_submit(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job, u64 *seq)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct ww_acquire_ctx acquire_ctx;
	struct dma_fence_chain *chain;
	struct amdxdna_gem_obj *abo;
	unsigned long timeout = 0;
	int ret, i;

	ret = down_interruptible(&hwctx->priv->job_sem);
	if (ret) {
		XDNA_ERR(xdna, "Grab job sem failed, ret %d", ret);
		return ret;
	}

	chain = dma_fence_chain_alloc();
	if (!chain) {
		XDNA_ERR(xdna, "Alloc fence chain failed");
		ret = -ENOMEM;
		goto up_sem;
	}

	ret = drm_sched_job_init(&job->base, &hwctx->priv->entity, 1, hwctx);
	if (ret) {
		XDNA_ERR(xdna, "DRM job init failed, ret %d", ret);
		goto free_chain;
	}

retry:
	ret = drm_gem_lock_reservations(job->bos, job->bo_cnt, &acquire_ctx);
	if (ret) {
		XDNA_WARN(xdna, "Failed to lock BOs, ret %d", ret);
		goto cleanup_job;
	}

	for (i = 0; i < job->bo_cnt; i++) {
		ret = dma_resv_reserve_fences(job->bos[i]->resv, 1);
		if (ret) {
			XDNA_WARN(xdna, "Failed to reserve fences %d", ret);
			drm_gem_unlock_reservations(job->bos, job->bo_cnt, &acquire_ctx);
			goto cleanup_job;
		}
	}

	down_read(&xdna->notifier_lock);
	for (i = 0; i < job->bo_cnt; i++) {
		abo = to_xdna_obj(job->bos[i]);
		if (abo->mem.map_invalid) {
			up_read(&xdna->notifier_lock);
			drm_gem_unlock_reservations(job->bos, job->bo_cnt, &acquire_ctx);
			if (!timeout) {
				timeout = jiffies +
					msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
			} else if (time_after(jiffies, timeout)) {
				ret = -ETIME;
				goto cleanup_job;
			}

			ret = aie2_populate_range(abo);
			if (ret)
				goto cleanup_job;
			goto retry;
		}
	}

	mutex_lock(&hwctx->priv->io_lock);
	drm_sched_job_arm(&job->base);
	job->out_fence = dma_fence_get(&job->base.s_fence->finished);
	for (i = 0; i < job->bo_cnt; i++)
		dma_resv_add_fence(job->bos[i]->resv, job->out_fence, DMA_RESV_USAGE_WRITE);
	job->seq = hwctx->priv->seq++;
	kref_get(&job->refcnt);
	drm_sched_entity_push_job(&job->base);

	*seq = job->seq;
	drm_syncobj_add_point(hwctx->priv->syncobj, chain, job->out_fence, *seq);
	mutex_unlock(&hwctx->priv->io_lock);

	up_read(&xdna->notifier_lock);
	drm_gem_unlock_reservations(job->bos, job->bo_cnt, &acquire_ctx);

	aie2_job_put(job);

	return 0;

cleanup_job:
	drm_sched_job_cleanup(&job->base);
free_chain:
	dma_fence_chain_free(chain);
up_sem:
	up(&hwctx->priv->job_sem);
	job->job_done = true;
	return ret;
}

void aie2_hmm_invalidate(struct amdxdna_gem_obj *abo,
			 unsigned long cur_seq)
{
	struct amdxdna_dev *xdna = to_xdna_dev(to_gobj(abo)->dev);
	struct drm_gem_object *gobj = to_gobj(abo);
	long ret;

	down_write(&xdna->notifier_lock);
	abo->mem.map_invalid = true;
	mmu_interval_set_seq(&abo->mem.notifier, cur_seq);
	up_write(&xdna->notifier_lock);
	ret = dma_resv_wait_timeout(gobj->resv, DMA_RESV_USAGE_BOOKKEEP,
				    true, MAX_SCHEDULE_TIMEOUT);
	if (!ret || ret == -ERESTARTSYS)
		XDNA_ERR(xdna, "Failed to wait for bo, ret %ld", ret);
}
