// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#include <drm/drm_vblank.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_color.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_vblank.h"
#include "intel_vrr.h"

/*
 * This timing diagram depicts the video signal in and
 * around the vertical blanking period.
 *
 * Assumptions about the fictitious mode used in this example:
 *  vblank_start >= 3
 *  vsync_start = vblank_start + 1
 *  vsync_end = vblank_start + 2
 *  vtotal = vblank_start + 3
 *
 *           start of vblank:
 *           latch double buffered registers
 *           increment frame counter (ctg+)
 *           generate start of vblank interrupt (gen4+)
 *           |
 *           |          frame start:
 *           |          generate frame start interrupt (aka. vblank interrupt) (gmch)
 *           |          may be shifted forward 1-3 extra lines via TRANSCONF
 *           |          |
 *           |          |  start of vsync:
 *           |          |  generate vsync interrupt
 *           |          |  |
 * ___xxxx___    ___xxxx___    ___xxxx___    ___xxxx___    ___xxxx___    ___xxxx
 *       .   \hs/   .      \hs/          \hs/          \hs/   .      \hs/
 * ----va---> <-----------------vb--------------------> <--------va-------------
 *       |          |       <----vs----->                     |
 * -vbs-----> <---vbs+1---> <---vbs+2---> <-----0-----> <-----1-----> <-----2--- (scanline counter gen2)
 * -vbs-2---> <---vbs-1---> <---vbs-----> <---vbs+1---> <---vbs+2---> <-----0--- (scanline counter gen3+)
 * -vbs-2---> <---vbs-2---> <---vbs-1---> <---vbs-----> <---vbs+1---> <---vbs+2- (scanline counter hsw+ hdmi)
 *       |          |                                         |
 *       last visible pixel                                   first visible pixel
 *                  |                                         increment frame counter (gen3/4)
 *                  pixel counter = vblank_start * htotal     pixel counter = 0 (gen3/4)
 *
 * x  = horizontal active
 * _  = horizontal blanking
 * hs = horizontal sync
 * va = vertical active
 * vb = vertical blanking
 * vs = vertical sync
 * vbs = vblank_start (number)
 *
 * Summary:
 * - most events happen at the start of horizontal sync
 * - frame start happens at the start of horizontal blank, 1-4 lines
 *   (depending on TRANSCONF settings) after the start of vblank
 * - gen3/4 pixel and frame counter are synchronized with the start
 *   of horizontal active on the first line of vertical active
 */

/*
 * Called from drm generic code, passed a 'crtc', which we use as a pipe index.
 */
u32 i915_get_vblank_counter(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(crtc);
	const struct drm_display_mode *mode = &vblank->hwmode;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	u32 pixel, vbl_start, hsync_start, htotal;
	u64 frame;

	/*
	 * On i965gm TV output the frame counter only works up to
	 * the point when we enable the TV encoder. After that the
	 * frame counter ceases to work and reads zero. We need a
	 * vblank wait before enabling the TV encoder and so we
	 * have to enable vblank interrupts while the frame counter
	 * is still in a working state. However the core vblank code
	 * does not like us returning non-zero frame counter values
	 * when we've told it that we don't have a working frame
	 * counter. Thus we must stop non-zero values leaking out.
	 */
	if (!vblank->max_vblank_count)
		return 0;

	htotal = mode->crtc_htotal;
	hsync_start = mode->crtc_hsync_start;
	vbl_start = intel_mode_vblank_start(mode);

	/* Convert to pixel count */
	vbl_start *= htotal;

	/* Start of vblank event occurs at start of hsync */
	vbl_start -= htotal - hsync_start;

	/*
	 * High & low register fields aren't synchronized, so make sure
	 * we get a low value that's stable across two reads of the high
	 * register.
	 */
	frame = intel_de_read64_2x32(display, PIPEFRAMEPIXEL(display, pipe),
				     PIPEFRAME(display, pipe));

	pixel = frame & PIPE_PIXEL_MASK;
	frame = (frame >> PIPE_FRAME_LOW_SHIFT) & 0xffffff;

	/*
	 * The frame counter increments at beginning of active.
	 * Cook up a vblank counter by also checking the pixel
	 * counter against vblank start.
	 */
	return (frame + (pixel >= vbl_start)) & 0xffffff;
}

u32 g4x_get_vblank_counter(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(crtc);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	if (!vblank->max_vblank_count)
		return 0;

	return intel_de_read(display, PIPE_FRMCOUNT_G4X(display, pipe));
}

static u32 intel_crtc_scanlines_since_frame_timestamp(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(&crtc->base);
	const struct drm_display_mode *mode = &vblank->hwmode;
	u32 htotal = mode->crtc_htotal;
	u32 clock = mode->crtc_clock;
	u32 scan_prev_time, scan_curr_time, scan_post_time;

	/*
	 * To avoid the race condition where we might cross into the
	 * next vblank just between the PIPE_FRMTMSTMP and TIMESTAMP_CTR
	 * reads. We make sure we read PIPE_FRMTMSTMP and TIMESTAMP_CTR
	 * during the same frame.
	 */
	do {
		/*
		 * This field provides read back of the display
		 * pipe frame time stamp. The time stamp value
		 * is sampled at every start of vertical blank.
		 */
		scan_prev_time = intel_de_read_fw(display,
						  PIPE_FRMTMSTMP(crtc->pipe));

		/*
		 * The TIMESTAMP_CTR register has the current
		 * time stamp value.
		 */
		scan_curr_time = intel_de_read_fw(display, IVB_TIMESTAMP_CTR);

		scan_post_time = intel_de_read_fw(display,
						  PIPE_FRMTMSTMP(crtc->pipe));
	} while (scan_post_time != scan_prev_time);

	return div_u64(mul_u32_u32(scan_curr_time - scan_prev_time,
				   clock), 1000 * htotal);
}

/*
 * On certain encoders on certain platforms, pipe
 * scanline register will not work to get the scanline,
 * since the timings are driven from the PORT or issues
 * with scanline register updates.
 * This function will use Framestamp and current
 * timestamp registers to calculate the scanline.
 */
static u32 __intel_get_crtc_scanline_from_timestamp(struct intel_crtc *crtc)
{
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(&crtc->base);
	const struct drm_display_mode *mode = &vblank->hwmode;
	u32 vblank_start = mode->crtc_vblank_start;
	u32 vtotal = mode->crtc_vtotal;
	u32 scanline;

	scanline = intel_crtc_scanlines_since_frame_timestamp(crtc);
	scanline = min(scanline, vtotal - 1);
	scanline = (scanline + vblank_start) % vtotal;

	return scanline;
}

int intel_crtc_scanline_offset(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	/*
	 * The scanline counter increments at the leading edge of hsync.
	 *
	 * On most platforms it starts counting from vtotal-1 on the
	 * first active line. That means the scanline counter value is
	 * always one less than what we would expect. Ie. just after
	 * start of vblank, which also occurs at start of hsync (on the
	 * last active line), the scanline counter will read vblank_start-1.
	 *
	 * On gen2 the scanline counter starts counting from 1 instead
	 * of vtotal-1, so we have to subtract one.
	 *
	 * On HSW+ the behaviour of the scanline counter depends on the output
	 * type. For DP ports it behaves like most other platforms, but on HDMI
	 * there's an extra 1 line difference. So we need to add two instead of
	 * one to the value.
	 *
	 * On VLV/CHV DSI the scanline counter would appear to increment
	 * approx. 1/3 of a scanline before start of vblank. Unfortunately
	 * that means we can't tell whether we're in vblank or not while
	 * we're on that particular line. We must still set scanline_offset
	 * to 1 so that the vblank timestamps come out correct when we query
	 * the scanline counter from within the vblank interrupt handler.
	 * However if queried just before the start of vblank we'll get an
	 * answer that's slightly in the future.
	 */
	if (DISPLAY_VER(display) == 2)
		return -1;
	else if (HAS_DDI(display) && intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return 2;
	else
		return 1;
}

/*
 * intel_de_read_fw(), only for fast reads of display block, no need for
 * forcewake etc.
 */
static int __intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(&crtc->base);
	const struct drm_display_mode *mode = &vblank->hwmode;
	enum pipe pipe = crtc->pipe;
	int position, vtotal;

	if (!crtc->active)
		return 0;

	if (crtc->mode_flags & I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP)
		return __intel_get_crtc_scanline_from_timestamp(crtc);

	vtotal = intel_mode_vtotal(mode);

	position = intel_de_read_fw(display, PIPEDSL(display, pipe)) & PIPEDSL_LINE_MASK;

	/*
	 * On HSW, the DSL reg (0x70000) appears to return 0 if we
	 * read it just before the start of vblank.  So try it again
	 * so we don't accidentally end up spanning a vblank frame
	 * increment, causing the pipe_update_end() code to squak at us.
	 *
	 * The nature of this problem means we can't simply check the ISR
	 * bit and return the vblank start value; nor can we use the scanline
	 * debug register in the transcoder as it appears to have the same
	 * problem.  We may need to extend this to include other platforms,
	 * but so far testing only shows the problem on HSW.
	 */
	if (HAS_DDI(display) && !position) {
		int i, temp;

		for (i = 0; i < 100; i++) {
			udelay(1);
			temp = intel_de_read_fw(display,
						PIPEDSL(display, pipe)) & PIPEDSL_LINE_MASK;
			if (temp != position) {
				position = temp;
				break;
			}
		}
	}

	/*
	 * See update_scanline_offset() for the details on the
	 * scanline_offset adjustment.
	 */
	return (position + vtotal + crtc->scanline_offset) % vtotal;
}

/*
 * The uncore version of the spin lock functions is used to decide
 * whether we need to lock the uncore lock or not.  This is only
 * needed in i915, not in Xe.
 *
 * This lock in i915 is needed because some old platforms (at least
 * IVB and possibly HSW as well), which are not supported in Xe, need
 * all register accesses to the same cacheline to be serialized,
 * otherwise they may hang.
 */
#ifdef I915
static void intel_vblank_section_enter(struct intel_display *display)
	__acquires(i915->uncore.lock)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	spin_lock(&i915->uncore.lock);
}

static void intel_vblank_section_exit(struct intel_display *display)
	__releases(i915->uncore.lock)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	spin_unlock(&i915->uncore.lock);
}
#else
static void intel_vblank_section_enter(struct intel_display *display)
{
}

static void intel_vblank_section_exit(struct intel_display *display)
{
}
#endif

static bool i915_get_crtc_scanoutpos(struct drm_crtc *_crtc,
				     bool in_vblank_irq,
				     int *vpos, int *hpos,
				     ktime_t *stime, ktime_t *etime,
				     const struct drm_display_mode *mode)
{
	struct intel_display *display = to_intel_display(_crtc->dev);
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	enum pipe pipe = crtc->pipe;
	int position;
	int vbl_start, vbl_end, hsync_start, htotal, vtotal;
	unsigned long irqflags;
	bool use_scanline_counter = DISPLAY_VER(display) >= 5 ||
		display->platform.g4x || DISPLAY_VER(display) == 2 ||
		crtc->mode_flags & I915_MODE_FLAG_USE_SCANLINE_COUNTER;

	if (drm_WARN_ON(display->drm, !mode->crtc_clock)) {
		drm_dbg(display->drm,
			"trying to get scanoutpos for disabled pipe %c\n",
			pipe_name(pipe));
		return false;
	}

	htotal = mode->crtc_htotal;
	hsync_start = mode->crtc_hsync_start;
	vtotal = intel_mode_vtotal(mode);
	vbl_start = intel_mode_vblank_start(mode);
	vbl_end = intel_mode_vblank_end(mode);

	/*
	 * Enter vblank critical section, as we will do multiple
	 * timing critical raw register reads, potentially with
	 * preemption disabled, so the following code must not block.
	 */
	local_irq_save(irqflags);
	intel_vblank_section_enter(display);

	/* preempt_disable_rt() should go right here in PREEMPT_RT patchset. */

	/* Get optional system timestamp before query. */
	if (stime)
		*stime = ktime_get();

	if (crtc->mode_flags & I915_MODE_FLAG_VRR) {
		int scanlines = intel_crtc_scanlines_since_frame_timestamp(crtc);

		position = __intel_get_crtc_scanline(crtc);

		/*
		 * Already exiting vblank? If so, shift our position
		 * so it looks like we're already apporaching the full
		 * vblank end. This should make the generated timestamp
		 * more or less match when the active portion will start.
		 */
		if (position >= vbl_start && scanlines < position)
			position = min(crtc->vmax_vblank_start + scanlines, vtotal - 1);
	} else if (use_scanline_counter) {
		/* No obvious pixelcount register. Only query vertical
		 * scanout position from Display scan line register.
		 */
		position = __intel_get_crtc_scanline(crtc);
	} else {
		/*
		 * Have access to pixelcount since start of frame.
		 * We can split this into vertical and horizontal
		 * scanout position.
		 */
		position = (intel_de_read_fw(display, PIPEFRAMEPIXEL(display, pipe)) & PIPE_PIXEL_MASK) >> PIPE_PIXEL_SHIFT;

		/* convert to pixel counts */
		vbl_start *= htotal;
		vbl_end *= htotal;
		vtotal *= htotal;

		/*
		 * In interlaced modes, the pixel counter counts all pixels,
		 * so one field will have htotal more pixels. In order to avoid
		 * the reported position from jumping backwards when the pixel
		 * counter is beyond the length of the shorter field, just
		 * clamp the position the length of the shorter field. This
		 * matches how the scanline counter based position works since
		 * the scanline counter doesn't count the two half lines.
		 */
		position = min(position, vtotal - 1);

		/*
		 * Start of vblank interrupt is triggered at start of hsync,
		 * just prior to the first active line of vblank. However we
		 * consider lines to start at the leading edge of horizontal
		 * active. So, should we get here before we've crossed into
		 * the horizontal active of the first line in vblank, we would
		 * not set the DRM_SCANOUTPOS_INVBL flag. In order to fix that,
		 * always add htotal-hsync_start to the current pixel position.
		 */
		position = (position + htotal - hsync_start) % vtotal;
	}

	/* Get optional system timestamp after query. */
	if (etime)
		*etime = ktime_get();

	/* preempt_enable_rt() should go right here in PREEMPT_RT patchset. */

	intel_vblank_section_exit(display);
	local_irq_restore(irqflags);

	/*
	 * While in vblank, position will be negative
	 * counting up towards 0 at vbl_end. And outside
	 * vblank, position will be positive counting
	 * up since vbl_end.
	 */
	if (position >= vbl_start)
		position -= vbl_end;
	else
		position += vtotal - vbl_end;

	if (use_scanline_counter) {
		*vpos = position;
		*hpos = 0;
	} else {
		*vpos = position / htotal;
		*hpos = position - (*vpos * htotal);
	}

	return true;
}

bool intel_crtc_get_vblank_timestamp(struct drm_crtc *crtc, int *max_error,
				     ktime_t *vblank_time, bool in_vblank_irq)
{
	return drm_crtc_vblank_helper_get_vblank_timestamp_internal(
		crtc, max_error, vblank_time, in_vblank_irq,
		i915_get_crtc_scanoutpos);
}

int intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	unsigned long irqflags;
	int position;

	local_irq_save(irqflags);
	intel_vblank_section_enter(display);

	position = __intel_get_crtc_scanline(crtc);

	intel_vblank_section_exit(display);
	local_irq_restore(irqflags);

	return position;
}

static bool pipe_scanline_is_moving(struct intel_display *display,
				    enum pipe pipe)
{
	i915_reg_t reg = PIPEDSL(display, pipe);
	u32 line1, line2;

	line1 = intel_de_read(display, reg) & PIPEDSL_LINE_MASK;
	msleep(5);
	line2 = intel_de_read(display, reg) & PIPEDSL_LINE_MASK;

	return line1 != line2;
}

static void wait_for_pipe_scanline_moving(struct intel_crtc *crtc, bool state)
{
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pipe = crtc->pipe;

	/* Wait for the display line to settle/start moving */
	if (wait_for(pipe_scanline_is_moving(display, pipe) == state, 100))
		drm_err(display->drm,
			"pipe %c scanline %s wait timed out\n",
			pipe_name(pipe), str_on_off(state));
}

void intel_wait_for_pipe_scanline_stopped(struct intel_crtc *crtc)
{
	wait_for_pipe_scanline_moving(crtc, false);
}

void intel_wait_for_pipe_scanline_moving(struct intel_crtc *crtc)
{
	wait_for_pipe_scanline_moving(crtc, true);
}

void intel_crtc_update_active_timings(const struct intel_crtc_state *crtc_state,
				      bool vrr_enable)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u8 mode_flags = crtc_state->mode_flags;
	struct drm_display_mode adjusted_mode;
	int vmax_vblank_start = 0;
	unsigned long irqflags;

	drm_mode_init(&adjusted_mode, &crtc_state->hw.adjusted_mode);

	if (vrr_enable) {
		drm_WARN_ON(display->drm,
			    (mode_flags & I915_MODE_FLAG_VRR) == 0);

		adjusted_mode.crtc_vtotal = crtc_state->vrr.vmax;
		adjusted_mode.crtc_vblank_end = crtc_state->vrr.vmax;
		adjusted_mode.crtc_vblank_start = intel_vrr_vmin_vblank_start(crtc_state);
		vmax_vblank_start = intel_vrr_vmax_vblank_start(crtc_state);
	} else {
		mode_flags &= ~I915_MODE_FLAG_VRR;
	}

	/*
	 * Belts and suspenders locking to guarantee everyone sees 100%
	 * consistent state during fastset seamless refresh rate changes.
	 *
	 * vblank_time_lock takes care of all drm_vblank.c stuff, and
	 * uncore.lock takes care of __intel_get_crtc_scanline() which
	 * may get called elsewhere as well.
	 *
	 * TODO maybe just protect everything (including
	 * __intel_get_crtc_scanline()) with vblank_time_lock?
	 * Need to audit everything to make sure it's safe.
	 */
	spin_lock_irqsave(&display->drm->vblank_time_lock, irqflags);
	intel_vblank_section_enter(display);

	drm_calc_timestamping_constants(&crtc->base, &adjusted_mode);

	crtc->vmax_vblank_start = vmax_vblank_start;

	crtc->mode_flags = mode_flags;

	crtc->scanline_offset = intel_crtc_scanline_offset(crtc_state);
	intel_vblank_section_exit(display);
	spin_unlock_irqrestore(&display->drm->vblank_time_lock, irqflags);
}

int intel_mode_vdisplay(const struct drm_display_mode *mode)
{
	int vdisplay = mode->crtc_vdisplay;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vdisplay = DIV_ROUND_UP(vdisplay, 2);

	return vdisplay;
}

int intel_mode_vblank_start(const struct drm_display_mode *mode)
{
	int vblank_start = mode->crtc_vblank_start;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_start = DIV_ROUND_UP(vblank_start, 2);

	return vblank_start;
}

int intel_mode_vblank_end(const struct drm_display_mode *mode)
{
	int vblank_end = mode->crtc_vblank_end;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_end /= 2;

	return vblank_end;
}

int intel_mode_vtotal(const struct drm_display_mode *mode)
{
	int vtotal = mode->crtc_vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vtotal /= 2;

	return vtotal;
}

void intel_vblank_evade_init(const struct intel_crtc_state *old_crtc_state,
			     const struct intel_crtc_state *new_crtc_state,
			     struct intel_vblank_evade_ctx *evade)
{
	struct intel_display *display = to_intel_display(new_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	const struct intel_crtc_state *crtc_state;
	const struct drm_display_mode *adjusted_mode;

	evade->crtc = crtc;

	evade->need_vlv_dsi_wa = (display->platform.valleyview ||
				  display->platform.cherryview) &&
		intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI);

	/*
	 * During fastsets/etc. the transcoder is still
	 * running with the old timings at this point.
	 *
	 * TODO: maybe just use the active timings here?
	 */
	if (intel_crtc_needs_modeset(new_crtc_state))
		crtc_state = new_crtc_state;
	else
		crtc_state = old_crtc_state;

	adjusted_mode = &crtc_state->hw.adjusted_mode;

	if (crtc->mode_flags & I915_MODE_FLAG_VRR) {
		/* timing changes should happen with VRR disabled */
		drm_WARN_ON(crtc->base.dev, intel_crtc_needs_modeset(new_crtc_state) ||
			    new_crtc_state->update_m_n || new_crtc_state->update_lrr);

		if (intel_vrr_is_push_sent(crtc_state))
			evade->vblank_start = intel_vrr_vmin_vblank_start(crtc_state);
		else
			evade->vblank_start = intel_vrr_vmax_vblank_start(crtc_state);
	} else {
		evade->vblank_start = intel_mode_vblank_start(adjusted_mode);
	}

	/* FIXME needs to be calibrated sensibly */
	evade->min = evade->vblank_start - intel_usecs_to_scanlines(adjusted_mode,
								    VBLANK_EVASION_TIME_US);
	evade->max = evade->vblank_start - 1;

	/*
	 * M/N and TRANS_VTOTAL are double buffered on the transcoder's
	 * undelayed vblank, so with seamless M/N and LRR we must evade
	 * both vblanks.
	 *
	 * DSB execution waits for the transcoder's undelayed vblank,
	 * hence we must kick off the commit before that.
	 */
	if (intel_color_uses_dsb(new_crtc_state) ||
	    new_crtc_state->update_m_n || new_crtc_state->update_lrr)
		evade->min -= intel_mode_vblank_start(adjusted_mode) -
			intel_mode_vdisplay(adjusted_mode);
}

/* must be called with vblank interrupt already enabled! */
int intel_vblank_evade(struct intel_vblank_evade_ctx *evade)
{
	struct intel_crtc *crtc = evade->crtc;
	struct intel_display *display = to_intel_display(crtc);
	long timeout = msecs_to_jiffies_timeout(1);
	wait_queue_head_t *wq = drm_crtc_vblank_waitqueue(&crtc->base);
	DEFINE_WAIT(wait);
	int scanline;

	if (evade->min <= 0 || evade->max <= 0)
		return 0;

	for (;;) {
		/*
		 * prepare_to_wait() has a memory barrier, which guarantees
		 * other CPUs can see the task state update by the time we
		 * read the scanline.
		 */
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);

		scanline = intel_get_crtc_scanline(crtc);
		if (scanline < evade->min || scanline > evade->max)
			break;

		if (!timeout) {
			drm_err(display->drm,
				"Potential atomic update failure on pipe %c\n",
				pipe_name(crtc->pipe));
			break;
		}

		local_irq_enable();

		timeout = schedule_timeout(timeout);

		local_irq_disable();
	}

	finish_wait(wq, &wait);

	/*
	 * On VLV/CHV DSI the scanline counter would appear to
	 * increment approx. 1/3 of a scanline before start of vblank.
	 * The registers still get latched at start of vblank however.
	 * This means we must not write any registers on the first
	 * line of vblank (since not the whole line is actually in
	 * vblank). And unfortunately we can't use the interrupt to
	 * wait here since it will fire too soon. We could use the
	 * frame start interrupt instead since it will fire after the
	 * critical scanline, but that would require more changes
	 * in the interrupt code. So for now we'll just do the nasty
	 * thing and poll for the bad scanline to pass us by.
	 *
	 * FIXME figure out if BXT+ DSI suffers from this as well
	 */
	while (evade->need_vlv_dsi_wa && scanline == evade->vblank_start)
		scanline = intel_get_crtc_scanline(crtc);

	return scanline;
}
