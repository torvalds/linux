// SPDX-License-Identifier: GPL-2.0
//
// System Control and Management Interface (SCMI) based regulator driver
//
// Copyright (C) 2020-2021 ARM Ltd.
//
// Implements a regulator driver on top of the SCMI Voltage Protocol.
//
// The ARM SCMI Protocol aims in general to hide as much as possible all the
// underlying operational details while providing an abstracted interface for
// its users to operate upon: as a consequence the resulting operational
// capabilities and configurability of this regulator device are much more
// limited than the ones usually available on a standard physical regulator.
//
// The supported SCMI regulator ops are restricted to the bare minimum:
//
//  - 'status_ops': enable/disable/is_enabled
//  - 'voltage_ops': get_voltage_sel/set_voltage_sel
//		     list_voltage/map_voltage
//
// Each SCMI regulator instance is associated, through the means of a proper DT
// entry description, to a specific SCMI Voltage Domain.

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>
#include <linux/types.h>

static const struct scmi_voltage_proto_ops *voltage_ops;

struct scmi_regulator {
	u32 id;
	struct scmi_device *sdev;
	struct scmi_protocol_handle *ph;
	struct regulator_dev *rdev;
	struct device_node *of_node;
	struct regulator_desc desc;
	struct regulator_config conf;
};

struct scmi_regulator_info {
	int num_doms;
	struct scmi_regulator **sregv;
};

static int scmi_reg_enable(struct regulator_dev *rdev)
{
	struct scmi_regulator *sreg = rdev_get_drvdata(rdev);

	return voltage_ops->config_set(sreg->ph, sreg->id,
				       SCMI_VOLTAGE_ARCH_STATE_ON);
}

static int scmi_reg_disable(struct regulator_dev *rdev)
{
	struct scmi_regulator *sreg = rdev_get_drvdata(rdev);

	return voltage_ops->config_set(sreg->ph, sreg->id,
				       SCMI_VOLTAGE_ARCH_STATE_OFF);
}

static int scmi_reg_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	u32 config;
	struct scmi_regulator *sreg = rdev_get_drvdata(rdev);

	ret = voltage_ops->config_get(sreg->ph, sreg->id, &config);
	if (ret) {
		dev_err(&sreg->sdev->dev,
			"Error %d reading regulator %s status.\n",
			ret, sreg->desc.name);
		return ret;
	}

	return config & SCMI_VOLTAGE_ARCH_STATE_ON;
}

static int scmi_reg_get_voltage_sel(struct regulator_dev *rdev)
{
	int ret;
	s32 volt_uV;
	struct scmi_regulator *sreg = rdev_get_drvdata(rdev);

	ret = voltage_ops->level_get(sreg->ph, sreg->id, &volt_uV);
	if (ret)
		return ret;

	return sreg->desc.ops->map_voltage(rdev, volt_uV, volt_uV);
}

static int scmi_reg_set_voltage_sel(struct regulator_dev *rdev,
				    unsigned int selector)
{
	s32 volt_uV;
	struct scmi_regulator *sreg = rdev_get_drvdata(rdev);

	volt_uV = sreg->desc.ops->list_voltage(rdev, selector);
	if (volt_uV <= 0)
		return -EINVAL;

	return voltage_ops->level_set(sreg->ph, sreg->id, 0x0, volt_uV);
}

static const struct regulator_ops scmi_reg_fixed_ops = {
	.enable = scmi_reg_enable,
	.disable = scmi_reg_disable,
	.is_enabled = scmi_reg_is_enabled,
};

static const struct regulator_ops scmi_reg_linear_ops = {
	.enable = scmi_reg_enable,
	.disable = scmi_reg_disable,
	.is_enabled = scmi_reg_is_enabled,
	.get_voltage_sel = scmi_reg_get_voltage_sel,
	.set_voltage_sel = scmi_reg_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
};

static const struct regulator_ops scmi_reg_discrete_ops = {
	.enable = scmi_reg_enable,
	.disable = scmi_reg_disable,
	.is_enabled = scmi_reg_is_enabled,
	.get_voltage_sel = scmi_reg_get_voltage_sel,
	.set_voltage_sel = scmi_reg_set_voltage_sel,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
};

static int
scmi_config_linear_regulator_mappings(struct scmi_regulator *sreg,
				      const struct scmi_voltage_info *vinfo)
{
	s32 delta_uV;

	/*
	 * Note that SCMI voltage domains describable by linear ranges
	 * (segments) {low, high, step} are guaranteed to come in one single
	 * triplet by the SCMI Voltage Domain protocol support itself.
	 */

	delta_uV = (vinfo->levels_uv[SCMI_VOLTAGE_SEGMENT_HIGH] -
			vinfo->levels_uv[SCMI_VOLTAGE_SEGMENT_LOW]);

	/* Rule out buggy negative-intervals answers from fw */
	if (delta_uV < 0) {
		dev_err(&sreg->sdev->dev,
			"Invalid volt-range %d-%duV for domain %d\n",
			vinfo->levels_uv[SCMI_VOLTAGE_SEGMENT_LOW],
			vinfo->levels_uv[SCMI_VOLTAGE_SEGMENT_HIGH],
			sreg->id);
		return -EINVAL;
	}

	if (!delta_uV) {
		/* Just one fixed voltage exposed by SCMI */
		sreg->desc.fixed_uV =
			vinfo->levels_uv[SCMI_VOLTAGE_SEGMENT_LOW];
		sreg->desc.n_voltages = 1;
		sreg->desc.ops = &scmi_reg_fixed_ops;
	} else {
		/* One simple linear mapping. */
		sreg->desc.min_uV =
			vinfo->levels_uv[SCMI_VOLTAGE_SEGMENT_LOW];
		sreg->desc.uV_step =
			vinfo->levels_uv[SCMI_VOLTAGE_SEGMENT_STEP];
		sreg->desc.linear_min_sel = 0;
		sreg->desc.n_voltages = (delta_uV / sreg->desc.uV_step) + 1;
		sreg->desc.ops = &scmi_reg_linear_ops;
	}

	return 0;
}

static int
scmi_config_discrete_regulator_mappings(struct scmi_regulator *sreg,
					const struct scmi_voltage_info *vinfo)
{
	/* Discrete non linear levels are mapped to volt_table */
	sreg->desc.n_voltages = vinfo->num_levels;

	if (sreg->desc.n_voltages > 1) {
		sreg->desc.volt_table = (const unsigned int *)vinfo->levels_uv;
		sreg->desc.ops = &scmi_reg_discrete_ops;
	} else {
		sreg->desc.fixed_uV = vinfo->levels_uv[0];
		sreg->desc.ops = &scmi_reg_fixed_ops;
	}

	return 0;
}

static int scmi_regulator_common_init(struct scmi_regulator *sreg)
{
	int ret;
	struct device *dev = &sreg->sdev->dev;
	const struct scmi_voltage_info *vinfo;

	vinfo = voltage_ops->info_get(sreg->ph, sreg->id);
	if (!vinfo) {
		dev_warn(dev, "Failure to get voltage domain %d\n",
			 sreg->id);
		return -ENODEV;
	}

	/*
	 * Regulator framework does not fully support negative voltages
	 * so we discard any voltage domain reported as supporting negative
	 * voltages: as a consequence each levels_uv entry is guaranteed to
	 * be non-negative from here on.
	 */
	if (vinfo->negative_volts_allowed) {
		dev_warn(dev, "Negative voltages NOT supported...skip %s\n",
			 sreg->of_node->full_name);
		return -EOPNOTSUPP;
	}

	sreg->desc.name = devm_kasprintf(dev, GFP_KERNEL, "%s", vinfo->name);
	if (!sreg->desc.name)
		return -ENOMEM;

	sreg->desc.id = sreg->id;
	sreg->desc.type = REGULATOR_VOLTAGE;
	sreg->desc.owner = THIS_MODULE;
	sreg->desc.of_match_full_name = true;
	sreg->desc.of_match = sreg->of_node->full_name;
	sreg->desc.regulators_node = "regulators";
	if (vinfo->segmented)
		ret = scmi_config_linear_regulator_mappings(sreg, vinfo);
	else
		ret = scmi_config_discrete_regulator_mappings(sreg, vinfo);
	if (ret)
		return ret;

	/*
	 * Using the scmi device here to have DT searched from Voltage
	 * protocol node down.
	 */
	sreg->conf.dev = dev;

	/* Store for later retrieval via rdev_get_drvdata() */
	sreg->conf.driver_data = sreg;

	return 0;
}

static int process_scmi_regulator_of_node(struct scmi_device *sdev,
					  struct scmi_protocol_handle *ph,
					  struct device_node *np,
					  struct scmi_regulator_info *rinfo)
{
	u32 dom, ret;

	ret = of_property_read_u32(np, "reg", &dom);
	if (ret)
		return ret;

	if (dom >= rinfo->num_doms)
		return -ENODEV;

	if (rinfo->sregv[dom]) {
		dev_err(&sdev->dev,
			"SCMI Voltage Domain %d already in use. Skipping: %s\n",
			dom, np->full_name);
		return -EINVAL;
	}

	rinfo->sregv[dom] = devm_kzalloc(&sdev->dev,
					 sizeof(struct scmi_regulator),
					 GFP_KERNEL);
	if (!rinfo->sregv[dom])
		return -ENOMEM;

	rinfo->sregv[dom]->id = dom;
	rinfo->sregv[dom]->sdev = sdev;
	rinfo->sregv[dom]->ph = ph;

	/* get hold of good nodes */
	of_node_get(np);
	rinfo->sregv[dom]->of_node = np;

	dev_dbg(&sdev->dev,
		"Found SCMI Regulator entry -- OF node [%d] -> %s\n",
		dom, np->full_name);

	return 0;
}

static int scmi_regulator_probe(struct scmi_device *sdev)
{
	int d, ret, num_doms;
	struct device_node *np, *child;
	const struct scmi_handle *handle = sdev->handle;
	struct scmi_regulator_info *rinfo;
	struct scmi_protocol_handle *ph;

	if (!handle)
		return -ENODEV;

	voltage_ops = handle->devm_protocol_get(sdev,
						SCMI_PROTOCOL_VOLTAGE, &ph);
	if (IS_ERR(voltage_ops))
		return PTR_ERR(voltage_ops);

	num_doms = voltage_ops->num_domains_get(ph);
	if (num_doms <= 0) {
		if (!num_doms) {
			dev_err(&sdev->dev,
				"number of voltage domains invalid\n");
			num_doms = -EINVAL;
		} else {
			dev_err(&sdev->dev,
				"failed to get voltage domains - err:%d\n",
				num_doms);
		}

		return num_doms;
	}

	rinfo = devm_kzalloc(&sdev->dev, sizeof(*rinfo), GFP_KERNEL);
	if (!rinfo)
		return -ENOMEM;

	/* Allocate pointers array for all possible domains */
	rinfo->sregv = devm_kcalloc(&sdev->dev, num_doms,
				    sizeof(void *), GFP_KERNEL);
	if (!rinfo->sregv)
		return -ENOMEM;

	rinfo->num_doms = num_doms;

	/*
	 * Start collecting into rinfo->sregv possibly good SCMI Regulators as
	 * described by a well-formed DT entry and associated with an existing
	 * plausible SCMI Voltage Domain number, all belonging to this SCMI
	 * platform instance node (handle->dev->of_node).
	 */
	np = of_find_node_by_name(handle->dev->of_node, "regulators");
	for_each_child_of_node(np, child) {
		ret = process_scmi_regulator_of_node(sdev, ph, child, rinfo);
		/* abort on any mem issue */
		if (ret == -ENOMEM) {
			of_node_put(child);
			return ret;
		}
	}
	of_node_put(np);
	/*
	 * Register a regulator for each valid regulator-DT-entry that we
	 * can successfully reach via SCMI and has a valid associated voltage
	 * domain.
	 */
	for (d = 0; d < num_doms; d++) {
		struct scmi_regulator *sreg = rinfo->sregv[d];

		/* Skip empty slots */
		if (!sreg)
			continue;

		ret = scmi_regulator_common_init(sreg);
		/* Skip invalid voltage domains */
		if (ret)
			continue;

		sreg->rdev = devm_regulator_register(&sdev->dev, &sreg->desc,
						     &sreg->conf);
		if (IS_ERR(sreg->rdev)) {
			sreg->rdev = NULL;
			continue;
		}

		dev_info(&sdev->dev,
			 "Regulator %s registered for domain [%d]\n",
			 sreg->desc.name, sreg->id);
	}

	dev_set_drvdata(&sdev->dev, rinfo);

	return 0;
}

static void scmi_regulator_remove(struct scmi_device *sdev)
{
	int d;
	struct scmi_regulator_info *rinfo;

	rinfo = dev_get_drvdata(&sdev->dev);
	if (!rinfo)
		return;

	for (d = 0; d < rinfo->num_doms; d++) {
		if (!rinfo->sregv[d])
			continue;
		of_node_put(rinfo->sregv[d]->of_node);
	}
}

static const struct scmi_device_id scmi_regulator_id_table[] = {
	{ SCMI_PROTOCOL_VOLTAGE,  "regulator" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_regulator_id_table);

static struct scmi_driver scmi_drv = {
	.name		= "scmi-regulator",
	.probe		= scmi_regulator_probe,
	.remove		= scmi_regulator_remove,
	.id_table	= scmi_regulator_id_table,
};

module_scmi_driver(scmi_drv);

MODULE_AUTHOR("Cristian Marussi <cristian.marussi@arm.com>");
MODULE_DESCRIPTION("ARM SCMI regulator driver");
MODULE_LICENSE("GPL v2");
