/*
 * Copyright 2005 Thomas Hellstrom. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S), AND/OR THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Thomas Hellstrom 2005.
 *
 * Video and XvMC related functions.
 */

#include "drmP.h"
#include "via_drm.h"
#include "via_drv.h"

void
via_init_futex(drm_via_private_t *dev_priv)
{
	unsigned int i;

	DRM_DEBUG("%s\n", __FUNCTION__);

	for (i = 0; i < VIA_NR_XVMC_LOCKS; ++i) {
		DRM_INIT_WAITQUEUE(&(dev_priv->decoder_queue[i]));
		XVMCLOCKPTR(dev_priv->sarea_priv, i)->lock = 0;
	}
}

void
via_cleanup_futex(drm_via_private_t *dev_priv)
{
}	

void
via_release_futex(drm_via_private_t *dev_priv, int context)
{
	unsigned int i;
	volatile int *lock;

	for (i=0; i < VIA_NR_XVMC_LOCKS; ++i) {
	        lock = (int *) XVMCLOCKPTR(dev_priv->sarea_priv, i);
		if ( (_DRM_LOCKING_CONTEXT( *lock ) == context)) {
			if (_DRM_LOCK_IS_HELD( *lock ) && (*lock & _DRM_LOCK_CONT)) {
				DRM_WAKEUP( &(dev_priv->decoder_queue[i]));
			}
			*lock = 0;
		}
	}	
}	

int 
via_decoder_futex(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_via_futex_t fx;
	volatile int *lock;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_sarea_t *sAPriv = dev_priv->sarea_priv;
	int ret = 0;

	DRM_DEBUG("%s\n", __FUNCTION__);

	DRM_COPY_FROM_USER_IOCTL(fx, (drm_via_futex_t __user *) data,
				 sizeof(fx));

	if (fx.lock > VIA_NR_XVMC_LOCKS)
		return -EFAULT;

	lock = (int *)XVMCLOCKPTR(sAPriv, fx.lock);

	switch (fx.func) {
	case VIA_FUTEX_WAIT:
		DRM_WAIT_ON(ret, dev_priv->decoder_queue[fx.lock],
			    (fx.ms / 10) * (DRM_HZ / 100), *lock != fx.val);
		return ret;
	case VIA_FUTEX_WAKE:
		DRM_WAKEUP(&(dev_priv->decoder_queue[fx.lock]));
		return 0;
	}
	return 0;
}

