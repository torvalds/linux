// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit test helpers for amdgpu_dm tests.
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/module.h>
#include <drm/drm_kunit_helpers.h>

#include "dc.h"
#include "core_types.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_kunit_test_helpers.h"

struct amdgpu_device *dm_kunit_alloc_adev(struct kunit *test)
{
	struct drm_device *drm;
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(struct amdgpu_device),
						   offsetof(struct amdgpu_device, ddev),
						   DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	return drm_to_adev(drm);
}
EXPORT_SYMBOL(dm_kunit_alloc_adev);

struct dc_link *dm_kunit_alloc_link(struct kunit *test)
{
	struct dc_link *link;

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	return link;
}
EXPORT_SYMBOL(dm_kunit_alloc_link);

struct dc_link *dm_kunit_alloc_link_with_ctx(struct kunit *test)
{
	struct dc_link *link;
	struct dc_context *ctx;
	struct dc *dc;

	link = dm_kunit_alloc_link(test);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	link->ctx = ctx;
	ctx->dc = dc;
	dc->ctx = ctx;

	return link;
}
EXPORT_SYMBOL(dm_kunit_alloc_link_with_ctx);

struct amdgpu_display_manager *dm_kunit_alloc_dm(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct dc *dc;
	struct dc_state *state;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	dm->dc = dc;
	dc->current_state = state;

	return dm;
}
EXPORT_SYMBOL(dm_kunit_alloc_dm);

struct dc_stream_state *dm_kunit_alloc_stream(struct kunit *test,
					      struct dc_link *link)
{
	struct dc_stream_state *stream;

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream);

	stream->link = link;
	kref_init(&stream->refcount);

	return stream;
}
EXPORT_SYMBOL(dm_kunit_alloc_stream);

void dm_kunit_add_stream_to_state(struct kunit *test, struct dc_state *state,
				  unsigned int index, struct dc_link *link)
{
	struct dc_stream_state *stream;

	KUNIT_ASSERT_LT(test, index, (unsigned int)MAX_PIPES);

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream);

	stream->link = link;
	state->streams[index] = stream;
	if (state->stream_count <= index)
		state->stream_count = index + 1;
}
EXPORT_SYMBOL(dm_kunit_add_stream_to_state);

struct amdgpu_dm_connector *dm_kunit_alloc_connector(struct kunit *test,
						     struct amdgpu_device *adev,
						     struct dc_link *link)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	if (adev)
		aconnector->base.dev = &adev->ddev;
	aconnector->dc_link = link;

	return aconnector;
}
EXPORT_SYMBOL(dm_kunit_alloc_connector);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit test helpers for amdgpu_dm tests");
