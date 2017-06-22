/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>

#include "core.h"
#include "vdec.h"
#include "venc.h"
#include "firmware.h"

static void venus_event_notify(struct venus_core *core, u32 event)
{
	struct venus_inst *inst;

	switch (event) {
	case EVT_SYS_WATCHDOG_TIMEOUT:
	case EVT_SYS_ERROR:
		break;
	default:
		return;
	}

	mutex_lock(&core->lock);
	core->sys_error = true;
	list_for_each_entry(inst, &core->instances, list)
		inst->ops->event_notify(inst, EVT_SESSION_ERROR, NULL);
	mutex_unlock(&core->lock);

	disable_irq_nosync(core->irq);

	/*
	 * Delay recovery to ensure venus has completed any pending cache
	 * operations. Without this sleep, we see device reset when firmware is
	 * unloaded after a system error.
	 */
	schedule_delayed_work(&core->work, msecs_to_jiffies(100));
}

static const struct hfi_core_ops venus_core_ops = {
	.event_notify = venus_event_notify,
};

static void venus_sys_error_handler(struct work_struct *work)
{
	struct venus_core *core =
			container_of(work, struct venus_core, work.work);
	int ret = 0;

	dev_warn(core->dev, "system error has occurred, starting recovery!\n");

	pm_runtime_get_sync(core->dev);

	hfi_core_deinit(core, true);
	hfi_destroy(core);
	mutex_lock(&core->lock);
	venus_shutdown(&core->dev_fw);

	pm_runtime_put_sync(core->dev);

	ret |= hfi_create(core, &venus_core_ops);

	pm_runtime_get_sync(core->dev);

	ret |= venus_boot(core->dev, &core->dev_fw, core->res->fwname);

	ret |= hfi_core_resume(core, true);

	enable_irq(core->irq);

	mutex_unlock(&core->lock);

	ret |= hfi_core_init(core);

	pm_runtime_put_sync(core->dev);

	if (ret) {
		disable_irq_nosync(core->irq);
		dev_warn(core->dev, "recovery failed (%d)\n", ret);
		schedule_delayed_work(&core->work, msecs_to_jiffies(10));
		return;
	}

	mutex_lock(&core->lock);
	core->sys_error = false;
	mutex_unlock(&core->lock);
}

static int venus_clks_get(struct venus_core *core)
{
	const struct venus_resources *res = core->res;
	struct device *dev = core->dev;
	unsigned int i;

	for (i = 0; i < res->clks_num; i++) {
		core->clks[i] = devm_clk_get(dev, res->clks[i]);
		if (IS_ERR(core->clks[i]))
			return PTR_ERR(core->clks[i]);
	}

	return 0;
}

static int venus_clks_enable(struct venus_core *core)
{
	const struct venus_resources *res = core->res;
	unsigned int i;
	int ret;

	for (i = 0; i < res->clks_num; i++) {
		ret = clk_prepare_enable(core->clks[i]);
		if (ret)
			goto err;
	}

	return 0;
err:
	while (i--)
		clk_disable_unprepare(core->clks[i]);

	return ret;
}

static void venus_clks_disable(struct venus_core *core)
{
	const struct venus_resources *res = core->res;
	unsigned int i = res->clks_num;

	while (i--)
		clk_disable_unprepare(core->clks[i]);
}

static int venus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct venus_core *core;
	struct resource *r;
	int ret;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->dev = dev;
	platform_set_drvdata(pdev, core);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	core->base = devm_ioremap_resource(dev, r);
	if (IS_ERR(core->base))
		return PTR_ERR(core->base);

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0)
		return core->irq;

	core->res = of_device_get_match_data(dev);
	if (!core->res)
		return -ENODEV;

	ret = venus_clks_get(core);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(dev, core->res->dma_mask);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&core->instances);
	mutex_init(&core->lock);
	INIT_DELAYED_WORK(&core->work, venus_sys_error_handler);

	ret = devm_request_threaded_irq(dev, core->irq, hfi_isr, hfi_isr_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"venus", core);
	if (ret)
		return ret;

	ret = hfi_create(core, &venus_core_ops);
	if (ret)
		return ret;

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_runtime_disable;

	ret = venus_boot(dev, &core->dev_fw, core->res->fwname);
	if (ret)
		goto err_runtime_disable;

	ret = hfi_core_resume(core, true);
	if (ret)
		goto err_venus_shutdown;

	ret = hfi_core_init(core);
	if (ret)
		goto err_venus_shutdown;

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret)
		goto err_core_deinit;

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret)
		goto err_dev_unregister;

	ret = pm_runtime_put_sync(dev);
	if (ret)
		goto err_dev_unregister;

	return 0;

err_dev_unregister:
	v4l2_device_unregister(&core->v4l2_dev);
err_core_deinit:
	hfi_core_deinit(core, false);
err_venus_shutdown:
	venus_shutdown(&core->dev_fw);
err_runtime_disable:
	pm_runtime_set_suspended(dev);
	pm_runtime_disable(dev);
	hfi_destroy(core);
	return ret;
}

static int venus_remove(struct platform_device *pdev)
{
	struct venus_core *core = platform_get_drvdata(pdev);
	struct device *dev = core->dev;
	int ret;

	ret = pm_runtime_get_sync(dev);
	WARN_ON(ret < 0);

	ret = hfi_core_deinit(core, true);
	WARN_ON(ret);

	hfi_destroy(core);
	venus_shutdown(&core->dev_fw);
	of_platform_depopulate(dev);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	v4l2_device_unregister(&core->v4l2_dev);

	return ret;
}

#ifdef CONFIG_PM
static int venus_runtime_suspend(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret;

	ret = hfi_core_suspend(core);

	venus_clks_disable(core);

	return ret;
}

static int venus_runtime_resume(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret;

	ret = venus_clks_enable(core);
	if (ret)
		return ret;

	ret = hfi_core_resume(core, false);
	if (ret)
		goto err_clks_disable;

	return 0;

err_clks_disable:
	venus_clks_disable(core);
	return ret;
}
#endif

static const struct dev_pm_ops venus_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(venus_runtime_suspend, venus_runtime_resume, NULL)
};

static const struct freq_tbl msm8916_freq_table[] = {
	{ 352800, 228570000 },	/* 1920x1088 @ 30 + 1280x720 @ 30 */
	{ 244800, 160000000 },	/* 1920x1088 @ 30 */
	{ 108000, 100000000 },	/* 1280x720 @ 30 */
};

static const struct reg_val msm8916_reg_preset[] = {
	{ 0xe0020, 0x05555556 },
	{ 0xe0024, 0x05555556 },
	{ 0x80124, 0x00000003 },
};

static const struct venus_resources msm8916_res = {
	.freq_tbl = msm8916_freq_table,
	.freq_tbl_size = ARRAY_SIZE(msm8916_freq_table),
	.reg_tbl = msm8916_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(msm8916_reg_preset),
	.clks = { "core", "iface", "bus", },
	.clks_num = 3,
	.max_load = 352800, /* 720p@30 + 1080p@30 */
	.hfi_version = HFI_VERSION_1XX,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xddc00000 - 1,
	.fwname = "qcom/venus-1.8/venus.mdt",
};

static const struct freq_tbl msm8996_freq_table[] = {
	{ 1944000, 490000000 },	/* 4k UHD @ 60 */
	{  972000, 320000000 },	/* 4k UHD @ 30 */
	{  489600, 150000000 },	/* 1080p @ 60 */
	{  244800,  75000000 },	/* 1080p @ 30 */
};

static const struct reg_val msm8996_reg_preset[] = {
	{ 0x80010, 0xffffffff },
	{ 0x80018, 0x00001556 },
	{ 0x8001C, 0x00001556 },
};

static const struct venus_resources msm8996_res = {
	.freq_tbl = msm8996_freq_table,
	.freq_tbl_size = ARRAY_SIZE(msm8996_freq_table),
	.reg_tbl = msm8996_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(msm8996_reg_preset),
	.clks = {"core", "iface", "bus", "mbus" },
	.clks_num = 4,
	.max_load = 2563200,
	.hfi_version = HFI_VERSION_3XX,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xddc00000 - 1,
	.fwname = "qcom/venus-4.2/venus.mdt",
};

static const struct of_device_id venus_dt_match[] = {
	{ .compatible = "qcom,msm8916-venus", .data = &msm8916_res, },
	{ .compatible = "qcom,msm8996-venus", .data = &msm8996_res, },
	{ }
};
MODULE_DEVICE_TABLE(of, venus_dt_match);

static struct platform_driver qcom_venus_driver = {
	.probe = venus_probe,
	.remove = venus_remove,
	.driver = {
		.name = "qcom-venus",
		.of_match_table = venus_dt_match,
		.pm = &venus_pm_ops,
	},
};
module_platform_driver(qcom_venus_driver);

MODULE_ALIAS("platform:qcom-venus");
MODULE_DESCRIPTION("Qualcomm Venus video encoder and decoder driver");
MODULE_LICENSE("GPL v2");
