// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_pp_smu.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include "dc.h"
#include "dm_services.h"
#include "dm_pp_smu.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_pp_smu.h"

/* ---- Stub DPM layer ---- */

/**
 * struct stub_dpm_context - Tracks stub DPM callback invocations
 * @ret_val: Return value for the next DPM callback
 * @get_current_clocks_info: Clock info returned by stub get_current_clocks
 * @get_clock_by_type_clocks: Clocks returned by stub get_clock_by_type
 * @get_validation_clks: Validation clocks returned by stub
 * @get_clock_by_type_with_latency_clks: Returned by stub with_latency
 * @get_clock_by_type_with_voltage_clks: Returned by stub with_voltage
 * @set_watermarks_ret: Return value for set_watermarks
 * @display_clock_voltage_ret: Return value for display_clock_voltage_request
 * @display_disable_memory_clock_switch_ret: Return for disable_memory_clock
 * @get_max_sustainable_ret: Return for get_max_sustainable_clocks_by_dc
 * @get_uclk_dpm_ret: Return for get_uclk_dpm_states
 * @get_dpm_clock_table_ret: Return for get_dpm_clock_table
 * @set_active_display_count_ret: Return for set_active_display_count
 * @set_min_deep_sleep_dcefclk_ret: Return for set_min_deep_sleep_dcefclk
 * @get_validation_clks_ret: Return for get_display_mode_validation_clocks
 */
struct stub_dpm_context {
	int ret_val;
	struct amd_pp_clock_info get_current_clocks_info;
	struct amd_pp_clocks get_clock_by_type_clocks;
	struct amd_pp_simple_clock_info get_validation_clks;
	int get_validation_clks_ret;
	struct pp_clock_levels_with_latency get_clock_by_type_with_latency_clks;
	struct pp_clock_levels_with_voltage get_clock_by_type_with_voltage_clks;
	int set_watermarks_ret;
	int display_clock_voltage_ret;
	int display_disable_memory_clock_switch_ret;
	int get_max_sustainable_ret;
	int get_uclk_dpm_ret;
	int get_dpm_clock_table_ret;
	int set_active_display_count_ret;
	int set_min_deep_sleep_dcefclk_ret;
};

static struct stub_dpm_context *stub_dpm_ctx;

static int stub_get_current_clocks(void *handle, struct amd_pp_clock_info *clocks)
{
	if (stub_dpm_ctx->ret_val)
		return stub_dpm_ctx->ret_val;
	*clocks = stub_dpm_ctx->get_current_clocks_info;
	return 0;
}

static int stub_get_clock_by_type(void *handle, enum amd_pp_clock_type type,
				  struct amd_pp_clocks *clocks)
{
	if (stub_dpm_ctx->ret_val)
		return stub_dpm_ctx->ret_val;
	*clocks = stub_dpm_ctx->get_clock_by_type_clocks;
	return 0;
}

static int stub_get_display_mode_validation_clocks(void *handle,
						   struct amd_pp_simple_clock_info *clocks)
{
	if (stub_dpm_ctx->get_validation_clks_ret)
		return stub_dpm_ctx->get_validation_clks_ret;
	*clocks = stub_dpm_ctx->get_validation_clks;
	return 0;
}

static int stub_get_clock_by_type_with_latency(void *handle,
					       enum amd_pp_clock_type type,
					       struct pp_clock_levels_with_latency *clocks)
{
	if (stub_dpm_ctx->ret_val)
		return stub_dpm_ctx->ret_val;
	*clocks = stub_dpm_ctx->get_clock_by_type_with_latency_clks;
	return 0;
}

static int stub_get_clock_by_type_with_voltage(void *handle,
					       enum amd_pp_clock_type type,
					       struct pp_clock_levels_with_voltage *clocks)
{
	if (stub_dpm_ctx->ret_val)
		return stub_dpm_ctx->ret_val;
	*clocks = stub_dpm_ctx->get_clock_by_type_with_voltage_clks;
	return 0;
}

static void stub_display_configuration_change(void *handle)
{
	/* No-op: satisfies display_configuration_changed callback */
}

static void stub_pm_compute_clocks(void *handle)
{
	/* No-op: satisfies pm_compute_clocks callback */
}

static int stub_set_watermarks_for_clocks_ranges(void *handle, void *clock_ranges)
{
	return stub_dpm_ctx->set_watermarks_ret;
}

static int stub_display_clock_voltage_request(void *handle,
					      struct pp_display_clock_request *clock)
{
	return stub_dpm_ctx->display_clock_voltage_ret;
}

static int stub_set_active_display_count(void *handle, uint32_t count)
{
	return stub_dpm_ctx->set_active_display_count_ret;
}

static int stub_set_min_deep_sleep_dcefclk(void *handle, uint32_t clock)
{
	return stub_dpm_ctx->set_min_deep_sleep_dcefclk_ret;
}

static int stub_set_hard_min_dcefclk_by_freq(void *handle, uint32_t clock)
{
	return 0;
}

static int stub_set_hard_min_fclk_by_freq(void *handle, uint32_t clock)
{
	return 0;
}

static int stub_notify_smu_enable_pwe(void *handle)
{
	return 0;
}

static int stub_display_disable_memory_clock_switch(void *handle,
						    bool disable_memory_clock_switch)
{
	return stub_dpm_ctx->display_disable_memory_clock_switch_ret;
}

static int stub_get_max_sustainable_clocks_by_dc(void *handle,
						 struct pp_smu_nv_clock_table *max_clocks)
{
	return stub_dpm_ctx->get_max_sustainable_ret;
}

static int stub_get_uclk_dpm_states(void *handle,
				    unsigned int *clock_values_in_khz,
				    unsigned int *num_states)
{
	return stub_dpm_ctx->get_uclk_dpm_ret;
}

static int stub_get_dpm_clock_table(void *handle, struct dpm_clocks *clock_table)
{
	return stub_dpm_ctx->get_dpm_clock_table_ret;
}

static const struct amd_pm_funcs stub_pp_funcs = {
	.get_current_clocks = stub_get_current_clocks,
	.get_clock_by_type = stub_get_clock_by_type,
	.get_display_mode_validation_clocks = stub_get_display_mode_validation_clocks,
	.get_clock_by_type_with_latency = stub_get_clock_by_type_with_latency,
	.get_clock_by_type_with_voltage = stub_get_clock_by_type_with_voltage,
	.display_configuration_changed = stub_display_configuration_change,
	.pm_compute_clocks = stub_pm_compute_clocks,
	.set_watermarks_for_clocks_ranges = stub_set_watermarks_for_clocks_ranges,
	.display_clock_voltage_request = stub_display_clock_voltage_request,
	.set_active_display_count = stub_set_active_display_count,
	.set_min_deep_sleep_dcefclk = stub_set_min_deep_sleep_dcefclk,
	.set_hard_min_dcefclk_by_freq = stub_set_hard_min_dcefclk_by_freq,
	.set_hard_min_fclk_by_freq = stub_set_hard_min_fclk_by_freq,
	.notify_smu_enable_pwe = stub_notify_smu_enable_pwe,
	.display_disable_memory_clock_switch = stub_display_disable_memory_clock_switch,
	.get_max_sustainable_clocks_by_dc = stub_get_max_sustainable_clocks_by_dc,
	.get_uclk_dpm_states = stub_get_uclk_dpm_states,
	.get_dpm_clock_table = stub_get_dpm_clock_table,
};

/**
 * setup_stub_dpm - Initialize a stub DPM environment for testing
 * @test: KUnit test context
 * @adev: Pointer to amdgpu_device to configure
 *
 * Sets up adev->powerplay.pp_funcs and initializes adev->pm.mutex so that
 * amdgpu_dpm_* functions can be safely called with stub callbacks.
 */
static void setup_stub_dpm(struct kunit *test, struct amdgpu_device *adev)
{
	stub_dpm_ctx = kunit_kzalloc(test, sizeof(*stub_dpm_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stub_dpm_ctx);

	adev->powerplay.pp_funcs = &stub_pp_funcs;
	adev->powerplay.pp_handle = adev;
	mutex_init(&adev->pm.mutex);
}

/* ---- Tests for get_default_clock_levels ---- */

/**
 * dm_test_default_clock_levels_display - Test display clock default levels
 * @test: KUnit test context
 *
 * Verify that get_default_clock_levels populates 6 display clock levels
 * with the expected frequencies in kHz.
 */
static void dm_test_default_clock_levels_display(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };
	uint32_t expected[] = { 300000, 400000, 496560, 626090, 685720, 757900 };
	int i;

	get_default_clock_levels(DM_PP_CLOCK_TYPE_DISPLAY_CLK, &clks);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 6U);
	for (i = 0; i < 6; i++)
		KUNIT_EXPECT_EQ(test, clks.clocks_in_khz[i], expected[i]);
}

/**
 * dm_test_default_clock_levels_engine - Test engine clock default levels
 * @test: KUnit test context
 *
 * Verify that get_default_clock_levels populates 6 engine clock levels
 * with the expected frequencies in kHz.
 */
static void dm_test_default_clock_levels_engine(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };
	uint32_t expected[] = { 300000, 360000, 423530, 514290, 626090, 720000 };
	int i;

	get_default_clock_levels(DM_PP_CLOCK_TYPE_ENGINE_CLK, &clks);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 6U);
	for (i = 0; i < 6; i++)
		KUNIT_EXPECT_EQ(test, clks.clocks_in_khz[i], expected[i]);
}

/**
 * dm_test_default_clock_levels_memory - Test memory clock default levels
 * @test: KUnit test context
 *
 * Verify that get_default_clock_levels populates 2 memory clock levels
 * with the expected frequencies in kHz.
 */
static void dm_test_default_clock_levels_memory(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };

	get_default_clock_levels(DM_PP_CLOCK_TYPE_MEMORY_CLK, &clks);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 2U);
	KUNIT_EXPECT_EQ(test, clks.clocks_in_khz[0], 333000U);
	KUNIT_EXPECT_EQ(test, clks.clocks_in_khz[1], 800000U);
}

/**
 * dm_test_default_clock_levels_unknown - Test unknown clock type default
 * @test: KUnit test context
 *
 * Verify that get_default_clock_levels sets num_levels to 0 for an
 * unrecognized clock type.
 */
static void dm_test_default_clock_levels_unknown(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };

	get_default_clock_levels(DM_PP_CLOCK_TYPE_FCLK, &clks);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 0U);
}

/* ---- Tests for dc_to_pp_clock_type ---- */

/**
 * dm_test_dc_to_pp_clock_type_display - Test display clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_DISPLAY_CLK maps to amd_pp_disp_clock.
 */
static void dm_test_dc_to_pp_clock_type_display(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_DISPLAY_CLK),
			(int)amd_pp_disp_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_engine - Test engine clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_ENGINE_CLK maps to amd_pp_sys_clock.
 */
static void dm_test_dc_to_pp_clock_type_engine(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_ENGINE_CLK),
			(int)amd_pp_sys_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_memory - Test memory clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_MEMORY_CLK maps to amd_pp_mem_clock.
 */
static void dm_test_dc_to_pp_clock_type_memory(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_MEMORY_CLK),
			(int)amd_pp_mem_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_dcefclk - Test DCEF clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_DCEFCLK maps to amd_pp_dcef_clock.
 */
static void dm_test_dc_to_pp_clock_type_dcefclk(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_DCEFCLK),
			(int)amd_pp_dcef_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_dcfclk - Test DCF clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_DCFCLK maps to amd_pp_dcf_clock.
 */
static void dm_test_dc_to_pp_clock_type_dcfclk(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_DCFCLK),
			(int)amd_pp_dcf_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_pixelclk - Test pixel clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_PIXELCLK maps to amd_pp_pixel_clock.
 */
static void dm_test_dc_to_pp_clock_type_pixelclk(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_PIXELCLK),
			(int)amd_pp_pixel_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_fclk - Test FCLK type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_FCLK maps to amd_pp_f_clock.
 */
static void dm_test_dc_to_pp_clock_type_fclk(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_FCLK),
			(int)amd_pp_f_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_phyclk - Test display PHY clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_DISPLAYPHYCLK maps to amd_pp_phy_clock.
 */
static void dm_test_dc_to_pp_clock_type_phyclk(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_DISPLAYPHYCLK),
			(int)amd_pp_phy_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_dppclk - Test DPP clock type mapping
 * @test: KUnit test context
 *
 * Verify DM_PP_CLOCK_TYPE_DPPCLK maps to amd_pp_dpp_clock.
 */
static void dm_test_dc_to_pp_clock_type_dppclk(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(DM_PP_CLOCK_TYPE_DPPCLK),
			(int)amd_pp_dpp_clock);
}

/**
 * dm_test_dc_to_pp_clock_type_invalid - Test invalid clock type mapping
 * @test: KUnit test context
 *
 * Verify that an invalid clock type value maps to 0.
 */
static void dm_test_dc_to_pp_clock_type_invalid(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)dc_to_pp_clock_type(0), 0);
}

/* ---- Tests for pp_to_dc_clock_levels ---- */

/**
 * dm_test_pp_to_dc_clock_levels_within_limit - Test normal copy within limit
 * @test: KUnit test context
 *
 * Verify that pp_to_dc_clock_levels correctly copies clock values when the
 * count is within DM_PP_MAX_CLOCK_LEVELS.
 */
static void dm_test_pp_to_dc_clock_levels_within_limit(struct kunit *test)
{
	struct amd_pp_clocks pp_clks = {};
	struct dm_pp_clock_levels dc_clks = {};

	pp_clks.count = 3;
	pp_clks.clock[0] = 300000;
	pp_clks.clock[1] = 500000;
	pp_clks.clock[2] = 700000;

	pp_to_dc_clock_levels(&pp_clks, &dc_clks, DM_PP_CLOCK_TYPE_ENGINE_CLK);

	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, 3U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[0], 300000U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[1], 500000U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[2], 700000U);
}

/**
 * dm_test_pp_to_dc_clock_levels_caps_at_max - Test count capping at max
 * @test: KUnit test context
 *
 * Verify that pp_to_dc_clock_levels caps num_levels at DM_PP_MAX_CLOCK_LEVELS
 * when the input count exceeds the maximum.
 */
static void dm_test_pp_to_dc_clock_levels_caps_at_max(struct kunit *test)
{
	struct amd_pp_clocks pp_clks = {};
	struct dm_pp_clock_levels dc_clks = {};
	uint32_t i;

	pp_clks.count = DM_PP_MAX_CLOCK_LEVELS + 1;
	for (i = 0; i < DM_PP_MAX_CLOCK_LEVELS; i++)
		pp_clks.clock[i] = (i + 1) * 100000;

	pp_to_dc_clock_levels(&pp_clks, &dc_clks, DM_PP_CLOCK_TYPE_ENGINE_CLK);

	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, (uint32_t)DM_PP_MAX_CLOCK_LEVELS);
}

/* ---- Tests for pp_to_dc_clock_levels_with_latency ---- */

/**
 * dm_test_pp_to_dc_clock_levels_latency_within_limit - Test normal copy
 * @test: KUnit test context
 *
 * Verify that pp_to_dc_clock_levels_with_latency correctly copies clock
 * and latency values when count is within limits.
 */
static void dm_test_pp_to_dc_clock_levels_latency_within_limit(struct kunit *test)
{
	struct pp_clock_levels_with_latency pp_clks = {};
	struct dm_pp_clock_levels_with_latency dc_clks = {};

	pp_clks.num_levels = 2;
	pp_clks.data[0].clocks_in_khz = 400000;
	pp_clks.data[0].latency_in_us = 10;
	pp_clks.data[1].clocks_in_khz = 800000;
	pp_clks.data[1].latency_in_us = 20;

	pp_to_dc_clock_levels_with_latency(&pp_clks, &dc_clks,
					   DM_PP_CLOCK_TYPE_ENGINE_CLK);

	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, 2U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[0].clocks_in_khz, 400000U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[0].latency_in_us, 10U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[1].clocks_in_khz, 800000U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[1].latency_in_us, 20U);
}

/**
 * dm_test_pp_to_dc_clock_levels_latency_caps_at_max - Test count capping
 * @test: KUnit test context
 *
 * Verify that pp_to_dc_clock_levels_with_latency caps num_levels at
 * DM_PP_MAX_CLOCK_LEVELS when input exceeds the maximum.
 */
static void dm_test_pp_to_dc_clock_levels_latency_caps_at_max(struct kunit *test)
{
	struct pp_clock_levels_with_latency pp_clks = {};
	struct dm_pp_clock_levels_with_latency dc_clks = {};

	pp_clks.num_levels = DM_PP_MAX_CLOCK_LEVELS + 1;

	pp_to_dc_clock_levels_with_latency(&pp_clks, &dc_clks,
					   DM_PP_CLOCK_TYPE_ENGINE_CLK);

	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, (uint32_t)DM_PP_MAX_CLOCK_LEVELS);
}

/* ---- Tests for pp_to_dc_clock_levels_with_voltage ---- */

/**
 * dm_test_pp_to_dc_clock_levels_voltage_within_limit - Test normal copy
 * @test: KUnit test context
 *
 * Verify that pp_to_dc_clock_levels_with_voltage correctly copies clock
 * and voltage values when count is within limits.
 */
static void dm_test_pp_to_dc_clock_levels_voltage_within_limit(struct kunit *test)
{
	struct pp_clock_levels_with_voltage pp_clks = {};
	struct dm_pp_clock_levels_with_voltage dc_clks = {};

	pp_clks.num_levels = 2;
	pp_clks.data[0].clocks_in_khz = 300000;
	pp_clks.data[0].voltage_in_mv = 800;
	pp_clks.data[1].clocks_in_khz = 600000;
	pp_clks.data[1].voltage_in_mv = 950;

	pp_to_dc_clock_levels_with_voltage(&pp_clks, &dc_clks,
					   DM_PP_CLOCK_TYPE_MEMORY_CLK);

	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, 2U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[0].clocks_in_khz, 300000U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[0].voltage_in_mv, 800U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[1].clocks_in_khz, 600000U);
	KUNIT_EXPECT_EQ(test, dc_clks.data[1].voltage_in_mv, 950U);
}

/**
 * dm_test_pp_to_dc_clock_levels_voltage_caps_at_max - Test count capping
 * @test: KUnit test context
 *
 * Verify that pp_to_dc_clock_levels_with_voltage caps num_levels at
 * DM_PP_MAX_CLOCK_LEVELS when input exceeds the maximum.
 */
static void dm_test_pp_to_dc_clock_levels_voltage_caps_at_max(struct kunit *test)
{
	struct pp_clock_levels_with_voltage pp_clks = {};
	struct dm_pp_clock_levels_with_voltage dc_clks = {};

	pp_clks.num_levels = DM_PP_MAX_CLOCK_LEVELS + 1;

	pp_to_dc_clock_levels_with_voltage(&pp_clks, &dc_clks,
					   DM_PP_CLOCK_TYPE_MEMORY_CLK);

	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, (uint32_t)DM_PP_MAX_CLOCK_LEVELS);
}

/* ---- Tests for dm_pp_get_funcs ---- */

/**
 * dm_test_get_funcs_rv - Test Raven PP SMU function table setup
 * @test: KUnit test context
 *
 * Verify that DCN 1.0 initializes the Raven SMU function table and stores
 * the DC context in the PP SMU handle.
 */
static void dm_test_get_funcs_rv(struct kunit *test)
{
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu_funcs *funcs = kunit_kzalloc(test, sizeof(*funcs), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_NOT_NULL(test, funcs);

	ctx->dce_version = DCN_VERSION_1_0;

	dm_pp_get_funcs(ctx, funcs);

	KUNIT_EXPECT_EQ(test, funcs->ctx.ver, PP_SMU_VER_RV);
	KUNIT_EXPECT_PTR_EQ(test, funcs->rv_funcs.pp_smu.dm, ctx);
	KUNIT_EXPECT_TRUE(test, funcs->rv_funcs.set_wm_ranges != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->rv_funcs.set_pme_wa_enable != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->rv_funcs.set_display_count != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->rv_funcs.set_min_deep_sleep_dcfclk != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->rv_funcs.set_hard_min_dcfclk_by_freq != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->rv_funcs.set_hard_min_fclk_by_freq != NULL);
	KUNIT_EXPECT_FALSE(test, funcs->rv_funcs.set_hard_min_socclk_by_freq != NULL);
}

/**
 * dm_test_get_funcs_rv_101 - Test DCN 1.01 Raven PP SMU setup
 * @test: KUnit test context
 *
 * Verify that DCN 1.01 uses the same Raven SMU function table as DCN 1.0.
 */
static void dm_test_get_funcs_rv_101(struct kunit *test)
{
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu_funcs *funcs = kunit_kzalloc(test, sizeof(*funcs), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_NOT_NULL(test, funcs);

	ctx->dce_version = DCN_VERSION_1_01;

	dm_pp_get_funcs(ctx, funcs);

	KUNIT_EXPECT_EQ(test, funcs->ctx.ver, PP_SMU_VER_RV);
	KUNIT_EXPECT_PTR_EQ(test, funcs->rv_funcs.pp_smu.dm, ctx);
	KUNIT_EXPECT_TRUE(test, funcs->rv_funcs.set_display_count != NULL);
}

/**
 * dm_test_get_funcs_nv - Test Navi PP SMU function table setup
 * @test: KUnit test context
 *
 * Verify that DCN 2.0 initializes the Navi SMU function table and leaves the
 * unsupported PME workaround callback unset.
 */
static void dm_test_get_funcs_nv(struct kunit *test)
{
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu_funcs *funcs = kunit_kzalloc(test, sizeof(*funcs), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_NOT_NULL(test, funcs);

	ctx->dce_version = DCN_VERSION_2_0;

	dm_pp_get_funcs(ctx, funcs);

	KUNIT_EXPECT_EQ(test, funcs->ctx.ver, PP_SMU_VER_NV);
	KUNIT_EXPECT_PTR_EQ(test, funcs->nv_funcs.pp_smu.dm, ctx);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.set_display_count != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.set_hard_min_dcfclk_by_freq != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.set_min_deep_sleep_dcfclk != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.set_voltage_by_freq != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.set_wm_ranges != NULL);
	KUNIT_EXPECT_FALSE(test, funcs->nv_funcs.set_pme_wa_enable != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.set_hard_min_uclk_by_freq != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.get_maximum_sustainable_clocks != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.get_uclk_dpm_states != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->nv_funcs.set_pstate_handshake_support != NULL);
}

/**
 * dm_test_get_funcs_rn - Test Renoir PP SMU function table setup
 * @test: KUnit test context
 *
 * Verify that DCN 2.1 initializes the Renoir SMU function table.
 */
static void dm_test_get_funcs_rn(struct kunit *test)
{
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu_funcs *funcs = kunit_kzalloc(test, sizeof(*funcs), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_NOT_NULL(test, funcs);

	ctx->dce_version = DCN_VERSION_2_1;

	dm_pp_get_funcs(ctx, funcs);

	KUNIT_EXPECT_EQ(test, funcs->ctx.ver, PP_SMU_VER_RN);
	KUNIT_EXPECT_PTR_EQ(test, funcs->rn_funcs.pp_smu.dm, ctx);
	KUNIT_EXPECT_TRUE(test, funcs->rn_funcs.set_wm_ranges != NULL);
	KUNIT_EXPECT_TRUE(test, funcs->rn_funcs.get_dpm_clock_table != NULL);
}

/**
 * dm_test_get_funcs_unsupported - Test unsupported DCE version handling
 * @test: KUnit test context
 *
 * Verify that unsupported DCE versions do not initialize a PP SMU version or
 * function table callbacks.
 */
static void dm_test_get_funcs_unsupported(struct kunit *test)
{
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu_funcs *funcs = kunit_kzalloc(test, sizeof(*funcs), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_NOT_NULL(test, funcs);

	ctx->dce_version = DCE_VERSION_MAX;

	dm_pp_get_funcs(ctx, funcs);

	KUNIT_EXPECT_EQ(test, funcs->ctx.ver, PP_SMU_UNSUPPORTED);
	KUNIT_EXPECT_FALSE(test, funcs->rv_funcs.set_wm_ranges != NULL);
}

/* ---- Tests for amdgpu_device-backed entry points ---- */

/**
 * dm_test_apply_display_requirements_dpm_disabled - Test DPM-disabled path
 * @test: KUnit test context
 *
 * Verify that dm_pp_apply_display_requirements returns true without touching
 * the display configuration when DPM is disabled.
 */
static void dm_test_apply_display_requirements_dpm_disabled(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_display_configuration cfg = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	adev->pm.dpm_enabled = false;
	ctx->driver_context = adev;

	KUNIT_EXPECT_TRUE(test, dm_pp_apply_display_requirements(ctx, &cfg));
}

/**
 * dm_test_apply_clock_for_voltage_invalid_type - Test invalid clock type path
 * @test: KUnit test context
 *
 * Verify that dm_pp_apply_clock_for_voltage_request returns false for a clock
 * type that does not map to a valid PP clock type, taking the early-return
 * path before any SMU request is issued.
 */
static void dm_test_apply_clock_for_voltage_invalid_type(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_for_voltage_req req = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->driver_context = adev;
	req.clk_type = (enum dm_pp_clock_type)0xffff;
	req.clocks_in_khz = 500000;

	KUNIT_EXPECT_FALSE(test, dm_pp_apply_clock_for_voltage_request(ctx, &req));
}

/* ---- Tests for build_pm_display_cfg ---- */

/**
 * dm_test_build_pm_display_cfg_scalar_fields - Test scalar field translation
 * @test: KUnit test context
 *
 * Verify that build_pm_display_cfg copies the pass-through fields and applies
 * the /10 (10 kHz) scaling, and sets the fixed constants.
 */
static void dm_test_build_pm_display_cfg_scalar_fields(struct kunit *test)
{
	struct amd_pp_display_configuration *pm =
		kunit_kzalloc(test, sizeof(*pm), GFP_KERNEL);
	struct dm_pp_display_configuration *pp =
		kunit_kzalloc(test, sizeof(*pp), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, pm);
	KUNIT_ASSERT_NOT_NULL(test, pp);

	pp->cpu_cc6_disable = true;
	pp->cpu_pstate_disable = true;
	pp->cpu_pstate_separation_time = 7;
	pp->nb_pstate_switch_disable = true;
	pp->display_count = 2;
	pp->min_engine_clock_khz = 300000;
	pp->min_engine_clock_deep_sleep_khz = 50000;
	pp->min_memory_clock_khz = 800000;
	pp->min_dcfclock_khz = 600000;
	pp->all_displays_in_sync = true;
	pp->avail_mclk_switch_time_us = 11;
	pp->disp_clk_khz = 400000;
	pp->avail_mclk_switch_time_in_disp_active_us = 13;
	pp->crtc_index = 3;
	pp->line_time_in_us = 17;
	pp->disp_configs[0].v_refresh = 60;

	build_pm_display_cfg(pm, pp);

	KUNIT_EXPECT_TRUE(test, pm->cpu_cc6_disable);
	KUNIT_EXPECT_TRUE(test, pm->cpu_pstate_disable);
	KUNIT_EXPECT_EQ(test, pm->cpu_pstate_separation_time, 7);
	KUNIT_EXPECT_TRUE(test, pm->nb_pstate_switch_disable);
	KUNIT_EXPECT_EQ(test, pm->num_display, 2);
	KUNIT_EXPECT_EQ(test, pm->num_path_including_non_display, 2);
	KUNIT_EXPECT_EQ(test, pm->min_core_set_clock, 30000);
	KUNIT_EXPECT_EQ(test, pm->min_core_set_clock_in_sr, 5000);
	KUNIT_EXPECT_EQ(test, pm->min_mem_set_clock, 80000);
	KUNIT_EXPECT_EQ(test, pm->min_dcef_deep_sleep_set_clk, 5000);
	KUNIT_EXPECT_EQ(test, pm->min_dcef_set_clk, 60000);
	KUNIT_EXPECT_TRUE(test, pm->multi_monitor_in_sync);
	KUNIT_EXPECT_EQ(test, pm->min_vblank_time, 11);
	KUNIT_EXPECT_EQ(test, pm->display_clk, 40000);
	KUNIT_EXPECT_EQ(test, pm->dce_tolerable_mclk_in_active_latency, 13);
	KUNIT_EXPECT_EQ(test, pm->crtc_index, 3);
	KUNIT_EXPECT_EQ(test, pm->line_time_in_us, 17);
	KUNIT_EXPECT_EQ(test, pm->vrefresh, 60);
	KUNIT_EXPECT_EQ(test, pm->crossfire_display_index, -1);
	KUNIT_EXPECT_EQ(test, pm->min_bus_bandwidth, 0);
}

/**
 * dm_test_build_pm_display_cfg_per_display - Test per-display translation
 * @test: KUnit test context
 *
 * Verify that build_pm_display_cfg maps each display config, applying the
 * controller_id = pipe_idx + 1 offset and copying the pixel clock.
 */
static void dm_test_build_pm_display_cfg_per_display(struct kunit *test)
{
	struct amd_pp_display_configuration *pm =
		kunit_kzalloc(test, sizeof(*pm), GFP_KERNEL);
	struct dm_pp_display_configuration *pp =
		kunit_kzalloc(test, sizeof(*pp), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, pm);
	KUNIT_ASSERT_NOT_NULL(test, pp);

	pp->display_count = 2;
	pp->disp_configs[0].pipe_idx = 0;
	pp->disp_configs[0].pixel_clock = 148500;
	pp->disp_configs[1].pipe_idx = 4;
	pp->disp_configs[1].pixel_clock = 297000;

	build_pm_display_cfg(pm, pp);

	KUNIT_EXPECT_EQ(test, pm->displays[0].controller_id, 1);
	KUNIT_EXPECT_EQ(test, pm->displays[0].pixel_clock, 148500);
	KUNIT_EXPECT_EQ(test, pm->displays[1].controller_id, 5);
	KUNIT_EXPECT_EQ(test, pm->displays[1].pixel_clock, 297000);
}

/* ---- Tests for build_wm_clock_ranges_soc15 ---- */

/**
 * dm_test_build_wm_clock_ranges_dmif - Test reader (DMIF) watermark sets
 * @test: KUnit test context
 *
 * Verify that build_wm_clock_ranges_soc15 copies the reader set count,
 * maps wm_inst to wm_set_id (clamping instances > 3 to WM_SET_A), and
 * converts every clock from MHz to kHz (x1000) into the DMIF clock ranges.
 */
static void dm_test_build_wm_clock_ranges_dmif(struct kunit *test)
{
	struct pp_smu_wm_range_sets *ranges =
		kunit_kzalloc(test, sizeof(*ranges), GFP_KERNEL);
	struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm =
		kunit_kzalloc(test, sizeof(*wm), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ranges);
	KUNIT_ASSERT_NOT_NULL(test, wm);

	ranges->num_reader_wm_sets = 2;
	/* set 0: wm_inst within range -> preserved */
	ranges->reader_wm_sets[0].wm_inst = 2;
	ranges->reader_wm_sets[0].max_drain_clk_mhz = 600;
	ranges->reader_wm_sets[0].min_drain_clk_mhz = 300;
	ranges->reader_wm_sets[0].max_fill_clk_mhz = 800;
	ranges->reader_wm_sets[0].min_fill_clk_mhz = 400;
	/* set 1: wm_inst > 3 -> clamped to WM_SET_A */
	ranges->reader_wm_sets[1].wm_inst = 5;
	ranges->reader_wm_sets[1].max_drain_clk_mhz = 700;
	ranges->reader_wm_sets[1].min_drain_clk_mhz = 350;
	ranges->reader_wm_sets[1].max_fill_clk_mhz = 900;
	ranges->reader_wm_sets[1].min_fill_clk_mhz = 450;

	build_wm_clock_ranges_soc15(ranges, wm);

	KUNIT_EXPECT_EQ(test, wm->num_wm_dmif_sets, 2U);
	KUNIT_EXPECT_EQ(test, wm->num_wm_mcif_sets, 0U);

	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[0].wm_set_id, WM_SET_C);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[0].wm_max_dcfclk_clk_in_khz, 600000U);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[0].wm_min_dcfclk_clk_in_khz, 300000U);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[0].wm_max_mem_clk_in_khz, 800000U);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[0].wm_min_mem_clk_in_khz, 400000U);

	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[1].wm_set_id, WM_SET_A);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[1].wm_max_dcfclk_clk_in_khz, 700000U);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[1].wm_min_dcfclk_clk_in_khz, 350000U);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[1].wm_max_mem_clk_in_khz, 900000U);
	KUNIT_EXPECT_EQ(test, wm->wm_dmif_clocks_ranges[1].wm_min_mem_clk_in_khz, 450000U);
}

/**
 * dm_test_build_wm_clock_ranges_mcif - Test writer (MCIF) watermark sets
 * @test: KUnit test context
 *
 * Verify that build_wm_clock_ranges_soc15 copies the writer set count and
 * maps the writer clocks into the MCIF ranges: fill clocks become socclk
 * and drain clocks become mem clk, each converted from MHz to kHz.
 */
static void dm_test_build_wm_clock_ranges_mcif(struct kunit *test)
{
	struct pp_smu_wm_range_sets *ranges =
		kunit_kzalloc(test, sizeof(*ranges), GFP_KERNEL);
	struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm =
		kunit_kzalloc(test, sizeof(*wm), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ranges);
	KUNIT_ASSERT_NOT_NULL(test, wm);

	ranges->num_writer_wm_sets = 2;
	ranges->writer_wm_sets[0].wm_inst = 1;
	ranges->writer_wm_sets[0].max_fill_clk_mhz = 1200;
	ranges->writer_wm_sets[0].min_fill_clk_mhz = 600;
	ranges->writer_wm_sets[0].max_drain_clk_mhz = 1000;
	ranges->writer_wm_sets[0].min_drain_clk_mhz = 500;
	/* set 1: wm_inst > 3 -> clamped to WM_SET_A */
	ranges->writer_wm_sets[1].wm_inst = 5;
	ranges->writer_wm_sets[1].max_fill_clk_mhz = 1400;
	ranges->writer_wm_sets[1].min_fill_clk_mhz = 700;
	ranges->writer_wm_sets[1].max_drain_clk_mhz = 1100;
	ranges->writer_wm_sets[1].min_drain_clk_mhz = 550;

	build_wm_clock_ranges_soc15(ranges, wm);

	KUNIT_EXPECT_EQ(test, wm->num_wm_dmif_sets, 0U);
	KUNIT_EXPECT_EQ(test, wm->num_wm_mcif_sets, 2U);

	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[0].wm_set_id, WM_SET_B);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[0].wm_max_socclk_clk_in_khz, 1200000U);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[0].wm_min_socclk_clk_in_khz, 600000U);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[0].wm_max_mem_clk_in_khz, 1000000U);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[0].wm_min_mem_clk_in_khz, 500000U);

	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[1].wm_set_id, WM_SET_A);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[1].wm_max_socclk_clk_in_khz, 1400000U);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[1].wm_min_socclk_clk_in_khz, 700000U);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[1].wm_max_mem_clk_in_khz, 1100000U);
	KUNIT_EXPECT_EQ(test, wm->wm_mcif_clocks_ranges[1].wm_min_mem_clk_in_khz, 550000U);
}

/* ---- Tests for cap_clock_levels_to_validation ---- */

/**
 * dm_test_cap_clock_levels_engine_caps - Test engine clock level capping
 * @test: KUnit test context
 *
 * Verify that for engine clocks, num_levels is reduced to the index of the
 * first level whose frequency exceeds the engine validation clock.
 */
static void dm_test_cap_clock_levels_engine_caps(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };
	struct amd_pp_simple_clock_info validation = {
		.engine_max_clock = 450000,
		.memory_max_clock = 800000,
	};

	clks.num_levels = 3;
	clks.clocks_in_khz[0] = 300000;
	clks.clocks_in_khz[1] = 400000;
	clks.clocks_in_khz[2] = 500000;

	cap_clock_levels_to_validation(&clks, DM_PP_CLOCK_TYPE_ENGINE_CLK, &validation);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 2U);
}

/**
 * dm_test_cap_clock_levels_engine_first_exceeds - Test floor of one level
 * @test: KUnit test context
 *
 * Verify that when the very first engine clock level already exceeds the
 * validation clock, num_levels is clamped to 1 rather than 0.
 */
static void dm_test_cap_clock_levels_engine_first_exceeds(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };
	struct amd_pp_simple_clock_info validation = {
		.engine_max_clock = 100000,
		.memory_max_clock = 800000,
	};

	clks.num_levels = 3;
	clks.clocks_in_khz[0] = 300000;
	clks.clocks_in_khz[1] = 400000;
	clks.clocks_in_khz[2] = 500000;

	cap_clock_levels_to_validation(&clks, DM_PP_CLOCK_TYPE_ENGINE_CLK, &validation);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 1U);
}

/**
 * dm_test_cap_clock_levels_memory_caps - Test memory clock level capping
 * @test: KUnit test context
 *
 * Verify that for memory clocks, num_levels is reduced based on the memory
 * validation clock (and is unaffected by the engine validation clock).
 */
static void dm_test_cap_clock_levels_memory_caps(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };
	struct amd_pp_simple_clock_info validation = {
		.engine_max_clock = 100000,
		.memory_max_clock = 700000,
	};

	clks.num_levels = 2;
	clks.clocks_in_khz[0] = 333000;
	clks.clocks_in_khz[1] = 800000;

	cap_clock_levels_to_validation(&clks, DM_PP_CLOCK_TYPE_MEMORY_CLK, &validation);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 1U);
}

/**
 * dm_test_cap_clock_levels_within_limit - Test no capping when within limit
 * @test: KUnit test context
 *
 * Verify that num_levels is left unchanged when no level exceeds the
 * validation clock.
 */
static void dm_test_cap_clock_levels_within_limit(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };
	struct amd_pp_simple_clock_info validation = {
		.engine_max_clock = 999000,
		.memory_max_clock = 999000,
	};

	clks.num_levels = 3;
	clks.clocks_in_khz[0] = 300000;
	clks.clocks_in_khz[1] = 400000;
	clks.clocks_in_khz[2] = 500000;

	cap_clock_levels_to_validation(&clks, DM_PP_CLOCK_TYPE_ENGINE_CLK, &validation);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 3U);
}

/**
 * dm_test_cap_clock_levels_other_type - Test non-engine/memory types ignored
 * @test: KUnit test context
 *
 * Verify that for clock types other than engine or memory, num_levels is
 * left unchanged regardless of the validation clocks.
 */
static void dm_test_cap_clock_levels_other_type(struct kunit *test)
{
	struct dm_pp_clock_levels clks = { 0 };
	struct amd_pp_simple_clock_info validation = {
		.engine_max_clock = 1,
		.memory_max_clock = 1,
	};

	clks.num_levels = 3;
	clks.clocks_in_khz[0] = 300000;
	clks.clocks_in_khz[1] = 400000;
	clks.clocks_in_khz[2] = 500000;

	cap_clock_levels_to_validation(&clks, DM_PP_CLOCK_TYPE_DISPLAY_CLK, &validation);

	KUNIT_EXPECT_EQ(test, clks.num_levels, 3U);
}

/* ---- Tests for pp_smu_nv_clock_id_to_pp ---- */

/**
 * dm_test_nv_clock_id_dispclk - Test DISPCLK id mapping
 * @test: KUnit test context
 *
 * Verify that PP_SMU_NV_DISPCLK maps to amd_pp_disp_clock and returns true.
 */
static void dm_test_nv_clock_id_dispclk(struct kunit *test)
{
	enum amd_pp_clock_type clock_type = amd_pp_mem_clock;

	KUNIT_EXPECT_TRUE(test, pp_smu_nv_clock_id_to_pp(PP_SMU_NV_DISPCLK, &clock_type));
	KUNIT_EXPECT_EQ(test, clock_type, amd_pp_disp_clock);
}

/**
 * dm_test_nv_clock_id_phyclk - Test PHYCLK id mapping
 * @test: KUnit test context
 *
 * Verify that PP_SMU_NV_PHYCLK maps to amd_pp_phy_clock and returns true.
 */
static void dm_test_nv_clock_id_phyclk(struct kunit *test)
{
	enum amd_pp_clock_type clock_type = amd_pp_mem_clock;

	KUNIT_EXPECT_TRUE(test, pp_smu_nv_clock_id_to_pp(PP_SMU_NV_PHYCLK, &clock_type));
	KUNIT_EXPECT_EQ(test, clock_type, amd_pp_phy_clock);
}

/**
 * dm_test_nv_clock_id_pixelclk - Test PIXELCLK id mapping
 * @test: KUnit test context
 *
 * Verify that PP_SMU_NV_PIXELCLK maps to amd_pp_pixel_clock and returns true.
 */
static void dm_test_nv_clock_id_pixelclk(struct kunit *test)
{
	enum amd_pp_clock_type clock_type = amd_pp_mem_clock;

	KUNIT_EXPECT_TRUE(test, pp_smu_nv_clock_id_to_pp(PP_SMU_NV_PIXELCLK, &clock_type));
	KUNIT_EXPECT_EQ(test, clock_type, amd_pp_pixel_clock);
}

/**
 * dm_test_nv_clock_id_invalid - Test unknown id is rejected
 * @test: KUnit test context
 *
 * Verify that an unknown clock id returns false and leaves the output
 * clock_type untouched, guarding against the previously uninitialized path.
 */
static void dm_test_nv_clock_id_invalid(struct kunit *test)
{
	enum amd_pp_clock_type clock_type = amd_pp_dcef_clock;

	KUNIT_EXPECT_FALSE(test, pp_smu_nv_clock_id_to_pp((enum pp_smu_nv_clock_id)0xff,
							  &clock_type));
	KUNIT_EXPECT_EQ(test, clock_type, amd_pp_dcef_clock);
}

/* ---- Tests using stub DPM layer ---- */

/**
 * dm_test_apply_display_requirements_dpm_enabled - Test DPM-enabled path
 * @test: KUnit test context
 *
 * Verify that dm_pp_apply_display_requirements calls build_pm_display_cfg
 * and the DPM callbacks when DPM is enabled, and returns true.
 */
static void dm_test_apply_display_requirements_dpm_enabled(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_display_configuration cfg = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	adev->pm.dpm_enabled = true;

	cfg.display_count = 1;
	cfg.min_engine_clock_khz = 300000;
	cfg.disp_configs[0].v_refresh = 60;

	KUNIT_EXPECT_TRUE(test, dm_pp_apply_display_requirements(ctx, &cfg));
	KUNIT_EXPECT_EQ(test, adev->pm.pm_display_cfg.min_core_set_clock, 30000);
	KUNIT_EXPECT_EQ(test, adev->pm.pm_display_cfg.vrefresh, 60);
}

/**
 * dm_test_get_clock_levels_by_type_dpm_error - Test DPM error fallback
 * @test: KUnit test context
 *
 * Verify that dm_pp_get_clock_levels_by_type falls back to default clock
 * levels when amdgpu_dpm_get_clock_by_type returns an error.
 */
static void dm_test_get_clock_levels_by_type_dpm_error(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_levels dc_clks = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	stub_dpm_ctx->ret_val = -EINVAL;

	KUNIT_EXPECT_TRUE(test, dm_pp_get_clock_levels_by_type(ctx,
				DM_PP_CLOCK_TYPE_DISPLAY_CLK, &dc_clks));
	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, 6U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[0], 300000U);
}

/**
 * dm_test_get_clock_levels_by_type_success - Test successful clock query
 * @test: KUnit test context
 *
 * Verify that dm_pp_get_clock_levels_by_type returns the queried clocks
 * capped by validation clocks.
 */
static void dm_test_get_clock_levels_by_type_success(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_levels dc_clks = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;

	stub_dpm_ctx->get_clock_by_type_clocks.count = 3;
	stub_dpm_ctx->get_clock_by_type_clocks.clock[0] = 300000;
	stub_dpm_ctx->get_clock_by_type_clocks.clock[1] = 500000;
	stub_dpm_ctx->get_clock_by_type_clocks.clock[2] = 700000;

	/* validation at 60000 * 10 = 600000 kHz → caps to 2 levels */
	stub_dpm_ctx->get_validation_clks.engine_max_clock = 60000;
	stub_dpm_ctx->get_validation_clks.memory_max_clock = 80000;

	KUNIT_EXPECT_TRUE(test, dm_pp_get_clock_levels_by_type(ctx,
				DM_PP_CLOCK_TYPE_ENGINE_CLK, &dc_clks));
	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, 2U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[0], 300000U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[1], 500000U);
}

/**
 * dm_test_get_clock_levels_by_type_validation_fallback - Test validation error
 * @test: KUnit test context
 *
 * Verify that dm_pp_get_clock_levels_by_type uses default validation clocks
 * (engine=720000, memory=800000 kHz) when get_display_mode_validation_clocks
 * returns an error, capping levels accordingly.
 */
static void dm_test_get_clock_levels_by_type_validation_fallback(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_levels dc_clks = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;

	/* get_clock_by_type succeeds with 3 engine clock levels */
	stub_dpm_ctx->get_clock_by_type_clocks.count = 3;
	stub_dpm_ctx->get_clock_by_type_clocks.clock[0] = 300000;
	stub_dpm_ctx->get_clock_by_type_clocks.clock[1] = 500000;
	stub_dpm_ctx->get_clock_by_type_clocks.clock[2] = 800000;

	/* Force validation clocks to fail → triggers default path */
	stub_dpm_ctx->get_validation_clks_ret = -EINVAL;

	KUNIT_EXPECT_TRUE(test, dm_pp_get_clock_levels_by_type(ctx,
				DM_PP_CLOCK_TYPE_ENGINE_CLK, &dc_clks));
	/*
	 * Default validation: engine_max_clock = 72000 * 10 = 720000 kHz.
	 * Clocks 300000 and 500000 are within limit, 800000 exceeds it,
	 * so num_levels is capped to 2.
	 */
	KUNIT_EXPECT_EQ(test, dc_clks.num_levels, 2U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[0], 300000U);
	KUNIT_EXPECT_EQ(test, dc_clks.clocks_in_khz[1], 500000U);
}

/**
 * dm_test_get_clock_levels_with_latency_success - Test latency clock query
 * @test: KUnit test context
 *
 * Verify dm_pp_get_clock_levels_by_type_with_latency returns true and
 * copies the clock/latency data from the DPM backend.
 */
static void dm_test_get_clock_levels_with_latency_success(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_levels_with_latency info = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;

	stub_dpm_ctx->get_clock_by_type_with_latency_clks.num_levels = 1;
	stub_dpm_ctx->get_clock_by_type_with_latency_clks.data[0].clocks_in_khz = 600000;
	stub_dpm_ctx->get_clock_by_type_with_latency_clks.data[0].latency_in_us = 15;

	KUNIT_EXPECT_TRUE(test, dm_pp_get_clock_levels_by_type_with_latency(ctx,
				DM_PP_CLOCK_TYPE_ENGINE_CLK, &info));
	KUNIT_EXPECT_EQ(test, info.num_levels, 1U);
	KUNIT_EXPECT_EQ(test, info.data[0].clocks_in_khz, 600000U);
	KUNIT_EXPECT_EQ(test, info.data[0].latency_in_us, 15U);
}

/**
 * dm_test_get_clock_levels_with_latency_failure - Test latency query error
 * @test: KUnit test context
 *
 * Verify dm_pp_get_clock_levels_by_type_with_latency returns false on DPM error.
 */
static void dm_test_get_clock_levels_with_latency_failure(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_levels_with_latency info = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	stub_dpm_ctx->ret_val = -EINVAL;

	KUNIT_EXPECT_FALSE(test, dm_pp_get_clock_levels_by_type_with_latency(ctx,
				DM_PP_CLOCK_TYPE_ENGINE_CLK, &info));
}

/**
 * dm_test_get_clock_levels_with_voltage_success - Test voltage clock query
 * @test: KUnit test context
 *
 * Verify dm_pp_get_clock_levels_by_type_with_voltage returns true and
 * copies the clock/voltage data from the DPM backend.
 */
static void dm_test_get_clock_levels_with_voltage_success(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_levels_with_voltage info = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;

	stub_dpm_ctx->get_clock_by_type_with_voltage_clks.num_levels = 1;
	stub_dpm_ctx->get_clock_by_type_with_voltage_clks.data[0].clocks_in_khz = 400000;
	stub_dpm_ctx->get_clock_by_type_with_voltage_clks.data[0].voltage_in_mv = 900;

	KUNIT_EXPECT_TRUE(test, dm_pp_get_clock_levels_by_type_with_voltage(ctx,
				DM_PP_CLOCK_TYPE_MEMORY_CLK, &info));
	KUNIT_EXPECT_EQ(test, info.num_levels, 1U);
	KUNIT_EXPECT_EQ(test, info.data[0].clocks_in_khz, 400000U);
	KUNIT_EXPECT_EQ(test, info.data[0].voltage_in_mv, 900U);
}

/**
 * dm_test_get_clock_levels_with_voltage_failure - Test voltage query error
 * @test: KUnit test context
 *
 * Verify dm_pp_get_clock_levels_by_type_with_voltage returns false on DPM error.
 */
static void dm_test_get_clock_levels_with_voltage_failure(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_levels_with_voltage info = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	stub_dpm_ctx->ret_val = -EINVAL;

	KUNIT_EXPECT_FALSE(test, dm_pp_get_clock_levels_by_type_with_voltage(ctx,
				DM_PP_CLOCK_TYPE_MEMORY_CLK, &info));
}

/**
 * dm_test_notify_wm_clock_changes_polaris - Test Polaris watermark path
 * @test: KUnit test context
 *
 * Verify dm_pp_notify_wm_clock_changes returns true for Polaris ASICs
 * when the DPM set_watermarks call succeeds.
 */
static void dm_test_notify_wm_clock_changes_polaris(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_wm_sets_with_clock_ranges wm = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	adev->asic_type = CHIP_POLARIS10;
	stub_dpm_ctx->set_watermarks_ret = 0;

	KUNIT_EXPECT_TRUE(test, dm_pp_notify_wm_clock_changes(ctx, &wm));
}

/**
 * dm_test_notify_wm_clock_changes_non_polaris - Test non-Polaris path
 * @test: KUnit test context
 *
 * Verify dm_pp_notify_wm_clock_changes returns false for non-Polaris ASICs.
 */
static void dm_test_notify_wm_clock_changes_non_polaris(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_wm_sets_with_clock_ranges wm = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	adev->asic_type = CHIP_NAVI10;

	KUNIT_EXPECT_FALSE(test, dm_pp_notify_wm_clock_changes(ctx, &wm));
}

/**
 * dm_test_apply_clock_for_voltage_success - Test successful voltage request
 * @test: KUnit test context
 *
 * Verify dm_pp_apply_clock_for_voltage_request returns true when the DPM
 * callback succeeds for a valid clock type.
 */
static void dm_test_apply_clock_for_voltage_success(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_for_voltage_req req = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	stub_dpm_ctx->display_clock_voltage_ret = 0;

	req.clk_type = DM_PP_CLOCK_TYPE_ENGINE_CLK;
	req.clocks_in_khz = 500000;

	KUNIT_EXPECT_TRUE(test, dm_pp_apply_clock_for_voltage_request(ctx, &req));
}

/**
 * dm_test_apply_clock_for_voltage_eopnotsupp - Test EOPNOTSUPP treated as success
 * @test: KUnit test context
 *
 * Verify dm_pp_apply_clock_for_voltage_request returns true when the DPM
 * callback returns -EOPNOTSUPP (not supported is non-fatal).
 */
static void dm_test_apply_clock_for_voltage_eopnotsupp(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_for_voltage_req req = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	stub_dpm_ctx->display_clock_voltage_ret = -EOPNOTSUPP;

	req.clk_type = DM_PP_CLOCK_TYPE_ENGINE_CLK;
	req.clocks_in_khz = 500000;

	KUNIT_EXPECT_TRUE(test, dm_pp_apply_clock_for_voltage_request(ctx, &req));
}

/**
 * dm_test_apply_clock_for_voltage_fail - Test DPM error returns false
 * @test: KUnit test context
 *
 * Verify dm_pp_apply_clock_for_voltage_request returns false when the DPM
 * callback fails with an error other than -EOPNOTSUPP.
 */
static void dm_test_apply_clock_for_voltage_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dm_pp_clock_for_voltage_req req = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	stub_dpm_ctx->display_clock_voltage_ret = -EIO;

	req.clk_type = DM_PP_CLOCK_TYPE_ENGINE_CLK;
	req.clocks_in_khz = 500000;

	KUNIT_EXPECT_FALSE(test, dm_pp_apply_clock_for_voltage_request(ctx, &req));
}

/* ---- Tests for pp_nv_set_display_count ---- */

/**
 * dm_test_nv_set_display_count_ok - Test successful display count set
 * @test: KUnit test context
 *
 * Verify pp_nv_set_display_count returns PP_SMU_RESULT_OK on success.
 */
static void dm_test_nv_set_display_count_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->set_active_display_count_ret = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_display_count(&pp_smu, 2),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_set_display_count_unsupported - Test EOPNOTSUPP mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_display_count returns PP_SMU_RESULT_UNSUPPORTED when
 * the DPM callback returns -EOPNOTSUPP.
 */
static void dm_test_nv_set_display_count_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->set_active_display_count_ret = -EOPNOTSUPP;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_display_count(&pp_smu, 2),
			(int)PP_SMU_RESULT_UNSUPPORTED);
}

/**
 * dm_test_nv_set_display_count_fail - Test generic error mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_display_count returns PP_SMU_RESULT_FAIL on a generic
 * DPM error.
 */
static void dm_test_nv_set_display_count_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->set_active_display_count_ret = -EIO;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_display_count(&pp_smu, 2),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_nv_set_voltage_by_freq ---- */

/**
 * dm_test_nv_set_voltage_by_freq_ok - Test successful voltage-by-freq
 * @test: KUnit test context
 *
 * Verify pp_nv_set_voltage_by_freq returns PP_SMU_RESULT_OK on success.
 */
static void dm_test_nv_set_voltage_by_freq_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_clock_voltage_ret = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_voltage_by_freq(&pp_smu, PP_SMU_NV_DISPCLK, 600),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_set_voltage_by_freq_invalid_id - Test invalid clock id
 * @test: KUnit test context
 *
 * Verify pp_nv_set_voltage_by_freq returns PP_SMU_RESULT_FAIL for an
 * unrecognized clock id without calling DPM.
 */
static void dm_test_nv_set_voltage_by_freq_invalid_id(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	KUNIT_EXPECT_EQ(test,
			(int)pp_nv_set_voltage_by_freq(&pp_smu, (enum pp_smu_nv_clock_id)0xff, 600),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_nv_set_pstate_handshake_support ---- */

/**
 * dm_test_nv_pstate_handshake_ok - Test successful pstate handshake
 * @test: KUnit test context
 *
 * Verify pp_nv_set_pstate_handshake_support returns PP_SMU_RESULT_OK
 * when the DPM callback succeeds.
 */
static void dm_test_nv_pstate_handshake_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_disable_memory_clock_switch_ret = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_pstate_handshake_support(&pp_smu, true),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_pstate_handshake_fail - Test failed pstate handshake
 * @test: KUnit test context
 *
 * Verify pp_nv_set_pstate_handshake_support returns PP_SMU_RESULT_FAIL
 * when the DPM callback returns non-zero.
 */
static void dm_test_nv_pstate_handshake_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_disable_memory_clock_switch_ret = -EIO;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_pstate_handshake_support(&pp_smu, true),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_rn_get_dpm_clock_table ---- */

/**
 * dm_test_rn_get_dpm_clock_table_ok - Test successful DPM clock table
 * @test: KUnit test context
 *
 * Verify pp_rn_get_dpm_clock_table returns PP_SMU_RESULT_OK on success.
 */
static void dm_test_rn_get_dpm_clock_table_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct dpm_clocks clock_table = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_dpm_clock_table_ret = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_rn_get_dpm_clock_table(&pp_smu, &clock_table),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_rn_get_dpm_clock_table_unsupported - Test EOPNOTSUPP mapping
 * @test: KUnit test context
 *
 * Verify pp_rn_get_dpm_clock_table returns PP_SMU_RESULT_UNSUPPORTED.
 */
static void dm_test_rn_get_dpm_clock_table_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct dpm_clocks clock_table = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_dpm_clock_table_ret = -EOPNOTSUPP;

	KUNIT_EXPECT_EQ(test, (int)pp_rn_get_dpm_clock_table(&pp_smu, &clock_table),
			(int)PP_SMU_RESULT_UNSUPPORTED);
}

/**
 * dm_test_rn_get_dpm_clock_table_fail - Test generic error mapping
 * @test: KUnit test context
 *
 * Verify pp_rn_get_dpm_clock_table returns PP_SMU_RESULT_FAIL.
 */
static void dm_test_rn_get_dpm_clock_table_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct dpm_clocks clock_table = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_dpm_clock_table_ret = -EIO;

	KUNIT_EXPECT_EQ(test, (int)pp_rn_get_dpm_clock_table(&pp_smu, &clock_table),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_rv_set_wm_ranges ---- */

/**
 * dm_test_rv_set_wm_ranges - Test Raven watermark range forwarding
 * @test: KUnit test context
 *
 * Verify pp_rv_set_wm_ranges converts watermark ranges via
 * build_wm_clock_ranges_soc15 and forwards them to DPM without crashing.
 */
static void dm_test_rv_set_wm_ranges(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct pp_smu_wm_range_sets ranges = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	ranges.num_reader_wm_sets = 1;
	ranges.reader_wm_sets[0].wm_inst = 0;
	ranges.reader_wm_sets[0].max_drain_clk_mhz = 600;
	ranges.reader_wm_sets[0].min_drain_clk_mhz = 300;

	pp_rv_set_wm_ranges(&pp_smu, &ranges);

	/* Reaching here without crash confirms coverage */
	KUNIT_SUCCEED(test);
}

/* ---- Tests for pp_rv_set_pme_wa_enable ---- */

/**
 * dm_test_rv_set_pme_wa_enable - Test Raven PME workaround enable
 * @test: KUnit test context
 *
 * Verify pp_rv_set_pme_wa_enable forwards the call to DPM without crashing.
 */
static void dm_test_rv_set_pme_wa_enable(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	pp_rv_set_pme_wa_enable(&pp_smu);

	KUNIT_SUCCEED(test);
}

/* ---- Tests for pp_rv_set_active_display_count ---- */

/**
 * dm_test_rv_set_active_display_count - Test Raven display count forwarding
 * @test: KUnit test context
 *
 * Verify pp_rv_set_active_display_count forwards the count to DPM without
 * crashing.
 */
static void dm_test_rv_set_active_display_count(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	pp_rv_set_active_display_count(&pp_smu, 2);

	KUNIT_SUCCEED(test);
}

/* ---- Tests for pp_rv_set_min_deep_sleep_dcfclk ---- */

/**
 * dm_test_rv_set_min_deep_sleep_dcfclk - Test Raven deep sleep clock
 * @test: KUnit test context
 *
 * Verify pp_rv_set_min_deep_sleep_dcfclk forwards the clock value to DPM
 * without crashing.
 */
static void dm_test_rv_set_min_deep_sleep_dcfclk(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	pp_rv_set_min_deep_sleep_dcfclk(&pp_smu, 300);

	KUNIT_SUCCEED(test);
}

/* ---- Tests for pp_rv_set_hard_min_dcefclk_by_freq ---- */

/**
 * dm_test_rv_set_hard_min_dcefclk_by_freq - Test Raven hard min DCEFCLK
 * @test: KUnit test context
 *
 * Verify pp_rv_set_hard_min_dcefclk_by_freq forwards the frequency to DPM
 * without crashing.
 */
static void dm_test_rv_set_hard_min_dcefclk_by_freq(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	pp_rv_set_hard_min_dcefclk_by_freq(&pp_smu, 600);

	KUNIT_SUCCEED(test);
}

/* ---- Tests for pp_rv_set_hard_min_fclk_by_freq ---- */

/**
 * dm_test_rv_set_hard_min_fclk_by_freq - Test Raven hard min FCLK
 * @test: KUnit test context
 *
 * Verify pp_rv_set_hard_min_fclk_by_freq forwards the frequency to DPM
 * without crashing.
 */
static void dm_test_rv_set_hard_min_fclk_by_freq(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	pp_rv_set_hard_min_fclk_by_freq(&pp_smu, 800);

	KUNIT_SUCCEED(test);
}

/* ---- Tests for pp_nv_set_wm_ranges ---- */

/**
 * dm_test_nv_set_wm_ranges - Test Navi watermark range forwarding
 * @test: KUnit test context
 *
 * Verify pp_nv_set_wm_ranges forwards ranges to DPM and unconditionally
 * returns PP_SMU_RESULT_OK.
 */
static void dm_test_nv_set_wm_ranges(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct pp_smu_wm_range_sets ranges = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;

	ranges.num_reader_wm_sets = 1;
	ranges.reader_wm_sets[0].wm_inst = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_wm_ranges(&pp_smu, &ranges),
			(int)PP_SMU_RESULT_OK);
}

/* ---- Tests for pp_nv_set_min_deep_sleep_dcfclk ---- */

/**
 * dm_test_nv_set_min_deep_sleep_dcfclk_ok - Test successful deep sleep set
 * @test: KUnit test context
 *
 * Verify pp_nv_set_min_deep_sleep_dcfclk returns PP_SMU_RESULT_OK on success.
 */
static void dm_test_nv_set_min_deep_sleep_dcfclk_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->set_min_deep_sleep_dcefclk_ret = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_min_deep_sleep_dcfclk(&pp_smu, 300),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_set_min_deep_sleep_dcfclk_unsupported - Test EOPNOTSUPP mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_min_deep_sleep_dcfclk returns PP_SMU_RESULT_UNSUPPORTED.
 */
static void dm_test_nv_set_min_deep_sleep_dcfclk_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->set_min_deep_sleep_dcefclk_ret = -EOPNOTSUPP;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_min_deep_sleep_dcfclk(&pp_smu, 300),
			(int)PP_SMU_RESULT_UNSUPPORTED);
}

/**
 * dm_test_nv_set_min_deep_sleep_dcfclk_fail - Test generic error mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_min_deep_sleep_dcfclk returns PP_SMU_RESULT_FAIL.
 */
static void dm_test_nv_set_min_deep_sleep_dcfclk_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->set_min_deep_sleep_dcefclk_ret = -EIO;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_min_deep_sleep_dcfclk(&pp_smu, 300),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_nv_set_hard_min_dcefclk_by_freq ---- */

/**
 * dm_test_nv_set_hard_min_dcefclk_ok - Test successful hard min DCEFCLK
 * @test: KUnit test context
 *
 * Verify pp_nv_set_hard_min_dcefclk_by_freq returns PP_SMU_RESULT_OK.
 */
static void dm_test_nv_set_hard_min_dcefclk_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_clock_voltage_ret = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_hard_min_dcefclk_by_freq(&pp_smu, 600),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_set_hard_min_dcefclk_unsupported - Test EOPNOTSUPP mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_hard_min_dcefclk_by_freq returns PP_SMU_RESULT_UNSUPPORTED.
 */
static void dm_test_nv_set_hard_min_dcefclk_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_clock_voltage_ret = -EOPNOTSUPP;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_hard_min_dcefclk_by_freq(&pp_smu, 600),
			(int)PP_SMU_RESULT_UNSUPPORTED);
}

/**
 * dm_test_nv_set_hard_min_dcefclk_fail - Test generic error mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_hard_min_dcefclk_by_freq returns PP_SMU_RESULT_FAIL.
 */
static void dm_test_nv_set_hard_min_dcefclk_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_clock_voltage_ret = -EIO;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_hard_min_dcefclk_by_freq(&pp_smu, 600),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_nv_set_hard_min_uclk_by_freq ---- */

/**
 * dm_test_nv_set_hard_min_uclk_ok - Test successful hard min UCLK
 * @test: KUnit test context
 *
 * Verify pp_nv_set_hard_min_uclk_by_freq returns PP_SMU_RESULT_OK.
 */
static void dm_test_nv_set_hard_min_uclk_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_clock_voltage_ret = 0;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_hard_min_uclk_by_freq(&pp_smu, 800),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_set_hard_min_uclk_unsupported - Test EOPNOTSUPP mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_hard_min_uclk_by_freq returns PP_SMU_RESULT_UNSUPPORTED.
 */
static void dm_test_nv_set_hard_min_uclk_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_clock_voltage_ret = -EOPNOTSUPP;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_hard_min_uclk_by_freq(&pp_smu, 800),
			(int)PP_SMU_RESULT_UNSUPPORTED);
}

/**
 * dm_test_nv_set_hard_min_uclk_fail - Test generic error mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_set_hard_min_uclk_by_freq returns PP_SMU_RESULT_FAIL.
 */
static void dm_test_nv_set_hard_min_uclk_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->display_clock_voltage_ret = -EIO;

	KUNIT_EXPECT_EQ(test, (int)pp_nv_set_hard_min_uclk_by_freq(&pp_smu, 800),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_nv_get_maximum_sustainable_clocks ---- */

/**
 * dm_test_nv_get_max_sustainable_clocks_ok - Test successful query
 * @test: KUnit test context
 *
 * Verify pp_nv_get_maximum_sustainable_clocks returns PP_SMU_RESULT_OK.
 */
static void dm_test_nv_get_max_sustainable_clocks_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct pp_smu_nv_clock_table max_clocks = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_max_sustainable_ret = 0;

	KUNIT_EXPECT_EQ(test,
			(int)pp_nv_get_maximum_sustainable_clocks(&pp_smu, &max_clocks),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_get_max_sustainable_clocks_unsupported - Test EOPNOTSUPP
 * @test: KUnit test context
 *
 * Verify pp_nv_get_maximum_sustainable_clocks returns PP_SMU_RESULT_UNSUPPORTED.
 */
static void dm_test_nv_get_max_sustainable_clocks_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct pp_smu_nv_clock_table max_clocks = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_max_sustainable_ret = -EOPNOTSUPP;

	KUNIT_EXPECT_EQ(test,
			(int)pp_nv_get_maximum_sustainable_clocks(&pp_smu, &max_clocks),
			(int)PP_SMU_RESULT_UNSUPPORTED);
}

/**
 * dm_test_nv_get_max_sustainable_clocks_fail - Test generic error
 * @test: KUnit test context
 *
 * Verify pp_nv_get_maximum_sustainable_clocks returns PP_SMU_RESULT_FAIL.
 */
static void dm_test_nv_get_max_sustainable_clocks_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	struct pp_smu_nv_clock_table max_clocks = {};

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_max_sustainable_ret = -EIO;

	KUNIT_EXPECT_EQ(test,
			(int)pp_nv_get_maximum_sustainable_clocks(&pp_smu, &max_clocks),
			(int)PP_SMU_RESULT_FAIL);
}

/* ---- Tests for pp_nv_get_uclk_dpm_states ---- */

/**
 * dm_test_nv_get_uclk_dpm_states_ok - Test successful DPM states query
 * @test: KUnit test context
 *
 * Verify pp_nv_get_uclk_dpm_states returns PP_SMU_RESULT_OK.
 */
static void dm_test_nv_get_uclk_dpm_states_ok(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	unsigned int clock_values[4] = {};
	unsigned int num_states = 0;

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_uclk_dpm_ret = 0;

	KUNIT_EXPECT_EQ(test,
			(int)pp_nv_get_uclk_dpm_states(&pp_smu, clock_values, &num_states),
			(int)PP_SMU_RESULT_OK);
}

/**
 * dm_test_nv_get_uclk_dpm_states_unsupported - Test EOPNOTSUPP mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_get_uclk_dpm_states returns PP_SMU_RESULT_UNSUPPORTED.
 */
static void dm_test_nv_get_uclk_dpm_states_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	unsigned int clock_values[4] = {};
	unsigned int num_states = 0;

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_uclk_dpm_ret = -EOPNOTSUPP;

	KUNIT_EXPECT_EQ(test,
			(int)pp_nv_get_uclk_dpm_states(&pp_smu, clock_values, &num_states),
			(int)PP_SMU_RESULT_UNSUPPORTED);
}

/**
 * dm_test_nv_get_uclk_dpm_states_fail - Test generic error mapping
 * @test: KUnit test context
 *
 * Verify pp_nv_get_uclk_dpm_states returns PP_SMU_RESULT_FAIL.
 */
static void dm_test_nv_get_uclk_dpm_states_fail(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct pp_smu pp_smu = {};
	unsigned int clock_values[4] = {};
	unsigned int num_states = 0;

	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	setup_stub_dpm(test, adev);
	ctx->driver_context = adev;
	pp_smu.dm = ctx;
	stub_dpm_ctx->get_uclk_dpm_ret = -EIO;

	KUNIT_EXPECT_EQ(test,
			(int)pp_nv_get_uclk_dpm_states(&pp_smu, clock_values, &num_states),
			(int)PP_SMU_RESULT_FAIL);
}

static struct kunit_case dm_pp_smu_test_cases[] = {
	/* get_default_clock_levels */
	KUNIT_CASE(dm_test_default_clock_levels_display),
	KUNIT_CASE(dm_test_default_clock_levels_engine),
	KUNIT_CASE(dm_test_default_clock_levels_memory),
	KUNIT_CASE(dm_test_default_clock_levels_unknown),
	/* dc_to_pp_clock_type */
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_display),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_engine),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_memory),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_dcefclk),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_dcfclk),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_pixelclk),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_fclk),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_phyclk),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_dppclk),
	KUNIT_CASE(dm_test_dc_to_pp_clock_type_invalid),
	/* pp_to_dc_clock_levels */
	KUNIT_CASE(dm_test_pp_to_dc_clock_levels_within_limit),
	KUNIT_CASE(dm_test_pp_to_dc_clock_levels_caps_at_max),
	/* pp_to_dc_clock_levels_with_latency */
	KUNIT_CASE(dm_test_pp_to_dc_clock_levels_latency_within_limit),
	KUNIT_CASE(dm_test_pp_to_dc_clock_levels_latency_caps_at_max),
	/* pp_to_dc_clock_levels_with_voltage */
	KUNIT_CASE(dm_test_pp_to_dc_clock_levels_voltage_within_limit),
	KUNIT_CASE(dm_test_pp_to_dc_clock_levels_voltage_caps_at_max),
	/* dm_pp_get_funcs */
	KUNIT_CASE(dm_test_get_funcs_rv),
	KUNIT_CASE(dm_test_get_funcs_rv_101),
	KUNIT_CASE(dm_test_get_funcs_nv),
	KUNIT_CASE(dm_test_get_funcs_rn),
	KUNIT_CASE(dm_test_get_funcs_unsupported),
	/* amdgpu_device-backed entry points */
	KUNIT_CASE(dm_test_apply_display_requirements_dpm_disabled),
	KUNIT_CASE(dm_test_apply_clock_for_voltage_invalid_type),
	/* build_pm_display_cfg */
	KUNIT_CASE(dm_test_build_pm_display_cfg_scalar_fields),
	KUNIT_CASE(dm_test_build_pm_display_cfg_per_display),
	/* build_wm_clock_ranges_soc15 */
	KUNIT_CASE(dm_test_build_wm_clock_ranges_dmif),
	KUNIT_CASE(dm_test_build_wm_clock_ranges_mcif),
	/* cap_clock_levels_to_validation */
	KUNIT_CASE(dm_test_cap_clock_levels_engine_caps),
	KUNIT_CASE(dm_test_cap_clock_levels_engine_first_exceeds),
	KUNIT_CASE(dm_test_cap_clock_levels_memory_caps),
	KUNIT_CASE(dm_test_cap_clock_levels_within_limit),
	KUNIT_CASE(dm_test_cap_clock_levels_other_type),
	/* pp_smu_nv_clock_id_to_pp */
	KUNIT_CASE(dm_test_nv_clock_id_dispclk),
	KUNIT_CASE(dm_test_nv_clock_id_phyclk),
	KUNIT_CASE(dm_test_nv_clock_id_pixelclk),
	KUNIT_CASE(dm_test_nv_clock_id_invalid),
	/* dm_pp_apply_display_requirements (DPM enabled) */
	KUNIT_CASE(dm_test_apply_display_requirements_dpm_enabled),
	/* dm_pp_get_clock_levels_by_type */
	KUNIT_CASE(dm_test_get_clock_levels_by_type_dpm_error),
	KUNIT_CASE(dm_test_get_clock_levels_by_type_success),
	KUNIT_CASE(dm_test_get_clock_levels_by_type_validation_fallback),
	/* dm_pp_get_clock_levels_by_type_with_latency */
	KUNIT_CASE(dm_test_get_clock_levels_with_latency_success),
	KUNIT_CASE(dm_test_get_clock_levels_with_latency_failure),
	/* dm_pp_get_clock_levels_by_type_with_voltage */
	KUNIT_CASE(dm_test_get_clock_levels_with_voltage_success),
	KUNIT_CASE(dm_test_get_clock_levels_with_voltage_failure),
	/* dm_pp_notify_wm_clock_changes */
	KUNIT_CASE(dm_test_notify_wm_clock_changes_polaris),
	KUNIT_CASE(dm_test_notify_wm_clock_changes_non_polaris),
	/* dm_pp_apply_clock_for_voltage_request (with DPM) */
	KUNIT_CASE(dm_test_apply_clock_for_voltage_success),
	KUNIT_CASE(dm_test_apply_clock_for_voltage_eopnotsupp),
	KUNIT_CASE(dm_test_apply_clock_for_voltage_fail),
	/* pp_nv_set_display_count */
	KUNIT_CASE(dm_test_nv_set_display_count_ok),
	KUNIT_CASE(dm_test_nv_set_display_count_unsupported),
	KUNIT_CASE(dm_test_nv_set_display_count_fail),
	/* pp_nv_set_voltage_by_freq */
	KUNIT_CASE(dm_test_nv_set_voltage_by_freq_ok),
	KUNIT_CASE(dm_test_nv_set_voltage_by_freq_invalid_id),
	/* pp_nv_set_pstate_handshake_support */
	KUNIT_CASE(dm_test_nv_pstate_handshake_ok),
	KUNIT_CASE(dm_test_nv_pstate_handshake_fail),
	/* pp_rn_get_dpm_clock_table */
	KUNIT_CASE(dm_test_rn_get_dpm_clock_table_ok),
	KUNIT_CASE(dm_test_rn_get_dpm_clock_table_unsupported),
	KUNIT_CASE(dm_test_rn_get_dpm_clock_table_fail),
	/* pp_rv_set_wm_ranges */
	KUNIT_CASE(dm_test_rv_set_wm_ranges),
	/* pp_rv_set_pme_wa_enable */
	KUNIT_CASE(dm_test_rv_set_pme_wa_enable),
	/* pp_rv_set_active_display_count */
	KUNIT_CASE(dm_test_rv_set_active_display_count),
	/* pp_rv_set_min_deep_sleep_dcfclk */
	KUNIT_CASE(dm_test_rv_set_min_deep_sleep_dcfclk),
	/* pp_rv_set_hard_min_dcefclk_by_freq */
	KUNIT_CASE(dm_test_rv_set_hard_min_dcefclk_by_freq),
	/* pp_rv_set_hard_min_fclk_by_freq */
	KUNIT_CASE(dm_test_rv_set_hard_min_fclk_by_freq),
	/* pp_nv_set_wm_ranges */
	KUNIT_CASE(dm_test_nv_set_wm_ranges),
	/* pp_nv_set_min_deep_sleep_dcfclk */
	KUNIT_CASE(dm_test_nv_set_min_deep_sleep_dcfclk_ok),
	KUNIT_CASE(dm_test_nv_set_min_deep_sleep_dcfclk_unsupported),
	KUNIT_CASE(dm_test_nv_set_min_deep_sleep_dcfclk_fail),
	/* pp_nv_set_hard_min_dcefclk_by_freq */
	KUNIT_CASE(dm_test_nv_set_hard_min_dcefclk_ok),
	KUNIT_CASE(dm_test_nv_set_hard_min_dcefclk_unsupported),
	KUNIT_CASE(dm_test_nv_set_hard_min_dcefclk_fail),
	/* pp_nv_set_hard_min_uclk_by_freq */
	KUNIT_CASE(dm_test_nv_set_hard_min_uclk_ok),
	KUNIT_CASE(dm_test_nv_set_hard_min_uclk_unsupported),
	KUNIT_CASE(dm_test_nv_set_hard_min_uclk_fail),
	/* pp_nv_get_maximum_sustainable_clocks */
	KUNIT_CASE(dm_test_nv_get_max_sustainable_clocks_ok),
	KUNIT_CASE(dm_test_nv_get_max_sustainable_clocks_unsupported),
	KUNIT_CASE(dm_test_nv_get_max_sustainable_clocks_fail),
	/* pp_nv_get_uclk_dpm_states */
	KUNIT_CASE(dm_test_nv_get_uclk_dpm_states_ok),
	KUNIT_CASE(dm_test_nv_get_uclk_dpm_states_unsupported),
	KUNIT_CASE(dm_test_nv_get_uclk_dpm_states_fail),
	{}
};

static struct kunit_suite dm_pp_smu_test_suite = {
	.name = "amdgpu_dm_pp_smu",
	.test_cases = dm_pp_smu_test_cases,
};

kunit_test_suite(dm_pp_smu_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_pp_smu");
