// SPDX-License-Identifier: GPL-2.0
/*
 * Cedrus VPU driver
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-mem2mem.h>

#include "cedrus.h"
#include "cedrus_video.h"
#include "cedrus_dec.h"
#include "cedrus_hw.h"

static const struct cedrus_control cedrus_controls[] = {
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS,
		},
		.codec		= CEDRUS_CODEC_MPEG2,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION,
		},
		.codec		= CEDRUS_CODEC_MPEG2,
		.required	= false,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_DECODE_PARAMS,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_SLICE_PARAMS,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_SPS,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_PPS,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_SCALING_MATRIX,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= false,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_PRED_WEIGHTS,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= false,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_DECODE_MODE,
			.max	= V4L2_STATELESS_H264_DECODE_MODE_SLICE_BASED,
			.def	= V4L2_STATELESS_H264_DECODE_MODE_SLICE_BASED,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= false,
	},
	{
		.cfg = {
			.id	= V4L2_CID_STATELESS_H264_START_CODE,
			.max	= V4L2_STATELESS_H264_START_CODE_NONE,
			.def	= V4L2_STATELESS_H264_START_CODE_NONE,
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= false,
	},
	/*
	 * We only expose supported profiles information,
	 * and not levels as it's not clear what is supported
	 * for each hardware/core version.
	 * In any case, TRY/S_FMT will clamp the format resolution
	 * to the maximum supported.
	 */
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			.min	= V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
			.def	= V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
			.max	= V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
			.menu_skip_mask =
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED),
		},
		.codec		= CEDRUS_CODEC_H264,
		.required	= false,
	},
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_HEVC_SPS,
		},
		.codec		= CEDRUS_CODEC_H265,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_HEVC_PPS,
		},
		.codec		= CEDRUS_CODEC_H265,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS,
		},
		.codec		= CEDRUS_CODEC_H265,
		.required	= true,
	},
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE,
			.max	= V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_SLICE_BASED,
			.def	= V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_SLICE_BASED,
		},
		.codec		= CEDRUS_CODEC_H265,
		.required	= false,
	},
	{
		.cfg = {
			.id	= V4L2_CID_MPEG_VIDEO_HEVC_START_CODE,
			.max	= V4L2_MPEG_VIDEO_HEVC_START_CODE_NONE,
			.def	= V4L2_MPEG_VIDEO_HEVC_START_CODE_NONE,
		},
		.codec		= CEDRUS_CODEC_H265,
		.required	= false,
	},
	{
		.cfg = {
			.id		= V4L2_CID_MPEG_VIDEO_VP8_FRAME_HEADER,
		},
		.codec		= CEDRUS_CODEC_VP8,
		.required	= true,
	},
};

#define CEDRUS_CONTROLS_COUNT	ARRAY_SIZE(cedrus_controls)

void *cedrus_find_control_data(struct cedrus_ctx *ctx, u32 id)
{
	unsigned int i;

	for (i = 0; ctx->ctrls[i]; i++)
		if (ctx->ctrls[i]->id == id)
			return ctx->ctrls[i]->p_cur.p;

	return NULL;
}

static int cedrus_init_ctrls(struct cedrus_dev *dev, struct cedrus_ctx *ctx)
{
	struct v4l2_ctrl_handler *hdl = &ctx->hdl;
	struct v4l2_ctrl *ctrl;
	unsigned int ctrl_size;
	unsigned int i;

	v4l2_ctrl_handler_init(hdl, CEDRUS_CONTROLS_COUNT);
	if (hdl->error) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to initialize control handler\n");
		return hdl->error;
	}

	ctrl_size = sizeof(ctrl) * CEDRUS_CONTROLS_COUNT + 1;

	ctx->ctrls = kzalloc(ctrl_size, GFP_KERNEL);
	if (!ctx->ctrls)
		return -ENOMEM;

	for (i = 0; i < CEDRUS_CONTROLS_COUNT; i++) {
		ctrl = v4l2_ctrl_new_custom(hdl, &cedrus_controls[i].cfg,
					    NULL);
		if (hdl->error) {
			v4l2_err(&dev->v4l2_dev,
				 "Failed to create new custom control\n");

			v4l2_ctrl_handler_free(hdl);
			kfree(ctx->ctrls);
			return hdl->error;
		}

		ctx->ctrls[i] = ctrl;
	}

	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	return 0;
}

static int cedrus_request_validate(struct media_request *req)
{
	struct media_request_object *obj;
	struct v4l2_ctrl_handler *parent_hdl, *hdl;
	struct cedrus_ctx *ctx = NULL;
	struct v4l2_ctrl *ctrl_test;
	unsigned int count;
	unsigned int i;
	int ret = 0;

	list_for_each_entry(obj, &req->objects, list) {
		struct vb2_buffer *vb;

		if (vb2_request_object_is_buffer(obj)) {
			vb = container_of(obj, struct vb2_buffer, req_obj);
			ctx = vb2_get_drv_priv(vb->vb2_queue);

			break;
		}
	}

	if (!ctx)
		return -ENOENT;

	count = vb2_request_buffer_cnt(req);
	if (!count) {
		v4l2_info(&ctx->dev->v4l2_dev,
			  "No buffer was provided with the request\n");
		return -ENOENT;
	} else if (count > 1) {
		v4l2_info(&ctx->dev->v4l2_dev,
			  "More than one buffer was provided with the request\n");
		return -EINVAL;
	}

	parent_hdl = &ctx->hdl;

	hdl = v4l2_ctrl_request_hdl_find(req, parent_hdl);
	if (!hdl) {
		v4l2_info(&ctx->dev->v4l2_dev, "Missing codec control(s)\n");
		return -ENOENT;
	}

	for (i = 0; i < CEDRUS_CONTROLS_COUNT; i++) {
		if (cedrus_controls[i].codec != ctx->current_codec ||
		    !cedrus_controls[i].required)
			continue;

		ctrl_test = v4l2_ctrl_request_hdl_ctrl_find(hdl,
							    cedrus_controls[i].cfg.id);
		if (!ctrl_test) {
			v4l2_info(&ctx->dev->v4l2_dev,
				  "Missing required codec control\n");
			ret = -ENOENT;
			break;
		}
	}

	v4l2_ctrl_request_hdl_put(hdl);

	if (ret)
		return ret;

	return vb2_request_validate(req);
}

static int cedrus_open(struct file *file)
{
	struct cedrus_dev *dev = video_drvdata(file);
	struct cedrus_ctx *ctx = NULL;
	int ret;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		mutex_unlock(&dev->dev_mutex);
		return -ENOMEM;
	}

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dev = dev;

	ret = cedrus_init_ctrls(dev, ctx);
	if (ret)
		goto err_free;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx,
					    &cedrus_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_ctrls;
	}
	ctx->dst_fmt.pixelformat = V4L2_PIX_FMT_SUNXI_TILED_NV12;
	cedrus_prepare_format(&ctx->dst_fmt);
	ctx->src_fmt.pixelformat = V4L2_PIX_FMT_MPEG2_SLICE;
	/*
	 * TILED_NV12 has more strict requirements, so copy the width and
	 * height to src_fmt to ensure that is matches the dst_fmt resolution.
	 */
	ctx->src_fmt.width = ctx->dst_fmt.width;
	ctx->src_fmt.height = ctx->dst_fmt.height;
	cedrus_prepare_format(&ctx->src_fmt);

	v4l2_fh_add(&ctx->fh);

	mutex_unlock(&dev->dev_mutex);

	return 0;

err_ctrls:
	v4l2_ctrl_handler_free(&ctx->hdl);
err_free:
	kfree(ctx);
	mutex_unlock(&dev->dev_mutex);

	return ret;
}

static int cedrus_release(struct file *file)
{
	struct cedrus_dev *dev = video_drvdata(file);
	struct cedrus_ctx *ctx = container_of(file->private_data,
					      struct cedrus_ctx, fh);

	mutex_lock(&dev->dev_mutex);

	v4l2_fh_del(&ctx->fh);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	v4l2_ctrl_handler_free(&ctx->hdl);
	kfree(ctx->ctrls);

	v4l2_fh_exit(&ctx->fh);

	kfree(ctx);

	mutex_unlock(&dev->dev_mutex);

	return 0;
}

static const struct v4l2_file_operations cedrus_fops = {
	.owner		= THIS_MODULE,
	.open		= cedrus_open,
	.release	= cedrus_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct video_device cedrus_video_device = {
	.name		= CEDRUS_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &cedrus_fops,
	.ioctl_ops	= &cedrus_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
	.device_caps	= V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

static const struct v4l2_m2m_ops cedrus_m2m_ops = {
	.device_run	= cedrus_device_run,
};

static const struct media_device_ops cedrus_m2m_media_ops = {
	.req_validate	= cedrus_request_validate,
	.req_queue	= v4l2_m2m_request_queue,
};

static int cedrus_probe(struct platform_device *pdev)
{
	struct cedrus_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->vfd = cedrus_video_device;
	dev->dev = &pdev->dev;
	dev->pdev = pdev;

	ret = cedrus_hw_probe(dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to probe hardware\n");
		return ret;
	}

	dev->dec_ops[CEDRUS_CODEC_MPEG2] = &cedrus_dec_ops_mpeg2;
	dev->dec_ops[CEDRUS_CODEC_H264] = &cedrus_dec_ops_h264;
	dev->dec_ops[CEDRUS_CODEC_H265] = &cedrus_dec_ops_h265;
	dev->dec_ops[CEDRUS_CODEC_VP8] = &cedrus_dec_ops_vp8;

	mutex_init(&dev->dev_mutex);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register V4L2 device\n");
		return ret;
	}

	vfd = &dev->vfd;
	vfd->lock = &dev->dev_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;

	snprintf(vfd->name, sizeof(vfd->name), "%s", cedrus_video_device.name);
	video_set_drvdata(vfd, dev);

	dev->m2m_dev = v4l2_m2m_init(&cedrus_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to initialize V4L2 M2M device\n");
		ret = PTR_ERR(dev->m2m_dev);

		goto err_v4l2;
	}

	dev->mdev.dev = &pdev->dev;
	strscpy(dev->mdev.model, CEDRUS_NAME, sizeof(dev->mdev.model));
	strscpy(dev->mdev.bus_info, "platform:" CEDRUS_NAME,
		sizeof(dev->mdev.bus_info));

	media_device_init(&dev->mdev);
	dev->mdev.ops = &cedrus_m2m_media_ops;
	dev->v4l2_dev.mdev = &dev->mdev;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto err_m2m;
	}

	v4l2_info(&dev->v4l2_dev,
		  "Device registered as /dev/video%d\n", vfd->num);

	ret = v4l2_m2m_register_media_controller(dev->m2m_dev, vfd,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to initialize V4L2 M2M media controller\n");
		goto err_video;
	}

	ret = media_device_register(&dev->mdev);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register media device\n");
		goto err_m2m_mc;
	}

	platform_set_drvdata(pdev, dev);

	return 0;

err_m2m_mc:
	v4l2_m2m_unregister_media_controller(dev->m2m_dev);
err_video:
	video_unregister_device(&dev->vfd);
err_m2m:
	v4l2_m2m_release(dev->m2m_dev);
err_v4l2:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static int cedrus_remove(struct platform_device *pdev)
{
	struct cedrus_dev *dev = platform_get_drvdata(pdev);

	if (media_devnode_is_registered(dev->mdev.devnode)) {
		media_device_unregister(&dev->mdev);
		v4l2_m2m_unregister_media_controller(dev->m2m_dev);
		media_device_cleanup(&dev->mdev);
	}

	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(&dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);

	cedrus_hw_remove(dev);

	return 0;
}

static const struct cedrus_variant sun4i_a10_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 320000000,
};

static const struct cedrus_variant sun5i_a13_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 320000000,
};

static const struct cedrus_variant sun7i_a20_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 320000000,
};

static const struct cedrus_variant sun8i_a33_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_UNTILED |
			  CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 320000000,
};

static const struct cedrus_variant sun8i_h3_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_UNTILED |
			  CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_H265_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 402000000,
};

static const struct cedrus_variant sun8i_v3s_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_UNTILED |
			  CEDRUS_CAPABILITY_H264_DEC,
	.mod_rate	= 297000000,
};

static const struct cedrus_variant sun8i_r40_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_UNTILED |
			  CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 297000000,
};

static const struct cedrus_variant sun50i_a64_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_UNTILED |
			  CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_H265_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 402000000,
};

static const struct cedrus_variant sun50i_h5_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_UNTILED |
			  CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_H265_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 402000000,
};

static const struct cedrus_variant sun50i_h6_cedrus_variant = {
	.capabilities	= CEDRUS_CAPABILITY_UNTILED |
			  CEDRUS_CAPABILITY_MPEG2_DEC |
			  CEDRUS_CAPABILITY_H264_DEC |
			  CEDRUS_CAPABILITY_H265_DEC |
			  CEDRUS_CAPABILITY_VP8_DEC,
	.mod_rate	= 600000000,
};

static const struct of_device_id cedrus_dt_match[] = {
	{
		.compatible = "allwinner,sun4i-a10-video-engine",
		.data = &sun4i_a10_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun5i-a13-video-engine",
		.data = &sun5i_a13_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun7i-a20-video-engine",
		.data = &sun7i_a20_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun8i-a33-video-engine",
		.data = &sun8i_a33_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun8i-h3-video-engine",
		.data = &sun8i_h3_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun8i-v3s-video-engine",
		.data = &sun8i_v3s_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun8i-r40-video-engine",
		.data = &sun8i_r40_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun50i-a64-video-engine",
		.data = &sun50i_a64_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun50i-h5-video-engine",
		.data = &sun50i_h5_cedrus_variant,
	},
	{
		.compatible = "allwinner,sun50i-h6-video-engine",
		.data = &sun50i_h6_cedrus_variant,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cedrus_dt_match);

static const struct dev_pm_ops cedrus_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(cedrus_hw_suspend,
			   cedrus_hw_resume, NULL)
};

static struct platform_driver cedrus_driver = {
	.probe		= cedrus_probe,
	.remove		= cedrus_remove,
	.driver		= {
		.name		= CEDRUS_NAME,
		.of_match_table	= of_match_ptr(cedrus_dt_match),
		.pm		= &cedrus_dev_pm_ops,
	},
};
module_platform_driver(cedrus_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Florent Revest <florent.revest@free-electrons.com>");
MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@bootlin.com>");
MODULE_DESCRIPTION("Cedrus VPU driver");
