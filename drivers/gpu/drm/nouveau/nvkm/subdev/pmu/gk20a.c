/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <subdev/clk.h>
#include <subdev/timer.h>
#include <subdev/volt.h>

#define BUSY_SLOT	0
#define CLK_SLOT	7

struct gk20a_pmu_dvfs_data {
	int p_load_target;
	int p_load_max;
	int p_smooth;
	unsigned int avg_load;
};

struct gk20a_pmu_priv {
	struct nvkm_pmu base;
	struct nvkm_alarm alarm;
	struct gk20a_pmu_dvfs_data *data;
};

struct gk20a_pmu_dvfs_dev_status {
	unsigned long total;
	unsigned long busy;
	int cur_state;
};

static int
gk20a_pmu_dvfs_target(struct gk20a_pmu_priv *priv, int *state)
{
	struct nvkm_clk *clk = nvkm_clk(priv);

	return nvkm_clk_astate(clk, *state, 0, false);
}

static int
gk20a_pmu_dvfs_get_cur_state(struct gk20a_pmu_priv *priv, int *state)
{
	struct nvkm_clk *clk = nvkm_clk(priv);

	*state = clk->pstate;
	return 0;
}

static int
gk20a_pmu_dvfs_get_target_state(struct gk20a_pmu_priv *priv,
				int *state, int load)
{
	struct gk20a_pmu_dvfs_data *data = priv->data;
	struct nvkm_clk *clk = nvkm_clk(priv);
	int cur_level, level;

	/* For GK20A, the performance level is directly mapped to pstate */
	level = cur_level = clk->pstate;

	if (load > data->p_load_max) {
		level = min(clk->state_nr - 1, level + (clk->state_nr / 3));
	} else {
		level += ((load - data->p_load_target) * 10 /
				data->p_load_target) / 2;
		level = max(0, level);
		level = min(clk->state_nr - 1, level);
	}

	nv_trace(priv, "cur level = %d, new level = %d\n", cur_level, level);

	*state = level;

	if (level == cur_level)
		return 0;
	else
		return 1;
}

static int
gk20a_pmu_dvfs_get_dev_status(struct gk20a_pmu_priv *priv,
			      struct gk20a_pmu_dvfs_dev_status *status)
{
	status->busy = nv_rd32(priv, 0x10a508 + (BUSY_SLOT * 0x10));
	status->total= nv_rd32(priv, 0x10a508 + (CLK_SLOT * 0x10));
	return 0;
}

static void
gk20a_pmu_dvfs_reset_dev_status(struct gk20a_pmu_priv *priv)
{
	nv_wr32(priv, 0x10a508 + (BUSY_SLOT * 0x10), 0x80000000);
	nv_wr32(priv, 0x10a508 + (CLK_SLOT * 0x10), 0x80000000);
}

static void
gk20a_pmu_dvfs_work(struct nvkm_alarm *alarm)
{
	struct gk20a_pmu_priv *priv =
		container_of(alarm, struct gk20a_pmu_priv, alarm);
	struct gk20a_pmu_dvfs_data *data = priv->data;
	struct gk20a_pmu_dvfs_dev_status status;
	struct nvkm_clk *clk = nvkm_clk(priv);
	struct nvkm_volt *volt = nvkm_volt(priv);
	u32 utilization = 0;
	int state, ret;

	/*
	 * The PMU is initialized before CLK and VOLT, so we have to make sure the
	 * CLK and VOLT are ready here.
	 */
	if (!clk || !volt)
		goto resched;

	ret = gk20a_pmu_dvfs_get_dev_status(priv, &status);
	if (ret) {
		nv_warn(priv, "failed to get device status\n");
		goto resched;
	}

	if (status.total)
		utilization = div_u64((u64)status.busy * 100, status.total);

	data->avg_load = (data->p_smooth * data->avg_load) + utilization;
	data->avg_load /= data->p_smooth + 1;
	nv_trace(priv, "utilization = %d %%, avg_load = %d %%\n",
			utilization, data->avg_load);

	ret = gk20a_pmu_dvfs_get_cur_state(priv, &state);
	if (ret) {
		nv_warn(priv, "failed to get current state\n");
		goto resched;
	}

	if (gk20a_pmu_dvfs_get_target_state(priv, &state, data->avg_load)) {
		nv_trace(priv, "set new state to %d\n", state);
		gk20a_pmu_dvfs_target(priv, &state);
	}

resched:
	gk20a_pmu_dvfs_reset_dev_status(priv);
	nvkm_timer_alarm(priv, 100000000, alarm);
}

static int
gk20a_pmu_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_pmu *pmu = (void *)object;
	struct gk20a_pmu_priv *priv = (void *)pmu;

	nvkm_timer_alarm_cancel(priv, &priv->alarm);

	return nvkm_subdev_fini(&pmu->base, suspend);
}

static int
gk20a_pmu_init(struct nvkm_object *object)
{
	struct nvkm_pmu *pmu = (void *)object;
	struct gk20a_pmu_priv *priv = (void *)pmu;
	int ret;

	ret = nvkm_subdev_init(&pmu->base);
	if (ret)
		return ret;

	pmu->pgob = nvkm_pmu_pgob;

	/* init pwr perf counter */
	nv_wr32(pmu, 0x10a504 + (BUSY_SLOT * 0x10), 0x00200001);
	nv_wr32(pmu, 0x10a50c + (BUSY_SLOT * 0x10), 0x00000002);
	nv_wr32(pmu, 0x10a50c + (CLK_SLOT * 0x10), 0x00000003);

	nvkm_timer_alarm(pmu, 2000000000, &priv->alarm);
	return ret;
}

static struct gk20a_pmu_dvfs_data
gk20a_dvfs_data= {
	.p_load_target = 70,
	.p_load_max = 90,
	.p_smooth = 1,
};

static int
gk20a_pmu_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct gk20a_pmu_priv *priv;
	int ret;

	ret = nvkm_pmu_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->data = &gk20a_dvfs_data;

	nvkm_alarm_init(&priv->alarm, gk20a_pmu_dvfs_work);
	return 0;
}

struct nvkm_oclass *
gk20a_pmu_oclass = &(struct nvkm_pmu_impl) {
	.base.handle = NV_SUBDEV(PMU, 0xea),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk20a_pmu_ctor,
		.dtor = _nvkm_pmu_dtor,
		.init = gk20a_pmu_init,
		.fini = gk20a_pmu_fini,
	},
}.base;
