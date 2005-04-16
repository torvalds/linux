/* mga_irq.c -- IRQ handling for radeon -*- linux-c -*-
 *
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
 *    Eric Anholt <anholt@FreeBSD.org>
 */

#include "drmP.h"
#include "drm.h"
#include "mga_drm.h"
#include "mga_drv.h"

irqreturn_t mga_driver_irq_handler( DRM_IRQ_ARGS )
{
	drm_device_t *dev = (drm_device_t *) arg;
	drm_mga_private_t *dev_priv = 
	   (drm_mga_private_t *)dev->dev_private;
	int status;

	status = MGA_READ( MGA_STATUS );
	
	/* VBLANK interrupt */
	if ( status & MGA_VLINEPEN ) {
		MGA_WRITE( MGA_ICLEAR, MGA_VLINEICLR );
		atomic_inc(&dev->vbl_received);
		DRM_WAKEUP(&dev->vbl_queue);
		drm_vbl_send_signals( dev );
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

int mga_driver_vblank_wait(drm_device_t *dev, unsigned int *sequence)
{
	unsigned int cur_vblank;
	int ret = 0;

	/* Assume that the user has missed the current sequence number
	 * by about a day rather than she wants to wait for years
	 * using vertical blanks... 
	 */
	DRM_WAIT_ON( ret, dev->vbl_queue, 3*DRM_HZ, 
		     ( ( ( cur_vblank = atomic_read(&dev->vbl_received ) )
			 - *sequence ) <= (1<<23) ) );

	*sequence = cur_vblank;

	return ret;
}

void mga_driver_irq_preinstall( drm_device_t *dev ) {
  	drm_mga_private_t *dev_priv = 
	   (drm_mga_private_t *)dev->dev_private;

	/* Disable *all* interrupts */
      	MGA_WRITE( MGA_IEN, 0 );
	/* Clear bits if they're already high */
   	MGA_WRITE( MGA_ICLEAR, ~0 );
}

void mga_driver_irq_postinstall( drm_device_t *dev ) {
  	drm_mga_private_t *dev_priv = 
	   (drm_mga_private_t *)dev->dev_private;

	/* Turn on VBL interrupt */
   	MGA_WRITE( MGA_IEN, MGA_VLINEIEN );
}

void mga_driver_irq_uninstall( drm_device_t *dev ) {
  	drm_mga_private_t *dev_priv = 
	   (drm_mga_private_t *)dev->dev_private;
	if (!dev_priv)
		return;

	/* Disable *all* interrupts */
	MGA_WRITE( MGA_IEN, 0 );
}
