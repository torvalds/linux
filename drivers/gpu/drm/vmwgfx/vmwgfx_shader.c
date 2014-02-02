/**************************************************************************
 *
 * Copyright © 2009-2012 VMware, Inc., Palo Alto, CA., USA
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

#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"
#include "ttm/ttm_placement.h"

struct vmw_shader {
	struct vmw_resource res;
	SVGA3dShaderType type;
	uint32_t size;
};

struct vmw_user_shader {
	struct ttm_base_object base;
	struct vmw_shader shader;
};

static void vmw_user_shader_free(struct vmw_resource *res);
static struct vmw_resource *
vmw_user_shader_base_to_res(struct ttm_base_object *base);

static int vmw_gb_shader_create(struct vmw_resource *res);
static int vmw_gb_shader_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf);
static int vmw_gb_shader_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf);
static int vmw_gb_shader_destroy(struct vmw_resource *res);

static uint64_t vmw_user_shader_size;

static const struct vmw_user_resource_conv user_shader_conv = {
	.object_type = VMW_RES_SHADER,
	.base_obj_to_res = vmw_user_shader_base_to_res,
	.res_free = vmw_user_shader_free
};

const struct vmw_user_resource_conv *user_shader_converter =
	&user_shader_conv;


static const struct vmw_res_func vmw_gb_shader_func = {
	.res_type = vmw_res_shader,
	.needs_backup = true,
	.may_evict = true,
	.type_name = "guest backed shaders",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_gb_shader_create,
	.destroy = vmw_gb_shader_destroy,
	.bind = vmw_gb_shader_bind,
	.unbind = vmw_gb_shader_unbind
};

/**
 * Shader management:
 */

static inline struct vmw_shader *
vmw_res_to_shader(struct vmw_resource *res)
{
	return container_of(res, struct vmw_shader, res);
}

static void vmw_hw_shader_destroy(struct vmw_resource *res)
{
	(void) vmw_gb_shader_destroy(res);
}

static int vmw_gb_shader_init(struct vmw_private *dev_priv,
			      struct vmw_resource *res,
			      uint32_t size,
			      uint64_t offset,
			      SVGA3dShaderType type,
			      struct vmw_dma_buffer *byte_code,
			      void (*res_free) (struct vmw_resource *res))
{
	struct vmw_shader *shader = vmw_res_to_shader(res);
	int ret;

	ret = vmw_resource_init(dev_priv, res, true,
				res_free, &vmw_gb_shader_func);


	if (unlikely(ret != 0)) {
		if (res_free)
			res_free(res);
		else
			kfree(res);
		return ret;
	}

	res->backup_size = size;
	if (byte_code) {
		res->backup = vmw_dmabuf_reference(byte_code);
		res->backup_offset = offset;
	}
	shader->size = size;
	shader->type = type;

	vmw_resource_activate(res, vmw_hw_shader_destroy);
	return 0;
}

static int vmw_gb_shader_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_shader *shader = vmw_res_to_shader(res);
	int ret;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBShader body;
	} *cmd;

	if (likely(res->id != -1))
		return 0;

	ret = vmw_resource_alloc_id(res);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to allocate a shader id.\n");
		goto out_no_id;
	}

	if (unlikely(res->id >= VMWGFX_NUM_GB_SHADER)) {
		ret = -EBUSY;
		goto out_no_fifo;
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for shader "
			  "creation.\n");
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	cmd->header.id = SVGA_3D_CMD_DEFINE_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	cmd->body.type = shader->type;
	cmd->body.sizeInBytes = shader->size;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	(void) vmw_3d_resource_inc(dev_priv, false);

	return 0;

out_no_fifo:
	vmw_resource_release_id(res);
out_no_id:
	return ret;
}

static int vmw_gb_shader_bind(struct vmw_resource *res,
			      struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBShader body;
	} *cmd;
	struct ttm_buffer_object *bo = val_buf->bo;

	BUG_ON(bo->mem.mem_type != VMW_PL_MOB);

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for shader "
			  "binding.\n");
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_BIND_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	cmd->body.mobid = bo->mem.start;
	cmd->body.offsetInBytes = 0;
	res->backup_dirty = false;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}

static int vmw_gb_shader_unbind(struct vmw_resource *res,
				bool readback,
				struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBShader body;
	} *cmd;
	struct vmw_fence_obj *fence;

	BUG_ON(res->backup->base.mem.mem_type != VMW_PL_MOB);

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for shader "
			  "unbinding.\n");
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_BIND_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	cmd->body.mobid = SVGA3D_INVALID_ID;
	cmd->body.offsetInBytes = 0;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	/*
	 * Create a fence object and fence the backup buffer.
	 */

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);

	vmw_fence_single_bo(val_buf->bo, fence);

	if (likely(fence != NULL))
		vmw_fence_obj_unreference(&fence);

	return 0;
}

static int vmw_gb_shader_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyGBShader body;
	} *cmd;

	if (likely(res->id == -1))
		return 0;

	mutex_lock(&dev_priv->binding_mutex);
	vmw_context_binding_res_list_kill(&res->binding_head);

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for shader "
			  "destruction.\n");
		mutex_unlock(&dev_priv->binding_mutex);
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_DESTROY_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	mutex_unlock(&dev_priv->binding_mutex);
	vmw_resource_release_id(res);
	vmw_3d_resource_dec(dev_priv, false);

	return 0;
}

/**
 * User-space shader management:
 */

static struct vmw_resource *
vmw_user_shader_base_to_res(struct ttm_base_object *base)
{
	return &(container_of(base, struct vmw_user_shader, base)->
		 shader.res);
}

static void vmw_user_shader_free(struct vmw_resource *res)
{
	struct vmw_user_shader *ushader =
		container_of(res, struct vmw_user_shader, shader.res);
	struct vmw_private *dev_priv = res->dev_priv;

	ttm_base_object_kfree(ushader, base);
	ttm_mem_global_free(vmw_mem_glob(dev_priv),
			    vmw_user_shader_size);
}

/**
 * This function is called when user space has no more references on the
 * base object. It releases the base-object's reference on the resource object.
 */

static void vmw_user_shader_base_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct vmw_resource *res = vmw_user_shader_base_to_res(base);

	*p_base = NULL;
	vmw_resource_unreference(&res);
}

int vmw_shader_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_vmw_shader_arg *arg = (struct drm_vmw_shader_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;

	return ttm_ref_object_base_unref(tfile, arg->handle,
					 TTM_REF_USAGE);
}

int vmw_shader_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_shader *ushader;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct drm_vmw_shader_create_arg *arg =
		(struct drm_vmw_shader_create_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	struct vmw_dma_buffer *buffer = NULL;
	SVGA3dShaderType shader_type;
	int ret;

	if (arg->buffer_handle != SVGA3D_INVALID_ID) {
		ret = vmw_user_dmabuf_lookup(tfile, arg->buffer_handle,
					     &buffer);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Could not find buffer for shader "
				  "creation.\n");
			return ret;
		}

		if ((u64)buffer->base.num_pages * PAGE_SIZE <
		    (u64)arg->size + (u64)arg->offset) {
			DRM_ERROR("Illegal buffer- or shader size.\n");
			ret = -EINVAL;
			goto out_bad_arg;
		}
	}

	switch (arg->shader_type) {
	case drm_vmw_shader_type_vs:
		shader_type = SVGA3D_SHADERTYPE_VS;
		break;
	case drm_vmw_shader_type_ps:
		shader_type = SVGA3D_SHADERTYPE_PS;
		break;
	case drm_vmw_shader_type_gs:
		shader_type = SVGA3D_SHADERTYPE_GS;
		break;
	default:
		DRM_ERROR("Illegal shader type.\n");
		ret = -EINVAL;
		goto out_bad_arg;
	}

	/*
	 * Approximate idr memory usage with 128 bytes. It will be limited
	 * by maximum number_of shaders anyway.
	 */

	if (unlikely(vmw_user_shader_size == 0))
		vmw_user_shader_size = ttm_round_pot(sizeof(*ushader))
			+ 128;

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   vmw_user_shader_size,
				   false, true);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for shader"
				  " creation.\n");
		goto out_unlock;
	}

	ushader = kzalloc(sizeof(*ushader), GFP_KERNEL);
	if (unlikely(ushader == NULL)) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    vmw_user_shader_size);
		ret = -ENOMEM;
		goto out_unlock;
	}

	res = &ushader->shader.res;
	ushader->base.shareable = false;
	ushader->base.tfile = NULL;

	/*
	 * From here on, the destructor takes over resource freeing.
	 */

	ret = vmw_gb_shader_init(dev_priv, res, arg->size,
				 arg->offset, shader_type, buffer,
				 vmw_user_shader_free);
	if (unlikely(ret != 0))
		goto out_unlock;

	tmp = vmw_resource_reference(res);
	ret = ttm_base_object_init(tfile, &ushader->base, false,
				   VMW_RES_SHADER,
				   &vmw_user_shader_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	arg->shader_handle = ushader->base.hash.key;
out_err:
	vmw_resource_unreference(&res);
out_unlock:
	ttm_read_unlock(&vmaster->lock);
out_bad_arg:
	vmw_dmabuf_unreference(&buffer);

	return ret;

}
