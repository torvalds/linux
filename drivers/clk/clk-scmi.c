// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Power Interface (SCMI) Protocol based clock driver
 *
 * Copyright (C) 2018-2024 ARM Ltd.
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/scmi_protocol.h>
#include <asm/div64.h>

#define NOT_ATOMIC	false
#define ATOMIC		true

enum scmi_clk_feats {
	SCMI_CLK_ATOMIC_SUPPORTED,
	SCMI_CLK_STATE_CTRL_SUPPORTED,
	SCMI_CLK_RATE_CTRL_SUPPORTED,
	SCMI_CLK_PARENT_CTRL_SUPPORTED,
	SCMI_CLK_DUTY_CYCLE_SUPPORTED,
	SCMI_CLK_FEATS_COUNT
};

#define SCMI_MAX_CLK_OPS	BIT(SCMI_CLK_FEATS_COUNT)

static const struct scmi_clk_proto_ops *scmi_proto_clk_ops;

struct scmi_clk {
	u32 id;
	struct device *dev;
	struct clk_hw hw;
	const struct scmi_clock_info *info;
	const struct scmi_protocol_handle *ph;
	struct clk_parent_data *parent_data;
};

#define to_scmi_clk(clk) container_of(clk, struct scmi_clk, hw)

static unsigned long scmi_clk_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	int ret;
	u64 rate;
	struct scmi_clk *clk = to_scmi_clk(hw);

	ret = scmi_proto_clk_ops->rate_get(clk->ph, clk->id, &rate);
	if (ret)
		return 0;
	return rate;
}

static long scmi_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	u64 fmin, fmax, ftmp;
	struct scmi_clk *clk = to_scmi_clk(hw);

	/*
	 * We can't figure out what rate it will be, so just return the
	 * rate back to the caller. scmi_clk_recalc_rate() will be called
	 * after the rate is set and we'll know what rate the clock is
	 * running at then.
	 */
	if (clk->info->rate_discrete)
		return rate;

	fmin = clk->info->range.min_rate;
	fmax = clk->info->range.max_rate;
	if (rate <= fmin)
		return fmin;
	else if (rate >= fmax)
		return fmax;

	ftmp = rate - fmin;
	ftmp += clk->info->range.step_size - 1; /* to round up */
	do_div(ftmp, clk->info->range.step_size);

	return ftmp * clk->info->range.step_size + fmin;
}

static int scmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct scmi_clk *clk = to_scmi_clk(hw);

	return scmi_proto_clk_ops->rate_set(clk->ph, clk->id, rate);
}

static int scmi_clk_set_parent(struct clk_hw *hw, u8 parent_index)
{
	struct scmi_clk *clk = to_scmi_clk(hw);

	return scmi_proto_clk_ops->parent_set(clk->ph, clk->id, parent_index);
}

static u8 scmi_clk_get_parent(struct clk_hw *hw)
{
	struct scmi_clk *clk = to_scmi_clk(hw);
	u32 parent_id, p_idx;
	int ret;

	ret = scmi_proto_clk_ops->parent_get(clk->ph, clk->id, &parent_id);
	if (ret)
		return 0;

	for (p_idx = 0; p_idx < clk->info->num_parents; p_idx++) {
		if (clk->parent_data[p_idx].index == parent_id)
			break;
	}

	if (p_idx == clk->info->num_parents)
		return 0;

	return p_idx;
}

static int scmi_clk_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	/*
	 * Suppose all the requested rates are supported, and let firmware
	 * to handle the left work.
	 */
	return 0;
}

static int scmi_clk_enable(struct clk_hw *hw)
{
	struct scmi_clk *clk = to_scmi_clk(hw);

	return scmi_proto_clk_ops->enable(clk->ph, clk->id, NOT_ATOMIC);
}

static void scmi_clk_disable(struct clk_hw *hw)
{
	struct scmi_clk *clk = to_scmi_clk(hw);

	scmi_proto_clk_ops->disable(clk->ph, clk->id, NOT_ATOMIC);
}

static int scmi_clk_atomic_enable(struct clk_hw *hw)
{
	struct scmi_clk *clk = to_scmi_clk(hw);

	return scmi_proto_clk_ops->enable(clk->ph, clk->id, ATOMIC);
}

static void scmi_clk_atomic_disable(struct clk_hw *hw)
{
	struct scmi_clk *clk = to_scmi_clk(hw);

	scmi_proto_clk_ops->disable(clk->ph, clk->id, ATOMIC);
}

static int __scmi_clk_is_enabled(struct clk_hw *hw, bool atomic)
{
	int ret;
	bool enabled = false;
	struct scmi_clk *clk = to_scmi_clk(hw);

	ret = scmi_proto_clk_ops->state_get(clk->ph, clk->id, &enabled, atomic);
	if (ret)
		dev_warn(clk->dev,
			 "Failed to get state for clock ID %d\n", clk->id);

	return !!enabled;
}

static int scmi_clk_atomic_is_enabled(struct clk_hw *hw)
{
	return __scmi_clk_is_enabled(hw, ATOMIC);
}

static int scmi_clk_is_enabled(struct clk_hw *hw)
{
	return __scmi_clk_is_enabled(hw, NOT_ATOMIC);
}

static int scmi_clk_get_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	int ret;
	u32 val;
	struct scmi_clk *clk = to_scmi_clk(hw);

	ret = scmi_proto_clk_ops->config_oem_get(clk->ph, clk->id,
						 SCMI_CLOCK_CFG_DUTY_CYCLE,
						 &val, NULL, false);
	if (!ret) {
		duty->num = val;
		duty->den = 100;
	} else {
		dev_warn(clk->dev,
			 "Failed to get duty cycle for clock ID %d\n", clk->id);
	}

	return ret;
}

static int scmi_clk_set_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	int ret;
	u32 val;
	struct scmi_clk *clk = to_scmi_clk(hw);

	/* SCMI OEM Duty Cycle is expressed as a percentage */
	val = (duty->num * 100) / duty->den;
	ret = scmi_proto_clk_ops->config_oem_set(clk->ph, clk->id,
						 SCMI_CLOCK_CFG_DUTY_CYCLE,
						 val, false);
	if (ret)
		dev_warn(clk->dev,
			 "Failed to set duty cycle(%u/%u) for clock ID %d\n",
			 duty->num, duty->den, clk->id);

	return ret;
}

static int scmi_clk_ops_init(struct device *dev, struct scmi_clk *sclk,
			     const struct clk_ops *scmi_ops)
{
	int ret;
	unsigned long min_rate, max_rate;

	struct clk_init_data init = {
		.flags = CLK_GET_RATE_NOCACHE,
		.num_parents = sclk->info->num_parents,
		.ops = scmi_ops,
		.name = sclk->info->name,
		.parent_data = sclk->parent_data,
	};

	sclk->hw.init = &init;
	ret = devm_clk_hw_register(dev, &sclk->hw);
	if (ret)
		return ret;

	if (sclk->info->rate_discrete) {
		int num_rates = sclk->info->list.num_rates;

		if (num_rates <= 0)
			return -EINVAL;

		min_rate = sclk->info->list.rates[0];
		max_rate = sclk->info->list.rates[num_rates - 1];
	} else {
		min_rate = sclk->info->range.min_rate;
		max_rate = sclk->info->range.max_rate;
	}

	clk_hw_set_rate_range(&sclk->hw, min_rate, max_rate);
	return ret;
}

/**
 * scmi_clk_ops_alloc() - Alloc and configure clock operations
 * @dev: A device reference for devres
 * @feats_key: A bitmap representing the desired clk_ops capabilities
 *
 * Allocate and configure a proper set of clock operations depending on the
 * specifically required SCMI clock features.
 *
 * Return: A pointer to the allocated and configured clk_ops on success,
 *	   or NULL on allocation failure.
 */
static const struct clk_ops *
scmi_clk_ops_alloc(struct device *dev, unsigned long feats_key)
{
	struct clk_ops *ops;

	ops = devm_kzalloc(dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return NULL;
	/*
	 * We can provide enable/disable/is_enabled atomic callbacks only if the
	 * underlying SCMI transport for an SCMI instance is configured to
	 * handle SCMI commands in an atomic manner.
	 *
	 * When no SCMI atomic transport support is available we instead provide
	 * only the prepare/unprepare API, as allowed by the clock framework
	 * when atomic calls are not available.
	 */
	if (feats_key & BIT(SCMI_CLK_STATE_CTRL_SUPPORTED)) {
		if (feats_key & BIT(SCMI_CLK_ATOMIC_SUPPORTED)) {
			ops->enable = scmi_clk_atomic_enable;
			ops->disable = scmi_clk_atomic_disable;
		} else {
			ops->prepare = scmi_clk_enable;
			ops->unprepare = scmi_clk_disable;
		}
	}

	if (feats_key & BIT(SCMI_CLK_ATOMIC_SUPPORTED))
		ops->is_enabled = scmi_clk_atomic_is_enabled;
	else
		ops->is_prepared = scmi_clk_is_enabled;

	/* Rate ops */
	ops->recalc_rate = scmi_clk_recalc_rate;
	ops->round_rate = scmi_clk_round_rate;
	ops->determine_rate = scmi_clk_determine_rate;
	if (feats_key & BIT(SCMI_CLK_RATE_CTRL_SUPPORTED))
		ops->set_rate = scmi_clk_set_rate;

	/* Parent ops */
	ops->get_parent = scmi_clk_get_parent;
	if (feats_key & BIT(SCMI_CLK_PARENT_CTRL_SUPPORTED))
		ops->set_parent = scmi_clk_set_parent;

	/* Duty cycle */
	if (feats_key & BIT(SCMI_CLK_DUTY_CYCLE_SUPPORTED)) {
		ops->get_duty_cycle = scmi_clk_get_duty_cycle;
		ops->set_duty_cycle = scmi_clk_set_duty_cycle;
	}

	return ops;
}

/**
 * scmi_clk_ops_select() - Select a proper set of clock operations
 * @sclk: A reference to an SCMI clock descriptor
 * @atomic_capable: A flag to indicate if atomic mode is supported by the
 *		    transport
 * @atomic_threshold_us: Platform atomic threshold value in microseconds:
 *			 clk_ops are atomic when clock enable latency is less
 *			 than this threshold
 * @clk_ops_db: A reference to the array used as a database to store all the
 *		created clock operations combinations.
 * @db_size: Maximum number of entries held by @clk_ops_db
 *
 * After having built a bitmap descriptor to represent the set of features
 * needed by this SCMI clock, at first use it to lookup into the set of
 * previously allocated clk_ops to check if a suitable combination of clock
 * operations was already created; when no match is found allocate a brand new
 * set of clk_ops satisfying the required combination of features and save it
 * for future references.
 *
 * In this way only one set of clk_ops is ever created for each different
 * combination that is effectively needed by a driver instance.
 *
 * Return: A pointer to the allocated and configured clk_ops on success, or
 *	   NULL otherwise.
 */
static const struct clk_ops *
scmi_clk_ops_select(struct scmi_clk *sclk, bool atomic_capable,
		    unsigned int atomic_threshold_us,
		    const struct clk_ops **clk_ops_db, size_t db_size)
{
	const struct scmi_clock_info *ci = sclk->info;
	unsigned int feats_key = 0;
	const struct clk_ops *ops;

	/*
	 * Note that when transport is atomic but SCMI protocol did not
	 * specify (or support) an enable_latency associated with a
	 * clock, we default to use atomic operations mode.
	 */
	if (atomic_capable && ci->enable_latency <= atomic_threshold_us)
		feats_key |= BIT(SCMI_CLK_ATOMIC_SUPPORTED);

	if (!ci->state_ctrl_forbidden)
		feats_key |= BIT(SCMI_CLK_STATE_CTRL_SUPPORTED);

	if (!ci->rate_ctrl_forbidden)
		feats_key |= BIT(SCMI_CLK_RATE_CTRL_SUPPORTED);

	if (!ci->parent_ctrl_forbidden)
		feats_key |= BIT(SCMI_CLK_PARENT_CTRL_SUPPORTED);

	if (ci->extended_config)
		feats_key |= BIT(SCMI_CLK_DUTY_CYCLE_SUPPORTED);

	if (WARN_ON(feats_key >= db_size))
		return NULL;

	/* Lookup previously allocated ops */
	ops = clk_ops_db[feats_key];
	if (ops)
		return ops;

	/* Did not find a pre-allocated clock_ops */
	ops = scmi_clk_ops_alloc(sclk->dev, feats_key);
	if (!ops)
		return NULL;

	/* Store new ops combinations */
	clk_ops_db[feats_key] = ops;

	return ops;
}

static int scmi_clocks_probe(struct scmi_device *sdev)
{
	int idx, count, err;
	unsigned int atomic_threshold_us;
	bool transport_is_atomic;
	struct clk_hw **hws;
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &sdev->dev;
	struct device_node *np = dev->of_node;
	const struct scmi_handle *handle = sdev->handle;
	struct scmi_protocol_handle *ph;
	const struct clk_ops *scmi_clk_ops_db[SCMI_MAX_CLK_OPS] = {};
	struct scmi_clk *sclks;

	if (!handle)
		return -ENODEV;

	scmi_proto_clk_ops =
		handle->devm_protocol_get(sdev, SCMI_PROTOCOL_CLOCK, &ph);
	if (IS_ERR(scmi_proto_clk_ops))
		return PTR_ERR(scmi_proto_clk_ops);

	count = scmi_proto_clk_ops->count_get(ph);
	if (count < 0) {
		dev_err(dev, "%pOFn: invalid clock output count\n", np);
		return -EINVAL;
	}

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, count),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = count;
	hws = clk_data->hws;

	transport_is_atomic = handle->is_transport_atomic(handle,
							  &atomic_threshold_us);

	sclks = devm_kcalloc(dev, count, sizeof(*sclks), GFP_KERNEL);
	if (!sclks)
		return -ENOMEM;

	for (idx = 0; idx < count; idx++)
		hws[idx] = &sclks[idx].hw;

	for (idx = 0; idx < count; idx++) {
		struct scmi_clk *sclk = &sclks[idx];
		const struct clk_ops *scmi_ops;

		sclk->info = scmi_proto_clk_ops->info_get(ph, idx);
		if (!sclk->info) {
			dev_dbg(dev, "invalid clock info for idx %d\n", idx);
			hws[idx] = NULL;
			continue;
		}

		sclk->id = idx;
		sclk->ph = ph;
		sclk->dev = dev;

		/*
		 * Note that the scmi_clk_ops_db is on the stack, not global,
		 * because it cannot be shared between mulitple probe-sequences
		 * to avoid sharing the devm_ allocated clk_ops between multiple
		 * SCMI clk driver instances.
		 */
		scmi_ops = scmi_clk_ops_select(sclk, transport_is_atomic,
					       atomic_threshold_us,
					       scmi_clk_ops_db,
					       ARRAY_SIZE(scmi_clk_ops_db));
		if (!scmi_ops)
			return -ENOMEM;

		/* Initialize clock parent data. */
		if (sclk->info->num_parents > 0) {
			sclk->parent_data = devm_kcalloc(dev, sclk->info->num_parents,
							 sizeof(*sclk->parent_data), GFP_KERNEL);
			if (!sclk->parent_data)
				return -ENOMEM;

			for (int i = 0; i < sclk->info->num_parents; i++) {
				sclk->parent_data[i].index = sclk->info->parents[i];
				sclk->parent_data[i].hw = hws[sclk->info->parents[i]];
			}
		}

		err = scmi_clk_ops_init(dev, sclk, scmi_ops);
		if (err) {
			dev_err(dev, "failed to register clock %d\n", idx);
			devm_kfree(dev, sclk->parent_data);
			hws[idx] = NULL;
		} else {
			dev_dbg(dev, "Registered clock:%s%s\n",
				sclk->info->name,
				scmi_ops->enable ? " (atomic ops)" : "");
		}
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   clk_data);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_CLOCK, "clocks" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_clocks_driver = {
	.name = "scmi-clocks",
	.probe = scmi_clocks_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_clocks_driver);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCMI clock driver");
MODULE_LICENSE("GPL v2");
