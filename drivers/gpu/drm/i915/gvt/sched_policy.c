/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Anhua Xu
 *    Kevin Tian <kevin.tian@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#include "i915_drv.h"
#include "gvt.h"

static bool vgpu_has_pending_workload(struct intel_vgpu *vgpu)
{
	enum intel_engine_id i;
	struct intel_engine_cs *engine;

	for_each_engine(engine, vgpu->gvt->dev_priv, i) {
		if (!list_empty(workload_q_head(vgpu, i)))
			return true;
	}

	return false;
}

/* We give 2 seconds higher prio for vGPU during start */
#define GVT_SCHED_VGPU_PRI_TIME  2

struct vgpu_sched_data {
	struct list_head lru_list;
	struct intel_vgpu *vgpu;
	bool active;
	bool pri_sched;
	ktime_t pri_time;
	ktime_t sched_in_time;
	ktime_t sched_time;
	ktime_t left_ts;
	ktime_t allocated_ts;

	struct vgpu_sched_ctl sched_ctl;
};

struct gvt_sched_data {
	struct intel_gvt *gvt;
	struct hrtimer timer;
	unsigned long period;
	struct list_head lru_runq_head;
	ktime_t expire_time;
};

static void vgpu_update_timeslice(struct intel_vgpu *vgpu, ktime_t cur_time)
{
	ktime_t delta_ts;
	struct vgpu_sched_data *vgpu_data;

	if (!vgpu || vgpu == vgpu->gvt->idle_vgpu)
		return;

	vgpu_data = vgpu->sched_data;
	delta_ts = ktime_sub(cur_time, vgpu_data->sched_in_time);
	vgpu_data->sched_time = ktime_add(vgpu_data->sched_time, delta_ts);
	vgpu_data->left_ts = ktime_sub(vgpu_data->left_ts, delta_ts);
	vgpu_data->sched_in_time = cur_time;
}

#define GVT_TS_BALANCE_PERIOD_MS 100
#define GVT_TS_BALANCE_STAGE_NUM 10

static void gvt_balance_timeslice(struct gvt_sched_data *sched_data)
{
	struct vgpu_sched_data *vgpu_data;
	struct list_head *pos;
	static uint64_t stage_check;
	int stage = stage_check++ % GVT_TS_BALANCE_STAGE_NUM;

	/* The timeslice accumulation reset at stage 0, which is
	 * allocated again without adding previous debt.
	 */
	if (stage == 0) {
		int total_weight = 0;
		ktime_t fair_timeslice;

		list_for_each(pos, &sched_data->lru_runq_head) {
			vgpu_data = container_of(pos, struct vgpu_sched_data, lru_list);
			total_weight += vgpu_data->sched_ctl.weight;
		}

		list_for_each(pos, &sched_data->lru_runq_head) {
			vgpu_data = container_of(pos, struct vgpu_sched_data, lru_list);
			fair_timeslice = ktime_divns(ms_to_ktime(GVT_TS_BALANCE_PERIOD_MS),
						     total_weight) * vgpu_data->sched_ctl.weight;

			vgpu_data->allocated_ts = fair_timeslice;
			vgpu_data->left_ts = vgpu_data->allocated_ts;
		}
	} else {
		list_for_each(pos, &sched_data->lru_runq_head) {
			vgpu_data = container_of(pos, struct vgpu_sched_data, lru_list);

			/* timeslice for next 100ms should add the left/debt
			 * slice of previous stages.
			 */
			vgpu_data->left_ts += vgpu_data->allocated_ts;
		}
	}
}

static void try_to_schedule_next_vgpu(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	enum intel_engine_id i;
	struct intel_engine_cs *engine;
	struct vgpu_sched_data *vgpu_data;
	ktime_t cur_time;

	/* no need to schedule if next_vgpu is the same with current_vgpu,
	 * let scheduler chose next_vgpu again by setting it to NULL.
	 */
	if (scheduler->next_vgpu == scheduler->current_vgpu) {
		scheduler->next_vgpu = NULL;
		return;
	}

	/*
	 * after the flag is set, workload dispatch thread will
	 * stop dispatching workload for current vgpu
	 */
	scheduler->need_reschedule = true;

	/* still have uncompleted workload? */
	for_each_engine(engine, gvt->dev_priv, i) {
		if (scheduler->current_workload[i])
			return;
	}

	cur_time = ktime_get();
	vgpu_update_timeslice(scheduler->current_vgpu, cur_time);
	vgpu_data = scheduler->next_vgpu->sched_data;
	vgpu_data->sched_in_time = cur_time;

	/* switch current vgpu */
	scheduler->current_vgpu = scheduler->next_vgpu;
	scheduler->next_vgpu = NULL;

	scheduler->need_reschedule = false;

	/* wake up workload dispatch thread */
	for_each_engine(engine, gvt->dev_priv, i)
		wake_up(&scheduler->waitq[i]);
}

static struct intel_vgpu *find_busy_vgpu(struct gvt_sched_data *sched_data)
{
	struct vgpu_sched_data *vgpu_data;
	struct intel_vgpu *vgpu = NULL;
	struct list_head *head = &sched_data->lru_runq_head;
	struct list_head *pos;

	/* search a vgpu with pending workload */
	list_for_each(pos, head) {

		vgpu_data = container_of(pos, struct vgpu_sched_data, lru_list);
		if (!vgpu_has_pending_workload(vgpu_data->vgpu))
			continue;

		if (vgpu_data->pri_sched) {
			if (ktime_before(ktime_get(), vgpu_data->pri_time)) {
				vgpu = vgpu_data->vgpu;
				break;
			} else
				vgpu_data->pri_sched = false;
		}

		/* Return the vGPU only if it has time slice left */
		if (vgpu_data->left_ts > 0) {
			vgpu = vgpu_data->vgpu;
			break;
		}
	}

	return vgpu;
}

/* in nanosecond */
#define GVT_DEFAULT_TIME_SLICE 1000000

static void tbs_sched_func(struct gvt_sched_data *sched_data)
{
	struct intel_gvt *gvt = sched_data->gvt;
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	struct vgpu_sched_data *vgpu_data;
	struct intel_vgpu *vgpu = NULL;

	/* no active vgpu or has already had a target */
	if (list_empty(&sched_data->lru_runq_head) || scheduler->next_vgpu)
		goto out;

	vgpu = find_busy_vgpu(sched_data);
	if (vgpu) {
		scheduler->next_vgpu = vgpu;
		vgpu_data = vgpu->sched_data;
		if (!vgpu_data->pri_sched) {
			/* Move the last used vGPU to the tail of lru_list */
			list_del_init(&vgpu_data->lru_list);
			list_add_tail(&vgpu_data->lru_list,
				      &sched_data->lru_runq_head);
		}
	} else {
		scheduler->next_vgpu = gvt->idle_vgpu;
	}
out:
	if (scheduler->next_vgpu)
		try_to_schedule_next_vgpu(gvt);
}

void intel_gvt_schedule(struct intel_gvt *gvt)
{
	struct gvt_sched_data *sched_data = gvt->scheduler.sched_data;
	ktime_t cur_time;

	mutex_lock(&gvt->sched_lock);
	cur_time = ktime_get();

	if (test_and_clear_bit(INTEL_GVT_REQUEST_SCHED,
				(void *)&gvt->service_request)) {
		if (cur_time >= sched_data->expire_time) {
			gvt_balance_timeslice(sched_data);
			sched_data->expire_time = ktime_add_ms(
				cur_time, GVT_TS_BALANCE_PERIOD_MS);
		}
	}
	clear_bit(INTEL_GVT_REQUEST_EVENT_SCHED, (void *)&gvt->service_request);

	vgpu_update_timeslice(gvt->scheduler.current_vgpu, cur_time);
	tbs_sched_func(sched_data);

	mutex_unlock(&gvt->sched_lock);
}

static enum hrtimer_restart tbs_timer_fn(struct hrtimer *timer_data)
{
	struct gvt_sched_data *data;

	data = container_of(timer_data, struct gvt_sched_data, timer);

	intel_gvt_request_service(data->gvt, INTEL_GVT_REQUEST_SCHED);

	hrtimer_add_expires_ns(&data->timer, data->period);

	return HRTIMER_RESTART;
}

static int tbs_sched_init(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler =
		&gvt->scheduler;

	struct gvt_sched_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&data->lru_runq_head);
	hrtimer_init(&data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	data->timer.function = tbs_timer_fn;
	data->period = GVT_DEFAULT_TIME_SLICE;
	data->gvt = gvt;

	scheduler->sched_data = data;

	return 0;
}

static void tbs_sched_clean(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler =
		&gvt->scheduler;
	struct gvt_sched_data *data = scheduler->sched_data;

	hrtimer_cancel(&data->timer);

	kfree(data);
	scheduler->sched_data = NULL;
}

static int tbs_sched_init_vgpu(struct intel_vgpu *vgpu)
{
	struct vgpu_sched_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->sched_ctl.weight = vgpu->sched_ctl.weight;
	data->vgpu = vgpu;
	INIT_LIST_HEAD(&data->lru_list);

	vgpu->sched_data = data;

	return 0;
}

static void tbs_sched_clean_vgpu(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct gvt_sched_data *sched_data = gvt->scheduler.sched_data;

	kfree(vgpu->sched_data);
	vgpu->sched_data = NULL;

	/* this vgpu id has been removed */
	if (idr_is_empty(&gvt->vgpu_idr))
		hrtimer_cancel(&sched_data->timer);
}

static void tbs_sched_start_schedule(struct intel_vgpu *vgpu)
{
	struct gvt_sched_data *sched_data = vgpu->gvt->scheduler.sched_data;
	struct vgpu_sched_data *vgpu_data = vgpu->sched_data;
	ktime_t now;

	if (!list_empty(&vgpu_data->lru_list))
		return;

	now = ktime_get();
	vgpu_data->pri_time = ktime_add(now,
					ktime_set(GVT_SCHED_VGPU_PRI_TIME, 0));
	vgpu_data->pri_sched = true;

	list_add(&vgpu_data->lru_list, &sched_data->lru_runq_head);

	if (!hrtimer_active(&sched_data->timer))
		hrtimer_start(&sched_data->timer, ktime_add_ns(ktime_get(),
			sched_data->period), HRTIMER_MODE_ABS);
	vgpu_data->active = true;
}

static void tbs_sched_stop_schedule(struct intel_vgpu *vgpu)
{
	struct vgpu_sched_data *vgpu_data = vgpu->sched_data;

	list_del_init(&vgpu_data->lru_list);
	vgpu_data->active = false;
}

static struct intel_gvt_sched_policy_ops tbs_schedule_ops = {
	.init = tbs_sched_init,
	.clean = tbs_sched_clean,
	.init_vgpu = tbs_sched_init_vgpu,
	.clean_vgpu = tbs_sched_clean_vgpu,
	.start_schedule = tbs_sched_start_schedule,
	.stop_schedule = tbs_sched_stop_schedule,
};

int intel_gvt_init_sched_policy(struct intel_gvt *gvt)
{
	int ret;

	mutex_lock(&gvt->sched_lock);
	gvt->scheduler.sched_ops = &tbs_schedule_ops;
	ret = gvt->scheduler.sched_ops->init(gvt);
	mutex_unlock(&gvt->sched_lock);

	return ret;
}

void intel_gvt_clean_sched_policy(struct intel_gvt *gvt)
{
	mutex_lock(&gvt->sched_lock);
	gvt->scheduler.sched_ops->clean(gvt);
	mutex_unlock(&gvt->sched_lock);
}

/* for per-vgpu scheduler policy, there are 2 per-vgpu data:
 * sched_data, and sched_ctl. We see these 2 data as part of
 * the global scheduler which are proteced by gvt->sched_lock.
 * Caller should make their decision if the vgpu_lock should
 * be hold outside.
 */

int intel_vgpu_init_sched_policy(struct intel_vgpu *vgpu)
{
	int ret;

	mutex_lock(&vgpu->gvt->sched_lock);
	ret = vgpu->gvt->scheduler.sched_ops->init_vgpu(vgpu);
	mutex_unlock(&vgpu->gvt->sched_lock);

	return ret;
}

void intel_vgpu_clean_sched_policy(struct intel_vgpu *vgpu)
{
	mutex_lock(&vgpu->gvt->sched_lock);
	vgpu->gvt->scheduler.sched_ops->clean_vgpu(vgpu);
	mutex_unlock(&vgpu->gvt->sched_lock);
}

void intel_vgpu_start_schedule(struct intel_vgpu *vgpu)
{
	struct vgpu_sched_data *vgpu_data = vgpu->sched_data;

	mutex_lock(&vgpu->gvt->sched_lock);
	if (!vgpu_data->active) {
		gvt_dbg_core("vgpu%d: start schedule\n", vgpu->id);
		vgpu->gvt->scheduler.sched_ops->start_schedule(vgpu);
	}
	mutex_unlock(&vgpu->gvt->sched_lock);
}

void intel_gvt_kick_schedule(struct intel_gvt *gvt)
{
	mutex_lock(&gvt->sched_lock);
	intel_gvt_request_service(gvt, INTEL_GVT_REQUEST_EVENT_SCHED);
	mutex_unlock(&gvt->sched_lock);
}

void intel_vgpu_stop_schedule(struct intel_vgpu *vgpu)
{
	struct intel_gvt_workload_scheduler *scheduler =
		&vgpu->gvt->scheduler;
	int ring_id;
	struct vgpu_sched_data *vgpu_data = vgpu->sched_data;
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	if (!vgpu_data->active)
		return;

	gvt_dbg_core("vgpu%d: stop schedule\n", vgpu->id);

	mutex_lock(&vgpu->gvt->sched_lock);
	scheduler->sched_ops->stop_schedule(vgpu);

	if (scheduler->next_vgpu == vgpu)
		scheduler->next_vgpu = NULL;

	if (scheduler->current_vgpu == vgpu) {
		/* stop workload dispatching */
		scheduler->need_reschedule = true;
		scheduler->current_vgpu = NULL;
	}

	intel_runtime_pm_get(dev_priv);
	spin_lock_bh(&scheduler->mmio_context_lock);
	for (ring_id = 0; ring_id < I915_NUM_ENGINES; ring_id++) {
		if (scheduler->engine_owner[ring_id] == vgpu) {
			intel_gvt_switch_mmio(vgpu, NULL, ring_id);
			scheduler->engine_owner[ring_id] = NULL;
		}
	}
	spin_unlock_bh(&scheduler->mmio_context_lock);
	intel_runtime_pm_put(dev_priv);
	mutex_unlock(&vgpu->gvt->sched_lock);
}
