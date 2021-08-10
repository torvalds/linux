// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include "backend/gpu/mali_kbase_clk_rate_trace_mgr.h"
#include "mali_kbase_csf_ipa_control.h"

/*
 * Status flags from the STATUS register of the IPA Control interface.
 */
#define STATUS_COMMAND_ACTIVE ((u32)1 << 0)
#define STATUS_TIMER_ACTIVE ((u32)1 << 1)
#define STATUS_AUTO_ACTIVE ((u32)1 << 2)
#define STATUS_PROTECTED_MODE ((u32)1 << 8)
#define STATUS_RESET ((u32)1 << 9)
#define STATUS_TIMER_ENABLED ((u32)1 << 31)

/*
 * Commands for the COMMAND register of the IPA Control interface.
 */
#define COMMAND_NOP ((u32)0)
#define COMMAND_APPLY ((u32)1)
#define COMMAND_CLEAR ((u32)2)
#define COMMAND_SAMPLE ((u32)3)
#define COMMAND_PROTECTED_ACK ((u32)4)
#define COMMAND_RESET_ACK ((u32)5)

/**
 * Default value for the TIMER register of the IPA Control interface,
 * expressed in milliseconds.
 *
 * The chosen value is a trade off between two requirements: the IPA Control
 * interface should sample counters with a resolution in the order of
 * milliseconds, while keeping GPU overhead as limited as possible.
 */
#define TIMER_DEFAULT_VALUE_MS ((u32)10) /* 10 milliseconds */

/**
 * Number of timer events per second.
 */
#define TIMER_EVENTS_PER_SECOND ((u32)1000 / TIMER_DEFAULT_VALUE_MS)

/**
 * Maximum number of loops polling the GPU before we assume the GPU has hung.
 */
#define IPA_INACTIVE_MAX_LOOPS ((unsigned int)8000000)

/**
 * Number of bits used to configure a performance counter in SELECT registers.
 */
#define IPA_CONTROL_SELECT_BITS_PER_CNT ((u64)8)

/**
 * Maximum value of a performance counter.
 */
#define MAX_PRFCNT_VALUE (((u64)1 << 48) - 1)

/**
 * struct kbase_ipa_control_listener_data - Data for the GPU clock frequency
 *                                          listener
 *
 * @listener: GPU clock frequency listener.
 * @kbdev:    Pointer to kbase device.
 */
struct kbase_ipa_control_listener_data {
	struct kbase_clk_rate_listener listener;
	struct kbase_device *kbdev;
};

static u32 timer_value(u32 gpu_rate)
{
	return gpu_rate / TIMER_EVENTS_PER_SECOND;
}

static int wait_status(struct kbase_device *kbdev, u32 flags)
{
	unsigned int max_loops = IPA_INACTIVE_MAX_LOOPS;
	u32 status = kbase_reg_read(kbdev, IPA_CONTROL_REG(STATUS));

	/*
	 * Wait for the STATUS register to indicate that flags have been
	 * cleared, in case a transition is pending.
	 */
	while (--max_loops && (status & flags))
		status = kbase_reg_read(kbdev, IPA_CONTROL_REG(STATUS));
	if (max_loops == 0) {
		dev_err(kbdev->dev, "IPA_CONTROL STATUS register stuck");
		return -EBUSY;
	}

	return 0;
}

static int apply_select_config(struct kbase_device *kbdev, u64 *select)
{
	int ret;

	u32 select_cshw_lo = (u32)(select[KBASE_IPA_CORE_TYPE_CSHW] & U32_MAX);
	u32 select_cshw_hi =
		(u32)((select[KBASE_IPA_CORE_TYPE_CSHW] >> 32) & U32_MAX);
	u32 select_memsys_lo =
		(u32)(select[KBASE_IPA_CORE_TYPE_MEMSYS] & U32_MAX);
	u32 select_memsys_hi =
		(u32)((select[KBASE_IPA_CORE_TYPE_MEMSYS] >> 32) & U32_MAX);
	u32 select_tiler_lo =
		(u32)(select[KBASE_IPA_CORE_TYPE_TILER] & U32_MAX);
	u32 select_tiler_hi =
		(u32)((select[KBASE_IPA_CORE_TYPE_TILER] >> 32) & U32_MAX);
	u32 select_shader_lo =
		(u32)(select[KBASE_IPA_CORE_TYPE_SHADER] & U32_MAX);
	u32 select_shader_hi =
		(u32)((select[KBASE_IPA_CORE_TYPE_SHADER] >> 32) & U32_MAX);

	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_CSHW_LO), select_cshw_lo);
	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_CSHW_HI), select_cshw_hi);
	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_MEMSYS_LO),
			select_memsys_lo);
	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_MEMSYS_HI),
			select_memsys_hi);
	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_TILER_LO),
			select_tiler_lo);
	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_TILER_HI),
			select_tiler_hi);
	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_SHADER_LO),
			select_shader_lo);
	kbase_reg_write(kbdev, IPA_CONTROL_REG(SELECT_SHADER_HI),
			select_shader_hi);

	ret = wait_status(kbdev, STATUS_COMMAND_ACTIVE);

	if (!ret)
		kbase_reg_write(kbdev, IPA_CONTROL_REG(COMMAND), COMMAND_APPLY);

	return ret;
}

static u64 read_value_cnt(struct kbase_device *kbdev, u8 type, int select_idx)
{
	u32 value_lo, value_hi;

	switch (type) {
	case KBASE_IPA_CORE_TYPE_CSHW:
		value_lo = kbase_reg_read(
			kbdev, IPA_CONTROL_REG(VALUE_CSHW_REG_LO(select_idx)));
		value_hi = kbase_reg_read(
			kbdev, IPA_CONTROL_REG(VALUE_CSHW_REG_HI(select_idx)));
		break;
	case KBASE_IPA_CORE_TYPE_MEMSYS:
		value_lo = kbase_reg_read(
			kbdev,
			IPA_CONTROL_REG(VALUE_MEMSYS_REG_LO(select_idx)));
		value_hi = kbase_reg_read(
			kbdev,
			IPA_CONTROL_REG(VALUE_MEMSYS_REG_HI(select_idx)));
		break;
	case KBASE_IPA_CORE_TYPE_TILER:
		value_lo = kbase_reg_read(
			kbdev, IPA_CONTROL_REG(VALUE_TILER_REG_LO(select_idx)));
		value_hi = kbase_reg_read(
			kbdev, IPA_CONTROL_REG(VALUE_TILER_REG_HI(select_idx)));
		break;
	case KBASE_IPA_CORE_TYPE_SHADER:
		value_lo = kbase_reg_read(
			kbdev,
			IPA_CONTROL_REG(VALUE_SHADER_REG_LO(select_idx)));
		value_hi = kbase_reg_read(
			kbdev,
			IPA_CONTROL_REG(VALUE_SHADER_REG_HI(select_idx)));
		break;
	default:
		WARN(1, "Unknown core type: %u\n", type);
		value_lo = value_hi = 0;
		break;
	}

	return (((u64)value_hi << 32) | value_lo);
}

static void build_select_config(struct kbase_ipa_control *ipa_ctrl,
				u64 *select_config)
{
	size_t i;

	for (i = 0; i < KBASE_IPA_CORE_TYPE_NUM; i++) {
		size_t j;

		select_config[i] = 0ULL;

		for (j = 0; j < KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS; j++) {
			struct kbase_ipa_control_prfcnt_config *prfcnt_config =
				&ipa_ctrl->blocks[i].select[j];

			select_config[i] |=
				((u64)prfcnt_config->idx
				 << (IPA_CONTROL_SELECT_BITS_PER_CNT * j));
		}
	}
}

static inline void calc_prfcnt_delta(struct kbase_device *kbdev,
				     struct kbase_ipa_control_prfcnt *prfcnt,
				     bool gpu_ready)
{
	u64 delta_value, raw_value;

	if (gpu_ready)
		raw_value = read_value_cnt(kbdev, (u8)prfcnt->type,
					   prfcnt->select_idx);
	else
		raw_value = prfcnt->latest_raw_value;

	if (raw_value < prfcnt->latest_raw_value) {
		delta_value = (MAX_PRFCNT_VALUE - prfcnt->latest_raw_value) +
			      raw_value;
	} else {
		delta_value = raw_value - prfcnt->latest_raw_value;
	}

	delta_value *= prfcnt->scaling_factor;

	if (!WARN_ON_ONCE(kbdev->csf.ipa_control.cur_gpu_rate == 0))
		if (prfcnt->gpu_norm)
			delta_value /= kbdev->csf.ipa_control.cur_gpu_rate;

	prfcnt->latest_raw_value = raw_value;

	/* Accumulate the difference */
	prfcnt->accumulated_diff += delta_value;
}

/**
 * kbase_ipa_control_rate_change_notify - GPU frequency change callback
 *
 * @listener:     Clock frequency change listener.
 * @clk_index:    Index of the clock for which the change has occurred.
 * @clk_rate_hz:  Clock frequency(Hz).
 *
 * This callback notifies kbase_ipa_control about GPU frequency changes.
 * Only top-level clock changes are meaningful. GPU frequency updates
 * affect all performance counters which require GPU normalization
 * in every session.
 */
static void
kbase_ipa_control_rate_change_notify(struct kbase_clk_rate_listener *listener,
				     u32 clk_index, u32 clk_rate_hz)
{
	if ((clk_index == KBASE_CLOCK_DOMAIN_TOP) && (clk_rate_hz != 0)) {
		size_t i;
		unsigned long flags;
		struct kbase_ipa_control_listener_data *listener_data =
			container_of(listener,
				     struct kbase_ipa_control_listener_data,
				     listener);
		struct kbase_device *kbdev = listener_data->kbdev;
		struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		if (!kbdev->pm.backend.gpu_ready) {
			dev_err(kbdev->dev,
				"%s: GPU frequency cannot change while GPU is off",
				__func__);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			return;
		}

		/* Interrupts are already disabled and interrupt state is also saved */
		spin_lock(&ipa_ctrl->lock);

		for (i = 0; i < ipa_ctrl->num_active_sessions; i++) {
			size_t j;
			struct kbase_ipa_control_session *session = &ipa_ctrl->sessions[i];

			for (j = 0; j < session->num_prfcnts; j++) {
				struct kbase_ipa_control_prfcnt *prfcnt =
					&session->prfcnts[j];

				if (prfcnt->gpu_norm)
					calc_prfcnt_delta(kbdev, prfcnt, true);
			 }
		}

		ipa_ctrl->cur_gpu_rate = clk_rate_hz;

		/* Update the timer for automatic sampling if active sessions
		 * are present. Counters have already been manually sampled.
		 */
		if (ipa_ctrl->num_active_sessions > 0) {
			kbase_reg_write(kbdev, IPA_CONTROL_REG(TIMER),
					timer_value(ipa_ctrl->cur_gpu_rate));
		}

		spin_unlock(&ipa_ctrl->lock);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
}

void kbase_ipa_control_init(struct kbase_device *kbdev)
{
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;
	struct kbase_clk_rate_trace_manager *clk_rtm = &kbdev->pm.clk_rtm;
	struct kbase_ipa_control_listener_data *listener_data;
	size_t i, j;

	for (i = 0; i < KBASE_IPA_CORE_TYPE_NUM; i++) {
		for (j = 0; j < KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS; j++) {
			ipa_ctrl->blocks[i].select[j].idx = 0;
			ipa_ctrl->blocks[i].select[j].refcount = 0;
		}
		ipa_ctrl->blocks[i].num_available_counters =
			KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS;
	}

	spin_lock_init(&ipa_ctrl->lock);
	ipa_ctrl->num_active_sessions = 0;
	for (i = 0; i < KBASE_IPA_CONTROL_MAX_SESSIONS; i++) {
		ipa_ctrl->sessions[i].active = false;
	}

	listener_data = kmalloc(sizeof(struct kbase_ipa_control_listener_data),
				GFP_KERNEL);
	if (listener_data) {
		listener_data->listener.notify =
			kbase_ipa_control_rate_change_notify;
		listener_data->kbdev = kbdev;
		ipa_ctrl->rtm_listener_data = listener_data;
	}

	spin_lock(&clk_rtm->lock);
	if (clk_rtm->clks[KBASE_CLOCK_DOMAIN_TOP])
		ipa_ctrl->cur_gpu_rate =
			clk_rtm->clks[KBASE_CLOCK_DOMAIN_TOP]->clock_val;
	if (listener_data)
		kbase_clk_rate_trace_manager_subscribe_no_lock(
			clk_rtm, &listener_data->listener);
	spin_unlock(&clk_rtm->lock);
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_init);

void kbase_ipa_control_term(struct kbase_device *kbdev)
{
	unsigned long flags;
	struct kbase_clk_rate_trace_manager *clk_rtm = &kbdev->pm.clk_rtm;
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;
	struct kbase_ipa_control_listener_data *listener_data =
		ipa_ctrl->rtm_listener_data;

	WARN_ON(ipa_ctrl->num_active_sessions);

	if (listener_data)
		kbase_clk_rate_trace_manager_unsubscribe(clk_rtm, &listener_data->listener);
	kfree(ipa_ctrl->rtm_listener_data);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (kbdev->pm.backend.gpu_powered)
		kbase_reg_write(kbdev, IPA_CONTROL_REG(TIMER), 0);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_term);

int kbase_ipa_control_register(
	struct kbase_device *kbdev,
	const struct kbase_ipa_control_perf_counter *perf_counters,
	size_t num_counters, void **client)
{
	int ret = 0;
	size_t i, session_idx, req_counters[KBASE_IPA_CORE_TYPE_NUM];
	bool already_configured[KBASE_IPA_CONTROL_MAX_COUNTERS];
	bool new_config = false;
	struct kbase_ipa_control *ipa_ctrl;
	struct kbase_ipa_control_session *session = NULL;
	unsigned long flags;

	if (WARN_ON(kbdev == NULL) || WARN_ON(perf_counters == NULL) ||
	    WARN_ON(client == NULL) ||
	    WARN_ON(num_counters > KBASE_IPA_CONTROL_MAX_COUNTERS)) {
		dev_err(kbdev->dev, "%s: wrong input arguments", __func__);
		return -EINVAL;
	}

	kbase_pm_context_active(kbdev);

	ipa_ctrl = &kbdev->csf.ipa_control;
	spin_lock_irqsave(&ipa_ctrl->lock, flags);

	if (ipa_ctrl->num_active_sessions == KBASE_IPA_CONTROL_MAX_SESSIONS) {
		dev_err(kbdev->dev, "%s: too many sessions", __func__);
		ret = -EBUSY;
		goto exit;
	}

	for (i = 0; i < KBASE_IPA_CORE_TYPE_NUM; i++)
		req_counters[i] = 0;

	/*
	 * Count how many counters would need to be configured in order to
	 * satisfy the request. Requested counters which happen to be already
	 * configured can be skipped.
	 */
	for (i = 0; i < num_counters; i++) {
		size_t j;
		enum kbase_ipa_core_type type = perf_counters[i].type;
		u8 idx = perf_counters[i].idx;

		if ((type >= KBASE_IPA_CORE_TYPE_NUM) ||
		    (idx >= KBASE_IPA_CONTROL_CNT_MAX_IDX)) {
			dev_err(kbdev->dev,
				"%s: invalid requested type %u and/or index %u",
				__func__, type, idx);
			ret = -EINVAL;
			goto exit;
		}

		for (j = 0; j < KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS; j++) {
			struct kbase_ipa_control_prfcnt_config *prfcnt_config =
				&ipa_ctrl->blocks[type].select[j];

			if (prfcnt_config->refcount > 0) {
				if (prfcnt_config->idx == idx) {
					already_configured[i] = true;
					break;
				}
			}
		}

		if (j == KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS) {
			already_configured[i] = false;
			req_counters[type]++;
			new_config = true;
		}
	}

	for (i = 0; i < KBASE_IPA_CORE_TYPE_NUM; i++)
		if (req_counters[i] >
		    ipa_ctrl->blocks[i].num_available_counters) {
			dev_err(kbdev->dev,
				"%s: more counters (%zu) than available (%zu) have been requested for type %zu",
				__func__, req_counters[i],
				ipa_ctrl->blocks[i].num_available_counters, i);
			ret = -EINVAL;
			goto exit;
		}

	/*
	 * The request has been validated.
	 * Firstly, find an available session and then set up the initial state
	 * of the session and update the configuration of performance counters
	 * in the internal state of kbase_ipa_control.
	 */
	for (session_idx = 0; session_idx < KBASE_IPA_CONTROL_MAX_SESSIONS;
	     session_idx++) {
		session = &ipa_ctrl->sessions[session_idx];
		if (!session->active)
			break;
	}

	if (!session) {
		dev_err(kbdev->dev, "%s: wrong or corrupt session state",
			__func__);
		ret = -EBUSY;
		goto exit;
	}

	for (i = 0; i < num_counters; i++) {
		struct kbase_ipa_control_prfcnt_config *prfcnt_config;
		size_t j;
		u8 type = perf_counters[i].type;
		u8 idx = perf_counters[i].idx;

		for (j = 0; j < KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS; j++) {
			prfcnt_config = &ipa_ctrl->blocks[type].select[j];

			if (already_configured[i]) {
				if ((prfcnt_config->refcount > 0) &&
				    (prfcnt_config->idx == idx)) {
					break;
				}
			} else {
				if (prfcnt_config->refcount == 0)
					break;
			}
		}

		if (WARN_ON((prfcnt_config->refcount > 0 &&
			     prfcnt_config->idx != idx) ||
			    (j == KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS))) {
			dev_err(kbdev->dev,
				"%s: invalid internal state: counter already configured or no counter available to configure",
				__func__);
			ret = -EBUSY;
			goto exit;
		}

		if (prfcnt_config->refcount == 0) {
			prfcnt_config->idx = idx;
			ipa_ctrl->blocks[type].num_available_counters--;
		}

		session->prfcnts[i].accumulated_diff = 0;
		session->prfcnts[i].type = type;
		session->prfcnts[i].select_idx = j;
		session->prfcnts[i].scaling_factor =
			perf_counters[i].scaling_factor;
		session->prfcnts[i].gpu_norm = perf_counters[i].gpu_norm;

		/* Reports to this client for GPU time spent in protected mode
		 * should begin from the point of registration.
		 */
		session->last_query_time = ktime_get_ns();

		/* Initially, no time has been spent in protected mode */
		session->protm_time = 0;

		prfcnt_config->refcount++;
	}

	/*
	 * Apply new configuration, if necessary.
	 * As a temporary solution, make sure that the GPU is on
	 * before applying the new configuration.
	 */
	if (new_config) {
		u64 select_config[KBASE_IPA_CORE_TYPE_NUM];

		build_select_config(ipa_ctrl, select_config);
		ret = apply_select_config(kbdev, select_config);
		if (ret)
			dev_err(kbdev->dev,
				"%s: failed to apply SELECT configuration",
				__func__);
	}

	if (!ret) {
		/* Accumulator registers don't contain any sample if the timer
		 * has not been enabled first. Take a sample manually before
		 * enabling the timer.
		 */
		if (ipa_ctrl->num_active_sessions == 0) {
			kbase_reg_write(kbdev, IPA_CONTROL_REG(COMMAND),
					COMMAND_SAMPLE);
			ret = wait_status(kbdev, STATUS_COMMAND_ACTIVE);
			if (!ret) {
				kbase_reg_write(
					kbdev, IPA_CONTROL_REG(TIMER),
					timer_value(ipa_ctrl->cur_gpu_rate));
			} else {
				dev_err(kbdev->dev,
					"%s: failed to sample new counters",
					__func__);
			}
		}
	}

	if (!ret) {
		session->num_prfcnts = num_counters;
		session->active = true;
		ipa_ctrl->num_active_sessions++;
		*client = session;

		/*
		 * Read current raw value to initialize the session.
		 * This is necessary to put the first query in condition
		 * to generate a correct value by calculating the difference
		 * from the beginning of the session.
		 */
		for (i = 0; i < session->num_prfcnts; i++) {
			struct kbase_ipa_control_prfcnt *prfcnt =
				&session->prfcnts[i];
			u64 raw_value = read_value_cnt(kbdev, (u8)prfcnt->type,
						       prfcnt->select_idx);
			prfcnt->latest_raw_value = raw_value;
		}
	}

exit:
	spin_unlock_irqrestore(&ipa_ctrl->lock, flags);
	kbase_pm_context_idle(kbdev);
	return ret;
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_register);

int kbase_ipa_control_unregister(struct kbase_device *kbdev, const void *client)
{
	struct kbase_ipa_control *ipa_ctrl;
	struct kbase_ipa_control_session *session;
	int ret = 0;
	size_t i;
	unsigned long flags;
	bool new_config = false, valid_session = false;

	if (WARN_ON(kbdev == NULL) || WARN_ON(client == NULL)) {
		dev_err(kbdev->dev, "%s: wrong input arguments", __func__);
		return -EINVAL;
	}

	kbase_pm_context_active(kbdev);

	ipa_ctrl = &kbdev->csf.ipa_control;
	session = (struct kbase_ipa_control_session *)client;

	spin_lock_irqsave(&ipa_ctrl->lock, flags);

	for (i = 0; i < KBASE_IPA_CONTROL_MAX_SESSIONS; i++) {
		if (session == &ipa_ctrl->sessions[i]) {
			valid_session = true;
			break;
		}
	}

	if (!valid_session) {
		dev_err(kbdev->dev, "%s: invalid session handle", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (ipa_ctrl->num_active_sessions == 0) {
		dev_err(kbdev->dev, "%s: no active sessions found", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (!session->active) {
		dev_err(kbdev->dev, "%s: session is already inactive",
			__func__);
		ret = -EINVAL;
		goto exit;
	}

	for (i = 0; i < session->num_prfcnts; i++) {
		struct kbase_ipa_control_prfcnt_config *prfcnt_config;
		u8 type = session->prfcnts[i].type;
		u8 idx = session->prfcnts[i].select_idx;

		prfcnt_config = &ipa_ctrl->blocks[type].select[idx];

		if (!WARN_ON(prfcnt_config->refcount == 0)) {
			prfcnt_config->refcount--;
			if (prfcnt_config->refcount == 0) {
				new_config = true;
				ipa_ctrl->blocks[type].num_available_counters++;
			}
		}
	}

	if (new_config) {
		u64 select_config[KBASE_IPA_CORE_TYPE_NUM];

		build_select_config(ipa_ctrl, select_config);
		ret = apply_select_config(kbdev, select_config);
		if (ret)
			dev_err(kbdev->dev,
				"%s: failed to apply SELECT configuration",
				__func__);
	}

	session->num_prfcnts = 0;
	session->active = false;
	ipa_ctrl->num_active_sessions--;

exit:
	spin_unlock_irqrestore(&ipa_ctrl->lock, flags);
	kbase_pm_context_idle(kbdev);
	return ret;
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_unregister);

int kbase_ipa_control_query(struct kbase_device *kbdev, const void *client,
			    u64 *values, size_t num_values, u64 *protected_time)
{
	struct kbase_ipa_control *ipa_ctrl;
	struct kbase_ipa_control_session *session;
	size_t i;
	unsigned long flags;
	bool gpu_ready;

	if (WARN_ON(kbdev == NULL) || WARN_ON(client == NULL) ||
	    WARN_ON(values == NULL)) {
		dev_err(kbdev->dev, "%s: wrong input arguments", __func__);
		return -EINVAL;
	}

	ipa_ctrl = &kbdev->csf.ipa_control;
	session = (struct kbase_ipa_control_session *)client;

	if (WARN_ON(num_values < session->num_prfcnts)) {
		dev_err(kbdev->dev,
			"%s: not enough space (%zu) to return all counter values (%zu)",
			__func__, num_values, session->num_prfcnts);
		return -EINVAL;
	}

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	gpu_ready = kbdev->pm.backend.gpu_ready;

	for (i = 0; i < session->num_prfcnts; i++) {
		struct kbase_ipa_control_prfcnt *prfcnt = &session->prfcnts[i];

		calc_prfcnt_delta(kbdev, prfcnt, gpu_ready);
		/* Return all the accumulated difference */
		values[i] = prfcnt->accumulated_diff;
		prfcnt->accumulated_diff = 0;
	}

	if (protected_time) {
		u64 time_now = ktime_get_ns();

		/* This is the amount of protected-mode time spent prior to
		 * the current protm period.
		 */
		*protected_time = session->protm_time;

		if (kbdev->protected_mode) {
			*protected_time +=
				time_now - MAX(session->last_query_time,
					       ipa_ctrl->protm_start);
		}
		session->last_query_time = time_now;
		session->protm_time = 0;
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	for (i = session->num_prfcnts; i < num_values; i++)
		values[i] = 0;

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_query);

void kbase_ipa_control_handle_gpu_power_off(struct kbase_device *kbdev)
{
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;
	size_t session_idx;
	int ret;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* GPU should still be ready for use when this function gets called */
	WARN_ON(!kbdev->pm.backend.gpu_ready);

	/* Interrupts are already disabled and interrupt state is also saved */
	spin_lock(&ipa_ctrl->lock);

	/* First disable the automatic sampling through TIMER  */
	kbase_reg_write(kbdev, IPA_CONTROL_REG(TIMER), 0);
	ret = wait_status(kbdev, STATUS_TIMER_ENABLED);
	if (ret) {
		dev_err(kbdev->dev,
			"Wait for disabling of IPA control timer failed: %d",
			ret);
	}

	/* Now issue the manual SAMPLE command */
	kbase_reg_write(kbdev, IPA_CONTROL_REG(COMMAND), COMMAND_SAMPLE);
	ret = wait_status(kbdev, STATUS_COMMAND_ACTIVE);
	if (ret) {
		dev_err(kbdev->dev,
			"Wait for the completion of manual sample failed: %d",
			ret);
	}

	for (session_idx = 0; session_idx < ipa_ctrl->num_active_sessions;
	     session_idx++) {
		struct kbase_ipa_control_session *session =
			&ipa_ctrl->sessions[session_idx];
		size_t i;

		for (i = 0; i < session->num_prfcnts; i++) {
			struct kbase_ipa_control_prfcnt *prfcnt =
				&session->prfcnts[i];

			calc_prfcnt_delta(kbdev, prfcnt, true);
		}
	}

	spin_unlock(&ipa_ctrl->lock);
}

void kbase_ipa_control_handle_gpu_power_on(struct kbase_device *kbdev)
{
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;
	int ret;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* GPU should have become ready for use when this function gets called */
	WARN_ON(!kbdev->pm.backend.gpu_ready);

	/* Interrupts are already disabled and interrupt state is also saved */
	spin_lock(&ipa_ctrl->lock);

	/* Re-issue the APPLY command, this is actually needed only for CSHW */
	kbase_reg_write(kbdev, IPA_CONTROL_REG(COMMAND), COMMAND_APPLY);
	ret = wait_status(kbdev, STATUS_COMMAND_ACTIVE);
	if (ret) {
		dev_err(kbdev->dev,
			"Wait for the completion of apply command failed: %d",
			ret);
	}

	/* Re-enable the timer for periodic sampling */
	kbase_reg_write(kbdev, IPA_CONTROL_REG(TIMER),
			timer_value(ipa_ctrl->cur_gpu_rate));

	spin_unlock(&ipa_ctrl->lock);
}

void kbase_ipa_control_handle_gpu_reset_pre(struct kbase_device *kbdev)
{
	/* A soft reset is treated as a power down */
	kbase_ipa_control_handle_gpu_power_off(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_handle_gpu_reset_pre);

void kbase_ipa_control_handle_gpu_reset_post(struct kbase_device *kbdev)
{
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;
	int ret;
	u32 status;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* GPU should have become ready for use when this function gets called */
	WARN_ON(!kbdev->pm.backend.gpu_ready);

	/* Interrupts are already disabled and interrupt state is also saved */
	spin_lock(&ipa_ctrl->lock);

	/* Check the status reset bit is set before acknowledging it */
	status = kbase_reg_read(kbdev, IPA_CONTROL_REG(STATUS));
	if (status & STATUS_RESET) {
		/* Acknowledge the reset command */
		kbase_reg_write(kbdev, IPA_CONTROL_REG(COMMAND), COMMAND_RESET_ACK);
		ret = wait_status(kbdev, STATUS_RESET);
		if (ret) {
			dev_err(kbdev->dev,
				"Wait for the reset ack command failed: %d",
				ret);
		}
	}

	spin_unlock(&ipa_ctrl->lock);

	kbase_ipa_control_handle_gpu_power_on(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_handle_gpu_reset_post);

#if MALI_UNIT_TEST
void kbase_ipa_control_rate_change_notify_test(struct kbase_device *kbdev,
					       u32 clk_index, u32 clk_rate_hz)
{
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;
	struct kbase_ipa_control_listener_data *listener_data =
		ipa_ctrl->rtm_listener_data;

	kbase_ipa_control_rate_change_notify(&listener_data->listener,
					     clk_index, clk_rate_hz);
}
KBASE_EXPORT_TEST_API(kbase_ipa_control_rate_change_notify_test);
#endif

void kbase_ipa_control_protm_entered(struct kbase_device *kbdev)
{
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	ipa_ctrl->protm_start = ktime_get_ns();
}

void kbase_ipa_control_protm_exited(struct kbase_device *kbdev)
{
	struct kbase_ipa_control *ipa_ctrl = &kbdev->csf.ipa_control;
	size_t i;
	u64 time_now = ktime_get_ns();
	u32 status;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < ipa_ctrl->num_active_sessions; i++) {
		struct kbase_ipa_control_session *session =
			&ipa_ctrl->sessions[i];
		u64 protm_time = time_now - MAX(session->last_query_time,
						ipa_ctrl->protm_start);

		session->protm_time += protm_time;
	}

	/* Acknowledge the protected_mode bit in the IPA_CONTROL STATUS
	 * register
	 */
	status = kbase_reg_read(kbdev, IPA_CONTROL_REG(STATUS));
	if (status & STATUS_PROTECTED_MODE) {
		int ret;

		/* Acknowledge the protm command */
		kbase_reg_write(kbdev, IPA_CONTROL_REG(COMMAND),
				COMMAND_PROTECTED_ACK);
		ret = wait_status(kbdev, STATUS_PROTECTED_MODE);
		if (ret) {
			dev_err(kbdev->dev,
				"Wait for the protm ack command failed: %d",
				ret);
		}
	}
}

