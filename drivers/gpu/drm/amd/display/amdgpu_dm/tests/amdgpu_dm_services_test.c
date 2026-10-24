// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_services.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "dm_services.h"
#include "dm_services_types.h"

/* Tests for dm_get_elapse_time_in_ns() */

/**
 * dm_test_get_elapse_time_zero_delta - Test Get elapse time zero delta
 * @test: The KUnit test context
 */
static void dm_test_get_elapse_time_zero_delta(struct kunit *test)
{
	unsigned long long ts = 1000000ULL;

	KUNIT_EXPECT_EQ(test, dm_get_elapse_time_in_ns(NULL, ts, ts), 0ULL);
}

/**
 * dm_test_get_elapse_time_positive_delta - Test Get elapse time positive delta
 * @test: The KUnit test context
 */
static void dm_test_get_elapse_time_positive_delta(struct kunit *test)
{
	unsigned long long current_ts = 5000000ULL;
	unsigned long long last_ts = 1000000ULL;

	KUNIT_EXPECT_EQ(test, dm_get_elapse_time_in_ns(NULL, current_ts, last_ts),
			4000000ULL);
}

/**
 * dm_test_get_elapse_time_large_delta - Test Get elapse time large delta
 * @test: The KUnit test context
 */
static void dm_test_get_elapse_time_large_delta(struct kunit *test)
{
	unsigned long long current_ts = ULLONG_MAX;
	unsigned long long last_ts = 0ULL;

	KUNIT_EXPECT_EQ(test, dm_get_elapse_time_in_ns(NULL, current_ts, last_ts),
			ULLONG_MAX);
}

/**
 * dm_test_get_elapse_time_wraparound - Test Get elapse time wraparound
 * @test: The KUnit test context
 */
static void dm_test_get_elapse_time_wraparound(struct kunit *test)
{
	/* Unsigned wraparound: result = ULLONG_MAX - last + current + 1 */
	unsigned long long current_ts = 5ULL;
	unsigned long long last_ts = ULLONG_MAX - 4ULL;

	KUNIT_EXPECT_EQ(test, dm_get_elapse_time_in_ns(NULL, current_ts, last_ts),
			10ULL);
}

/* Tests for dm_perf_trace_timestamp() */

/**
 * dm_test_perf_trace_timestamp_basic - Test Perf trace timestamp basic
 * @test: The KUnit test context
 *
 * The tracepoint is a no-op without an attached probe, so this verifies the
 * function dereferences ctx->perf_trace safely and does not crash.
 */
static void dm_test_perf_trace_timestamp_basic(struct kunit *test)
{
	struct dc_context *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ctx->perf_trace = kunit_kzalloc(test, sizeof(*ctx->perf_trace), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->perf_trace);

	ctx->perf_trace->read_count = 10;
	ctx->perf_trace->write_count = 20;

	dm_perf_trace_timestamp(__func__, __LINE__, ctx);
}

/* Tests for dm_trace_smu_enter() */

/**
 * dm_test_trace_smu_enter_null_ctx - Test Trace smu enter null ctx
 * @test: The KUnit test context
 */
static void dm_test_trace_smu_enter_null_ctx(struct kunit *test)
{
	/* Empty stub — must not crash with NULL ctx */
	dm_trace_smu_enter(0, 0, 0, NULL);
}

/**
 * dm_test_trace_smu_enter_with_params - Test Trace smu enter with params
 * @test: The KUnit test context
 */
static void dm_test_trace_smu_enter_with_params(struct kunit *test)
{
	/* Exercise non-zero msg_id, param_in, and delay */
	dm_trace_smu_enter(0xFF, 0x12345678, 1000, NULL);
}

/* Tests for dm_trace_smu_exit() */

/**
 * dm_test_trace_smu_exit_success_null_ctx - Test Trace smu exit success null ctx
 * @test: The KUnit test context
 */
static void dm_test_trace_smu_exit_success_null_ctx(struct kunit *test)
{
	/* Empty stub — must not crash on success path with NULL ctx */
	dm_trace_smu_exit(true, 0x0, NULL);
}

/**
 * dm_test_trace_smu_exit_failure_null_ctx - Test Trace smu exit failure null ctx
 * @test: The KUnit test context
 */
static void dm_test_trace_smu_exit_failure_null_ctx(struct kunit *test)
{
	/* Empty stub — must not crash on failure path with NULL ctx */
	dm_trace_smu_exit(false, 0x0, NULL);
}

/**
 * dm_test_trace_smu_exit_with_response - Test Trace smu exit with response
 * @test: The KUnit test context
 */
static void dm_test_trace_smu_exit_with_response(struct kunit *test)
{
	/* Exercise non-zero response value */
	dm_trace_smu_exit(true, 0xDEADBEEF, NULL);
}

/* Tests for dm_query_extended_brightness_caps() */

/**
 * dm_test_query_brightness_caps_null_ctx - Test Query brightness caps null ctx
 * @test: The KUnit test context
 */
static void dm_test_query_brightness_caps_null_ctx(struct kunit *test)
{
	struct dm_acpi_atif_backlight_caps caps = {};

	KUNIT_EXPECT_FALSE(test,
			   dm_query_extended_brightness_caps(NULL, AcpiDisplayType_LCD1, &caps));
}

/**
 * dm_test_query_brightness_caps_null_caps - Test Query brightness caps null caps
 * @test: The KUnit test context
 */
static void dm_test_query_brightness_caps_null_caps(struct kunit *test)
{
	struct dc_context ctx = {};

	ctx.driver_context = (void *)0x1; /* non-NULL sentinel */

	KUNIT_EXPECT_FALSE(test,
			   dm_query_extended_brightness_caps(&ctx, AcpiDisplayType_LCD1, NULL));
}

/**
 * dm_test_query_brightness_caps_null_driver_ctx - Test Query brightness caps null driver ctx
 * @test: The KUnit test context
 */
static void dm_test_query_brightness_caps_null_driver_ctx(struct kunit *test)
{
	struct dc_context ctx = {};
	struct dm_acpi_atif_backlight_caps caps = {};

	ctx.driver_context = NULL;

	KUNIT_EXPECT_FALSE(test,
			   dm_query_extended_brightness_caps(&ctx, AcpiDisplayType_LCD1, &caps));
}

/**
 * dm_test_query_brightness_caps_lcd2_null_ctx - Test Query brightness caps lcd2 null ctx
 * @test: The KUnit test context
 */
static void dm_test_query_brightness_caps_lcd2_null_ctx(struct kunit *test)
{
	struct dm_acpi_atif_backlight_caps caps = {};

	KUNIT_EXPECT_FALSE(test,
			   dm_query_extended_brightness_caps(NULL, AcpiDisplayType_LCD2, &caps));
}

/**
 * dm_test_query_brightness_caps_lcd1_success - Test Query brightness caps lcd1 success
 * @test: The KUnit test context
 */
static void dm_test_query_brightness_caps_lcd1_success(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_dm_backlight_caps *source_caps;
	struct dc_context ctx = {};
	struct dm_acpi_atif_backlight_caps caps = {};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	source_caps = &adev->dm.backlight_caps[0];
	source_caps->caps_valid = true;
	source_caps->min_input_signal = 12;
	source_caps->max_input_signal = 240;
	source_caps->ac_level = 80;
	source_caps->dc_level = 40;
	source_caps->data_points = 2;
	source_caps->luminance_data[0].luminance = 10;
	source_caps->luminance_data[0].input_signal = 22;
	source_caps->luminance_data[1].luminance = 90;
	source_caps->luminance_data[1].input_signal = 200;
	ctx.driver_context = adev;

	KUNIT_EXPECT_TRUE(test,
			  dm_query_extended_brightness_caps(&ctx, AcpiDisplayType_LCD1, &caps));
	KUNIT_EXPECT_EQ(test, caps.num_data_points, 2);
	KUNIT_EXPECT_EQ(test, caps.max_input_signal, 240);
	KUNIT_EXPECT_EQ(test, caps.min_input_signal, 12);
	KUNIT_EXPECT_EQ(test, caps.ac_level_percentage, 80);
	KUNIT_EXPECT_EQ(test, caps.dc_level_percentage, 40);
	KUNIT_EXPECT_EQ(test, caps.data_points[0].luminance, 10);
	KUNIT_EXPECT_EQ(test, caps.data_points[0].signal_level, 22);
	KUNIT_EXPECT_EQ(test, caps.data_points[1].luminance, 90);
	KUNIT_EXPECT_EQ(test, caps.data_points[1].signal_level, 200);
}

/**
 * dm_test_query_brightness_caps_non_lcd1_uses_second_slot - Test Query brightness caps non lcd1 uses second slot
 * @test: The KUnit test context
 */
static void dm_test_query_brightness_caps_non_lcd1_uses_second_slot(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_dm_backlight_caps *source_caps;
	struct dc_context ctx = {};
	struct dm_acpi_atif_backlight_caps caps = {};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->dm.backlight_caps[0].caps_valid = true;
	adev->dm.backlight_caps[0].min_input_signal = 1;
	adev->dm.backlight_caps[0].max_input_signal = 2;
	source_caps = &adev->dm.backlight_caps[1];
	source_caps->caps_valid = true;
	source_caps->min_input_signal = 33;
	source_caps->max_input_signal = 199;
	source_caps->ac_level = 70;
	source_caps->dc_level = 30;
	source_caps->data_points = 0;
	ctx.driver_context = adev;

	KUNIT_EXPECT_TRUE(test,
			  dm_query_extended_brightness_caps(&ctx, AcpiDisplayType_DFP1, &caps));
	KUNIT_EXPECT_EQ(test, caps.num_data_points, 0);
	KUNIT_EXPECT_EQ(test, caps.max_input_signal, 199);
	KUNIT_EXPECT_EQ(test, caps.min_input_signal, 33);
	KUNIT_EXPECT_EQ(test, caps.ac_level_percentage, 70);
	KUNIT_EXPECT_EQ(test, caps.dc_level_percentage, 30);
	KUNIT_EXPECT_EQ(test, caps.data_points[0].luminance, 0);
	KUNIT_EXPECT_EQ(test, caps.data_points[0].signal_level, 0);
}

/* Tests for dm_allocate_gpu_mem() and dm_free_gpu_mem() */

#define DM_TEST_FAKE_GPU_ADDR 0xF00DBEEFULL

struct dm_test_bo_ops_ctx {
	void *cpu_ptr;
	u64 gpu_addr;
	int create_ret;
	unsigned long create_size;
	int create_align;
	u32 create_domain;
	unsigned int create_calls;
	unsigned int free_calls;
};

static struct dm_test_bo_ops_ctx dm_test_bo_ctx;

static int dm_test_bo_create_kernel(struct amdgpu_device *adev, unsigned long size,
				    int align, u32 domain, struct amdgpu_bo **bo_ptr,
				    u64 *gpu_addr, void **cpu_addr)
{
	dm_test_bo_ctx.create_calls++;
	dm_test_bo_ctx.create_size = size;
	dm_test_bo_ctx.create_align = align;
	dm_test_bo_ctx.create_domain = domain;

	if (dm_test_bo_ctx.create_ret)
		return dm_test_bo_ctx.create_ret;

	*gpu_addr = dm_test_bo_ctx.gpu_addr;
	*cpu_addr = dm_test_bo_ctx.cpu_ptr;

	return 0;
}

static void dm_test_bo_free_kernel(struct amdgpu_bo **bo, u64 *gpu_addr, void **cpu_addr)
{
	dm_test_bo_ctx.free_calls++;

	*bo = NULL;
	*gpu_addr = 0;
	*cpu_addr = NULL;
}

static const struct amdgpu_dm_services_kunit_ops dm_test_bo_ops = {
	.bo_create_kernel = dm_test_bo_create_kernel,
	.bo_free_kernel = dm_test_bo_free_kernel,
};

/**
 * dm_test_gpu_mem_init - Install the fake buffer object ops
 * @test: The KUnit test context
 *
 * amdgpu_bo_create_kernel() and amdgpu_bo_free_kernel() need a live TTM
 * device, so route them through a fake table for the duration of the test.
 */
static int dm_test_gpu_mem_init(struct kunit *test)
{
	void *cpu_ptr;

	cpu_ptr = kunit_kzalloc(test, sizeof(cpu_ptr), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cpu_ptr);

	dm_test_bo_ctx = (struct dm_test_bo_ops_ctx) {
		.cpu_ptr = cpu_ptr,
		.gpu_addr = DM_TEST_FAKE_GPU_ADDR,
	};

	amdgpu_dm_services_kunit_set_ops(&dm_test_bo_ops);

	return 0;
}

static void dm_test_gpu_mem_exit(struct kunit *test)
{
	amdgpu_dm_services_kunit_set_ops(NULL);
}

static struct amdgpu_device *dm_test_alloc_adev(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	INIT_LIST_HEAD(&adev->dm.da_list);

	return adev;
}

/**
 * dm_test_allocate_gpu_mem_gart - Test Allocate gpu mem gart
 * @test: The KUnit test context
 */
static void dm_test_allocate_gpu_mem_gart(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev(test);
	long long addr = 0;
	void *mem;

	mem = dm_allocate_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, 4096, &addr);

	KUNIT_EXPECT_PTR_EQ(test, mem, dm_test_bo_ctx.cpu_ptr);
	KUNIT_EXPECT_EQ(test, addr, (long long)DM_TEST_FAKE_GPU_ADDR);
	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.create_calls, 1U);
	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.create_size, 4096UL);
	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.create_align, (int)PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.create_domain, (u32)AMDGPU_GEM_DOMAIN_GTT);
	KUNIT_EXPECT_FALSE(test, list_empty(&adev->dm.da_list));

	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, mem);
}

/**
 * dm_test_allocate_gpu_mem_frame_buffer - Test Allocate gpu mem frame buffer
 * @test: The KUnit test context
 *
 * Any non-GART allocation type must land in the VRAM domain.
 */
static void dm_test_allocate_gpu_mem_frame_buffer(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev(test);
	long long addr = 0;
	void *mem;

	mem = dm_allocate_gpu_mem(adev, DC_MEM_ALLOC_TYPE_FRAME_BUFFER, 8192, &addr);

	KUNIT_EXPECT_PTR_EQ(test, mem, dm_test_bo_ctx.cpu_ptr);
	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.create_domain, (u32)AMDGPU_GEM_DOMAIN_VRAM);
	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.create_size, 8192UL);

	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_FRAME_BUFFER, mem);
}

/**
 * dm_test_allocate_gpu_mem_create_fails - Test Allocate gpu mem create fails
 * @test: The KUnit test context
 */
static void dm_test_allocate_gpu_mem_create_fails(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev(test);
	long long addr = 0;
	void *mem;

	dm_test_bo_ctx.create_ret = -ENOMEM;

	mem = dm_allocate_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, 4096, &addr);

	KUNIT_EXPECT_NULL(test, mem);
	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.create_calls, 1U);
	KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.da_list));
}

/**
 * dm_test_free_gpu_mem_matching - Test Free gpu mem matching
 * @test: The KUnit test context
 */
static void dm_test_free_gpu_mem_matching(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev(test);
	long long addr = 0;
	void *mem;

	mem = dm_allocate_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, 4096, &addr);
	KUNIT_ASSERT_NOT_NULL(test, mem);

	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, mem);

	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.free_calls, 1U);
	KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.da_list));
}

/**
 * dm_test_free_gpu_mem_no_match - Test Free gpu mem no match
 * @test: The KUnit test context
 */
static void dm_test_free_gpu_mem_no_match(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev(test);
	long long addr = 0;
	void *mem;

	mem = dm_allocate_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, 4096, &addr);
	KUNIT_ASSERT_NOT_NULL(test, mem);

	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, (char *)mem + 1);

	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.free_calls, 0U);
	KUNIT_EXPECT_FALSE(test, list_empty(&adev->dm.da_list));

	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, mem);
}

/**
 * dm_test_free_gpu_mem_empty_list - Test Free gpu mem empty list
 * @test: The KUnit test context
 */
static void dm_test_free_gpu_mem_empty_list(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev(test);

	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, dm_test_bo_ctx.cpu_ptr);

	KUNIT_EXPECT_EQ(test, dm_test_bo_ctx.free_calls, 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.da_list));
}

static struct kunit_case amdgpu_dm_services_test_cases[] = {
	/* dm_get_elapse_time_in_ns */
	KUNIT_CASE(dm_test_get_elapse_time_zero_delta),
	KUNIT_CASE(dm_test_get_elapse_time_positive_delta),
	KUNIT_CASE(dm_test_get_elapse_time_large_delta),
	KUNIT_CASE(dm_test_get_elapse_time_wraparound),
	/* dm_perf_trace_timestamp */
	KUNIT_CASE(dm_test_perf_trace_timestamp_basic),
	/* dm_trace_smu_enter */
	KUNIT_CASE(dm_test_trace_smu_enter_null_ctx),
	KUNIT_CASE(dm_test_trace_smu_enter_with_params),
	/* dm_trace_smu_exit */
	KUNIT_CASE(dm_test_trace_smu_exit_success_null_ctx),
	KUNIT_CASE(dm_test_trace_smu_exit_failure_null_ctx),
	KUNIT_CASE(dm_test_trace_smu_exit_with_response),
	/* dm_query_extended_brightness_caps */
	KUNIT_CASE(dm_test_query_brightness_caps_null_ctx),
	KUNIT_CASE(dm_test_query_brightness_caps_null_caps),
	KUNIT_CASE(dm_test_query_brightness_caps_null_driver_ctx),
	KUNIT_CASE(dm_test_query_brightness_caps_lcd2_null_ctx),
	KUNIT_CASE(dm_test_query_brightness_caps_lcd1_success),
	KUNIT_CASE(dm_test_query_brightness_caps_non_lcd1_uses_second_slot),
	{}
};

static struct kunit_suite amdgpu_dm_services_test_suite = {
	.name = "amdgpu_dm_services",
	.test_cases = amdgpu_dm_services_test_cases,
};

static struct kunit_case amdgpu_dm_services_gpu_mem_test_cases[] = {
	/* dm_allocate_gpu_mem */
	KUNIT_CASE(dm_test_allocate_gpu_mem_gart),
	KUNIT_CASE(dm_test_allocate_gpu_mem_frame_buffer),
	KUNIT_CASE(dm_test_allocate_gpu_mem_create_fails),
	/* dm_free_gpu_mem */
	KUNIT_CASE(dm_test_free_gpu_mem_matching),
	KUNIT_CASE(dm_test_free_gpu_mem_no_match),
	KUNIT_CASE(dm_test_free_gpu_mem_empty_list),
	{}
};

static struct kunit_suite amdgpu_dm_services_gpu_mem_test_suite = {
	.name = "amdgpu_dm_services_gpu_mem",
	.init = dm_test_gpu_mem_init,
	.exit = dm_test_gpu_mem_exit,
	.test_cases = amdgpu_dm_services_gpu_mem_test_cases,
};

kunit_test_suites(&amdgpu_dm_services_test_suite,
		  &amdgpu_dm_services_gpu_mem_test_suite);

MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_services");
MODULE_LICENSE("Dual MIT/GPL");
