// SPDX-License-Identifier: GPL-2.0
/*
 * A virtual stateless decoder device for stateless uAPI development purposes.
 *
 * This tool's objective is to help the development and testing of userspace
 * applications that use the V4L2 stateless API to decode media.
 *
 * A userspace implementation can use visl to run a decoding loop even when no
 * hardware is available or when the kernel uAPI for the codec has not been
 * upstreamed yet. This can reveal bugs at an early stage.
 *
 * This driver can also trace the contents of the V4L2 controls submitted to it.
 * It can also dump the contents of the vb2 buffers through a debugfs
 * interface. This is in many ways similar to the tracing infrastructure
 * available for other popular encode/decode APIs out there and can help develop
 * a userspace application by using another (working) one as a reference.
 *
 * Note that no actual decoding of video frames is performed by visl. The V4L2
 * test pattern generator is used to write various debug information to the
 * capture buffers instead.
 *
 * Copyright (C) 2022 Collabora, Ltd.
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * Based on the vicodec driver, that is:
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * Based on the Cedrus VPU driver, that is:
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>

#include "visl.h"
#include "visl-dec.h"
#include "visl-debugfs.h"
#include "visl-video.h"

unsigned int visl_debug;
module_param(visl_debug, uint, 0644);
MODULE_PARM_DESC(visl_debug, " activates debug info");

unsigned int visl_transtime_ms;
module_param(visl_transtime_ms, uint, 0644);
MODULE_PARM_DESC(visl_transtime_ms, " simulated process time in milliseconds.");

/*
 * dprintk can be slow through serial. This lets one limit the tracing to a
 * particular number of frames
 */
int visl_dprintk_frame_start = -1;
module_param(visl_dprintk_frame_start, int, 0444);
MODULE_PARM_DESC(visl_dprintk_frame_start,
		 " a frame number to start tracing with dprintk");

unsigned int visl_dprintk_nframes;
module_param(visl_dprintk_nframes, uint, 0444);
MODULE_PARM_DESC(visl_dprintk_nframes,
		 " the number of frames to trace with dprintk");

bool keep_bitstream_buffers;
module_param(keep_bitstream_buffers, bool, 0444);
MODULE_PARM_DESC(keep_bitstream_buffers,
		 " keep bitstream buffers in debugfs after streaming is stopped");

int bitstream_trace_frame_start = -1;
module_param(bitstream_trace_frame_start, int, 0444);
MODULE_PARM_DESC(bitstream_trace_frame_start,
		 " a frame number to start dumping the bitstream through debugfs");

unsigned int bitstream_trace_nframes;
module_param(bitstream_trace_nframes, uint, 0444);
MODULE_PARM_DESC(bitstream_trace_nframes,
		 " the number of frames to dump the bitstream through debugfs");

bool tpg_verbose;
module_param(tpg_verbose, bool, 0644);
MODULE_PARM_DESC(tpg_verbose,
		 " add more verbose information on the generated output frames");

static const struct visl_ctrl_desc visl_fwht_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_FWHT_PARAMS,
	},
};

const struct visl_ctrls visl_fwht_ctrls = {
	.ctrls = visl_fwht_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(visl_fwht_ctrl_descs)
};

static const struct visl_ctrl_desc visl_mpeg2_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_MPEG2_SEQUENCE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_MPEG2_PICTURE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_MPEG2_QUANTISATION,
	},
};

const struct visl_ctrls visl_mpeg2_ctrls = {
	.ctrls = visl_mpeg2_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(visl_mpeg2_ctrl_descs),
};

static const struct visl_ctrl_desc visl_vp8_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_VP8_FRAME,
	},
};

const struct visl_ctrls visl_vp8_ctrls = {
	.ctrls = visl_vp8_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(visl_vp8_ctrl_descs),
};

static const struct visl_ctrl_desc visl_vp9_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_VP9_FRAME,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_VP9_COMPRESSED_HDR,
	},
};

const struct visl_ctrls visl_vp9_ctrls = {
	.ctrls = visl_vp9_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(visl_vp9_ctrl_descs),
};

static const struct visl_ctrl_desc visl_h264_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_SPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_PPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_START_CODE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_SLICE_PARAMS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_PRED_WEIGHTS,
	},
};

const struct visl_ctrls visl_h264_ctrls = {
	.ctrls = visl_h264_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(visl_h264_ctrl_descs),
};

static const struct visl_ctrl_desc visl_hevc_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_PPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SLICE_PARAMS,
		/* The absolute maximum for level > 6 */
		.cfg.dims = { 600 },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SCALING_MATRIX,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_DECODE_PARAMS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_DECODE_MODE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_START_CODE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_ENTRY_POINT_OFFSETS,
		.cfg.dims = { 256 },
		.cfg.max = 0xffffffff,
		.cfg.step = 1,
	},

};

const struct visl_ctrls visl_hevc_ctrls = {
	.ctrls = visl_hevc_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(visl_hevc_ctrl_descs),
};

static const struct visl_ctrl_desc visl_av1_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_FRAME,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_TILE_GROUP_ENTRY,
		.cfg.dims = { V4L2_AV1_MAX_TILE_COUNT },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_SEQUENCE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_FILM_GRAIN,
	},
};

const struct visl_ctrls visl_av1_ctrls = {
	.ctrls = visl_av1_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(visl_av1_ctrl_descs),
};

struct v4l2_ctrl *visl_find_control(struct visl_ctx *ctx, u32 id)
{
	struct v4l2_ctrl_handler *hdl = &ctx->hdl;

	return v4l2_ctrl_find(hdl, id);
}

void *visl_find_control_data(struct visl_ctx *ctx, u32 id)
{
	struct v4l2_ctrl *ctrl;

	ctrl = visl_find_control(ctx, id);
	if (ctrl)
		return ctrl->p_cur.p;

	return NULL;
}

u32 visl_control_num_elems(struct visl_ctx *ctx, u32 id)
{
	struct v4l2_ctrl *ctrl;

	ctrl = visl_find_control(ctx, id);
	if (ctrl)
		return ctrl->elems;

	return 0;
}

static void visl_device_release(struct video_device *vdev)
{
	struct visl_dev *dev = container_of(vdev, struct visl_dev, vfd);

	v4l2_device_unregister(&dev->v4l2_dev);
	v4l2_m2m_release(dev->m2m_dev);
	media_device_cleanup(&dev->mdev);
	visl_debugfs_deinit(dev);
	kfree(dev);
}

#define VISL_CONTROLS_COUNT	ARRAY_SIZE(visl_controls)

static int visl_init_ctrls(struct visl_ctx *ctx)
{
	struct visl_dev *dev = ctx->dev;
	struct v4l2_ctrl_handler *hdl = &ctx->hdl;
	unsigned int ctrl_cnt = 0;
	unsigned int i;
	unsigned int j;
	const struct visl_ctrls *ctrls;

	for (i = 0; i < num_coded_fmts; i++)
		ctrl_cnt += visl_coded_fmts[i].ctrls->num_ctrls;

	v4l2_ctrl_handler_init(hdl, ctrl_cnt);

	for (i = 0; i < num_coded_fmts; i++) {
		ctrls = visl_coded_fmts[i].ctrls;
		for (j = 0; j < ctrls->num_ctrls; j++)
			v4l2_ctrl_new_custom(hdl, &ctrls->ctrls[j].cfg, NULL);
	}

	if (hdl->error) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to initialize control handler\n");
		v4l2_ctrl_handler_free(hdl);
		return hdl->error;
	}

	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	return 0;
}

static int visl_open(struct file *file)
{
	struct visl_dev *dev = video_drvdata(file);
	struct visl_ctx *ctx = NULL;
	int rc = 0;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto unlock;
	}

	ctx->tpg_str_buf = kzalloc(TPG_STR_BUF_SZ, GFP_KERNEL);

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dev = dev;

	rc = visl_init_ctrls(ctx);
	if (rc)
		goto free_ctx;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &visl_queue_init);

	mutex_init(&ctx->vb_mutex);

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		rc = PTR_ERR(ctx->fh.m2m_ctx);
		goto free_hdl;
	}

	rc = visl_set_default_format(ctx);
	if (rc)
		goto free_m2m_ctx;

	v4l2_fh_add(&ctx->fh);

	dprintk(dev, "Created instance: %p, m2m_ctx: %p\n",
		ctx, ctx->fh.m2m_ctx);

	mutex_unlock(&dev->dev_mutex);
	return rc;

free_m2m_ctx:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
free_hdl:
	v4l2_ctrl_handler_free(&ctx->hdl);
	v4l2_fh_exit(&ctx->fh);
free_ctx:
	kfree(ctx->tpg_str_buf);
	kfree(ctx);
unlock:
	mutex_unlock(&dev->dev_mutex);
	return rc;
}

static int visl_release(struct file *file)
{
	struct visl_dev *dev = video_drvdata(file);
	struct visl_ctx *ctx = visl_file_to_ctx(file);

	dprintk(dev, "Releasing instance %p\n", ctx);

	tpg_free(&ctx->tpg);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->hdl);
	mutex_lock(&dev->dev_mutex);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	mutex_unlock(&dev->dev_mutex);

	kfree(ctx->tpg_str_buf);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations visl_fops = {
	.owner		= THIS_MODULE,
	.open		= visl_open,
	.release	= visl_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct video_device visl_videodev = {
	.name		= VISL_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &visl_fops,
	.ioctl_ops	= &visl_ioctl_ops,
	.minor		= -1,
	.release	= visl_device_release,
	.device_caps	= V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING,
};

static const struct v4l2_m2m_ops visl_m2m_ops = {
	.device_run	= visl_device_run,
};

static const struct media_device_ops visl_m2m_media_ops = {
	.req_validate	= visl_request_validate,
	.req_queue	= v4l2_m2m_request_queue,
};

static int visl_probe(struct platform_device *pdev)
{
	struct visl_dev *dev;
	struct video_device *vfd;
	int ret;
	int rc;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto error_visl_dev;

	mutex_init(&dev->dev_mutex);

	dev->vfd = visl_videodev;
	vfd = &dev->vfd;
	vfd->lock = &dev->dev_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;

	video_set_drvdata(vfd, dev);

	platform_set_drvdata(pdev, dev);

	dev->m2m_dev = v4l2_m2m_init(&visl_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		dev->m2m_dev = NULL;
		goto error_dev;
	}

	dev->mdev.dev = &pdev->dev;
	strscpy(dev->mdev.model, "visl", sizeof(dev->mdev.model));
	strscpy(dev->mdev.bus_info, "platform:visl",
		sizeof(dev->mdev.bus_info));
	media_device_init(&dev->mdev);
	dev->mdev.ops = &visl_m2m_media_ops;
	dev->v4l2_dev.mdev = &dev->mdev;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto error_m2m;
	}

	v4l2_info(&dev->v4l2_dev,
		  "Device registered as /dev/video%d\n", vfd->num);

	ret = v4l2_m2m_register_media_controller(dev->m2m_dev, vfd,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem media controller\n");
		goto error_v4l2;
	}

	ret = media_device_register(&dev->mdev);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register mem2mem media device\n");
		goto error_m2m_mc;
	}

	rc = visl_debugfs_init(dev);
	if (rc)
		dprintk(dev, "visl_debugfs_init failed: %d\n"
			"Continuing without debugfs support\n", rc);

	return 0;

error_m2m_mc:
	v4l2_m2m_unregister_media_controller(dev->m2m_dev);
error_v4l2:
	video_unregister_device(&dev->vfd);
	/* visl_device_release called by video_unregister_device to release various objects */
	return ret;
error_m2m:
	v4l2_m2m_release(dev->m2m_dev);
error_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
error_visl_dev:
	kfree(dev);

	return ret;
}

static void visl_remove(struct platform_device *pdev)
{
	struct visl_dev *dev = platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " VISL_NAME);

#ifdef CONFIG_MEDIA_CONTROLLER
	if (media_devnode_is_registered(dev->mdev.devnode)) {
		media_device_unregister(&dev->mdev);
		v4l2_m2m_unregister_media_controller(dev->m2m_dev);
	}
#endif
	video_unregister_device(&dev->vfd);
}

static struct platform_driver visl_pdrv = {
	.probe		= visl_probe,
	.remove		= visl_remove,
	.driver		= {
		.name	= VISL_NAME,
	},
};

static void visl_dev_release(struct device *dev) {}

static struct platform_device visl_pdev = {
	.name		= VISL_NAME,
	.dev.release	= visl_dev_release,
};

static void __exit visl_exit(void)
{
	platform_driver_unregister(&visl_pdrv);
	platform_device_unregister(&visl_pdev);
}

static int __init visl_init(void)
{
	int ret;

	ret = platform_device_register(&visl_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&visl_pdrv);
	if (ret)
		platform_device_unregister(&visl_pdev);

	return ret;
}

MODULE_DESCRIPTION("Virtual stateless decoder device");
MODULE_AUTHOR("Daniel Almeida <daniel.almeida@collabora.com>");
MODULE_LICENSE("GPL");

module_init(visl_init);
module_exit(visl_exit);
