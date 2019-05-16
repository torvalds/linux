// SPDX-License-Identifier: GPL-2.0
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include "panfrost_device.h"
#include "panfrost_devfreq.h"
#include "panfrost_features.h"
#include "panfrost_gpu.h"
#include "panfrost_job.h"
#include "panfrost_mmu.h"

static int panfrost_reset_init(struct panfrost_device *pfdev)
{
	int err;

	pfdev->rstc = devm_reset_control_array_get(pfdev->dev, false, true);
	if (IS_ERR(pfdev->rstc)) {
		dev_err(pfdev->dev, "get reset failed %ld\n", PTR_ERR(pfdev->rstc));
		return PTR_ERR(pfdev->rstc);
	}

	err = reset_control_deassert(pfdev->rstc);
	if (err)
		return err;

	return 0;
}

static void panfrost_reset_fini(struct panfrost_device *pfdev)
{
	reset_control_assert(pfdev->rstc);
}

static int panfrost_clk_init(struct panfrost_device *pfdev)
{
	int err;
	unsigned long rate;

	pfdev->clock = devm_clk_get(pfdev->dev, NULL);
	if (IS_ERR(pfdev->clock)) {
		dev_err(pfdev->dev, "get clock failed %ld\n", PTR_ERR(pfdev->clock));
		return PTR_ERR(pfdev->clock);
	}

	rate = clk_get_rate(pfdev->clock);
	dev_info(pfdev->dev, "clock rate = %lu\n", rate);

	err = clk_prepare_enable(pfdev->clock);
	if (err)
		return err;

	return 0;
}

static void panfrost_clk_fini(struct panfrost_device *pfdev)
{
	clk_disable_unprepare(pfdev->clock);
}

static int panfrost_regulator_init(struct panfrost_device *pfdev)
{
	int ret;

	pfdev->regulator = devm_regulator_get_optional(pfdev->dev, "mali");
	if (IS_ERR(pfdev->regulator)) {
		ret = PTR_ERR(pfdev->regulator);
		pfdev->regulator = NULL;
		if (ret == -ENODEV)
			return 0;
		dev_err(pfdev->dev, "failed to get regulator: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(pfdev->regulator);
	if (ret < 0) {
		dev_err(pfdev->dev, "failed to enable regulator: %d\n", ret);
		return ret;
	}

	return 0;
}

static void panfrost_regulator_fini(struct panfrost_device *pfdev)
{
	if (pfdev->regulator)
		regulator_disable(pfdev->regulator);
}

int panfrost_device_init(struct panfrost_device *pfdev)
{
	int err;
	struct resource *res;

	mutex_init(&pfdev->sched_lock);
	mutex_init(&pfdev->reset_lock);
	INIT_LIST_HEAD(&pfdev->scheduled_jobs);

	spin_lock_init(&pfdev->hwaccess_lock);

	err = panfrost_clk_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "clk init failed %d\n", err);
		return err;
	}

	err = panfrost_regulator_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "regulator init failed %d\n", err);
		goto err_out0;
	}

	err = panfrost_reset_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "reset init failed %d\n", err);
		goto err_out1;
	}

	res = platform_get_resource(pfdev->pdev, IORESOURCE_MEM, 0);
	pfdev->iomem = devm_ioremap_resource(pfdev->dev, res);
	if (IS_ERR(pfdev->iomem)) {
		dev_err(pfdev->dev, "failed to ioremap iomem\n");
		err = PTR_ERR(pfdev->iomem);
		goto err_out2;
	}

	err = panfrost_gpu_init(pfdev);
	if (err)
		goto err_out2;

	err = panfrost_mmu_init(pfdev);
	if (err)
		goto err_out3;

	err = panfrost_job_init(pfdev);
	if (err)
		goto err_out4;

	/* runtime PM will wake us up later */
	panfrost_gpu_power_off(pfdev);

	pm_runtime_set_active(pfdev->dev);
	pm_runtime_get_sync(pfdev->dev);
	pm_runtime_mark_last_busy(pfdev->dev);
	pm_runtime_put_autosuspend(pfdev->dev);

	return 0;
err_out4:
	panfrost_mmu_fini(pfdev);
err_out3:
	panfrost_gpu_fini(pfdev);
err_out2:
	panfrost_reset_fini(pfdev);
err_out1:
	panfrost_regulator_fini(pfdev);
err_out0:
	panfrost_clk_fini(pfdev);
	return err;
}

void panfrost_device_fini(struct panfrost_device *pfdev)
{
	panfrost_job_fini(pfdev);
	panfrost_mmu_fini(pfdev);
	panfrost_gpu_fini(pfdev);
	panfrost_reset_fini(pfdev);
	panfrost_regulator_fini(pfdev);
	panfrost_clk_fini(pfdev);
}

const char *panfrost_exception_name(struct panfrost_device *pfdev, u32 exception_code)
{
	switch (exception_code) {
		/* Non-Fault Status code */
	case 0x00: return "NOT_STARTED/IDLE/OK";
	case 0x01: return "DONE";
	case 0x02: return "INTERRUPTED";
	case 0x03: return "STOPPED";
	case 0x04: return "TERMINATED";
	case 0x08: return "ACTIVE";
		/* Job exceptions */
	case 0x40: return "JOB_CONFIG_FAULT";
	case 0x41: return "JOB_POWER_FAULT";
	case 0x42: return "JOB_READ_FAULT";
	case 0x43: return "JOB_WRITE_FAULT";
	case 0x44: return "JOB_AFFINITY_FAULT";
	case 0x48: return "JOB_BUS_FAULT";
	case 0x50: return "INSTR_INVALID_PC";
	case 0x51: return "INSTR_INVALID_ENC";
	case 0x52: return "INSTR_TYPE_MISMATCH";
	case 0x53: return "INSTR_OPERAND_FAULT";
	case 0x54: return "INSTR_TLS_FAULT";
	case 0x55: return "INSTR_BARRIER_FAULT";
	case 0x56: return "INSTR_ALIGN_FAULT";
	case 0x58: return "DATA_INVALID_FAULT";
	case 0x59: return "TILE_RANGE_FAULT";
	case 0x5A: return "ADDR_RANGE_FAULT";
	case 0x60: return "OUT_OF_MEMORY";
		/* GPU exceptions */
	case 0x80: return "DELAYED_BUS_FAULT";
	case 0x88: return "SHAREABILITY_FAULT";
		/* MMU exceptions */
	case 0xC1: return "TRANSLATION_FAULT_LEVEL1";
	case 0xC2: return "TRANSLATION_FAULT_LEVEL2";
	case 0xC3: return "TRANSLATION_FAULT_LEVEL3";
	case 0xC4: return "TRANSLATION_FAULT_LEVEL4";
	case 0xC8: return "PERMISSION_FAULT";
	case 0xC9 ... 0xCF: return "PERMISSION_FAULT";
	case 0xD1: return "TRANSTAB_BUS_FAULT_LEVEL1";
	case 0xD2: return "TRANSTAB_BUS_FAULT_LEVEL2";
	case 0xD3: return "TRANSTAB_BUS_FAULT_LEVEL3";
	case 0xD4: return "TRANSTAB_BUS_FAULT_LEVEL4";
	case 0xD8: return "ACCESS_FLAG";
	case 0xD9 ... 0xDF: return "ACCESS_FLAG";
	case 0xE0 ... 0xE7: return "ADDRESS_SIZE_FAULT";
	case 0xE8 ... 0xEF: return "MEMORY_ATTRIBUTES_FAULT";
	}

	return "UNKNOWN";
}

#ifdef CONFIG_PM
int panfrost_device_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct panfrost_device *pfdev = platform_get_drvdata(pdev);

	panfrost_gpu_soft_reset(pfdev);

	/* TODO: Re-enable all other address spaces */
	panfrost_gpu_power_on(pfdev);
	panfrost_mmu_enable(pfdev, 0);
	panfrost_job_enable_interrupts(pfdev);
	panfrost_devfreq_resume(pfdev);

	return 0;
}

int panfrost_device_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct panfrost_device *pfdev = platform_get_drvdata(pdev);

	if (!panfrost_job_is_idle(pfdev))
		return -EBUSY;

	panfrost_devfreq_suspend(pfdev);
	panfrost_gpu_power_off(pfdev);

	return 0;
}
#endif
