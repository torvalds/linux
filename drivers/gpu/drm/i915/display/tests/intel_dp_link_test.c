// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <kunit/test.h>

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/log2.h>
#include <linux/prandom.h>
#include <linux/random.h>

#include <drm/display/drm_dp_helper.h>

#include <drm/intel/display_member.h>

#include "intel_connector.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_dp_link_caps.h"
#include "intel_dp_link_training.h"

#define LINK_TEST_NUM_LANE_CONFIGS(__max_lane_count) \
	(ilog2(__max_lane_count) + 1)

#define LINK_TEST_NUM_CONFIGS(__num_rates, __max_lane_count) \
	((__num_rates) * LINK_TEST_NUM_LANE_CONFIGS(__max_lane_count))

#define LINK_TEST_MAX_LANE_COUNT		((u32)4)
#define LINK_TEST_MAX_CONFIGS			LINK_TEST_NUM_CONFIGS(DP_MAX_SUPPORTED_RATES, \
								      LINK_TEST_MAX_LANE_COUNT)

#define LINK_TEST_NUM_RANDOM_ITERATIONS		50

struct test_ctx {
	struct {
		struct intel_display display;
		struct device device;
		struct __intel_generic_device generic_device;

		struct intel_connector connector;
		struct intel_digital_port dig_port;

		struct intel_crtc_state crtc_state;
	} dev;

	const struct intel_dp_link_caps_test_ops *link_caps_ops;
	const struct intel_dp_link_training_test_ops *link_training_ops;

	struct rnd_state rnd;
};

struct link_rate_set {
	const int *entries;
	int size;
};

struct link_config_set {
	struct intel_dp_link_config entries[LINK_TEST_MAX_CONFIGS];
	int size;
};

struct test_config_table {
	struct kunit *test;

	struct link_rate_set rates;
	int max_lane_count;
	struct link_config_set disabled_configs;
};

static const int standard_dp_link_rates[] = {
	162000, 270000, 540000, 810000, 1000000, 1350000, 2000000
};

#define LINK_TEST_NUM_STANDARD_RATES (ARRAY_SIZE(standard_dp_link_rates))

#define INIT_STANDARD_TABLE(__test, __num_rates, __max_lane_count) { \
	.test = (__test), \
	.rates = { \
		.entries = standard_dp_link_rates, \
		.size = (__num_rates), \
	}, \
	.max_lane_count = (__max_lane_count), \
}

static const struct link_config_set standard_dp_link_configs[] = {
	[INTEL_DP_LINK_CAPS_ORDER_KEY_BW] = {                        /* MBps    PBN    */
		.entries = {
			{ .rate =  162000, .lane_count = 1 }, /*  162.0    3.00 */
			{ .rate =  270000, .lane_count = 1 }, /*  270.0    5.00 */
			{ .rate =  162000, .lane_count = 2 }, /*  324.0    6.00 */
			{ .rate =  270000, .lane_count = 2 }, /*  540.0   10.00 */
			{ .rate =  540000, .lane_count = 1 }, /*  540.0   10.00 */
			{ .rate =  162000, .lane_count = 4 }, /*  648.0   12.00 */
			{ .rate =  810000, .lane_count = 1 }, /*  810.0   15.00 */
			{ .rate =  270000, .lane_count = 4 }, /* 1080.0   20.00 */
			{ .rate =  540000, .lane_count = 2 }, /* 1080.0   20.00 */
			{ .rate = 1000000, .lane_count = 1 }, /* 1208.9   22.39 */
			{ .rate =  810000, .lane_count = 2 }, /* 1620.0   30.00 */
			{ .rate = 1350000, .lane_count = 1 }, /* 1632.0   30.22 */
			{ .rate =  540000, .lane_count = 4 }, /* 2160.0   40.00 */
			{ .rate = 1000000, .lane_count = 2 }, /* 2417.8   44.77 */
			{ .rate = 2000000, .lane_count = 1 }, /* 2417.8   44.77 */
			{ .rate =  810000, .lane_count = 4 }, /* 3240.0   60.00 */
			{ .rate = 1350000, .lane_count = 2 }, /* 3264.0   60.44 */
			{ .rate = 1000000, .lane_count = 4 }, /* 4835.6   89.55 */
			{ .rate = 2000000, .lane_count = 2 }, /* 4835.6   89.55 */
			{ .rate = 1350000, .lane_count = 4 }, /* 6527.9  120.89 */
			{ .rate = 2000000, .lane_count = 4 }, /* 9671.1  179.09 */
		},
		.size = LINK_TEST_NUM_CONFIGS(ARRAY_SIZE(standard_dp_link_rates),
					      LINK_TEST_MAX_LANE_COUNT),
	},
	[INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE] = {
		.entries = {
			{ .rate = 162000,  .lane_count = 1 },
			{ .rate = 162000,  .lane_count = 2 },
			{ .rate = 162000,  .lane_count = 4 },

			{ .rate = 270000,  .lane_count = 1 },
			{ .rate = 270000,  .lane_count = 2 },
			{ .rate = 270000,  .lane_count = 4 },

			{ .rate = 540000,  .lane_count = 1 },
			{ .rate = 540000,  .lane_count = 2 },
			{ .rate = 540000,  .lane_count = 4 },

			{ .rate = 810000,  .lane_count = 1 },
			{ .rate = 810000,  .lane_count = 2 },
			{ .rate = 810000,  .lane_count = 4 },

			{ .rate = 1000000, .lane_count = 1 },
			{ .rate = 1000000, .lane_count = 2 },
			{ .rate = 1000000, .lane_count = 4 },

			{ .rate = 1350000, .lane_count = 1 },
			{ .rate = 1350000, .lane_count = 2 },
			{ .rate = 1350000, .lane_count = 4 },

			{ .rate = 2000000, .lane_count = 1 },
			{ .rate = 2000000, .lane_count = 2 },
			{ .rate = 2000000, .lane_count = 4 },
		},
		.size = LINK_TEST_NUM_CONFIGS(ARRAY_SIZE(standard_dp_link_rates),
					      LINK_TEST_MAX_LANE_COUNT),
	},
	[INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE] = {
		.entries = {
			{ .rate = 162000,  .lane_count = 1 },
			{ .rate = 270000,  .lane_count = 1 },
			{ .rate = 540000,  .lane_count = 1 },
			{ .rate = 810000,  .lane_count = 1 },
			{ .rate = 1000000, .lane_count = 1 },
			{ .rate = 1350000, .lane_count = 1 },
			{ .rate = 2000000, .lane_count = 1 },

			{ .rate = 162000,  .lane_count = 2 },
			{ .rate = 270000,  .lane_count = 2 },
			{ .rate = 540000,  .lane_count = 2 },
			{ .rate = 810000,  .lane_count = 2 },
			{ .rate = 1000000, .lane_count = 2 },
			{ .rate = 1350000, .lane_count = 2 },
			{ .rate = 2000000, .lane_count = 2 },

			{ .rate = 162000,  .lane_count = 4 },
			{ .rate = 270000,  .lane_count = 4 },
			{ .rate = 540000,  .lane_count = 4 },
			{ .rate = 810000,  .lane_count = 4 },
			{ .rate = 1000000, .lane_count = 4 },
			{ .rate = 1350000, .lane_count = 4 },
			{ .rate = 2000000, .lane_count = 4 },
		},
		.size = LINK_TEST_NUM_CONFIGS(ARRAY_SIZE(standard_dp_link_rates),
					      LINK_TEST_MAX_LANE_COUNT),
	},
};

static int lookup_rate(const struct link_rate_set *rate_set, int rate)
{
	int i;

	for (i = 0; i < rate_set->size; i++)
		if (rate_set->entries[i] == rate)
			return i;

	return -1;
}

static bool has_rate(const struct link_rate_set *rate_set, int rate)
{
	return lookup_rate(rate_set, rate) >= 0;
}

static bool link_configs_match(const struct intel_dp_link_config *a,
			       const struct intel_dp_link_config *b)
{
	return a->rate == b->rate && a->lane_count == b->lane_count;
}

static int lookup_config(const struct link_config_set *config_set,
			 const struct intel_dp_link_config *config)
{
	int i;

	for (i = 0; i < config_set->size; i++)
		if (link_configs_match(&config_set->entries[i], config))
			return i;

	return -1;
}

static bool has_config(const struct link_config_set *config_set,
		       const struct intel_dp_link_config *config)
{
	return lookup_config(config_set, config) >= 0;
}

static void add_config(struct kunit *test,
		       struct link_config_set *config_set,
		       const struct intel_dp_link_config *config)
{
	KUNIT_ASSERT_LT(test, config_set->size, ARRAY_SIZE(config_set->entries));

	config_set->entries[config_set->size] = *config;
	config_set->size++;
}

static const struct intel_dp_link_caps_order config_orders[] = {
	{
		.key = INTEL_DP_LINK_CAPS_ORDER_KEY_BW,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_ASC,
	}, {
		.key = INTEL_DP_LINK_CAPS_ORDER_KEY_BW,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_DESC,
	}, {
		.key = INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_ASC,
	}, {
		.key = INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_DESC,
	}, {
		.key = INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_ASC,
	}, {
		.key = INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_DESC,
	}
};

static const struct link_config_set *
link_caps_config_order_key_to_set(struct kunit *test, enum intel_dp_link_caps_order_key key)
{
	return &standard_dp_link_configs[key];
}

/*
 * TEST: Baseline with fixed reference table
 * -----------------------------------------
 * Verify the link_caps config iterator using fixed standard DP config tables.
 */
static void baseline_test_for_order(struct kunit *test,
				    struct intel_dp_link_caps *link_caps,
				    struct intel_dp_link_caps_order config_order)
{
	struct test_ctx *ctx = test->priv;
	const struct link_config_set *config_set =
		link_caps_config_order_key_to_set(test, config_order.key);
	const struct intel_dp_link_caps_test_ops *ops = ctx->link_caps_ops;
	struct intel_dp_link_config iter_config;
	struct intel_dp_link_caps_iter iter;
	int pos = 0;

	ops->iter_start(&iter, link_caps, config_order, INTEL_DP_LINK_CAPS_FILTER_ALL);
	for_each_dp_link_config(&iter, &iter_config) {
		int idx = pos;

		if (config_order.dir == INTEL_DP_LINK_CAPS_ORDER_DIR_DESC)
			idx = config_set->size - idx - 1;

		KUNIT_EXPECT_TRUE(test, link_configs_match(&iter_config,
							   &config_set->entries[idx]));

		pos++;
	}
	ops->iter_end(&iter);
}

static void intel_dp_link_caps_test_baseline(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	const struct intel_dp_link_caps_test_ops *ops =
		ctx->link_caps_ops;
	int i;

	ops->update(link_caps,
		    standard_dp_link_rates, LINK_TEST_NUM_STANDARD_RATES,
		    LINK_TEST_MAX_LANE_COUNT,
		    true);

	for (i = 0; i < ARRAY_SIZE(config_orders); i++)
		baseline_test_for_order(test, link_caps, config_orders[i]);
}

static int get_num_configs(int num_rates, int max_lane_count)
{
	return num_rates * LINK_TEST_NUM_LANE_CONFIGS(max_lane_count);
}

static int rand_in_range(struct test_ctx *ctx, int min, int max)
{
	return min + (prandom_u32_state(&ctx->rnd) % (max - min + 1));
}

/*
 * TEST: Update reset
 * ------------------
 * Verify that resetting link_caps with the DP standard rates/lane
 * counts updates the configuration table accordingly for all
 * combinations.
 */
static void verify_bw_asc_config_order(struct kunit *test,
				       const struct intel_dp_link_config *last_config,
				       const struct intel_dp_link_config *config)
{
	int config_bw = drm_dp_max_dprx_data_rate(config->rate,
						  config->lane_count);
	int last_config_bw = drm_dp_max_dprx_data_rate(last_config->rate,
						       last_config->lane_count);

	KUNIT_EXPECT_GE(test, config_bw, last_config_bw);
	if (config_bw == last_config_bw)
		KUNIT_EXPECT_GT(test, config->rate, last_config->rate);
}

static void verify_bw_desc_config_order(struct kunit *test,
					const struct intel_dp_link_config *last_config,
					const struct intel_dp_link_config *config)
{
	int config_bw = drm_dp_max_dprx_data_rate(config->rate,
						  config->lane_count);
	int last_config_bw = drm_dp_max_dprx_data_rate(last_config->rate,
						       last_config->lane_count);

	KUNIT_EXPECT_LE(test, config_bw, last_config_bw);
	if (config_bw == last_config_bw)
		KUNIT_EXPECT_LT(test, config->rate, last_config->rate);
}

static void verify_rate_lane_asc_config_order(struct kunit *test,
					      const struct intel_dp_link_config *last_config,
					      const struct intel_dp_link_config *config)
{
	KUNIT_EXPECT_GE(test, config->rate, last_config->rate);
	if (config->rate == last_config->rate)
		KUNIT_EXPECT_GT(test, config->lane_count, last_config->lane_count);
}

static void verify_rate_lane_desc_config_order(struct kunit *test,
					       const struct intel_dp_link_config *last_config,
					       const struct intel_dp_link_config *config)
{
	KUNIT_EXPECT_LE(test, config->rate, last_config->rate);
	if (config->rate == last_config->rate)
		KUNIT_EXPECT_LT(test, config->lane_count, last_config->lane_count);
}

static void verify_lane_rate_asc_config_order(struct kunit *test,
					      const struct intel_dp_link_config *last_config,
					      const struct intel_dp_link_config *config)
{
	KUNIT_EXPECT_GE(test, config->lane_count, last_config->lane_count);
	if (config->lane_count == last_config->lane_count)
		KUNIT_EXPECT_GT(test, config->rate, last_config->rate);
}

static void verify_lane_rate_desc_config_order(struct kunit *test,
					       const struct intel_dp_link_config *last_config,
					       const struct intel_dp_link_config *config)
{
	KUNIT_EXPECT_LE(test, config->lane_count, last_config->lane_count);
	if (config->lane_count == last_config->lane_count)
		KUNIT_EXPECT_LT(test, config->rate, last_config->rate);
}

static void verify_config_order(struct kunit *test,
				struct intel_dp_link_caps_order config_order,
				const struct intel_dp_link_config *last_config,
				const struct intel_dp_link_config *config)
{
	switch (config_order.key) {
	case INTEL_DP_LINK_CAPS_ORDER_KEY_BW:
		if (config_order.dir == INTEL_DP_LINK_CAPS_ORDER_DIR_ASC)
			verify_bw_asc_config_order(test, last_config, config);
		else
			verify_bw_desc_config_order(test, last_config, config);
		break;
	case INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE:
		if (config_order.dir == INTEL_DP_LINK_CAPS_ORDER_DIR_ASC)
			verify_rate_lane_asc_config_order(test, last_config, config);
		else
			verify_rate_lane_desc_config_order(test, last_config, config);
		break;
	case INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE:
		if (config_order.dir == INTEL_DP_LINK_CAPS_ORDER_DIR_ASC)
			verify_lane_rate_asc_config_order(test, last_config, config);
		else
			verify_lane_rate_desc_config_order(test, last_config, config);
		break;
	default:
		KUNIT_FAIL_AND_ABORT(test, "Missing order key: %d", config_order.key);
	}
}

static int expected_num_configs(const struct test_config_table *expected_table,
				const struct intel_dp_link_config *max_limits)
{
	int num_configs = 0;
	int lane_count;
	int rate_idx;

	for (rate_idx = 0; rate_idx < expected_table->rates.size; rate_idx++) {
		for (lane_count = 1; lane_count <= expected_table->max_lane_count; lane_count <<= 1) {
			struct intel_dp_link_config config = {
				.rate = expected_table->rates.entries[rate_idx],
				.lane_count = lane_count,
			};

			if (config.rate > max_limits->rate ||
			    config.lane_count > max_limits->lane_count)
				continue;

			if (has_config(&expected_table->disabled_configs, &config))
				continue;

			num_configs++;
		}
	}

	return num_configs;
}

static void
verify_link_caps_for_order(const struct test_config_table *expected_table,
			   struct intel_dp_link_caps *link_caps,
			   struct intel_dp_link_caps_order config_order,
			   const struct intel_dp_link_config *max_limits)
{
	struct kunit *test = expected_table->test;
	struct test_ctx *ctx = test->priv;
	const struct intel_dp_link_caps_test_ops *ops =
		ctx->link_caps_ops;
	struct intel_dp_link_config expected_max_bw_config = {};
	struct intel_dp_link_config actual_max_bw_config;
	struct intel_dp_link_config last_config = {};
	struct intel_dp_link_config old_max_limits;
	struct intel_dp_link_config iter_config;
	struct intel_dp_link_caps_iter iter;
	int num_actual_configs = 0;
	int max_bw = 0;

	ops->get_max_limits(link_caps, &old_max_limits);
	ops->set_max_limits(link_caps, max_limits);

	ops->iter_start(&iter, link_caps, config_order, INTEL_DP_LINK_CAPS_FILTER_ALL);
	for_each_dp_link_config(&iter, &iter_config) {
		int bw;

		KUNIT_EXPECT_LE(test, iter_config.rate, max_limits->rate);
		KUNIT_EXPECT_LE(test, iter_config.lane_count, max_limits->lane_count);

		num_actual_configs++;

		/*
		 * Verify the config's rate/lane-count values and its ordering relative
		 * to the previous config.
		 */
		if (last_config.rate)
			verify_config_order(test, config_order, &last_config, &iter_config);
		last_config = iter_config;

		KUNIT_EXPECT_TRUE(test, has_rate(&expected_table->rates,
						 iter_config.rate));
		KUNIT_EXPECT_LE(test, iter_config.lane_count,
				      expected_table->max_lane_count);
		KUNIT_EXPECT_TRUE(test, is_power_of_2(iter_config.lane_count));

		/* Verify the config's disabled state */
		KUNIT_EXPECT_FALSE(test, has_config(&expected_table->disabled_configs,
						    &iter_config));

		/*
		 * Update the max limits for allowed configs, verified at the
		 * end for the whole config table.
		 */

		bw = drm_dp_max_dprx_data_rate(iter_config.rate, iter_config.lane_count);
		if (bw > max_bw ||
		    (bw == max_bw && iter_config.rate > expected_max_bw_config.rate)) {
			max_bw = bw;
			expected_max_bw_config = iter_config;
		}
	}
	ops->iter_end(&iter);

	KUNIT_EXPECT_EQ(test, num_actual_configs, expected_num_configs(expected_table, max_limits));

	ops->get_max_bw_config(link_caps, &actual_max_bw_config);
	KUNIT_EXPECT_TRUE(test, link_configs_match(&expected_max_bw_config,
						   &actual_max_bw_config));

	KUNIT_ASSERT_TRUE(test, ops->set_max_limits(link_caps, &old_max_limits));
}

static bool max_limits_valid(const struct test_config_table *expected_table,
			     const struct intel_dp_link_config *max_limits)
{
	int lane_count;
	int rate_idx;

	for (rate_idx = 0; rate_idx < expected_table->rates.size; rate_idx++) {
		for (lane_count = 1; lane_count <= expected_table->max_lane_count; lane_count <<= 1) {
			struct intel_dp_link_config config = {
				.rate = expected_table->rates.entries[rate_idx],
				.lane_count = lane_count,
			};

			if (has_config(&expected_table->disabled_configs, &config))
				continue;

			if (config.rate <= max_limits->rate &&
			    config.lane_count <= max_limits->lane_count)
				return true;
		}
	}

	return false;
}

static void get_max_limits(const struct test_config_table *expected_table,
			   struct intel_dp_link_config *max_limits)
{
	int lane_count;
	int rate_idx;

	max_limits->rate = 0;
	max_limits->lane_count = 0;

	for (rate_idx = 0; rate_idx < expected_table->rates.size; rate_idx++) {
		for (lane_count = 1; lane_count <= expected_table->max_lane_count; lane_count <<= 1) {
			struct intel_dp_link_config config = {
				.rate = expected_table->rates.entries[rate_idx],
				.lane_count = lane_count,
			};

			if (has_config(&expected_table->disabled_configs, &config))
				continue;

			max_limits->rate = max(max_limits->rate, config.rate);
			max_limits->lane_count = max(max_limits->lane_count, config.lane_count);
		}
	}
}

static void verify_link_caps(const struct test_config_table *expected_table,
			     struct intel_dp_link_caps *link_caps)
{
	struct kunit *test = expected_table->test;
	struct test_ctx *ctx = test->priv;
	const struct intel_dp_link_caps_test_ops *ops = ctx->link_caps_ops;
	struct intel_dp_link_config max_limits;
	int i;

	get_max_limits(expected_table, &max_limits);

	for (i = 0; i < ARRAY_SIZE(config_orders); i++) {
		int lane_count;
		int rate_idx;

		verify_link_caps_for_order(expected_table, link_caps, config_orders[i], &max_limits);
		/*
		 * Verify iteration after setting the max limits to each
		 * configurations.
		 */
		for (rate_idx = 0; rate_idx < expected_table->rates.size; rate_idx++) {
			for (lane_count = 1; lane_count <= expected_table->max_lane_count; lane_count <<= 1) {
				struct intel_dp_link_config config = {
					.rate = expected_table->rates.entries[rate_idx],
					.lane_count = lane_count,
				};

				if (!max_limits_valid(expected_table, &config)) {
					/* Verify that invalid max limits are rejected. */
					KUNIT_EXPECT_FALSE(test, ops->set_max_limits(link_caps, &config));

					continue;
				}

				verify_link_caps_for_order(expected_table, link_caps, config_orders[i],
							   &config);
			}
		}
	}
}

static void update_link_caps_and_verify(struct test_config_table *expected_table,
					struct intel_dp_link_caps *link_caps,
					bool reset)
{
	struct kunit *test = expected_table->test;
	struct test_ctx *ctx = test->priv;
	const struct intel_dp_link_caps_test_ops *ops =
		ctx->link_caps_ops;
	bool link_params_changed;

	link_params_changed = ops->update(link_caps,
					  expected_table->rates.entries,
					  expected_table->rates.size,
					  expected_table->max_lane_count,
					  reset);
	KUNIT_EXPECT_TRUE(test, !reset || link_params_changed);

	/*
	 * ops->update() re-enables all configurations when called with
	 * reset=true, or changed link parameters.
	 */
	if (link_params_changed)
		expected_table->disabled_configs.size = 0;

	verify_link_caps(expected_table, link_caps);
}

static void intel_dp_link_caps_test_update_reset(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	int max_lane_count;
	int num_rates;

	for (max_lane_count = 1;
	     max_lane_count <= LINK_TEST_MAX_LANE_COUNT;
	     max_lane_count <<= 1) {
		for (num_rates = 1;
		     num_rates <= LINK_TEST_NUM_STANDARD_RATES;
		     num_rates++) {
			struct test_config_table expected_table =
				INIT_STANDARD_TABLE(test, num_rates,
						    max_lane_count);

			update_link_caps_and_verify(&expected_table, link_caps, true);
		}
	}
}

/*
 * TEST: Update shrink and expand
 * ------------------------------
 * Verify that removing or adding supported rates/lane counts updates
 * the configuration table accordingly.
 */
static void disable_configs_and_verify(struct kunit *test,
				       struct intel_dp_link_caps *link_caps,
				       struct test_config_table *expected_table,
				       const struct link_config_set *config_set)
{
	struct test_ctx *ctx = test->priv;
	const struct intel_dp_link_caps_test_ops *ops =
		ctx->link_caps_ops;
	int i;

	for (i = 0; i < config_set->size; i++) {
		KUNIT_ASSERT_FALSE(test, has_config(&expected_table->disabled_configs,
						    &config_set->entries[i]));
		add_config(test, &expected_table->disabled_configs, &config_set->entries[i]);

		KUNIT_ASSERT_TRUE(test, ops->disable_config(link_caps, &config_set->entries[i]));

		verify_link_caps(expected_table, link_caps);
	}
}

static void disable_configs_for_shrink_and_verify(struct test_config_table *expected_table,
						  struct intel_dp_link_caps *link_caps)
{
	struct kunit *test = expected_table->test;
	struct link_config_set config_set = {};
	struct intel_dp_link_config max_config;

	/*
	 * When configs shrink disable the config with the
	 * second-highest rate, lane params, so the disabled config
	 * stays around after the configs got shrunk.
	 */
	KUNIT_ASSERT_GE(test, expected_table->rates.size, 2);
	KUNIT_ASSERT_GE(test, expected_table->max_lane_count, 2);

	max_config.rate = expected_table->rates.entries[expected_table->rates.size - 2];
	max_config.lane_count = expected_table->max_lane_count >> 1;

	add_config(test, &config_set, &max_config);
	disable_configs_and_verify(test, link_caps, expected_table,
				   &config_set);
}

static void disable_configs_for_expand_and_verify(struct test_config_table *expected_table,
						  struct intel_dp_link_caps *link_caps)
{
	struct kunit *test = expected_table->test;
	struct link_config_set config_set = {};
	struct intel_dp_link_config max_config;

	KUNIT_ASSERT_GE(test, expected_table->rates.size, 1);

	max_config.rate = expected_table->rates.entries[expected_table->rates.size - 1];
	max_config.lane_count = expected_table->max_lane_count;

	add_config(test, &config_set, &max_config);
	disable_configs_and_verify(test, link_caps, expected_table,
				   &config_set);
}

static void get_nth_rate_lane_config(const struct test_config_table *expected_table, int n,
				     struct intel_dp_link_config *config)
{
	int num_lane_configs = LINK_TEST_NUM_LANE_CONFIGS(expected_table->max_lane_count);
	int rate_idx = n / num_lane_configs;
	int lane_count_exp = n % num_lane_configs;

	config->rate = expected_table->rates.entries[rate_idx];
	config->lane_count = 1 << lane_count_exp;
}

static void test_update_rates_shrink(struct kunit *test, bool disable_configs)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, LINK_TEST_NUM_STANDARD_RATES,
					  LINK_TEST_MAX_LANE_COUNT);

	update_link_caps_and_verify(&expected_table, link_caps, true);

	while (expected_table.rates.size > 1) {
		if (disable_configs)
			disable_configs_for_shrink_and_verify(&expected_table, link_caps);

		expected_table.rates.size--;

		update_link_caps_and_verify(&expected_table, link_caps, false);
	}
}

static void intel_dp_link_caps_test_update_rates_shrink(struct kunit *test)
{
	test_update_rates_shrink(test, false);
}

static void intel_dp_link_caps_test_update_rates_shrink_disable(struct kunit *test)
{
	test_update_rates_shrink(test, true);
}

static void test_update_rates_expand(struct kunit *test, bool disable_configs)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, 1, LINK_TEST_MAX_LANE_COUNT);

	update_link_caps_and_verify(&expected_table, link_caps, true);

	while (expected_table.rates.size < LINK_TEST_NUM_STANDARD_RATES) {
		if (disable_configs)
			disable_configs_for_expand_and_verify(&expected_table, link_caps);

		expected_table.rates.size++;

		update_link_caps_and_verify(&expected_table, link_caps, false);
	}
}

static void intel_dp_link_caps_test_update_rates_expand(struct kunit *test)
{
	test_update_rates_expand(test, false);
}

static void intel_dp_link_caps_test_update_rates_expand_disable(struct kunit *test)
{
	test_update_rates_expand(test, true);
}

static void test_update_lanes_shrink(struct kunit *test, bool disable_configs)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, LINK_TEST_NUM_STANDARD_RATES,
					  LINK_TEST_MAX_LANE_COUNT);

	update_link_caps_and_verify(&expected_table, link_caps, true);

	while (expected_table.max_lane_count > 1) {
		if (disable_configs)
			disable_configs_for_shrink_and_verify(&expected_table, link_caps);

		expected_table.max_lane_count >>= 1;

		update_link_caps_and_verify(&expected_table, link_caps, false);
	}
}

static void intel_dp_link_caps_test_update_lanes_shrink(struct kunit *test)
{
	test_update_lanes_shrink(test, false);
}

static void intel_dp_link_caps_test_update_lanes_shrink_disable(struct kunit *test)
{
	test_update_lanes_shrink(test, true);
}

static void test_update_lanes_expand(struct kunit *test, bool disable_configs)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, LINK_TEST_NUM_STANDARD_RATES, 1);

	update_link_caps_and_verify(&expected_table, link_caps, true);

	while (expected_table.max_lane_count < LINK_TEST_MAX_LANE_COUNT) {
		if (disable_configs)
			disable_configs_for_expand_and_verify(&expected_table, link_caps);

		expected_table.max_lane_count <<= 1;

		update_link_caps_and_verify(&expected_table, link_caps, false);
	}
}

static void intel_dp_link_caps_test_update_lanes_expand(struct kunit *test)
{
	test_update_lanes_expand(test, false);
}

static void intel_dp_link_caps_test_update_lanes_expand_disable(struct kunit *test)
{
	test_update_lanes_expand(test, true);
}

static void disable_random_configs_and_verify(struct test_config_table *expected_table,
					      struct intel_dp_link_caps *link_caps)
{
	struct kunit *test = expected_table->test;
	struct test_ctx *ctx = test->priv;
	struct link_config_set config_set = {};
	u32 disabled_config_mask;
	int num_configs;
	int i;

	num_configs = get_num_configs(expected_table->rates.size,
				      expected_table->max_lane_count);
	disabled_config_mask = prandom_u32_state(&ctx->rnd) &
			       GENMASK_U32(num_configs - 1, 0);

	for (i = 0; i < num_configs; i++) {
		struct intel_dp_link_config config;

		/* At least one config must remain enabled. */
		if (expected_table->disabled_configs.size +
		    config_set.size + 1 >= num_configs)
			break;

		if (!(BIT(i) & disabled_config_mask))
			continue;

		get_nth_rate_lane_config(expected_table, i, &config);
		/* Don't disable a config twice. */
		if (has_config(&expected_table->disabled_configs, &config))
			continue;

		add_config(test, &config_set, &config);
	}

	disable_configs_and_verify(test, link_caps, expected_table,
				   &config_set);
}

static void get_params_shrink_step(struct test_ctx *ctx,
				   int num_rates, int max_lane_count,
				   int *rates_step, int *lanes_step)
{
	int shrink_mask;

	*rates_step = 0;
	*lanes_step = 0;

	if (num_rates == 1)
		shrink_mask = BIT(0);				/* shrink only lanes */
	else if (max_lane_count == 1)
		shrink_mask = BIT(1);				/* shrink only rates */
	else
		shrink_mask = rand_in_range(ctx,
					    BIT(0),
					    BIT(0) | BIT(1));	/* shrink one or both params */

	if (shrink_mask & BIT(1))
		*rates_step = rand_in_range(ctx, 1, num_rates - 1);

	if (shrink_mask & BIT(0))
		*lanes_step = rand_in_range(ctx, 1, ilog2(max_lane_count));
}

static void get_params_expand_step(struct test_ctx *ctx,
				   int max_num_rates, int num_rates,
				   int max_supported_lane_count, int max_lane_count,
				   int *rates_step, int *lanes_step)
{
	int expand_mask;

	*rates_step = 0;
	*lanes_step = 0;

	if (num_rates == max_num_rates)
		expand_mask = BIT(0);				/* expand only lanes */
	else if (max_lane_count == max_supported_lane_count)
		expand_mask = BIT(1);				/* expand only rates */
	else
		expand_mask = rand_in_range(ctx,
					    BIT(0),
					    BIT(0) | BIT(1));	/* expand one or both params */

	if (expand_mask & BIT(1))
		*rates_step = rand_in_range(ctx, 1, max_num_rates - num_rates);

	if (expand_mask & BIT(0))
		*lanes_step = rand_in_range(ctx, 1, ilog2(max_supported_lane_count /
							  max_lane_count));
}

static void test_update_params_shrink_random(struct kunit *test, bool disable_configs)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, LINK_TEST_NUM_STANDARD_RATES,
					  LINK_TEST_MAX_LANE_COUNT);

	update_link_caps_and_verify(&expected_table, link_caps, true);

	while (expected_table.rates.size > 1 || expected_table.max_lane_count > 1) {
		int rates_step;
		int lanes_step;

		if (disable_configs)
			disable_random_configs_and_verify(&expected_table, link_caps);

		get_params_shrink_step(ctx,
				       expected_table.rates.size,
				       expected_table.max_lane_count,
				       &rates_step, &lanes_step);

		expected_table.rates.size -= rates_step;
		expected_table.max_lane_count >>= lanes_step;

		update_link_caps_and_verify(&expected_table, link_caps, false);
	}
}

static void intel_dp_link_caps_test_update_params_shrink_random(struct kunit *test)
{
	int i;

	for (i = 0; i < LINK_TEST_NUM_RANDOM_ITERATIONS; i++)
		test_update_params_shrink_random(test, false);
}

static void intel_dp_link_caps_test_update_params_shrink_disable_random(struct kunit *test)
{
	int i;

	for (i = 0; i < LINK_TEST_NUM_RANDOM_ITERATIONS; i++)
		test_update_params_shrink_random(test, true);
}

static void test_update_params_expand_random(struct kunit *test, bool disable_configs)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, 1, 1);

	update_link_caps_and_verify(&expected_table, link_caps, true);

	while (expected_table.rates.size < LINK_TEST_NUM_STANDARD_RATES ||
	       expected_table.max_lane_count < LINK_TEST_MAX_LANE_COUNT) {
		int rates_step;
		int lanes_step;

		if (disable_configs)
			disable_random_configs_and_verify(&expected_table, link_caps);

		get_params_expand_step(ctx,
				       LINK_TEST_NUM_STANDARD_RATES,
				       expected_table.rates.size,
				       LINK_TEST_MAX_LANE_COUNT,
				       expected_table.max_lane_count,
				       &rates_step, &lanes_step);

		expected_table.rates.size += rates_step;
		expected_table.max_lane_count <<= lanes_step;

		update_link_caps_and_verify(&expected_table, link_caps, false);
	}
}

static void intel_dp_link_caps_test_update_params_expand_random(struct kunit *test)
{
	int i;

	for (i = 0; i < LINK_TEST_NUM_RANDOM_ITERATIONS; i++)
		test_update_params_expand_random(test, false);
}

static void intel_dp_link_caps_test_update_params_expand_disable_random(struct kunit *test)
{
	int i;

	for (i = 0; i < LINK_TEST_NUM_RANDOM_ITERATIONS; i++)
		test_update_params_expand_random(test, true);
}

/*
 * TEST: Fallback sequence
 * -----------------------
 * Verify the eDP fallback logic to set the maximum supported configuration
 * as a preference.
 *
 * For DP SST and MST verify fallback selection from the connector's
 * maximum configuration and iteration of the resulting allowed
 * configurations.
 */
static void intel_dp_link_test_fallback_for_edp(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, LINK_TEST_NUM_STANDARD_RATES,
					  LINK_TEST_MAX_LANE_COUNT);
	struct intel_digital_port *dig_port = &ctx->dev.dig_port;
	const struct intel_dp_link_training_test_ops *lt_ops =
		ctx->link_training_ops;
	const struct intel_dp_link_caps_test_ops *lc_ops =
		ctx->link_caps_ops;
	struct intel_dp_link_config min_config = {
		.rate = expected_table.rates.entries[0],
		.lane_count = 1,
	};
	struct intel_dp_link_config max_config = {
		.rate = expected_table.rates.entries[expected_table.rates.size - 1],
		.lane_count = LINK_TEST_MAX_LANE_COUNT,
	};
	struct intel_dp_link_caps_order order;
	struct intel_dp_link_config iter_config;
	struct intel_dp_link_caps_iter iter;
	int fallback_err;

	dig_port->base.type = INTEL_OUTPUT_EDP;
	ctx->dev.dig_port.dp.use_max_params = false;

	update_link_caps_and_verify(&expected_table, link_caps, true);

	order = lc_ops->connector_compute_order(&ctx->dev.connector);

	lc_ops->iter_start(&iter, link_caps, order, INTEL_DP_LINK_CAPS_FILTER_ALL);
	for_each_dp_link_config(&iter, &iter_config)
		break;
	lc_ops->iter_end(&iter);

	KUNIT_EXPECT_FALSE(test, ctx->dev.dig_port.dp.use_max_params);
	KUNIT_EXPECT_TRUE(test, link_configs_match(&iter_config, &min_config));

	ctx->dev.crtc_state.output_types = BIT(dig_port->base.type);
	ctx->dev.crtc_state.port_clock = min_config.rate;
	ctx->dev.crtc_state.lane_count = min_config.lane_count;

	fallback_err = lt_ops->get_fallback_values(&ctx->dev.dig_port.dp, &ctx->dev.crtc_state);
	KUNIT_EXPECT_EQ(test, fallback_err, 0);

	/* The fallback should've changed the order. */
	order = lc_ops->connector_compute_order(&ctx->dev.connector);

	lc_ops->iter_start(&iter, link_caps, order, INTEL_DP_LINK_CAPS_FILTER_ALL);
	for_each_dp_link_config(&iter, &iter_config)
		break;
	lc_ops->iter_end(&iter);

	KUNIT_EXPECT_TRUE(test, ctx->dev.dig_port.dp.use_max_params);
	KUNIT_EXPECT_TRUE(test, link_configs_match(&iter_config, &max_config));
}

static bool test_fallback_from_target(struct test_config_table *expected_table,
				      enum intel_output_type output_type,
				      const struct intel_dp_link_config *expected_target_config,
				      const struct intel_dp_link_config *expected_fallback_config)
{
	struct kunit *test = expected_table->test;
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct intel_dp_link_config iter_config;
	const struct intel_dp_link_training_test_ops *lt_ops =
		ctx->link_training_ops;
	const struct intel_dp_link_caps_test_ops *lc_ops =
		ctx->link_caps_ops;
	/* Modify default order direction for max config lookup. */
	struct intel_dp_link_caps_order fallback_order =
		lc_ops->connector_fallback_order(ctx->dev.connector.mst.dp);
	struct intel_dp_link_caps_iter iter;
	int expected_fallback_err = 0;
	int fallback_err;

	/* Get the max connector config, optionally filtered to the max_rate limit. */
	lc_ops->iter_start(&iter, link_caps, fallback_order, INTEL_DP_LINK_CAPS_FILTER_ALL);
	for_each_dp_link_config(&iter, &iter_config)
		break;
	lc_ops->iter_end(&iter);

	KUNIT_EXPECT_TRUE(test, link_configs_match(&iter_config,
						   expected_target_config));
	KUNIT_EXPECT_FALSE(test, link_configs_match(&iter_config,
						    &INTEL_DP_LINK_CONFIG_NULL));

	ctx->dev.crtc_state.output_types = BIT(output_type);
	ctx->dev.crtc_state.port_clock = expected_target_config->rate;
	ctx->dev.crtc_state.lane_count = expected_target_config->lane_count;

	if (link_configs_match(expected_fallback_config, &INTEL_DP_LINK_CONFIG_NULL))
		expected_fallback_err = -1;

	fallback_err = lt_ops->get_fallback_values(&ctx->dev.dig_port.dp, &ctx->dev.crtc_state);
	KUNIT_EXPECT_EQ(test, fallback_err, expected_fallback_err);

	if (!fallback_err) {
		/*
		 * NOTE: This test does not verify any implied fallback
		 * target selection.
		 *
		 * The current driver behavior may still select a fallback
		 * configuration indirectly via max_limits, but that is an
		 * implementation artifact rather than part of the intended
		 * fallback API behavior, and is therefore not verified here.
		 *
		 * Instead, the effect of the fallback logic is verified by
		 * checking that the failed target configuration is disabled.
		 * Selecting the next target configuration from the remaining
		 * allowed configurations belongs to the modeset link target
		 * selection logic.
		 */
		add_config(test, &expected_table->disabled_configs,
			   expected_target_config);
	}

	verify_link_caps(expected_table, link_caps);

	return !fallback_err;
}

static const struct link_config_set *
get_target_configs_for_output_type(struct kunit *test,
				   enum intel_output_type output_type)
{
	switch (output_type) {
	case INTEL_OUTPUT_DDI:
	case INTEL_OUTPUT_DP:
	case INTEL_OUTPUT_EDP:
		return &standard_dp_link_configs[INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE];
	case INTEL_OUTPUT_DP_MST:
		return &standard_dp_link_configs[INTEL_DP_LINK_CAPS_ORDER_KEY_BW];
	default:
		KUNIT_FAIL_AND_ABORT(test, "Missing output type: %d", output_type);
	}
}

static const struct link_config_set *
get_fallback_configs_for_output_type(struct kunit *test,
				     enum intel_output_type output_type)
{
	switch (output_type) {
	case INTEL_OUTPUT_DDI:
	case INTEL_OUTPUT_DP:
	case INTEL_OUTPUT_EDP:
		return &standard_dp_link_configs[INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE];
	case INTEL_OUTPUT_DP_MST:
		return &standard_dp_link_configs[INTEL_DP_LINK_CAPS_ORDER_KEY_BW];
	default:
		KUNIT_FAIL_AND_ABORT(test, "Missing output type: %d", output_type);
	}
}

static void assert_config_is_supported(const struct test_config_table *expected_table,
				       const struct intel_dp_link_config *config)
{
	struct kunit *test = expected_table->test;

	KUNIT_ASSERT_TRUE(test, has_rate(&expected_table->rates, config->rate));
	KUNIT_ASSERT_LE(test, config->lane_count, expected_table->max_lane_count);
}

static bool get_fallback_config(const struct test_config_table *expected_table,
				enum intel_output_type output_type,
				const struct intel_dp_link_config *target_config,
				struct intel_dp_link_config *fallback_config)
{
	struct kunit *test = expected_table->test;
	const struct link_config_set *config_set =
		get_fallback_configs_for_output_type(test, output_type);
	int i;

	i = lookup_config(config_set, target_config);
	KUNIT_ASSERT_GE(test, i, 0);

	for (i--; i >= 0; i--) {
		const struct intel_dp_link_config *config =
			&config_set->entries[i];

		assert_config_is_supported(expected_table, config);
		*fallback_config = *config;

		return true;
	}

	return false;
}

static bool get_target_config(const struct test_config_table *expected_table,
			      enum intel_output_type output_type,
			      struct intel_dp_link_config *target)
{
	struct kunit *test = expected_table->test;
	const struct link_config_set *config_set =
		get_target_configs_for_output_type(test, output_type);
	int i;

	for (i = config_set->size - 1; i >= 0; i--) {
		const struct intel_dp_link_config *config =
			&config_set->entries[i];

		assert_config_is_supported(expected_table, config);
		*target = *config;

		return true;
	}

	return false;
}

static void test_fallback_seq(struct kunit *test,
			      enum intel_output_type output_type)
{
	struct test_ctx *ctx = test->priv;
	struct intel_dp_link_caps *link_caps = ctx->dev.dig_port.dp.link.caps;
	struct test_config_table expected_table =
		INIT_STANDARD_TABLE(test, LINK_TEST_NUM_STANDARD_RATES,
					  LINK_TEST_MAX_LANE_COUNT);
	struct intel_digital_port *dig_port = &ctx->dev.dig_port;
	struct intel_dp_link_config fallback_config = {};
	struct intel_dp_link_config target_config;
	int fallback_count = 0;
	bool target_found;

	dig_port->base.type = output_type;
	ctx->dev.dig_port.dp.use_max_params = false;

	update_link_caps_and_verify(&expected_table, link_caps, true);

	/* Get the initial target config. */
	target_found = get_target_config(&expected_table, output_type,
					 &target_config);
	KUNIT_ASSERT_TRUE(test, target_found);

	for (;;) {
		/* Also test the case where no fallback is available. */
		if (!get_fallback_config(&expected_table, output_type,
					 &target_config, &fallback_config))
			fallback_config = INTEL_DP_LINK_CONFIG_NULL;

		if (!test_fallback_from_target(&expected_table, output_type,
					       &target_config, &fallback_config))
			break;

		/* Simply select the fallback config as the next target. */
		target_config = fallback_config;

		fallback_count++;
		KUNIT_ASSERT_LT(test, fallback_count, LINK_TEST_MAX_CONFIGS);
	}
}

static void intel_dp_link_test_fallback_for_sst(struct kunit *test)
{
	test_fallback_seq(test, INTEL_OUTPUT_DP);
}

static void intel_dp_link_test_fallback_for_mst(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;

	ctx->dev.connector.mst.dp = &ctx->dev.dig_port.dp;

	test_fallback_seq(test, INTEL_OUTPUT_DP_MST);
}

static struct kunit_case intel_dp_link_test_cases[] = {
	KUNIT_CASE(intel_dp_link_caps_test_baseline),

	KUNIT_CASE(intel_dp_link_caps_test_update_reset),

	KUNIT_CASE(intel_dp_link_caps_test_update_rates_shrink),
	KUNIT_CASE(intel_dp_link_caps_test_update_rates_shrink_disable),
	KUNIT_CASE(intel_dp_link_caps_test_update_rates_expand),
	KUNIT_CASE(intel_dp_link_caps_test_update_rates_expand_disable),
	KUNIT_CASE(intel_dp_link_caps_test_update_lanes_shrink),
	KUNIT_CASE(intel_dp_link_caps_test_update_lanes_shrink_disable),
	KUNIT_CASE(intel_dp_link_caps_test_update_lanes_expand),
	KUNIT_CASE(intel_dp_link_caps_test_update_lanes_expand_disable),
	KUNIT_CASE(intel_dp_link_caps_test_update_params_shrink_random),
	KUNIT_CASE(intel_dp_link_caps_test_update_params_shrink_disable_random),
	KUNIT_CASE(intel_dp_link_caps_test_update_params_expand_random),
	KUNIT_CASE(intel_dp_link_caps_test_update_params_expand_disable_random),

	KUNIT_CASE(intel_dp_link_test_fallback_for_edp),
	KUNIT_CASE(intel_dp_link_test_fallback_for_sst),
	KUNIT_CASE(intel_dp_link_test_fallback_for_mst),

	{}
};

static struct test_ctx test_ctx;

static int intel_dp_link_test_init(struct kunit *test)
{
	struct intel_digital_port *dig_port;
	struct intel_encoder *encoder;
	struct intel_dp *intel_dp;

	/* Reset the dev state for each test. */
	memset(&test_ctx.dev, 0, sizeof(test_ctx.dev));

	test_ctx.dev.generic_device.drm.dev = &test_ctx.dev.device;

	test_ctx.dev.display.drm = &test_ctx.dev.generic_device.drm;
	test_ctx.dev.generic_device.display = &test_ctx.dev.display;

	encoder = &test_ctx.dev.dig_port.base;
	encoder->base.dev = &test_ctx.dev.generic_device.drm;

	dig_port = &test_ctx.dev.dig_port;
	dig_port->base.type = INTEL_OUTPUT_DP;

	test_ctx.dev.connector.encoder = encoder;

	intel_dp = &dig_port->dp;
	intel_dp->attached_connector = &test_ctx.dev.connector;

	intel_dp->link.caps = test_ctx.link_caps_ops->init(intel_dp);

	test->priv = &test_ctx;

	return 0;
}

static void intel_dp_link_test_exit(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;

	ctx->link_caps_ops->cleanup(ctx->dev.dig_port.dp.link.caps);
}

static int intel_dp_link_test_suite_init(struct kunit_suite *test_suite)
{
#ifdef I915
	test_ctx.link_caps_ops = &i915_display_dp_link_caps_test_ops;
	test_ctx.link_training_ops = &i915_display_dp_link_training_test_ops;
#else
	test_ctx.link_caps_ops = &intel_display_dp_link_caps_test_ops;
	test_ctx.link_training_ops = &intel_display_dp_link_training_test_ops;
#endif
	prandom_seed_state(&test_ctx.rnd, 0);

	return 0;
}

static struct kunit_suite intel_dp_link_test_suite = {
	.name = "intel_dp_link",
	.suite_init = intel_dp_link_test_suite_init,
	.init = intel_dp_link_test_init,
	.exit = intel_dp_link_test_exit,
	.test_cases = intel_dp_link_test_cases,
};

kunit_test_suites(&intel_dp_link_test_suite);

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL and additional rights");
MODULE_DESCRIPTION("Intel DP link KUnit tests");
