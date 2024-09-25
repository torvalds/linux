// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*	Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "mtk_vcodec_enc.h"
#include "mtk_vcodec_enc_pm.h"
#include "../common/mtk_vcodec_intr.h"

static const struct mtk_video_fmt mtk_video_formats_output[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.type = MTK_FMT_FRAME,
		.num_planes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21M,
		.type = MTK_FMT_FRAME,
		.num_planes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV420M,
		.type = MTK_FMT_FRAME,
		.num_planes = 3,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVU420M,
		.type = MTK_FMT_FRAME,
		.num_planes = 3,
	},
};

static const struct mtk_video_fmt mtk_video_formats_capture_h264[] =  {
	{
		.fourcc = V4L2_PIX_FMT_H264,
		.type = MTK_FMT_ENC,
		.num_planes = 1,
	},
};

static const struct mtk_video_fmt mtk_video_formats_capture_vp8[] =  {
	{
		.fourcc = V4L2_PIX_FMT_VP8,
		.type = MTK_FMT_ENC,
		.num_planes = 1,
	},
};

static void clean_irq_status(unsigned int irq_status, void __iomem *addr)
{
	if (irq_status & MTK_VENC_IRQ_STATUS_PAUSE)
		writel(MTK_VENC_IRQ_STATUS_PAUSE, addr);

	if (irq_status & MTK_VENC_IRQ_STATUS_SWITCH)
		writel(MTK_VENC_IRQ_STATUS_SWITCH, addr);

	if (irq_status & MTK_VENC_IRQ_STATUS_DRAM)
		writel(MTK_VENC_IRQ_STATUS_DRAM, addr);

	if (irq_status & MTK_VENC_IRQ_STATUS_SPS)
		writel(MTK_VENC_IRQ_STATUS_SPS, addr);

	if (irq_status & MTK_VENC_IRQ_STATUS_PPS)
		writel(MTK_VENC_IRQ_STATUS_PPS, addr);

	if (irq_status & MTK_VENC_IRQ_STATUS_FRM)
		writel(MTK_VENC_IRQ_STATUS_FRM, addr);

}
static irqreturn_t mtk_vcodec_enc_irq_handler(int irq, void *priv)
{
	struct mtk_vcodec_enc_dev *dev = priv;
	struct mtk_vcodec_enc_ctx *ctx;
	unsigned long flags;
	void __iomem *addr;
	int core_id;

	spin_lock_irqsave(&dev->irqlock, flags);
	ctx = dev->curr_ctx;
	spin_unlock_irqrestore(&dev->irqlock, flags);

	core_id = dev->venc_pdata->core_id;
	if (core_id < 0 || core_id >= NUM_MAX_VCODEC_REG_BASE) {
		mtk_v4l2_venc_err(ctx, "Invalid core id: %d, ctx id: %d", core_id, ctx->id);
		return IRQ_HANDLED;
	}

	mtk_v4l2_venc_dbg(1, ctx, "id: %d, core id: %d", ctx->id, core_id);

	addr = dev->reg_base[core_id] + MTK_VENC_IRQ_ACK_OFFSET;

	ctx->irq_status = readl(dev->reg_base[core_id] +
				(MTK_VENC_IRQ_STATUS_OFFSET));

	clean_irq_status(ctx->irq_status, addr);

	wake_up_enc_ctx(ctx, MTK_INST_IRQ_RECEIVED, 0);
	return IRQ_HANDLED;
}

static int fops_vcodec_open(struct file *file)
{
	struct mtk_vcodec_enc_dev *dev = video_drvdata(file);
	struct mtk_vcodec_enc_ctx *ctx = NULL;
	int ret = 0;
	struct vb2_queue *src_vq;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&dev->dev_mutex);
	/*
	 * Use simple counter to uniquely identify this context. Only
	 * used for logging.
	 */
	ctx->id = dev->id_counter++;
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	INIT_LIST_HEAD(&ctx->list);
	ctx->dev = dev;
	init_waitqueue_head(&ctx->queue[0]);
	mutex_init(&ctx->q_mutex);

	ctx->type = MTK_INST_ENCODER;
	ret = mtk_vcodec_enc_ctrls_setup(ctx);
	if (ret) {
		mtk_v4l2_venc_err(ctx, "Failed to setup controls() (%d)", ret);
		goto err_ctrls_setup;
	}
	ctx->m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev_enc, ctx,
					 &mtk_vcodec_enc_queue_init);
	if (IS_ERR((__force void *)ctx->m2m_ctx)) {
		ret = PTR_ERR((__force void *)ctx->m2m_ctx);
		mtk_v4l2_venc_err(ctx, "Failed to v4l2_m2m_ctx_init() (%d)", ret);
		goto err_m2m_ctx_init;
	}
	src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
				 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	ctx->empty_flush_buf.vb.vb2_buf.vb2_queue = src_vq;
	mtk_vcodec_enc_set_default_params(ctx);

	if (v4l2_fh_is_singular(&ctx->fh)) {
		/*
		 * load fireware to checks if it was loaded already and
		 * does nothing in that case
		 */
		ret = mtk_vcodec_fw_load_firmware(dev->fw_handler);
		if (ret < 0) {
			/*
			 * Return 0 if downloading firmware successfully,
			 * otherwise it is failed
			 */
			mtk_v4l2_venc_err(ctx, "vpu_load_firmware failed!");
			goto err_load_fw;
		}

		dev->enc_capability =
			mtk_vcodec_fw_get_venc_capa(dev->fw_handler);
		mtk_v4l2_venc_dbg(0, ctx, "encoder capability %x", dev->enc_capability);
	}

	mtk_v4l2_venc_dbg(2, ctx, "Create instance [%d]@%p m2m_ctx=%p ",
			  ctx->id, ctx, ctx->m2m_ctx);

	mutex_lock(&dev->dev_ctx_lock);
	list_add(&ctx->list, &dev->ctx_list);
	mutex_unlock(&dev->dev_ctx_lock);

	mutex_unlock(&dev->dev_mutex);
	mtk_v4l2_venc_dbg(0, ctx, "%s encoder [%d]", dev_name(&dev->plat_dev->dev),
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
	struct mtk_vcodec_enc_dev *dev = video_drvdata(file);
	struct mtk_vcodec_enc_ctx *ctx = fh_to_enc_ctx(file->private_data);

	mtk_v4l2_venc_dbg(1, ctx, "[%d] encoder", ctx->id);
	mutex_lock(&dev->dev_mutex);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	mtk_vcodec_enc_release(ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);

	mutex_lock(&dev->dev_ctx_lock);
	list_del_init(&ctx->list);
	mutex_unlock(&dev->dev_ctx_lock);
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
	struct mtk_vcodec_enc_dev *dev;
	struct video_device *vfd_enc;
	phandle rproc_phandle;
	enum mtk_vcodec_fw_type fw_type;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&dev->ctx_list);
	dev->plat_dev = pdev;

	if (!of_property_read_u32(pdev->dev.of_node, "mediatek,vpu",
				  &rproc_phandle)) {
		fw_type = VPU;
	} else if (!of_property_read_u32(pdev->dev.of_node, "mediatek,scp",
					 &rproc_phandle)) {
		fw_type = SCP;
	} else {
		dev_err(&pdev->dev, "[MTK VCODEC] Could not get venc IPI device");
		return -ENODEV;
	}
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	dev->fw_handler = mtk_vcodec_fw_select(dev, fw_type, ENCODER);
	if (IS_ERR(dev->fw_handler))
		return PTR_ERR(dev->fw_handler);

	dev->venc_pdata = of_device_get_match_data(&pdev->dev);
	ret = mtk_vcodec_init_enc_clk(dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "[MTK VCODEC] Failed to get mtk vcodec clock source!");
		goto err_enc_pm;
	}

	pm_runtime_enable(&pdev->dev);

	dev->reg_base[dev->venc_pdata->core_id] =
		devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->reg_base[dev->venc_pdata->core_id])) {
		ret = PTR_ERR(dev->reg_base[dev->venc_pdata->core_id]);
		goto err_res;
	}

	dev->enc_irq = platform_get_irq(pdev, 0);
	if (dev->enc_irq < 0) {
		ret = dev->enc_irq;
		goto err_res;
	}

	irq_set_status_flags(dev->enc_irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(&pdev->dev, dev->enc_irq,
			       mtk_vcodec_enc_irq_handler,
			       0, pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev,
			"[MTK VCODEC] Failed to install dev->enc_irq %d (%d) core_id (%d)",
			dev->enc_irq, ret, dev->venc_pdata->core_id);
		ret = -EINVAL;
		goto err_res;
	}

	mutex_init(&dev->enc_mutex);
	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->dev_ctx_lock);
	spin_lock_init(&dev->irqlock);

	snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name), "%s",
		 "[MTK_V4L2_VENC]");

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "[MTK VCODEC] v4l2_device_register err=%d", ret);
		goto err_res;
	}

	/* allocate video device for encoder and register it */
	vfd_enc = video_device_alloc();
	if (!vfd_enc) {
		dev_err(&pdev->dev, "[MTK VCODEC] Failed to allocate video device");
		ret = -ENOMEM;
		goto err_enc_alloc;
	}
	vfd_enc->fops           = &mtk_vcodec_fops;
	vfd_enc->ioctl_ops      = &mtk_venc_ioctl_ops;
	vfd_enc->release        = video_device_release;
	vfd_enc->lock           = &dev->dev_mutex;
	vfd_enc->v4l2_dev       = &dev->v4l2_dev;
	vfd_enc->vfl_dir        = VFL_DIR_M2M;
	vfd_enc->device_caps	= V4L2_CAP_VIDEO_M2M_MPLANE |
					V4L2_CAP_STREAMING;

	snprintf(vfd_enc->name, sizeof(vfd_enc->name), "%s",
		 MTK_VCODEC_ENC_NAME);
	video_set_drvdata(vfd_enc, dev);
	dev->vfd_enc = vfd_enc;
	platform_set_drvdata(pdev, dev);

	dev->m2m_dev_enc = v4l2_m2m_init(&mtk_venc_m2m_ops);
	if (IS_ERR((__force void *)dev->m2m_dev_enc)) {
		dev_err(&pdev->dev, "[MTK VCODEC] Failed to init mem2mem enc device");
		ret = PTR_ERR((__force void *)dev->m2m_dev_enc);
		goto err_enc_mem_init;
	}

	dev->encode_workqueue =
			alloc_ordered_workqueue(MTK_VCODEC_ENC_NAME,
						WQ_MEM_RECLAIM |
						WQ_FREEZABLE);
	if (!dev->encode_workqueue) {
		dev_err(&pdev->dev, "[MTK VCODEC] Failed to create encode workqueue");
		ret = -EINVAL;
		goto err_event_workq;
	}

	ret = video_register_device(vfd_enc, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(&pdev->dev, "[MTK VCODEC] Failed to register video device");
		goto err_enc_reg;
	}

	mtk_vcodec_dbgfs_init(dev, true);
	dev_dbg(&pdev->dev,  "[MTK VCODEC] encoder %d registered as /dev/video%d",
		dev->venc_pdata->core_id, vfd_enc->num);

	return 0;

err_enc_reg:
	destroy_workqueue(dev->encode_workqueue);
err_event_workq:
	v4l2_m2m_release(dev->m2m_dev_enc);
err_enc_mem_init:
	video_unregister_device(vfd_enc);
err_enc_alloc:
	v4l2_device_unregister(&dev->v4l2_dev);
err_res:
	pm_runtime_disable(dev->pm.dev);
err_enc_pm:
	mtk_vcodec_fw_release(dev->fw_handler);
	return ret;
}

static const struct mtk_vcodec_enc_pdata mt8173_avc_pdata = {
	.capture_formats = mtk_video_formats_capture_h264,
	.num_capture_formats = ARRAY_SIZE(mtk_video_formats_capture_h264),
	.output_formats = mtk_video_formats_output,
	.num_output_formats = ARRAY_SIZE(mtk_video_formats_output),
	.min_bitrate = 64,
	.max_bitrate = 60000000,
	.core_id = VENC_SYS,
};

static const struct mtk_vcodec_enc_pdata mt8173_vp8_pdata = {
	.capture_formats = mtk_video_formats_capture_vp8,
	.num_capture_formats = ARRAY_SIZE(mtk_video_formats_capture_vp8),
	.output_formats = mtk_video_formats_output,
	.num_output_formats = ARRAY_SIZE(mtk_video_formats_output),
	.min_bitrate = 64,
	.max_bitrate = 9000000,
	.core_id = VENC_LT_SYS,
};

static const struct mtk_vcodec_enc_pdata mt8183_pdata = {
	.uses_ext = true,
	.capture_formats = mtk_video_formats_capture_h264,
	.num_capture_formats = ARRAY_SIZE(mtk_video_formats_capture_h264),
	.output_formats = mtk_video_formats_output,
	.num_output_formats = ARRAY_SIZE(mtk_video_formats_output),
	.min_bitrate = 64,
	.max_bitrate = 40000000,
	.core_id = VENC_SYS,
};

static const struct mtk_vcodec_enc_pdata mt8188_pdata = {
	.uses_ext = true,
	.capture_formats = mtk_video_formats_capture_h264,
	.num_capture_formats = ARRAY_SIZE(mtk_video_formats_capture_h264),
	.output_formats = mtk_video_formats_output,
	.num_output_formats = ARRAY_SIZE(mtk_video_formats_output),
	.min_bitrate = 64,
	.max_bitrate = 50000000,
	.core_id = VENC_SYS,
	.uses_34bit = true,
};

static const struct mtk_vcodec_enc_pdata mt8192_pdata = {
	.uses_ext = true,
	.capture_formats = mtk_video_formats_capture_h264,
	.num_capture_formats = ARRAY_SIZE(mtk_video_formats_capture_h264),
	.output_formats = mtk_video_formats_output,
	.num_output_formats = ARRAY_SIZE(mtk_video_formats_output),
	.min_bitrate = 64,
	.max_bitrate = 100000000,
	.core_id = VENC_SYS,
};

static const struct mtk_vcodec_enc_pdata mt8195_pdata = {
	.uses_ext = true,
	.capture_formats = mtk_video_formats_capture_h264,
	.num_capture_formats = ARRAY_SIZE(mtk_video_formats_capture_h264),
	.output_formats = mtk_video_formats_output,
	.num_output_formats = ARRAY_SIZE(mtk_video_formats_output),
	.min_bitrate = 64,
	.max_bitrate = 100000000,
	.core_id = VENC_SYS,
};

static const struct of_device_id mtk_vcodec_enc_match[] = {
	{.compatible = "mediatek,mt8173-vcodec-enc",
			.data = &mt8173_avc_pdata},
	{.compatible = "mediatek,mt8173-vcodec-enc-vp8",
			.data = &mt8173_vp8_pdata},
	{.compatible = "mediatek,mt8183-vcodec-enc", .data = &mt8183_pdata},
	{.compatible = "mediatek,mt8188-vcodec-enc", .data = &mt8188_pdata},
	{.compatible = "mediatek,mt8192-vcodec-enc", .data = &mt8192_pdata},
	{.compatible = "mediatek,mt8195-vcodec-enc", .data = &mt8195_pdata},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_vcodec_enc_match);

static void mtk_vcodec_enc_remove(struct platform_device *pdev)
{
	struct mtk_vcodec_enc_dev *dev = platform_get_drvdata(pdev);

	destroy_workqueue(dev->encode_workqueue);
	if (dev->m2m_dev_enc)
		v4l2_m2m_release(dev->m2m_dev_enc);

	if (dev->vfd_enc)
		video_unregister_device(dev->vfd_enc);

	mtk_vcodec_dbgfs_deinit(&dev->dbgfs);
	v4l2_device_unregister(&dev->v4l2_dev);
	pm_runtime_disable(dev->pm.dev);
	mtk_vcodec_fw_release(dev->fw_handler);
}

static struct platform_driver mtk_vcodec_enc_driver = {
	.probe	= mtk_vcodec_probe,
	.remove = mtk_vcodec_enc_remove,
	.driver	= {
		.name	= MTK_VCODEC_ENC_NAME,
		.of_match_table = mtk_vcodec_enc_match,
	},
};

module_platform_driver(mtk_vcodec_enc_driver);


MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek video codec V4L2 encoder driver");
