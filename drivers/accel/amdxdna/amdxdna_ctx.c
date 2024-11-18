// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "amdxdna_ctx.h"
#include "amdxdna_pci_drv.h"

#define MAX_HWCTX_ID		255

static void amdxdna_hwctx_destroy(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;

	/* At this point, user is not able to submit new commands */
	mutex_lock(&xdna->dev_lock);
	xdna->dev_info->ops->hwctx_fini(hwctx);
	mutex_unlock(&xdna->dev_lock);

	kfree(hwctx->name);
	kfree(hwctx);
}

/*
 * This should be called in close() and remove(). DO NOT call in other syscalls.
 * This guarantee that when hwctx and resources will be released, if user
 * doesn't call amdxdna_drm_destroy_hwctx_ioctl.
 */
void amdxdna_hwctx_remove_all(struct amdxdna_client *client)
{
	struct amdxdna_hwctx *hwctx;
	int next = 0;

	mutex_lock(&client->hwctx_lock);
	idr_for_each_entry_continue(&client->hwctx_idr, hwctx, next) {
		XDNA_DBG(client->xdna, "PID %d close HW context %d",
			 client->pid, hwctx->id);
		idr_remove(&client->hwctx_idr, hwctx->id);
		mutex_unlock(&client->hwctx_lock);
		amdxdna_hwctx_destroy(hwctx);
		mutex_lock(&client->hwctx_lock);
	}
	mutex_unlock(&client->hwctx_lock);
}

int amdxdna_drm_create_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_drm_create_hwctx *args = data;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_hwctx *hwctx;
	int ret, idx;

	if (args->ext || args->ext_flags)
		return -EINVAL;

	if (!drm_dev_enter(dev, &idx))
		return -ENODEV;

	hwctx = kzalloc(sizeof(*hwctx), GFP_KERNEL);
	if (!hwctx) {
		ret = -ENOMEM;
		goto exit;
	}

	if (copy_from_user(&hwctx->qos, u64_to_user_ptr(args->qos_p), sizeof(hwctx->qos))) {
		XDNA_ERR(xdna, "Access QoS info failed");
		ret = -EFAULT;
		goto free_hwctx;
	}

	hwctx->client = client;
	hwctx->fw_ctx_id = -1;
	hwctx->num_tiles = args->num_tiles;
	hwctx->mem_size = args->mem_size;
	hwctx->max_opc = args->max_opc;
	mutex_lock(&client->hwctx_lock);
	ret = idr_alloc_cyclic(&client->hwctx_idr, hwctx, 0, MAX_HWCTX_ID, GFP_KERNEL);
	if (ret < 0) {
		mutex_unlock(&client->hwctx_lock);
		XDNA_ERR(xdna, "Allocate hwctx ID failed, ret %d", ret);
		goto free_hwctx;
	}
	hwctx->id = ret;
	mutex_unlock(&client->hwctx_lock);

	hwctx->name = kasprintf(GFP_KERNEL, "hwctx.%d.%d", client->pid, hwctx->id);
	if (!hwctx->name) {
		ret = -ENOMEM;
		goto rm_id;
	}

	mutex_lock(&xdna->dev_lock);
	ret = xdna->dev_info->ops->hwctx_init(hwctx);
	if (ret) {
		mutex_unlock(&xdna->dev_lock);
		XDNA_ERR(xdna, "Init hwctx failed, ret %d", ret);
		goto free_name;
	}
	args->handle = hwctx->id;
	args->syncobj_handle = hwctx->syncobj_hdl;
	mutex_unlock(&xdna->dev_lock);

	XDNA_DBG(xdna, "PID %d create HW context %d, ret %d", client->pid, args->handle, ret);
	drm_dev_exit(idx);
	return 0;

free_name:
	kfree(hwctx->name);
rm_id:
	mutex_lock(&client->hwctx_lock);
	idr_remove(&client->hwctx_idr, hwctx->id);
	mutex_unlock(&client->hwctx_lock);
free_hwctx:
	kfree(hwctx);
exit:
	drm_dev_exit(idx);
	return ret;
}

int amdxdna_drm_destroy_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_drm_destroy_hwctx *args = data;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_hwctx *hwctx;
	int ret = 0, idx;

	if (!drm_dev_enter(dev, &idx))
		return -ENODEV;

	mutex_lock(&client->hwctx_lock);
	hwctx = idr_find(&client->hwctx_idr, args->handle);
	if (!hwctx) {
		mutex_unlock(&client->hwctx_lock);
		ret = -EINVAL;
		XDNA_DBG(xdna, "PID %d HW context %d not exist",
			 client->pid, args->handle);
		goto out;
	}
	idr_remove(&client->hwctx_idr, hwctx->id);
	mutex_unlock(&client->hwctx_lock);

	amdxdna_hwctx_destroy(hwctx);

	XDNA_DBG(xdna, "PID %d destroyed HW context %d", client->pid, args->handle);
out:
	drm_dev_exit(idx);
	return ret;
}

int amdxdna_drm_config_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_drm_config_hwctx *args = data;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_hwctx *hwctx;
	u32 buf_size;
	void *buf;
	u64 val;
	int ret;

	if (!xdna->dev_info->ops->hwctx_config)
		return -EOPNOTSUPP;

	val = args->param_val;
	buf_size = args->param_val_size;

	switch (args->param_type) {
	case DRM_AMDXDNA_HWCTX_CONFIG_CU:
		/* For those types that param_val is pointer */
		if (buf_size > PAGE_SIZE) {
			XDNA_ERR(xdna, "Config CU param buffer too large");
			return -E2BIG;
		}

		/* Hwctx needs to keep buf */
		buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		if (copy_from_user(buf, u64_to_user_ptr(val), buf_size)) {
			kfree(buf);
			return -EFAULT;
		}

		break;
	case DRM_AMDXDNA_HWCTX_ASSIGN_DBG_BUF:
	case DRM_AMDXDNA_HWCTX_REMOVE_DBG_BUF:
		/* For those types that param_val is a value */
		buf = NULL;
		buf_size = 0;
		break;
	default:
		XDNA_DBG(xdna, "Unknown HW context config type %d", args->param_type);
		return -EINVAL;
	}

	mutex_lock(&xdna->dev_lock);
	hwctx = idr_find(&client->hwctx_idr, args->handle);
	if (!hwctx) {
		XDNA_DBG(xdna, "PID %d failed to get hwctx %d", client->pid, args->handle);
		ret = -EINVAL;
		goto unlock;
	}

	ret = xdna->dev_info->ops->hwctx_config(hwctx, args->param_type, val, buf, buf_size);

unlock:
	mutex_unlock(&xdna->dev_lock);
	kfree(buf);
	return ret;
}
