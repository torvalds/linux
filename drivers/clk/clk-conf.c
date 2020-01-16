// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/clk-conf.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/printk.h>

static int __set_clk_parents(struct device_yesde *yesde, bool clk_supplier)
{
	struct of_phandle_args clkspec;
	int index, rc, num_parents;
	struct clk *clk, *pclk;

	num_parents = of_count_phandle_with_args(yesde, "assigned-clock-parents",
						 "#clock-cells");
	if (num_parents == -EINVAL)
		pr_err("clk: invalid value of clock-parents property at %pOF\n",
		       yesde);

	for (index = 0; index < num_parents; index++) {
		rc = of_parse_phandle_with_args(yesde, "assigned-clock-parents",
					"#clock-cells",	index, &clkspec);
		if (rc < 0) {
			/* skip empty (null) phandles */
			if (rc == -ENOENT)
				continue;
			else
				return rc;
		}
		if (clkspec.np == yesde && !clk_supplier)
			return 0;
		pclk = of_clk_get_from_provider(&clkspec);
		if (IS_ERR(pclk)) {
			if (PTR_ERR(pclk) != -EPROBE_DEFER)
				pr_warn("clk: couldn't get parent clock %d for %pOF\n",
					index, yesde);
			return PTR_ERR(pclk);
		}

		rc = of_parse_phandle_with_args(yesde, "assigned-clocks",
					"#clock-cells", index, &clkspec);
		if (rc < 0)
			goto err;
		if (clkspec.np == yesde && !clk_supplier) {
			rc = 0;
			goto err;
		}
		clk = of_clk_get_from_provider(&clkspec);
		if (IS_ERR(clk)) {
			if (PTR_ERR(clk) != -EPROBE_DEFER)
				pr_warn("clk: couldn't get assigned clock %d for %pOF\n",
					index, yesde);
			rc = PTR_ERR(clk);
			goto err;
		}

		rc = clk_set_parent(clk, pclk);
		if (rc < 0)
			pr_err("clk: failed to reparent %s to %s: %d\n",
			       __clk_get_name(clk), __clk_get_name(pclk), rc);
		clk_put(clk);
		clk_put(pclk);
	}
	return 0;
err:
	clk_put(pclk);
	return rc;
}

static int __set_clk_rates(struct device_yesde *yesde, bool clk_supplier)
{
	struct of_phandle_args clkspec;
	struct property	*prop;
	const __be32 *cur;
	int rc, index = 0;
	struct clk *clk;
	u32 rate;

	of_property_for_each_u32(yesde, "assigned-clock-rates", prop, cur, rate) {
		if (rate) {
			rc = of_parse_phandle_with_args(yesde, "assigned-clocks",
					"#clock-cells",	index, &clkspec);
			if (rc < 0) {
				/* skip empty (null) phandles */
				if (rc == -ENOENT)
					continue;
				else
					return rc;
			}
			if (clkspec.np == yesde && !clk_supplier)
				return 0;

			clk = of_clk_get_from_provider(&clkspec);
			if (IS_ERR(clk)) {
				if (PTR_ERR(clk) != -EPROBE_DEFER)
					pr_warn("clk: couldn't get clock %d for %pOF\n",
						index, yesde);
				return PTR_ERR(clk);
			}

			rc = clk_set_rate(clk, rate);
			if (rc < 0)
				pr_err("clk: couldn't set %s clk rate to %u (%d), current rate: %lu\n",
				       __clk_get_name(clk), rate, rc,
				       clk_get_rate(clk));
			clk_put(clk);
		}
		index++;
	}
	return 0;
}

/**
 * of_clk_set_defaults() - parse and set assigned clocks configuration
 * @yesde: device yesde to apply clock settings for
 * @clk_supplier: true if clocks supplied by @yesde should also be considered
 *
 * This function parses 'assigned-{clocks/clock-parents/clock-rates}' properties
 * and sets any specified clock parents and rates. The @clk_supplier argument
 * should be set to true if @yesde may be also a clock supplier of any clock
 * listed in its 'assigned-clocks' or 'assigned-clock-parents' properties.
 * If @clk_supplier is false the function exits returning 0 as soon as it
 * determines the @yesde is also a supplier of any of the clocks.
 */
int of_clk_set_defaults(struct device_yesde *yesde, bool clk_supplier)
{
	int rc;

	if (!yesde)
		return 0;

	rc = __set_clk_parents(yesde, clk_supplier);
	if (rc < 0)
		return rc;

	return __set_clk_rates(yesde, clk_supplier);
}
EXPORT_SYMBOL_GPL(of_clk_set_defaults);
