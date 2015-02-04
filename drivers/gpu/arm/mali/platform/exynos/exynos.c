/*
 * Mali400 platform glue for Samsung Exynos SoCs
 *
 * Copyright 2013 by Samsung Electronics Co., Ltd.
 * Author: Tomasz Figa <t.figa@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/mali/mali_utgard.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"

#ifdef CONFIG_MALI400_PROFILING
#include "mali_osk_profiling.h"
#endif

#include "exynos.h"

struct mali_exynos_variant {
	const struct mali_exynos_dvfs_step *steps;
	unsigned int nr_steps;
	unsigned int has_smmuclk;
};

struct mali_exynos_dvfs_step {
	unsigned int rate;
	unsigned int voltage;
	unsigned int downthreshold;
	unsigned int upthreshold;
};

struct mali_exynos_drvdata {
	struct device *dev;

	const struct mali_exynos_dvfs_step *steps;
	unsigned int nr_steps;
	unsigned int has_smmuclk;

	struct clk *pll;
	struct clk *mux1;
	struct clk *mux2;
	struct clk *sclk;
	struct clk *smmu;
	struct clk *g3d;

	struct regulator *vdd_g3d;

	mali_power_mode power_mode;
	unsigned int dvfs_step;
	unsigned int load;

	struct workqueue_struct *dvfs_workqueue;
	struct work_struct dvfs_work;
};

extern struct platform_device *mali_platform_device;

static struct mali_exynos_drvdata *mali;

/*
 * DVFS tables
 */

#define MALI_DVFS_STEP(freq, voltage, down, up) \
	{freq, voltage, (256 * down) / 100, (256 * up) / 100}

static const struct mali_exynos_dvfs_step mali_exynos_dvfs_step_3250[] = {
	MALI_DVFS_STEP(134,       0,  0, 100)
};

static const struct mali_exynos_dvfs_step mali_exynos_dvfs_step_4210[] = {
	MALI_DVFS_STEP(160,  950000,  0,  90),
	MALI_DVFS_STEP(266, 1050000, 85, 100)
};

static const struct mali_exynos_dvfs_step mali_exynos_dvfs_step_4x12[] = {
	MALI_DVFS_STEP(160,  875000,  0,  70),
	MALI_DVFS_STEP(266,  900000, 62,  90),
	MALI_DVFS_STEP(350,  950000, 85,  90),
	MALI_DVFS_STEP(440, 1025000, 85, 100)
};

static const struct mali_exynos_dvfs_step mali_exynos_dvfs_step_4x12_prime[] = {
	MALI_DVFS_STEP(160,  875000,  0,  70),
	MALI_DVFS_STEP(266,  900000, 62,  90),
	MALI_DVFS_STEP(350,  950000, 85,  90),
	MALI_DVFS_STEP(440, 1025000, 85,  90),
	MALI_DVFS_STEP(533, 1075000, 95, 100)
};

/*
 * Variants
 */

static const struct mali_exynos_variant mali_variant_3250 = {
	.steps = mali_exynos_dvfs_step_3250,
	.nr_steps = ARRAY_SIZE(mali_exynos_dvfs_step_3250),
	.has_smmuclk = true,
};

static const struct mali_exynos_variant mali_variant_4210 = {
	.steps = mali_exynos_dvfs_step_4210,
	.nr_steps = ARRAY_SIZE(mali_exynos_dvfs_step_4210),
};

static const struct mali_exynos_variant mali_variant_4x12 = {
	.steps = mali_exynos_dvfs_step_4x12,
	.nr_steps = ARRAY_SIZE(mali_exynos_dvfs_step_4x12),
};

static const struct mali_exynos_variant mali_variant_4x12_prime = {
	.steps = mali_exynos_dvfs_step_4x12_prime,
	.nr_steps = ARRAY_SIZE(mali_exynos_dvfs_step_4x12_prime),
};

const struct of_device_id mali_of_matches[] = {
	{ .compatible = "samsung,exynos3250-g3d",
					.data = &mali_variant_3250, },
	{ .compatible = "samsung,exynos4210-g3d",
					.data = &mali_variant_4210, },
	{ .compatible = "samsung,exynos4x12-g3d",
					.data = &mali_variant_4x12, },
	{ .compatible = "samsung,exynos4x12-prime-g3d",
					.data = &mali_variant_4x12_prime, },
	{ /* Sentinel */ }
};

#ifdef CONFIG_MALI400_PROFILING
static inline void _mali_osk_profiling_add_gpufreq_event(int rate, int vol)
{
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
		 MALI_PROFILING_EVENT_CHANNEL_GPU |
		 MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
		 rate, vol, 0, 0, 0);
}
#else
static inline void _mali_osk_profiling_add_gpufreq_event(int rate, int vol)
{
}
#endif

/*
 * DVFS control
 */

static void mali_exynos_set_dvfs_step(struct mali_exynos_drvdata *mali,
							unsigned int step)
{
	const struct mali_exynos_dvfs_step *next = &mali->steps[step];

	if (step <= mali->dvfs_step)
		clk_set_rate(mali->sclk, next->rate * 1000000);

	regulator_set_voltage(mali->vdd_g3d,
					next->voltage, next->voltage);

	if (step > mali->dvfs_step)
		clk_set_rate(mali->sclk, next->rate * 1000000);

	_mali_osk_profiling_add_gpufreq_event(next->rate * 1000000,
		 regulator_get_voltage(mali->vdd_g3d) / 1000);
	mali->dvfs_step = step;
}

static void exynos_dvfs_work(struct work_struct *work)
{
	struct mali_exynos_drvdata *mali = container_of(work,
					struct mali_exynos_drvdata, dvfs_work);
	unsigned int step = mali->dvfs_step;
	const struct mali_exynos_dvfs_step *cur = &mali->steps[step];

	if (mali->load > cur->upthreshold)
		++step;
	else if (mali->load < cur->downthreshold)
		--step;

	BUG_ON(step >= mali->nr_steps);

	if (step != mali->dvfs_step)
		mali_exynos_set_dvfs_step(mali, step);
}

static void exynos_update_dvfs(struct mali_gpu_utilization_data *data)
{
	if (data->utilization_gpu > 255)
		data->utilization_gpu = 255;

	mali->load = data->utilization_gpu;

	queue_work(mali->dvfs_workqueue, &mali->dvfs_work);
}

/*
 * Power management
 */

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	if (WARN_ON(mali->power_mode == power_mode))
		MALI_SUCCESS;

	switch (power_mode) {
	case MALI_POWER_MODE_ON:
		mali_exynos_set_dvfs_step(mali, 0);
		clk_prepare_enable(mali->g3d);
		clk_prepare_enable(mali->sclk);
		if (mali->has_smmuclk)
			clk_prepare_enable(mali->smmu);
		break;

	case MALI_POWER_MODE_LIGHT_SLEEP:
	case MALI_POWER_MODE_DEEP_SLEEP:
		if (mali->has_smmuclk)
			clk_disable_unprepare(mali->smmu);
		clk_disable_unprepare(mali->sclk);
		clk_disable_unprepare(mali->g3d);
		_mali_osk_profiling_add_gpufreq_event(0, 0);
		break;
	}

	mali->power_mode = power_mode;

	MALI_SUCCESS;
}

/*
 * Platform-specific initialization/cleanup
 */

static struct mali_gpu_device_data mali_exynos_gpu_data = {
	.shared_mem_size = SZ_256M,
	.fb_start = 0x40000000,
	.fb_size = 0xb1000000,
	.utilization_interval = 100, /* 100ms in Tizen */
	.utilization_callback = exynos_update_dvfs,
};

_mali_osk_errcode_t mali_platform_init(void)
{
	struct platform_device *pdev = mali_platform_device;
	const struct mali_exynos_variant *variant;
	const struct of_device_id *match;
	struct resource *old_res, *new_res;
	unsigned int i, irq_res, mem_res;
	struct device_node *np;
	int ret;

	if (WARN_ON(!pdev))
		return -ENODEV;

	MALI_DEBUG_PRINT(4, ("mali_platform_device_register() called\n"));

	pdev->dev.platform_data = &mali_exynos_gpu_data;

	np = pdev->dev.of_node;
	if (WARN_ON(!np))
		return -ENODEV;

	match = of_match_node(mali_of_matches, np);
	if (WARN_ON(!match))
		return -ENODEV;

	variant = match->data;

	old_res = pdev->resource;
	new_res = kzalloc(sizeof(*new_res) * pdev->num_resources, GFP_KERNEL);
	if (WARN_ON(!new_res))
		return -ENOMEM;

	/* Copy first resource */
	memcpy(new_res, old_res++, sizeof(*new_res));

	/* Rearrange next resources */
	irq_res = 0;
	mem_res = 0;
	for (i = 1; i < pdev->num_resources; ++i, ++old_res) {
		if (resource_type(old_res) == IORESOURCE_MEM)
			memcpy(&new_res[1 + 2 * mem_res++],
						old_res, sizeof(*old_res));
		else if (resource_type(old_res) == IORESOURCE_IRQ)
			memcpy(&new_res[2 + 2 * irq_res++],
						old_res, sizeof(*old_res));
	}

	kfree(pdev->resource);
	pdev->resource = new_res;

	mali = devm_kzalloc(&pdev->dev, sizeof(*mali), GFP_KERNEL);
	if (WARN_ON(!mali))
		return -ENOMEM;

	mali->dev = &pdev->dev;
	mali->steps = variant->steps;
	mali->nr_steps = variant->nr_steps;
	mali->has_smmuclk = variant->has_smmuclk;

	mali->pll = devm_clk_get(mali->dev, "pll");
	if (WARN_ON(IS_ERR(mali->pll)))
		return PTR_ERR(mali->pll);

	mali->mux1 = devm_clk_get(mali->dev, "mux1");
	if (WARN_ON(IS_ERR(mali->mux1)))
		return PTR_ERR(mali->mux1);

	mali->mux2 = devm_clk_get(mali->dev, "mux2");
	if (WARN_ON(IS_ERR(mali->mux2)))
		return PTR_ERR(mali->mux2);

	mali->sclk = devm_clk_get(mali->dev, "sclk");
	if (WARN_ON(IS_ERR(mali->sclk)))
		return PTR_ERR(mali->sclk);

	if (mali->has_smmuclk) {
		mali->smmu = devm_clk_get(mali->dev, "smmu");
		if (WARN_ON(IS_ERR(mali->smmu)))
			return PTR_ERR(mali->smmu);
	}

	mali->g3d = devm_clk_get(mali->dev, "g3d");
	if (WARN_ON(IS_ERR(mali->g3d)))
		return PTR_ERR(mali->g3d);

	mali->vdd_g3d = devm_regulator_get(mali->dev, "vdd_g3d");
	if (WARN_ON(IS_ERR(mali->vdd_g3d)))
		return PTR_ERR(mali->vdd_g3d);

	mali->dvfs_workqueue = create_singlethread_workqueue("mali_dvfs");
	if (WARN_ON(!mali->dvfs_workqueue))
		return -EFAULT;

	mali->power_mode = MALI_POWER_MODE_LIGHT_SLEEP;

	INIT_WORK(&mali->dvfs_work, exynos_dvfs_work);

	ret = regulator_enable(mali->vdd_g3d);
	if (WARN_ON(ret)) {
		destroy_workqueue(mali->dvfs_workqueue);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	clk_set_parent(mali->mux1, mali->pll);
	clk_set_parent(mali->mux2, mali->mux1);
	mali_exynos_set_dvfs_step(mali, 0);

	pm_runtime_set_autosuspend_delay(&pdev->dev, 300);
	pm_runtime_use_autosuspend(&pdev->dev);

	pm_runtime_enable(&pdev->dev);

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(void)
{
	struct platform_device *pdev = mali_platform_device;

	pm_runtime_disable(&pdev->dev);

	regulator_disable(mali->vdd_g3d);

	_mali_osk_profiling_add_gpufreq_event(0, 0);

	MALI_SUCCESS;
}
