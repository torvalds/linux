/*
 * Copyright © 2014 Intel Corporation
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <drm/drm_atomic_helper.h>

#include "display/intel_dp.h"

#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp_aux.h"
#include "intel_hdmi.h"
#include "intel_psr.h"
#include "intel_snps_phy.h"
#include "intel_sprite.h"
#include "skl_universal_plane.h"

/**
 * DOC: Panel Self Refresh (PSR/SRD)
 *
 * Since Haswell Display controller supports Panel Self-Refresh on display
 * panels witch have a remote frame buffer (RFB) implemented according to PSR
 * spec in eDP1.3. PSR feature allows the display to go to lower standby states
 * when system is idle but display is on as it eliminates display refresh
 * request to DDR memory completely as long as the frame buffer for that
 * display is unchanged.
 *
 * Panel Self Refresh must be supported by both Hardware (source) and
 * Panel (sink).
 *
 * PSR saves power by caching the framebuffer in the panel RFB, which allows us
 * to power down the link and memory controller. For DSI panels the same idea
 * is called "manual mode".
 *
 * The implementation uses the hardware-based PSR support which automatically
 * enters/exits self-refresh mode. The hardware takes care of sending the
 * required DP aux message and could even retrain the link (that part isn't
 * enabled yet though). The hardware also keeps track of any frontbuffer
 * changes to know when to exit self-refresh mode again. Unfortunately that
 * part doesn't work too well, hence why the i915 PSR support uses the
 * software frontbuffer tracking to make sure it doesn't miss a screen
 * update. For this integration intel_psr_invalidate() and intel_psr_flush()
 * get called by the frontbuffer tracking code. Note that because of locking
 * issues the self-refresh re-enable code is done from a work queue, which
 * must be correctly synchronized/cancelled when shutting down the pipe."
 *
 * DC3CO (DC3 clock off)
 *
 * On top of PSR2, GEN12 adds a intermediate power savings state that turns
 * clock off automatically during PSR2 idle state.
 * The smaller overhead of DC3co entry/exit vs. the overhead of PSR2 deep sleep
 * entry/exit allows the HW to enter a low-power state even when page flipping
 * periodically (for instance a 30fps video playback scenario).
 *
 * Every time a flips occurs PSR2 will get out of deep sleep state(if it was),
 * so DC3CO is enabled and tgl_dc3co_disable_work is schedule to run after 6
 * frames, if no other flip occurs and the function above is executed, DC3CO is
 * disabled and PSR2 is configured to enter deep sleep, resetting again in case
 * of another flip.
 * Front buffer modifications do not trigger DC3CO activation on purpose as it
 * would bring a lot of complexity and most of the moderns systems will only
 * use page flips.
 */

static bool psr_global_enabled(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	switch (intel_dp->psr.debug & I915_PSR_DEBUG_MODE_MASK) {
	case I915_PSR_DEBUG_DEFAULT:
		return i915->params.enable_psr;
	case I915_PSR_DEBUG_DISABLE:
		return false;
	default:
		return true;
	}
}

static bool psr2_global_enabled(struct intel_dp *intel_dp)
{
	switch (intel_dp->psr.debug & I915_PSR_DEBUG_MODE_MASK) {
	case I915_PSR_DEBUG_DISABLE:
	case I915_PSR_DEBUG_FORCE_PSR1:
		return false;
	default:
		return true;
	}
}

static void psr_irq_control(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	enum transcoder trans_shift;
	i915_reg_t imr_reg;
	u32 mask, val;

	/*
	 * gen12+ has registers relative to transcoder and one per transcoder
	 * using the same bit definition: handle it as TRANSCODER_EDP to force
	 * 0 shift in bit definition
	 */
	if (DISPLAY_VER(dev_priv) >= 12) {
		trans_shift = 0;
		imr_reg = TRANS_PSR_IMR(intel_dp->psr.transcoder);
	} else {
		trans_shift = intel_dp->psr.transcoder;
		imr_reg = EDP_PSR_IMR;
	}

	mask = EDP_PSR_ERROR(trans_shift);
	if (intel_dp->psr.debug & I915_PSR_DEBUG_IRQ)
		mask |= EDP_PSR_POST_EXIT(trans_shift) |
			EDP_PSR_PRE_ENTRY(trans_shift);

	/* Warning: it is masking/setting reserved bits too */
	val = intel_de_read(dev_priv, imr_reg);
	val &= ~EDP_PSR_TRANS_MASK(trans_shift);
	val |= ~mask;
	intel_de_write(dev_priv, imr_reg, val);
}

static void psr_event_print(struct drm_i915_private *i915,
			    u32 val, bool psr2_enabled)
{
	drm_dbg_kms(&i915->drm, "PSR exit events: 0x%x\n", val);
	if (val & PSR_EVENT_PSR2_WD_TIMER_EXPIRE)
		drm_dbg_kms(&i915->drm, "\tPSR2 watchdog timer expired\n");
	if ((val & PSR_EVENT_PSR2_DISABLED) && psr2_enabled)
		drm_dbg_kms(&i915->drm, "\tPSR2 disabled\n");
	if (val & PSR_EVENT_SU_DIRTY_FIFO_UNDERRUN)
		drm_dbg_kms(&i915->drm, "\tSU dirty FIFO underrun\n");
	if (val & PSR_EVENT_SU_CRC_FIFO_UNDERRUN)
		drm_dbg_kms(&i915->drm, "\tSU CRC FIFO underrun\n");
	if (val & PSR_EVENT_GRAPHICS_RESET)
		drm_dbg_kms(&i915->drm, "\tGraphics reset\n");
	if (val & PSR_EVENT_PCH_INTERRUPT)
		drm_dbg_kms(&i915->drm, "\tPCH interrupt\n");
	if (val & PSR_EVENT_MEMORY_UP)
		drm_dbg_kms(&i915->drm, "\tMemory up\n");
	if (val & PSR_EVENT_FRONT_BUFFER_MODIFY)
		drm_dbg_kms(&i915->drm, "\tFront buffer modification\n");
	if (val & PSR_EVENT_WD_TIMER_EXPIRE)
		drm_dbg_kms(&i915->drm, "\tPSR watchdog timer expired\n");
	if (val & PSR_EVENT_PIPE_REGISTERS_UPDATE)
		drm_dbg_kms(&i915->drm, "\tPIPE registers updated\n");
	if (val & PSR_EVENT_REGISTER_UPDATE)
		drm_dbg_kms(&i915->drm, "\tRegister updated\n");
	if (val & PSR_EVENT_HDCP_ENABLE)
		drm_dbg_kms(&i915->drm, "\tHDCP enabled\n");
	if (val & PSR_EVENT_KVMR_SESSION_ENABLE)
		drm_dbg_kms(&i915->drm, "\tKVMR session enabled\n");
	if (val & PSR_EVENT_VBI_ENABLE)
		drm_dbg_kms(&i915->drm, "\tVBI enabled\n");
	if (val & PSR_EVENT_LPSP_MODE_EXIT)
		drm_dbg_kms(&i915->drm, "\tLPSP mode exited\n");
	if ((val & PSR_EVENT_PSR_DISABLE) && !psr2_enabled)
		drm_dbg_kms(&i915->drm, "\tPSR disabled\n");
}

void intel_psr_irq_handler(struct intel_dp *intel_dp, u32 psr_iir)
{
	enum transcoder cpu_transcoder = intel_dp->psr.transcoder;
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	ktime_t time_ns =  ktime_get();
	enum transcoder trans_shift;
	i915_reg_t imr_reg;

	if (DISPLAY_VER(dev_priv) >= 12) {
		trans_shift = 0;
		imr_reg = TRANS_PSR_IMR(intel_dp->psr.transcoder);
	} else {
		trans_shift = intel_dp->psr.transcoder;
		imr_reg = EDP_PSR_IMR;
	}

	if (psr_iir & EDP_PSR_PRE_ENTRY(trans_shift)) {
		intel_dp->psr.last_entry_attempt = time_ns;
		drm_dbg_kms(&dev_priv->drm,
			    "[transcoder %s] PSR entry attempt in 2 vblanks\n",
			    transcoder_name(cpu_transcoder));
	}

	if (psr_iir & EDP_PSR_POST_EXIT(trans_shift)) {
		intel_dp->psr.last_exit = time_ns;
		drm_dbg_kms(&dev_priv->drm,
			    "[transcoder %s] PSR exit completed\n",
			    transcoder_name(cpu_transcoder));

		if (DISPLAY_VER(dev_priv) >= 9) {
			u32 val = intel_de_read(dev_priv,
						PSR_EVENT(cpu_transcoder));
			bool psr2_enabled = intel_dp->psr.psr2_enabled;

			intel_de_write(dev_priv, PSR_EVENT(cpu_transcoder),
				       val);
			psr_event_print(dev_priv, val, psr2_enabled);
		}
	}

	if (psr_iir & EDP_PSR_ERROR(trans_shift)) {
		u32 val;

		drm_warn(&dev_priv->drm, "[transcoder %s] PSR aux error\n",
			 transcoder_name(cpu_transcoder));

		intel_dp->psr.irq_aux_error = true;

		/*
		 * If this interruption is not masked it will keep
		 * interrupting so fast that it prevents the scheduled
		 * work to run.
		 * Also after a PSR error, we don't want to arm PSR
		 * again so we don't care about unmask the interruption
		 * or unset irq_aux_error.
		 */
		val = intel_de_read(dev_priv, imr_reg);
		val |= EDP_PSR_ERROR(trans_shift);
		intel_de_write(dev_priv, imr_reg, val);

		schedule_work(&intel_dp->psr.work);
	}
}

static bool intel_dp_get_alpm_status(struct intel_dp *intel_dp)
{
	u8 alpm_caps = 0;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_RECEIVER_ALPM_CAP,
			      &alpm_caps) != 1)
		return false;
	return alpm_caps & DP_ALPM_CAP;
}

static u8 intel_dp_get_sink_sync_latency(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 val = 8; /* assume the worst if we can't read the value */

	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_SYNCHRONIZATION_LATENCY_IN_SINK, &val) == 1)
		val &= DP_MAX_RESYNC_FRAME_COUNT_MASK;
	else
		drm_dbg_kms(&i915->drm,
			    "Unable to get sink synchronization latency, assuming 8 frames\n");
	return val;
}

static void intel_dp_get_su_granularity(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	ssize_t r;
	u16 w;
	u8 y;

	/* If sink don't have specific granularity requirements set legacy ones */
	if (!(intel_dp->psr_dpcd[1] & DP_PSR2_SU_GRANULARITY_REQUIRED)) {
		/* As PSR2 HW sends full lines, we do not care about x granularity */
		w = 4;
		y = 4;
		goto exit;
	}

	r = drm_dp_dpcd_read(&intel_dp->aux, DP_PSR2_SU_X_GRANULARITY, &w, 2);
	if (r != 2)
		drm_dbg_kms(&i915->drm,
			    "Unable to read DP_PSR2_SU_X_GRANULARITY\n");
	/*
	 * Spec says that if the value read is 0 the default granularity should
	 * be used instead.
	 */
	if (r != 2 || w == 0)
		w = 4;

	r = drm_dp_dpcd_read(&intel_dp->aux, DP_PSR2_SU_Y_GRANULARITY, &y, 1);
	if (r != 1) {
		drm_dbg_kms(&i915->drm,
			    "Unable to read DP_PSR2_SU_Y_GRANULARITY\n");
		y = 4;
	}
	if (y == 0)
		y = 1;

exit:
	intel_dp->psr.su_w_granularity = w;
	intel_dp->psr.su_y_granularity = y;
}

void intel_psr_init_dpcd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv =
		to_i915(dp_to_dig_port(intel_dp)->base.base.dev);

	drm_dp_dpcd_read(&intel_dp->aux, DP_PSR_SUPPORT, intel_dp->psr_dpcd,
			 sizeof(intel_dp->psr_dpcd));

	if (!intel_dp->psr_dpcd[0])
		return;
	drm_dbg_kms(&dev_priv->drm, "eDP panel supports PSR version %x\n",
		    intel_dp->psr_dpcd[0]);

	if (drm_dp_has_quirk(&intel_dp->desc, DP_DPCD_QUIRK_NO_PSR)) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR support not currently available for this panel\n");
		return;
	}

	if (!(intel_dp->edp_dpcd[1] & DP_EDP_SET_POWER_CAP)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Panel lacks power state control, PSR cannot be enabled\n");
		return;
	}

	intel_dp->psr.sink_support = true;
	intel_dp->psr.sink_sync_latency =
		intel_dp_get_sink_sync_latency(intel_dp);

	if (DISPLAY_VER(dev_priv) >= 9 &&
	    (intel_dp->psr_dpcd[0] == DP_PSR2_WITH_Y_COORD_IS_SUPPORTED)) {
		bool y_req = intel_dp->psr_dpcd[1] &
			     DP_PSR2_SU_Y_COORDINATE_REQUIRED;
		bool alpm = intel_dp_get_alpm_status(intel_dp);

		/*
		 * All panels that supports PSR version 03h (PSR2 +
		 * Y-coordinate) can handle Y-coordinates in VSC but we are
		 * only sure that it is going to be used when required by the
		 * panel. This way panel is capable to do selective update
		 * without a aux frame sync.
		 *
		 * To support PSR version 02h and PSR version 03h without
		 * Y-coordinate requirement panels we would need to enable
		 * GTC first.
		 */
		intel_dp->psr.sink_psr2_support = y_req && alpm;
		drm_dbg_kms(&dev_priv->drm, "PSR2 %ssupported\n",
			    intel_dp->psr.sink_psr2_support ? "" : "not ");

		if (intel_dp->psr.sink_psr2_support) {
			intel_dp->psr.colorimetry_support =
				intel_dp_get_colorimetry_status(intel_dp);
			intel_dp_get_su_granularity(intel_dp);
		}
	}
}

static void hsw_psr_setup_aux(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 aux_clock_divider, aux_ctl;
	int i;
	static const u8 aux_msg[] = {
		[0] = DP_AUX_NATIVE_WRITE << 4,
		[1] = DP_SET_POWER >> 8,
		[2] = DP_SET_POWER & 0xff,
		[3] = 1 - 1,
		[4] = DP_SET_POWER_D0,
	};
	u32 psr_aux_mask = EDP_PSR_AUX_CTL_TIME_OUT_MASK |
			   EDP_PSR_AUX_CTL_MESSAGE_SIZE_MASK |
			   EDP_PSR_AUX_CTL_PRECHARGE_2US_MASK |
			   EDP_PSR_AUX_CTL_BIT_CLOCK_2X_MASK;

	BUILD_BUG_ON(sizeof(aux_msg) > 20);
	for (i = 0; i < sizeof(aux_msg); i += 4)
		intel_de_write(dev_priv,
			       EDP_PSR_AUX_DATA(intel_dp->psr.transcoder, i >> 2),
			       intel_dp_pack_aux(&aux_msg[i], sizeof(aux_msg) - i));

	aux_clock_divider = intel_dp->get_aux_clock_divider(intel_dp, 0);

	/* Start with bits set for DDI_AUX_CTL register */
	aux_ctl = intel_dp->get_aux_send_ctl(intel_dp, sizeof(aux_msg),
					     aux_clock_divider);

	/* Select only valid bits for SRD_AUX_CTL */
	aux_ctl &= psr_aux_mask;
	intel_de_write(dev_priv, EDP_PSR_AUX_CTL(intel_dp->psr.transcoder),
		       aux_ctl);
}

static void intel_psr_enable_sink(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 dpcd_val = DP_PSR_ENABLE;

	/* Enable ALPM at sink for psr2 */
	if (intel_dp->psr.psr2_enabled) {
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_RECEIVER_ALPM_CONFIG,
				   DP_ALPM_ENABLE |
				   DP_ALPM_LOCK_ERROR_IRQ_HPD_ENABLE);

		dpcd_val |= DP_PSR_ENABLE_PSR2 | DP_PSR_IRQ_HPD_WITH_CRC_ERRORS;
	} else {
		if (intel_dp->psr.link_standby)
			dpcd_val |= DP_PSR_MAIN_LINK_ACTIVE;

		if (DISPLAY_VER(dev_priv) >= 8)
			dpcd_val |= DP_PSR_CRC_VERIFICATION;
	}

	if (intel_dp->psr.req_psr2_sdp_prior_scanline)
		dpcd_val |= DP_PSR_SU_REGION_SCANLINE_CAPTURE;

	drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_EN_CFG, dpcd_val);

	drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER, DP_SET_POWER_D0);
}

static u32 intel_psr1_get_tp_time(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 val = 0;

	if (DISPLAY_VER(dev_priv) >= 11)
		val |= EDP_PSR_TP4_TIME_0US;

	if (dev_priv->params.psr_safest_params) {
		val |= EDP_PSR_TP1_TIME_2500us;
		val |= EDP_PSR_TP2_TP3_TIME_2500us;
		goto check_tp3_sel;
	}

	if (dev_priv->vbt.psr.tp1_wakeup_time_us == 0)
		val |= EDP_PSR_TP1_TIME_0us;
	else if (dev_priv->vbt.psr.tp1_wakeup_time_us <= 100)
		val |= EDP_PSR_TP1_TIME_100us;
	else if (dev_priv->vbt.psr.tp1_wakeup_time_us <= 500)
		val |= EDP_PSR_TP1_TIME_500us;
	else
		val |= EDP_PSR_TP1_TIME_2500us;

	if (dev_priv->vbt.psr.tp2_tp3_wakeup_time_us == 0)
		val |= EDP_PSR_TP2_TP3_TIME_0us;
	else if (dev_priv->vbt.psr.tp2_tp3_wakeup_time_us <= 100)
		val |= EDP_PSR_TP2_TP3_TIME_100us;
	else if (dev_priv->vbt.psr.tp2_tp3_wakeup_time_us <= 500)
		val |= EDP_PSR_TP2_TP3_TIME_500us;
	else
		val |= EDP_PSR_TP2_TP3_TIME_2500us;

check_tp3_sel:
	if (intel_dp_source_supports_hbr2(intel_dp) &&
	    drm_dp_tps3_supported(intel_dp->dpcd))
		val |= EDP_PSR_TP1_TP3_SEL;
	else
		val |= EDP_PSR_TP1_TP2_SEL;

	return val;
}

static u8 psr_compute_idle_frames(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	int idle_frames;

	/* Let's use 6 as the minimum to cover all known cases including the
	 * off-by-one issue that HW has in some cases.
	 */
	idle_frames = max(6, dev_priv->vbt.psr.idle_frames);
	idle_frames = max(idle_frames, intel_dp->psr.sink_sync_latency + 1);

	if (drm_WARN_ON(&dev_priv->drm, idle_frames > 0xf))
		idle_frames = 0xf;

	return idle_frames;
}

static void hsw_activate_psr1(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 max_sleep_time = 0x1f;
	u32 val = EDP_PSR_ENABLE;

	val |= psr_compute_idle_frames(intel_dp) << EDP_PSR_IDLE_FRAME_SHIFT;

	val |= max_sleep_time << EDP_PSR_MAX_SLEEP_TIME_SHIFT;
	if (IS_HASWELL(dev_priv))
		val |= EDP_PSR_MIN_LINK_ENTRY_TIME_8_LINES;

	if (intel_dp->psr.link_standby)
		val |= EDP_PSR_LINK_STANDBY;

	val |= intel_psr1_get_tp_time(intel_dp);

	if (DISPLAY_VER(dev_priv) >= 8)
		val |= EDP_PSR_CRC_ENABLE;

	val |= (intel_de_read(dev_priv, EDP_PSR_CTL(intel_dp->psr.transcoder)) &
		EDP_PSR_RESTORE_PSR_ACTIVE_CTX_MASK);
	intel_de_write(dev_priv, EDP_PSR_CTL(intel_dp->psr.transcoder), val);
}

static u32 intel_psr2_get_tp_time(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 val = 0;

	if (dev_priv->params.psr_safest_params)
		return EDP_PSR2_TP2_TIME_2500us;

	if (dev_priv->vbt.psr.psr2_tp2_tp3_wakeup_time_us >= 0 &&
	    dev_priv->vbt.psr.psr2_tp2_tp3_wakeup_time_us <= 50)
		val |= EDP_PSR2_TP2_TIME_50us;
	else if (dev_priv->vbt.psr.psr2_tp2_tp3_wakeup_time_us <= 100)
		val |= EDP_PSR2_TP2_TIME_100us;
	else if (dev_priv->vbt.psr.psr2_tp2_tp3_wakeup_time_us <= 500)
		val |= EDP_PSR2_TP2_TIME_500us;
	else
		val |= EDP_PSR2_TP2_TIME_2500us;

	return val;
}

static void hsw_activate_psr2(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 val = EDP_PSR2_ENABLE;

	val |= psr_compute_idle_frames(intel_dp) << EDP_PSR2_IDLE_FRAME_SHIFT;

	if (!IS_ALDERLAKE_P(dev_priv))
		val |= EDP_SU_TRACK_ENABLE;

	if (DISPLAY_VER(dev_priv) >= 10 && DISPLAY_VER(dev_priv) <= 12)
		val |= EDP_Y_COORDINATE_ENABLE;

	val |= EDP_PSR2_FRAME_BEFORE_SU(intel_dp->psr.sink_sync_latency + 1);
	val |= intel_psr2_get_tp_time(intel_dp);

	/* Wa_22012278275:adl-p */
	if (IS_ADLP_DISPLAY_STEP(dev_priv, STEP_A0, STEP_E0)) {
		static const u8 map[] = {
			2, /* 5 lines */
			1, /* 6 lines */
			0, /* 7 lines */
			3, /* 8 lines */
			6, /* 9 lines */
			5, /* 10 lines */
			4, /* 11 lines */
			7, /* 12 lines */
		};
		/*
		 * Still using the default IO_BUFFER_WAKE and FAST_WAKE, see
		 * comments bellow for more information
		 */
		u32 tmp, lines = 7;

		val |= TGL_EDP_PSR2_BLOCK_COUNT_NUM_2;

		tmp = map[lines - TGL_EDP_PSR2_IO_BUFFER_WAKE_MIN_LINES];
		tmp = tmp << TGL_EDP_PSR2_IO_BUFFER_WAKE_SHIFT;
		val |= tmp;

		tmp = map[lines - TGL_EDP_PSR2_FAST_WAKE_MIN_LINES];
		tmp = tmp << TGL_EDP_PSR2_FAST_WAKE_MIN_SHIFT;
		val |= tmp;
	} else if (DISPLAY_VER(dev_priv) >= 12) {
		/*
		 * TODO: 7 lines of IO_BUFFER_WAKE and FAST_WAKE are default
		 * values from BSpec. In order to setting an optimal power
		 * consumption, lower than 4k resoluition mode needs to decrese
		 * IO_BUFFER_WAKE and FAST_WAKE. And higher than 4K resolution
		 * mode needs to increase IO_BUFFER_WAKE and FAST_WAKE.
		 */
		val |= TGL_EDP_PSR2_BLOCK_COUNT_NUM_2;
		val |= TGL_EDP_PSR2_IO_BUFFER_WAKE(7);
		val |= TGL_EDP_PSR2_FAST_WAKE(7);
	} else if (DISPLAY_VER(dev_priv) >= 9) {
		val |= EDP_PSR2_IO_BUFFER_WAKE(7);
		val |= EDP_PSR2_FAST_WAKE(7);
	}

	if (intel_dp->psr.req_psr2_sdp_prior_scanline)
		val |= EDP_PSR2_SU_SDP_SCANLINE;

	if (intel_dp->psr.psr2_sel_fetch_enabled) {
		/* Wa_1408330847 */
		if (IS_TGL_DISPLAY_STEP(dev_priv, STEP_A0, STEP_B0))
			intel_de_rmw(dev_priv, CHICKEN_PAR1_1,
				     DIS_RAM_BYPASS_PSR2_MAN_TRACK,
				     DIS_RAM_BYPASS_PSR2_MAN_TRACK);

		intel_de_write(dev_priv,
			       PSR2_MAN_TRK_CTL(intel_dp->psr.transcoder),
			       PSR2_MAN_TRK_CTL_ENABLE);
	} else if (HAS_PSR2_SEL_FETCH(dev_priv)) {
		intel_de_write(dev_priv,
			       PSR2_MAN_TRK_CTL(intel_dp->psr.transcoder), 0);
	}

	/*
	 * PSR2 HW is incorrectly using EDP_PSR_TP1_TP3_SEL and BSpec is
	 * recommending keep this bit unset while PSR2 is enabled.
	 */
	intel_de_write(dev_priv, EDP_PSR_CTL(intel_dp->psr.transcoder), 0);

	intel_de_write(dev_priv, EDP_PSR2_CTL(intel_dp->psr.transcoder), val);
}

static bool
transcoder_has_psr2(struct drm_i915_private *dev_priv, enum transcoder trans)
{
	if (DISPLAY_VER(dev_priv) < 9)
		return false;
	else if (DISPLAY_VER(dev_priv) >= 12)
		return trans == TRANSCODER_A;
	else
		return trans == TRANSCODER_EDP;
}

static u32 intel_get_frame_time_us(const struct intel_crtc_state *cstate)
{
	if (!cstate || !cstate->hw.active)
		return 0;

	return DIV_ROUND_UP(1000 * 1000,
			    drm_mode_vrefresh(&cstate->hw.adjusted_mode));
}

static void psr2_program_idle_frames(struct intel_dp *intel_dp,
				     u32 idle_frames)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 val;

	idle_frames <<=  EDP_PSR2_IDLE_FRAME_SHIFT;
	val = intel_de_read(dev_priv, EDP_PSR2_CTL(intel_dp->psr.transcoder));
	val &= ~EDP_PSR2_IDLE_FRAME_MASK;
	val |= idle_frames;
	intel_de_write(dev_priv, EDP_PSR2_CTL(intel_dp->psr.transcoder), val);
}

static void tgl_psr2_enable_dc3co(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	psr2_program_idle_frames(intel_dp, 0);
	intel_display_power_set_target_dc_state(dev_priv, DC_STATE_EN_DC3CO);
}

static void tgl_psr2_disable_dc3co(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	intel_display_power_set_target_dc_state(dev_priv, DC_STATE_EN_UPTO_DC6);
	psr2_program_idle_frames(intel_dp, psr_compute_idle_frames(intel_dp));
}

static void tgl_dc3co_disable_work(struct work_struct *work)
{
	struct intel_dp *intel_dp =
		container_of(work, typeof(*intel_dp), psr.dc3co_work.work);

	mutex_lock(&intel_dp->psr.lock);
	/* If delayed work is pending, it is not idle */
	if (delayed_work_pending(&intel_dp->psr.dc3co_work))
		goto unlock;

	tgl_psr2_disable_dc3co(intel_dp);
unlock:
	mutex_unlock(&intel_dp->psr.lock);
}

static void tgl_disallow_dc3co_on_psr2_exit(struct intel_dp *intel_dp)
{
	if (!intel_dp->psr.dc3co_exitline)
		return;

	cancel_delayed_work(&intel_dp->psr.dc3co_work);
	/* Before PSR2 exit disallow dc3co*/
	tgl_psr2_disable_dc3co(intel_dp);
}

static bool
dc3co_is_pipe_port_compatible(struct intel_dp *intel_dp,
			      struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe = to_intel_crtc(crtc_state->uapi.crtc)->pipe;
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	enum port port = dig_port->base.port;

	if (IS_ALDERLAKE_P(dev_priv))
		return pipe <= PIPE_B && port <= PORT_B;
	else
		return pipe == PIPE_A && port == PORT_A;
}

static void
tgl_dc3co_exitline_compute_config(struct intel_dp *intel_dp,
				  struct intel_crtc_state *crtc_state)
{
	const u32 crtc_vdisplay = crtc_state->uapi.adjusted_mode.crtc_vdisplay;
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 exit_scanlines;

	/*
	 * FIXME: Due to the changed sequence of activating/deactivating DC3CO,
	 * disable DC3CO until the changed dc3co activating/deactivating sequence
	 * is applied. B.Specs:49196
	 */
	return;

	/*
	 * DMC's DC3CO exit mechanism has an issue with Selective Fecth
	 * TODO: when the issue is addressed, this restriction should be removed.
	 */
	if (crtc_state->enable_psr2_sel_fetch)
		return;

	if (!(dev_priv->dmc.allowed_dc_mask & DC_STATE_EN_DC3CO))
		return;

	if (!dc3co_is_pipe_port_compatible(intel_dp, crtc_state))
		return;

	/* Wa_16011303918:adl-p */
	if (IS_ADLP_DISPLAY_STEP(dev_priv, STEP_A0, STEP_B0))
		return;

	/*
	 * DC3CO Exit time 200us B.Spec 49196
	 * PSR2 transcoder Early Exit scanlines = ROUNDUP(200 / line time) + 1
	 */
	exit_scanlines =
		intel_usecs_to_scanlines(&crtc_state->uapi.adjusted_mode, 200) + 1;

	if (drm_WARN_ON(&dev_priv->drm, exit_scanlines > crtc_vdisplay))
		return;

	crtc_state->dc3co_exitline = crtc_vdisplay - exit_scanlines;
}

static bool intel_psr2_sel_fetch_config_valid(struct intel_dp *intel_dp,
					      struct intel_crtc_state *crtc_state)
{
	struct intel_atomic_state *state = to_intel_atomic_state(crtc_state->uapi.state);
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	int i;

	if (!dev_priv->params.enable_psr2_sel_fetch &&
	    intel_dp->psr.debug != I915_PSR_DEBUG_ENABLE_SEL_FETCH) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 sel fetch not enabled, disabled by parameter\n");
		return false;
	}

	if (crtc_state->uapi.async_flip) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 sel fetch not enabled, async flip enabled\n");
		return false;
	}

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		if (plane_state->uapi.rotation != DRM_MODE_ROTATE_0) {
			drm_dbg_kms(&dev_priv->drm,
				    "PSR2 sel fetch not enabled, plane rotated\n");
			return false;
		}
	}

	/* Wa_14010254185 Wa_14010103792 */
	if (IS_TGL_DISPLAY_STEP(dev_priv, STEP_A0, STEP_C0)) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 sel fetch not enabled, missing the implementation of WAs\n");
		return false;
	}

	return crtc_state->enable_psr2_sel_fetch = true;
}

static bool psr2_granularity_check(struct intel_dp *intel_dp,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	const int crtc_hdisplay = crtc_state->hw.adjusted_mode.crtc_hdisplay;
	const int crtc_vdisplay = crtc_state->hw.adjusted_mode.crtc_vdisplay;
	u16 y_granularity = 0;

	/* PSR2 HW only send full lines so we only need to validate the width */
	if (crtc_hdisplay % intel_dp->psr.su_w_granularity)
		return false;

	if (crtc_vdisplay % intel_dp->psr.su_y_granularity)
		return false;

	/* HW tracking is only aligned to 4 lines */
	if (!crtc_state->enable_psr2_sel_fetch)
		return intel_dp->psr.su_y_granularity == 4;

	/*
	 * adl_p has 1 line granularity. For other platforms with SW tracking we
	 * can adjust the y coordinates to match sink requirement if multiple of
	 * 4.
	 */
	if (IS_ALDERLAKE_P(dev_priv))
		y_granularity = intel_dp->psr.su_y_granularity;
	else if (intel_dp->psr.su_y_granularity <= 2)
		y_granularity = 4;
	else if ((intel_dp->psr.su_y_granularity % 4) == 0)
		y_granularity = intel_dp->psr.su_y_granularity;

	if (y_granularity == 0 || crtc_vdisplay % y_granularity)
		return false;

	crtc_state->su_y_granularity = y_granularity;
	return true;
}

static bool _compute_psr2_sdp_prior_scanline_indication(struct intel_dp *intel_dp,
							struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *adjusted_mode = &crtc_state->uapi.adjusted_mode;
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 hblank_total, hblank_ns, req_ns;

	hblank_total = adjusted_mode->crtc_hblank_end - adjusted_mode->crtc_hblank_start;
	hblank_ns = div_u64(1000000ULL * hblank_total, adjusted_mode->crtc_clock);

	/* From spec: (72 / number of lanes) * 1000 / symbol clock frequency MHz */
	req_ns = (72 / crtc_state->lane_count) * 1000 / (crtc_state->port_clock / 1000);

	if ((hblank_ns - req_ns) > 100)
		return true;

	if (DISPLAY_VER(dev_priv) < 13 || intel_dp->edp_dpcd[0] < DP_EDP_14b)
		return false;

	crtc_state->req_psr2_sdp_prior_scanline = true;
	return true;
}

static bool intel_psr2_config_valid(struct intel_dp *intel_dp,
				    struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	int crtc_hdisplay = crtc_state->hw.adjusted_mode.crtc_hdisplay;
	int crtc_vdisplay = crtc_state->hw.adjusted_mode.crtc_vdisplay;
	int psr_max_h = 0, psr_max_v = 0, max_bpp = 0;

	if (!intel_dp->psr.sink_psr2_support)
		return false;

	/* JSL and EHL only supports eDP 1.3 */
	if (IS_JSL_EHL(dev_priv)) {
		drm_dbg_kms(&dev_priv->drm, "PSR2 not supported by phy\n");
		return false;
	}

	/* Wa_16011181250 */
	if (IS_ROCKETLAKE(dev_priv) || IS_ALDERLAKE_S(dev_priv) ||
	    IS_DG2(dev_priv)) {
		drm_dbg_kms(&dev_priv->drm, "PSR2 is defeatured for this platform\n");
		return false;
	}

	/*
	 * We are missing the implementation of some workarounds to enabled PSR2
	 * in Alderlake_P, until ready PSR2 should be kept disabled.
	 */
	if (IS_ALDERLAKE_P(dev_priv)) {
		drm_dbg_kms(&dev_priv->drm, "PSR2 is missing the implementation of workarounds\n");
		return false;
	}

	if (!transcoder_has_psr2(dev_priv, crtc_state->cpu_transcoder)) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 not supported in transcoder %s\n",
			    transcoder_name(crtc_state->cpu_transcoder));
		return false;
	}

	if (!psr2_global_enabled(intel_dp)) {
		drm_dbg_kms(&dev_priv->drm, "PSR2 disabled by flag\n");
		return false;
	}

	/*
	 * DSC and PSR2 cannot be enabled simultaneously. If a requested
	 * resolution requires DSC to be enabled, priority is given to DSC
	 * over PSR2.
	 */
	if (crtc_state->dsc.compression_enable) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 cannot be enabled since DSC is enabled\n");
		return false;
	}

	if (crtc_state->crc_enabled) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 not enabled because it would inhibit pipe CRC calculation\n");
		return false;
	}

	if (DISPLAY_VER(dev_priv) >= 12) {
		psr_max_h = 5120;
		psr_max_v = 3200;
		max_bpp = 30;
	} else if (DISPLAY_VER(dev_priv) >= 10) {
		psr_max_h = 4096;
		psr_max_v = 2304;
		max_bpp = 24;
	} else if (DISPLAY_VER(dev_priv) == 9) {
		psr_max_h = 3640;
		psr_max_v = 2304;
		max_bpp = 24;
	}

	if (crtc_state->pipe_bpp > max_bpp) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 not enabled, pipe bpp %d > max supported %d\n",
			    crtc_state->pipe_bpp, max_bpp);
		return false;
	}

	if (HAS_PSR2_SEL_FETCH(dev_priv)) {
		if (!intel_psr2_sel_fetch_config_valid(intel_dp, crtc_state) &&
		    !HAS_PSR_HW_TRACKING(dev_priv)) {
			drm_dbg_kms(&dev_priv->drm,
				    "PSR2 not enabled, selective fetch not valid and no HW tracking available\n");
			return false;
		}
	}

	/* Wa_2209313811 */
	if (!crtc_state->enable_psr2_sel_fetch &&
	    IS_TGL_DISPLAY_STEP(dev_priv, STEP_A0, STEP_C0)) {
		drm_dbg_kms(&dev_priv->drm, "PSR2 HW tracking is not supported this Display stepping\n");
		return false;
	}

	if (!psr2_granularity_check(intel_dp, crtc_state)) {
		drm_dbg_kms(&dev_priv->drm, "PSR2 not enabled, SU granularity not compatible\n");
		return false;
	}

	if (!crtc_state->enable_psr2_sel_fetch &&
	    (crtc_hdisplay > psr_max_h || crtc_vdisplay > psr_max_v)) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 not enabled, resolution %dx%d > max supported %dx%d\n",
			    crtc_hdisplay, crtc_vdisplay,
			    psr_max_h, psr_max_v);
		return false;
	}

	if (!_compute_psr2_sdp_prior_scanline_indication(intel_dp, crtc_state)) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 not enabled, PSR2 SDP indication do not fit in hblank\n");
		return false;
	}

	/* Wa_16011303918:adl-p */
	if (crtc_state->vrr.enable &&
	    IS_ADLP_DISPLAY_STEP(dev_priv, STEP_A0, STEP_B0)) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR2 not enabled, not compatible with HW stepping + VRR\n");
		return false;
	}

	tgl_dc3co_exitline_compute_config(intel_dp, crtc_state);
	return true;
}

void intel_psr_compute_config(struct intel_dp *intel_dp,
			      struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int psr_setup_time;

	/*
	 * Current PSR panels dont work reliably with VRR enabled
	 * So if VRR is enabled, do not enable PSR.
	 */
	if (crtc_state->vrr.enable)
		return;

	if (!CAN_PSR(intel_dp))
		return;

	if (!psr_global_enabled(intel_dp)) {
		drm_dbg_kms(&dev_priv->drm, "PSR disabled by flag\n");
		return;
	}

	if (intel_dp->psr.sink_not_reliable) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR sink implementation is not reliable\n");
		return;
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR condition failed: Interlaced mode enabled\n");
		return;
	}

	psr_setup_time = drm_dp_psr_setup_time(intel_dp->psr_dpcd);
	if (psr_setup_time < 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR condition failed: Invalid PSR setup time (0x%02x)\n",
			    intel_dp->psr_dpcd[1]);
		return;
	}

	if (intel_usecs_to_scanlines(adjusted_mode, psr_setup_time) >
	    adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vdisplay - 1) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR condition failed: PSR setup time (%d us) too long\n",
			    psr_setup_time);
		return;
	}

	crtc_state->has_psr = true;
	crtc_state->has_psr2 = intel_psr2_config_valid(intel_dp, crtc_state);
	crtc_state->infoframes.enable |= intel_hdmi_infoframe_enable(DP_SDP_VSC);
}

void intel_psr_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp;
	u32 val;

	if (!dig_port)
		return;

	intel_dp = &dig_port->dp;
	if (!CAN_PSR(intel_dp))
		return;

	mutex_lock(&intel_dp->psr.lock);
	if (!intel_dp->psr.enabled)
		goto unlock;

	/*
	 * Not possible to read EDP_PSR/PSR2_CTL registers as it is
	 * enabled/disabled because of frontbuffer tracking and others.
	 */
	pipe_config->has_psr = true;
	pipe_config->has_psr2 = intel_dp->psr.psr2_enabled;
	pipe_config->infoframes.enable |= intel_hdmi_infoframe_enable(DP_SDP_VSC);

	if (!intel_dp->psr.psr2_enabled)
		goto unlock;

	if (HAS_PSR2_SEL_FETCH(dev_priv)) {
		val = intel_de_read(dev_priv, PSR2_MAN_TRK_CTL(intel_dp->psr.transcoder));
		if (val & PSR2_MAN_TRK_CTL_ENABLE)
			pipe_config->enable_psr2_sel_fetch = true;
	}

	if (DISPLAY_VER(dev_priv) >= 12) {
		val = intel_de_read(dev_priv, EXITLINE(intel_dp->psr.transcoder));
		val &= EXITLINE_MASK;
		pipe_config->dc3co_exitline = val;
	}
unlock:
	mutex_unlock(&intel_dp->psr.lock);
}

static void intel_psr_activate(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	enum transcoder transcoder = intel_dp->psr.transcoder;

	if (transcoder_has_psr2(dev_priv, transcoder))
		drm_WARN_ON(&dev_priv->drm,
			    intel_de_read(dev_priv, EDP_PSR2_CTL(transcoder)) & EDP_PSR2_ENABLE);

	drm_WARN_ON(&dev_priv->drm,
		    intel_de_read(dev_priv, EDP_PSR_CTL(transcoder)) & EDP_PSR_ENABLE);
	drm_WARN_ON(&dev_priv->drm, intel_dp->psr.active);
	lockdep_assert_held(&intel_dp->psr.lock);

	/* psr1 and psr2 are mutually exclusive.*/
	if (intel_dp->psr.psr2_enabled)
		hsw_activate_psr2(intel_dp);
	else
		hsw_activate_psr1(intel_dp);

	intel_dp->psr.active = true;
}

static void intel_psr_enable_source(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	enum transcoder cpu_transcoder = intel_dp->psr.transcoder;
	u32 mask;

	/* Only HSW and BDW have PSR AUX registers that need to be setup. SKL+
	 * use hardcoded values PSR AUX transactions
	 */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		hsw_psr_setup_aux(intel_dp);

	if (intel_dp->psr.psr2_enabled && DISPLAY_VER(dev_priv) == 9) {
		i915_reg_t reg = CHICKEN_TRANS(cpu_transcoder);
		u32 chicken = intel_de_read(dev_priv, reg);

		chicken |= PSR2_VSC_ENABLE_PROG_HEADER |
			   PSR2_ADD_VERTICAL_LINE_COUNT;
		intel_de_write(dev_priv, reg, chicken);
	}

	/*
	 * Per Spec: Avoid continuous PSR exit by masking MEMUP and HPD also
	 * mask LPSP to avoid dependency on other drivers that might block
	 * runtime_pm besides preventing  other hw tracking issues now we
	 * can rely on frontbuffer tracking.
	 */
	mask = EDP_PSR_DEBUG_MASK_MEMUP |
	       EDP_PSR_DEBUG_MASK_HPD |
	       EDP_PSR_DEBUG_MASK_LPSP |
	       EDP_PSR_DEBUG_MASK_MAX_SLEEP;

	if (DISPLAY_VER(dev_priv) < 11)
		mask |= EDP_PSR_DEBUG_MASK_DISP_REG_WRITE;

	intel_de_write(dev_priv, EDP_PSR_DEBUG(intel_dp->psr.transcoder),
		       mask);

	psr_irq_control(intel_dp);

	if (intel_dp->psr.dc3co_exitline) {
		u32 val;

		/*
		 * TODO: if future platforms supports DC3CO in more than one
		 * transcoder, EXITLINE will need to be unset when disabling PSR
		 */
		val = intel_de_read(dev_priv, EXITLINE(cpu_transcoder));
		val &= ~EXITLINE_MASK;
		val |= intel_dp->psr.dc3co_exitline << EXITLINE_SHIFT;
		val |= EXITLINE_ENABLE;
		intel_de_write(dev_priv, EXITLINE(cpu_transcoder), val);
	}

	if (HAS_PSR_HW_TRACKING(dev_priv) && HAS_PSR2_SEL_FETCH(dev_priv))
		intel_de_rmw(dev_priv, CHICKEN_PAR1_1, IGNORE_PSR2_HW_TRACKING,
			     intel_dp->psr.psr2_sel_fetch_enabled ?
			     IGNORE_PSR2_HW_TRACKING : 0);

	/* Wa_16011168373:adl-p */
	if (IS_ADLP_DISPLAY_STEP(dev_priv, STEP_A0, STEP_B0) &&
	    intel_dp->psr.psr2_enabled)
		intel_de_rmw(dev_priv,
			     TRANS_SET_CONTEXT_LATENCY(intel_dp->psr.transcoder),
			     TRANS_SET_CONTEXT_LATENCY_MASK,
			     TRANS_SET_CONTEXT_LATENCY_VALUE(1));
}

static bool psr_interrupt_error_check(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 val;

	/*
	 * If a PSR error happened and the driver is reloaded, the EDP_PSR_IIR
	 * will still keep the error set even after the reset done in the
	 * irq_preinstall and irq_uninstall hooks.
	 * And enabling in this situation cause the screen to freeze in the
	 * first time that PSR HW tries to activate so lets keep PSR disabled
	 * to avoid any rendering problems.
	 */
	if (DISPLAY_VER(dev_priv) >= 12) {
		val = intel_de_read(dev_priv,
				    TRANS_PSR_IIR(intel_dp->psr.transcoder));
		val &= EDP_PSR_ERROR(0);
	} else {
		val = intel_de_read(dev_priv, EDP_PSR_IIR);
		val &= EDP_PSR_ERROR(intel_dp->psr.transcoder);
	}
	if (val) {
		intel_dp->psr.sink_not_reliable = true;
		drm_dbg_kms(&dev_priv->drm,
			    "PSR interruption error set, not enabling PSR\n");
		return false;
	}

	return true;
}

static void intel_psr_enable_locked(struct intel_dp *intel_dp,
				    const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	enum phy phy = intel_port_to_phy(dev_priv, dig_port->base.port);
	struct intel_encoder *encoder = &dig_port->base;
	u32 val;

	drm_WARN_ON(&dev_priv->drm, intel_dp->psr.enabled);

	intel_dp->psr.psr2_enabled = crtc_state->has_psr2;
	intel_dp->psr.busy_frontbuffer_bits = 0;
	intel_dp->psr.pipe = to_intel_crtc(crtc_state->uapi.crtc)->pipe;
	intel_dp->psr.transcoder = crtc_state->cpu_transcoder;
	/* DC5/DC6 requires at least 6 idle frames */
	val = usecs_to_jiffies(intel_get_frame_time_us(crtc_state) * 6);
	intel_dp->psr.dc3co_exit_delay = val;
	intel_dp->psr.dc3co_exitline = crtc_state->dc3co_exitline;
	intel_dp->psr.psr2_sel_fetch_enabled = crtc_state->enable_psr2_sel_fetch;
	intel_dp->psr.req_psr2_sdp_prior_scanline =
		crtc_state->req_psr2_sdp_prior_scanline;

	if (!psr_interrupt_error_check(intel_dp))
		return;

	drm_dbg_kms(&dev_priv->drm, "Enabling PSR%s\n",
		    intel_dp->psr.psr2_enabled ? "2" : "1");
	intel_dp_compute_psr_vsc_sdp(intel_dp, crtc_state, conn_state,
				     &intel_dp->psr.vsc);
	intel_write_dp_vsc_sdp(encoder, crtc_state, &intel_dp->psr.vsc);
	intel_snps_phy_update_psr_power_state(dev_priv, phy, true);
	intel_psr_enable_sink(intel_dp);
	intel_psr_enable_source(intel_dp);
	intel_dp->psr.enabled = true;
	intel_dp->psr.paused = false;

	intel_psr_activate(intel_dp);
}

/**
 * intel_psr_enable - Enable PSR
 * @intel_dp: Intel DP
 * @crtc_state: new CRTC state
 * @conn_state: new CONNECTOR state
 *
 * This function can only be called after the pipe is fully trained and enabled.
 */
void intel_psr_enable(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state,
		      const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!CAN_PSR(intel_dp))
		return;

	if (!crtc_state->has_psr)
		return;

	drm_WARN_ON(&dev_priv->drm, dev_priv->drrs.dp);

	mutex_lock(&intel_dp->psr.lock);
	intel_psr_enable_locked(intel_dp, crtc_state, conn_state);
	mutex_unlock(&intel_dp->psr.lock);
}

static void intel_psr_exit(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 val;

	if (!intel_dp->psr.active) {
		if (transcoder_has_psr2(dev_priv, intel_dp->psr.transcoder)) {
			val = intel_de_read(dev_priv,
					    EDP_PSR2_CTL(intel_dp->psr.transcoder));
			drm_WARN_ON(&dev_priv->drm, val & EDP_PSR2_ENABLE);
		}

		val = intel_de_read(dev_priv,
				    EDP_PSR_CTL(intel_dp->psr.transcoder));
		drm_WARN_ON(&dev_priv->drm, val & EDP_PSR_ENABLE);

		return;
	}

	if (intel_dp->psr.psr2_enabled) {
		tgl_disallow_dc3co_on_psr2_exit(intel_dp);
		val = intel_de_read(dev_priv,
				    EDP_PSR2_CTL(intel_dp->psr.transcoder));
		drm_WARN_ON(&dev_priv->drm, !(val & EDP_PSR2_ENABLE));
		val &= ~EDP_PSR2_ENABLE;
		intel_de_write(dev_priv,
			       EDP_PSR2_CTL(intel_dp->psr.transcoder), val);
	} else {
		val = intel_de_read(dev_priv,
				    EDP_PSR_CTL(intel_dp->psr.transcoder));
		drm_WARN_ON(&dev_priv->drm, !(val & EDP_PSR_ENABLE));
		val &= ~EDP_PSR_ENABLE;
		intel_de_write(dev_priv,
			       EDP_PSR_CTL(intel_dp->psr.transcoder), val);
	}
	intel_dp->psr.active = false;
}

static void intel_psr_wait_exit_locked(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	i915_reg_t psr_status;
	u32 psr_status_mask;

	if (intel_dp->psr.psr2_enabled) {
		psr_status = EDP_PSR2_STATUS(intel_dp->psr.transcoder);
		psr_status_mask = EDP_PSR2_STATUS_STATE_MASK;
	} else {
		psr_status = EDP_PSR_STATUS(intel_dp->psr.transcoder);
		psr_status_mask = EDP_PSR_STATUS_STATE_MASK;
	}

	/* Wait till PSR is idle */
	if (intel_de_wait_for_clear(dev_priv, psr_status,
				    psr_status_mask, 2000))
		drm_err(&dev_priv->drm, "Timed out waiting PSR idle state\n");
}

static void intel_psr_disable_locked(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	enum phy phy = intel_port_to_phy(dev_priv,
					 dp_to_dig_port(intel_dp)->base.port);

	lockdep_assert_held(&intel_dp->psr.lock);

	if (!intel_dp->psr.enabled)
		return;

	drm_dbg_kms(&dev_priv->drm, "Disabling PSR%s\n",
		    intel_dp->psr.psr2_enabled ? "2" : "1");

	intel_psr_exit(intel_dp);
	intel_psr_wait_exit_locked(intel_dp);

	/* Wa_1408330847 */
	if (intel_dp->psr.psr2_sel_fetch_enabled &&
	    IS_TGL_DISPLAY_STEP(dev_priv, STEP_A0, STEP_B0))
		intel_de_rmw(dev_priv, CHICKEN_PAR1_1,
			     DIS_RAM_BYPASS_PSR2_MAN_TRACK, 0);

	/* Wa_16011168373:adl-p */
	if (IS_ADLP_DISPLAY_STEP(dev_priv, STEP_A0, STEP_B0) &&
	    intel_dp->psr.psr2_enabled)
		intel_de_rmw(dev_priv,
			     TRANS_SET_CONTEXT_LATENCY(intel_dp->psr.transcoder),
			     TRANS_SET_CONTEXT_LATENCY_MASK, 0);

	intel_snps_phy_update_psr_power_state(dev_priv, phy, false);

	/* Disable PSR on Sink */
	drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_EN_CFG, 0);

	if (intel_dp->psr.psr2_enabled)
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_RECEIVER_ALPM_CONFIG, 0);

	intel_dp->psr.enabled = false;
}

/**
 * intel_psr_disable - Disable PSR
 * @intel_dp: Intel DP
 * @old_crtc_state: old CRTC state
 *
 * This function needs to be called before disabling pipe.
 */
void intel_psr_disable(struct intel_dp *intel_dp,
		       const struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!old_crtc_state->has_psr)
		return;

	if (drm_WARN_ON(&dev_priv->drm, !CAN_PSR(intel_dp)))
		return;

	mutex_lock(&intel_dp->psr.lock);

	intel_psr_disable_locked(intel_dp);

	mutex_unlock(&intel_dp->psr.lock);
	cancel_work_sync(&intel_dp->psr.work);
	cancel_delayed_work_sync(&intel_dp->psr.dc3co_work);
}

/**
 * intel_psr_pause - Pause PSR
 * @intel_dp: Intel DP
 *
 * This function need to be called after enabling psr.
 */
void intel_psr_pause(struct intel_dp *intel_dp)
{
	struct intel_psr *psr = &intel_dp->psr;

	if (!CAN_PSR(intel_dp))
		return;

	mutex_lock(&psr->lock);

	if (!psr->enabled) {
		mutex_unlock(&psr->lock);
		return;
	}

	intel_psr_exit(intel_dp);
	intel_psr_wait_exit_locked(intel_dp);
	psr->paused = true;

	mutex_unlock(&psr->lock);

	cancel_work_sync(&psr->work);
	cancel_delayed_work_sync(&psr->dc3co_work);
}

/**
 * intel_psr_resume - Resume PSR
 * @intel_dp: Intel DP
 *
 * This function need to be called after pausing psr.
 */
void intel_psr_resume(struct intel_dp *intel_dp)
{
	struct intel_psr *psr = &intel_dp->psr;

	if (!CAN_PSR(intel_dp))
		return;

	mutex_lock(&psr->lock);

	if (!psr->paused)
		goto unlock;

	psr->paused = false;
	intel_psr_activate(intel_dp);

unlock:
	mutex_unlock(&psr->lock);
}

static void psr_force_hw_tracking_exit(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (DISPLAY_VER(dev_priv) >= 9)
		/*
		 * Display WA #0884: skl+
		 * This documented WA for bxt can be safely applied
		 * broadly so we can force HW tracking to exit PSR
		 * instead of disabling and re-enabling.
		 * Workaround tells us to write 0 to CUR_SURFLIVE_A,
		 * but it makes more sense write to the current active
		 * pipe.
		 */
		intel_de_write(dev_priv, CURSURFLIVE(intel_dp->psr.pipe), 0);
	else
		/*
		 * A write to CURSURFLIVE do not cause HW tracking to exit PSR
		 * on older gens so doing the manual exit instead.
		 */
		intel_psr_exit(intel_dp);
}

void intel_psr2_program_plane_sel_fetch(struct intel_plane *plane,
					const struct intel_crtc_state *crtc_state,
					const struct intel_plane_state *plane_state,
					int color_plane)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	const struct drm_rect *clip;
	u32 val, offset;
	int ret, x, y;

	if (!crtc_state->enable_psr2_sel_fetch)
		return;

	val = plane_state ? plane_state->ctl : 0;
	val &= plane->id == PLANE_CURSOR ? val : PLANE_SEL_FETCH_CTL_ENABLE;
	intel_de_write_fw(dev_priv, PLANE_SEL_FETCH_CTL(pipe, plane->id), val);
	if (!val || plane->id == PLANE_CURSOR)
		return;

	clip = &plane_state->psr2_sel_fetch_area;

	val = (clip->y1 + plane_state->uapi.dst.y1) << 16;
	val |= plane_state->uapi.dst.x1;
	intel_de_write_fw(dev_priv, PLANE_SEL_FETCH_POS(pipe, plane->id), val);

	/* TODO: consider auxiliary surfaces */
	x = plane_state->uapi.src.x1 >> 16;
	y = (plane_state->uapi.src.y1 >> 16) + clip->y1;
	ret = skl_calc_main_surface_offset(plane_state, &x, &y, &offset);
	if (ret)
		drm_warn_once(&dev_priv->drm, "skl_calc_main_surface_offset() returned %i\n",
			      ret);
	val = y << 16 | x;
	intel_de_write_fw(dev_priv, PLANE_SEL_FETCH_OFFSET(pipe, plane->id),
			  val);

	/* Sizes are 0 based */
	val = (drm_rect_height(clip) - 1) << 16;
	val |= (drm_rect_width(&plane_state->uapi.src) >> 16) - 1;
	intel_de_write_fw(dev_priv, PLANE_SEL_FETCH_SIZE(pipe, plane->id), val);
}

void intel_psr2_program_trans_man_trk_ctl(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);

	if (!HAS_PSR2_SEL_FETCH(dev_priv) ||
	    !crtc_state->enable_psr2_sel_fetch)
		return;

	intel_de_write(dev_priv, PSR2_MAN_TRK_CTL(crtc_state->cpu_transcoder),
		       crtc_state->psr2_man_track_ctl);
}

static void psr2_man_trk_ctl_calc(struct intel_crtc_state *crtc_state,
				  struct drm_rect *clip, bool full_update)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 val = PSR2_MAN_TRK_CTL_ENABLE;

	if (full_update) {
		if (IS_ALDERLAKE_P(dev_priv))
			val |= ADLP_PSR2_MAN_TRK_CTL_SF_SINGLE_FULL_FRAME;
		else
			val |= PSR2_MAN_TRK_CTL_SF_SINGLE_FULL_FRAME;

		goto exit;
	}

	if (clip->y1 == -1)
		goto exit;

	if (IS_ALDERLAKE_P(dev_priv)) {
		val |= ADLP_PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR(clip->y1);
		val |= ADLP_PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR(clip->y2);
	} else {
		drm_WARN_ON(crtc_state->uapi.crtc->dev, clip->y1 % 4 || clip->y2 % 4);

		val |= PSR2_MAN_TRK_CTL_SF_PARTIAL_FRAME_UPDATE;
		val |= PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR(clip->y1 / 4 + 1);
		val |= PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR(clip->y2 / 4 + 1);
	}
exit:
	crtc_state->psr2_man_track_ctl = val;
}

static void clip_area_update(struct drm_rect *overlap_damage_area,
			     struct drm_rect *damage_area)
{
	if (overlap_damage_area->y1 == -1) {
		overlap_damage_area->y1 = damage_area->y1;
		overlap_damage_area->y2 = damage_area->y2;
		return;
	}

	if (damage_area->y1 < overlap_damage_area->y1)
		overlap_damage_area->y1 = damage_area->y1;

	if (damage_area->y2 > overlap_damage_area->y2)
		overlap_damage_area->y2 = damage_area->y2;
}

static void intel_psr2_sel_fetch_pipe_alignment(const struct intel_crtc_state *crtc_state,
						struct drm_rect *pipe_clip)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	const u16 y_alignment = crtc_state->su_y_granularity;

	pipe_clip->y1 -= pipe_clip->y1 % y_alignment;
	if (pipe_clip->y2 % y_alignment)
		pipe_clip->y2 = ((pipe_clip->y2 / y_alignment) + 1) * y_alignment;

	if (IS_ALDERLAKE_P(dev_priv) && crtc_state->dsc.compression_enable)
		drm_warn(&dev_priv->drm, "Missing PSR2 sel fetch alignment with DSC\n");
}

int intel_psr2_sel_fetch_update(struct intel_atomic_state *state,
				struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	struct drm_rect pipe_clip = { .x1 = 0, .y1 = -1, .x2 = INT_MAX, .y2 = -1 };
	struct intel_plane_state *new_plane_state, *old_plane_state;
	struct intel_plane *plane;
	bool full_update = false;
	int i, ret;

	if (!crtc_state->enable_psr2_sel_fetch)
		return 0;

	ret = drm_atomic_add_affected_planes(&state->base, &crtc->base);
	if (ret)
		return ret;

	/*
	 * Calculate minimal selective fetch area of each plane and calculate
	 * the pipe damaged area.
	 * In the next loop the plane selective fetch area will actually be set
	 * using whole pipe damaged area.
	 */
	for_each_oldnew_intel_plane_in_state(state, plane, old_plane_state,
					     new_plane_state, i) {
		struct drm_rect src, damaged_area = { .y1 = -1 };
		struct drm_mode_rect *damaged_clips;
		u32 num_clips, j;

		if (new_plane_state->uapi.crtc != crtc_state->uapi.crtc)
			continue;

		if (!new_plane_state->uapi.visible &&
		    !old_plane_state->uapi.visible)
			continue;

		/*
		 * TODO: Not clear how to handle planes with negative position,
		 * also planes are not updated if they have a negative X
		 * position so for now doing a full update in this cases
		 */
		if (new_plane_state->uapi.dst.y1 < 0 ||
		    new_plane_state->uapi.dst.x1 < 0) {
			full_update = true;
			break;
		}

		num_clips = drm_plane_get_damage_clips_count(&new_plane_state->uapi);

		/*
		 * If visibility or plane moved, mark the whole plane area as
		 * damaged as it needs to be complete redraw in the new and old
		 * position.
		 */
		if (new_plane_state->uapi.visible != old_plane_state->uapi.visible ||
		    !drm_rect_equals(&new_plane_state->uapi.dst,
				     &old_plane_state->uapi.dst)) {
			if (old_plane_state->uapi.visible) {
				damaged_area.y1 = old_plane_state->uapi.dst.y1;
				damaged_area.y2 = old_plane_state->uapi.dst.y2;
				clip_area_update(&pipe_clip, &damaged_area);
			}

			if (new_plane_state->uapi.visible) {
				damaged_area.y1 = new_plane_state->uapi.dst.y1;
				damaged_area.y2 = new_plane_state->uapi.dst.y2;
				clip_area_update(&pipe_clip, &damaged_area);
			}
			continue;
		} else if (new_plane_state->uapi.alpha != old_plane_state->uapi.alpha ||
			   (!num_clips &&
			    new_plane_state->uapi.fb != old_plane_state->uapi.fb)) {
			/*
			 * If the plane don't have damaged areas but the
			 * framebuffer changed or alpha changed, mark the whole
			 * plane area as damaged.
			 */
			damaged_area.y1 = new_plane_state->uapi.dst.y1;
			damaged_area.y2 = new_plane_state->uapi.dst.y2;
			clip_area_update(&pipe_clip, &damaged_area);
			continue;
		}

		drm_rect_fp_to_int(&src, &new_plane_state->uapi.src);
		damaged_clips = drm_plane_get_damage_clips(&new_plane_state->uapi);

		for (j = 0; j < num_clips; j++) {
			struct drm_rect clip;

			clip.x1 = damaged_clips[j].x1;
			clip.y1 = damaged_clips[j].y1;
			clip.x2 = damaged_clips[j].x2;
			clip.y2 = damaged_clips[j].y2;
			if (drm_rect_intersect(&clip, &src))
				clip_area_update(&damaged_area, &clip);
		}

		if (damaged_area.y1 == -1)
			continue;

		damaged_area.y1 += new_plane_state->uapi.dst.y1 - src.y1;
		damaged_area.y2 += new_plane_state->uapi.dst.y1 - src.y1;
		clip_area_update(&pipe_clip, &damaged_area);
	}

	if (full_update)
		goto skip_sel_fetch_set_loop;

	intel_psr2_sel_fetch_pipe_alignment(crtc_state, &pipe_clip);

	/*
	 * Now that we have the pipe damaged area check if it intersect with
	 * every plane, if it does set the plane selective fetch area.
	 */
	for_each_oldnew_intel_plane_in_state(state, plane, old_plane_state,
					     new_plane_state, i) {
		struct drm_rect *sel_fetch_area, inter;

		if (new_plane_state->uapi.crtc != crtc_state->uapi.crtc ||
		    !new_plane_state->uapi.visible)
			continue;

		inter = pipe_clip;
		if (!drm_rect_intersect(&inter, &new_plane_state->uapi.dst))
			continue;

		sel_fetch_area = &new_plane_state->psr2_sel_fetch_area;
		sel_fetch_area->y1 = inter.y1 - new_plane_state->uapi.dst.y1;
		sel_fetch_area->y2 = inter.y2 - new_plane_state->uapi.dst.y1;
		crtc_state->update_planes |= BIT(plane->id);
	}

skip_sel_fetch_set_loop:
	psr2_man_trk_ctl_calc(crtc_state, &pipe_clip, full_update);
	return 0;
}

/**
 * intel_psr_update - Update PSR state
 * @intel_dp: Intel DP
 * @crtc_state: new CRTC state
 * @conn_state: new CONNECTOR state
 *
 * This functions will update PSR states, disabling, enabling or switching PSR
 * version when executing fastsets. For full modeset, intel_psr_disable() and
 * intel_psr_enable() should be called instead.
 */
void intel_psr_update(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state,
		      const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_psr *psr = &intel_dp->psr;
	bool enable, psr2_enable;

	if (!CAN_PSR(intel_dp))
		return;

	mutex_lock(&intel_dp->psr.lock);

	enable = crtc_state->has_psr;
	psr2_enable = crtc_state->has_psr2;

	if (enable == psr->enabled && psr2_enable == psr->psr2_enabled &&
	    crtc_state->enable_psr2_sel_fetch == psr->psr2_sel_fetch_enabled) {
		/* Force a PSR exit when enabling CRC to avoid CRC timeouts */
		if (crtc_state->crc_enabled && psr->enabled)
			psr_force_hw_tracking_exit(intel_dp);
		else if (DISPLAY_VER(dev_priv) < 9 && psr->enabled) {
			/*
			 * Activate PSR again after a force exit when enabling
			 * CRC in older gens
			 */
			if (!intel_dp->psr.active &&
			    !intel_dp->psr.busy_frontbuffer_bits)
				schedule_work(&intel_dp->psr.work);
		}

		goto unlock;
	}

	if (psr->enabled)
		intel_psr_disable_locked(intel_dp);

	if (enable)
		intel_psr_enable_locked(intel_dp, crtc_state, conn_state);

unlock:
	mutex_unlock(&intel_dp->psr.lock);
}

/**
 * psr_wait_for_idle - wait for PSR1 to idle
 * @intel_dp: Intel DP
 * @out_value: PSR status in case of failure
 *
 * Returns: 0 on success or -ETIMEOUT if PSR status does not idle.
 *
 */
static int psr_wait_for_idle(struct intel_dp *intel_dp, u32 *out_value)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	/*
	 * From bspec: Panel Self Refresh (BDW+)
	 * Max. time for PSR to idle = Inverse of the refresh rate + 6 ms of
	 * exit training time + 1.5 ms of aux channel handshake. 50 ms is
	 * defensive enough to cover everything.
	 */
	return __intel_wait_for_register(&dev_priv->uncore,
					 EDP_PSR_STATUS(intel_dp->psr.transcoder),
					 EDP_PSR_STATUS_STATE_MASK,
					 EDP_PSR_STATUS_STATE_IDLE, 2, 50,
					 out_value);
}

/**
 * intel_psr_wait_for_idle - wait for PSR1 to idle
 * @new_crtc_state: new CRTC state
 *
 * This function is expected to be called from pipe_update_start() where it is
 * not expected to race with PSR enable or disable.
 */
void intel_psr_wait_for_idle(const struct intel_crtc_state *new_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(new_crtc_state->uapi.crtc->dev);
	struct intel_encoder *encoder;

	if (!new_crtc_state->has_psr)
		return;

	for_each_intel_encoder_mask_with_psr(&dev_priv->drm, encoder,
					     new_crtc_state->uapi.encoder_mask) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
		u32 psr_status;

		mutex_lock(&intel_dp->psr.lock);
		if (!intel_dp->psr.enabled || intel_dp->psr.psr2_enabled) {
			mutex_unlock(&intel_dp->psr.lock);
			continue;
		}

		/* when the PSR1 is enabled */
		if (psr_wait_for_idle(intel_dp, &psr_status))
			drm_err(&dev_priv->drm,
				"PSR idle timed out 0x%x, atomic update may fail\n",
				psr_status);
		mutex_unlock(&intel_dp->psr.lock);
	}
}

static bool __psr_wait_for_idle_locked(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	i915_reg_t reg;
	u32 mask;
	int err;

	if (!intel_dp->psr.enabled)
		return false;

	if (intel_dp->psr.psr2_enabled) {
		reg = EDP_PSR2_STATUS(intel_dp->psr.transcoder);
		mask = EDP_PSR2_STATUS_STATE_MASK;
	} else {
		reg = EDP_PSR_STATUS(intel_dp->psr.transcoder);
		mask = EDP_PSR_STATUS_STATE_MASK;
	}

	mutex_unlock(&intel_dp->psr.lock);

	err = intel_de_wait_for_clear(dev_priv, reg, mask, 50);
	if (err)
		drm_err(&dev_priv->drm,
			"Timed out waiting for PSR Idle for re-enable\n");

	/* After the unlocked wait, verify that PSR is still wanted! */
	mutex_lock(&intel_dp->psr.lock);
	return err == 0 && intel_dp->psr.enabled;
}

static int intel_psr_fastset_force(struct drm_i915_private *dev_priv)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	int err = 0;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);
	state->acquire_ctx = &ctx;

retry:

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(conn, &conn_iter) {
		struct drm_connector_state *conn_state;
		struct drm_crtc_state *crtc_state;

		if (conn->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		conn_state = drm_atomic_get_connector_state(state, conn);
		if (IS_ERR(conn_state)) {
			err = PTR_ERR(conn_state);
			break;
		}

		if (!conn_state->crtc)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, conn_state->crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			break;
		}

		/* Mark mode as changed to trigger a pipe->update() */
		crtc_state->mode_changed = true;
	}
	drm_connector_list_iter_end(&conn_iter);

	if (err == 0)
		err = drm_atomic_commit(state);

	if (err == -EDEADLK) {
		drm_atomic_state_clear(state);
		err = drm_modeset_backoff(&ctx);
		if (!err)
			goto retry;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	drm_atomic_state_put(state);

	return err;
}

int intel_psr_debug_set(struct intel_dp *intel_dp, u64 val)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	const u32 mode = val & I915_PSR_DEBUG_MODE_MASK;
	u32 old_mode;
	int ret;

	if (val & ~(I915_PSR_DEBUG_IRQ | I915_PSR_DEBUG_MODE_MASK) ||
	    mode > I915_PSR_DEBUG_ENABLE_SEL_FETCH) {
		drm_dbg_kms(&dev_priv->drm, "Invalid debug mask %llx\n", val);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&intel_dp->psr.lock);
	if (ret)
		return ret;

	old_mode = intel_dp->psr.debug & I915_PSR_DEBUG_MODE_MASK;
	intel_dp->psr.debug = val;

	/*
	 * Do it right away if it's already enabled, otherwise it will be done
	 * when enabling the source.
	 */
	if (intel_dp->psr.enabled)
		psr_irq_control(intel_dp);

	mutex_unlock(&intel_dp->psr.lock);

	if (old_mode != mode)
		ret = intel_psr_fastset_force(dev_priv);

	return ret;
}

static void intel_psr_handle_irq(struct intel_dp *intel_dp)
{
	struct intel_psr *psr = &intel_dp->psr;

	intel_psr_disable_locked(intel_dp);
	psr->sink_not_reliable = true;
	/* let's make sure that sink is awaken */
	drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER, DP_SET_POWER_D0);
}

static void intel_psr_work(struct work_struct *work)
{
	struct intel_dp *intel_dp =
		container_of(work, typeof(*intel_dp), psr.work);

	mutex_lock(&intel_dp->psr.lock);

	if (!intel_dp->psr.enabled)
		goto unlock;

	if (READ_ONCE(intel_dp->psr.irq_aux_error))
		intel_psr_handle_irq(intel_dp);

	/*
	 * We have to make sure PSR is ready for re-enable
	 * otherwise it keeps disabled until next full enable/disable cycle.
	 * PSR might take some time to get fully disabled
	 * and be ready for re-enable.
	 */
	if (!__psr_wait_for_idle_locked(intel_dp))
		goto unlock;

	/*
	 * The delayed work can race with an invalidate hence we need to
	 * recheck. Since psr_flush first clears this and then reschedules we
	 * won't ever miss a flush when bailing out here.
	 */
	if (intel_dp->psr.busy_frontbuffer_bits || intel_dp->psr.active)
		goto unlock;

	intel_psr_activate(intel_dp);
unlock:
	mutex_unlock(&intel_dp->psr.lock);
}

/**
 * intel_psr_invalidate - Invalidade PSR
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 * @origin: which operation caused the invalidate
 *
 * Since the hardware frontbuffer tracking has gaps we need to integrate
 * with the software frontbuffer tracking. This function gets called every
 * time frontbuffer rendering starts and a buffer gets dirtied. PSR must be
 * disabled if the frontbuffer mask contains a buffer relevant to PSR.
 *
 * Dirty frontbuffers relevant to PSR are tracked in busy_frontbuffer_bits."
 */
void intel_psr_invalidate(struct drm_i915_private *dev_priv,
			  unsigned frontbuffer_bits, enum fb_op_origin origin)
{
	struct intel_encoder *encoder;

	if (origin == ORIGIN_FLIP)
		return;

	for_each_intel_encoder_with_psr(&dev_priv->drm, encoder) {
		unsigned int pipe_frontbuffer_bits = frontbuffer_bits;
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		mutex_lock(&intel_dp->psr.lock);
		if (!intel_dp->psr.enabled) {
			mutex_unlock(&intel_dp->psr.lock);
			continue;
		}

		pipe_frontbuffer_bits &=
			INTEL_FRONTBUFFER_ALL_MASK(intel_dp->psr.pipe);
		intel_dp->psr.busy_frontbuffer_bits |= pipe_frontbuffer_bits;

		if (pipe_frontbuffer_bits)
			intel_psr_exit(intel_dp);

		mutex_unlock(&intel_dp->psr.lock);
	}
}
/*
 * When we will be completely rely on PSR2 S/W tracking in future,
 * intel_psr_flush() will invalidate and flush the PSR for ORIGIN_FLIP
 * event also therefore tgl_dc3co_flush() require to be changed
 * accordingly in future.
 */
static void
tgl_dc3co_flush(struct intel_dp *intel_dp, unsigned int frontbuffer_bits,
		enum fb_op_origin origin)
{
	mutex_lock(&intel_dp->psr.lock);

	if (!intel_dp->psr.dc3co_exitline)
		goto unlock;

	if (!intel_dp->psr.psr2_enabled || !intel_dp->psr.active)
		goto unlock;

	/*
	 * At every frontbuffer flush flip event modified delay of delayed work,
	 * when delayed work schedules that means display has been idle.
	 */
	if (!(frontbuffer_bits &
	    INTEL_FRONTBUFFER_ALL_MASK(intel_dp->psr.pipe)))
		goto unlock;

	tgl_psr2_enable_dc3co(intel_dp);
	mod_delayed_work(system_wq, &intel_dp->psr.dc3co_work,
			 intel_dp->psr.dc3co_exit_delay);

unlock:
	mutex_unlock(&intel_dp->psr.lock);
}

/**
 * intel_psr_flush - Flush PSR
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 * @origin: which operation caused the flush
 *
 * Since the hardware frontbuffer tracking has gaps we need to integrate
 * with the software frontbuffer tracking. This function gets called every
 * time frontbuffer rendering has completed and flushed out to memory. PSR
 * can be enabled again if no other frontbuffer relevant to PSR is dirty.
 *
 * Dirty frontbuffers relevant to PSR are tracked in busy_frontbuffer_bits.
 */
void intel_psr_flush(struct drm_i915_private *dev_priv,
		     unsigned frontbuffer_bits, enum fb_op_origin origin)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder_with_psr(&dev_priv->drm, encoder) {
		unsigned int pipe_frontbuffer_bits = frontbuffer_bits;
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (origin == ORIGIN_FLIP) {
			tgl_dc3co_flush(intel_dp, frontbuffer_bits, origin);
			continue;
		}

		mutex_lock(&intel_dp->psr.lock);
		if (!intel_dp->psr.enabled) {
			mutex_unlock(&intel_dp->psr.lock);
			continue;
		}

		pipe_frontbuffer_bits &=
			INTEL_FRONTBUFFER_ALL_MASK(intel_dp->psr.pipe);
		intel_dp->psr.busy_frontbuffer_bits &= ~pipe_frontbuffer_bits;

		/*
		 * If the PSR is paused by an explicit intel_psr_paused() call,
		 * we have to ensure that the PSR is not activated until
		 * intel_psr_resume() is called.
		 */
		if (intel_dp->psr.paused) {
			mutex_unlock(&intel_dp->psr.lock);
			continue;
		}

		/* By definition flush = invalidate + flush */
		if (pipe_frontbuffer_bits)
			psr_force_hw_tracking_exit(intel_dp);

		if (!intel_dp->psr.active && !intel_dp->psr.busy_frontbuffer_bits)
			schedule_work(&intel_dp->psr.work);
		mutex_unlock(&intel_dp->psr.lock);
	}
}

/**
 * intel_psr_init - Init basic PSR work and mutex.
 * @intel_dp: Intel DP
 *
 * This function is called after the initializing connector.
 * (the initializing of connector treats the handling of connector capabilities)
 * And it initializes basic PSR stuff for each DP Encoder.
 */
void intel_psr_init(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!HAS_PSR(dev_priv))
		return;

	/*
	 * HSW spec explicitly says PSR is tied to port A.
	 * BDW+ platforms have a instance of PSR registers per transcoder but
	 * BDW, GEN9 and GEN11 are not validated by HW team in other transcoder
	 * than eDP one.
	 * For now it only supports one instance of PSR for BDW, GEN9 and GEN11.
	 * So lets keep it hardcoded to PORT_A for BDW, GEN9 and GEN11.
	 * But GEN12 supports a instance of PSR registers per transcoder.
	 */
	if (DISPLAY_VER(dev_priv) < 12 && dig_port->base.port != PORT_A) {
		drm_dbg_kms(&dev_priv->drm,
			    "PSR condition failed: Port not supported\n");
		return;
	}

	intel_dp->psr.source_support = true;

	if (IS_HASWELL(dev_priv))
		/*
		 * HSW don't have PSR registers on the same space as transcoder
		 * so set this to a value that when subtract to the register
		 * in transcoder space results in the right offset for HSW
		 */
		dev_priv->hsw_psr_mmio_adjust = _SRD_CTL_EDP - _HSW_EDP_PSR_BASE;

	if (dev_priv->params.enable_psr == -1)
		if (DISPLAY_VER(dev_priv) < 9 || !dev_priv->vbt.psr.enable)
			dev_priv->params.enable_psr = 0;

	/* Set link_standby x link_off defaults */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		/* HSW and BDW require workarounds that we don't implement. */
		intel_dp->psr.link_standby = false;
	else if (DISPLAY_VER(dev_priv) < 12)
		/* For new platforms up to TGL let's respect VBT back again */
		intel_dp->psr.link_standby = dev_priv->vbt.psr.full_link;

	INIT_WORK(&intel_dp->psr.work, intel_psr_work);
	INIT_DELAYED_WORK(&intel_dp->psr.dc3co_work, tgl_dc3co_disable_work);
	mutex_init(&intel_dp->psr.lock);
}

static int psr_get_status_and_error_status(struct intel_dp *intel_dp,
					   u8 *status, u8 *error_status)
{
	struct drm_dp_aux *aux = &intel_dp->aux;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_PSR_STATUS, status);
	if (ret != 1)
		return ret;

	ret = drm_dp_dpcd_readb(aux, DP_PSR_ERROR_STATUS, error_status);
	if (ret != 1)
		return ret;

	*status = *status & DP_PSR_SINK_STATE_MASK;

	return 0;
}

static void psr_alpm_check(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct drm_dp_aux *aux = &intel_dp->aux;
	struct intel_psr *psr = &intel_dp->psr;
	u8 val;
	int r;

	if (!psr->psr2_enabled)
		return;

	r = drm_dp_dpcd_readb(aux, DP_RECEIVER_ALPM_STATUS, &val);
	if (r != 1) {
		drm_err(&dev_priv->drm, "Error reading ALPM status\n");
		return;
	}

	if (val & DP_ALPM_LOCK_TIMEOUT_ERROR) {
		intel_psr_disable_locked(intel_dp);
		psr->sink_not_reliable = true;
		drm_dbg_kms(&dev_priv->drm,
			    "ALPM lock timeout error, disabling PSR\n");

		/* Clearing error */
		drm_dp_dpcd_writeb(aux, DP_RECEIVER_ALPM_STATUS, val);
	}
}

static void psr_capability_changed_check(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_psr *psr = &intel_dp->psr;
	u8 val;
	int r;

	r = drm_dp_dpcd_readb(&intel_dp->aux, DP_PSR_ESI, &val);
	if (r != 1) {
		drm_err(&dev_priv->drm, "Error reading DP_PSR_ESI\n");
		return;
	}

	if (val & DP_PSR_CAPS_CHANGE) {
		intel_psr_disable_locked(intel_dp);
		psr->sink_not_reliable = true;
		drm_dbg_kms(&dev_priv->drm,
			    "Sink PSR capability changed, disabling PSR\n");

		/* Clearing it */
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_ESI, val);
	}
}

void intel_psr_short_pulse(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_psr *psr = &intel_dp->psr;
	u8 status, error_status;
	const u8 errors = DP_PSR_RFB_STORAGE_ERROR |
			  DP_PSR_VSC_SDP_UNCORRECTABLE_ERROR |
			  DP_PSR_LINK_CRC_ERROR;

	if (!CAN_PSR(intel_dp))
		return;

	mutex_lock(&psr->lock);

	if (!psr->enabled)
		goto exit;

	if (psr_get_status_and_error_status(intel_dp, &status, &error_status)) {
		drm_err(&dev_priv->drm,
			"Error reading PSR status or error status\n");
		goto exit;
	}

	if (status == DP_PSR_SINK_INTERNAL_ERROR || (error_status & errors)) {
		intel_psr_disable_locked(intel_dp);
		psr->sink_not_reliable = true;
	}

	if (status == DP_PSR_SINK_INTERNAL_ERROR && !error_status)
		drm_dbg_kms(&dev_priv->drm,
			    "PSR sink internal error, disabling PSR\n");
	if (error_status & DP_PSR_RFB_STORAGE_ERROR)
		drm_dbg_kms(&dev_priv->drm,
			    "PSR RFB storage error, disabling PSR\n");
	if (error_status & DP_PSR_VSC_SDP_UNCORRECTABLE_ERROR)
		drm_dbg_kms(&dev_priv->drm,
			    "PSR VSC SDP uncorrectable error, disabling PSR\n");
	if (error_status & DP_PSR_LINK_CRC_ERROR)
		drm_dbg_kms(&dev_priv->drm,
			    "PSR Link CRC error, disabling PSR\n");

	if (error_status & ~errors)
		drm_err(&dev_priv->drm,
			"PSR_ERROR_STATUS unhandled errors %x\n",
			error_status & ~errors);
	/* clear status register */
	drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_ERROR_STATUS, error_status);

	psr_alpm_check(intel_dp);
	psr_capability_changed_check(intel_dp);

exit:
	mutex_unlock(&psr->lock);
}

bool intel_psr_enabled(struct intel_dp *intel_dp)
{
	bool ret;

	if (!CAN_PSR(intel_dp))
		return false;

	mutex_lock(&intel_dp->psr.lock);
	ret = intel_dp->psr.enabled;
	mutex_unlock(&intel_dp->psr.lock);

	return ret;
}
