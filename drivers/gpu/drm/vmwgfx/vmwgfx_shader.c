// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2023 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/ttm/ttm_placement.h>

#include "vmwgfx_binding.h"
#include "vmwgfx_bo.h"
#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"

struct vmw_shader {
	struct vmw_resource res;
	SVGA3dShaderType type;
	uint32_t size;
	uint8_t num_input_sig;
	uint8_t num_output_sig;
};

struct vmw_user_shader {
	struct ttm_base_object base;
	struct vmw_shader shader;
};

struct vmw_dx_shader {
	struct vmw_resource res;
	struct vmw_resource *ctx;
	struct vmw_resource *cotable;
	u32 id;
	bool committed;
	struct list_head cotable_head;
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

static int vmw_dx_shader_create(struct vmw_resource *res);
static int vmw_dx_shader_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf);
static int vmw_dx_shader_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf);
static void vmw_dx_shader_commit_notify(struct vmw_resource *res,
					enum vmw_cmdbuf_res_state state);
static bool vmw_shader_id_ok(u32 user_key, SVGA3dShaderType shader_type);
static u32 vmw_shader_key(u32 user_key, SVGA3dShaderType shader_type);

static const struct vmw_user_resource_conv user_shader_conv = {
	.object_type = VMW_RES_SHADER,
	.base_obj_to_res = vmw_user_shader_base_to_res,
	.res_free = vmw_user_shader_free
};

const struct vmw_user_resource_conv *user_shader_converter =
	&user_shader_conv;


static const struct vmw_res_func vmw_gb_shader_func = {
	.res_type = vmw_res_shader,
	.needs_guest_memory = true,
	.may_evict = true,
	.prio = 3,
	.dirty_prio = 3,
	.type_name = "guest backed shaders",
	.domain = VMW_BO_DOMAIN_MOB,
	.busy_domain = VMW_BO_DOMAIN_MOB,
	.create = vmw_gb_shader_create,
	.destroy = vmw_gb_shader_destroy,
	.bind = vmw_gb_shader_bind,
	.unbind = vmw_gb_shader_unbind
};

static const struct vmw_res_func vmw_dx_shader_func = {
	.res_type = vmw_res_shader,
	.needs_guest_memory = true,
	.may_evict = true,
	.prio = 3,
	.dirty_prio = 3,
	.type_name = "dx shaders",
	.domain = VMW_BO_DOMAIN_MOB,
	.busy_domain = VMW_BO_DOMAIN_MOB,
	.create = vmw_dx_shader_create,
	/*
	 * The destroy callback is only called with a committed resource on
	 * context destroy, in which case we destroy the cotable anyway,
	 * so there's no need to destroy DX shaders separately.
	 */
	.destroy = NULL,
	.bind = vmw_dx_shader_bind,
	.unbind = vmw_dx_shader_unbind,
	.commit_notify = vmw_dx_shader_commit_notify,
};

/*
 * Shader management:
 */

static inline struct vmw_shader *
vmw_res_to_shader(struct vmw_resource *res)
{
	return container_of(res, struct vmw_shader, res);
}

/**
 * vmw_res_to_dx_shader - typecast a struct vmw_resource to a
 * struct vmw_dx_shader
 *
 * @res: Pointer to the struct vmw_resource.
 */
static inline struct vmw_dx_shader *
vmw_res_to_dx_shader(struct vmw_resource *res)
{
	return container_of(res, struct vmw_dx_shader, res);
}

static void vmw_hw_shader_destroy(struct vmw_resource *res)
{
	if (likely(res->func->destroy))
		(void) res->func->destroy(res);
	else
		res->id = -1;
}


static int vmw_gb_shader_init(struct vmw_private *dev_priv,
			      struct vmw_resource *res,
			      uint32_t size,
			      uint64_t offset,
			      SVGA3dShaderType type,
			      uint8_t num_input_sig,
			      uint8_t num_output_sig,
			      struct vmw_bo *byte_code,
			      void (*res_free) (struct vmw_resource *res))
{
	struct vmw_shader *shader = vmw_res_to_shader(res);
	int ret;

	ret = vmw_resource_init(dev_priv, res, true, res_free,
				&vmw_gb_shader_func);

	if (unlikely(ret != 0)) {
		if (res_free)
			res_free(res);
		else
			kfree(res);
		return ret;
	}

	res->guest_memory_size = size;
	if (byte_code) {
		res->guest_memory_bo = vmw_user_bo_ref(byte_code);
		res->guest_memory_offset = offset;
	}
	shader->size = size;
	shader->type = type;
	shader->num_input_sig = num_input_sig;
	shader->num_output_sig = num_output_sig;

	res->hw_destroy = vmw_hw_shader_destroy;
	return 0;
}

/*
 * GB shader code:
 */

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

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	cmd->header.id = SVGA_3D_CMD_DEFINE_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	cmd->body.type = shader->type;
	cmd->body.sizeInBytes = shader->size;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	vmw_fifo_resource_inc(dev_priv);

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

	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_BIND_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	cmd->body.mobid = bo->resource->start;
	cmd->body.offsetInBytes = res->guest_memory_offset;
	res->guest_memory_dirty = false;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

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

	BUG_ON(res->guest_memory_bo->tbo.resource->mem_type != VMW_PL_MOB);

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_BIND_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	cmd->body.mobid = SVGA3D_INVALID_ID;
	cmd->body.offsetInBytes = 0;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	/*
	 * Create a fence object and fence the backup buffer.
	 */

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);

	vmw_bo_fence_single(val_buf->bo, fence);

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
	vmw_binding_res_list_scrub(&res->binding_head);

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		mutex_unlock(&dev_priv->binding_mutex);
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_DESTROY_GB_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.shid = res->id;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	mutex_unlock(&dev_priv->binding_mutex);
	vmw_resource_release_id(res);
	vmw_fifo_resource_dec(dev_priv);

	return 0;
}

/*
 * DX shader code:
 */

/**
 * vmw_dx_shader_commit_notify - Notify that a shader operation has been
 * committed to hardware from a user-supplied command stream.
 *
 * @res: Pointer to the shader resource.
 * @state: Indicating whether a creation or removal has been committed.
 *
 */
static void vmw_dx_shader_commit_notify(struct vmw_resource *res,
					enum vmw_cmdbuf_res_state state)
{
	struct vmw_dx_shader *shader = vmw_res_to_dx_shader(res);
	struct vmw_private *dev_priv = res->dev_priv;

	if (state == VMW_CMDBUF_RES_ADD) {
		mutex_lock(&dev_priv->binding_mutex);
		vmw_cotable_add_resource(shader->cotable,
					 &shader->cotable_head);
		shader->committed = true;
		res->id = shader->id;
		mutex_unlock(&dev_priv->binding_mutex);
	} else {
		mutex_lock(&dev_priv->binding_mutex);
		list_del_init(&shader->cotable_head);
		shader->committed = false;
		res->id = -1;
		mutex_unlock(&dev_priv->binding_mutex);
	}
}

/**
 * vmw_dx_shader_unscrub - Have the device reattach a MOB to a DX shader.
 *
 * @res: The shader resource
 *
 * This function reverts a scrub operation.
 */
static int vmw_dx_shader_unscrub(struct vmw_resource *res)
{
	struct vmw_dx_shader *shader = vmw_res_to_dx_shader(res);
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXBindShader body;
	} *cmd;

	if (!list_empty(&shader->cotable_head) || !shader->committed)
		return 0;

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), shader->ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_BIND_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = shader->ctx->id;
	cmd->body.shid = shader->id;
	cmd->body.mobid = res->guest_memory_bo->tbo.resource->start;
	cmd->body.offsetInBytes = res->guest_memory_offset;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	vmw_cotable_add_resource(shader->cotable, &shader->cotable_head);

	return 0;
}

/**
 * vmw_dx_shader_create - The DX shader create callback
 *
 * @res: The DX shader resource
 *
 * The create callback is called as part of resource validation and
 * makes sure that we unscrub the shader if it's previously been scrubbed.
 */
static int vmw_dx_shader_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_dx_shader *shader = vmw_res_to_dx_shader(res);
	int ret = 0;

	WARN_ON_ONCE(!shader->committed);

	if (vmw_resource_mob_attached(res)) {
		mutex_lock(&dev_priv->binding_mutex);
		ret = vmw_dx_shader_unscrub(res);
		mutex_unlock(&dev_priv->binding_mutex);
	}

	res->id = shader->id;
	return ret;
}

/**
 * vmw_dx_shader_bind - The DX shader bind callback
 *
 * @res: The DX shader resource
 * @val_buf: Pointer to the validate buffer.
 *
 */
static int vmw_dx_shader_bind(struct vmw_resource *res,
			      struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct ttm_buffer_object *bo = val_buf->bo;

	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);
	mutex_lock(&dev_priv->binding_mutex);
	vmw_dx_shader_unscrub(res);
	mutex_unlock(&dev_priv->binding_mutex);

	return 0;
}

/**
 * vmw_dx_shader_scrub - Have the device unbind a MOB from a DX shader.
 *
 * @res: The shader resource
 *
 * This function unbinds a MOB from the DX shader without requiring the
 * MOB dma_buffer to be reserved. The driver still considers the MOB bound.
 * However, once the driver eventually decides to unbind the MOB, it doesn't
 * need to access the context.
 */
static int vmw_dx_shader_scrub(struct vmw_resource *res)
{
	struct vmw_dx_shader *shader = vmw_res_to_dx_shader(res);
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXBindShader body;
	} *cmd;

	if (list_empty(&shader->cotable_head))
		return 0;

	WARN_ON_ONCE(!shader->committed);
	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_BIND_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = shader->ctx->id;
	cmd->body.shid = res->id;
	cmd->body.mobid = SVGA3D_INVALID_ID;
	cmd->body.offsetInBytes = 0;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	res->id = -1;
	list_del_init(&shader->cotable_head);

	return 0;
}

/**
 * vmw_dx_shader_unbind - The dx shader unbind callback.
 *
 * @res: The shader resource
 * @readback: Whether this is a readback unbind. Currently unused.
 * @val_buf: MOB buffer information.
 */
static int vmw_dx_shader_unbind(struct vmw_resource *res,
				bool readback,
				struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_fence_obj *fence;
	int ret;

	BUG_ON(res->guest_memory_bo->tbo.resource->mem_type != VMW_PL_MOB);

	mutex_lock(&dev_priv->binding_mutex);
	ret = vmw_dx_shader_scrub(res);
	mutex_unlock(&dev_priv->binding_mutex);

	if (ret)
		return ret;

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);
	vmw_bo_fence_single(val_buf->bo, fence);

	if (likely(fence != NULL))
		vmw_fence_obj_unreference(&fence);

	return 0;
}

/**
 * vmw_dx_shader_cotable_list_scrub - The cotable unbind_func callback for
 * DX shaders.
 *
 * @dev_priv: Pointer to device private structure.
 * @list: The list of cotable resources.
 * @readback: Whether the call was part of a readback unbind.
 *
 * Scrubs all shader MOBs so that any subsequent shader unbind or shader
 * destroy operation won't need to swap in the context.
 */
void vmw_dx_shader_cotable_list_scrub(struct vmw_private *dev_priv,
				      struct list_head *list,
				      bool readback)
{
	struct vmw_dx_shader *entry, *next;

	lockdep_assert_held_once(&dev_priv->binding_mutex);

	list_for_each_entry_safe(entry, next, list, cotable_head) {
		WARN_ON(vmw_dx_shader_scrub(&entry->res));
		if (!readback)
			entry->committed = false;
	}
}

/**
 * vmw_dx_shader_res_free - The DX shader free callback
 *
 * @res: The shader resource
 *
 * Frees the DX shader resource.
 */
static void vmw_dx_shader_res_free(struct vmw_resource *res)
{
	struct vmw_dx_shader *shader = vmw_res_to_dx_shader(res);

	vmw_resource_unreference(&shader->cotable);
	kfree(shader);
}

/**
 * vmw_dx_shader_add - Add a shader resource as a command buffer managed
 * resource.
 *
 * @man: The command buffer resource manager.
 * @ctx: Pointer to the context resource.
 * @user_key: The id used for this shader.
 * @shader_type: The shader type.
 * @list: The list of staged command buffer managed resources.
 */
int vmw_dx_shader_add(struct vmw_cmdbuf_res_manager *man,
		      struct vmw_resource *ctx,
		      u32 user_key,
		      SVGA3dShaderType shader_type,
		      struct list_head *list)
{
	struct vmw_dx_shader *shader;
	struct vmw_resource *res;
	struct vmw_private *dev_priv = ctx->dev_priv;
	int ret;

	if (!vmw_shader_id_ok(user_key, shader_type))
		return -EINVAL;

	shader = kmalloc(sizeof(*shader), GFP_KERNEL);
	if (!shader) {
		return -ENOMEM;
	}

	res = &shader->res;
	shader->ctx = ctx;
	shader->cotable = vmw_resource_reference
		(vmw_context_cotable(ctx, SVGA_COTABLE_DXSHADER));
	shader->id = user_key;
	shader->committed = false;
	INIT_LIST_HEAD(&shader->cotable_head);
	ret = vmw_resource_init(dev_priv, res, true,
				vmw_dx_shader_res_free, &vmw_dx_shader_func);
	if (ret)
		goto out_resource_init;

	/*
	 * The user_key name-space is not per shader type for DX shaders,
	 * so when hashing, use a single zero shader type.
	 */
	ret = vmw_cmdbuf_res_add(man, vmw_cmdbuf_res_shader,
				 vmw_shader_key(user_key, 0),
				 res, list);
	if (ret)
		goto out_resource_init;

	res->id = shader->id;
	res->hw_destroy = vmw_hw_shader_destroy;

out_resource_init:
	vmw_resource_unreference(&res);

	return ret;
}



/*
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

	ttm_base_object_kfree(ushader, base);
}

static void vmw_shader_free(struct vmw_resource *res)
{
	struct vmw_shader *shader = vmw_res_to_shader(res);

	kfree(shader);
}

/*
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

	return ttm_ref_object_base_unref(tfile, arg->handle);
}

static int vmw_user_shader_alloc(struct vmw_private *dev_priv,
				 struct vmw_bo *buffer,
				 size_t shader_size,
				 size_t offset,
				 SVGA3dShaderType shader_type,
				 uint8_t num_input_sig,
				 uint8_t num_output_sig,
				 struct ttm_object_file *tfile,
				 u32 *handle)
{
	struct vmw_user_shader *ushader;
	struct vmw_resource *res, *tmp;
	int ret;

	ushader = kzalloc(sizeof(*ushader), GFP_KERNEL);
	if (unlikely(!ushader)) {
		ret = -ENOMEM;
		goto out;
	}

	res = &ushader->shader.res;
	ushader->base.shareable = false;
	ushader->base.tfile = NULL;

	/*
	 * From here on, the destructor takes over resource freeing.
	 */

	ret = vmw_gb_shader_init(dev_priv, res, shader_size,
				 offset, shader_type, num_input_sig,
				 num_output_sig, buffer,
				 vmw_user_shader_free);
	if (unlikely(ret != 0))
		goto out;

	tmp = vmw_resource_reference(res);
	ret = ttm_base_object_init(tfile, &ushader->base, false,
				   VMW_RES_SHADER,
				   &vmw_user_shader_base_release);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	if (handle)
		*handle = ushader->base.handle;
out_err:
	vmw_resource_unreference(&res);
out:
	return ret;
}


static struct vmw_resource *vmw_shader_alloc(struct vmw_private *dev_priv,
					     struct vmw_bo *buffer,
					     size_t shader_size,
					     size_t offset,
					     SVGA3dShaderType shader_type)
{
	struct vmw_shader *shader;
	struct vmw_resource *res;
	int ret;

	shader = kzalloc(sizeof(*shader), GFP_KERNEL);
	if (unlikely(!shader)) {
		ret = -ENOMEM;
		goto out_err;
	}

	res = &shader->res;

	/*
	 * From here on, the destructor takes over resource freeing.
	 */
	ret = vmw_gb_shader_init(dev_priv, res, shader_size,
				 offset, shader_type, 0, 0, buffer,
				 vmw_shader_free);

out_err:
	return ret ? ERR_PTR(ret) : res;
}


static int vmw_shader_define(struct drm_device *dev, struct drm_file *file_priv,
			     enum drm_vmw_shader_type shader_type_drm,
			     u32 buffer_handle, size_t size, size_t offset,
			     uint8_t num_input_sig, uint8_t num_output_sig,
			     uint32_t *shader_handle)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_bo *buffer = NULL;
	SVGA3dShaderType shader_type;
	int ret;

	if (buffer_handle != SVGA3D_INVALID_ID) {
		ret = vmw_user_bo_lookup(file_priv, buffer_handle, &buffer);
		if (unlikely(ret != 0)) {
			VMW_DEBUG_USER("Couldn't find buffer for shader creation.\n");
			return ret;
		}

		if ((u64)buffer->tbo.base.size < (u64)size + (u64)offset) {
			VMW_DEBUG_USER("Illegal buffer- or shader size.\n");
			ret = -EINVAL;
			goto out_bad_arg;
		}
	}

	switch (shader_type_drm) {
	case drm_vmw_shader_type_vs:
		shader_type = SVGA3D_SHADERTYPE_VS;
		break;
	case drm_vmw_shader_type_ps:
		shader_type = SVGA3D_SHADERTYPE_PS;
		break;
	default:
		VMW_DEBUG_USER("Illegal shader type.\n");
		ret = -EINVAL;
		goto out_bad_arg;
	}

	ret = vmw_user_shader_alloc(dev_priv, buffer, size, offset,
				    shader_type, num_input_sig,
				    num_output_sig, tfile, shader_handle);
out_bad_arg:
	vmw_user_bo_unref(&buffer);
	return ret;
}

/**
 * vmw_shader_id_ok - Check whether a compat shader user key and
 * shader type are within valid bounds.
 *
 * @user_key: User space id of the shader.
 * @shader_type: Shader type.
 *
 * Returns true if valid false if not.
 */
static bool vmw_shader_id_ok(u32 user_key, SVGA3dShaderType shader_type)
{
	return user_key <= ((1 << 20) - 1) && (unsigned) shader_type < 16;
}

/**
 * vmw_shader_key - Compute a hash key suitable for a compat shader.
 *
 * @user_key: User space id of the shader.
 * @shader_type: Shader type.
 *
 * Returns a hash key suitable for a command buffer managed resource
 * manager hash table.
 */
static u32 vmw_shader_key(u32 user_key, SVGA3dShaderType shader_type)
{
	return user_key | (shader_type << 20);
}

/**
 * vmw_shader_remove - Stage a compat shader for removal.
 *
 * @man: Pointer to the compat shader manager identifying the shader namespace.
 * @user_key: The key that is used to identify the shader. The key is
 * unique to the shader type.
 * @shader_type: Shader type.
 * @list: Caller's list of staged command buffer resource actions.
 */
int vmw_shader_remove(struct vmw_cmdbuf_res_manager *man,
		      u32 user_key, SVGA3dShaderType shader_type,
		      struct list_head *list)
{
	struct vmw_resource *dummy;

	if (!vmw_shader_id_ok(user_key, shader_type))
		return -EINVAL;

	return vmw_cmdbuf_res_remove(man, vmw_cmdbuf_res_shader,
				     vmw_shader_key(user_key, shader_type),
				     list, &dummy);
}

/**
 * vmw_compat_shader_add - Create a compat shader and stage it for addition
 * as a command buffer managed resource.
 *
 * @dev_priv: Pointer to device private structure.
 * @man: Pointer to the compat shader manager identifying the shader namespace.
 * @user_key: The key that is used to identify the shader. The key is
 * unique to the shader type.
 * @bytecode: Pointer to the bytecode of the shader.
 * @shader_type: Shader type.
 * @size: Command size.
 * @list: Caller's list of staged command buffer resource actions.
 *
 */
int vmw_compat_shader_add(struct vmw_private *dev_priv,
			  struct vmw_cmdbuf_res_manager *man,
			  u32 user_key, const void *bytecode,
			  SVGA3dShaderType shader_type,
			  size_t size,
			  struct list_head *list)
{
	struct ttm_operation_ctx ctx = { false, true };
	struct vmw_bo *buf;
	struct ttm_bo_kmap_obj map;
	bool is_iomem;
	int ret;
	struct vmw_resource *res;
	struct vmw_bo_params bo_params = {
		.domain = VMW_BO_DOMAIN_SYS,
		.busy_domain = VMW_BO_DOMAIN_SYS,
		.bo_type = ttm_bo_type_device,
		.size = size,
		.pin = true
	};

	if (!vmw_shader_id_ok(user_key, shader_type))
		return -EINVAL;

	ret = vmw_bo_create(dev_priv, &bo_params, &buf);
	if (unlikely(ret != 0))
		goto out;

	ret = ttm_bo_reserve(&buf->tbo, false, true, NULL);
	if (unlikely(ret != 0))
		goto no_reserve;

	/* Map and copy shader bytecode. */
	ret = ttm_bo_kmap(&buf->tbo, 0, PFN_UP(size), &map);
	if (unlikely(ret != 0)) {
		ttm_bo_unreserve(&buf->tbo);
		goto no_reserve;
	}

	memcpy(ttm_kmap_obj_virtual(&map, &is_iomem), bytecode, size);
	WARN_ON(is_iomem);

	ttm_bo_kunmap(&map);
	ret = ttm_bo_validate(&buf->tbo, &buf->placement, &ctx);
	WARN_ON(ret != 0);
	ttm_bo_unreserve(&buf->tbo);

	res = vmw_shader_alloc(dev_priv, buf, size, 0, shader_type);
	if (unlikely(ret != 0))
		goto no_reserve;

	ret = vmw_cmdbuf_res_add(man, vmw_cmdbuf_res_shader,
				 vmw_shader_key(user_key, shader_type),
				 res, list);
	vmw_resource_unreference(&res);
no_reserve:
	vmw_bo_unreference(&buf);
out:
	return ret;
}

/**
 * vmw_shader_lookup - Look up a compat shader
 *
 * @man: Pointer to the command buffer managed resource manager identifying
 * the shader namespace.
 * @user_key: The user space id of the shader.
 * @shader_type: The shader type.
 *
 * Returns a refcounted pointer to a struct vmw_resource if the shader was
 * found. An error pointer otherwise.
 */
struct vmw_resource *
vmw_shader_lookup(struct vmw_cmdbuf_res_manager *man,
		  u32 user_key,
		  SVGA3dShaderType shader_type)
{
	if (!vmw_shader_id_ok(user_key, shader_type))
		return ERR_PTR(-EINVAL);

	return vmw_cmdbuf_res_lookup(man, vmw_cmdbuf_res_shader,
				     vmw_shader_key(user_key, shader_type));
}

int vmw_shader_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct drm_vmw_shader_create_arg *arg =
		(struct drm_vmw_shader_create_arg *)data;

	return vmw_shader_define(dev, file_priv, arg->shader_type,
				 arg->buffer_handle,
				 arg->size, arg->offset,
				 0, 0,
				 &arg->shader_handle);
}
