// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/log2.h>
#include <linux/seq_buf.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/types.h>

#include <drm/drm_print.h>

#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_display_utils.h"
#include "intel_dp.h"
#include "intel_dp_link_caps.h"

/**
 * DOC: DisplayPort link capabilities
 *
 * The Intel DP link caps API tracks the supported and allowed
 * DisplayPort link configurations for a DP encoder and its attached
 * connectors, and provides helpers to iterate over the allowed
 * configurations and constrain them by filtering, disabling, or
 * limiting them to maximum link parameters.
 *
 * Locking
 * -------
 *
 * All accesses to this API must be serialized. The only exception
 * is intel_dp_link_caps_get_max_limits(), which allow lockless
 * lookup. Such lookups may observe an out-of-sync &struct
 * intel_dp_link_config tuple, i.e. a rate from one state and a lane
 * count from another.
 *
 * The Intel i915/xe drivers ensure the above serialization by holding
 * &drm_mode_config.connection_mutex and, while holding the lock,
 * waiting for any pending asynchronous atomic commits. This also allows
 * use of the API from the tails of asynchronous atomic commits, which
 * cannot hold the lock.
 *
 * Iterating and restricting link configurations
 * ---------------------------------------------
 *
 * The link configuration iterators can iterate the ``allowed
 * configurations`` during modeset configuration selection or link
 * training fallback handling in a configurable order.
 *
 * The iteration order can depend on connector type (eDP, DP SST,
 * DP MST) and modeset-specific conditions or driver policies, such
 * as DSC vs. non-DSC modes, power saving vs. better user experience,
 * or policy changes after a link training failure.
 *
 * The configurations exposed via the iterators can be additionally
 * constrained in the following ways:
 *
 * - Filtered for a given modeset based on modeset-specific conditions.
 *   Examples for such conditions include driver policies preferring
 *   power saving or better user experience, post-link training failure
 *   preference changes, or sink automated test requests limiting the
 *   usable configurations.
 *
 * - Disabled permanently for the connected sink. Examples of reasons
 *   to disable a configuration include a link training failure for a
 *   given configuration or a driver workaround preventing the use of
 *   a particular configuration.
 *
 * - Limited via a maximum link rate and lane count. For example, after
 *   a link training failure, subsequent modesets may be limited to
 *   configurations at or below the failed parameters.
 *
 *   This mechanism exists for backward compatibility only. Eventually,
 *   it will be removed in favor of relying solely on individually
 *   disabled configurations, as described above.
 *
 * Terminology
 * -----------
 *
 * ``Common link capabilities`` (or ``common caps``) refer to the link
 * rates and maximum lane count supported by both the source and the
 * sink, i.e. the intersection of their respective capabilities.
 *
 * ``Supported configurations`` are all configurations defined by the
 * ``Common link capabilities``' link rates and maximum lane count.
 *
 * ``Disabled configurations`` are ``Supported configurations`` disabled
 * via this API.
 *
 * ``Enabled configurations`` are ``Supported configurations`` that are
 * not disabled.
 *
 * ``Forced configurations`` are ``Enabled configurations`` forced via
 * forced link parameter debugfs entries.
 *
 * ``Allowed configurations`` are the ``Enabled configurations``, or if
 * forcing is in effect the ``Forced configurations``, constrained by a
 * maximum rate and lane count set via the API.
 */
struct intel_dp_link_caps {
	struct intel_dp *dp;

	/* Rate, lane count caps common to source and sink. */
	int num_rates;
	int rates[DP_MAX_SUPPORTED_RATES];
	int max_lane_count;

	/* common rate,lane_count configs in bw order */
	int num_configs;
#define INTEL_DP_MAX_LANE_COUNT			4
#define INTEL_DP_MAX_SUPPORTED_LANE_CONFIGS	(ilog2(INTEL_DP_MAX_LANE_COUNT) + 1)
#define INTEL_DP_LANE_COUNT_EXP_BITS		order_base_2(INTEL_DP_MAX_SUPPORTED_LANE_CONFIGS)
#define INTEL_DP_LINK_RATE_IDX_BITS		(BITS_PER_TYPE(u8) - INTEL_DP_LANE_COUNT_EXP_BITS)
#define INTEL_DP_MAX_LINK_CONFIGS		(DP_MAX_SUPPORTED_RATES * \
						 INTEL_DP_MAX_SUPPORTED_LANE_CONFIGS)
	struct intel_dp_link_config_entry {
		/* index into rates[] */
		u8 link_rate_idx:INTEL_DP_LINK_RATE_IDX_BITS;
		u8 lane_count_exp:INTEL_DP_LANE_COUNT_EXP_BITS;
	} configs[INTEL_DP_MAX_LINK_CONFIGS];

	/*
	 * Indices to intel_dp_link_caps::configs[] in rate/lane count,
	 * lane_count/rate order.
	 */
	u8 rate_lane_map[INTEL_DP_MAX_LINK_CONFIGS];
	u8 lane_rate_map[INTEL_DP_MAX_LINK_CONFIGS];

	/*
	 * Filter of configurations enabled for the current sink
	 * connection.
	 *
	 * Each bit in the filter's configuration mask corresponds to a
	 * configuration index in the intel_dp_link_caps::configs[] array.
	 *
	 * All configurations start out enabled in the filter after a
	 * new sink is connected. Users disable configurations afterwards
	 * via the link caps API. All configurations get re-enabled
	 * internally in the following cases:
	 * - when forcing a link rate or lane count
	 * - when intel_dp_link_caps_update(reset=true) is called after
	 *   a new sink is connected
	 * - when intel_dp_link_caps_update(reset=false) with changed
	 *   link capabilities is called
	 * - when intel_dp_link_caps_reset() is called after a new sink
	 *   is connected
	 */
	struct intel_dp_link_caps_filter enabled_configs;

	/*
	 * Allowed configurations are the supported configurations defined by
	 * config_table.rates and config_table.max_lane_count, constrained by
	 * config_table.enabled_configs and the forced_params and
	 * max_limits values below.
	 *
	 * See get_allowed_config_filter() for the filter of these
	 * configurations.
	 */

	/*
	 * Forced parameters requested via debugfs. Remains set across sink
	 * disconnects.
	 */
	struct intel_dp_link_config forced_params;

	/*
	 * User set maximum limits. These limits constrain the currently
	 * allowed set of configurations and are not adjusted when sink
	 * capabilities change.
	 *
	 * max_limits.rate/lane_count may come from different allowed
	 * configurations, i.e. the (max_limits.rate, max_limits.lane_count)
	 * tuple itself may not be an allowed configuration.
	 */
	struct intel_dp_link_config max_limits;
};
static_assert(BITS_PER_TYPE(((struct intel_dp_link_caps_filter *)NULL)->config_mask) >=
	      ARRAY_SIZE(((struct intel_dp_link_caps *)NULL)->configs));

static struct intel_dp_link_caps_order bw_desc_config_order(void)
{
	struct intel_dp_link_caps_order order = {
		.key = INTEL_DP_LINK_CAPS_ORDER_KEY_BW,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_DESC,
	};

	return order;
}

static enum intel_dp_link_caps_order_key
connector_compute_order_key(bool is_mst)
{
	if (is_mst)
		return INTEL_DP_LINK_CAPS_ORDER_KEY_BW;
	else
		return INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE;
}

static enum intel_dp_link_caps_order_key
connector_fallback_order_key(bool is_mst)
{
	if (is_mst)
		return INTEL_DP_LINK_CAPS_ORDER_KEY_BW;
	else
		return INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE;
}

static enum intel_dp_link_caps_order_direction
connector_compute_order_dir(bool is_mst, bool use_max_params)
{
	if (is_mst || use_max_params)
		return INTEL_DP_LINK_CAPS_ORDER_DIR_DESC;
	else
		return INTEL_DP_LINK_CAPS_ORDER_DIR_ASC;
}

struct intel_dp_link_caps_order
intel_dp_link_caps_connector_compute_order(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_caps_order order = {
		.key = connector_compute_order_key(connector->mst.dp),
		.dir = connector_compute_order_dir(connector->mst.dp, intel_dp->use_max_params)
	};

	return order;
}

struct intel_dp_link_caps_order
intel_dp_link_caps_connector_fallback_order(bool is_mst)
{
	struct intel_dp_link_caps_order order = {
		.key = connector_fallback_order_key(is_mst),
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_DESC,
	};

	return order;
}

/* Get length of common rates array potentially limited by max_rate. */
static int intel_dp_common_len_rate_limit(struct intel_dp_link_caps *link_caps,
					  int max_rate)
{
	return intel_dp_rate_limit_len(link_caps->rates,
				       link_caps->num_rates, max_rate);
}

static int intel_dp_common_rate(struct intel_dp_link_caps *link_caps, int index)
{
	struct intel_display *display = to_intel_display(link_caps->dp);

	if (drm_WARN_ON(display->drm,
			index < 0 || index >= link_caps->num_rates))
		return 162000;

	return link_caps->rates[index];
}

/* Theoretical max between source and sink */
static int intel_dp_max_common_rate(struct intel_dp_link_caps *link_caps)
{
	return intel_dp_common_rate(link_caps, link_caps->num_rates - 1);
}

void intel_dp_link_caps_print_common_rates(struct intel_dp_link_caps *link_caps)
{
	struct intel_display *display = to_intel_display(link_caps->dp);
	DECLARE_SEQ_BUF(s, 128);
	int i;

	for (i = 0; i < link_caps->num_rates; i++)
		seq_buf_printf(&s, "%s%d", i ? ", " : "", link_caps->rates[i]);

	drm_dbg_kms(display->drm, "common rates: %s\n", seq_buf_str(&s));
}

static int intel_dp_link_caps_max_common_lane_count(struct intel_dp_link_caps *link_caps)
{
	return link_caps->max_lane_count;
}

static int forced_lane_count(struct intel_dp_link_caps *link_caps)
{
	if (!link_caps->forced_params.lane_count)
		return 0;

	return clamp(link_caps->forced_params.lane_count,
		     1, intel_dp_link_caps_max_common_lane_count(link_caps));
}

static int forced_link_rate(struct intel_dp_link_caps *link_caps)
{
	int len;

	if (!link_caps->forced_params.rate)
		return 0;

	len = intel_dp_common_len_rate_limit(link_caps, link_caps->forced_params.rate);
	if (len == 0)
		return intel_dp_common_rate(link_caps, 0);

	return intel_dp_common_rate(link_caps, len - 1);
}

void intel_dp_link_caps_get_forced_params(struct intel_dp_link_caps *link_caps,
					  struct intel_dp_link_config *forced_params)
{
	forced_params->rate = forced_link_rate(link_caps);
	forced_params->lane_count = forced_lane_count(link_caps);
}

static int intel_dp_link_config_rate(struct intel_dp_link_caps *link_caps,
				     const struct intel_dp_link_config_entry *lce)
{
	return intel_dp_common_rate(link_caps, lce->link_rate_idx);
}

static int intel_dp_link_config_lane_count(const struct intel_dp_link_config_entry *lce)
{
	return 1 << lce->lane_count_exp;
}

static void
to_intel_dp_link_config(struct intel_dp_link_caps *link_caps,
			int config_idx, struct intel_dp_link_config *config)
{
	const struct intel_dp_link_config_entry *lce = &link_caps->configs[config_idx];

	config->rate = intel_dp_link_config_rate(link_caps, lce);
	config->lane_count = intel_dp_link_config_lane_count(lce);
}

static int
iter_pos_to_idx(struct intel_dp_link_caps *link_caps,
		struct intel_dp_link_caps_order config_order,
		int iter_pos)
{
	int config_idx;

	if (!in_range(iter_pos, 0, link_caps->num_configs))
		return -1;

	switch (config_order.dir) {
	case INTEL_DP_LINK_CAPS_ORDER_DIR_ASC:
		break;
	case INTEL_DP_LINK_CAPS_ORDER_DIR_DESC:
		iter_pos = link_caps->num_configs - 1 - iter_pos;

		break;
	default:
		MISSING_CASE(config_order.dir);

		return -1;
	}

	switch (config_order.key) {
	case INTEL_DP_LINK_CAPS_ORDER_KEY_BW:
		config_idx = iter_pos;

		break;
	case INTEL_DP_LINK_CAPS_ORDER_KEY_RATE_LANE:
		config_idx = link_caps->rate_lane_map[iter_pos];

		break;
	case INTEL_DP_LINK_CAPS_ORDER_KEY_LANE_RATE:
		config_idx = link_caps->lane_rate_map[iter_pos];

		break;
	default:
		MISSING_CASE(config_order.key);

		return -1;
	}

	return config_idx;
}

static bool iter_get_next_config(struct intel_dp_link_caps_iter *iter,
				 struct intel_dp_link_config *config)
{
	while (true) {
		int config_idx;

		iter->pos++;

		config_idx = iter_pos_to_idx(iter->link_caps, iter->order, iter->pos);
		if (config_idx < 0) {
			iter->pos = -1;
			*config = INTEL_DP_LINK_CONFIG_NULL;

			break;
		}

		if (!(BIT(config_idx) & iter->filter.config_mask))
			continue;

		to_intel_dp_link_config(iter->link_caps, config_idx, config);

		break;
	}

	return iter->pos >= 0;
}

static void iter_start(struct intel_dp_link_caps_iter *iter,
		       struct intel_dp_link_caps *link_caps,
		       struct intel_dp_link_caps_order order,
		       struct intel_dp_link_caps_filter filter)
{
	iter->link_caps = link_caps;
	iter->pos = -1;
	iter->order = order;
	iter->filter = filter;

	iter->get_next_config = iter_get_next_config;
}

static struct intel_dp_link_caps_filter
calc_allowed_config_filter(struct intel_dp_link_caps *link_caps,
			   struct intel_dp_link_caps_filter enabled_configs,
			   const struct intel_dp_link_config *max_limits,
			   const struct intel_dp_link_config *forced_params)
{
	struct intel_dp_link_caps_filter allowed_configs = INTEL_DP_LINK_CAPS_FILTER_NONE;
	struct intel_dp_link_caps_order order = bw_desc_config_order();
	struct intel_dp_link_caps_iter iter;
	struct intel_dp_link_config config;

	iter_start(&iter, link_caps, order, enabled_configs);
	for_each_dp_link_config(&iter, &config) {
		if (forced_params->rate &&
		    forced_params->rate != config.rate)
			continue;

		if (forced_params->lane_count &&
		    forced_params->lane_count != config.lane_count)
			continue;

		if (config.rate > max_limits->rate)
			continue;

		if (config.lane_count > max_limits->lane_count)
			continue;

		allowed_configs.config_mask |= BIT(iter_pos_to_idx(link_caps, order, iter.pos));
	}
	intel_dp_link_caps_iter_end(&iter);

	return allowed_configs;
}

/*
 * get_allowed_config_filter - get filter for the currently allowed configs
 * @link_caps: link capabilities state
 *
 * Return:
 * Filter of link configurations allowed after applying the current
 * maximum link limits, and further narrowing them by removing any disabled
 * configuration and limiting to forced link parameters.
 *
 * See also:
 * - intel_dp_link_caps_get_max_limits()
 * - intel_dp_link_caps_get_forced_params()
 */
static struct intel_dp_link_caps_filter
get_allowed_config_filter(struct intel_dp_link_caps *link_caps)
{
	struct intel_dp_link_config forced_params;

	intel_dp_link_caps_get_forced_params(link_caps, &forced_params);

	return calc_allowed_config_filter(link_caps, link_caps->enabled_configs,
					  &link_caps->max_limits, &forced_params);
}

void intel_dp_link_caps_iter_start(struct intel_dp_link_caps_iter *iter,
				   struct intel_dp_link_caps *link_caps,
				   struct intel_dp_link_caps_order order,
				   struct intel_dp_link_caps_filter filter)
{
	filter.config_mask &= get_allowed_config_filter(link_caps).config_mask;

	iter_start(iter, link_caps, order, filter);
}

void intel_dp_link_caps_iter_end(struct intel_dp_link_caps_iter *iter)
{
	memset(iter, 0, sizeof(*iter));
}

/**
 * intel_dp_link_caps_get_max_config - get the maximum config in a given order
 * @link_caps: link capabilities state
 * @order_key: ordering key used to rank candidate configurations
 * @filter: filter for candidate configurations
 * @max_config: returned maximum link configuration
 *
 * Find the last configuration among the currently allowed
 * configurations filtered by @filter in the iteration order
 * selected by @order_key, and store it in @max_config.
 *
 * See also:
 * - &enum intel_dp_link_caps_order_key
 *
 * Returns:
 * %true if a maximum config is returned
 * %false otherwise.
 */
bool intel_dp_link_caps_get_max_config(struct intel_dp_link_caps *link_caps,
				       enum intel_dp_link_caps_order_key order_key,
				       struct intel_dp_link_caps_filter filter,
				       struct intel_dp_link_config *max_config)
{
	struct intel_dp_link_caps_order order = {
		.key = order_key,
		.dir = INTEL_DP_LINK_CAPS_ORDER_DIR_DESC
	};
	struct intel_dp_link_config iter_config;
	struct intel_dp_link_caps_iter iter;
	bool found = false;

	intel_dp_link_caps_iter_start(&iter, link_caps, order, filter);
	for_each_dp_link_config(&iter, &iter_config) {
		found = true;
		break;
	}
	intel_dp_link_caps_iter_end(&iter);

	if (!found)
		return false;

	*max_config = iter_config;

	return true;
}

/**
 * intel_dp_link_caps_get_max_bw_config - get maximum BW link configuration
 * @link_caps: link capabilities state
 * @max_config: returned maximum link configuration
 *
 * Return the maximum BW link configuration among the currently
 * allowed configurations.
 */
void intel_dp_link_caps_get_max_bw_config(struct intel_dp_link_caps *link_caps,
					  struct intel_dp_link_config *max_config)
{
	if (!intel_dp_link_caps_get_max_config(link_caps,
					       bw_desc_config_order().key, INTEL_DP_LINK_CAPS_FILTER_ALL,
					       max_config))
		*max_config = INTEL_DP_LINK_CONFIG_NULL;
}

static int find_config_idx(struct intel_dp_link_caps *link_caps,
			   struct intel_dp_link_caps_filter filter,
			   const struct intel_dp_link_config *link_config)
{
	struct intel_dp_link_caps_order order = bw_desc_config_order();
	struct intel_dp_link_config iter_config;
	struct intel_dp_link_caps_iter iter;
	int pos = -1;

	intel_dp_link_caps_iter_start(&iter, link_caps, order, filter);
	for_each_dp_link_config(&iter, &iter_config) {
		if (iter_config.rate == link_config->rate &&
		    iter_config.lane_count == link_config->lane_count) {
			pos = iter.pos;

			break;
		}
	}
	intel_dp_link_caps_iter_end(&iter);

	if (pos < 0)
		return pos;

	return iter_pos_to_idx(link_caps, order, pos);
}

bool intel_dp_link_caps_filter_add(struct intel_dp_link_caps *link_caps,
				   struct intel_dp_link_caps_filter *filter,
				   const struct intel_dp_link_config *config)
{
	int idx;

	idx = find_config_idx(link_caps, get_allowed_config_filter(link_caps), config);
	if (idx < 0)
		return false;

	filter->config_mask |= BIT(idx);

	return true;
}

static bool intel_dp_link_caps_filter_remove(struct intel_dp_link_caps *link_caps,
					     struct intel_dp_link_caps_filter *filter,
					     const struct intel_dp_link_config *config)
{
	int idx;

	idx = find_config_idx(link_caps, get_allowed_config_filter(link_caps), config);
	if (idx < 0)
		return false;

	filter->config_mask &= ~BIT(idx);

	return true;
}

static void set_max_link_limits(struct intel_dp_link_caps *link_caps,
				const struct intel_dp_link_config *max_link_limits)
{
	link_caps->max_limits = *max_link_limits;
}

static void reset_max_link_limits(struct intel_dp_link_caps *link_caps)
{
	struct intel_dp_link_config max_link_limits = {
		.rate = intel_dp_max_common_rate(link_caps),
		.lane_count = intel_dp_link_caps_max_common_lane_count(link_caps),
	};

	set_max_link_limits(link_caps, &max_link_limits);
}

static void reset_max_link_limits_reenable_all(struct intel_dp_link_caps *link_caps)
{
	link_caps->enabled_configs = INTEL_DP_LINK_CAPS_FILTER_ALL;
	reset_max_link_limits(link_caps);
}

/**
 * intel_dp_link_caps_disable_config - disable a configuration
 * @link_caps: link capabilities state
 * @config: configuration to disable
 *
 * Disable the configuration identified by @config. This removes the
 * configuration from the set of allowed configurations. The disabling
 * shouldn't leave the remaining configuration set empty.
 *
 * The configuration remains disallowed until intel_dp_link_caps() with
 * reset=%true or changed sink capabilities is called, or
 * intel_dp_link_caps_reset() is called. Each of these happens after a
 * new sink is connected or the currently connected sink changes its
 * capabilities.
 *
 * Return:
 * - %true  if @config was valid and the derived state was updated.
 * - %false if @config was invalid or the remaining configuration set
 *   would remain empty.
 */
bool intel_dp_link_caps_disable_config(struct intel_dp_link_caps *link_caps,
				       const struct intel_dp_link_config *config)
{
	struct intel_dp_link_caps_filter enabled_configs = link_caps->enabled_configs;
	struct intel_dp_link_config forced_params;

	if (!intel_dp_link_caps_filter_remove(link_caps, &enabled_configs, config))
		return false;

	intel_dp_link_caps_get_forced_params(link_caps, &forced_params);

	if (!calc_allowed_config_filter(link_caps, enabled_configs,
					&link_caps->max_limits, &forced_params).config_mask)
		return false;

	link_caps->enabled_configs = enabled_configs;

	return true;
}

/**
 * intel_dp_link_caps_get_max_limits - get the current maximum link limits
 * @link_caps: link capabilities state
 * @max_link_limits: returned maximum link limits
 *
 * Return the current maximum rate and lane count limits in
 * @max_link_limits.
 *
 * These limits constrain the set of allowed configurations.
 *
 * The limits are set to the maximum common supported values after
 * intel_dp_link_caps_reset() is called, and can later be modified by
 * intel_dp_link_caps_set_max_limits(). The max rate and lane count
 * parameters are independent limits, so the pair does not necessarily
 * define a valid configuration.
 *
 * This function may be called without serializing against updates to
 * @link_caps. However, without such serialization the returned value may be
 * an out-of-sync (link rate, lane count) tuple, i.e. the parameters may
 * belong to different update snapshots in time.
 */
void intel_dp_link_caps_get_max_limits(struct intel_dp_link_caps *link_caps,
				       struct intel_dp_link_config *max_link_limits)
{
	*max_link_limits = link_caps->max_limits;
}

static bool max_link_limits_valid(struct intel_dp_link_caps *link_caps,
				  const struct intel_dp_link_config *max_link_limits)
{
	struct intel_dp_link_caps_filter allowed_configs;
	struct intel_dp_link_config forced_params;

	if (max_link_limits->lane_count > INTEL_DP_MAX_LANE_COUNT ||
	    !is_power_of_2(max_link_limits->lane_count))
		return false;

	/* TODO: Validate max_link_limits->rate against the source supported rates. */

	intel_dp_link_caps_get_forced_params(link_caps, &forced_params);
	allowed_configs = calc_allowed_config_filter(link_caps, link_caps->enabled_configs,
						     max_link_limits, &forced_params);

	return allowed_configs.config_mask != 0;
}

/**
 * intel_dp_link_caps_set_max_limits - set the current maximum link limits
 * @link_caps: link capabilities state
 * @max_link_limits: new maximum link limits
 *
 * Set the current maximum rate and lane count limits to @max_link_limits,
 * constraining the set of allowed configurations.
 *
 * The new limits must leave at least one configuration allowed: the limits
 * must not be below the currently active forced parameters or below all the
 * configurations that remain after disabled configurations are excluded.
 *
 * Unlike intel_dp_link_caps_get_max_limits(), the caller must serialize
 * this call against concurrent queries and updates to @link_caps, in line
 * with the rest of the API.
 *
 * Return:
 * - %true  if the @link_caps cached max limits value got updated with
 *          @max_link_limits.
 * - %false if @max_link_limits is invalid.
 */
bool intel_dp_link_caps_set_max_limits(struct intel_dp_link_caps *link_caps,
				       const struct intel_dp_link_config *max_link_limits)
{
	if (!max_link_limits_valid(link_caps, max_link_limits))
		return false;

	set_max_link_limits(link_caps, max_link_limits);

	return true;
}

/**
 * intel_dp_link_caps_reset_max_limits - reset the current maximum link limits
 * @link_caps: link capabilities state
 *
 * Reset the current maximum link limits to the maximum supported common link
 * rate and lane count.
 */
void intel_dp_link_caps_reset_max_limits(struct intel_dp_link_caps *link_caps)
{
	reset_max_link_limits(link_caps);
}

static int intel_dp_link_config_bw(struct intel_dp_link_caps *link_caps,
				   const struct intel_dp_link_config_entry *lce)
{
	return drm_dp_max_dprx_data_rate(intel_dp_link_config_rate(link_caps, lce),
					 intel_dp_link_config_lane_count(lce));
}

static int link_config_cmp_by_bw(const void *a, const void *b, const void *p)
{
	struct intel_dp *intel_dp = (struct intel_dp *)p;	/* remove const */
	struct intel_dp_link_caps *link_caps = intel_dp->link.caps;

	const struct intel_dp_link_config_entry *lce_a = a;
	const struct intel_dp_link_config_entry *lce_b = b;
	int bw_a = intel_dp_link_config_bw(link_caps, lce_a);
	int bw_b = intel_dp_link_config_bw(link_caps, lce_b);

	if (bw_a != bw_b)
		return bw_a - bw_b;

	return intel_dp_link_config_rate(link_caps, lce_a) -
	       intel_dp_link_config_rate(link_caps, lce_b);
}

static int link_config_cmp_by_rate_lane(const void *a, const void *b, const void *p)
{
	const struct intel_dp_link_caps *link_caps = p;
	u8 *lce_a_idx = (u8 *)a;
	u8 *lce_b_idx = (u8 *)b;
	const struct intel_dp_link_config_entry *lce_a = &link_caps->configs[*lce_a_idx];
	const struct intel_dp_link_config_entry *lce_b = &link_caps->configs[*lce_b_idx];

	if (lce_a->link_rate_idx != lce_b->link_rate_idx)
		return lce_a->link_rate_idx - lce_b->link_rate_idx;

	return lce_a->lane_count_exp - lce_b->lane_count_exp;
}

static int link_config_cmp_by_lane_rate(const void *a, const void *b, const void *p)
{
	const struct intel_dp_link_caps *link_caps = p;
	u8 *lce_a_idx = (u8 *)a;
	u8 *lce_b_idx = (u8 *)b;
	const struct intel_dp_link_config_entry *lce_a = &link_caps->configs[*lce_a_idx];
	const struct intel_dp_link_config_entry *lce_b = &link_caps->configs[*lce_b_idx];

	if (lce_a->lane_count_exp != lce_b->lane_count_exp)
		return lce_a->lane_count_exp - lce_b->lane_count_exp;

	return lce_a->link_rate_idx - lce_b->link_rate_idx;
}

/**
 * intel_dp_link_caps_update - rebuild the supported link configuration state
 * @link_caps: link capabilities state
 * @rates: supported common link rates
 * @num_rates: number of entries in @rates
 * @max_lane_count: supported maximum lane count
 * @reset: reset limits and disabled configs
 *
 * Rebuild the supported link configuration state from @rates and
 * @max_lane_count.
 *
 * If @reset is %true, reset the maximum link limits to the maximum
 * supported rate and lane count, and re-enable all configurations.
 *
 * This function is called regularly, at least after a sink is connected,
 * but it may also be called later whenever the sink capabilities may have
 * changed, for example in response to HPD IRQ / RX_CAP_CHANGED signaling.
 *
 * In the Intel driver this function is currently called whenever the
 * connector detect handler runs, after reading the sink capabilities. This
 * may change if those capabilities are cached until the sink is
 * disconnected, or until RX_CAP_CHANGED is signaled. In any case, this
 * function should be called whenever the sink capabilities were read out
 * and may have changed.
 *
 * Returns:
 * - %true if the link capabilities have changed, %false otherwise.
 */
bool intel_dp_link_caps_update(struct intel_dp_link_caps *link_caps,
			       const int *rates, int num_rates, int max_lane_count,
			       bool reset)
{
	struct intel_dp *intel_dp = link_caps->dp;
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_dp_link_config_entry *lce;
	bool link_params_changed = reset;
	int num_common_lane_configs;
	int i;
	int j;

	if (drm_WARN_ON(display->drm, !is_power_of_2(max_lane_count)))
		return false;

	if (drm_WARN_ON(display->drm, num_rates > ARRAY_SIZE(link_caps->rates)))
		return false;

	num_common_lane_configs = ilog2(max_lane_count) + 1;

	if (drm_WARN_ON(display->drm, num_rates * num_common_lane_configs >
				    ARRAY_SIZE(link_caps->configs)))
		return false;

	/* TODO: Add a struct containing both rates and number of rates. */
	static_assert(__same_type(rates[0], link_caps->rates[0]));
	if (num_rates != link_caps->num_rates ||
	    memcmp(rates, link_caps->rates, num_rates * sizeof(rates[0])))
		link_params_changed = true;

	if (max_lane_count != link_caps->max_lane_count)
		link_params_changed = true;

	memcpy(link_caps->rates, rates, num_rates * sizeof(rates[0]));
	link_caps->num_rates = num_rates;
	link_caps->max_lane_count = max_lane_count;

	link_caps->num_configs = num_rates * num_common_lane_configs;

	lce = &link_caps->configs[0];
	for (i = 0; i < link_caps->num_rates; i++) {
		for (j = 0; j < num_common_lane_configs; j++) {
			lce->lane_count_exp = j;
			lce->link_rate_idx = i;

			lce++;
		}
	}

	sort_r(link_caps->configs, link_caps->num_configs,
	       sizeof(link_caps->configs[0]),
	       link_config_cmp_by_bw, NULL,
	       intel_dp);

	for (i = 0; i < link_caps->num_configs; i++) {
		link_caps->rate_lane_map[i] = i;
		link_caps->lane_rate_map[i] = i;
	}

	sort_r(link_caps->rate_lane_map, link_caps->num_configs,
	       sizeof(link_caps->rate_lane_map[0]),
	       link_config_cmp_by_rate_lane, NULL,
	       link_caps);

	sort_r(link_caps->lane_rate_map, link_caps->num_configs,
	       sizeof(link_caps->lane_rate_map[0]),
	       link_config_cmp_by_lane_rate, NULL,
	       link_caps);

	if (link_params_changed)
		reset_max_link_limits_reenable_all(link_caps);

	return link_params_changed;
}

/**
 * intel_dp_link_caps_reset - reset link capability restrictions
 * @link_caps: link capabilities state
 *
 * Reset all current restrictions except for the user requested forced
 * parameters, thus updating the set of allowed configurations and the
 * derived maximum link information accordingly.
 *
 * This function is regularly called after a sink is connected, either
 * for the first time to the connector or after a previous sink was
 * disconnected from it, and intel_dp_link_caps_update() was called.
 */
void intel_dp_link_caps_reset(struct intel_dp_link_caps *link_caps)
{
	reset_max_link_limits_reenable_all(link_caps);
}

static int i915_dp_force_link_rate_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_caps *link_caps = intel_dp->link.caps;
	int current_rate = -1;
	int force_rate;
	int err;
	int i;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_flush_connector_commits(connector);

	if (intel_dp->link.active)
		current_rate = intel_dp->link_rate;

	force_rate = link_caps->forced_params.rate;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	seq_printf(m, "%sauto%s",
		   force_rate == 0 ? "[" : "",
		   force_rate == 0 ? "]" : "");

	for (i = 0; i < intel_dp->num_source_rates; i++)
		seq_printf(m, " %s%d%s%s",
			   intel_dp->source_rates[i] == force_rate ? "[" : "",
			   intel_dp->source_rates[i],
			   intel_dp->source_rates[i] == current_rate ? "*" : "",
			   intel_dp->source_rates[i] == force_rate ? "]" : "");

	seq_putc(m, '\n');

	return 0;
}

static int parse_link_rate(struct intel_dp_link_caps *link_caps, const char __user *ubuf, size_t len)
{
	struct intel_dp *intel_dp = link_caps->dp;
	char *kbuf;
	const char *p;
	int rate;
	int ret = 0;

	kbuf = memdup_user_nul(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	p = strim(kbuf);

	if (!strcmp(p, "auto")) {
		rate = 0;
	} else {
		ret = kstrtoint(p, 0, &rate);
		if (ret < 0)
			goto out_free;

		if (intel_dp_rate_index(intel_dp->source_rates,
					intel_dp->num_source_rates,
					rate) < 0)
			ret = -EINVAL;
	}

out_free:
	kfree(kbuf);

	return ret < 0 ? ret : rate;
}

static ssize_t i915_dp_force_link_rate_write(struct file *file,
					     const char __user *ubuf,
					     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_caps *link_caps = intel_dp->link.caps;
	int rate;
	int err;

	rate = parse_link_rate(link_caps, ubuf, len);
	if (rate < 0)
		return rate;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_flush_connector_commits(connector);

	intel_dp_reset_link_params(intel_dp);
	link_caps->forced_params.rate = rate;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	*offp += len;

	return len;
}
DEFINE_SHOW_STORE_ATTRIBUTE(i915_dp_force_link_rate);

static int i915_dp_force_lane_count_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_caps *link_caps = intel_dp->link.caps;
	int current_lane_count = -1;
	int force_lane_count;
	int err;
	int i;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_flush_connector_commits(connector);

	if (intel_dp->link.active)
		current_lane_count = intel_dp->lane_count;
	force_lane_count = link_caps->forced_params.lane_count;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	seq_printf(m, "%sauto%s",
		   force_lane_count == 0 ? "[" : "",
		   force_lane_count == 0 ? "]" : "");

	for (i = 1; i <= 4; i <<= 1)
		seq_printf(m, " %s%d%s%s",
			   i == force_lane_count ? "[" : "",
			   i,
			   i == current_lane_count ? "*" : "",
			   i == force_lane_count ? "]" : "");

	seq_putc(m, '\n');

	return 0;
}

static int parse_lane_count(const char __user *ubuf, size_t len)
{
	char *kbuf;
	const char *p;
	int lane_count;
	int ret = 0;

	kbuf = memdup_user_nul(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	p = strim(kbuf);

	if (!strcmp(p, "auto")) {
		lane_count = 0;
	} else {
		ret = kstrtoint(p, 0, &lane_count);
		if (ret < 0)
			goto out_free;

		switch (lane_count) {
		case 1:
		case 2:
		case 4:
			break;
		default:
			ret = -EINVAL;
		}
	}

out_free:
	kfree(kbuf);

	return ret < 0 ? ret : lane_count;
}

static ssize_t i915_dp_force_lane_count_write(struct file *file,
					      const char __user *ubuf,
					      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_caps *link_caps = intel_dp->link.caps;
	int lane_count;
	int err;

	lane_count = parse_lane_count(ubuf, len);
	if (lane_count < 0)
		return lane_count;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_flush_connector_commits(connector);

	intel_dp_reset_link_params(intel_dp);
	link_caps->forced_params.lane_count = lane_count;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	*offp += len;

	return len;
}
DEFINE_SHOW_STORE_ATTRIBUTE(i915_dp_force_lane_count);

static int i915_dp_max_link_rate_show(void *data, u64 *val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_config max_link_limits;
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_flush_connector_commits(connector);

	intel_dp_link_caps_get_max_limits(intel_dp->link.caps, &max_link_limits);
	*val = max_link_limits.rate;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(i915_dp_max_link_rate_fops, i915_dp_max_link_rate_show, NULL, "%llu\n");

static int i915_dp_max_lane_count_show(void *data, u64 *val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_config max_link_limits;
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_flush_connector_commits(connector);

	intel_dp_link_caps_get_max_limits(intel_dp->link.caps, &max_link_limits);
	*val = max_link_limits.lane_count;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(i915_dp_max_lane_count_fops, i915_dp_max_lane_count_show, NULL, "%llu\n");

static int intel_dp_allowed_link_configs_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_dp_link_caps *link_caps = intel_dp->link.caps;
	struct intel_dp_link_config link_config;
	struct intel_dp_link_caps_iter iter;
	int err;
	int i;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_flush_connector_commits(connector);

	i = 0;
	intel_dp_link_caps_iter_start(&iter,
				      link_caps,
				      intel_dp_link_caps_connector_compute_order(connector),
				      INTEL_DP_LINK_CAPS_FILTER_ALL);
	for_each_dp_link_config(&iter, &link_config) {
		seq_printf(m, "%s%dx%d",
			   i ? " " : "",
			   link_config.lane_count, link_config.rate);
		i++;
	}
	intel_dp_link_caps_iter_end(&iter);

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	seq_putc(m, '\n');

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(intel_dp_allowed_link_configs);

/**
 * intel_dp_link_caps_debugfs_add - add link caps debugfs files for a connector
 * @connector: connector to add the debugfs files for
 *
 * Add the link-capability debugfs files for a DP @connector.
 */
void intel_dp_link_caps_debugfs_add(struct intel_connector *connector)
{
	struct dentry *root = connector->base.debugfs_entry;

	if (connector->base.connector_type != DRM_MODE_CONNECTOR_DisplayPort &&
	    connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
		return;

	debugfs_create_file("i915_dp_force_link_rate", 0644, root,
			    connector, &i915_dp_force_link_rate_fops);

	debugfs_create_file("i915_dp_force_lane_count", 0644, root,
			    connector, &i915_dp_force_lane_count_fops);

	debugfs_create_file("i915_dp_max_link_rate", 0444, root,
			    connector, &i915_dp_max_link_rate_fops);

	debugfs_create_file("i915_dp_max_lane_count", 0444, root,
			    connector, &i915_dp_max_lane_count_fops);

	debugfs_create_file("intel_dp_allowed_link_configs", 0444, root,
			    connector, &intel_dp_allowed_link_configs_fops);
}

struct intel_dp_link_caps *intel_dp_link_caps_init(struct intel_dp *intel_dp)
{
	struct intel_dp_link_caps *link_caps;

	link_caps = kzalloc_obj(*link_caps);
	if (!link_caps)
		return NULL;

	link_caps->dp = intel_dp;
	link_caps->enabled_configs = INTEL_DP_LINK_CAPS_FILTER_ALL;

	return link_caps;
}

void intel_dp_link_caps_cleanup(struct intel_dp_link_caps *link_caps)
{
	kfree(link_caps);
}

#if IS_ENABLED(CONFIG_KUNIT)

#define __INIT_MEMBER(__name, __fn) \
	.__name = __fn,

#define INTEL_DP_LINK_CAPS_TEST_OPS_INIT \
	INTEL_DP_LINK_CAPS_TEST_OPS_MEMBERS(__INIT_MEMBER)

#ifdef I915

const struct intel_dp_link_caps_test_ops i915_display_dp_link_caps_test_ops = {
	INTEL_DP_LINK_CAPS_TEST_OPS_INIT
};
EXPORT_SYMBOL(i915_display_dp_link_caps_test_ops);

#else

const struct intel_dp_link_caps_test_ops intel_display_dp_link_caps_test_ops = {
	INTEL_DP_LINK_CAPS_TEST_OPS_INIT
};
EXPORT_SYMBOL(intel_display_dp_link_caps_test_ops);

#endif	/* I915 */

#undef INTEL_DP_LINK_CAPS_TEST_OPS_INIT
#undef __INIT_MEMBER

#endif	/* CONFIG_KUNIT */
