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
#include "qxl_object.h"

/*
 * TODO: allocating a new gem(in qxl_bo) for each request.
 * This is wasteful since bo's are page aligned.
 */
static int qxl_alloc_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
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
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_map *qxl_map = data;

	return qxl_mode_dumb_mmap(file_priv, qdev->ddev, qxl_map->handle,
				  &qxl_map->offset);
}

/*
 * dst must be validated, i.e. whole bo on vram/surfacesram (right now all bo's
 * are on vram).
 * *(dst + dst_off) = qxl_bo_physical_address(src, src_off)
 */
static void
apply_reloc(struct qxl_device *qdev, struct qxl_bo *dst, uint64_t dst_off,
	    struct qxl_bo *src, uint64_t src_off)
{
	void *reloc_page;

	reloc_page = qxl_bo_kmap_atomic_page(qdev, dst, dst_off & PAGE_MASK);
	*(uint64_t *)(reloc_page + (dst_off & ~PAGE_MASK)) = qxl_bo_physical_address(qdev,
								     src, src_off);
	qxl_bo_kunmap_atomic_page(qdev, dst, reloc_page);
}

static void
apply_surf_reloc(struct qxl_device *qdev, struct qxl_bo *dst, uint64_t dst_off,
		 struct qxl_bo *src)
{
	uint32_t id = 0;
	void *reloc_page;

	if (src && !src->is_primary)
		id = src->surface_id;

	reloc_page = qxl_bo_kmap_atomic_page(qdev, dst, dst_off & PAGE_MASK);
	*(uint32_t *)(reloc_page + (dst_off & ~PAGE_MASK)) = id;
	qxl_bo_kunmap_atomic_page(qdev, dst, reloc_page);
}

/* return holding the reference to this object */
static struct qxl_bo *qxlhw_handle_to_bo(struct qxl_device *qdev,
					 struct drm_file *file_priv, uint64_t handle,
					 struct qxl_reloc_list *reloc_list)
{
	struct drm_gem_object *gobj;
	struct qxl_bo *qobj;
	int ret;

	gobj = drm_gem_object_lookup(qdev->ddev, file_priv, handle);
	if (!gobj) {
		DRM_ERROR("bad bo handle %lld\n", handle);
		return NULL;
	}
	qobj = gem_to_qxl_bo(gobj);

	ret = qxl_bo_list_add(reloc_list, qobj);
	if (ret)
		return NULL;

	return qobj;
}

/*
 * Usage of execbuffer:
 * Relocations need to take into account the full QXLDrawable size.
 * However, the command as passed from user space must *not* contain the initial
 * QXLReleaseInfo struct (first XXX bytes)
 */
static int qxl_execbuffer_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_execbuffer *execbuffer = data;
	struct drm_qxl_command user_cmd;
	int cmd_num;
	struct qxl_bo *reloc_src_bo;
	struct qxl_bo *reloc_dst_bo;
	struct drm_qxl_reloc reloc;
	void *fb_cmd;
	int i, ret;
	struct qxl_reloc_list reloc_list;
	int unwritten;
	uint32_t reloc_dst_offset;
	INIT_LIST_HEAD(&reloc_list.bos);

	for (cmd_num = 0; cmd_num < execbuffer->commands_num; ++cmd_num) {
		struct qxl_release *release;
		struct qxl_bo *cmd_bo;
		int release_type;
		struct drm_qxl_command *commands =
			(struct drm_qxl_command *)(uintptr_t)execbuffer->commands;

		if (DRM_COPY_FROM_USER(&user_cmd, &commands[cmd_num],
				       sizeof(user_cmd)))
			return -EFAULT;
		switch (user_cmd.type) {
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

		if (user_cmd.command_size > PAGE_SIZE - sizeof(union qxl_release_info))
			return -EINVAL;

		if (!access_ok(VERIFY_READ,
			       (void *)(unsigned long)user_cmd.command,
			       user_cmd.command_size))
			return -EFAULT;

		ret = qxl_alloc_release_reserved(qdev,
						 sizeof(union qxl_release_info) +
						 user_cmd.command_size,
						 release_type,
						 &release,
						 &cmd_bo);
		if (ret)
			return ret;

		/* TODO copy slow path code from i915 */
		fb_cmd = qxl_bo_kmap_atomic_page(qdev, cmd_bo, (release->release_offset & PAGE_SIZE));
		unwritten = __copy_from_user_inatomic_nocache(fb_cmd + sizeof(union qxl_release_info) + (release->release_offset & ~PAGE_SIZE), (void *)(unsigned long)user_cmd.command, user_cmd.command_size);
		qxl_bo_kunmap_atomic_page(qdev, cmd_bo, fb_cmd);
		if (unwritten) {
			DRM_ERROR("got unwritten %d\n", unwritten);
			qxl_release_unreserve(qdev, release);
			qxl_release_free(qdev, release);
			return -EFAULT;
		}

		for (i = 0 ; i < user_cmd.relocs_num; ++i) {
			if (DRM_COPY_FROM_USER(&reloc,
					       &((struct drm_qxl_reloc *)(uintptr_t)user_cmd.relocs)[i],
					       sizeof(reloc))) {
				qxl_bo_list_unreserve(&reloc_list, true);
				qxl_release_unreserve(qdev, release);
				qxl_release_free(qdev, release);
				return -EFAULT;
			}

			/* add the bos to the list of bos to validate -
			   need to validate first then process relocs? */
			if (reloc.dst_handle) {
				reloc_dst_bo = qxlhw_handle_to_bo(qdev, file_priv,
								  reloc.dst_handle, &reloc_list);
				if (!reloc_dst_bo) {
					qxl_bo_list_unreserve(&reloc_list, true);
					qxl_release_unreserve(qdev, release);
					qxl_release_free(qdev, release);
					return -EINVAL;
				}
				reloc_dst_offset = 0;
			} else {
				reloc_dst_bo = cmd_bo;
				reloc_dst_offset = release->release_offset;
			}

			/* reserve and validate the reloc dst bo */
			if (reloc.reloc_type == QXL_RELOC_TYPE_BO || reloc.src_handle > 0) {
				reloc_src_bo =
					qxlhw_handle_to_bo(qdev, file_priv,
							   reloc.src_handle, &reloc_list);
				if (!reloc_src_bo) {
					if (reloc_dst_bo != cmd_bo)
						drm_gem_object_unreference_unlocked(&reloc_dst_bo->gem_base);
					qxl_bo_list_unreserve(&reloc_list, true);
					qxl_release_unreserve(qdev, release);
					qxl_release_free(qdev, release);
					return -EINVAL;
				}
			} else
				reloc_src_bo = NULL;
			if (reloc.reloc_type == QXL_RELOC_TYPE_BO) {
				apply_reloc(qdev, reloc_dst_bo, reloc_dst_offset + reloc.dst_offset,
					    reloc_src_bo, reloc.src_offset);
			} else if (reloc.reloc_type == QXL_RELOC_TYPE_SURF) {
				apply_surf_reloc(qdev, reloc_dst_bo, reloc_dst_offset + reloc.dst_offset, reloc_src_bo);
			} else {
				DRM_ERROR("unknown reloc type %d\n", reloc.reloc_type);
				return -EINVAL;
			}

			if (reloc_src_bo && reloc_src_bo != cmd_bo) {
				qxl_release_add_res(qdev, release, reloc_src_bo);
				drm_gem_object_unreference_unlocked(&reloc_src_bo->gem_base);
			}

			if (reloc_dst_bo != cmd_bo)
				drm_gem_object_unreference_unlocked(&reloc_dst_bo->gem_base);
		}
		qxl_fence_releaseable(qdev, release);

		ret = qxl_push_command_ring_release(qdev, release, user_cmd.type, true);
		if (ret == -ERESTARTSYS) {
			qxl_release_unreserve(qdev, release);
			qxl_release_free(qdev, release);
			qxl_bo_list_unreserve(&reloc_list, true);
			return ret;
		}
		qxl_release_unreserve(qdev, release);
	}
	qxl_bo_list_unreserve(&reloc_list, 0);
	return 0;
}

static int qxl_update_area_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_update_area *update_area = data;
	struct qxl_rect area = {.left = update_area->left,
				.top = update_area->top,
				.right = update_area->right,
				.bottom = update_area->bottom};
	int ret;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qobj = NULL;

	if (update_area->left >= update_area->right ||
	    update_area->top >= update_area->bottom)
		return -EINVAL;

	gobj = drm_gem_object_lookup(dev, file, update_area->handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_qxl_bo(gobj);

	ret = qxl_bo_reserve(qobj, false);
	if (ret)
		goto out;

	if (!qobj->pin_count) {
		qxl_ttm_placement_from_domain(qobj, qobj->type);
		ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
				      true, false);
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
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int qxl_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
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
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_clientcap *param = data;
	int byte, idx;

	byte = param->index / 8;
	idx = param->index % 8;

	if (qdev->pdev->revision < 4)
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
	struct qxl_device *qdev = dev->dev_private;
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

struct drm_ioctl_desc qxl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(QXL_ALLOC, qxl_alloc_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(QXL_MAP, qxl_map_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(QXL_EXECBUFFER, qxl_execbuffer_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_UPDATE_AREA, qxl_update_area_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_GETPARAM, qxl_getparam_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_CLIENTCAP, qxl_clientcap_ioctl,
							DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(QXL_ALLOC_SURF, qxl_alloc_surf_ioctl,
			  DRM_AUTH|DRM_UNLOCKED),
};

int qxl_max_ioctls = DRM_ARRAY_SIZE(qxl_ioctls);
