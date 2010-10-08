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
#include "drmP.h"
#include "ttm/ttm_placement.h"

bool vmw_fifo_have_3d(struct vmw_private *dev_priv)
{
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	uint32_t fifo_min, hwversion;

	if (!(dev_priv->capabilities & SVGA_CAP_EXTENDED_FIFO))
		return false;

	fifo_min = ioread32(fifo_mem  + SVGA_FIFO_MIN);
	if (fifo_min <= SVGA_FIFO_3D_HWVERSION * sizeof(unsigned int))
		return false;

	hwversion = ioread32(fifo_mem + SVGA_FIFO_3D_HWVERSION);
	if (hwversion == 0)
		return false;

	if (hwversion < SVGA3D_HWVERSION_WS65_B1)
		return false;

	return true;
}

bool vmw_fifo_have_pitchlock(struct vmw_private *dev_priv)
{
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	uint32_t caps;

	if (!(dev_priv->capabilities & SVGA_CAP_EXTENDED_FIFO))
		return false;

	caps = ioread32(fifo_mem + SVGA_FIFO_CAPABILITIES);
	if (caps & SVGA_FIFO_CAP_PITCHLOCK)
		return true;

	return false;
}

int vmw_fifo_init(struct vmw_private *dev_priv, struct vmw_fifo_state *fifo)
{
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	uint32_t max;
	uint32_t min;
	uint32_t dummy;
	int ret;

	fifo->static_buffer_size = VMWGFX_FIFO_STATIC_SIZE;
	fifo->static_buffer = vmalloc(fifo->static_buffer_size);
	if (unlikely(fifo->static_buffer == NULL))
		return -ENOMEM;

	fifo->last_buffer_size = VMWGFX_FIFO_STATIC_SIZE;
	fifo->last_data_size = 0;
	fifo->last_buffer_add = false;
	fifo->last_buffer = vmalloc(fifo->last_buffer_size);
	if (unlikely(fifo->last_buffer == NULL)) {
		ret = -ENOMEM;
		goto out_err;
	}

	fifo->dynamic_buffer = NULL;
	fifo->reserved_size = 0;
	fifo->using_bounce_buffer = false;

	mutex_init(&fifo->fifo_mutex);
	init_rwsem(&fifo->rwsem);

	/*
	 * Allow mapping the first page read-only to user-space.
	 */

	DRM_INFO("width %d\n", vmw_read(dev_priv, SVGA_REG_WIDTH));
	DRM_INFO("height %d\n", vmw_read(dev_priv, SVGA_REG_HEIGHT));
	DRM_INFO("bpp %d\n", vmw_read(dev_priv, SVGA_REG_BITS_PER_PIXEL));

	mutex_lock(&dev_priv->hw_mutex);
	dev_priv->enable_state = vmw_read(dev_priv, SVGA_REG_ENABLE);
	dev_priv->config_done_state = vmw_read(dev_priv, SVGA_REG_CONFIG_DONE);
	dev_priv->traces_state = vmw_read(dev_priv, SVGA_REG_TRACES);
	vmw_write(dev_priv, SVGA_REG_ENABLE, 1);

	min = 4;
	if (dev_priv->capabilities & SVGA_CAP_EXTENDED_FIFO)
		min = vmw_read(dev_priv, SVGA_REG_MEM_REGS);
	min <<= 2;

	if (min < PAGE_SIZE)
		min = PAGE_SIZE;

	iowrite32(min, fifo_mem + SVGA_FIFO_MIN);
	iowrite32(dev_priv->mmio_size, fifo_mem + SVGA_FIFO_MAX);
	wmb();
	iowrite32(min,  fifo_mem + SVGA_FIFO_NEXT_CMD);
	iowrite32(min,  fifo_mem + SVGA_FIFO_STOP);
	iowrite32(0, fifo_mem + SVGA_FIFO_BUSY);
	mb();

	vmw_write(dev_priv, SVGA_REG_CONFIG_DONE, 1);
	mutex_unlock(&dev_priv->hw_mutex);

	max = ioread32(fifo_mem + SVGA_FIFO_MAX);
	min = ioread32(fifo_mem  + SVGA_FIFO_MIN);
	fifo->capabilities = ioread32(fifo_mem + SVGA_FIFO_CAPABILITIES);

	DRM_INFO("Fifo max 0x%08x min 0x%08x cap 0x%08x\n",
		 (unsigned int) max,
		 (unsigned int) min,
		 (unsigned int) fifo->capabilities);

	atomic_set(&dev_priv->fence_seq, dev_priv->last_read_sequence);
	iowrite32(dev_priv->last_read_sequence, fifo_mem + SVGA_FIFO_FENCE);
	vmw_fence_queue_init(&fifo->fence_queue);
	return vmw_fifo_send_fence(dev_priv, &dummy);
out_err:
	vfree(fifo->static_buffer);
	fifo->static_buffer = NULL;
	return ret;
}

void vmw_fifo_ping_host(struct vmw_private *dev_priv, uint32_t reason)
{
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;

	mutex_lock(&dev_priv->hw_mutex);

	if (unlikely(ioread32(fifo_mem + SVGA_FIFO_BUSY) == 0)) {
		iowrite32(1, fifo_mem + SVGA_FIFO_BUSY);
		vmw_write(dev_priv, SVGA_REG_SYNC, reason);
	}

	mutex_unlock(&dev_priv->hw_mutex);
}

void vmw_fifo_release(struct vmw_private *dev_priv, struct vmw_fifo_state *fifo)
{
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;

	mutex_lock(&dev_priv->hw_mutex);

	while (vmw_read(dev_priv, SVGA_REG_BUSY) != 0)
		vmw_write(dev_priv, SVGA_REG_SYNC, SVGA_SYNC_GENERIC);

	dev_priv->last_read_sequence = ioread32(fifo_mem + SVGA_FIFO_FENCE);

	vmw_write(dev_priv, SVGA_REG_CONFIG_DONE,
		  dev_priv->config_done_state);
	vmw_write(dev_priv, SVGA_REG_ENABLE,
		  dev_priv->enable_state);
	vmw_write(dev_priv, SVGA_REG_TRACES,
		  dev_priv->traces_state);

	mutex_unlock(&dev_priv->hw_mutex);
	vmw_fence_queue_takedown(&fifo->fence_queue);

	if (likely(fifo->last_buffer != NULL)) {
		vfree(fifo->last_buffer);
		fifo->last_buffer = NULL;
	}

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
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	uint32_t max = ioread32(fifo_mem + SVGA_FIFO_MAX);
	uint32_t next_cmd = ioread32(fifo_mem + SVGA_FIFO_NEXT_CMD);
	uint32_t min = ioread32(fifo_mem + SVGA_FIFO_MIN);
	uint32_t stop = ioread32(fifo_mem + SVGA_FIFO_STOP);

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
	unsigned long irq_flags;

	if (likely(!vmw_fifo_is_full(dev_priv, bytes)))
		return 0;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_FIFOFULL);
	if (!(dev_priv->capabilities & SVGA_CAP_IRQMASK))
		return vmw_fifo_wait_noirq(dev_priv, bytes,
					   interruptible, timeout);

	mutex_lock(&dev_priv->hw_mutex);
	if (atomic_add_return(1, &dev_priv->fifo_queue_waiters) > 0) {
		spin_lock_irqsave(&dev_priv->irq_lock, irq_flags);
		outl(SVGA_IRQFLAG_FIFO_PROGRESS,
		     dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
		vmw_write(dev_priv, SVGA_REG_IRQMASK,
			  vmw_read(dev_priv, SVGA_REG_IRQMASK) |
			  SVGA_IRQFLAG_FIFO_PROGRESS);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irq_flags);
	}
	mutex_unlock(&dev_priv->hw_mutex);

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

	mutex_lock(&dev_priv->hw_mutex);
	if (atomic_dec_and_test(&dev_priv->fifo_queue_waiters)) {
		spin_lock_irqsave(&dev_priv->irq_lock, irq_flags);
		vmw_write(dev_priv, SVGA_REG_IRQMASK,
			  vmw_read(dev_priv, SVGA_REG_IRQMASK) &
			  ~SVGA_IRQFLAG_FIFO_PROGRESS);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irq_flags);
	}
	mutex_unlock(&dev_priv->hw_mutex);

	return ret;
}

void *vmw_fifo_reserve(struct vmw_private *dev_priv, uint32_t bytes)
{
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	uint32_t max;
	uint32_t min;
	uint32_t next_cmd;
	uint32_t reserveable = fifo_state->capabilities & SVGA_FIFO_CAP_RESERVE;
	int ret;

	mutex_lock(&fifo_state->fifo_mutex);
	max = ioread32(fifo_mem + SVGA_FIFO_MAX);
	min = ioread32(fifo_mem + SVGA_FIFO_MIN);
	next_cmd = ioread32(fifo_mem + SVGA_FIFO_NEXT_CMD);

	if (unlikely(bytes >= (max - min)))
		goto out_err;

	BUG_ON(fifo_state->reserved_size != 0);
	BUG_ON(fifo_state->dynamic_buffer != NULL);

	fifo_state->reserved_size = bytes;

	while (1) {
		uint32_t stop = ioread32(fifo_mem + SVGA_FIFO_STOP);
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
					iowrite32(bytes, fifo_mem +
						  SVGA_FIFO_RESERVED);
				return fifo_mem + (next_cmd >> 2);
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
				return fifo_state->dynamic_buffer;
			}
		}
	}
out_err:
	fifo_state->reserved_size = 0;
	mutex_unlock(&fifo_state->fifo_mutex);
	return NULL;
}

static void vmw_fifo_res_copy(struct vmw_fifo_state *fifo_state,
			      __le32 __iomem *fifo_mem,
			      uint32_t next_cmd,
			      uint32_t max, uint32_t min, uint32_t bytes)
{
	uint32_t chunk_size = max - next_cmd;
	uint32_t rest;
	uint32_t *buffer = (fifo_state->dynamic_buffer != NULL) ?
	    fifo_state->dynamic_buffer : fifo_state->static_buffer;

	if (bytes < chunk_size)
		chunk_size = bytes;

	iowrite32(bytes, fifo_mem + SVGA_FIFO_RESERVED);
	mb();
	memcpy_toio(fifo_mem + (next_cmd >> 2), buffer, chunk_size);
	rest = bytes - chunk_size;
	if (rest)
		memcpy_toio(fifo_mem + (min >> 2), buffer + (chunk_size >> 2),
			    rest);
}

static void vmw_fifo_slow_copy(struct vmw_fifo_state *fifo_state,
			       __le32 __iomem *fifo_mem,
			       uint32_t next_cmd,
			       uint32_t max, uint32_t min, uint32_t bytes)
{
	uint32_t *buffer = (fifo_state->dynamic_buffer != NULL) ?
	    fifo_state->dynamic_buffer : fifo_state->static_buffer;

	while (bytes > 0) {
		iowrite32(*buffer++, fifo_mem + (next_cmd >> 2));
		next_cmd += sizeof(uint32_t);
		if (unlikely(next_cmd == max))
			next_cmd = min;
		mb();
		iowrite32(next_cmd, fifo_mem + SVGA_FIFO_NEXT_CMD);
		mb();
		bytes -= sizeof(uint32_t);
	}
}

void vmw_fifo_commit(struct vmw_private *dev_priv, uint32_t bytes)
{
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	uint32_t next_cmd = ioread32(fifo_mem + SVGA_FIFO_NEXT_CMD);
	uint32_t max = ioread32(fifo_mem + SVGA_FIFO_MAX);
	uint32_t min = ioread32(fifo_mem + SVGA_FIFO_MIN);
	bool reserveable = fifo_state->capabilities & SVGA_FIFO_CAP_RESERVE;

	BUG_ON((bytes & 3) != 0);
	BUG_ON(bytes > fifo_state->reserved_size);

	fifo_state->reserved_size = 0;

	if (fifo_state->using_bounce_buffer) {
		if (reserveable)
			vmw_fifo_res_copy(fifo_state, fifo_mem,
					  next_cmd, max, min, bytes);
		else
			vmw_fifo_slow_copy(fifo_state, fifo_mem,
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
		iowrite32(next_cmd, fifo_mem + SVGA_FIFO_NEXT_CMD);
	}

	if (reserveable)
		iowrite32(0, fifo_mem + SVGA_FIFO_RESERVED);
	mb();
	up_write(&fifo_state->rwsem);
	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
	mutex_unlock(&fifo_state->fifo_mutex);
}

int vmw_fifo_send_fence(struct vmw_private *dev_priv, uint32_t *sequence)
{
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;
	struct svga_fifo_cmd_fence *cmd_fence;
	void *fm;
	int ret = 0;
	uint32_t bytes = sizeof(__le32) + sizeof(*cmd_fence);

	fm = vmw_fifo_reserve(dev_priv, bytes);
	if (unlikely(fm == NULL)) {
		*sequence = atomic_read(&dev_priv->fence_seq);
		ret = -ENOMEM;
		(void)vmw_fallback_wait(dev_priv, false, true, *sequence,
					false, 3*HZ);
		goto out_err;
	}

	do {
		*sequence = atomic_add_return(1, &dev_priv->fence_seq);
	} while (*sequence == 0);

	if (!(fifo_state->capabilities & SVGA_FIFO_CAP_FENCE)) {

		/*
		 * Don't request hardware to send a fence. The
		 * waiting code in vmwgfx_irq.c will emulate this.
		 */

		vmw_fifo_commit(dev_priv, 0);
		return 0;
	}

	*(__le32 *) fm = cpu_to_le32(SVGA_CMD_FENCE);
	cmd_fence = (struct svga_fifo_cmd_fence *)
	    ((unsigned long)fm + sizeof(__le32));

	iowrite32(*sequence, &cmd_fence->fence);
	fifo_state->last_buffer_add = true;
	vmw_fifo_commit(dev_priv, bytes);
	fifo_state->last_buffer_add = false;
	(void) vmw_fence_push(&fifo_state->fence_queue, *sequence);
	vmw_update_sequence(dev_priv, fifo_state);

out_err:
	return ret;
}

/**
 * Map the first page of the FIFO read-only to user-space.
 */

static int vmw_fifo_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int ret;
	unsigned long address = (unsigned long)vmf->virtual_address;

	if (address != vma->vm_start)
		return VM_FAULT_SIGBUS;

	ret = vm_insert_pfn(vma, address, vma->vm_pgoff);
	if (likely(ret == -EBUSY || ret == 0))
		return VM_FAULT_NOPAGE;
	else if (ret == -ENOMEM)
		return VM_FAULT_OOM;

	return VM_FAULT_SIGBUS;
}

static struct vm_operations_struct vmw_fifo_vm_ops = {
	.fault = vmw_fifo_vm_fault,
	.open = NULL,
	.close = NULL
};

int vmw_fifo_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct vmw_private *dev_priv;

	file_priv = (struct drm_file *)filp->private_data;
	dev_priv = vmw_priv(file_priv->minor->dev);

	if (vma->vm_pgoff != (dev_priv->mmio_start >> PAGE_SHIFT) ||
	    (vma->vm_end - vma->vm_start) != PAGE_SIZE)
		return -EINVAL;

	vma->vm_flags &= ~(VM_WRITE | VM_MAYWRITE);
	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_SHARED;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	vma->vm_page_prot = ttm_io_prot(TTM_PL_FLAG_UNCACHED,
					vma->vm_page_prot);
	vma->vm_ops = &vmw_fifo_vm_ops;
	return 0;
}
