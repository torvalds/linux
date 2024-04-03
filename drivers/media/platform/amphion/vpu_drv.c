// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-map-ops.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <linux/of_reserved_mem.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <linux/debugfs.h>
#include "vpu.h"
#include "vpu_imx8q.h"

bool debug;
module_param(debug, bool, 0644);

void vpu_writel(struct vpu_dev *vpu, u32 reg, u32 val)
{
	writel(val, vpu->base + reg);
}

u32 vpu_readl(struct vpu_dev *vpu, u32 reg)
{
	return readl(vpu->base + reg);
}

static void vpu_dev_get(struct vpu_dev *vpu)
{
	if (atomic_inc_return(&vpu->ref_vpu) == 1 && vpu->res->setup)
		vpu->res->setup(vpu);
}

static void vpu_dev_put(struct vpu_dev *vpu)
{
	atomic_dec(&vpu->ref_vpu);
}

static void vpu_enc_get(struct vpu_dev *vpu)
{
	if (atomic_inc_return(&vpu->ref_enc) == 1 && vpu->res->setup_encoder)
		vpu->res->setup_encoder(vpu);
}

static void vpu_enc_put(struct vpu_dev *vpu)
{
	atomic_dec(&vpu->ref_enc);
}

static void vpu_dec_get(struct vpu_dev *vpu)
{
	if (atomic_inc_return(&vpu->ref_dec) == 1 && vpu->res->setup_decoder)
		vpu->res->setup_decoder(vpu);
}

static void vpu_dec_put(struct vpu_dev *vpu)
{
	atomic_dec(&vpu->ref_dec);
}

static int vpu_init_media_device(struct vpu_dev *vpu)
{
	vpu->mdev.dev = vpu->dev;
	strscpy(vpu->mdev.model, "amphion-vpu", sizeof(vpu->mdev.model));
	strscpy(vpu->mdev.bus_info, "platform: amphion-vpu", sizeof(vpu->mdev.bus_info));
	media_device_init(&vpu->mdev);
	vpu->v4l2_dev.mdev = &vpu->mdev;

	return 0;
}

static int vpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vpu_dev *vpu;
	int ret;

	dev_dbg(dev, "probe\n");
	vpu = devm_kzalloc(dev, sizeof(*vpu), GFP_KERNEL);
	if (!vpu)
		return -ENOMEM;

	vpu->pdev = pdev;
	vpu->dev = dev;
	mutex_init(&vpu->lock);
	INIT_LIST_HEAD(&vpu->cores);
	platform_set_drvdata(pdev, vpu);
	atomic_set(&vpu->ref_vpu, 0);
	atomic_set(&vpu->ref_enc, 0);
	atomic_set(&vpu->ref_dec, 0);
	vpu->get_vpu = vpu_dev_get;
	vpu->put_vpu = vpu_dev_put;
	vpu->get_enc = vpu_enc_get;
	vpu->put_enc = vpu_enc_put;
	vpu->get_dec = vpu_dec_get;
	vpu->put_dec = vpu_dec_put;

	vpu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vpu->base))
		return PTR_ERR(vpu->base);

	vpu->res = of_device_get_match_data(dev);
	if (!vpu->res)
		return -ENODEV;

	pm_runtime_enable(dev);

	ret = v4l2_device_register(dev, &vpu->v4l2_dev);
	if (ret)
		goto err_vpu_deinit;

	vpu_init_media_device(vpu);
	vpu->encoder.type = VPU_CORE_TYPE_ENC;
	vpu->encoder.function = MEDIA_ENT_F_PROC_VIDEO_ENCODER;
	vpu->decoder.type = VPU_CORE_TYPE_DEC;
	vpu->decoder.function = MEDIA_ENT_F_PROC_VIDEO_DECODER;
	ret = vpu_add_func(vpu, &vpu->decoder);
	if (ret)
		goto err_add_decoder;
	ret = vpu_add_func(vpu, &vpu->encoder);
	if (ret)
		goto err_add_encoder;
	ret = media_device_register(&vpu->mdev);
	if (ret)
		goto err_vpu_media;
	vpu->debugfs = debugfs_create_dir("amphion_vpu", NULL);

	of_platform_populate(dev->of_node, NULL, NULL, dev);

	return 0;

err_vpu_media:
	vpu_remove_func(&vpu->encoder);
err_add_encoder:
	vpu_remove_func(&vpu->decoder);
err_add_decoder:
	media_device_cleanup(&vpu->mdev);
	v4l2_device_unregister(&vpu->v4l2_dev);
err_vpu_deinit:
	pm_runtime_set_suspended(dev);
	pm_runtime_disable(dev);

	return ret;
}

static void vpu_remove(struct platform_device *pdev)
{
	struct vpu_dev *vpu = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	debugfs_remove_recursive(vpu->debugfs);
	vpu->debugfs = NULL;

	pm_runtime_disable(dev);

	media_device_unregister(&vpu->mdev);
	vpu_remove_func(&vpu->decoder);
	vpu_remove_func(&vpu->encoder);
	media_device_cleanup(&vpu->mdev);
	v4l2_device_unregister(&vpu->v4l2_dev);
	mutex_destroy(&vpu->lock);
}

static int __maybe_unused vpu_runtime_resume(struct device *dev)
{
	return 0;
}

static int __maybe_unused vpu_runtime_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused vpu_resume(struct device *dev)
{
	return 0;
}

static int __maybe_unused vpu_suspend(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops vpu_pm_ops = {
	SET_RUNTIME_PM_OPS(vpu_runtime_suspend, vpu_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(vpu_suspend, vpu_resume)
};

static struct vpu_resources imx8qxp_res = {
	.plat_type = IMX8QXP,
	.mreg_base = 0x40000000,
	.setup = vpu_imx8q_setup,
	.setup_encoder = vpu_imx8q_setup_enc,
	.setup_decoder = vpu_imx8q_setup_dec,
	.reset = vpu_imx8q_reset
};

static struct vpu_resources imx8qm_res = {
	.plat_type = IMX8QM,
	.mreg_base = 0x40000000,
	.setup = vpu_imx8q_setup,
	.setup_encoder = vpu_imx8q_setup_enc,
	.setup_decoder = vpu_imx8q_setup_dec,
	.reset = vpu_imx8q_reset
};

static const struct of_device_id vpu_dt_match[] = {
	{ .compatible = "nxp,imx8qxp-vpu", .data = &imx8qxp_res },
	{ .compatible = "nxp,imx8qm-vpu", .data = &imx8qm_res },
	{}
};
MODULE_DEVICE_TABLE(of, vpu_dt_match);

static struct platform_driver amphion_vpu_driver = {
	.probe = vpu_probe,
	.remove_new = vpu_remove,
	.driver = {
		.name = "amphion-vpu",
		.of_match_table = vpu_dt_match,
		.pm = &vpu_pm_ops,
	},
};

static int __init vpu_driver_init(void)
{
	int ret;

	ret = platform_driver_register(&amphion_vpu_driver);
	if (ret)
		return ret;

	ret = vpu_core_driver_init();
	if (ret)
		platform_driver_unregister(&amphion_vpu_driver);

	return ret;
}

static void __exit vpu_driver_exit(void)
{
	vpu_core_driver_exit();
	platform_driver_unregister(&amphion_vpu_driver);
}
module_init(vpu_driver_init);
module_exit(vpu_driver_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Linux VPU driver for Freescale i.MX8Q");
MODULE_LICENSE("GPL v2");
