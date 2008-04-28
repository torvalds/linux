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

static void radeon_irq_set_state(struct drm_device *dev, u32 mask, int state)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if (state)
		dev_priv->irq_enable_reg |= mask;
	else
		dev_priv->irq_enable_reg &= ~mask;

	RADEON_WRITE(RADEON_GEN_INT_CNTL, dev_priv->irq_enable_reg);
}

int radeon_enable_vblank(struct drm_device *dev, int crtc)
{
	switch (crtc) {
	case 0:
		radeon_irq_set_state(dev, RADEON_CRTC_VBLANK_MASK, 1);
		break;
	case 1:
		radeon_irq_set_state(dev, RADEON_CRTC2_VBLANK_MASK, 1);
		break;
	default:
		DRM_ERROR("tried to enable vblank on non-existent crtc %d\n",
			  crtc);
		return EINVAL;
	}

	return 0;
}

void radeon_disable_vblank(struct drm_device *dev, int crtc)
{
	switch (crtc) {
	case 0:
		radeon_irq_set_state(dev, RADEON_CRTC_VBLANK_MASK, 0);
		break;
	case 1:
		radeon_irq_set_state(dev, RADEON_CRTC2_VBLANK_MASK, 0);
		break;
	default:
		DRM_ERROR("tried to enable vblank on non-existent crtc %d\n",
			  crtc);
		break;
	}
}

static __inline__ u32 radeon_acknowledge_irqs(drm_radeon_private_t * dev_priv)
{
	u32 irqs = RADEON_READ(RADEON_GEN_INT_STATUS) &
		(RADEON_SW_INT_TEST | RADEON_CRTC_VBLANK_STAT |
		 RADEON_CRTC2_VBLANK_STAT);

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
	stat = radeon_acknowledge_irqs(dev_priv);
	if (!stat)
		return IRQ_NONE;

	stat &= dev_priv->irq_enable_reg;

	/* SW interrupt */
	if (stat & RADEON_SW_INT_TEST)
		DRM_WAKEUP(&dev_priv->swi_queue);

	/* VBLANK interrupt */
	if (stat & RADEON_CRTC_VBLANK_STAT)
		drm_handle_vblank(dev, 0);
	if (stat & RADEON_CRTC2_VBLANK_STAT)
		drm_handle_vblank(dev, 1);

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

u32 radeon_get_vblank_counter(struct drm_device *dev, int crtc)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32 crtc_cnt_reg, crtc_status_reg;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (crtc == 0) {
		crtc_cnt_reg = RADEON_CRTC_CRNT_FRAME;
		crtc_status_reg = RADEON_CRTC_STATUS;
	} else if (crtc == 1) {
		crtc_cnt_reg = RADEON_CRTC2_CRNT_FRAME;
		crtc_status_reg = RADEON_CRTC2_STATUS;
	} else {
		return -EINVAL;
	}

	return RADEON_READ(crtc_cnt_reg) + (RADEON_READ(crtc_status_reg) & 1);
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

/* drm_dma.h hooks
*/
void radeon_driver_irq_preinstall(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;

	/* Disable *all* interrupts */
	RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);

	/* Clear bits if they're already high */
	radeon_acknowledge_irqs(dev_priv);
}

int radeon_driver_irq_postinstall(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	int ret;

	atomic_set(&dev_priv->swi_emitted, 0);
	DRM_INIT_WAITQUEUE(&dev_priv->swi_queue);

	ret = drm_vblank_init(dev, 2);
	if (ret)
		return ret;

	dev->max_vblank_count = 0x001fffff;

	radeon_irq_set_state(dev, RADEON_SW_INT_ENABLE, 1);

	return 0;
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
	return 0;
}
