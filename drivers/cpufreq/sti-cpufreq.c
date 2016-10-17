/*
 * Match running platform with pre-defined OPP values for CPUFreq
 *
 * Author: Ajit Pal Singh <ajitpal.singh@st.com>
 *         Lee Jones <lee.jones@linaro.org>
 *
 * Copyright (C) 2015 STMicroelectronics (R&D) Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License as
 * published by the Free Software Foundation
 */

#include <linux/cpu.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>

#define VERSION_ELEMENTS	3
#define MAX_PCODE_NAME_LEN	7

#define VERSION_SHIFT		28
#define HW_INFO_INDEX		1
#define MAJOR_ID_INDEX		1
#define MINOR_ID_INDEX		2

/*
 * Only match on "suitable for ALL versions" entries
 *
 * This will be used with the BIT() macro.  It sets the
 * top bit of a 32bit value and is equal to 0x80000000.
 */
#define DEFAULT_VERSION		31

enum {
	PCODE = 0,
	SUBSTRATE,
	DVFS_MAX_REGFIELDS,
};

/**
 * ST CPUFreq Driver Data
 *
 * @cpu_node		CPU's OF node
 * @syscfg_eng		Engineering Syscon register map
 * @regmap		Syscon register map
 */
static struct sti_cpufreq_ddata {
	struct device *cpu;
	struct regmap *syscfg_eng;
	struct regmap *syscfg;
} ddata;

static int sti_cpufreq_fetch_major(void) {
	struct device_node *np = ddata.cpu->of_node;
	struct device *dev = ddata.cpu;
	unsigned int major_offset;
	unsigned int socid;
	int ret;

	ret = of_property_read_u32_index(np, "st,syscfg",
					 MAJOR_ID_INDEX, &major_offset);
	if (ret) {
		dev_err(dev, "No major number offset provided in %s [%d]\n",
			np->full_name, ret);
		return ret;
	}

	ret = regmap_read(ddata.syscfg, major_offset, &socid);
	if (ret) {
		dev_err(dev, "Failed to read major number from syscon [%d]\n",
			ret);
		return ret;
	}

	return ((socid >> VERSION_SHIFT) & 0xf) + 1;
}

static int sti_cpufreq_fetch_minor(void)
{
	struct device *dev = ddata.cpu;
	struct device_node *np = dev->of_node;
	unsigned int minor_offset;
	unsigned int minid;
	int ret;

	ret = of_property_read_u32_index(np, "st,syscfg-eng",
					 MINOR_ID_INDEX, &minor_offset);
	if (ret) {
		dev_err(dev,
			"No minor number offset provided %s [%d]\n",
			np->full_name, ret);
		return ret;
	}

	ret = regmap_read(ddata.syscfg_eng, minor_offset, &minid);
	if (ret) {
		dev_err(dev,
			"Failed to read the minor number from syscon [%d]\n",
			ret);
		return ret;
	}

	return minid & 0xf;
}

static int sti_cpufreq_fetch_regmap_field(const struct reg_field *reg_fields,
					  int hw_info_offset, int field)
{
	struct regmap_field *regmap_field;
	struct reg_field reg_field = reg_fields[field];
	struct device *dev = ddata.cpu;
	unsigned int value;
	int ret;

	reg_field.reg = hw_info_offset;
	regmap_field = devm_regmap_field_alloc(dev,
					       ddata.syscfg_eng,
					       reg_field);
	if (IS_ERR(regmap_field)) {
		dev_err(dev, "Failed to allocate reg field\n");
		return PTR_ERR(regmap_field);
	}

	ret = regmap_field_read(regmap_field, &value);
	if (ret) {
		dev_err(dev, "Failed to read %s code\n",
			field ? "SUBSTRATE" : "PCODE");
		return ret;
	}

	return value;
}

static const struct reg_field sti_stih407_dvfs_regfields[DVFS_MAX_REGFIELDS] = {
	[PCODE]		= REG_FIELD(0, 16, 19),
	[SUBSTRATE]	= REG_FIELD(0, 0, 2),
};

static const struct reg_field *sti_cpufreq_match(void)
{
	if (of_machine_is_compatible("st,stih407") ||
	    of_machine_is_compatible("st,stih410"))
		return sti_stih407_dvfs_regfields;

	return NULL;
}

static int sti_cpufreq_set_opp_info(void)
{
	struct device *dev = ddata.cpu;
	struct device_node *np = dev->of_node;
	const struct reg_field *reg_fields;
	unsigned int hw_info_offset;
	unsigned int version[VERSION_ELEMENTS];
	int pcode, substrate, major, minor;
	int ret;
	char name[MAX_PCODE_NAME_LEN];

	reg_fields = sti_cpufreq_match();
	if (!reg_fields) {
		dev_err(dev, "This SoC doesn't support voltage scaling\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_index(np, "st,syscfg-eng",
					 HW_INFO_INDEX, &hw_info_offset);
	if (ret) {
		dev_warn(dev, "Failed to read HW info offset from DT\n");
		substrate = DEFAULT_VERSION;
		pcode = 0;
		goto use_defaults;
	}

	pcode = sti_cpufreq_fetch_regmap_field(reg_fields,
					       hw_info_offset,
					       PCODE);
	if (pcode < 0) {
		dev_warn(dev, "Failed to obtain process code\n");
		/* Use default pcode */
		pcode = 0;
	}

	substrate = sti_cpufreq_fetch_regmap_field(reg_fields,
						   hw_info_offset,
						   SUBSTRATE);
	if (substrate) {
		dev_warn(dev, "Failed to obtain substrate code\n");
		/* Use default substrate */
		substrate = DEFAULT_VERSION;
	}

use_defaults:
	major = sti_cpufreq_fetch_major();
	if (major < 0) {
		dev_err(dev, "Failed to obtain major version\n");
		/* Use default major number */
		major = DEFAULT_VERSION;
	}

	minor = sti_cpufreq_fetch_minor();
	if (minor < 0) {
		dev_err(dev, "Failed to obtain minor version\n");
		/* Use default minor number */
		minor = DEFAULT_VERSION;
	}

	snprintf(name, MAX_PCODE_NAME_LEN, "pcode%d", pcode);

	ret = dev_pm_opp_set_prop_name(dev, name);
	if (ret) {
		dev_err(dev, "Failed to set prop name\n");
		return ret;
	}

	version[0] = BIT(major);
	version[1] = BIT(minor);
	version[2] = BIT(substrate);

	ret = dev_pm_opp_set_supported_hw(dev, version, VERSION_ELEMENTS);
	if (ret) {
		dev_err(dev, "Failed to set supported hardware\n");
		return ret;
	}

	dev_dbg(dev, "pcode: %d major: %d minor: %d substrate: %d\n",
		pcode, major, minor, substrate);
	dev_dbg(dev, "version[0]: %x version[1]: %x version[2]: %x\n",
		version[0], version[1], version[2]);

	return 0;
}

static int sti_cpufreq_fetch_syscon_regsiters(void)
{
	struct device *dev = ddata.cpu;
	struct device_node *np = dev->of_node;

	ddata.syscfg = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(ddata.syscfg)) {
		dev_err(dev,  "\"st,syscfg\" not supplied\n");
		return PTR_ERR(ddata.syscfg);
	}

	ddata.syscfg_eng = syscon_regmap_lookup_by_phandle(np, "st,syscfg-eng");
	if (IS_ERR(ddata.syscfg_eng)) {
		dev_err(dev, "\"st,syscfg-eng\" not supplied\n");
		return PTR_ERR(ddata.syscfg_eng);
	}

	return 0;
}

static int sti_cpufreq_init(void)
{
	int ret;

	if ((!of_machine_is_compatible("st,stih407")) &&
		(!of_machine_is_compatible("st,stih410")))
		return -ENODEV;

	ddata.cpu = get_cpu_device(0);
	if (!ddata.cpu) {
		dev_err(ddata.cpu, "Failed to get device for CPU0\n");
		goto skip_voltage_scaling;
	}

	if (!of_get_property(ddata.cpu->of_node, "operating-points-v2", NULL)) {
		dev_err(ddata.cpu, "OPP-v2 not supported\n");
		goto skip_voltage_scaling;
	}

	ret = sti_cpufreq_fetch_syscon_regsiters();
	if (ret)
		goto skip_voltage_scaling;

	ret = sti_cpufreq_set_opp_info();
	if (!ret)
		goto register_cpufreq_dt;

skip_voltage_scaling:
	dev_err(ddata.cpu, "Not doing voltage scaling\n");

register_cpufreq_dt:
	platform_device_register_simple("cpufreq-dt", -1, NULL, 0);

	return 0;
}
module_init(sti_cpufreq_init);

MODULE_DESCRIPTION("STMicroelectronics CPUFreq/OPP driver");
MODULE_AUTHOR("Ajitpal Singh <ajitpal.singh@st.com>");
MODULE_AUTHOR("Lee Jones <lee.jones@linaro.org>");
MODULE_LICENSE("GPL v2");
