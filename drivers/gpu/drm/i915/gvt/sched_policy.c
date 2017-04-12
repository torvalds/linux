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

static void try_to_schedule_next_vgpu(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	enum intel_engine_id i;
	struct intel_engine_cs *engine;

	/* no target to schedule */
	if (!scheduler->next_vgpu)
		return;

	gvt_dbg_sched("try to schedule next vgpu %d\n",
			scheduler->next_vgpu->id);

	/*
	 * after the flag is set, workload dispatch thread will
	 * stop dispatching workload for current vgpu
	 */
	scheduler->need_reschedule = true;

	/* still have uncompleted workload? */
	for_each_engine(engine, gvt->dev_priv, i) {
		if (scheduler->current_workload[i]) {
			gvt_dbg_sched("still have running workload\n");
			return;
		}
	}

	gvt_dbg_sched("switch to next vgpu %d\n",
			scheduler->next_vgpu->id);

	/* switch current vgpu */
	scheduler->current_vgpu = scheduler->next_vgpu;
	scheduler->next_vgpu = NULL;

	scheduler->need_reschedule = false;

	/* wake up workload dispatch thread */
	for_each_engine(engine, gvt->dev_priv, i)
		wake_up(&scheduler->waitq[i]);
}

struct tbs_vgpu_data {
	struct list_head list;
	struct intel_vgpu *vgpu;
	/* put some per-vgpu sched stats here */
};

struct tbs_sched_data {
	struct intel_gvt *gvt;
	struct delayed_work work;
	unsigned long period;
	struct list_head runq_head;
};

#define GVT_DEFAULT_TIME_SLICE (msecs_to_jiffies(1))

static void tbs_sched_func(struct work_struct *work)
{
	struct tbs_sched_data *sched_data = container_of(work,
			struct tbs_sched_data, work.work);
	struct tbs_vgpu_data *vgpu_data;

	struct intel_gvt *gvt = sched_data->gvt;
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;

	struct intel_vgpu *vgpu = NULL;
	struct list_head *pos, *head;

	mutex_lock(&gvt->lock);

	/* no vgpu or has already had a target */
	if (list_empty(&sched_data->runq_head) || scheduler->next_vgpu)
		goto out;

	if (scheduler->current_vgpu) {
		vgpu_data = scheduler->current_vgpu->sched_data;
		head = &vgpu_data->list;
	} else {
		head = &sched_data->runq_head;
	}

	/* search a vgpu with pending workload */
	list_for_each(pos, head) {
		if (pos == &sched_data->runq_head)
			continue;

		vgpu_data = container_of(pos, struct tbs_vgpu_data, list);
		if (!vgpu_has_pending_workload(vgpu_data->vgpu))
			continue;

		vgpu = vgpu_data->vgpu;
		break;
	}

	if (vgpu) {
		scheduler->next_vgpu = vgpu;
		gvt_dbg_sched("pick next vgpu %d\n", vgpu->id);
	}
out:
	if (scheduler->next_vgpu) {
		gvt_dbg_sched("try to schedule next vgpu %d\n",
				scheduler->next_vgpu->id);
		try_to_schedule_next_vgpu(gvt);
	}

	/*
	 * still have vgpu on runq
	 * or last schedule haven't finished due to running workload
	 */
	if (!list_empty(&sched_data->runq_head) || scheduler->next_vgpu)
		schedule_delayed_work(&sched_data->work, sched_data->period);

	mutex_unlock(&gvt->lock);
}

static int tbs_sched_init(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler =
		&gvt->scheduler;

	struct tbs_sched_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&data->runq_head);
	INIT_DELAYED_WORK(&data->work, tbs_sched_func);
	data->period = GVT_DEFAULT_TIME_SLICE;
	data->gvt = gvt;

	scheduler->sched_data = data;
	return 0;
}

static void tbs_sched_clean(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler =
		&gvt->scheduler;
	struct tbs_sched_data *data = scheduler->sched_data;

	cancel_delayed_work(&data->work);
	kfree(data);
	scheduler->sched_data = NULL;
}

static int tbs_sched_init_vgpu(struct intel_vgpu *vgpu)
{
	struct tbs_vgpu_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->vgpu = vgpu;
	INIT_LIST_HEAD(&data->list);

	vgpu->sched_data = data;
	return 0;
}

static void tbs_sched_clean_vgpu(struct intel_vgpu *vgpu)
{
	kfree(vgpu->sched_data);
	vgpu->sched_data = NULL;
}

static void tbs_sched_start_schedule(struct intel_vgpu *vgpu)
{
	struct tbs_sched_data *sched_data = vgpu->gvt->scheduler.sched_data;
	struct tbs_vgpu_data *vgpu_data = vgpu->sched_data;

	if (!list_empty(&vgpu_data->list))
		return;

	list_add_tail(&vgpu_data->list, &sched_data->runq_head);
	schedule_delayed_work(&sched_data->work, 0);
}

static void tbs_sched_stop_schedule(struct intel_vgpu *vgpu)
{
	struct tbs_vgpu_data *vgpu_data = vgpu->sched_data;

	list_del_init(&vgpu_data->list);
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
	gvt->scheduler.sched_ops = &tbs_schedule_ops;

	return gvt->scheduler.sched_ops->init(gvt);
}

void intel_gvt_clean_sched_policy(struct intel_gvt *gvt)
{
	gvt->scheduler.sched_ops->clean(gvt);
}

int intel_vgpu_init_sched_policy(struct intel_vgpu *vgpu)
{
	return vgpu->gvt->scheduler.sched_ops->init_vgpu(vgpu);
}

void intel_vgpu_clean_sched_policy(struct intel_vgpu *vgpu)
{
	vgpu->gvt->scheduler.sched_ops->clean_vgpu(vgpu);
}

void intel_vgpu_start_schedule(struct intel_vgpu *vgpu)
{
	gvt_dbg_core("vgpu%d: start schedule\n", vgpu->id);

	vgpu->gvt->scheduler.sched_ops->start_schedule(vgpu);
}

void intel_vgpu_stop_schedule(struct intel_vgpu *vgpu)
{
	struct intel_gvt_workload_scheduler *scheduler =
		&vgpu->gvt->scheduler;

	gvt_dbg_core("vgpu%d: stop schedule\n", vgpu->id);

	scheduler->sched_ops->stop_schedule(vgpu);

	if (scheduler->next_vgpu == vgpu)
		scheduler->next_vgpu = NULL;

	if (scheduler->current_vgpu == vgpu) {
		/* stop workload dispatching */
		scheduler->need_reschedule = true;
		scheduler->current_vgpu = NULL;
	}
}
