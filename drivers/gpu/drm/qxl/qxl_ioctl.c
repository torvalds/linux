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

#include <linux/pci.h>
#include <linux/uaccess.h>

#include "qxl_drv.h"
#include "qxl_object.h"

/*
 * TODO: allocating a new gem(in qxl_bo) for each request.
 * This is wasteful since bo's are page aligned.
 */
static int qxl_alloc_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_qxl_alloc *qxl_alloc = data;
	int ret;
	struct qxl_bo *qobj;
	uint32_t handle;
	u32 domain = QXL_GEM_DOMAIN_VRAM;

	if (qxl_alloc->size == 0) {
		DRM_ERROR("invalid size %d\n", qxl_alloc->size);
		return -EINVAL;
	}
	ret = qxl_gem_object_create_with_handle(qdev, file_priv,
						domain,
						qxl_alloc->size,
						NULL,
						&qobj, &handle);
	if (ret) {
		DRM_ERROR("%s: failed to create gem ret=%d\n",
			  __func__, ret);
		return -ENOMEM;
	}
	qxl_alloc->handle = handle;
	return 0;
}

static int qxl_map_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_qxl_map *qxl_map = data;

	return qxl_mode_dumb_mmap(file_priv, &qdev->ddev, qxl_map->handle,
				  &qxl_map->offset);
}

struct qxl_reloc_info {
	int type;
	struct qxl_bo *dst_bo;
	uint32_t dst_offset;
	struct qxl_bo *src_bo;
	int src_offset;
};

/*
 * dst must be validated, i.e. whole bo on vram/surfacesram (right now all bo's
 * are on vram).
 * *(dst + dst_off) = qxl_bo_physical_address(src, src_off)
 */
static void
apply_reloc(struct qxl_device *qdev, struct qxl_reloc_info *info)
{
	void *reloc_page;

	reloc_page = qxl_bo_kmap_atomic_page(qdev, info->dst_bo, info->dst_offset & PAGE_MASK);
	*(uint64_t *)(reloc_page + (info->dst_offset & ~PAGE_MASK)) = qxl_bo_physical_address(qdev,
											      info->src_bo,
											      info->src_offset);
	qxl_bo_kunmap_atomic_page(qdev, info->dst_bo, reloc_page);
}

static void
apply_surf_reloc(struct qxl_device *qdev, struct qxl_reloc_info *info)
{
	uint32_t id = 0;
	void *reloc_page;

	if (info->src_bo && !info->src_bo->is_primary)
		id = info->src_bo->surface_id;

	reloc_page = qxl_bo_kmap_atomic_page(qdev, info->dst_bo, info->dst_offset & PAGE_MASK);
	*(uint32_t *)(reloc_page + (info->dst_offset & ~PAGE_MASK)) = id;
	qxl_bo_kunmap_atomic_page(qdev, info->dst_bo, reloc_page);
}

/* return holding the reference to this object */
static int qxlhw_handle_to_bo(struct drm_file *file_priv, uint64_t handle,
			      struct qxl_release *release, struct qxl_bo **qbo_p)
{
	struct drm_gem_object *gobj;
	struct qxl_bo *qobj;
	int ret;

	gobj = drm_gem_object_lookup(file_priv, handle);
	if (!gobj)
		return -EINVAL;

	qobj = gem_to_qxl_bo(gobj);

	ret = qxl_release_list_add(release, qobj);
	drm_gem_object_put_unlocked(gobj);
	if (ret)
		return ret;

	*qbo_p = qobj;
	return 0;
}

/*
 * Usage of execbuffer:
 * Relocations need to take into account the full QXLDrawable size.
 * However, the command as passed from user space must *not* contain the initial
 * QXLReleaseInfo struct (first XXX bytes)
 */
static int qxl_process_single_command(struct qxl_device *qdev,
				      struct drm_qxl_command *cmd,
				      struct drm_file *file_priv)
{
	struct qxl_reloc_info *reloc_info;
	int release_type;
	struct qxl_release *release;
	struct qxl_bo *cmd_bo;
	void *fb_cmd;
	int i, ret, num_relocs;
	int unwritten;

	switch (cmd->type) {
	case QXL_CMD_DRAW:
		release_type = QXL_RELEASE_DRAWABLE;
		break;
	case QXL_CMD_SURFACE:
	case QXL_CMD_CURSOR:
	default:
		DRM_DEBUG("Only draw commands in execbuffers\n");
		return -EINVAL;
		break;
	}

	if (cmd->command_size > PAGE_SIZE - sizeof(union qxl_release_info))
		return -EINVAL;

	if (!access_ok(u64_to_user_ptr(cmd->command),
		       cmd->command_size))
		return -EFAULT;

	reloc_info = kmalloc_array(cmd->relocs_num,
				   sizeof(struct qxl_reloc_info), GFP_KERNEL);
	if (!reloc_info)
		return -ENOMEM;

	ret = qxl_alloc_release_reserved(qdev,
					 sizeof(union qxl_release_info) +
					 cmd->command_size,
					 release_type,
					 &release,
					 &cmd_bo);
	if (ret)
		goto out_free_reloc;

	/* TODO copy slow path code from i915 */
	fb_cmd = qxl_bo_kmap_atomic_page(qdev, cmd_bo, (release->release_offset & PAGE_MASK));
	unwritten = __copy_from_user_inatomic_nocache
		(fb_cmd + sizeof(union qxl_release_info) + (release->release_offset & ~PAGE_MASK),
		 u64_to_user_ptr(cmd->command), cmd->command_size);

	{
		struct qxl_drawable *draw = fb_cmd;

		draw->mm_time = qdev->rom->mm_clock;
	}

	qxl_bo_kunmap_atomic_page(qdev, cmd_bo, fb_cmd);
	if (unwritten) {
		DRM_ERROR("got unwritten %d\n", unwritten);
		ret = -EFAULT;
		goto out_free_release;
	}

	/* fill out reloc info structs */
	num_relocs = 0;
	for (i = 0; i < cmd->relocs_num; ++i) {
		struct drm_qxl_reloc reloc;
		struct drm_qxl_reloc __user *u = u64_to_user_ptr(cmd->relocs);

		if (copy_from_user(&reloc, u + i, sizeof(reloc))) {
			ret = -EFAULT;
			goto out_free_bos;
		}

		/* add the bos to the list of bos to validate -
		   need to validate first then process relocs? */
		if (reloc.reloc_type != QXL_RELOC_TYPE_BO && reloc.reloc_type != QXL_RELOC_TYPE_SURF) {
			DRM_DEBUG("unknown reloc type %d\n", reloc.reloc_type);

			ret = -EINVAL;
			goto out_free_bos;
		}
		reloc_info[i].type = reloc.reloc_type;

		if (reloc.dst_handle) {
			ret = qxlhw_handle_to_bo(file_priv, reloc.dst_handle, release,
						 &reloc_info[i].dst_bo);
			if (ret)
				goto out_free_bos;
			reloc_info[i].dst_offset = reloc.dst_offset;
		} else {
			reloc_info[i].dst_bo = cmd_bo;
			reloc_info[i].dst_offset = reloc.dst_offset + release->release_offset;
		}
		num_relocs++;

		/* reserve and validate the reloc dst bo */
		if (reloc.reloc_type == QXL_RELOC_TYPE_BO || reloc.src_handle) {
			ret = qxlhw_handle_to_bo(file_priv, reloc.src_handle, release,
						 &reloc_info[i].src_bo);
			if (ret)
				goto out_free_bos;
			reloc_info[i].src_offset = reloc.src_offset;
		} else {
			reloc_info[i].src_bo = NULL;
			reloc_info[i].src_offset = 0;
		}
	}

	/* validate all buffers */
	ret = qxl_release_reserve_list(release, false);
	if (ret)
		goto out_free_bos;

	for (i = 0; i < cmd->relocs_num; ++i) {
		if (reloc_info[i].type == QXL_RELOC_TYPE_BO)
			apply_reloc(qdev, &reloc_info[i]);
		else if (reloc_info[i].type == QXL_RELOC_TYPE_SURF)
			apply_surf_reloc(qdev, &reloc_info[i]);
	}

	qxl_release_fence_buffer_objects(release);
	ret = qxl_push_command_ring_release(qdev, release, cmd->type, true);

out_free_bos:
out_free_release:
	if (ret)
		qxl_release_free(qdev, release);
out_free_reloc:
	kfree(reloc_info);
	return ret;
}

static int qxl_execbuffer_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_qxl_execbuffer *execbuffer = data;
	struct drm_qxl_command user_cmd;
	int cmd_num;
	int ret;

	for (cmd_num = 0; cmd_num < execbuffer->commands_num; ++cmd_num) {

		struct drm_qxl_command __user *commands =
			u64_to_user_ptr(execbuffer->commands);

		if (copy_from_user(&user_cmd, commands + cmd_num,
				       sizeof(user_cmd)))
			return -EFAULT;

		ret = qxl_process_single_command(qdev, &user_cmd, file_priv);
		if (ret)
			return ret;
	}
	return 0;
}

static int qxl_update_area_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file)
{
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_qxl_update_area *update_area = data;
	struct qxl_rect area = {.left = update_area->left,
				.top = update_area->top,
				.right = update_area->right,
				.bottom = update_area->bottom};
	int ret;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qobj = NULL;
	struct ttm_operation_ctx ctx = { true, false };

	if (update_area->left >= update_area->right ||
	    update_area->top >= update_area->bottom)
		return -EINVAL;

	gobj = drm_gem_object_lookup(file, update_area->handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_qxl_bo(gobj);

	ret = qxl_bo_reserve(qobj, false);
	if (ret)
		goto out;

	if (!qobj->pin_count) {
		qxl_ttm_placement_from_domain(qobj, qobj->type, false);
		ret = ttm_bo_validate(&qobj->tbo, &qobj->placement, &ctx);
		if (unlikely(ret))
			goto out;
	}

	ret = qxl_bo_check_id(qdev, qobj);
	if (ret)
		goto out2;
	if (!qobj->surface_id)
		DRM_ERROR("got update area for surface with no id %d\n", update_area->handle);
	ret = qxl_io_update_area(qdev, qobj, &area);

out2:
	qxl_bo_unreserve(qobj);

out:
	drm_gem_object_put_unlocked(gobj);
	return ret;
}

static int qxl_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_qxl_getparam *param = data;

	switch (param->param) {
	case QXL_PARAM_NUM_SURFACES:
		param->value = qdev->rom->n_surfaces;
		break;
	case QXL_PARAM_MAX_RELOCS:
		param->value = QXL_MAX_RES;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int qxl_clientcap_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_qxl_clientcap *param = data;
	int byte, idx;

	byte = param->index / 8;
	idx = param->index % 8;

	if (dev->pdev->revision < 4)
		return -ENOSYS;

	if (byte >= 58)
		return -ENOSYS;

	if (qdev->rom->client_capabilities[byte] & (1 << idx))
		return 0;
	return -ENOSYS;
}

static int qxl_alloc_surf_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file)
{
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_qxl_alloc_surf *param = data;
	struct qxl_bo *qobj;
	int handle;
	int ret;
	int size, actual_stride;
	struct qxl_surface surf;

	/* work out size allocate bo with handle */
	actual_stride = param->stride < 0 ? -param->stride : param->stride;
	size = actual_stride * param->height + actual_stride;

	surf.format = param->format;
	surf.width = param->width;
	surf.height = param->height;
	surf.stride = param->stride;
	surf.data = 0;

	ret = qxl_gem_object_create_with_handle(qdev, file,
						QXL_GEM_DOMAIN_SURFACE,
						size,
						&surf,
						&qobj, &handle);
	if (ret) {
		DRM_ERROR("%s: failed to create gem ret=%d\n",
			  __func__, ret);
		return -ENOMEM;
	} else
		param->handle = handle;
	return ret;
}

const struct drm_ioctl_desc qxl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(QXL_ALLOC, qxl_alloc_ioctl, DRM_AUTH),

	DRM_IOCTL_DEF_DRV(QXL_MAP, qxl_map_ioctl, DRM_AUTH),

	DRM_IOCTL_DEF_DRV(QXL_EXECBUFFER, qxl_execbuffer_ioctl,
							DRM_AUTH),
	DRM_IOCTL_DEF_DRV(QXL_UPDATE_AREA, qxl_update_area_ioctl,
							DRM_AUTH),
	DRM_IOCTL_DEF_DRV(QXL_GETPARAM, qxl_getparam_ioctl,
							DRM_AUTH),
	DRM_IOCTL_DEF_DRV(QXL_CLIENTCAP, qxl_clientcap_ioctl,
							DRM_AUTH),

	DRM_IOCTL_DEF_DRV(QXL_ALLOC_SURF, qxl_alloc_surf_ioctl,
			  DRM_AUTH),
};

int qxl_max_ioctls = ARRAY_SIZE(qxl_ioctls);
