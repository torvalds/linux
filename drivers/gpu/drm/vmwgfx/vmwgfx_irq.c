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

#define VMW_FENCE_WRAP (1 << 24)

irqreturn_t vmw_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct vmw_private *dev_priv = vmw_priv(dev);
	uint32_t status;

	spin_lock(&dev_priv->irq_lock);
	status = inl(dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
	spin_unlock(&dev_priv->irq_lock);

	if (status & SVGA_IRQFLAG_ANY_FENCE)
		wake_up_all(&dev_priv->fence_queue);
	if (status & SVGA_IRQFLAG_FIFO_PROGRESS)
		wake_up_all(&dev_priv->fifo_queue);

	if (likely(status)) {
		outl(status, dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static bool vmw_fifo_idle(struct vmw_private *dev_priv, uint32_t sequence)
{
	uint32_t busy;

	mutex_lock(&dev_priv->hw_mutex);
	busy = vmw_read(dev_priv, SVGA_REG_BUSY);
	mutex_unlock(&dev_priv->hw_mutex);

	return (busy == 0);
}


bool vmw_fence_signaled(struct vmw_private *dev_priv,
			uint32_t sequence)
{
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	struct vmw_fifo_state *fifo_state;
	bool ret;

	if (likely(dev_priv->last_read_sequence - sequence < VMW_FENCE_WRAP))
		return true;

	dev_priv->last_read_sequence = ioread32(fifo_mem + SVGA_FIFO_FENCE);
	if (likely(dev_priv->last_read_sequence - sequence < VMW_FENCE_WRAP))
		return true;

	fifo_state = &dev_priv->fifo;
	if (!(fifo_state->capabilities & SVGA_FIFO_CAP_FENCE) &&
	    vmw_fifo_idle(dev_priv, sequence))
		return true;

	/**
	 * Then check if the sequence is higher than what we've actually
	 * emitted. Then the fence is stale and signaled.
	 */

	ret = ((atomic_read(&dev_priv->fence_seq) - sequence)
	       > VMW_FENCE_WRAP);

	return ret;
}

int vmw_fallback_wait(struct vmw_private *dev_priv,
		      bool lazy,
		      bool fifo_idle,
		      uint32_t sequence,
		      bool interruptible,
		      unsigned long timeout)
{
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;

	uint32_t count = 0;
	uint32_t signal_seq;
	int ret;
	unsigned long end_jiffies = jiffies + timeout;
	bool (*wait_condition)(struct vmw_private *, uint32_t);
	DEFINE_WAIT(__wait);

	wait_condition = (fifo_idle) ? &vmw_fifo_idle :
		&vmw_fence_signaled;

	/**
	 * Block command submission while waiting for idle.
	 */

	if (fifo_idle)
		down_read(&fifo_state->rwsem);
	signal_seq = atomic_read(&dev_priv->fence_seq);
	ret = 0;

	for (;;) {
		prepare_to_wait(&dev_priv->fence_queue, &__wait,
				(interruptible) ?
				TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		if (wait_condition(dev_priv, sequence))
			break;
		if (time_after_eq(jiffies, end_jiffies)) {
			DRM_ERROR("SVGA device lockup.\n");
			break;
		}
		if (lazy)
			schedule_timeout(1);
		else if ((++count & 0x0F) == 0) {
			/**
			 * FIXME: Use schedule_hr_timeout here for
			 * newer kernels and lower CPU utilization.
			 */

			__set_current_state(TASK_RUNNING);
			schedule();
			__set_current_state((interruptible) ?
					    TASK_INTERRUPTIBLE :
					    TASK_UNINTERRUPTIBLE);
		}
		if (interruptible && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	finish_wait(&dev_priv->fence_queue, &__wait);
	if (ret == 0 && fifo_idle) {
		__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
		iowrite32(signal_seq, fifo_mem + SVGA_FIFO_FENCE);
	}
	wake_up_all(&dev_priv->fence_queue);
	if (fifo_idle)
		up_read(&fifo_state->rwsem);

	return ret;
}

int vmw_wait_fence(struct vmw_private *dev_priv,
		   bool lazy, uint32_t sequence,
		   bool interruptible, unsigned long timeout)
{
	long ret;
	unsigned long irq_flags;
	struct vmw_fifo_state *fifo = &dev_priv->fifo;

	if (likely(dev_priv->last_read_sequence - sequence < VMW_FENCE_WRAP))
		return 0;

	if (likely(vmw_fence_signaled(dev_priv, sequence)))
		return 0;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);

	if (!(fifo->capabilities & SVGA_FIFO_CAP_FENCE))
		return vmw_fallback_wait(dev_priv, lazy, true, sequence,
					 interruptible, timeout);

	if (!(dev_priv->capabilities & SVGA_CAP_IRQMASK))
		return vmw_fallback_wait(dev_priv, lazy, false, sequence,
					 interruptible, timeout);

	mutex_lock(&dev_priv->hw_mutex);
	if (atomic_add_return(1, &dev_priv->fence_queue_waiters) > 0) {
		spin_lock_irqsave(&dev_priv->irq_lock, irq_flags);
		outl(SVGA_IRQFLAG_ANY_FENCE,
		     dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
		vmw_write(dev_priv, SVGA_REG_IRQMASK,
			  vmw_read(dev_priv, SVGA_REG_IRQMASK) |
			  SVGA_IRQFLAG_ANY_FENCE);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irq_flags);
	}
	mutex_unlock(&dev_priv->hw_mutex);

	if (interruptible)
		ret = wait_event_interruptible_timeout
		    (dev_priv->fence_queue,
		     vmw_fence_signaled(dev_priv, sequence),
		     timeout);
	else
		ret = wait_event_timeout
		    (dev_priv->fence_queue,
		     vmw_fence_signaled(dev_priv, sequence),
		     timeout);

	if (unlikely(ret == 0))
		ret = -EBUSY;
	else if (likely(ret > 0))
		ret = 0;

	mutex_lock(&dev_priv->hw_mutex);
	if (atomic_dec_and_test(&dev_priv->fence_queue_waiters)) {
		spin_lock_irqsave(&dev_priv->irq_lock, irq_flags);
		vmw_write(dev_priv, SVGA_REG_IRQMASK,
			  vmw_read(dev_priv, SVGA_REG_IRQMASK) &
			  ~SVGA_IRQFLAG_ANY_FENCE);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irq_flags);
	}
	mutex_unlock(&dev_priv->hw_mutex);

	return ret;
}

void vmw_irq_preinstall(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	uint32_t status;

	if (!(dev_priv->capabilities & SVGA_CAP_IRQMASK))
		return;

	spin_lock_init(&dev_priv->irq_lock);
	status = inl(dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
	outl(status, dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
}

int vmw_irq_postinstall(struct drm_device *dev)
{
	return 0;
}

void vmw_irq_uninstall(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	uint32_t status;

	if (!(dev_priv->capabilities & SVGA_CAP_IRQMASK))
		return;

	mutex_lock(&dev_priv->hw_mutex);
	vmw_write(dev_priv, SVGA_REG_IRQMASK, 0);
	mutex_unlock(&dev_priv->hw_mutex);

	status = inl(dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
	outl(status, dev_priv->io_start + VMWGFX_IRQSTATUS_PORT);
}

#define VMW_FENCE_WAIT_TIMEOUT 3*HZ;

int vmw_fence_wait_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_vmw_fence_wait_arg *arg =
	    (struct drm_vmw_fence_wait_arg *)data;
	unsigned long timeout;

	if (!arg->cookie_valid) {
		arg->cookie_valid = 1;
		arg->kernel_cookie = jiffies + VMW_FENCE_WAIT_TIMEOUT;
	}

	timeout = jiffies;
	if (time_after_eq(timeout, (unsigned long)arg->kernel_cookie))
		return -EBUSY;

	timeout = (unsigned long)arg->kernel_cookie - timeout;
	return vmw_wait_fence(vmw_priv(dev), true, arg->sequence, true, timeout);
}
