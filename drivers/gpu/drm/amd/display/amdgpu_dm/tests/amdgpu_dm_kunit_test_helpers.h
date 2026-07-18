/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * KUnit test helpers for amdgpu_dm tests.
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#ifndef AMDGPU_DM_KUNIT_TEST_HELPERS_H
#define AMDGPU_DM_KUNIT_TEST_HELPERS_H

#include <kunit/test.h>

struct amdgpu_device;
struct amdgpu_display_manager;
struct amdgpu_dm_connector;
struct dc_link;
struct dc_state;
struct dc_stream_state;

struct amdgpu_device *dm_kunit_alloc_adev(struct kunit *test);
struct dc_link *dm_kunit_alloc_link(struct kunit *test);
struct dc_link *dm_kunit_alloc_link_with_ctx(struct kunit *test);
struct amdgpu_display_manager *dm_kunit_alloc_dm(struct kunit *test);
struct dc_stream_state *dm_kunit_alloc_stream(struct kunit *test,
					      struct dc_link *link);
void dm_kunit_add_stream_to_state(struct kunit *test, struct dc_state *state,
				  unsigned int index, struct dc_link *link);
struct amdgpu_dm_connector *dm_kunit_alloc_connector(struct kunit *test,
						     struct amdgpu_device *adev,
						     struct dc_link *link);

#endif /* AMDGPU_DM_KUNIT_TEST_HELPERS_H */
