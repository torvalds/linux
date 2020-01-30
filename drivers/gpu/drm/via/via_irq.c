/* via_irq.c
 *
 * Copyright 2004 BEAM Ltd.
 * Copyright 2002 Tungsten Graphics, Inc.
 * Copyright 2005 Thomas Hellstrom.
 * All Rights Reserved.
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
 * BEAM LTD, TUNGSTEN GRAPHICS  AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Terry Barnaby <terry1@beam.ltd.uk>
 *    Keith Whitwell <keith@tungstengraphics.com>
 *    Thomas Hellstrom <unichrome@shipmail.org>
 *
 * This code provides standard DRM access to the Via Unichrome / Pro Vertical blank
 * interrupt, as well as an infrastructure to handle other interrupts of the chip.
 * The refresh rate is also calculated for video playback sync purposes.
 */

#include <drm/drm_device.h>
#include <drm/drm_vblank.h>
#include <drm/via_drm.h>

#include "via_drv.h"

#define VIA_REG_INTERRUPT       0x200

/* VIA_REG_INTERRUPT */
#define VIA_IRQ_GLOBAL	  (1 << 31)
#define VIA_IRQ_VBLANK_ENABLE   (1 << 19)
#define VIA_IRQ_VBLANK_PENDING  (1 << 3)
#define VIA_IRQ_HQV0_ENABLE     (1 << 11)
#define VIA_IRQ_HQV1_ENABLE     (1 << 25)
#define VIA_IRQ_HQV0_PENDING    (1 << 9)
#define VIA_IRQ_HQV1_PENDING    (1 << 10)
#define VIA_IRQ_DMA0_DD_ENABLE  (1 << 20)
#define VIA_IRQ_DMA0_TD_ENABLE  (1 << 21)
#define VIA_IRQ_DMA1_DD_ENABLE  (1 << 22)
#define VIA_IRQ_DMA1_TD_ENABLE  (1 << 23)
#define VIA_IRQ_DMA0_DD_PENDING (1 << 4)
#define VIA_IRQ_DMA0_TD_PENDING (1 << 5)
#define VIA_IRQ_DMA1_DD_PENDING (1 << 6)
#define VIA_IRQ_DMA1_TD_PENDING (1 << 7)


/*
 * Device-specific IRQs go here. This type might need to be extended with
 * the register if there are multiple IRQ control registers.
 * Currently we activate the HQV interrupts of  Unichrome Pro group A.
 */

static maskarray_t via_pro_group_a_irqs[] = {
	{VIA_IRQ_HQV0_ENABLE, VIA_IRQ_HQV0_PENDING, 0x000003D0, 0x00008010,
	 0x00000000 },
	{VIA_IRQ_HQV1_ENABLE, VIA_IRQ_HQV1_PENDING, 0x000013D0, 0x00008010,
	 0x00000000 },
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
};
static int via_num_pro_group_a = ARRAY_SIZE(via_pro_group_a_irqs);
static int via_irqmap_pro_group_a[] = {0, 1, -1, 2, -1, 3};

static maskarray_t via_unichrome_irqs[] = {
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008}
};
static int via_num_unichrome = ARRAY_SIZE(via_unichrome_irqs);
static int via_irqmap_unichrome[] = {-1, -1, -1, 0, -1, 1};


u32 via_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	if (pipe != 0)
		return 0;

	return atomic_read(&dev_priv->vbl_received);
}

irqreturn_t via_driver_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;
	int handled = 0;
	ktime_t cur_vblank;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int i;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	if (status & VIA_IRQ_VBLANK_PENDING) {
		atomic_inc(&dev_priv->vbl_received);
		if (!(atomic_read(&dev_priv->vbl_received) & 0x0F)) {
			cur_vblank = ktime_get();
			if (dev_priv->last_vblank_valid) {
				dev_priv->nsec_per_vblank =
					ktime_sub(cur_vblank,
						dev_priv->last_vblank) >> 4;
			}
			dev_priv->last_vblank = cur_vblank;
			dev_priv->last_vblank_valid = 1;
		}
		if (!(atomic_read(&dev_priv->vbl_received) & 0xFF)) {
			DRM_DEBUG("nsec per vblank is: %llu\n",
				  ktime_to_ns(dev_priv->nsec_per_vblank));
		}
		drm_handle_vblank(dev, 0);
		handled = 1;
	}

	for (i = 0; i < dev_priv->num_irqs; ++i) {
		if (status & cur_irq->pending_mask) {
			atomic_inc(&cur_irq->irq_received);
			wake_up(&cur_irq->irq_queue);
			handled = 1;
			if (dev_priv->irq_map[drm_via_irq_dma0_td] == i)
				via_dmablit_handler(dev, 0, 1);
			else if (dev_priv->irq_map[drm_via_irq_dma1_td] == i)
				via_dmablit_handler(dev, 1, 1);
		}
		cur_irq++;
	}

	/* Acknowledge interrupts */
	via_write(dev_priv, VIA_REG_INTERRUPT, status);


	if (handled)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static __inline__ void viadrv_acknowledge_irqs(drm_via_private_t *dev_priv)
{
	u32 status;

	if (dev_priv) {
		/* Acknowledge interrupts */
		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status |
			  dev_priv->irq_pending_mask);
	}
}

int via_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	u32 status;

	if (pipe != 0) {
		DRM_ERROR("%s:  bad crtc %u\n", __func__, pipe);
		return -EINVAL;
	}

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status | VIA_IRQ_VBLANK_ENABLE);

	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0x30);

	return 0;
}

void via_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	u32 status;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status & ~VIA_IRQ_VBLANK_ENABLE);

	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0);

	if (pipe != 0)
		DRM_ERROR("%s:  bad crtc %u\n", __func__, pipe);
}

static int
via_driver_irq_wait(struct drm_device *dev, unsigned int irq, int force_sequence,
		    unsigned int *sequence)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	unsigned int cur_irq_sequence;
	drm_via_irq_t *cur_irq;
	int ret = 0;
	maskarray_t *masks;
	int real_irq;

	DRM_DEBUG("\n");

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (irq >= drm_via_irq_num) {
		DRM_ERROR("Trying to wait on unknown irq %d\n", irq);
		return -EINVAL;
	}

	real_irq = dev_priv->irq_map[irq];

	if (real_irq < 0) {
		DRM_ERROR("Video IRQ %d not available on this hardware.\n",
			  irq);
		return -EINVAL;
	}

	masks = dev_priv->irq_masks;
	cur_irq = dev_priv->via_irqs + real_irq;

	if (masks[real_irq][2] && !force_sequence) {
		VIA_WAIT_ON(ret, cur_irq->irq_queue, 3 * HZ,
			    ((via_read(dev_priv, masks[irq][2]) & masks[irq][3]) ==
			     masks[irq][4]));
		cur_irq_sequence = atomic_read(&cur_irq->irq_received);
	} else {
		VIA_WAIT_ON(ret, cur_irq->irq_queue, 3 * HZ,
			    (((cur_irq_sequence =
			       atomic_read(&cur_irq->irq_received)) -
			      *sequence) <= (1 << 23)));
	}
	*sequence = cur_irq_sequence;
	return ret;
}


/*
 * drm_dma.h hooks
 */

void via_driver_irq_preinstall(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;
	drm_via_irq_t *cur_irq;
	int i;

	DRM_DEBUG("dev_priv: %p\n", dev_priv);
	if (dev_priv) {
		cur_irq = dev_priv->via_irqs;

		dev_priv->irq_enable_mask = VIA_IRQ_VBLANK_ENABLE;
		dev_priv->irq_pending_mask = VIA_IRQ_VBLANK_PENDING;

		if (dev_priv->chipset == VIA_PRO_GROUP_A ||
		    dev_priv->chipset == VIA_DX9_0) {
			dev_priv->irq_masks = via_pro_group_a_irqs;
			dev_priv->num_irqs = via_num_pro_group_a;
			dev_priv->irq_map = via_irqmap_pro_group_a;
		} else {
			dev_priv->irq_masks = via_unichrome_irqs;
			dev_priv->num_irqs = via_num_unichrome;
			dev_priv->irq_map = via_irqmap_unichrome;
		}

		for (i = 0; i < dev_priv->num_irqs; ++i) {
			atomic_set(&cur_irq->irq_received, 0);
			cur_irq->enable_mask = dev_priv->irq_masks[i][0];
			cur_irq->pending_mask = dev_priv->irq_masks[i][1];
			init_waitqueue_head(&cur_irq->irq_queue);
			dev_priv->irq_enable_mask |= cur_irq->enable_mask;
			dev_priv->irq_pending_mask |= cur_irq->pending_mask;
			cur_irq++;

			DRM_DEBUG("Initializing IRQ %d\n", i);
		}

		dev_priv->last_vblank_valid = 0;

		/* Clear VSync interrupt regs */
		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status &
			  ~(dev_priv->irq_enable_mask));

		/* Clear bits if they're already high */
		viadrv_acknowledge_irqs(dev_priv);
	}
}

int via_driver_irq_postinstall(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("via_driver_irq_postinstall\n");
	if (!dev_priv)
		return -EINVAL;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status | VIA_IRQ_GLOBAL
		  | dev_priv->irq_enable_mask);

	/* Some magic, oh for some data sheets ! */
	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0x30);

	return 0;
}

void via_driver_irq_uninstall(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("\n");
	if (dev_priv) {

		/* Some more magic, oh for some data sheets ! */

		via_write8(dev_priv, 0x83d4, 0x11);
		via_write8_mask(dev_priv, 0x83d5, 0x30, 0);

		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status &
			  ~(VIA_IRQ_VBLANK_ENABLE | dev_priv->irq_enable_mask));
	}
}

int via_wait_irq(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_irqwait_t *irqwait = data;
	struct timespec64 now;
	int ret = 0;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int force_sequence;

	if (irqwait->request.irq >= dev_priv->num_irqs) {
		DRM_ERROR("Trying to wait on unknown irq %d\n",
			  irqwait->request.irq);
		return -EINVAL;
	}

	cur_irq += irqwait->request.irq;

	switch (irqwait->request.type & ~VIA_IRQ_FLAGS_MASK) {
	case VIA_IRQ_RELATIVE:
		irqwait->request.sequence +=
			atomic_read(&cur_irq->irq_received);
		irqwait->request.type &= ~_DRM_VBLANK_RELATIVE;
	case VIA_IRQ_ABSOLUTE:
		break;
	default:
		return -EINVAL;
	}

	if (irqwait->request.type & VIA_IRQ_SIGNAL) {
		DRM_ERROR("Signals on Via IRQs not implemented yet.\n");
		return -EINVAL;
	}

	force_sequence = (irqwait->request.type & VIA_IRQ_FORCE_SEQUENCE);

	ret = via_driver_irq_wait(dev, irqwait->request.irq, force_sequence,
				  &irqwait->request.sequence);
	ktime_get_ts64(&now);
	irqwait->reply.tval_sec = now.tv_sec;
	irqwait->reply.tval_usec = now.tv_nsec / NSEC_PER_USEC;

	return ret;
}
