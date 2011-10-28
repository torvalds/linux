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


#include "drmP.h"
#include "vmwgfx_drv.h"

#include "ttm/ttm_placement.h"

#include "svga_overlay.h"
#include "svga_escape.h"

#define VMW_MAX_NUM_STREAMS 1

struct vmw_stream {
	struct vmw_dma_buffer *buf;
	bool claimed;
	bool paused;
	struct drm_vmw_control_stream_arg saved;
};

/**
 * Overlay control
 */
struct vmw_overlay {
	/*
	 * Each stream is a single overlay. In Xv these are called ports.
	 */
	struct mutex mutex;
	struct vmw_stream stream[VMW_MAX_NUM_STREAMS];
};

static inline struct vmw_overlay *vmw_overlay(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	return dev_priv ? dev_priv->overlay_priv : NULL;
}

struct vmw_escape_header {
	uint32_t cmd;
	SVGAFifoCmdEscape body;
};

struct vmw_escape_video_flush {
	struct vmw_escape_header escape;
	SVGAEscapeVideoFlush flush;
};

static inline void fill_escape(struct vmw_escape_header *header,
			       uint32_t size)
{
	header->cmd = SVGA_CMD_ESCAPE;
	header->body.nsid = SVGA_ESCAPE_NSID_VMWARE;
	header->body.size = size;
}

static inline void fill_flush(struct vmw_escape_video_flush *cmd,
			      uint32_t stream_id)
{
	fill_escape(&cmd->escape, sizeof(cmd->flush));
	cmd->flush.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_FLUSH;
	cmd->flush.streamId = stream_id;
}

/**
 * Pin or unpin a buffer in vram.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to pin or unpin.
 * @pin:  Pin buffer in vram if true.
 * @interruptible:  Use interruptible wait.
 *
 * Takes the current masters ttm lock in read.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
static int vmw_dmabuf_pin_in_vram(struct vmw_private *dev_priv,
				  struct vmw_dma_buffer *buf,
				  bool pin, bool interruptible)
{
	struct ttm_buffer_object *bo = &buf->base;
	struct ttm_placement *overlay_placement = &vmw_vram_placement;
	int ret;

	ret = ttm_read_lock(&dev_priv->active_master->lock, interruptible);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_reserve(bo, interruptible, false, false, 0);
	if (unlikely(ret != 0))
		goto err;

	if (pin)
		overlay_placement = &vmw_vram_ne_placement;

	ret = ttm_bo_validate(bo, overlay_placement, interruptible, false, false);

	ttm_bo_unreserve(bo);

err:
	ttm_read_unlock(&dev_priv->active_master->lock);

	return ret;
}

/**
 * Send put command to hw.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
static int vmw_overlay_send_put(struct vmw_private *dev_priv,
				struct vmw_dma_buffer *buf,
				struct drm_vmw_control_stream_arg *arg,
				bool interruptible)
{
	struct {
		struct vmw_escape_header escape;
		struct {
			struct {
				uint32_t cmdType;
				uint32_t streamId;
			} header;
			struct {
				uint32_t registerId;
				uint32_t value;
			} items[SVGA_VIDEO_PITCH_3 + 1];
		} body;
		struct vmw_escape_video_flush flush;
	} *cmds;
	uint32_t offset;
	int i, ret;

	for (;;) {
		cmds = vmw_fifo_reserve(dev_priv, sizeof(*cmds));
		if (cmds)
			break;

		ret = vmw_fallback_wait(dev_priv, false, true, 0,
					interruptible, 3*HZ);
		if (interruptible && ret == -ERESTARTSYS)
			return ret;
		else
			BUG_ON(ret != 0);
	}

	fill_escape(&cmds->escape, sizeof(cmds->body));
	cmds->body.header.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS;
	cmds->body.header.streamId = arg->stream_id;

	for (i = 0; i <= SVGA_VIDEO_PITCH_3; i++)
		cmds->body.items[i].registerId = i;

	offset = buf->base.offset + arg->offset;

	cmds->body.items[SVGA_VIDEO_ENABLED].value     = true;
	cmds->body.items[SVGA_VIDEO_FLAGS].value       = arg->flags;
	cmds->body.items[SVGA_VIDEO_DATA_OFFSET].value = offset;
	cmds->body.items[SVGA_VIDEO_FORMAT].value      = arg->format;
	cmds->body.items[SVGA_VIDEO_COLORKEY].value    = arg->color_key;
	cmds->body.items[SVGA_VIDEO_SIZE].value        = arg->size;
	cmds->body.items[SVGA_VIDEO_WIDTH].value       = arg->width;
	cmds->body.items[SVGA_VIDEO_HEIGHT].value      = arg->height;
	cmds->body.items[SVGA_VIDEO_SRC_X].value       = arg->src.x;
	cmds->body.items[SVGA_VIDEO_SRC_Y].value       = arg->src.y;
	cmds->body.items[SVGA_VIDEO_SRC_WIDTH].value   = arg->src.w;
	cmds->body.items[SVGA_VIDEO_SRC_HEIGHT].value  = arg->src.h;
	cmds->body.items[SVGA_VIDEO_DST_X].value       = arg->dst.x;
	cmds->body.items[SVGA_VIDEO_DST_Y].value       = arg->dst.y;
	cmds->body.items[SVGA_VIDEO_DST_WIDTH].value   = arg->dst.w;
	cmds->body.items[SVGA_VIDEO_DST_HEIGHT].value  = arg->dst.h;
	cmds->body.items[SVGA_VIDEO_PITCH_1].value     = arg->pitch[0];
	cmds->body.items[SVGA_VIDEO_PITCH_2].value     = arg->pitch[1];
	cmds->body.items[SVGA_VIDEO_PITCH_3].value     = arg->pitch[2];

	fill_flush(&cmds->flush, arg->stream_id);

	vmw_fifo_commit(dev_priv, sizeof(*cmds));

	return 0;
}

/**
 * Send stop command to hw.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
static int vmw_overlay_send_stop(struct vmw_private *dev_priv,
				 uint32_t stream_id,
				 bool interruptible)
{
	struct {
		struct vmw_escape_header escape;
		SVGAEscapeVideoSetRegs body;
		struct vmw_escape_video_flush flush;
	} *cmds;
	int ret;

	for (;;) {
		cmds = vmw_fifo_reserve(dev_priv, sizeof(*cmds));
		if (cmds)
			break;

		ret = vmw_fallback_wait(dev_priv, false, true, 0,
					interruptible, 3*HZ);
		if (interruptible && ret == -ERESTARTSYS)
			return ret;
		else
			BUG_ON(ret != 0);
	}

	fill_escape(&cmds->escape, sizeof(cmds->body));
	cmds->body.header.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS;
	cmds->body.header.streamId = stream_id;
	cmds->body.items[0].registerId = SVGA_VIDEO_ENABLED;
	cmds->body.items[0].value = false;
	fill_flush(&cmds->flush, stream_id);

	vmw_fifo_commit(dev_priv, sizeof(*cmds));

	return 0;
}

/**
 * Stop or pause a stream.
 *
 * If the stream is paused the no evict flag is removed from the buffer
 * but left in vram. This allows for instance mode_set to evict it
 * should it need to.
 *
 * The caller must hold the overlay lock.
 *
 * @stream_id which stream to stop/pause.
 * @pause true to pause, false to stop completely.
 */
static int vmw_overlay_stop(struct vmw_private *dev_priv,
			    uint32_t stream_id, bool pause,
			    bool interruptible)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	struct vmw_stream *stream = &overlay->stream[stream_id];
	int ret;

	/* no buffer attached the stream is completely stopped */
	if (!stream->buf)
		return 0;

	/* If the stream is paused this is already done */
	if (!stream->paused) {
		ret = vmw_overlay_send_stop(dev_priv, stream_id,
					    interruptible);
		if (ret)
			return ret;

		/* We just remove the NO_EVICT flag so no -ENOMEM */
		ret = vmw_dmabuf_pin_in_vram(dev_priv, stream->buf, false,
					     interruptible);
		if (interruptible && ret == -ERESTARTSYS)
			return ret;
		else
			BUG_ON(ret != 0);
	}

	if (!pause) {
		vmw_dmabuf_unreference(&stream->buf);
		stream->paused = false;
	} else {
		stream->paused = true;
	}

	return 0;
}

/**
 * Update a stream and send any put or stop fifo commands needed.
 *
 * The caller must hold the overlay lock.
 *
 * Returns
 * -ENOMEM if buffer doesn't fit in vram.
 * -ERESTARTSYS if interrupted.
 */
static int vmw_overlay_update_stream(struct vmw_private *dev_priv,
				     struct vmw_dma_buffer *buf,
				     struct drm_vmw_control_stream_arg *arg,
				     bool interruptible)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	struct vmw_stream *stream = &overlay->stream[arg->stream_id];
	int ret = 0;

	if (!buf)
		return -EINVAL;

	DRM_DEBUG("   %s: old %p, new %p, %spaused\n", __func__,
		  stream->buf, buf, stream->paused ? "" : "not ");

	if (stream->buf != buf) {
		ret = vmw_overlay_stop(dev_priv, arg->stream_id,
				       false, interruptible);
		if (ret)
			return ret;
	} else if (!stream->paused) {
		/* If the buffers match and not paused then just send
		 * the put command, no need to do anything else.
		 */
		ret = vmw_overlay_send_put(dev_priv, buf, arg, interruptible);
		if (ret == 0)
			stream->saved = *arg;
		else
			BUG_ON(!interruptible);

		return ret;
	}

	/* We don't start the old stream if we are interrupted.
	 * Might return -ENOMEM if it can't fit the buffer in vram.
	 */
	ret = vmw_dmabuf_pin_in_vram(dev_priv, buf, true, interruptible);
	if (ret)
		return ret;

	ret = vmw_overlay_send_put(dev_priv, buf, arg, interruptible);
	if (ret) {
		/* This one needs to happen no matter what. We only remove
		 * the NO_EVICT flag so this is safe from -ENOMEM.
		 */
		BUG_ON(vmw_dmabuf_pin_in_vram(dev_priv, buf, false, false) != 0);
		return ret;
	}

	if (stream->buf != buf)
		stream->buf = vmw_dmabuf_reference(buf);
	stream->saved = *arg;
	/* stream is no longer stopped/paused */
	stream->paused = false;

	return 0;
}

/**
 * Stop all streams.
 *
 * Used by the fb code when starting.
 *
 * Takes the overlay lock.
 */
int vmw_overlay_stop_all(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	int i, ret;

	if (!overlay)
		return 0;

	mutex_lock(&overlay->mutex);

	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {
		struct vmw_stream *stream = &overlay->stream[i];
		if (!stream->buf)
			continue;

		ret = vmw_overlay_stop(dev_priv, i, false, false);
		WARN_ON(ret != 0);
	}

	mutex_unlock(&overlay->mutex);

	return 0;
}

/**
 * Try to resume all paused streams.
 *
 * Used by the kms code after moving a new scanout buffer to vram.
 *
 * Takes the overlay lock.
 */
int vmw_overlay_resume_all(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	int i, ret;

	if (!overlay)
		return 0;

	mutex_lock(&overlay->mutex);

	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {
		struct vmw_stream *stream = &overlay->stream[i];
		if (!stream->paused)
			continue;

		ret = vmw_overlay_update_stream(dev_priv, stream->buf,
						&stream->saved, false);
		if (ret != 0)
			DRM_INFO("%s: *warning* failed to resume stream %i\n",
				 __func__, i);
	}

	mutex_unlock(&overlay->mutex);

	return 0;
}

/**
 * Pauses all active streams.
 *
 * Used by the kms code when moving a new scanout buffer to vram.
 *
 * Takes the overlay lock.
 */
int vmw_overlay_pause_all(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	int i, ret;

	if (!overlay)
		return 0;

	mutex_lock(&overlay->mutex);

	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {
		if (overlay->stream[i].paused)
			DRM_INFO("%s: *warning* stream %i already paused\n",
				 __func__, i);
		ret = vmw_overlay_stop(dev_priv, i, true, false);
		WARN_ON(ret != 0);
	}

	mutex_unlock(&overlay->mutex);

	return 0;
}

int vmw_overlay_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	struct drm_vmw_control_stream_arg *arg =
	    (struct drm_vmw_control_stream_arg *)data;
	struct vmw_dma_buffer *buf;
	struct vmw_resource *res;
	int ret;

	if (!overlay)
		return -ENOSYS;

	ret = vmw_user_stream_lookup(dev_priv, tfile, &arg->stream_id, &res);
	if (ret)
		return ret;

	mutex_lock(&overlay->mutex);

	if (!arg->enabled) {
		ret = vmw_overlay_stop(dev_priv, arg->stream_id, false, true);
		goto out_unlock;
	}

	ret = vmw_user_dmabuf_lookup(tfile, arg->handle, &buf);
	if (ret)
		goto out_unlock;

	ret = vmw_overlay_update_stream(dev_priv, buf, arg, true);

	vmw_dmabuf_unreference(&buf);

out_unlock:
	mutex_unlock(&overlay->mutex);
	vmw_resource_unreference(&res);

	return ret;
}

int vmw_overlay_num_overlays(struct vmw_private *dev_priv)
{
	if (!dev_priv->overlay_priv)
		return 0;

	return VMW_MAX_NUM_STREAMS;
}

int vmw_overlay_num_free_overlays(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	int i, k;

	if (!overlay)
		return 0;

	mutex_lock(&overlay->mutex);

	for (i = 0, k = 0; i < VMW_MAX_NUM_STREAMS; i++)
		if (!overlay->stream[i].claimed)
			k++;

	mutex_unlock(&overlay->mutex);

	return k;
}

int vmw_overlay_claim(struct vmw_private *dev_priv, uint32_t *out)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	int i;

	if (!overlay)
		return -ENOSYS;

	mutex_lock(&overlay->mutex);

	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {

		if (overlay->stream[i].claimed)
			continue;

		overlay->stream[i].claimed = true;
		*out = i;
		mutex_unlock(&overlay->mutex);
		return 0;
	}

	mutex_unlock(&overlay->mutex);
	return -ESRCH;
}

int vmw_overlay_unref(struct vmw_private *dev_priv, uint32_t stream_id)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;

	BUG_ON(stream_id >= VMW_MAX_NUM_STREAMS);

	if (!overlay)
		return -ENOSYS;

	mutex_lock(&overlay->mutex);

	WARN_ON(!overlay->stream[stream_id].claimed);
	vmw_overlay_stop(dev_priv, stream_id, false, false);
	overlay->stream[stream_id].claimed = false;

	mutex_unlock(&overlay->mutex);
	return 0;
}

int vmw_overlay_init(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay;
	int i;

	if (dev_priv->overlay_priv)
		return -EINVAL;

	if (!(dev_priv->fifo.capabilities & SVGA_FIFO_CAP_VIDEO) &&
	     (dev_priv->fifo.capabilities & SVGA_FIFO_CAP_ESCAPE)) {
		DRM_INFO("hardware doesn't support overlays\n");
		return -ENOSYS;
	}

	overlay = kmalloc(sizeof(*overlay), GFP_KERNEL);
	if (!overlay)
		return -ENOMEM;

	memset(overlay, 0, sizeof(*overlay));
	mutex_init(&overlay->mutex);
	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {
		overlay->stream[i].buf = NULL;
		overlay->stream[i].paused = false;
		overlay->stream[i].claimed = false;
	}

	dev_priv->overlay_priv = overlay;

	return 0;
}

int vmw_overlay_close(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	bool forgotten_buffer = false;
	int i;

	if (!overlay)
		return -ENOSYS;

	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {
		if (overlay->stream[i].buf) {
			forgotten_buffer = true;
			vmw_overlay_stop(dev_priv, i, false, false);
		}
	}

	WARN_ON(forgotten_buffer);

	dev_priv->overlay_priv = NULL;
	kfree(overlay);

	return 0;
}
