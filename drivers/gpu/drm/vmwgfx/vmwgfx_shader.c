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

#define VMW_COMPAT_SHADER_HT_ORDER 12

struct vmw_shader {
	struct vmw_resource res;
	SVGA3dShaderType type;
	uint32_t size;
};

struct vmw_user_shader {
	struct ttm_base_object base;
	struct vmw_shader shader;
};

/**
 * enum vmw_compat_shader_state - Staging state for compat shaders
 */
enum vmw_compat_shader_state {
	VMW_COMPAT_COMMITED,
	VMW_COMPAT_ADD,
	VMW_COMPAT_DEL
};

/**
 * struct vmw_compat_shader - Metadata for compat shaders.
 *
 * @handle: The TTM handle of the guest backed shader.
 * @tfile: The struct ttm_object_file the guest backed shader is registered
 * with.
 * @hash: Hash item for lookup.
 * @head: List head for staging lists or the compat shader manager list.
 * @state: Staging state.
 *
 * The structure is protected by the cmdbuf lock.
 */
struct vmw_compat_shader {
	u32 handle;
	struct ttm_object_file *tfile;
	struct drm_hash_item hash;
	struct list_head head;
	enum vmw_compat_shader_state state;
};

/**
 * struct vmw_compat_shader_manager - Compat shader manager.
 *
 * @shaders: Hash table containing staged and commited compat shaders
 * @list: List of commited shaders.
 * @dev_priv: Pointer to a device private structure.
 *
 * @shaders and @list are protected by the cmdbuf mutex for now.
 */
struct vmw_compat_shader_manager {
	struct drm_open_hash shaders;
	struct list_head list;
	struct vmw_private *dev_priv;
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
	vmw_context_binding_res_list_scrub(&res->binding_head);

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

static int vmw_shader_alloc(struct vmw_private *dev_priv,
			    struct vmw_dma_buffer *buffer,
			    size_t shader_size,
			    size_t offset,
			    SVGA3dShaderType shader_type,
			    struct ttm_object_file *tfile,
			    u32 *handle)
{
	struct vmw_user_shader *ushader;
	struct vmw_resource *res, *tmp;
	int ret;

	/*
	 * Approximate idr memory usage with 128 bytes. It will be limited
	 * by maximum number_of shaders anyway.
	 */
	if (unlikely(vmw_user_shader_size == 0))
		vmw_user_shader_size =
			ttm_round_pot(sizeof(struct vmw_user_shader)) + 128;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   vmw_user_shader_size,
				   false, true);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for shader "
				  "creation.\n");
		goto out;
	}

	ushader = kzalloc(sizeof(*ushader), GFP_KERNEL);
	if (unlikely(ushader == NULL)) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    vmw_user_shader_size);
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
				 offset, shader_type, buffer,
				 vmw_user_shader_free);
	if (unlikely(ret != 0))
		goto out;

	tmp = vmw_resource_reference(res);
	ret = ttm_base_object_init(tfile, &ushader->base, false,
				   VMW_RES_SHADER,
				   &vmw_user_shader_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	if (handle)
		*handle = ushader->base.hash.key;
out_err:
	vmw_resource_unreference(&res);
out:
	return ret;
}


int vmw_shader_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_shader_create_arg *arg =
		(struct drm_vmw_shader_create_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
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

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (unlikely(ret != 0))
		goto out_bad_arg;

	ret = vmw_shader_alloc(dev_priv, buffer, arg->size, arg->offset,
			       shader_type, tfile, &arg->shader_handle);

	ttm_read_unlock(&dev_priv->reservation_sem);
out_bad_arg:
	vmw_dmabuf_unreference(&buffer);
	return ret;
}

/**
 * vmw_compat_shader_lookup - Look up a compat shader
 *
 * @man: Pointer to the compat shader manager.
 * @shader_type: The shader type, that combined with the user_key identifies
 * the shader.
 * @user_key: On entry, this should be a pointer to the user_key.
 * On successful exit, it will contain the guest-backed shader's TTM handle.
 *
 * Returns 0 on success. Non-zero on failure, in which case the value pointed
 * to by @user_key is unmodified.
 */
int vmw_compat_shader_lookup(struct vmw_compat_shader_manager *man,
			     SVGA3dShaderType shader_type,
			     u32 *user_key)
{
	struct drm_hash_item *hash;
	int ret;
	unsigned long key = *user_key | (shader_type << 24);

	ret = drm_ht_find_item(&man->shaders, key, &hash);
	if (unlikely(ret != 0))
		return ret;

	*user_key = drm_hash_entry(hash, struct vmw_compat_shader,
				   hash)->handle;

	return 0;
}

/**
 * vmw_compat_shader_free - Free a compat shader.
 *
 * @man: Pointer to the compat shader manager.
 * @entry: Pointer to a struct vmw_compat_shader.
 *
 * Frees a struct vmw_compat_shder entry and drops its reference to the
 * guest backed shader.
 */
static void vmw_compat_shader_free(struct vmw_compat_shader_manager *man,
				   struct vmw_compat_shader *entry)
{
	list_del(&entry->head);
	WARN_ON(drm_ht_remove_item(&man->shaders, &entry->hash));
	WARN_ON(ttm_ref_object_base_unref(entry->tfile, entry->handle,
					  TTM_REF_USAGE));
	kfree(entry);
}

/**
 * vmw_compat_shaders_commit - Commit a list of compat shader actions.
 *
 * @man: Pointer to the compat shader manager.
 * @list: Caller's list of compat shader actions.
 *
 * This function commits a list of compat shader additions or removals.
 * It is typically called when the execbuf ioctl call triggering these
 * actions has commited the fifo contents to the device.
 */
void vmw_compat_shaders_commit(struct vmw_compat_shader_manager *man,
			       struct list_head *list)
{
	struct vmw_compat_shader *entry, *next;

	list_for_each_entry_safe(entry, next, list, head) {
		list_del(&entry->head);
		switch (entry->state) {
		case VMW_COMPAT_ADD:
			entry->state = VMW_COMPAT_COMMITED;
			list_add_tail(&entry->head, &man->list);
			break;
		case VMW_COMPAT_DEL:
			ttm_ref_object_base_unref(entry->tfile, entry->handle,
						  TTM_REF_USAGE);
			kfree(entry);
			break;
		default:
			BUG();
			break;
		}
	}
}

/**
 * vmw_compat_shaders_revert - Revert a list of compat shader actions
 *
 * @man: Pointer to the compat shader manager.
 * @list: Caller's list of compat shader actions.
 *
 * This function reverts a list of compat shader additions or removals.
 * It is typically called when the execbuf ioctl call triggering these
 * actions failed for some reason, and the command stream was never
 * submitted.
 */
void vmw_compat_shaders_revert(struct vmw_compat_shader_manager *man,
			       struct list_head *list)
{
	struct vmw_compat_shader *entry, *next;
	int ret;

	list_for_each_entry_safe(entry, next, list, head) {
		switch (entry->state) {
		case VMW_COMPAT_ADD:
			vmw_compat_shader_free(man, entry);
			break;
		case VMW_COMPAT_DEL:
			ret = drm_ht_insert_item(&man->shaders, &entry->hash);
			list_del(&entry->head);
			list_add_tail(&entry->head, &man->list);
			entry->state = VMW_COMPAT_COMMITED;
			break;
		default:
			BUG();
			break;
		}
	}
}

/**
 * vmw_compat_shader_remove - Stage a compat shader for removal.
 *
 * @man: Pointer to the compat shader manager
 * @user_key: The key that is used to identify the shader. The key is
 * unique to the shader type.
 * @shader_type: Shader type.
 * @list: Caller's list of staged shader actions.
 *
 * This function stages a compat shader for removal and removes the key from
 * the shader manager's hash table. If the shader was previously only staged
 * for addition it is completely removed (But the execbuf code may keep a
 * reference if it was bound to a context between addition and removal). If
 * it was previously commited to the manager, it is staged for removal.
 */
int vmw_compat_shader_remove(struct vmw_compat_shader_manager *man,
			     u32 user_key, SVGA3dShaderType shader_type,
			     struct list_head *list)
{
	struct vmw_compat_shader *entry;
	struct drm_hash_item *hash;
	int ret;

	ret = drm_ht_find_item(&man->shaders, user_key | (shader_type << 24),
			       &hash);
	if (likely(ret != 0))
		return -EINVAL;

	entry = drm_hash_entry(hash, struct vmw_compat_shader, hash);

	switch (entry->state) {
	case VMW_COMPAT_ADD:
		vmw_compat_shader_free(man, entry);
		break;
	case VMW_COMPAT_COMMITED:
		(void) drm_ht_remove_item(&man->shaders, &entry->hash);
		list_del(&entry->head);
		entry->state = VMW_COMPAT_DEL;
		list_add_tail(&entry->head, list);
		break;
	default:
		BUG();
		break;
	}

	return 0;
}

/**
 * vmw_compat_shader_add - Create a compat shader and add the
 * key to the manager
 *
 * @man: Pointer to the compat shader manager
 * @user_key: The key that is used to identify the shader. The key is
 * unique to the shader type.
 * @bytecode: Pointer to the bytecode of the shader.
 * @shader_type: Shader type.
 * @tfile: Pointer to a struct ttm_object_file that the guest-backed shader is
 * to be created with.
 * @list: Caller's list of staged shader actions.
 *
 * Note that only the key is added to the shader manager's hash table.
 * The shader is not yet added to the shader manager's list of shaders.
 */
int vmw_compat_shader_add(struct vmw_compat_shader_manager *man,
			  u32 user_key, const void *bytecode,
			  SVGA3dShaderType shader_type,
			  size_t size,
			  struct ttm_object_file *tfile,
			  struct list_head *list)
{
	struct vmw_dma_buffer *buf;
	struct ttm_bo_kmap_obj map;
	bool is_iomem;
	struct vmw_compat_shader *compat;
	u32 handle;
	int ret;

	if (user_key > ((1 << 24) - 1) || (unsigned) shader_type > 16)
		return -EINVAL;

	/* Allocate and pin a DMA buffer */
	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (unlikely(buf == NULL))
		return -ENOMEM;

	ret = vmw_dmabuf_init(man->dev_priv, buf, size, &vmw_sys_ne_placement,
			      true, vmw_dmabuf_bo_free);
	if (unlikely(ret != 0))
		goto out;

	ret = ttm_bo_reserve(&buf->base, false, true, false, NULL);
	if (unlikely(ret != 0))
		goto no_reserve;

	/* Map and copy shader bytecode. */
	ret = ttm_bo_kmap(&buf->base, 0, PAGE_ALIGN(size) >> PAGE_SHIFT,
			  &map);
	if (unlikely(ret != 0)) {
		ttm_bo_unreserve(&buf->base);
		goto no_reserve;
	}

	memcpy(ttm_kmap_obj_virtual(&map, &is_iomem), bytecode, size);
	WARN_ON(is_iomem);

	ttm_bo_kunmap(&map);
	ret = ttm_bo_validate(&buf->base, &vmw_sys_placement, false, true);
	WARN_ON(ret != 0);
	ttm_bo_unreserve(&buf->base);

	/* Create a guest-backed shader container backed by the dma buffer */
	ret = vmw_shader_alloc(man->dev_priv, buf, size, 0, shader_type,
			       tfile, &handle);
	vmw_dmabuf_unreference(&buf);
	if (unlikely(ret != 0))
		goto no_reserve;
	/*
	 * Create a compat shader structure and stage it for insertion
	 * in the manager
	 */
	compat = kzalloc(sizeof(*compat), GFP_KERNEL);
	if (compat == NULL)
		goto no_compat;

	compat->hash.key = user_key |  (shader_type << 24);
	ret = drm_ht_insert_item(&man->shaders, &compat->hash);
	if (unlikely(ret != 0))
		goto out_invalid_key;

	compat->state = VMW_COMPAT_ADD;
	compat->handle = handle;
	compat->tfile = tfile;
	list_add_tail(&compat->head, list);

	return 0;

out_invalid_key:
	kfree(compat);
no_compat:
	ttm_ref_object_base_unref(tfile, handle, TTM_REF_USAGE);
no_reserve:
out:
	return ret;
}

/**
 * vmw_compat_shader_man_create - Create a compat shader manager
 *
 * @dev_priv: Pointer to a device private structure.
 *
 * Typically done at file open time. If successful returns a pointer to a
 * compat shader manager. Otherwise returns an error pointer.
 */
struct vmw_compat_shader_manager *
vmw_compat_shader_man_create(struct vmw_private *dev_priv)
{
	struct vmw_compat_shader_manager *man;
	int ret;

	man = kzalloc(sizeof(*man), GFP_KERNEL);
	if (man == NULL)
		return ERR_PTR(-ENOMEM);

	man->dev_priv = dev_priv;
	INIT_LIST_HEAD(&man->list);
	ret = drm_ht_create(&man->shaders, VMW_COMPAT_SHADER_HT_ORDER);
	if (ret == 0)
		return man;

	kfree(man);
	return ERR_PTR(ret);
}

/**
 * vmw_compat_shader_man_destroy - Destroy a compat shader manager
 *
 * @man: Pointer to the shader manager to destroy.
 *
 * Typically done at file close time.
 */
void vmw_compat_shader_man_destroy(struct vmw_compat_shader_manager *man)
{
	struct vmw_compat_shader *entry, *next;

	mutex_lock(&man->dev_priv->cmdbuf_mutex);
	list_for_each_entry_safe(entry, next, &man->list, head)
		vmw_compat_shader_free(man, entry);

	mutex_unlock(&man->dev_priv->cmdbuf_mutex);
	kfree(man);
}
