// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/types.h>
#include <drm/drm_vblank.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_dm_ism.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_trace.h"
#include "amdgpu_dm_kunit_helpers.h"


/**
 * dm_ism_next_state - Get next state based on current state and event
 * @current_state: current ISM state
 * @event: event being processed
 * @next_state: place to store the next state
 *
 * This function defines the idle state management FSM. Invalid transitions
 * are ignored and will not progress the FSM.
 */
STATIC_IFN_KUNIT
bool dm_ism_next_state(enum amdgpu_dm_ism_state current_state,
		       enum amdgpu_dm_ism_event event,
		       enum amdgpu_dm_ism_state *next_state)
{
	switch (STATE_EVENT(current_state, event)) {
	case STATE_EVENT(DM_ISM_STATE_FULL_POWER_RUNNING,
			 DM_ISM_EVENT_ENTER_IDLE_REQUESTED):
		*next_state = DM_ISM_STATE_HYSTERESIS_WAITING;
		break;
	case STATE_EVENT(DM_ISM_STATE_FULL_POWER_RUNNING,
			 DM_ISM_EVENT_BEGIN_CURSOR_UPDATE):
		*next_state = DM_ISM_STATE_FULL_POWER_BUSY;
		break;

	case STATE_EVENT(DM_ISM_STATE_FULL_POWER_BUSY,
			 DM_ISM_EVENT_ENTER_IDLE_REQUESTED):
		*next_state = DM_ISM_STATE_HYSTERESIS_BUSY;
		break;
	case STATE_EVENT(DM_ISM_STATE_FULL_POWER_BUSY,
			 DM_ISM_EVENT_END_CURSOR_UPDATE):
		*next_state = DM_ISM_STATE_FULL_POWER_RUNNING;
		break;

	case STATE_EVENT(DM_ISM_STATE_HYSTERESIS_WAITING,
			 DM_ISM_EVENT_EXIT_IDLE_REQUESTED):
		*next_state = DM_ISM_STATE_TIMER_ABORTED;
		break;
	case STATE_EVENT(DM_ISM_STATE_HYSTERESIS_WAITING,
			 DM_ISM_EVENT_BEGIN_CURSOR_UPDATE):
		*next_state = DM_ISM_STATE_HYSTERESIS_BUSY;
		break;
	case STATE_EVENT(DM_ISM_STATE_HYSTERESIS_WAITING,
			 DM_ISM_EVENT_TIMER_ELAPSED):
		*next_state = DM_ISM_STATE_OPTIMIZED_IDLE;
		break;
	case STATE_EVENT(DM_ISM_STATE_HYSTERESIS_WAITING,
			 DM_ISM_EVENT_IMMEDIATE):
		*next_state = DM_ISM_STATE_OPTIMIZED_IDLE;
		break;

	case STATE_EVENT(DM_ISM_STATE_HYSTERESIS_BUSY,
			 DM_ISM_EVENT_EXIT_IDLE_REQUESTED):
		*next_state = DM_ISM_STATE_FULL_POWER_BUSY;
		break;
	case STATE_EVENT(DM_ISM_STATE_HYSTERESIS_BUSY,
			 DM_ISM_EVENT_END_CURSOR_UPDATE):
		*next_state = DM_ISM_STATE_HYSTERESIS_WAITING;
		break;

	case STATE_EVENT(DM_ISM_STATE_OPTIMIZED_IDLE,
			 DM_ISM_EVENT_EXIT_IDLE_REQUESTED):
		*next_state = DM_ISM_STATE_FULL_POWER_RUNNING;
		break;
	case STATE_EVENT(DM_ISM_STATE_OPTIMIZED_IDLE,
			 DM_ISM_EVENT_BEGIN_CURSOR_UPDATE):
		*next_state = DM_ISM_STATE_HYSTERESIS_BUSY;
		break;
	case STATE_EVENT(DM_ISM_STATE_OPTIMIZED_IDLE,
			 DM_ISM_EVENT_SSO_TIMER_ELAPSED):
	case STATE_EVENT(DM_ISM_STATE_OPTIMIZED_IDLE,
			 DM_ISM_EVENT_IMMEDIATE):
		*next_state = DM_ISM_STATE_OPTIMIZED_IDLE_SSO;
		break;

	case STATE_EVENT(DM_ISM_STATE_OPTIMIZED_IDLE_SSO,
			 DM_ISM_EVENT_EXIT_IDLE_REQUESTED):
		*next_state = DM_ISM_STATE_FULL_POWER_RUNNING;
		break;
	case STATE_EVENT(DM_ISM_STATE_OPTIMIZED_IDLE_SSO,
			 DM_ISM_EVENT_BEGIN_CURSOR_UPDATE):
		*next_state = DM_ISM_STATE_HYSTERESIS_BUSY;
		break;

	case STATE_EVENT(DM_ISM_STATE_TIMER_ABORTED,
			 DM_ISM_EVENT_IMMEDIATE):
		*next_state = DM_ISM_STATE_FULL_POWER_RUNNING;
		break;

	default:
		return false;
	}
	return true;
}
EXPORT_IF_KUNIT(dm_ism_next_state);

STATIC_IFN_KUNIT
uint64_t dm_ism_get_sso_delay(const struct amdgpu_dm_ism *ism,
				     const struct dc_stream_state *stream)
{
	const struct amdgpu_dm_ism_config *config = &ism->config;
	uint32_t v_total, h_total;
	uint64_t one_frame_ns, sso_delay_ns;

	if (!stream)
		return 0;

	if (!config->sso_num_frames)
		return 0;

	v_total = stream->timing.v_total;
	h_total = stream->timing.h_total;

	one_frame_ns = div64_u64(v_total * h_total * 10000000ull,
				 stream->timing.pix_clk_100hz);
	sso_delay_ns = config->sso_num_frames * one_frame_ns;

	return sso_delay_ns;
}
EXPORT_IF_KUNIT(dm_ism_get_sso_delay);

/**
 * dm_ism_get_idle_allow_delay - Calculate hysteresis-based idle allow delay
 * @ism: ISM instance containing configuration, history, and current state
 * @stream: display stream used to derive frame timing values for delay
 *
 * Calculates the delay before allowing idle optimizations based on recent
 * idle history and the current stream timing.
 */
STATIC_IFN_KUNIT
uint64_t dm_ism_get_idle_allow_delay(const struct amdgpu_dm_ism *ism,
				     const struct dc_stream_state *stream)
{
	const struct amdgpu_dm_ism_config *config = &ism->config;
	uint32_t v_total, h_total;
	uint64_t one_frame_ns, short_idle_ns, old_hist_ns;
	uint32_t history_size;
	int pos;
	uint32_t short_idle_count = 0;
	uint64_t ret_ns = 0;

	if (!stream)
		return 0;

	if (!config->filter_num_frames)
		return 0;
	if (!config->filter_entry_count)
		return 0;
	if (!config->activation_num_delay_frames)
		return 0;

	v_total = stream->timing.v_total;
	h_total = stream->timing.h_total;

	one_frame_ns = div64_u64(v_total * h_total * 10000000ull,
				 stream->timing.pix_clk_100hz);

	short_idle_ns = config->filter_num_frames * one_frame_ns;
	old_hist_ns = config->filter_old_history_threshold * one_frame_ns;

	/*
	 * Look back into the recent history and count how many times we entered
	 * idle power state for a short duration of time
	 */
	history_size = min(
		max(config->filter_history_size, config->filter_entry_count),
		AMDGPU_DM_IDLE_HIST_LEN);
	pos = ism->next_record_idx;

	for (int k = 0; k < history_size; k++) {
		if (pos <= 0 || pos > AMDGPU_DM_IDLE_HIST_LEN)
			pos = AMDGPU_DM_IDLE_HIST_LEN;
		pos -= 1;

		if (ism->records[pos].duration_ns <= short_idle_ns)
			short_idle_count += 1;

		if (short_idle_count >= config->filter_entry_count)
			break;

		if (old_hist_ns > 0 &&
		    ism->last_idle_timestamp_ns - ism->records[pos].timestamp_ns > old_hist_ns)
			break;
	}

	if (short_idle_count >= config->filter_entry_count)
		ret_ns = config->activation_num_delay_frames * one_frame_ns;

	return ret_ns;
}
EXPORT_IF_KUNIT(dm_ism_get_idle_allow_delay);

/**
 * dm_ism_insert_record - Insert a record into the circular history buffer
 * @ism: ISM instance
 */
STATIC_IFN_KUNIT
void dm_ism_insert_record(struct amdgpu_dm_ism *ism)
{
	struct amdgpu_dm_ism_record *record;

	if (ism->next_record_idx < 0 ||
	    ism->next_record_idx >= AMDGPU_DM_IDLE_HIST_LEN)
		ism->next_record_idx = 0;

	record = &ism->records[ism->next_record_idx];
	ism->next_record_idx += 1;

	record->timestamp_ns = ktime_get_ns();
	record->duration_ns =
		record->timestamp_ns - ism->last_idle_timestamp_ns;
}
EXPORT_IF_KUNIT(dm_ism_insert_record);


STATIC_IFN_KUNIT
void dm_ism_set_last_idle_ts(struct amdgpu_dm_ism *ism)
{
	ism->last_idle_timestamp_ns = ktime_get_ns();
}
EXPORT_IF_KUNIT(dm_ism_set_last_idle_ts);


STATIC_IFN_KUNIT
bool dm_ism_trigger_event(struct amdgpu_dm_ism *ism,
				 enum amdgpu_dm_ism_event event)
{
	enum amdgpu_dm_ism_state next_state;

	bool gotNextState = dm_ism_next_state(ism->current_state, event,
					      &next_state);

	if (gotNextState) {
		ism->previous_state = ism->current_state;
		ism->current_state = next_state;
	}

	return gotNextState;
}
EXPORT_IF_KUNIT(dm_ism_trigger_event);


static void dm_ism_commit_idle_optimization_state(struct amdgpu_dm_ism *ism,
					     struct dc_stream_state *stream,
					     bool vblank_enabled,
					     bool allow_panel_sso)
{
	struct amdgpu_crtc *acrtc = ism_to_amdgpu_crtc(ism);
	struct amdgpu_device *adev = drm_to_adev(acrtc->base.dev);
	struct amdgpu_display_manager *dm = &adev->dm;

	trace_amdgpu_dm_ism_commit(dm->active_vblank_irq_count,
				   vblank_enabled,
				   allow_panel_sso);

	/*
	 * If there is an active vblank requestor, or if SSO is being engaged,
	 * then disallow idle optimizations.
	 */
	if (vblank_enabled || allow_panel_sso)
		dc_allow_idle_optimizations(dm->dc, false);

	/*
	 * Control PSR based on vblank requirements from OS
	 *
	 * If panel supports PSR SU/Replay, there's no need to exit self-refresh
	 * when OS is submitting fast atomic commits, as they can allow
	 * self-refresh during vblank periods.
	 */
	if (stream && stream->link) {
		/*
		 * If the OS requires vblank events (or vblank is otherwise enabled),
		 * do not allow static screen optimizations.
		 *
		 * Keep ism->allow_static_screen_optimizations unchanged so the
		 * hysteresis-based decision can be reused once vblank is disabled.
		 */
		allow_panel_sso = allow_panel_sso && !vblank_enabled;
		amdgpu_dm_crtc_set_static_screen_optimze(
			dm, stream, allow_panel_sso,
			acrtc->dm_irq_params.allow_sr_entry);
	}

	/*
	 * Check for any active drm vblank requestors on other CRTCs
	 * (dm->active_vblank_irq_count) before allowing HW-wide idle
	 * optimizations.
	 *
	 * There's no need to have a "balanced" check when disallowing idle
	 * optimizations at the start of this func -- we should disallow
	 * whenever there's *an* active CRTC.
	 */
	if (!vblank_enabled && dm->active_vblank_irq_count == 0) {
		dc_post_update_surfaces_to_stream(dm->dc);
		dc_allow_idle_optimizations(dm->dc, true);
	}
}

STATIC_IFN_KUNIT
enum amdgpu_dm_ism_event dm_ism_dispatch_next_event(
	enum amdgpu_dm_ism_state current_state,
	uint64_t delay_ns,
	uint64_t sso_delay_ns)
{
	switch (current_state) {
	case DM_ISM_STATE_HYSTERESIS_WAITING:
		if (delay_ns == 0)
			return DM_ISM_EVENT_IMMEDIATE;
		break;
	case DM_ISM_STATE_OPTIMIZED_IDLE:
		if (sso_delay_ns == 0)
			return DM_ISM_EVENT_IMMEDIATE;
		break;
	case DM_ISM_STATE_TIMER_ABORTED:
		return DM_ISM_EVENT_IMMEDIATE;
	default:
		break;
	}
	return DM_ISM_NUM_EVENTS;
}
EXPORT_IF_KUNIT(dm_ism_dispatch_next_event);

static enum amdgpu_dm_ism_event dm_ism_dispatch_power_state(
	struct amdgpu_dm_ism *ism,
	struct dm_crtc_state *acrtc_state)
{
	const struct amdgpu_dm_ism_config *config = &ism->config;
	uint64_t delay_ns = 0, sso_delay_ns = 0;

	switch (ism->previous_state) {
	case DM_ISM_STATE_HYSTERESIS_WAITING:
		/*
		 * Stop the timer if it was set, and we're not running from the
		 * idle allow worker.
		 */
		if (ism->current_state != DM_ISM_STATE_OPTIMIZED_IDLE &&
		    ism->current_state != DM_ISM_STATE_OPTIMIZED_IDLE_SSO)
			cancel_delayed_work(&ism->delayed_work);
		break;
	case DM_ISM_STATE_OPTIMIZED_IDLE:
		if (ism->current_state == DM_ISM_STATE_OPTIMIZED_IDLE_SSO)
			break;
		/* If idle disallow, cancel SSO work and insert record */
		cancel_delayed_work(&ism->sso_delayed_work);
		dm_ism_insert_record(ism);
		dm_ism_commit_idle_optimization_state(ism, acrtc_state->stream,
						      true, false);
		break;
	case DM_ISM_STATE_OPTIMIZED_IDLE_SSO:
		/* Disable idle optimization */
		dm_ism_insert_record(ism);
		dm_ism_commit_idle_optimization_state(ism, acrtc_state->stream,
						      true, false);
		break;
	default:
		break;
	}

	switch (ism->current_state) {
	case DM_ISM_STATE_HYSTERESIS_WAITING:
		dm_ism_set_last_idle_ts(ism);
		delay_ns = dm_ism_get_idle_allow_delay(ism, acrtc_state->stream);
		/* Schedule worker */
		if (delay_ns > 0)
			mod_delayed_work(system_dfl_wq, &ism->delayed_work,
					 nsecs_to_jiffies(delay_ns));
		break;
	case DM_ISM_STATE_OPTIMIZED_IDLE:
		sso_delay_ns = dm_ism_get_sso_delay(ism, acrtc_state->stream);
		if (sso_delay_ns > 0) {
			/*
			 * If sso_num_frames is less than hysteresis frames, it
			 * indicates that allowing idle here, then disallowing
			 * idle after sso_num_frames has expired, will likely
			 * have a negative power impact. Skip idle allow here,
			 * and let the sso_delayed_work handle it.
			 */
			if (config->sso_num_frames >= config->filter_num_frames)
				dm_ism_commit_idle_optimization_state(ism, acrtc_state->stream,
								      false, false);

			mod_delayed_work(system_dfl_wq,
					 &ism->sso_delayed_work,
					 nsecs_to_jiffies(sso_delay_ns));
		}
		break;
	case DM_ISM_STATE_OPTIMIZED_IDLE_SSO:
		/* Enable static screen optimizations. */
		dm_ism_commit_idle_optimization_state(ism, acrtc_state->stream,
						      false, true);
		break;
	case DM_ISM_STATE_TIMER_ABORTED:
		dm_ism_insert_record(ism);
		dm_ism_commit_idle_optimization_state(ism, acrtc_state->stream,
						      true, false);
		break;
	default:
		break;
	}

	return dm_ism_dispatch_next_event(ism->current_state, delay_ns, sso_delay_ns);
}

static char *dm_ism_events_str[DM_ISM_NUM_EVENTS] = {
	[DM_ISM_EVENT_IMMEDIATE] = "IMMEDIATE",
	[DM_ISM_EVENT_ENTER_IDLE_REQUESTED] = "ENTER_IDLE_REQUESTED",
	[DM_ISM_EVENT_EXIT_IDLE_REQUESTED] = "EXIT_IDLE_REQUESTED",
	[DM_ISM_EVENT_BEGIN_CURSOR_UPDATE] = "BEGIN_CURSOR_UPDATE",
	[DM_ISM_EVENT_END_CURSOR_UPDATE] = "END_CURSOR_UPDATE",
	[DM_ISM_EVENT_TIMER_ELAPSED] = "TIMER_ELAPSED",
	[DM_ISM_EVENT_SSO_TIMER_ELAPSED] = "SSO_TIMER_ELAPSED",
};

static char *dm_ism_states_str[DM_ISM_NUM_STATES] = {
	[DM_ISM_STATE_FULL_POWER_RUNNING] = "FULL_POWER_RUNNING",
	[DM_ISM_STATE_FULL_POWER_BUSY] = "FULL_POWER_BUSY",
	[DM_ISM_STATE_HYSTERESIS_WAITING] = "HYSTERESIS_WAITING",
	[DM_ISM_STATE_HYSTERESIS_BUSY] = "HYSTERESIS_BUSY",
	[DM_ISM_STATE_OPTIMIZED_IDLE] = "OPTIMIZED_IDLE",
	[DM_ISM_STATE_OPTIMIZED_IDLE_SSO] = "OPTIMIZED_IDLE_SSO",
	[DM_ISM_STATE_TIMER_ABORTED] = "TIMER_ABORTED",
};


void amdgpu_dm_ism_commit_event(struct amdgpu_dm_ism *ism,
				enum amdgpu_dm_ism_event event)
{
	enum amdgpu_dm_ism_event next_event = event;
	struct amdgpu_crtc *acrtc = ism_to_amdgpu_crtc(ism);
	struct amdgpu_device *adev = drm_to_adev(acrtc->base.dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(acrtc->base.state);

	/* ISM transitions must be called with dc_lock held */
	lockdep_assert_held(&dm->dc_lock);

	/* ISM should not run after dc is destroyed */
	ASSERT(dm->dc);

	if (!acrtc_state) {
		trace_amdgpu_dm_ism_event(acrtc->crtc_id, "NO_STATE",
					  "NO_STATE", "N/A");
		return;
	}

	do {
		bool transition = dm_ism_trigger_event(ism, event);

		next_event = DM_ISM_NUM_EVENTS;
		if (transition) {
			trace_amdgpu_dm_ism_event(
				acrtc->crtc_id,
				dm_ism_states_str[ism->previous_state],
				dm_ism_states_str[ism->current_state],
				dm_ism_events_str[event]);
			next_event = dm_ism_dispatch_power_state(ism, acrtc_state);
		} else {
			trace_amdgpu_dm_ism_event(
				acrtc->crtc_id,
				dm_ism_states_str[ism->current_state],
				dm_ism_states_str[ism->current_state],
				dm_ism_events_str[event]);
		}

		event = next_event;

	} while (next_event < DM_ISM_NUM_EVENTS);
}


static void dm_ism_delayed_work_func(struct work_struct *work)
{
	struct amdgpu_dm_ism *ism =
		container_of(work, struct amdgpu_dm_ism, delayed_work.work);
	struct amdgpu_crtc *acrtc = ism_to_amdgpu_crtc(ism);
	struct amdgpu_device *adev = drm_to_adev(acrtc->base.dev);
	struct amdgpu_display_manager *dm = &adev->dm;

	guard(mutex)(&dm->dc_lock);

	amdgpu_dm_ism_commit_event(ism, DM_ISM_EVENT_TIMER_ELAPSED);
}

static void dm_ism_sso_delayed_work_func(struct work_struct *work)
{
	struct amdgpu_dm_ism *ism =
		container_of(work, struct amdgpu_dm_ism, sso_delayed_work.work);
	struct amdgpu_crtc *acrtc = ism_to_amdgpu_crtc(ism);
	struct amdgpu_device *adev = drm_to_adev(acrtc->base.dev);
	struct amdgpu_display_manager *dm = &adev->dm;

	guard(mutex)(&dm->dc_lock);

	amdgpu_dm_ism_commit_event(ism, DM_ISM_EVENT_SSO_TIMER_ELAPSED);
}

/**
 * amdgpu_dm_ism_disable - Quiesce ISM workers
 *
 * @dm: The amdgpu display manager
 *
 * Cancels and disables any pending or in-flight ISM delayed work and waits
 * for in-progress work to finish. After this returns, no ISM worker can run
 * and subsequent mod_delayed_work() calls become no-ops via
 * clear_pending_if_disabled().
 *
 * Must NOT be called with dc_lock held: the workers themselves take dc_lock,
 * so a synchronous wait under dc_lock would deadlock.
 *
 * The caller is responsible for driving the FSM back to FULL_POWER_RUNNING
 * (under dc_lock) by calling amdgpu_dm_ism_force_full_power().
 */
void amdgpu_dm_ism_disable(struct amdgpu_display_manager *dm)
{
	struct drm_crtc *crtc;
	struct amdgpu_crtc *acrtc;
	struct amdgpu_dm_ism *ism;

	/*
	 * Caller must NOT hold dc_lock: the ISM delayed work handlers
	 * acquire dc_lock themselves, so waiting for them via
	 * disable_delayed_work_sync() while holding dc_lock would
	 * self-deadlock against an in-flight worker.
	 */
	lockdep_assert_not_held(&dm->dc_lock);

	drm_for_each_crtc(crtc, dm->ddev) {
		acrtc = to_amdgpu_crtc(crtc);
		ism = &acrtc->ism;

		disable_delayed_work_sync(&ism->delayed_work);
		disable_delayed_work_sync(&ism->sso_delayed_work);
	}
}

/**
 * amdgpu_dm_ism_force_full_power - Force every CRTC's ISM FSM to FULL_POWER
 *
 * @dm: The amdgpu display manager
 *
 * Sends DM_ISM_EVENT_EXIT_IDLE_REQUESTED to every CRTC's ISM, leaving each
 * FSM in FULL_POWER_RUNNING. Intended to be paired with
 * amdgpu_dm_ism_disable(): callers should first quiesce workers (without
 * dc_lock), then take dc_lock and call this helper.
 *
 * Must be called with dc_lock held.
 */
void amdgpu_dm_ism_force_full_power(struct amdgpu_display_manager *dm)
{
	struct drm_crtc *crtc;
	struct amdgpu_crtc *acrtc;

	/*
	 * Caller must hold dc_lock: commit_event() drives the FSM and
	 * may touch dc state via dc_allow_idle_optimizations() etc.
	 */
	lockdep_assert_held(&dm->dc_lock);

	drm_for_each_crtc(crtc, dm->ddev) {
		acrtc = to_amdgpu_crtc(crtc);

		/*
		 * When disabled, leave in FULL_POWER_RUNNING state.
		 * EXIT_IDLE will not queue any work.
		 */
		amdgpu_dm_ism_commit_event(&acrtc->ism,
					   DM_ISM_EVENT_EXIT_IDLE_REQUESTED);
	}
}

/**
 * amdgpu_dm_ism_enable - enable the ISM
 *
 * @dm: The amdgpu display manager
 *
 * Re-enable the idle state manager by enabling work that was disabled by
 * amdgpu_dm_ism_disable.
 */
void amdgpu_dm_ism_enable(struct amdgpu_display_manager *dm)
{
	struct drm_crtc *crtc;
	struct amdgpu_crtc *acrtc;
	struct amdgpu_dm_ism *ism;

	drm_for_each_crtc(crtc, dm->ddev) {
		acrtc = to_amdgpu_crtc(crtc);
		ism = &acrtc->ism;

		enable_delayed_work(&ism->delayed_work);
		enable_delayed_work(&ism->sso_delayed_work);
	}
}

void amdgpu_dm_ism_init(struct amdgpu_dm_ism *ism,
			struct amdgpu_dm_ism_config *config)
{
	ism->config = *config;

	ism->current_state = DM_ISM_STATE_FULL_POWER_RUNNING;
	ism->previous_state = DM_ISM_STATE_FULL_POWER_RUNNING;
	ism->next_record_idx = 0;
	ism->last_idle_timestamp_ns = 0;

	INIT_DELAYED_WORK(&ism->delayed_work, dm_ism_delayed_work_func);
	INIT_DELAYED_WORK(&ism->sso_delayed_work, dm_ism_sso_delayed_work_func);
}
EXPORT_IF_KUNIT(amdgpu_dm_ism_init);


void amdgpu_dm_ism_fini(struct amdgpu_dm_ism *ism)
{
	cancel_delayed_work_sync(&ism->sso_delayed_work);
	cancel_delayed_work_sync(&ism->delayed_work);
}
EXPORT_IF_KUNIT(amdgpu_dm_ism_fini);
