/*
 * Samsung EXYNOS4412 FIMC-ISP driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * All rights reserved.
 */
#define DEBUG

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/pm_qos.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include <mach/regs-pmu.h>

#include "fimc-mdevice.h"
#include "fimc-core.h"
#include "fimc-is-cmd.h"
#include "fimc-is-config.h"
#include "fimc-is-regs.h"
#include "fimc-is.h"

static int debug;
module_param(debug, int, 0644);

static struct pm_qos_request exynos4_isp_qos;

static const struct fimc_fmt fimc_is_formats[] = {
	{
		.name		= "RAW8 (GRBG)",
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.depth		= { 8 },
		.color		= FIMC_FMT_RAW8,
		.memplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_SGRBG8_1X8,
	}, {
		.name		= "RAW10 (GRBG)",
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.depth		= { 10 },
		.color		= FIMC_FMT_RAW10,
		.memplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_SGRBG10_1X10,
	}, {
		.name		= "RAW12 (GRBG)",
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.depth		= { 12 },
		.color		= FIMC_FMT_RAW12,
		.memplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_SGRBG12_1X12,
	},
};

static inline struct fimc_is *fimc_isp_to_is(struct fimc_isp *isp)
{
	return container_of(isp, struct fimc_is, isp);
}

/*
 * fimc_is_find_format - lookup fimc color format by fourcc or media bus code
 * @pixelformat: fourcc to match, ignored if null
 * @mbus_code: media bus code to match, ignored if null
 * @index: index to the fimc_is_formats array, ignored if negative
 */
static const struct fimc_fmt *fimc_is_find_format(const u32 *pixelformat,
					const u32 *mbus_code, int index)
{
	const struct fimc_fmt *fmt, *def_fmt = NULL;
	unsigned int i;
	int id = 0;

	if (index >= (int)ARRAY_SIZE(fimc_is_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(fimc_is_formats); ++i) {
		fmt = &fimc_is_formats[i];
		if (pixelformat && fmt->fourcc == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == id)
			def_fmt = fmt;
		id++;
	}
	return def_fmt;
}

void fimc_isp_irq_handler(struct fimc_is *is)
{
	/* TODO: Add isp dma handler */
	pr_warn("\n");
}


static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	/* TODO : start isp dma out */
	return 0;
}

static int stop_streaming(struct vb2_queue *q)
{
	/* TODO : stop isp dma out */
	return 0;
}

static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *pfmt,
		       unsigned int *num_buffers, unsigned int *num_planes,
		       unsigned int sizes[], void *allocators[])
{
	/* TODO : buffer setting */
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	/* TODO : cal buffer size */
	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	/* TODO : queue buffer */
}

static void fimc_lock(struct vb2_queue *vq)
{
	struct fimc_is *fimc = vb2_get_drv_priv(vq);
	mutex_lock(&fimc->lock);
}

static void fimc_unlock(struct vb2_queue *vq)
{
	struct fimc_is *fimc = vb2_get_drv_priv(vq);
	mutex_unlock(&fimc->lock);
}

static const struct vb2_ops fimc_is_qops = {
	.queue_setup = queue_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.wait_prepare = fimc_unlock,
	.wait_finish = fimc_lock,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
};

static int fimc_is_open(struct file *file)
{
	struct fimc_is *fimc = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&fimc->lock))
		return -ERESTARTSYS;

	set_bit(ST_FIMC_IS_IN_USE, &fimc->state);
	ret = pm_runtime_get_sync(&fimc->pdev->dev);
	if (ret < 0)
		goto done;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto done;

done:
	mutex_unlock(&fimc->lock);
	return ret;
}

static int fimc_is_close(struct file *file)
{
	struct fimc_is *fimc = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&fimc->lock))
		return -ERESTARTSYS;

	pm_runtime_put(&fimc->pdev->dev);

	mutex_unlock(&fimc->lock);
	return ret;
}

static unsigned int fimc_is_poll(struct file *file,
				   struct poll_table_struct *wait)
{
	/* TODO : use vb poll*/
	return 0;
}

static int fimc_is_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* TODO : use vb map*/
	return 0;
}

static const struct v4l2_file_operations fimc_is_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_open,
	.release	= fimc_is_close,
	.poll		= fimc_is_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_mmap,
};

/*
 * Format and crop negotiation helpers
 */
static const struct fimc_fmt *fimc_is_try_format(struct fimc_is *fimc,
					u32 *width, u32 *height,
					u32 *code, u32 *fourcc, int pad)
{
	const struct fimc_fmt *fmt;

	fmt = fimc_is_find_format(fourcc, code, 0);
	if (WARN_ON(!fmt))
		return NULL;

	return fmt;
}

static void fimc_is_try_crop(struct fimc_is *fimc, struct v4l2_rect *r)
{
	/* TODO : crop size validation */
}

static void fimc_is_try_compose(struct fimc_is *fimc, struct v4l2_rect *r)
{
	/* TODO : use isp bayer crop */
}

/*
 * Video node ioctl operations
 */
static int fimc_vidioc_querycap_capture(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	strlcpy(cap->driver, FIMC_IS_DRV_NAME, sizeof(cap->driver));
	cap->bus_info[0] = 0;
	cap->card[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING;
	return 0;
}

static int fimc_is_enum_fmt_mplane(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	const struct fimc_fmt *fmt;

	if (f->index >= ARRAY_SIZE(fimc_is_formats))
		return -EINVAL;

	fmt = &fimc_is_formats[f->index];
	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int fimc_is_g_fmt_mplane(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	/* TODO : return supported format */
	return 0;
}

static int fimc_is_try_fmt(struct fimc_is *fimc,
			     struct v4l2_pix_format_mplane *pixm,
			     const struct fimc_fmt **ffmt)
{
	/* TODO : format validation */
	return 0;
}

static int fimc_is_try_fmt_mplane(struct file *file, void *fh,
				    struct v4l2_format *f)
{
	struct fimc_is *fimc = video_drvdata(file);

	return fimc_is_try_fmt(fimc, &f->fmt.pix_mp, NULL);
}

static int fimc_is_s_fmt_mplane(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	/* TODO : set a given format */
	return 0;
}

static int fimc_pipeline_validate(struct fimc_is *fimc)
{
	/* TODO :  check the size matching beween pads */
	return 0;
}

static int fimc_is_streamon(struct file *file, void *priv,
			      enum v4l2_buf_type type)
{
	/* TODO : start streaming */
	return 0;
}

static int fimc_is_streamoff(struct file *file, void *priv,
			       enum v4l2_buf_type type)
{
	/* TODO : end streaming */
	return 0;
}

static int fimc_is_reqbufs(struct file *file, void *priv,
			     struct v4l2_requestbuffers *reqbufs)
{
	/* TODO : vb2_reqbufs */
	return 0;
}

static int fimc_is_querybuf(struct file *file, void *priv,
			      struct v4l2_buffer *buf)
{
	/* TODO : vb2_querybuf */
	return 0;
}

static int fimc_is_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	/* TODO : vb2_qbuf */
	return 0;
}

static int fimc_is_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	/* TODO : vb2_dqbuf*/
	return 0;
}

static int fimc_is_create_bufs(struct file *file, void *priv,
				 struct v4l2_create_buffers *create)
{
	/* TODO : buffer setting */
	return 0;
}

static int fimc_is_prepare_buf(struct file *file, void *priv,
				 struct v4l2_buffer *b)
{
	/* TODO : cal buffer size */
	return 0;
}

/* Return 1 if rectangle a is enclosed in rectangle b, or 0 otherwise. */
static int enclosed_rectangle(struct v4l2_rect *a, struct v4l2_rect *b)
{
	if (a->left < b->left || a->top < b->top)
		return 0;
	if (a->left + a->width > b->left + b->width)
		return 0;
	if (a->top + a->height > b->top + b->height)
		return 0;

	return 1;
}

static const struct v4l2_ioctl_ops fimc_is_ioctl_ops = {
	.vidioc_querycap		= fimc_vidioc_querycap_capture,
	.vidioc_enum_fmt_vid_cap_mplane	= fimc_is_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_is_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_is_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_is_g_fmt_mplane,
	.vidioc_reqbufs			= fimc_is_reqbufs,
	.vidioc_querybuf		= fimc_is_querybuf,
	.vidioc_prepare_buf		= fimc_is_prepare_buf,
	.vidioc_create_bufs		= fimc_is_create_bufs,
	.vidioc_qbuf			= fimc_is_qbuf,
	.vidioc_dqbuf			= fimc_is_dqbuf,
	.vidioc_streamon		= fimc_is_streamon,
	.vidioc_streamoff		= fimc_is_streamoff,
};

/* Capture subdev media entity operations */
static int fimc_is_link_setup(struct media_entity *entity,
				const struct media_pad *local,
				const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations fimc_is_subdev_media_ops = {
	.link_setup = fimc_is_link_setup,
};

static int fimc_is_subdev_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_mbus_code_enum *code)
{
	const struct fimc_fmt *fmt;

	fmt = fimc_is_find_format(NULL, NULL, code->index);
	if (!fmt)
		return -EINVAL;
	code->code = fmt->mbus_code;
	return 0;
}

static int fimc_isp_subdev_get_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_format *fmt)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct v4l2_mbus_framefmt cur_fmt;

	__is_get_size(is, &cur_fmt);
	if (fmt->pad == FIMC_IS_SD_PAD_SINK) {
		/* full camera input frame size */
		mf->width = cur_fmt.width + 16;
		mf->height = cur_fmt.height + 12;
		mf->code = V4L2_MBUS_FMT_SGRBG10_1X10;
	} else {
		/* crop size */
		mf->width = cur_fmt.width;
		mf->height = cur_fmt.height;
		mf->code = V4L2_MBUS_FMT_YUV8_1X24;
	}

	return 0;
}

static int fimc_isp_subdev_set_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_format *fmt)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	v4l2_info(sd, "pad%d: code: 0x%x, %dx%d", fmt->pad,
					mf->code, mf->width, mf->height);

	__is_set_size(is, mf);

	return 0;
}

static int fimc_isp_subdev_s_stream(struct v4l2_subdev *sd, int on)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	int ret;

	v4l2_info(sd, "on: %d\n", on);

	if (!test_bit(IS_ST_INIT_DONE, &is->state))
		return -EBUSY;

#ifdef VIDEOBUF2_DMA_CONTIG
	fimc_is_mem_cache_clean(is->is_p_region, IS_PARAM_SIZE);
#else
	fimc_is_region_flush(is);
#endif

	if (on) {
		if (atomic_read(&is->cfg_param[is->scenario_id].p_region_num))
			ret = fimc_is_itf_s_param(is, true);
		pr_info("Changing mode to %d\n", is->scenario_id);
		ret = fimc_is_itf_mode_change(is);
		if (ret)
			return -EINVAL;

		clear_bit(IS_ST_STREAM_ON, &is->state);
		fimc_is_hw_set_stream(is, on);
		ret = wait_event_timeout(is->irq_queue,
					  test_bit(IS_ST_STREAM_ON, &is->state),
					  FIMC_IS_SHUTDOWN_TIMEOUT);
		if (ret == 0) {
			pr_err("Stream on timeout\n");
			return -EINVAL;
		}
	} else {
		clear_bit(IS_ST_STREAM_OFF, &is->state);
		fimc_is_hw_set_stream(is, on);
		ret = wait_event_timeout(is->irq_queue,
					 test_bit(IS_ST_STREAM_OFF, &is->state),
					 FIMC_IS_CONFIG_TIMEOUT);
		if (ret == 0) {
			pr_err("Stream off timeout\n");
			return -EINVAL;
		}
		is->setfile.sub_index = 0;
	}

	return 0;
}

static int fimc_is_subdev_s_power(struct v4l2_subdev *sd, int on)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct device *dev = &is->pdev->dev;
	int ret = 0;

	v4l2_info(sd, "%s:%d: on: %d\n", __func__, __LINE__, on);

	if (on) {
		/* bus lock */
		pm_qos_add_request(&exynos4_isp_qos,
						PM_QOS_BUS_THROUGHPUT,
						400200);
#if defined(CONFIG_PM_RUNTIME)
		ret = pm_runtime_get_sync(dev);
#else
		ret = fimc_is_resume(dev);
#endif
		if (ret < 0) {
			v4l2_err(sd, "failed to enable pm get sync\n");
			return -EINVAL;
		}
		set_bit(IS_ST_PWR_ON, &is->state);
		/* 1. Load FW */
		ret = fimc_is_request_firmware(is, FIMC_IS_FW_FILENAME);
		if (ret < 0) {
			v4l2_err(sd, "failed to request_firmware\n");
			return -EINVAL;
		}
		set_bit(IS_ST_FW_LOADED, &is->state);

		/* 2. A5 power on */
		fimc_is_cpu_set_power(is, 1);
		ret = wait_event_timeout(is->irq_queue,
				 test_bit(IS_ST_A5_PWR_ON, &is->state),
				 FIMC_IS_FW_LOAD_TIMEOUT);
		if (!ret) {
			v4l2_err(sd, "FIMC-IS CPU power on timeout\n");
			return -EINVAL;
		}
		set_bit(IS_ST_PWR_SUBIP_ON, &is->state);
		v4l2_info(sd, "FIMC-IS FW info = %s\n", is->fw.info);
		v4l2_info(sd, "FIMC-IS FW ver = %s\n", is->fw.version);

		/* 3. Init set */
		ret = fimc_is_hw_initialize(is);
		if (ret) {
			v4l2_err(sd, "FIMC-IS Init sequence error\n");
			return -EINVAL;
		}
	} else {
		/* 1. Close sensor */
		if (!test_bit(IS_ST_PWR_ON, &is->state)) {
			v4l2_err(sd, "FIMC-IS was already power off state!!\n");
			fimc_is_hw_close_sensor(is, 0);
			ret = wait_event_timeout(is->irq_queue,
				 !test_bit(IS_ST_OPEN_SENSOR, &is->state),
				 FIMC_IS_CONFIG_TIMEOUT);
			if (!ret) {
				v4l2_err(sd, "FIMC-IS Close sensor timeout\n");
				return -EINVAL;
			}
		}

		/* 1. SUB IP power off */
		if (test_bit(IS_ST_PWR_SUBIP_ON, &is->state)) {
			fimc_is_hw_subip_power_off(is);
			ret = wait_event_timeout(is->irq_queue,
				!test_bit(IS_ST_PWR_SUBIP_ON, &is->state),
				FIMC_IS_CONFIG_TIMEOUT);
			if (!ret) {
				v4l2_err(sd, "FIMC-IS Close sensor timeout\n");
				return -EINVAL;
			}
		}

		/* 2. A5 power off */
		fimc_is_cpu_set_power(is, 0);
#if defined(CONFIG_PM_RUNTIME)
		ret = pm_runtime_put_sync(dev);
#else
		ret = fimc_is_suspend(dev);
#endif
		clear_bit(IS_ST_PWR_ON, &is->state);
		/* bus release */
		pm_qos_remove_request(&exynos4_isp_qos);
		is->state = 0;
		is->cfg_param[is->scenario_id].p_region_index1 = 0;
		is->cfg_param[is->scenario_id].p_region_index2 = 0;
		set_bit(IS_ST_IDLE, &is->state);
	}
	return ret;
}

static int fimc_is_log_status(struct v4l2_subdev *sd)
{
	/* TODO : display log */
	return 0;
}

static int fimc_isp_subdev_open(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	int ret = 0;

	v4l2_info(sd, "%s:%d\n", __func__, __LINE__);

	if (!sd->ctrl_handler) {
		ret = fimc_is_create_controls(isp);
		if (ret)
			return -EINVAL;
		sd->ctrl_handler = &isp->ctrl_handler;
	}
	return ret;
}

static int fimc_isp_subdev_close(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	int ret = 0;

	v4l2_info(sd, "%s:%d\n", __func__, __LINE__);

	if (sd->ctrl_handler) {
		ret = fimc_is_delete_controls(isp);
		if (ret)
			return -EINVAL;
		sd->ctrl_handler = NULL;
	}
	return ret;
}

static int fimc_isp_subdev_registered(struct v4l2_subdev *sd)
{
	return 0;
}

static void fimc_isp_subdev_unregistered(struct v4l2_subdev *sd)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);

	if (isp == NULL)
		return;
}

static const struct v4l2_subdev_internal_ops fimc_is_subdev_internal_ops = {
	.registered = fimc_isp_subdev_registered,
	.unregistered = fimc_isp_subdev_unregistered,
	.open = fimc_isp_subdev_open,
	.close = fimc_isp_subdev_close,
};

static const struct v4l2_subdev_pad_ops fimc_is_subdev_pad_ops = {
	.enum_mbus_code = fimc_is_subdev_enum_mbus_code,
	.get_fmt = fimc_isp_subdev_get_fmt,
	.set_fmt = fimc_isp_subdev_set_fmt,
};

static const struct v4l2_subdev_video_ops fimc_is_subdev_video_ops = {
	.s_stream = fimc_isp_subdev_s_stream,
};

static const struct v4l2_subdev_core_ops fimc_is_core_ops = {
	.s_power = fimc_is_subdev_s_power,
	.log_status = fimc_is_log_status,
};

static struct v4l2_subdev_ops fimc_is_subdev_ops = {
	.core = &fimc_is_core_ops,
	.video = &fimc_is_subdev_video_ops,
	.pad = &fimc_is_subdev_pad_ops,
};

static int fimc_is_ctrl_set_mode(struct fimc_is *is, int value)
{
	is->scenario_id = value;
	return 0;
}

static int fimc_is_ctrl_effect_mode(struct fimc_is *is, int value)
{
	__is_set_isp_effect(is, value);
	return 0;
}

static int fimc_is_ctrl_awb_mode(struct fimc_is *is, int value)
{
	switch (value) {
	case IS_AWB_AUTO:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_AUTO, 0);
		break;
	case IS_AWB_DAYLIGHT:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
						ISP_AWB_ILLUMINATION_DAYLIGHT);
		break;
	case IS_AWB_CLOUDY:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
						ISP_AWB_ILLUMINATION_CLOUDY);
		break;
	case IS_AWB_TUNGSTEN:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
						ISP_AWB_ILLUMINATION_TUNGSTEN);
		break;
	case IS_AWB_FLUORESCENT:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
					ISP_AWB_ILLUMINATION_FLUORESCENT);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fimc_is_ctrl_frame_rate(struct fimc_is *is, int value)
{
	int ret = 0;

	/*
	 * If the status of IS chain is streaming status,
	 * ISP must be stopped before updating frame duration parameters
	 */
	if (test_bit(IS_ST_STREAM_ON, &is->state)) {
		IS_ISP_SET_PARAM_CONTROL_CMD(is, CONTROL_COMMAND_STOP);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_CONTROL);
		IS_UPDATE_PARAM_NUM(is);
		ret = fimc_is_itf_s_param(is, false);
	}

	__is_set_sensor(is, value);
	ret = fimc_is_itf_s_param(is, true);

	if (test_bit(IS_ST_STREAM_ON, &is->state)) {
		IS_ISP_SET_PARAM_CONTROL_CMD(is, CONTROL_COMMAND_START);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_CONTROL);
		IS_UPDATE_PARAM_NUM(is);
		ret = fimc_is_itf_s_param(is, false);
	}
	return ret;
}

static int fimc_is_ctrl_set_position_x(struct fimc_is *is, int value)
{
	u32 max;

	max = is->cfg_param[is->scenario_id].isp.otf_input.width;
	if ((value < 0) || (value > max))
		return -EINVAL;
	is->af.pos_x = value;
	return 0;
}

static int fimc_is_ctrl_set_position_y(struct fimc_is *is, int value)
{
	u32 max;

	max = is->cfg_param[is->scenario_id].isp.otf_input.height;
	if ((value < 0) || (value > max))
		return -EINVAL;
	is->af.pos_y = value;
	return 0;
}

static int fimc_is_ctrl_af_mode(struct fimc_is *is, int value)
{
	int ret;

	if (!is->af.use_af) {
		pr_info("%s can't support af functionality\n",
				is->sensor[is->sensor_index].subdev.name);
		return 0;
	}

	__is_set_isp_aa_af_mode(is, value);

	switch (value) {
	case IS_FOCUS_MODE_AUTO:
	case IS_FOCUS_MODE_MACRO:
		/* Auto and macro mode have to be run by start command */
		break;
	case IS_FOCUS_MODE_INFINITY:
	case IS_FOCUS_MODE_CONTINUOUS:
	case IS_FOCUS_MODE_TOUCH:
	case IS_FOCUS_MODE_FACEDETECT:
		ret = fimc_is_itf_s_param(is, true);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int fimc_is_ctrl_af_start_stop(struct fimc_is *is, int value)
{
	unsigned long *p_index;

	if (!is->af.use_af) {
		pr_info("%s can't support af functionality\n",
				is->sensor[is->sensor_index].subdev.name);
		return 0;
	}

	if ((is->af.mode != IS_FOCUS_MODE_AUTO) &&
				(is->af.mode != IS_FOCUS_MODE_MACRO))
		return 0;

	p_index = &is->cfg_param[is->scenario_id].p_region_index1;
	switch (value) {
	case IS_FOCUS_START:
		__is_set_isp_aa_af_start_stop(is, 1);
		break;
	case IS_FOCUS_STOP:
		__is_set_isp_aa_af_start_stop(is, 0);
		break;
	default:
		return -EINVAL;
	}

	return fimc_is_itf_s_param(is, true);
}

static int fimc_is_ctrl_set_metering_position_x(struct fimc_is *is, int value)
{
	u32 max;

	max = is->cfg_param[is->scenario_id].isp.otf_input.width;
	if ((value < 0) || (value > max))
		return -EINVAL;
	__is_set_isp_metering(is, IS_METERING_CONFIG_WIN_POS_X, value);
	return 0;
}

static int fimc_is_ctrl_set_metering_position_y(struct fimc_is *is, int value)
{
	u32 max;

	max = is->cfg_param[is->scenario_id].isp.otf_input.height;
	if ((value < 0) || (value > max))
		return -EINVAL;
	__is_set_isp_metering(is, IS_METERING_CONFIG_WIN_POS_Y, value);
	return 0;
}

static int fimc_is_ctrl_set_metering_window_x(struct fimc_is *is, int value)
{
	u32 max, offset;

	max = is->cfg_param[is->scenario_id].isp.otf_input.width;
	offset = is->cfg_param[is->scenario_id].isp.metering.win_pos_x;
	if ((value < 0) || ((value + offset) > max))
		return -EINVAL;
	__is_set_isp_metering(is, IS_METERING_CONFIG_WIN_WIDTH, value);
	return 0;
}

static int fimc_is_ctrl_set_metering_window_y(struct fimc_is *is, int value)
{
	u32 max, offset;

	max = is->cfg_param[is->scenario_id].isp.otf_input.height;
	offset = is->cfg_param[is->scenario_id].isp.metering.win_pos_y;
	if ((value < 0) || ((value + offset) > max))
		return -EINVAL;
	__is_set_isp_metering(is, IS_METERING_CONFIG_WIN_HEIGHT, value);
	return 0;
}

static int fimc_is_ctrl_ae_awb_lock_unlock(struct fimc_is *is, int value)
{
	int ret;

	switch (value) {
	case IS_AE_UNLOCK_AWB_UNLOCK:
		IS_ISP_SET_PARAM_AA_CMD(is, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(is, ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
		is->af.ae_lock_state = 0;
		is->af.awb_lock_state = 0;
		ret = fimc_is_itf_s_param(is, false);
		break;
	case IS_AE_LOCK_AWB_UNLOCK:
		IS_ISP_SET_PARAM_AA_CMD(is, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(is, ISP_AA_TARGET_AE);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
		is->af.ae_lock_state = 1;
		ret = fimc_is_itf_s_param(is, false);

		IS_ISP_SET_PARAM_AA_CMD(is, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(is, ISP_AA_TARGET_AWB);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
		is->af.awb_lock_state = 0;
		ret = fimc_is_itf_s_param(is, false);
		break;
	case IS_AE_UNLOCK_AWB_LOCK:
		IS_ISP_SET_PARAM_AA_CMD(is, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(is, ISP_AA_TARGET_AE);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
		is->af.ae_lock_state = 0;
		ret = fimc_is_itf_s_param(is, false);

		IS_ISP_SET_PARAM_AA_CMD(is, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(is, ISP_AA_TARGET_AWB);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
		is->af.awb_lock_state = 1;
		ret = fimc_is_itf_s_param(is, false);
		break;
	case IS_AE_LOCK_AWB_LOCK:
		IS_ISP_SET_PARAM_AA_CMD(is, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(is, ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
		is->af.ae_lock_state = 1;
		is->af.awb_lock_state = 1;
		ret = fimc_is_itf_s_param(is, false);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int fimc_is_ctrl_iso(struct fimc_is *is, int value)
{
	switch (value) {
	case IS_ISO_AUTO:
		__is_set_isp_iso(is, ISP_ISO_COMMAND_AUTO, 0);
		break;
	case IS_ISO_50:
		__is_set_isp_iso(is, ISP_ISO_COMMAND_MANUAL, 50);
		break;
	case IS_ISO_100:
		__is_set_isp_iso(is, ISP_ISO_COMMAND_MANUAL, 100);
		break;
	case IS_ISO_200:
		__is_set_isp_iso(is, ISP_ISO_COMMAND_MANUAL, 200);
		break;
	case IS_ISO_400:
		__is_set_isp_iso(is, ISP_ISO_COMMAND_MANUAL, 400);
		break;
	case IS_ISO_800:
		__is_set_isp_iso(is, ISP_ISO_COMMAND_MANUAL, 800);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fimc_is_ctrl_contrast(struct fimc_is *is, int value)
{
	__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_CONTRAST,
					value);
	return 0;
}

static int fimc_is_ctrl_saturation(struct fimc_is *is, int value)
{
	__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_SATURATION,
					value);

	return 0;
}

static int fimc_is_ctrl_sharpness(struct fimc_is *is, int value)
{
	__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_SHARPNESS,
					value);

	return 0;
}

static int fimc_is_ctrl_exposure(struct fimc_is *is, int value)
{
	__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_EXPOSURE,
					value);

	return 0;
}

static int fimc_is_ctrl_brightness(struct fimc_is *is, int value)
{
	__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS,
					value);

	return 0;
}

static int fimc_is_ctrl_hue(struct fimc_is *is, int value)
{
	__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_HUE,
					value);

	return 0;
}

static int fimc_is_ctrl_metering(struct fimc_is *is, int value)
{
	switch (value) {
	case IS_METERING_AVERAGE:
		__is_set_isp_metering(is,
			IS_METERING_CONFIG_CMD, ISP_METERING_COMMAND_AVERAGE);
		break;
	case IS_METERING_SPOT:
		__is_set_isp_metering(is,
			IS_METERING_CONFIG_CMD, ISP_METERING_COMMAND_SPOT);
		break;
	case IS_METERING_MATRIX:
		__is_set_isp_metering(is,
			IS_METERING_CONFIG_CMD, ISP_METERING_COMMAND_MATRIX);
		break;
	case IS_METERING_CENTER:
		__is_set_isp_metering(is,
			IS_METERING_CONFIG_CMD, ISP_METERING_COMMAND_CENTER);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fimc_is_ctrl_afc(struct fimc_is *is, int value)
{
	switch (value) {
	case IS_AFC_DISABLE:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_DISABLE, 0);
		break;
	case IS_AFC_AUTO:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_AUTO, 0);
		break;
	case IS_AFC_MANUAL_50HZ:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_MANUAL, 50);
		break;
	case IS_AFC_MANUAL_60HZ:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_MANUAL, 60);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fimc_is_ctrl_flash_mode(struct fimc_is *is, int value)
{
	switch (value) {
	case IS_FLASH_MODE_OFF:
		__is_set_isp_flash(is, ISP_FLASH_COMMAND_DISABLE, 0);
		break;
	case IS_FLASH_MODE_AUTO:
		__is_set_isp_flash(is, ISP_FLASH_COMMAND_AUTO, 0);
		break;
	case IS_FLASH_MODE_AUTO_REDEYE:
		__is_set_isp_flash(is, ISP_FLASH_COMMAND_AUTO, 1);
		break;
	case IS_FLASH_MODE_ON:
		__is_set_isp_flash(is, ISP_FLASH_COMMAND_MANUALON, 0);
		break;
	case IS_FLASH_MODE_TORCH:
		__is_set_isp_flash(is, ISP_FLASH_COMMAND_TORCH, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void fimc_is_ctrl_adjust_all(struct fimc_is *is, int contrast,
							int saturation,
							int sharpness,
							int exposure,
							int brightness,
							int hue)
{
	__is_set_isp_adjust(is,
			ISP_ADJUST_COMMAND_MANUAL_CONTRAST, contrast);
	__is_set_isp_adjust(is,
			ISP_ADJUST_COMMAND_MANUAL_SATURATION, saturation);
	__is_set_isp_adjust(is,
			ISP_ADJUST_COMMAND_MANUAL_SHARPNESS, sharpness);
	__is_set_isp_adjust(is,
			ISP_ADJUST_COMMAND_MANUAL_EXPOSURE, exposure);
	__is_set_isp_adjust(is,
			ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS, brightness);
	__is_set_isp_adjust(is,
			ISP_ADJUST_COMMAND_MANUAL_HUE, hue);
}

static int fimc_is_ctrl_scene_mode(struct fimc_is *is, int value)
{
	u32 frametime_max;

	switch (value) {
	case IS_SCENE_MODE_NONE:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_AUTO);
		break;
	case IS_SCENE_MODE_PORTRAIT:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, -1, -1, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_AUTO);
		break;
	case IS_SCENE_MODE_NIGHTSHOT:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_BACK_LIGHT:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_ON);
		break;
	case IS_SCENE_MODE_LANDSCAPE:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_MATRIX);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 1, 1, 1, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_SPORTS:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_PARTY_INDOOR:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_200);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 1, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_AUTO);
		break;
	case IS_SCENE_MODE_BEACH_SNOW:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 1, 0, 0, 1, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_SUNSET:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_DAYLIGHT);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_DUSK_DAWN:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_FLUORESCENT);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_FALL_COLOR:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 2, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_FIREWORKS:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	case IS_SCENE_MODE_TEXT:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_AUTO);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 2, 2, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_AUTO);
		break;
	case IS_SCENE_MODE_CANDLE_LIGHT:
		/* ISO */
		fimc_is_ctrl_iso(is, IS_ISO_AUTO);
		/* Metering */
		fimc_is_ctrl_metering(is, IS_METERING_CENTER);
		/* AWB */
		fimc_is_ctrl_awb_mode(is, IS_AWB_DAYLIGHT);
		/* Adjust */
		fimc_is_ctrl_adjust_all(is, 0, 0, 0, 0, 0, 0);
		/* Flash */
		fimc_is_ctrl_flash_mode(is, IS_FLASH_MODE_OFF);
		break;
	default:
		return -EINVAL;
	}

	/* fps control */
	frametime_max = is->is_p_region->parameter.isp.otf_input.frametime_max;
	if (value == IS_SCENE_MODE_SPORTS)
		fimc_is_ctrl_frame_rate(is, 30);
	else if ((is->scenario_id == 0) && (frametime_max == 33333))
		fimc_is_ctrl_frame_rate(is, 0);
	return 0;
}

static int fimc_is_ctrl_vt_mode(struct fimc_is *is, int value)
{
	is->setfile.sub_index = value;
	pr_info("%s: vt_mode = %d\n", __func__, value);
	return 0;
}

static int fimc_is_ctrl_drc(struct fimc_is *is, int value)
{
	switch (value) {
	case IS_DRC_BYPASS_DISABLE:
		__is_set_drc_control(is, CONTROL_BYPASS_DISABLE);
		break;
	case IS_DRC_BYPASS_ENABLE:
		__is_set_drc_control(is, CONTROL_BYPASS_ENABLE);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fimc_is_ctrl_fd(struct fimc_is *is, int value)
{
	switch (value) {
	case IS_FD_COMMAND_STOP:
		__is_set_fd_control(is, CONTROL_COMMAND_STOP);
		break;
	case IS_FD_COMMAND_START:
		__is_set_fd_control(is, CONTROL_COMMAND_START);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fimc_is_ctrl_fd_set_max_face(struct fimc_is *is, int value)
{
	__is_set_fd_config_maxface(is, value);

	return 0;
}

static int fimc_is_ctrl_fd_set_roll_angle(struct fimc_is *is, int value)
{
	__is_set_fd_config_rollangle(is, value);

	return 0;
}

static int fimc_is_ctrl_fd_set_yaw_angle(struct fimc_is *is, int value)
{
	__is_set_fd_config_yawangle(is, value);

	return 0;
}

static int fimc_is_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fimc_isp *isp = container_of(ctrl->handler, struct fimc_isp,
					      ctrl_handler);
	struct fimc_is *is = fimc_isp_to_is(isp);
	int ret = 0;
	bool s_parm_flg = true;

	switch (ctrl->id) {
	case V4L2_CID_IS_S_FORMAT_SCENARIO:
		ret = fimc_is_ctrl_set_mode(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_FRAME_RATE:
		ret = fimc_is_ctrl_frame_rate(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_CAMERA_OBJECT_POSITION_X:
		ret = fimc_is_ctrl_set_position_x(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_CAMERA_OBJECT_POSITION_Y:
		ret = fimc_is_ctrl_set_position_y(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_CAMERA_METERING_POSITION_X:
		ret = fimc_is_ctrl_set_metering_position_x(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_CAMERA_METERING_POSITION_Y:
		ret = fimc_is_ctrl_set_metering_position_y(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_CAMERA_METERING_WINDOW_X:
		ret = fimc_is_ctrl_set_metering_window_x(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_CAMERA_METERING_WINDOW_Y:
		ret = fimc_is_ctrl_set_metering_window_y(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_AE_AWB_LOCK_UNLOCK:
		ret = fimc_is_ctrl_ae_awb_lock_unlock(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_CAMERA_IMAGE_EFFECT:
		ret = fimc_is_ctrl_effect_mode(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_AWB_MODE:
		ret = fimc_is_ctrl_awb_mode(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_ISO:
		ret = fimc_is_ctrl_iso(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_CONTRAST:
		ret = fimc_is_ctrl_contrast(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_SATURATION:
		ret = fimc_is_ctrl_saturation(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_SHARPNESS:
		ret = fimc_is_ctrl_sharpness(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_EXPOSURE:
		ret = fimc_is_ctrl_exposure(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_BRIGHTNESS:
		ret = fimc_is_ctrl_brightness(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_HUE:
		ret = fimc_is_ctrl_hue(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_METERING:
		ret = fimc_is_ctrl_metering(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_AFC_MODE:
		ret = fimc_is_ctrl_afc(is, ctrl->val);
		break;
	case V4L2_CID_IS_SCENE_MODE:
		ret = fimc_is_ctrl_scene_mode(is, ctrl->val);
		break;
	case V4L2_CID_IS_VT_MODE:
		ret = fimc_is_ctrl_vt_mode(is, ctrl->val);
		s_parm_flg = false;
		break;
	case V4L2_CID_IS_SET_DRC:
		ret = fimc_is_ctrl_drc(is, ctrl->val);
		break;
	case V4L2_CID_IS_CMD_FD:
		ret = fimc_is_ctrl_fd(is, ctrl->val);
		break;
	case V4L2_CID_IS_FD_SET_MAX_FACE_NUMBER:
		ret = fimc_is_ctrl_fd_set_max_face(is, ctrl->val);
		break;
	case V4L2_CID_IS_FD_SET_ROLL_ANGLE:
		ret = fimc_is_ctrl_fd_set_roll_angle(is, ctrl->val);
		break;
	case V4L2_CID_IS_FD_SET_YAW_ANGLE:
		ret = fimc_is_ctrl_fd_set_yaw_angle(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_FOCUS_MODE:
		ret = fimc_is_ctrl_af_mode(is, ctrl->val);
		break;
	case V4L2_CID_IS_CAMERA_FOCUS_START_STOP:
		ret = fimc_is_ctrl_af_start_stop(is, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret) {
		pr_err("%s error : id = 0x%#x, val = %d\n", __func__,
							ctrl->id, ctrl->val);
		return -EINVAL;
	}

	if (s_parm_flg && test_bit(IS_ST_STREAM_ON, &is->state))
		ret = fimc_is_itf_s_param(is, true);

	return ret;
}

static int fimc_is_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fimc_isp *isp = container_of(ctrl->handler, struct fimc_isp,
					      ctrl_handler);
	struct fimc_is *is = fimc_isp_to_is(isp);
	u32 curr_index;

	fimc_is_region_invalid(is);
	curr_index = is->fd_header.curr_index;
	switch (ctrl->id) {
	/* Face Detection CID handler */
	/* 1. Overall information */
	case V4L2_CID_IS_FD_GET_FACE_COUNT:
		ctrl->val = is->fd_header.count;
		curr_index = is->fd_header.index;
		is->fd_header.curr_index = is->fd_header.index;
		is->fd_header.count = 0;
		is->fd_header.index = 0;
		is->fd_header.offset = 0;
		break;
	case V4L2_CID_IS_FD_GET_FACE_FRAME_NUMBER:
		if (is->fd_header.offset < is->fd_header.count)
			ctrl->val =
				is->is_p_region->face[curr_index].frame_number;
		else
			ctrl->val = 0;
		break;
	case V4L2_CID_IS_FD_GET_FACE_CONFIDENCE:
		ctrl->val = is->is_p_region->face[curr_index].confidence;
		break;
	case V4L2_CID_IS_FD_GET_FACE_SMILE_LEVEL:
		ctrl->val = is->is_p_region->face[curr_index].smile_level;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BLINK_LEVEL:
		ctrl->val = is->is_p_region->face[curr_index].blink_level;
		break;
	/* 2. Face information */
	case V4L2_CID_IS_FD_GET_FACE_TOPLEFT_X:
		ctrl->val = is->is_p_region->face[curr_index].face.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_FACE_TOPLEFT_Y:
		ctrl->val = is->is_p_region->face[curr_index].face.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_X:
		ctrl->val = is->is_p_region->face[curr_index].face.offset_x
				+ is->is_p_region->face[curr_index].face.width;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_Y:
		ctrl->val = is->is_p_region->face[curr_index].face.offset_y
				+ is->is_p_region->face[curr_index].face.height;
		break;
	/* 3. Left eye information */
	case V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_X:
		ctrl->val = is->is_p_region->face[curr_index].left_eye.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_Y:
		ctrl->val = is->is_p_region->face[curr_index].left_eye.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_X:
		ctrl->val = is->is_p_region->face[curr_index].left_eye.offset_x
			+ is->is_p_region->face[curr_index].left_eye.width;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_Y:
		ctrl->val = is->is_p_region->face[curr_index].left_eye.offset_y
			+ is->is_p_region->face[curr_index].left_eye.height;
		break;
	/* 4. Right eye information */
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_X:
		ctrl->val =
			is->is_p_region->face[curr_index].right_eye.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_Y:
		ctrl->val =
			is->is_p_region->face[curr_index].right_eye.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_X:
		ctrl->val = is->is_p_region->face[curr_index].right_eye.offset_x
			+ is->is_p_region->face[curr_index].right_eye.width;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_Y:
		ctrl->val = is->is_p_region->face[curr_index].right_eye.offset_y
			+ is->is_p_region->face[curr_index].right_eye.height;
		break;
	/* 5. Mouth eye information */
	case V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_X:
		ctrl->val = is->is_p_region->face[curr_index].mouth.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_Y:
		ctrl->val = is->is_p_region->face[curr_index].mouth.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_X:
		ctrl->val = is->is_p_region->face[curr_index].mouth.offset_x
			+ is->is_p_region->face[curr_index].mouth.width;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_Y:
		ctrl->val = is->is_p_region->face[curr_index].mouth.offset_y
			+ is->is_p_region->face[curr_index].mouth.height;
		break;
	/* 6. Angle information */
	case V4L2_CID_IS_FD_GET_ANGLE:
		ctrl->val = is->is_p_region->face[curr_index].roll_angle;
		break;
	case V4L2_CID_IS_FD_GET_YAW_ANGLE:
		ctrl->val = is->is_p_region->face[curr_index].yaw_angle;
		break;
	/* 7. Update next face information */
	case V4L2_CID_IS_FD_GET_NEXT:
		ctrl->val = 0;
		is->fd_header.offset++;
		is->fd_header.curr_index++;
		break;
	/* EXIF CID handler */
	case V4L2_CID_IS_CAMERA_EXIF_EXPTIME:
		ctrl->val = is->is_p_region->header[0].exif.exposure_time.den;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_FLASH:
		ctrl->val = is->is_p_region->header[0].exif.flash;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_ISO:
		ctrl->val = is->is_p_region->header[0].exif.iso_speed_rating;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_SHUTTERSPEED:
		ctrl->val = is->is_p_region->header[0].exif.exposure_time.den;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_BRIGHTNESS:
		ctrl->val = is->is_p_region->header[0].exif.brightness.num;
		break;
	case V4L2_CID_IS_CAMERA_AUTO_FOCUS_RESULT:
		ctrl->val = is->af.af_lock_state;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops fimc_is_ctrl_ops = {
	.s_ctrl	= fimc_is_s_ctrl,
	.g_volatile_ctrl = fimc_is_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config fimc_is_ctrl[] = {
	{
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_S_FORMAT_SCENARIO,
		.name = "Set FIMC-IS scenario mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_MODE_CAPTURE_VIDEO,
		.step = 1,
		.min = IS_MODE_PREVIEW_STILL,
		.def = IS_MODE_PREVIEW_STILL,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FRAME_RATE,
		.name = "Set FIMC-IS fps rate",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 30,
		.step = 1,
		.min = 0, /* Auto mode : variable fps mode (15~30) */
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_OBJECT_POSITION_X,
		.name = "Set position X",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_OBJECT_POSITION_Y,
		.name = "Set position Y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_METERING_POSITION_X,
		.name = "Set position X",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_METERING_POSITION_Y,
		.name = "Set position Y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_METERING_WINDOW_X,
		.name = "Set window X",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_METERING_WINDOW_Y,
		.name = "Set window Y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_AE_AWB_LOCK_UNLOCK,
		.name = "Set AE/AWB lock/unlock",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_AE_LOCK_AWB_LOCK,
		.step = 1,
		.min = IS_AE_UNLOCK_AWB_UNLOCK,
		.def = IS_AE_UNLOCK_AWB_UNLOCK,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_IMAGE_EFFECT,
		.name = "Set FIMC-ISP effect mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_IMAGE_EFFECT_SEPIA,
		.step = 1,
		.min = IS_IMAGE_EFFECT_DISABLE,
		.def = IS_IMAGE_EFFECT_DISABLE,
	}, { /* 10 */
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_AWB_MODE,
		.name = "Set FIMC-ISP AWB mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_AWB_FLUORESCENT,
		.step = 1,
		.min = IS_AWB_AUTO,
		.def = IS_AWB_AUTO,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_ISO,
		.name = "Set FIMC-ISP iso",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_ISO_800,
		.step = 1,
		.min = IS_ISO_AUTO,
		.def = IS_ISO_AUTO,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_CONTRAST,
		.name = "Set FIMC-ISP contrast",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2,
		.step = 1,
		.min = -2,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_SATURATION,
		.name = "Set FIMC-ISP saturation",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2,
		.step = 1,
		.min = -2,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_SHARPNESS,
		.name = "Set FIMC-ISP sharpness",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2,
		.step = 1,
		.min = -2,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_EXPOSURE,
		.name = "Set FIMC-ISP exposure",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 4,
		.step = 1,
		.min = -4,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_BRIGHTNESS,
		.name = "Set FIMC-ISP brightness",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2,
		.step = 1,
		.min = -2,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_HUE,
		.name = "Set FIMC-ISP hue",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 2,
		.step = 1,
		.min = -2,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_METERING,
		.name = "Set FIMC-ISP metering mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_METERING_CENTER,
		.step = 1,
		.min = IS_METERING_AVERAGE,
		.def = IS_METERING_CENTER,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_AFC_MODE,
		.name = "Set FIMC-ISP afc mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_AFC_MANUAL_60HZ,
		.step = 1,
		.min = IS_AFC_DISABLE,
		.def = IS_AFC_AUTO,
	}, { /* 20 */
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_SCENE_MODE,
		.name = "Set FIMC-ISP scene mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_SCENE_MODE_CANDLE_LIGHT,
		.step = 1,
		.min = IS_SCENE_MODE_NONE,
		.def = IS_SCENE_MODE_NONE,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_VT_MODE,
		.name = "Set FIMC-ISP VT mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 3,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_SET_DRC,
		.name = "Set FIMC-DRC on/off",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_DRC_BYPASS_ENABLE,
		.step = 1,
		.min = IS_DRC_BYPASS_DISABLE,
		.def = IS_DRC_BYPASS_DISABLE,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CMD_FD,
		.name = "Set FIMC-FD on/off",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_FD_COMMAND_START,
		.step = 1,
		.min = IS_FD_COMMAND_STOP,
		.def = IS_FD_COMMAND_STOP,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_SET_MAX_FACE_NUMBER,
		.name = "Set FIMC-FD max detected number of face",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 16,
		.step = 1,
		.min = 1,
		.def = 5,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_SET_ROLL_ANGLE,
		.name = "Set FIMC-FD roll angle option",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_FD_ROLL_ANGLE_PRECISE_FULL,
		.step = 1,
		.min = IS_FD_ROLL_ANGLE_BASIC,
		.def = IS_FD_ROLL_ANGLE_BASIC,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_SET_YAW_ANGLE,
		.name = "Set FIMC-FD yaw angle option",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_FD_YAW_ANGLE_45_90,
		.step = 1,
		.min = IS_FD_YAW_ANGLE_0,
		.def = IS_FD_YAW_ANGLE_0,
	}, { /* 27 */
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_COUNT,
		.name = "Get FD face count",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 16,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_FRAME_NUMBER,
		.name = "Get FD face frame number",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 65536,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_CONFIDENCE,
		.name = "Get FD confidence",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 500,
		.step = 1,
		.min = 0,
		.def = 0,
	}, { /* 30 */
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_SMILE_LEVEL,
		.name = "Get FD smile level",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 500,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_BLINK_LEVEL,
		.name = "Get FD blink level",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 500,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_TOPLEFT_X,
		.name = "Get FD Face pos topleft x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_TOPLEFT_Y,
		.name = "Get FD Face pos topleft y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_X,
		.name = "Get FD Face pos bottomright x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_Y,
		.name = "Get FD Face pos bottomright y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_X,
		.name = "Get FD left eye pos topleft x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_Y,
		.name = "Get FD left eye pos topleft y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_X,
		.name = "Get FD left eye pos bottomright x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_Y,
		.name = "Get FD left eye pos bottomright y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {  /* 40 */
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_X,
		.name = "Get FD right eye pos topleft x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_Y,
		.name = "Get FD right eye pos topleft y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_X,
		.name = "Get FD right eye pos bottomright x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_Y,
		.name = "Get FD right eye pos bottomright y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_X,
		.name = "Get FD right mouth topleft x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_Y,
		.name = "Get FD right mouth topleft y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_X,
		.name = "Get FD right mouth bottomright x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 3264,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_Y,
		.name = "Get FD right mouth bottomright y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 2448,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_ANGLE,
		.name = "Get FD get roll angle",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 360,
		.step = 90,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_YAW_ANGLE,
		.name = "Get FD get yaw angle",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 90,
		.step = 45,
		.min = 0,
		.def = 0,
	}, { /* 50 */
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_FD_GET_NEXT,
		.name = "Get FD get next data",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 1,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_EXIF_EXPTIME,
		.name = "Get Exif info : exptime",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 65536,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_EXIF_FLASH,
		.name = "Get Exif info : flash",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 65536,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_EXIF_ISO,
		.name = "Get Exif info : iso",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 65536,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_EXIF_SHUTTERSPEED,
		.name = "Get Exif info : shutter speed",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 65536,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_EXIF_BRIGHTNESS,
		.name = "Get Exif info : brightness",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 65536,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_AUTO_FOCUS_RESULT,
		.name = "Get AF result",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.max = 10,
		.step = 1,
		.min = 0,
		.def = 0,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_FOCUS_MODE,
		.name = "Set FIMC-ISP focus mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_FOCUS_MODE_MAX,
		.step = 1,
		.min = IS_FOCUS_MODE_AUTO,
		.def = IS_FOCUS_MODE_AUTO,
	}, {
		.ops = &fimc_is_ctrl_ops,
		.id = V4L2_CID_IS_CAMERA_FOCUS_START_STOP,
		.name = "Set focus start / stop",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = IS_FOCUS_START,
		.step = 1,
		.min = IS_FOCUS_STOP,
		.def = IS_FOCUS_STOP,
	},
};

#define NUM_CTRLS ARRAY_SIZE(fimc_is_ctrl)

int fimc_is_create_controls(struct fimc_isp *isp)
{
	struct v4l2_ctrl_handler *handler = &isp->ctrl_handler;
	struct fimc_is_control *ctrl_info = &isp->ctrl_isp;

	/* set the controls using v4l2 control frameworks */
	v4l2_ctrl_handler_init(handler, NUM_CTRLS);

	/* set control handler */
	ctrl_info->set_scenaio_mode =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[0], NULL);
	ctrl_info->fps =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[1], NULL);
	ctrl_info->set_position_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[2], NULL);
	ctrl_info->set_position_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[3], NULL);
	ctrl_info->set_metering_pos_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[4], NULL);
	ctrl_info->set_metering_pos_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[5], NULL);
	ctrl_info->set_metering_win_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[6], NULL);
	ctrl_info->set_metering_win_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[7], NULL);
	ctrl_info->ae_awb_lock_unlock =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[8], NULL);
	ctrl_info->effect =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[9], NULL);
	ctrl_info->awb =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[10], NULL);
	ctrl_info->iso =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[11], NULL);
	ctrl_info->contrast =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[12], NULL);
	ctrl_info->saturation =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[13], NULL);
	ctrl_info->sharpness =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[14], NULL);
	ctrl_info->exposure =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[15], NULL);
	ctrl_info->brightness =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[16], NULL);
	ctrl_info->hue =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[17], NULL);
	ctrl_info->metering =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[18], NULL);
	ctrl_info->afc =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[19], NULL);
	ctrl_info->scene_mode =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[20], NULL);
	ctrl_info->vt_mode =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[21], NULL);
	ctrl_info->drc =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[22], NULL);
	ctrl_info->fd =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[23], NULL);
	ctrl_info->fd_set_face_number =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[24], NULL);
	ctrl_info->fd_set_roll_angle =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[25], NULL);
	ctrl_info->fd_set_yaw_angle =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[26], NULL);

	/* get control handler */
	/* FD result */
	ctrl_info->fd_get_face_cnt =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[27], NULL);
	ctrl_info->fd_get_face_frame_number =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[28], NULL);
	ctrl_info->fd_get_face_confidence =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[29], NULL);
	ctrl_info->fd_get_face_blink_level =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[30], NULL);
	ctrl_info->fd_get_face_smile_level =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[31], NULL);
	ctrl_info->fd_get_face_pos_tl_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[32], NULL);
	ctrl_info->fd_get_face_pos_tl_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[33], NULL);
	ctrl_info->fd_get_face_pos_br_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[34], NULL);
	ctrl_info->fd_get_face_pos_br_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[35], NULL);
	ctrl_info->fd_get_left_eye_pos_tl_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[36], NULL);
	ctrl_info->fd_get_left_eye_pos_tl_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[37], NULL);
	ctrl_info->fd_get_left_eye_pos_br_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[38], NULL);
	ctrl_info->fd_get_left_eye_pos_br_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[39], NULL);
	ctrl_info->fd_get_right_eye_pos_tl_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[40], NULL);
	ctrl_info->fd_get_right_eye_pos_tl_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[41], NULL);
	ctrl_info->fd_get_right_eye_pos_br_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[42], NULL);
	ctrl_info->fd_get_right_eye_pos_br_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[43], NULL);
	ctrl_info->fd_get_mouth_pos_tl_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[44], NULL);
	ctrl_info->fd_get_mouth_pos_tl_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[45], NULL);
	ctrl_info->fd_get_mouth_pos_br_x =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[46], NULL);
	ctrl_info->fd_get_mouth_pos_br_y =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[47], NULL);
	ctrl_info->fd_get_roll_angle =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[48], NULL);
	ctrl_info->fd_get_yaw_angle =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[49], NULL);
	ctrl_info->fd_get_next_data =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[50], NULL);
	/* Exif info */
	ctrl_info->exif_exptime =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[51], NULL);
	ctrl_info->exif_flash =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[52], NULL);
	ctrl_info->exif_iso =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[53], NULL);
	ctrl_info->exif_shutterspeed =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[54], NULL);
	ctrl_info->exif_brightness =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[55], NULL);
	/* AF status */
	ctrl_info->af_status =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[56], NULL);
	ctrl_info->focus_mode =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[57], NULL);
	ctrl_info->focus_start_stop =
			v4l2_ctrl_new_custom(handler, &fimc_is_ctrl[58], NULL);

	if (handler->error) {
		pr_err("%s : ctrl_handler failed\n", __func__);
		v4l2_ctrl_handler_free(handler);
		return handler->error;
	}

	return 0;
}

int fimc_is_delete_controls(struct fimc_isp *isp)
{
	struct v4l2_ctrl_handler *handler = &isp->ctrl_handler;

	v4l2_ctrl_handler_free(handler);
	return 0;
}

int fimc_isp_subdev_create(struct fimc_isp *isp)
{
	struct v4l2_ctrl_handler *handler = &isp->ctrl_handler;
	struct v4l2_subdev *sd = &isp->subdev;
	int ret;

	v4l2_subdev_init(sd, &fimc_is_subdev_ops);
	sd->grp_id = FIMC_IS_GROUP_ID;
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "IS-ISP");

	isp->subdev_pads[FIMC_IS_SD_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	isp->subdev_pads[FIMC_IS_SD_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, FIMC_IS_SD_PADS_NUM,
				isp->subdev_pads, 0);
	if (ret)
		return ret;

	ret = fimc_is_create_controls(isp);
	if (ret) {
		media_entity_cleanup(&sd->entity);
		return ret;
	}

	sd->ctrl_handler = handler;
	sd->internal_ops = &fimc_is_subdev_internal_ops;
	sd->entity.ops = &fimc_is_subdev_media_ops;
	v4l2_set_subdevdata(sd, isp);

	return 0;
}

void fimc_isp_subdev_destroy(struct fimc_isp *isp)
{
	struct v4l2_subdev *sd = &isp->subdev;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&isp->ctrl_handler);
	v4l2_set_subdevdata(sd, NULL);
}
