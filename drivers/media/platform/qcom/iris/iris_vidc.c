// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>

#include "iris_vidc.h"
#include "iris_instance.h"
#include "iris_platform_common.h"

#define IRIS_DRV_NAME "iris_driver"
#define IRIS_BUS_NAME "platform:iris_icc"
#define STEP_WIDTH 1
#define STEP_HEIGHT 1

static void iris_v4l2_fh_init(struct iris_inst *inst)
{
	v4l2_fh_init(&inst->fh, inst->core->vdev_dec);
	v4l2_fh_add(&inst->fh);
}

static void iris_v4l2_fh_deinit(struct iris_inst *inst)
{
	v4l2_fh_del(&inst->fh);
	v4l2_fh_exit(&inst->fh);
}

static inline struct iris_inst *iris_get_inst(struct file *filp, void *fh)
{
	return container_of(filp->private_data, struct iris_inst, fh);
}

static void iris_m2m_device_run(void *priv)
{
}

static void iris_m2m_job_abort(void *priv)
{
	struct iris_inst *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;

	v4l2_m2m_job_finish(inst->m2m_dev, m2m_ctx);
}

static const struct v4l2_m2m_ops iris_m2m_ops = {
	.device_run = iris_m2m_device_run,
	.job_abort = iris_m2m_job_abort,
};

static int
iris_m2m_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct iris_inst *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->drv_priv = inst;
	src_vq->dev = inst->core->dev;
	src_vq->lock = &inst->ctx_q_lock;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->drv_priv = inst;
	dst_vq->dev = inst->core->dev;
	dst_vq->lock = &inst->ctx_q_lock;

	return vb2_queue_init(dst_vq);
}

int iris_open(struct file *filp)
{
	struct iris_core *core = video_drvdata(filp);
	struct iris_inst *inst;
	int ret;

	ret = pm_runtime_resume_and_get(core->dev);
	if (ret < 0)
		return ret;

	ret = iris_core_init(core);
	if (ret) {
		dev_err(core->dev, "core init failed\n");
		pm_runtime_put_sync(core->dev);
		return ret;
	}

	pm_runtime_put_sync(core->dev);

	inst = core->iris_platform_data->get_instance();
	if (!inst)
		return -ENOMEM;

	inst->core = core;

	mutex_init(&inst->ctx_q_lock);

	iris_v4l2_fh_init(inst);

	inst->m2m_dev = v4l2_m2m_init(&iris_m2m_ops);
	if (IS_ERR_OR_NULL(inst->m2m_dev)) {
		ret = -EINVAL;
		goto fail_v4l2_fh_deinit;
	}

	inst->m2m_ctx = v4l2_m2m_ctx_init(inst->m2m_dev, inst, iris_m2m_queue_init);
	if (IS_ERR_OR_NULL(inst->m2m_ctx)) {
		ret = -EINVAL;
		goto fail_m2m_release;
	}

	inst->fh.m2m_ctx = inst->m2m_ctx;
	filp->private_data = &inst->fh;

	return 0;

fail_m2m_release:
	v4l2_m2m_release(inst->m2m_dev);
fail_v4l2_fh_deinit:
	iris_v4l2_fh_deinit(inst);
	mutex_destroy(&inst->ctx_q_lock);
	kfree(inst);

	return ret;
}

int iris_close(struct file *filp)
{
	struct iris_inst *inst = iris_get_inst(filp, NULL);

	v4l2_m2m_ctx_release(inst->m2m_ctx);
	v4l2_m2m_release(inst->m2m_dev);
	iris_v4l2_fh_deinit(inst);
	mutex_destroy(&inst->ctx_q_lock);
	kfree(inst);
	filp->private_data = NULL;

	return 0;
}

static struct v4l2_file_operations iris_v4l2_file_ops = {
	.owner                          = THIS_MODULE,
	.open                           = iris_open,
	.release                        = iris_close,
	.unlocked_ioctl                 = video_ioctl2,
	.poll                           = v4l2_m2m_fop_poll,
	.mmap                           = v4l2_m2m_fop_mmap,
};

void iris_init_ops(struct iris_core *core)
{
	core->iris_v4l2_file_ops = &iris_v4l2_file_ops;
}
