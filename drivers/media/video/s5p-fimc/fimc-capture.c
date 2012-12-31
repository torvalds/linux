/*
 * Samsung S5P/EXYNOS4 SoC series camera interface (camera capture) driver
 *
 * Copyright (C) 2010 - 2011 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/i2c.h>

#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-cma-phys.h>

#include "fimc-core.h"

static struct v4l2_subdev *fimc_subdev_register(struct fimc_dev *fimc,
					    struct s5p_fimc_isp_info *isp_info)
{
	struct i2c_adapter *i2c_adap;
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	struct v4l2_subdev *sd = NULL;

	i2c_adap = i2c_get_adapter(isp_info->i2c_bus_num);
	if (!i2c_adap)
		return ERR_PTR(-ENOMEM);

	sd = v4l2_i2c_new_subdev_board(&vid_cap->v4l2_dev, i2c_adap,
					isp_info->board_info, NULL);
	if (!sd) {
		v4l2_err(&vid_cap->v4l2_dev, "failed to acquire subdev\n");
		return NULL;
	}

	v4l2_info(&vid_cap->v4l2_dev, "subdevice %s registered successfuly\n",
		isp_info->board_info->type);

	return sd;
}

static void fimc_subdev_unregister(struct fimc_dev *fimc)
{
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	struct i2c_client *client;

	if (vid_cap->input_index < 0)
		return;	/* Subdevice already released or not registered. */

	if (vid_cap->sd) {
		v4l2_device_unregister_subdev(vid_cap->sd);
		client = v4l2_get_subdevdata(vid_cap->sd);
		i2c_unregister_device(client);
		i2c_put_adapter(client->adapter);
		vid_cap->sd = NULL;
	}
	if (vid_cap->mipi_sd) {
		v4l2_device_unregister_subdev(vid_cap->mipi_sd);
		vid_cap->mipi_sd = NULL;
	}
	if (vid_cap->fb_sd) {
		v4l2_device_unregister_subdev(vid_cap->fb_sd);
		vid_cap->fb_sd = NULL;
	}
	if (vid_cap->is.sd) {
		v4l2_device_unregister_subdev(vid_cap->is.sd);
		vid_cap->is.sd = NULL;
		vid_cap->is.frame_count = 0;
		vid_cap->is.valid = 0;
		vid_cap->is.bad_mark = 0;
		vid_cap->is.offset_x = 0;
		vid_cap->is.offset_y = 0;
	}
	if (vid_cap->flite_sd) {
		v4l2_device_unregister_subdev(vid_cap->flite_sd);
		vid_cap->flite_sd = NULL;
	}
	vid_cap->input_index = -1;
}

static int mipi_csi_register_callback(struct device *dev, void *p)
{
	struct v4l2_subdev **sd_list = p;
	struct v4l2_subdev *sd = NULL;

	sd = dev_get_drvdata(dev);

	if (sd) {
		struct platform_device *pdev = v4l2_get_subdevdata(sd);
		if (pdev)
			dbg("pdev->id: %d", pdev->id);
		*(sd_list + pdev->id) = sd;
	}

	return 0; /* non-zero value stops iteration */
}

static struct v4l2_subdev *s5p_mipi_get_subdev(int id)
{
	const char *module_name = "s5p-mipi-csis";
	struct device_driver *drv;
	struct v4l2_subdev *sd[FIMC_MAX_CSIS_NUM] = {NULL,};
	int ret;

	drv = driver_find(module_name, &platform_bus_type);
	if (!drv)  {
		request_module(module_name);
		drv = driver_find(module_name, &platform_bus_type);
	}
	if (!drv)
		return ERR_PTR(-ENODEV);

	ret = driver_for_each_device(drv, NULL, &sd[0],
				     mipi_csi_register_callback);
	put_driver(drv);

	return ret ? NULL : sd[id];
}

static int s3cfb_register_callback(struct device *dev, void *p)
{
	struct v4l2_subdev **sd = p;

	/*
	 * FIXME: detect platform device id and handle multiple
	 * MIPI-CSI devices.
	 */
	*sd = dev_get_drvdata(dev);

	if (*sd) {
		struct platform_device *pdev = v4l2_get_subdevdata(*sd);
		if (pdev)
			dbg("pdev->id: %d", pdev->id);
	}

	return 0; /* non-zero value stops iteration */
}

static int flite_register_callback(struct device *dev, void *p)
{
	struct v4l2_subdev **sd_list = p;
	struct v4l2_subdev *sd = NULL;

	sd = dev_get_drvdata(dev);
	if (sd) {
		struct platform_device *pdev = v4l2_get_subdevdata(sd);
		*(sd_list + pdev->id) = sd;
	}

	return 0; /* non-zero value stops iteration */
}

static struct v4l2_subdev *exynos_flite_get_subdev(int id)
{
	const char *module_name = "exynos-fimc-lite";
	struct device_driver *drv;
	struct v4l2_subdev *sd[FLITE_MAX_NUM] = {NULL,};
	int ret;

	drv = driver_find(module_name, &platform_bus_type);
	if (!drv)  {
		request_module(module_name);
		drv = driver_find(module_name, &platform_bus_type);
	}
	if (!drv)
		return ERR_PTR(-ENODEV);

	ret = driver_for_each_device(drv, NULL, &sd[0],
				     flite_register_callback);
	put_driver(drv);

	return ret ? NULL : sd[id];
}

static int fimc_is_register_callback(struct device *dev, void *p)
{
	struct v4l2_subdev **sd = p;
	struct platform_device *pdev;

	*sd = dev_get_drvdata(dev);

	if (*sd)
		pdev = v4l2_get_subdevdata(*sd);

	return 0; /* non-zero value stops iteration */
}

static struct v4l2_subdev *fimc_is_get_subdev(int id)
{
	const char *module_name = "exynos4-fimc-is";
	struct device_driver *drv;
	struct v4l2_subdev *sd = NULL;
	int ret;

	drv = driver_find(module_name, &platform_bus_type);
	if (!drv)  {
		request_module(module_name);
		drv = driver_find(module_name, &platform_bus_type);
	}
	if (!drv)
		return ERR_PTR(-ENODEV);

	ret = driver_for_each_device(drv, NULL, &sd,
				     fimc_is_register_callback);
	put_driver(drv);
	return ret ? NULL : sd;
}

static struct v4l2_subdev *s3c_fb_get_subdev(int id)
{
	const char *module_name = FIMD_MODULE_NAME;
	struct device_driver *drv;
	struct v4l2_subdev *sd = NULL;
	int ret;

	drv = driver_find(module_name, &platform_bus_type);
	if (!drv) {
		request_module(module_name);
		drv = driver_find(module_name, &platform_bus_type);
	}
	if (!drv)
		return ERR_PTR(-ENODEV);
	/*
	 * FIXME: detect platform device id and handle multiple
	 * MIPI-CSI devices. Now always a subdev from the last
	 * found device is returned.
	 */
	ret = driver_for_each_device(drv, NULL, &sd,
				     s3cfb_register_callback);
	put_driver(drv);

	return ret ? NULL : sd;
}

/**
 * fimc_subdev_attach - attach v4l2_subdev to camera host interface
 *
 * @fimc: FIMC device information
 * @index: index to the array of available subdevices,
 *	   -1 for full array search or non negative value
 *	   to select specific subdevice
 */
static int fimc_subdev_attach(struct fimc_dev *fimc, int index)
{
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	struct s5p_platform_fimc *pdata = fimc->pdata;
	struct s5p_fimc_isp_info *isp_info;
	struct v4l2_subdev *sd;
	int i, ret;

	for (i = 0; i < FIMC_MAX_CAMIF_CLIENTS; ++i) {
		isp_info = pdata->isp_info[i];
		if (!isp_info || (index >= 0 && i != index))
			continue;
		if (isp_info->bus_type == FIMC_MIPI_CSI2) {
			vid_cap->mipi_sd = s5p_mipi_get_subdev(isp_info->mux_id);
			if (IS_ERR_OR_NULL(vid_cap->mipi_sd)) {
				fimc->vid_cap.mipi_sd = NULL;
				return PTR_ERR(vid_cap->mipi_sd);
			}
		} else if (isp_info->bus_type == FIMC_LCD_WB) {
			vid_cap->fb_sd = s3c_fb_get_subdev(0);
			if (IS_ERR_OR_NULL(vid_cap->fb_sd))
				return PTR_ERR(vid_cap->fb_sd);
			vid_cap->ctx->in_path = FIMC_LCD_WB;
			vid_cap->input_index = i;

			return 0;
		}

		if (!isp_info->use_isp) {
			sd = fimc_subdev_register(fimc, isp_info);
			if (sd) {
				vid_cap->sd = sd;
				vid_cap->input_index = i;

				return 0;
			}
		} else {
			/* Register FIMC-Lite */
			vid_cap->flite_sd = exynos_flite_get_subdev(isp_info->flite_id);
			if (IS_ERR_OR_NULL(vid_cap->flite_sd)) {
				vid_cap->flite_sd = NULL;
				return PTR_ERR(vid_cap->flite_sd);
			} else {
				if (fimc_cam_use(index)) {
					ret = v4l2_subdev_call(vid_cap->flite_sd, core, s_power, 1);
					if (ret)
						err("s_power failed: %d", ret);
				}

			}
			dbg("FIMC%d Register FIMC-Lite subdev\n", fimc->id);
			/* Register FIMC-IS*/
			vid_cap->is.sd = fimc_is_get_subdev(index);
			if (IS_ERR_OR_NULL(vid_cap->is.sd)) {
				vid_cap->is.sd = NULL;
				return PTR_ERR(vid_cap->is.sd);
			}
			vid_cap->input_index = i;
			dbg("FIMC%d Register FIMC-IS subdev\n", fimc->id);
			vid_cap->is.fmt.width = 0;
			vid_cap->is.fmt.height = 0;
			vid_cap->is.frame_count = 0;

			return 0;
		}
	}

	vid_cap->input_index = -1;
	vid_cap->sd = NULL;
	v4l2_err(&vid_cap->v4l2_dev, "fimc%d: sensor attach failed\n",
		 fimc->id);
	return -ENODEV;
}

static int fimc_isp_subdev_init(struct fimc_dev *fimc, unsigned int index)
{
	struct s5p_fimc_isp_info *isp_info;
	int ret;
	if (index >= FIMC_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	isp_info = fimc->pdata->isp_info[index];
	if (!isp_info)
		return -EINVAL;

	if (isp_info->clk_frequency && isp_info->use_cam) {
		if (isp_info->mux_id == 0) {
			fimc->vid_cap.mux_id = 0;
			fimc->clock[CLK_CAM0] =
				clk_get(&fimc->pdev->dev, CLK_NAME_CAM0);
			ret = fimc_clk_setrate(fimc, CLK_CAM0, isp_info);
			if (!ret)
				clk_enable(fimc->clock[CLK_CAM0]);
		} else {
			fimc->vid_cap.mux_id = 1;
			fimc->clock[CLK_CAM1] =
				clk_get(&fimc->pdev->dev, CLK_NAME_CAM1);
			ret = fimc_clk_setrate(fimc, CLK_CAM1, isp_info);
			if (!ret)
				clk_enable(fimc->clock[CLK_CAM1]);
		}
		if (ret < 0)
			return -EINVAL;
	}

	if (isp_info->use_cam) {
		dbg("FIMC%d try to attatch sensor\n", fimc->id);
		ret = fimc_subdev_attach(fimc, index);
		if (ret)
			return ret;
		fimc->vid_cap.is.camcording = 0;
	} else {
		dbg("FIMC%d didn't try to attatch sensor\n", fimc->id);
		fimc->vid_cap.input_index = index;
		fimc->vid_cap.mux_id = -1;
		if (isp_info->use_isp)
			fimc->vid_cap.is.camcording = 1;
		else
			fimc->vid_cap.is.camcording = 0;
	}

	ret = fimc_hw_set_camera_polarity(fimc, isp_info);
	if (ret)
		return ret;

	if (fimc->vid_cap.mipi_sd)
		ret = v4l2_subdev_call(fimc->vid_cap.mipi_sd, core, s_power, 1);

	if (fimc->vid_cap.sd)
		ret = v4l2_subdev_call(fimc->vid_cap.sd, core, s_power, 1);

	if (fimc->vid_cap.fb_sd) {
		unsigned int wb_on = 1;
		dbg("write-back mode\n");
		ret = v4l2_subdev_call(fimc->vid_cap.fb_sd, core, ioctl,
					(unsigned int)NULL, &wb_on);
	}

	if (fimc->vid_cap.is.sd) {
		dbg("FIMC-IS Init sequence\n");
		ret = isp_info->cam_power(1);
		if (unlikely(ret < 0))
			err("Fail to power on\n");
		ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, s_power, 1);
		if (ret < 0) {
			err("FIMC-IS init failed - power on");
			return -ENODEV;
		}
		ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, load_fw);
		if (ret < 0) {
			err("FIMC-IS init failed - load fw");
			return -ENODEV;
		}

		if (strcmp(&isp_info->board_info->type[0], "S5K3H2") == 0) {
			switch (isp_info->mux_id) {
			case 0:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 1);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			case 1:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 101);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			default:
				break;
			}
		} else if (strcmp(&isp_info->board_info->type[0], "S5K4E5") == 0) {
			switch (isp_info->mux_id) {
			case 0:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 3);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			case 1:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 103);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			default:
				break;
			}
		} else if (strcmp(&isp_info->board_info->type[0], "S5K3H7") == 0) {
			switch (isp_info->mux_id) {
			case 0:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 4);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			case 1:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 104);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			default:
				break;
			}
		} else if (strcmp(&isp_info->board_info->type[0], "S5K6A3") == 0) {
			switch (isp_info->mux_id) {
			case 0:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 2);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			case 1:
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, init, 102);
				if (ret < 0) {
					err("FIMC-IS init failed - open sensor");
					return -ENODEV;
				}
				break;
			default:
				break;
			}
		}
	}

	if (!ret) {
		return ret;
    }

	/* enabling power failed so unregister subdev */
	fimc_subdev_unregister(fimc);

	v4l2_err(&fimc->vid_cap.v4l2_dev, "ISP initialization failed: %d\n",
		 ret);

	return ret;
}

static int fimc_stop_capture(struct fimc_dev *fimc)
{
	unsigned long flags;
	struct fimc_vid_cap *cap;
	struct fimc_vid_buffer *buf;
	int ret = 0;
	cap = &fimc->vid_cap;

	if (!fimc_capture_active(fimc))
		return 0;

	spin_lock_irqsave(&fimc->slock, flags);
	set_bit(ST_CAPT_SHUT, &fimc->state);
	fimc_deactivate_capture(fimc);
	spin_unlock_irqrestore(&fimc->slock, flags);

	ret = fimc_wait_disable_capture(fimc);
	if (ret < 0)
		err("wait stop seq fail");

	if (cap->sd)
		v4l2_subdev_call(cap->sd, video, s_stream, 0);

	if (cap->is.sd)
		v4l2_subdev_call(fimc->vid_cap.is.sd, video, s_stream, 0);

	if (cap->flite_sd)
		v4l2_subdev_call(fimc->vid_cap.flite_sd, video, s_stream, 0);

	if (cap->mipi_sd)
		v4l2_subdev_call(fimc->vid_cap.mipi_sd, video, s_stream, 0);

	spin_lock_irqsave(&fimc->slock, flags);
	fimc->state &= ~(1 << ST_CAPT_RUN | 1 << ST_CAPT_PEND |
			 1 << ST_CAPT_SHUT | 1 << ST_CAPT_STREAM |
			 1 << ST_CAPT_SENS_STREAM);

	fimc->vid_cap.active_buf_cnt = 0;

	/* Release buffers that were enqueued in the driver by videobuf2. */
	while (!list_empty(&cap->pending_buf_q)) {
		buf = pending_queue_pop(cap);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	while (!list_empty(&cap->active_buf_q)) {
		buf = active_queue_pop(cap);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&fimc->slock, flags);

	dbg("state: 0x%lx", fimc->state);
	return 0;
}

static int start_streaming(struct vb2_queue *q)
{
	struct fimc_ctx *ctx = q->drv_priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct s5p_fimc_isp_info *isp_info;
	int ret = 0;

	if (!test_and_set_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_get_sync(&fimc->pdev->dev);

	if (fimc->vid_cap.mipi_sd) {
		ret = v4l2_subdev_call(fimc->vid_cap.mipi_sd, video, s_stream, 1);
		if (ret) {
			err("mipi s_stream error");
			return ret;
		}
	}

	ret = fimc_prepare_config(ctx, ctx->state);
	if (ret)
		return ret;

	isp_info = fimc->pdata->isp_info[fimc->vid_cap.input_index];
	fimc_hw_set_camera_type(fimc, isp_info);

	if (ctx->in_path == FIMC_LCD_WB) {
		if (isp_info->mux_id == 0)
			fimc_hwset_sysreg_camblk_fimd0_wb(fimc);
		else
			fimc_hwset_sysreg_camblk_fimd1_wb(fimc);
	}

	if (fimc->vid_cap.is.sd) {
		struct platform_device *pdev = fimc->pdev;
		struct clk *pxl_async = NULL;

		dbg("FIMC-IS pixel async setting\n");
		pxl_async = clk_get(&pdev->dev, "pxl_async1");
		if (IS_ERR(pxl_async)) {
		    err("failed to get pxl_async\n");
		    return -ENODEV;
		}

		clk_enable(pxl_async);
		clk_put(pxl_async);
		fimc_hwset_sysreg_camblk_isp_wb(fimc);
	}

	if (fimc->vid_cap.flite_sd) {
		dbg("FIMC-Lite stream on..\n");
		v4l2_subdev_call(fimc->vid_cap.flite_sd, video, s_stream, 1);
	}

	fimc_hw_set_camera_source(fimc, isp_info);
	fimc_hw_set_camera_offset(fimc, &ctx->s_frame);

	if (ctx->state & FIMC_PARAMS) {
		ret = fimc_set_scaler_info(ctx);
		if (ret)
			err("Scaler setup error");
		fimc_hw_set_input_path(ctx);
		fimc_hw_set_prescaler(ctx);
		fimc_hw_set_mainscaler(ctx);
		fimc_hw_set_target_format(ctx);
		fimc_hw_set_rotation(ctx);
		fimc_hw_set_effect(ctx);
	}

	fimc_hw_set_output_path(ctx);
	fimc_hw_set_out_dma(ctx);
	/* for zero shuterlack at Exynos4210 EVT1 */
	fimc_hwset_enable_lastend(fimc);

	INIT_LIST_HEAD(&fimc->vid_cap.pending_buf_q);
	INIT_LIST_HEAD(&fimc->vid_cap.active_buf_q);
	fimc->vid_cap.active_buf_cnt = 0;
	fimc->vid_cap.frame_count = 0;
	fimc->vid_cap.buf_index = fimc_hw_get_frame_index(fimc);

	set_bit(ST_CAPT_PEND, &fimc->state);

	return 0;
}

static int stop_streaming(struct vb2_queue *q)
{
	struct fimc_ctx *ctx = q->drv_priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	if (!test_and_set_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_get_sync(&fimc->pdev->dev);

	if (!fimc_capture_active(fimc))
		return -EINVAL;

	fimc_stop_capture(fimc);
	fimc_hw_reset(fimc);
	fimc->vb2->suspend(fimc->alloc_ctx);

	if (test_and_clear_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_put_sync(&fimc->pdev->dev);

	return 0;
}

static unsigned int get_plane_size(struct fimc_frame *fr, unsigned int plane)
{
	if (!fr || plane >= fr->fmt->memplanes)
		return 0;

	return fr->payload[plane];
}

static int queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
		       unsigned int *num_planes, unsigned long sizes[],
		       void *allocators[])
{
	struct fimc_ctx *ctx = vq->drv_priv;
	struct fimc_fmt *fmt = ctx->d_frame.fmt;
	int i;

	if (!fmt)
		return -EINVAL;

	*num_planes = fmt->memplanes;

	for (i = 0; i < fmt->memplanes; i++) {
		sizes[i] = get_plane_size(&ctx->d_frame, i);
		allocators[i] = ctx->fimc_dev->alloc_ctx;
	}

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct fimc_ctx *ctx = vq->drv_priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	struct v4l2_device *v4l2_dev = &ctx->fimc_dev->m2m.v4l2_dev;
	int i;

	if (!ctx->d_frame.fmt || vq->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	for (i = 0; i < ctx->d_frame.fmt->memplanes; i++) {
		unsigned long size = get_plane_size(&ctx->d_frame, i);

		if (vb2_plane_size(vb, i) < size) {
			v4l2_err(v4l2_dev, "User buffer too small(%ld < %ld)\n",
				 vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	if (ctx->d_frame.cacheable)
		fimc->vb2->cache_flush(vb, frame->fmt->memplanes);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct fimc_vid_buffer *buf
		= container_of(vb, struct fimc_vid_buffer, vb);
	struct fimc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	struct fimc_fmt *fmt = vid_cap->ctx->d_frame.fmt;
	unsigned long flags;
	int min_bufs;

	spin_lock_irqsave(&fimc->slock, flags);
	fimc_prepare_addr(ctx, &buf->vb, &ctx->d_frame, &buf->paddr);

	if (fmt->fourcc == V4L2_PIX_FMT_YVU420 ||
			fmt->fourcc == V4L2_PIX_FMT_YVU420M) {
		u32 t_cb = buf->paddr.cb;
		buf->paddr.cb = buf->paddr.cr;
		buf->paddr.cr = t_cb;
	}

	if (!test_and_set_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_get_sync(&fimc->pdev->dev);

	if (!test_bit(ST_CAPT_STREAM, &fimc->state)
	     && vid_cap->active_buf_cnt < FIMC_MAX_OUT_BUFS) {
		/* Setup the buffer directly for processing. */
		int buf_id = (vid_cap->reqbufs_count == 1) ? -1 :
				vid_cap->buf_index;
		fimc_hw_set_output_addr(fimc, &buf->paddr, buf_id);
		buf->index = vid_cap->buf_index;
		active_queue_add(vid_cap, buf);

		if (++vid_cap->buf_index >= FIMC_MAX_OUT_BUFS)
			vid_cap->buf_index = 0;
	} else {
		fimc_pending_queue_add(vid_cap, buf);
	}

	min_bufs = vid_cap->reqbufs_count > 1 ? 2 : 1;

	if (!test_bit(ST_CAPT_RUN, &fimc->state)) {
		if (vid_cap->active_buf_cnt == 1)
			fimc->vb2->resume(fimc->alloc_ctx);
	}
	if (vid_cap->active_buf_cnt >= min_bufs &&
	    !test_and_set_bit(ST_CAPT_STREAM, &fimc->state)) {
		int ret = 0;
		fimc_activate_capture(ctx);
		spin_unlock_irqrestore(&fimc->slock, flags);

		if (!test_and_set_bit(ST_CAPT_SENS_STREAM, &fimc->state)) {
			if (fimc->vid_cap.is.sd)
				ret = v4l2_subdev_call(fimc->vid_cap.is.sd,
					video, s_stream, 1);
			else
				ret = v4l2_subdev_call(fimc->vid_cap.sd, video,
						s_stream, 1);
		}
		return;  /* ret = -ENOIOCTLCMD ? 0 : ret; */
	}

	spin_unlock_irqrestore(&fimc->slock, flags);

	return;
}

static void fimc_lock(struct vb2_queue *vq)
{
#ifdef FOR_DIFF_VER
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->fimc_dev->lock);
#endif
}

static void fimc_unlock(struct vb2_queue *vq)
{
#ifdef FOR_DIFF_VER
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->fimc_dev->lock);
#endif
}

static struct vb2_ops fimc_capture_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_queue		= buffer_queue,
	.wait_prepare		= fimc_unlock,
	.wait_finish		= fimc_lock,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
};

static int fimc_capture_open(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);

	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), fimc->state);
	if (!test_and_set_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_get_sync(&fimc->pdev->dev);

	fimc_hw_reset(fimc);
	fimc_hw_set_irq_level(fimc);

	if (fimc->variant->out_buf_count > 4)
		fimc_hw_set_dma_seq(fimc, 0xF);

	/* Return if the corresponding video mem2mem node is already opened. */
	if (fimc_m2m_active(fimc))
		return -EBUSY;

	file->private_data = fimc->vid_cap.ctx;

	if (test_and_clear_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_put_sync(&fimc->pdev->dev);

	return 0;
}

static int fimc_capture_close(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);

	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), fimc->state);

	if (!test_and_set_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_get_sync(&fimc->pdev->dev);

	if (--fimc->vid_cap.refcnt == 0) {
		fimc_stop_capture(fimc);
		vb2_queue_release(&fimc->vid_cap.vbq);

		v4l2_err(&fimc->vid_cap.v4l2_dev, "releasing ISP\n");

		if (fimc->vid_cap.sd)
			v4l2_subdev_call(fimc->vid_cap.sd, core, s_power, 0);

		if (fimc->vid_cap.is.sd)
			v4l2_subdev_call(fimc->vid_cap.is.sd, core, s_power, 0);

		if (fimc->vid_cap.flite_sd)
			v4l2_subdev_call(fimc->vid_cap.flite_sd, core, s_power, 0);

		if (fimc->vid_cap.mipi_sd)
			v4l2_subdev_call(fimc->vid_cap.mipi_sd, core, s_power, 0);

		if (fimc->vid_cap.mux_id == 0) {
			clk_put(fimc->clock[CLK_CAM0]);
			clk_disable(fimc->clock[CLK_CAM0]);
		} else if (fimc->vid_cap.mux_id == 1) {
			clk_put(fimc->clock[CLK_CAM1]);
			clk_disable(fimc->clock[CLK_CAM1]);
		}
		fimc_subdev_unregister(fimc);
	}

	if (test_and_clear_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_put_sync(&fimc->pdev->dev);

	return 0;
}

static unsigned int fimc_capture_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

	return vb2_poll(&fimc->vid_cap.vbq, file, wait);
}

static int fimc_capture_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

	return vb2_mmap(&fimc->vid_cap.vbq, vma);
}

/* video device file operations */
static const struct v4l2_file_operations fimc_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_capture_open,
	.release	= fimc_capture_close,
	.poll		= fimc_capture_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_capture_mmap,
};

static int fimc_vidioc_querycap_capture(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

	strncpy(cap->driver, fimc->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, fimc->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

/* Synchronize formats of the camera interface input and attached  sensor. */
static int sync_capture_fmt(struct fimc_ctx *ctx, struct v4l2_rect *r)
{
	struct fimc_frame *frame = &ctx->d_frame;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct v4l2_mbus_framefmt *fmt = &fimc->vid_cap.fmt;
	struct fimc_pix_limit *plim = fimc->variant->pix_limit;
	int ret;
	int max_w, max_h;

	fmt->width  = r->width;
	fmt->height = r->height;
	max_w = ctx->scaler.enabled ? plim->scaler_en_w : plim->scaler_dis_w;
	max_h = fimc_fmt_is_jpeg(frame->fmt->color) ? 0xFFFF : plim->in_rot_en_h;

	v4l_bound_align_image(&fmt->width, 16, max_w, 4,
			      &fmt->height, 2, max_h, 1, 0);

	dbg("%dx%d", fmt->width, fmt->height);
	/*
	 *  Try to match image sensor pixel format to the color format
	 *  of the capture image to avoid color conversion when possible.
	 *  If fimc destination is jpeg, sensor should be jpeg mbus code,
	 *  and others should be yuv type although fimc'output is rgb.
	 */
	if (frame->fmt->fourcc == V4L2_PIX_FMT_JPEG)
		fmt->code = frame->fmt->mbus_code;
	else
		fmt->code = DEFAULT_ISP_PIXCODE;

	dbg("frame->fmt->mbus_code : 0x%x", frame->fmt->mbus_code);
	dbg("frame->fmt->name : %s", frame->fmt->name);
	dbg("frame->fmt->fourcc : 0x%x", frame->fmt->fourcc);
	dbg("frame->fmt->color : 0x%x", frame->fmt->color);
	dbg("frame->fmt->memplanes : 0x%x", frame->fmt->memplanes);
	dbg("frame->fmt->colplanes : 0x%x", frame->fmt->colplanes);

	dbg("fmt->code : 0x%x", fmt->code);
	dbg("fmt->colorspace : 0x%x", fmt->colorspace);

	if (fimc->vid_cap.sd)
		ret = v4l2_subdev_call(fimc->vid_cap.sd, video, s_mbus_fmt, fmt);

	if (ret) {
		err("s_mbus_fmt failed");
		return ret;
	}

	err("IS : w= %d, h= %d", fimc->vid_cap.is.mbus_fmt.width, fimc->vid_cap.is.mbus_fmt.height);
	if (fimc->vid_cap.mipi_sd) {
		if (fimc->vid_cap.is.sd)
			ret = v4l2_subdev_call(fimc->vid_cap.mipi_sd, video, s_mbus_fmt, &fimc->vid_cap.is.mbus_fmt);
		else
			ret = v4l2_subdev_call(fimc->vid_cap.mipi_sd, video, s_mbus_fmt, fmt);
		if (ret) {
			err("s_mbus_fmt failed: %d", ret);
			return ret;
		}
	}

	if (fimc->vid_cap.flite_sd)
		ret = v4l2_subdev_call(fimc->vid_cap.flite_sd, video, s_mbus_fmt, &fimc->vid_cap.is.mbus_fmt);

	if (ret) {
		err("s_mbus_fmt failed");
		return ret;
	}

	dbg("w= %d, h= %d, code= %d", fmt->width, fmt->height, fmt->code);

	frame = &ctx->s_frame;

	frame->fmt = find_mbus_format(fmt, FMT_FLAGS_CAM);
	if (!frame->fmt) {
		v4l2_err(&fimc->vid_cap.v4l2_dev,
			 "fimc source format not found\n");
		return -EINVAL;
	}

	/* Color conversion to JPEG is not supported */
	if (ctx->d_frame.fmt->color == S5P_FIMC_JPEG &&
	    fmt->code != V4L2_MBUS_FMT_JPEG_1X8)
		return -EINVAL;
	frame->f_width	= fmt->width;
	frame->f_height = fmt->height;
	frame->width	= fmt->width;
	frame->height	= fmt->height;
	frame->o_width	= fmt->width;
	frame->o_height = fmt->height;
	frame->offs_h	= 0;
	frame->offs_v	= 0;

	dbg("frame->width : %d, frame->f_height : %d, frame->width : %d,\
		frame->height : %d, frame->o_width : %d, frame->o_height :\
		%d\n", frame->width, frame->f_height, frame->width,\
		frame->height, frame->o_width, frame->o_height);
	return 0;
}

static int fimc_cap_s_fmt_mplane(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_frame *frame;
	struct v4l2_pix_format_mplane *pix;
	struct v4l2_rect r;
	int ret;
	int i;
	struct v4l2_control is_ctrl;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	r.width  = f->fmt.pix_mp.width;
	r.height = f->fmt.pix_mp.height;

	ret = fimc_vidioc_try_fmt_mplane(file, priv, f);
	if (ret)
		return ret;

	if (vb2_is_streaming(&fimc->vid_cap.vbq) || fimc_capture_active(fimc))
		return -EBUSY;

	frame = &ctx->d_frame;

	pix = &f->fmt.pix_mp;
	frame->fmt = find_format(f, FMT_FLAGS_M2M | FMT_FLAGS_CAM);
	if (!frame->fmt) {
		err("fimc target format not found\n");
		return -EINVAL;
	}

	for (i = 0; i < frame->fmt->colplanes; i++)
		frame->payload[i] = pix->plane_fmt[i].sizeimage;

	fimc_set_frame_size_mp(frame, f);

	ctx->state |= (FIMC_PARAMS | FIMC_DST_FMT);

	if (!test_and_set_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_get_sync(&fimc->pdev->dev);

	/* for returning preview size for FIMC-Lite */
	if (fimc->vid_cap.is.sd) {
		fimc->vid_cap.is.mbus_fmt.code = V4L2_MBUS_FMT_SGRBG10_1X10;
		is_ctrl.id = V4L2_CID_IS_GET_SENSOR_WIDTH;
		v4l2_subdev_call(fimc->vid_cap.is.sd, core, g_ctrl, &is_ctrl);
		fimc->vid_cap.is.fmt.width = fimc->vid_cap.is.mbus_fmt.width = is_ctrl.value;

		is_ctrl.id = V4L2_CID_IS_GET_SENSOR_HEIGHT;
		v4l2_subdev_call(fimc->vid_cap.is.sd, core, g_ctrl, &is_ctrl);
		fimc->vid_cap.is.fmt.height = fimc->vid_cap.is.mbus_fmt.height = is_ctrl.value;
		/* default offset values */
		fimc->vid_cap.is.offset_x = 16;
		fimc->vid_cap.is.offset_y = 12;
		fimc->vid_cap.is.mbus_fmt.width += fimc->vid_cap.is.offset_x;
		fimc->vid_cap.is.mbus_fmt.height += fimc->vid_cap.is.offset_y;
	}
	ret = sync_capture_fmt(ctx, &r);

	/* Scaling is not supported with JPEG color format. */
	ctx->scaler.enabled = !fimc_fmt_is_jpeg(ctx->d_frame.fmt->color);

	return ret;
}

static int fimc_s_fmt_vid_private(struct file *file, void *fh, struct v4l2_format *f)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct v4l2_mbus_framefmt *mbus_fmt;
	int ret = 0;

	dbg("%s\n", __func__);
	mbus_fmt = kzalloc(sizeof(*mbus_fmt), GFP_KERNEL);
	if (!mbus_fmt) {
		err("%s: no memory for "
			"mbus_fmt\n", __func__);
		return -ENOMEM;
	}
	fimc->vid_cap.is.fmt.width = f->fmt.pix.width;
	fimc->vid_cap.is.fmt.height = f->fmt.pix.height;
	fimc->vid_cap.is.fmt.pixelformat = f->fmt.pix.pixelformat;
	fimc->vid_cap.is.mbus_fmt.width = f->fmt.pix.width + 16;
	fimc->vid_cap.is.mbus_fmt.height = f->fmt.pix.height + 12;
	fimc->vid_cap.is.mbus_fmt.code = V4L2_MBUS_FMT_SGRBG10_1X10;
	fimc->vid_cap.is.mbus_fmt.colorspace = V4L2_COLORSPACE_SRGB;

	mbus_fmt->width = f->fmt.pix.width;
	mbus_fmt->height = f->fmt.pix.height;
	mbus_fmt->code = V4L2_MBUS_FMT_YUYV8_2X8; /*dummy*/
	mbus_fmt->field = f->fmt.pix.field;
	mbus_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	if (fimc->vid_cap.is.sd)
		ret = v4l2_subdev_call(fimc->vid_cap.is.sd, video,
						s_mbus_fmt, mbus_fmt);
	kfree(mbus_fmt);
	return ret;
}

static int fimc_cap_enum_input(struct file *file, void *priv,
				     struct v4l2_input *i)
{
	struct fimc_ctx *ctx = priv;
	struct s5p_platform_fimc *pldata = ctx->fimc_dev->pdata;
	struct s5p_fimc_isp_info *isp_info;

	if (i->index >= FIMC_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	isp_info = pldata->isp_info[i->index];
	if (isp_info == NULL)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;
	strncpy(i->name, isp_info->board_info->type, 32);
	return 0;
}

static int fimc_cap_s_input(struct file *file, void *priv,
				  unsigned int i)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct s5p_platform_fimc *pdata = fimc->pdata;
	int ret;

	if (fimc_capture_active(ctx->fimc_dev))
		return -EBUSY;

	if (i >= FIMC_MAX_CAMIF_CLIENTS || !pdata->isp_info[i]) {
		err("error max client or pdata");
		return -EINVAL;
	}

	if (fimc->vid_cap.mipi_sd) {
		ret = v4l2_subdev_call(fimc->vid_cap.mipi_sd, core, s_power, 0);
		if (ret)
			err("s_power failed: %d", ret);
	}

	if (fimc->vid_cap.sd) {
		ret = v4l2_subdev_call(fimc->vid_cap.sd, core, s_power, 0);
		if (ret)
			err("s_power failed: %d", ret);
	}
	/* FIMC-IS power of */
	if (fimc->vid_cap.is.sd) {
		ret = v4l2_subdev_call(fimc->vid_cap.is.sd, core, s_power, 0);
		if (ret)
			err("s_power failed: %d", ret);
	}

	if (fimc->vid_cap.flite_sd) {
		ret = v4l2_subdev_call(fimc->vid_cap.flite_sd, core, s_power, 0);
		if (ret)
			err("s_power failed: %d", ret);
	}

	dbg("Release the attached sensor subdevice");
	/* Release the attached sensor subdevice. */
	fimc_subdev_unregister(fimc);

	if (!test_and_set_bit(ST_PWR_ON, &fimc->state))
		pm_runtime_get_sync(&fimc->pdev->dev);

	ret = fimc_isp_subdev_init(fimc, i);
	if (ret)
		err("subdev init failed");

	fimc->vid_cap.refcnt++;

	return ret;
}

static int fimc_cap_g_input(struct file *file, void *priv,
				       unsigned int *i)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;

	*i = cap->input_index;
	return 0;
}

static int fimc_cap_streamon(struct file *file, void *priv,
			     enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	if (!(ctx->state & FIMC_DST_FMT)) {
		v4l2_err(&fimc->vid_cap.v4l2_dev, "Format is not set\n");
		return -EINVAL;
	}

	return vb2_streamon(&fimc->vid_cap.vbq, type);
}

static int fimc_cap_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	return vb2_streamoff(&fimc->vid_cap.vbq, type);
}

static int fimc_cap_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;
	struct fimc_frame *frame;
	int ret;

	frame = ctx_get_frame(ctx, reqbufs->type);
	frame->cacheable = ctx->cacheable;
	fimc->vb2->set_cacheable(fimc->alloc_ctx, frame->cacheable);

	ret = vb2_reqbufs(&cap->vbq, reqbufs);
	if (!ret)
		cap->reqbufs_count = reqbufs->count;

	return ret;
}

static int fimc_cap_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;

	return vb2_querybuf(&cap->vbq, buf);
}

static int fimc_cap_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;

	return vb2_qbuf(&cap->vbq, buf);
}

static int fimc_cap_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	return vb2_dqbuf(&ctx->fimc_dev->vid_cap.vbq, buf,
		file->f_flags & O_NONBLOCK);
}

static int fimc_cap_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct fimc_ctx *ctx = priv;
	int ret = -EINVAL;

	/* Allow any controls but 90/270 rotation while streaming */
	if (!fimc_capture_active(ctx->fimc_dev) ||
	    ctrl->id != V4L2_CID_ROTATE ||
	    (ctrl->value != 90 && ctrl->value != 270)) {
		ret = check_ctrl_val(ctx, ctrl);
		if (!ret) {
			ret = fimc_s_ctrl(ctx, ctrl);
			if (!ret)
				ctx->state |= FIMC_PARAMS;
		}
	}
	if (ret == -EINVAL && ctx->fimc_dev->vid_cap.sd)
		ret = v4l2_subdev_call(ctx->fimc_dev->vid_cap.sd,
				       core, s_ctrl, ctrl);
	if (ctx->fimc_dev->vid_cap.is.sd)
		ret = v4l2_subdev_call(ctx->fimc_dev->vid_cap.is.sd,
				       core, s_ctrl, ctrl);
	return ret;
}

static int fimc_cap_cropcap(struct file *file, void *fh,
			    struct v4l2_cropcap *cr)
{
	struct fimc_frame *f;
	struct fimc_ctx *ctx = fh;

	if (cr->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	f = &ctx->s_frame;

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= f->o_width;
	cr->bounds.height	= f->o_height;
	cr->defrect		= cr->bounds;

	return 0;
}

static int fimc_cap_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_frame *f;
	struct fimc_ctx *ctx = file->private_data;

	f = &ctx->s_frame;

	cr->c.left	= f->offs_h;
	cr->c.top	= f->offs_v;
	cr->c.width	= f->width;
	cr->c.height	= f->height;

	return 0;
}

static int fimc_cap_s_crop(struct file *file, void *fh,
			       struct v4l2_crop *cr)
{
	struct fimc_frame *f;
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;
	int ret = -EINVAL;

	if (fimc_capture_active(fimc))
		return -EBUSY;

	ret = fimc_try_crop(ctx, cr);
	if (ret)
		return ret;

	if (!(ctx->state & FIMC_DST_FMT)) {
		v4l2_err(&fimc->vid_cap.v4l2_dev,
			 "Capture color format not set\n");
		return -EINVAL; /* TODO: make sure this is the right value */
	}

	f = &ctx->s_frame;
	/* Check for the pixel scaling ratio when cropping input image. */
	ret = fimc_check_scaler_ratio(ctx, cr->c.width, cr->c.height,
				      ctx->d_frame.width, ctx->d_frame.height,
				      ctx->rotation);
	if (ret) {
		v4l2_err(&fimc->vid_cap.v4l2_dev, "Out of the scaler range");
		return ret;
	}

	f->offs_h = cr->c.left;
	f->offs_v = cr->c.top;
	f->width  = cr->c.width;
	f->height = cr->c.height;

	return 0;
}


static const struct v4l2_ioctl_ops fimc_capture_ioctl_ops = {
	.vidioc_querycap		= fimc_vidioc_querycap_capture,

	.vidioc_enum_fmt_vid_cap_mplane	= fimc_vidioc_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_vidioc_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_cap_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_vidioc_g_fmt_mplane,
	.vidioc_s_fmt_type_private	= fimc_s_fmt_vid_private,

	.vidioc_reqbufs			= fimc_cap_reqbufs,
	.vidioc_querybuf		= fimc_cap_querybuf,

	.vidioc_qbuf			= fimc_cap_qbuf,
	.vidioc_dqbuf			= fimc_cap_dqbuf,

	.vidioc_streamon		= fimc_cap_streamon,
	.vidioc_streamoff		= fimc_cap_streamoff,

	.vidioc_queryctrl		= fimc_vidioc_queryctrl,
	.vidioc_g_ctrl			= fimc_vidioc_g_ctrl,
	.vidioc_s_ctrl			= fimc_cap_s_ctrl,

	.vidioc_g_crop			= fimc_cap_g_crop,
	.vidioc_s_crop			= fimc_cap_s_crop,
	.vidioc_cropcap			= fimc_cap_cropcap,

	.vidioc_enum_input		= fimc_cap_enum_input,
	.vidioc_s_input			= fimc_cap_s_input,
	.vidioc_g_input			= fimc_cap_g_input,
};

/*
 * The End Of Frame notification sent by sensor subdev in its still capture
 * mode. If there is only a single VSYNC generated by the sensor, FIMC does
 * not issue the LastIrq (end of frame) interrupt. And this notification
 *  is used to complete a frame capture and passing it to userspace.
 */
void fimc_v4l2_dev_notify(struct v4l2_subdev *sd, unsigned int notification,
			  void *arg)
{
	struct fimc_vid_cap *cap = container_of(sd->v4l2_dev,
						struct fimc_vid_cap, v4l2_dev);
	struct fimc_dev *fimc = container_of(cap, struct fimc_dev, vid_cap);
	struct fimc_vid_buffer *buf;
	unsigned long flags;

	dbg("bytesused: %d", notification);

	spin_lock_irqsave(&fimc->slock, flags);
	buf = list_entry(cap->active_buf_q.next, struct fimc_vid_buffer, list);

	vb2_set_plane_payload(&buf->vb, 0, notification);

	fimc_capture_irq_handler(fimc);
	fimc_deactivate_capture(fimc);

	spin_unlock_irqrestore(&fimc->slock, flags);
}

/* fimc->lock must be already initialized */
int fimc_register_capture_device(struct fimc_dev *fimc)
{
	struct v4l2_device *v4l2_dev = &fimc->vid_cap.v4l2_dev;
	struct video_device *vfd;
	struct fimc_vid_cap *vid_cap;
	struct fimc_ctx *ctx;
	struct v4l2_format f;
	struct fimc_frame *fr;
	struct vb2_queue *q;
	int ret;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->fimc_dev	 = fimc;
	ctx->in_path	 = FIMC_CAMERA;
	ctx->out_path	 = FIMC_DMA;
	ctx->state	 = FIMC_CTX_CAP;

	/* Default format of the output frames */
	f.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
	fr = &ctx->d_frame;
	fr->fmt = find_format(&f, FMT_FLAGS_M2M);
	fr->width = fr->f_width = fr->o_width = 640;
	fr->height = fr->f_height = fr->o_height = 480;

	if (!v4l2_dev->name[0])
		snprintf(v4l2_dev->name, sizeof(v4l2_dev->name),
			 "%s.capture", dev_name(&fimc->pdev->dev));

	v4l2_dev->notify = fimc_v4l2_dev_notify;

	ret = v4l2_device_register(NULL, v4l2_dev);
	if (ret)
		goto err_info;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(v4l2_dev, "Failed to allocate video device\n");
		goto err_v4l2_reg;
	}

	snprintf(vfd->name, sizeof(vfd->name), "%s:cap",
		 dev_name(&fimc->pdev->dev));

	vfd->fops	= &fimc_capture_fops;
	vfd->ioctl_ops	= &fimc_capture_ioctl_ops;
	vfd->minor	= -1;
	vfd->release	= video_device_release;
#ifdef FOR_DIFF_VER
	vfd->lock	= &fimc->lock;
#endif
	video_set_drvdata(vfd, fimc);

	vid_cap = &fimc->vid_cap;
	vid_cap->vfd = vfd;
	vid_cap->active_buf_cnt = 0;
	vid_cap->reqbufs_count  = 0;
	vid_cap->refcnt = 0;
	vid_cap->sd = NULL;
	vid_cap->fb_sd = NULL;
	vid_cap->mipi_sd = NULL;
	vid_cap->is.sd = NULL;
	vid_cap->is.frame_count = 0;
	vid_cap->is.valid = 0;
	vid_cap->is.bad_mark = 0;
	vid_cap->is.offset_x = 0;
	vid_cap->is.offset_y = 0;

	/* Default color format for 4EA image sensor */
	vid_cap->fmt.code = V4L2_MBUS_FMT_VYUY8_2X8;

	INIT_LIST_HEAD(&vid_cap->pending_buf_q);
	INIT_LIST_HEAD(&vid_cap->active_buf_q);
	spin_lock_init(&ctx->slock);
	vid_cap->ctx = ctx;

	q = &fimc->vid_cap.vbq;
	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = fimc->vid_cap.ctx;
	q->ops = &fimc_capture_qops;
	q->mem_ops = ctx->fimc_dev->vb2->ops;
	q->buf_struct_size = sizeof(struct fimc_vid_buffer);

	vb2_queue_init(q);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret) {
		v4l2_err(v4l2_dev, "Failed to register video device\n");
		goto err_vd_reg;
	}

	v4l2_info(v4l2_dev,
		  "FIMC capture driver registered as /dev/video%d\n",
		  vfd->num);
	dbg("%s : code : %d\n", __func__, fimc->vid_cap.fmt.code);
	return 0;

err_vd_reg:
	video_device_release(vfd);
err_v4l2_reg:
	v4l2_device_unregister(v4l2_dev);
err_info:
	kfree(ctx);
	dev_err(&fimc->pdev->dev, "failed to install\n");
	return ret;
}

void fimc_unregister_capture_device(struct fimc_dev *fimc)
{
	struct fimc_vid_cap *capture = &fimc->vid_cap;

	if (capture->vfd)
		video_unregister_device(capture->vfd);

	kfree(capture->ctx);
}
