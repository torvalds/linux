// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <linux/types.h>

#include "aie2_pci.h"
#include "aie2_solver.h"
#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_pci_drv.h"

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

int aie2_hwctx_init(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_client *client = hwctx->client;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_hwctx_priv *priv;
	struct amdxdna_gem_obj *heap;
	int ret;

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

	ret = amdxdna_gem_pin(heap);
	if (ret) {
		XDNA_ERR(xdna, "Dev heap pin failed, ret %d", ret);
		goto put_heap;
	}

	ret = aie2_hwctx_col_list(hwctx);
	if (ret) {
		XDNA_ERR(xdna, "Create col list failed, ret %d", ret);
		goto unpin;
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
	hwctx->status = HWCTX_STAT_INIT;

	XDNA_DBG(xdna, "hwctx %s init completed", hwctx->name);

	return 0;

release_resource:
	aie2_release_resource(hwctx);
free_col_list:
	kfree(hwctx->col_list);
unpin:
	amdxdna_gem_unpin(heap);
put_heap:
	drm_gem_object_put(to_gobj(heap));
free_priv:
	kfree(priv);
	return ret;
}

void aie2_hwctx_fini(struct amdxdna_hwctx *hwctx)
{
	aie2_release_resource(hwctx);

	amdxdna_gem_unpin(hwctx->priv->heap);
	drm_gem_object_put(to_gobj(hwctx->priv->heap));

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
