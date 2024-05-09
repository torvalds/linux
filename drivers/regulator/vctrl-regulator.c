// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for voltage controller regulators
 *
 * Copyright (C) 2017 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/sort.h>

#include "internal.h"

struct vctrl_voltage_range {
	int min_uV;
	int max_uV;
};

struct vctrl_voltage_ranges {
	struct vctrl_voltage_range ctrl;
	struct vctrl_voltage_range out;
};

struct vctrl_voltage_table {
	int ctrl;
	int out;
	int ovp_min_sel;
};

struct vctrl_data {
	struct regulator_dev *rdev;
	struct regulator_desc desc;
	bool enabled;
	unsigned int min_slew_down_rate;
	unsigned int ovp_threshold;
	struct vctrl_voltage_ranges vrange;
	struct vctrl_voltage_table *vtable;
	unsigned int sel;
};

static int vctrl_calc_ctrl_voltage(struct vctrl_data *vctrl, int out_uV)
{
	struct vctrl_voltage_range *ctrl = &vctrl->vrange.ctrl;
	struct vctrl_voltage_range *out = &vctrl->vrange.out;

	return ctrl->min_uV +
		DIV_ROUND_CLOSEST_ULL((s64)(out_uV - out->min_uV) *
				      (ctrl->max_uV - ctrl->min_uV),
				      out->max_uV - out->min_uV);
}

static int vctrl_calc_output_voltage(struct vctrl_data *vctrl, int ctrl_uV)
{
	struct vctrl_voltage_range *ctrl = &vctrl->vrange.ctrl;
	struct vctrl_voltage_range *out = &vctrl->vrange.out;

	if (ctrl_uV < 0) {
		pr_err("vctrl: failed to get control voltage\n");
		return ctrl_uV;
	}

	if (ctrl_uV < ctrl->min_uV)
		return out->min_uV;

	if (ctrl_uV > ctrl->max_uV)
		return out->max_uV;

	return out->min_uV +
		DIV_ROUND_CLOSEST_ULL((s64)(ctrl_uV - ctrl->min_uV) *
				      (out->max_uV - out->min_uV),
				      ctrl->max_uV - ctrl->min_uV);
}

static int vctrl_get_voltage(struct regulator_dev *rdev)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);
	int ctrl_uV;

	if (!rdev->supply)
		return -EPROBE_DEFER;

	ctrl_uV = regulator_get_voltage_rdev(rdev->supply->rdev);

	return vctrl_calc_output_voltage(vctrl, ctrl_uV);
}

static int vctrl_set_voltage(struct regulator_dev *rdev,
			     int req_min_uV, int req_max_uV,
			     unsigned int *selector)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);
	int orig_ctrl_uV;
	int uV;
	int ret;

	if (!rdev->supply)
		return -EPROBE_DEFER;

	orig_ctrl_uV = regulator_get_voltage_rdev(rdev->supply->rdev);
	uV = vctrl_calc_output_voltage(vctrl, orig_ctrl_uV);

	if (req_min_uV >= uV || !vctrl->ovp_threshold)
		/* voltage rising or no OVP */
		return regulator_set_voltage_rdev(rdev->supply->rdev,
			vctrl_calc_ctrl_voltage(vctrl, req_min_uV),
			vctrl_calc_ctrl_voltage(vctrl, req_max_uV),
			PM_SUSPEND_ON);

	while (uV > req_min_uV) {
		int max_drop_uV = (uV * vctrl->ovp_threshold) / 100;
		int next_uV;
		int next_ctrl_uV;
		int delay;

		/* Make sure no infinite loop even in crazy cases */
		if (max_drop_uV == 0)
			max_drop_uV = 1;

		next_uV = max_t(int, req_min_uV, uV - max_drop_uV);
		next_ctrl_uV = vctrl_calc_ctrl_voltage(vctrl, next_uV);

		ret = regulator_set_voltage_rdev(rdev->supply->rdev,
					    next_ctrl_uV,
					    next_ctrl_uV,
					    PM_SUSPEND_ON);
		if (ret)
			goto err;

		delay = DIV_ROUND_UP(uV - next_uV, vctrl->min_slew_down_rate);
		usleep_range(delay, delay + DIV_ROUND_UP(delay, 10));

		uV = next_uV;
	}

	return 0;

err:
	/* Try to go back to original voltage */
	regulator_set_voltage_rdev(rdev->supply->rdev, orig_ctrl_uV, orig_ctrl_uV,
				   PM_SUSPEND_ON);

	return ret;
}

static int vctrl_get_voltage_sel(struct regulator_dev *rdev)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);

	return vctrl->sel;
}

static int vctrl_set_voltage_sel(struct regulator_dev *rdev,
				 unsigned int selector)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);
	unsigned int orig_sel = vctrl->sel;
	int ret;

	if (!rdev->supply)
		return -EPROBE_DEFER;

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	if (selector >= vctrl->sel || !vctrl->ovp_threshold) {
		/* voltage rising or no OVP */
		ret = regulator_set_voltage_rdev(rdev->supply->rdev,
					    vctrl->vtable[selector].ctrl,
					    vctrl->vtable[selector].ctrl,
					    PM_SUSPEND_ON);
		if (!ret)
			vctrl->sel = selector;

		return ret;
	}

	while (vctrl->sel != selector) {
		unsigned int next_sel;
		int delay;

		next_sel = max_t(unsigned int, selector, vctrl->vtable[vctrl->sel].ovp_min_sel);

		ret = regulator_set_voltage_rdev(rdev->supply->rdev,
					    vctrl->vtable[next_sel].ctrl,
					    vctrl->vtable[next_sel].ctrl,
					    PM_SUSPEND_ON);
		if (ret) {
			dev_err(&rdev->dev,
				"failed to set control voltage to %duV\n",
				vctrl->vtable[next_sel].ctrl);
			goto err;
		}
		vctrl->sel = next_sel;

		delay = DIV_ROUND_UP(vctrl->vtable[vctrl->sel].out -
				     vctrl->vtable[next_sel].out,
				     vctrl->min_slew_down_rate);
		usleep_range(delay, delay + DIV_ROUND_UP(delay, 10));
	}

	return 0;

err:
	if (vctrl->sel != orig_sel) {
		/* Try to go back to original voltage */
		if (!regulator_set_voltage_rdev(rdev->supply->rdev,
					   vctrl->vtable[orig_sel].ctrl,
					   vctrl->vtable[orig_sel].ctrl,
					   PM_SUSPEND_ON))
			vctrl->sel = orig_sel;
		else
			dev_warn(&rdev->dev,
				 "failed to restore original voltage\n");
	}

	return ret;
}

static int vctrl_list_voltage(struct regulator_dev *rdev,
			      unsigned int selector)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	return vctrl->vtable[selector].out;
}

static int vctrl_parse_dt(struct platform_device *pdev,
			  struct vctrl_data *vctrl)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;
	u32 pval;
	u32 vrange_ctrl[2];

	ret = of_property_read_u32(np, "ovp-threshold-percent", &pval);
	if (!ret) {
		vctrl->ovp_threshold = pval;
		if (vctrl->ovp_threshold > 100) {
			dev_err(&pdev->dev,
				"ovp-threshold-percent (%u) > 100\n",
				vctrl->ovp_threshold);
			return -EINVAL;
		}
	}

	ret = of_property_read_u32(np, "min-slew-down-rate", &pval);
	if (!ret) {
		vctrl->min_slew_down_rate = pval;

		/* We use the value as int and as divider; sanity check */
		if (vctrl->min_slew_down_rate == 0) {
			dev_err(&pdev->dev,
				"min-slew-down-rate must not be 0\n");
			return -EINVAL;
		} else if (vctrl->min_slew_down_rate > INT_MAX) {
			dev_err(&pdev->dev, "min-slew-down-rate (%u) too big\n",
				vctrl->min_slew_down_rate);
			return -EINVAL;
		}
	}

	if (vctrl->ovp_threshold && !vctrl->min_slew_down_rate) {
		dev_err(&pdev->dev,
			"ovp-threshold-percent requires min-slew-down-rate\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "regulator-min-microvolt", &pval);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to read regulator-min-microvolt: %d\n", ret);
		return ret;
	}
	vctrl->vrange.out.min_uV = pval;

	ret = of_property_read_u32(np, "regulator-max-microvolt", &pval);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to read regulator-max-microvolt: %d\n", ret);
		return ret;
	}
	vctrl->vrange.out.max_uV = pval;

	ret = of_property_read_u32_array(np, "ctrl-voltage-range", vrange_ctrl,
					 2);
	if (ret) {
		dev_err(&pdev->dev, "failed to read ctrl-voltage-range: %d\n",
			ret);
		return ret;
	}

	if (vrange_ctrl[0] >= vrange_ctrl[1]) {
		dev_err(&pdev->dev, "ctrl-voltage-range is invalid: %d-%d\n",
			vrange_ctrl[0], vrange_ctrl[1]);
		return -EINVAL;
	}

	vctrl->vrange.ctrl.min_uV = vrange_ctrl[0];
	vctrl->vrange.ctrl.max_uV = vrange_ctrl[1];

	return 0;
}

static int vctrl_cmp_ctrl_uV(const void *a, const void *b)
{
	const struct vctrl_voltage_table *at = a;
	const struct vctrl_voltage_table *bt = b;

	return at->ctrl - bt->ctrl;
}

static int vctrl_init_vtable(struct platform_device *pdev,
			     struct regulator *ctrl_reg)
{
	struct vctrl_data *vctrl = platform_get_drvdata(pdev);
	struct regulator_desc *rdesc = &vctrl->desc;
	struct vctrl_voltage_range *vrange_ctrl = &vctrl->vrange.ctrl;
	int n_voltages;
	int ctrl_uV;
	int i, idx_vt;

	n_voltages = regulator_count_voltages(ctrl_reg);

	rdesc->n_voltages = n_voltages;

	/* determine number of steps within the range of the vctrl regulator */
	for (i = 0; i < n_voltages; i++) {
		ctrl_uV = regulator_list_voltage(ctrl_reg, i);

		if (ctrl_uV < vrange_ctrl->min_uV ||
		    ctrl_uV > vrange_ctrl->max_uV)
			rdesc->n_voltages--;
	}

	if (rdesc->n_voltages == 0) {
		dev_err(&pdev->dev, "invalid configuration\n");
		return -EINVAL;
	}

	vctrl->vtable = devm_kcalloc(&pdev->dev, rdesc->n_voltages,
				     sizeof(struct vctrl_voltage_table),
				     GFP_KERNEL);
	if (!vctrl->vtable)
		return -ENOMEM;

	/* create mapping control <=> output voltage */
	for (i = 0, idx_vt = 0; i < n_voltages; i++) {
		ctrl_uV = regulator_list_voltage(ctrl_reg, i);

		if (ctrl_uV < vrange_ctrl->min_uV ||
		    ctrl_uV > vrange_ctrl->max_uV)
			continue;

		vctrl->vtable[idx_vt].ctrl = ctrl_uV;
		vctrl->vtable[idx_vt].out =
			vctrl_calc_output_voltage(vctrl, ctrl_uV);
		idx_vt++;
	}

	/* we rely on the table to be ordered by ascending voltage */
	sort(vctrl->vtable, rdesc->n_voltages,
	     sizeof(struct vctrl_voltage_table), vctrl_cmp_ctrl_uV,
	     NULL);

	/* pre-calculate OVP-safe downward transitions */
	for (i = rdesc->n_voltages - 1; i > 0; i--) {
		int j;
		int ovp_min_uV = (vctrl->vtable[i].out *
				  (100 - vctrl->ovp_threshold)) / 100;

		for (j = 0; j < i; j++) {
			if (vctrl->vtable[j].out >= ovp_min_uV) {
				vctrl->vtable[i].ovp_min_sel = j;
				break;
			}
		}

		if (j == i) {
			dev_warn(&pdev->dev, "switching down from %duV may cause OVP shutdown\n",
				vctrl->vtable[i].out);
			/* use next lowest voltage */
			vctrl->vtable[i].ovp_min_sel = i - 1;
		}
	}

	return 0;
}

static int vctrl_enable(struct regulator_dev *rdev)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);

	vctrl->enabled = true;

	return 0;
}

static int vctrl_disable(struct regulator_dev *rdev)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);

	vctrl->enabled = false;

	return 0;
}

static int vctrl_is_enabled(struct regulator_dev *rdev)
{
	struct vctrl_data *vctrl = rdev_get_drvdata(rdev);

	return vctrl->enabled;
}

static const struct regulator_ops vctrl_ops_cont = {
	.enable		  = vctrl_enable,
	.disable	  = vctrl_disable,
	.is_enabled	  = vctrl_is_enabled,
	.get_voltage	  = vctrl_get_voltage,
	.set_voltage	  = vctrl_set_voltage,
};

static const struct regulator_ops vctrl_ops_non_cont = {
	.enable		  = vctrl_enable,
	.disable	  = vctrl_disable,
	.is_enabled	  = vctrl_is_enabled,
	.set_voltage_sel = vctrl_set_voltage_sel,
	.get_voltage_sel = vctrl_get_voltage_sel,
	.list_voltage    = vctrl_list_voltage,
	.map_voltage     = regulator_map_voltage_iterate,
};

static int vctrl_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct vctrl_data *vctrl;
	const struct regulator_init_data *init_data;
	struct regulator_desc *rdesc;
	struct regulator_config cfg = { };
	struct vctrl_voltage_range *vrange_ctrl;
	struct regulator *ctrl_reg;
	int ctrl_uV;
	int ret;

	vctrl = devm_kzalloc(&pdev->dev, sizeof(struct vctrl_data),
			     GFP_KERNEL);
	if (!vctrl)
		return -ENOMEM;

	platform_set_drvdata(pdev, vctrl);

	ret = vctrl_parse_dt(pdev, vctrl);
	if (ret)
		return ret;

	ctrl_reg = devm_regulator_get(&pdev->dev, "ctrl");
	if (IS_ERR(ctrl_reg))
		return PTR_ERR(ctrl_reg);

	vrange_ctrl = &vctrl->vrange.ctrl;

	rdesc = &vctrl->desc;
	rdesc->name = "vctrl";
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->owner = THIS_MODULE;
	rdesc->supply_name = "ctrl";

	if ((regulator_get_linear_step(ctrl_reg) == 1) ||
	    (regulator_count_voltages(ctrl_reg) == -EINVAL)) {
		rdesc->continuous_voltage_range = true;
		rdesc->ops = &vctrl_ops_cont;
	} else {
		rdesc->ops = &vctrl_ops_non_cont;
	}

	init_data = of_get_regulator_init_data(&pdev->dev, np, rdesc);
	if (!init_data)
		return -ENOMEM;

	cfg.of_node = np;
	cfg.dev = &pdev->dev;
	cfg.driver_data = vctrl;
	cfg.init_data = init_data;

	if (!rdesc->continuous_voltage_range) {
		ret = vctrl_init_vtable(pdev, ctrl_reg);
		if (ret)
			return ret;

		/* Use locked consumer API when not in regulator framework */
		ctrl_uV = regulator_get_voltage(ctrl_reg);
		if (ctrl_uV < 0) {
			dev_err(&pdev->dev, "failed to get control voltage\n");
			return ctrl_uV;
		}

		/* determine current voltage selector from control voltage */
		if (ctrl_uV < vrange_ctrl->min_uV) {
			vctrl->sel = 0;
		} else if (ctrl_uV > vrange_ctrl->max_uV) {
			vctrl->sel = rdesc->n_voltages - 1;
		} else {
			int i;

			for (i = 0; i < rdesc->n_voltages; i++) {
				if (ctrl_uV == vctrl->vtable[i].ctrl) {
					vctrl->sel = i;
					break;
				}
			}
		}
	}

	/* Drop ctrl-supply here in favor of regulator core managed supply */
	devm_regulator_put(ctrl_reg);

	vctrl->rdev = devm_regulator_register(&pdev->dev, rdesc, &cfg);
	if (IS_ERR(vctrl->rdev)) {
		ret = PTR_ERR(vctrl->rdev);
		dev_err(&pdev->dev, "failed to register regulator: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id vctrl_of_match[] = {
	{ .compatible = "vctrl-regulator", },
	{},
};
MODULE_DEVICE_TABLE(of, vctrl_of_match);

static struct platform_driver vctrl_driver = {
	.probe		= vctrl_probe,
	.driver		= {
		.name		= "vctrl-regulator",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(vctrl_of_match),
	},
};

module_platform_driver(vctrl_driver);

MODULE_DESCRIPTION("Voltage Controlled Regulator Driver");
MODULE_AUTHOR("Matthias Kaehlcke <mka@chromium.org>");
MODULE_LICENSE("GPL v2");
