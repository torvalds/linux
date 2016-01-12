/*
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014,2015, Linaro Ltd.
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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/qcom_scm.h>

#include <asm/cpuidle.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>

#define MAX_PMIC_DATA		2
#define MAX_SEQ_DATA		64
#define SPM_CTL_INDEX		0x7f
#define SPM_CTL_INDEX_SHIFT	4
#define SPM_CTL_EN		BIT(0)

enum pm_sleep_mode {
	PM_SLEEP_MODE_STBY,
	PM_SLEEP_MODE_RET,
	PM_SLEEP_MODE_SPC,
	PM_SLEEP_MODE_PC,
	PM_SLEEP_MODE_NR,
};

enum spm_reg {
	SPM_REG_CFG,
	SPM_REG_SPM_CTL,
	SPM_REG_DLY,
	SPM_REG_PMIC_DLY,
	SPM_REG_PMIC_DATA_0,
	SPM_REG_PMIC_DATA_1,
	SPM_REG_VCTL,
	SPM_REG_SEQ_ENTRY,
	SPM_REG_SPM_STS,
	SPM_REG_PMIC_STS,
	SPM_REG_NR,
};

struct spm_reg_data {
	const u8 *reg_offset;
	u32 spm_cfg;
	u32 spm_dly;
	u32 pmic_dly;
	u32 pmic_data[MAX_PMIC_DATA];
	u8 seq[MAX_SEQ_DATA];
	u8 start_index[PM_SLEEP_MODE_NR];
};

struct spm_driver_data {
	void __iomem *reg_base;
	const struct spm_reg_data *reg_data;
};

static const u8 spm_reg_offset_v2_1[SPM_REG_NR] = {
	[SPM_REG_CFG]		= 0x08,
	[SPM_REG_SPM_CTL]	= 0x30,
	[SPM_REG_DLY]		= 0x34,
	[SPM_REG_SEQ_ENTRY]	= 0x80,
};

/* SPM register data for 8974, 8084 */
static const struct spm_reg_data spm_reg_8974_8084_cpu  = {
	.reg_offset = spm_reg_offset_v2_1,
	.spm_cfg = 0x1,
	.spm_dly = 0x3C102800,
	.seq = { 0x03, 0x0B, 0x0F, 0x00, 0x20, 0x80, 0x10, 0xE8, 0x5B, 0x03,
		0x3B, 0xE8, 0x5B, 0x82, 0x10, 0x0B, 0x30, 0x06, 0x26, 0x30,
		0x0F },
	.start_index[PM_SLEEP_MODE_STBY] = 0,
	.start_index[PM_SLEEP_MODE_SPC] = 3,
};

static const u8 spm_reg_offset_v1_1[SPM_REG_NR] = {
	[SPM_REG_CFG]		= 0x08,
	[SPM_REG_SPM_CTL]	= 0x20,
	[SPM_REG_PMIC_DLY]	= 0x24,
	[SPM_REG_PMIC_DATA_0]	= 0x28,
	[SPM_REG_PMIC_DATA_1]	= 0x2C,
	[SPM_REG_SEQ_ENTRY]	= 0x80,
};

/* SPM register data for 8064 */
static const struct spm_reg_data spm_reg_8064_cpu = {
	.reg_offset = spm_reg_offset_v1_1,
	.spm_cfg = 0x1F,
	.pmic_dly = 0x02020004,
	.pmic_data[0] = 0x0084009C,
	.pmic_data[1] = 0x00A4001C,
	.seq = { 0x03, 0x0F, 0x00, 0x24, 0x54, 0x10, 0x09, 0x03, 0x01,
		0x10, 0x54, 0x30, 0x0C, 0x24, 0x30, 0x0F },
	.start_index[PM_SLEEP_MODE_STBY] = 0,
	.start_index[PM_SLEEP_MODE_SPC] = 2,
};

static DEFINE_PER_CPU(struct spm_driver_data *, cpu_spm_drv);

typedef int (*idle_fn)(void);
static DEFINE_PER_CPU(idle_fn*, qcom_idle_ops);

static inline void spm_register_write(struct spm_driver_data *drv,
					enum spm_reg reg, u32 val)
{
	if (drv->reg_data->reg_offset[reg])
		writel_relaxed(val, drv->reg_base +
				drv->reg_data->reg_offset[reg]);
}

/* Ensure a guaranteed write, before return */
static inline void spm_register_write_sync(struct spm_driver_data *drv,
					enum spm_reg reg, u32 val)
{
	u32 ret;

	if (!drv->reg_data->reg_offset[reg])
		return;

	do {
		writel_relaxed(val, drv->reg_base +
				drv->reg_data->reg_offset[reg]);
		ret = readl_relaxed(drv->reg_base +
				drv->reg_data->reg_offset[reg]);
		if (ret == val)
			break;
		cpu_relax();
	} while (1);
}

static inline u32 spm_register_read(struct spm_driver_data *drv,
					enum spm_reg reg)
{
	return readl_relaxed(drv->reg_base + drv->reg_data->reg_offset[reg]);
}

static void spm_set_low_power_mode(struct spm_driver_data *drv,
					enum pm_sleep_mode mode)
{
	u32 start_index;
	u32 ctl_val;

	start_index = drv->reg_data->start_index[mode];

	ctl_val = spm_register_read(drv, SPM_REG_SPM_CTL);
	ctl_val &= ~(SPM_CTL_INDEX << SPM_CTL_INDEX_SHIFT);
	ctl_val |= start_index << SPM_CTL_INDEX_SHIFT;
	ctl_val |= SPM_CTL_EN;
	spm_register_write_sync(drv, SPM_REG_SPM_CTL, ctl_val);
}

static int qcom_pm_collapse(unsigned long int unused)
{
	qcom_scm_cpu_power_down(QCOM_SCM_CPU_PWR_DOWN_L2_ON);

	/*
	 * Returns here only if there was a pending interrupt and we did not
	 * power down as a result.
	 */
	return -1;
}

static int qcom_cpu_spc(void)
{
	int ret;
	struct spm_driver_data *drv = __this_cpu_read(cpu_spm_drv);

	spm_set_low_power_mode(drv, PM_SLEEP_MODE_SPC);
	ret = cpu_suspend(0, qcom_pm_collapse);
	/*
	 * ARM common code executes WFI without calling into our driver and
	 * if the SPM mode is not reset, then we may accidently power down the
	 * cpu when we intended only to gate the cpu clock.
	 * Ensure the state is set to standby before returning.
	 */
	spm_set_low_power_mode(drv, PM_SLEEP_MODE_STBY);

	return ret;
}

static int qcom_idle_enter(unsigned long index)
{
	return __this_cpu_read(qcom_idle_ops)[index]();
}

static const struct of_device_id qcom_idle_state_match[] __initconst = {
	{ .compatible = "qcom,idle-state-spc", .data = qcom_cpu_spc },
	{ },
};

static int __init qcom_cpuidle_init(struct device_node *cpu_node, int cpu)
{
	const struct of_device_id *match_id;
	struct device_node *state_node;
	int i;
	int state_count = 1;
	idle_fn idle_fns[CPUIDLE_STATE_MAX];
	idle_fn *fns;
	cpumask_t mask;
	bool use_scm_power_down = false;

	for (i = 0; ; i++) {
		state_node = of_parse_phandle(cpu_node, "cpu-idle-states", i);
		if (!state_node)
			break;

		if (!of_device_is_available(state_node))
			continue;

		if (i == CPUIDLE_STATE_MAX) {
			pr_warn("%s: cpuidle states reached max possible\n",
					__func__);
			break;
		}

		match_id = of_match_node(qcom_idle_state_match, state_node);
		if (!match_id)
			return -ENODEV;

		idle_fns[state_count] = match_id->data;

		/* Check if any of the states allow power down */
		if (match_id->data == qcom_cpu_spc)
			use_scm_power_down = true;

		state_count++;
	}

	if (state_count == 1)
		goto check_spm;

	fns = devm_kcalloc(get_cpu_device(cpu), state_count, sizeof(*fns),
			GFP_KERNEL);
	if (!fns)
		return -ENOMEM;

	for (i = 1; i < state_count; i++)
		fns[i] = idle_fns[i];

	if (use_scm_power_down) {
		/* We have atleast one power down mode */
		cpumask_clear(&mask);
		cpumask_set_cpu(cpu, &mask);
		qcom_scm_set_warm_boot_addr(cpu_resume_arm, &mask);
	}

	per_cpu(qcom_idle_ops, cpu) = fns;

	/*
	 * SPM probe for the cpu should have happened by now, if the
	 * SPM device does not exist, return -ENXIO to indicate that the
	 * cpu does not support idle states.
	 */
check_spm:
	return per_cpu(cpu_spm_drv, cpu) ? 0 : -ENXIO;
}

static struct cpuidle_ops qcom_cpuidle_ops __initdata = {
	.suspend = qcom_idle_enter,
	.init = qcom_cpuidle_init,
};

CPUIDLE_METHOD_OF_DECLARE(qcom_idle_v1, "qcom,kpss-acc-v1", &qcom_cpuidle_ops);
CPUIDLE_METHOD_OF_DECLARE(qcom_idle_v2, "qcom,kpss-acc-v2", &qcom_cpuidle_ops);

static struct spm_driver_data *spm_get_drv(struct platform_device *pdev,
		int *spm_cpu)
{
	struct spm_driver_data *drv = NULL;
	struct device_node *cpu_node, *saw_node;
	int cpu;
	bool found;

	for_each_possible_cpu(cpu) {
		cpu_node = of_cpu_device_node_get(cpu);
		if (!cpu_node)
			continue;
		saw_node = of_parse_phandle(cpu_node, "qcom,saw", 0);
		found = (saw_node == pdev->dev.of_node);
		of_node_put(saw_node);
		of_node_put(cpu_node);
		if (found)
			break;
	}

	if (found) {
		drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
		if (drv)
			*spm_cpu = cpu;
	}

	return drv;
}

static const struct of_device_id spm_match_table[] = {
	{ .compatible = "qcom,msm8974-saw2-v2.1-cpu",
	  .data = &spm_reg_8974_8084_cpu },
	{ .compatible = "qcom,apq8084-saw2-v2.1-cpu",
	  .data = &spm_reg_8974_8084_cpu },
	{ .compatible = "qcom,apq8064-saw2-v1.1-cpu",
	  .data = &spm_reg_8064_cpu },
	{ },
};

static int spm_dev_probe(struct platform_device *pdev)
{
	struct spm_driver_data *drv;
	struct resource *res;
	const struct of_device_id *match_id;
	void __iomem *addr;
	int cpu;

	drv = spm_get_drv(pdev, &cpu);
	if (!drv)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drv->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drv->reg_base))
		return PTR_ERR(drv->reg_base);

	match_id = of_match_node(spm_match_table, pdev->dev.of_node);
	if (!match_id)
		return -ENODEV;

	drv->reg_data = match_id->data;

	/* Write the SPM sequences first.. */
	addr = drv->reg_base + drv->reg_data->reg_offset[SPM_REG_SEQ_ENTRY];
	__iowrite32_copy(addr, drv->reg_data->seq,
			ARRAY_SIZE(drv->reg_data->seq) / 4);

	/*
	 * ..and then the control registers.
	 * On some SoC if the control registers are written first and if the
	 * CPU was held in reset, the reset signal could trigger the SPM state
	 * machine, before the sequences are completely written.
	 */
	spm_register_write(drv, SPM_REG_CFG, drv->reg_data->spm_cfg);
	spm_register_write(drv, SPM_REG_DLY, drv->reg_data->spm_dly);
	spm_register_write(drv, SPM_REG_PMIC_DLY, drv->reg_data->pmic_dly);
	spm_register_write(drv, SPM_REG_PMIC_DATA_0,
				drv->reg_data->pmic_data[0]);
	spm_register_write(drv, SPM_REG_PMIC_DATA_1,
				drv->reg_data->pmic_data[1]);

	/* Set up Standby as the default low power mode */
	spm_set_low_power_mode(drv, PM_SLEEP_MODE_STBY);

	per_cpu(cpu_spm_drv, cpu) = drv;

	return 0;
}

static struct platform_driver spm_driver = {
	.probe = spm_dev_probe,
	.driver = {
		.name = "saw",
		.of_match_table = spm_match_table,
	},
};
module_platform_driver(spm_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SAW power controller driver");
MODULE_ALIAS("platform:saw");
