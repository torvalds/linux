/* radeon_irq.c -- IRQ handling for radeon -*- linux-c -*- */
/*
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 *    Michel Dänzer <michel@daenzer.net>
 */

#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"

static __inline__ u32 radeon_acknowledge_irqs(drm_radeon_private_t * dev_priv,
					      u32 mask)
{
	u32 irqs = RADEON_READ(RADEON_GEN_INT_STATUS) & mask;
	if (irqs)
		RADEON_WRITE(RADEON_GEN_INT_STATUS, irqs);
	return irqs;
}

/* Interrupts - Used for device synchronization and flushing in the
 * following circumstances:
 *
 * - Exclusive FB access with hw idle:
 *    - Wait for GUI Idle (?) interrupt, then do normal flush.
 *
 * - Frame throttling, NV_fence:
 *    - Drop marker irq's into command stream ahead of time.
 *    - Wait on irq's with lock *not held*
 *    - Check each for termination condition
 *
 * - Internally in cp_getbuffer, etc:
 *    - as above, but wait with lock held???
 *
 * NOTE: These functions are misleadingly named -- the irq's aren't
 * tied to dma at all, this is just a hangover from dri prehistory.
 */

irqreturn_t radeon_driver_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	u32 stat;

	/* Only consider the bits we're interested in - others could be used
	 * outside the DRM
	 */
	stat = radeon_acknowledge_irqs(dev_priv, (RADEON_SW_INT_TEST_ACK |
						  RADEON_CRTC_VBLANK_STAT |
						  RADEON_CRTC2_VBLANK_STAT));
	if (!stat)
		return IRQ_NONE;

	stat &= dev_priv->irq_enable_reg;

	/* SW interrupt */
	if (stat & RADEON_SW_INT_TEST) {
		DRM_WAKEUP(&dev_priv->swi_queue);
	}

	/* VBLANK interrupt */
	if (stat & (RADEON_CRTC_VBLANK_STAT|RADEON_CRTC2_VBLANK_STAT)) {
		int vblank_crtc = dev_priv->vblank_crtc;

		if ((vblank_crtc &
		     (DRM_RADEON_VBLANK_CRTC1 | DRM_RADEON_VBLANK_CRTC2)) ==
		    (DRM_RADEON_VBLANK_CRTC1 | DRM_RADEON_VBLANK_CRTC2)) {
			if (stat & RADEON_CRTC_VBLANK_STAT)
				atomic_inc(&dev->vbl_received);
			if (stat & RADEON_CRTC2_VBLANK_STAT)
				atomic_inc(&dev->vbl_received2);
		} else if (((stat & RADEON_CRTC_VBLANK_STAT) &&
			   (vblank_crtc & DRM_RADEON_VBLANK_CRTC1)) ||
			   ((stat & RADEON_CRTC2_VBLANK_STAT) &&
			    (vblank_crtc & DRM_RADEON_VBLANK_CRTC2)))
			atomic_inc(&dev->vbl_received);

		DRM_WAKEUP(&dev->vbl_queue);
		drm_vbl_send_signals(dev);
	}

	return IRQ_HANDLED;
}

static int radeon_emit_irq(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	unsigned int ret;
	RING_LOCALS;

	atomic_inc(&dev_priv->swi_emitted);
	ret = atomic_read(&dev_priv->swi_emitted);

	BEGIN_RING(4);
	OUT_RING_REG(RADEON_LAST_SWI_REG, ret);
	OUT_RING_REG(RADEON_GEN_INT_STATUS, RADEON_SW_INT_FIRE);
	ADVANCE_RING();
	COMMIT_RING();

	return ret;
}

static int radeon_wait_irq(struct drm_device * dev, int swi_nr)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	int ret = 0;

	if (RADEON_READ(RADEON_LAST_SWI_REG) >= swi_nr)
		return 0;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	DRM_WAIT_ON(ret, dev_priv->swi_queue, 3 * DRM_HZ,
		    RADEON_READ(RADEON_LAST_SWI_REG) >= swi_nr);

	return ret;
}

static int radeon_driver_vblank_do_wait(struct drm_device * dev,
					unsigned int *sequence, int crtc)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	unsigned int cur_vblank;
	int ret = 0;
	int ack = 0;
	atomic_t *counter;
	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (crtc == DRM_RADEON_VBLANK_CRTC1) {
		counter = &dev->vbl_received;
		ack |= RADEON_CRTC_VBLANK_STAT;
	} else if (crtc == DRM_RADEON_VBLANK_CRTC2) {
		counter = &dev->vbl_received2;
		ack |= RADEON_CRTC2_VBLANK_STAT;
	} else
		return -EINVAL;

	radeon_acknowledge_irqs(dev_priv, ack);

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	/* Assume that the user has missed the current sequence number
	 * by about a day rather than she wants to wait for years
	 * using vertical blanks...
	 */
	DRM_WAIT_ON(ret, dev->vbl_queue, 3 * DRM_HZ,
		    (((cur_vblank = atomic_read(counter))
		      - *sequence) <= (1 << 23)));

	*sequence = cur_vblank;

	return ret;
}

int radeon_driver_vblank_wait(struct drm_device *dev, unsigned int *sequence)
{
	return radeon_driver_vblank_do_wait(dev, sequence, DRM_RADEON_VBLANK_CRTC1);
}

int radeon_driver_vblank_wait2(struct drm_device *dev, unsigned int *sequence)
{
	return radeon_driver_vblank_do_wait(dev, sequence, DRM_RADEON_VBLANK_CRTC2);
}

/* Needs the lock as it touches the ring.
 */
int radeon_irq_emit(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_irq_emit_t *emit = data;
	int result;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	result = radeon_emit_irq(dev);

	if (DRM_COPY_TO_USER(emit->irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

/* Doesn't need the hardware lock.
 */
int radeon_irq_wait(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_irq_wait_t *irqwait = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return radeon_wait_irq(dev, irqwait->irq_seq);
}

static void radeon_enable_interrupt(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = (drm_radeon_private_t *) dev->dev_private;

	dev_priv->irq_enable_reg = RADEON_SW_INT_ENABLE;
	if (dev_priv->vblank_crtc & DRM_RADEON_VBLANK_CRTC1)
		dev_priv->irq_enable_reg |= RADEON_CRTC_VBLANK_MASK;

	if (dev_priv->vblank_crtc & DRM_RADEON_VBLANK_CRTC2)
		dev_priv->irq_enable_reg |= RADEON_CRTC2_VBLANK_MASK;

	RADEON_WRITE(RADEON_GEN_INT_CNTL, dev_priv->irq_enable_reg);
	dev_priv->irq_enabled = 1;
}

/* drm_dma.h hooks
*/
void radeon_driver_irq_preinstall(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;

	/* Disable *all* interrupts */
	RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);

	/* Clear bits if they're already high */
	radeon_acknowledge_irqs(dev_priv, (RADEON_SW_INT_TEST_ACK |
					   RADEON_CRTC_VBLANK_STAT |
					   RADEON_CRTC2_VBLANK_STAT));
}

void radeon_driver_irq_postinstall(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;

	atomic_set(&dev_priv->swi_emitted, 0);
	DRM_INIT_WAITQUEUE(&dev_priv->swi_queue);

	radeon_enable_interrupt(dev);
}

void radeon_driver_irq_uninstall(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	if (!dev_priv)
		return;

	dev_priv->irq_enabled = 0;

	/* Disable *all* interrupts */
	RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);
}


int radeon_vblank_crtc_get(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = (drm_radeon_private_t *) dev->dev_private;
	u32 flag;
	u32 value;

	flag = RADEON_READ(RADEON_GEN_INT_CNTL);
	value = 0;

	if (flag & RADEON_CRTC_VBLANK_MASK)
		value |= DRM_RADEON_VBLANK_CRTC1;

	if (flag & RADEON_CRTC2_VBLANK_MASK)
		value |= DRM_RADEON_VBLANK_CRTC2;
	return value;
}

int radeon_vblank_crtc_set(struct drm_device *dev, int64_t value)
{
	drm_radeon_private_t *dev_priv = (drm_radeon_private_t *) dev->dev_private;
	if (value & ~(DRM_RADEON_VBLANK_CRTC1 | DRM_RADEON_VBLANK_CRTC2)) {
		DRM_ERROR("called with invalid crtc 0x%x\n", (unsigned int)value);
		return -EINVAL;
	}
	dev_priv->vblank_crtc = (unsigned int)value;
	radeon_enable_interrupt(dev);
	return 0;
}
