// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Raspberry Pi
 */

#include "v3d_drv.h"
#include "v3d_regs.h"

#define V3D_PERFMONID_MIN	1
#define V3D_PERFMONID_MAX	U32_MAX

static const struct v3d_perf_counter_desc v3d_v42_performance_counters[] = {
	{"FEP", "FEP-valid-primitives-no-rendered-pixels", "[FEP] Valid primitives that result in no rendered pixels, for all rendered tiles"},
	{"FEP", "FEP-valid-primitives-rendered-pixels", "[FEP] Valid primitives for all rendered tiles (primitives may be counted in more than one tile)"},
	{"FEP", "FEP-clipped-quads", "[FEP] Early-Z/Near/Far clipped quads"},
	{"FEP", "FEP-valid-quads", "[FEP] Valid quads"},
	{"TLB", "TLB-quads-not-passing-stencil-test", "[TLB] Quads with no pixels passing the stencil test"},
	{"TLB", "TLB-quads-not-passing-z-and-stencil-test", "[TLB] Quads with no pixels passing the Z and stencil tests"},
	{"TLB", "TLB-quads-passing-z-and-stencil-test", "[TLB] Quads with any pixels passing the Z and stencil tests"},
	{"TLB", "TLB-quads-with-zero-coverage", "[TLB] Quads with all pixels having zero coverage"},
	{"TLB", "TLB-quads-with-non-zero-coverage", "[TLB] Quads with any pixels having non-zero coverage"},
	{"TLB", "TLB-quads-written-to-color-buffer", "[TLB] Quads with valid pixels written to colour buffer"},
	{"PTB", "PTB-primitives-discarded-outside-viewport", "[PTB] Primitives discarded by being outside the viewport"},
	{"PTB", "PTB-primitives-need-clipping", "[PTB] Primitives that need clipping"},
	{"PTB", "PTB-primitives-discarded-reversed", "[PTB] Primitives that are discarded because they are reversed"},
	{"QPU", "QPU-total-idle-clk-cycles", "[QPU] Total idle clock cycles for all QPUs"},
	{"QPU", "QPU-total-active-clk-cycles-vertex-coord-shading", "[QPU] Total active clock cycles for all QPUs doing vertex/coordinate/user shading (counts only when QPU is not stalled)"},
	{"QPU", "QPU-total-active-clk-cycles-fragment-shading", "[QPU] Total active clock cycles for all QPUs doing fragment shading (counts only when QPU is not stalled)"},
	{"QPU", "QPU-total-clk-cycles-executing-valid-instr", "[QPU] Total clock cycles for all QPUs executing valid instructions"},
	{"QPU", "QPU-total-clk-cycles-waiting-TMU", "[QPU] Total clock cycles for all QPUs stalled waiting for TMUs only (counter won't increment if QPU also stalling for another reason)"},
	{"QPU", "QPU-total-clk-cycles-waiting-scoreboard", "[QPU] Total clock cycles for all QPUs stalled waiting for Scoreboard only (counter won't increment if QPU also stalling for another reason)"},
	{"QPU", "QPU-total-clk-cycles-waiting-varyings", "[QPU] Total clock cycles for all QPUs stalled waiting for Varyings only (counter won't increment if QPU also stalling for another reason)"},
	{"QPU", "QPU-total-instr-cache-hit", "[QPU] Total instruction cache hits for all slices"},
	{"QPU", "QPU-total-instr-cache-miss", "[QPU] Total instruction cache misses for all slices"},
	{"QPU", "QPU-total-uniform-cache-hit", "[QPU] Total uniforms cache hits for all slices"},
	{"QPU", "QPU-total-uniform-cache-miss", "[QPU] Total uniforms cache misses for all slices"},
	{"TMU", "TMU-total-text-quads-access", "[TMU] Total texture cache accesses"},
	{"TMU", "TMU-total-text-cache-miss", "[TMU] Total texture cache misses (number of fetches from memory/L2cache)"},
	{"VPM", "VPM-total-clk-cycles-VDW-stalled", "[VPM] Total clock cycles VDW is stalled waiting for VPM access"},
	{"VPM", "VPM-total-clk-cycles-VCD-stalled", "[VPM] Total clock cycles VCD is stalled waiting for VPM access"},
	{"CLE", "CLE-bin-thread-active-cycles", "[CLE] Bin thread active cycles"},
	{"CLE", "CLE-render-thread-active-cycles", "[CLE] Render thread active cycles"},
	{"L2T", "L2T-total-cache-hit", "[L2T] Total Level 2 cache hits"},
	{"L2T", "L2T-total-cache-miss", "[L2T] Total Level 2 cache misses"},
	{"CORE", "cycle-count", "[CORE] Cycle counter"},
	{"QPU", "QPU-total-clk-cycles-waiting-vertex-coord-shading", "[QPU] Total stalled clock cycles for all QPUs doing vertex/coordinate/user shading"},
	{"QPU", "QPU-total-clk-cycles-waiting-fragment-shading", "[QPU] Total stalled clock cycles for all QPUs doing fragment shading"},
	{"PTB", "PTB-primitives-binned", "[PTB] Total primitives binned"},
	{"AXI", "AXI-writes-seen-watch-0", "[AXI] Writes seen by watch 0"},
	{"AXI", "AXI-reads-seen-watch-0", "[AXI] Reads seen by watch 0"},
	{"AXI", "AXI-writes-stalled-seen-watch-0", "[AXI] Write stalls seen by watch 0"},
	{"AXI", "AXI-reads-stalled-seen-watch-0", "[AXI] Read stalls seen by watch 0"},
	{"AXI", "AXI-write-bytes-seen-watch-0", "[AXI] Total bytes written seen by watch 0"},
	{"AXI", "AXI-read-bytes-seen-watch-0", "[AXI] Total bytes read seen by watch 0"},
	{"AXI", "AXI-writes-seen-watch-1", "[AXI] Writes seen by watch 1"},
	{"AXI", "AXI-reads-seen-watch-1", "[AXI] Reads seen by watch 1"},
	{"AXI", "AXI-writes-stalled-seen-watch-1", "[AXI] Write stalls seen by watch 1"},
	{"AXI", "AXI-reads-stalled-seen-watch-1", "[AXI] Read stalls seen by watch 1"},
	{"AXI", "AXI-write-bytes-seen-watch-1", "[AXI] Total bytes written seen by watch 1"},
	{"AXI", "AXI-read-bytes-seen-watch-1", "[AXI] Total bytes read seen by watch 1"},
	{"TLB", "TLB-partial-quads-written-to-color-buffer", "[TLB] Partial quads written to the colour buffer"},
	{"TMU", "TMU-total-config-access", "[TMU] Total config accesses"},
	{"L2T", "L2T-no-id-stalled", "[L2T] No ID stall"},
	{"L2T", "L2T-command-queue-stalled", "[L2T] Command queue full stall"},
	{"L2T", "L2T-TMU-writes", "[L2T] TMU write accesses"},
	{"TMU", "TMU-active-cycles", "[TMU] Active cycles"},
	{"TMU", "TMU-stalled-cycles", "[TMU] Stalled cycles"},
	{"CLE", "CLE-thread-active-cycles", "[CLE] Bin or render thread active cycles"},
	{"L2T", "L2T-TMU-reads", "[L2T] TMU read accesses"},
	{"L2T", "L2T-CLE-reads", "[L2T] CLE read accesses"},
	{"L2T", "L2T-VCD-reads", "[L2T] VCD read accesses"},
	{"L2T", "L2T-TMU-config-reads", "[L2T] TMU CFG read accesses"},
	{"L2T", "L2T-SLC0-reads", "[L2T] SLC0 read accesses"},
	{"L2T", "L2T-SLC1-reads", "[L2T] SLC1 read accesses"},
	{"L2T", "L2T-SLC2-reads", "[L2T] SLC2 read accesses"},
	{"L2T", "L2T-TMU-write-miss", "[L2T] TMU write misses"},
	{"L2T", "L2T-TMU-read-miss", "[L2T] TMU read misses"},
	{"L2T", "L2T-CLE-read-miss", "[L2T] CLE read misses"},
	{"L2T", "L2T-VCD-read-miss", "[L2T] VCD read misses"},
	{"L2T", "L2T-TMU-config-read-miss", "[L2T] TMU CFG read misses"},
	{"L2T", "L2T-SLC0-read-miss", "[L2T] SLC0 read misses"},
	{"L2T", "L2T-SLC1-read-miss", "[L2T] SLC1 read misses"},
	{"L2T", "L2T-SLC2-read-miss", "[L2T] SLC2 read misses"},
	{"CORE", "core-memory-writes", "[CORE] Total memory writes"},
	{"L2T", "L2T-memory-writes", "[L2T] Total memory writes"},
	{"PTB", "PTB-memory-writes", "[PTB] Total memory writes"},
	{"TLB", "TLB-memory-writes", "[TLB] Total memory writes"},
	{"CORE", "core-memory-reads", "[CORE] Total memory reads"},
	{"L2T", "L2T-memory-reads", "[L2T] Total memory reads"},
	{"PTB", "PTB-memory-reads", "[PTB] Total memory reads"},
	{"PSE", "PSE-memory-reads", "[PSE] Total memory reads"},
	{"TLB", "TLB-memory-reads", "[TLB] Total memory reads"},
	{"GMP", "GMP-memory-reads", "[GMP] Total memory reads"},
	{"PTB", "PTB-memory-words-writes", "[PTB] Total memory words written"},
	{"TLB", "TLB-memory-words-writes", "[TLB] Total memory words written"},
	{"PSE", "PSE-memory-words-reads", "[PSE] Total memory words read"},
	{"TLB", "TLB-memory-words-reads", "[TLB] Total memory words read"},
	{"TMU", "TMU-MRU-hits", "[TMU] Total MRU hits"},
	{"CORE", "compute-active-cycles", "[CORE] Compute active cycles"},
};

static const struct v3d_perf_counter_desc v3d_v71_performance_counters[] = {
	{"CORE", "cycle-count", "[CORE] Cycle counter"},
	{"CORE", "core-active", "[CORE] Bin/Render/Compute active cycles"},
	{"CLE", "CLE-bin-thread-active-cycles", "[CLE] Bin thread active cycles"},
	{"CLE", "CLE-render-thread-active-cycles", "[CLE] Render thread active cycles"},
	{"CORE", "compute-active-cycles", "[CORE] Compute active cycles"},
	{"FEP", "FEP-valid-primitives-no-rendered-pixels", "[FEP] Valid primitives that result in no rendered pixels, for all rendered tiles"},
	{"FEP", "FEP-valid-primitives-rendered-pixels", "[FEP] Valid primitives for all rendered tiles (primitives may be counted in more than one tile)"},
	{"FEP", "FEP-clipped-quads", "[FEP] Early-Z/Near/Far clipped quads"},
	{"FEP", "FEP-valid-quads", "[FEP] Valid quads"},
	{"TLB", "TLB-quads-not-passing-stencil-test", "[TLB] Quads with no pixels passing the stencil test"},
	{"TLB", "TLB-quads-not-passing-z-and-stencil-test", "[TLB] Quads with no pixels passing the Z and stencil tests"},
	{"TLB", "TLB-quads-passing-z-and-stencil-test", "[TLB] Quads with any pixels passing the Z and stencil tests"},
	{"TLB", "TLB-quads-written-to-color-buffer", "[TLB] Quads with valid pixels written to colour buffer"},
	{"TLB", "TLB-partial-quads-written-to-color-buffer", "[TLB] Partial quads written to the colour buffer"},
	{"PTB", "PTB-primitives-need-clipping", "[PTB] Primitives that need clipping"},
	{"PTB", "PTB-primitives-discarded-outside-viewport", "[PTB] Primitives discarded by being outside the viewport"},
	{"PTB", "PTB-primitives-binned", "[PTB] Total primitives binned"},
	{"PTB", "PTB-primitives-discarded-reversed", "[PTB] Primitives that are discarded because they are reversed"},
	{"QPU", "QPU-total-instr-cache-hit", "[QPU] Total instruction cache hits for all slices"},
	{"QPU", "QPU-total-instr-cache-miss", "[QPU] Total instruction cache misses for all slices"},
	{"QPU", "QPU-total-uniform-cache-hit", "[QPU] Total uniforms cache hits for all slices"},
	{"QPU", "QPU-total-uniform-cache-miss", "[QPU] Total uniforms cache misses for all slices"},
	{"TMU", "TMU-active-cycles", "[TMU] Active cycles"},
	{"TMU", "TMU-stalled-cycles", "[TMU] Stalled cycles"},
	{"TMU", "TMU-total-text-quads-access", "[TMU] Total texture cache accesses"},
	{"TMU", "TMU-cache-x4-active-cycles", "[TMU] Cache active cycles for x4 access"},
	{"TMU", "TMU-cache-x4-stalled-cycles", "[TMU] Cache stalled cycles for x4 access"},
	{"TMU", "TMU-total-text-quads-x4-access", "[TMU] Total texture cache x4 access"},
	{"L2T", "L2T-total-cache-hit", "[L2T] Total Level 2 cache hits"},
	{"L2T", "L2T-total-cache-miss", "[L2T] Total Level 2 cache misses"},
	{"L2T", "L2T-local", "[L2T] Local mode access"},
	{"L2T", "L2T-writeback", "[L2T] Writeback"},
	{"L2T", "L2T-zero", "[L2T] Zero"},
	{"L2T", "L2T-merge", "[L2T] Merge"},
	{"L2T", "L2T-fill", "[L2T] Fill"},
	{"L2T", "L2T-stalls-no-wid", "[L2T] Stalls because no WID available"},
	{"L2T", "L2T-stalls-no-rid", "[L2T] Stalls because no RID available"},
	{"L2T", "L2T-stalls-queue-full", "[L2T] Stalls because internal queue full"},
	{"L2T", "L2T-stalls-wrightback", "[L2T] Stalls because writeback in flight"},
	{"L2T", "L2T-stalls-mem", "[L2T] Stalls because AXI blocks read"},
	{"L2T", "L2T-stalls-fill", "[L2T] Stalls because fill pending for victim cache-line"},
	{"L2T", "L2T-hitq", "[L2T] Sent request via hit queue"},
	{"L2T", "L2T-hitq-full", "[L2T] Sent request via main queue because hit queue is full"},
	{"L2T", "L2T-stalls-read-data", "[L2T] Stalls because waiting for data from SDRAM"},
	{"L2T", "L2T-TMU-read-hits", "[L2T] TMU read hits"},
	{"L2T", "L2T-TMU-read-miss", "[L2T] TMU read misses"},
	{"L2T", "L2T-VCD-read-hits", "[L2T] VCD read hits"},
	{"L2T", "L2T-VCD-read-miss", "[L2T] VCD read misses"},
	{"L2T", "L2T-SLC-read-hits", "[L2T] SLC read hits (all slices)"},
	{"L2T", "L2T-SLC-read-miss", "[L2T] SLC read misses (all slices)"},
	{"AXI", "AXI-writes-seen-watch-0", "[AXI] Writes seen by watch 0"},
	{"AXI", "AXI-reads-seen-watch-0", "[AXI] Reads seen by watch 0"},
	{"AXI", "AXI-writes-stalled-seen-watch-0", "[AXI] Write stalls seen by watch 0"},
	{"AXI", "AXI-reads-stalled-seen-watch-0", "[AXI] Read stalls seen by watch 0"},
	{"AXI", "AXI-write-bytes-seen-watch-0", "[AXI] Total bytes written seen by watch 0"},
	{"AXI", "AXI-read-bytes-seen-watch-0", "[AXI] Total bytes read seen by watch 0"},
	{"AXI", "AXI-writes-seen-watch-1", "[AXI] Writes seen by watch 1"},
	{"AXI", "AXI-reads-seen-watch-1", "[AXI] Reads seen by watch 1"},
	{"AXI", "AXI-writes-stalled-seen-watch-1", "[AXI] Write stalls seen by watch 1"},
	{"AXI", "AXI-reads-stalled-seen-watch-1", "[AXI] Read stalls seen by watch 1"},
	{"AXI", "AXI-write-bytes-seen-watch-1", "[AXI] Total bytes written seen by watch 1"},
	{"AXI", "AXI-read-bytes-seen-watch-1", "[AXI] Total bytes read seen by watch 1"},
	{"CORE", "core-memory-writes", "[CORE] Total memory writes"},
	{"L2T", "L2T-memory-writes", "[L2T] Total memory writes"},
	{"PTB", "PTB-memory-writes", "[PTB] Total memory writes"},
	{"TLB", "TLB-memory-writes", "[TLB] Total memory writes"},
	{"CORE", "core-memory-reads", "[CORE] Total memory reads"},
	{"L2T", "L2T-memory-reads", "[L2T] Total memory reads"},
	{"PTB", "PTB-memory-reads", "[PTB] Total memory reads"},
	{"PSE", "PSE-memory-reads", "[PSE] Total memory reads"},
	{"TLB", "TLB-memory-reads", "[TLB] Total memory reads"},
	{"PTB", "PTB-memory-words-writes", "[PTB] Total memory words written"},
	{"TLB", "TLB-memory-words-writes", "[TLB] Total memory words written"},
	{"PSE", "PSE-memory-words-reads", "[PSE] Total memory words read"},
	{"TLB", "TLB-memory-words-reads", "[TLB] Total memory words read"},
	{"AXI", "AXI-read-trans", "[AXI] Read transaction count"},
	{"AXI", "AXI-write-trans", "[AXI] Write transaction count"},
	{"AXI", "AXI-read-wait-cycles", "[AXI] Read total wait cycles"},
	{"AXI", "AXI-write-wait-cycles", "[AXI] Write total wait cycles"},
	{"AXI", "AXI-max-outstanding-reads", "[AXI] Maximum outstanding read transactions"},
	{"AXI", "AXI-max-outstanding-writes", "[AXI] Maximum outstanding write transactions"},
	{"QPU", "QPU-wait-bubble", "[QPU] Pipeline bubble in qcycles due all threads waiting"},
	{"QPU", "QPU-ic-miss-bubble", "[QPU] Pipeline bubble in qcycles due instruction-cache miss"},
	{"QPU", "QPU-active", "[QPU] Executed shader instruction"},
	{"QPU", "QPU-total-active-clk-cycles-fragment-shading", "[QPU] Total active clock cycles for all QPUs doing fragment shading (counts only when QPU is not stalled)"},
	{"QPU", "QPU-stalls", "[QPU] Stalled qcycles executing shader instruction"},
	{"QPU", "QPU-total-clk-cycles-waiting-fragment-shading", "[QPU] Total stalled clock cycles for all QPUs doing fragment shading"},
	{"QPU", "QPU-stalls-TMU", "[QPU] Stalled qcycles waiting for TMU"},
	{"QPU", "QPU-stalls-TLB", "[QPU] Stalled qcycles waiting for TLB"},
	{"QPU", "QPU-stalls-VPM", "[QPU] Stalled qcycles waiting for VPM"},
	{"QPU", "QPU-stalls-uniforms", "[QPU] Stalled qcycles waiting for uniforms"},
	{"QPU", "QPU-stalls-SFU", "[QPU] Stalled qcycles waiting for SFU"},
	{"QPU", "QPU-stalls-other", "[QPU] Stalled qcycles waiting for any other reason (vary/W/Z)"},
};

void v3d_perfmon_init(struct v3d_dev *v3d)
{
	const struct v3d_perf_counter_desc *counters = NULL;
	unsigned int max = 0;

	if (v3d->ver >= 71) {
		counters = v3d_v71_performance_counters;
		max = ARRAY_SIZE(v3d_v71_performance_counters);
	} else if (v3d->ver >= 42) {
		counters = v3d_v42_performance_counters;
		max = ARRAY_SIZE(v3d_v42_performance_counters);
	}

	v3d->perfmon_info.max_counters = max;
	v3d->perfmon_info.counters = counters;
}

void v3d_perfmon_get(struct v3d_perfmon *perfmon)
{
	if (perfmon)
		refcount_inc(&perfmon->refcnt);
}

void v3d_perfmon_put(struct v3d_perfmon *perfmon)
{
	if (perfmon && refcount_dec_and_test(&perfmon->refcnt)) {
		mutex_destroy(&perfmon->lock);
		kfree(perfmon);
	}
}

void v3d_perfmon_start(struct v3d_dev *v3d, struct v3d_perfmon *perfmon)
{
	unsigned int i;
	u32 mask;
	u8 ncounters;

	if (WARN_ON_ONCE(!perfmon || v3d->active_perfmon))
		return;

	ncounters = perfmon->ncounters;
	mask = GENMASK(ncounters - 1, 0);

	for (i = 0; i < ncounters; i++) {
		u32 source = i / 4;
		u32 channel = V3D_SET_FIELD(perfmon->counters[i], V3D_PCTR_S0);

		i++;
		channel |= V3D_SET_FIELD(i < ncounters ? perfmon->counters[i] : 0,
					 V3D_PCTR_S1);
		i++;
		channel |= V3D_SET_FIELD(i < ncounters ? perfmon->counters[i] : 0,
					 V3D_PCTR_S2);
		i++;
		channel |= V3D_SET_FIELD(i < ncounters ? perfmon->counters[i] : 0,
					 V3D_PCTR_S3);
		V3D_CORE_WRITE(0, V3D_V4_PCTR_0_SRC_X(source), channel);
	}

	V3D_CORE_WRITE(0, V3D_V4_PCTR_0_CLR, mask);
	V3D_CORE_WRITE(0, V3D_PCTR_0_OVERFLOW, mask);
	V3D_CORE_WRITE(0, V3D_V4_PCTR_0_EN, mask);

	v3d->active_perfmon = perfmon;
}

void v3d_perfmon_stop(struct v3d_dev *v3d, struct v3d_perfmon *perfmon,
		      bool capture)
{
	unsigned int i;

	if (!perfmon || !v3d->active_perfmon)
		return;

	mutex_lock(&perfmon->lock);
	if (perfmon != v3d->active_perfmon) {
		mutex_unlock(&perfmon->lock);
		return;
	}

	if (capture)
		for (i = 0; i < perfmon->ncounters; i++)
			perfmon->values[i] += V3D_CORE_READ(0, V3D_PCTR_0_PCTRX(i));

	V3D_CORE_WRITE(0, V3D_V4_PCTR_0_EN, 0);

	v3d->active_perfmon = NULL;
	mutex_unlock(&perfmon->lock);
}

struct v3d_perfmon *v3d_perfmon_find(struct v3d_file_priv *v3d_priv, int id)
{
	struct v3d_perfmon *perfmon;

	mutex_lock(&v3d_priv->perfmon.lock);
	perfmon = idr_find(&v3d_priv->perfmon.idr, id);
	v3d_perfmon_get(perfmon);
	mutex_unlock(&v3d_priv->perfmon.lock);

	return perfmon;
}

void v3d_perfmon_open_file(struct v3d_file_priv *v3d_priv)
{
	mutex_init(&v3d_priv->perfmon.lock);
	idr_init_base(&v3d_priv->perfmon.idr, 1);
}

static int v3d_perfmon_idr_del(int id, void *elem, void *data)
{
	struct v3d_perfmon *perfmon = elem;
	struct v3d_dev *v3d = (struct v3d_dev *)data;

	/* If the active perfmon is being destroyed, stop it first */
	if (perfmon == v3d->active_perfmon)
		v3d_perfmon_stop(v3d, perfmon, false);

	v3d_perfmon_put(perfmon);

	return 0;
}

void v3d_perfmon_close_file(struct v3d_file_priv *v3d_priv)
{
	struct v3d_dev *v3d = v3d_priv->v3d;

	mutex_lock(&v3d_priv->perfmon.lock);
	idr_for_each(&v3d_priv->perfmon.idr, v3d_perfmon_idr_del, v3d);
	idr_destroy(&v3d_priv->perfmon.idr);
	mutex_unlock(&v3d_priv->perfmon.lock);
	mutex_destroy(&v3d_priv->perfmon.lock);
}

int v3d_perfmon_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_perfmon_create *req = data;
	struct v3d_dev *v3d = v3d_priv->v3d;
	struct v3d_perfmon *perfmon;
	unsigned int i;
	int ret;

	/* Number of monitored counters cannot exceed HW limits. */
	if (req->ncounters > DRM_V3D_MAX_PERF_COUNTERS ||
	    !req->ncounters)
		return -EINVAL;

	/* Make sure all counters are valid. */
	for (i = 0; i < req->ncounters; i++) {
		if (req->counters[i] >= v3d->perfmon_info.max_counters)
			return -EINVAL;
	}

	perfmon = kzalloc(struct_size(perfmon, values, req->ncounters),
			  GFP_KERNEL);
	if (!perfmon)
		return -ENOMEM;

	for (i = 0; i < req->ncounters; i++)
		perfmon->counters[i] = req->counters[i];

	perfmon->ncounters = req->ncounters;

	refcount_set(&perfmon->refcnt, 1);
	mutex_init(&perfmon->lock);

	mutex_lock(&v3d_priv->perfmon.lock);
	ret = idr_alloc(&v3d_priv->perfmon.idr, perfmon, V3D_PERFMONID_MIN,
			V3D_PERFMONID_MAX, GFP_KERNEL);
	mutex_unlock(&v3d_priv->perfmon.lock);

	if (ret < 0) {
		mutex_destroy(&perfmon->lock);
		kfree(perfmon);
		return ret;
	}

	req->id = ret;

	return 0;
}

int v3d_perfmon_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_perfmon_destroy *req = data;
	struct v3d_perfmon *perfmon;

	mutex_lock(&v3d_priv->perfmon.lock);
	perfmon = idr_remove(&v3d_priv->perfmon.idr, req->id);
	mutex_unlock(&v3d_priv->perfmon.lock);

	if (!perfmon)
		return -EINVAL;

	v3d_perfmon_put(perfmon);

	return 0;
}

int v3d_perfmon_get_values_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_perfmon_get_values *req = data;
	struct v3d_perfmon *perfmon;
	int ret = 0;

	if (req->pad != 0)
		return -EINVAL;

	mutex_lock(&v3d_priv->perfmon.lock);
	perfmon = idr_find(&v3d_priv->perfmon.idr, req->id);
	v3d_perfmon_get(perfmon);
	mutex_unlock(&v3d_priv->perfmon.lock);

	if (!perfmon)
		return -EINVAL;

	v3d_perfmon_stop(v3d, perfmon, true);

	if (copy_to_user(u64_to_user_ptr(req->values_ptr), perfmon->values,
			 perfmon->ncounters * sizeof(u64)))
		ret = -EFAULT;

	v3d_perfmon_put(perfmon);

	return ret;
}

int v3d_perfmon_get_counter_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct drm_v3d_perfmon_get_counter *req = data;
	struct v3d_dev *v3d = to_v3d_dev(dev);
	const struct v3d_perf_counter_desc *counter;

	for (int i = 0; i < ARRAY_SIZE(req->reserved); i++) {
		if (req->reserved[i] != 0)
			return -EINVAL;
	}

	if (!v3d->perfmon_info.max_counters)
		return -EOPNOTSUPP;

	/* Make sure that the counter ID is valid */
	if (req->counter >= v3d->perfmon_info.max_counters)
		return -EINVAL;

	counter = &v3d->perfmon_info.counters[req->counter];

	strscpy(req->name, counter->name, sizeof(req->name));
	strscpy(req->category, counter->category, sizeof(req->category));
	strscpy(req->description, counter->description, sizeof(req->description));

	return 0;
}
