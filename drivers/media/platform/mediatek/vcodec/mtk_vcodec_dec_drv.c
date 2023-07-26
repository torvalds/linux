// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 *         Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_dec_hw.h"
#include "mtk_vcodec_dec_pm.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_fw.h"

static int mtk_vcodec_get_hw_count(struct mtk_vcodec_dev *dev)
{
	switch (dev->vdec_pdata->hw_arch) {
	case MTK_VDEC_PURE_SINGLE_CORE:
		return MTK_VDEC_ONE_CORE;
	case MTK_VDEC_LAT_SINGLE_CORE:
		return MTK_VDEC_ONE_LAT_ONE_CORE;
	default:
		mtk_v4l2_err("hw arch %d not supported", dev->vdec_pdata->hw_arch);
		return MTK_VDEC_NO_HW;
	}
}

static bool mtk_vcodec_is_hw_active(struct mtk_vcodec_dev *dev)
{
	u32 cg_status;

	if (dev->vdecsys_regmap)
		return !regmap_test_bits(dev->vdecsys_regmap, VDEC_HW_ACTIVE_ADDR,
					 VDEC_HW_ACTIVE_MASK);

	cg_status = readl(dev->reg_base[VDEC_SYS] + VDEC_HW_ACTIVE_ADDR);
	return !FIELD_GET(VDEC_HW_ACTIVE_MASK, cg_status);
}

static irqreturn_t mtk_vcodec_dec_irq_handler(int irq, void *priv)
{
	struct mtk_vcodec_dev *dev = priv;
	struct mtk_vcodec_ctx *ctx;
	unsigned int dec_done_status = 0;
	void __iomem *vdec_misc_addr = dev->reg_base[VDEC_MISC] +
					VDEC_IRQ_CFG_REG;

	ctx = mtk_vcodec_get_curr_ctx(dev, MTK_VDEC_CORE);

	if (!mtk_vcodec_is_hw_active(dev)) {
		mtk_v4l2_err("DEC ISR, VDEC active is not 0x0");
		return IRQ_HANDLED;
	}

	dec_done_status = readl(vdec_misc_addr);
	ctx->irq_status = dec_done_status;
	if ((dec_done_status & MTK_VDEC_IRQ_STATUS_DEC_SUCCESS) !=
		MTK_VDEC_IRQ_STATUS_DEC_SUCCESS)
		return IRQ_HANDLED;

	/* clear interrupt */
	writel((readl(vdec_misc_addr) | VDEC_IRQ_CFG),
		dev->reg_base[VDEC_MISC] + VDEC_IRQ_CFG_REG);
	writel((readl(vdec_misc_addr) & ~VDEC_IRQ_CLR),
		dev->reg_base[VDEC_MISC] + VDEC_IRQ_CFG_REG);

	wake_up_ctx(ctx, MTK_INST_IRQ_RECEIVED, 0);

	mtk_v4l2_debug(3,
			"mtk_vcodec_dec_irq_handler :wake up ctx %d, dec_done_status=%x",
			ctx->id, dec_done_status);

	return IRQ_HANDLED;
}

static int mtk_vcodec_get_reg_bases(struct mtk_vcodec_dev *dev)
{
	struct platform_device *pdev = dev->plat_dev;
	int reg_num, i;
	struct resource *res;
	bool has_vdecsys_reg;
	int num_max_vdec_regs;
	static const char * const mtk_dec_reg_names[] = {
		"misc",
		"ld",
		"top",
		"cm",
		"ad",
		"av",
		"pp",
		"hwd",
		"hwq",
		"hwb",
		"hwg"
	};

	/*
	 * If we have reg-names in devicetree, this means that we're on a new
	 * register organization, which implies that the VDEC_SYS iospace gets
	 * R/W through a syscon (regmap).
	 * Here we try to get the "misc" iostart only to check if we have reg-names
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "misc");
	if (res)
		has_vdecsys_reg = false;
	else
		has_vdecsys_reg = true;

	num_max_vdec_regs = has_vdecsys_reg ? NUM_MAX_VDEC_REG_BASE :
					      ARRAY_SIZE(mtk_dec_reg_names);

	/* Sizeof(u32) * 4 bytes for each register base. */
	reg_num = of_property_count_elems_of_size(pdev->dev.of_node, "reg",
						  sizeof(u32) * 4);
	if (reg_num <= 0 || reg_num > num_max_vdec_regs) {
		dev_err(&pdev->dev, "Invalid register property size: %d\n", reg_num);
		return -EINVAL;
	}

	if (has_vdecsys_reg) {
		for (i = 0; i < reg_num; i++) {
			dev->reg_base[i] = devm_platform_ioremap_resource(pdev, i);
			if (IS_ERR(dev->reg_base[i]))
				return PTR_ERR(dev->reg_base[i]);

			mtk_v4l2_debug(2, "reg[%d] base=%p", i, dev->reg_base[i]);
		}
	} else {
		for (i = 0; i < reg_num; i++) {
			dev->reg_base[i+1] = devm_platform_ioremap_resource_byname(pdev, mtk_dec_reg_names[i]);
			if (IS_ERR(dev->reg_base[i+1]))
				return PTR_ERR(dev->reg_base[i+1]);

			mtk_v4l2_debug(2, "reg[%d] base=%p", i+1, dev->reg_base[i+1]);
		}

		dev->vdecsys_regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
								      "mediatek,vdecsys");
		if (IS_ERR(dev->vdecsys_regmap)) {
			dev_err(&pdev->dev, "Missing mediatek,vdecsys property");
			return PTR_ERR(dev->vdecsys_regmap);
		}
	}

	return 0;
}

static int mtk_vcodec_init_dec_resources(struct mtk_vcodec_dev *dev)
{
	struct platform_device *pdev = dev->plat_dev;
	int ret;

	ret = mtk_vcodec_get_reg_bases(dev);
	if (ret)
		return ret;

	if (dev->vdec_pdata->is_subdev_supported)
		return 0;

	dev->dec_irq = platform_get_irq(pdev, 0);
	if (dev->dec_irq < 0)
		return dev->dec_irq;

	irq_set_status_flags(dev->dec_irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(&pdev->dev, dev->dec_irq,
			       mtk_vcodec_dec_irq_handler, 0, pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to install dev->dec_irq %d (%d)",
			dev->dec_irq, ret);
		return ret;
	}

	ret = mtk_vcodec_init_dec_clk(pdev, &dev->pm);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get mt vcodec clock source");
		return ret;
	}

	pm_runtime_enable(&pdev->dev);
	return 0;
}

static int fops_vcodec_open(struct file *file)
{
	struct mtk_vcodec_dev *dev = video_drvdata(file);
	struct mtk_vcodec_ctx *ctx = NULL;
	int ret = 0, i, hw_count;
	struct vb2_queue *src_vq;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&dev->dev_mutex);
	ctx->id = dev->id_counter++;
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	INIT_LIST_HEAD(&ctx->list);
	ctx->dev = dev;
	if (ctx->dev->vdec_pdata->is_subdev_supported) {
		hw_count = mtk_vcodec_get_hw_count(dev);
		if (!hw_count || !dev->subdev_prob_done) {
			ret = -EINVAL;
			goto err_ctrls_setup;
		}

		ret = dev->subdev_prob_done(dev);
		if (ret)
			goto err_ctrls_setup;

		for (i = 0; i < hw_count; i++)
			init_waitqueue_head(&ctx->queue[i]);
	} else {
		init_waitqueue_head(&ctx->queue[0]);
	}
	mutex_init(&ctx->lock);

	ctx->type = MTK_INST_DECODER;
	ret = dev->vdec_pdata->ctrls_setup(ctx);
	if (ret) {
		mtk_v4l2_err("Failed to setup mt vcodec controls");
		goto err_ctrls_setup;
	}
	ctx->m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev_dec, ctx,
		&mtk_vcodec_dec_queue_init);
	if (IS_ERR((__force void *)ctx->m2m_ctx)) {
		ret = PTR_ERR((__force void *)ctx->m2m_ctx);
		mtk_v4l2_err("Failed to v4l2_m2m_ctx_init() (%d)",
			ret);
		goto err_m2m_ctx_init;
	}
	src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
				V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	ctx->empty_flush_buf.vb.vb2_buf.vb2_queue = src_vq;
	mtk_vcodec_dec_set_default_params(ctx);

	if (v4l2_fh_is_singular(&ctx->fh)) {
		/*
		 * Does nothing if firmware was already loaded.
		 */
		ret = mtk_vcodec_fw_load_firmware(dev->fw_handler);
		if (ret < 0) {
			/*
			 * Return 0 if downloading firmware successfully,
			 * otherwise it is failed
			 */
			mtk_v4l2_err("failed to load firmware!");
			goto err_load_fw;
		}

		dev->dec_capability =
			mtk_vcodec_fw_get_vdec_capa(dev->fw_handler);

		mtk_v4l2_debug(0, "decoder capability %x", dev->dec_capability);
	}

	ctx->dev->vdec_pdata->init_vdec_params(ctx);

	list_add(&ctx->list, &dev->ctx_list);
	mtk_vcodec_dbgfs_create(ctx);

	mutex_unlock(&dev->dev_mutex);
	mtk_v4l2_debug(0, "%s decoder [%d]", dev_name(&dev->plat_dev->dev),
			ctx->id);
	return ret;

	/* Deinit when failure occurred */
err_load_fw:
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
err_m2m_ctx_init:
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
err_ctrls_setup:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&dev->dev_mutex);

	return ret;
}

static int fops_vcodec_release(struct file *file)
{
	struct mtk_vcodec_dev *dev = video_drvdata(file);
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(file->private_data);

	mtk_v4l2_debug(0, "[%d] decoder", ctx->id);
	mutex_lock(&dev->dev_mutex);

	/*
	 * Call v4l2_m2m_ctx_release before mtk_vcodec_dec_release. First, it
	 * makes sure the worker thread is not running after vdec_if_deinit.
	 * Second, the decoder will be flushed and all the buffers will be
	 * returned in stop_streaming.
	 */
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	mtk_vcodec_dec_release(ctx);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);

	mtk_vcodec_dbgfs_remove(dev, ctx->id);
	list_del_init(&ctx->list);
	kfree(ctx);
	mutex_unlock(&dev->dev_mutex);
	return 0;
}

static const struct v4l2_file_operations mtk_vcodec_fops = {
	.owner		= THIS_MODULE,
	.open		= fops_vcodec_open,
	.release	= fops_vcodec_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static int mtk_vcodec_probe(struct platform_device *pdev)
{
	struct mtk_vcodec_dev *dev;
	struct video_device *vfd_dec;
	phandle rproc_phandle;
	enum mtk_vcodec_fw_type fw_type;
	int i, ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&dev->ctx_list);
	dev->plat_dev = pdev;

	dev->vdec_pdata = of_device_get_match_data(&pdev->dev);
	if (!of_property_read_u32(pdev->dev.of_node, "mediatek,vpu",
				  &rproc_phandle)) {
		fw_type = VPU;
	} else if (!of_property_read_u32(pdev->dev.of_node, "mediatek,scp",
					 &rproc_phandle)) {
		fw_type = SCP;
	} else {
		mtk_v4l2_err("Could not get vdec IPI device");
		return -ENODEV;
	}
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	dev->fw_handler = mtk_vcodec_fw_select(dev, fw_type, DECODER);
	if (IS_ERR(dev->fw_handler))
		return PTR_ERR(dev->fw_handler);

	ret = mtk_vcodec_init_dec_resources(dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init dec resources");
		goto err_dec_pm;
	}

	if (IS_VDEC_LAT_ARCH(dev->vdec_pdata->hw_arch)) {
		dev->core_workqueue =
			alloc_ordered_workqueue("core-decoder",
						WQ_MEM_RECLAIM | WQ_FREEZABLE);
		if (!dev->core_workqueue) {
			mtk_v4l2_err("Failed to create core workqueue");
			ret = -EINVAL;
			goto err_res;
		}
	}

	for (i = 0; i < MTK_VDEC_HW_MAX; i++)
		mutex_init(&dev->dec_mutex[i]);
	mutex_init(&dev->dev_mutex);
	spin_lock_init(&dev->irqlock);

	snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name), "%s",
		"[/MTK_V4L2_VDEC]");

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		mtk_v4l2_err("v4l2_device_register err=%d", ret);
		goto err_core_workq;
	}

	init_waitqueue_head(&dev->queue);

	vfd_dec = video_device_alloc();
	if (!vfd_dec) {
		mtk_v4l2_err("Failed to allocate video device");
		ret = -ENOMEM;
		goto err_dec_alloc;
	}
	vfd_dec->fops		= &mtk_vcodec_fops;
	vfd_dec->ioctl_ops	= &mtk_vdec_ioctl_ops;
	vfd_dec->release	= video_device_release;
	vfd_dec->lock		= &dev->dev_mutex;
	vfd_dec->v4l2_dev	= &dev->v4l2_dev;
	vfd_dec->vfl_dir	= VFL_DIR_M2M;
	vfd_dec->device_caps	= V4L2_CAP_VIDEO_M2M_MPLANE |
			V4L2_CAP_STREAMING;

	snprintf(vfd_dec->name, sizeof(vfd_dec->name), "%s",
		MTK_VCODEC_DEC_NAME);
	video_set_drvdata(vfd_dec, dev);
	dev->vfd_dec = vfd_dec;
	platform_set_drvdata(pdev, dev);

	dev->m2m_dev_dec = v4l2_m2m_init(&mtk_vdec_m2m_ops);
	if (IS_ERR((__force void *)dev->m2m_dev_dec)) {
		mtk_v4l2_err("Failed to init mem2mem dec device");
		ret = PTR_ERR((__force void *)dev->m2m_dev_dec);
		goto err_dec_alloc;
	}

	dev->decode_workqueue =
		alloc_ordered_workqueue(MTK_VCODEC_DEC_NAME,
			WQ_MEM_RECLAIM | WQ_FREEZABLE);
	if (!dev->decode_workqueue) {
		mtk_v4l2_err("Failed to create decode workqueue");
		ret = -EINVAL;
		goto err_event_workq;
	}

	if (dev->vdec_pdata->is_subdev_supported) {
		ret = of_platform_populate(pdev->dev.of_node, NULL, NULL,
					   &pdev->dev);
		if (ret) {
			mtk_v4l2_err("Main device of_platform_populate failed.");
			goto err_reg_cont;
		}
	} else {
		set_bit(MTK_VDEC_CORE, dev->subdev_bitmap);
	}

	atomic_set(&dev->dec_active_cnt, 0);
	memset(dev->vdec_racing_info, 0, sizeof(dev->vdec_racing_info));
	mutex_init(&dev->dec_racing_info_mutex);

	ret = video_register_device(vfd_dec, VFL_TYPE_VIDEO, -1);
	if (ret) {
		mtk_v4l2_err("Failed to register video device");
		goto err_reg_cont;
	}

	if (dev->vdec_pdata->uses_stateless_api) {
		v4l2_disable_ioctl(vfd_dec, VIDIOC_DECODER_CMD);
		v4l2_disable_ioctl(vfd_dec, VIDIOC_TRY_DECODER_CMD);

		dev->mdev_dec.dev = &pdev->dev;
		strscpy(dev->mdev_dec.model, MTK_VCODEC_DEC_NAME,
			sizeof(dev->mdev_dec.model));

		media_device_init(&dev->mdev_dec);
		dev->mdev_dec.ops = &mtk_vcodec_media_ops;
		dev->v4l2_dev.mdev = &dev->mdev_dec;

		ret = v4l2_m2m_register_media_controller(dev->m2m_dev_dec, dev->vfd_dec,
							 MEDIA_ENT_F_PROC_VIDEO_DECODER);
		if (ret) {
			mtk_v4l2_err("Failed to register media controller");
			goto err_dec_mem_init;
		}

		ret = media_device_register(&dev->mdev_dec);
		if (ret) {
			mtk_v4l2_err("Failed to register media device");
			goto err_media_reg;
		}

		mtk_v4l2_debug(0, "media registered as /dev/media%d", vfd_dec->minor);
	}

	mtk_vcodec_dbgfs_init(dev, false);
	mtk_v4l2_debug(0, "decoder registered as /dev/video%d", vfd_dec->minor);

	return 0;

err_media_reg:
	v4l2_m2m_unregister_media_controller(dev->m2m_dev_dec);
err_dec_mem_init:
	video_unregister_device(vfd_dec);
err_reg_cont:
	if (dev->vdec_pdata->uses_stateless_api)
		media_device_cleanup(&dev->mdev_dec);
	destroy_workqueue(dev->decode_workqueue);
err_event_workq:
	v4l2_m2m_release(dev->m2m_dev_dec);
err_dec_alloc:
	v4l2_device_unregister(&dev->v4l2_dev);
err_core_workq:
	if (IS_VDEC_LAT_ARCH(dev->vdec_pdata->hw_arch))
		destroy_workqueue(dev->core_workqueue);
err_res:
	if (!dev->vdec_pdata->is_subdev_supported)
		pm_runtime_disable(dev->pm.dev);
err_dec_pm:
	mtk_vcodec_fw_release(dev->fw_handler);
	return ret;
}

static const struct of_device_id mtk_vcodec_match[] = {
	{
		.compatible = "mediatek,mt8173-vcodec-dec",
		.data = &mtk_vdec_8173_pdata,
	},
	{
		.compatible = "mediatek,mt8183-vcodec-dec",
		.data = &mtk_vdec_8183_pdata,
	},
	{
		.compatible = "mediatek,mt8192-vcodec-dec",
		.data = &mtk_lat_sig_core_pdata,
	},
	{
		.compatible = "mediatek,mt8186-vcodec-dec",
		.data = &mtk_vdec_single_core_pdata,
	},
	{
		.compatible = "mediatek,mt8195-vcodec-dec",
		.data = &mtk_lat_sig_core_pdata,
	},
	{
		.compatible = "mediatek,mt8188-vcodec-dec",
		.data = &mtk_lat_sig_core_pdata,
	},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_vcodec_match);

static void mtk_vcodec_dec_remove(struct platform_device *pdev)
{
	struct mtk_vcodec_dev *dev = platform_get_drvdata(pdev);

	destroy_workqueue(dev->decode_workqueue);

	if (media_devnode_is_registered(dev->mdev_dec.devnode)) {
		media_device_unregister(&dev->mdev_dec);
		v4l2_m2m_unregister_media_controller(dev->m2m_dev_dec);
		media_device_cleanup(&dev->mdev_dec);
	}

	if (dev->m2m_dev_dec)
		v4l2_m2m_release(dev->m2m_dev_dec);

	if (dev->vfd_dec)
		video_unregister_device(dev->vfd_dec);

	mtk_vcodec_dbgfs_deinit(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	if (!dev->vdec_pdata->is_subdev_supported)
		pm_runtime_disable(dev->pm.dev);
	mtk_vcodec_fw_release(dev->fw_handler);
}

static struct platform_driver mtk_vcodec_dec_driver = {
	.probe	= mtk_vcodec_probe,
	.remove_new = mtk_vcodec_dec_remove,
	.driver	= {
		.name	= MTK_VCODEC_DEC_NAME,
		.of_match_table = mtk_vcodec_match,
	},
};

module_platform_driver(mtk_vcodec_dec_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek video codec V4L2 decoder driver");
