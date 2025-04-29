// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_vidc.h"

static int iris_init_icc(struct iris_core *core)
{
	const struct icc_info *icc_tbl;
	u32 i = 0;

	icc_tbl = core->iris_platform_data->icc_tbl;

	core->icc_count = core->iris_platform_data->icc_tbl_size;
	core->icc_tbl = devm_kzalloc(core->dev,
				     sizeof(struct icc_bulk_data) * core->icc_count,
				     GFP_KERNEL);
	if (!core->icc_tbl)
		return -ENOMEM;

	for (i = 0; i < core->icc_count; i++) {
		core->icc_tbl[i].name = icc_tbl[i].name;
		core->icc_tbl[i].avg_bw = icc_tbl[i].bw_min_kbps;
		core->icc_tbl[i].peak_bw = 0;
	}

	return devm_of_icc_bulk_get(core->dev, core->icc_count, core->icc_tbl);
}

static int iris_init_power_domains(struct iris_core *core)
{
	const struct platform_clk_data *clk_tbl;
	u32 clk_cnt, i;
	int ret;

	struct dev_pm_domain_attach_data iris_pd_data = {
		.pd_names = core->iris_platform_data->pmdomain_tbl,
		.num_pd_names = core->iris_platform_data->pmdomain_tbl_size,
		.pd_flags = PD_FLAG_NO_DEV_LINK,
	};

	struct dev_pm_domain_attach_data iris_opp_pd_data = {
		.pd_names = core->iris_platform_data->opp_pd_tbl,
		.num_pd_names = core->iris_platform_data->opp_pd_tbl_size,
		.pd_flags = PD_FLAG_DEV_LINK_ON,
	};

	ret = devm_pm_domain_attach_list(core->dev, &iris_pd_data, &core->pmdomain_tbl);
	if (ret < 0)
		return ret;

	ret =  devm_pm_domain_attach_list(core->dev, &iris_opp_pd_data, &core->opp_pmdomain_tbl);
	if (ret < 0)
		return ret;

	clk_tbl = core->iris_platform_data->clk_tbl;
	clk_cnt = core->iris_platform_data->clk_tbl_size;

	for (i = 0; i < clk_cnt; i++) {
		if (clk_tbl[i].clk_type == IRIS_HW_CLK) {
			ret = devm_pm_opp_set_clkname(core->dev, clk_tbl[i].clk_name);
			if (ret)
				return ret;
		}
	}

	return devm_pm_opp_of_add_table(core->dev);
}

static int iris_init_clocks(struct iris_core *core)
{
	int ret;

	ret = devm_clk_bulk_get_all(core->dev, &core->clock_tbl);
	if (ret < 0)
		return ret;

	core->clk_count = ret;

	return 0;
}

static int iris_init_resets(struct iris_core *core)
{
	const char * const *rst_tbl;
	u32 rst_tbl_size;
	u32 i = 0;

	rst_tbl = core->iris_platform_data->clk_rst_tbl;
	rst_tbl_size = core->iris_platform_data->clk_rst_tbl_size;

	core->resets = devm_kzalloc(core->dev,
				    sizeof(*core->resets) * rst_tbl_size,
				    GFP_KERNEL);
	if (!core->resets)
		return -ENOMEM;

	for (i = 0; i < rst_tbl_size; i++)
		core->resets[i].id = rst_tbl[i];

	return devm_reset_control_bulk_get_exclusive(core->dev, rst_tbl_size, core->resets);
}

static int iris_init_resources(struct iris_core *core)
{
	int ret;

	ret = iris_init_icc(core);
	if (ret)
		return ret;

	ret = iris_init_power_domains(core);
	if (ret)
		return ret;

	ret = iris_init_clocks(core);
	if (ret)
		return ret;

	return iris_init_resets(core);
}

static int iris_register_video_device(struct iris_core *core)
{
	struct video_device *vdev;
	int ret;

	vdev = video_device_alloc();
	if (!vdev)
		return -ENOMEM;

	strscpy(vdev->name, "qcom-iris-decoder", sizeof(vdev->name));
	vdev->release = video_device_release;
	vdev->fops = core->iris_v4l2_file_ops;
	vdev->ioctl_ops = core->iris_v4l2_ioctl_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_vdev_release;

	core->vdev_dec = vdev;
	video_set_drvdata(vdev, core);

	return 0;

err_vdev_release:
	video_device_release(vdev);

	return ret;
}

static void iris_remove(struct platform_device *pdev)
{
	struct iris_core *core;

	core = platform_get_drvdata(pdev);
	if (!core)
		return;

	iris_core_deinit(core);

	video_unregister_device(core->vdev_dec);

	v4l2_device_unregister(&core->v4l2_dev);

	mutex_destroy(&core->lock);
}

static void iris_sys_error_handler(struct work_struct *work)
{
	struct iris_core *core =
			container_of(work, struct iris_core, sys_error_handler.work);

	iris_core_deinit(core);
	iris_core_init(core);
}

static int iris_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iris_core *core;
	u64 dma_mask;
	int ret;

	core = devm_kzalloc(&pdev->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;
	core->dev = dev;

	core->state = IRIS_CORE_DEINIT;
	mutex_init(&core->lock);
	init_completion(&core->core_init_done);

	core->response_packet = devm_kzalloc(core->dev, IFACEQ_CORE_PKT_SIZE, GFP_KERNEL);
	if (!core->response_packet)
		return -ENOMEM;

	INIT_LIST_HEAD(&core->instances);
	INIT_DELAYED_WORK(&core->sys_error_handler, iris_sys_error_handler);

	core->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->reg_base))
		return PTR_ERR(core->reg_base);

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0)
		return core->irq;

	core->iris_platform_data = of_device_get_match_data(core->dev);

	ret = devm_request_threaded_irq(core->dev, core->irq, iris_hfi_isr,
					iris_hfi_isr_handler, IRQF_TRIGGER_HIGH, "iris", core);
	if (ret)
		return ret;

	disable_irq_nosync(core->irq);

	iris_init_ops(core);
	core->iris_platform_data->init_hfi_command_ops(core);
	core->iris_platform_data->init_hfi_response_ops(core);

	ret = iris_init_resources(core);
	if (ret)
		return ret;

	iris_session_init_caps(core);

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret)
		return ret;

	ret = iris_register_video_device(core);
	if (ret)
		goto err_v4l2_unreg;

	platform_set_drvdata(pdev, core);

	dma_mask = core->iris_platform_data->dma_mask;

	ret = dma_set_mask_and_coherent(dev, dma_mask);
	if (ret)
		goto err_vdev_unreg;

	dma_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));
	dma_set_seg_boundary(&pdev->dev, DMA_BIT_MASK(32));

	pm_runtime_set_autosuspend_delay(core->dev, AUTOSUSPEND_DELAY_VALUE);
	pm_runtime_use_autosuspend(core->dev);
	ret = devm_pm_runtime_enable(core->dev);
	if (ret)
		goto err_vdev_unreg;

	return 0;

err_vdev_unreg:
	video_unregister_device(core->vdev_dec);
err_v4l2_unreg:
	v4l2_device_unregister(&core->v4l2_dev);

	return ret;
}

static int __maybe_unused iris_pm_suspend(struct device *dev)
{
	struct iris_core *core;
	int ret = 0;

	core = dev_get_drvdata(dev);

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT)
		goto exit;

	ret = iris_hfi_pm_suspend(core);

exit:
	mutex_unlock(&core->lock);

	return ret;
}

static int __maybe_unused iris_pm_resume(struct device *dev)
{
	struct iris_core *core;
	int ret = 0;

	core = dev_get_drvdata(dev);

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT)
		goto exit;

	ret = iris_hfi_pm_resume(core);
	pm_runtime_mark_last_busy(core->dev);

exit:
	mutex_unlock(&core->lock);

	return ret;
}

static const struct dev_pm_ops iris_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(iris_pm_suspend, iris_pm_resume, NULL)
};

static const struct of_device_id iris_dt_match[] = {
	{
		.compatible = "qcom,sm8550-iris",
		.data = &sm8550_data,
	},
#if (!IS_ENABLED(CONFIG_VIDEO_QCOM_VENUS))
		{
			.compatible = "qcom,sm8250-venus",
			.data = &sm8250_data,
		},
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, iris_dt_match);

static struct platform_driver qcom_iris_driver = {
	.probe = iris_probe,
	.remove = iris_remove,
	.driver = {
		.name = "qcom-iris",
		.of_match_table = iris_dt_match,
		.pm = &iris_pm_ops,
	},
};

module_platform_driver(qcom_iris_driver);
MODULE_DESCRIPTION("Qualcomm iris video driver");
MODULE_LICENSE("GPL");
