// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2020 VMware, Inc., Palo Alto, CA., USA
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

#include <linux/sched/signal.h>

#include <drm/ttm/ttm_placement.h>

#include "vmwgfx_drv.h"

struct vmw_temp_set_context {
	SVGA3dCmdHeader header;
	SVGA3dCmdDXTempSetContext body;
};

bool vmw_supports_3d(struct vmw_private *dev_priv)
{
	uint32_t fifo_min, hwversion;
	const struct vmw_fifo_state *fifo = &dev_priv->fifo;

	if (!(dev_priv->capabilities & SVGA_CAP_3D))
		return false;

	if (dev_priv->capabilities & SVGA_CAP_GBOBJECTS) {
		uint32_t result;

		if (!dev_priv->has_mob)
			return false;

		spin_lock(&dev_priv->cap_lock);
		vmw_write(dev_priv, SVGA_REG_DEV_CAP, SVGA3D_DEVCAP_3D);
		result = vmw_read(dev_priv, SVGA_REG_DEV_CAP);
		spin_unlock(&dev_priv->cap_lock);

		return (result != 0);
	}

	if (!(dev_priv->capabilities & SVGA_CAP_EXTENDED_FIFO))
		return false;

	fifo_min = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MIN);
	if (fifo_min <= SVGA_FIFO_3D_HWVERSION * sizeof(unsigned int))
		return false;

	hwversion = vmw_fifo_mem_read(dev_priv,
				      ((fifo->capabilities &
					SVGA_FIFO_CAP_3D_HWVERSION_REVISED) ?
					       SVGA_FIFO_3D_HWVERSION_REVISED :
					       SVGA_FIFO_3D_HWVERSION));

	if (hwversion == 0)
		return false;

	if (hwversion < SVGA3D_HWVERSION_WS8_B1)
		return false;

	/* Legacy Display Unit does not support surfaces */
	if (dev_priv->active_display_unit == vmw_du_legacy)
		return false;

	return true;
}

bool vmw_fifo_have_pitchlock(struct vmw_private *dev_priv)
{
	uint32_t caps;

	if (!(dev_priv->capabilities & SVGA_CAP_EXTENDED_FIFO))
		return false;

	caps = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_CAPABILITIES);
	if (caps & SVGA_FIFO_CAP_PITCHLOCK)
		return true;

	return false;
}

int vmw_fifo_init(struct vmw_private *dev_priv, struct vmw_fifo_state *fifo)
{
	uint32_t max;
	uint32_t min;

	fifo->dx = false;
	fifo->static_buffer_size = VMWGFX_FIFO_STATIC_SIZE;
	fifo->static_buffer = vmalloc(fifo->static_buffer_size);
	if (unlikely(fifo->static_buffer == NULL))
		return -ENOMEM;

	fifo->dynamic_buffer = NULL;
	fifo->reserved_size = 0;
	fifo->using_bounce_buffer = false;

	mutex_init(&fifo->fifo_mutex);
	init_rwsem(&fifo->rwsem);

	DRM_INFO("width %d\n", vmw_read(dev_priv, SVGA_REG_WIDTH));
	DRM_INFO("height %d\n", vmw_read(dev_priv, SVGA_REG_HEIGHT));
	DRM_INFO("bpp %d\n", vmw_read(dev_priv, SVGA_REG_BITS_PER_PIXEL));

	dev_priv->enable_state = vmw_read(dev_priv, SVGA_REG_ENABLE);
	dev_priv->config_done_state = vmw_read(dev_priv, SVGA_REG_CONFIG_DONE);
	dev_priv->traces_state = vmw_read(dev_priv, SVGA_REG_TRACES);

	vmw_write(dev_priv, SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE |
		  SVGA_REG_ENABLE_HIDE);

	vmw_write(dev_priv, SVGA_REG_TRACES, 0);

	min = 4;
	if (dev_priv->capabilities & SVGA_CAP_EXTENDED_FIFO)
		min = vmw_read(dev_priv, SVGA_REG_MEM_REGS);
	min <<= 2;

	if (min < PAGE_SIZE)
		min = PAGE_SIZE;

	vmw_fifo_mem_write(dev_priv, SVGA_FIFO_MIN, min);
	vmw_fifo_mem_write(dev_priv, SVGA_FIFO_MAX, dev_priv->fifo_mem_size);
	wmb();
	vmw_fifo_mem_write(dev_priv, SVGA_FIFO_NEXT_CMD, min);
	vmw_fifo_mem_write(dev_priv, SVGA_FIFO_STOP, min);
	vmw_fifo_mem_write(dev_priv, SVGA_FIFO_BUSY, 0);
	mb();

	vmw_write(dev_priv, SVGA_REG_CONFIG_DONE, 1);

	max = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MAX);
	min = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MIN);
	fifo->capabilities = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_CAPABILITIES);

	DRM_INFO("Fifo max 0x%08x min 0x%08x cap 0x%08x\n",
		 (unsigned int) max,
		 (unsigned int) min,
		 (unsigned int) fifo->capabilities);

	atomic_set(&dev_priv->marker_seq, dev_priv->last_read_seqno);
	vmw_fifo_mem_write(dev_priv, SVGA_FIFO_FENCE, dev_priv->last_read_seqno);

	return 0;
}

void vmw_fifo_ping_host(struct vmw_private *dev_priv, uint32_t reason)
{
	u32 *fifo_mem = dev_priv->fifo_mem;

	if (cmpxchg(fifo_mem + SVGA_FIFO_BUSY, 0, 1) == 0)
		vmw_write(dev_priv, SVGA_REG_SYNC, reason);
}

void vmw_fifo_release(struct vmw_private *dev_priv, struct vmw_fifo_state *fifo)
{
	vmw_write(dev_priv, SVGA_REG_SYNC, SVGA_SYNC_GENERIC);
	while (vmw_read(dev_priv, SVGA_REG_BUSY) != 0)
		;

	dev_priv->last_read_seqno = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_FENCE);

	vmw_write(dev_priv, SVGA_REG_CONFIG_DONE,
		  dev_priv->config_done_state);
	vmw_write(dev_priv, SVGA_REG_ENABLE,
		  dev_priv->enable_state);
	vmw_write(dev_priv, SVGA_REG_TRACES,
		  dev_priv->traces_state);

	if (likely(fifo->static_buffer != NULL)) {
		vfree(fifo->static_buffer);
		fifo->static_buffer = NULL;
	}

	if (likely(fifo->dynamic_buffer != NULL)) {
		vfree(fifo->dynamic_buffer);
		fifo->dynamic_buffer = NULL;
	}
}

static bool vmw_fifo_is_full(struct vmw_private *dev_priv, uint32_t bytes)
{
	uint32_t max = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MAX);
	uint32_t next_cmd = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_NEXT_CMD);
	uint32_t min = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MIN);
	uint32_t stop = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_STOP);

	return ((max - next_cmd) + (stop - min) <= bytes);
}

static int vmw_fifo_wait_noirq(struct vmw_private *dev_priv,
			       uint32_t bytes, bool interruptible,
			       unsigned long timeout)
{
	int ret = 0;
	unsigned long end_jiffies = jiffies + timeout;
	DEFINE_WAIT(__wait);

	DRM_INFO("Fifo wait noirq.\n");

	for (;;) {
		prepare_to_wait(&dev_priv->fifo_queue, &__wait,
				(interruptible) ?
				TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		if (!vmw_fifo_is_full(dev_priv, bytes))
			break;
		if (time_after_eq(jiffies, end_jiffies)) {
			ret = -EBUSY;
			DRM_ERROR("SVGA device lockup.\n");
			break;
		}
		schedule_timeout(1);
		if (interruptible && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	finish_wait(&dev_priv->fifo_queue, &__wait);
	wake_up_all(&dev_priv->fifo_queue);
	DRM_INFO("Fifo noirq exit.\n");
	return ret;
}

static int vmw_fifo_wait(struct vmw_private *dev_priv,
			 uint32_t bytes, bool interruptible,
			 unsigned long timeout)
{
	long ret = 1L;

	if (likely(!vmw_fifo_is_full(dev_priv, bytes)))
		return 0;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_FIFOFULL);
	if (!(dev_priv->capabilities & SVGA_CAP_IRQMASK))
		return vmw_fifo_wait_noirq(dev_priv, bytes,
					   interruptible, timeout);

	vmw_generic_waiter_add(dev_priv, SVGA_IRQFLAG_FIFO_PROGRESS,
			       &dev_priv->fifo_queue_waiters);

	if (interruptible)
		ret = wait_event_interruptible_timeout
		    (dev_priv->fifo_queue,
		     !vmw_fifo_is_full(dev_priv, bytes), timeout);
	else
		ret = wait_event_timeout
		    (dev_priv->fifo_queue,
		     !vmw_fifo_is_full(dev_priv, bytes), timeout);

	if (unlikely(ret == 0))
		ret = -EBUSY;
	else if (likely(ret > 0))
		ret = 0;

	vmw_generic_waiter_remove(dev_priv, SVGA_IRQFLAG_FIFO_PROGRESS,
				  &dev_priv->fifo_queue_waiters);

	return ret;
}

/*
 * Reserve @bytes number of bytes in the fifo.
 *
 * This function will return NULL (error) on two conditions:
 *  If it timeouts waiting for fifo space, or if @bytes is larger than the
 *   available fifo space.
 *
 * Returns:
 *   Pointer to the fifo, or null on error (possible hardware hang).
 */
static void *vmw_local_fifo_reserve(struct vmw_private *dev_priv,
				    uint32_t bytes)
{
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;
	u32  *fifo_mem = dev_priv->fifo_mem;
	uint32_t max;
	uint32_t min;
	uint32_t next_cmd;
	uint32_t reserveable = fifo_state->capabilities & SVGA_FIFO_CAP_RESERVE;
	int ret;

	mutex_lock(&fifo_state->fifo_mutex);
	max = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MAX);
	min = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MIN);
	next_cmd = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_NEXT_CMD);

	if (unlikely(bytes >= (max - min)))
		goto out_err;

	BUG_ON(fifo_state->reserved_size != 0);
	BUG_ON(fifo_state->dynamic_buffer != NULL);

	fifo_state->reserved_size = bytes;

	while (1) {
		uint32_t stop = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_STOP);
		bool need_bounce = false;
		bool reserve_in_place = false;

		if (next_cmd >= stop) {
			if (likely((next_cmd + bytes < max ||
				    (next_cmd + bytes == max && stop > min))))
				reserve_in_place = true;

			else if (vmw_fifo_is_full(dev_priv, bytes)) {
				ret = vmw_fifo_wait(dev_priv, bytes,
						    false, 3 * HZ);
				if (unlikely(ret != 0))
					goto out_err;
			} else
				need_bounce = true;

		} else {

			if (likely((next_cmd + bytes < stop)))
				reserve_in_place = true;
			else {
				ret = vmw_fifo_wait(dev_priv, bytes,
						    false, 3 * HZ);
				if (unlikely(ret != 0))
					goto out_err;
			}
		}

		if (reserve_in_place) {
			if (reserveable || bytes <= sizeof(uint32_t)) {
				fifo_state->using_bounce_buffer = false;

				if (reserveable)
					vmw_fifo_mem_write(dev_priv,
							   SVGA_FIFO_RESERVED,
							   bytes);
				return (void __force *) (fifo_mem +
							 (next_cmd >> 2));
			} else {
				need_bounce = true;
			}
		}

		if (need_bounce) {
			fifo_state->using_bounce_buffer = true;
			if (bytes < fifo_state->static_buffer_size)
				return fifo_state->static_buffer;
			else {
				fifo_state->dynamic_buffer = vmalloc(bytes);
				if (!fifo_state->dynamic_buffer)
					goto out_err;
				return fifo_state->dynamic_buffer;
			}
		}
	}
out_err:
	fifo_state->reserved_size = 0;
	mutex_unlock(&fifo_state->fifo_mutex);

	return NULL;
}

void *vmw_cmd_ctx_reserve(struct vmw_private *dev_priv, uint32_t bytes,
			  int ctx_id)
{
	void *ret;

	if (dev_priv->cman)
		ret = vmw_cmdbuf_reserve(dev_priv->cman, bytes,
					 ctx_id, false, NULL);
	else if (ctx_id == SVGA3D_INVALID_ID)
		ret = vmw_local_fifo_reserve(dev_priv, bytes);
	else {
		WARN(1, "Command buffer has not been allocated.\n");
		ret = NULL;
	}
	if (IS_ERR_OR_NULL(ret))
		return NULL;

	return ret;
}

static void vmw_fifo_res_copy(struct vmw_fifo_state *fifo_state,
			      struct vmw_private *vmw,
			      uint32_t next_cmd,
			      uint32_t max, uint32_t min, uint32_t bytes)
{
	u32 *fifo_mem = vmw->fifo_mem;
	uint32_t chunk_size = max - next_cmd;
	uint32_t rest;
	uint32_t *buffer = (fifo_state->dynamic_buffer != NULL) ?
	    fifo_state->dynamic_buffer : fifo_state->static_buffer;

	if (bytes < chunk_size)
		chunk_size = bytes;

	vmw_fifo_mem_write(vmw, SVGA_FIFO_RESERVED, bytes);
	mb();
	memcpy(fifo_mem + (next_cmd >> 2), buffer, chunk_size);
	rest = bytes - chunk_size;
	if (rest)
		memcpy(fifo_mem + (min >> 2), buffer + (chunk_size >> 2), rest);
}

static void vmw_fifo_slow_copy(struct vmw_fifo_state *fifo_state,
			       struct vmw_private *vmw,
			       uint32_t next_cmd,
			       uint32_t max, uint32_t min, uint32_t bytes)
{
	uint32_t *buffer = (fifo_state->dynamic_buffer != NULL) ?
	    fifo_state->dynamic_buffer : fifo_state->static_buffer;

	while (bytes > 0) {
		vmw_fifo_mem_write(vmw, (next_cmd >> 2), *buffer++);
		next_cmd += sizeof(uint32_t);
		if (unlikely(next_cmd == max))
			next_cmd = min;
		mb();
		vmw_fifo_mem_write(vmw, SVGA_FIFO_NEXT_CMD, next_cmd);
		mb();
		bytes -= sizeof(uint32_t);
	}
}

static void vmw_local_fifo_commit(struct vmw_private *dev_priv, uint32_t bytes)
{
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;
	uint32_t next_cmd = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_NEXT_CMD);
	uint32_t max = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MAX);
	uint32_t min = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_MIN);
	bool reserveable = fifo_state->capabilities & SVGA_FIFO_CAP_RESERVE;

	if (fifo_state->dx)
		bytes += sizeof(struct vmw_temp_set_context);

	fifo_state->dx = false;
	BUG_ON((bytes & 3) != 0);
	BUG_ON(bytes > fifo_state->reserved_size);

	fifo_state->reserved_size = 0;

	if (fifo_state->using_bounce_buffer) {
		if (reserveable)
			vmw_fifo_res_copy(fifo_state, dev_priv,
					  next_cmd, max, min, bytes);
		else
			vmw_fifo_slow_copy(fifo_state, dev_priv,
					   next_cmd, max, min, bytes);

		if (fifo_state->dynamic_buffer) {
			vfree(fifo_state->dynamic_buffer);
			fifo_state->dynamic_buffer = NULL;
		}

	}

	down_write(&fifo_state->rwsem);
	if (fifo_state->using_bounce_buffer || reserveable) {
		next_cmd += bytes;
		if (next_cmd >= max)
			next_cmd -= max - min;
		mb();
		vmw_fifo_mem_write(dev_priv, SVGA_FIFO_NEXT_CMD, next_cmd);
	}

	if (reserveable)
		vmw_fifo_mem_write(dev_priv, SVGA_FIFO_RESERVED, 0);
	mb();
	up_write(&fifo_state->rwsem);
	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
	mutex_unlock(&fifo_state->fifo_mutex);
}

void vmw_cmd_commit(struct vmw_private *dev_priv, uint32_t bytes)
{
	if (dev_priv->cman)
		vmw_cmdbuf_commit(dev_priv->cman, bytes, NULL, false);
	else
		vmw_local_fifo_commit(dev_priv, bytes);
}


/**
 * vmw_fifo_commit_flush - Commit fifo space and flush any buffered commands.
 *
 * @dev_priv: Pointer to device private structure.
 * @bytes: Number of bytes to commit.
 */
void vmw_cmd_commit_flush(struct vmw_private *dev_priv, uint32_t bytes)
{
	if (dev_priv->cman)
		vmw_cmdbuf_commit(dev_priv->cman, bytes, NULL, true);
	else
		vmw_local_fifo_commit(dev_priv, bytes);
}

/**
 * vmw_fifo_flush - Flush any buffered commands and make sure command processing
 * starts.
 *
 * @dev_priv: Pointer to device private structure.
 * @interruptible: Whether to wait interruptible if function needs to sleep.
 */
int vmw_cmd_flush(struct vmw_private *dev_priv, bool interruptible)
{
	might_sleep();

	if (dev_priv->cman)
		return vmw_cmdbuf_cur_flush(dev_priv->cman, interruptible);
	else
		return 0;
}

int vmw_cmd_send_fence(struct vmw_private *dev_priv, uint32_t *seqno)
{
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;
	struct svga_fifo_cmd_fence *cmd_fence;
	u32 *fm;
	int ret = 0;
	uint32_t bytes = sizeof(u32) + sizeof(*cmd_fence);

	fm = VMW_CMD_RESERVE(dev_priv, bytes);
	if (unlikely(fm == NULL)) {
		*seqno = atomic_read(&dev_priv->marker_seq);
		ret = -ENOMEM;
		(void)vmw_fallback_wait(dev_priv, false, true, *seqno,
					false, 3*HZ);
		goto out_err;
	}

	do {
		*seqno = atomic_add_return(1, &dev_priv->marker_seq);
	} while (*seqno == 0);

	if (!(fifo_state->capabilities & SVGA_FIFO_CAP_FENCE)) {

		/*
		 * Don't request hardware to send a fence. The
		 * waiting code in vmwgfx_irq.c will emulate this.
		 */

		vmw_cmd_commit(dev_priv, 0);
		return 0;
	}

	*fm++ = SVGA_CMD_FENCE;
	cmd_fence = (struct svga_fifo_cmd_fence *) fm;
	cmd_fence->fence = *seqno;
	vmw_cmd_commit_flush(dev_priv, bytes);
	vmw_update_seqno(dev_priv, fifo_state);

out_err:
	return ret;
}

/**
 * vmw_fifo_emit_dummy_legacy_query - emits a dummy query to the fifo using
 * legacy query commands.
 *
 * @dev_priv: The device private structure.
 * @cid: The hardware context id used for the query.
 *
 * See the vmw_fifo_emit_dummy_query documentation.
 */
static int vmw_fifo_emit_dummy_legacy_query(struct vmw_private *dev_priv,
					    uint32_t cid)
{
	/*
	 * A query wait without a preceding query end will
	 * actually finish all queries for this cid
	 * without writing to the query result structure.
	 */

	struct ttm_buffer_object *bo = &dev_priv->dummy_query_bo->base;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdWaitForQuery body;
	} *cmd;

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_WAIT_FOR_QUERY;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = cid;
	cmd->body.type = SVGA3D_QUERYTYPE_OCCLUSION;

	if (bo->mem.mem_type == TTM_PL_VRAM) {
		cmd->body.guestResult.gmrId = SVGA_GMR_FRAMEBUFFER;
		cmd->body.guestResult.offset = bo->mem.start << PAGE_SHIFT;
	} else {
		cmd->body.guestResult.gmrId = bo->mem.start;
		cmd->body.guestResult.offset = 0;
	}

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_fifo_emit_dummy_gb_query - emits a dummy query to the fifo using
 * guest-backed resource query commands.
 *
 * @dev_priv: The device private structure.
 * @cid: The hardware context id used for the query.
 *
 * See the vmw_fifo_emit_dummy_query documentation.
 */
static int vmw_fifo_emit_dummy_gb_query(struct vmw_private *dev_priv,
					uint32_t cid)
{
	/*
	 * A query wait without a preceding query end will
	 * actually finish all queries for this cid
	 * without writing to the query result structure.
	 */

	struct ttm_buffer_object *bo = &dev_priv->dummy_query_bo->base;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdWaitForGBQuery body;
	} *cmd;

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_WAIT_FOR_GB_QUERY;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = cid;
	cmd->body.type = SVGA3D_QUERYTYPE_OCCLUSION;
	BUG_ON(bo->mem.mem_type != VMW_PL_MOB);
	cmd->body.mobid = bo->mem.start;
	cmd->body.offset = 0;

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}


/**
 * vmw_fifo_emit_dummy_gb_query - emits a dummy query to the fifo using
 * appropriate resource query commands.
 *
 * @dev_priv: The device private structure.
 * @cid: The hardware context id used for the query.
 *
 * This function is used to emit a dummy occlusion query with
 * no primitives rendered between query begin and query end.
 * It's used to provide a query barrier, in order to know that when
 * this query is finished, all preceding queries are also finished.
 *
 * A Query results structure should have been initialized at the start
 * of the dev_priv->dummy_query_bo buffer object. And that buffer object
 * must also be either reserved or pinned when this function is called.
 *
 * Returns -ENOMEM on failure to reserve fifo space.
 */
int vmw_cmd_emit_dummy_query(struct vmw_private *dev_priv,
			      uint32_t cid)
{
	if (dev_priv->has_mob)
		return vmw_fifo_emit_dummy_gb_query(dev_priv, cid);

	return vmw_fifo_emit_dummy_legacy_query(dev_priv, cid);
}
