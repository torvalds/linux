/*
 * Copyright (c) 2015, Sony Mobile Communications AB.
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/soc/qcom/smd-rpm.h>

struct qcom_rpm_reg {
	struct device *dev;

	struct qcom_smd_rpm *rpm;

	u32 type;
	u32 id;

	struct regulator_desc desc;

	int is_enabled;
	int uV;
};

struct rpm_regulator_req {
	__le32 key;
	__le32 nbytes;
	__le32 value;
};

#define RPM_KEY_SWEN	0x6e657773 /* "swen" */
#define RPM_KEY_UV	0x00007675 /* "uv" */
#define RPM_KEY_MA	0x0000616d /* "ma" */

static int rpm_reg_write_active(struct qcom_rpm_reg *vreg,
				struct rpm_regulator_req *req,
				size_t size)
{
	return qcom_rpm_smd_write(vreg->rpm,
				  QCOM_SMD_RPM_ACTIVE_STATE,
				  vreg->type,
				  vreg->id,
				  req, size);
}

static int rpm_reg_enable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	struct rpm_regulator_req req;
	int ret;

	req.key = cpu_to_le32(RPM_KEY_SWEN);
	req.nbytes = cpu_to_le32(sizeof(u32));
	req.value = cpu_to_le32(1);

	ret = rpm_reg_write_active(vreg, &req, sizeof(req));
	if (!ret)
		vreg->is_enabled = 1;

	return ret;
}

static int rpm_reg_is_enabled(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);

	return vreg->is_enabled;
}

static int rpm_reg_disable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	struct rpm_regulator_req req;
	int ret;

	req.key = cpu_to_le32(RPM_KEY_SWEN);
	req.nbytes = cpu_to_le32(sizeof(u32));
	req.value = 0;

	ret = rpm_reg_write_active(vreg, &req, sizeof(req));
	if (!ret)
		vreg->is_enabled = 0;

	return ret;
}

static int rpm_reg_get_voltage(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);

	return vreg->uV;
}

static int rpm_reg_set_voltage(struct regulator_dev *rdev,
			       int min_uV,
			       int max_uV,
			       unsigned *selector)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	struct rpm_regulator_req req;
	int ret = 0;

	req.key = cpu_to_le32(RPM_KEY_UV);
	req.nbytes = cpu_to_le32(sizeof(u32));
	req.value = cpu_to_le32(min_uV);

	ret = rpm_reg_write_active(vreg, &req, sizeof(req));
	if (!ret)
		vreg->uV = min_uV;

	return ret;
}

static int rpm_reg_set_load(struct regulator_dev *rdev, int load_uA)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	struct rpm_regulator_req req;

	req.key = cpu_to_le32(RPM_KEY_MA);
	req.nbytes = cpu_to_le32(sizeof(u32));
	req.value = cpu_to_le32(load_uA / 1000);

	return rpm_reg_write_active(vreg, &req, sizeof(req));
}

static const struct regulator_ops rpm_smps_ldo_ops = {
	.enable = rpm_reg_enable,
	.disable = rpm_reg_disable,
	.is_enabled = rpm_reg_is_enabled,
	.list_voltage = regulator_list_voltage_linear_range,

	.get_voltage = rpm_reg_get_voltage,
	.set_voltage = rpm_reg_set_voltage,

	.set_load = rpm_reg_set_load,
};

static const struct regulator_ops rpm_smps_ldo_ops_fixed = {
	.enable = rpm_reg_enable,
	.disable = rpm_reg_disable,
	.is_enabled = rpm_reg_is_enabled,

	.get_voltage = rpm_reg_get_voltage,
	.set_voltage = rpm_reg_set_voltage,

	.set_load = rpm_reg_set_load,
};

static const struct regulator_ops rpm_switch_ops = {
	.enable = rpm_reg_enable,
	.disable = rpm_reg_disable,
	.is_enabled = rpm_reg_is_enabled,
};

static const struct regulator_desc pma8084_hfsmps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(375000,  0,  95, 12500),
		REGULATOR_LINEAR_RANGE(1550000, 96, 158, 25000),
	},
	.n_linear_ranges = 2,
	.n_voltages = 159,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pma8084_ftsmps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(350000,  0, 184, 5000),
		REGULATOR_LINEAR_RANGE(1280000, 185, 261, 10000),
	},
	.n_linear_ranges = 2,
	.n_voltages = 262,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pma8084_pldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE( 750000,  0,  63, 12500),
		REGULATOR_LINEAR_RANGE(1550000, 64, 126, 25000),
		REGULATOR_LINEAR_RANGE(3100000, 127, 163, 50000),
	},
	.n_linear_ranges = 3,
	.n_voltages = 164,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pma8084_nldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(750000, 0, 63, 12500),
	},
	.n_linear_ranges = 1,
	.n_voltages = 64,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pma8084_switch = {
	.ops = &rpm_switch_ops,
};

static const struct regulator_desc pm8x41_hfsmps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE( 375000,  0,  95, 12500),
		REGULATOR_LINEAR_RANGE(1575000, 96, 158, 25000),
	},
	.n_linear_ranges = 2,
	.n_voltages = 159,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8841_ftsmps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(350000,  0, 184, 5000),
		REGULATOR_LINEAR_RANGE(1280000, 185, 261, 10000),
	},
	.n_linear_ranges = 2,
	.n_voltages = 262,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8941_boost = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(4000000, 0, 30, 50000),
	},
	.n_linear_ranges = 1,
	.n_voltages = 31,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8941_pldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE( 750000,  0,  63, 12500),
		REGULATOR_LINEAR_RANGE(1550000, 64, 126, 25000),
		REGULATOR_LINEAR_RANGE(3100000, 127, 163, 50000),
	},
	.n_linear_ranges = 3,
	.n_voltages = 164,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8941_nldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(750000, 0, 63, 12500),
	},
	.n_linear_ranges = 1,
	.n_voltages = 64,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8941_lnldo = {
	.fixed_uV = 1740000,
	.n_voltages = 1,
	.ops = &rpm_smps_ldo_ops_fixed,
};

static const struct regulator_desc pm8941_switch = {
	.ops = &rpm_switch_ops,
};

static const struct regulator_desc pm8916_pldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(750000, 0, 208, 12500),
	},
	.n_linear_ranges = 1,
	.n_voltages = 209,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8916_nldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(375000, 0, 93, 12500),
	},
	.n_linear_ranges = 1,
	.n_voltages = 94,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8916_buck_lvo_smps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(375000, 0, 95, 12500),
		REGULATOR_LINEAR_RANGE(750000, 96, 127, 25000),
	},
	.n_linear_ranges = 2,
	.n_voltages = 128,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8916_buck_hvo_smps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(1550000, 0, 31, 25000),
	},
	.n_linear_ranges = 1,
	.n_voltages = 32,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8994_hfsmps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE( 375000,  0,  95, 12500),
		REGULATOR_LINEAR_RANGE(1550000, 96, 158, 25000),
	},
	.n_linear_ranges = 2,
	.n_voltages = 159,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8994_ftsmps = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(350000,  0, 199, 5000),
		REGULATOR_LINEAR_RANGE(700000, 200, 349, 10000),
	},
	.n_linear_ranges = 2,
	.n_voltages = 350,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8994_nldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE(750000, 0, 63, 12500),
	},
	.n_linear_ranges = 1,
	.n_voltages = 64,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8994_pldo = {
	.linear_ranges = (struct regulator_linear_range[]) {
		REGULATOR_LINEAR_RANGE( 750000,  0,  63, 12500),
		REGULATOR_LINEAR_RANGE(1550000, 64, 126, 25000),
		REGULATOR_LINEAR_RANGE(3100000, 127, 163, 50000),
	},
	.n_linear_ranges = 3,
	.n_voltages = 164,
	.ops = &rpm_smps_ldo_ops,
};

static const struct regulator_desc pm8994_switch = {
	.ops = &rpm_switch_ops,
};

static const struct regulator_desc pm8994_lnldo = {
	.fixed_uV = 1740000,
	.n_voltages = 1,
	.ops = &rpm_smps_ldo_ops_fixed,
};

struct rpm_regulator_data {
	const char *name;
	u32 type;
	u32 id;
	const struct regulator_desc *desc;
	const char *supply;
};

static const struct rpm_regulator_data rpm_pm8841_regulators[] = {
	{ "s1", QCOM_SMD_RPM_SMPB, 1, &pm8x41_hfsmps, "vdd_s1" },
	{ "s2", QCOM_SMD_RPM_SMPB, 2, &pm8841_ftsmps, "vdd_s2" },
	{ "s3", QCOM_SMD_RPM_SMPB, 3, &pm8x41_hfsmps, "vdd_s3" },
	{ "s4", QCOM_SMD_RPM_SMPB, 4, &pm8841_ftsmps, "vdd_s4" },
	{ "s5", QCOM_SMD_RPM_SMPB, 5, &pm8841_ftsmps, "vdd_s5" },
	{ "s6", QCOM_SMD_RPM_SMPB, 6, &pm8841_ftsmps, "vdd_s6" },
	{ "s7", QCOM_SMD_RPM_SMPB, 7, &pm8841_ftsmps, "vdd_s7" },
	{ "s8", QCOM_SMD_RPM_SMPB, 8, &pm8841_ftsmps, "vdd_s8" },
	{}
};

static const struct rpm_regulator_data rpm_pm8916_regulators[] = {
	{ "s1", QCOM_SMD_RPM_SMPA, 1, &pm8916_buck_lvo_smps, "vdd_s1" },
	{ "s2", QCOM_SMD_RPM_SMPA, 2, &pm8916_buck_lvo_smps, "vdd_s2" },
	{ "s3", QCOM_SMD_RPM_SMPA, 3, &pm8916_buck_lvo_smps, "vdd_s3" },
	{ "s4", QCOM_SMD_RPM_SMPA, 4, &pm8916_buck_hvo_smps, "vdd_s4" },
	{ "l1", QCOM_SMD_RPM_LDOA, 1, &pm8916_nldo, "vdd_l1_l2_l3" },
	{ "l2", QCOM_SMD_RPM_LDOA, 2, &pm8916_nldo, "vdd_l1_l2_l3" },
	{ "l3", QCOM_SMD_RPM_LDOA, 3, &pm8916_nldo, "vdd_l1_l2_l3" },
	{ "l4", QCOM_SMD_RPM_LDOA, 4, &pm8916_pldo, "vdd_l4_l5_l6" },
	{ "l5", QCOM_SMD_RPM_LDOA, 5, &pm8916_pldo, "vdd_l4_l5_l6" },
	{ "l6", QCOM_SMD_RPM_LDOA, 6, &pm8916_pldo, "vdd_l4_l5_l6" },
	{ "l7", QCOM_SMD_RPM_LDOA, 7, &pm8916_pldo, "vdd_l7" },
	{ "l8", QCOM_SMD_RPM_LDOA, 8, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18" },
	{ "l9", QCOM_SMD_RPM_LDOA, 9, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18" },
	{ "l10", QCOM_SMD_RPM_LDOA, 10, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l11", QCOM_SMD_RPM_LDOA, 11, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l12", QCOM_SMD_RPM_LDOA, 12, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l13", QCOM_SMD_RPM_LDOA, 13, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l14", QCOM_SMD_RPM_LDOA, 14, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l15", QCOM_SMD_RPM_LDOA, 15, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l16", QCOM_SMD_RPM_LDOA, 16, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l17", QCOM_SMD_RPM_LDOA, 17, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{ "l18", QCOM_SMD_RPM_LDOA, 18, &pm8916_pldo, "vdd_l8_l9_l10_l11_l12_l13_l14_l15_l16_l17_l18"},
	{}
};

static const struct rpm_regulator_data rpm_pm8941_regulators[] = {
	{ "s1", QCOM_SMD_RPM_SMPA, 1, &pm8x41_hfsmps, "vdd_s1" },
	{ "s2", QCOM_SMD_RPM_SMPA, 2, &pm8x41_hfsmps, "vdd_s2" },
	{ "s3", QCOM_SMD_RPM_SMPA, 3, &pm8x41_hfsmps, "vdd_s3" },
	{ "s4", QCOM_SMD_RPM_BOOST, 1, &pm8941_boost },

	{ "l1", QCOM_SMD_RPM_LDOA, 1, &pm8941_nldo, "vdd_l1_l3" },
	{ "l2", QCOM_SMD_RPM_LDOA, 2, &pm8941_nldo, "vdd_l2_lvs1_2_3" },
	{ "l3", QCOM_SMD_RPM_LDOA, 3, &pm8941_nldo, "vdd_l1_l3" },
	{ "l4", QCOM_SMD_RPM_LDOA, 4, &pm8941_nldo, "vdd_l4_l11" },
	{ "l5", QCOM_SMD_RPM_LDOA, 5, &pm8941_lnldo, "vdd_l5_l7" },
	{ "l6", QCOM_SMD_RPM_LDOA, 6, &pm8941_pldo, "vdd_l6_l12_l14_l15" },
	{ "l7", QCOM_SMD_RPM_LDOA, 7, &pm8941_lnldo, "vdd_l5_l7" },
	{ "l8", QCOM_SMD_RPM_LDOA, 8, &pm8941_pldo, "vdd_l8_l16_l18_l19" },
	{ "l9", QCOM_SMD_RPM_LDOA, 9, &pm8941_pldo, "vdd_l9_l10_l17_l22" },
	{ "l10", QCOM_SMD_RPM_LDOA, 10, &pm8941_pldo, "vdd_l9_l10_l17_l22" },
	{ "l11", QCOM_SMD_RPM_LDOA, 11, &pm8941_nldo, "vdd_l4_l11" },
	{ "l12", QCOM_SMD_RPM_LDOA, 12, &pm8941_pldo, "vdd_l6_l12_l14_l15" },
	{ "l13", QCOM_SMD_RPM_LDOA, 13, &pm8941_pldo, "vdd_l13_l20_l23_l24" },
	{ "l14", QCOM_SMD_RPM_LDOA, 14, &pm8941_pldo, "vdd_l6_l12_l14_l15" },
	{ "l15", QCOM_SMD_RPM_LDOA, 15, &pm8941_pldo, "vdd_l6_l12_l14_l15" },
	{ "l16", QCOM_SMD_RPM_LDOA, 16, &pm8941_pldo, "vdd_l8_l16_l18_l19" },
	{ "l17", QCOM_SMD_RPM_LDOA, 17, &pm8941_pldo, "vdd_l9_l10_l17_l22" },
	{ "l18", QCOM_SMD_RPM_LDOA, 18, &pm8941_pldo, "vdd_l8_l16_l18_l19" },
	{ "l19", QCOM_SMD_RPM_LDOA, 19, &pm8941_pldo, "vdd_l8_l16_l18_l19" },
	{ "l20", QCOM_SMD_RPM_LDOA, 20, &pm8941_pldo, "vdd_l13_l20_l23_l24" },
	{ "l21", QCOM_SMD_RPM_LDOA, 21, &pm8941_pldo, "vdd_l21" },
	{ "l22", QCOM_SMD_RPM_LDOA, 22, &pm8941_pldo, "vdd_l9_l10_l17_l22" },
	{ "l23", QCOM_SMD_RPM_LDOA, 23, &pm8941_pldo, "vdd_l13_l20_l23_l24" },
	{ "l24", QCOM_SMD_RPM_LDOA, 24, &pm8941_pldo, "vdd_l13_l20_l23_l24" },

	{ "lvs1", QCOM_SMD_RPM_VSA, 1, &pm8941_switch, "vdd_l2_lvs1_2_3" },
	{ "lvs2", QCOM_SMD_RPM_VSA, 2, &pm8941_switch, "vdd_l2_lvs1_2_3" },
	{ "lvs3", QCOM_SMD_RPM_VSA, 3, &pm8941_switch, "vdd_l2_lvs1_2_3" },

	{ "5vs1", QCOM_SMD_RPM_VSA, 4, &pm8941_switch, "vin_5vs" },
	{ "5vs2", QCOM_SMD_RPM_VSA, 5, &pm8941_switch, "vin_5vs" },

	{}
};

static const struct rpm_regulator_data rpm_pma8084_regulators[] = {
	{ "s1", QCOM_SMD_RPM_SMPA, 1, &pma8084_ftsmps, "vdd_s1" },
	{ "s2", QCOM_SMD_RPM_SMPA, 2, &pma8084_ftsmps, "vdd_s2" },
	{ "s3", QCOM_SMD_RPM_SMPA, 3, &pma8084_hfsmps, "vdd_s3" },
	{ "s4", QCOM_SMD_RPM_SMPA, 4, &pma8084_hfsmps, "vdd_s4" },
	{ "s5", QCOM_SMD_RPM_SMPA, 5, &pma8084_hfsmps, "vdd_s5" },
	{ "s6", QCOM_SMD_RPM_SMPA, 6, &pma8084_ftsmps, "vdd_s6" },
	{ "s7", QCOM_SMD_RPM_SMPA, 7, &pma8084_ftsmps, "vdd_s7" },
	{ "s8", QCOM_SMD_RPM_SMPA, 8, &pma8084_ftsmps, "vdd_s8" },
	{ "s9", QCOM_SMD_RPM_SMPA, 9, &pma8084_ftsmps, "vdd_s9" },
	{ "s10", QCOM_SMD_RPM_SMPA, 10, &pma8084_ftsmps, "vdd_s10" },
	{ "s11", QCOM_SMD_RPM_SMPA, 11, &pma8084_ftsmps, "vdd_s11" },
	{ "s12", QCOM_SMD_RPM_SMPA, 12, &pma8084_ftsmps, "vdd_s12" },

	{ "l1", QCOM_SMD_RPM_LDOA, 1, &pma8084_nldo, "vdd_l1_l11" },
	{ "l2", QCOM_SMD_RPM_LDOA, 2, &pma8084_nldo, "vdd_l2_l3_l4_l27" },
	{ "l3", QCOM_SMD_RPM_LDOA, 3, &pma8084_nldo, "vdd_l2_l3_l4_l27" },
	{ "l4", QCOM_SMD_RPM_LDOA, 4, &pma8084_nldo, "vdd_l2_l3_l4_l27" },
	{ "l5", QCOM_SMD_RPM_LDOA, 5, &pma8084_pldo, "vdd_l5_l7" },
	{ "l6", QCOM_SMD_RPM_LDOA, 6, &pma8084_pldo, "vdd_l6_l12_l14_l15_l26" },
	{ "l7", QCOM_SMD_RPM_LDOA, 7, &pma8084_pldo, "vdd_l5_l7" },
	{ "l8", QCOM_SMD_RPM_LDOA, 8, &pma8084_pldo, "vdd_l8" },
	{ "l9", QCOM_SMD_RPM_LDOA, 9, &pma8084_pldo, "vdd_l9_l10_l13_l20_l23_l24" },
	{ "l10", QCOM_SMD_RPM_LDOA, 10, &pma8084_pldo, "vdd_l9_l10_l13_l20_l23_l24" },
	{ "l11", QCOM_SMD_RPM_LDOA, 11, &pma8084_nldo, "vdd_l1_l11" },
	{ "l12", QCOM_SMD_RPM_LDOA, 12, &pma8084_pldo, "vdd_l6_l12_l14_l15_l26" },
	{ "l13", QCOM_SMD_RPM_LDOA, 13, &pma8084_pldo, "vdd_l9_l10_l13_l20_l23_l24" },
	{ "l14", QCOM_SMD_RPM_LDOA, 14, &pma8084_pldo, "vdd_l6_l12_l14_l15_l26" },
	{ "l15", QCOM_SMD_RPM_LDOA, 15, &pma8084_pldo, "vdd_l6_l12_l14_l15_l26" },
	{ "l16", QCOM_SMD_RPM_LDOA, 16, &pma8084_pldo, "vdd_l16_l25" },
	{ "l17", QCOM_SMD_RPM_LDOA, 17, &pma8084_pldo, "vdd_l17" },
	{ "l18", QCOM_SMD_RPM_LDOA, 18, &pma8084_pldo, "vdd_l18" },
	{ "l19", QCOM_SMD_RPM_LDOA, 19, &pma8084_pldo, "vdd_l19" },
	{ "l20", QCOM_SMD_RPM_LDOA, 20, &pma8084_pldo, "vdd_l9_l10_l13_l20_l23_l24" },
	{ "l21", QCOM_SMD_RPM_LDOA, 21, &pma8084_pldo, "vdd_l21" },
	{ "l22", QCOM_SMD_RPM_LDOA, 22, &pma8084_pldo, "vdd_l22" },
	{ "l23", QCOM_SMD_RPM_LDOA, 23, &pma8084_pldo, "vdd_l9_l10_l13_l20_l23_l24" },
	{ "l24", QCOM_SMD_RPM_LDOA, 24, &pma8084_pldo, "vdd_l9_l10_l13_l20_l23_l24" },
	{ "l25", QCOM_SMD_RPM_LDOA, 25, &pma8084_pldo, "vdd_l16_l25" },
	{ "l26", QCOM_SMD_RPM_LDOA, 26, &pma8084_pldo, "vdd_l6_l12_l14_l15_l26" },
	{ "l27", QCOM_SMD_RPM_LDOA, 27, &pma8084_nldo, "vdd_l2_l3_l4_l27" },

	{ "lvs1", QCOM_SMD_RPM_VSA, 1, &pma8084_switch },
	{ "lvs2", QCOM_SMD_RPM_VSA, 2, &pma8084_switch },
	{ "lvs3", QCOM_SMD_RPM_VSA, 3, &pma8084_switch },
	{ "lvs4", QCOM_SMD_RPM_VSA, 4, &pma8084_switch },
	{ "5vs1", QCOM_SMD_RPM_VSA, 5, &pma8084_switch },

	{}
};

static const struct rpm_regulator_data rpm_pm8994_regulators[] = {
	{ "s1", QCOM_SMD_RPM_SMPA, 1, &pm8994_ftsmps, "vdd_s1" },
	{ "s2", QCOM_SMD_RPM_SMPA, 2, &pm8994_ftsmps, "vdd_s2" },
	{ "s3", QCOM_SMD_RPM_SMPA, 3, &pm8994_hfsmps, "vdd_s3" },
	{ "s4", QCOM_SMD_RPM_SMPA, 4, &pm8994_hfsmps, "vdd_s4" },
	{ "s5", QCOM_SMD_RPM_SMPA, 5, &pm8994_hfsmps, "vdd_s5" },
	{ "s6", QCOM_SMD_RPM_SMPA, 6, &pm8994_ftsmps, "vdd_s6" },
	{ "s7", QCOM_SMD_RPM_SMPA, 7, &pm8994_hfsmps, "vdd_s7" },
	{ "s8", QCOM_SMD_RPM_SMPA, 8, &pm8994_ftsmps, "vdd_s8" },
	{ "s9", QCOM_SMD_RPM_SMPA, 9, &pm8994_ftsmps, "vdd_s9" },
	{ "s10", QCOM_SMD_RPM_SMPA, 10, &pm8994_ftsmps, "vdd_s10" },
	{ "s11", QCOM_SMD_RPM_SMPA, 11, &pm8994_ftsmps, "vdd_s11" },
	{ "s12", QCOM_SMD_RPM_SMPA, 12, &pm8994_ftsmps, "vdd_s12" },
	{ "l1", QCOM_SMD_RPM_LDOA, 1, &pm8994_nldo, "vdd_l1" },
	{ "l2", QCOM_SMD_RPM_LDOA, 2, &pm8994_nldo, "vdd_l2_l26_l28" },
	{ "l3", QCOM_SMD_RPM_LDOA, 3, &pm8994_nldo, "vdd_l3_l11" },
	{ "l4", QCOM_SMD_RPM_LDOA, 4, &pm8994_nldo, "vdd_l4_l27_l31" },
	{ "l5", QCOM_SMD_RPM_LDOA, 5, &pm8994_lnldo, "vdd_l5_l7" },
	{ "l6", QCOM_SMD_RPM_LDOA, 6, &pm8994_pldo, "vdd_l6_l12_l32" },
	{ "l7", QCOM_SMD_RPM_LDOA, 7, &pm8994_lnldo, "vdd_l5_l7" },
	{ "l8", QCOM_SMD_RPM_LDOA, 8, &pm8994_pldo, "vdd_l8_l16_l30" },
	{ "l9", QCOM_SMD_RPM_LDOA, 9, &pm8994_pldo, "vdd_l9_l10_l18_l22" },
	{ "l10", QCOM_SMD_RPM_LDOA, 10, &pm8994_pldo, "vdd_l9_l10_l18_l22" },
	{ "l11", QCOM_SMD_RPM_LDOA, 11, &pm8994_nldo, "vdd_l3_l11" },
	{ "l12", QCOM_SMD_RPM_LDOA, 12, &pm8994_pldo, "vdd_l6_l12_l32" },
	{ "l13", QCOM_SMD_RPM_LDOA, 13, &pm8994_pldo, "vdd_l13_l19_l23_l24" },
	{ "l14", QCOM_SMD_RPM_LDOA, 14, &pm8994_pldo, "vdd_l14_l15" },
	{ "l15", QCOM_SMD_RPM_LDOA, 15, &pm8994_pldo, "vdd_l14_l15" },
	{ "l16", QCOM_SMD_RPM_LDOA, 16, &pm8994_pldo, "vdd_l8_l16_l30" },
	{ "l17", QCOM_SMD_RPM_LDOA, 17, &pm8994_pldo, "vdd_l17_l29" },
	{ "l18", QCOM_SMD_RPM_LDOA, 18, &pm8994_pldo, "vdd_l9_l10_l18_l22" },
	{ "l19", QCOM_SMD_RPM_LDOA, 19, &pm8994_pldo, "vdd_l13_l19_l23_l24" },
	{ "l20", QCOM_SMD_RPM_LDOA, 20, &pm8994_pldo, "vdd_l20_l21" },
	{ "l21", QCOM_SMD_RPM_LDOA, 21, &pm8994_pldo, "vdd_l20_l21" },
	{ "l22", QCOM_SMD_RPM_LDOA, 22, &pm8994_pldo, "vdd_l9_l10_l18_l22" },
	{ "l23", QCOM_SMD_RPM_LDOA, 23, &pm8994_pldo, "vdd_l13_l19_l23_l24" },
	{ "l24", QCOM_SMD_RPM_LDOA, 24, &pm8994_pldo, "vdd_l13_l19_l23_l24" },
	{ "l25", QCOM_SMD_RPM_LDOA, 25, &pm8994_pldo, "vdd_l25" },
	{ "l26", QCOM_SMD_RPM_LDOA, 26, &pm8994_nldo, "vdd_l2_l26_l28" },
	{ "l27", QCOM_SMD_RPM_LDOA, 27, &pm8994_nldo, "vdd_l4_l27_l31" },
	{ "l28", QCOM_SMD_RPM_LDOA, 28, &pm8994_nldo, "vdd_l2_l26_l28" },
	{ "l29", QCOM_SMD_RPM_LDOA, 29, &pm8994_pldo, "vdd_l17_l29" },
	{ "l30", QCOM_SMD_RPM_LDOA, 30, &pm8994_pldo, "vdd_l8_l16_l30" },
	{ "l31", QCOM_SMD_RPM_LDOA, 31, &pm8994_nldo, "vdd_l4_l27_l31" },
	{ "l32", QCOM_SMD_RPM_LDOA, 32, &pm8994_pldo, "vdd_l6_l12_l32" },
	{ "lvs1", QCOM_SMD_RPM_VSA, 1, &pm8994_switch, "vdd_lvs1_2" },
	{ "lvs2", QCOM_SMD_RPM_VSA, 2, &pm8994_switch, "vdd_lvs1_2" },

	{}
};

static const struct of_device_id rpm_of_match[] = {
	{ .compatible = "qcom,rpm-pm8841-regulators", .data = &rpm_pm8841_regulators },
	{ .compatible = "qcom,rpm-pm8916-regulators", .data = &rpm_pm8916_regulators },
	{ .compatible = "qcom,rpm-pm8941-regulators", .data = &rpm_pm8941_regulators },
	{ .compatible = "qcom,rpm-pm8994-regulators", .data = &rpm_pm8994_regulators },
	{ .compatible = "qcom,rpm-pma8084-regulators", .data = &rpm_pma8084_regulators },
	{}
};
MODULE_DEVICE_TABLE(of, rpm_of_match);

static int rpm_reg_probe(struct platform_device *pdev)
{
	const struct rpm_regulator_data *reg;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct qcom_rpm_reg *vreg;
	struct qcom_smd_rpm *rpm;

	rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpm) {
		dev_err(&pdev->dev, "unable to retrieve handle to rpm\n");
		return -ENODEV;
	}

	match = of_match_device(rpm_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "failed to match device\n");
		return -ENODEV;
	}

	for (reg = match->data; reg->name; reg++) {
		vreg = devm_kzalloc(&pdev->dev, sizeof(*vreg), GFP_KERNEL);
		if (!vreg)
			return -ENOMEM;

		vreg->dev = &pdev->dev;
		vreg->type = reg->type;
		vreg->id = reg->id;
		vreg->rpm = rpm;

		memcpy(&vreg->desc, reg->desc, sizeof(vreg->desc));

		vreg->desc.id = -1;
		vreg->desc.owner = THIS_MODULE;
		vreg->desc.type = REGULATOR_VOLTAGE;
		vreg->desc.name = reg->name;
		vreg->desc.supply_name = reg->supply;
		vreg->desc.of_match = reg->name;

		config.dev = &pdev->dev;
		config.driver_data = vreg;
		rdev = devm_regulator_register(&pdev->dev, &vreg->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n", reg->name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static struct platform_driver rpm_reg_driver = {
	.probe = rpm_reg_probe,
	.driver = {
		.name  = "qcom_rpm_smd_regulator",
		.of_match_table = rpm_of_match,
	},
};

static int __init rpm_reg_init(void)
{
	return platform_driver_register(&rpm_reg_driver);
}
subsys_initcall(rpm_reg_init);

static void __exit rpm_reg_exit(void)
{
	platform_driver_unregister(&rpm_reg_driver);
}
module_exit(rpm_reg_exit)

MODULE_DESCRIPTION("Qualcomm RPM regulator driver");
MODULE_LICENSE("GPL v2");
