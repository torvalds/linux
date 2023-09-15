/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 */
#include "amdgpu.h"
#include "amdgpu_xcp.h"
#include "amdgpu_drv.h"

#include <drm/drm_drv.h>
#include "../amdxcp/amdgpu_xcp_drv.h"

static int __amdgpu_xcp_run(struct amdgpu_xcp_mgr *xcp_mgr,
			    struct amdgpu_xcp_ip *xcp_ip, int xcp_state)
{
	int (*run_func)(void *handle, uint32_t inst_mask);
	int ret = 0;

	if (!xcp_ip || !xcp_ip->valid || !xcp_ip->ip_funcs)
		return 0;

	run_func = NULL;

	switch (xcp_state) {
	case AMDGPU_XCP_PREPARE_SUSPEND:
		run_func = xcp_ip->ip_funcs->prepare_suspend;
		break;
	case AMDGPU_XCP_SUSPEND:
		run_func = xcp_ip->ip_funcs->suspend;
		break;
	case AMDGPU_XCP_PREPARE_RESUME:
		run_func = xcp_ip->ip_funcs->prepare_resume;
		break;
	case AMDGPU_XCP_RESUME:
		run_func = xcp_ip->ip_funcs->resume;
		break;
	}

	if (run_func)
		ret = run_func(xcp_mgr->adev, xcp_ip->inst_mask);

	return ret;
}

static int amdgpu_xcp_run_transition(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id,
				     int state)
{
	struct amdgpu_xcp_ip *xcp_ip;
	struct amdgpu_xcp *xcp;
	int i, ret;

	if (xcp_id >= MAX_XCP || !xcp_mgr->xcp[xcp_id].valid)
		return -EINVAL;

	xcp = &xcp_mgr->xcp[xcp_id];
	for (i = 0; i < AMDGPU_XCP_MAX_BLOCKS; ++i) {
		xcp_ip = &xcp->ip[i];
		ret = __amdgpu_xcp_run(xcp_mgr, xcp_ip, state);
		if (ret)
			break;
	}

	return ret;
}

int amdgpu_xcp_prepare_suspend(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id)
{
	return amdgpu_xcp_run_transition(xcp_mgr, xcp_id,
					 AMDGPU_XCP_PREPARE_SUSPEND);
}

int amdgpu_xcp_suspend(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id)
{
	return amdgpu_xcp_run_transition(xcp_mgr, xcp_id, AMDGPU_XCP_SUSPEND);
}

int amdgpu_xcp_prepare_resume(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id)
{
	return amdgpu_xcp_run_transition(xcp_mgr, xcp_id,
					 AMDGPU_XCP_PREPARE_RESUME);
}

int amdgpu_xcp_resume(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id)
{
	return amdgpu_xcp_run_transition(xcp_mgr, xcp_id, AMDGPU_XCP_RESUME);
}

static void __amdgpu_xcp_add_block(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id,
				   struct amdgpu_xcp_ip *ip)
{
	struct amdgpu_xcp *xcp;

	if (!ip)
		return;

	xcp = &xcp_mgr->xcp[xcp_id];
	xcp->ip[ip->ip_id] = *ip;
	xcp->ip[ip->ip_id].valid = true;

	xcp->valid = true;
}

int amdgpu_xcp_init(struct amdgpu_xcp_mgr *xcp_mgr, int num_xcps, int mode)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	struct amdgpu_xcp_ip ip;
	uint8_t mem_id;
	int i, j, ret;

	if (!num_xcps || num_xcps > MAX_XCP)
		return -EINVAL;

	xcp_mgr->mode = mode;

	for (i = 0; i < MAX_XCP; ++i)
		xcp_mgr->xcp[i].valid = false;

	/* This is needed for figuring out memory id of xcp */
	xcp_mgr->num_xcp_per_mem_partition = num_xcps / xcp_mgr->adev->gmc.num_mem_partitions;

	for (i = 0; i < num_xcps; ++i) {
		for (j = AMDGPU_XCP_GFXHUB; j < AMDGPU_XCP_MAX_BLOCKS; ++j) {
			ret = xcp_mgr->funcs->get_ip_details(xcp_mgr, i, j,
							     &ip);
			if (ret)
				continue;

			__amdgpu_xcp_add_block(xcp_mgr, i, &ip);
		}

		xcp_mgr->xcp[i].id = i;

		if (xcp_mgr->funcs->get_xcp_mem_id) {
			ret = xcp_mgr->funcs->get_xcp_mem_id(
				xcp_mgr, &xcp_mgr->xcp[i], &mem_id);
			if (ret)
				continue;
			else
				xcp_mgr->xcp[i].mem_id = mem_id;
		}
	}

	xcp_mgr->num_xcps = num_xcps;
	amdgpu_xcp_update_partition_sched_list(adev);

	return 0;
}

int amdgpu_xcp_switch_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr, int mode)
{
	int ret, curr_mode, num_xcps = 0;

	if (!xcp_mgr || mode == AMDGPU_XCP_MODE_NONE)
		return -EINVAL;

	if (xcp_mgr->mode == mode)
		return 0;

	if (!xcp_mgr->funcs || !xcp_mgr->funcs->switch_partition_mode)
		return 0;

	mutex_lock(&xcp_mgr->xcp_lock);

	curr_mode = xcp_mgr->mode;
	/* State set to transient mode */
	xcp_mgr->mode = AMDGPU_XCP_MODE_TRANS;

	ret = xcp_mgr->funcs->switch_partition_mode(xcp_mgr, mode, &num_xcps);

	if (ret) {
		/* Failed, get whatever mode it's at now */
		if (xcp_mgr->funcs->query_partition_mode)
			xcp_mgr->mode = amdgpu_xcp_query_partition_mode(
				xcp_mgr, AMDGPU_XCP_FL_LOCKED);
		else
			xcp_mgr->mode = curr_mode;

		goto out;
	}

out:
	mutex_unlock(&xcp_mgr->xcp_lock);

	return ret;
}

int amdgpu_xcp_query_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr, u32 flags)
{
	int mode;

	if (xcp_mgr->mode == AMDGPU_XCP_MODE_NONE)
		return xcp_mgr->mode;

	if (!xcp_mgr->funcs || !xcp_mgr->funcs->query_partition_mode)
		return xcp_mgr->mode;

	if (!(flags & AMDGPU_XCP_FL_LOCKED))
		mutex_lock(&xcp_mgr->xcp_lock);
	mode = xcp_mgr->funcs->query_partition_mode(xcp_mgr);
	if (xcp_mgr->mode != AMDGPU_XCP_MODE_TRANS && mode != xcp_mgr->mode)
		dev_WARN(
			xcp_mgr->adev->dev,
			"Cached partition mode %d not matching with device mode %d",
			xcp_mgr->mode, mode);

	if (!(flags & AMDGPU_XCP_FL_LOCKED))
		mutex_unlock(&xcp_mgr->xcp_lock);

	return mode;
}

static int amdgpu_xcp_dev_alloc(struct amdgpu_device *adev)
{
	struct drm_device *p_ddev;
	struct drm_device *ddev;
	int i, ret;

	ddev = adev_to_drm(adev);

	/* xcp #0 shares drm device setting with adev */
	adev->xcp_mgr->xcp->ddev = ddev;

	for (i = 1; i < MAX_XCP; i++) {
		ret = amdgpu_xcp_drm_dev_alloc(&p_ddev);
		if (ret == -ENOSPC) {
			dev_warn(adev->dev,
			"Skip xcp node #%d when out of drm node resource.", i);
			return 0;
		} else if (ret) {
			return ret;
		}

		/* Redirect all IOCTLs to the primary device */
		adev->xcp_mgr->xcp[i].rdev = p_ddev->render->dev;
		adev->xcp_mgr->xcp[i].pdev = p_ddev->primary->dev;
		adev->xcp_mgr->xcp[i].driver = (struct drm_driver *)p_ddev->driver;
		adev->xcp_mgr->xcp[i].vma_offset_manager = p_ddev->vma_offset_manager;
		p_ddev->render->dev = ddev;
		p_ddev->primary->dev = ddev;
		p_ddev->vma_offset_manager = ddev->vma_offset_manager;
		p_ddev->driver = &amdgpu_partition_driver;
		adev->xcp_mgr->xcp[i].ddev = p_ddev;
	}

	return 0;
}

int amdgpu_xcp_mgr_init(struct amdgpu_device *adev, int init_mode,
			int init_num_xcps,
			struct amdgpu_xcp_mgr_funcs *xcp_funcs)
{
	struct amdgpu_xcp_mgr *xcp_mgr;

	if (!xcp_funcs || !xcp_funcs->switch_partition_mode ||
	    !xcp_funcs->get_ip_details)
		return -EINVAL;

	xcp_mgr = kzalloc(sizeof(*xcp_mgr), GFP_KERNEL);

	if (!xcp_mgr)
		return -ENOMEM;

	xcp_mgr->adev = adev;
	xcp_mgr->funcs = xcp_funcs;
	xcp_mgr->mode = init_mode;
	mutex_init(&xcp_mgr->xcp_lock);

	if (init_mode != AMDGPU_XCP_MODE_NONE)
		amdgpu_xcp_init(xcp_mgr, init_num_xcps, init_mode);

	adev->xcp_mgr = xcp_mgr;

	return amdgpu_xcp_dev_alloc(adev);
}

int amdgpu_xcp_get_partition(struct amdgpu_xcp_mgr *xcp_mgr,
			     enum AMDGPU_XCP_IP_BLOCK ip, int instance)
{
	struct amdgpu_xcp *xcp;
	int i, id_mask = 0;

	if (ip >= AMDGPU_XCP_MAX_BLOCKS)
		return -EINVAL;

	for (i = 0; i < xcp_mgr->num_xcps; ++i) {
		xcp = &xcp_mgr->xcp[i];
		if ((xcp->valid) && (xcp->ip[ip].valid) &&
		    (xcp->ip[ip].inst_mask & BIT(instance)))
			id_mask |= BIT(i);
	}

	if (!id_mask)
		id_mask = -ENXIO;

	return id_mask;
}

int amdgpu_xcp_get_inst_details(struct amdgpu_xcp *xcp,
				enum AMDGPU_XCP_IP_BLOCK ip,
				uint32_t *inst_mask)
{
	if (!xcp->valid || !inst_mask || !(xcp->ip[ip].valid))
		return -EINVAL;

	*inst_mask = xcp->ip[ip].inst_mask;

	return 0;
}

int amdgpu_xcp_dev_register(struct amdgpu_device *adev,
			const struct pci_device_id *ent)
{
	int i, ret;

	if (!adev->xcp_mgr)
		return 0;

	for (i = 1; i < MAX_XCP; i++) {
		if (!adev->xcp_mgr->xcp[i].ddev)
			break;

		ret = drm_dev_register(adev->xcp_mgr->xcp[i].ddev, ent->driver_data);
		if (ret)
			return ret;
	}

	return 0;
}

void amdgpu_xcp_dev_unplug(struct amdgpu_device *adev)
{
	struct drm_device *p_ddev;
	int i;

	if (!adev->xcp_mgr)
		return;

	for (i = 1; i < MAX_XCP; i++) {
		if (!adev->xcp_mgr->xcp[i].ddev)
			break;

		p_ddev = adev->xcp_mgr->xcp[i].ddev;
		drm_dev_unplug(p_ddev);
		p_ddev->render->dev = adev->xcp_mgr->xcp[i].rdev;
		p_ddev->primary->dev = adev->xcp_mgr->xcp[i].pdev;
		p_ddev->driver =  adev->xcp_mgr->xcp[i].driver;
		p_ddev->vma_offset_manager = adev->xcp_mgr->xcp[i].vma_offset_manager;
	}
}

int amdgpu_xcp_open_device(struct amdgpu_device *adev,
			   struct amdgpu_fpriv *fpriv,
			   struct drm_file *file_priv)
{
	int i;

	if (!adev->xcp_mgr)
		return 0;

	fpriv->xcp_id = AMDGPU_XCP_NO_PARTITION;
	for (i = 0; i < MAX_XCP; ++i) {
		if (!adev->xcp_mgr->xcp[i].ddev)
			break;

		if (file_priv->minor == adev->xcp_mgr->xcp[i].ddev->render) {
			if (adev->xcp_mgr->xcp[i].valid == FALSE) {
				dev_err(adev->dev, "renderD%d partition %d not valid!",
						file_priv->minor->index, i);
				return -ENOENT;
			}
			dev_dbg(adev->dev, "renderD%d partition %d opened!",
					file_priv->minor->index, i);
			fpriv->xcp_id = i;
			break;
		}
	}

	fpriv->vm.mem_id = fpriv->xcp_id == AMDGPU_XCP_NO_PARTITION ? -1 :
				adev->xcp_mgr->xcp[fpriv->xcp_id].mem_id;
	return 0;
}

void amdgpu_xcp_release_sched(struct amdgpu_device *adev,
				  struct amdgpu_ctx_entity *entity)
{
	struct drm_gpu_scheduler *sched;
	struct amdgpu_ring *ring;

	if (!adev->xcp_mgr)
		return;

	sched = entity->entity.rq->sched;
	if (sched->ready) {
		ring = to_amdgpu_ring(entity->entity.rq->sched);
		atomic_dec(&adev->xcp_mgr->xcp[ring->xcp_id].ref_cnt);
	}
}

