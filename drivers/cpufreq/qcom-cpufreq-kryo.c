// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

/*
 * In Certain QCOM SoCs like apq8096 and msm8996 that have KRYO processors,
 * the CPU frequency subset and voltage value of each OPP varies
 * based on the silicon variant in use. Qualcomm Process Voltage Scaling Tables
 * defines the voltage and frequency value based on the msm-id in SMEM
 * and speedbin blown in the efuse combination.
 * The qcom-cpufreq-kryo driver reads the msm-id and efuse value from the SoC
 * to provide the OPP framework with required information.
 * This is used to determine the voltage and frequency value for each OPP of
 * operating-points-v2 table when it is parsed by the OPP framework.
 */

#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>

#define MSM_ID_SMEM	137

enum _msm_id {
	MSM8996V3 = 0xF6ul,
	APQ8096V3 = 0x123ul,
	MSM8996SG = 0x131ul,
	APQ8096SG = 0x138ul,
};

enum _msm8996_version {
	MSM8996_V3,
	MSM8996_SG,
	NUM_OF_MSM8996_VERSIONS,
};

static enum _msm8996_version __init qcom_cpufreq_kryo_get_msm_id(void)
{
	size_t len;
	u32 *msm_id;
	enum _msm8996_version version;

	msm_id = qcom_smem_get(QCOM_SMEM_HOST_ANY, MSM_ID_SMEM, &len);
	if (IS_ERR(msm_id))
		return NUM_OF_MSM8996_VERSIONS;

	/* The first 4 bytes are format, next to them is the actual msm-id */
	msm_id++;

	switch ((enum _msm_id)*msm_id) {
	case MSM8996V3:
	case APQ8096V3:
		version = MSM8996_V3;
		break;
	case MSM8996SG:
	case APQ8096SG:
		version = MSM8996_SG;
		break;
	default:
		version = NUM_OF_MSM8996_VERSIONS;
	}

	return version;
}

static int qcom_cpufreq_kryo_probe(struct platform_device *pdev)
{
	struct opp_table *opp_tables[NR_CPUS] = {0};
	struct platform_device *cpufreq_dt_pdev;
	enum _msm8996_version msm8996_version;
	struct nvmem_cell *speedbin_nvmem;
	struct device_node *np;
	struct device *cpu_dev;
	unsigned cpu;
	u8 *speedbin;
	u32 versions;
	size_t len;
	int ret;

	cpu_dev = get_cpu_device(0);
	if (NULL == cpu_dev)
		ret = -ENODEV;

	msm8996_version = qcom_cpufreq_kryo_get_msm_id();
	if (NUM_OF_MSM8996_VERSIONS == msm8996_version) {
		dev_err(cpu_dev, "Not Snapdragon 820/821!");
		return -ENODEV;
	}

	np = dev_pm_opp_of_get_opp_desc_node(cpu_dev);
	if (IS_ERR(np))
		return PTR_ERR(np);

	ret = of_device_is_compatible(np, "operating-points-v2-kryo-cpu");
	if (!ret) {
		of_node_put(np);
		return -ENOENT;
	}

	speedbin_nvmem = of_nvmem_cell_get(np, NULL);
	of_node_put(np);
	if (IS_ERR(speedbin_nvmem)) {
		dev_err(cpu_dev, "Could not get nvmem cell: %ld\n",
			PTR_ERR(speedbin_nvmem));
		return PTR_ERR(speedbin_nvmem);
	}

	speedbin = nvmem_cell_read(speedbin_nvmem, &len);
	nvmem_cell_put(speedbin_nvmem);

	switch (msm8996_version) {
	case MSM8996_V3:
		versions = 1 << (unsigned int)(*speedbin);
		break;
	case MSM8996_SG:
		versions = 1 << ((unsigned int)(*speedbin) + 4);
		break;
	default:
		BUG();
		break;
	}

	for_each_possible_cpu(cpu) {
		cpu_dev = get_cpu_device(cpu);
		if (NULL == cpu_dev) {
			ret = -ENODEV;
			goto free_opp;
		}

		opp_tables[cpu] = dev_pm_opp_set_supported_hw(cpu_dev,
							      &versions, 1);
		if (IS_ERR(opp_tables[cpu])) {
			ret = PTR_ERR(opp_tables[cpu]);
			dev_err(cpu_dev, "Failed to set supported hardware\n");
			goto free_opp;
		}
	}

	cpufreq_dt_pdev = platform_device_register_simple("cpufreq-dt", -1,
							  NULL, 0);
	if (!IS_ERR(cpufreq_dt_pdev))
		return 0;

	ret = PTR_ERR(cpufreq_dt_pdev);
	dev_err(cpu_dev, "Failed to register platform device\n");

free_opp:
	for_each_possible_cpu(cpu) {
		if (IS_ERR_OR_NULL(opp_tables[cpu]))
			break;
		dev_pm_opp_put_supported_hw(opp_tables[cpu]);
	}

	return ret;
}

static struct platform_driver qcom_cpufreq_kryo_driver = {
	.probe = qcom_cpufreq_kryo_probe,
	.driver = {
		.name = "qcom-cpufreq-kryo",
	},
};

static const struct of_device_id qcom_cpufreq_kryo_match_list[] __initconst = {
	{ .compatible = "qcom,apq8096", },
	{ .compatible = "qcom,msm8996", },
};

/*
 * Since the driver depends on smem and nvmem drivers, which may
 * return EPROBE_DEFER, all the real activity is done in the probe,
 * which may be defered as well. The init here is only registering
 * the driver and the platform device.
 */
static int __init qcom_cpufreq_kryo_init(void)
{
	struct device_node *np = of_find_node_by_path("/");
	const struct of_device_id *match;
	int ret;

	if (!np)
		return -ENODEV;

	match = of_match_node(qcom_cpufreq_kryo_match_list, np);
	of_node_put(np);
	if (!match)
		return -ENODEV;

	ret = platform_driver_register(&qcom_cpufreq_kryo_driver);
	if (unlikely(ret < 0))
		return ret;

	ret = PTR_ERR_OR_ZERO(platform_device_register_simple(
		"qcom-cpufreq-kryo", -1, NULL, 0));
	if (0 == ret)
		return 0;

	platform_driver_unregister(&qcom_cpufreq_kryo_driver);
	return ret;
}
module_init(qcom_cpufreq_kryo_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Kryo CPUfreq driver");
MODULE_LICENSE("GPL v2");
