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
 * The qcom-cpufreq-nvmem driver reads the msm-id and efuse value from the SoC
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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
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

struct qcom_cpufreq_drv;

struct qcom_cpufreq_match_data {
	int (*get_version)(struct device *cpu_dev,
			   struct nvmem_cell *speedbin_nvmem,
			   char **pvs_name,
			   struct qcom_cpufreq_drv *drv);
	const char **genpd_names;
};

struct qcom_cpufreq_drv {
	int *opp_tokens;
	u32 versions;
	const struct qcom_cpufreq_match_data *data;
};

static struct platform_device *cpufreq_dt_pdev, *cpufreq_pdev;

static void get_krait_bin_format_a(struct device *cpu_dev,
					  int *speed, int *pvs, int *pvs_ver,
					  struct nvmem_cell *pvs_nvmem, u8 *buf)
{
	u32 pte_efuse;

	pte_efuse = *((u32 *)buf);

	*speed = pte_efuse & 0xf;
	if (*speed == 0xf)
		*speed = (pte_efuse >> 4) & 0xf;

	if (*speed == 0xf) {
		*speed = 0;
		dev_warn(cpu_dev, "Speed bin: Defaulting to %d\n", *speed);
	} else {
		dev_dbg(cpu_dev, "Speed bin: %d\n", *speed);
	}

	*pvs = (pte_efuse >> 10) & 0x7;
	if (*pvs == 0x7)
		*pvs = (pte_efuse >> 13) & 0x7;

	if (*pvs == 0x7) {
		*pvs = 0;
		dev_warn(cpu_dev, "PVS bin: Defaulting to %d\n", *pvs);
	} else {
		dev_dbg(cpu_dev, "PVS bin: %d\n", *pvs);
	}
}

static void get_krait_bin_format_b(struct device *cpu_dev,
					  int *speed, int *pvs, int *pvs_ver,
					  struct nvmem_cell *pvs_nvmem, u8 *buf)
{
	u32 pte_efuse, redundant_sel;

	pte_efuse = *((u32 *)buf);
	redundant_sel = (pte_efuse >> 24) & 0x7;

	*pvs_ver = (pte_efuse >> 4) & 0x3;

	switch (redundant_sel) {
	case 1:
		*pvs = ((pte_efuse >> 28) & 0x8) | ((pte_efuse >> 6) & 0x7);
		*speed = (pte_efuse >> 27) & 0xf;
		break;
	case 2:
		*pvs = (pte_efuse >> 27) & 0xf;
		*speed = pte_efuse & 0x7;
		break;
	default:
		/* 4 bits of PVS are in efuse register bits 31, 8-6. */
		*pvs = ((pte_efuse >> 28) & 0x8) | ((pte_efuse >> 6) & 0x7);
		*speed = pte_efuse & 0x7;
	}

	/* Check SPEED_BIN_BLOW_STATUS */
	if (pte_efuse & BIT(3)) {
		dev_dbg(cpu_dev, "Speed bin: %d\n", *speed);
	} else {
		dev_warn(cpu_dev, "Speed bin not set. Defaulting to 0!\n");
		*speed = 0;
	}

	/* Check PVS_BLOW_STATUS */
	pte_efuse = *(((u32 *)buf) + 1);
	pte_efuse &= BIT(21);
	if (pte_efuse) {
		dev_dbg(cpu_dev, "PVS bin: %d\n", *pvs);
	} else {
		dev_warn(cpu_dev, "PVS bin not set. Defaulting to 0!\n");
		*pvs = 0;
	}

	dev_dbg(cpu_dev, "PVS version: %d\n", *pvs_ver);
}

static enum _msm8996_version qcom_cpufreq_get_msm_id(void)
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

static int qcom_cpufreq_kryo_name_version(struct device *cpu_dev,
					  struct nvmem_cell *speedbin_nvmem,
					  char **pvs_name,
					  struct qcom_cpufreq_drv *drv)
{
	size_t len;
	u8 *speedbin;
	enum _msm8996_version msm8996_version;
	*pvs_name = NULL;

	msm8996_version = qcom_cpufreq_get_msm_id();
	if (NUM_OF_MSM8996_VERSIONS == msm8996_version) {
		dev_err(cpu_dev, "Not Snapdragon 820/821!");
		return -ENODEV;
	}

	speedbin = nvmem_cell_read(speedbin_nvmem, &len);
	if (IS_ERR(speedbin))
		return PTR_ERR(speedbin);

	switch (msm8996_version) {
	case MSM8996_V3:
		drv->versions = 1 << (unsigned int)(*speedbin);
		break;
	case MSM8996_SG:
		drv->versions = 1 << ((unsigned int)(*speedbin) + 4);
		break;
	default:
		BUG();
		break;
	}

	kfree(speedbin);
	return 0;
}

static int qcom_cpufreq_krait_name_version(struct device *cpu_dev,
					   struct nvmem_cell *speedbin_nvmem,
					   char **pvs_name,
					   struct qcom_cpufreq_drv *drv)
{
	int speed = 0, pvs = 0, pvs_ver = 0;
	u8 *speedbin;
	size_t len;

	speedbin = nvmem_cell_read(speedbin_nvmem, &len);

	if (IS_ERR(speedbin))
		return PTR_ERR(speedbin);

	switch (len) {
	case 4:
		get_krait_bin_format_a(cpu_dev, &speed, &pvs, &pvs_ver,
				       speedbin_nvmem, speedbin);
		break;
	case 8:
		get_krait_bin_format_b(cpu_dev, &speed, &pvs, &pvs_ver,
				       speedbin_nvmem, speedbin);
		break;
	default:
		dev_err(cpu_dev, "Unable to read nvmem data. Defaulting to 0!\n");
		return -ENODEV;
	}

	snprintf(*pvs_name, sizeof("speedXX-pvsXX-vXX"), "speed%d-pvs%d-v%d",
		 speed, pvs, pvs_ver);

	drv->versions = (1 << speed);

	kfree(speedbin);
	return 0;
}

static const struct qcom_cpufreq_match_data match_data_kryo = {
	.get_version = qcom_cpufreq_kryo_name_version,
};

static const struct qcom_cpufreq_match_data match_data_krait = {
	.get_version = qcom_cpufreq_krait_name_version,
};

static const char *qcs404_genpd_names[] = { "cpr", NULL };

static const struct qcom_cpufreq_match_data match_data_qcs404 = {
	.genpd_names = qcs404_genpd_names,
};

static int qcom_cpufreq_probe(struct platform_device *pdev)
{
	struct qcom_cpufreq_drv *drv;
	struct nvmem_cell *speedbin_nvmem;
	struct device_node *np;
	struct device *cpu_dev;
	char *pvs_name = "speedXX-pvsXX-vXX";
	unsigned cpu;
	const struct of_device_id *match;
	int ret;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev)
		return -ENODEV;

	np = dev_pm_opp_of_get_opp_desc_node(cpu_dev);
	if (!np)
		return -ENOENT;

	ret = of_device_is_compatible(np, "operating-points-v2-kryo-cpu");
	if (!ret) {
		of_node_put(np);
		return -ENOENT;
	}

	drv = kzalloc(sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	match = pdev->dev.platform_data;
	drv->data = match->data;
	if (!drv->data) {
		ret = -ENODEV;
		goto free_drv;
	}

	if (drv->data->get_version) {
		speedbin_nvmem = of_nvmem_cell_get(np, NULL);
		if (IS_ERR(speedbin_nvmem)) {
			if (PTR_ERR(speedbin_nvmem) != -EPROBE_DEFER)
				dev_err(cpu_dev,
					"Could not get nvmem cell: %ld\n",
					PTR_ERR(speedbin_nvmem));
			ret = PTR_ERR(speedbin_nvmem);
			goto free_drv;
		}

		ret = drv->data->get_version(cpu_dev,
							speedbin_nvmem, &pvs_name, drv);
		if (ret) {
			nvmem_cell_put(speedbin_nvmem);
			goto free_drv;
		}
		nvmem_cell_put(speedbin_nvmem);
	}
	of_node_put(np);

	drv->opp_tokens = kcalloc(num_possible_cpus(), sizeof(*drv->opp_tokens),
				  GFP_KERNEL);
	if (!drv->opp_tokens) {
		ret = -ENOMEM;
		goto free_drv;
	}

	for_each_possible_cpu(cpu) {
		struct dev_pm_opp_config config = {
			.supported_hw = NULL,
		};

		cpu_dev = get_cpu_device(cpu);
		if (NULL == cpu_dev) {
			ret = -ENODEV;
			goto free_opp;
		}

		if (drv->data->get_version) {
			config.supported_hw = &drv->versions;
			config.supported_hw_count = 1;

			if (pvs_name)
				config.prop_name = pvs_name;
		}

		if (drv->data->genpd_names) {
			config.genpd_names = drv->data->genpd_names;
			config.virt_devs = NULL;
		}

		if (config.supported_hw || config.genpd_names) {
			drv->opp_tokens[cpu] = dev_pm_opp_set_config(cpu_dev, &config);
			if (drv->opp_tokens[cpu] < 0) {
				ret = drv->opp_tokens[cpu];
				dev_err(cpu_dev, "Failed to set OPP config\n");
				goto free_opp;
			}
		}
	}

	cpufreq_dt_pdev = platform_device_register_simple("cpufreq-dt", -1,
							  NULL, 0);
	if (!IS_ERR(cpufreq_dt_pdev)) {
		platform_set_drvdata(pdev, drv);
		return 0;
	}

	ret = PTR_ERR(cpufreq_dt_pdev);
	dev_err(cpu_dev, "Failed to register platform device\n");

free_opp:
	for_each_possible_cpu(cpu)
		dev_pm_opp_clear_config(drv->opp_tokens[cpu]);
	kfree(drv->opp_tokens);
free_drv:
	kfree(drv);

	return ret;
}

static int qcom_cpufreq_remove(struct platform_device *pdev)
{
	struct qcom_cpufreq_drv *drv = platform_get_drvdata(pdev);
	unsigned int cpu;

	platform_device_unregister(cpufreq_dt_pdev);

	for_each_possible_cpu(cpu)
		dev_pm_opp_clear_config(drv->opp_tokens[cpu]);

	kfree(drv->opp_tokens);
	kfree(drv);

	return 0;
}

static struct platform_driver qcom_cpufreq_driver = {
	.probe = qcom_cpufreq_probe,
	.remove = qcom_cpufreq_remove,
	.driver = {
		.name = "qcom-cpufreq-nvmem",
	},
};

static const struct of_device_id qcom_cpufreq_match_list[] __initconst = {
	{ .compatible = "qcom,apq8096", .data = &match_data_kryo },
	{ .compatible = "qcom,msm8996", .data = &match_data_kryo },
	{ .compatible = "qcom,qcs404", .data = &match_data_qcs404 },
	{ .compatible = "qcom,ipq8064", .data = &match_data_krait },
	{ .compatible = "qcom,apq8064", .data = &match_data_krait },
	{ .compatible = "qcom,msm8974", .data = &match_data_krait },
	{ .compatible = "qcom,msm8960", .data = &match_data_krait },
	{},
};
MODULE_DEVICE_TABLE(of, qcom_cpufreq_match_list);

/*
 * Since the driver depends on smem and nvmem drivers, which may
 * return EPROBE_DEFER, all the real activity is done in the probe,
 * which may be defered as well. The init here is only registering
 * the driver and the platform device.
 */
static int __init qcom_cpufreq_init(void)
{
	struct device_node *np = of_find_node_by_path("/");
	const struct of_device_id *match;
	int ret;

	if (!np)
		return -ENODEV;

	match = of_match_node(qcom_cpufreq_match_list, np);
	of_node_put(np);
	if (!match)
		return -ENODEV;

	ret = platform_driver_register(&qcom_cpufreq_driver);
	if (unlikely(ret < 0))
		return ret;

	cpufreq_pdev = platform_device_register_data(NULL, "qcom-cpufreq-nvmem",
						     -1, match, sizeof(*match));
	ret = PTR_ERR_OR_ZERO(cpufreq_pdev);
	if (0 == ret)
		return 0;

	platform_driver_unregister(&qcom_cpufreq_driver);
	return ret;
}
module_init(qcom_cpufreq_init);

static void __exit qcom_cpufreq_exit(void)
{
	platform_device_unregister(cpufreq_pdev);
	platform_driver_unregister(&qcom_cpufreq_driver);
}
module_exit(qcom_cpufreq_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. CPUfreq driver");
MODULE_LICENSE("GPL v2");
