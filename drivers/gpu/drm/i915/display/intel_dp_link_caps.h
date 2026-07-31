/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef __INTEL_DP_LINK_CAPS_H__
#define __INTEL_DP_LINK_CAPS_H__

#include <linux/bitops.h>
#include <linux/types.h>

struct intel_connector;
struct intel_dp;
struct intel_dp_link_caps;
struct intel_dp_link_config;

/**
 * enum intel_dp_link_caps_order_key - key used to order configurations
 * @INTEL_DP_LINK_CAPS_ORDER_KEY_BW:
 *   Order configurations by bandwidth, then by link rate.
 * @INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE:
 *   Order configurations by link rate, then by lane count.
 * @INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE:
 *   Order configurations by lane count, then by link rate.
 * @INTEL_DP_LINK_CAPS_ORDER_KEY_NUM:
 *   Number of ordering keys.
 *
 * Selects how a caller wants the configuration table to be ordered,
 * together with an &enum intel_dp_link_caps_order_direction, for
 * iteration queries.
 *
 * See also:
 *  - &struct intel_dp_link_caps_order
 *  - intel_dp_link_caps_get_max_config()
 */
enum intel_dp_link_caps_order_key {
	INTEL_DP_LINK_CAPS_ORDER_KEY_BW,
	INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE,
	INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE,

	INTEL_DP_LINK_CAPS_ORDER_KEY_NUM
};

/**
 * enum intel_dp_link_caps_order_direction - iteration direction
 * @INTEL_DP_LINK_CAPS_ORDER_DIR_ASC:
 *   Iterate in ascending order according to the selected ordering key.
 * @INTEL_DP_LINK_CAPS_ORDER_DIR_DESC:
 *   Iterate in descending order according to the selected ordering key.
 * @INTEL_DP_LINK_CAPS_ORDER_DIR_NUM:
 *   Number of ordering directions.
 *
 * Selects the direction associated with an
 * &enum intel_dp_link_caps_order_key for iteration queries.
 *
 * See also:
 *  - &struct intel_dp_link_caps_order
 */
enum intel_dp_link_caps_order_direction {
	INTEL_DP_LINK_CAPS_ORDER_DIR_ASC,
	INTEL_DP_LINK_CAPS_ORDER_DIR_DESC,

	INTEL_DP_LINK_CAPS_ORDER_DIR_NUM
};

/**
 * struct intel_dp_link_caps_order - configuration ordering
 * @key:
 *   Key used to order configurations.
 * @dir:
 *   Direction of the selected ordering.
 *
 * Describes an iteration order for link configurations.
 *
 * See also:
 *  - for_each_dp_link_config()
 */
struct intel_dp_link_caps_order {
	enum intel_dp_link_caps_order_key key;
	enum intel_dp_link_caps_order_direction dir;
};

struct intel_dp_link_caps_filter {
	u32 config_mask;
};

#define INTEL_DP_LINK_CAPS_FILTER_NONE	\
	((struct intel_dp_link_caps_filter){ .config_mask = 0 })
#define INTEL_DP_LINK_CAPS_FILTER_ALL	\
	((struct intel_dp_link_caps_filter){ .config_mask = (u32)-1 })

struct intel_dp_link_caps_iter {
	struct intel_dp_link_caps *link_caps;
	int pos;
	struct intel_dp_link_caps_order order;
	struct intel_dp_link_caps_filter filter;

	bool (*get_next_config)(struct intel_dp_link_caps_iter *iter,
				struct intel_dp_link_config *config);
};

/**
 * for_each_dp_link_config - iterate allowed link configurations
 * @__iter:
 *   &struct intel_dp_link_caps_iter being iterated
 * @__config:
 *   pointer to &struct intel_dp_link_config filled for each match
 */
#define for_each_dp_link_config(__iter, __config) \
	while ((__iter)->get_next_config((__iter), (__config)))

void intel_dp_link_caps_iter_start(struct intel_dp_link_caps_iter *iter,
				   struct intel_dp_link_caps *link_caps,
				   struct intel_dp_link_caps_order order,
				   struct intel_dp_link_caps_filter filter);

void intel_dp_link_caps_iter_end(struct intel_dp_link_caps_iter *iter);

struct intel_dp_link_caps_order
intel_dp_link_caps_connector_compute_order(struct intel_connector *connector);
struct intel_dp_link_caps_order
intel_dp_link_caps_connector_fallback_order(bool is_mst);

void intel_dp_link_caps_print_common_rates(struct intel_dp_link_caps *link_caps);

void intel_dp_link_caps_get_forced_params(struct intel_dp_link_caps *link_caps,
					  struct intel_dp_link_config *forced_params);

bool intel_dp_link_caps_filter_add(struct intel_dp_link_caps *link_caps,
				   struct intel_dp_link_caps_filter *filter,
				   const struct intel_dp_link_config *config);

bool intel_dp_link_caps_get_max_config(struct intel_dp_link_caps *link_caps,
				       enum intel_dp_link_caps_order_key order_key,
				       struct intel_dp_link_caps_filter filter,
				       struct intel_dp_link_config *max_config);

void intel_dp_link_caps_get_max_bw_config(struct intel_dp_link_caps *link_caps,
					  struct intel_dp_link_config *max_config);

bool intel_dp_link_caps_disable_config(struct intel_dp_link_caps *link_caps,
				       const struct intel_dp_link_config *config);

void intel_dp_link_caps_get_max_limits(struct intel_dp_link_caps *link_caps,
				       struct intel_dp_link_config *max_link_limits);
bool intel_dp_link_caps_set_max_limits(struct intel_dp_link_caps *link_caps,
				       const struct intel_dp_link_config *max_link_limits);
void intel_dp_link_caps_reset_max_limits(struct intel_dp_link_caps *link_caps);

bool intel_dp_link_caps_update(struct intel_dp_link_caps *link_caps,
			       const int *rates, int num_rates, int max_lane_count,
			       bool reset);
void intel_dp_link_caps_reset(struct intel_dp_link_caps *link_caps);

void intel_dp_link_caps_debugfs_add(struct intel_connector *connector);

struct intel_dp_link_caps *intel_dp_link_caps_init(struct intel_dp *intel_dp);
void intel_dp_link_caps_cleanup(struct intel_dp_link_caps *link_caps);

#if IS_ENABLED(CONFIG_KUNIT)

#define INTEL_DP_LINK_CAPS_TEST_OPS_MEMBERS(__X) \
	__X(connector_compute_order,	intel_dp_link_caps_connector_compute_order) \
	__X(connector_fallback_order,	intel_dp_link_caps_connector_fallback_order) \
	__X(iter_start,			intel_dp_link_caps_iter_start) \
	__X(iter_end,			intel_dp_link_caps_iter_end) \
	__X(set_max_limits,		intel_dp_link_caps_set_max_limits) \
	__X(get_max_limits,		intel_dp_link_caps_get_max_limits) \
	__X(get_max_bw_config,		intel_dp_link_caps_get_max_bw_config) \
	__X(reset_max_limits,		intel_dp_link_caps_reset_max_limits) \
	__X(disable_config,		intel_dp_link_caps_disable_config) \
	__X(update,			intel_dp_link_caps_update) \
	__X(init,			intel_dp_link_caps_init) \
	__X(cleanup,			intel_dp_link_caps_cleanup)

#define __DECLARE_MEMBER(__name, __fn) \
	typeof(__fn) *__name;

#define INTEL_DP_LINK_CAPS_TEST_OPS_DECLARE \
	INTEL_DP_LINK_CAPS_TEST_OPS_MEMBERS(__DECLARE_MEMBER)

struct intel_dp_link_caps_test_ops {
	INTEL_DP_LINK_CAPS_TEST_OPS_DECLARE
};

#undef INTEL_DP_LINK_CAPS_TEST_OPS_DECLARE
#undef __DECLARE_MEMBER

#ifdef I915
extern const struct intel_dp_link_caps_test_ops i915_display_dp_link_caps_test_ops;
#else
extern const struct intel_dp_link_caps_test_ops intel_display_dp_link_caps_test_ops;
#endif	/* I915 */

#endif	/* CONFIG_KUNIT */

#endif /* __INTEL_DP_LINK_CAPS_H__ */
