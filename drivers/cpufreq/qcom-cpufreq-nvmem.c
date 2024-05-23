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
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>

#include <dt-bindings/arm/qcom,ids.h>

enum ipq806x_versions {
	IPQ8062_VERSION = 0,
	IPQ8064_VERSION,
	IPQ8065_VERSION,
};

#define IPQ6000_VERSION	BIT(2)

enum ipq8074_versions {
	IPQ8074_HAWKEYE_VERSION = 0,
	IPQ8074_ACORN_VERSION,
};

struct qcom_cpufreq_drv;

struct qcom_cpufreq_match_data {
	int (*get_version)(struct device *cpu_dev,
			   struct nvmem_cell *speedbin_nvmem,
			   char **pvs_name,
			   struct qcom_cpufreq_drv *drv);
	const char **genpd_names;
};

struct qcom_cpufreq_drv_cpu {
	int opp_token;
	struct device **virt_devs;
};

struct qcom_cpufreq_drv {
	u32 versions;
	const struct qcom_cpufreq_match_data *data;
	struct qcom_cpufreq_drv_cpu cpus[];
};

static struct platform_device *cpufreq_dt_pdev, *cpufreq_pdev;

static int qcom_cpufreq_simple_get_version(struct device *cpu_dev,
					   struct nvmem_cell *speedbin_nvmem,
					   char **pvs_name,
					   struct qcom_cpufreq_drv *drv)
{
	u8 *speedbin;

	*pvs_name = NULL;
	speedbin = nvmem_cell_read(speedbin_nvmem, NULL);
	if (IS_ERR(speedbin))
		return PTR_ERR(speedbin);

	dev_dbg(cpu_dev, "speedbin: %d\n", *speedbin);
	drv->versions = 1 << *speedbin;
	kfree(speedbin);
	return 0;
}

static void get_krait_bin_format_a(struct device *cpu_dev,
					  int *speed, int *pvs,
					  u8 *buf)
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
					  u8 *buf)
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

static int qcom_cpufreq_kryo_name_version(struct device *cpu_dev,
					  struct nvmem_cell *speedbin_nvmem,
					  char **pvs_name,
					  struct qcom_cpufreq_drv *drv)
{
	size_t len;
	u32 msm_id;
	u8 *speedbin;
	int ret;
	*pvs_name = NULL;

	ret = qcom_smem_get_soc_id(&msm_id);
	if (ret)
		return ret;

	speedbin = nvmem_cell_read(speedbin_nvmem, &len);
	if (IS_ERR(speedbin))
		return PTR_ERR(speedbin);

	switch (msm_id) {
	case QCOM_ID_MSM8996:
	case QCOM_ID_APQ8096:
	case QCOM_ID_IPQ5332:
	case QCOM_ID_IPQ5322:
	case QCOM_ID_IPQ5312:
	case QCOM_ID_IPQ5302:
	case QCOM_ID_IPQ5300:
	case QCOM_ID_IPQ9514:
	case QCOM_ID_IPQ9550:
	case QCOM_ID_IPQ9554:
	case QCOM_ID_IPQ9570:
	case QCOM_ID_IPQ9574:
		drv->versions = 1 << (unsigned int)(*speedbin);
		break;
	case QCOM_ID_MSM8996SG:
	case QCOM_ID_APQ8096SG:
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
	int ret = 0;

	speedbin = nvmem_cell_read(speedbin_nvmem, &len);

	if (IS_ERR(speedbin))
		return PTR_ERR(speedbin);

	switch (len) {
	case 4:
		get_krait_bin_format_a(cpu_dev, &speed, &pvs, speedbin);
		break;
	case 8:
		get_krait_bin_format_b(cpu_dev, &speed, &pvs, &pvs_ver,
				       speedbin);
		break;
	default:
		dev_err(cpu_dev, "Unable to read nvmem data. Defaulting to 0!\n");
		ret = -ENODEV;
		goto len_error;
	}

	snprintf(*pvs_name, sizeof("speedXX-pvsXX-vXX"), "speed%d-pvs%d-v%d",
		 speed, pvs, pvs_ver);

	drv->versions = (1 << speed);

len_error:
	kfree(speedbin);
	return ret;
}

static int qcom_cpufreq_ipq8064_name_version(struct device *cpu_dev,
					     struct nvmem_cell *speedbin_nvmem,
					     char **pvs_name,
					     struct qcom_cpufreq_drv *drv)
{
	int speed = 0, pvs = 0;
	int msm_id, ret = 0;
	u8 *speedbin;
	size_t len;

	speedbin = nvmem_cell_read(speedbin_nvmem, &len);
	if (IS_ERR(speedbin))
		return PTR_ERR(speedbin);

	if (len != 4) {
		dev_err(cpu_dev, "Unable to read nvmem data. Defaulting to 0!\n");
		ret = -ENODEV;
		goto exit;
	}

	get_krait_bin_format_a(cpu_dev, &speed, &pvs, speedbin);

	ret = qcom_smem_get_soc_id(&msm_id);
	if (ret)
		goto exit;

	switch (msm_id) {
	case QCOM_ID_IPQ8062:
		drv->versions = BIT(IPQ8062_VERSION);
		break;
	case QCOM_ID_IPQ8064:
	case QCOM_ID_IPQ8066:
	case QCOM_ID_IPQ8068:
		drv->versions = BIT(IPQ8064_VERSION);
		break;
	case QCOM_ID_IPQ8065:
	case QCOM_ID_IPQ8069:
		drv->versions = BIT(IPQ8065_VERSION);
		break;
	default:
		dev_err(cpu_dev,
			"SoC ID %u is not part of IPQ8064 family, limiting to 1.0GHz!\n",
			msm_id);
		drv->versions = BIT(IPQ8062_VERSION);
		break;
	}

	/* IPQ8064 speed is never fused. Only pvs values are fused. */
	snprintf(*pvs_name, sizeof("speed0-pvsXX"), "speed0-pvs%d", pvs);

exit:
	kfree(speedbin);
	return ret;
}

static int qcom_cpufreq_ipq6018_name_version(struct device *cpu_dev,
					     struct nvmem_cell *speedbin_nvmem,
					     char **pvs_name,
					     struct qcom_cpufreq_drv *drv)
{
	u32 msm_id;
	int ret;
	u8 *speedbin;
	*pvs_name = NULL;

	ret = qcom_smem_get_soc_id(&msm_id);
	if (ret)
		return ret;

	speedbin = nvmem_cell_read(speedbin_nvmem, NULL);
	if (IS_ERR(speedbin))
		return PTR_ERR(speedbin);

	switch (msm_id) {
	case QCOM_ID_IPQ6005:
	case QCOM_ID_IPQ6010:
	case QCOM_ID_IPQ6018:
	case QCOM_ID_IPQ6028:
		/* Fuse Value    Freq    BIT to set
		 * ---------------------------------
		 *   2’b0     No Limit     BIT(0)
		 *   2’b1     1.5 GHz      BIT(1)
		 */
		drv->versions = 1 << (unsigned int)(*speedbin);
		break;
	case QCOM_ID_IPQ6000:
		/*
		 * IPQ6018 family only has one bit to advertise the CPU
		 * speed-bin, but that is not enough for IPQ6000 which
		 * is only rated up to 1.2GHz.
		 * So for IPQ6000 manually set BIT(2) based on SMEM ID.
		 */
		drv->versions = IPQ6000_VERSION;
		break;
	default:
		dev_err(cpu_dev,
			"SoC ID %u is not part of IPQ6018 family, limiting to 1.2GHz!\n",
			msm_id);
		drv->versions = IPQ6000_VERSION;
		break;
	}

	kfree(speedbin);
	return 0;
}

static int qcom_cpufreq_ipq8074_name_version(struct device *cpu_dev,
					     struct nvmem_cell *speedbin_nvmem,
					     char **pvs_name,
					     struct qcom_cpufreq_drv *drv)
{
	u32 msm_id;
	int ret;
	*pvs_name = NULL;

	ret = qcom_smem_get_soc_id(&msm_id);
	if (ret)
		return ret;

	switch (msm_id) {
	case QCOM_ID_IPQ8070A:
	case QCOM_ID_IPQ8071A:
	case QCOM_ID_IPQ8172:
	case QCOM_ID_IPQ8173:
	case QCOM_ID_IPQ8174:
		drv->versions = BIT(IPQ8074_ACORN_VERSION);
		break;
	case QCOM_ID_IPQ8072A:
	case QCOM_ID_IPQ8074A:
	case QCOM_ID_IPQ8076A:
	case QCOM_ID_IPQ8078A:
		drv->versions = BIT(IPQ8074_HAWKEYE_VERSION);
		break;
	default:
		dev_err(cpu_dev,
			"SoC ID %u is not part of IPQ8074 family, limiting to 1.4GHz!\n",
			msm_id);
		drv->versions = BIT(IPQ8074_ACORN_VERSION);
		break;
	}

	return 0;
}

static const char *generic_genpd_names[] = { "perf", NULL };

static const struct qcom_cpufreq_match_data match_data_kryo = {
	.get_version = qcom_cpufreq_kryo_name_version,
};

static const struct qcom_cpufreq_match_data match_data_krait = {
	.get_version = qcom_cpufreq_krait_name_version,
};

static const struct qcom_cpufreq_match_data match_data_msm8909 = {
	.get_version = qcom_cpufreq_simple_get_version,
	.genpd_names = generic_genpd_names,
};

static const char *qcs404_genpd_names[] = { "cpr", NULL };

static const struct qcom_cpufreq_match_data match_data_qcs404 = {
	.genpd_names = qcs404_genpd_names,
};

static const struct qcom_cpufreq_match_data match_data_ipq6018 = {
	.get_version = qcom_cpufreq_ipq6018_name_version,
};

static const struct qcom_cpufreq_match_data match_data_ipq8064 = {
	.get_version = qcom_cpufreq_ipq8064_name_version,
};

static const struct qcom_cpufreq_match_data match_data_ipq8074 = {
	.get_version = qcom_cpufreq_ipq8074_name_version,
};

static void qcom_cpufreq_suspend_virt_devs(struct qcom_cpufreq_drv *drv, unsigned int cpu)
{
	const char * const *name = drv->data->genpd_names;
	int i;

	if (!drv->cpus[cpu].virt_devs)
		return;

	for (i = 0; *name; i++, name++)
		device_set_awake_path(drv->cpus[cpu].virt_devs[i]);
}

static void qcom_cpufreq_put_virt_devs(struct qcom_cpufreq_drv *drv, unsigned int cpu)
{
	const char * const *name = drv->data->genpd_names;
	int i;

	if (!drv->cpus[cpu].virt_devs)
		return;

	for (i = 0; *name; i++, name++)
		pm_runtime_put(drv->cpus[cpu].virt_devs[i]);
}

static int qcom_cpufreq_probe(struct platform_device *pdev)
{
	struct qcom_cpufreq_drv *drv;
	struct nvmem_cell *speedbin_nvmem;
	struct device_node *np;
	struct device *cpu_dev;
	char pvs_name_buffer[] = "speedXX-pvsXX-vXX";
	char *pvs_name = pvs_name_buffer;
	unsigned cpu;
	const struct of_device_id *match;
	int ret;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev)
		return -ENODEV;

	np = dev_pm_opp_of_get_opp_desc_node(cpu_dev);
	if (!np)
		return -ENOENT;

	ret = of_device_is_compatible(np, "operating-points-v2-kryo-cpu") ||
	      of_device_is_compatible(np, "operating-points-v2-krait-cpu");
	if (!ret) {
		of_node_put(np);
		return -ENOENT;
	}

	drv = devm_kzalloc(&pdev->dev, struct_size(drv, cpus, num_possible_cpus()),
		           GFP_KERNEL);
	if (!drv) {
		of_node_put(np);
		return -ENOMEM;
	}

	match = pdev->dev.platform_data;
	drv->data = match->data;
	if (!drv->data) {
		of_node_put(np);
		return -ENODEV;
	}

	if (drv->data->get_version) {
		speedbin_nvmem = of_nvmem_cell_get(np, NULL);
		if (IS_ERR(speedbin_nvmem)) {
			of_node_put(np);
			return dev_err_probe(cpu_dev, PTR_ERR(speedbin_nvmem),
					     "Could not get nvmem cell\n");
		}

		ret = drv->data->get_version(cpu_dev,
							speedbin_nvmem, &pvs_name, drv);
		if (ret) {
			of_node_put(np);
			nvmem_cell_put(speedbin_nvmem);
			return ret;
		}
		nvmem_cell_put(speedbin_nvmem);
	}
	of_node_put(np);

	for_each_possible_cpu(cpu) {
		struct device **virt_devs = NULL;
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
			config.virt_devs = &virt_devs;
		}

		if (config.supported_hw || config.genpd_names) {
			drv->cpus[cpu].opp_token = dev_pm_opp_set_config(cpu_dev, &config);
			if (drv->cpus[cpu].opp_token < 0) {
				ret = drv->cpus[cpu].opp_token;
				dev_err(cpu_dev, "Failed to set OPP config\n");
				goto free_opp;
			}
		}

		if (virt_devs) {
			const char * const *name = config.genpd_names;
			int i, j;

			for (i = 0; *name; i++, name++) {
				ret = pm_runtime_resume_and_get(virt_devs[i]);
				if (ret) {
					dev_err(cpu_dev, "failed to resume %s: %d\n",
						*name, ret);

					/* Rollback previous PM runtime calls */
					name = config.genpd_names;
					for (j = 0; *name && j < i; j++, name++)
						pm_runtime_put(virt_devs[j]);

					goto free_opp;
				}
			}
			drv->cpus[cpu].virt_devs = virt_devs;
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
	for_each_possible_cpu(cpu) {
		qcom_cpufreq_put_virt_devs(drv, cpu);
		dev_pm_opp_clear_config(drv->cpus[cpu].opp_token);
	}
	return ret;
}

static void qcom_cpufreq_remove(struct platform_device *pdev)
{
	struct qcom_cpufreq_drv *drv = platform_get_drvdata(pdev);
	unsigned int cpu;

	platform_device_unregister(cpufreq_dt_pdev);

	for_each_possible_cpu(cpu) {
		qcom_cpufreq_put_virt_devs(drv, cpu);
		dev_pm_opp_clear_config(drv->cpus[cpu].opp_token);
	}
}

static int qcom_cpufreq_suspend(struct device *dev)
{
	struct qcom_cpufreq_drv *drv = dev_get_drvdata(dev);
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		qcom_cpufreq_suspend_virt_devs(drv, cpu);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(qcom_cpufreq_pm_ops, qcom_cpufreq_suspend, NULL);

static struct platform_driver qcom_cpufreq_driver = {
	.probe = qcom_cpufreq_probe,
	.remove_new = qcom_cpufreq_remove,
	.driver = {
		.name = "qcom-cpufreq-nvmem",
		.pm = pm_sleep_ptr(&qcom_cpufreq_pm_ops),
	},
};

static const struct of_device_id qcom_cpufreq_match_list[] __initconst = {
	{ .compatible = "qcom,apq8096", .data = &match_data_kryo },
	{ .compatible = "qcom,msm8909", .data = &match_data_msm8909 },
	{ .compatible = "qcom,msm8996", .data = &match_data_kryo },
	{ .compatible = "qcom,qcs404", .data = &match_data_qcs404 },
	{ .compatible = "qcom,ipq5332", .data = &match_data_kryo },
	{ .compatible = "qcom,ipq6018", .data = &match_data_ipq6018 },
	{ .compatible = "qcom,ipq8064", .data = &match_data_ipq8064 },
	{ .compatible = "qcom,ipq8074", .data = &match_data_ipq8074 },
	{ .compatible = "qcom,apq8064", .data = &match_data_krait },
	{ .compatible = "qcom,ipq9574", .data = &match_data_kryo },
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
