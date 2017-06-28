/**************************************************************************
 *
 * Copyright © 2015 VMware, Inc., Palo Alto, CA., USA
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
#include "ttm/ttm_bo_api.h"

/*
 * Size of inline command buffers. Try to make sure that a page size is a
 * multiple of the DMA pool allocation size.
 */
#define VMW_CMDBUF_INLINE_ALIGN 64
#define VMW_CMDBUF_INLINE_SIZE \
	(1024 - ALIGN(sizeof(SVGACBHeader), VMW_CMDBUF_INLINE_ALIGN))

/**
 * struct vmw_cmdbuf_context - Command buffer context queues
 *
 * @submitted: List of command buffers that have been submitted to the
 * manager but not yet submitted to hardware.
 * @hw_submitted: List of command buffers submitted to hardware.
 * @preempted: List of preempted command buffers.
 * @num_hw_submitted: Number of buffers currently being processed by hardware
 */
struct vmw_cmdbuf_context {
	struct list_head submitted;
	struct list_head hw_submitted;
	struct list_head preempted;
	unsigned num_hw_submitted;
};

/**
 * struct vmw_cmdbuf_man: - Command buffer manager
 *
 * @cur_mutex: Mutex protecting the command buffer used for incremental small
 * kernel command submissions, @cur.
 * @space_mutex: Mutex to protect against starvation when we allocate
 * main pool buffer space.
 * @work: A struct work_struct implementeing command buffer error handling.
 * Immutable.
 * @dev_priv: Pointer to the device private struct. Immutable.
 * @ctx: Array of command buffer context queues. The queues and the context
 * data is protected by @lock.
 * @error: List of command buffers that have caused device errors.
 * Protected by @lock.
 * @mm: Range manager for the command buffer space. Manager allocations and
 * frees are protected by @lock.
 * @cmd_space: Buffer object for the command buffer space, unless we were
 * able to make a contigous coherent DMA memory allocation, @handle. Immutable.
 * @map_obj: Mapping state for @cmd_space. Immutable.
 * @map: Pointer to command buffer space. May be a mapped buffer object or
 * a contigous coherent DMA memory allocation. Immutable.
 * @cur: Command buffer for small kernel command submissions. Protected by
 * the @cur_mutex.
 * @cur_pos: Space already used in @cur. Protected by @cur_mutex.
 * @default_size: Default size for the @cur command buffer. Immutable.
 * @max_hw_submitted: Max number of in-flight command buffers the device can
 * handle. Immutable.
 * @lock: Spinlock protecting command submission queues.
 * @header: Pool of DMA memory for device command buffer headers.
 * Internal protection.
 * @dheaders: Pool of DMA memory for device command buffer headers with trailing
 * space for inline data. Internal protection.
 * @tasklet: Tasklet struct for irq processing. Immutable.
 * @alloc_queue: Wait queue for processes waiting to allocate command buffer
 * space.
 * @idle_queue: Wait queue for processes waiting for command buffer idle.
 * @irq_on: Whether the process function has requested irq to be turned on.
 * Protected by @lock.
 * @using_mob: Whether the command buffer space is a MOB or a contigous DMA
 * allocation. Immutable.
 * @has_pool: Has a large pool of DMA memory which allows larger allocations.
 * Typically this is false only during bootstrap.
 * @handle: DMA address handle for the command buffer space if @using_mob is
 * false. Immutable.
 * @size: The size of the command buffer space. Immutable.
 */
struct vmw_cmdbuf_man {
	struct mutex cur_mutex;
	struct mutex space_mutex;
	struct work_struct work;
	struct vmw_private *dev_priv;
	struct vmw_cmdbuf_context ctx[SVGA_CB_CONTEXT_MAX];
	struct list_head error;
	struct drm_mm mm;
	struct ttm_buffer_object *cmd_space;
	struct ttm_bo_kmap_obj map_obj;
	u8 *map;
	struct vmw_cmdbuf_header *cur;
	size_t cur_pos;
	size_t default_size;
	unsigned max_hw_submitted;
	spinlock_t lock;
	struct dma_pool *headers;
	struct dma_pool *dheaders;
	struct tasklet_struct tasklet;
	wait_queue_head_t alloc_queue;
	wait_queue_head_t idle_queue;
	bool irq_on;
	bool using_mob;
	bool has_pool;
	dma_addr_t handle;
	size_t size;
};

/**
 * struct vmw_cmdbuf_header - Command buffer metadata
 *
 * @man: The command buffer manager.
 * @cb_header: Device command buffer header, allocated from a DMA pool.
 * @cb_context: The device command buffer context.
 * @list: List head for attaching to the manager lists.
 * @node: The range manager node.
 * @handle. The DMA address of @cb_header. Handed to the device on command
 * buffer submission.
 * @cmd: Pointer to the command buffer space of this buffer.
 * @size: Size of the command buffer space of this buffer.
 * @reserved: Reserved space of this buffer.
 * @inline_space: Whether inline command buffer space is used.
 */
struct vmw_cmdbuf_header {
	struct vmw_cmdbuf_man *man;
	SVGACBHeader *cb_header;
	SVGACBContext cb_context;
	struct list_head list;
	struct drm_mm_node node;
	dma_addr_t handle;
	u8 *cmd;
	size_t size;
	size_t reserved;
	bool inline_space;
};

/**
 * struct vmw_cmdbuf_dheader - Device command buffer header with inline
 * command buffer space.
 *
 * @cb_header: Device command buffer header.
 * @cmd: Inline command buffer space.
 */
struct vmw_cmdbuf_dheader {
	SVGACBHeader cb_header;
	u8 cmd[VMW_CMDBUF_INLINE_SIZE] __aligned(VMW_CMDBUF_INLINE_ALIGN);
};

/**
 * struct vmw_cmdbuf_alloc_info - Command buffer space allocation metadata
 *
 * @page_size: Size of requested command buffer space in pages.
 * @node: Pointer to the range manager node.
 * @done: True if this allocation has succeeded.
 */
struct vmw_cmdbuf_alloc_info {
	size_t page_size;
	struct drm_mm_node *node;
	bool done;
};

/* Loop over each context in the command buffer manager. */
#define for_each_cmdbuf_ctx(_man, _i, _ctx) \
	for (_i = 0, _ctx = &(_man)->ctx[0]; (_i) < SVGA_CB_CONTEXT_MAX; \
	     ++(_i), ++(_ctx))

static int vmw_cmdbuf_startstop(struct vmw_cmdbuf_man *man, bool enable);


/**
 * vmw_cmdbuf_cur_lock - Helper to lock the cur_mutex.
 *
 * @man: The range manager.
 * @interruptible: Whether to wait interruptible when locking.
 */
static int vmw_cmdbuf_cur_lock(struct vmw_cmdbuf_man *man, bool interruptible)
{
	if (interruptible) {
		if (mutex_lock_interruptible(&man->cur_mutex))
			return -ERESTARTSYS;
	} else {
		mutex_lock(&man->cur_mutex);
	}

	return 0;
}

/**
 * vmw_cmdbuf_cur_unlock - Helper to unlock the cur_mutex.
 *
 * @man: The range manager.
 */
static void vmw_cmdbuf_cur_unlock(struct vmw_cmdbuf_man *man)
{
	mutex_unlock(&man->cur_mutex);
}

/**
 * vmw_cmdbuf_header_inline_free - Free a struct vmw_cmdbuf_header that has
 * been used for the device context with inline command buffers.
 * Need not be called locked.
 *
 * @header: Pointer to the header to free.
 */
static void vmw_cmdbuf_header_inline_free(struct vmw_cmdbuf_header *header)
{
	struct vmw_cmdbuf_dheader *dheader;

	if (WARN_ON_ONCE(!header->inline_space))
		return;

	dheader = container_of(header->cb_header, struct vmw_cmdbuf_dheader,
			       cb_header);
	dma_pool_free(header->man->dheaders, dheader, header->handle);
	kfree(header);
}

/**
 * __vmw_cmdbuf_header_free - Free a struct vmw_cmdbuf_header  and its
 * associated structures.
 *
 * header: Pointer to the header to free.
 *
 * For internal use. Must be called with man::lock held.
 */
static void __vmw_cmdbuf_header_free(struct vmw_cmdbuf_header *header)
{
	struct vmw_cmdbuf_man *man = header->man;

	lockdep_assert_held_once(&man->lock);

	if (header->inline_space) {
		vmw_cmdbuf_header_inline_free(header);
		return;
	}

	drm_mm_remove_node(&header->node);
	wake_up_all(&man->alloc_queue);
	if (header->cb_header)
		dma_pool_free(man->headers, header->cb_header,
			      header->handle);
	kfree(header);
}

/**
 * vmw_cmdbuf_header_free - Free a struct vmw_cmdbuf_header  and its
 * associated structures.
 *
 * @header: Pointer to the header to free.
 */
void vmw_cmdbuf_header_free(struct vmw_cmdbuf_header *header)
{
	struct vmw_cmdbuf_man *man = header->man;

	/* Avoid locking if inline_space */
	if (header->inline_space) {
		vmw_cmdbuf_header_inline_free(header);
		return;
	}
	spin_lock_bh(&man->lock);
	__vmw_cmdbuf_header_free(header);
	spin_unlock_bh(&man->lock);
}


/**
 * vmw_cmbuf_header_submit: Submit a command buffer to hardware.
 *
 * @header: The header of the buffer to submit.
 */
static int vmw_cmdbuf_header_submit(struct vmw_cmdbuf_header *header)
{
	struct vmw_cmdbuf_man *man = header->man;
	u32 val;

	val = upper_32_bits(header->handle);
	vmw_write(man->dev_priv, SVGA_REG_COMMAND_HIGH, val);

	val = lower_32_bits(header->handle);
	val |= header->cb_context & SVGA_CB_CONTEXT_MASK;
	vmw_write(man->dev_priv, SVGA_REG_COMMAND_LOW, val);

	return header->cb_header->status;
}

/**
 * vmw_cmdbuf_ctx_init: Initialize a command buffer context.
 *
 * @ctx: The command buffer context to initialize
 */
static void vmw_cmdbuf_ctx_init(struct vmw_cmdbuf_context *ctx)
{
	INIT_LIST_HEAD(&ctx->hw_submitted);
	INIT_LIST_HEAD(&ctx->submitted);
	INIT_LIST_HEAD(&ctx->preempted);
	ctx->num_hw_submitted = 0;
}

/**
 * vmw_cmdbuf_ctx_submit: Submit command buffers from a command buffer
 * context.
 *
 * @man: The command buffer manager.
 * @ctx: The command buffer context.
 *
 * Submits command buffers to hardware until there are no more command
 * buffers to submit or the hardware can't handle more command buffers.
 */
static void vmw_cmdbuf_ctx_submit(struct vmw_cmdbuf_man *man,
				  struct vmw_cmdbuf_context *ctx)
{
	while (ctx->num_hw_submitted < man->max_hw_submitted &&
	      !list_empty(&ctx->submitted)) {
		struct vmw_cmdbuf_header *entry;
		SVGACBStatus status;

		entry = list_first_entry(&ctx->submitted,
					 struct vmw_cmdbuf_header,
					 list);

		status = vmw_cmdbuf_header_submit(entry);

		/* This should never happen */
		if (WARN_ON_ONCE(status == SVGA_CB_STATUS_QUEUE_FULL)) {
			entry->cb_header->status = SVGA_CB_STATUS_NONE;
			break;
		}

		list_del(&entry->list);
		list_add_tail(&entry->list, &ctx->hw_submitted);
		ctx->num_hw_submitted++;
	}

}

/**
 * vmw_cmdbuf_ctx_submit: Process a command buffer context.
 *
 * @man: The command buffer manager.
 * @ctx: The command buffer context.
 *
 * Submit command buffers to hardware if possible, and process finished
 * buffers. Typically freeing them, but on preemption or error take
 * appropriate action. Wake up waiters if appropriate.
 */
static void vmw_cmdbuf_ctx_process(struct vmw_cmdbuf_man *man,
				   struct vmw_cmdbuf_context *ctx,
				   int *notempty)
{
	struct vmw_cmdbuf_header *entry, *next;

	vmw_cmdbuf_ctx_submit(man, ctx);

	list_for_each_entry_safe(entry, next, &ctx->hw_submitted, list) {
		SVGACBStatus status = entry->cb_header->status;

		if (status == SVGA_CB_STATUS_NONE)
			break;

		list_del(&entry->list);
		wake_up_all(&man->idle_queue);
		ctx->num_hw_submitted--;
		switch (status) {
		case SVGA_CB_STATUS_COMPLETED:
			__vmw_cmdbuf_header_free(entry);
			break;
		case SVGA_CB_STATUS_COMMAND_ERROR:
		case SVGA_CB_STATUS_CB_HEADER_ERROR:
			list_add_tail(&entry->list, &man->error);
			schedule_work(&man->work);
			break;
		case SVGA_CB_STATUS_PREEMPTED:
			list_add(&entry->list, &ctx->preempted);
			break;
		default:
			WARN_ONCE(true, "Undefined command buffer status.\n");
			__vmw_cmdbuf_header_free(entry);
			break;
		}
	}

	vmw_cmdbuf_ctx_submit(man, ctx);
	if (!list_empty(&ctx->submitted))
		(*notempty)++;
}

/**
 * vmw_cmdbuf_man_process - Process all command buffer contexts and
 * switch on and off irqs as appropriate.
 *
 * @man: The command buffer manager.
 *
 * Calls vmw_cmdbuf_ctx_process() on all contexts. If any context has
 * command buffers left that are not submitted to hardware, Make sure
 * IRQ handling is turned on. Otherwise, make sure it's turned off.
 */
static void vmw_cmdbuf_man_process(struct vmw_cmdbuf_man *man)
{
	int notempty;
	struct vmw_cmdbuf_context *ctx;
	int i;

retry:
	notempty = 0;
	for_each_cmdbuf_ctx(man, i, ctx)
		vmw_cmdbuf_ctx_process(man, ctx, &notempty);

	if (man->irq_on && !notempty) {
		vmw_generic_waiter_remove(man->dev_priv,
					  SVGA_IRQFLAG_COMMAND_BUFFER,
					  &man->dev_priv->cmdbuf_waiters);
		man->irq_on = false;
	} else if (!man->irq_on && notempty) {
		vmw_generic_waiter_add(man->dev_priv,
				       SVGA_IRQFLAG_COMMAND_BUFFER,
				       &man->dev_priv->cmdbuf_waiters);
		man->irq_on = true;

		/* Rerun in case we just missed an irq. */
		goto retry;
	}
}

/**
 * vmw_cmdbuf_ctx_add - Schedule a command buffer for submission on a
 * command buffer context
 *
 * @man: The command buffer manager.
 * @header: The header of the buffer to submit.
 * @cb_context: The command buffer context to use.
 *
 * This function adds @header to the "submitted" queue of the command
 * buffer context identified by @cb_context. It then calls the command buffer
 * manager processing to potentially submit the buffer to hardware.
 * @man->lock needs to be held when calling this function.
 */
static void vmw_cmdbuf_ctx_add(struct vmw_cmdbuf_man *man,
			       struct vmw_cmdbuf_header *header,
			       SVGACBContext cb_context)
{
	if (!(header->cb_header->flags & SVGA_CB_FLAG_DX_CONTEXT))
		header->cb_header->dxContext = 0;
	header->cb_context = cb_context;
	list_add_tail(&header->list, &man->ctx[cb_context].submitted);

	vmw_cmdbuf_man_process(man);
}

/**
 * vmw_cmdbuf_man_tasklet - The main part of the command buffer interrupt
 * handler implemented as a tasklet.
 *
 * @data: Tasklet closure. A pointer to the command buffer manager cast to
 * an unsigned long.
 *
 * The bottom half (tasklet) of the interrupt handler simply calls into the
 * command buffer processor to free finished buffers and submit any
 * queued buffers to hardware.
 */
static void vmw_cmdbuf_man_tasklet(unsigned long data)
{
	struct vmw_cmdbuf_man *man = (struct vmw_cmdbuf_man *) data;

	spin_lock(&man->lock);
	vmw_cmdbuf_man_process(man);
	spin_unlock(&man->lock);
}

/**
 * vmw_cmdbuf_work_func - The deferred work function that handles
 * command buffer errors.
 *
 * @work: The work func closure argument.
 *
 * Restarting the command buffer context after an error requires process
 * context, so it is deferred to this work function.
 */
static void vmw_cmdbuf_work_func(struct work_struct *work)
{
	struct vmw_cmdbuf_man *man =
		container_of(work, struct vmw_cmdbuf_man, work);
	struct vmw_cmdbuf_header *entry, *next;
	uint32_t dummy;
	bool restart = false;

	spin_lock_bh(&man->lock);
	list_for_each_entry_safe(entry, next, &man->error, list) {
		restart = true;
		DRM_ERROR("Command buffer error.\n");

		list_del(&entry->list);
		__vmw_cmdbuf_header_free(entry);
		wake_up_all(&man->idle_queue);
	}
	spin_unlock_bh(&man->lock);

	if (restart && vmw_cmdbuf_startstop(man, true))
		DRM_ERROR("Failed restarting command buffer context 0.\n");

	/* Send a new fence in case one was removed */
	vmw_fifo_send_fence(man->dev_priv, &dummy);
}

/**
 * vmw_cmdbuf_man idle - Check whether the command buffer manager is idle.
 *
 * @man: The command buffer manager.
 * @check_preempted: Check also the preempted queue for pending command buffers.
 *
 */
static bool vmw_cmdbuf_man_idle(struct vmw_cmdbuf_man *man,
				bool check_preempted)
{
	struct vmw_cmdbuf_context *ctx;
	bool idle = false;
	int i;

	spin_lock_bh(&man->lock);
	vmw_cmdbuf_man_process(man);
	for_each_cmdbuf_ctx(man, i, ctx) {
		if (!list_empty(&ctx->submitted) ||
		    !list_empty(&ctx->hw_submitted) ||
		    (check_preempted && !list_empty(&ctx->preempted)))
			goto out_unlock;
	}

	idle = list_empty(&man->error);

out_unlock:
	spin_unlock_bh(&man->lock);

	return idle;
}

/**
 * __vmw_cmdbuf_cur_flush - Flush the current command buffer for small kernel
 * command submissions
 *
 * @man: The command buffer manager.
 *
 * Flushes the current command buffer without allocating a new one. A new one
 * is automatically allocated when needed. Call with @man->cur_mutex held.
 */
static void __vmw_cmdbuf_cur_flush(struct vmw_cmdbuf_man *man)
{
	struct vmw_cmdbuf_header *cur = man->cur;

	WARN_ON(!mutex_is_locked(&man->cur_mutex));

	if (!cur)
		return;

	spin_lock_bh(&man->lock);
	if (man->cur_pos == 0) {
		__vmw_cmdbuf_header_free(cur);
		goto out_unlock;
	}

	man->cur->cb_header->length = man->cur_pos;
	vmw_cmdbuf_ctx_add(man, man->cur, SVGA_CB_CONTEXT_0);
out_unlock:
	spin_unlock_bh(&man->lock);
	man->cur = NULL;
	man->cur_pos = 0;
}

/**
 * vmw_cmdbuf_cur_flush - Flush the current command buffer for small kernel
 * command submissions
 *
 * @man: The command buffer manager.
 * @interruptible: Whether to sleep interruptible when sleeping.
 *
 * Flushes the current command buffer without allocating a new one. A new one
 * is automatically allocated when needed.
 */
int vmw_cmdbuf_cur_flush(struct vmw_cmdbuf_man *man,
			 bool interruptible)
{
	int ret = vmw_cmdbuf_cur_lock(man, interruptible);

	if (ret)
		return ret;

	__vmw_cmdbuf_cur_flush(man);
	vmw_cmdbuf_cur_unlock(man);

	return 0;
}

/**
 * vmw_cmdbuf_idle - Wait for command buffer manager idle.
 *
 * @man: The command buffer manager.
 * @interruptible: Sleep interruptible while waiting.
 * @timeout: Time out after this many ticks.
 *
 * Wait until the command buffer manager has processed all command buffers,
 * or until a timeout occurs. If a timeout occurs, the function will return
 * -EBUSY.
 */
int vmw_cmdbuf_idle(struct vmw_cmdbuf_man *man, bool interruptible,
		    unsigned long timeout)
{
	int ret;

	ret = vmw_cmdbuf_cur_flush(man, interruptible);
	vmw_generic_waiter_add(man->dev_priv,
			       SVGA_IRQFLAG_COMMAND_BUFFER,
			       &man->dev_priv->cmdbuf_waiters);

	if (interruptible) {
		ret = wait_event_interruptible_timeout
			(man->idle_queue, vmw_cmdbuf_man_idle(man, true),
			 timeout);
	} else {
		ret = wait_event_timeout
			(man->idle_queue, vmw_cmdbuf_man_idle(man, true),
			 timeout);
	}
	vmw_generic_waiter_remove(man->dev_priv,
				  SVGA_IRQFLAG_COMMAND_BUFFER,
				  &man->dev_priv->cmdbuf_waiters);
	if (ret == 0) {
		if (!vmw_cmdbuf_man_idle(man, true))
			ret = -EBUSY;
		else
			ret = 0;
	}
	if (ret > 0)
		ret = 0;

	return ret;
}

/**
 * vmw_cmdbuf_try_alloc - Try to allocate buffer space from the main pool.
 *
 * @man: The command buffer manager.
 * @info: Allocation info. Will hold the size on entry and allocated mm node
 * on successful return.
 *
 * Try to allocate buffer space from the main pool. Returns true if succeeded.
 * If a fatal error was hit, the error code is returned in @info->ret.
 */
static bool vmw_cmdbuf_try_alloc(struct vmw_cmdbuf_man *man,
				 struct vmw_cmdbuf_alloc_info *info)
{
	int ret;

	if (info->done)
		return true;
 
	memset(info->node, 0, sizeof(*info->node));
	spin_lock_bh(&man->lock);
	ret = drm_mm_insert_node(&man->mm, info->node, info->page_size);
	if (ret) {
		vmw_cmdbuf_man_process(man);
		ret = drm_mm_insert_node(&man->mm, info->node, info->page_size);
	}

	spin_unlock_bh(&man->lock);
	info->done = !ret;

	return info->done;
}

/**
 * vmw_cmdbuf_alloc_space - Allocate buffer space from the main pool.
 *
 * @man: The command buffer manager.
 * @node: Pointer to pre-allocated range-manager node.
 * @size: The size of the allocation.
 * @interruptible: Whether to sleep interruptible while waiting for space.
 *
 * This function allocates buffer space from the main pool, and if there is
 * no space available ATM, it turns on IRQ handling and sleeps waiting for it to
 * become available.
 */
static int vmw_cmdbuf_alloc_space(struct vmw_cmdbuf_man *man,
				  struct drm_mm_node *node,
				  size_t size,
				  bool interruptible)
{
	struct vmw_cmdbuf_alloc_info info;

	info.page_size = PAGE_ALIGN(size) >> PAGE_SHIFT;
	info.node = node;
	info.done = false;

	/*
	 * To prevent starvation of large requests, only one allocating call
	 * at a time waiting for space.
	 */
	if (interruptible) {
		if (mutex_lock_interruptible(&man->space_mutex))
			return -ERESTARTSYS;
	} else {
		mutex_lock(&man->space_mutex);
	}

	/* Try to allocate space without waiting. */
	if (vmw_cmdbuf_try_alloc(man, &info))
		goto out_unlock;

	vmw_generic_waiter_add(man->dev_priv,
			       SVGA_IRQFLAG_COMMAND_BUFFER,
			       &man->dev_priv->cmdbuf_waiters);

	if (interruptible) {
		int ret;

		ret = wait_event_interruptible
			(man->alloc_queue, vmw_cmdbuf_try_alloc(man, &info));
		if (ret) {
			vmw_generic_waiter_remove
				(man->dev_priv, SVGA_IRQFLAG_COMMAND_BUFFER,
				 &man->dev_priv->cmdbuf_waiters);
			mutex_unlock(&man->space_mutex);
			return ret;
		}
	} else {
		wait_event(man->alloc_queue, vmw_cmdbuf_try_alloc(man, &info));
	}
	vmw_generic_waiter_remove(man->dev_priv,
				  SVGA_IRQFLAG_COMMAND_BUFFER,
				  &man->dev_priv->cmdbuf_waiters);

out_unlock:
	mutex_unlock(&man->space_mutex);

	return 0;
}

/**
 * vmw_cmdbuf_space_pool - Set up a command buffer header with command buffer
 * space from the main pool.
 *
 * @man: The command buffer manager.
 * @header: Pointer to the header to set up.
 * @size: The requested size of the buffer space.
 * @interruptible: Whether to sleep interruptible while waiting for space.
 */
static int vmw_cmdbuf_space_pool(struct vmw_cmdbuf_man *man,
				 struct vmw_cmdbuf_header *header,
				 size_t size,
				 bool interruptible)
{
	SVGACBHeader *cb_hdr;
	size_t offset;
	int ret;

	if (!man->has_pool)
		return -ENOMEM;

	ret = vmw_cmdbuf_alloc_space(man, &header->node,  size, interruptible);

	if (ret)
		return ret;

	header->cb_header = dma_pool_alloc(man->headers, GFP_KERNEL,
					   &header->handle);
	if (!header->cb_header) {
		ret = -ENOMEM;
		goto out_no_cb_header;
	}

	header->size = header->node.size << PAGE_SHIFT;
	cb_hdr = header->cb_header;
	offset = header->node.start << PAGE_SHIFT;
	header->cmd = man->map + offset;
	memset(cb_hdr, 0, sizeof(*cb_hdr));
	if (man->using_mob) {
		cb_hdr->flags = SVGA_CB_FLAG_MOB;
		cb_hdr->ptr.mob.mobid = man->cmd_space->mem.start;
		cb_hdr->ptr.mob.mobOffset = offset;
	} else {
		cb_hdr->ptr.pa = (u64)man->handle + (u64)offset;
	}

	return 0;

out_no_cb_header:
	spin_lock_bh(&man->lock);
	drm_mm_remove_node(&header->node);
	spin_unlock_bh(&man->lock);

	return ret;
}

/**
 * vmw_cmdbuf_space_inline - Set up a command buffer header with
 * inline command buffer space.
 *
 * @man: The command buffer manager.
 * @header: Pointer to the header to set up.
 * @size: The requested size of the buffer space.
 */
static int vmw_cmdbuf_space_inline(struct vmw_cmdbuf_man *man,
				   struct vmw_cmdbuf_header *header,
				   int size)
{
	struct vmw_cmdbuf_dheader *dheader;
	SVGACBHeader *cb_hdr;

	if (WARN_ON_ONCE(size > VMW_CMDBUF_INLINE_SIZE))
		return -ENOMEM;

	dheader = dma_pool_alloc(man->dheaders, GFP_KERNEL,
				 &header->handle);
	if (!dheader)
		return -ENOMEM;

	header->inline_space = true;
	header->size = VMW_CMDBUF_INLINE_SIZE;
	cb_hdr = &dheader->cb_header;
	header->cb_header = cb_hdr;
	header->cmd = dheader->cmd;
	memset(dheader, 0, sizeof(*dheader));
	cb_hdr->status = SVGA_CB_STATUS_NONE;
	cb_hdr->flags = SVGA_CB_FLAG_NONE;
	cb_hdr->ptr.pa = (u64)header->handle +
		(u64)offsetof(struct vmw_cmdbuf_dheader, cmd);

	return 0;
}

/**
 * vmw_cmdbuf_alloc - Allocate a command buffer header complete with
 * command buffer space.
 *
 * @man: The command buffer manager.
 * @size: The requested size of the buffer space.
 * @interruptible: Whether to sleep interruptible while waiting for space.
 * @p_header: points to a header pointer to populate on successful return.
 *
 * Returns a pointer to command buffer space if successful. Otherwise
 * returns an error pointer. The header pointer returned in @p_header should
 * be used for upcoming calls to vmw_cmdbuf_reserve() and vmw_cmdbuf_commit().
 */
void *vmw_cmdbuf_alloc(struct vmw_cmdbuf_man *man,
		       size_t size, bool interruptible,
		       struct vmw_cmdbuf_header **p_header)
{
	struct vmw_cmdbuf_header *header;
	int ret = 0;

	*p_header = NULL;

	header = kzalloc(sizeof(*header), GFP_KERNEL);
	if (!header)
		return ERR_PTR(-ENOMEM);

	if (size <= VMW_CMDBUF_INLINE_SIZE)
		ret = vmw_cmdbuf_space_inline(man, header, size);
	else
		ret = vmw_cmdbuf_space_pool(man, header, size, interruptible);

	if (ret) {
		kfree(header);
		return ERR_PTR(ret);
	}

	header->man = man;
	INIT_LIST_HEAD(&header->list);
	header->cb_header->status = SVGA_CB_STATUS_NONE;
	*p_header = header;

	return header->cmd;
}

/**
 * vmw_cmdbuf_reserve_cur - Reserve space for commands in the current
 * command buffer.
 *
 * @man: The command buffer manager.
 * @size: The requested size of the commands.
 * @ctx_id: The context id if any. Otherwise set to SVGA3D_REG_INVALID.
 * @interruptible: Whether to sleep interruptible while waiting for space.
 *
 * Returns a pointer to command buffer space if successful. Otherwise
 * returns an error pointer.
 */
static void *vmw_cmdbuf_reserve_cur(struct vmw_cmdbuf_man *man,
				    size_t size,
				    int ctx_id,
				    bool interruptible)
{
	struct vmw_cmdbuf_header *cur;
	void *ret;

	if (vmw_cmdbuf_cur_lock(man, interruptible))
		return ERR_PTR(-ERESTARTSYS);

	cur = man->cur;
	if (cur && (size + man->cur_pos > cur->size ||
		    ((cur->cb_header->flags & SVGA_CB_FLAG_DX_CONTEXT) &&
		     ctx_id != cur->cb_header->dxContext)))
		__vmw_cmdbuf_cur_flush(man);

	if (!man->cur) {
		ret = vmw_cmdbuf_alloc(man,
				       max_t(size_t, size, man->default_size),
				       interruptible, &man->cur);
		if (IS_ERR(ret)) {
			vmw_cmdbuf_cur_unlock(man);
			return ret;
		}

		cur = man->cur;
	}

	if (ctx_id != SVGA3D_INVALID_ID) {
		cur->cb_header->flags |= SVGA_CB_FLAG_DX_CONTEXT;
		cur->cb_header->dxContext = ctx_id;
	}

	cur->reserved = size;

	return (void *) (man->cur->cmd + man->cur_pos);
}

/**
 * vmw_cmdbuf_commit_cur - Commit commands in the current command buffer.
 *
 * @man: The command buffer manager.
 * @size: The size of the commands actually written.
 * @flush: Whether to flush the command buffer immediately.
 */
static void vmw_cmdbuf_commit_cur(struct vmw_cmdbuf_man *man,
				  size_t size, bool flush)
{
	struct vmw_cmdbuf_header *cur = man->cur;

	WARN_ON(!mutex_is_locked(&man->cur_mutex));

	WARN_ON(size > cur->reserved);
	man->cur_pos += size;
	if (!size)
		cur->cb_header->flags &= ~SVGA_CB_FLAG_DX_CONTEXT;
	if (flush)
		__vmw_cmdbuf_cur_flush(man);
	vmw_cmdbuf_cur_unlock(man);
}

/**
 * vmw_cmdbuf_reserve - Reserve space for commands in a command buffer.
 *
 * @man: The command buffer manager.
 * @size: The requested size of the commands.
 * @ctx_id: The context id if any. Otherwise set to SVGA3D_REG_INVALID.
 * @interruptible: Whether to sleep interruptible while waiting for space.
 * @header: Header of the command buffer. NULL if the current command buffer
 * should be used.
 *
 * Returns a pointer to command buffer space if successful. Otherwise
 * returns an error pointer.
 */
void *vmw_cmdbuf_reserve(struct vmw_cmdbuf_man *man, size_t size,
			 int ctx_id, bool interruptible,
			 struct vmw_cmdbuf_header *header)
{
	if (!header)
		return vmw_cmdbuf_reserve_cur(man, size, ctx_id, interruptible);

	if (size > header->size)
		return ERR_PTR(-EINVAL);

	if (ctx_id != SVGA3D_INVALID_ID) {
		header->cb_header->flags |= SVGA_CB_FLAG_DX_CONTEXT;
		header->cb_header->dxContext = ctx_id;
	}

	header->reserved = size;
	return header->cmd;
}

/**
 * vmw_cmdbuf_commit - Commit commands in a command buffer.
 *
 * @man: The command buffer manager.
 * @size: The size of the commands actually written.
 * @header: Header of the command buffer. NULL if the current command buffer
 * should be used.
 * @flush: Whether to flush the command buffer immediately.
 */
void vmw_cmdbuf_commit(struct vmw_cmdbuf_man *man, size_t size,
		       struct vmw_cmdbuf_header *header, bool flush)
{
	if (!header) {
		vmw_cmdbuf_commit_cur(man, size, flush);
		return;
	}

	(void) vmw_cmdbuf_cur_lock(man, false);
	__vmw_cmdbuf_cur_flush(man);
	WARN_ON(size > header->reserved);
	man->cur = header;
	man->cur_pos = size;
	if (!size)
		header->cb_header->flags &= ~SVGA_CB_FLAG_DX_CONTEXT;
	if (flush)
		__vmw_cmdbuf_cur_flush(man);
	vmw_cmdbuf_cur_unlock(man);
}

/**
 * vmw_cmdbuf_tasklet_schedule - Schedule the interrupt handler bottom half.
 *
 * @man: The command buffer manager.
 */
void vmw_cmdbuf_tasklet_schedule(struct vmw_cmdbuf_man *man)
{
	if (!man)
		return;

	tasklet_schedule(&man->tasklet);
}

/**
 * vmw_cmdbuf_send_device_command - Send a command through the device context.
 *
 * @man: The command buffer manager.
 * @command: Pointer to the command to send.
 * @size: Size of the command.
 *
 * Synchronously sends a device context command.
 */
static int vmw_cmdbuf_send_device_command(struct vmw_cmdbuf_man *man,
					  const void *command,
					  size_t size)
{
	struct vmw_cmdbuf_header *header;
	int status;
	void *cmd = vmw_cmdbuf_alloc(man, size, false, &header);

	if (IS_ERR(cmd))
		return PTR_ERR(cmd);

	memcpy(cmd, command, size);
	header->cb_header->length = size;
	header->cb_context = SVGA_CB_CONTEXT_DEVICE;
	spin_lock_bh(&man->lock);
	status = vmw_cmdbuf_header_submit(header);
	spin_unlock_bh(&man->lock);
	vmw_cmdbuf_header_free(header);

	if (status != SVGA_CB_STATUS_COMPLETED) {
		DRM_ERROR("Device context command failed with status %d\n",
			  status);
		return -EINVAL;
	}

	return 0;
}

/**
 * vmw_cmdbuf_startstop - Send a start / stop command through the device
 * context.
 *
 * @man: The command buffer manager.
 * @enable: Whether to enable or disable the context.
 *
 * Synchronously sends a device start / stop context command.
 */
static int vmw_cmdbuf_startstop(struct vmw_cmdbuf_man *man,
				bool enable)
{
	struct {
		uint32 id;
		SVGADCCmdStartStop body;
	} __packed cmd;

	cmd.id = SVGA_DC_CMD_START_STOP_CONTEXT;
	cmd.body.enable = (enable) ? 1 : 0;
	cmd.body.context = SVGA_CB_CONTEXT_0;

	return vmw_cmdbuf_send_device_command(man, &cmd, sizeof(cmd));
}

/**
 * vmw_cmdbuf_set_pool_size - Set command buffer manager sizes
 *
 * @man: The command buffer manager.
 * @size: The size of the main space pool.
 * @default_size: The default size of the command buffer for small kernel
 * submissions.
 *
 * Set the size and allocate the main command buffer space pool,
 * as well as the default size of the command buffer for
 * small kernel submissions. If successful, this enables large command
 * submissions. Note that this function requires that rudimentary command
 * submission is already available and that the MOB memory manager is alive.
 * Returns 0 on success. Negative error code on failure.
 */
int vmw_cmdbuf_set_pool_size(struct vmw_cmdbuf_man *man,
			     size_t size, size_t default_size)
{
	struct vmw_private *dev_priv = man->dev_priv;
	bool dummy;
	int ret;

	if (man->has_pool)
		return -EINVAL;

	/* First, try to allocate a huge chunk of DMA memory */
	size = PAGE_ALIGN(size);
	man->map = dma_alloc_coherent(&dev_priv->dev->pdev->dev, size,
				      &man->handle, GFP_KERNEL);
	if (man->map) {
		man->using_mob = false;
	} else {
		/*
		 * DMA memory failed. If we can have command buffers in a
		 * MOB, try to use that instead. Note that this will
		 * actually call into the already enabled manager, when
		 * binding the MOB.
		 */
		if (!(dev_priv->capabilities & SVGA_CAP_DX))
			return -ENOMEM;

		ret = ttm_bo_create(&dev_priv->bdev, size, ttm_bo_type_device,
				    &vmw_mob_ne_placement, 0, false, NULL,
				    &man->cmd_space);
		if (ret)
			return ret;

		man->using_mob = true;
		ret = ttm_bo_kmap(man->cmd_space, 0, size >> PAGE_SHIFT,
				  &man->map_obj);
		if (ret)
			goto out_no_map;

		man->map = ttm_kmap_obj_virtual(&man->map_obj, &dummy);
	}

	man->size = size;
	drm_mm_init(&man->mm, 0, size >> PAGE_SHIFT);

	man->has_pool = true;

	/*
	 * For now, set the default size to VMW_CMDBUF_INLINE_SIZE to
	 * prevent deadlocks from happening when vmw_cmdbuf_space_pool()
	 * needs to wait for space and we block on further command
	 * submissions to be able to free up space.
	 */
	man->default_size = VMW_CMDBUF_INLINE_SIZE;
	DRM_INFO("Using command buffers with %s pool.\n",
		 (man->using_mob) ? "MOB" : "DMA");

	return 0;

out_no_map:
	if (man->using_mob)
		ttm_bo_unref(&man->cmd_space);

	return ret;
}

/**
 * vmw_cmdbuf_man_create: Create a command buffer manager and enable it for
 * inline command buffer submissions only.
 *
 * @dev_priv: Pointer to device private structure.
 *
 * Returns a pointer to a cummand buffer manager to success or error pointer
 * on failure. The command buffer manager will be enabled for submissions of
 * size VMW_CMDBUF_INLINE_SIZE only.
 */
struct vmw_cmdbuf_man *vmw_cmdbuf_man_create(struct vmw_private *dev_priv)
{
	struct vmw_cmdbuf_man *man;
	struct vmw_cmdbuf_context *ctx;
	int i;
	int ret;

	if (!(dev_priv->capabilities & SVGA_CAP_COMMAND_BUFFERS))
		return ERR_PTR(-ENOSYS);

	man = kzalloc(sizeof(*man), GFP_KERNEL);
	if (!man)
		return ERR_PTR(-ENOMEM);

	man->headers = dma_pool_create("vmwgfx cmdbuf",
				       &dev_priv->dev->pdev->dev,
				       sizeof(SVGACBHeader),
				       64, PAGE_SIZE);
	if (!man->headers) {
		ret = -ENOMEM;
		goto out_no_pool;
	}

	man->dheaders = dma_pool_create("vmwgfx inline cmdbuf",
					&dev_priv->dev->pdev->dev,
					sizeof(struct vmw_cmdbuf_dheader),
					64, PAGE_SIZE);
	if (!man->dheaders) {
		ret = -ENOMEM;
		goto out_no_dpool;
	}

	for_each_cmdbuf_ctx(man, i, ctx)
		vmw_cmdbuf_ctx_init(ctx);

	INIT_LIST_HEAD(&man->error);
	spin_lock_init(&man->lock);
	mutex_init(&man->cur_mutex);
	mutex_init(&man->space_mutex);
	tasklet_init(&man->tasklet, vmw_cmdbuf_man_tasklet,
		     (unsigned long) man);
	man->default_size = VMW_CMDBUF_INLINE_SIZE;
	init_waitqueue_head(&man->alloc_queue);
	init_waitqueue_head(&man->idle_queue);
	man->dev_priv = dev_priv;
	man->max_hw_submitted = SVGA_CB_MAX_QUEUED_PER_CONTEXT - 1;
	INIT_WORK(&man->work, &vmw_cmdbuf_work_func);
	vmw_generic_waiter_add(dev_priv, SVGA_IRQFLAG_ERROR,
			       &dev_priv->error_waiters);
	ret = vmw_cmdbuf_startstop(man, true);
	if (ret) {
		DRM_ERROR("Failed starting command buffer context 0.\n");
		vmw_cmdbuf_man_destroy(man);
		return ERR_PTR(ret);
	}

	return man;

out_no_dpool:
	dma_pool_destroy(man->headers);
out_no_pool:
	kfree(man);

	return ERR_PTR(ret);
}

/**
 * vmw_cmdbuf_remove_pool - Take down the main buffer space pool.
 *
 * @man: Pointer to a command buffer manager.
 *
 * This function removes the main buffer space pool, and should be called
 * before MOB memory management is removed. When this function has been called,
 * only small command buffer submissions of size VMW_CMDBUF_INLINE_SIZE or
 * less are allowed, and the default size of the command buffer for small kernel
 * submissions is also set to this size.
 */
void vmw_cmdbuf_remove_pool(struct vmw_cmdbuf_man *man)
{
	if (!man->has_pool)
		return;

	man->has_pool = false;
	man->default_size = VMW_CMDBUF_INLINE_SIZE;
	(void) vmw_cmdbuf_idle(man, false, 10*HZ);
	if (man->using_mob) {
		(void) ttm_bo_kunmap(&man->map_obj);
		ttm_bo_unref(&man->cmd_space);
	} else {
		dma_free_coherent(&man->dev_priv->dev->pdev->dev,
				  man->size, man->map, man->handle);
	}
}

/**
 * vmw_cmdbuf_man_destroy - Take down a command buffer manager.
 *
 * @man: Pointer to a command buffer manager.
 *
 * This function idles and then destroys a command buffer manager.
 */
void vmw_cmdbuf_man_destroy(struct vmw_cmdbuf_man *man)
{
	WARN_ON_ONCE(man->has_pool);
	(void) vmw_cmdbuf_idle(man, false, 10*HZ);
	if (vmw_cmdbuf_startstop(man, false))
		DRM_ERROR("Failed stopping command buffer context 0.\n");

	vmw_generic_waiter_remove(man->dev_priv, SVGA_IRQFLAG_ERROR,
				  &man->dev_priv->error_waiters);
	tasklet_kill(&man->tasklet);
	(void) cancel_work_sync(&man->work);
	dma_pool_destroy(man->dheaders);
	dma_pool_destroy(man->headers);
	mutex_destroy(&man->cur_mutex);
	mutex_destroy(&man->space_mutex);
	kfree(man);
}
