/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
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
#include "vmwgfx_reg.h"
#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_placement.h"

static int vmw_cmd_invalid(struct vmw_private *dev_priv,
			   struct vmw_sw_context *sw_context,
			   SVGA3dCmdHeader *header)
{
	return capable(CAP_SYS_ADMIN) ? : -EINVAL;
}

static int vmw_cmd_ok(struct vmw_private *dev_priv,
		      struct vmw_sw_context *sw_context,
		      SVGA3dCmdHeader *header)
{
	return 0;
}

static int vmw_cmd_cid_check(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     SVGA3dCmdHeader *header)
{
	struct vmw_cid_cmd {
		SVGA3dCmdHeader header;
		__le32 cid;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_cid_cmd, header);
	if (likely(sw_context->cid_valid && cmd->cid == sw_context->last_cid))
		return 0;

	ret = vmw_context_check(dev_priv, sw_context->tfile, cmd->cid);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Could not find or use context %u\n",
			  (unsigned) cmd->cid);
		return ret;
	}

	sw_context->last_cid = cmd->cid;
	sw_context->cid_valid = true;

	return 0;
}

static int vmw_cmd_sid_check(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     uint32_t *sid)
{
	if (*sid == SVGA3D_INVALID_ID)
		return 0;

	if (unlikely((!sw_context->sid_valid  ||
		      *sid != sw_context->last_sid))) {
		int real_id;
		int ret = vmw_surface_check(dev_priv, sw_context->tfile,
					    *sid, &real_id);

		if (unlikely(ret != 0)) {
			DRM_ERROR("Could ot find or use surface 0x%08x "
				  "address 0x%08lx\n",
				  (unsigned int) *sid,
				  (unsigned long) sid);
			return ret;
		}

		sw_context->last_sid = *sid;
		sw_context->sid_valid = true;
		*sid = real_id;
		sw_context->sid_translation = real_id;
	} else
		*sid = sw_context->sid_translation;

	return 0;
}


static int vmw_cmd_set_render_target_check(struct vmw_private *dev_priv,
					   struct vmw_sw_context *sw_context,
					   SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetRenderTarget body;
	} *cmd;
	int ret;

	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	cmd = container_of(header, struct vmw_sid_cmd, header);
	ret = vmw_cmd_sid_check(dev_priv, sw_context, &cmd->body.target.sid);
	return ret;
}

static int vmw_cmd_surface_copy_check(struct vmw_private *dev_priv,
				      struct vmw_sw_context *sw_context,
				      SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceCopy body;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_sid_cmd, header);
	ret = vmw_cmd_sid_check(dev_priv, sw_context, &cmd->body.src.sid);
	if (unlikely(ret != 0))
		return ret;
	return vmw_cmd_sid_check(dev_priv, sw_context, &cmd->body.dest.sid);
}

static int vmw_cmd_stretch_blt_check(struct vmw_private *dev_priv,
				     struct vmw_sw_context *sw_context,
				     SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceStretchBlt body;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_sid_cmd, header);
	ret = vmw_cmd_sid_check(dev_priv, sw_context, &cmd->body.src.sid);
	if (unlikely(ret != 0))
		return ret;
	return vmw_cmd_sid_check(dev_priv, sw_context, &cmd->body.dest.sid);
}

static int vmw_cmd_blt_surf_screen_check(struct vmw_private *dev_priv,
					 struct vmw_sw_context *sw_context,
					 SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdBlitSurfaceToScreen body;
	} *cmd;

	cmd = container_of(header, struct vmw_sid_cmd, header);
	return vmw_cmd_sid_check(dev_priv, sw_context, &cmd->body.srcImage.sid);
}

static int vmw_cmd_present_check(struct vmw_private *dev_priv,
				 struct vmw_sw_context *sw_context,
				 SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdPresent body;
	} *cmd;

	cmd = container_of(header, struct vmw_sid_cmd, header);
	return vmw_cmd_sid_check(dev_priv, sw_context, &cmd->body.sid);
}

static int vmw_translate_guest_ptr(struct vmw_private *dev_priv,
				   struct vmw_sw_context *sw_context,
				   SVGAGuestPtr *ptr,
				   struct vmw_dma_buffer **vmw_bo_p)
{
	struct vmw_dma_buffer *vmw_bo = NULL;
	struct ttm_buffer_object *bo;
	uint32_t handle = ptr->gmrId;
	struct vmw_relocation *reloc;
	uint32_t cur_validate_node;
	struct ttm_validate_buffer *val_buf;
	int ret;

	ret = vmw_user_dmabuf_lookup(sw_context->tfile, handle, &vmw_bo);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Could not find or use GMR region.\n");
		return -EINVAL;
	}
	bo = &vmw_bo->base;

	if (unlikely(sw_context->cur_reloc >= VMWGFX_MAX_RELOCATIONS)) {
		DRM_ERROR("Max number relocations per submission"
			  " exceeded\n");
		ret = -EINVAL;
		goto out_no_reloc;
	}

	reloc = &sw_context->relocs[sw_context->cur_reloc++];
	reloc->location = ptr;

	cur_validate_node = vmw_dmabuf_validate_node(bo, sw_context->cur_val_buf);
	if (unlikely(cur_validate_node >= VMWGFX_MAX_GMRS)) {
		DRM_ERROR("Max number of DMA buffers per submission"
			  " exceeded.\n");
		ret = -EINVAL;
		goto out_no_reloc;
	}

	reloc->index = cur_validate_node;
	if (unlikely(cur_validate_node == sw_context->cur_val_buf)) {
		val_buf = &sw_context->val_bufs[cur_validate_node];
		val_buf->bo = ttm_bo_reference(bo);
		val_buf->new_sync_obj_arg = (void *) dev_priv;
		list_add_tail(&val_buf->head, &sw_context->validate_nodes);
		++sw_context->cur_val_buf;
	}
	*vmw_bo_p = vmw_bo;
	return 0;

out_no_reloc:
	vmw_dmabuf_unreference(&vmw_bo);
	vmw_bo_p = NULL;
	return ret;
}

static int vmw_cmd_end_query(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo;
	struct vmw_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdEndQuery q;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_query_cmd, header);
	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_translate_guest_ptr(dev_priv, sw_context,
				      &cmd->q.guestResult,
				      &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	vmw_dmabuf_unreference(&vmw_bo);
	return 0;
}

static int vmw_cmd_wait_query(struct vmw_private *dev_priv,
			      struct vmw_sw_context *sw_context,
			      SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo;
	struct vmw_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdWaitForQuery q;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_query_cmd, header);
	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_translate_guest_ptr(dev_priv, sw_context,
				      &cmd->q.guestResult,
				      &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	vmw_dmabuf_unreference(&vmw_bo);
	return 0;
}


static int vmw_cmd_dma(struct vmw_private *dev_priv,
		       struct vmw_sw_context *sw_context,
		       SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo = NULL;
	struct ttm_buffer_object *bo;
	struct vmw_surface *srf = NULL;
	struct vmw_dma_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceDMA dma;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_dma_cmd, header);
	ret = vmw_translate_guest_ptr(dev_priv, sw_context,
				      &cmd->dma.guest.ptr,
				      &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	bo = &vmw_bo->base;
	ret = vmw_user_surface_lookup_handle(dev_priv, sw_context->tfile,
					     cmd->dma.host.sid, &srf);
	if (ret) {
		DRM_ERROR("could not find surface\n");
		goto out_no_reloc;
	}

	/**
	 * Patch command stream with device SID.
	 */

	cmd->dma.host.sid = srf->res.id;
	vmw_kms_cursor_snoop(srf, sw_context->tfile, bo, header);
	/**
	 * FIXME: May deadlock here when called from the
	 * command parsing code.
	 */
	vmw_surface_unreference(&srf);

out_no_reloc:
	vmw_dmabuf_unreference(&vmw_bo);
	return ret;
}

static int vmw_cmd_draw(struct vmw_private *dev_priv,
			struct vmw_sw_context *sw_context,
			SVGA3dCmdHeader *header)
{
	struct vmw_draw_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdDrawPrimitives body;
	} *cmd;
	SVGA3dVertexDecl *decl = (SVGA3dVertexDecl *)(
		(unsigned long)header + sizeof(*cmd));
	SVGA3dPrimitiveRange *range;
	uint32_t i;
	uint32_t maxnum;
	int ret;

	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	cmd = container_of(header, struct vmw_draw_cmd, header);
	maxnum = (header->size - sizeof(cmd->body)) / sizeof(*decl);

	if (unlikely(cmd->body.numVertexDecls > maxnum)) {
		DRM_ERROR("Illegal number of vertex declarations.\n");
		return -EINVAL;
	}

	for (i = 0; i < cmd->body.numVertexDecls; ++i, ++decl) {
		ret = vmw_cmd_sid_check(dev_priv, sw_context,
					&decl->array.surfaceId);
		if (unlikely(ret != 0))
			return ret;
	}

	maxnum = (header->size - sizeof(cmd->body) -
		  cmd->body.numVertexDecls * sizeof(*decl)) / sizeof(*range);
	if (unlikely(cmd->body.numRanges > maxnum)) {
		DRM_ERROR("Illegal number of index ranges.\n");
		return -EINVAL;
	}

	range = (SVGA3dPrimitiveRange *) decl;
	for (i = 0; i < cmd->body.numRanges; ++i, ++range) {
		ret = vmw_cmd_sid_check(dev_priv, sw_context,
					&range->indexArray.surfaceId);
		if (unlikely(ret != 0))
			return ret;
	}
	return 0;
}


static int vmw_cmd_tex_state(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     SVGA3dCmdHeader *header)
{
	struct vmw_tex_state_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetTextureState state;
	};

	SVGA3dTextureState *last_state = (SVGA3dTextureState *)
	  ((unsigned long) header + header->size + sizeof(header));
	SVGA3dTextureState *cur_state = (SVGA3dTextureState *)
		((unsigned long) header + sizeof(struct vmw_tex_state_cmd));
	int ret;

	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	for (; cur_state < last_state; ++cur_state) {
		if (likely(cur_state->name != SVGA3D_TS_BIND_TEXTURE))
			continue;

		ret = vmw_cmd_sid_check(dev_priv, sw_context,
					&cur_state->value);
		if (unlikely(ret != 0))
			return ret;
	}

	return 0;
}


typedef int (*vmw_cmd_func) (struct vmw_private *,
			     struct vmw_sw_context *,
			     SVGA3dCmdHeader *);

#define VMW_CMD_DEF(cmd, func) \
	[cmd - SVGA_3D_CMD_BASE] = func

static vmw_cmd_func vmw_cmd_funcs[SVGA_3D_CMD_MAX] = {
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_DEFINE, &vmw_cmd_invalid),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_DESTROY, &vmw_cmd_invalid),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_COPY, &vmw_cmd_surface_copy_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_STRETCHBLT, &vmw_cmd_stretch_blt_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_DMA, &vmw_cmd_dma),
	VMW_CMD_DEF(SVGA_3D_CMD_CONTEXT_DEFINE, &vmw_cmd_invalid),
	VMW_CMD_DEF(SVGA_3D_CMD_CONTEXT_DESTROY, &vmw_cmd_invalid),
	VMW_CMD_DEF(SVGA_3D_CMD_SETTRANSFORM, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETZRANGE, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETRENDERSTATE, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETRENDERTARGET,
		    &vmw_cmd_set_render_target_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETTEXTURESTATE, &vmw_cmd_tex_state),
	VMW_CMD_DEF(SVGA_3D_CMD_SETMATERIAL, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETLIGHTDATA, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETLIGHTENABLED, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETVIEWPORT, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SETCLIPPLANE, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_CLEAR, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_PRESENT, &vmw_cmd_present_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SHADER_DEFINE, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SHADER_DESTROY, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_SHADER, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_SHADER_CONST, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_DRAW_PRIMITIVES, &vmw_cmd_draw),
	VMW_CMD_DEF(SVGA_3D_CMD_SETSCISSORRECT, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_BEGIN_QUERY, &vmw_cmd_cid_check),
	VMW_CMD_DEF(SVGA_3D_CMD_END_QUERY, &vmw_cmd_end_query),
	VMW_CMD_DEF(SVGA_3D_CMD_WAIT_FOR_QUERY, &vmw_cmd_wait_query),
	VMW_CMD_DEF(SVGA_3D_CMD_PRESENT_READBACK, &vmw_cmd_ok),
	VMW_CMD_DEF(SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN,
		    &vmw_cmd_blt_surf_screen_check)
};

static int vmw_cmd_check(struct vmw_private *dev_priv,
			 struct vmw_sw_context *sw_context,
			 void *buf, uint32_t *size)
{
	uint32_t cmd_id;
	uint32_t size_remaining = *size;
	SVGA3dCmdHeader *header = (SVGA3dCmdHeader *) buf;
	int ret;

	cmd_id = ((uint32_t *)buf)[0];
	if (cmd_id == SVGA_CMD_UPDATE) {
		*size = 5 << 2;
		return 0;
	}

	cmd_id = le32_to_cpu(header->id);
	*size = le32_to_cpu(header->size) + sizeof(SVGA3dCmdHeader);

	cmd_id -= SVGA_3D_CMD_BASE;
	if (unlikely(*size > size_remaining))
		goto out_err;

	if (unlikely(cmd_id >= SVGA_3D_CMD_MAX - SVGA_3D_CMD_BASE))
		goto out_err;

	ret = vmw_cmd_funcs[cmd_id](dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		goto out_err;

	return 0;
out_err:
	DRM_ERROR("Illegal / Invalid SVGA3D command: %d\n",
		  cmd_id + SVGA_3D_CMD_BASE);
	return -EINVAL;
}

static int vmw_cmd_check_all(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     void *buf, uint32_t size)
{
	int32_t cur_size = size;
	int ret;

	while (cur_size > 0) {
		size = cur_size;
		ret = vmw_cmd_check(dev_priv, sw_context, buf, &size);
		if (unlikely(ret != 0))
			return ret;
		buf = (void *)((unsigned long) buf + size);
		cur_size -= size;
	}

	if (unlikely(cur_size != 0)) {
		DRM_ERROR("Command verifier out of sync.\n");
		return -EINVAL;
	}

	return 0;
}

static void vmw_free_relocations(struct vmw_sw_context *sw_context)
{
	sw_context->cur_reloc = 0;
}

static void vmw_apply_relocations(struct vmw_sw_context *sw_context)
{
	uint32_t i;
	struct vmw_relocation *reloc;
	struct ttm_validate_buffer *validate;
	struct ttm_buffer_object *bo;

	for (i = 0; i < sw_context->cur_reloc; ++i) {
		reloc = &sw_context->relocs[i];
		validate = &sw_context->val_bufs[reloc->index];
		bo = validate->bo;
		if (bo->mem.mem_type == TTM_PL_VRAM) {
			reloc->location->offset += bo->offset;
			reloc->location->gmrId = SVGA_GMR_FRAMEBUFFER;
		} else
			reloc->location->gmrId = bo->mem.start;
	}
	vmw_free_relocations(sw_context);
}

static void vmw_clear_validations(struct vmw_sw_context *sw_context)
{
	struct ttm_validate_buffer *entry, *next;

	list_for_each_entry_safe(entry, next, &sw_context->validate_nodes,
				 head) {
		list_del(&entry->head);
		vmw_dmabuf_validate_clear(entry->bo);
		ttm_bo_unref(&entry->bo);
		sw_context->cur_val_buf--;
	}
	BUG_ON(sw_context->cur_val_buf != 0);
}

static int vmw_validate_single_buffer(struct vmw_private *dev_priv,
				      struct ttm_buffer_object *bo)
{
	int ret;

	/**
	 * Put BO in VRAM if there is space, otherwise as a GMR.
	 * If there is no space in VRAM and GMR ids are all used up,
	 * start evicting GMRs to make room. If the DMA buffer can't be
	 * used as a GMR, this will return -ENOMEM.
	 */

	ret = ttm_bo_validate(bo, &vmw_vram_gmr_placement, true, false, false);
	if (likely(ret == 0 || ret == -ERESTARTSYS))
		return ret;

	/**
	 * If that failed, try VRAM again, this time evicting
	 * previous contents.
	 */

	DRM_INFO("Falling through to VRAM.\n");
	ret = ttm_bo_validate(bo, &vmw_vram_placement, true, false, false);
	return ret;
}


static int vmw_validate_buffers(struct vmw_private *dev_priv,
				struct vmw_sw_context *sw_context)
{
	struct ttm_validate_buffer *entry;
	int ret;

	list_for_each_entry(entry, &sw_context->validate_nodes, head) {
		ret = vmw_validate_single_buffer(dev_priv, entry->bo);
		if (unlikely(ret != 0))
			return ret;
	}
	return 0;
}

int vmw_execbuf_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_execbuf_arg *arg = (struct drm_vmw_execbuf_arg *)data;
	struct drm_vmw_fence_rep fence_rep;
	struct drm_vmw_fence_rep __user *user_fence_rep;
	int ret;
	void *user_cmd;
	void *cmd;
	uint32_t sequence;
	struct vmw_sw_context *sw_context = &dev_priv->ctx;
	struct vmw_master *vmaster = vmw_master(file_priv->master);

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		return ret;

	ret = mutex_lock_interruptible(&dev_priv->cmdbuf_mutex);
	if (unlikely(ret != 0)) {
		ret = -ERESTARTSYS;
		goto out_no_cmd_mutex;
	}

	cmd = vmw_fifo_reserve(dev_priv, arg->command_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving fifo space for commands.\n");
		ret = -ENOMEM;
		goto out_unlock;
	}

	user_cmd = (void __user *)(unsigned long)arg->commands;
	ret = copy_from_user(cmd, user_cmd, arg->command_size);

	if (unlikely(ret != 0)) {
		ret = -EFAULT;
		DRM_ERROR("Failed copying commands.\n");
		goto out_commit;
	}

	sw_context->tfile = vmw_fpriv(file_priv)->tfile;
	sw_context->cid_valid = false;
	sw_context->sid_valid = false;
	sw_context->cur_reloc = 0;
	sw_context->cur_val_buf = 0;

	INIT_LIST_HEAD(&sw_context->validate_nodes);

	ret = vmw_cmd_check_all(dev_priv, sw_context, cmd, arg->command_size);
	if (unlikely(ret != 0))
		goto out_err;
	ret = ttm_eu_reserve_buffers(&sw_context->validate_nodes,
				     dev_priv->val_seq++);
	if (unlikely(ret != 0))
		goto out_err;

	ret = vmw_validate_buffers(dev_priv, sw_context);
	if (unlikely(ret != 0))
		goto out_err;

	vmw_apply_relocations(sw_context);

	if (arg->throttle_us) {
		ret = vmw_wait_lag(dev_priv, &dev_priv->fifo.fence_queue,
				   arg->throttle_us);

		if (unlikely(ret != 0))
			goto out_err;
	}

	vmw_fifo_commit(dev_priv, arg->command_size);

	ret = vmw_fifo_send_fence(dev_priv, &sequence);

	ttm_eu_fence_buffer_objects(&sw_context->validate_nodes,
				    (void *)(unsigned long) sequence);
	vmw_clear_validations(sw_context);
	mutex_unlock(&dev_priv->cmdbuf_mutex);

	/*
	 * This error is harmless, because if fence submission fails,
	 * vmw_fifo_send_fence will sync.
	 */

	if (ret != 0)
		DRM_ERROR("Fence submission error. Syncing.\n");

	fence_rep.error = ret;
	fence_rep.fence_seq = (uint64_t) sequence;
	fence_rep.pad64 = 0;

	user_fence_rep = (struct drm_vmw_fence_rep __user *)
	    (unsigned long)arg->fence_rep;

	/*
	 * copy_to_user errors will be detected by user space not
	 * seeing fence_rep::error filled in.
	 */

	ret = copy_to_user(user_fence_rep, &fence_rep, sizeof(fence_rep));

	vmw_kms_cursor_post_execbuf(dev_priv);
	ttm_read_unlock(&vmaster->lock);
	return 0;
out_err:
	vmw_free_relocations(sw_context);
	ttm_eu_backoff_reservation(&sw_context->validate_nodes);
	vmw_clear_validations(sw_context);
out_commit:
	vmw_fifo_commit(dev_priv, 0);
out_unlock:
	mutex_unlock(&dev_priv->cmdbuf_mutex);
out_no_cmd_mutex:
	ttm_read_unlock(&vmaster->lock);
	return ret;
}
