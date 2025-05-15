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

#include "vmwgfx_bo.h"
#include "vmwgfx_drv.h"
#include "vmwgfx_kms.h"

#include "vmw_surface_cache.h"

#include <drm/drm_crtc.h>
#include <drm/drm_debugfs_crc.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include <linux/crc32.h>
#include <linux/delay.h>

#define GUESTINFO_VBLANK  "guestinfo.vmwgfx.vkms_enable"

static int
vmw_surface_sync(struct vmw_private *vmw,
		 struct vmw_surface *surf)
{
	int ret;
	struct vmw_fence_obj *fence = NULL;
	struct vmw_bo *bo = surf->res.guest_memory_bo;

	vmw_resource_clean(&surf->res);

	ret = ttm_bo_reserve(&bo->tbo, false, false, NULL);
	if (ret != 0) {
		drm_warn(&vmw->drm, "%s: failed reserve\n", __func__);
		goto done;
	}

	ret = vmw_execbuf_fence_commands(NULL, vmw, &fence, NULL);
	if (ret != 0) {
		drm_warn(&vmw->drm, "%s: failed execbuf\n", __func__);
		ttm_bo_unreserve(&bo->tbo);
		goto done;
	}

	dma_fence_wait(&fence->base, false);
	dma_fence_put(&fence->base);

	ttm_bo_unreserve(&bo->tbo);
done:
	return ret;
}

static void
compute_crc(struct drm_crtc *crtc,
	    struct vmw_surface *surf,
	    u32 *crc)
{
	u8 *mapped_surface;
	struct vmw_bo *bo = surf->res.guest_memory_bo;
	const struct SVGA3dSurfaceDesc *desc =
		vmw_surface_get_desc(surf->metadata.format);
	u32 row_pitch_bytes;
	SVGA3dSize blocks;
	u32 y;

	*crc = 0;

	vmw_surface_get_size_in_blocks(desc, &surf->metadata.base_size, &blocks);
	row_pitch_bytes = blocks.width * desc->pitchBytesPerBlock;
	WARN_ON(!bo);
	mapped_surface = vmw_bo_map_and_cache(bo);

	for (y = 0; y < blocks.height; y++) {
		*crc = crc32_le(*crc, mapped_surface, row_pitch_bytes);
		mapped_surface += row_pitch_bytes;
	}

	vmw_bo_unmap(bo);
}

static void
crc_generate_worker(struct work_struct *work)
{
	struct vmw_display_unit *du =
		container_of(work, struct vmw_display_unit, vkms.crc_generator_work);
	struct drm_crtc *crtc = &du->crtc;
	struct vmw_private *vmw = vmw_priv(crtc->dev);
	bool crc_pending;
	u64 frame_start, frame_end;
	u32 crc32 = 0;
	struct vmw_surface *surf = 0;

	spin_lock_irq(&du->vkms.crc_state_lock);
	crc_pending = du->vkms.crc_pending;
	spin_unlock_irq(&du->vkms.crc_state_lock);

	/*
	 * We raced with the vblank hrtimer and previous work already computed
	 * the crc, nothing to do.
	 */
	if (!crc_pending)
		return;

	spin_lock_irq(&du->vkms.crc_state_lock);
	surf = vmw_surface_reference(du->vkms.surface);
	spin_unlock_irq(&du->vkms.crc_state_lock);

	if (surf) {
		if (vmw_surface_sync(vmw, surf)) {
			drm_warn(
				crtc->dev,
				"CRC worker wasn't able to sync the crc surface!\n");
			return;
		}

		compute_crc(crtc, surf, &crc32);
		vmw_surface_unreference(&surf);
	}

	spin_lock_irq(&du->vkms.crc_state_lock);
	frame_start = du->vkms.frame_start;
	frame_end = du->vkms.frame_end;
	du->vkms.frame_start = 0;
	du->vkms.frame_end = 0;
	du->vkms.crc_pending = false;
	spin_unlock_irq(&du->vkms.crc_state_lock);

	/*
	 * The worker can fall behind the vblank hrtimer, make sure we catch up.
	 */
	while (frame_start <= frame_end)
		drm_crtc_add_crc_entry(crtc, true, frame_start++, &crc32);
}

static enum hrtimer_restart
vmw_vkms_vblank_simulate(struct hrtimer *timer)
{
	struct vmw_display_unit *du = container_of(timer, struct vmw_display_unit, vkms.timer);
	struct drm_crtc *crtc = &du->crtc;
	struct vmw_private *vmw = vmw_priv(crtc->dev);
	bool has_surface = false;
	u64 ret_overrun;
	bool locked, ret;

	ret_overrun = hrtimer_forward_now(&du->vkms.timer,
					  du->vkms.period_ns);
	if (ret_overrun != 1)
		drm_dbg_driver(crtc->dev, "vblank timer missed %lld frames.\n",
			       ret_overrun - 1);

	locked = vmw_vkms_vblank_trylock(crtc);
	ret = drm_crtc_handle_vblank(crtc);
	WARN_ON(!ret);
	if (!locked)
		return HRTIMER_RESTART;
	has_surface = du->vkms.surface != NULL;
	vmw_vkms_unlock(crtc);

	if (du->vkms.crc_enabled && has_surface) {
		u64 frame = drm_crtc_accurate_vblank_count(crtc);

		spin_lock(&du->vkms.crc_state_lock);
		if (!du->vkms.crc_pending)
			du->vkms.frame_start = frame;
		else
			drm_dbg_driver(crtc->dev,
				       "crc worker falling behind, frame_start: %llu, frame_end: %llu\n",
				       du->vkms.frame_start, frame);
		du->vkms.frame_end = frame;
		du->vkms.crc_pending = true;
		spin_unlock(&du->vkms.crc_state_lock);

		ret = queue_work(vmw->crc_workq, &du->vkms.crc_generator_work);
		if (!ret)
			drm_dbg_driver(crtc->dev, "Composer worker already queued\n");
	}

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
	}

	vmw->crc_workq = alloc_ordered_workqueue("vmwgfx_crc_generator", 0);
	if (!vmw->crc_workq) {
		drm_warn(&vmw->drm, "crc workqueue allocation failed. Disabling vkms.");
		vmw->vkms_enabled = false;
	}
	if (vmw->vkms_enabled)
		drm_info(&vmw->drm, "VKMS enabled\n");
}

void
vmw_vkms_cleanup(struct vmw_private *vmw)
{
	destroy_workqueue(vmw->crc_workq);
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

	hrtimer_setup(&du->vkms.timer, &vmw_vkms_vblank_simulate, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);
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

	hrtimer_cancel(&du->vkms.timer);
	du->vkms.surface = NULL;
	du->vkms.period_ns = ktime_set(0, 0);
}

enum vmw_vkms_lock_state {
	VMW_VKMS_LOCK_UNLOCKED     = 0,
	VMW_VKMS_LOCK_MODESET      = 1,
	VMW_VKMS_LOCK_VBLANK       = 2
};

void
vmw_vkms_crtc_init(struct drm_crtc *crtc)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);

	atomic_set(&du->vkms.atomic_lock, VMW_VKMS_LOCK_UNLOCKED);
	spin_lock_init(&du->vkms.crc_state_lock);

	INIT_WORK(&du->vkms.crc_generator_work, crc_generate_worker);
	du->vkms.surface = NULL;
}

void
vmw_vkms_crtc_cleanup(struct drm_crtc *crtc)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);

	if (du->vkms.surface)
		vmw_surface_unreference(&du->vkms.surface);
	WARN_ON(work_pending(&du->vkms.crc_generator_work));
	hrtimer_cancel(&du->vkms.timer);
}

void
vmw_vkms_crtc_atomic_begin(struct drm_crtc *crtc,
			   struct drm_atomic_state *state)
{
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (vmw->vkms_enabled)
		vmw_vkms_modeset_lock(crtc);
}

void
vmw_vkms_crtc_atomic_flush(struct drm_crtc *crtc,
			   struct drm_atomic_state *state)
{
	unsigned long flags;
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (!vmw->vkms_enabled)
		return;

	if (crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}

	vmw_vkms_unlock(crtc);
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

static bool
is_crc_supported(struct drm_crtc *crtc)
{
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (!vmw->vkms_enabled)
		return false;

	if (vmw->active_display_unit != vmw_du_screen_target)
		return false;

	return true;
}

static const char * const pipe_crc_sources[] = {"auto"};

static int
crc_parse_source(const char *src_name,
		 bool *enabled)
{
	int ret = 0;

	if (!src_name) {
		*enabled = false;
	} else if (strcmp(src_name, "auto") == 0) {
		*enabled = true;
	} else {
		*enabled = false;
		ret = -EINVAL;
	}

	return ret;
}

const char *const *
vmw_vkms_get_crc_sources(struct drm_crtc *crtc,
			 size_t *count)
{
	*count = 0;
	if (!is_crc_supported(crtc))
		return NULL;

	*count = ARRAY_SIZE(pipe_crc_sources);
	return pipe_crc_sources;
}

int
vmw_vkms_verify_crc_source(struct drm_crtc *crtc,
			   const char *src_name,
			   size_t *values_cnt)
{
	bool enabled;

	if (!is_crc_supported(crtc))
		return -EINVAL;

	if (crc_parse_source(src_name, &enabled) < 0) {
		drm_dbg_driver(crtc->dev, "unknown source '%s'\n", src_name);
		return -EINVAL;
	}

	*values_cnt = 1;

	return 0;
}

int
vmw_vkms_set_crc_source(struct drm_crtc *crtc,
			const char *src_name)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	bool enabled, prev_enabled, locked;
	int ret;

	if (!is_crc_supported(crtc))
		return -EINVAL;

	ret = crc_parse_source(src_name, &enabled);

	if (enabled)
		drm_crtc_vblank_get(crtc);

	locked = vmw_vkms_modeset_lock_relaxed(crtc);
	prev_enabled = du->vkms.crc_enabled;
	du->vkms.crc_enabled = enabled;
	if (locked)
		vmw_vkms_unlock(crtc);

	if (prev_enabled)
		drm_crtc_vblank_put(crtc);

	return ret;
}

void
vmw_vkms_set_crc_surface(struct drm_crtc *crtc,
			 struct vmw_surface *surf)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	struct vmw_private *vmw = vmw_priv(crtc->dev);

	if (vmw->vkms_enabled && du->vkms.surface != surf) {
		WARN_ON(atomic_read(&du->vkms.atomic_lock) != VMW_VKMS_LOCK_MODESET);
		if (du->vkms.surface)
			vmw_surface_unreference(&du->vkms.surface);
		if (surf)
			du->vkms.surface = vmw_surface_reference(surf);
	}
}

/**
 * vmw_vkms_lock_max_wait_ns - Return the max wait for the vkms lock
 * @du: The vmw_display_unit from which to grab the vblank timings
 *
 * Returns the maximum wait time used to acquire the vkms lock. By
 * default uses a time of a single frame and in case where vblank
 * was not initialized for the display unit 1/60th of a second.
 */
static inline u64
vmw_vkms_lock_max_wait_ns(struct vmw_display_unit *du)
{
	s64 nsecs = ktime_to_ns(du->vkms.period_ns);

	return  (nsecs > 0) ? nsecs : 16666666;
}

/**
 * vmw_vkms_modeset_lock - Protects access to crtc during modeset
 * @crtc: The crtc to lock for vkms
 *
 * This function prevents the VKMS timers/callbacks from being called
 * while a modeset operation is in process. We don't want the callbacks
 * e.g. the vblank simulator to be trying to access incomplete state
 * so we need to make sure they execute only when the modeset has
 * finished.
 *
 * Normally this would have been done with a spinlock but locking the
 * entire atomic modeset with vmwgfx is impossible because kms prepare
 * executes non-atomic ops (e.g. vmw_validation_prepare holds a mutex to
 * guard various bits of state). Which means that we need to synchronize
 * atomic context (the vblank handler) with the non-atomic entirity
 * of kms - so use an atomic_t to track which part of vkms has access
 * to the basic vkms state.
 */
void
vmw_vkms_modeset_lock(struct drm_crtc *crtc)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	const u64 nsecs_delay = 10;
	const u64 MAX_NSECS_DELAY = vmw_vkms_lock_max_wait_ns(du);
	u64 total_delay = 0;
	int ret;

	do {
		ret = atomic_cmpxchg(&du->vkms.atomic_lock,
				     VMW_VKMS_LOCK_UNLOCKED,
				     VMW_VKMS_LOCK_MODESET);
		if (ret == VMW_VKMS_LOCK_UNLOCKED || total_delay >= MAX_NSECS_DELAY)
			break;
		ndelay(nsecs_delay);
		total_delay += nsecs_delay;
	} while (1);

	if (total_delay >= MAX_NSECS_DELAY) {
		drm_warn(crtc->dev, "VKMS lock expired! total_delay = %lld, ret = %d, cur = %d\n",
			 total_delay, ret, atomic_read(&du->vkms.atomic_lock));
	}
}

/**
 * vmw_vkms_modeset_lock_relaxed - Protects access to crtc during modeset
 * @crtc: The crtc to lock for vkms
 *
 * Much like vmw_vkms_modeset_lock except that when the crtc is currently
 * in a modeset it will return immediately.
 *
 * Returns true if actually locked vkms to modeset or false otherwise.
 */
bool
vmw_vkms_modeset_lock_relaxed(struct drm_crtc *crtc)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	const u64 nsecs_delay = 10;
	const u64 MAX_NSECS_DELAY = vmw_vkms_lock_max_wait_ns(du);
	u64 total_delay = 0;
	int ret;

	do {
		ret = atomic_cmpxchg(&du->vkms.atomic_lock,
				     VMW_VKMS_LOCK_UNLOCKED,
				     VMW_VKMS_LOCK_MODESET);
		if (ret == VMW_VKMS_LOCK_UNLOCKED ||
		    ret == VMW_VKMS_LOCK_MODESET ||
		    total_delay >= MAX_NSECS_DELAY)
			break;
		ndelay(nsecs_delay);
		total_delay += nsecs_delay;
	} while (1);

	if (total_delay >= MAX_NSECS_DELAY) {
		drm_warn(crtc->dev, "VKMS relaxed lock expired!\n");
		return false;
	}

	return ret == VMW_VKMS_LOCK_UNLOCKED;
}

/**
 * vmw_vkms_vblank_trylock - Protects access to crtc during vblank
 * @crtc: The crtc to lock for vkms
 *
 * Tries to lock vkms for vblank, returns immediately.
 *
 * Returns true if locked vkms to vblank or false otherwise.
 */
bool
vmw_vkms_vblank_trylock(struct drm_crtc *crtc)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	u32 ret;

	ret = atomic_cmpxchg(&du->vkms.atomic_lock,
			     VMW_VKMS_LOCK_UNLOCKED,
			     VMW_VKMS_LOCK_VBLANK);

	return ret == VMW_VKMS_LOCK_UNLOCKED;
}

void
vmw_vkms_unlock(struct drm_crtc *crtc)
{
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);

	/* Release flag; mark it as unlocked. */
	atomic_set(&du->vkms.atomic_lock, VMW_VKMS_LOCK_UNLOCKED);
}
