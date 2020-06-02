/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/kref.h>

#include "gem/i915_gem_pm.h"
#include "gt/intel_gt.h"

#include "i915_selftest.h"

#include "igt_flush_test.h"
#include "lib_sw_fence.h"

#define TEST_OA_CONFIG_UUID "12345678-1234-1234-1234-1234567890ab"

static int
alloc_empty_config(struct i915_perf *perf)
{
	struct i915_oa_config *oa_config;

	oa_config = kzalloc(sizeof(*oa_config), GFP_KERNEL);
	if (!oa_config)
		return -ENOMEM;

	oa_config->perf = perf;
	kref_init(&oa_config->ref);

	strlcpy(oa_config->uuid, TEST_OA_CONFIG_UUID, sizeof(oa_config->uuid));

	mutex_lock(&perf->metrics_lock);

	oa_config->id = idr_alloc(&perf->metrics_idr, oa_config, 2, 0, GFP_KERNEL);
	if (oa_config->id < 0)  {
		mutex_unlock(&perf->metrics_lock);
		i915_oa_config_put(oa_config);
		return -ENOMEM;
	}

	mutex_unlock(&perf->metrics_lock);

	return 0;
}

static void
destroy_empty_config(struct i915_perf *perf)
{
	struct i915_oa_config *oa_config = NULL, *tmp;
	int id;

	mutex_lock(&perf->metrics_lock);

	idr_for_each_entry(&perf->metrics_idr, tmp, id) {
		if (!strcmp(tmp->uuid, TEST_OA_CONFIG_UUID)) {
			oa_config = tmp;
			break;
		}
	}

	if (oa_config)
		idr_remove(&perf->metrics_idr, oa_config->id);

	mutex_unlock(&perf->metrics_lock);

	if (oa_config)
		i915_oa_config_put(oa_config);
}

static struct i915_oa_config *
get_empty_config(struct i915_perf *perf)
{
	struct i915_oa_config *oa_config = NULL, *tmp;
	int id;

	mutex_lock(&perf->metrics_lock);

	idr_for_each_entry(&perf->metrics_idr, tmp, id) {
		if (!strcmp(tmp->uuid, TEST_OA_CONFIG_UUID)) {
			oa_config = i915_oa_config_get(tmp);
			break;
		}
	}

	mutex_unlock(&perf->metrics_lock);

	return oa_config;
}

static struct i915_perf_stream *
test_stream(struct i915_perf *perf)
{
	struct drm_i915_perf_open_param param = {};
	struct i915_oa_config *oa_config = get_empty_config(perf);
	struct perf_open_properties props = {
		.engine = intel_engine_lookup_user(perf->i915,
						   I915_ENGINE_CLASS_RENDER,
						   0),
		.sample_flags = SAMPLE_OA_REPORT,
		.oa_format = IS_GEN(perf->i915, 12) ?
		I915_OA_FORMAT_A32u40_A4u32_B8_C8 : I915_OA_FORMAT_C4_B8,
	};
	struct i915_perf_stream *stream;

	if (!oa_config)
		return NULL;

	props.metrics_set = oa_config->id;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream) {
		i915_oa_config_put(oa_config);
		return NULL;
	}

	stream->perf = perf;

	mutex_lock(&perf->lock);
	if (i915_oa_stream_init(stream, &param, &props)) {
		kfree(stream);
		stream =  NULL;
	}
	mutex_unlock(&perf->lock);

	i915_oa_config_put(oa_config);

	return stream;
}

static void stream_destroy(struct i915_perf_stream *stream)
{
	struct i915_perf *perf = stream->perf;

	mutex_lock(&perf->lock);
	i915_perf_destroy_locked(stream);
	mutex_unlock(&perf->lock);
}

static int live_sanitycheck(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_perf_stream *stream;

	/* Quick check we can create a perf stream */

	stream = test_stream(&i915->perf);
	if (!stream)
		return -EINVAL;

	stream_destroy(stream);
	return 0;
}

static int write_timestamp(struct i915_request *rq, int slot)
{
	u32 *cs;
	int len;

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	len = 5;
	if (INTEL_GEN(rq->engine->i915) >= 8)
		len++;

	*cs++ = GFX_OP_PIPE_CONTROL(len);
	*cs++ = PIPE_CONTROL_GLOBAL_GTT_IVB |
		PIPE_CONTROL_STORE_DATA_INDEX |
		PIPE_CONTROL_WRITE_TIMESTAMP;
	*cs++ = slot * sizeof(u32);
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;

	intel_ring_advance(rq, cs);

	return 0;
}

static ktime_t poll_status(struct i915_request *rq, int slot)
{
	while (!intel_read_status_page(rq->engine, slot) &&
	       !i915_request_completed(rq))
		cpu_relax();

	return ktime_get();
}

static int live_noa_delay(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_perf_stream *stream;
	struct i915_request *rq;
	ktime_t t0, t1;
	u64 expected;
	u32 delay;
	int err;
	int i;

	/* Check that the GPU delays matches expectations */

	stream = test_stream(&i915->perf);
	if (!stream)
		return -ENOMEM;

	expected = atomic64_read(&stream->perf->noa_programming_delay);

	if (stream->engine->class != RENDER_CLASS) {
		err = -ENODEV;
		goto out;
	}

	for (i = 0; i < 4; i++)
		intel_write_status_page(stream->engine, 0x100 + i, 0);

	rq = intel_engine_create_kernel_request(stream->engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out;
	}

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (err) {
			i915_request_add(rq);
			goto out;
		}
	}

	err = write_timestamp(rq, 0x100);
	if (err) {
		i915_request_add(rq);
		goto out;
	}

	err = rq->engine->emit_bb_start(rq,
					i915_ggtt_offset(stream->noa_wait), 0,
					I915_DISPATCH_SECURE);
	if (err) {
		i915_request_add(rq);
		goto out;
	}

	err = write_timestamp(rq, 0x102);
	if (err) {
		i915_request_add(rq);
		goto out;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	preempt_disable();
	t0 = poll_status(rq, 0x100);
	t1 = poll_status(rq, 0x102);
	preempt_enable();

	pr_info("CPU delay: %lluns, expected %lluns\n",
		ktime_sub(t1, t0), expected);

	delay = intel_read_status_page(stream->engine, 0x102);
	delay -= intel_read_status_page(stream->engine, 0x100);
	delay = i915_cs_timestamp_ticks_to_ns(i915, delay);
	pr_info("GPU delay: %uns, expected %lluns\n",
		delay, expected);

	if (4 * delay < 3 * expected || 2 * delay > 3 * expected) {
		pr_err("GPU delay [%uus] outside of expected threshold! [%lluus, %lluus]\n",
		       delay / 1000,
		       div_u64(3 * expected, 4000),
		       div_u64(3 * expected, 2000));
		err = -EINVAL;
	}

	i915_request_put(rq);
out:
	stream_destroy(stream);
	return err;
}

int i915_perf_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_sanitycheck),
		SUBTEST(live_noa_delay),
	};
	struct i915_perf *perf = &i915->perf;
	int err;

	if (!perf->metrics_kobj || !perf->ops.enable_metric_set)
		return 0;

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	err = alloc_empty_config(&i915->perf);
	if (err)
		return err;

	err = i915_subtests(tests, i915);

	destroy_empty_config(&i915->perf);

	return err;
}
