// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>

#include <dt-bindings/regulator/qcom,rpmh-regulator.h>

/**
 * enum rpmh_regulator_type - supported RPMh accelerator types
 * @VRM:	RPMh VRM accelerator which supports voting on enable, voltage,
 *		and mode of LDO, SMPS, and BOB type PMIC regulators.
 * @XOB:	RPMh XOB accelerator which supports voting on the enable state
 *		of PMIC regulators.
 */
enum rpmh_regulator_type {
	VRM,
	XOB,
};

#define RPMH_REGULATOR_REG_VRM_VOLTAGE		0x0
#define RPMH_REGULATOR_REG_ENABLE		0x4
#define RPMH_REGULATOR_REG_VRM_MODE		0x8

#define PMIC4_LDO_MODE_RETENTION		4
#define PMIC4_LDO_MODE_LPM			5
#define PMIC4_LDO_MODE_HPM			7

#define PMIC4_SMPS_MODE_RETENTION		4
#define PMIC4_SMPS_MODE_PFM			5
#define PMIC4_SMPS_MODE_AUTO			6
#define PMIC4_SMPS_MODE_PWM			7

#define PMIC4_BOB_MODE_PASS			0
#define PMIC4_BOB_MODE_PFM			1
#define PMIC4_BOB_MODE_AUTO			2
#define PMIC4_BOB_MODE_PWM			3

#define PMIC5_LDO_MODE_RETENTION		3
#define PMIC5_LDO_MODE_LPM			4
#define PMIC5_LDO_MODE_HPM			7

#define PMIC5_SMPS_MODE_RETENTION		3
#define PMIC5_SMPS_MODE_PFM			4
#define PMIC5_SMPS_MODE_AUTO			6
#define PMIC5_SMPS_MODE_PWM			7

#define PMIC5_BOB_MODE_PASS			2
#define PMIC5_BOB_MODE_PFM			4
#define PMIC5_BOB_MODE_AUTO			6
#define PMIC5_BOB_MODE_PWM			7

/**
 * struct rpmh_vreg_hw_data - RPMh regulator hardware configurations
 * @regulator_type:		RPMh accelerator type used to manage this
 *				regulator
 * @ops:			Pointer to regulator ops callback structure
 * @voltage_range:		The single range of voltages supported by this
 *				PMIC regulator type
 * @n_voltages:			The number of unique voltage set points defined
 *				by voltage_range
 * @hpm_min_load_uA:		Minimum load current in microamps that requires
 *				high power mode (HPM) operation.  This is used
 *				for LDO hardware type regulators only.
 * @pmic_mode_map:		Array indexed by regulator framework mode
 *				containing PMIC hardware modes.  Must be large
 *				enough to index all framework modes supported
 *				by this regulator hardware type.
 * @of_map_mode:		Maps an RPMH_REGULATOR_MODE_* mode value defined
 *				in device tree to a regulator framework mode
 */
struct rpmh_vreg_hw_data {
	enum rpmh_regulator_type		regulator_type;
	const struct regulator_ops		*ops;
	const struct linear_range	voltage_range;
	int					n_voltages;
	int					hpm_min_load_uA;
	const int				*pmic_mode_map;
	unsigned int			      (*of_map_mode)(unsigned int mode);
};

/**
 * struct rpmh_vreg - individual RPMh regulator data structure encapsulating a
 *		single regulator device
 * @dev:			Device pointer for the top-level PMIC RPMh
 *				regulator parent device.  This is used as a
 *				handle in RPMh write requests.
 * @addr:			Base address of the regulator resource within
 *				an RPMh accelerator
 * @rdesc:			Regulator descriptor
 * @hw_data:			PMIC regulator configuration data for this RPMh
 *				regulator
 * @always_wait_for_ack:	Boolean flag indicating if a request must always
 *				wait for an ACK from RPMh before continuing even
 *				if it corresponds to a strictly lower power
 *				state (e.g. enabled --> disabled).
 * @enabled:			Flag indicating if the regulator is enabled or
 *				not
 * @bypassed:			Boolean indicating if the regulator is in
 *				bypass (pass-through) mode or not.  This is
 *				only used by BOB rpmh-regulator resources.
 * @voltage_selector:		Selector used for get_voltage_sel() and
 *				set_voltage_sel() callbacks
 * @mode:			RPMh VRM regulator current framework mode
 */
struct rpmh_vreg {
	struct device			*dev;
	u32				addr;
	struct regulator_desc		rdesc;
	const struct rpmh_vreg_hw_data	*hw_data;
	bool				always_wait_for_ack;

	int				enabled;
	bool				bypassed;
	int				voltage_selector;
	unsigned int			mode;
};

/**
 * struct rpmh_vreg_init_data - initialization data for an RPMh regulator
 * @name:			Name for the regulator which also corresponds
 *				to the device tree subnode name of the regulator
 * @resource_name:		RPMh regulator resource name format string.
 *				This must include exactly one field: '%s' which
 *				is filled at run-time with the PMIC ID provided
 *				by device tree property qcom,pmic-id.  Example:
 *				"ldo%s1" for RPMh resource "ldoa1".
 * @supply_name:		Parent supply regulator name
 * @hw_data:			Configuration data for this PMIC regulator type
 */
struct rpmh_vreg_init_data {
	const char			*name;
	const char			*resource_name;
	const char			*supply_name;
	const struct rpmh_vreg_hw_data	*hw_data;
};

/**
 * rpmh_regulator_send_request() - send the request to RPMh
 * @vreg:		Pointer to the RPMh regulator
 * @cmd:		Pointer to the RPMh command to send
 * @wait_for_ack:	Boolean indicating if execution must wait until the
 *			request has been acknowledged as complete
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_send_request(struct rpmh_vreg *vreg,
			struct tcs_cmd *cmd, bool wait_for_ack)
{
	int ret;

	if (wait_for_ack || vreg->always_wait_for_ack)
		ret = rpmh_write(vreg->dev, RPMH_ACTIVE_ONLY_STATE, cmd, 1);
	else
		ret = rpmh_write_async(vreg->dev, RPMH_ACTIVE_ONLY_STATE, cmd,
					1);

	return ret;
}

static int _rpmh_regulator_vrm_set_voltage_sel(struct regulator_dev *rdev,
				unsigned int selector, bool wait_for_ack)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	struct tcs_cmd cmd = {
		.addr = vreg->addr + RPMH_REGULATOR_REG_VRM_VOLTAGE,
	};
	int ret;

	/* VRM voltage control register is set with voltage in millivolts. */
	cmd.data = DIV_ROUND_UP(regulator_list_voltage_linear_range(rdev,
							selector), 1000);

	ret = rpmh_regulator_send_request(vreg, &cmd, wait_for_ack);
	if (!ret)
		vreg->voltage_selector = selector;

	return ret;
}

static int rpmh_regulator_vrm_set_voltage_sel(struct regulator_dev *rdev,
					unsigned int selector)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	if (vreg->enabled == -EINVAL) {
		/*
		 * Cache the voltage and send it later when the regulator is
		 * enabled or disabled.
		 */
		vreg->voltage_selector = selector;
		return 0;
	}

	return _rpmh_regulator_vrm_set_voltage_sel(rdev, selector,
					selector > vreg->voltage_selector);
}

static int rpmh_regulator_vrm_get_voltage_sel(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->voltage_selector;
}

static int rpmh_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->enabled;
}

static int rpmh_regulator_set_enable_state(struct regulator_dev *rdev,
					bool enable)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	struct tcs_cmd cmd = {
		.addr = vreg->addr + RPMH_REGULATOR_REG_ENABLE,
		.data = enable,
	};
	int ret;

	if (vreg->enabled == -EINVAL &&
	    vreg->voltage_selector != -ENOTRECOVERABLE) {
		ret = _rpmh_regulator_vrm_set_voltage_sel(rdev,
						vreg->voltage_selector, true);
		if (ret < 0)
			return ret;
	}

	ret = rpmh_regulator_send_request(vreg, &cmd, enable);
	if (!ret)
		vreg->enabled = enable;

	return ret;
}

static int rpmh_regulator_enable(struct regulator_dev *rdev)
{
	return rpmh_regulator_set_enable_state(rdev, true);
}

static int rpmh_regulator_disable(struct regulator_dev *rdev)
{
	return rpmh_regulator_set_enable_state(rdev, false);
}

static int rpmh_regulator_vrm_set_mode_bypass(struct rpmh_vreg *vreg,
					unsigned int mode, bool bypassed)
{
	struct tcs_cmd cmd = {
		.addr = vreg->addr + RPMH_REGULATOR_REG_VRM_MODE,
	};
	int pmic_mode;

	if (mode > REGULATOR_MODE_STANDBY)
		return -EINVAL;

	pmic_mode = vreg->hw_data->pmic_mode_map[mode];
	if (pmic_mode < 0)
		return pmic_mode;

	if (bypassed)
		cmd.data = PMIC4_BOB_MODE_PASS;
	else
		cmd.data = pmic_mode;

	return rpmh_regulator_send_request(vreg, &cmd, true);
}

static int rpmh_regulator_vrm_set_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	int ret;

	if (mode == vreg->mode)
		return 0;

	ret = rpmh_regulator_vrm_set_mode_bypass(vreg, mode, vreg->bypassed);
	if (!ret)
		vreg->mode = mode;

	return ret;
}

static unsigned int rpmh_regulator_vrm_get_mode(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->mode;
}

/**
 * rpmh_regulator_vrm_set_load() - set the regulator mode based upon the load
 *		current requested
 * @rdev:		Regulator device pointer for the rpmh-regulator
 * @load_uA:		Aggregated load current in microamps
 *
 * This function is used in the regulator_ops for VRM type RPMh regulator
 * devices.
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_vrm_set_load(struct regulator_dev *rdev, int load_uA)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	unsigned int mode;

	if (load_uA >= vreg->hw_data->hpm_min_load_uA)
		mode = REGULATOR_MODE_NORMAL;
	else
		mode = REGULATOR_MODE_IDLE;

	return rpmh_regulator_vrm_set_mode(rdev, mode);
}

static int rpmh_regulator_vrm_set_bypass(struct regulator_dev *rdev,
				bool enable)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	int ret;

	if (vreg->bypassed == enable)
		return 0;

	ret = rpmh_regulator_vrm_set_mode_bypass(vreg, vreg->mode, enable);
	if (!ret)
		vreg->bypassed = enable;

	return ret;
}

static int rpmh_regulator_vrm_get_bypass(struct regulator_dev *rdev,
				bool *enable)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	*enable = vreg->bypassed;

	return 0;
}

static const struct regulator_ops rpmh_regulator_vrm_ops = {
	.enable			= rpmh_regulator_enable,
	.disable		= rpmh_regulator_disable,
	.is_enabled		= rpmh_regulator_is_enabled,
	.set_voltage_sel	= rpmh_regulator_vrm_set_voltage_sel,
	.get_voltage_sel	= rpmh_regulator_vrm_get_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.set_mode		= rpmh_regulator_vrm_set_mode,
	.get_mode		= rpmh_regulator_vrm_get_mode,
};

static const struct regulator_ops rpmh_regulator_vrm_drms_ops = {
	.enable			= rpmh_regulator_enable,
	.disable		= rpmh_regulator_disable,
	.is_enabled		= rpmh_regulator_is_enabled,
	.set_voltage_sel	= rpmh_regulator_vrm_set_voltage_sel,
	.get_voltage_sel	= rpmh_regulator_vrm_get_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.set_mode		= rpmh_regulator_vrm_set_mode,
	.get_mode		= rpmh_regulator_vrm_get_mode,
	.set_load		= rpmh_regulator_vrm_set_load,
};

static const struct regulator_ops rpmh_regulator_vrm_bypass_ops = {
	.enable			= rpmh_regulator_enable,
	.disable		= rpmh_regulator_disable,
	.is_enabled		= rpmh_regulator_is_enabled,
	.set_voltage_sel	= rpmh_regulator_vrm_set_voltage_sel,
	.get_voltage_sel	= rpmh_regulator_vrm_get_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.set_mode		= rpmh_regulator_vrm_set_mode,
	.get_mode		= rpmh_regulator_vrm_get_mode,
	.set_bypass		= rpmh_regulator_vrm_set_bypass,
	.get_bypass		= rpmh_regulator_vrm_get_bypass,
};

static const struct regulator_ops rpmh_regulator_xob_ops = {
	.enable			= rpmh_regulator_enable,
	.disable		= rpmh_regulator_disable,
	.is_enabled		= rpmh_regulator_is_enabled,
};

/**
 * rpmh_regulator_init_vreg() - initialize all attributes of an rpmh-regulator
 * @vreg:		Pointer to the individual rpmh-regulator resource
 * @dev:			Pointer to the top level rpmh-regulator PMIC device
 * @node:		Pointer to the individual rpmh-regulator resource
 *			device node
 * @pmic_id:		String used to identify the top level rpmh-regulator
 *			PMIC device on the board
 * @pmic_rpmh_data:	Pointer to a null-terminated array of rpmh-regulator
 *			resources defined for the top level PMIC device
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_init_vreg(struct rpmh_vreg *vreg, struct device *dev,
			struct device_node *node, const char *pmic_id,
			const struct rpmh_vreg_init_data *pmic_rpmh_data)
{
	struct regulator_config reg_config = {};
	char rpmh_resource_name[20] = "";
	const struct rpmh_vreg_init_data *rpmh_data;
	struct regulator_init_data *init_data;
	struct regulator_dev *rdev;
	int ret;

	vreg->dev = dev;

	for (rpmh_data = pmic_rpmh_data; rpmh_data->name; rpmh_data++)
		if (of_node_name_eq(node, rpmh_data->name))
			break;

	if (!rpmh_data->name) {
		dev_err(dev, "Unknown regulator %pOFn\n", node);
		return -EINVAL;
	}

	scnprintf(rpmh_resource_name, sizeof(rpmh_resource_name),
		rpmh_data->resource_name, pmic_id);

	vreg->addr = cmd_db_read_addr(rpmh_resource_name);
	if (!vreg->addr) {
		dev_err(dev, "%pOFn: could not find RPMh address for resource %s\n",
			node, rpmh_resource_name);
		return -ENODEV;
	}

	vreg->rdesc.name = rpmh_data->name;
	vreg->rdesc.supply_name = rpmh_data->supply_name;
	vreg->hw_data = rpmh_data->hw_data;

	vreg->enabled = -EINVAL;
	vreg->voltage_selector = -ENOTRECOVERABLE;
	vreg->mode = REGULATOR_MODE_INVALID;

	if (rpmh_data->hw_data->n_voltages) {
		vreg->rdesc.linear_ranges = &rpmh_data->hw_data->voltage_range;
		vreg->rdesc.n_linear_ranges = 1;
		vreg->rdesc.n_voltages = rpmh_data->hw_data->n_voltages;
	}

	vreg->always_wait_for_ack = of_property_read_bool(node,
						"qcom,always-wait-for-ack");

	vreg->rdesc.owner	= THIS_MODULE;
	vreg->rdesc.type	= REGULATOR_VOLTAGE;
	vreg->rdesc.ops		= vreg->hw_data->ops;
	vreg->rdesc.of_map_mode	= vreg->hw_data->of_map_mode;

	init_data = of_get_regulator_init_data(dev, node, &vreg->rdesc);
	if (!init_data)
		return -ENOMEM;

	if (rpmh_data->hw_data->regulator_type == XOB &&
	    init_data->constraints.min_uV &&
	    init_data->constraints.min_uV == init_data->constraints.max_uV) {
		vreg->rdesc.fixed_uV = init_data->constraints.min_uV;
		vreg->rdesc.n_voltages = 1;
	}

	reg_config.dev		= dev;
	reg_config.init_data	= init_data;
	reg_config.of_node	= node;
	reg_config.driver_data	= vreg;

	rdev = devm_regulator_register(dev, &vreg->rdesc, &reg_config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(dev, "%pOFn: devm_regulator_register() failed, ret=%d\n",
			node, ret);
		return ret;
	}

	dev_dbg(dev, "%pOFn regulator registered for RPMh resource %s @ 0x%05X\n",
		node, rpmh_resource_name, vreg->addr);

	return 0;
}

static const int pmic_mode_map_pmic4_ldo[REGULATOR_MODE_STANDBY + 1] = {
	[REGULATOR_MODE_INVALID] = -EINVAL,
	[REGULATOR_MODE_STANDBY] = PMIC4_LDO_MODE_RETENTION,
	[REGULATOR_MODE_IDLE]    = PMIC4_LDO_MODE_LPM,
	[REGULATOR_MODE_NORMAL]  = PMIC4_LDO_MODE_HPM,
	[REGULATOR_MODE_FAST]    = -EINVAL,
};

static const int pmic_mode_map_pmic5_ldo[REGULATOR_MODE_STANDBY + 1] = {
	[REGULATOR_MODE_INVALID] = -EINVAL,
	[REGULATOR_MODE_STANDBY] = PMIC5_LDO_MODE_RETENTION,
	[REGULATOR_MODE_IDLE]    = PMIC5_LDO_MODE_LPM,
	[REGULATOR_MODE_NORMAL]  = PMIC5_LDO_MODE_HPM,
	[REGULATOR_MODE_FAST]    = -EINVAL,
};

static unsigned int rpmh_regulator_pmic4_ldo_of_map_mode(unsigned int rpmh_mode)
{
	unsigned int mode;

	switch (rpmh_mode) {
	case RPMH_REGULATOR_MODE_HPM:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case RPMH_REGULATOR_MODE_LPM:
		mode = REGULATOR_MODE_IDLE;
		break;
	case RPMH_REGULATOR_MODE_RET:
		mode = REGULATOR_MODE_STANDBY;
		break;
	default:
		mode = REGULATOR_MODE_INVALID;
		break;
	}

	return mode;
}

static const int pmic_mode_map_pmic4_smps[REGULATOR_MODE_STANDBY + 1] = {
	[REGULATOR_MODE_INVALID] = -EINVAL,
	[REGULATOR_MODE_STANDBY] = PMIC4_SMPS_MODE_RETENTION,
	[REGULATOR_MODE_IDLE]    = PMIC4_SMPS_MODE_PFM,
	[REGULATOR_MODE_NORMAL]  = PMIC4_SMPS_MODE_AUTO,
	[REGULATOR_MODE_FAST]    = PMIC4_SMPS_MODE_PWM,
};

static const int pmic_mode_map_pmic5_smps[REGULATOR_MODE_STANDBY + 1] = {
	[REGULATOR_MODE_INVALID] = -EINVAL,
	[REGULATOR_MODE_STANDBY] = PMIC5_SMPS_MODE_RETENTION,
	[REGULATOR_MODE_IDLE]    = PMIC5_SMPS_MODE_PFM,
	[REGULATOR_MODE_NORMAL]  = PMIC5_SMPS_MODE_AUTO,
	[REGULATOR_MODE_FAST]    = PMIC5_SMPS_MODE_PWM,
};

static unsigned int
rpmh_regulator_pmic4_smps_of_map_mode(unsigned int rpmh_mode)
{
	unsigned int mode;

	switch (rpmh_mode) {
	case RPMH_REGULATOR_MODE_HPM:
		mode = REGULATOR_MODE_FAST;
		break;
	case RPMH_REGULATOR_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case RPMH_REGULATOR_MODE_LPM:
		mode = REGULATOR_MODE_IDLE;
		break;
	case RPMH_REGULATOR_MODE_RET:
		mode = REGULATOR_MODE_STANDBY;
		break;
	default:
		mode = REGULATOR_MODE_INVALID;
		break;
	}

	return mode;
}

static const int pmic_mode_map_pmic4_bob[REGULATOR_MODE_STANDBY + 1] = {
	[REGULATOR_MODE_INVALID] = -EINVAL,
	[REGULATOR_MODE_STANDBY] = -EINVAL,
	[REGULATOR_MODE_IDLE]    = PMIC4_BOB_MODE_PFM,
	[REGULATOR_MODE_NORMAL]  = PMIC4_BOB_MODE_AUTO,
	[REGULATOR_MODE_FAST]    = PMIC4_BOB_MODE_PWM,
};

static const int pmic_mode_map_pmic5_bob[REGULATOR_MODE_STANDBY + 1] = {
	[REGULATOR_MODE_INVALID] = -EINVAL,
	[REGULATOR_MODE_STANDBY] = -EINVAL,
	[REGULATOR_MODE_IDLE]    = PMIC5_BOB_MODE_PFM,
	[REGULATOR_MODE_NORMAL]  = PMIC5_BOB_MODE_AUTO,
	[REGULATOR_MODE_FAST]    = PMIC5_BOB_MODE_PWM,
};

static unsigned int rpmh_regulator_pmic4_bob_of_map_mode(unsigned int rpmh_mode)
{
	unsigned int mode;

	switch (rpmh_mode) {
	case RPMH_REGULATOR_MODE_HPM:
		mode = REGULATOR_MODE_FAST;
		break;
	case RPMH_REGULATOR_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case RPMH_REGULATOR_MODE_LPM:
		mode = REGULATOR_MODE_IDLE;
		break;
	default:
		mode = REGULATOR_MODE_INVALID;
		break;
	}

	return mode;
}

static const struct rpmh_vreg_hw_data pmic4_pldo = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_drms_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(1664000, 0, 255, 8000),
	.n_voltages = 256,
	.hpm_min_load_uA = 10000,
	.pmic_mode_map = pmic_mode_map_pmic4_ldo,
	.of_map_mode = rpmh_regulator_pmic4_ldo_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic4_pldo_lv = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_drms_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(1256000, 0, 127, 8000),
	.n_voltages = 128,
	.hpm_min_load_uA = 10000,
	.pmic_mode_map = pmic_mode_map_pmic4_ldo,
	.of_map_mode = rpmh_regulator_pmic4_ldo_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic4_nldo = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_drms_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(312000, 0, 127, 8000),
	.n_voltages = 128,
	.hpm_min_load_uA = 30000,
	.pmic_mode_map = pmic_mode_map_pmic4_ldo,
	.of_map_mode = rpmh_regulator_pmic4_ldo_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic4_hfsmps3 = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(320000, 0, 215, 8000),
	.n_voltages = 216,
	.pmic_mode_map = pmic_mode_map_pmic4_smps,
	.of_map_mode = rpmh_regulator_pmic4_smps_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic4_ftsmps426 = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(320000, 0, 258, 4000),
	.n_voltages = 259,
	.pmic_mode_map = pmic_mode_map_pmic4_smps,
	.of_map_mode = rpmh_regulator_pmic4_smps_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic4_bob = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_bypass_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(1824000, 0, 83, 32000),
	.n_voltages = 84,
	.pmic_mode_map = pmic_mode_map_pmic4_bob,
	.of_map_mode = rpmh_regulator_pmic4_bob_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic4_lvs = {
	.regulator_type = XOB,
	.ops = &rpmh_regulator_xob_ops,
	/* LVS hardware does not support voltage or mode configuration. */
};

static const struct rpmh_vreg_hw_data pmic5_pldo = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_drms_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(1504000, 0, 255, 8000),
	.n_voltages = 256,
	.hpm_min_load_uA = 10000,
	.pmic_mode_map = pmic_mode_map_pmic5_ldo,
	.of_map_mode = rpmh_regulator_pmic4_ldo_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic5_pldo_lv = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_drms_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(1504000, 0, 62, 8000),
	.n_voltages = 63,
	.hpm_min_load_uA = 10000,
	.pmic_mode_map = pmic_mode_map_pmic5_ldo,
	.of_map_mode = rpmh_regulator_pmic4_ldo_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic5_nldo = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_drms_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(320000, 0, 123, 8000),
	.n_voltages = 124,
	.hpm_min_load_uA = 30000,
	.pmic_mode_map = pmic_mode_map_pmic5_ldo,
	.of_map_mode = rpmh_regulator_pmic4_ldo_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic5_hfsmps510 = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(320000, 0, 215, 8000),
	.n_voltages = 216,
	.pmic_mode_map = pmic_mode_map_pmic5_smps,
	.of_map_mode = rpmh_regulator_pmic4_smps_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic5_ftsmps510 = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(300000, 0, 263, 4000),
	.n_voltages = 264,
	.pmic_mode_map = pmic_mode_map_pmic5_smps,
	.of_map_mode = rpmh_regulator_pmic4_smps_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic5_hfsmps515 = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(2800000, 0, 4, 16000),
	.n_voltages = 5,
	.pmic_mode_map = pmic_mode_map_pmic5_smps,
	.of_map_mode = rpmh_regulator_pmic4_smps_of_map_mode,
};

static const struct rpmh_vreg_hw_data pmic5_bob = {
	.regulator_type = VRM,
	.ops = &rpmh_regulator_vrm_bypass_ops,
	.voltage_range = REGULATOR_LINEAR_RANGE(3000000, 0, 31, 32000),
	.n_voltages = 32,
	.pmic_mode_map = pmic_mode_map_pmic5_bob,
	.of_map_mode = rpmh_regulator_pmic4_bob_of_map_mode,
};

#define RPMH_VREG(_name, _resource_name, _hw_data, _supply_name) \
{ \
	.name		= _name, \
	.resource_name	= _resource_name, \
	.hw_data	= _hw_data, \
	.supply_name	= _supply_name, \
}

static const struct rpmh_vreg_init_data pm8998_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic4_ftsmps426, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic4_ftsmps426, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic4_hfsmps3,   "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic4_hfsmps3,   "vdd-s4"),
	RPMH_VREG("smps5",  "smp%s5",  &pmic4_hfsmps3,   "vdd-s5"),
	RPMH_VREG("smps6",  "smp%s6",  &pmic4_ftsmps426, "vdd-s6"),
	RPMH_VREG("smps7",  "smp%s7",  &pmic4_ftsmps426, "vdd-s7"),
	RPMH_VREG("smps8",  "smp%s8",  &pmic4_ftsmps426, "vdd-s8"),
	RPMH_VREG("smps9",  "smp%s9",  &pmic4_ftsmps426, "vdd-s9"),
	RPMH_VREG("smps10", "smp%s10", &pmic4_ftsmps426, "vdd-s10"),
	RPMH_VREG("smps11", "smp%s11", &pmic4_ftsmps426, "vdd-s11"),
	RPMH_VREG("smps12", "smp%s12", &pmic4_ftsmps426, "vdd-s12"),
	RPMH_VREG("smps13", "smp%s13", &pmic4_ftsmps426, "vdd-s13"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic4_nldo,      "vdd-l1-l27"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic4_nldo,      "vdd-l2-l8-l17"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic4_nldo,      "vdd-l3-l11"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic4_nldo,      "vdd-l4-l5"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic4_nldo,      "vdd-l4-l5"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic4_pldo,      "vdd-l6"),
	RPMH_VREG("ldo7",   "ldo%s7",  &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo8",   "ldo%s8",  &pmic4_nldo,      "vdd-l2-l8-l17"),
	RPMH_VREG("ldo9",   "ldo%s9",  &pmic4_pldo,      "vdd-l9"),
	RPMH_VREG("ldo10",  "ldo%s10", &pmic4_pldo,      "vdd-l10-l23-l25"),
	RPMH_VREG("ldo11",  "ldo%s11", &pmic4_nldo,      "vdd-l3-l11"),
	RPMH_VREG("ldo12",  "ldo%s12", &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo13",  "ldo%s13", &pmic4_pldo,      "vdd-l13-l19-l21"),
	RPMH_VREG("ldo14",  "ldo%s14", &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo15",  "ldo%s15", &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo16",  "ldo%s16", &pmic4_pldo,      "vdd-l16-l28"),
	RPMH_VREG("ldo17",  "ldo%s17", &pmic4_nldo,      "vdd-l2-l8-l17"),
	RPMH_VREG("ldo18",  "ldo%s18", &pmic4_pldo,      "vdd-l18-l22"),
	RPMH_VREG("ldo19",  "ldo%s19", &pmic4_pldo,      "vdd-l13-l19-l21"),
	RPMH_VREG("ldo20",  "ldo%s20", &pmic4_pldo,      "vdd-l20-l24"),
	RPMH_VREG("ldo21",  "ldo%s21", &pmic4_pldo,      "vdd-l13-l19-l21"),
	RPMH_VREG("ldo22",  "ldo%s22", &pmic4_pldo,      "vdd-l18-l22"),
	RPMH_VREG("ldo23",  "ldo%s23", &pmic4_pldo,      "vdd-l10-l23-l25"),
	RPMH_VREG("ldo24",  "ldo%s24", &pmic4_pldo,      "vdd-l20-l24"),
	RPMH_VREG("ldo25",  "ldo%s25", &pmic4_pldo,      "vdd-l10-l23-l25"),
	RPMH_VREG("ldo26",  "ldo%s26", &pmic4_nldo,      "vdd-l26"),
	RPMH_VREG("ldo27",  "ldo%s27", &pmic4_nldo,      "vdd-l1-l27"),
	RPMH_VREG("ldo28",  "ldo%s28", &pmic4_pldo,      "vdd-l16-l28"),
	RPMH_VREG("lvs1",   "vs%s1",   &pmic4_lvs,       "vin-lvs-1-2"),
	RPMH_VREG("lvs2",   "vs%s2",   &pmic4_lvs,       "vin-lvs-1-2"),
	{},
};

static const struct rpmh_vreg_init_data pmi8998_vreg_data[] = {
	RPMH_VREG("bob",    "bob%s1",  &pmic4_bob,       "vdd-bob"),
	{},
};

static const struct rpmh_vreg_init_data pm8005_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic4_ftsmps426, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic4_ftsmps426, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic4_ftsmps426, "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic4_ftsmps426, "vdd-s4"),
	{},
};

static const struct rpmh_vreg_init_data pm8150_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic5_hfsmps510,   "vdd-s4"),
	RPMH_VREG("smps5",  "smp%s5",  &pmic5_hfsmps510,   "vdd-s5"),
	RPMH_VREG("smps6",  "smp%s6",  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  "smp%s7",  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  "smp%s8",  &pmic5_ftsmps510, "vdd-s8"),
	RPMH_VREG("smps9",  "smp%s9",  &pmic5_ftsmps510, "vdd-s9"),
	RPMH_VREG("smps10", "smp%s10", &pmic5_ftsmps510, "vdd-s10"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic5_pldo,      "vdd-l2-l10"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic5_nldo,      "vdd-l6-l9"),
	RPMH_VREG("ldo7",   "ldo%s7",  &pmic5_pldo,      "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo8",   "ldo%s8",  &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo9",   "ldo%s9",  &pmic5_nldo,      "vdd-l6-l9"),
	RPMH_VREG("ldo10",  "ldo%s10", &pmic5_pldo,      "vdd-l2-l10"),
	RPMH_VREG("ldo11",  "ldo%s11", &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo12",  "ldo%s12", &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo13",  "ldo%s13", &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo14",  "ldo%s14", &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo15",  "ldo%s15", &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo16",  "ldo%s16", &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo17",  "ldo%s17", &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo18",  "ldo%s18", &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	{},
};

static const struct rpmh_vreg_init_data pm8150l_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  "smp%s5",  &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  "smp%s6",  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  "smp%s7",  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  "smp%s8",  &pmic5_hfsmps510, "vdd-s8"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic5_pldo_lv,   "vdd-l1-l8"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo7",   "ldo%s7",  &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("ldo8",   "ldo%s8",  &pmic5_pldo_lv,   "vdd-l1-l8"),
	RPMH_VREG("ldo9",   "ldo%s9",  &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo10",  "ldo%s10", &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo11",  "ldo%s11", &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("bob",    "bob%s1",  &pmic5_bob,       "vdd-bob"),
	{},
};

static const struct rpmh_vreg_init_data pm8350_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  "smp%s5",  &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  "smp%s6",  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  "smp%s7",  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  "smp%s8",  &pmic5_ftsmps510, "vdd-s8"),
	RPMH_VREG("smps9",  "smp%s9",  &pmic5_ftsmps510, "vdd-s9"),
	RPMH_VREG("smps10", "smp%s10", &pmic5_hfsmps510, "vdd-s10"),
	RPMH_VREG("smps11", "smp%s11", &pmic5_hfsmps510, "vdd-s11"),
	RPMH_VREG("smps12", "smp%s12", &pmic5_hfsmps510, "vdd-s12"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic5_nldo,      "vdd-l1-l4"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic5_pldo,      "vdd-l2-l7"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic5_nldo,      "vdd-l3-l5"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic5_nldo,      "vdd-l1-l4"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic5_nldo,      "vdd-l3-l5"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic5_nldo,      "vdd-l6-l9-l10"),
	RPMH_VREG("ldo7",   "ldo%s7",  &pmic5_pldo,      "vdd-l2-l7"),
	RPMH_VREG("ldo8",   "ldo%s8",  &pmic5_nldo,      "vdd-l8"),
	RPMH_VREG("ldo9",   "ldo%s9",  &pmic5_nldo,      "vdd-l6-l9-l10"),
	RPMH_VREG("ldo10",  "ldo%s10", &pmic5_nldo,      "vdd-l6-l9-l10"),
	{},
};

static const struct rpmh_vreg_init_data pm8350c_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic5_hfsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  "smp%s5",  &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  "smp%s6",  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  "smp%s7",  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  "smp%s8",  &pmic5_ftsmps510, "vdd-s8"),
	RPMH_VREG("smps9",  "smp%s9",  &pmic5_ftsmps510, "vdd-s9"),
	RPMH_VREG("smps10", "smp%s10", &pmic5_ftsmps510, "vdd-s10"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic5_pldo_lv,   "vdd-l1-l12"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic5_pldo_lv,   "vdd-l2-l8"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic5_pldo,      "vdd-l6-l9-l11"),
	RPMH_VREG("ldo7",   "ldo%s7",  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo8",   "ldo%s8",  &pmic5_pldo_lv,   "vdd-l2-l8"),
	RPMH_VREG("ldo9",   "ldo%s9",  &pmic5_pldo,      "vdd-l6-l9-l11"),
	RPMH_VREG("ldo10",  "ldo%s10", &pmic5_nldo,      "vdd-l10"),
	RPMH_VREG("ldo11",  "ldo%s11", &pmic5_pldo,      "vdd-l6-l9-l11"),
	RPMH_VREG("ldo12",  "ldo%s12", &pmic5_pldo_lv,   "vdd-l1-l12"),
	RPMH_VREG("ldo13",  "ldo%s13", &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("bob",    "bob%s1",  &pmic5_bob,       "vdd-bob"),
	{},
};

static const struct rpmh_vreg_init_data pm8009_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic5_hfsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic5_hfsmps515, "vdd-s2"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic5_nldo,      "vdd-l1"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic5_nldo,      "vdd-l2"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic5_nldo,      "vdd-l3"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic5_nldo,      "vdd-l4"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo7",   "ldo%s6",  &pmic5_pldo_lv,   "vdd-l7"),
	{},
};

static const struct rpmh_vreg_init_data pm6150_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic5_hfsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  "smp%s5",  &pmic5_hfsmps510, "vdd-s5"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic5_nldo,      "vdd-l1"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic5_nldo,      "vdd-l4-l7-l8"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic5_pldo,   "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic5_nldo,      "vdd-l6"),
	RPMH_VREG("ldo7",   "ldo%s7",  &pmic5_nldo,      "vdd-l4-l7-l8"),
	RPMH_VREG("ldo8",   "ldo%s8",  &pmic5_nldo,      "vdd-l4-l7-l8"),
	RPMH_VREG("ldo9",   "ldo%s9",  &pmic5_nldo,      "vdd-l9"),
	RPMH_VREG("ldo10",  "ldo%s10", &pmic5_pldo_lv,   "vdd-l10-l14-l15"),
	RPMH_VREG("ldo11",  "ldo%s11", &pmic5_pldo_lv,   "vdd-l11-l12-l13"),
	RPMH_VREG("ldo12",  "ldo%s12", &pmic5_pldo_lv,   "vdd-l11-l12-l13"),
	RPMH_VREG("ldo13",  "ldo%s13", &pmic5_pldo_lv,   "vdd-l11-l12-l13"),
	RPMH_VREG("ldo14",  "ldo%s14", &pmic5_pldo_lv,   "vdd-l10-l14-l15"),
	RPMH_VREG("ldo15",  "ldo%s15", &pmic5_pldo_lv,   "vdd-l10-l14-l15"),
	RPMH_VREG("ldo16",  "ldo%s16", &pmic5_pldo,   "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo17",  "ldo%s17", &pmic5_pldo,   "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo18",  "ldo%s18", &pmic5_pldo,   "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo19",  "ldo%s19", &pmic5_pldo,   "vdd-l5-l16-l17-l18-l19"),
	{},
};

static const struct rpmh_vreg_init_data pm6150l_vreg_data[] = {
	RPMH_VREG("smps1",  "smp%s1",  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  "smp%s2",  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  "smp%s3",  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  "smp%s4",  &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  "smp%s5",  &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  "smp%s6",  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  "smp%s7",  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  "smp%s8",  &pmic5_hfsmps510, "vdd-s8"),
	RPMH_VREG("ldo1",   "ldo%s1",  &pmic5_pldo_lv,   "vdd-l1-l8"),
	RPMH_VREG("ldo2",   "ldo%s2",  &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo3",   "ldo%s3",  &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo4",   "ldo%s4",  &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo5",   "ldo%s5",  &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo6",   "ldo%s6",  &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo7",   "ldo%s7",  &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("ldo8",   "ldo%s8",  &pmic5_pldo,      "vdd-l1-l8"),
	RPMH_VREG("ldo9",   "ldo%s9",  &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo10",  "ldo%s10", &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo11",  "ldo%s11", &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("bob",    "bob%s1",  &pmic5_bob,       "vdd-bob"),
	{},
};

static const struct rpmh_vreg_init_data pmx55_vreg_data[] = {
	RPMH_VREG("smps1",   "smp%s1",    &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",   "smp%s2",    &pmic5_hfsmps510, "vdd-s2"),
	RPMH_VREG("smps3",   "smp%s3",    &pmic5_hfsmps510, "vdd-s3"),
	RPMH_VREG("smps4",   "smp%s4",    &pmic5_hfsmps510, "vdd-s4"),
	RPMH_VREG("smps5",   "smp%s5",    &pmic5_hfsmps510, "vdd-s5"),
	RPMH_VREG("smps6",   "smp%s6",    &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",   "smp%s7",    &pmic5_hfsmps510, "vdd-s7"),
	RPMH_VREG("ldo1",    "ldo%s1",    &pmic5_nldo,      "vdd-l1-l2"),
	RPMH_VREG("ldo2",    "ldo%s2",    &pmic5_nldo,      "vdd-l1-l2"),
	RPMH_VREG("ldo3",    "ldo%s3",    &pmic5_nldo,      "vdd-l3-l9"),
	RPMH_VREG("ldo4",    "ldo%s4",    &pmic5_nldo,      "vdd-l4-l12"),
	RPMH_VREG("ldo5",    "ldo%s5",    &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo6",    "ldo%s6",    &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo7",    "ldo%s7",    &pmic5_nldo,      "vdd-l7-l8"),
	RPMH_VREG("ldo8",    "ldo%s8",    &pmic5_nldo,      "vdd-l7-l8"),
	RPMH_VREG("ldo9",    "ldo%s9",    &pmic5_nldo,      "vdd-l3-l9"),
	RPMH_VREG("ldo10",   "ldo%s10",   &pmic5_pldo,      "vdd-l10-l11-l13"),
	RPMH_VREG("ldo11",   "ldo%s11",   &pmic5_pldo,      "vdd-l10-l11-l13"),
	RPMH_VREG("ldo12",   "ldo%s12",   &pmic5_nldo,      "vdd-l4-l12"),
	RPMH_VREG("ldo13",   "ldo%s13",   &pmic5_pldo,      "vdd-l10-l11-l13"),
	RPMH_VREG("ldo14",   "ldo%s14",   &pmic5_nldo,      "vdd-l14"),
	RPMH_VREG("ldo15",   "ldo%s15",   &pmic5_nldo,      "vdd-l15"),
	RPMH_VREG("ldo16",   "ldo%s16",   &pmic5_pldo,      "vdd-l16"),
	{},
};

static int rpmh_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct rpmh_vreg_init_data *vreg_data;
	struct device_node *node;
	struct rpmh_vreg *vreg;
	const char *pmic_id;
	int ret;

	vreg_data = of_device_get_match_data(dev);
	if (!vreg_data)
		return -ENODEV;

	ret = of_property_read_string(dev->of_node, "qcom,pmic-id", &pmic_id);
	if (ret < 0) {
		dev_err(dev, "qcom,pmic-id missing in DT node\n");
		return ret;
	}

	for_each_available_child_of_node(dev->of_node, node) {
		vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
		if (!vreg) {
			of_node_put(node);
			return -ENOMEM;
		}

		ret = rpmh_regulator_init_vreg(vreg, dev, node, pmic_id,
						vreg_data);
		if (ret < 0) {
			of_node_put(node);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id __maybe_unused rpmh_regulator_match_table[] = {
	{
		.compatible = "qcom,pm8005-rpmh-regulators",
		.data = pm8005_vreg_data,
	},
	{
		.compatible = "qcom,pm8009-rpmh-regulators",
		.data = pm8009_vreg_data,
	},
	{
		.compatible = "qcom,pm8150-rpmh-regulators",
		.data = pm8150_vreg_data,
	},
	{
		.compatible = "qcom,pm8150l-rpmh-regulators",
		.data = pm8150l_vreg_data,
	},
	{
		.compatible = "qcom,pm8350-rpmh-regulators",
		.data = pm8350_vreg_data,
	},
	{
		.compatible = "qcom,pm8350c-rpmh-regulators",
		.data = pm8350c_vreg_data,
	},
	{
		.compatible = "qcom,pm8998-rpmh-regulators",
		.data = pm8998_vreg_data,
	},
	{
		.compatible = "qcom,pmi8998-rpmh-regulators",
		.data = pmi8998_vreg_data,
	},
	{
		.compatible = "qcom,pm6150-rpmh-regulators",
		.data = pm6150_vreg_data,
	},
	{
		.compatible = "qcom,pm6150l-rpmh-regulators",
		.data = pm6150l_vreg_data,
	},
	{
		.compatible = "qcom,pmx55-rpmh-regulators",
		.data = pmx55_vreg_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rpmh_regulator_match_table);

static struct platform_driver rpmh_regulator_driver = {
	.driver = {
		.name = "qcom-rpmh-regulator",
		.of_match_table	= of_match_ptr(rpmh_regulator_match_table),
	},
	.probe = rpmh_regulator_probe,
};
module_platform_driver(rpmh_regulator_driver);

MODULE_DESCRIPTION("Qualcomm RPMh regulator driver");
MODULE_LICENSE("GPL v2");
