// SPDX-License-Identifier: GPL-2.0
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>

#include "panfrost_device.h"
#include "panfrost_devfreq.h"
#include "panfrost_features.h"
#include "panfrost_gpu.h"
#include "panfrost_job.h"
#include "panfrost_mmu.h"
#include "panfrost_perfcnt.h"

static int panfrost_reset_init(struct panfrost_device *pfdev)
{
	pfdev->rstc = devm_reset_control_array_get_optional_exclusive(pfdev->dev);
	if (IS_ERR(pfdev->rstc)) {
		dev_err(pfdev->dev, "get reset failed %ld\n", PTR_ERR(pfdev->rstc));
		return PTR_ERR(pfdev->rstc);
	}

	return reset_control_deassert(pfdev->rstc);
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

	pfdev->bus_clock = devm_clk_get_optional(pfdev->dev, "bus");
	if (IS_ERR(pfdev->bus_clock)) {
		dev_err(pfdev->dev, "get bus_clock failed %ld\n",
			PTR_ERR(pfdev->bus_clock));
		err = PTR_ERR(pfdev->bus_clock);
		goto disable_clock;
	}

	if (pfdev->bus_clock) {
		rate = clk_get_rate(pfdev->bus_clock);
		dev_info(pfdev->dev, "bus_clock rate = %lu\n", rate);

		err = clk_prepare_enable(pfdev->bus_clock);
		if (err)
			goto disable_clock;
	}

	return 0;

disable_clock:
	clk_disable_unprepare(pfdev->clock);

	return err;
}

static void panfrost_clk_fini(struct panfrost_device *pfdev)
{
	clk_disable_unprepare(pfdev->bus_clock);
	clk_disable_unprepare(pfdev->clock);
}

static int panfrost_regulator_init(struct panfrost_device *pfdev)
{
	int ret, i;

	pfdev->regulators = devm_kcalloc(pfdev->dev, pfdev->comp->num_supplies,
					 sizeof(*pfdev->regulators),
					 GFP_KERNEL);
	if (!pfdev->regulators)
		return -ENOMEM;

	for (i = 0; i < pfdev->comp->num_supplies; i++)
		pfdev->regulators[i].supply = pfdev->comp->supply_names[i];

	ret = devm_regulator_bulk_get(pfdev->dev,
				      pfdev->comp->num_supplies,
				      pfdev->regulators);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(pfdev->dev, "failed to get regulators: %d\n",
				ret);
		return ret;
	}

	ret = regulator_bulk_enable(pfdev->comp->num_supplies,
				    pfdev->regulators);
	if (ret < 0) {
		dev_err(pfdev->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	return 0;
}

static void panfrost_regulator_fini(struct panfrost_device *pfdev)
{
	if (!pfdev->regulators)
		return;

	regulator_bulk_disable(pfdev->comp->num_supplies, pfdev->regulators);
}

static void panfrost_pm_domain_fini(struct panfrost_device *pfdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pfdev->pm_domain_devs); i++) {
		if (!pfdev->pm_domain_devs[i])
			break;

		if (pfdev->pm_domain_links[i])
			device_link_del(pfdev->pm_domain_links[i]);

		dev_pm_domain_detach(pfdev->pm_domain_devs[i], true);
	}
}

static int panfrost_pm_domain_init(struct panfrost_device *pfdev)
{
	int err;
	int i, num_domains;

	num_domains = of_count_phandle_with_args(pfdev->dev->of_node,
						 "power-domains",
						 "#power-domain-cells");

	/*
	 * Single domain is handled by the core, and, if only a single power
	 * the power domain is requested, the property is optional.
	 */
	if (num_domains < 2 && pfdev->comp->num_pm_domains < 2)
		return 0;

	if (num_domains != pfdev->comp->num_pm_domains) {
		dev_err(pfdev->dev,
			"Incorrect number of power domains: %d provided, %d needed\n",
			num_domains, pfdev->comp->num_pm_domains);
		return -EINVAL;
	}

	if (WARN(num_domains > ARRAY_SIZE(pfdev->pm_domain_devs),
			"Too many supplies in compatible structure.\n"))
		return -EINVAL;

	for (i = 0; i < num_domains; i++) {
		pfdev->pm_domain_devs[i] =
			dev_pm_domain_attach_by_name(pfdev->dev,
					pfdev->comp->pm_domain_names[i]);
		if (IS_ERR_OR_NULL(pfdev->pm_domain_devs[i])) {
			err = PTR_ERR(pfdev->pm_domain_devs[i]) ? : -ENODATA;
			pfdev->pm_domain_devs[i] = NULL;
			dev_err(pfdev->dev,
				"failed to get pm-domain %s(%d): %d\n",
				pfdev->comp->pm_domain_names[i], i, err);
			goto err;
		}

		pfdev->pm_domain_links[i] = device_link_add(pfdev->dev,
				pfdev->pm_domain_devs[i], DL_FLAG_PM_RUNTIME |
				DL_FLAG_STATELESS | DL_FLAG_RPM_ACTIVE);
		if (!pfdev->pm_domain_links[i]) {
			dev_err(pfdev->pm_domain_devs[i],
				"adding device link failed!\n");
			err = -ENODEV;
			goto err;
		}
	}

	return 0;

err:
	panfrost_pm_domain_fini(pfdev);
	return err;
}

int panfrost_device_init(struct panfrost_device *pfdev)
{
	int err;
	struct resource *res;

	mutex_init(&pfdev->sched_lock);
	INIT_LIST_HEAD(&pfdev->scheduled_jobs);
	INIT_LIST_HEAD(&pfdev->as_lru_list);

	spin_lock_init(&pfdev->as_lock);

	err = panfrost_clk_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "clk init failed %d\n", err);
		return err;
	}

	err = panfrost_devfreq_init(pfdev);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(pfdev->dev, "devfreq init failed %d\n", err);
		goto out_clk;
	}

	/* OPP will handle regulators */
	if (!pfdev->pfdevfreq.opp_of_table_added) {
		err = panfrost_regulator_init(pfdev);
		if (err)
			goto out_devfreq;
	}

	err = panfrost_reset_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "reset init failed %d\n", err);
		goto out_regulator;
	}

	err = panfrost_pm_domain_init(pfdev);
	if (err)
		goto out_reset;

	res = platform_get_resource(pfdev->pdev, IORESOURCE_MEM, 0);
	pfdev->iomem = devm_ioremap_resource(pfdev->dev, res);
	if (IS_ERR(pfdev->iomem)) {
		err = PTR_ERR(pfdev->iomem);
		goto out_pm_domain;
	}

	err = panfrost_gpu_init(pfdev);
	if (err)
		goto out_pm_domain;

	err = panfrost_mmu_init(pfdev);
	if (err)
		goto out_gpu;

	err = panfrost_job_init(pfdev);
	if (err)
		goto out_mmu;

	err = panfrost_perfcnt_init(pfdev);
	if (err)
		goto out_job;

	return 0;
out_job:
	panfrost_job_fini(pfdev);
out_mmu:
	panfrost_mmu_fini(pfdev);
out_gpu:
	panfrost_gpu_fini(pfdev);
out_pm_domain:
	panfrost_pm_domain_fini(pfdev);
out_reset:
	panfrost_reset_fini(pfdev);
out_regulator:
	panfrost_regulator_fini(pfdev);
out_devfreq:
	panfrost_devfreq_fini(pfdev);
out_clk:
	panfrost_clk_fini(pfdev);
	return err;
}

void panfrost_device_fini(struct panfrost_device *pfdev)
{
	panfrost_perfcnt_fini(pfdev);
	panfrost_job_fini(pfdev);
	panfrost_mmu_fini(pfdev);
	panfrost_gpu_fini(pfdev);
	panfrost_pm_domain_fini(pfdev);
	panfrost_reset_fini(pfdev);
	panfrost_devfreq_fini(pfdev);
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

void panfrost_device_reset(struct panfrost_device *pfdev)
{
	panfrost_gpu_soft_reset(pfdev);

	panfrost_gpu_power_on(pfdev);
	panfrost_mmu_reset(pfdev);
	panfrost_job_enable_interrupts(pfdev);
}

#ifdef CONFIG_PM
int panfrost_device_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct panfrost_device *pfdev = platform_get_drvdata(pdev);

	panfrost_device_reset(pfdev);
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
