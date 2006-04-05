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

#include "drmP.h"
#include "drm.h"
#include "via_drm.h"
#include "via_drv.h"

#define VIA_REG_INTERRUPT       0x200

/* VIA_REG_INTERRUPT */
#define VIA_IRQ_GLOBAL          (1 << 31)
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
	 0x00000000},
	{VIA_IRQ_HQV1_ENABLE, VIA_IRQ_HQV1_PENDING, 0x000013D0, 0x00008010,
	 0x00000000},
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
};
static int via_num_pro_group_a =
    sizeof(via_pro_group_a_irqs) / sizeof(maskarray_t);
static int via_irqmap_pro_group_a[] = {0, 1, -1, 2, -1, 3};

static maskarray_t via_unichrome_irqs[] = {
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008}
};
static int via_num_unichrome = sizeof(via_unichrome_irqs) / sizeof(maskarray_t);
static int via_irqmap_unichrome[] = {-1, -1, -1, 0, -1, 1};

static unsigned time_diff(struct timeval *now, struct timeval *then)
{
	return (now->tv_usec >= then->tv_usec) ?
	    now->tv_usec - then->tv_usec :
	    1000000 - (then->tv_usec - now->tv_usec);
}

irqreturn_t via_driver_irq_handler(DRM_IRQ_ARGS)
{
	drm_device_t *dev = (drm_device_t *) arg;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;
	int handled = 0;
	struct timeval cur_vblank;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int i;

	status = VIA_READ(VIA_REG_INTERRUPT);
	if (status & VIA_IRQ_VBLANK_PENDING) {
		atomic_inc(&dev->vbl_received);
		if (!(atomic_read(&dev->vbl_received) & 0x0F)) {
			do_gettimeofday(&cur_vblank);
			if (dev_priv->last_vblank_valid) {
				dev_priv->usec_per_vblank =
				    time_diff(&cur_vblank,
					      &dev_priv->last_vblank) >> 4;
			}
			dev_priv->last_vblank = cur_vblank;
			dev_priv->last_vblank_valid = 1;
		}
		if (!(atomic_read(&dev->vbl_received) & 0xFF)) {
			DRM_DEBUG("US per vblank is: %u\n",
				  dev_priv->usec_per_vblank);
		}
		DRM_WAKEUP(&dev->vbl_queue);
		drm_vbl_send_signals(dev);
		handled = 1;
	}

	for (i = 0; i < dev_priv->num_irqs; ++i) {
		if (status & cur_irq->pending_mask) {
			atomic_inc(&cur_irq->irq_received);
			DRM_WAKEUP(&cur_irq->irq_queue);
			handled = 1;
			if (dev_priv->irq_map[drm_via_irq_dma0_td] == i) {
				via_dmablit_handler(dev, 0, 1);
			} else if (dev_priv->irq_map[drm_via_irq_dma1_td] == i) {
				via_dmablit_handler(dev, 1, 1);
			}
		}
		cur_irq++;
	}

	/* Acknowlege interrupts */
	VIA_WRITE(VIA_REG_INTERRUPT, status);

	if (handled)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static __inline__ void viadrv_acknowledge_irqs(drm_via_private_t * dev_priv)
{
	u32 status;

	if (dev_priv) {
		/* Acknowlege interrupts */
		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status |
			  dev_priv->irq_pending_mask);
	}
}

int via_driver_vblank_wait(drm_device_t * dev, unsigned int *sequence)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	unsigned int cur_vblank;
	int ret = 0;

	DRM_DEBUG("viadrv_vblank_wait\n");
	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return -EINVAL;
	}

	viadrv_acknowledge_irqs(dev_priv);

	/* Assume that the user has missed the current sequence number
	 * by about a day rather than she wants to wait for years
	 * using vertical blanks...
	 */

	DRM_WAIT_ON(ret, dev->vbl_queue, 3 * DRM_HZ,
		    (((cur_vblank = atomic_read(&dev->vbl_received)) -
		      *sequence) <= (1 << 23)));

	*sequence = cur_vblank;
	return ret;
}

static int
via_driver_irq_wait(drm_device_t * dev, unsigned int irq, int force_sequence,
		    unsigned int *sequence)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	unsigned int cur_irq_sequence;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int ret = 0;
	maskarray_t *masks;
	int real_irq;

	DRM_DEBUG("%s\n", __FUNCTION__);

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	if (irq >= drm_via_irq_num) {
		DRM_ERROR("%s Trying to wait on unknown irq %d\n", __FUNCTION__,
			  irq);
		return DRM_ERR(EINVAL);
	}

	real_irq = dev_priv->irq_map[irq];

	if (real_irq < 0) {
		DRM_ERROR("%s Video IRQ %d not available on this hardware.\n",
			  __FUNCTION__, irq);
		return DRM_ERR(EINVAL);
	}

	masks = dev_priv->irq_masks;
	cur_irq += real_irq;

	if (masks[real_irq][2] && !force_sequence) {
		DRM_WAIT_ON(ret, cur_irq->irq_queue, 3 * DRM_HZ,
			    ((VIA_READ(masks[irq][2]) & masks[irq][3]) ==
			     masks[irq][4]));
		cur_irq_sequence = atomic_read(&cur_irq->irq_received);
	} else {
		DRM_WAIT_ON(ret, cur_irq->irq_queue, 3 * DRM_HZ,
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

void via_driver_irq_preinstall(drm_device_t * dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int i;

	DRM_DEBUG("driver_irq_preinstall: dev_priv: %p\n", dev_priv);
	if (dev_priv) {

		dev_priv->irq_enable_mask = VIA_IRQ_VBLANK_ENABLE;
		dev_priv->irq_pending_mask = VIA_IRQ_VBLANK_PENDING;

		dev_priv->irq_masks = (dev_priv->pro_group_a) ?
		    via_pro_group_a_irqs : via_unichrome_irqs;
		dev_priv->num_irqs = (dev_priv->pro_group_a) ?
		    via_num_pro_group_a : via_num_unichrome;
		dev_priv->irq_map = (dev_priv->pro_group_a) ?
			via_irqmap_pro_group_a : via_irqmap_unichrome;

		for (i = 0; i < dev_priv->num_irqs; ++i) {
			atomic_set(&cur_irq->irq_received, 0);
			cur_irq->enable_mask = dev_priv->irq_masks[i][0];
			cur_irq->pending_mask = dev_priv->irq_masks[i][1];
			DRM_INIT_WAITQUEUE(&cur_irq->irq_queue);
			dev_priv->irq_enable_mask |= cur_irq->enable_mask;
			dev_priv->irq_pending_mask |= cur_irq->pending_mask;
			cur_irq++;

			DRM_DEBUG("Initializing IRQ %d\n", i);
		}

		dev_priv->last_vblank_valid = 0;

		/* Clear VSync interrupt regs */
		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status &
			  ~(dev_priv->irq_enable_mask));

		/* Clear bits if they're already high */
		viadrv_acknowledge_irqs(dev_priv);
	}
}

void via_driver_irq_postinstall(drm_device_t * dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("via_driver_irq_postinstall\n");
	if (dev_priv) {
		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status | VIA_IRQ_GLOBAL
			  | dev_priv->irq_enable_mask);

		/* Some magic, oh for some data sheets ! */

		VIA_WRITE8(0x83d4, 0x11);
		VIA_WRITE8(0x83d5, VIA_READ8(0x83d5) | 0x30);

	}
}

void via_driver_irq_uninstall(drm_device_t * dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("driver_irq_uninstall)\n");
	if (dev_priv) {

		/* Some more magic, oh for some data sheets ! */

		VIA_WRITE8(0x83d4, 0x11);
		VIA_WRITE8(0x83d5, VIA_READ8(0x83d5) & ~0x30);

		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status &
			  ~(VIA_IRQ_VBLANK_ENABLE | dev_priv->irq_enable_mask));
	}
}

int via_wait_irq(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_via_irqwait_t __user *argp = (void __user *)data;
	drm_via_irqwait_t irqwait;
	struct timeval now;
	int ret = 0;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int force_sequence;

	if (!dev->irq)
		return DRM_ERR(EINVAL);

	DRM_COPY_FROM_USER_IOCTL(irqwait, argp, sizeof(irqwait));
	if (irqwait.request.irq >= dev_priv->num_irqs) {
		DRM_ERROR("%s Trying to wait on unknown irq %d\n", __FUNCTION__,
			  irqwait.request.irq);
		return DRM_ERR(EINVAL);
	}

	cur_irq += irqwait.request.irq;

	switch (irqwait.request.type & ~VIA_IRQ_FLAGS_MASK) {
	case VIA_IRQ_RELATIVE:
		irqwait.request.sequence += atomic_read(&cur_irq->irq_received);
		irqwait.request.type &= ~_DRM_VBLANK_RELATIVE;
	case VIA_IRQ_ABSOLUTE:
		break;
	default:
		return DRM_ERR(EINVAL);
	}

	if (irqwait.request.type & VIA_IRQ_SIGNAL) {
		DRM_ERROR("%s Signals on Via IRQs not implemented yet.\n",
			  __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	force_sequence = (irqwait.request.type & VIA_IRQ_FORCE_SEQUENCE);

	ret = via_driver_irq_wait(dev, irqwait.request.irq, force_sequence,
				  &irqwait.request.sequence);
	do_gettimeofday(&now);
	irqwait.reply.tval_sec = now.tv_sec;
	irqwait.reply.tval_usec = now.tv_usec;

	DRM_COPY_TO_USER_IOCTL(argp, irqwait, sizeof(irqwait));

	return ret;
}
