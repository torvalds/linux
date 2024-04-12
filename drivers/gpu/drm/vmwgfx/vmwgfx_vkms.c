// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright (c) 2024 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
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

#include "vmwgfx_vkms.h"

#include "vmwgfx_drv.h"
#include "vmwgfx_kms.h"
#include "vmwgfx_vkms.h"

#include <drm/drm_crtc.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#define GUESTINFO_VBLANK  "guestinfo.vmwgfx.vkms_enable"

enum hrtimer_restart
vmw_vkms_vblank_simulate(struct hrtimer *timer)
{
	struct vmw_display_unit *du = container_of(timer, struct vmw_display_unit, vkms.timer);
	struct drm_crtc *crtc = &du->crtc;
	u64 ret_overrun;
	bool ret;

	ret_overrun = hrtimer_forward_now(&du->vkms.timer,
					  du->vkms.period_ns);
	if (ret_overrun != 1)
		DRM_WARN("%s: vblank timer overrun\n", __func__);

	ret = drm_crtc_handle_vblank(crtc);
	/* Don't queue timer again when vblank is disabled. */
	if (!ret)
		return HRTIMER_NORESTART;

	return HRTIMER_RESTART;
}

void
vmw_vkms_init(struct vmw_private *vmw)
{
	char buffer[64];
	const size_t max_buf_len = sizeof(buffer) - 1;
	size_t buf_len = max_buf_len;
	int ret;

	vmw->vkms_enabled = false;

	ret = vmw_host_get_guestinfo(GUESTINFO_VBLANK, buffer, &buf_len);
	if (ret || buf_len > max_buf_len)
		return;
	buffer[buf_len] = '\0';

	ret = kstrtobool(buffer, &vmw->vkms_enabled);
	if (!ret && vmw->vkms_enabled) {
		ret = drm_vblank_init(&vmw->drm, VMWGFX_NUM_DISPLAY_UNITS);
		vmw->vkms_enabled = (ret == 0);
		drm_info(&vmw->drm, "vkms_enabled = %d\n", vmw->vkms_enabled);
	}
}

bool
vmw_vkms_get_vblank_timestamp(struct drm_crtc *crtc,
			      int *max_error,
			      ktime_t *vblank_time,
			      bool in_vblank_irq)
{
	struct drm_device *dev = crtc->dev;
	struct vmw_private *vmw = vmw_priv(dev);
	unsigned int pipe = crtc->index;
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	if (!vmw->vkms_enabled)
		return false;

	if (!READ_ONCE(vblank->enabled)) {
		*vblank_time = ktime_get();
		return true;
	}

	*vblank_time = READ_ONCE(du->vkms.timer.node.expires);

	if (WARN_ON(*vblank_time == vblank->time))
		return true;

	/*
	 * To prevent races we roll the hrtimer forward before we do any
	 * interrupt processing - this is how real hw works (the interrupt is
	 * only generated after all the vblank registers are updated) and what
	 * the vblank core expects. Therefore we need to always correct the
	 * timestampe by one frame.
	 */
	*vblank_time -= du->vkms.period_ns;

	return true;
}

int
vmw_vkms_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct vmw_private *vmw = vmw_priv(dev);
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);

	if (!vmw->vkms_enabled)
		return -EINVAL;

	drm_calc_timestamping_constants(crtc, &crtc->mode);

	du->vkms.period_ns = ktime_set(0, vblank->framedur_ns);
	hrtimer_start(&du->vkms.timer, du->vkms.period_ns, HRTIMER_MODE_REL);

	return 0;
}

void
vmw_vkms_disable_vblank(struct drm_crtc *crtc)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (!vmw->vkms_enabled)
		return;

	hrtimer_try_to_cancel(&du->vkms.timer);
}

void
vmw_vkms_crtc_atomic_flush(struct drm_crtc *crtc,
			   struct drm_atomic_state *state)
{
	unsigned long flags;
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (vmw->vkms_enabled && crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}
}

void
vmw_vkms_crtc_atomic_enable(struct drm_crtc *crtc,
			    struct drm_atomic_state *state)
{
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (vmw->vkms_enabled)
		drm_crtc_vblank_on(crtc);
}

void
vmw_vkms_crtc_atomic_disable(struct drm_crtc *crtc,
			     struct drm_atomic_state *state)
{
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (vmw->vkms_enabled)
		drm_crtc_vblank_off(crtc);
}
